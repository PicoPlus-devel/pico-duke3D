//
//  duke_boot.c — RP2350 entry point for the Duke3D game image.
//
//  Brings up the hardware (clocks 378 MHz + fixed 126 MHz clk_hstx, PSRAM, SD)
//  then calls the renamed Duke entry Duke3D_main(). Video/audio come up inside
//  Duke's own init (_platform_init -> pico_display). Clock sequence is the
//  proven Fruit Jam path shared with the M0 bring-up (pico_main.c).
//
#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/pll.h"
#include "hardware/vreg.h"
#include "hardware/uart.h"
#include "hardware/watchdog.h"
#include "hardware/structs/qmi.h"

// ---------------------------------------------------------------------------
// Hardfault breadcrumb (system-freeze diagnosis). Raw UART register writes —
// no stdio, no locks — so it works from any context on either core. If the
// freeze is a crash, this prints "!HF sp=... pc=... lr=..."; if NOTHING
// appears and the system is frozen, it's a bus-level wedge (nothing executes,
// not even the fault handler) — a decisive distinction.
// ---------------------------------------------------------------------------
static void hf_puthex(uint32_t v)
{
    for (int i = 28; i >= 0; i -= 4) {
        int d = (v >> i) & 0xf;
        uart_putc_raw(uart0, d < 10 ? ('0' + d) : ('a' + d - 10));
    }
}

void isr_hardfault(void)
{
    uint32_t *sp;
    __asm volatile("mrs %0, msp" : "=r"(sp));
    const char *tag = "\n!HF sp=";
    for (const char *p = tag; *p; p++) uart_putc_raw(uart0, *p);
    hf_puthex((uint32_t)sp);
    uart_putc_raw(uart0, ' '); uart_putc_raw(uart0, 'p'); uart_putc_raw(uart0, 'c'); uart_putc_raw(uart0, '=');
    hf_puthex(sp[6]);   // stacked PC
    uart_putc_raw(uart0, ' '); uart_putc_raw(uart0, 'l'); uart_putc_raw(uart0, 'r'); uart_putc_raw(uart0, '=');
    hf_puthex(sp[5]);   // stacked LR
    uart_putc_raw(uart0, '\n');
    while (1) { __asm volatile("nop"); }
}

extern int  Duke3D_main(int argc, char **argv);
extern void duke_psram_init(void);    // SetupPsram + zero .psram_bss + heap
extern void duke_fatfs_init(void);    // SD mount + chdir /roms/duke3d

static void setup_clocks(void)
{
    vreg_disable_voltage_limit();
    vreg_set_voltage(VREG_VOLTAGE_1_60);
    qmi_hw->m[0].timing = 0x60007304;   // relax XIP timing before raising clk_sys
    sleep_ms(100);
    set_sys_clock_khz(378000, true);
    sleep_ms(100);

    // clk_hstx = fixed 126 MHz from a retasked PLL_USB (decoupled from PLL_SYS
    // jitter; safe because USB host runs on Pico-PIO-USB).
    pll_deinit(pll_usb);
    pll_init(pll_usb, 1, 756000000, 6, 1);   // 756 / 6 = 126 MHz
    clock_configure(clk_hstx, 0,
                    CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,
                    126000000u, 126000000u);
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
// Non-zero status (a failed assert or abort()) keeps the stock breakpoint loop
// on purpose: rebooting would throw away the state right when a debugger wants
// to inspect it, and hanging there is what those paths already did.
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
        printf("\nduke3d: exit(%d) — halting for the debugger\n", status);
        stdio_flush();
    }
    while (1) {
        if (status != 0) __breakpoint();
        tight_loop_contents();
    }
}

int main(void)

{
    setup_clocks();
    stdio_init_all();
    sleep_ms(500);
    printf("\n\n=== pico-duke3D (M2) — Adafruit Fruit Jam ===\n");
    printf("clk_sys=%lu clk_hstx=%lu\n",
           (unsigned long)clock_get_hz(clk_sys),
           (unsigned long)clock_get_hz(clk_hstx));

    duke_psram_init();
    duke_fatfs_init();

    static char arg0[] = "duke3d";
    static char *argv[] = { arg0, 0 };
    return Duke3D_main(1, argv);
}
