//
//  duke_fatfs_io.c — FatFs-backed file I/O for the Duke3D port.
//
//  Duke reaches the filesystem two different ways, and BOTH need backing:
//
//   1. POSIX wrappers (open/read/write/lseek/close). The engine's
//      filesystem.c reads the GRP and any external file through these, so
//      overriding them lets filesystem.c be used UNCHANGED.
//
//   2. newlib syscalls (_open/_read/_write/_lseek/_close/_fstat/_isatty),
//      which are what stdio's FILE* layer calls. The game uses fopen() for
//      everything it WRITES -- savegames (menues.c saveplayer -> dfwrite),
//      the config file (config.c CONFIG_WriteSetup, called from ShutDown),
//      scriplib, and the console startup script. The SDK's versions are weak
//      stubs that just return -1, so before this every one of those silently
//      failed: no saves, and settings were never persisted.
//
//  Both layers share one fd table, so an fd is valid through either. fds 0/1/2
//  stay with stdio: _write/_read delegate those to the UART exactly as the SDK
//  does, or printf would vanish into FatFs.
//
//  Paths are relative to the working directory set below (/roms/duke3d), which
//  is where saves land -- "game0.sav" next to DUKE3D.GRP. Nothing goes to
//  flash, so there is no in-flash save region to reserve and no SAVE_FLASH_BASE
//  floor for the image to collide with.
//
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

#include "pico/stdlib.h"
#include "pico/stdio.h"
#include "hardware/pio.h"
#include "ff.h"
#include "tf_card.h"
#include "duke_dostext.h"

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

// ---------------------------------------------------------------------------
// Shared fd table
// ---------------------------------------------------------------------------
static FIL *fil_for(int fd)
{
    int slot = fd - DUKE_FD_BASE;
    if (slot < 0 || slot >= DUKE_MAXFDS || !s_used[slot]) return NULL;
    return &s_fils[slot];
}

// POSIX open flags -> FatFs mode. fopen() maps its mode strings onto these, so
// covering the O_* combinations covers "rb"/"wb"/"a" and friends:
//   "rb" -> O_RDONLY                     "wb" -> O_WRONLY|O_CREAT|O_TRUNC
//   "r+" -> O_RDWR                       "ab" -> O_WRONLY|O_CREAT|O_APPEND
static BYTE fatfs_mode(int oflag)
{
    BYTE m = 0;
    switch (oflag & O_ACCMODE) {
        case O_WRONLY: m = FA_WRITE; break;
        case O_RDWR:   m = FA_READ | FA_WRITE; break;
        default:       m = FA_READ; break;
    }
    if (oflag & O_CREAT) {
        if      (oflag & O_EXCL)  m |= FA_CREATE_NEW;
        else if (oflag & O_TRUNC) m |= FA_CREATE_ALWAYS;
        else                      m |= FA_OPEN_ALWAYS;
    } else if (oflag & O_TRUNC) {
        m |= FA_CREATE_ALWAYS;
    } else {
        m |= FA_OPEN_EXISTING;
    }
    if (oflag & O_APPEND) m |= FA_OPEN_APPEND;
    return m;
}

static int duke_open(const char *path, int oflag)
{
    int slot = -1;
    for (int i = 0; i < DUKE_MAXFDS; i++) {
        if (!s_used[i]) { slot = i; break; }
    }
    if (slot < 0) { errno = EMFILE; return -1; }
    if (f_open(&s_fils[slot], strip_dotslash(path), fatfs_mode(oflag)) != FR_OK) {
        errno = ENOENT;
        return -1;
    }
    s_used[slot] = 1;
    return slot + DUKE_FD_BASE;
}

static int duke_close(int fd)
{
    FIL *f = fil_for(fd);
    if (!f) return -1;
    FRESULT fr = f_close(f);
    s_used[fd - DUKE_FD_BASE] = 0;
    return fr == FR_OK ? 0 : -1;
}

static _ssize_t duke_read(int fd, void *buf, size_t nbyte)
{
    FIL *f = fil_for(fd);
    if (!f) return -1;
    UINT br = 0;
    if (f_read(f, buf, (UINT)nbyte, &br) != FR_OK) return -1;
    return (_ssize_t)br;
}

static _ssize_t duke_write(int fd, const void *buf, size_t nbyte)
{
    FIL *f = fil_for(fd);
    if (!f) return -1;
    UINT bw = 0;
    if (f_write(f, buf, (UINT)nbyte, &bw) != FR_OK) return -1;
    // A short write means the card filled up; report it rather than pretending.
    return (_ssize_t)bw;
}

static off_t duke_lseek(int fd, off_t offset, int whence)
{
    FIL *f = fil_for(fd);
    if (!f) return -1;
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

// Everything written to stdout/stderr goes to the UART as before AND, while the
// game is still starting up, to the on-screen DOS console (duke_dostext.c). That
// is the whole mechanism behind the startup screen: in DOS this text WAS the
// screen, so mirroring stdout reproduces it without touching game code.
static int console_write(const char *buf, int len)
{
    duke_dostext_write(buf, (size_t)len);
    return stdio_put_string(buf, len, false, true);
}

// ---------------------------------------------------------------------------
// Layer 1: POSIX wrappers — what the engine's filesystem.c calls directly.
// ---------------------------------------------------------------------------
int open(const char *path, int oflag, ...)      { return duke_open(path, oflag); }
int close(int fd)                               { return duke_close(fd); }
_ssize_t read(int fd, void *buf, size_t n)      { return duke_read(fd, buf, n); }
_ssize_t write(int fd, const void *buf, size_t n)
{
    // Keep the stdio handles working even through the POSIX name.
    if (fd == 1 || fd == 2) return console_write((const char *)buf, (int)n);
    return duke_write(fd, buf, n);
}
off_t lseek(int fd, off_t off, int whence)      { return duke_lseek(fd, off, whence); }

// Duke removes its temp file on the way out (gameexit: unlink("duke3d.tmp")),
// and the config writer replaces files in place.
int unlink(const char *path)
{
    return f_unlink(strip_dotslash(path)) == FR_OK ? 0 : -1;
}

int rename(const char *from, const char *to)
{
    return f_rename(strip_dotslash(from), strip_dotslash(to)) == FR_OK ? 0 : -1;
}

// ---------------------------------------------------------------------------
// Layer 2: newlib syscalls — what stdio's FILE* layer calls. These override
// the SDK's weak stubs. fds 0/1/2 are delegated to stdio exactly as the SDK
// does it, so printf and friends behave identically.
// ---------------------------------------------------------------------------
int _open(const char *fn, int oflag, ...)       { return duke_open(fn, oflag); }
int _close(int fd)                              { return duke_close(fd); }

int _read(int handle, char *buffer, int length)
{
    if (handle == 0) return stdio_get_until(buffer, length, at_the_end_of_time);
    return (int)duke_read(handle, buffer, (size_t)length);
}

int _write(int handle, char *buffer, int length)
{
    if (handle == 1 || handle == 2) return console_write(buffer, length);
    return (int)duke_write(handle, buffer, (size_t)length);
}

off_t _lseek(int fd, off_t pos, int whence)     { return duke_lseek(fd, pos, whence); }

// stdio consults these to pick a buffering strategy and to answer ftell/fseek
// on some paths. Reporting our files as regular with a real size (rather than
// the SDK stub's blanket -1) is what lets fread/fseek behave normally.
int _fstat(int fd, struct stat *buf)
{
    if (!buf) return -1;
    memset(buf, 0, sizeof(*buf));
    if (fd >= 0 && fd <= 2) { buf->st_mode = S_IFCHR; return 0; }
    FIL *f = fil_for(fd);
    if (!f) return -1;
    buf->st_mode = S_IFREG;
    buf->st_size = (off_t)f_size(f);
    return 0;
}

int _isatty(int fd)
{
    // Only the console is a tty. Answering 1 for a real file would make stdio
    // line-buffer it, which turns a savegame write into a syscall per newline.
    return (fd >= 0 && fd <= 2) ? 1 : 0;
}
