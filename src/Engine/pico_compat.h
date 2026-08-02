//
//  pico_compat.h — bare-metal RP2350 platform compat for Chocolate Duke3D.
//
//  Analogous to unix_compat.h but with NO OS / SDL / POSIX filesystem: the
//  platform layer (video/input/timer/file access) is reimplemented against
//  pico_shared + FatFs in src/pico/. Deliberately does NOT define
//  PLATFORM_SUPPORTS_SDL, so the engine's SDL code paths (display.c,
//  sdl_midi.c, dsl.c) are excluded and replaced.
//
#ifndef DUKE3D_PICO_COMPAT_H
#define DUKE3D_PICO_COMPAT_H

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stdint.h>
#include <assert.h>

// POSIX-ish headers the file layer references (open/read/lseek/close/access +
// their flag constants). newlib provides the prototypes; the actual _open/_read
// syscalls are retargeted onto FatFs by the SD file layer.
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#ifndef O_BINARY
#define O_BINARY 0
#endif
#ifndef O_RDONLY
#define O_RDONLY 0
#endif
#ifndef S_IREAD
#define S_IREAD S_IRUSR
#endif
#ifndef F_OK
#define F_OK 0
#endif

#define PLATFORM_PICO 1
#define PLATFORM_LITTLEENDIAN 1

// Fatal-error reporting (src/pico/duke_fatal.c). There is no terminal on this
// platform, so a fatal has to reach the HDMI output or the user learns nothing:
// duke_fatal* paint the reason on the DOS console -- under the last lines of log
// that led to it -- and halt.
//
// DUKE_FATAL_ABORT() replaces the engine's "printf the reason; getch(); exit(0)"
// idiom, which on bare metal is a permanent silent hang: no registered stdio
// driver has in_chars, so the key that getchar() waits for can never arrive.
void duke_fatal(const char *fmt, ...) __attribute__((noreturn));
void duke_fatal_halt(void) __attribute__((noreturn));
#define DUKE_FATAL_ABORT() duke_fatal_halt()

// BUILD allocation macros. The engine's large permanent allocations use
// kkmalloc: the tile cache (tiles.c), the 64 KB translucency table and the
// palette-lookup tables (engine.c). These do not fit the ~250 KB SRAM heap, so
// route kkmalloc to the PSRAM bump allocator (psram_alloc, in src/pico). It
// returns NULL on OOM (not a panic), which lets the engine's own fallbacks run
// (makepalookup -> allocache; transluc -> disabled). kkfree is only used at
// engine shutdown, so a no-op is safe. Small/transient kmalloc stays in SRAM.
extern void *psram_alloc(size_t);
#define kmalloc(x)  malloc(x)
#define kkmalloc(x) psram_alloc((size_t)(x))
#define kfree(x)    free(x)
#define kkfree(x)   ((void)0)

// Watcom-ism: the source casts pointers to 32-bit ints via FP_OFF.
#ifdef FP_OFF
#undef FP_OFF
#endif
#define FP_OFF(x) ((int32_t)(x))

#ifndef max
#define max(x, y) (((x) > (y)) ? (x) : (y))
#endif
#ifndef min
#define min(x, y) (((x) < (y)) ? (x) : (y))
#endif

#define __int64 int64_t

#ifndef stricmp
#define stricmp strcasecmp
#endif
#ifndef strcmpi
#define strcmpi strcasecmp
#endif

// Single-player: the multiplayer layer is stubbed (dummy_multi.c).
#define USER_DUMMY_NETWORK 1

#endif // DUKE3D_PICO_COMPAT_H
