//
//  duke_fatfs_io.c — FatFs-backed POSIX file I/O for the Duke3D port.
//
//  Chocolate Duke3D's filesystem.c reads the GRP (and any external files)
//  through plain POSIX open/read/lseek/close. We override those four libc
//  wrappers with FatFs-backed versions, so filesystem.c is used UNCHANGED.
//  stdio (printf) is unaffected: it goes through the _write/_read *syscalls*,
//  not these POSIX wrappers.
//
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "ff.h"
#include "tf_card.h"

#define DUKE_MAXFDS  16
#define DUKE_FD_BASE 3    // keep clear of stdio fds 0/1/2

static FIL     s_fils[DUKE_MAXFDS];
static uint8_t s_used[DUKE_MAXFDS];
static FATFS   s_fs;

// Mount the SD card and cd into the Duke asset directory. Called from
// duke_boot before Duke3D_main (which opens the GRP first thing).
void duke_fatfs_init(void)
{
    static pico_fatfs_spi_config_t cfg = {
        SDCARD_SPI, CLK_SLOW_DEFAULT, CLK_FAST_DEFAULT_PIO,
        SD_RX, SD_CS, SD_SCK, SD_TX, true
    };
    if (pico_fatfs_set_config(&cfg)) {
        printf("duke_fatfs: hardware SPI%d\n", spi_get_index(SDCARD_SPI));
    } else {
        pico_fatfs_config_spi_pio(SDCARD_PIO, pio_claim_unused_sm(SDCARD_PIO, true));
        printf("duke_fatfs: PIO-SPI fallback\n");
    }
    FRESULT fr = f_mount(&s_fs, "", 1);
    if (fr != FR_OK) {
        printf("duke_fatfs: MOUNT FAILED (%d) — SD inserted & FAT-formatted?\n", fr);
        return;
    }
    fr = f_chdir("/roms/duke3d");
    printf("duke_fatfs: mounted; chdir /roms/duke3d -> %s\n",
           fr == FR_OK ? "OK" : "MISSING (put DUKE3D.GRP there)");
}

static const char *strip_dotslash(const char *p)
{
    if (p && p[0] == '.' && (p[1] == '/' || p[1] == '\\')) return p + 2;
    return p;
}

int open(const char *path, int oflag, ...)
{
    (void)oflag;   // read-only for now (M2: title screen). Saves come later.
    int slot = -1;
    for (int i = 0; i < DUKE_MAXFDS; i++) {
        if (!s_used[i]) { slot = i; break; }
    }
    if (slot < 0) return -1;
    if (f_open(&s_fils[slot], strip_dotslash(path), FA_READ) != FR_OK) return -1;
    s_used[slot] = 1;
    return slot + DUKE_FD_BASE;
}

int close(int fd)
{
    int slot = fd - DUKE_FD_BASE;
    if (slot < 0 || slot >= DUKE_MAXFDS || !s_used[slot]) return -1;
    f_close(&s_fils[slot]);
    s_used[slot] = 0;
    return 0;
}

_ssize_t read(int fd, void *buf, size_t nbyte)
{
    int slot = fd - DUKE_FD_BASE;
    if (slot < 0 || slot >= DUKE_MAXFDS || !s_used[slot]) return -1;
    UINT br = 0;
    if (f_read(&s_fils[slot], buf, (UINT)nbyte, &br) != FR_OK) return -1;
    return (_ssize_t)br;
}

off_t lseek(int fd, off_t offset, int whence)
{
    int slot = fd - DUKE_FD_BASE;
    if (slot < 0 || slot >= DUKE_MAXFDS || !s_used[slot]) return -1;
    FIL *f = &s_fils[slot];
    FSIZE_t pos;
    switch (whence) {
        case SEEK_SET: pos = (FSIZE_t)offset; break;
        case SEEK_CUR: pos = f_tell(f) + offset; break;
        case SEEK_END: pos = f_size(f) + offset; break;
        default: return -1;
    }
    if (f_lseek(f, pos) != FR_OK) return -1;
    return (off_t)pos;
}
