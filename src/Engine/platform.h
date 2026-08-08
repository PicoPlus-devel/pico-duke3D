#ifndef _INCLUDE_PLATFORM_H_
#define _INCLUDE_PLATFORM_H_

#if (defined PLATFORM_PICO)
    #include "pico_compat.h"
#elif (defined PLATFORM_WIN32)
    #include "win32_compat.h"
#elif (defined PLATFORM_UNIX)
    #include "unix_compat.h"
#elif (defined PLATFORM_DOS)
    #include "doscmpat.h"
#elif (defined __APPLE__)
    #include "macos_compat.h"
#else
#error Define your platform!
#endif

#if (!defined __EXPORT__)
    #define __EXPORT__
#endif

// Unrecoverable-error exit. Historically each site did "printf the reason;
// getchar(); exit(0)" — wait for the user to read it, then quit. pico_compat.h
// overrides this with a screen the user can actually see (see duke_fatal.c);
// every other platform keeps the original behaviour.
#if (!defined DUKE_FATAL_ABORT)
    #define DUKE_FATAL_ABORT() do { getchar(); exit(0); } while (0)
#endif

uint16_t _swap16(uint16_t D);
unsigned int _swap32(unsigned int D);
#if defined(PLATFORM_MACOSX) && defined(__ppc__)
#define PLATFORM_BIGENDIAN 1
#define BUILDSWAP_INTEL16(x) _swap16(x)
#define BUILDSWAP_INTEL32(x) _swap32(x)
#else
#define PLATFORM_LITTLEENDIAN 1
#define BUILDSWAP_INTEL16(x) (x)
#define BUILDSWAP_INTEL32(x) (x)
#endif

#endif  /* !defined _INCLUDE_PLATFORM_H_ */

/* end of platform.h ... */


