//
//  dirent.h — no-op POSIX directory-iteration shim for the bare-metal port.
//
//  newlib on arm-none-eabi has no <dirent.h> (its sys/dirent.h #errors). The
//  Duke code uses opendir/readdir for GRP auto-discovery and save-game
//  browsing, neither of which the RP2350 port needs (the GRP is opened by a
//  fixed SD path). These stubs make that code compile; opendir() returns NULL
//  so every scan simply finds nothing. Real FatFs-backed enumeration can be
//  added later if a file browser is wanted.
//
#ifndef DUKE3D_DIRENT_SHIM_H
#define DUKE3D_DIRENT_SHIM_H

struct dirent {
    char           d_name[256];
    unsigned short d_namlen;
};

typedef struct DIR DIR;

DIR           *opendir(const char *name);
struct dirent *readdir(DIR *dir);
int            closedir(DIR *dir);
void           rewinddir(DIR *dir);

#endif // DUKE3D_DIRENT_SHIM_H
