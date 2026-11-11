// duke_settings.c -- the port's own settings, in /settings_DUKE3D.dat on the SD card.
//
// So far only the display mode (HDMI or DVI). Everything else the engine keeps in
// duke3d.cfg, which it reads long after the display has started; the display
// mode is needed before that, so it has this file, which
// duke_fatfs_init reads right after mounting the card.
//
// The format follows pico-doom's /settings_DOOM.dat (and pico_shared's
// /settings_<TAG>.dat): a fixed-size record with a leading version, written raw
// and accepted on read only with the exact size and version. Anything else
// leaves the defaults.

#include <stdio.h>
#include <string.h>

#include "ff.h"

#include "pico_bootcfg.h"
#include "duke_settings.h"

#define DUKE_SETTINGS_PATH	"/settings_DUKE3D.dat"
#define DUKE_SETTINGS_VERSION	1

// On-disk layout: fixed-width types only.
typedef struct
{
	uint16_t	version;
	int8_t		display;		// PICO_DISPLAY_*: 0 board default, 1 HDMI, 2 DVI
	int8_t		reserved[13];	// zeroed; bump the version before using any
} duke_settings_t;

_Static_assert (sizeof(duke_settings_t) == 16, "the settings record is an on-disk format");

static FIL				fil;		// also lends pico_bootcfg_display its FIL
static duke_settings_t	stored;		// the record as it is in the file
static bool				boot_dvi = PICO_BOARD_DEFAULT_DVI;

void duke_settings_load (void)
{
	duke_settings_t	rec;
	UINT			got = 0;
	FRESULT			fr;

	memset (&stored, 0, sizeof(stored));
	stored.version = DUKE_SETTINGS_VERSION;

	fr = f_open (&fil, DUKE_SETTINGS_PATH, FA_READ);
	if (fr == FR_OK)
	{
		if (f_size (&fil) == sizeof(rec))
			fr = f_read (&fil, &rec, sizeof(rec), &got);
		f_close (&fil);
		if (fr == FR_OK && got == sizeof(rec) && rec.version == DUKE_SETTINGS_VERSION)
		{
			stored = rec;
			if (stored.display < PICO_DISPLAY_DEFAULT || stored.display > PICO_DISPLAY_DVI)
				stored.display = PICO_DISPLAY_DEFAULT;
		}
		else
			printf ("duke_settings: " DUKE_SETTINGS_PATH " is not a version %d record, using defaults\n",
				DUKE_SETTINGS_VERSION);
	}

	boot_dvi = pico_bootcfg_boot_dvi (stored.display, &fil);
	printf ("duke_settings: display %s (stored %d, board default %s)\n", boot_dvi ? "DVI" : "HDMI",
		stored.display, PICO_BOARD_DEFAULT_DVI ? "DVI" : "HDMI");
}

bool duke_settings_boot_dvi (void)
{
	return boot_dvi;
}

void duke_settings_save_display (bool dvi)
{
	duke_settings_t	rec = stored;
	UINT			put = 0;
	FRESULT			fr;

	rec.display = pico_bootcfg_stored (dvi);
	if (rec.display == stored.display)
		return;
	fr = f_open (&fil, DUKE_SETTINGS_PATH, FA_WRITE | FA_CREATE_ALWAYS);
	if (fr == FR_OK)
	{
		fr = f_write (&fil, &rec, sizeof(rec), &put);
		if (f_close (&fil) != FR_OK && fr == FR_OK)
			fr = FR_DISK_ERR;
	}
	// Taken over either way: a write-protected card fails once, not on every change.
	stored = rec;
	if (fr != FR_OK || put != sizeof(rec))
		printf ("duke_settings: could not write " DUKE_SETTINGS_PATH " (FatFs error %d)\n", fr);
	else
		printf ("duke_settings: display %s saved\n", dvi ? "DVI" : "HDMI");
}
