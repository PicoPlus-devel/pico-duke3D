# pico-duke3D

Duke Nukem 3D (Chocolate Duke3D / BUILD engine) port to the RP2350, targeting
the **Adafruit Fruit Jam** (`HW_CONFIG 8`). Built on the same skeleton as
[fruitjam-doom](../fruitjam-doom), reusing Frank's `pico_shared` driver library.

Design and milestones: see the approved plan at
`~/.claude/plans/start-porting-duke-nukem3d-cozy-swing.md`.

## Status: M0 — hardware bring-up skeleton ✅ builds

The BUILD engine is **not vendored yet**. M0 proves the toolchain and every
hardware subsystem the port depends on, using the vendored `pico_shared`
drivers only:

- **Clocks** — 378 MHz @ 1.60 V, `clk_hstx` = fixed 126 MHz from a retasked
  PLL_USB (decoupled from PLL_SYS jitter).
- **PSRAM** — `SetupPsram()` on QMI CS1 (GPIO 47); reports size + a spread R/W test.
- **SD card** — FatFs mount over SPI0; reports filesystem and the presence/size
  of `/roms/duke3d/DUKE3D.GRP` (the asset `cache1d` will stream from M2 on).
- **HDMI** — `pico_hdmi` HSTX, 640×480p60, custom scanline callback drawing
  colour bars + a scrolling white band (the exact palette-expand scanout path
  Duke3D will use).
- **Audio + headphone routing** — TLV320DAC3100 over I2S *and* HDMI data-island
  audio, with a 440 Hz test tone routed to **exactly one** sink, switched live
  by headphone-jack detection: plug in → tone on headphones, HDMI audio muted;
  unplug → tone back on HDMI, internal speaker stays muted.

### Build

Requires `PICO_SDK_PATH` in the environment (SDK 2.2.0, arm-none-eabi-gcc
13.2.Rel1) and `picotool`.

```sh
./fruitjam-build.sh
# -> build_fruitjam/src/pico/duke3d.uf2
```

### Flash & verify (M0)

1. Copy the shareware `DUKE3D.GRP` to `/roms/duke3d/DUKE3D.GRP` on the SD card
   (optional for M0 — its absence is reported, not fatal).
2. Flash `build_fruitjam/src/pico/duke3d.uf2` (BOOTSEL, or `picotool load`).
3. On UART0 (GPIO 44/45, 115200) you should see: clocks at 378 MHz,
   `psram: OK, 8192 KB`, `sd: mounted`, `hdmi: ... colour bars up`, and a
   `status: NN fps` line each second.
4. HDMI: colour bars with a white band scrolling downward.
5. Plug/unplug headphones: the tone follows the jack and the UART logs
   `CONNECT`/`DISCONNECT`.

## Layout

```
fruitjam_cflags.h            board config (HW_CONFIG 8), force-included at build
fruitjam-build.sh            build script
CMakeLists.txt               SDK init + vendored driver subdirs + src/pico
src/pico/pico_main.c         M0 bring-up
3rdparty/pico_shared_drivers vendored pico_shared drivers (HSTX/I2S/TLV320/PSRAM/SD/...)
```
