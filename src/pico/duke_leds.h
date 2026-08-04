//
//  duke_leds.h — status output on whatever LEDs the board carries: a heartbeat
//  on the plain onboard LED, and on the Fruit Jam a 5-pixel WS2812 audio VU
//  meter. Ported from pico-doom's doom_leds.{c,h}, which in turn ports
//  pico-infonesPlus (pico_shared/vumeter.cpp and Frens::blinkLed in
//  pico_shared/FrensHelpers.cpp).
//
#ifndef DUKE_LEDS_H
#define DUKE_LEDS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Board pins, set in <tag>_cflags.h. -1 = the board does not have it, same
// convention as NES_PIN_CLK and WII_PIN_SDA.
#ifndef DUKE_LED_PIN
#define DUKE_LED_PIN -1
#endif
#ifndef DUKE_VU_WS2812_PIN
#define DUKE_VU_WS2812_PIN -1
#endif

#if PICO_ON_DEVICE

// Claim the pins. Call once, LATE: after the final clk_sys (the WS2812 clock
// divider is computed from it, once) and after every other PIO consumer has
// claimed, so a failure here means "nothing was free" rather than "we took
// audio's state machine". See the call site in game.c, just after Startup().
void duke_leds_init(void);

// Per-frame housekeeping: advance the heartbeat and repaint the VU meter.
// Core0 only — it is the only place the PIO FIFO is written. Called from
// _nextpage() with the game-frame counter.
void duke_leds_frame(unsigned frame);

// LED off, strip dark. For the exit path in duke_boot.c, where
// duke_leds_frame() stops being called but the pixels keep whatever they were
// last given.
void duke_leds_off(void);

#if DUKE_VU_WS2812_PIN >= 0
// Feed the VU meter one mix division (int16 stereo interleaved, `frames`
// frames). Called from the audio pump, so it runs on core1; see duke_leds.c for
// why the hand-off to duke_leds_frame() needs no lock.
void duke_vu_add_chunk(const int16_t *stereo, unsigned frames);
#else
#define duke_vu_add_chunk(stereo, frames) ((void)(stereo), (void)(frames))
#endif

#else

#define duke_leds_init() ((void)0)
#define duke_leds_frame(frame) ((void)(frame))
#define duke_leds_off() ((void)0)
#define duke_vu_add_chunk(stereo, frames) ((void)(stereo), (void)(frames))

#endif

#ifdef __cplusplus
}
#endif

#endif // DUKE_LEDS_H
