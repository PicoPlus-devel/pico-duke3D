# Changelog

Add a section per release, headed with the tag. `release-notes.sh` extracts the
section matching the tag being released and puts it in the GitHub release body,
so keep the heading text exactly the tag name.

## v0.4

### Added

- **Olimex RP2040-PICO-PC with a Raspberry Pi Pico 2** is now supported
  (`duke3d_game_olimexpicopc.uf2`). Picture and sound over HDMI, sound on the
  board's audio jack at the same time, USB keyboard, mouse and gamepads on the
  USB-A socket, and a NES/SNES pad on the UEXT connector. Duke needs a PSRAM
  chip fitted to the Pico 2 (GPIO 8).

## v0.3

### Added

- **A `BOOTSEL MODE` item at the bottom of the OPTIONS menu.** It restarts the
  board as the USB firmware drive, so it can be reflashed without holding
  **BOOT** while replugging it — which is awkward once the board is in a case.
  Settings are written to the card on the way out, so a change made in the menu
  just before is kept. There is no confirmation: the item sits last, furthest
  from where the cursor starts, with a warning line under it. See
  [Bootsel mode](https://github.com/fhoedemakers/pico-duke3D#bootsel-mode)

## v0.2

A controller fix for the DE-9 sockets, and documentation for building the game
into a finished console. Nothing about the engine, the video or the audio
changed, so a v0.1 SD card works as it is.

### Fixed

- **Aftermarket and clone NES controllers now fire and jump.** Only *original*
  Nintendo pads announce themselves on the wire, so a clone was translated as a
  SNES pad: its A landed on jump, its B on jetpack, and it had no fire button at
  all, with confirm and back swapped in the menus. A port now assumes NES and
  settles on SNES the moment a button arrives that only a SNES pad has (A, X, L
  or R), which no two-button pad can send

  The one cost is a single press: a SNES pad whose *first* press is B or Y has
  that press read as a NES pad's A or B — fire instead of jump. Any A press
  settles the port for the rest of the session, and the two sockets settle
  independently. See
  [One quirk with SNES pads in these sockets](https://github.com/fhoedemakers/pico-duke3D#one-quirk-with-snes-pads-in-these-sockets)

### Documentation

- A **PicoNES PCB** section in the README:
  [@johnedgarpark](https://twitter.com/johnedgarpark)'s board, which carries the
  module and both breakouts and turns the *Adafruit DVI + SD* build into a
  console with two controller ports and a case.
  Parts list, what to flash, what the board does and does not give you, and
  Gavin Knight's 3D-printed enclosure
- Which build of that PCB runs Duke, stated up front, because only one does:
  design v2.6, a Pimoroni Pico Plus 2, and male headers in the through-holes. A
  Pico 2 has too little memory, and a Plus 2 cannot be soldered flat
- The DE-9 sockets speak the SNES protocol too, so a SNES pad with a
  SNES-to-NES adapter cable gives the full control scheme on a board whose ports
  are NES ones. A NES pad plays the game, but has no crouch or strafe

## v0.1

First release of pico-duke3D: Duke Nukem 3D (Chocolate Duke3D / BUILD engine) on
the RP2350, with `DUKE3D.GRP` streamed from the SD card.

### Supported boards

All three are tested and working on real hardware.

- Adafruit Fruit Jam (`HW_CONFIG 8`)
- Murmulator M2 (`HW_CONFIG 13`)
- Pimoroni Pico Plus 2 + Adafruit DVI and MicroSD breakouts (`HW_CONFIG 2`)

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
- One gamepad layout for every pad, modelled on **Retro-Go**'s duke3d-go: fire on A,
  jump on B, crouch on X, jetpack on Y, strafe on L/R, next weapon on SELECT, use on
  START and the menu on L+R. Holding START for 500 ms turns the D-pad into the
  inventory, with look up/down on X and B
- A real NES controller in a DE-9 port is **detected as such** and its two buttons are
  mapped to A and B, so it fires and jumps like any other pad with no setting to
  change
- Saved games and settings on the SD card, next to the GRP
  (`/roms/duke3d/game0.sav` … `game9.sav`, `/roms/duke3d/duke3d.cfg`). Nothing is
  written to flash. Saving needs no keyboard: the pad's A accepts an empty name
- **F12 screenshots**, as 320×200 8-bit PNGs in `/screenshots/dukeNNNN.png` at the
  root of the SD card, numbered from the lowest free slot. The game's own frame
  and palette go out unconverted, so the shot matches the screen exactly. The
  encoder (bitbank2 PNGenc) keeps its ~47 KB workspace in PSRAM, so the feature
  costs no SRAM
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
