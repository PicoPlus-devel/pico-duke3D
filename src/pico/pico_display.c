//
//  pico_display.c — RP2350 reimplementation of the BUILD baselayer (display.c).
//
//  Replaces Chocolate Duke3D's SDL video/palette/timer with the pico_hdmi HSTX
//  driver. The engine renders into an 8-bit palettized framebuffer (frameplace,
//  320x200); _nextpage() palette-expands it to an RGB555 intermediate that the
//  HSTX scanline callback (core1) scales to 640x480 and scans out. Timer logic
//  is ported straight from display.c with TIMER_GetPlatformTicks() on the
//  RP2350 microsecond clock. Input (keyboard/mouse) is stubbed for M2 — the
//  Duke logo/title/demo sequence advances on totalclock alone.
//
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"

#include "video_output.h"          // pico_hdmi
#include "hstx_data_island_queue.h"

// BUILD engine headers (declarations + functions we drive).
#include "build.h"
#include "engine.h"
#include "display.h"

// ---------------------------------------------------------------------------
// Engine framebuffer globals (were defined in display.c).
// ---------------------------------------------------------------------------
int32_t  xres, yres, bytesperline, imageSize, maxpages;
uint8_t *frameplace = 0;
uint8_t *frameoffset = 0;
int32_t  buffermode, origbuffermode, linearmode;
uint8_t  permanentupdate = 0, vgacompatible = 1;
uint8_t  lastPalette[768];
// horizlookup / horizlookup2 are owned by the engine (build.h); just use them.
extern int32_t *horizlookup, *horizlookup2;

// ---------------------------------------------------------------------------
// Backing buffers + HSTX plumbing.
// ---------------------------------------------------------------------------
#define FB_W 320
#define FB_H 200

static uint8_t  s_fb8[FB_W * FB_H];       // 8bpp — frameplace points here
static uint16_t s_rgb[FB_W * FB_H];       // RGB555 — scanned out by core1
static uint16_t s_pal555[256];            // palette lookup (RGB555)

static volatile uint32_t s_frame_counter = 0;   // HSTX scanout frames (core1)
static uint32_t s_page_count = 0;               // _nextpage calls = GAME frames
static uint32_t s_idle_count = 0;               // duke_pico_idle calls (hot hook)
static uint32_t s_core1_stack[2048] __attribute__((aligned(8)));
static bool s_video_up = false;

// core1 (DMA IRQ): fill 320 words (two RGB555 px each) from the RGB555 frame,
// vertical nearest-neighbour 480->200, horizontal 2x doubling. Kept minimal.
static void __not_in_flash_func(duke_scanline_cb)(uint32_t v_scanline,
                                                  uint32_t active_line,
                                                  uint32_t *line_buffer)
{
    (void)v_scanline;
    uint32_t row = (active_line * FB_H) / 480;      // 0..199
    if (row >= FB_H) row = FB_H - 1;
    const uint16_t *s = &s_rgb[row * FB_W];
    for (uint32_t i = 0; i < FB_W; i++) {
        uint32_t px = s[i];
        line_buffer[i] = px | (px << 16);
    }
}

static void __not_in_flash_func(duke_vsync_cb)(void)
{
    s_frame_counter++;
}

// ---------------------------------------------------------------------------
// Platform init / video mode
// ---------------------------------------------------------------------------
void _platform_init(int argc, char **argv, const char *title, const char *iconName)
{
    (void)argc; (void)argv; (void)title; (void)iconName;
    // clk_hstx is configured to a fixed 126 MHz in duke_boot before we get here.
    assert(clock_get_hz(clk_hstx) == 126000000u);
    memset(s_pal555, 0, sizeof(s_pal555));
    memset(s_rgb, 0, sizeof(s_rgb));

    // HDMI mode (not DVI-only) so audio data islands are emitted; init the DI
    // queue before video comes up (M0-proven order). duke_audio.c pushes the
    // mixed samples; 48 kHz is the exact-lock rate for the 25.2 MHz pixel clock.
    video_output_set_dvi_mode(false);
    hstx_di_queue_init();
    video_output_set_vsync_callback(duke_vsync_cb);
    // Audio pump runs as core1's idle-loop background task — immune to
    // core0 frame-rate dips (which starved audible gaps into the music).
    {
        extern void duke_audio_core1_task(void);
        video_output_set_background_task(duke_audio_core1_task);
    }
    video_output_init(640, 480);
    pico_hdmi_set_audio_sample_rate(48000);
    video_output_set_scanline_callback(duke_scanline_cb);
    multicore_launch_core1_with_stack(video_output_core1_run,
                                      s_core1_stack, sizeof(s_core1_stack));
    s_video_up = true;
    printf("pico_display: HSTX 640x480 up (clk_hstx=%lu)\n",
           (unsigned long)clock_get_hz(clk_hstx));

    // USB host after video (fruitjam-doom order): gamepad + keyboard input.
    extern void duke_usb_init(void);
    duke_usb_init();
}

void _uninitengine(void) { }

void getvalidvesamodes(void)
{
    // Advertise a single 320x200 mode (vidoption 2).
    validmodecnt = 1;
    validmode[0] = 0;
    validmodexdim[0] = 320;
    validmodeydim[0] = 200;
}

// Ported from display.c init_new_res_vars (SDL bits removed): point the engine
// at our 320x200 8bpp framebuffer and rebuild the derived tables.
static void init_new_res_vars(void)
{
    xdim = xres = FB_W;
    ydim = yres = FB_H;
    bytesperline = FB_W;
    vgacompatible = 1;
    linearmode = 1;
    qsetmode = FB_H;
    activepage = visualpage = 0;

    frameoffset = frameplace = s_fb8;

    // horizlookup / horizlookup2 scratch (ydim*4 int32 each, per display.c).
    int j = ydim * 4 * (int)sizeof(int32_t);
    if (horizlookup)  free(horizlookup);
    if (horizlookup2) free(horizlookup2);
    horizlookup  = (int32_t *)malloc(j);
    horizlookup2 = (int32_t *)malloc(j);

    // Column start offsets (screenspace Y -> framebuffer byte offset).
    j = 0;
    for (int i = 0; i <= ydim; i++) { ylookup[i] = j; j += bytesperline; }

    horizycent = ((ydim * 4) >> 1);
    oxyaspect = oxdimen = oviewingrange = -1;

    setBytesPerLine(bytesperline);
    setview(0L, 0L, xdim - 1, ydim - 1);
    setbrightness(curbrightness, palette);

    if (searchx < 0) { searchx = halfxdimen; searchy = (ydimen >> 1); }
}

int32_t _setgamemode(uint8_t davidoption, int32_t daxdim, int32_t daydim)
{
    (void)davidoption; (void)daxdim; (void)daydim;   // fixed 320x200 for M2
    vidoption = 2;
    getvalidvesamodes();
    init_new_res_vars();
    printf("pico_display: gamemode %dx%d\n", (int)xdim, (int)ydim);
    return 0;
}

void setvmode(int mode) { (void)mode; }

void *_getVideoBase(void)   { return s_fb8; }
void *get_framebuffer(void) { return s_fb8; }

// ---------------------------------------------------------------------------
// Palette — Ken's 4-byte BGR entries (0-63) -> RGB555 LUT (matches display.c).
// ---------------------------------------------------------------------------
int VBE_setPalette(uint8_t *palettebuffer)
{
    uint8_t *p = palettebuffer;
    memcpy(lastPalette, palettebuffer, 768);
    for (int i = 0; i < 256; i++) {
        uint32_t b = *p++;                 // 0..63
        uint32_t g = *p++;
        uint32_t r = *p++;
        p++;                               // reserved
        s_pal555[i] = (uint16_t)(((r >> 1) << 10) | ((g >> 1) << 5) | (b >> 1));
    }
    return 0;
}

int VBE_getPalette(int32_t start, int32_t num, uint8_t *palettebuffer)
{
    (void)start; (void)num; (void)palettebuffer;
    return 1;
}

// ---------------------------------------------------------------------------
// Input delivery — duke_usb_input.cpp posts DOS scancodes here; keyhandler()
// (Game/src/keyboard.c) reads them back via _readlastkeyhit() and updates
// KB_KeyDown[] + CONTROL state. Bit 0x80 = key release.
// ---------------------------------------------------------------------------
static uint8_t s_lastkey = 0;

void pico_display_post_key(uint8_t rawcode)
{
    s_lastkey = rawcode;
    keyhandler();
}

// ---------------------------------------------------------------------------
// Present
// ---------------------------------------------------------------------------
extern void duke_usb_poll(void);
extern void duke_audio_poll_headphone(void);
extern void duke_audio_pump(void);

void _handle_events(void)
{
    duke_pico_idle();
}
void _idle(void) { }

// The port's "keep the world alive" hook: audio pump + input + headphone
// detect, all thread-context. Called from _handle_events (per frame), the
// engine's faketimerhandler (busy loops, art loads, sound-waits) and — via
// sampletimer() below — from getpackets-style wait loops. All call sites are
// core0 thread context; pump_once() has cheap early-outs, so frequent calls
// are fine.
void duke_pico_idle(void)
{
    // HOT PATH: the engine calls faketimerhandler (and therefore this) from
    // inside its render inner loops — thousands of times per frame. Everything
    // here must be a few instructions in the common case. In particular use
    // time_us_32() (one register read) and NEVER to_ms_since_boot(), which is a
    // 64-bit read plus a 64-bit divide and cost us most of the frame rate.
    const uint32_t now_us = time_us_32();
    s_idle_count++;

    // Cheap: a queue-level check that returns unless core1 has fallen behind.
    duke_audio_pump();

    // Cheap: two volatile reads unless a sound actually finished. Must run at
    // this frequency — Duke spin-waits on sound completion inside
    // faketimerhandler (premap.c's level-start speech wait), and those loops
    // only exit once a queued callback lands.
    {
        extern void duke_audio_run_deferred_callbacks(void);
        duke_audio_run_deferred_callbacks();
    }

    // USB host + headphone detect are expensive (tuh_task, codec I2C) and do
    // not need inner-loop rates.
    {
        static uint32_t last_io_us;
        if (now_us - last_io_us >= 2000u) {
            last_io_us = now_us;
            duke_usb_poll();
            duke_audio_poll_headphone();
        }
    }

    // 1 Hz audio health report. Diagnostic-build only: in the shipping build the
    // whole block (and the per-stage timers behind it, see duke_audio.c) compiles
    // out, so the audio path carries no measurement cost.
    //
    //   clip=N     music+SFX sum saturated -> lower DUKE_MUSIC_GAIN_SHIFT.
    //   starve=N   pump found the sink near-empty.
    //   dv vs need production rate against what 48 kHz demands. THE number: if
    //              dv < need the sink drains and, because the MIDI clock and
    //              voice playback advance with rendered samples, music and
    //              speech play slow by exactly that ratio.
    //   bg         core1 pump entries/s. Hundreds of thousands = healthy (it is
    //              early-outing on a satisfied target). Single digits = each
    //              call is grinding through its full division guard, which is
    //              what a production shortfall looks like from the outside.
    //   di/du      HDMI island queue level (target 512) and underrun count.
    //              du must stay frozen.
    // Healthy in-level reference, E1L1 with music (RP2350 @ 378 MHz):
    //   dv=188 need=187 di~500 du frozen; stage lock=0 svc=27 music=555 push=143.
#if DUKE_VIDEO_DIAG
    {
        extern uint32_t duke_music_take_clip_count(void);
        extern uint32_t duke_audio_take_starve_count(void);
        static uint32_t last_report_us;
        if (now_us - last_report_us >= 1000000u) {
            last_report_us = now_us;
            uint32_t clips = duke_music_take_clip_count();
            uint32_t starve = duke_audio_take_starve_count();
            // bg/dv/need: see duke_audio_take_rate_stats. dv is the production
            // rate and "need" is what 48 kHz demands — if dv < need the sink
            // drains and no amount of ring depth can hide it. bg says whether
            // core1's tight loop is actually spinning (thousands) or wedged in
            // long pump calls (tens).
            extern void duke_audio_take_rate_stats(uint32_t *, uint32_t *, uint32_t *);
            extern void duke_audio_take_stage_us(uint32_t *, uint32_t *, uint32_t *, uint32_t *);
            uint32_t bg, dv, fpd, u_lock, u_svc, u_music, u_push;
            duke_audio_take_rate_stats(&bg, &dv, &fpd);
            duke_audio_take_stage_us(&u_lock, &u_svc, &u_music, &u_push);
            printf("audio: clip=%lu starve=%lu bg=%lu/s dv=%lu/s need=%lu/s di=%lu du=%lu\n",
                   (unsigned long)clips, (unsigned long)starve,
                   (unsigned long)bg, (unsigned long)dv,
                   (unsigned long)(fpd ? 48000u / fpd : 0),
                   (unsigned long)hstx_di_queue_get_level(),
                   (unsigned long)hstx_di_queue_get_underrun_count());
            // ms of core1 wall time per second in each pump stage. These four
            // should sum to well under 1000; whichever dominates is the cause of
            // the in-level production shortfall.
            printf("  stage ms/s: lock=%lu svc=%lu music=%lu push=%lu (of 1000)\n",
                   (unsigned long)(u_lock / 1000), (unsigned long)(u_svc / 1000),
                   (unsigned long)(u_music / 1000), (unsigned long)(u_push / 1000));
        }
    }
#endif
}

void _nextpage(void)
{
    s_page_count++;   // game-frame counter (see the diag report below)

    // _handle_events -> duke_pico_idle also drains the deferred sound
    // callbacks, so there is no separate dispatch needed here.
    _handle_events();

    if (!s_video_up) return;

    // Palette-expand the 8bpp frame into the RGB555 scanout buffer (core0).
    const uint8_t *src = s_fb8;
    uint16_t *dst = s_rgb;
    for (int i = 0; i < FB_W * FB_H; i++) dst[i] = s_pal555[src[i]];

    // Pace to one HSTX frame so we cap near 60 Hz — but never hang if core1
    // stops advancing (that's exactly the failure we're diagnosing).
    uint32_t f = s_frame_counter;
    absolute_time_t deadline = make_timeout_time_ms(50);
    while (s_frame_counter == f && !time_reached(deadline)) tight_loop_contents();

#ifdef DUKE_VIDEO_DIAG
    // 1 Hz core0-side status (enable with -DDUKE_VIDEO_DIAG=1). If core1
    // dies, "vf" freezes while this keeps printing; if the watchdog is
    // resync-looping, "rs" climbs. "du" = DI underrun (audio pump starved).
    static uint32_t s_last_report, s_last_vf, s_last_pages, s_last_idle;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - s_last_report >= 1000) {
        extern uint32_t hstx_di_queue_get_underrun_count(void);
        extern uint32_t hstx_di_queue_get_level(void);
        extern int get_video_output_resync_count(void);
        // gf = GAME frames/s (_nextpage calls) — the number that matters.
        // vf = HSTX scanout frames/s, which is always ~60 and says nothing
        // about how fast the game itself is running.
        // ic = idle-hook calls/s. This hook runs from the engine's render
        // inner loops, so its per-call cost is multiplied by this number.
        printf("vid: gf=%lu/s ic=%lu/s vf=+%lu/s rs=%d di=%lu du=%lu\n",
               (unsigned long)(s_page_count - s_last_pages),
               (unsigned long)(s_idle_count - s_last_idle),
               (unsigned long)(s_frame_counter - s_last_vf),
               get_video_output_resync_count(),
               (unsigned long)hstx_di_queue_get_level(),
               (unsigned long)hstx_di_queue_get_underrun_count());
        s_last_pages = s_page_count;
        s_last_idle = s_idle_count;
        s_last_vf = s_frame_counter;
        s_last_report = now;
    }
#endif
}

void _updateScreenRect(int32_t x, int32_t y, int32_t w, int32_t h)
{
    (void)x; (void)y; (void)w; (void)h;
    _nextpage();
}

// ---------------------------------------------------------------------------
// Direct 8bpp pixel access (used by a few engine paths).
// ---------------------------------------------------------------------------
uint8_t readpixel(uint8_t *location) { return *location; }
void    drawpixel(uint8_t *location, uint8_t pixel) { *location = pixel; }

int screencapture(char *filename, uint8_t inverseit) { (void)filename;(void)inverseit; return 0; }

// ---------------------------------------------------------------------------
// 2D / 16-colour helpers (editor/overhead map) — stubbed for M2.
// ---------------------------------------------------------------------------
void drawpixel16(int32_t offset) { (void)offset; }
void fillscreen16(int32_t a, int32_t b, int32_t c) { (void)a;(void)b;(void)c; }
void drawline16(int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint8_t col) { (void)x1;(void)y1;(void)x2;(void)y2;(void)col; }
void setcolor16(uint8_t color) { (void)color; }
void clear2dscreen(void) { }

// ---------------------------------------------------------------------------
// Input stubs
// ---------------------------------------------------------------------------
int  setupmouse(void) { return 0; }
void readmousexy(short *x, short *y) { if (x) *x = 0; if (y) *y = 0; }
void readmousebstatus(short *b) { if (b) *b = 0; }
uint8_t _readlastkeyhit(void) { return s_lastkey; }
void initkeys(void) { }
void uninitkeys(void) { }

void _joystick_init(void) { }
void _joystick_deinit(void) { }
int  _joystick_update(void) { return 0; }
int  _joystick_axis(int axis) { (void)axis; return 0; }
int  _joystick_hat(int hat) { (void)hat; return 0; }
int  _joystick_button(int button) { (void)button; return 0; }

// ---------------------------------------------------------------------------
// Timer — ported from display.c; platform ticks = time_us_64().
// ---------------------------------------------------------------------------
static int64_t timerfreq = 0;
static int32_t timerticspersec = 0;
static int32_t timerlastsample = 0;
static void (*usertimercallback)(void) = NULL;

int  TIMER_GetPlatformTicksInOneSecond(int64_t *t) { *t = 1000000; return 1; }
void TIMER_GetPlatformTicks(int64_t *t) { *t = (int64_t)time_us_64(); }

void (*installusertimercallback(void (*callback)(void)))(void)
{
    void (*old)(void) = usertimercallback;
    usertimercallback = callback;
    return old;
}

int inittimer(int tickspersecond)
{
    int64_t t;
    if (timerfreq) return 0;
    TIMER_GetPlatformTicksInOneSecond(&t);
    timerfreq = t;
    timerticspersec = tickspersecond;
    TIMER_GetPlatformTicks(&t);
    timerlastsample = (int32_t)(t * timerticspersec / timerfreq);
    usertimercallback = NULL;
    return 0;
}

void uninittimer(void) { timerfreq = 0; timerticspersec = 0; }

void sampletimer(void)
{
    int64_t i;
    int32_t n;

    // Keep audio flowing in wait loops that poll the clock (getpackets-style
    // spins call sampletimer each iteration). Audio only — this is called
    // from tight loops, and pump_once early-outs when the sinks are full.
    duke_audio_pump();

    if (!timerfreq) return;
    TIMER_GetPlatformTicks(&i);
    n = (int32_t)(i * timerticspersec / timerfreq) - timerlastsample;
    if (n > 0) { totalclock += n; timerlastsample += n; }
    if (usertimercallback) for (; n > 0; n--) usertimercallback();
}

uint32_t getticks(void)
{
    int64_t i;
    TIMER_GetPlatformTicks(&i);
    return (uint32_t)(i * 1000 / (timerfreq ? timerfreq : 1000000));
}

int gettimerfreq(void) { return timerticspersec; }
