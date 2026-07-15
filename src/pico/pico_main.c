//
// pico-duke3D — M0 hardware bring-up skeleton for the Adafruit Fruit Jam.
//
// Proves the toolchain + every hardware subsystem the Duke3D port depends on,
// individually and end-to-end, before the BUILD engine is vendored (M1+):
//
//   * clocks   : 378 MHz @ 1.60 V, clk_hstx = fixed 126 MHz from a retasked
//                PLL_USB (decoupled from PLL_SYS to keep the TMDS bit clock
//                jitter-free). Sequence ported from fruitjam-doom's i_main.c /
//                pico_shared FrensHelpers::setClocksAndStartStdio().
//   * PSRAM    : SetupPsram() on QMI CS1 (GPIO 47); reports size + spread R/W test.
//   * SD card  : pico_fatfs mount; reports filesystem + presence/size of
//                /roms/duke3d/DUKE3D.GRP (the asset cache1d will stream in M2+).
//   * HDMI     : pico_hdmi HSTX output, 640x480p60, custom scanline callback
//                drawing SMPTE-ish colour bars with a scrolling white band
//                (proves the exact palette-expand scanout path Duke3D will use).
//   * audio    : TLV320DAC3100 over I2S + HDMI data-island audio, a 440 Hz test
//                tone routed to exactly one sink, switched live by headphone
//                jack detection — the headline feature of this port. Headphones
//                in => tone on I2S/headphones, HDMI audio muted (silence
//                islands). Headphones out => tone back on HDMI, internal
//                speaker stays muted (project spec).
//
// Every phase logs to UART before it runs, so a hang on a headless board is
// attributable from the serial log alone.
//

#include <stdio.h>
#include <math.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/pll.h"
#include "hardware/vreg.h"
#include "hardware/structs/qmi.h"

// Vendored pico_shared drivers.
#include "SetupPsram.h"                 // pico_psram
#include "tf_card.h"                    // pico_fatfs
#include "ff.h"                         // FatFs
#include "video_output.h"               // pico_hdmi
#include "hstx_packet.h"                // pico_hdmi (audio data islands)
#include "hstx_data_island_queue.h"     // pico_hdmi (audio data islands)
#include "audio_i2s.h"                  // pico_audio_i2s_shared
#include "tlv320dac3100.h"              // TLV320 codec + headphone detect

// PSRAM cached XIP CS1 window (Fruit Jam: 8 MB APS6404).
#define PSRAM_XIP_BASE 0x11000000u

// Asset the streaming loader (cache1d) will consume from M2 onward.
#define GRP_SD_PATH "/roms/duke3d/DUKE3D.GRP"

// ---------------------------------------------------------------------------
// RGB555 helper — R in bits 14-10, G 9-5, B 4-0 (the format pico_hdmi's TMDS
// encoder expects; matches PICO_SCANVIDEO_PIXEL_*SHIFT in fruitjam_cflags.h).
// ---------------------------------------------------------------------------
static inline uint16_t rgb555(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
}

// Eight colour bars, precomputed in SRAM so the IRQ-context scanline callback
// never touches flash.
static uint16_t s_bar_colors[8];
static volatile uint32_t s_frame_counter = 0;

static void init_bar_colors(void)
{
    s_bar_colors[0] = rgb555(192, 192, 192); // grey
    s_bar_colors[1] = rgb555(192, 192,   0); // yellow
    s_bar_colors[2] = rgb555(  0, 192, 192); // cyan
    s_bar_colors[3] = rgb555(  0, 192,   0); // green
    s_bar_colors[4] = rgb555(192,   0, 192); // magenta
    s_bar_colors[5] = rgb555(192,   0,   0); // red
    s_bar_colors[6] = rgb555(  0,   0, 192); // blue
    s_bar_colors[7] = rgb555( 24,  24,  24); // near-black
}

// core1 (DMA IRQ): fill (MODE_H_ACTIVE_PIXELS/2) uint32 words, two RGB555
// pixels per word. Keep it trivial — this is the ISR-deadline path.
static void __not_in_flash_func(m0_scanline_cb)(uint32_t v_scanline,
                                                uint32_t active_line,
                                                uint32_t *line_buffer)
{
    (void)v_scanline;
    const uint32_t words = MODE_H_ACTIVE_PIXELS / 2; // 320 for 640x480
    // A white band scrolls down the screen so it is obvious frames advance.
    uint32_t band = (s_frame_counter >> 1) % MODE_V_ACTIVE_LINES;
    if (active_line >= band && active_line < band + 8u) {
        for (uint32_t i = 0; i < words; i++) line_buffer[i] = 0x7FFF7FFFu;
        return;
    }
    for (uint32_t i = 0; i < words; i++) {
        uint32_t bar = (i * 8u) / words;            // 0..7
        uint32_t px = s_bar_colors[bar];
        line_buffer[i] = px | (px << 16);
    }
}

static void __not_in_flash_func(m0_vsync_cb)(void)
{
    s_frame_counter++;
}

// core1 stack for the HSTX video service loop (rp2350-doom lesson: give it a
// real 8 KB stack, and keep core1 dedicated to HDMI output).
static uint32_t s_core1_stack[2048] __attribute__((aligned(8)));

// ---------------------------------------------------------------------------
// HDMI audio push — verbatim from fruitjam-doom's pico_hdmi_glue.c. The
// driver's hstx.c (which normally provides this) is not compiled; accumulate 4
// samples per data island and drop above the high watermark so the mixer never
// blocks.
// ---------------------------------------------------------------------------
#ifndef HSTX_AUDIO_DI_HIGH_WATERMARK
#define HSTX_AUDIO_DI_HIGH_WATERMARK 200
#endif

static void __not_in_flash_func(hstx_push_audio_sample)(int left, int right)
{
    static int frame_counter = 0;
    static audio_sample_t acc_buf[4];
    static int acc_count = 0;
    acc_buf[acc_count].left = (int16_t)left;
    acc_buf[acc_count].right = (int16_t)right;
    acc_count++;
    if (acc_count == 4) {
        if (hstx_di_queue_get_level() >= HSTX_AUDIO_DI_HIGH_WATERMARK) {
            acc_count = 0;
            return;
        }
        hstx_packet_t packet;
        frame_counter = hstx_packet_set_audio_samples_cs(&packet, acc_buf, 4, frame_counter);
        hstx_data_island_t island;
        hstx_encode_data_island(&island, &packet, false, true);
        (void)hstx_di_queue_push(&island);
        acc_count = 0;
    }
}

// ---------------------------------------------------------------------------
// 440 Hz test tone (Q16 phase into a 256-entry sine LUT).
// ---------------------------------------------------------------------------
static int16_t s_sine_lut[256];
static uint32_t s_tone_phase = 0;
static const uint32_t s_tone_inc =
    (uint32_t)(((uint64_t)440 * 256u << 16) / PICO_SOUND_SAMPLE_FREQ);

static void init_sine_lut(void)
{
    // One full sine cycle at ~-15 dBFS. Computed once at boot, so sinf() cost
    // is irrelevant (pico_float is already linked).
    for (int i = 0; i < 256; i++) {
        float s = sinf((2.0f * 3.14159265f) * (float)i / 256.0f);
        s_sine_lut[i] = (int16_t)(s * 6000.0f);
    }
}

static inline int16_t next_tone_sample(void)
{
    int16_t s = s_sine_lut[(s_tone_phase >> 16) & 0xFFu];
    s_tone_phase += s_tone_inc;
    return s;
}

// ---------------------------------------------------------------------------
// Clocks: 378 MHz @ 1.60 V, clk_hstx = 126 MHz from PLL_USB. Ported verbatim
// from fruitjam-doom src/i_main.c (HAS_USBPIO branch).
// ---------------------------------------------------------------------------
static void setup_clocks(void)
{
    vreg_disable_voltage_limit();
    vreg_set_voltage(VREG_VOLTAGE_1_60);
    // Relax XIP timing BEFORE raising clk_sys (hardware-proven profile).
    qmi_hw->m[0].timing = 0x60007304;
    sleep_ms(100);
    set_sys_clock_khz(378000, true);
    sleep_ms(100);

    // Retask PLL_USB as a dedicated fixed 126 MHz clk_hstx source, decoupled
    // from PLL_SYS jitter (safe because the USB host runs on Pico-PIO-USB, so
    // the hardware USB controller's PLL_USB@48 MHz is unused).
    pll_deinit(pll_usb);
    pll_init(pll_usb, 1, 756000000, 6, 1); // 756 / (6*1) = 126 MHz
    clock_configure(clk_hstx,
                    0,
                    CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,
                    126000000u,
                    126000000u);
    // Put clk_peri on PLL_SYS (set_sys_clock_khz parked it on the now-retasked
    // PLL_USB); stdio_init_all derives UART dividers from this.
    clock_configure(clk_peri,
                    0,
                    CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
                    378000000u,
                    378000000u);
}

// ---------------------------------------------------------------------------
// PSRAM
// ---------------------------------------------------------------------------
static int32_t setup_psram(void)
{
    printf("psram: init QMI CS1 on GPIO %d (clk_sys %u kHz)...\n",
           PSRAM_CS_PIN, (unsigned)(clock_get_hz(clk_sys) / 1000));
    int32_t size = SetupPsram(PSRAM_CS_PIN);
    if (size <= 0) {
        printf("psram: NONE detected (expected 8 MB on Fruit Jam)\n");
        return size;
    }
    printf("psram: OK, %ld KB (%ld MB) at 0x%08x\n",
           (long)(size / 1024), (long)(size / (1024 * 1024)), PSRAM_XIP_BASE);

    // Spread read/write test across the cached window.
    volatile uint32_t *p = (volatile uint32_t *)PSRAM_XIP_BASE;
    size_t words = (size_t)size / 4;
    const size_t stride = words / 8;
    bool ok = true;
    for (size_t i = 0; i < words; i += stride) {
        p[i] = 0xA5A50000u ^ (uint32_t)i;
    }
    for (size_t i = 0; i < words; i += stride) {
        uint32_t want = 0xA5A50000u ^ (uint32_t)i;
        if (p[i] != want) {
            printf("psram: R/W MISMATCH at word %u: got 0x%08x want 0x%08x\n",
                   (unsigned)i, (unsigned)p[i], (unsigned)want);
            ok = false;
        }
    }
    printf("psram: spread R/W test %s\n", ok ? "PASS" : "FAIL");
    return size;
}

// ---------------------------------------------------------------------------
// SD card
// ---------------------------------------------------------------------------
static FATFS s_fs;

static const char *fs_type_name(BYTE t)
{
    switch (t) {
        case FS_FAT12: return "FAT12";
        case FS_FAT16: return "FAT16";
        case FS_FAT32: return "FAT32";
        case FS_EXFAT: return "exFAT";
        default:       return "unknown";
    }
}

static bool setup_sd(void)
{
    printf("sd: config SPI%d SCK=%d MOSI=%d MISO=%d CS=%d...\n",
           spi_get_index(SDCARD_SPI), SD_SCK, SD_TX, SD_RX, SD_CS);
    static pico_fatfs_spi_config_t cfg = {
        SDCARD_SPI,
        CLK_SLOW_DEFAULT,
        CLK_FAST_DEFAULT_PIO,
        SD_RX,   // MISO
        SD_CS,
        SD_SCK,
        SD_TX,   // MOSI
        true     // pullups
    };
    if (pico_fatfs_set_config(&cfg)) {
        printf("sd: hardware SPI\n");
    } else {
        pico_fatfs_config_spi_pio(SDCARD_PIO, pio_claim_unused_sm(SDCARD_PIO, true));
        printf("sd: PIO-SPI fallback\n");
    }

    FRESULT fr = f_mount(&s_fs, "", 1);
    if (fr != FR_OK) {
        printf("sd: MOUNT FAILED (FatFs %d) — card inserted & FAT-formatted?\n", fr);
        return false;
    }
    printf("sd: mounted, filesystem %s\n", fs_type_name(s_fs.fs_type));

    // Report the GRP the streaming loader will use later.
    FIL fil;
    fr = f_open(&fil, GRP_SD_PATH, FA_READ);
    if (fr == FR_OK) {
        FSIZE_t sz = f_size(&fil);
        f_close(&fil);
        printf("sd: found %s (%lu KB / %lu MB)\n", GRP_SD_PATH,
               (unsigned long)(sz / 1024), (unsigned long)(sz / (1024 * 1024)));
    } else {
        printf("sd: %s not present yet (FatFs %d) — copy the shareware GRP there for M2\n",
               GRP_SD_PATH, fr);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Video (HSTX/HDMI) — mirrors fruitjam-doom's doom_hdmi_init ordering.
// ---------------------------------------------------------------------------
static void setup_video(void)
{
    printf("hdmi: clk_hstx = %lu Hz (expect 126000000)\n",
           (unsigned long)clock_get_hz(clk_hstx));
    init_bar_colors();
    video_output_set_dvi_mode(false);   // HDMI mode: emit data-island audio
    hstx_di_queue_init();
    video_output_set_vsync_callback(m0_vsync_cb);
    video_output_init(640, 480);
    pico_hdmi_set_audio_sample_rate(PICO_SOUND_SAMPLE_FREQ);
    video_output_set_scanline_callback(m0_scanline_cb);
    multicore_launch_core1_with_stack(video_output_core1_run,
                                      s_core1_stack, sizeof(s_core1_stack));
    printf("hdmi: 640x480p60 colour bars up on core1\n");
}

// ---------------------------------------------------------------------------
// Audio + headphone routing
// ---------------------------------------------------------------------------
typedef enum { SINK_HDMI = 0, SINK_HEADPHONES = 1 } audio_sink_t;
static volatile audio_sink_t s_sink = SINK_HDMI;

static bool setup_audio(void)
{
    printf("audio: I2S setup (TLV320, %d Hz)...\n", PICO_SOUND_SAMPLE_FREQ);
    // audio_i2s_setup(TLV320) internally does i2c_init + codec reset + full
    // register program incl. headset-detect (regs 0x43/0x30/0x33). DMA chan 6
    // (>=4) keeps HSTX on DMA_IRQ_0 and audio on DMA_IRQ_1.
    audio_i2s_hw_t *i2s = audio_i2s_setup(PICO_AUDIO_I2S_DRIVER_TLV320,
                                          PICO_SOUND_SAMPLE_FREQ, /*dmachan=*/6);
    if (!i2s) {
        printf("audio: I2S setup FAILED\n");
        return false;
    }
    audio_i2s_muteInternalSpeaker(true);  // internal speaker never plays
    audio_i2s_setVolume(14);              // +14 dB
    init_sine_lut();
    printf("audio: ready, default sink = HDMI\n");
    return true;
}

static void poll_headphone(void)
{
    enum headphone_toggle_t ev = tlv320_poll_headphone();
    if (ev == HP_TOGGLE_CONNECT) {
        s_sink = SINK_HEADPHONES;
        printf("audio: headphones CONNECT -> sink = HEADPHONES (HDMI muted)\n");
        // speaker already muted by the codec's default handling.
    } else if (ev == HP_TOGGLE_DISCONNECT) {
        s_sink = SINK_HDMI;
        audio_i2s_muteInternalSpeaker(true);  // spec: never route to speaker
        printf("audio: headphones DISCONNECT -> sink = HDMI\n");
    }
}

// Generate the tone into whichever sink is active; feed silence to the other
// so its clock/DMA keeps running (I2S BCLK stays alive, HDMI drains to
// silence islands).
static void pump_audio(void)
{
    int freeSamples = audio_i2s_get_freebuffer_size();
    if (s_sink == SINK_HEADPHONES) {
        for (int i = 0; i < freeSamples; i++) {
            int16_t s = next_tone_sample();
            audio_i2s_enqueue_sample(((uint32_t)(uint16_t)s << 16) | (uint16_t)s);
        }
    } else { // SINK_HDMI
        for (int i = 0; i < freeSamples; i++) {
            audio_i2s_enqueue_sample(0);        // keep BCLK alive
        }
        // Push the tone to HDMI, paced against the DI ring.
        int guard = 0;
        while (hstx_di_queue_get_level() < (HSTX_AUDIO_DI_HIGH_WATERMARK - 32)
               && guard++ < 512) {
            int16_t s = next_tone_sample();
            hstx_push_audio_sample(s, s);
        }
    }
}

// ---------------------------------------------------------------------------
int main(void)
{
    // Fruit Jam hard buttons 2/3 on GPIO 4/5 (pull up so a floating pin does
    // not read low).
    gpio_init_mask((1u << 4) | (1u << 5));
    gpio_pull_up(4);
    gpio_pull_up(5);

    setup_clocks();
    stdio_init_all();
    sleep_ms(500); // let the UART settle / terminal attach

    printf("\n\n=== pico-duke3D M0 bring-up (Adafruit Fruit Jam, HW_CONFIG 8) ===\n");
    printf("clocks: clk_sys=%lu Hz clk_peri=%lu Hz clk_hstx=%lu Hz\n",
           (unsigned long)clock_get_hz(clk_sys),
           (unsigned long)clock_get_hz(clk_peri),
           (unsigned long)clock_get_hz(clk_hstx));

    setup_psram();
    setup_sd();
    bool audio_ok = setup_audio();
    setup_video();

    printf("=== M0 running: colour bars on HDMI, 440 Hz tone follows the jack ===\n");

    uint32_t last_report = to_ms_since_boot(get_absolute_time());
    uint32_t last_frames = 0;
    while (true) {
        poll_headphone();
        if (audio_ok) pump_audio();

        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - last_report >= 1000) {
            uint32_t f = s_frame_counter;
            printf("status: %lu fps, sink=%s, di_level=%lu, frames=%lu\n",
                   (unsigned long)(f - last_frames),
                   s_sink == SINK_HEADPHONES ? "HEADPHONES" : "HDMI",
                   (unsigned long)hstx_di_queue_get_level(),
                   (unsigned long)f);
            last_frames = f;
            last_report = now;
        }
    }
    return 0;
}
