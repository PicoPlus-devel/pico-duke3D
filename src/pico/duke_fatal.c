//
//  duke_fatal.c — fatal errors on the HDMI output.
//
//  Every way this port could die used to end at a UART nobody has connected:
//  duke_psram/duke_fatfs printed a diagnosis and RETURNED (so the boot carried
//  on into garbage), the engine's missing-file paths printed and then blocked in
//  getchar() forever -- no registered stdio driver has in_chars, so that key can
//  never arrive -- and Error(EXIT_SUCCESS, "...") printed and rebooted 1 ms
//  later, which at startup is an invisible bootloop. On the Murmulator M2, which
//  has no UART at all (its GPIO 0/1 are the Wii connector), the user got a black
//  screen and nothing else.
//
//  So: paint the reason on the DOS console the port already has, and STOP.
//  Halting rather than rebooting is deliberate -- the conditions that get here
//  (no SD card, no PSRAM, no DUKE3D.GRP) are all permanent, so a reboot would
//  just wipe the message off the screen and show it again too briefly to read.
//
//  The console is normally already live: duke_boot's main() brings the display
//  up right after duke_psram_init(), before the SD card. duke_video_ensure_up()
//  covers the case where it is not.
//
#include <stdarg.h>
#include <stdio.h>

#include "pico/stdlib.h"

#include "duke_fatal.h"
#include "duke_dostext.h"

// pico_display.c (owns the RGB555 scanout surface).
extern void duke_video_ensure_up(void);
extern void duke_display_console_resume(void);

// A fatal inside a fatal must not recurse. It happens on the ordinary path:
// Error() calls duke_fatal_halt(), and _exit() -- which some callers still
// reach -- calls it again. The second call just parks.
static volatile bool s_in_fatal = false;

static void __attribute__((noreturn)) halt_forever(void)
{
    stdio_flush();
    // No __breakpoint() here: with no debugger attached BKPT escalates to a
    // HardFault, and isr_hardfault's breadcrumb would then be the last thing
    // anyone sees on the UART instead of the reason we came here for.
    while (1) tight_loop_contents();
}

// Banner + footer, drawn around whatever the caller already printed.
static void __attribute__((noreturn)) finish(void)
{
    printf("\n");
#if BUILD_FOR_BOOTLOADER
    printf("Press RESET (returns to the boot menu).\n");
#else
    printf("Press RESET, or power-cycle the board.\n");
#endif
    halt_forever();
}

static void begin(void)
{
    // The display may not exist yet (a fatal from duke_psram_init) or may have
    // been handed to the game long ago (a fatal mid-level). Both are handled:
    // ensure_up is a no-op when video is already running, and console_resume
    // repaints the character grid -- which duke_dostext keeps maintaining even
    // while retired -- so the error lands under the log that led to it either way.
    duke_video_ensure_up();
    duke_dostext_set_title("FATAL ERROR");
    duke_display_console_resume();
    printf("\n*** FATAL ERROR ***\n");
}

void duke_fatal(const char *fmt, ...)
{
    if (s_in_fatal) halt_forever();
    s_in_fatal = true;

    begin();
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    finish();
}

void duke_fatal_halt(void)
{
    if (s_in_fatal) halt_forever();
    s_in_fatal = true;

    // The caller already printf'd its reason, so it is in the console grid and
    // resuming replays it right above the banner.
    begin();
    finish();
}
