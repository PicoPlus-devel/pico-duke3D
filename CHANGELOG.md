# Changelog

Add a section per release, headed with the tag. `release-notes.sh` extracts the
section matching the tag being released and puts it in the GitHub release body,
so keep the heading text exactly the tag name.

## v1.0

First release of pico-duke3D: Duke Nukem 3D (Chocolate Duke3D / BUILD engine) on
the RP2350, with `DUKE3D.GRP` streamed from the SD card.

### Supported boards

- Adafruit Fruit Jam (`HW_CONFIG 8`) — the hardware-verified board
- Murmulator M2 (`HW_CONFIG 13`) — builds clean, not yet hardware-tested
- Pimoroni Pico Plus 2 + Adafruit DVI and MicroSD breakouts (`HW_CONFIG 2`) —
  builds clean, not yet hardware-tested

### Features

- 378 MHz at 1.50 V, with `clk_hstx` a fixed 126 MHz
- HDMI 640×480p60 over HSTX, driven entirely by core1, ~30 fps in-level
- `DUKE3D.GRP` streamed from the SD card through BUILD's `cache1d`; the engine's
  map arrays and the tile cache live in PSRAM, so the GRP never has to fit
- Sound effects and OPL2 music, with two sinks: I2S (TLV320DAC3100 or PCM5100A)
  and HDMI data-island audio. On the Fruit Jam, headphone-jack detection switches
  between them — plug in and HDMI audio mutes, unplug and it returns
- USB gamepads and keyboards, over PIO-USB or the native RP2350 controller
  depending on the board, including XInput pads
- NES/SNES controller ports over PIO on the boards that have them, folded into
  the same scancode stream as USB, so a pad in a DE-9 port behaves like a USB pad
- An optional four-button **NES pad layout** (Options → Game Options → NES PAD)
  for a vintage NES controller, with SELECT as a shift layer; persists in
  `duke3d.cfg`
- Saved games and settings on the SD card, next to the GRP
  (`/roms/duke3d/game0.sav` … `game9.sav`, `/roms/duke3d/duke3d.cfg`). Nothing is
  written to flash. Saving needs no keyboard: the pad's A accepts an empty name
- A DOS-style startup screen on the HDMI output, live from the first line of
  boot, that keeps recording every `printf` for the whole run
- Fatal errors — no PSRAM, no SD card, no GRP, a corrupt GRP, CON errors — are
  painted on that screen with the log that led to them, and the board halts so
  the message stays readable, instead of hanging on a black screen
- Quit returns to the pico-bootLoader picker, or resets the board standalone

### Build

- Three board scripts (`<board>-build.sh`), one `<board>_cflags.h` per board;
  nothing in `CMakeLists.txt` is board-specific
- The Pico SDK and Pico-PIO-USB come from `PICO_SDK_PATH` and
  `PICO_PIO_USB_PATH`; everything else — the `pico_shared` drivers, the xinput
  driver, the OPL2 emulator and the Chocolate Duke3D sources — is vendored in
  the repository, so there are no submodules to initialise
- Releases are built by `.github/workflows/BuildAndRelease.yml`, which is a thin
  wrapper around `./buildAll.sh` and `./release-notes.sh`
- The pico-bootLoader variants (`<board>-build-forbootloader.sh`) are built and
  released by the pico-bootLoader repository, not from here
