//
//  duke_psram.c — PSRAM bring-up for the Duke3D image.
//
//  The linker (memmap_duke.ld) relocates all of libduke.a's .bss into a
//  .psram_bss section at the PSRAM XIP window (0x11000000). That region is
//  outside the SRAM __bss_start__..__bss_end__ range, so crt0 never zeroes it;
//  we must do so here, AFTER SetupPsram() maps the PSRAM. The space above the
//  relocated bss (__psram_heap_start..__psram_heap_end) is a simple bump heap
//  for the cache1d tile cache and other large runtime allocations.
//
#include <stddef.h>
#include <string.h>
#include <stdio.h>

#include "SetupPsram.h"

// Provided by memmap_duke.ld.
extern char __psram_bss_start[];
extern char __psram_bss_end[];
extern char __psram_heap_start[];
extern char __psram_heap_end[];

static char *s_psram_brk;

void duke_psram_init(void)
{
    int32_t sz = SetupPsram(PSRAM_CS_PIN);   // PSRAM_CS_PIN from fruitjam_cflags.h
    if (sz <= 0) {
        printf("duke_psram: NO PSRAM on GPIO %d — cannot run\n", PSRAM_CS_PIN);
        return;
    }
    size_t bss_len = (size_t)(__psram_bss_end - __psram_bss_start);
    printf("duke_psram: %ld KB PSRAM; zeroing %u KB relocated bss at %p\n",
           (long)(sz / 1024), (unsigned)(bss_len / 1024), (void *)__psram_bss_start);
    memset(__psram_bss_start, 0, bss_len);
    s_psram_brk = __psram_heap_start;
    printf("duke_psram: heap %p..%p (%u KB)\n", (void *)__psram_heap_start,
           (void *)__psram_heap_end,
           (unsigned)((__psram_heap_end - __psram_heap_start) / 1024));
}

// 32-byte-aligned bump allocator over the PSRAM heap. Permanent allocations
// only (no free) — used for the cache1d tile cache.
void *psram_alloc(size_t n)
{
    n = (n + 31u) & ~(size_t)31u;
    if (s_psram_brk == NULL || s_psram_brk + n > __psram_heap_end) {
        return NULL;
    }
    void *p = s_psram_brk;
    s_psram_brk += n;
    return p;
}
