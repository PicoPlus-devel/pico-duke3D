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
//  Routing policy depends on the board (DUKE_AUDIO_EXCLUSIVE_SINK below).
//
//  With a TLV320 codec and its headphone-detect IRQ (Fruit Jam) the sinks are
//  EXCLUSIVE — the project spec, same as fruitjam-doom:
//   - default sink is HDMI; the internal speaker NEVER plays.
//   - headphone plug  -> samples go to I2S only; HDMI queue drains to
//     auto-inserted silence islands (= muted).
//   - headphone unplug -> samples return to HDMI; I2S is fed zeros to keep
//     BCLK/DAC-PLL alive (prevents pops); speaker re-muted.
//
//  On a board with a bare DAC and no jack detect (Murmulator M2's PCM5100A)
//  there is nothing to switch on, so both sinks get the real samples and the
//  user picks with their cables.
//
//  A board with no I2S DAC at all (Olimex RP2040-PICO-PC) plays over HDMI and
//  its PWM audio jack: every HDMI sample is also handed to pwm_audio, which
//  therefore runs at the HDMI pace.
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
#include "duke_leds.h"                // duke_vu_add_chunk (stubs out with no strip)
#include "pwm_audio.h"                // pico_shared PWM audio jack (no-op stubs without one)

#include "../Game/audiolib/dsl.h"

#ifndef HSTX_AUDIO_DI_HIGH_WATERMARK
#define HSTX_AUDIO_DI_HIGH_WATERMARK 200
#endif

#define DUKE_AUDIO_RATE 48000   // HDMI exact-lock rate (N=6144/CTS=25200);
                                // multivoc reads it back via DSL_GetPlaybackRate

// I2S back end, from the board's cflags header (audio_i2s.h defines the ids).
#ifndef DUKE_AUDIO_I2S_DRIVER
#define DUKE_AUDIO_I2S_DRIVER PICO_AUDIO_I2S_DRIVER_TLV320
#endif

// Driver 0 (PICO_AUDIO_I2S_DRIVER_NONE): no DAC to set up. s_i2s_ok then stays
// false, which already keeps every I2S path below quiet.
#define DUKE_AUDIO_HAS_I2S (DUKE_AUDIO_I2S_DRIVER != PICO_AUDIO_I2S_DRIVER_NONE)

// Can this board tell where the listener is? Only a TLV320 with its
// headset-detect interrupt wired can, and only then is it right to mute one
// sink: everywhere else muting HDMI would silence the board's only output.
#if DUKE_AUDIO_I2S_DRIVER == PICO_AUDIO_I2S_DRIVER_TLV320 && PICO_AUDIO_I2S_INTERRUPT_PIN != -1
#define DUKE_AUDIO_EXCLUSIVE_SINK 1
#else
#define DUKE_AUDIO_EXCLUSIVE_SINK 0
#endif

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
    pwm_audio_push(left, right);  // the PWM audio jack plays the same samples (no-op without one)
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
// One mixed frame out to whichever sinks this board drives. On an exclusive-
// sink board that is the one the jack state selected; otherwise both, since
// there is no way to know which cable the listener is using.
static inline void __not_in_flash_func(emit_sample)(int16_t l, int16_t r, sink_t sink)
{
#if DUKE_AUDIO_EXCLUSIVE_SINK
    if (sink == SINK_HEADPHONES)
        audio_i2s_enqueue_sample(((uint32_t)(uint16_t)l << 16) | (uint16_t)r);
    else
        hstx_push_audio_sample(l, r);
#else
    (void)sink;
    if (s_i2s_ok)
        audio_i2s_enqueue_sample(((uint32_t)(uint16_t)l << 16) | (uint16_t)r);
    hstx_push_audio_sample(l, r);
#endif
}

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
            emit_sample(l, r, sink);
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
            emit_sample(l, r, sink);
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
// How much queued audio the pump aims to keep in the active sink (~43 ms).
// The rings are larger (64 ms DI / 85 ms I2S) so pushes never drop.
#define PUMP_TARGET_I2S_QUEUED   2048   // samples of the 4096-sample I2S ring
#define PUMP_TARGET_DI_LEVEL      512   // islands of the 768-island HDMI ring

static volatile bool s_pumping = false;   // pump body active (for teardown sync)

// Has the sink reached its queue target (or run out of room for one more
// division)? On a dual-sink board both rings are fed from the same loop, so
// EITHER of them being full has to stop it — pushing into a full ring just
// drops samples. The two rings are clocked from different domains (I2S BCLK
// off clk_sys via PIO, HDMI data islands off clk_hstx), so they drift apart by
// a few ppm and the slower consumer ends up pacing the mixer. That is inherent
// to driving two independent clocks from one producer and is what fruitjam-doom
// ships on these boards too.
static inline bool __not_in_flash_func(sink_at_target)(sink_t sink, int frames_per_div)
{
#if DUKE_AUDIO_EXCLUSIVE_SINK
    if (sink == SINK_HEADPHONES) {
        if (!s_i2s_ok) return true;
        const int free = audio_i2s_get_freebuffer_size();
        return (I2S_AUDIO_RING_SIZE - free) >= PUMP_TARGET_I2S_QUEUED ||
               free < frames_per_div;
    }
    return hstx_di_queue_get_level() >= PUMP_TARGET_DI_LEVEL;
#else
    (void)sink;
    if (hstx_di_queue_get_level() >= PUMP_TARGET_DI_LEVEL)
        return true;
    if (s_i2s_ok) {
        const int free = audio_i2s_get_freebuffer_size();
        if ((I2S_AUDIO_RING_SIZE - free) >= PUMP_TARGET_I2S_QUEUED ||
            free < frames_per_div)
            return true;
    }
    return false;
#endif
}

// Did the pump arrive with a sink already near-empty? Counted, not corrected —
// the 1 Hz report in duke_pico_idle uses it to tell CPU starvation apart from
// everything else that can make audio sound wrong.
static inline bool __not_in_flash_func(sink_starved)(sink_t sink)
{
    const bool i2s_low = s_i2s_ok &&
        (I2S_AUDIO_RING_SIZE - audio_i2s_get_freebuffer_size()) < (PUMP_TARGET_I2S_QUEUED / 4);
    const bool di_low = hstx_di_queue_get_level() < (PUMP_TARGET_DI_LEVEL / 4);
#if DUKE_AUDIO_EXCLUSIVE_SINK
    return (sink == SINK_HEADPHONES) ? i2s_low : di_low;
#else
    (void)sink;
    return i2s_low || di_low;
#endif
}

// Serializes the two pumpers (core1 background task + core0 safety net) so
// divisions are mixed and pushed in order. Deliberately SEPARATE from the
// audiolib critical-section lock: core0's frequent FX_*/MV_* calls take only
// that one, so they never wait behind a sink push (the HDMI path BCH/TERC4
// encodes 16 data islands per division — holding the audiolib lock across that
// blocked the game loop and cost most of the frame rate). Lock order is always
// pump lock -> audiolib lock, never the reverse, so there is no deadlock.
static spin_lock_t *s_pump_lock;
static uint32_t s_starve_count;           // pump arrived with the sink near-empty

// Production-rate instrumentation. The whole in-level audio hunt kept stalling
// on not knowing WHICH of two very different failures we had: core1 unable to
// mix fast enough, or core1 mixing fine but hardly ever being called. These
// separate them directly.
//   bg = duke_audio_core1_task() entries/s. The driver calls it from core1's
//        tight loop, so this should be in the thousands. If it is tens, each
//        pump_once() is taking milliseconds and the loop is the problem.
//   dv = divisions actually mixed/s. This is the production rate, and it has a
//        known required value: 48000 / frames_per_div (= 750/s at 64 frames).
//        Below that, the sink MUST drain no matter how big the ring is.
static uint32_t s_bg_calls, s_divs_mixed;

// Per-stage timing, in microseconds accumulated per report interval. dv=122/s
// against need=187/s means one 256-frame division costs ~8 ms to produce
// in-level, versus comfortably under 5 ms in the menu. These four counters
// partition that 8 ms so the next change targets the actual consumer:
//   lock  = core1 blocked acquiring the audiolib spinlock, i.e. core0 holding
//           it across FX_*/MV_* calls (suspicion: an uncached sound lump being
//           pulled off SD through cache1d while the lock is held).
//   svc   = MV_ServiceVoc: mixing all active SFX voices. In-level there are
//           many voices and every voice's VOC data lives in PSRAM, read through
//           the XIP cache core0 is thrashing with texture fetches.
//   music = duke_music_mix: OPL render + 49716->48000 resample.
//   push  = push_division_to_sink: BCH/TERC4 encode of 64 data islands.
static uint32_t s_us_lock, s_us_svc, s_us_music, s_us_push;

// Timing the stages costs six time_us_32() reads per division (~1100/s), which
// is immaterial, but it is pure diagnostics — compile it out of the ship build.
#if DUKE_VIDEO_DIAG
#define STAGE_T(v)          uint32_t v = time_us_32()
#define STAGE_ADD(acc,b,a)  (acc) += (b) - (a)
#else
#define STAGE_T(v)          ((void)0)
#define STAGE_ADD(acc,b,a)  ((void)0)
#endif

void duke_audio_take_rate_stats(uint32_t *bg, uint32_t *dv, uint32_t *fpd)
{
    *bg = s_bg_calls;    s_bg_calls = 0;
    *dv = s_divs_mixed;  s_divs_mixed = 0;
    *fpd = (uint32_t)(s_divsize / 4);
}

// Returns per-stage microseconds since the last call (read-and-clear).
void duke_audio_take_stage_us(uint32_t *lock, uint32_t *svc, uint32_t *music, uint32_t *push)
{
    *lock  = s_us_lock;  s_us_lock  = 0;
    *svc   = s_us_svc;   s_us_svc   = 0;
    *music = s_us_music; s_us_music = 0;
    *push  = s_us_push;  s_us_push  = 0;
}

// Read-and-clear (called ~1 Hz from core0's idle hook).
uint32_t duke_audio_take_starve_count(void)
{
    uint32_t c = s_starve_count;
    s_starve_count = 0;
    return c;
}

// ---------------------------------------------------------------------------
// Deferred sound-completion callbacks.
//
// multivoc invokes the game's FX callback (Duke's TestCallBack) when a voice
// finishes, and that callback WRITES game state (sprite[], sector[],
// hittype[], SoundOwner[]). The mixer runs from the pump on either core and
// from inside the engine's level-load/art-cache loops, where those arrays are
// mid-update — calling it there indexes them with garbage and corrupts PSRAM.
// So multivoc queues here instead (see MV_INVOKE_CALLBACK in multivoc.c) and
// duke_audio_run_deferred_callbacks() dispatches from a frame boundary on
// core0.
// ---------------------------------------------------------------------------
#define CB_QUEUE_SIZE 64
typedef struct { void (*fn)(unsigned long); unsigned long val; } cb_entry_t;
static cb_entry_t s_cbq[CB_QUEUE_SIZE];
static volatile uint32_t s_cbq_head, s_cbq_tail;
static uint32_t s_cbq_dropped;

void duke_audio_defer_callback(void (*fn)(unsigned long), unsigned long val)
{
    if (!fn) return;
    uint32_t next = (s_cbq_head + 1) % CB_QUEUE_SIZE;
    if (next == s_cbq_tail) { s_cbq_dropped++; return; }   // full: drop
    s_cbq[s_cbq_head].fn = fn;
    s_cbq[s_cbq_head].val = val;
    s_cbq_head = next;
}

// Call ONLY from core0 with the engine in a consistent state (frame boundary).
void duke_audio_run_deferred_callbacks(void)
{
    while (s_cbq_tail != s_cbq_head) {
        cb_entry_t e = s_cbq[s_cbq_tail];
        s_cbq_tail = (s_cbq_tail + 1) % CB_QUEUE_SIZE;
        if (e.fn) e.fn(e.val);
    }
}

static void __not_in_flash_func(pump_once)(int max_divisions)
{
    if (!s_playing) return;

    if (!s_pump_lock) s_pump_lock = spin_lock_init(spin_lock_claim_unused(true));
    spin_lock_unsafe_blocking(s_pump_lock);

    const sink_t sink = s_sink;
    const int frames_per_div = s_divsize / 4;   // S16 stereo (worst case)

    // Mix as many divisions as needed to restore ~43 ms of queued audio in
    // the active sink, bounded by a runaway guard. The frame pump may not run
    // again for 30+ ms in heavy scenes, so each call must be able to refill
    // from empty (the old cap of 8 divisions = 10.6 ms starved the sinks into
    // audible crackle even at 60 fps). The 43 ms target keeps latency
    // moderate while bridging frame gaps; the rings are larger (64/85 ms) so
    // pushes never drop.
    // Starvation detector: if the active sink has fallen to a small fraction of
    // its target when we arrive, the pump is not keeping up — that is audible as
    // music running slow/gappy and chopped SFX. Counted, not fixed, here: the
    // 1 Hz report in duke_pico_idle tells us whether a "still sounds wrong"
    // complaint is CPU starvation or something else entirely.
    if (sink_starved(sink))
        s_starve_count++;

    for (int guard = 0; guard < max_divisions; guard++) {
        if (sink_at_target(sink, frames_per_div))
            break;
        // Mix under the AUDIOLIB lock (shared with core0's FX_*/MV_* calls) —
        // keep this section as short as possible.
        s_divs_mixed++;
        STAGE_T(t0);
        uint32_t lk = DisableInterrupts();
        STAGE_T(t1);
        s_callback();   // MV_ServiceVoc: mixes next division, advances MV_MixPage
        STAGE_T(t2);
        uint8_t *div = (uint8_t *)&s_buffer[MV_MixPage * s_divsize];
        const bool s16_stereo =
            (s_mixmode & (STEREO | SIXTEEN_BIT)) == (STEREO | SIXTEEN_BIT);
        if (s16_stereo) {
            extern void duke_music_mix(int16_t *stereo, int frames);
            duke_music_mix((int16_t *)div, frames_per_div);
        }
        STAGE_T(t3);
        RestoreInterrupts(lk);
        STAGE_ADD(s_us_lock,  t1, t0);
        STAGE_ADD(s_us_svc,   t2, t1);
        STAGE_ADD(s_us_music, t3, t2);

        // VU meter tap: one pass over the left channel of the division we are
        // about to hand to the sink. After the music mix so the meter shows what
        // is actually heard, outside the audiolib lock so it cannot lengthen the
        // critical section, and before the push so it is written once rather than
        // once per sink. Compiles to nothing on a board without a strip.
        if (s16_stereo)
            duke_vu_add_chunk((const int16_t *)div, (unsigned)frames_per_div);

        // Push OUTSIDE the audiolib lock. The pump lock still guarantees only
        // one pumper is here, so the sink helpers' static state is safe.
        STAGE_T(t4);
        push_division_to_sink(div, sink);
#if DUKE_VIDEO_DIAG
        s_us_push += time_us_32() - t4;
#endif
    }

#if DUKE_AUDIO_EXCLUSIVE_SINK
    // Keep the I2S clock chain alive with silence while HDMI is active, so the
    // DAC's PLL stays locked and unplugging headphones doesn't pop.
    //
    // Only correct when the sinks are exclusive. On a dual-sink board this loop
    // would top the ring up with zeros AHEAD of the real samples the next pass
    // pushes, i.e. it would silence the DAC completely.
    if (sink == SINK_HDMI && s_i2s_ok) {
        int free = audio_i2s_get_freebuffer_size();
        while (free-- > 0) audio_i2s_enqueue_sample(0);
    }
#endif

    spin_unlock_unsafe(s_pump_lock);
}

// core1 background task (registered via video_output_set_background_task in
// pico_display's _platform_init; the driver calls it in core1's idle loop).
void __not_in_flash_func(duke_audio_core1_task)(void)
{
    if (!s_playing) return;
    s_bg_calls++;
    s_pumping = true;
    pump_once(64);          // core1: fill to target
    s_pumping = false;
}

// core0 entry, called from the engine's faketimerhandler/idle hook (which fires
// from all over the render loops, not just once per frame).
//
// The pump is fed from BOTH cores on purpose. Core1 alone is not enough: it
// also services the HSTX scanline ISR, and while core0 streams tiles from
// PSRAM the shared QMI bus stalls core1's code fetches — starving the pump,
// which in this design makes music play SLOW (the MIDI clock advances with
// rendered samples) and chops SFX in the same stream. Whichever core has slack
// fills the rings; the queue targets make the other one early-out cheaply.
// Both paths hold the recursive cross-core audiolib spinlock while mixing.
void duke_audio_pump(void)
{
    // DELIBERATELY A NO-OP.
    //
    // core1's background task runs in a tight loop and keeps the sink at its
    // target on its own — measured on hardware: di_lvl 500-535 against a target
    // of 512, with the underrun counter frozen. So mixing here bought nothing
    // and cost everything: this hook is called from the engine's render INNER
    // LOOPS, and in-level the game frame rate collapsed to 4-15/s with it
    // enabled (the menu, which barely calls this, stayed at ~50/s).
    //
    // Kept as a symbol so the existing call sites (faketimerhandler ->
    // duke_pico_idle, sampletimer) need no edits, and so this note survives.
}

// ---------------------------------------------------------------------------
// Headphone jack — poll from frame context (I2C traffic; NOT the pump IRQ).
// ---------------------------------------------------------------------------
void duke_audio_poll_headphone(void)
{
#if !DUKE_AUDIO_EXCLUSIVE_SINK
    // No codec / no detect pin: both sinks are always live, nothing to switch.
    // (tlv320_poll_headphone would short-circuit on !s_active anyway; skipping
    // the call keeps the frame loop honest about what this board can do.)
    return;
#else
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
#endif
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
#if DUKE_AUDIO_HAS_I2S
    // DAC + I2S bring-up (for a TLV320: i2c, codec regs incl. headset detect;
    // for a bare PCM5100A just the PIO SM). DMA chan 6 -> DMA_IRQ_1, leaving
    // DMA_IRQ_0 for HSTX. The driver id comes from the board's cflags header.
    audio_i2s_hw_t *i2s = audio_i2s_setup(DUKE_AUDIO_I2S_DRIVER,
                                          DUKE_AUDIO_RATE, /*dmachan=*/6);
    s_i2s_ok = (i2s != NULL);
    if (s_i2s_ok) {
        // Both are no-ops on a board without a TLV320 (every tlv320_* entry
        // point short-circuits on !s_active), so this needs no board guard.
        audio_i2s_muteInternalSpeaker(true);
        audio_i2s_setVolume(14);
    } else {
        printf("audio: I2S setup FAILED (driver %d) — HDMI only\n",
               DUKE_AUDIO_I2S_DRIVER);
    }
#endif
    // PWM audio jack (Olimex RP2040-PICO-PC), a no-op on boards without one.
    // On core0, after the clocks are final: the PWM wrap is derived from
    // clk_sys and its interrupt runs on the core that calls this, while the
    // samples arrive from the core1 pump.
    pwm_audio_init(DUKE_AUDIO_RATE);
    printf("audio: DSL_Init ok (%d Hz, sinks=%s)\n", DUKE_AUDIO_RATE,
           DUKE_AUDIO_EXCLUSIVE_SINK ? "HDMI or headphones (jack detect)"
           : DUKE_AUDIO_HAS_I2S      ? "HDMI + I2S"
           : PWM_AUDIO_IS_ENABLED    ? "HDMI + PWM jack"
                                     : "HDMI");
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
