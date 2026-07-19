//
//  duke_audio.c — bare-metal audio backend for the Duke3D port (M4).
//
//  Implements audiolib's DSL_* backend API (the SDL_mixer replacement) over
//  the pico_shared dual-sink audio path:
//
//    multivoc (MV_ServiceVoc)  ->  MV_MixBuffer divisions (64 S16-stereo
//    frames each)              ->  3 ms timer-IRQ pump  ->  ACTIVE sink:
//        HDMI  : hstx_push_audio_sample() -> data-island queue (core1 scanout)
//        jack  : audio_i2s_enqueue_sample() -> I2S DMA -> TLV320 headphones
//
//  Routing policy (project spec, same as fruitjam-doom):
//   - default sink is HDMI; the internal speaker NEVER plays.
//   - headphone plug  -> samples go to I2S only; HDMI queue drains to
//     auto-inserted silence islands (= muted).
//   - headphone unplug -> samples return to HDMI; I2S is fed zeros to keep
//     BCLK/DAC-PLL alive (prevents pops); speaker re-muted.
//
//  DisableInterrupts/RestoreInterrupts (no-ops in the SDL build's dsl.c) are
//  implemented as real IRQ save/disable here: they are what protects the
//  multivoc voice list from the timer-IRQ pump on core0.
//
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "pico/time.h"
#include "pico/multicore.h"
#include "hardware/sync.h"

#include "audio_i2s.h"                // pico_shared I2S + TLV320
#include "tlv320dac3100.h"
#include "hstx_packet.h"              // pico_hdmi data-island audio
#include "hstx_data_island_queue.h"

#include "../Game/audiolib/dsl.h"

#ifndef HSTX_AUDIO_DI_HIGH_WATERMARK
#define HSTX_AUDIO_DI_HIGH_WATERMARK 200
#endif

#define DUKE_AUDIO_RATE 48000   // HDMI exact-lock rate (N=6144/CTS=25200);
                                // multivoc reads it back via DSL_GetPlaybackRate

// audiolib critical-section guards, implemented further down as a recursive
// cross-core spinlock (used by the pump before their definition).
uint32_t DisableInterrupts(void);
void     RestoreInterrupts(uint32_t flags);

// ---------------------------------------------------------------------------
// Sink state
// ---------------------------------------------------------------------------
typedef enum { SINK_HDMI = 0, SINK_HEADPHONES = 1 } sink_t;
static volatile sink_t s_sink = SINK_HDMI;
static bool s_i2s_ok = false;

// ---------------------------------------------------------------------------
// Mixer hookup (set by DSL_BeginBufferedPlayback)
// ---------------------------------------------------------------------------
extern volatile int MV_MixPage;

static void (*s_callback)(void) = 0;
static volatile char *s_buffer = 0;
static int s_divsize = 0;             // bytes per division
static int s_mixmode = 0;
static volatile bool s_playing = false;

// ---------------------------------------------------------------------------
// HDMI audio push — fruitjam-doom's pico_hdmi_glue pattern: accumulate 4
// samples per data island, encode with IEC 60958 channel status, drop above
// the high watermark so we never block.
// ---------------------------------------------------------------------------
static void __not_in_flash_func(hstx_push_audio_sample)(int left, int right)
{
    static int frame_counter = 0;
    static audio_sample_t acc_buf[4];
    static int acc_count = 0;
    acc_buf[acc_count].left = (int16_t)left;
    acc_buf[acc_count].right = (int16_t)right;
    acc_count++;
    if (acc_count == 4) {
        if (hstx_di_queue_get_level() >= HSTX_AUDIO_DI_HIGH_WATERMARK) {
            acc_count = 0;
            return;
        }
        hstx_packet_t packet;
        frame_counter = hstx_packet_set_audio_samples_cs(&packet, acc_buf, 4, frame_counter);
        hstx_data_island_t island;
        hstx_encode_data_island(&island, &packet, false, true);
        (void)hstx_di_queue_push(&island);
        acc_count = 0;
    }
}

// ---------------------------------------------------------------------------
// The pump: mix divisions on demand and fan out to the active sink.
// Runs in timer-IRQ context on core0 (~3 ms cadence).
// ---------------------------------------------------------------------------
static void __not_in_flash_func(push_division_to_sink)(const uint8_t *div, sink_t sink)
{
    // Decode per MixMode; Duke defaults to STEREO|SIXTEEN_BIT (64 frames).
    if (s_mixmode & SIXTEEN_BIT) {
        const int16_t *s = (const int16_t *)div;
        int frames = s_divsize / ((s_mixmode & STEREO) ? 4 : 2);
        for (int i = 0; i < frames; i++) {
            int16_t l, r;
            if (s_mixmode & STEREO) { l = s[2 * i]; r = s[2 * i + 1]; }
            else                    { l = r = s[i]; }
            if (sink == SINK_HEADPHONES)
                audio_i2s_enqueue_sample(((uint32_t)(uint16_t)l << 16) | (uint16_t)r);
            else
                hstx_push_audio_sample(l, r);
        }
    } else {
        const uint8_t *s = div;      // unsigned 8-bit
        int frames = s_divsize / ((s_mixmode & STEREO) ? 2 : 1);
        for (int i = 0; i < frames; i++) {
            int16_t l, r;
            if (s_mixmode & STEREO) {
                l = (int16_t)((s[2 * i] - 128) << 8);
                r = (int16_t)((s[2 * i + 1] - 128) << 8);
            } else {
                l = r = (int16_t)((s[i] - 128) << 8);
            }
            if (sink == SINK_HEADPHONES)
                audio_i2s_enqueue_sample(((uint32_t)(uint16_t)l << 16) | (uint16_t)r);
            else
                hstx_push_audio_sample(l, r);
        }
    }
}

// ---------------------------------------------------------------------------
// The pump runs as core1's background task (video_output_set_background_task):
// core1 idles between scanout IRQs, so audio is immune to core0's frame rate
// (the frame-paced pump starved 10-80 ms gaps into the music whenever the
// game dropped below ~45 fps — WAV-analysis-proven). Mixer state shared with
// core0 (FX_Play voice ops, MUSIC_* calls) is protected by the recursive
// cross-core spinlock below, taken per mixed division so core0 never waits
// more than ~100 us.
// ---------------------------------------------------------------------------
static volatile bool s_pumping = false;   // pump body active (for teardown sync)

static void __not_in_flash_func(pump_once)(void)
{
    if (!s_playing) return;

    const sink_t sink = s_sink;
    const int frames_per_div = s_divsize / 4;   // S16 stereo (worst case)

    // Mix as many divisions as needed to restore ~43 ms of queued audio in
    // the active sink, bounded by a runaway guard. The frame pump may not run
    // again for 30+ ms in heavy scenes, so each call must be able to refill
    // from empty (the old cap of 8 divisions = 10.6 ms starved the sinks into
    // audible crackle even at 60 fps). The 43 ms target keeps latency
    // moderate while bridging frame gaps; the rings are larger (64/85 ms) so
    // pushes never drop.
    #define PUMP_TARGET_I2S_QUEUED   2048   // samples (~43 ms of 4096 ring)
    #define PUMP_TARGET_DI_LEVEL      512   // islands (~43 ms of 768 ring)
    for (int guard = 0; guard < 64; guard++) {
        if (sink == SINK_HEADPHONES) {
            if (!s_i2s_ok) break;
            int queued = I2S_AUDIO_RING_SIZE - audio_i2s_get_freebuffer_size();
            if (queued >= PUMP_TARGET_I2S_QUEUED ||
                audio_i2s_get_freebuffer_size() < frames_per_div)
                break;
        } else {
            if (hstx_di_queue_get_level() >= PUMP_TARGET_DI_LEVEL)
                break;
        }
        // Mix one division + advance the MIDI sequencer under the audio lock
        // (excludes core0's FX_Play/MUSIC_* mutations); push it lock-free.
        uint32_t lk = DisableInterrupts();
        s_callback();   // MV_ServiceVoc: mixes next division, advances MV_MixPage
        if ((s_mixmode & (STEREO | SIXTEEN_BIT)) == (STEREO | SIXTEEN_BIT)) {
            extern void duke_music_mix(int16_t *stereo, int frames);
            duke_music_mix((int16_t *)&s_buffer[MV_MixPage * s_divsize], frames_per_div);
        }
        RestoreInterrupts(lk);
        push_division_to_sink((const uint8_t *)&s_buffer[MV_MixPage * s_divsize], sink);
    }

    // Keep the I2S clock chain alive with silence while HDMI is active.
    if (sink == SINK_HDMI && s_i2s_ok) {
        int free = audio_i2s_get_freebuffer_size();
        while (free-- > 0) audio_i2s_enqueue_sample(0);
    }
}

// core1 background task (registered via video_output_set_background_task in
// pico_display's _platform_init; the driver calls it in core1's idle loop).
void __not_in_flash_func(duke_audio_core1_task)(void)
{
    if (!s_playing) return;
    s_pumping = true;
    pump_once();
    s_pumping = false;
}

// Legacy frame-context entry — now a no-op (core1 owns the pump); kept so
// existing call sites need no change.
void duke_audio_pump(void) { }

// ---------------------------------------------------------------------------
// Headphone jack — poll from frame context (I2C traffic; NOT the pump IRQ).
// ---------------------------------------------------------------------------
void duke_audio_poll_headphone(void)
{
    if (!s_i2s_ok) return;
    enum headphone_toggle_t ev = tlv320_poll_headphone();
    if (ev == HP_TOGGLE_CONNECT) {
        s_sink = SINK_HEADPHONES;
        printf("audio: headphones CONNECT -> I2S (HDMI muted)\n");
    } else if (ev == HP_TOGGLE_DISCONNECT) {
        s_sink = SINK_HDMI;
        audio_i2s_muteInternalSpeaker(true);   // spec: speaker never plays
        printf("audio: headphones DISCONNECT -> HDMI\n");
    }
}

// ---------------------------------------------------------------------------
// DSL_* — audiolib backend API (replaces dsl.c)
// ---------------------------------------------------------------------------
char *DSL_ErrorString(int ErrorNumber)
{
    (void)ErrorNumber;
    return "pico dual-sink audio driver";
}

int DSL_Init(void)
{
    // TLV320 + I2S bring-up (i2c, codec regs incl. headset detect, PIO1 SM,
    // DMA chan 6 -> DMA_IRQ_1, leaving DMA_IRQ_0 for HSTX).
    audio_i2s_hw_t *i2s = audio_i2s_setup(PICO_AUDIO_I2S_DRIVER_TLV320,
                                          DUKE_AUDIO_RATE, /*dmachan=*/6);
    s_i2s_ok = (i2s != NULL);
    if (s_i2s_ok) {
        audio_i2s_muteInternalSpeaker(true);
        audio_i2s_setVolume(14);
    } else {
        printf("audio: I2S/TLV320 setup FAILED — HDMI-only, no jack detect\n");
    }
    printf("audio: DSL_Init ok (%d Hz, sink=HDMI)\n", DUKE_AUDIO_RATE);
    return DSL_Ok;
}

int DSL_BeginBufferedPlayback(char *BufferStart, int BufferSize, int NumDivisions,
                              unsigned SampleRate, int MixMode,
                              void (*CallBackFunc)(void))
{
    (void)SampleRate;   // we always run the sinks at DUKE_AUDIO_RATE; multivoc
                        // adopts it via DSL_GetPlaybackRate() after this call.
    if (s_playing) return DSL_Error;

    s_callback = CallBackFunc;
    s_buffer   = BufferStart;
    s_divsize  = BufferSize / NumDivisions;
    s_mixmode  = MixMode;
    s_playing  = true;   // core1 background task starts pumping from here on

    printf("audio: playback started (div=%dB x%d, mode=%s%s, pump=core1)\n",
           s_divsize, NumDivisions,
           (MixMode & STEREO) ? "stereo" : "mono",
           (MixMode & SIXTEEN_BIT) ? "16" : "8");
    return DSL_Ok;
}

void DSL_StopPlayback(void)
{
    if (s_playing) {
        s_playing = false;
        // Wait out an in-flight core1 pump pass before the caller may free
        // mixer state (MV_Shutdown frees MV_Voices right after this).
        while (s_pumping) tight_loop_contents();
    }
}

unsigned DSL_GetPlaybackRate(void)
{
    return DUKE_AUDIO_RATE;
}

void DSL_Shutdown(void)
{
    DSL_StopPlayback();
}

// ---------------------------------------------------------------------------
// audiolib critical sections. Originally DOS cli/sti; with the pump on core1
// these become a RECURSIVE CROSS-CORE SPINLOCK protecting the mixer/sequencer
// state between core0 (FX_Play*/MUSIC_* calls from the game) and core1 (the
// pump). Deliberately does NOT disable interrupts on either core: core1's
// scanline ISR must never be blocked (video), and core0 spinning with IRQs
// live keeps PIO-USB healthy. Hold times are one mixed division (~100 us).
// ---------------------------------------------------------------------------
static spin_lock_t *s_alock;
static volatile int s_alock_owner = -1;
static int s_alock_depth;

static void alock_ensure(void)
{
    if (!s_alock) s_alock = spin_lock_init(spin_lock_claim_unused(true));
}

uint32_t DisableInterrupts(void)
{
    alock_ensure();
    int core = (int)get_core_num();
    if (s_alock_owner != core) {
        spin_lock_unsafe_blocking(s_alock);
        s_alock_owner = core;
    }
    s_alock_depth++;
    return 0;
}

void RestoreInterrupts(uint32_t flags)
{
    (void)flags;
    if (s_alock_depth > 0 && --s_alock_depth == 0) {
        s_alock_owner = -1;
        spin_unlock_unsafe(s_alock);
    }
}
