//
//  duke_dostext.h — 40x25 DOS-style startup console on the HDMI output.
//
#ifndef DUKE_DOSTEXT_H
#define DUKE_DOSTEXT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// This module is C++ so it can call pico_shared's FrensFonts.cpp directly --
// that header has no extern "C" guard, and keeping the vendored copy pristine is
// worth more than keeping this file C. Callers (pico_display.c, duke_fatfs_io.c)
// are C, hence the guard here.
#ifdef __cplusplus
extern "C" {
#endif

#define DOSTEXT_COLS 40
#define DOSTEXT_ROWS 25

// Start capturing stdout, as a pico_stdio driver. Call right after
// stdio_init_all(), long before the display exists — text accumulates in the
// character grid and duke_dostext_init() later replays it onto the screen.
void duke_dostext_attach_stdio(void);

// Point the console at the RGB555 scanout surface and clear it. Must be called
// after the HSTX output is up (Duke's _platform_init runs before Startup(), so
// every startup message still lands on screen).
void duke_dostext_init(uint16_t *surface, int stride);

// Mirror stdout here. Handles \n, \r and \t, wraps at 40 columns and scrolls.
void duke_dostext_write(const char *s, size_t len);

// Called once the game takes over the framebuffer; further writes are ignored.
void duke_dostext_stop(void);

bool duke_dostext_active(void);

#ifdef __cplusplus
}
#endif

#endif
