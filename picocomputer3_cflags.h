#ifndef DUKE3D_PICOCOMPUTER3_CFLAGS_H
#define DUKE3D_PICOCOMPUTER3_CFLAGS_H 1
//
// Board configuration for pico-duke3D on the Pico Computer 3 (PC3, HW_CONFIG
// 17, https://github.com/UKTailwind/Pico-Computer-3): an RP2350B with 16 MB
// flash and 8 MB PSRAM, HSTX video on its HDMI connector, a PCM5102 I2S DAC on
// the audio jack, a microSD slot on SPI1, a four-port USB hub on the RP2350's
// own USB controller, and an I/O header with two NES/SNES controller ports
// (shared CLK/LAT) and a Wii extension port.
//
// Force-included into every translation unit (see CMakeLists.txt). Pin values
// mirror pico-infonesPlus/pico_shared BoardConfigs.cmake HW_CONFIG 17. The
// build uses PICO_BOARD=weact_studio_rp2350b_core, as pico_shared does for this
// config; the pico-sdk ships that header, and it declares the RP2350B the SD
// pins need.
//
// Differences from the Fruit Jam (fruitjam_cflags.h) that the C code branches
// on, all keyed off macros defined (or deliberately NOT defined) here:
//
//   HAS_USBPIO absent  -> USB host runs on the RP2350's NATIVE controller
//                         (tusb_config.h picks rhport 0), behind the board's
//                         USB hub. Build with -DENABLE_PIO_USB=0 to match.
//   DUKE_AUDIO_I2S_DRIVER 2 -> PCM5102, no codec, no headphone detect, so
//                         audio goes to I2S *and* HDMI simultaneously.
//   NES_PIN_*          -> two legacy NES/SNES ports polled over PIO, with
//                         shared CLK/LAT.
//   WII_PIN_*          -> a Wii extension pad on GPIO 20/21 over i2c0, which it
//                         shares with the board's RTC (no codec on it here).
//   SDCARD_SPI spi1    -> the SD card is on SPI1 (GPIO 28/30/31/33).
//   PICO_DEFAULT_UART 1 -> the console is on UART1, GPIO 8/9.
//

// pico_shared BoardConfigs identity of this board. REQUIRED: the vendored
// nespad.cpp selects its shared-CLK/LAT PIO program variant with
// "#if HW_CONFIG == 12 || HW_CONFIG == 13 || HW_CONFIG == 17".
#define HW_CONFIG 17

// A game-level decision that has to be repeated per board because hid_app.cpp
// reads it from the force-included header: the MantaPad (VID 081f) boots in NES
// mode, where its physical X arrives as logical A and its A as logical B — and
// B is Escape, so pressing what looks like fire opens the menu. Duke needs all
// four face buttons. Same rationale as fruitjam_cflags.h; see README.md.
#define MANTAPAD_DEFAULT_SNES_MODE 1

// --- UART (debug console) ---------------------------------------------------
// UART1 on GPIO 8/9, the board's serial console. The board header's default
// (UART0 on GPIO 12/13) would take two HSTX pins. duke_boot.c's hardfault
// breadcrumb writes to uart_default, so it follows this.
#define PICO_DEFAULT_UART 1
#define PICO_DEFAULT_UART_TX_PIN 8
#define PICO_DEFAULT_UART_RX_PIN 9

// --- HSTX / HDMI video lanes (read by pico_hdmi/video_output.c) -------------
// Same lanes as the Fruit Jam and the Murmulator M2, also inverted
// (D- = D+ - 1).
#define HSTX_CKP 13
#define HSTX_D0P 15
#define HSTX_D1P 17
#define HSTX_D2P 19
#define GPIOHSTXCK HSTX_CKP
#define GPIOHSTXD0 HSTX_D0P
#define GPIOHSTXD1 HSTX_D1P
#define GPIOHSTXD2 HSTX_D2P
#define GPIOHSTXINVERTED 1
#define USE_HSTX 1

// RGB555 packing (R in bits 14-10, G 9-5, B 4-0) — the format pico_hdmi's
// TMDS encoder expects; pico_display.c packs the palette with these shifts.
#define PICO_SCANVIDEO_PIXEL_RCOUNT 5
#define PICO_SCANVIDEO_PIXEL_GCOUNT 5
#define PICO_SCANVIDEO_PIXEL_BCOUNT 5
#define PICO_SCANVIDEO_PIXEL_RSHIFT 10
#define PICO_SCANVIDEO_PIXEL_GSHIFT 5
#define PICO_SCANVIDEO_PIXEL_BSHIFT 0

// --- I2S audio: PCM5102 DAC (the audio jack) ---------------------------------
// Driver 2 = PICO_AUDIO_I2S_DRIVER_PCM5000A (audio_i2s.h). There is no codec
// and no headphone-detect pin, so duke_audio.c drops its exclusive-sink policy
// and feeds real samples to BOTH the I2S DAC and the HDMI data islands. BCK on
// GPIO 10, LRCK on GPIO 11, DIN on GPIO 22.
#define DUKE_AUDIO_I2S_DRIVER 2
#define PICO_AUDIO_I2S_DATA_PIN 22
#define PICO_AUDIO_I2S_CLOCK_PIN_BASE 10
#define PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED 0
#define PICO_AUDIO_I2S_PIO 1
#define PICO_AUDIO_I2S_RESET_PIN -1
#define PICO_AUDIO_I2S_INTERRUPT_PIN -1
#define PICO_AUDIO_I2S_INTERRUPT_IS_BUTTON 0
// PCM510x has no volume register and benefits from the DC blocker in
// audio_i2s.c (see I2S_AUDIO_COMPENSATE_DC_OFFSET in audio_i2s.h).
#define I2S_AUDIO_COMPENSATE_DC_OFFSET 1
// Wii extension port (NES/SNES Classic Mini, Wii Classic Controller (Pro)) on
// the I/O header, read over I2C by src/pico/duke_wiipad.cpp: i2c0 on GPIO 20/21,
// the board's system I2C, which it shares with the RTC. Matches
// BoardConfigs.cmake HW_CONFIG 17. No TLV320 here, so duke_wiipad_init needs no
// reset-hold dance. The codec's init never runs for driver 2, and every
// tlv320_* entry point short-circuits on !s_active.
#define WIIPAD_I2C i2c0
#define WII_PIN_SDA 20
#define WII_PIN_SCL 21

// Lock the audio sample rate to 48 kHz — the exact-lock rate for the 25.2 MHz
// pixel clock (HDMI ACR N=6144, CTS=25200) and the rate baked into
// hstx_packet.c's IEC 60958 channel-status table.
#define PICO_SOUND_SAMPLE_FREQ 48000

// Audio buffering for the frame-context pump; see fruitjam_cflags.h for the
// sizing rationale (the sink rings must bridge the longest gap between pumps).
#define I2S_AUDIO_RING_SIZE 4096

// --- SD card (hardware SPI1; PIO-SPI fallback available) --------------------
// GPIO 30 = SPI1 SCK, 31 = SPI1 TX, 28 = SPI1 RX; CS 33 is a plain GPIO. GPIO
// 30/31 are only in the vendored pico_fatfs SPI1 table on RP2350B, hence the
// weact_studio_rp2350b_core board header.
#define SD_TX 31   // MOSI
#define SD_RX 28   // MISO
#define SD_SCK 30
#define SD_CS 33
#define USE_SD 1
#define SDCARD_SPI spi1
#define SDCARD_PIO pio1

// --- PSRAM (QMI CS1 = GPIO 47) -----------------------------------------------
// 8 MB on the board. Duke needs ~2.1 MB for the relocated engine .bss plus
// whatever is left for the cache1d tile cache (which caps at 4 MB and shrinks
// to fit). The heap top is clamped to the size SetupPsram() actually reports.
// See duke_psram.c.
#define PSRAM_CS_PIN 47

// --- Status LEDs (src/pico/duke_leds.c) -------------------------------------
// -1 means the board does not have it, the same convention as the NES pins
// below. The HEARTBEAT LED (jumper H5 on 1-2), blinked every 60 game frames.
#define DUKE_LED_PIN 25
// No NeoPixels on this board, so no VU meter.
#define DUKE_VU_WS2812_PIN -1

// --- Legacy NES/SNES controller ports ---------------------------------------
// Two ports on the I/O header, SNES auto-detected, polled over PIO by the
// vendored pico_shared nespad driver (see duke_usb_input.cpp). CLK (GPIO 4) and
// LAT (GPIO 5) are shared between the ports; only DATA differs, so the driver
// runs two lockstep state machines released by the same PIO IRQ — the
// HW_CONFIG 12/13/17 program variant. No SNES Mouse on this board: that program
// does not have the timing the mouse needs.
#define NES_PIN_CLK 4
#define NES_PIN_DATA 6
#define NES_PIN_LAT 5
#define NES_PIO pio1
#define NES_PIN_CLK_1 4
#define NES_PIN_DATA_1 7
#define NES_PIN_LAT_1 5
#define NES_PIO_1 pio1

// --- USB host ----------------------------------------------------------------
// NO HAS_USBPIO / PIN_USB_HOST_* on purpose: tusb_config.h sees HAS_USBPIO
// undefined and selects the native RP2350 controller (rhport 0). The four USB-A
// ports are behind the board's hub (CFG_TUH_HUB 1, three devices). The 48 MHz
// is divided from PLL_SYS at 384 MHz, so duke_boot.c can still use PLL_USB as
// the 126 MHz clk_hstx source.

#endif // DUKE3D_PICOCOMPUTER3_CFLAGS_H
