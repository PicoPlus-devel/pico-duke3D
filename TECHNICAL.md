# pico-duke3D — technical notes

Implementation notes for this port: how it is put together, what differs per board,
how to build and release it, and the things that will bite you if you change the
wrong thing. For installing and playing, see [README.md](README.md).

- [What it is](#what-it-is)
- [Status](#status)
- [Boards](#boards)
- [Build](#build)
- [Cutting a release](#cutting-a-release)
- [Input plumbing](#input-plumbing)
- [Repository layout](#repository-layout)
- [Notes worth knowing before changing things](#notes-worth-knowing-before-changing-things)

## What it is

Duke Nukem 3D (Chocolate Duke3D / BUILD engine) ported to the RP2350, for the
**Adafruit Fruit Jam** (`HW_CONFIG 8`), the **Murmulator M2** (`HW_CONFIG 13`),
**Adafruit DVI + SD breakout boards** (`HW_CONFIG 2`) and the **Olimex
RP2040-PICO-PC with a Pico 2** (`HW_CONFIG 15`). Built on the same skeleton as
Frank's `fruitjam-doom` port, reusing his `pico_shared` driver library.

`DUKE3D.GRP` is **streamed from the SD card** via BUILD's `cache1d` — it does not fit
in the 8 MB PSRAM, which instead holds the tile cache and the engine's map arrays
(`sector`/`wall`/`sprite`, relocated there by a generated linker script).

## Status

Playable, with sound and music, and **hardware-verified on all four boards**. What was
verified on the Fruit Jam:

- **378 MHz @ 1.50 V**, `clk_hstx` a fixed 126 MHz from a retasked PLL_USB.
- **HDMI** 640×480p60 over HSTX (`pico_hdmi`), core1-owned, ~30 fps in-level.
- **PSRAM** 8 MB on QMI CS1; **SD** over SPI0 with FatFs.
- **Sound effects and OPL2 music**, with headphone-jack detection switching between
  I2S (TLV320DAC3100) and HDMI data-island audio — plug in and HDMI audio mutes,
  unplug and it returns.
- **USB gamepad and keyboard** over PIO-USB.
- **DOS-style startup screen** on the HDMI output (40×25, pico_shared's 8×8 font),
  live from the first line of boot, with fatal errors reported on it.
- Bootloader variant, quit-to-picker, save/load and settings persistence.

The **Murmulator M2**, **Adafruit DVI + SD** and **Olimex RP2040-PICO-PC** builds are
hardware-verified too. They differ from the Fruit Jam in the clock source for
`clk_hstx`, the audio DAC and routing, the USB host controller, and the HSTX lane
inversion — see [Boards](#boards) for the whole list.

## Boards

The board is chosen at configure time by `DUKE_BOARD`, which force-includes
`<tag>_cflags.h` into every translation unit. Everything the C code branches on lives
in that one header.

| | Fruit Jam (`fruitjam`) | Murmulator M2 (`murmulatorm2`) | Adafruit DVI + SD (`adafruitdvisd`) | Olimex PICO-PC + Pico 2 (`olimexpicopc`) |
|---|---|---|---|---|
| `HW_CONFIG` | 8 | 13 | 2 | 15 |
| `PICO_BOARD` | `adafruit_fruit_jam` | `pico2` | `pimoroni_pico_plus2_rp2350` | `pico2` |
| Flash | 16 MB | 4 MB | 16 MB | 4 MB, the last 260 KB held by pico-launcher |
| Video | HSTX 640×480p60, lanes 13/15/17/19 **inverted** | same | lanes **12/14/16/18, not inverted** | lanes 13/15/**19/17** (D1/D2 swapped), inverted |
| `clk_hstx` | 126 MHz from a **retasked PLL_USB** (jitter-free) | 126 MHz **from `clk_sys`** (378/3) — PLL_USB is spoken for | same as the M2 | same as the M2 |
| Audio DAC | TLV320DAC3100 + headphone detect | PCM5100A, no codec, no detect | PCM5100A (**optional module**), no detect | **none** — PWM audio jack on GP28 (L) / GP27 (R) |
| Audio routing | **exclusive**: headphones *or* HDMI | **both** sinks always live | **both** sinks always live | **both** HDMI and the PWM jack always live |
| USB host | Pico-PIO-USB on GP1/GP2 | **native** RP2350 controller (OTG adapter) | **native** RP2350 controller (OTG adapter) | **native** RP2350 controller (USB-A socket) |
| Controller ports | — | two NES/SNES ports on PIO (shared CLK/LAT) | two NES/SNES ports on PIO (independent CLK/LAT) | one NES/SNES port on UEXT (CLK 5 / LAT 9 / DATA 20) |
| Wii extension port | GP20/21 — **the codec's own I2C bus** | GP0/1 (why there is no UART) | — | — |
| Status LEDs | LED GP29 + **5 NeoPixels GP32 as a VU meter** | LED GP25 | LED GP25 | LED GP25 |
| PSRAM | 8 MB on CS1 = GP47 | on CS1 = **GP8** | 8 MB on CS1 = GP47 | **must be fitted**, on CS1 = GP8 |
| SD (SPI0) | MOSI 35 / MISO 36 / SCK 34 / CS 39 | MOSI 7 / MISO 4 / SCK 6 / CS 5 | MOSI 3 / MISO 4 / SCK 2 / CS 5 | MOSI 7 / MISO 4 / SCK 6 / CS 22 |
| UART console | UART0 on GP44/45 | **none** — GP0/1 are the Wii connector | board default (GP0/1) | **none** — GP0/1 are the PS/2 port |

`adafruitdvisd` is a Pico 2 footprint (breadboard or Frank's PCB) with the
[Adafruit DVI Breakout 4984](https://www.adafruit.com/product/4984) on HSTX and the
[MicroSD breakout 254](https://www.adafruit.com/product/254) on SPI0. **The module
must be a Pimoroni Pico Plus 2**, not a stock Pico 2: Duke cannot run without PSRAM,
and `HW_CONFIG 2` puts the PSRAM chip select on GP47 = QMI CS1, which only exists on
an RP2350B. Its HSTX lanes are the only ones in this table that are *not* inverted —
a garbled or absent picture there points at `GPIOHSTXINVERTED` first.

`olimexpicopc` is the Olimex RP2040-PICO-PC with a Raspberry Pi Pico 2 plugged in
instead of the original Pico. A stock Pico 2 has no PSRAM, so one has to be fitted
on GP8 (QMI CS1) before Duke will start. The board has no I2S DAC: `DUKE_AUDIO_I2S_DRIVER`
is 0, `DSL_Init()` skips the I2S setup, and every sample pushed to HDMI is also handed
to the vendored pico_shared `pwm_audio` driver, which plays it on the board's audio jack.
That driver keeps a 2048-frame ring in SRAM and drains it from a PWM-wrap interrupt on
core0 at 48 kHz (378 MHz / 7875); it drops or repeats a sample when the ring leaves its
1/4..3/4 band, so the HDMI ring alone paces the mixer. The PS/2 port on GP0/1 is not
used.

Two consequences worth knowing on the Murmulator:

* **No serial log.** The DOS-style startup screen on HDMI is the log — it mirrors
  every `printf`. The hardfault breadcrumb in `duke_boot.c` is compiled out with it
  (poking `uart0` while the peripheral is held in reset would fault inside the fault
  handler). To get a console back, drop `NO_USE_UART` from `murmulatorm2_cflags.h`
  and `-DDUKE_NO_STDIO_UART=1` from the build script — but GP0/1 are the Wii
  extension port, and now genuinely driven as `i2c0`, so you would have to give up
  Wii pads to get the console back.
* **PSRAM smaller than 8 MB still boots.** The 8 MB linker region is an
  address-space declaration; the heap top is clamped at boot to what `SetupPsram()`
  reports and the cache1d tile cache shrinks to fit. Roughly 2.1 MB of engine arrays
  are the hard floor — below about 4 MB total the port is not worth running.

Two notes on the last two rows:

* **The Fruit Jam's Wii port is the codec's I2C bus**, pin for pin. An uninitialised
  SNES-Classic pad holds SDA low, so the pad is brought up first — before anything
  talks to the DAC — with the codec held in reset. Without that, a pad attached at
  power-on means no sound at all. See `duke_wiipad_init()`.
* **Status LEDs** are the plain onboard LED, blinked 60 game frames on / 60 off (so
  its period follows the frame rate — roughly a second each way at 60 fps, slower in
  a busy level), plus the Fruit Jam's five NeoPixels driven as an audio VU meter. The
  meter is a 5-pixel bargraph, so it only exists on that board. Both are in
  `src/pico/duke_leds.c`; set the pins to `-1` to switch them off.

Adding a board is a `<tag>_cflags.h` plus a `<tag>-build.sh`; nothing in
`CMakeLists.txt` is board-specific.

## Build

Requires `PICO_SDK_PATH` in the environment (SDK 2.2.0, arm-none-eabi-gcc 13.2.Rel1)
and `picotool`.

```sh
./fruitjam-build.sh                      # -> build_fruitjam/src/pico/duke3d_game.uf2
./fruitjam-build-forbootloader.sh        # -> build_bl_fruitjam/...
./murmulatorm2-build.sh                  # -> build_murmulatorm2/src/pico/duke3d_game.uf2
./murmulatorm2-build-forbootloader.sh    # -> build_bl_murmulatorm2/...
./adafruitdvisd-build.sh                 # -> build_adafruitdvisd/src/pico/duke3d_game.uf2
./adafruitdvisd-build-forbootloader.sh   # -> build_bl_adafruitdvisd/...
./olimexpicopc-build.sh                  # -> build_olimexpicopc/src/pico/duke3d_game.uf2
./olimexpicopc-build-forbootloader.sh    # -> build_bl_olimexpicopc/...
```

A plain `cmake -S . -B build` (VSCode / CMake Tools) still configures for the Fruit
Jam. Note that it builds `./build` while the scripts build `build_<tag>` — if a fix
appears not to work, check which tree you flashed.

The `-forbootloader` variants link the image into the pico-bootLoader's app partition
at `0x10080000` instead of owning flash from `0x10000000`, so the picker can launch
it — copy that UF2 to `/emu/<HW_CONFIG>/` on the SD card. Any reset returns to the
picker, as does quitting Duke.

| Build | Image | App slot | Flash total |
|---|---|---|---|
| Fruit Jam standalone | `0x10000000` | — | 16 MB |
| Fruit Jam bootloader | `0x10080000` | 15.5 MB | 16 MB |
| Murmulator standalone | `0x10000000` | — | 4 MB |
| Murmulator bootloader | `0x10080000` | 3.5 MB | 4 MB |
| Adafruit DVI+SD standalone | `0x10000000` | — | 16 MB |
| Adafruit DVI+SD bootloader | `0x10080000` | 15.5 MB | 16 MB |
| Olimex PICO-PC standalone | `0x10000000` | — | 4 MB |
| Olimex PICO-PC bootloader | `0x10080000` | 3.24 MB (ends at pico-launcher, `0x103BF000`) | 4 MB |

Duke keeps **nothing** in flash — `DUKE3D.GRP` streams from SD and savegames sit next
to it — so the whole partition goes to the app on every board. Only the 4 MB boards
need the totals overridden (`-DDUKE_APP_SIZE` / `-DDUKE_FLASH_TOTAL` in their scripts);
the other two match the defaults in `cmake/BootPartition.cmake`. On the Olimex board
the app size also stops short of the last 260 KB of flash, which hold pico-launcher,
so the linker refuses an image that would overwrite it. The pico-bootLoader does not
build for `HW_CONFIG 15` yet; the bootloader variant is ready for when it does.

Useful options:

| Option | Effect |
|---|---|
| `-DDUKE_VIDEO_DIAG=1` | 1 Hz `vid:`/`audio:` health lines on core0 (fps, audio production rate, queue level, underruns) |
| `-DDUKE_HSTX_DEBUG=1` | pico_hdmi's own core1 register dump. **Costs ~33 ms/s of core1 and starves the audio** — for debugging an HSTX wedge only, never for judging audio |
| `-DDUKE_OPL_RENDERER=linear` | cheaper OPL renderer (~half the cost, quality unvalidated on device); default is `reference` |
| `-DDUKE_PAD_DIAG=1` | log the internal gamepad button word on every change — per source, decoded to names. **Use this before editing any button mapping**, see below |
| `-DDUKE_PSRAM_SIZE=4096k` | shrink the linker's PSRAM region (the runtime clamp usually makes this unnecessary) |

**Every option above is sticky in `CMakeCache.txt`.** Re-running a build script without
the flag does *not* turn it off — pass `-DDUKE_PAD_DIAG=0` or delete the build tree.
Silently shipping a diagnostic build is easy to do otherwise.

**`DUKE_PAD_DIAG` exists because pads do not agree on which internal bit a face button
reports.** Several USB pads land their A on `io Y` rather than `io A`. Reasoning about a
mapping from the button *names* is therefore unreliable — press one button at a time with
this enabled, read what actually arrives, and only then change a table:

```
pad: usb=00000040 nes=00000000 wii=00000000 -> 00000040 [SELECT] RETRO
```

**The build is not warning-guarded.** `src/pico/*.cpp` is compiled with no `-Wall` or
`-Wextra`, and `libduke` (the vendored engine and game) with `-w`
(`cmake/duke_engine.cmake`). Unused functions, enum slots and constants are therefore
completely silent — after a removal, grep is your only gate, not the compiler.

**`#if DUKE_HAS_WIIPAD` and `#if DUKE_HAS_NESPAD` are never both compiled on one
board** (fruitjam = Wii only, adafruitdvisd = NES only, murmulatorm2 = both), so a
stale signature in one half will not show up on the wrong board. Build `fruitjam`
*and* `adafruitdvisd` at minimum.

## Cutting a release

Releases are built by
[.github/workflows/BuildAndRelease.yml](.github/workflows/BuildAndRelease.yml) on a
self-hosted runner when a `v*` tag is pushed. The workflow is a thin wrapper:
everything it does is `./buildAll.sh` and `./release-notes.sh`, so running those two
scripts locally *is* testing the pipeline.

```sh
./buildAll.sh                    # fills releases/ -- exactly what CI runs
./release-notes.sh v1.0          # writes release-notes.md, the release body
```

`buildAll.sh` builds the standalone variant for all three boards and renames each
`duke3d_game.uf2` to `duke3d_game_<board>.uf2` — three files, and that is the whole
release. There is no data file to ship: `DUKE3D.GRP` streams from the SD card, so
nothing lives in flash beside the image.

The pico-bootLoader variants are **not** released here. They are built in the
[pico-bootLoader](https://github.com/fhoedemakers/pico-bootLoader) repository, which
invokes `<board>-build-forbootloader.sh` itself and links each image into its own app
partition.

Boards, human-readable names and the caveats printed in the notes all come from
[boards.sh](boards.sh), sourced by both scripts, so the artifacts and the "what do I
flash?" table cannot drift apart. Adding a board to a release is one line there once
its `<tag>_cflags.h` and `<tag>-build.sh` exist.

`CMAKE_ARGS="-DDUKE_RELEASE_VERSION=v1.0"` stamps the tag into the UF2's binary info;
`buildAll.sh` prints `picotool info` for every artifact so the stamp is visible in the
CI log. An ordinary build stamps `dev`, and no tracked file is rewritten to cut a
release.

The runner needs `PICO_SDK_PATH` and `PICO_PIO_USB_PATH` (set in the workflow to
`/datalocal/pico/...`), plus `picotool` ≥ 2.2.0, `ninja` and `arm-none-eabi-gcc` on
`PATH`. The SDK must be 2.2.0 with its own `lib/tinyusb` checked out; `buildAll.sh`
checks all of this up front rather than failing deep inside CMake.

### Steps

1. Add a `## vX.Y` section to [CHANGELOG.md](CHANGELOG.md) — the heading text must be
   exactly the tag, since `release-notes.sh` extracts that section.
2. Commit, then dry-run on the runner without publishing anything:
   `gh workflow run BuildAndRelease.yml` (leave the `tag` input empty).
3. `git tag vX.Y && git push origin vX.Y`.

## Input plumbing

Everything reaches the game as **DOS scancodes**. `duke_usb_input.cpp` posts them to
`pico_display_post_key()` → Duke's `keyhandler()`, which updates both `KB_KeyDown[]`
(menus) and `CONTROL_UpdateKeyboardState()` (in-game bindings). So one mapping drives
menu navigation *and* gameplay, and a gamepad button is indistinguishable from the key
it stands for. The user-facing tables are in [README.md](README.md#controls).

**The pad layer does not diff per button.** It builds an effective *set* of scancodes
and diffs that once per poll (`postKeyDiff`, `ScIdx`/`kScancode`). That is necessary
because one scancode is reachable from different buttons depending on menu state, and it fixed a latent bug the old per-button code had: B and START both mean
Escape inside a menu, so releasing START while B was held used to post an Escape
key-*up*. Every pad source — both USB pads, both DE-9 ports, the Wii pad — is OR-ed
into one button word first, so several pads drive one Duke and a button held on one
stays held as another releases the same button.

`duke_menu_is_active()` in `menues.c` is what lets one button mean fire in game and
confirm in a menu. **Do not use the `in_menu` global in `game.c`** — it is only
refreshed inside `playback()`, i.e. on the title/demo loop, so it is stale for the
whole of a level. The authoritative flag is `ps[myconnectindex].gm & MODE_MENU`, with
`MODE_TYPE` folded in so that typing a message counts too.

**Duke needs no generated menu art, ever.** `menutext()` composes a label character
by character from the `BIGALPHANUM` font tiles, and `gametext()` does the same over
`STARTALPHANUM`..`ENDALPHANUM` for the whole printable ASCII range. Any new menu
string is free — no GRP editing, no vpatch, no tooling. (This is why the pico-doom
port needed a vpatch generator and this one does not: Doom's menu items are
pre-rendered image lumps, Duke's are a font.)

### The held-button latch

A button already down when the menu opens or closes, or when the shift layer engages or
releases, would immediately act out its new meaning — chording the menu open while firing
would confirm the highlighted item on the very next poll. `retroLayout()` therefore
ignores whatever is held across such a transition until it is released, which doubles as
the chord suppressor. The latch covers the **whole** button word including the directions,
because with the layer up those are inventory actions and a direction held as the layer
engages would fire one.

### The shift layer's two timing constants

`kShiftHoldUs` (500 ms) and `kUsePulseUs` (100 ms), both in `duke_usb_input.cpp`.
START cannot act on press, since we have to see whether the hold reaches 500 ms, so
Use fires on *release* — and because the press length is then arbitrary while Duke
samples `KB_KeyDown[]` only once per `TICSPERFRAME` (~38 ms at worst), a quick tap
would be missed entirely. Hence the 100 ms pulse.

It cannot double-fire: `Open` is bit 29 and `sector.c` edge-triggers that whole bit
group through `p->interface_toggle_flag`, so a held `Open` acts exactly once. The same
is true of Jetpack, Inventory, Inventory_Left/Right, the weapon nibble and Escape,
which is why none of the hotkeys need repeat suppression of their own.
`Look_Up`/`Look_Down` are deliberately *outside* that mask, i.e. continuous.

**The `hold.reset()` on `duke_menu_is_active()` is not optional.** `probe()` reads
PgUp/PgDn as menu up/down and the layer's look keys *are* PgUp/PgDn; `probe()` also
takes Space as confirm and a draining Use pulse *is* Space. Drop that reset and the
pad scrolls and confirms menus by itself.

### One rule: a pad's A is always `io A`

Everything about the pad layer got simpler once this was true, and a lot of past
complexity was working around it not being true.

**A NES pad shifts out only 8 buttons, so its A and B land in the SNES *serial*
positions B and Y** — the same wires a SNES pad uses for its B and Y. Translated as a
SNES pad, a real NES controller therefore came out with its A on `io B` and its B on
`io Y`, which the layout reads as jump and jetpack: no fire button, and confirm/back
swapped in menus.

The fix is at the source. `nespad_decode()` in the vendored driver **already
distinguishes the two shapes** — an original NES pad leaves clocks 13-16 low, while a
SNES pad drives those 4 ID bits high so it can never set all four — and then strips the
ID bits and discards the verdict. A local addition (`nespad_is_nes[2]`, same pattern as
`nespad_read_ready()`) publishes it, and `nesToButtons(nes, is_nes)` puts a NES pad's
two buttons on `io A` and `io B`, where an ordinary two-button pad has them.

**`nespad_is_nes[]` is a positive ID of an *original* NES pad, not a pad-shape oracle.**
It relies on the 4021's serial input being grounded. Aftermarket and clone NES pads idle
that line high, so they report `nes=0` and are bit-for-bit indistinguishable from a SNES
pad with nothing pressed (measured: A → `0x0001`, B → `0x0002`, `nes=0`, bits 8-11 dead).
No static test can separate them. `padIsNes()` therefore adds the one *dynamic*
discriminator that has no false positives: bits 8-11 are SNES A, X, L and R, and a
two-button pad can never set them, so a port is assumed NES and **latches SNES for good**
the first time it sets one. The price is one press: a genuine SNES pad whose first press
is B or Y — before any A, X, L or R — has that press read as a NES A or B. The latch is
per port and lives until reboot, except that a positive NES ID clears it.

Sister ports are immune for reasons Duke cannot copy: pico-infonesPlus feeds bits 0-7
straight into a NES emulator where bit 0 *is* A, and pico-doom maps bit 0 to fire for
*both* shapes at once and never asks. Duke's four-button layout cannot do the latter
without giving a SNES pad two fire buttons and no crouch or jetpack.

`nesButtons()` returns `io` bits rather than the raw serial word, and translates **each
port separately before merging**, because the verdict is per port and the two sockets can
hold different pad shapes.

That one rule is what let a large amount of machinery be deleted:

* **`menuKeys()` is three lines**: `io A` confirms, `io B` and START go back. It was a
  pair of *per-layout* tables that disagreed with each other — `nesLayout` folded `io Y`
  into back and `io B` into confirm, `retroLayout` did the opposite — plus `io Y`/`io X`
  accepted as spare confirm/back slots. A pad whose A sits on `io Y` therefore had confirm
  and back swap over the moment PAD LAYOUT changed, which is what was reported from
  hardware.
* **`wiiToButtons()` takes no layout** and has one table. Its second table existed only to
  land a NES Classic Mini's two buttons in `nesLayout`'s two folded pairs.
* **The whole NES layout is gone**, along with the PAD LAYOUT option, `pollGamePads()`'s
  `settling` latch and `kDirButtons`. It existed to rescue a DE-9 NES pad that had no fire
  button; once the translation was right, all it added was a SELECT shift layer for crouch
  and strafe — at the cost of a mode in which SELECT stopped cycling weapons and which
  persisted silently in `duke3d.cfg`. That mode cost real debugging time, twice.

**When a button does the wrong thing, do not reason from the button names.** Build with
`-DDUKE_PAD_DIAG=1`, press one button, and read which `io` bit actually arrives. Three
successive wrong fixes in this area came from assuming `io A` meant the pad's A.

### The keyboard's two deliberate gaps

* **Insert / Home / PageUp / Delete / End / PageDown** and PrintScreen are omitted.
  Those are extended (`0xE0`-prefixed) on a PC keyboard and Duke reaches them through
  its own `extscanToSC` table, so posting a bare code would mean a different key. It
  costs nothing: every one is Duke's *first* binding for look and aim and its *second*
  is a keypad key, which is why the README's tables put look and aim on the keypad. The
  keypad always works regardless of Num Lock, since a USB keyboard reports keypad keys
  as their own HID usages and the host decides what Num Lock means.
* **Arrows** use Duke's remapped extended codes rather than `0xE0` pairs, since this
  layer posts single bytes. That is why the arrow and keypad entries differ even though
  a PC keyboard shares their scancodes.

The MantaPad (VID `081f`) is defaulted to SNES mode via `MANTAPAD_DEFAULT_SNES_MODE` in
the board cflags header. In its NES mode only two face buttons are reported, and its
"NES B" *is* physically the SNES X — so X arrives as logical A and A as logical B, and
fire and jump swap over until Y is pressed.

### Saving without a keyboard

Duke's save flow ends in `strget()`, which asks for a save name. That looks like it
needs a keyboard, but `strget()` returns "accept" as soon as it sees Enter **whether or
not anything was typed**, and `KB_Getch()` derives its ASCII from `KB_LastScan` through
`scancodeToASCII[]`. The pad's **A** sends Enter, so it lands in that queue like a real
key.

## Repository layout

```
fruitjam_cflags.h              board config (HW_CONFIG 8), force-included at build
murmulatorm2_cflags.h          board config (HW_CONFIG 13), likewise
adafruitdvisd_cflags.h         board config (HW_CONFIG 2), likewise
olimexpicopc_cflags.h          board config (HW_CONFIG 15), likewise
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
  duke_usb_input.cpp           pad/keyboard -> DOS scancodes
  duke_wiipad.cpp              Wii-extension pads: shared-bus init, hot-plug
  duke_leds.c ws2812.pio       LED heartbeat + NeoPixel VU meter
  duke_dostext.cpp             DOS startup screen
  duke_fatal.c                 fatal errors on that screen, then halt
  duke_fatfs_io.c              FatFs behind POSIX *and* newlib syscalls
3rdparty/pico_shared_drivers   vendored pico_shared (HSTX/I2S/TLV320/PSRAM/SD/
                               wiipad/i2c-recovery/font/pwm_audio)
3rdparty/emu8950               OPL2 emulator
```

## Notes worth knowing before changing things

- **Never put audio code or data behind the QMI bus.** PSRAM at `0x11000000` is the
  *cached* XIP window for CS1 and shares one 8 KB cache with flash code at
  `0x10000000`, so core0 streaming textures evicts core1's audio code. The linker
  script keeps the whole audio path in SRAM.
- **The mix accumulator is `float`, not `double`.** This core has no double-precision
  FPU; audiolib's stock `double` mixer cost eleven soft-float calls per sample and held
  audio to 65 % of the rate the sink consumes, which made music and speech play slow.
- **The stack lives in its own region at the top of SRAM.** It must stay outside both
  `__bss_start__..__bss_end__` (or crt0 zeroes the stack it is running on) and
  `end..__StackLimit` (or the heap grows into it).
- **`ENABLE_PIO_USB` and the header's `HAS_USBPIO` must agree.** They configure
  different halves of one decision — which `hcd_*.c` is linked, and which one
  `tusb_config.h` asks for — and disagreeing versions link cleanly and boot a board
  with no input at all. `duke_usb_input.cpp` `#error`s on the mismatch; keep that check
  if you touch the USB wiring.
- **Core voltage stays at `VREG_VOLTAGE_1_50` for 378 MHz.** 1.60 V bootloops some
  boards.
- **The Wii pad must be brought up before anything touches the codec**, because on the
  Fruit Jam they share SDA/SCL. `duke_wiipad_init()` therefore sits in `main()`, long
  before `DSL_Init()`, and holds the DAC in reset while it clears the bus. Moving it
  later costs all audio whenever a pad is attached at power-on. The corresponding half
  of the fix is in the vendored `tlv320dac3100.c`: a settle gap plus retries on every
  register access, and an `i2c_bus_clear()` before each init attempt.
- **`duke_leds_init()` must stay the last PIO claim.** GPIO 32 needs a PIO whose GPIO
  base can be moved to 16, and that is only possible on a *completely unused* PIO — so
  if it runs before I2S or PIO-USB have claimed theirs, it will take one of their state
  machines instead of pio2. Its call site right after `Startup()` is load-bearing.
- **Video is brought up after PSRAM and before the SD**, deliberately: `SetupPsram()`
  stalls XIP in QMI direct mode, which would starve core1's scanout.
- **`grep` silently skips `src/Engine/engine.c`** (and `mmulti.c`, `awe32.c`,
  `myprint.c`) because they contain a byte that makes grep call them binary. Use
  `grep -a` when searching the engine.
