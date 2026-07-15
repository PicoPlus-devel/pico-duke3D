#ifndef SETUP_PSRAM_H
#define SETUP_PSRAM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the APS6404-style QSPI PSRAM on the RP2350 QMI chip-select 1.
// On success the PSRAM is memory-mapped (cached) at 0x11000000, with a
// non-cached alias at 0x15000000, and the window is writable.
// Returns the detected size in bytes (1/2/4/8 MiB), or 0 if no PSRAM found.
// Upstream declares this in PicoPlusPsram.h; this header is local to the
// trimmed vendored copy (see CMakeLists.txt).
int32_t SetupPsram(int psramCS);

#ifdef __cplusplus
}
#endif

#endif // SETUP_PSRAM_H
