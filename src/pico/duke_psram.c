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
// Top of the usable heap: __psram_heap_end clamped to the part actually
// fitted. See duke_psram_init.
static char *s_psram_limit;

void duke_psram_init(void)
{
    int32_t sz = SetupPsram(PSRAM_CS_PIN);   // PSRAM_CS_PIN from the board cflags
    if (sz <= 0) {
        printf("duke_psram: NO PSRAM on GPIO %d — cannot run\n", PSRAM_CS_PIN);
        return;
    }
    // The linker's PSRAM region (DUKE_PSRAM_SIZE, 8 MB by default) is an
    // address-space declaration, not a claim about the fitted part: boards in
    // this family carry different PSRAM. Clamp the heap top to what SetupPsram
    // actually mapped so a smaller part is a smaller tile cache — which
    // tiles.c already handles, retrying 64 KB smaller until psram_alloc
    // succeeds — instead of writes that fall off the end of the chip and alias
    // back over the engine's arrays.
    char *const detected_end = (char *)__psram_bss_start + sz;
    s_psram_limit = (detected_end < __psram_heap_end) ? detected_end : __psram_heap_end;

    size_t bss_len = (size_t)(__psram_bss_end - __psram_bss_start);
    printf("duke_psram: %ld KB PSRAM; zeroing %u KB relocated bss at %p\n",
           (long)(sz / 1024), (unsigned)(bss_len / 1024), (void *)__psram_bss_start);
    if (s_psram_limit < __psram_bss_end) {
        // Not even the relocated .bss fits. Nothing good happens past here:
        // zeroing it would wrap, and the engine would run on arrays that alias
        // each other. Say so plainly rather than crash somewhere downstream.
        printf("duke_psram: FATAL — need %u KB for the engine arrays, chip has %ld KB\n",
               (unsigned)(bss_len / 1024), (long)(sz / 1024));
        return;
    }
    memset(__psram_bss_start, 0, bss_len);
    s_psram_brk = __psram_heap_start;
    printf("duke_psram: heap %p..%p (%u KB)\n", (void *)__psram_heap_start,
           (void *)s_psram_limit,
           (unsigned)((s_psram_limit - __psram_heap_start) / 1024));
}

// 32-byte-aligned bump allocator over the PSRAM heap. Permanent allocations
// only (no free) — used for the cache1d tile cache.
void *psram_alloc(size_t n)
{
    n = (n + 31u) & ~(size_t)31u;
    if (s_psram_brk == NULL || s_psram_brk + n > s_psram_limit) {
        return NULL;
    }
    void *p = s_psram_brk;
    s_psram_brk += n;
    return p;
}
