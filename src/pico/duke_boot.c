//
//  duke_boot.c — RP2350 entry point for the Duke3D game image.
//
//  Brings up the hardware (clocks 378 MHz + 126 MHz clk_hstx, PSRAM, SD) then
//  calls the renamed Duke entry Duke3D_main(). Video/audio come up inside
//  Duke's own init (_platform_init -> pico_display). Clock sequence is the
//  proven Fruit Jam path shared with the M0 bring-up (pico_main.c), with the
//  clk_hstx source branching on the board's USB transport — see setup_clocks.
//
#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/pll.h"
#include "hardware/vreg.h"
#include "hardware/uart.h"
#include "hardware/watchdog.h"
#include "hardware/structs/qmi.h"

#include "duke_dostext.h"
#include "duke_fatal.h"

// ---------------------------------------------------------------------------
// Hardfault breadcrumb (system-freeze diagnosis). Raw UART register writes —
// no stdio, no locks — so it works from any context on either core. If the
// freeze is a crash, this prints "!HF sp=... pc=... lr=..."; if NOTHING
// appears and the system is frozen, it's a bus-level wedge (nothing executes,
// not even the fault handler) — a decisive distinction.
//
// NO_USE_UART boards (Murmulator M2, whose GPIO 0/1 carry the Wii connector)
// have no console at all, and uart0 is never taken out of reset there — poking
// its registers would fault inside the fault handler. The handler stays
// installed so a debugger still lands on a known symbol, it just has nothing
// to print to.
// ---------------------------------------------------------------------------
#if NO_USE_UART
#define hf_putc(c)  ((void)(c))
#else
#define hf_putc(c)  uart_putc_raw(uart0, (c))
#endif

static void hf_puthex(uint32_t v)
{
    for (int i = 28; i >= 0; i -= 4) {
        int d = (v >> i) & 0xf;
        hf_putc(d < 10 ? ('0' + d) : ('a' + d - 10));
    }
}

static void hf_puts(const char *p) { for (; *p; p++) hf_putc(*p); }
static void hf_field(const char *name, uint32_t v) { hf_puts(name); hf_puthex(v); }

// The exception frame is [r0 r1 r2 r3 r12 lr pc xpsr] at the SP in use when the
// fault was taken. Reading MSP from inside a normal C function gets this WRONG:
// the compiler's prologue has already pushed registers, so the frame is not at
// MSP and lr/pc come out as neighbouring garbage. That is not hypothetical -- it
// had this handler reporting pc=0x00000040 for a fault whose real PC was
// 0x1002d3c2, which sent a debugging session chasing a wild branch that never
// happened. Hence the naked trampoline: nothing is pushed, so r0 is the frame.
//
// Also dump the fault status registers. CFSR/HFSR say WHAT went wrong in one
// read (0x01000000 = UFSR UNALIGNED, 0x00000100 = BFSR IBUSERR, 0x00000082 =
// MMFSR DACCVIOL+MMARVALID, ...), which beats inferring it from an address.
void hf_report(uint32_t *sp)
{
    hf_field("\n!HF pc=", sp[6]);
    hf_field(" lr=",  sp[5]);
    hf_field(" psr=", sp[7]);
    hf_field(" sp=",  (uint32_t)sp);
    hf_field("\n!HF cfsr=", *(volatile uint32_t *)0xE000ED28u);
    hf_field(" hfsr=",       *(volatile uint32_t *)0xE000ED2Cu);
    hf_field(" mmfar=",      *(volatile uint32_t *)0xE000ED34u);
    hf_field(" bfar=",       *(volatile uint32_t *)0xE000ED38u);
    hf_puts("\n");
    while (1) { __asm volatile("nop"); }
}

__attribute__((naked)) void isr_hardfault(void)
{
    __asm volatile(
        "tst lr, #4\n"          // which stack was in use?
        "ite eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"
        "b hf_report\n"
    );
}

extern int  Duke3D_main(int argc, char **argv);
extern void duke_psram_init(void);          // SetupPsram + zero .psram_bss + heap
extern const char *duke_psram_error(void);  // NULL when the PSRAM is usable
extern void duke_fatfs_init(void);          // SD mount + chdir /roms/duke3d
extern void duke_video_ensure_up(void);     // HSTX + DOS console (pico_display.c)

static void setup_clocks(void)
{
    vreg_disable_voltage_limit();
    vreg_set_voltage(VREG_VOLTAGE_1_50);
    qmi_hw->m[0].timing = 0x60007304;   // relax XIP timing before raising clk_sys
    sleep_ms(100);
    set_sys_clock_khz(378000, true);
    sleep_ms(100);

#ifdef HAS_USBPIO
    // clk_hstx = fixed 126 MHz from a retasked PLL_USB. Deliberately NOT
    // derived from clk_sys even though 378/3 = 126 divides exactly: at 378 MHz
    // PLL_SYS jitter propagates into the TMDS bit clock and strict HDMI sinks
    // show sparkles. Safe only on PIO-USB boards, where the native USB
    // controller (which needs PLL_USB at 48 MHz) is unused.
    pll_deinit(pll_usb);
    pll_init(pll_usb, 1, 756000000, 6, 1);   // 756 / 6 = 126 MHz
    clock_configure(clk_hstx, 0,
                    CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,
                    126000000u, 126000000u);
#else
    // Native-USB boards (Murmulator M2): the USB host controller needs PLL_USB
    // at its stock 48 MHz, so it cannot be retasked. Derive clk_hstx from
    // clk_sys instead — 378/3 = 126 exactly, which keeps the 25.2 MHz pixel
    // clock and satisfies pico_display's 126 MHz assert. This mirrors the
    // native-USB path in pico_shared's FrensHelpers::setClocksAndStartStdio()
    // verbatim (the CLK_SYS aux tap; do NOT switch to the CLKSRC_PLL_SYS tap,
    // the ecosystem has only ever shipped the CLK_SYS form on these boards).
    // Trade-off: PLL_SYS jitter now rides on the TMDS bit clock, so a strict
    // sink may sparkle. Unavoidable while PLL_USB is spoken for.
    clock_configure(clk_hstx, 0,
                    CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLK_SYS,
                    378000000u, 126000000u);
#endif
    // set_sys_clock_khz parked clk_peri on PLL_USB@48 MHz (which the PIO-USB
    // branch just retasked to 126) — put it on PLL_SYS at full speed on both
    // variants, before stdio_init_all derives its UART divisors from it.
    clock_configure(clk_peri, 0,
                    CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
                    378000000u, 378000000u);
}

// ---------------------------------------------------------------------------
// Exit path. Duke never returns from Duke3D_main(): quitting runs
// gameexit() -> Shutdown() -> ShutDown() (which writes the config) ->
// Error(EXIT_SUCCESS) -> exit(), and the engine's fatal paths (filesystem.c,
// tiles.c) call exit(0) directly. The SDK's exit() jumps straight to _exit()
// and deliberately does NOT run atexit handlers ("no desire to pull in
// __call_exitprocs"), so overriding the SDK's __weak _exit is the only hook
// that catches every way out.
//
// The stock _exit spins on __breakpoint(), i.e. quitting Duke currently wedges
// the board until a power cycle. Reset instead: the bootrom always runs
// whatever sits at the start of flash, so a BUILD_FOR_BOOTLOADER image lands
// back in the pico-bootLoader picker and a standalone image simply restarts
// Duke. No watchdog scratch handshake is needed for that — the picker is first
// in flash, so every reset reaches it.
//
// Non-zero status (a failed assert or abort()) does NOT reboot — that would
// throw the state away right when a debugger wants to inspect it. It hands over
// to duke_fatal(), which paints the reason on the HDMI console and then spins,
// so the state survives for a debugger AND a user with no serial cable finds out
// something died. (The stock __breakpoint() loop is gone: with no debugger
// attached BKPT escalates to a HardFault, and isr_hardfault's breadcrumb would
// then be the last thing on the UART instead of the reason.)
// ---------------------------------------------------------------------------
void __attribute__((noreturn)) _exit(int status)
{
    if (status == 0) {
        printf("\nduke3d: exit -> reset%s\n",
#if BUILD_FOR_BOOTLOADER
               " (returning to bootloader)"
#else
               ""
#endif
               );
        stdio_flush();
        watchdog_reboot(0, 0, 1);
    } else {
        // Failed assert, abort(), or an Error() path that did not go through
        // duke_fatal itself. Say so on the HDMI output too — this is the last
        // chance to tell a user with no serial cable that something died, and
        // duke_fatal never returns, so the breakpoint loop below is unreachable
        // for this branch. (duke_fatal is re-entrancy guarded, so arriving here
        // from inside a fatal just parks.)
        printf("\nduke3d: exit(%d)\n", status);
        duke_fatal("Unexpected exit (status %d).", status);
    }
    while (1) {
        tight_loop_contents();
    }
}

int main(void)

{
    setup_clocks();
    stdio_init_all();
    // Mirror everything printed from here on to the DOS-style startup screen.
    // Registered before the first printf so the whole sequence is captured; the
    // text is buffered until the display comes up in _platform_init.
    duke_dostext_attach_stdio();
    sleep_ms(500);
    // Board name from the force-included cflags header's HW_CONFIG, so the
    // startup screen names the board it was actually built for. (A UART-less
    // board still shows all of this: duke_dostext mirrors stdout to HDMI.)
#if HW_CONFIG == 13
#define DUKE_BOARD_NAME "Murmulator M2"
#elif HW_CONFIG == 8
#define DUKE_BOARD_NAME "Adafruit Fruit Jam"
#elif HW_CONFIG == 2
#define DUKE_BOARD_NAME "Adafruit DVI + SD"
#else
#define DUKE_BOARD_NAME "unknown board"
#endif
    printf("\n\n=== pico-duke3D — %s ===\n", DUKE_BOARD_NAME);
    printf("clk_sys=%lu clk_hstx=%lu\n",
           (unsigned long)clock_get_hz(clk_sys),
           (unsigned long)clock_get_hz(clk_hstx));

    // PSRAM first, and only THEN the display. SetupPsram() drives the QMI in
    // direct mode with interrupts off, and XIP reads stall while DIRECT_CSR.EN
    // is set — if core1's scanout were already running, a flash fetch inside
    // that window would stall it past the HSTX DMA deadline. Nothing is lost by
    // waiting: the DOS console records into its character grid from the very
    // first printf, so duke_video_ensure_up() replays these lines the moment the
    // screen appears.
    duke_psram_init();

    // From here on the boot log is on the HDMI output, which is the whole point:
    // everything after this — the SD mount, the GRP, the engine startup — can
    // fail, and those failures are what a user without a serial cable (or, on
    // the Murmulator M2, without a UART at all) could never see.
    duke_video_ensure_up();

    if (duke_psram_error()) duke_fatal("%s", duke_psram_error());

    duke_fatfs_init();

    static char arg0[] = "duke3d";
    static char *argv[] = { arg0, 0 };
    return Duke3D_main(1, argv);
}
