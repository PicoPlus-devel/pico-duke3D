//
//  duke_music.c — MIDI music for the Duke3D port (M5).
//
//  Revives the DOS audiolib music chain over an OPL2 emulator:
//
//    game (sounds.c playmusic)  ->  PlayMusic(): load .MID from the GRP into
//    MusicPtr  ->  MUSIC_* API (implemented here, replacing DOS music.c and
//    its sound-card matrix)  ->  midi.c sequencer (vendored, unmodified)  ->
//    al_midi.c AdLib FM driver (vendored; its outp()/inp() port I/O resolves
//    to the shims below)  ->  emu8950 (Okazaki/Sanderson OPL emulator, MIT,
//    vendored from fruitjam-doom)  ->  duke_music_mix() adds rendered FM into
//    each SFX division in the audio pump (duke_audio.c).
//
//  Sequencer timing: DOS ran _MIDI_ServiceRoutine from a task_man timer task;
//  here a minimal task shim is paced by consumed samples (duke_music_mix
//  advances tasks by the frames it renders). All of this runs in the pump's
//  thread context (core0) — same thread that calls MUSIC_PlaySong, so no
//  locking is needed.
//
//  Note: emu8950 is built with EMU8950_NO_RATECONV (fruitjam-doom precedent):
//  it renders at the OPL's native 49716 Hz while we play at 48000 Hz — music
//  is ~3.45% flat, inaudible for game music (same trade shipped in
//  rp2350-doom v0.1.2). Revisit in the audio-polish pass if wanted.
//
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "emu8950.h"

#include "../Game/audiolib/music.h"
#include "../Game/audiolib/midi.h"
#include "../Game/audiolib/al_midi.h"
#include "../Game/audiolib/task_man.h"
#include "../Game/audiolib/sndcards.h"

// From the game (global.c / filesystem).
extern uint8_t MusicPtr[72000];
extern int32_t kopen4load(const char *filename, int openOnlyFromGRP);
extern int32_t kread(int32_t handle, void *buffer, int32_t leng);
extern int32_t kfilelength(int32_t handle);
extern void    kclose(int32_t handle);

// The OPL2's own tick rate (3579545/72). With EMU8950_NO_RATECONV the emulator
// emits exactly one sample per internal tick, so this — NOT the sink rate — is
// the rate its samples exist at, and the rate the sequencer must be paced in.
#define DUKE_OPL_NATIVE_RATE 49716
// Our output/HDMI rate (exact-locked to the 25.2 MHz pixel clock).
#define DUKE_SINK_RATE       48000

static OPL *s_opl = 0;
static int  s_music_up = 0;
static int  s_volume = 255;

// ---------------------------------------------------------------------------
// OPL2 port I/O shims — al_midi.c's outp()/inp() land here. Port 0x388 is the
// address latch, 0x389 the data write (classic AdLib). inp() is only used for
// bus-settle delays and AL_DetectFM (which we never call): return 0.
// ---------------------------------------------------------------------------
static uint8_t s_opl_addr;

void outp(int port, int value)
{
    if ((port & 1) == 0) {
        s_opl_addr = (uint8_t)value;
    } else if (s_opl) {
        OPL_writeReg(s_opl, s_opl_addr, (uint8_t)value);
    }
}

int inp(int port)
{
    (void)port;
    return 0;
}

// ---------------------------------------------------------------------------
// task_man shim — midi.c schedules _MIDI_ServiceRoutine (and changes its rate
// with tempo). Tasks fire from duke_music_tasks_advance(), paced by rendered
// samples. Small fixed pool; midi.c uses exactly one task.
// ---------------------------------------------------------------------------
#define MAX_TASKS 4
static task     s_tasks[MAX_TASKS];
static uint32_t s_task_accum[MAX_TASKS];

task *TS_ScheduleTask(void (*function)(task *), int rate, int priority, void *data)
{
    for (int i = 0; i < MAX_TASKS; i++) {
        if (!s_tasks[i].active) {
            s_tasks[i].TaskService = function;
            s_tasks[i].data = data;
            s_tasks[i].rate = rate > 0 ? rate : 1;
            s_tasks[i].priority = priority;
            s_tasks[i].active = 1;
            s_task_accum[i] = 0;
            return &s_tasks[i];
        }
    }
    return 0;
}

int TS_Terminate(task *ptr)
{
    if (ptr) ptr->active = 0;
    return TASK_Ok;
}

void TS_SetTaskRate(task *ptr, int rate)
{
    if (ptr) ptr->rate = rate > 0 ? rate : 1;
}

void TS_Dispatch(void) { }
volatile int TS_InInterrupt = 0;   // tasks fire from thread context here

// ---------------------------------------------------------------------------
// Small DOS-era shims the vendored midi.c/al_midi.c reference.
// ---------------------------------------------------------------------------
// DOS sti/cli. These MUST be real: audiolib patterns like
// "DisableInterrupts(); _enable(); <wait>; RestoreInterrupts(flags)" rely on
// _enable re-enabling IRQs during the wait — as no-ops, such waits become
// total IRQ blackouts that corrupt the PIO-USB host and the audio pump.
void _enable(void)  { __asm volatile ("cpsie i" ::: "memory"); }
void _disable(void) { __asm volatile ("cpsid i" ::: "memory"); }

// al_midi.c calls min()/max() as FUNCTIONS (it includes no platform header
// that provides the macros). Undef the macro forms that reach this file via
// the engine headers so real functions can be defined for the linker.
#undef min
#undef max
int min(int a, int b) { return a < b ? a : b; }
int max(int a, int b) { return a > b ? a : b; }

int MUSIC_SoundDevice = Adlib;     // normally defined by the DOS music.c

// Blaster card plumbing referenced by AL_Init's SoundBlaster branch (we
// always init the plain Adlib path; these just satisfy the linker).
typedef struct { int Address, Type, Interrupt, Dma8, Dma16, Midi, Emu; } BLASTER_CONFIG_shim;
int BLASTER_GetCardSettings(void *blaster) { (void)blaster; return -1; }
int BLASTER_GetEnv(void *blaster) { (void)blaster; return -1; }

// Advance all active tasks by `samples` rendered output samples.
// Chip samples rendered but not yet accounted to the sequencer. Incremented by
// the render helper, consumed by duke_music_tasks_advance_chip().
static uint32_t s_chip_samples_rendered;

// Advance all active tasks by the chip samples rendered since the last call.
// Task rates are ticks/second, so the accumulator divides by the rate those
// samples actually exist at (chip-native, NOT the sink rate) — pacing against
// 48000 made every song run 3.45% slow.
static void duke_music_tasks_advance_chip(void)
{
    uint32_t samples = s_chip_samples_rendered;
    if (!samples) return;
    s_chip_samples_rendered = 0;

    for (int i = 0; i < MAX_TASKS; i++) {
        if (!s_tasks[i].active || !s_tasks[i].TaskService) continue;
        s_task_accum[i] += samples * (uint32_t)s_tasks[i].rate;
        int fires = 0;
        while (s_task_accum[i] >= DUKE_OPL_NATIVE_RATE && fires < 64) {
            s_task_accum[i] -= DUKE_OPL_NATIVE_RATE;
            s_tasks[i].TaskService(&s_tasks[i]);
            fires++;
        }
        if (fires == 64) s_task_accum[i] = 0;   // runaway guard after stalls
    }
}

// ---------------------------------------------------------------------------
// Render + mix: called from the audio pump for every mixed SFX division
// (S16 stereo interleaved). Adds the FM output with saturation.
// ---------------------------------------------------------------------------
static inline int16_t sat16(int32_t v)
{
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

// ---------------------------------------------------------------------------
// Output stage. Arithmetic matches the known-good rh1tech/frank-duke3d port
// (src/i_music.c MusicGenerator), whose commit history documents each piece:
//
//  * one-pole low-pass filter — "The real OPL2 DAC + analog stage naturally
//    attenuates harsh high-frequency content from high-feedback patches.
//    Without this, FB=7 instruments sound much harsher in digital emulation
//    than on real hardware." (their commit c3ddf9f). Duke's bank is full of
//    those: 37% of D3DTIMBR.TMB's 256 patches use FB>=5, 48 use FB=7.
//  * <<3 gain — "matches Doom/Heretic OPL output level"; they landed on 8x
//    after 10x hard-clipped percussion (dc3fc60). We were ~10x quieter.
//  * chip-native render rate — with NO_RATECONV the emulator advances one
//    internal tick per sample, so its output IS 49716 Hz; consuming it at
//    another rate detunes oscillators AND stretches envelopes (their b553d03:
//    "wrong notes and distortion"). We resample to the 48 kHz HDMI sink.
//
// LPF/resampler state persists across calls (reset in duke_music_reset_dsp).
// ---------------------------------------------------------------------------
extern void OPL_calc_buffer_stereo(OPL *opl, int32_t *buffer, uint32_t nsamples);

// 16.16 step: how many chip samples advance per output sample.
#define RESAMP_STEP ((uint32_t)(((uint64_t)DUKE_OPL_NATIVE_RATE << 16) / DUKE_SINK_RATE))

// Output gain, as a left shift. 3 (=8x) is frank-duke3d's tuned value ("matches
// Doom/Heretic OPL output level"; they backed off from 10x because percussion
// hard-clipped). Music is summed into multivoc's SFX division, so if loud SFX
// over loud music clips, drop this to 2 rather than touching the SFX path.
#ifndef DUKE_MUSIC_GAIN_SHIFT
#define DUKE_MUSIC_GAIN_SHIFT 3
#endif

static uint32_t s_clip_count;      // music+SFX sums that saturated (diagnostic)
static int32_t  s_lpf_prev;        // low-pass history (mono chip output)
static int32_t  s_rs_prev;         // previous chip sample (resampler history)
static int32_t  s_rs_cur;          // current chip sample
static uint32_t s_rs_frac;         // 16.16 position between prev and cur
static bool     s_rs_primed;

void duke_music_reset_dsp(void)
{
    s_lpf_prev = 0;
    s_rs_prev = s_rs_cur = 0;
    s_rs_frac = 0;
    s_rs_primed = false;
}

// Read-and-clear the clip counter (called ~1 Hz from core0's idle hook).
uint32_t duke_music_take_clip_count(void)
{
    uint32_t c = s_clip_count;
    s_clip_count = 0;
    return c;
}

// Render one chip sample (49716 Hz), low-pass filtered and gained.
static inline int32_t next_chip_sample(void)
{
    static int32_t  buf[64];
    static uint32_t have, pos;

    if (pos >= have) {
        OPL_calc_buffer_stereo(s_opl, buf, 64);
        have = 64;
        pos = 0;
        s_chip_samples_rendered += 64;   // sequencer pacing (chip-rate units)
    }
    int32_t packed = buf[pos++];
    // Packed as (raw<<16)|raw — the mono OPL output duplicated in both halves.
    int32_t s = (int16_t)(packed >> 16);
    // One-pole LPF (y = 0.5*x + 0.5*prev), then the 8x output gain.
    s = (s + s_lpf_prev) >> 1;
    s_lpf_prev = s;
    return s << DUKE_MUSIC_GAIN_SHIFT;
}

void duke_music_mix(int16_t *stereo, int frames)
{
    if (!s_music_up || !MIDI_SongPlaying()) return;

    if (!s_rs_primed) {
        s_rs_prev = next_chip_sample();
        s_rs_cur  = next_chip_sample();
        s_rs_frac = 0;
        s_rs_primed = true;
    }

    for (int i = 0; i < frames; i++) {
        // Linear interpolation between the two straddling chip samples.
        int32_t m = s_rs_prev +
                    (int32_t)(((int64_t)(s_rs_cur - s_rs_prev) * (int32_t)s_rs_frac) >> 16);
        m = (m * s_volume) >> 8;            // Duke's music-volume slider
        int32_t l = stereo[2 * i] + m, r = stereo[2 * i + 1] + m;
        // Count clipping of the music+SFX sum: if loud SFX over loud music
        // saturates, that is audible as SFX distortion and the fix is to lower
        // DUKE_MUSIC_GAIN_SHIFT (not to touch the SFX path).
        if (l > 32767 || l < -32768 || r > 32767 || r < -32768) s_clip_count++;
        stereo[2 * i]     = sat16(l);
        stereo[2 * i + 1] = sat16(r);

        s_rs_frac += RESAMP_STEP;
        while (s_rs_frac >= (1u << 16)) {
            s_rs_frac -= (1u << 16);
            s_rs_prev = s_rs_cur;
            s_rs_cur  = next_chip_sample();
        }
    }

    // Advance the MIDI sequencer by the chip samples actually consumed.
    duke_music_tasks_advance_chip();
}

// ---------------------------------------------------------------------------
// MUSIC_* API (the subset Duke uses), over midi.c + al_midi.c.
// ---------------------------------------------------------------------------
static midifuncs s_funcs;

char *MUSIC_ErrorString(int ErrorNumber) { (void)ErrorNumber; return "pico OPL music driver"; }

int MUSIC_Init(int SoundCard, int Address)
{
    (void)SoundCard; (void)Address;   // always the emulated AdLib

    if (!s_opl) {
        // Under NO_RATECONV the rate argument is not used for conversion; pass
        // the native rate so the intent is explicit.
        s_opl = OPL_new(3579545, DUKE_OPL_NATIVE_RATE);
        if (!s_opl) {
            printf("music: OPL_new failed\n");
            return MUSIC_Error;
        }
        OPL_reset(s_opl);
    }

    if (AL_Init(Adlib) != AL_Ok) {
        printf("music: AL_Init failed\n");
        return MUSIC_Error;
    }

    memset(&s_funcs, 0, sizeof(s_funcs));
    s_funcs.NoteOff       = AL_NoteOff;
    s_funcs.NoteOn        = AL_NoteOn;
    s_funcs.ControlChange = AL_ControlChange;
    s_funcs.ProgramChange = AL_ProgramChange;
    s_funcs.PitchBend     = AL_SetPitchBend;
    MIDI_SetMidiFuncs(&s_funcs);

    s_music_up = 1;
    printf("music: OPL2 (emu8950) + audiolib MIDI sequencer up\n");
    return MUSIC_Ok;
}

int MUSIC_Shutdown(void)
{
    MIDI_StopSong();
    if (s_music_up) AL_Shutdown();
    s_music_up = 0;
    return MUSIC_Ok;
}

void MUSIC_SetMaxFMMidiChannel(int channel) { AL_SetMaxMidiChannel(channel); }
void MUSIC_SetVolume(int volume)
{
    if (volume < 0) volume = 0;
    if (volume > 255) volume = 255;
    s_volume = volume;
    MIDI_SetVolume(volume);
}
void MUSIC_SetMidiChannelVolume(int channel, int volume) { MIDI_SetUserChannelVolume(channel, volume); }
void MUSIC_ResetMidiChannelVolumes(void) { MIDI_ResetUserChannelVolume(); }
int  MUSIC_GetVolume(void) { return s_volume; }
void MUSIC_SetLoopFlag(int loopflag) { MIDI_SetLoopFlag(loopflag); }
int  MUSIC_SongPlaying(void) { return MIDI_SongPlaying(); }
void MUSIC_Continue(void) { MIDI_ContinueSong(); }
void MUSIC_Pause(void) { MIDI_PauseSong(); }
int  MUSIC_StopSong(void)
{
    extern uint32_t DisableInterrupts(void);
    extern void RestoreInterrupts(uint32_t);
    uint32_t lk = DisableInterrupts();
    MIDI_StopSong();
    RestoreInterrupts(lk);
    return MUSIC_Ok;
}

int MUSIC_PlaySong(char *songData, int loopflag)
{
    // Bracket with the audiolib cross-core lock: the sequencer runs from
    // core1's pump, and track setup must not interleave with it.
    extern uint32_t DisableInterrupts(void);
    extern void RestoreInterrupts(uint32_t);
    uint32_t lk = DisableInterrupts();
    MIDI_StopSong();
    duke_music_reset_dsp();   // clear LPF/resampler history so songs start clean
    int status = MUSIC_Error;
    if (s_music_up && MIDI_PlaySong((unsigned char *)songData, loopflag) == MIDI_Ok)
        status = MUSIC_Ok;
    RestoreInterrupts(lk);
    return status;
}

void MUSIC_SetContext(int context) { MIDI_SetContext(context); }
int  MUSIC_GetContext(void) { return MIDI_GetContext(); }
void MUSIC_SetSongTick(uint32_t PositionInTicks) { MIDI_SetSongTick(PositionInTicks); }
void MUSIC_SetSongTime(uint32_t milliseconds) { MIDI_SetSongTime(milliseconds); }
void MUSIC_SetSongPosition(int measure, int beat, int tick) { MIDI_SetSongPosition(measure, beat, tick); }
void MUSIC_GetSongPosition(songposition *pos) { MIDI_GetSongPosition(pos); }
void MUSIC_GetSongLength(songposition *pos) { MIDI_GetSongLength(pos); }

// Fades: applied instantly (menu transitions only; smooth fades can come in
// the polish pass).
int  MUSIC_FadeVolume(int tovolume, int milliseconds) { (void)milliseconds; MUSIC_SetVolume(tovolume); return MUSIC_Ok; }
int  MUSIC_FadeActive(void) { return 0; }
void MUSIC_StopFade(void) { }

void MUSIC_RerouteMidiChannel(int channel, int (*function)(int event, int c1, int c2)) { MIDI_RerouteMidiChannel(channel, function); }
void MUSIC_RegisterTimbreBank(unsigned char *timbres) { AL_RegisterTimbreBank(timbres); }

// Duke's wrapper (normally in the excluded dummy_audiolib.c): load the .MID
// from the GRP into MusicPtr and start it.
void PlayMusic(char *filename)
{
    int32_t fd = kopen4load(filename, 0);
    if (fd < 0) {
        printf("music: '%s' not found\n", filename);
        return;
    }
    int32_t len = kfilelength(fd);
    if (len > (int32_t)sizeof(MusicPtr)) {
        printf("music: '%s' too large (%ld > %u)\n", filename, (long)len,
               (unsigned)sizeof(MusicPtr));
        kclose(fd);
        return;
    }
    kread(fd, MusicPtr, len);
    kclose(fd);
    printf("music: playing '%s' (%ld bytes)\n", filename, (long)len);
    MUSIC_PlaySong((char *)MusicPtr, MUSIC_LoopSong);
}
