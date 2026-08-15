//
//  duke_boot.h — the ways out of a running game.
//
//  duke_boot.c is the RP2350 entry point; the only thing it exposes is the
//  reset below. Quitting takes the other path, _exit() in duke_boot.c, which
//  the SDK reaches on its own — nothing needs to declare that one.
//
#ifndef DUKE_BOOT_H
#define DUKE_BOOT_H

#ifdef __cplusplus
extern "C" {
#endif

// Reset into the chip's ROM bootloader, so the board comes up as the RP2350 UF2
// drive and can be reflashed without holding BOOT while replugging. This is the
// OPTIONS menu's "BOOTSEL MODE" item.
//
// A different destination from _exit()'s watchdog_reboot(), which restarts Duke
// (or lands back in the pico-bootLoader picker on a BUILD_FOR_BOOTLOADER image).
// The ROM bootloader runs before anything in flash, so this is correct for both
// variants and needs no BUILD_FOR_BOOTLOADER conditional.
//
// Blanks the LEDs and hands the Wii I2C bus back, exactly as _exit() does.
// Writing duke3d.cfg is the CALLER's job — see the call site in menues.c; this
// file deliberately pulls in no engine headers. Never returns.
void __attribute__((noreturn)) duke_reboot_to_bootsel(void);

#ifdef __cplusplus
}
#endif

#endif // DUKE_BOOT_H
