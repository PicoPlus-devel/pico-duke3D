// pico_bootcfg.h -- the HDMI or DVI display mode a port starts in.
//
// The same file is used by pico-quake2, pico-duke3D, pico-doom and pico-lutionX;
// keep the copies identical.
//
// The HSTX output runs in HDMI mode (with data-island audio) or DVI mode (real
// DVI 1.0, no audio on the cable). The mode a port starts in comes from:
//
//   1. /boot.txt, key DISPLAY=HDMI|DVI, when the port was started by
//      pico-bootLoader: the port then follows the bootloader's own setting.
//   2. Otherwise the port's own setting (PICO_DISPLAY_*), stored in its
//      settings file and changed from its options menu.
//   3. Otherwise the board default: DVI on the Murmulator M2 (HW_CONFIG 13),
//      HDMI everywhere else. pico_shared's resetsettings() and pico-bootLoader
//      use the same default.

#ifndef PICO_BOOTCFG_H
#define PICO_BOOTCFG_H

#include <stdbool.h>
#include <stdint.h>

#include "ff.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(HW_CONFIG) && HW_CONFIG == 13
#define PICO_BOARD_DEFAULT_DVI	1
#else
#define PICO_BOARD_DEFAULT_DVI	0
#endif

// The stored display setting. 0 is what a zeroed record reads as.
#define PICO_DISPLAY_DEFAULT	0	// the board default
#define PICO_DISPLAY_HDMI		1
#define PICO_DISPLAY_DVI		2

// True when pico-bootLoader started this image. Only bootloader builds can be,
// so this is always false in a standalone build.
bool pico_bootcfg_from_loader (void);

// DISPLAY= from /boot.txt: 1 for DVI, 0 for HDMI, -1 when the file or the key
// is absent or the value is not one of the two. `fil` is a FIL the caller owns
// (a FIL is ~600 bytes, too big for some stacks); it is closed again on return.
// NULL (no usable card) also gives -1.
int pico_bootcfg_display (FIL *fil);

// The mode to start in: /boot.txt when started by the bootloader, else the
// stored setting (PICO_DISPLAY_*), else the board default.
bool pico_bootcfg_boot_dvi (int stored, FIL *fil);

// The stored setting for a mode the user picked.
static inline int8_t pico_bootcfg_stored (bool dvi)
{
	return dvi ? PICO_DISPLAY_DVI : PICO_DISPLAY_HDMI;
}

#ifdef __cplusplus
}
#endif

#endif // PICO_BOOTCFG_H
