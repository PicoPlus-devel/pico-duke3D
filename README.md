# pico-duke3D

Duke Nukem 3D (Chocolate Duke3D / BUILD engine) port to the RP2350, for the
**Adafruit Fruit Jam** (`HW_CONFIG 8`), the **Murmulator M2** (`HW_CONFIG 13`)
and **Adafruit DVI + SD breakout boards** (`HW_CONFIG 2`). Built on the same
skeleton as [fruitjam-doom](../fruitjam-doom), reusing Frank's `pico_shared`
driver library.

`DUKE3D.GRP` is **streamed from the SD card** via BUILD's `cache1d` — it does not
fit in the 8 MB PSRAM, which instead holds the tile cache and the engine's map
arrays (`sector`/`wall`/`sprite`, relocated there by a generated linker script).

Design and milestones: see the approved plan at
`~/.claude/plans/start-porting-duke-nukem3d-cozy-swing.md`.

## Status

Playable, with sound and music. Hardware-verified **on the Fruit Jam**:

- **378 MHz @ 1.50 V**, `clk_hstx` a fixed 126 MHz from a retasked PLL_USB.
- **HDMI** 640×480p60 over HSTX (`pico_hdmi`), core1-owned, ~30 fps in-level.
- **PSRAM** 8 MB on QMI CS1; **SD** over SPI0 with FatFs.
- **Sound effects and OPL2 music**, with headphone-jack detection switching
  between I2S (TLV320DAC3100) and HDMI data-island audio — plug in and HDMI audio
  mutes, unplug and it returns.
- **USB gamepad and keyboard** over PIO-USB.
- **DOS-style startup screen** on the HDMI output (40×25, pico_shared's 8×8 font),
  live from the first line of boot, with **fatal errors reported on it** — see
  [When something is wrong](#when-something-is-wrong).
- Bootloader variant, quit-to-picker, save/load and settings persistence.

The **Murmulator M2** and **Adafruit DVI + SD** builds are complete and build
clean, but are **not yet hardware-tested**. See [Boards](#boards) for what
differs there.

## Boards

The board is chosen at configure time by `DUKE_BOARD`, which force-includes
`<tag>_cflags.h` into every translation unit. Everything the C code branches on
lives in that one header.

| | Fruit Jam (`fruitjam`) | Murmulator M2 (`murmulatorm2`) | Adafruit DVI + SD (`adafruitdvisd`) |
|---|---|---|---|
| `HW_CONFIG` | 8 | 13 | 2 |
| `PICO_BOARD` | `adafruit_fruit_jam` | `pico2` | `pimoroni_pico_plus2_rp2350` |
| Flash | 16 MB | 4 MB | 16 MB |
| Video | HSTX 640×480p60, lanes 13/15/17/19 **inverted** | same | lanes **12/14/16/18, not inverted** |
| `clk_hstx` | 126 MHz from a **retasked PLL_USB** (jitter-free) | 126 MHz **from `clk_sys`** (378/3) — PLL_USB is spoken for | same as the M2 |
| Audio DAC | TLV320DAC3100 + headphone detect | PCM5100A, no codec, no detect | PCM5100A (**optional module**), no detect |
| Audio routing | **exclusive**: headphones *or* HDMI | **both** sinks always live | **both** sinks always live |
| USB host | Pico-PIO-USB on GP1/GP2 | **native** RP2350 controller (OTG adapter) | **native** RP2350 controller (OTG adapter) |
| Controller ports | — | two NES/SNES ports on PIO (shared CLK/LAT) | two NES/SNES ports on PIO (independent CLK/LAT) |
| PSRAM | 8 MB on CS1 = GP47 | on CS1 = **GP8** | 8 MB on CS1 = GP47 |
| SD (SPI0) | MOSI 35 / MISO 36 / SCK 34 / CS 39 | MOSI 7 / MISO 4 / SCK 6 / CS 5 | MOSI 3 / MISO 4 / SCK 2 / CS 5 |
| UART console | UART0 on GP44/45 | **none** — GP0/1 are the Wii connector | board default (GP0/1) |

`adafruitdvisd` is a Pico 2 footprint (breadboard or Frank's PCB) with the
[Adafruit DVI Breakout 4984](https://www.adafruit.com/product/4984) on HSTX and
the [MicroSD breakout 254](https://www.adafruit.com/product/254) on SPI0. **The
module must be a Pimoroni Pico Plus 2**, not a stock Pico 2: Duke cannot run
without PSRAM, and `HW_CONFIG 2` puts the PSRAM chip select on GP47 = QMI CS1,
which only exists on an RP2350B. Its HSTX lanes are the only ones in this table
that are *not* inverted — a garbled or absent picture there points at
`GPIOHSTXINVERTED` first.

Two consequences worth knowing on the Murmulator:

* **No serial log.** The DOS-style startup screen on HDMI is the log — it
  mirrors every `printf`. The hardfault breadcrumb in `duke_boot.c` is compiled
  out with it (poking `uart0` while the peripheral is held in reset would fault
  inside the fault handler). To get a console back, drop `NO_USE_UART` from
  `murmulatorm2_cflags.h` and `-DDUKE_NO_STDIO_UART=1` from the build script —
  but that drives GP0/1.
* **PSRAM smaller than 8 MB still boots.** The 8 MB linker region is an
  address-space declaration; the heap top is clamped at boot to what
  `SetupPsram()` reports and the cache1d tile cache shrinks to fit. Roughly
  2.1 MB of engine arrays are the hard floor — below about 4 MB total the port
  is not worth running.

Adding a board is a `<tag>_cflags.h` plus a `<tag>-build.sh`; nothing in
`CMakeLists.txt` is board-specific.

## Build

Requires `PICO_SDK_PATH` in the environment (SDK 2.2.0, arm-none-eabi-gcc
13.2.Rel1) and `picotool`.

```sh
./fruitjam-build.sh                      # -> build_fruitjam/src/pico/duke3d_game.uf2
./fruitjam-build-forbootloader.sh        # -> build_bl_fruitjam/...
./murmulatorm2-build.sh                  # -> build_murmulatorm2/src/pico/duke3d_game.uf2
./murmulatorm2-build-forbootloader.sh    # -> build_bl_murmulatorm2/...
./adafruitdvisd-build.sh                 # -> build_adafruitdvisd/src/pico/duke3d_game.uf2
./adafruitdvisd-build-forbootloader.sh   # -> build_bl_adafruitdvisd/...
```

A plain `cmake -S . -B build` (VSCode / CMake Tools) still configures for the
Fruit Jam.

The `-forbootloader` variants link the image into the pico-bootLoader's app
partition at `0x10080000` instead of owning flash from `0x10000000`, so the
picker can launch it — copy that UF2 to `/emu/<HW_CONFIG>/` on the SD card. Any
reset returns to the picker, as does quitting Duke.

| Build | Image | App slot | Flash total |
|---|---|---|---|
| Fruit Jam standalone | `0x10000000` | — | 16 MB |
| Fruit Jam bootloader | `0x10080000` | 15.5 MB | 16 MB |
| Murmulator standalone | `0x10000000` | — | 4 MB |
| Murmulator bootloader | `0x10080000` | 3.5 MB | 4 MB |
| Adafruit DVI+SD standalone | `0x10000000` | — | 16 MB |
| Adafruit DVI+SD bootloader | `0x10080000` | 15.5 MB | 16 MB |

Duke keeps **nothing** in flash — `DUKE3D.GRP` streams from SD and savegames sit
next to it — so the whole partition goes to the app on every board. Only the
Murmulator needs the totals overridden (`-DDUKE_APP_SIZE` / `-DDUKE_FLASH_TOTAL`
in its script); the other two match the defaults in `cmake/BootPartition.cmake`.

Useful options:

| Option | Effect |
|---|---|
| `-DDUKE_VIDEO_DIAG=1` | 1 Hz `vid:`/`audio:` health lines on core0 (fps, audio production rate, queue level, underruns) |
| `-DDUKE_HSTX_DEBUG=1` | pico_hdmi's own core1 register dump. **Costs ~33 ms/s of core1 and starves the audio** — for debugging an HSTX wedge only, never for judging audio |
| `-DDUKE_OPL_RENDERER=linear` | cheaper OPL renderer (~half the cost, quality unvalidated on device); default is `reference` |
| `-DDUKE_PSRAM_SIZE=4096k` | shrink the linker's PSRAM region (the runtime clamp usually makes this unnecessary) |

## Cutting a release

Releases are built by
[.github/workflows/BuildAndRelease.yml](.github/workflows/BuildAndRelease.yml) on
a self-hosted runner when a `v*` tag is pushed. The workflow is a thin wrapper:
everything it does is `./buildAll.sh` and `./release-notes.sh`, so running those
two scripts locally *is* testing the pipeline.

```sh
./buildAll.sh                    # fills releases/ -- exactly what CI runs
./release-notes.sh v1.0          # writes release-notes.md, the release body
```

`buildAll.sh` builds the standalone variant for all three boards and renames each
`duke3d_game.uf2` to `duke3d_game_<board>.uf2` — three files, and that is the
whole release. There is no data file to ship: `DUKE3D.GRP` streams from the SD
card, so nothing lives in flash beside the image.

The pico-bootLoader variants are **not** released here. They are built in the
[pico-bootLoader](https://github.com/fhoedemakers/pico-bootLoader) repository,
which invokes `<board>-build-forbootloader.sh` itself and links each image into
its own app partition.

Boards, human-readable names and the caveats printed in the notes all come from
[boards.sh](boards.sh), sourced by both scripts, so the artifacts and the
"what do I flash?" table cannot drift apart. Adding a board to a release is one
line there once its `<tag>_cflags.h` and `<tag>-build.sh` exist.

`CMAKE_ARGS="-DDUKE_RELEASE_VERSION=v1.0"` stamps the tag into the UF2's binary
info; `buildAll.sh` prints `picotool info` for every artifact so the stamp is
visible in the CI log. An ordinary build stamps `dev`, and no tracked file is
rewritten to cut a release.

The runner needs `PICO_SDK_PATH` and `PICO_PIO_USB_PATH` (set in the workflow to
`/datalocal/pico/...`), plus `picotool` ≥ 2.2.0, `ninja` and `arm-none-eabi-gcc`
on `PATH`. The SDK must be 2.2.0 with its own `lib/tinyusb` checked out;
`buildAll.sh` checks all of this up front rather than failing deep inside CMake.

### Steps

1. Add a `## vX.Y` section to [CHANGELOG.md](CHANGELOG.md) — the heading text
   must be exactly the tag, since `release-notes.sh` extracts that section.
2. Commit, then dry-run on the runner without publishing anything:
   `gh workflow run BuildAndRelease.yml` (leave the `tag` input empty).
3. `git tag vX.Y && git push origin vX.Y`.

## SD card

```
/roms/duke3d/DUKE3D.GRP      the game data (shareware or registered/Atomic)
/roms/duke3d/game0.sav       savegames land here, next to the GRP
/roms/duke3d/duke3d.cfg      settings
/emu/8/duke3d_game.uf2       bootloader variant, Fruit Jam
/emu/13/duke3d_game.uf2      bootloader variant, Murmulator M2
/emu/2/duke3d_game.uf2       bootloader variant, Adafruit DVI + SD
```

## When something is wrong

You do **not** need a serial cable to find out. The DOS-style startup screen is
live on the HDMI output from the first line of boot, and anything fatal is
painted on it under the log that led to it, with a red `FATAL ERROR` bar:

```
              FATAL ERROR
 duke_fatfs: MOUNT FAILED (3)

 *** FATAL ERROR ***
 No SD card (FatFs error 3).
 Insert a FAT/exFAT-formatted card with
 DUKE3D.GRP in /roms/duke3d.

 Press RESET (returns to the boot menu).
```

The board then **halts** rather than rebooting, so the message stays readable —
every condition that gets here is permanent, and a reboot would only show it
again too briefly to read. Reported this way:

| | |
|---|---|
| No PSRAM, or too small for the engine arrays | `src/pico/duke_psram.c` |
| No SD card, or not FAT/exFAT formatted | `src/pico/duke_fatfs_io.c` |
| `/roms/duke3d` or `DUKE3D.GRP` missing | `src/pico/duke_fatfs_io.c` |
| Corrupt or unrecognised GRP, CON errors, missing `TILES0xx.ART`, cache overflow, out of sound handles | engine/game `Error()` and `DUKE_FATAL_ABORT()` |

In-game failures work too: the console keeps recording every `printf` after the
game takes the screen, so a fatal mid-level brings it back with the last 24 lines
of log still on it. See `src/pico/duke_fatal.c`.

## Controls

Everything reaches the game as **DOS scancodes**. `duke_usb_input.cpp` posts them
to `pico_display_post_key()` → Duke's `keyhandler()`, which updates both
`KB_KeyDown[]` (menus) and `CONTROL_UpdateKeyboardState()` (in-game bindings). So
one table drives menu navigation *and* gameplay, and a gamepad button is
indistinguishable from the key it stands for.

There are **three gamepad layouts**, chosen with **PAD LAYOUT** in
*Options → Game Options → Gamepad Setup*: the six-button SNES one below, a
four-button [NES layout](#nes-pad-layout) for a vintage NES controller or a
NES-shelled MantaPad, and a [Retro-Go layout](#retro-go-layout) modelled on
[duke3d-go](https://github.com/DynaMight1124/retro-go/blob/megapack/duke3d-go/CONTROLS.md).
The same menu carries **SHIFT MODE**, Retro-Go's held-START hotkey layer. Both
settings persist in `duke3d.cfg` (`Misc/NesPadLayout` — a legacy key name — and
`Misc/PadShiftLayer`) and apply to every pad at once, USB and the legacy ports
alike.

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
  onto `B` (which duplicates START anyway) — one line in `snesLayout()` in
  `src/pico/duke_usb_input.cpp`.

#### NES pad layout

Set **PAD LAYOUT** to `NES`. A NES controller has four
buttons and Duke needs fire, open, jump, crouch, weapons, strafe and the menu, so
**SELECT is a shift layer** rather than a button of its own — it does nothing on
its own, which means there is no press-versus-hold to get wrong.

| | base | SELECT held |
|---|---|---|
| **A** | **fire** (Left Ctrl) | next weapon (`'`) |
| **B** | open / use (Space) | crouch (`Z`) |
| **START** | jump (`A`) | **open the menu** (Escape) |
| **D-pad ←→** | turn | strafe left / right (`,` `.`) |
| **D-pad ↑↓** | move forward / back | move forward / back |

In a menu the layer is ignored: **A** confirms, **B** and **START** go back, the
D-pad navigates. That is why `menues.c` needs no changes of its own — `probe()`
already takes Enter to confirm and Escape to go back.

Three things worth knowing:

* **No Run button is left**, so switching the option on also turns Duke's own
  **AutoRun** on, and `CONFIG_ReadSetup` re-forces it at every startup. You are
  always running.
* Both face-button pairs are accepted as the NES A/B, so the layout works from a
  SNES-shaped pad too — there **B** and **Y** sit where a NES pad's buttons are.
  (A NES-shelled MantaPad reports its buttons on io `A` and io `X`; a NES pad in
  a DE-9 port arrives on io `B` and io `Y`.)
* A button already held when the menu opens or closes, or when the SELECT layer
  goes up or down, is **ignored until you let go** of it — otherwise chording the
  menu open while firing would confirm the highlighted item on the next poll.
  Same when the option itself is toggled: you are pressing A at that moment, and
  A means something different on the other side of the switch.

#### Retro-Go layout

Set **PAD LAYOUT** to `RETRO`. This is Retro-Go's
[duke3d-go](https://github.com/DynaMight1124/retro-go/blob/megapack/duke3d-go/CONTROLS.md)
arrangement, which puts fire on **A** and use on **START** instead of the DOS-ish
default. Retro-Go's handhelds have ten buttons and a SNES pad has eight: their
`OPTION` only duplicated crouch, so it is dropped, and `MENU` becomes a chord.

| Button | Sends | In game |
|---|---|---|
| D-pad / left stick | arrow keys | move + turn |
| **A** | Left Ctrl | **fire** |
| **B** | `A` | jump |
| **X** | `Z` | crouch |
| **Y** | `J` | jetpack |
| **L** / **R** | `,` / `.` | strafe left / right |
| **SELECT** | `'` | next weapon |
| **START** | Space | open / use |
| **L+R** | Escape | **open the menu** |
| **SELECT+START** | Escape | open the menu (see below) |
| **C** | `Z` | crouch again — only Genesis pads report a `C` |

**L+R is the chord to reach for.** It is the only one with no side effect at all:
`player.c` gives `Strafe_Left` `svel += keymove` and `Strafe_Right`
`svel += -keymove`, so held together they cancel exactly. **SELECT+START** is
accepted as well because a plain NES pad in a DE-9 port reports no shoulder
buttons and would otherwise be locked out of the menu — it costs one visible
weapon switch on the way in, which is why it is not the primary chord.

As in the NES layout there is **no Run button** (L and R are strafe), so selecting
`RETRO` turns Duke's own **AutoRun** on and `CONFIG_ReadSetup` re-forces it at
every startup.

##### Shift mode

**SHIFT MODE** turns Retro-Go's hotkey layer on. Duke wants more actions than a
pad has buttons, so **holding START for 500 ms** turns the D-pad into the
inventory:

| Button | Sends | Action |
|---|---|---|
| **D-pad ↑** | Enter | **use inventory item** (medkit, steroids…) |
| **D-pad ↓** | `J` | jetpack |
| **D-pad ←→** | `[` / `]` | previous / next inventory item |
| **B** | PgDn | look down |
| **X** | PgUp | look up |

**A**, **Y**, **L** and **R** keep firing, jetpacking and strafing, so only those
six change. **SELECT** is the exception: START is held by definition here, so
SELECT completes the SELECT+START chord and opens the menu — which is how a pad
with no shoulder buttons gets out of the layer.

Four things worth knowing:

* **Nothing happens during the 500 ms**, which Retro-Go calls the *transparent
  hold* — you keep running and turning while the timer expires. Once the layer
  engages the D-pad no longer walks, and a direction held across that moment is
  **ignored until you let go**: otherwise running forward would instantly burn an
  inventory item.
* **Use fires when you release START**, not when you press it, because START
  cannot act until we know whether the hold reached 500 ms. The press length is
  then arbitrary, and Duke samples `KB_KeyDown[]` only once per `TICSPERFRAME`
  (~38 ms at worst), so a quick tap would be missed entirely — the release is
  therefore stretched into a **100 ms pulse**. It cannot open a door twice:
  `Open` is bit 29 and `sector.c` edge-triggers that whole bit group through
  `p->interface_toggle_flag`, so a held `Open` acts exactly once. The same is true
  of Jetpack, Inventory, the weapon nibble and Escape, which is why none of the
  hotkeys above need debouncing. `Look_Up`/`Look_Down` are deliberately *outside*
  that mask, i.e. continuous.
* **Releasing START after the layer engaged does not use anything** — the hold is
  what you asked for, so there is no Use on the way out.
* **SHIFT MODE off removes the layer and the release latency both**: START goes
  back to being a plain immediate Use, since there is then nothing to tell apart.
  It has no effect on the SNES or NES layouts, which have no START to spare.

#### MantaPad (cheap AliExpress SNES pad, VID 081f)

This pad can act as either a NES or a SNES controller, and pico_shared defaults
it to **NES mode** until the player presses Y. NES mode reports only two face
buttons, and because its "NES B" *is* physically the SNES **X**, the mapping came
out shuffled: the pad's X arrived as logical A, and its A arrived as logical
**B → Escape**. That is why pressing what looked like fire opened the menu, and
why pressing Y — or navigating a menu, which makes you press Y — appeared to
"fix" it.

Duke needs all four face buttons regardless, so this port defaults the pad to
SNES mode via `MANTAPAD_DEFAULT_SNES_MODE` in the board cflags header. The boot
log reads `defaulting to SNES mode` instead of `Press Y to activate SNES mode`,
and X is fire from the first frame — no Y press, no level restart.

### NES/SNES controller ports (Murmulator M2 and Adafruit DVI-SD)

Those boards' two DE-9 ports are polled over PIO by the vendored `pico_shared`
nespad driver and folded into the **same** scancode stream as the USB pads, so a
**SNES pad in a port behaves exactly like a USB SNES pad** — the tables above
apply unchanged, one set of layouts to reason about. Both ports are OR-ed
together, so two pads drive one Duke.

A **plain NES pad has no fire button in the default layout.** Its two buttons
report in the SNES serial positions B and Y, which that layout maps to Escape and
Space; fire lives on SNES **X**, which a NES pad does not have. The driver masks
the ID bits that distinguish the two pad shapes before the game sees them, so it
cannot be detected and corrected at runtime — that is exactly what the
[NES layout](#nes-pad-layout) is for. Such a pad has no shoulder buttons either,
so in the [Retro-Go layout](#retro-go-layout) **SELECT+START** is its only way
into the menu.

USB input still works alongside the ports, through the RP2350's native USB
controller — a pad or keyboard needs an OTG/host adapter on the module's own
socket.

### USB keyboard

A keyboard works alongside the pad — both are live at once, and neither can tell
it is sharing. All letters, digits and punctuation are translated, plus F1–F12,
the whole keypad, Backspace, Tab, Enter, Escape, Space, Caps Lock, Scroll Lock,
Pause, Num Lock and the arrows. Shift is tracked as a real scancode, so
`KB_Getch()` picks the shifted ASCII table and capitals work when you type a save
name.

#### What the keys do

Duke's own defaults, written into `duke3d.cfg` on first run and **rebindable** in
*Options → Setup Keyboard*. Where the table says `Kpad`, that keypad key is the
only one that works here — see [the two gaps](#two-deliberate-gaps) below.

Moving:

| Action | Key |
|---|---|
| Move forward / backward | **↑** / **↓**, or Kpad8 / Kpad2 |
| Turn left / right | **←** / **→**, or Kpad4 / Kpad6 |
| Strafe left / right | **`,`** / **`.`** |
| Strafe (hold: the turn keys sidestep instead) | **Alt** |
| Run (hold) | **Shift** |
| AutoRun on / off | **Caps Lock** |
| Jump | **`A`** or **`/`** |
| Crouch | **`Z`** |
| Look up / down | **Kpad9** / **Kpad3** |
| Look left / right | **Kpad0** / **Kpad.** |
| Aim up / down | **Kpad7** / **Kpad1** |
| Centre the view | **Kpad5** |

Fighting:

| Action | Key |
|---|---|
| Fire | **Ctrl** |
| Open / use / flip a switch | **Space** |
| Quick kick | **`C`** |
| Next / previous weapon | **`'`** / **`;`** |
| Select weapon 1–10 | **`1`**…**`9`**, **`0`** |
| Turn around | **Backspace** |
| Holster weapon | **Scroll Lock** |
| Hide the on-screen weapon | **`S`** |
| Auto-aim on / off | **`V`** |
| Crosshair on / off | **`I`** |

Inventory — each fires once per press, however long you hold it:

| Action | Key |
|---|---|
| Use the selected item | **Enter** or Kpad Enter |
| Previous / next item | **`[`** / **`]`** |
| Medkit | **`M`** |
| Steroids | **`R`** |
| Jetpack | **`J`** |
| Night vision | **`N`** |
| Holo Duke | **`H`** |

Screen and system:

| Action | Key |
|---|---|
| Open the menu | **Escape** |
| Pause | **Pause** |
| Overhead map | **Tab** |
| Map follow mode | **`F`** |
| Shrink / enlarge the view | **`-`** / **`=`**, or Kpad- / Kpad+ |
| Console (Chocolate Duke leftover, does nothing) | **`` ` ``** |
| Send message, show opponent's weapon, co-op view | **`T`**, **`W`**, **`K`** — multiplayer only |
| Mouse aiming toggle | **`U`** — no mouse on this port |

Function keys, handled by the game itself and **not** rebindable:

| Key | Does |
|---|---|
| **F1** | help screen — Space or Enter pages through it |
| **F2** / **F3** | save game / load game |
| **F4** | sound setup menu |
| **F5** | name the current music track; **Shift+F5** changes it |
| **F6** | quicksave — opens the save menu instead until you have made one save |
| **F7** | third-person view on / off |
| **F8** | messages on / off |
| **F9** | quickload |
| **F10** | quit |
| **F11** | brightness up; **Shift+F11** down |
| **F12** | screenshot — **reports "SCREEN SAVED" but writes nothing**, because `screencapture()` is a stub on this port (`src/pico/pico_display.c`) |

#### Two deliberate gaps

* **Insert / Home / PageUp / Delete / End / PageDown** are omitted, and so is
  PrintScreen. Those six are extended (`0xE0`-prefixed) on a PC keyboard and Duke
  reaches them through its own `extscanToSC` table, so posting a bare code would
  mean a different key. It costs nothing: every one of them is *Duke's first*
  binding for look and aim, and its *second* binding is a keypad key — which is
  why the tables above put look and aim on Kpad9/Kpad3/Kpad0/Kpad./Kpad7/Kpad1.
  The keypad always works regardless of Num Lock, since a USB keyboard reports
  keypad keys as their own HID usages and the host decides what Num Lock means.
* **Arrows** use Duke's remapped extended codes rather than `0xE0` pairs, since
  this layer posts single bytes. That is why the arrow and keypad entries differ
  even though a PC keyboard shares their scancodes.

Two smaller details: the **right-hand Ctrl, Shift and Alt** are folded onto the
left ones, which matches Duke's own defaults (it binds both), and the **Windows /
GUI keys send nothing** — no scancode is defined for them.

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
murmulatorm2_cflags.h          board config (HW_CONFIG 13), likewise
adafruitdvisd_cflags.h         board config (HW_CONFIG 2), likewise
<tag>-build.sh                 standalone build for that board
<tag>-build-forbootloader.sh   pico-bootLoader app-partition build
boards.sh                      which boards a release covers, and their metadata
buildAll.sh release-notes.sh   the release pipeline; CI runs exactly these two
CMakeLists.txt                 board selection + SDK init + driver subdirs + src/pico
cmake/psram_linker.cmake       generates the linker script: PSRAM region, engine
                               .bss -> PSRAM, audio path -> SRAM, stack carve-out,
                               and the bootloader FLASH relink (single owner)
cmake/BootPartition.cmake      bootloader flash map
src/Engine/ src/Game/          vendored Chocolate Duke3D, edited in place
src/pico/                      RP2350 platform layer (replaces BUILD's sdlayer.c)
  duke_boot.c                  entry: clocks, PSRAM, video, SD, exit -> reset
  pico_display.c               BUILD baselayer: video, palette, timer, input entry
  duke_audio.c duke_music.c    dual-sink mixer + OPL2 music
  duke_usb_input.cpp           pad/keyboard -> DOS scancodes (table above)
  duke_dostext.cpp             DOS startup screen
  duke_fatal.c                 fatal errors on that screen, then halt
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
- **`ENABLE_PIO_USB` and the header's `HAS_USBPIO` must agree.** They configure
  different halves of one decision — which `hcd_*.c` is linked, and which one
  `tusb_config.h` asks for — and disagreeing versions link cleanly and boot a
  board with no input at all. `duke_usb_input.cpp` `#error`s on the mismatch;
  keep that check if you touch the USB wiring.
- **Core voltage stays at `VREG_VOLTAGE_1_50` for 378 MHz.** 1.60 V bootloops
  some boards.
