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

#define DUKE_MUSIC_RATE 48000   // playback rate the task pacing is based on

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
static void duke_music_tasks_advance(int samples)
{
    for (int i = 0; i < MAX_TASKS; i++) {
        if (!s_tasks[i].active || !s_tasks[i].TaskService) continue;
        s_task_accum[i] += (uint32_t)samples * (uint32_t)s_tasks[i].rate;
        int fires = 0;
        while (s_task_accum[i] >= DUKE_MUSIC_RATE && fires < 8) {
            s_task_accum[i] -= DUKE_MUSIC_RATE;
            s_tasks[i].TaskService(&s_tasks[i]);
            fires++;
        }
        if (fires == 8) s_task_accum[i] = 0;   // runaway guard after stalls
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

// Raw LINEAR render: int32 accumulator per sample, BEFORE the driver's
// packed-uint16 conversion. We must consume this form: the packed variant
// (OPL_calc_buffer_stereo) truncates `accum >> 1` into uint16 with NO clamp
// (the source even says "// todo clamp?"), which WRAPS on loud passages —
// audible as harsh polarity-flip clicks whenever several FM channels sum
// past int16 range (Duke's al_midi drives hotter levels than Doom's driver,
// which is why doom never hit it).
extern void OPL_calc_buffer_linear(OPL *opl, int32_t *buffer, uint32_t nsamples);

void duke_music_mix(int16_t *stereo, int frames)
{
    if (!s_music_up || !MIDI_SongPlaying()) return;
    duke_music_tasks_advance(frames);

    static int32_t fm[64];
    while (frames > 0) {
        int n = frames > 64 ? 64 : frames;
        OPL_calc_buffer_linear(s_opl, fm, (uint32_t)n);
        for (int i = 0; i < n; i++) {
            int32_t m = (-fm[i]) >> 1;          // _MO(): mono out
            m = (m * s_volume) >> 8;            // music volume
            stereo[2 * i]     = sat16(stereo[2 * i] + m);
            stereo[2 * i + 1] = sat16(stereo[2 * i + 1] + m);
        }
        stereo += 2 * n;
        frames -= n;
    }
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
        s_opl = OPL_new(3579552, DUKE_MUSIC_RATE);  // NO_RATECONV: renders at 49716
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
