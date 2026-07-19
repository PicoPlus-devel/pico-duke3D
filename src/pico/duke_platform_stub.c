//
//  duke_platform_stub.c — remaining bare-metal POSIX shims for the Duke3D port.
//
//  Video/palette/timer/input now live in pico_display.c; this file keeps only
//  the small POSIX-ish stubs the engine needs that have no home yet: a
//  filelength() (filesystem.c only defines it on __linux__/__APPLE__), a no-op
//  mkdir(), and the directory-enumeration shims (no SD file browser yet).
//
#include <stdint.h>
#include <unistd.h>
#include <sys/stat.h>

#include "dirent.h"   // shim (src/pico/shim)

// filesystem.c only defines filelength() on __linux__/__APPLE__.
int32_t filelength(int32_t fd)
{
    long cur = lseek(fd, 0, SEEK_CUR);
    long end = lseek(fd, 0, SEEK_END);
    lseek(fd, cur, SEEK_SET);
    return (int32_t)end;
}

// mkdir: newlib declares it (2-arg) but has no syscall; no-op for now.
int mkdir(const char *path, mode_t mode) { (void)path;(void)mode; return 0; }

// dirent shim implementations (no directory enumeration on the SD yet).
DIR           *opendir(const char *name) { (void)name; return 0; }
struct dirent *readdir(DIR *dir) { (void)dir; return 0; }
int            closedir(DIR *dir) { (void)dir; return 0; }
void           rewinddir(DIR *dir) { (void)dir; }
