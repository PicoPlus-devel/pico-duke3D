//
//  duke_screenshot.cpp — F12 screenshots, as 8-bit indexed PNG on the SD card.
//
//  Implements the engine's screencapture() (Engine/engine.h), which was a live
//  no-op on this port: upstream Chocolate Duke3D did it with SDL_SaveBMP() in
//  Engine/display.c, and display.c is excluded from this build.
//
//  The frame is already exactly what we want to save -- 320x200, 8bpp,
//  palette-indexed (pico_display.c's s_fb8, reached through get_framebuffer())
//  -- so it goes out as an indexed PNG with no colour conversion at all: one
//  PLTE chunk from duke_display_palette_bgr888(), then the index bytes straight
//  from the framebuffer. About 10-25 KB per shot against 64000 raw.
//
//  MEMORY: PNGenc's whole footprint is one PNGENCIMAGE (~47 KB: a 32 KB zlib
//  window + deflate_state, two scanline buffers, the palette and a 2 KB file
//  buffer), and it lives in PSRAM. cmake/psram_linker.cmake moves this object
//  file's entire .bss there, so the feature costs no SRAM -- see the comment on
//  the *duke_screenshot.cpp.o line in that file. Nothing here is touched before
//  duke_psram_init() maps the PSRAM: PNGENC has no constructor, so s_png is
//  plain .bss with no static initialiser, and the only entry points are called
//  from the game loop.
//
//  SPEED: deflate therefore runs out of PSRAM, behind the QMI bus, with an 8 KB
//  XIP cache it shares with flash code -- so expect a visible hitch of a few
//  hundred ms rather than a few tens. That is fine for a one-shot F12 keypress,
//  and it cannot break audio: core1's audio code AND data are SRAM-resident
//  precisely so core0's PSRAM traffic cannot starve them (see
//  cmake/psram_linker.cmake), and its other job here -- the HSTX scanline
//  callback -- is __not_in_flash_func. The "in NNN ms" figure is printed on
//  every capture; if it ever needs to come down, -DMEM_SHRINK=5 shrinks
//  deflate's working set to ~6 KB, which then fits the XIP cache, at the cost of
//  a bigger file.
//
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "pico/stdlib.h"

#include "ff.h"
#include "PNGenc.h"

#define SHOT_DIR   "/screenshots"
#define SHOT_W     320
#define SHOT_H     200
#define SHOT_SLOTS 10000              // duke0000.png .. duke9999.png
#define SHOT_LEVEL 6                  // zlib compression level (1 fast .. 9 small)

// pico_display.c owns all three; it is C, hence the extern "C".
extern "C" void          *get_framebuffer(void);
extern "C" const uint8_t *duke_display_palette_bgr888(void);
extern "C" void           duke_pico_idle(void);

// ---------------------------------------------------------------------------
// PNGenc <-> FatFs
//
// PNGenc's file mode needs all five callbacks, and read/seek are NOT optional
// stubs: PNGEndFile() (3rdparty/PNGenc/png.inl) seeks back to patch the IDAT
// chunk length and then reads the whole compressed stream back to CRC it.
// So the file is opened FA_READ|FA_WRITE.
//
// It also ignores every callback's return value, so I/O failures are latched in
// s_io_error and checked once at the end instead.
// ---------------------------------------------------------------------------
static FIL     s_fil;
static bool    s_fil_open;
static bool    s_io_error;
static FRESULT s_io_fr;

static void latch(FRESULT fr)
{
    if (fr != FR_OK && !s_io_error) { s_io_error = true; s_io_fr = fr; }
}

static void *shot_open(const char *path)
{
    FRESULT fr = f_open(&s_fil, path, FA_READ | FA_WRITE | FA_CREATE_ALWAYS);
    if (fr != FR_OK) {
        printf("screenshot: cannot create %s (FRESULT %d)\n", path, (int)fr);
        latch(fr);
        return NULL;
    }
    s_fil_open = true;
    return &s_fil;
}

static void shot_close(PNGFILE *pf)
{
    (void)pf;
    if (s_fil_open) { latch(f_close(&s_fil)); s_fil_open = false; }
}

static int32_t shot_read(PNGFILE *pf, uint8_t *buf, int32_t len)
{
    (void)pf;
    UINT br = 0;
    if (len <= 0) return 0;
    latch(f_read(&s_fil, buf, (UINT)len, &br));
    return (int32_t)br;
}

static int32_t shot_write(PNGFILE *pf, uint8_t *buf, int32_t len)
{
    (void)pf;
    UINT bw = 0;
    if (len <= 0) return 0;
    FRESULT fr = f_write(&s_fil, buf, (UINT)len, &bw);
    latch(fr);
    // A short write is a full card, which f_write reports as FR_OK. Catch it
    // here or the PNG ends up silently truncated.
    if (fr == FR_OK && bw != (UINT)len) {
        printf("screenshot: short write (%u of %ld) -- card full?\n",
               (unsigned)bw, (long)len);
        s_io_error = true;
        s_io_fr = FR_DENIED;
    }
    return (int32_t)bw;
}

static int32_t shot_seek(PNGFILE *pf, int32_t pos)
{
    (void)pf;
    latch(f_lseek(&s_fil, (FSIZE_t)pos));
    return pos;
}

// ---------------------------------------------------------------------------
// The encoder instance. ~47 KB, in PSRAM (see the file header).
// ---------------------------------------------------------------------------
static PNGENC  s_png;
static uint8_t s_palette[768];        // BGR triplets, as PNGenc's PLTE wants
static char    s_tmp_path[80];

static const char *png_err_name(int e)
{
    switch (e) {
        case PNG_SUCCESS:             return "PNG_SUCCESS";
        case PNG_INVALID_PARAMETER:   return "PNG_INVALID_PARAMETER";
        case PNG_ENCODE_ERROR:        return "PNG_ENCODE_ERROR";
        case PNG_MEM_ERROR:           return "PNG_MEM_ERROR";
        case PNG_NO_BUFFER:           return "PNG_NO_BUFFER";
        case PNG_UNSUPPORTED_FEATURE: return "PNG_UNSUPPORTED_FEATURE";
        case PNG_INVALID_FILE:        return "PNG_INVALID_FILE";
        case PNG_TOO_BIG:             return "PNG_TOO_BIG";
        case PNG_NOT_INITIALIZED:     return "PNG_NOT_INITIALIZED";
        default:                      return "?";
    }
}

// ---------------------------------------------------------------------------
// Pick the next free /screenshots/dukeNNNN.png, creating the folder if needed.
//
// Sequential, like the DOS original, because there is no RTC on this board:
// upstream's date-stamped name would read 1970 + uptime (and is broken anyway --
// it builds the name into text[] and then formats the shared tempbuf scratch
// into the path, see takescreenshot() in Game/game.c).
//
// s_next only ever moves forward: a successful capture leaves the file behind,
// so the next call's access() finds the slot taken and steps past it, and a
// failed one retries the same slot. That makes the directory scan O(1) after the
// first call without any bookkeeping of its own.
// ---------------------------------------------------------------------------
extern "C" int duke_screenshot_next_path(char *out, int outsz)
{
    static int s_next = 0;

    if (mkdir(SHOT_DIR, 0777) != 0) {
        printf("screenshot: cannot create %s\n", SHOT_DIR);
        return -1;
    }
    for (; s_next < SHOT_SLOTS; s_next++) {
        snprintf(out, outsz, SHOT_DIR "/duke%04d.png", s_next);
        if (access(out, F_OK) != 0) return 0;      // free slot
    }
    printf("screenshot: %s is full (duke0000..duke9999)\n", SHOT_DIR);
    return -1;
}

// ---------------------------------------------------------------------------
// screencapture() — the engine entry point (Engine/engine.h).
//
// Safe to read the framebuffer directly: F12 is handled in nonsharedkeys(),
// which runs before the next drawrooms(), so s_fb8 still holds the frame that
// was just presented.
// ---------------------------------------------------------------------------
extern "C" int screencapture(char *filename, uint8_t inverseit)
{
    if (!filename || !filename[0]) return -1;

    const uint8_t *fb = (const uint8_t *)get_framebuffer();
    if (!fb) { printf("screenshot: no framebuffer\n"); return -1; }

    printf("screenshot: F12 -> %s\n", filename);

    // Snapshot the palette. Copied rather than passed through because
    // encodeBegin() takes a non-const pointer, and because inverseit needs
    // somewhere to put the inverted version. Duke always passes 0 here; the
    // editor's inverse capture is the reason the parameter exists.
    memcpy(s_palette, duke_display_palette_bgr888(), sizeof(s_palette));
    if (inverseit) {
        for (unsigned i = 0; i < sizeof(s_palette); i++)
            s_palette[i] = (uint8_t)(255 - s_palette[i]);
    }

    // Write to <name>.tmp and rename on success, the same atomic-finalize
    // pattern pico-bootLoader's image_convert.cpp uses. Without it a power cut
    // mid-write leaves a truncated dukeNNNN.png that then consumes that slot
    // forever, because next_path() only checks whether the name exists.
    snprintf(s_tmp_path, sizeof(s_tmp_path), "%s.tmp", filename);

    s_io_error = false;
    s_io_fr    = FR_OK;
    s_fil_open = false;

    int rc = s_png.open(s_tmp_path, shot_open, shot_close, shot_read,
                        shot_write, shot_seek);
    if (rc != PNG_SUCCESS) {
        printf("screenshot: open failed (%d %s)\n", rc, png_err_name(rc));
        return -1;
    }

    printf("screenshot: %dx%d 8bpp indexed, 256-colour PLTE, deflate level %d, "
           "%d KB window (MEM_SHRINK %d)\n",
           SHOT_W, SHOT_H, SHOT_LEVEL,
           (1 << (MAX_WBITS - MEM_SHRINK)) / 1024, MEM_SHRINK);

    uint64_t t0 = time_us_64();

    rc = s_png.encodeBegin(SHOT_W, SHOT_H, PNG_PIXEL_INDEXED, 8, s_palette,
                           SHOT_LEVEL);
    if (rc != PNG_SUCCESS) {
        printf("screenshot: encodeBegin failed (%d %s)\n", rc, png_err_name(rc));
        s_png.close();
        f_unlink(s_tmp_path);
        return -1;
    }

    // Top-down, one row per call. addLine(0) is what writes the PNG header and
    // initialises zlib; addLine(199) flushes, finalises the IDAT and appends
    // IEND -- so all 200 must be handed over, in order.
    for (int y = 0; y < SHOT_H; y++) {
        rc = s_png.addLine((uint8_t *)&fb[(size_t)y * SHOT_W]);
        if (rc != PNG_SUCCESS) {
            printf("screenshot: addLine failed at row %d (%d %s)\n",
                   y, rc, png_err_name(rc));
            s_png.close();
            f_unlink(s_tmp_path);
            return -1;
        }
#if DUKE_SCREENSHOT_DIAG
        // Off by default on purpose: SDK UART stdio BUSY-WAITS, so 200 lines of
        // this is ~8000 characters, i.e. ~700 ms of pure blocking at 115200.
        printf("screenshot: row %d\n", y);
#endif
        // Keep the world alive. This is the longest core0 stall in the port that
        // is not already inside an engine loop -- deflating out of PSRAM takes
        // long enough that leaving USB unserviced throughout would be asking for
        // a device timeout on the PIO-USB boards. Costs nothing per call: the
        // audio pump early-outs on a queue-level check and duke_usb_poll() is
        // gated to 2 ms inside the hook, so 200 calls give USB its normal
        // latency rather than 200 extra tuh_task()s.
        duke_pico_idle();

        if (s_io_error) break;
    }

    // addLine(199) already finalised the PNG; this runs our close callback
    // (f_close, which is what commits FatFs's buffered sector) and hands back the
    // total file size it wrote.
    int size = s_png.close();
    uint32_t ms = (uint32_t)((time_us_64() - t0) / 1000u);

    if (s_io_error) {
        printf("screenshot: SD write failed (FRESULT %d) after %lu ms\n",
               (int)s_io_fr, (unsigned long)ms);
        f_unlink(s_tmp_path);
        return -1;
    }
    if (size <= 0) {
        printf("screenshot: encoder produced %d bytes (%d %s)\n",
               size, s_png.getLastError(), png_err_name(s_png.getLastError()));
        f_unlink(s_tmp_path);
        return -1;
    }

    f_unlink(filename);                // replace, in case a stale one is there
    FRESULT fr = f_rename(s_tmp_path, filename);
    if (fr != FR_OK) {
        printf("screenshot: rename %s -> %s failed (FRESULT %d)\n",
               s_tmp_path, filename, (int)fr);
        f_unlink(s_tmp_path);
        return -1;
    }

    // Ratio against the raw 8bpp frame, in tenths, so it reads as "4.3:1".
    unsigned ratio10 = (unsigned)((SHOT_W * SHOT_H * 10L) / size);
    printf("screenshot: wrote %s -- %d bytes in %lu ms (%u.%u:1 vs %d raw)\n",
           filename, size, (unsigned long)ms,
           ratio10 / 10, ratio10 % 10, SHOT_W * SHOT_H);
    return 0;
}
