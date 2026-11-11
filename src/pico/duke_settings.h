// duke_settings.h -- the port's own settings on the SD card (see duke_settings.c).

#ifndef DUKE_SETTINGS_H
#define DUKE_SETTINGS_H

#include <stdbool.h>

// Reads /settings_DUKE3D.dat and decides the display mode to start in. Called by
// duke_fatfs_init right after the card is mounted, before the display starts.
void	duke_settings_load (void);

// The display mode to start in: true for DVI. The board default until loaded.
bool	duke_settings_boot_dvi (void);

// Stores the display mode picked in the menu (written at once if it changed).
void	duke_settings_save_display (bool dvi);

#endif
