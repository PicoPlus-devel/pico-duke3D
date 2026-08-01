#ifndef DUKE3D_MURMULATORM2_CFLAGS_H
#define DUKE3D_MURMULATORM2_CFLAGS_H 1
//
// Board configuration for pico-duke3D on the Murmulator M2 (HW_CONFIG 13):
// an RP2350 Pico 2 module on a carrier with HSTX video, a PCM5100A I2S DAC,
// two NES/SNES controller ports (shared CLK/LAT) and PSRAM on QMI CS1.
//
// Force-included into every translation unit (see CMakeLists.txt). Pin values
// mirror pico-infonesPlus/pico_shared BoardConfigs.cmake HW_CONFIG 13 and
// pico-doom's murmulatorm2_cflags.h.
//
// Differences from the Fruit Jam (fruitjam_cflags.h) that the C code branches
// on, all keyed off macros defined (or deliberately NOT defined) here:
//
//   HAS_USBPIO absent  -> USB host runs on the RP2350's NATIVE controller
//                         (tusb_config.h picks rhport 0; gamepads need an OTG
//                         adapter). That also forces clk_hstx off clk_sys
//                         instead of a retasked PLL_USB — see duke_boot.c.
//                         Build with -DENABLE_PIO_USB=0 to match.
//   DUKE_AUDIO_I2S_DRIVER 2 -> PCM5100A, no codec, no headphone detect, so
//                         audio goes to I2S *and* HDMI simultaneously.
//   NES_PIN_*          -> two legacy NES/SNES ports polled over PIO.
//   NO_USE_UART        -> GPIO 0/1 are the M2's Wii connector, so the console
//                         stays off the UART entirely (build passes
//                         -DDUKE_NO_STDIO_UART=1). The DOS startup screen on
//                         HDMI is the log.
//

// pico_shared BoardConfigs identity of this board. REQUIRED: the vendored
// nespad.cpp selects its shared-CLK/LAT PIO program variant with
// "#if HW_CONFIG == 12 || HW_CONFIG == 13".
#define HW_CONFIG 13

// A game-level decision that has to be repeated per board because hid_app.cpp
// reads it from the force-included header: the MantaPad (VID 081f) boots in NES
// mode, where its physical X arrives as logical A and its A as logical B — and
// B is Escape, so pressing what looks like fire opens the menu. Duke needs all
// four face buttons. Same rationale as fruitjam_cflags.h; see README.md.
#define MANTAPAD_DEFAULT_SNES_MODE 1

// --- UART (debug console) ---------------------------------------------------
// Off. The M2's Wii connector sits on GPIO 0/1, which are the pico2 default
// UART pins, so claiming them would drive the connector. NO_USE_UART also
// suppresses duke_boot.c's raw-register hardfault breadcrumb — writing uart0
// registers while the peripheral is held in reset would fault inside the fault
// handler. Flip both this and -DDUKE_NO_STDIO_UART=0 to get a console back.
#define NO_USE_UART 1

// --- HSTX / HDMI video lanes (read by pico_hdmi/video_output.c) -------------
// Same lanes as the Fruit Jam, also inverted (D- = D+ - 1).
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

// --- I2S audio: PCM5100A DAC -------------------------------------------------
// Driver 2 = PICO_AUDIO_I2S_DRIVER_PCM5000A (audio_i2s.h). There is no codec
// and no headphone-detect pin, so duke_audio.c drops its exclusive-sink policy
// and feeds real samples to BOTH the I2S DAC and the HDMI data islands.
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
// No TLV320 here. Its pins are unused (the codec init never runs for driver 2,
// and every tlv320_* entry point short-circuits on !s_active), but WIIPAD_I2C
// must stay a valid i2c instance: tlv320dac3100.c uses it as an i2c_inst_t* in
// always-compiled code.
#define WIIPAD_I2C i2c0
#define WII_PIN_SDA -1
#define WII_PIN_SCL -1

// Lock the audio sample rate to 48 kHz — the exact-lock rate for the 25.2 MHz
// pixel clock (HDMI ACR N=6144, CTS=25200) and the rate baked into
// hstx_packet.c's IEC 60958 channel-status table.
#define PICO_SOUND_SAMPLE_FREQ 48000

// Audio buffering for the frame-context pump; see fruitjam_cflags.h for the
// sizing rationale (the sink rings must bridge the longest gap between pumps).
#define I2S_AUDIO_RING_SIZE 4096

// --- SD card (hardware SPI0; PIO-SPI fallback available) --------------------
#define SD_TX 7    // MOSI
#define SD_RX 4    // MISO
#define SD_SCK 6
#define SD_CS 5
#define USE_SD 1
#define SDCARD_SPI spi0
#define SDCARD_PIO pio1

// --- PSRAM (QMI CS1 = GPIO 8) ------------------------------------------------
// Duke needs ~2.1 MB for the relocated engine .bss plus whatever is left for
// the cache1d tile cache (which caps at 4 MB and shrinks to fit). The heap top
// is clamped to the size SetupPsram() actually reports, so a smaller part than
// the linker's 8 MB region still boots — it just streams tiles harder. See
// duke_psram.c.
#define PSRAM_CS_PIN 8

// --- Legacy NES/SNES controller ports ---------------------------------------
// Two ports, SNES auto-detected, polled over PIO by the vendored pico_shared
// nespad driver (see duke_usb_input.cpp). CLK and LAT are shared between the
// ports; only DATA differs, so the driver runs two lockstep state machines
// released by the same PIO IRQ — that is the HW_CONFIG 13 program variant.
#define NES_PIN_CLK 20
#define NES_PIN_DATA 26
#define NES_PIN_LAT 21
#define NES_PIO pio1
#define NES_PIN_CLK_1 20
#define NES_PIN_DATA_1 27
#define NES_PIN_LAT_1 21
#define NES_PIO_1 pio1

// --- USB host ----------------------------------------------------------------
// NO HAS_USBPIO / PIN_USB_HOST_* on purpose: tusb_config.h sees HAS_USBPIO
// undefined and selects the native RP2350 controller (rhport 0), and
// duke_boot.c takes the clk_sys-derived clk_hstx branch because PLL_USB has to
// stay at its stock 48 MHz for that controller.

#endif // DUKE3D_MURMULATORM2_CFLAGS_H
