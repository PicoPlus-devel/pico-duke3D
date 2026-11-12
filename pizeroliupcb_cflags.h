#ifndef DUKE3D_PIZEROLIUPCB_CFLAGS_H
#define DUKE3D_PIZEROLIUPCB_CFLAGS_H 1
//
// Board configuration for pico-duke3D on Liu Szuping's PCB for the Waveshare
// RP2350-PiZero (HW_CONFIG 18): an RP2350B with 16 MB flash, HSTX video on the
// PCB's HDMI port (the PiZero's own HDMI port is not used), a PCM5102 I2S DAC,
// two NES/SNES controller ports with separate CLK/LAT, the PiZero's SD card
// slot on SPI1 and its PIO-USB port. PSRAM is optional on the PiZero and has
// to be soldered on (QMI CS1 = GPIO 47) — Duke cannot run without it.
//
// Force-included into every translation unit (see CMakeLists.txt). Pin values
// mirror pico-infonesPlus/pico_shared BoardConfigs.cmake HW_CONFIG 18. The
// build uses PICO_BOARD=waveshare_rp2350_pizero (boards/, copied from
// pico_shared), which declares the RP2350B the SD pins need.
//
// Differences from the Fruit Jam (fruitjam_cflags.h) that the C code branches
// on, all keyed off macros defined (or deliberately NOT defined) here:
//
//   DUKE_AUDIO_I2S_DRIVER 2 -> PCM5102, no codec, no headphone detect, so
//                         audio goes to I2S *and* HDMI simultaneously.
//   NES_PIN_*          -> two legacy NES/SNES ports polled over PIO.
//   WII_PIN_* = -1     -> no Wii extension port.
//   SDCARD_SPI spi1    -> the SD card is on SPI1 (GPIO 30/31/40/43).
//

// pico_shared BoardConfigs identity of this board. The vendored nespad.cpp
// picks its PIO program variant from it; 18 takes the original program
// (separate CLK/LAT per port).
#define HW_CONFIG 18

// A game-level decision that has to be repeated per board because hid_app.cpp
// reads it from the force-included header: the MantaPad (VID 081f) boots in NES
// mode, where its physical X arrives as logical A and its A as logical B — and
// B is Escape, so pressing what looks like fire opens the menu. Duke needs all
// four face buttons. Same rationale as fruitjam_cflags.h; see README.md.
#define MANTAPAD_DEFAULT_SNES_MODE 1

// --- UART (debug console) ---------------------------------------------------
// UART0 on GPIO 0/1, the board header's default. Those pins are free on this
// PCB (UART_ENABLED 1 in BoardConfigs), so the console stays on.

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

// --- I2S audio: PCM5102 DAC (the PCB's audio jack) ---------------------------
// Driver 2 = PICO_AUDIO_I2S_DRIVER_PCM5000A (audio_i2s.h). There is no codec
// and no headphone-detect pin, so duke_audio.c drops its exclusive-sink policy
// and feeds real samples to BOTH the I2S DAC and the HDMI data islands. Same
// pins as the Murmulator M2.
#define DUKE_AUDIO_I2S_DRIVER 2
#define PICO_AUDIO_I2S_DATA_PIN 9
#define PICO_AUDIO_I2S_CLOCK_PIN_BASE 10
#define PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED 0
#define PICO_AUDIO_I2S_PIO 1
#define PICO_AUDIO_I2S_RESET_PIN -1
#define PICO_AUDIO_I2S_INTERRUPT_PIN -1
#define PICO_AUDIO_I2S_INTERRUPT_IS_BUTTON 0
// PCM510x has no volume register and benefits from the DC blocker in
// audio_i2s.c (see I2S_AUDIO_COMPENSATE_DC_OFFSET in audio_i2s.h).
#define I2S_AUDIO_COMPENSATE_DC_OFFSET 1
// No TLV320 codec and no Wii extension port on this board. The codec's pins
// are unused — its init never runs for driver 2, and every tlv320_* entry
// point short-circuits on !s_active — but WIIPAD_I2C must still be a valid i2c
// instance, because tlv320dac3100.c uses it as an i2c_inst_t* in
// always-compiled code. -1 compiles duke_wiipad.cpp away.
#define WIIPAD_I2C i2c1
#define WII_PIN_SDA -1
#define WII_PIN_SCL -1

// Lock the audio sample rate to 48 kHz — the exact-lock rate for the 25.2 MHz
// pixel clock (HDMI ACR N=6144, CTS=25200) and the rate baked into
// hstx_packet.c's IEC 60958 channel-status table.
#define PICO_SOUND_SAMPLE_FREQ 48000

// Audio buffering for the frame-context pump; see fruitjam_cflags.h for the
// sizing rationale (the sink rings must bridge the longest gap between pumps).
#define I2S_AUDIO_RING_SIZE 4096

// --- SD card (hardware SPI1; PIO-SPI fallback available) --------------------
// GPIO 30 = SPI1 SCK, 31 = SPI1 TX, 40 = SPI1 RX; CS 43 is a plain GPIO. GPIO
// 40 is only in the vendored pico_fatfs SPI1 table on RP2350B, hence the
// waveshare_rp2350_pizero board header.
#define SD_TX 31   // MOSI
#define SD_RX 40   // MISO
#define SD_SCK 30
#define SD_CS 43
#define USE_SD 1
#define SDCARD_SPI spi1
#define SDCARD_PIO pio1

// --- PSRAM (QMI CS1 = GPIO 47) -----------------------------------------------
// Not fitted on a stock RP2350-PiZero: a PSRAM chip has to be soldered on.
// Duke needs ~2.1 MB for the relocated engine .bss plus whatever is left for
// the cache1d tile cache (which caps at 4 MB and shrinks to fit). The heap top
// is clamped to the size SetupPsram() actually reports, so a smaller part than
// the linker's 8 MB region still boots — it just streams tiles harder. See
// duke_psram.c. PSRAM_CS_PIN defaults to 47 in BoardConfigs for this board.
#define PSRAM_CS_PIN 47

// --- Status LEDs (src/pico/duke_leds.c) -------------------------------------
// -1 means the board does not have it, the same convention as the NES pins
// below. Plain LED, blinked every 60 game frames (LED_GPIO_PIN 25 in
// BoardConfigs).
#define DUKE_LED_PIN 25
// No NeoPixels on this board, so no VU meter.
#define DUKE_VU_WS2812_PIN -1

// --- Legacy NES/SNES controller ports ---------------------------------------
// Two ports, SNES auto-detected, polled over PIO by the vendored pico_shared
// nespad driver (see duke_usb_input.cpp). Each port has its own CLK and LAT.
// The PCB's ESP32 socket (BlueRetro, for Bluetooth controllers) shares these
// pins: remove the module to use the physical ports.
#define NES_PIN_CLK 2
#define NES_PIN_DATA 4
#define NES_PIN_LAT 3
#define NES_PIO pio1
#define NES_PIN_CLK_1 5
#define NES_PIN_DATA_1 7
#define NES_PIN_LAT_1 6
#define NES_PIO_1 pio1
// An original SNES Mouse in either port, merged with a USB mouse in
// duke_usb_input.cpp. Each read clocks 32 bits instead of 18 (about 390 us
// instead of 220 us), well inside the 2 ms between two polls.
#define NESPAD_SNES_MOUSE 1

// --- USB host on Pico-PIO-USB -----------------------------------------------
// Selects the Pico-PIO-USB host stack (duke_usb_input.cpp) on the PiZero's
// PIO-USB port, D+ on GPIO 28 and D- on GPIO 29. No VBUS-enable GPIO on this
// board, so PIN_USB_HOST_VBUS stays undefined. The clock path does not depend
// on it: duke_boot.c picks it from the CPU clock.
#define HAS_USBPIO
#define PIN_USB_HOST_DP (28u)
#define PIN_USB_HOST_DM (29u)

#endif // DUKE3D_PIZEROLIUPCB_CFLAGS_H
