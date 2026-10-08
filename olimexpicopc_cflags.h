#ifndef DUKE3D_OLIMEXPICOPC_CFLAGS_H
#define DUKE3D_OLIMEXPICOPC_CFLAGS_H 1
//
// Board configuration for pico-duke3D on pico_shared HW_CONFIG 15: an Olimex
// RP2040-PICO-PC carrying a Raspberry Pi Pico 2 (RP2350A, 4 MB flash) — HSTX
// video on the HDMI socket, a USB-A host port on the Pico's native USB, an SD
// card on SPI0, one NES/SNES pad on the UEXT connector and a PWM audio jack.
//
// Force-included into every translation unit (see CMakeLists.txt). Pin values
// mirror pico-infonesPlus/pico_shared BoardConfigs.cmake HW_CONFIG 15 and
// pico-doom's olimexpicopc_cflags.h.
//
// PSRAM MUST BE FITTED ON GPIO 8 (QMI CS1). Duke relocates ~2.1 MB of engine
// .bss into PSRAM and cannot run without it, and a stock Pico 2 has none; a
// board without it says so on screen and halts (duke_fatal).
//
// Differences from the Murmulator M2 (murmulatorm2_cflags.h) that the C code
// branches on, all keyed off macros defined (or deliberately NOT defined) here:
//
//   DUKE_AUDIO_I2S_DRIVER 0 -> no I2S DAC at all. duke_audio.c skips the I2S
//                         setup and paces the mixer by the HDMI ring alone;
//                         the samples go to HDMI and to the PWM audio jack
//                         (PWM_AUDIO_PIN_L/R), at the same time.
//   GPIOHSTXD1/D2      -> swapped compared with the Fruit Jam and the M2.
//   NES_PIN_*          -> one port with its own CLK/LAT (the original nespad
//                         PIO program), no second port.
//   WII_PIN_* -1       -> no Wii extension port.
//   NO_USE_UART        -> GPIO 0/1 are the board's PS/2 port, so the console
//                         stays off the UART entirely (build passes
//                         -DDUKE_NO_STDIO_UART=1). The DOS startup screen on
//                         HDMI is the log.
//   HAS_USBPIO absent  -> as on the M2: native USB host (here wired to the
//                         board's USB-A socket), clk_hstx off clk_sys — see
//                         duke_boot.c. Build with -DENABLE_PIO_USB=0 to match.
//

// pico_shared BoardConfigs identity of this board. The vendored nespad.cpp
// selects its PIO program variant with "#if HW_CONFIG == 12 || 13", and 15
// correctly takes the original (non-shared CLK/LAT) program.
#define HW_CONFIG 15

// A game-level decision that has to be repeated per board because hid_app.cpp
// reads it from the force-included header: the MantaPad (VID 081f) boots in NES
// mode, where its physical X arrives as logical A and its A as logical B — and
// B is Escape, so pressing what looks like fire opens the menu. Duke needs all
// four face buttons. Same rationale as fruitjam_cflags.h; see README.md.
#define MANTAPAD_DEFAULT_SNES_MODE 1

// --- UART (debug console) ---------------------------------------------------
// Off. GPIO 0/1, the pico2 default UART pins, are the board's PS/2 port.
// NO_USE_UART also suppresses duke_boot.c's raw-register hardfault breadcrumb —
// writing uart0 registers while the peripheral is held in reset would fault
// inside the fault handler. Matches UART_ENABLED 0 in BoardConfigs.cmake.
#define NO_USE_UART 1

// --- HSTX / HDMI video lanes (read by pico_hdmi/video_output.c) -------------
// Inverted (D- = D+ - 1), like the Fruit Jam and the M2, but with D1 and D2
// swapped: D1 on 19, D2 on 17.
#define GPIOHSTXCK 13
#define GPIOHSTXD0 15
#define GPIOHSTXD1 19
#define GPIOHSTXD2 17
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

// --- Audio: HDMI + PWM jack, no I2S DAC --------------------------------------
// Driver 0 = PICO_AUDIO_I2S_DRIVER_NONE (audio_i2s.h). The I2S pins are unused
// (-1, as in BoardConfigs.cmake), and so is the codec: WIIPAD_I2C below only
// has to be a valid i2c instance, because tlv320dac3100.c uses it as an
// i2c_inst_t* in always-compiled code.
#define DUKE_AUDIO_I2S_DRIVER 0
#define PICO_AUDIO_I2S_DATA_PIN -1
#define PICO_AUDIO_I2S_CLOCK_PIN_BASE -1
#define PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED 0
#define PICO_AUDIO_I2S_PIO 1
#define PICO_AUDIO_I2S_RESET_PIN -1
#define PICO_AUDIO_I2S_INTERRUPT_PIN -1
#define PICO_AUDIO_I2S_INTERRUPT_IS_BUTTON 0
// PWM audio jack: RC-filtered PWM, left on GPIO 28, right on GPIO 27, fed with
// the very samples that go to HDMI (pwm_audio_push() in duke_audio.c's
// hstx_push_audio_sample). GPIO 23 high takes the Pico 2's SMPS out of its
// power-save mode, which adds audible hiss to the PWM output. Matches
// BoardConfigs.cmake HW_CONFIG 15.
#define PWM_AUDIO_PIN_L 28
#define PWM_AUDIO_PIN_R 27
#define PWM_AUDIO_SMPS_PIN 23
// No Wii extension port: duke_wiipad.cpp and the wiipad driver compile away on
// WII_PIN_SDA -1.
#define WIIPAD_I2C i2c1
#define WII_PIN_SDA -1
#define WII_PIN_SCL -1

// Lock the audio sample rate to 48 kHz — the exact-lock rate for the 25.2 MHz
// pixel clock (HDMI ACR N=6144, CTS=25200) and the rate baked into
// hstx_packet.c's IEC 60958 channel-status table. Also an exact PWM rate:
// 378 MHz / 48 kHz = 7875 clk_sys cycles per sample.
#define PICO_SOUND_SAMPLE_FREQ 48000

// Audio buffering for the frame-context pump; see fruitjam_cflags.h for the
// sizing rationale. Unused without an I2S driver (the ring is never allocated),
// kept so duke_audio.c's arithmetic compiles the same on every board.
#define I2S_AUDIO_RING_SIZE 4096

// --- SD card (hardware SPI0; PIO-SPI fallback available) --------------------
#define SD_TX 7    // MOSI
#define SD_RX 4    // MISO
#define SD_SCK 6
#define SD_CS 22
#define USE_SD 1
#define SDCARD_SPI spi0
#define SDCARD_PIO pio1

// --- PSRAM (QMI CS1 = GPIO 8) ------------------------------------------------
// Required, see the note at the top. The heap top is clamped to the size
// SetupPsram() actually reports, so a part smaller than the linker's 8 MB
// region still boots — it just streams tiles harder. See duke_psram.c.
#define PSRAM_CS_PIN 8

// --- Status LEDs (src/pico/duke_leds.c) -------------------------------------
// -1 means the board does not have it, the same convention as the NES pins
// below. Plain onboard LED, blinked every 60 game frames. = PICO_DEFAULT_LED_PIN
// of the Pico 2 the board carries.
#define DUKE_LED_PIN 25
// No NeoPixels on this board, so no VU meter.
#define DUKE_VU_WS2812_PIN -1

// --- Legacy NES/SNES controller port ----------------------------------------
// One port on the UEXT connector (SNES auto-detected), polled over PIO by the
// vendored pico_shared nespad driver (see duke_usb_input.cpp). Linked in by
// -DDUKE_NESPAD=ON; harmless with nothing plugged in. No second port.
#define NES_PIN_CLK 5
#define NES_PIN_DATA 20
#define NES_PIN_LAT 9
#define NES_PIO pio1
#define NES_PIN_CLK_1 -1
#define NES_PIN_DATA_1 -1
#define NES_PIN_LAT_1 -1
#define NES_PIO_1 pio1

// --- USB host ----------------------------------------------------------------
// NO HAS_USBPIO / PIN_USB_HOST_* on purpose: tusb_config.h sees HAS_USBPIO
// undefined and selects the native RP2350 controller (rhport 0), and
// duke_boot.c takes the clk_sys-derived clk_hstx branch because PLL_USB has to
// stay at its stock 48 MHz for that controller.

#endif // DUKE3D_OLIMEXPICOPC_CFLAGS_H
