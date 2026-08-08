//
//  duke_fatal.h — put fatal errors on the HDMI screen instead of only the UART.
//
#ifndef DUKE_FATAL_H
#define DUKE_FATAL_H

#ifdef __cplusplus
extern "C" {
#endif

// Show a fatal error on the DOS console (bringing the display up first if
// needed) and halt. Never returns. The message wraps at 40 columns.
void duke_fatal(const char *fmt, ...)
    __attribute__((noreturn, format(printf, 1, 2)));

// Same, for call sites that have already printf'd their reason: adds the banner
// and the footer over whatever is already on screen. Never returns.
void duke_fatal_halt(void) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif

#endif
