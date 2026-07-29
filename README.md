# pico-duke3D

Duke Nukem 3D (Chocolate Duke3D / BUILD engine) port to the RP2350, targeting the
**Adafruit Fruit Jam** (`HW_CONFIG 8`). Built on the same skeleton as
[fruitjam-doom](../fruitjam-doom), reusing Frank's `pico_shared` driver library.

`DUKE3D.GRP` is **streamed from the SD card** via BUILD's `cache1d` — it does not
fit in the 8 MB PSRAM, which instead holds the tile cache and the engine's map
arrays (`sector`/`wall`/`sprite`, relocated there by a generated linker script).

Design and milestones: see the approved plan at
`~/.claude/plans/start-porting-duke-nukem3d-cozy-swing.md`.

## Status

Playable, with sound and music. Hardware-verified:

- **378 MHz @ 1.60 V**, `clk_hstx` a fixed 126 MHz from a retasked PLL_USB.
- **HDMI** 640×480p60 over HSTX (`pico_hdmi`), core1-owned, ~30 fps in-level.
- **PSRAM** 8 MB on QMI CS1; **SD** over SPI0 with FatFs.
- **Sound effects and OPL2 music**, with headphone-jack detection switching
  between I2S (TLV320DAC3100) and HDMI data-island audio — plug in and HDMI audio
  mutes, unplug and it returns.
- **USB gamepad and keyboard** over PIO-USB.
- **DOS-style startup screen** on the HDMI output (40×25, pico_shared's 8×8 font).

Awaiting hardware test: the bootloader variant, quit-to-picker, save/load, and
settings persistence.

## Build

Requires `PICO_SDK_PATH` in the environment (SDK 2.2.0, arm-none-eabi-gcc
13.2.Rel1) and `picotool`.

```sh
./fruitjam-build.sh                 # -> build_fruitjam/src/pico/duke3d_game.uf2
./fruitjam-build-forbootloader.sh   # -> build_bl_fruitjam/src/pico/duke3d_game.uf2
```

The second links the image into the pico-bootLoader's app partition at
`0x10080000` instead of owning flash from `0x10000000`, so the picker can launch
it — copy that UF2 to `/emu/8/` on the SD card. See `cmake/BootPartition.cmake`
for the flash map. Any reset returns to the picker, as does quitting Duke.

Useful options:

| Option | Effect |
|---|---|
| `-DDUKE_VIDEO_DIAG=1` | 1 Hz `vid:`/`audio:` health lines on core0 (fps, audio production rate, queue level, underruns) |
| `-DDUKE_HSTX_DEBUG=1` | pico_hdmi's own core1 register dump. **Costs ~33 ms/s of core1 and starves the audio** — for debugging an HSTX wedge only, never for judging audio |
| `-DDUKE_OPL_RENDERER=linear` | cheaper OPL renderer (~half the cost, quality unvalidated on device); default is `reference` |

## SD card

```
/roms/duke3d/DUKE3D.GRP      the game data (shareware or registered/Atomic)
/roms/duke3d/game0.sav       savegames land here, next to the GRP
/roms/duke3d/duke3d.cfg      settings
/emu/8/duke3d_game.uf2       only for the bootloader variant
```

## Controls

Everything reaches the game as **DOS scancodes**. `duke_usb_input.cpp` posts them
to `pico_display_post_key()` → Duke's `keyhandler()`, which updates both
`KB_KeyDown[]` (menus) and `CONTROL_UpdateKeyboardState()` (in-game bindings). So
one table drives menu navigation *and* gameplay, and a gamepad button is
indistinguishable from the key it stands for.

### Gamepad

Up to two pads. `hid_app.cpp` folds the D-pad hat and the left analogue stick
into the same direction bits, so either works.

| Button | Sends | In game | In menus |
|---|---|---|---|
| D-pad / left stick | arrow keys | move + turn | navigate |
| **A** | Enter | — | **select / confirm** |
| **B** | Escape | open the menu | back |
| **START** | Escape | open the menu | back |
| **X** | Left Ctrl | **fire** | — |
| **Y** | Space | open / use | — |
| **L** | Left Shift | run | — |
| **R** | Left Alt | strafe | — |
| **SELECT** | A | jump | — |
| **C** | Z | crouch | — |

Two notes on that table:

* **B and START both send Escape.** Deliberate: B-as-back is the console
  convention, START-as-menu is the DOS one.
* **`C` does not exist on a SNES-shaped pad**, so crouch is currently unreachable
  on one. Every other action has a button. To put crouch on a SNES pad, move it
  onto `B` (which duplicates START anyway) — one line in `padmap[]` in
  `src/pico/duke_usb_input.cpp`.

#### MantaPad (cheap AliExpress SNES pad, VID 081f)

This pad can act as either a NES or a SNES controller, and pico_shared defaults
it to **NES mode** until the player presses Y. NES mode reports only two face
buttons, and because its "NES B" *is* physically the SNES **X**, the mapping came
out shuffled: the pad's X arrived as logical A, and its A arrived as logical
**B → Escape**. That is why pressing what looked like fire opened the menu, and
why pressing Y — or navigating a menu, which makes you press Y — appeared to
"fix" it.

Duke needs all four face buttons regardless, so this port defaults the pad to
SNES mode via `MANTAPAD_DEFAULT_SNES_MODE` in `fruitjam_cflags.h`. The boot log
reads `defaulting to SNES mode` instead of `Press Y to activate SNES mode`, and X
is fire from the first frame — no Y press, no level restart.

### USB keyboard

A keyboard works alongside the pad, with all letters, digits and punctuation,
F1–F12, the keypad, Backspace, Tab, Enter, Escape, Space and the arrows. Shift is
tracked as a real scancode, so `KB_Getch()` picks the shifted ASCII table and
capitals work. `F6` quicksave and `F9` quickload work from a keyboard.

Two deliberate gaps:

* **Insert / Home / PageUp / Delete / End / PageDown** are omitted. They are
  extended (`0xE0`-prefixed) on a PC keyboard and Duke reaches them through its
  own `extscanToSC` table, so posting a bare code would mean a different key. The
  keypad equivalents do work.
* **Arrows** use Duke's remapped extended codes rather than `0xE0` pairs, since
  this layer posts single bytes. That is why the arrow and keypad entries differ
  even though a PC keyboard shares their scancodes.

### Saving and loading — a keyboard is optional

Duke's save flow ends in `strget()`, which asks for a save name. That looks like
it needs a keyboard, but it doesn't: `strget()` returns "accept" as soon as it
sees Enter, **whether or not anything was typed**, and `KB_Getch()` derives its
ASCII from `KB_LastScan` through `scancodeToASCII[]`. The pad's **A** button
sends Enter, so it lands in that queue like a real key.

With the pad alone:

1. **START** or **B** — open the menu
2. navigate to *Save Game* with the D-pad, **A** to select
3. pick a slot, **A** to select
4. **A** again to accept the (empty) name → written to `/roms/duke3d/game0.sav`

Loading is the same via *Load Game*. The only thing you give up is a *named*
save — the slot shows blank. With a keyboard you can type a name at step 4.

`F6`/`F9` skip the name prompt once you have made one ordinary save, but they are
**not on the pad** — a SNES pad has no spare button, so that would need a chord.

## Layout

```
fruitjam_cflags.h              board config (HW_CONFIG 8), force-included at build
fruitjam-build.sh              standalone build
fruitjam-build-forbootloader.sh pico-bootLoader app-partition build
CMakeLists.txt                 SDK init + vendored driver subdirs + src/pico
cmake/psram_linker.cmake       generates the linker script: PSRAM region, engine
                               .bss -> PSRAM, audio path -> SRAM, stack carve-out,
                               and the bootloader FLASH relink (single owner)
cmake/BootPartition.cmake      bootloader flash map
src/Engine/ src/Game/          vendored Chocolate Duke3D, edited in place
src/pico/                      RP2350 platform layer (replaces BUILD's sdlayer.c)
  duke_boot.c                  entry: clocks, PSRAM, SD, exit -> reset
  pico_display.c               BUILD baselayer: video, palette, timer, input entry
  duke_audio.c duke_music.c    dual-sink mixer + OPL2 music
  duke_usb_input.cpp           pad/keyboard -> DOS scancodes (table above)
  duke_dostext.cpp             DOS startup screen
  duke_fatfs_io.c              FatFs behind POSIX *and* newlib syscalls
3rdparty/pico_shared_drivers   vendored pico_shared (HSTX/I2S/TLV320/PSRAM/SD/font)
3rdparty/emu8950               OPL2 emulator
```

### Notes worth knowing before changing things

- **Never put audio code or data behind the QMI bus.** PSRAM at `0x11000000` is
  the *cached* XIP window for CS1 and shares one 8 KB cache with flash code at
  `0x10000000`, so core0 streaming textures evicts core1's audio code. The
  linker script keeps the whole audio path in SRAM.
- **The mix accumulator is `float`, not `double`.** This core has no
  double-precision FPU; audiolib's stock `double` mixer cost eleven soft-float
  calls per sample and held audio to 65 % of the rate the sink consumes, which
  made music and speech play slow.
- **The stack lives in its own region at the top of SRAM.** It must stay outside
  both `__bss_start__..__bss_end__` (or crt0 zeroes the stack it is running on)
  and `end..__StackLimit` (or the heap grows into it).
