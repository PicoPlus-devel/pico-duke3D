//
//  duke_platform_stub.c — remaining bare-metal POSIX shims for the Duke3D port.
//
//  Video/palette/timer/input now live in pico_display.c; this file keeps only
//  the small POSIX-ish stubs the engine needs that have no home yet: a
//  filelength() (filesystem.c only defines it on __linux__/__APPLE__) and the
//  directory-enumeration shims (no SD file browser yet).
//
//  mkdir() used to be a no-op here. It is now backed by f_mkdir over in
//  duke_fatfs_io.c, with the rest of the filesystem-backed POSIX calls.
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

// dirent shim implementations (no directory enumeration on the SD yet).
DIR           *opendir(const char *name) { (void)name; return 0; }
struct dirent *readdir(DIR *dir) { (void)dir; return 0; }
int            closedir(DIR *dir) { (void)dir; return 0; }
void           rewinddir(DIR *dir) { (void)dir; }
