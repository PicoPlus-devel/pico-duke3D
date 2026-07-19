//
//  SDL.h — minimal no-op SDL shim for the bare-metal Duke3D port.
//
//  Chocolate Duke3D's SDL backends (display.c, dsl.c, sdl_midi.c) are replaced
//  by pico_shared drivers, but a few game-logic files still `#include "SDL.h"`
//  unconditionally (menues.c, global.c) and reference a handful of SDL symbols
//  for mouse-grab / shutdown. This shim satisfies those without real SDL. We do
//  NOT define USE_SDL, so duke3d.h never pulls in SDL_mixer.h.
//
#ifndef DUKE3D_SDL_SHIM_H
#define DUKE3D_SDL_SHIM_H

#include <stdint.h>

typedef struct SDL_Surface {
    void *pixels;
    int   w, h, pitch;
} SDL_Surface;

typedef void SDL_mutex;

#define SDL_INIT_VIDEO 0x00000020u
#define SDL_INIT_AUDIO 0x00000010u

#define SDL_GRAB_QUERY (-1)
#define SDL_GRAB_OFF   0
#define SDL_GRAB_ON    1

static inline int  SDL_WM_GrabInput(int mode) { (void)mode; return SDL_GRAB_OFF; }
static inline int  SDL_ShowCursor(int toggle) { (void)toggle; return 1; }
static inline void SDL_QuitSubSystem(unsigned f) { (void)f; }
static inline void SDL_Quit(void) { }

static inline SDL_mutex *SDL_CreateMutex(void) { return (SDL_mutex *)0; }
static inline void       SDL_DestroyMutex(SDL_mutex *m) { (void)m; }
static inline int        SDL_mutexP(SDL_mutex *m) { (void)m; return 0; }
static inline int        SDL_mutexV(SDL_mutex *m) { (void)m; return 0; }

#endif // DUKE3D_SDL_SHIM_H
