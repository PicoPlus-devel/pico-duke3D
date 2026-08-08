//
//  duke_wiipad.h — Wii-extension controllers over I2C (NES Classic Mini, SNES
//  Classic Mini, Wii Classic Controller (Pro)), on top of the vendored
//  pico_shared wiipad driver (3rdparty/pico_shared_drivers/wiipad). Adds the
//  three things the bare driver leaves to its caller: an early init that is safe
//  on a bus shared with the TLV320 codec, rate limiting, and hot-plug retries.
//
//  Ported from pico-doom's src/pico/doom_wiipad.{cpp,h}, which in turn ports
//  pico-infonesPlus (initVintageControllers in pico_shared/FrensHelpers.cpp and
//  the reconnect retry in pico_shared/menu.cpp).
//
//  This header is deliberately C-callable and does NOT include wiipad.h, which
//  is a C++ header: the call sites are duke_boot.c (C) and duke_usb_input.cpp.
//
#ifndef DUKE_WIIPAD_H
#define DUKE_WIIPAD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Board pins, set in <tag>_cflags.h. -1 = the board has no Wii extension port,
// same convention as NES_PIN_CLK and DUKE_LED_PIN.
#ifndef WII_PIN_SDA
#define WII_PIN_SDA -1
#endif
#ifndef WII_PIN_SCL
#define WII_PIN_SCL -1
#endif

#if PICO_ON_DEVICE && WII_PIN_SDA >= 0 && WII_PIN_SCL >= 0

// Bring up the pad. Call once, EARLY — before anything touches the codec,
// because on the TLV320 boards it sits on the same SDA/SCL as the pad and an
// uninitialised SNES-Classic pad wedges the bus for it. See the call site in
// duke_boot.c's main(); the codec's own i2c_init is much later, inside
// DSL_Init -> audio_i2s_setup -> tlv320_init.
void duke_wiipad_init(void);

// Latest button state, in the driver's own layout: bit0=A, 1=B, 2=Select,
// 3=Start, 4=Up, 5=Down, 6=Left, 7=Right, 8=X, 9=Y, 10=L, 11=R. Rate-limited
// internally, so it is safe to call as fast as the caller likes; also retries
// the connect handshake so a pad plugged in after boot still works.
uint16_t duke_wiipad_read(void);

// Release the bus and the pins. For the reset in duke_boot.c's _exit(): leaving
// the pad initialised across a watchdog reset has been seen to hang the next
// boot (pico-infonesPlus does the same before its own reboot).
void duke_wiipad_shutdown(void);

#else

#define duke_wiipad_init() ((void)0)
#define duke_wiipad_read() ((uint16_t)0)
#define duke_wiipad_shutdown() ((void)0)

#endif

#ifdef __cplusplus
}
#endif

#endif // DUKE_WIIPAD_H
