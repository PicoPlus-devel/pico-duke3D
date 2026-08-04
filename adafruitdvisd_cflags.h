#ifndef DUKE3D_ADAFRUITDVISD_CFLAGS_H
#define DUKE3D_ADAFRUITDVISD_CFLAGS_H 1
//
// Board configuration for pico-duke3D on pico_shared HW_CONFIG 2: an RP2350
// module on a breadboard or Frank's PCB, with the Adafruit DVI Breakout
// (product 4984) on HSTX, the Adafruit MicroSD breakout (product 254) on SPI0,
// an optional PCM5100A I2S DAC and two NES/SNES controller ports.
//
// Force-included into every translation unit (see CMakeLists.txt). Pin values
// mirror pico-infonesPlus/pico_shared BoardConfigs.cmake HW_CONFIG 2 and
// pico-doom's adafruitdvisd_cflags.h.
//
// THE MODULE MUST BE A PIMORONI PICO PLUS 2 (or another RP2350B carrying PSRAM
// on QMI CS1 = GPIO 47). Duke relocates ~2.1 MB of engine .bss into PSRAM and
// cannot run without it, and GPIO 47 does not exist on the RP2350A in a stock
// Pico 2. Build with -DPICO_BOARD=pimoroni_pico_plus2_rp2350; a board without
// PSRAM now says so on screen and halts instead of crashing later (duke_fatal).
//
// Differences from the other two boards that the C code branches on:
//
//   GPIOHSTXINVERTED 0 -> the DVI breakout wires D- = D+ + 1, the NORMAL
//                         polarity. Both the Fruit Jam and the Murmulator M2
//                         are inverted, so this is the one lane setting that is
//                         genuinely new; a garbled or absent picture on this
//                         board points here first.
//   HAS_USBPIO absent  -> USB host runs on the RP2350's NATIVE controller
//                         (tusb_config.h picks rhport 0; gamepads and keyboards
//                         need an OTG adapter). That also forces clk_hstx off
//                         clk_sys instead of a retasked PLL_USB — see
//                         duke_boot.c. Build with -DENABLE_PIO_USB=0 to match.
//                         (BoardConfigs also offers PIO-USB on this board with
//                         D+ on GPIO 20; deliberately unused, as in pico-doom.)
//   DUKE_AUDIO_I2S_DRIVER 2 -> PCM5100A, no codec, no headphone detect, so
//                         audio goes to I2S *and* HDMI simultaneously. Harmless
//                         with no DAC module fitted — HDMI audio still works.
//   NO_USE_UART absent -> unlike the Murmulator M2, GPIO 0/1 are free here, so
//                         the serial console stays on. The DOS startup screen
//                         on HDMI mirrors it either way.
//

// pico_shared BoardConfigs identity of this board. REQUIRED: the vendored
// nespad.cpp selects its PIO program variant with "#if HW_CONFIG == 12 || 13",
// and 2 correctly takes the original (non-shared CLK/LAT) program.
#define HW_CONFIG 2

// A game-level decision that has to be repeated per board because hid_app.cpp
// reads it from the force-included header: the MantaPad (VID 081f) boots in NES
// mode, where its physical X arrives as logical A and its A as logical B — and
// B is Escape, so pressing what looks like fire opens the menu. Duke needs all
// four face buttons. Same rationale as fruitjam_cflags.h; see README.md.
#define MANTAPAD_DEFAULT_SNES_MODE 1

// --- HSTX / HDMI video lanes (read by pico_hdmi/video_output.c) -------------
// Adafruit DVI Breakout 4984 wiring. NOT inverted — see the note above.
#define GPIOHSTXCK 14
#define GPIOHSTXD0 12
#define GPIOHSTXD1 18
#define GPIOHSTXD2 16
#define GPIOHSTXINVERTED 0
#define USE_HSTX 1

// RGB555 packing (R in bits 14-10, G 9-5, B 4-0) — the format pico_hdmi's
// TMDS encoder expects; pico_display.c packs the palette with these shifts.
#define PICO_SCANVIDEO_PIXEL_RCOUNT 5
#define PICO_SCANVIDEO_PIXEL_GCOUNT 5
#define PICO_SCANVIDEO_PIXEL_BCOUNT 5
#define PICO_SCANVIDEO_PIXEL_RSHIFT 10
#define PICO_SCANVIDEO_PIXEL_GSHIFT 5
#define PICO_SCANVIDEO_PIXEL_BSHIFT 0

// --- I2S audio: optional PCM5100A DAC ---------------------------------------
// Driver 2 = PICO_AUDIO_I2S_DRIVER_PCM5000A (audio_i2s.h). There is no codec
// and no headphone-detect pin, so duke_audio.c drops its exclusive-sink policy
// and feeds real samples to BOTH the I2S DAC and the HDMI data islands.
#define DUKE_AUDIO_I2S_DRIVER 2
#define PICO_AUDIO_I2S_DATA_PIN 26
#define PICO_AUDIO_I2S_CLOCK_PIN_BASE 27
#define PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED 0
#define PICO_AUDIO_I2S_PIO 1
#define PICO_AUDIO_I2S_RESET_PIN -1
#define PICO_AUDIO_I2S_INTERRUPT_PIN -1
#define PICO_AUDIO_I2S_INTERRUPT_IS_BUTTON 0
// PCM510x has no volume register and benefits from the DC blocker in
// audio_i2s.c (see I2S_AUDIO_COMPENSATE_DC_OFFSET in audio_i2s.h).
#define I2S_AUDIO_COMPENSATE_DC_OFFSET 1
// No TLV320 and no Wii extension port on this board. Both sets of pins are
// unused (the codec init never runs for driver 2, every tlv320_* entry point
// short-circuits on !s_active, and duke_wiipad.cpp plus the wiipad driver
// compile away on WII_PIN_SDA -1), but WIIPAD_I2C must stay a valid i2c
// instance: tlv320dac3100.c uses it as an i2c_inst_t* in always-compiled code.
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

// --- SD card (hardware SPI0; PIO-SPI fallback available) --------------------
// Adafruit MicroSD breakout 254. SD_TX = MOSI, SD_RX = MISO.
#define SD_TX 3
#define SD_RX 4
#define SD_SCK 2
#define SD_CS 5
#define USE_SD 1
#define SDCARD_SPI spi0
#define SDCARD_PIO pio1

// --- PSRAM (QMI CS1 = GPIO 47, i.e. an RP2350B module) ----------------------
// Duke needs ~2.1 MB for the relocated engine .bss plus whatever is left for
// the cache1d tile cache (which caps at 4 MB and shrinks to fit). The heap top
// is clamped to the size SetupPsram() actually reports, so a part smaller than
// the linker's 8 MB region still boots — it just streams tiles harder. See
// duke_psram.c.
#define PSRAM_CS_PIN 47

// --- Status LEDs (src/pico/duke_leds.c) -------------------------------------
// -1 means the board does not have it, the same convention as the NES pins
// below. Plain onboard LED, blinked every 60 game frames.
// = PICO_DEFAULT_LED_PIN of the Pico Plus 2 this board requires.
#define DUKE_LED_PIN 25
// No NeoPixels on a Pico Plus 2, so no VU meter.
#define DUKE_VU_WS2812_PIN -1

// --- Legacy NES/SNES controller ports ---------------------------------------
// Two independent ports (SNES auto-detected), polled over PIO by the vendored
// pico_shared nespad driver — see duke_usb_input.cpp. Unlike the Murmulator M2
// each port has its own CLK and LAT, so this uses the original PIO program.
// Linked in by -DDUKE_NESPAD=ON; harmless with nothing plugged in.
#define NES_PIN_CLK 6
#define NES_PIN_DATA 7
#define NES_PIN_LAT 8
#define NES_PIO pio1
#define NES_PIN_CLK_1 9
#define NES_PIN_DATA_1 10
#define NES_PIN_LAT_1 11
#define NES_PIO_1 pio1

// --- USB host ----------------------------------------------------------------
// NO HAS_USBPIO / PIN_USB_HOST_* on purpose: tusb_config.h sees HAS_USBPIO
// undefined and selects the native RP2350 controller (rhport 0), and
// duke_boot.c takes the clk_sys-derived clk_hstx branch because PLL_USB has to
// stay at its stock 48 MHz for that controller.

#endif // DUKE3D_ADAFRUITDVISD_CFLAGS_H
