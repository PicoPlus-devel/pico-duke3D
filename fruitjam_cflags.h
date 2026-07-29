#ifndef DUKE3D_FRUITJAM_CFLAGS_H
#define DUKE3D_FRUITJAM_CFLAGS_H 1
//
// Board configuration for pico-duke3D on the Adafruit Fruit Jam (HW_CONFIG 8).
//
// Force-included into every translation unit (see the build script's
// -include). The vendored pico_shared drivers read these macros at compile
// time (HSTX lane pins, I2S/TLV320 pins, I2C bus, SD pins, PSRAM CS). Because
// the pico-sdk board header (adafruit_fruit_jam.h) guards its own defaults
// with #ifndef, anything defined here wins.
//
// Pin values mirror pico-infonesPlus/pico_shared BoardConfigs.cmake HW_CONFIG 8
// and fruitjam-doom's fruitjam_cflags.h. Doom-specific bits (TINY_WAD_ADDR,
// SAVE_FLASH_BASE, HSTX_DI_RING_ADDRESS, scanvideo pins) are intentionally
// omitted for the M0 bring-up; add them when the SRAM map gets tight.
//

// The cheap AliExpress MantaPad (VID 081f) can act as a NES or a SNES pad, and
// pico_shared defaults it to NES mode until the player presses Y. NES mode
// reports only two face buttons, and since its "NES B" IS the physical SNES X,
// the pad's X arrives as logical A and its A arrives as logical B -- which is
// bound to Escape, so pressing what looks like fire opened the menu. Duke needs
// all four face buttons regardless, so default to SNES mode. See
// 3rdparty/pico_shared_drivers/usb_hid/hid_app.cpp and docs/CONTROLS.md.
#define MANTAPAD_DEFAULT_SNES_MODE 1

// pico_shared BoardConfigs identity of this board (consumed by nespad.cpp in
// later milestones to pick its PIO program variant).
#define HW_CONFIG 8

// --- UART (debug console) ---------------------------------------------------
// The board default (UART1 on GPIO 8/9) collides with the on-board ESP32, so
// route the console to UART0 on GPIO 44/45 like the pico-infonesPlus ports.
#define PICO_DEFAULT_UART 0
#define PICO_DEFAULT_UART_TX_PIN 44
#define PICO_DEFAULT_UART_RX_PIN 45

// --- HSTX / HDMI video lanes (read by pico_hdmi/video_output.c) -------------
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
// TMDS encoder expects. Kept for reference/consistency with fruitjam-doom;
// the scanline callback packs pixels with these shifts.
#define PICO_SCANVIDEO_PIXEL_RCOUNT 5
#define PICO_SCANVIDEO_PIXEL_GCOUNT 5
#define PICO_SCANVIDEO_PIXEL_BCOUNT 5
#define PICO_SCANVIDEO_PIXEL_RSHIFT 10
#define PICO_SCANVIDEO_PIXEL_GSHIFT 5
#define PICO_SCANVIDEO_PIXEL_BSHIFT 0

// --- I2S audio + TLV320DAC3100 codec ----------------------------------------
#define PICO_AUDIO_I2S_DATA_PIN 24
#define PICO_AUDIO_I2S_CLOCK_PIN_BASE 26
#define PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED 0
#define PICO_AUDIO_I2S_PIO 1
#define PICO_AUDIO_I2S_RESET_PIN 22
#define PICO_AUDIO_I2S_INTERRUPT_PIN 23
#define PICO_AUDIO_I2S_INTERRUPT_IS_BUTTON 0
// I2C bus for the TLV320DAC3100 codec (macros consumed by tlv320dac3100.c).
#define WIIPAD_I2C i2c0
#define WII_PIN_SDA 20
#define WII_PIN_SCL 21
// Lock the audio sample rate to 48 kHz — the exact-lock rate for the 25.2 MHz
// pixel clock (HDMI ACR N=6144, CTS=25200) and the rate baked into
// hstx_packet.c's IEC 60958 channel-status table.
#define PICO_SOUND_SAMPLE_FREQ 48000

// Audio buffering for the FRAME-CONTEXT pump: the mixer only advances when
// the game pumps it (per frame / faketimerhandler / sampletimer), so the sink
// rings must bridge the longest gap between pumps (heavy scenes can exceed
// 30 ms). DI ring: 768 islands x 4 samples = 64 ms (malloc'd — at 110 KB it
// no longer fits the old fixed 0x20076000 pin). I2S ring: 4096 samples =
// 85 ms (must be a power of two).
#define I2S_AUDIO_RING_SIZE 4096

// --- SD card (hardware SPI0; PIO-SPI fallback available) --------------------
#define SD_TX 35   // MOSI
#define SD_RX 36   // MISO
#define SD_SCK 34
#define SD_CS 39
#define USE_SD 1
#define SDCARD_SPI spi0
#define SDCARD_PIO pio1

// --- PSRAM (8 MB APS6404 on QMI CS1 = GPIO 47) ------------------------------
#define PSRAM_CS_PIN 47

// --- USB host on Pico-PIO-USB -----------------------------------------------
// Marker only in M0: selects the "retask PLL_USB as the fixed 126 MHz clk_hstx
// source" clock path (see pico_main.c). The USB host stack itself is wired up
// in the input milestone (M3).
#define HAS_USBPIO
#define PIN_USB_HOST_DP (1u)
#define PIN_USB_HOST_DM (2u)
#define PIN_USB_HOST_VBUS (11u)

#endif // DUKE3D_FRUITJAM_CFLAGS_H
