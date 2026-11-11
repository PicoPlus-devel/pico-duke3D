// pico_bootcfg.c -- the HDMI or DVI display mode a port starts in (see pico_bootcfg.h).
//
// The same file is used by pico-quake2, pico-duke3D, pico-doom and pico-lutionX;
// keep the copies identical. Also compiled by the host harnesses, which do not
// build for the bootloader.

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

#if defined(BUILD_FOR_BOOTLOADER) && BUILD_FOR_BOOTLOADER
#include "hardware/structs/watchdog.h"
#endif

#include "pico_bootcfg.h"

// pico-bootLoader's handshake: it writes LOADER_LAUNCH_MAGIC to watchdog
// scratch[6] just before it jumps to the application (pico_shared
// FrensHelpers.cpp, markLaunchedFromBootloader).
#define LOADER_LAUNCH_MAGIC		0xB007ED01u
#define LOADER_LAUNCH_SCRATCH	6

#define BOOT_TXT				"/boot.txt"

bool pico_bootcfg_from_loader (void)
{
#if defined(BUILD_FOR_BOOTLOADER) && BUILD_FOR_BOOTLOADER
	return watchdog_hw->scratch[LOADER_LAUNCH_SCRATCH] == LOADER_LAUNCH_MAGIC;
#else
	// The scratch registers survive a watchdog reset, so a standalone image
	// could still see the magic of an earlier bootloader session. Only an image
	// linked for the bootloader's app partition can have been started by it.
	return false;
#endif
}

static char *trim (char *s)
{
	char	*end;

	while (*s && isspace ((unsigned char)*s))
		s++;
	end = s + strlen (s);
	while (end > s && isspace ((unsigned char)end[-1]))
		*--end = '\0';
	return s;
}

// The bootloader's rules (pico-bootLoader src/sd_boot_ini.c): one KEY=VALUE per
// line, blank lines and lines starting with # or ; ignored, key and value
// trimmed and compared without regard to case. Anything else in the file is the
// bootloader's business; only DISPLAY is looked at here.
int pico_bootcfg_display (FIL *fil)
{
	char	line[96];
	bool	tail = false;
	int		result = -1;

	if (!fil || f_open (fil, BOOT_TXT, FA_READ) != FR_OK)
		return -1;
	while (f_gets (line, sizeof(line), fil))
	{
		size_t	n = strlen (line);
		bool	in_tail = tail;
		char	*key, *val, *eq;

		// A line longer than the buffer arrives in pieces; only its first
		// piece is a line start.
		tail = n == sizeof(line) - 1 && line[n - 1] != '\n';
		if (in_tail)
			continue;
		key = trim (line);
		if (key[0] == '\0' || key[0] == '#' || key[0] == ';')
			continue;
		eq = strchr (key, '=');
		if (!eq)
			continue;
		*eq = '\0';
		key = trim (key);
		val = trim (eq + 1);
		if (strcasecmp (key, "DISPLAY") != 0)
			continue;
		if (strcasecmp (val, "DVI") == 0)
			result = 1;
		else if (strcasecmp (val, "HDMI") == 0)
			result = 0;
		break;
	}
	f_close (fil);
	return result;
}

bool pico_bootcfg_boot_dvi (int stored, FIL *fil)
{
	if (pico_bootcfg_from_loader ())
	{
		int	loader = pico_bootcfg_display (fil);

		printf ("display: started by pico-bootLoader, " BOOT_TXT " DISPLAY=%s\n",
			loader < 0 ? "(not set)" : loader ? "DVI" : "HDMI");
		return loader < 0 ? PICO_BOARD_DEFAULT_DVI : loader;
	}
	if (stored == PICO_DISPLAY_HDMI)
		return false;
	if (stored == PICO_DISPLAY_DVI)
		return true;
	return PICO_BOARD_DEFAULT_DVI;
}
