# pico-duke3D

A port of Duke Nukem 3D to four small RP2350 hobby boards. Output is HDMI, input is
a USB gamepad, keyboard or mouse, and the game data comes off an SD card.

The game data stays on the card, so switching between the shareware episode and the
full Atomic Edition means replacing one file.

## What works

- Video over HDMI
- Sound effects and Adlib / Sound Blaster music
- USB gamepads, keyboards and mice, plus NES, SNES and Wii Classic controllers on the
  boards that have those connectors, and a SNES Mouse on the Adafruit DVI + SD board
- Savegames, settings and screenshots on the SD card
- Errors are reported on the screen, so no serial cable or computer is needed to find
  out what went wrong
- Runs standalone, or from the
  [pico-bootLoader](https://github.com/fhoedemakers/pico-bootLoader) menu

## What you need

- One of the four [boards](#which-board) below
- A micro SD card, formatted FAT or exFAT
- `DUKE3D.GRP` — the game's data file. The shareware version works, as does the one
  from the registered or Atomic Edition.
- An HDMI screen, and a gamepad or a keyboard. Either input is sufficient on its own:
  the game can be played, saved and loaded with a gamepad alone. A mouse is optional,
  for turning and aiming.

## Which board

All four are tested and working on real hardware.

| | Fruit Jam | Murmulator M2 | Adafruit DVI + SD | Olimex RP2040-PICO-PC |
|---|---|---|---|---|
| Video | HDMI 640×480 | same | same | same |
| Sound | headphones **or** HDMI — switches when you plug in | headphones and HDMI at once | headphones (optional module) and HDMI at once | audio jack and HDMI at once |
| USB pad / keyboard / mouse | plug straight in | needs an OTG adapter | needs an OTG adapter | plug straight into the USB-A socket |
| NES/SNES controller ports | — | two | two, also for a SNES Mouse | one, on the UEXT connector |
| Wii Classic pad | yes | yes | — | — |
| Lights | blinking LED, plus five LEDs used as a sound-level meter | blinking LED | blinking LED | blinking LED |
| Also needed | — | — | the module must be a **Pimoroni Pico Plus 2** | a Pico 2 with a **PSRAM chip fitted on GPIO 8** |
| Folder on the SD card, **when using [pico-bootLoader](https://github.com/fhoedemakers/pico-bootLoader)** | `/emu/8/` | `/emu/13/` | `/emu/2/` | `/emu/15/` |

The Adafruit DVI + SD entry is a self-built option: a Pico-shaped module on a
breadboard or a small circuit board, with an
[Adafruit DVI breakout](https://www.adafruit.com/product/4984) for video and a
[MicroSD breakout](https://www.adafruit.com/product/254) for the card. The module must
be a Pimoroni Pico Plus 2 and not a plain Pico 2, because the game needs the extra
memory that module carries and will not start without it. That circuit board also
exists as a finished design — see [PicoNES PCB](#picones-pcb) below.

The Olimex RP2040-PICO-PC is used here with a Raspberry Pi Pico 2 in place of the
original Pico. A stock Pico 2 has no PSRAM, which the game needs, so a PSRAM chip must
be fitted to it (chip select on GPIO 8); without it the game reports this on screen and
stops. The board's PS/2 keyboard port is not used.

## PicoNES PCB

The parts above can be soldered onto a PCB instead of wired on a breadboard, which turns them into a finished console with two controller ports and a case. The design is the **PicoNES**, by [John Edgar Park](https://twitter.com/johnedgarpark). It comes in two versions, which differ only in the controller ports: **v2.6** with NES controller ports and **v2.8** with SNES controller ports. Nothing about the game changes: this is the *Adafruit DVI + SD* column of the table above, wired neatly, so it runs the same firmware and reads the same SD card.

| Design | Picture | Controller ports | Designed by |
| ------ | ------- | ---------------- | ----------- |
| PicoNES v2.6 | <img src="https://raw.githubusercontent.com/PicoPlus-devel/pico_shared/main/PCB/images/picones_v2.6_assembled.jpg" width="240" alt="PicoNES v2.6 with a Pimoroni Pico Plus 2"><br><sub>See note</sub> | 2 × NES | [John Edgar Park](https://twitter.com/johnedgarpark) |
| PicoNES v2.8 | <img src="https://raw.githubusercontent.com/PicoPlus-devel/pico_shared/main/PCB/images/picones_v2.8s_assembled.jpg" width="240" alt="PicoNES v2.8 with SNES controller ports"><br><sub>See note</sub> | 2 × SNES | [John Edgar Park](https://twitter.com/johnedgarpark) |

> [!NOTE]
> The PicoNES v2.6 and v2.8 are shown with a Pimoroni Pico Plus 2. The game needs the PSRAM of the Pimoroni Pico Plus 2, so no other board can be fitted.

Parts lists, ordering instructions, assembly notes, the pin assignment and the 3D-printed case are described in the [PCB README](https://github.com/PicoPlus-devel/pico_shared/blob/main/PCB/README.md). The Gerber files are attached to every [release](https://github.com/PicoPlus-devel/pico-duke3D/releases) of this project and also live in the [PCB folder of the pico_shared repository](https://github.com/PicoPlus-devel/pico_shared/tree/main/PCB). The other designs in the PCB README, the PicoNES Mini and PicoNES Micro, are built around Waveshare boards without PSRAM and cannot run the game.

> [!NOTE]
> Sellers on AliExpress have copied this design and sell pre-populated boards. For questions about those, contact the seller.

### Only one build of it runs Duke

The PCB takes any Pico-shaped board, and two ways of mounting one. For Duke there is only one combination that works:

**Design v2.6 or v2.8, a Pimoroni Pico Plus 2, and male headers plugged into the through-holes.**

> [!IMPORTANT]
> A Pico 2 or Pico 2 W will not run the game. Duke needs the memory that only the Pico Plus 2 carries; on anything else it stops at startup and says so on the screen, as in [When something goes wrong](#when-something-goes-wrong). The Raspberry Pi Pico, Pico 2 and Pico 2 W in the parts list of the PCB README therefore do not apply here.

The Pico Plus 2 cannot lie flat against the PCB — the SP/CE connector on its back is in the way — so it needs the through-holes of v2.6 or v2.8 and a set of male headers. Optional female socket headers, soldered into the PCB, let the board be plugged in and swapped without desoldering. The Pico Plus 2 has a **USB-C** connector, so the OTG Y-cable and the power supply must be USB-C as well, while the PCB README, which also covers the Pico 2, lists micro USB.

### Controllers

- **v2.8:** original SNES controllers plug in directly.
- **v2.6:** NES controllers plug in directly. A SNES controller needs a [SNES-to-NES adapter cable you make yourself](https://github.com/PicoPlus-devel/pico-snesPlus/blob/main/snestonescontroller.md), one per port. Ready-made cables exist but are hard to find, and some do not work as expected.

A SNES Mouse (the one sold with Mario Paint) works in either port as well; see
[Mouse](#mouse).

**A NES controller plays the game, a SNES controller plays all of it.** The ports read a NES pad as a NES pad, so A fires and B jumps, and the menu opens with SELECT+START — but with only two face buttons and no shoulder buttons, crouch and strafe stay out of reach. The ports speak the SNES protocol as well, so a SNES controller gives you the full scheme described in [Which controllers work](#which-controllers-work).

Two controllers give a two-player setup; a USB pad for player 1 and a controller in either port for player 2 works just as well.

| NES controller ports (v2.6) | SNES controller ports (v2.8) |
| --------------------------- | ---------------------------- |
| <img src="https://raw.githubusercontent.com/PicoPlus-devel/pico_shared/main/PCB/images/picones_v2.6_assembled.jpg" width="400" alt="PicoNES v2.6 with NES controller ports"> | <img src="https://raw.githubusercontent.com/PicoPlus-devel/pico_shared/main/PCB/images/picones_v2.8s_assembled.jpg" width="400" alt="PicoNES v2.8 with SNES controller ports"> |

### What to flash

`duke3d_game_adafruitdvisd.uf2` from the [releases page](https://github.com/PicoPlus-devel/pico-duke3D/releases), then `DUKE3D.GRP` onto the SD card in `roms/duke3d` — the same steps as [Installing](#installing).

To pick Duke from the [pico-bootLoader](https://github.com/PicoPlus-devel/pico-bootLoader) menu instead, flash that project's `AdafruitDVISD` loader binary and put its build of Duke in `/emu/2/` on the card.

### What you get on this board

| | |
|---|---|
| Two controller ports | NES on v2.6, SNES on v2.8. Both behave exactly as [Which controllers work](#which-controllers-work) describes — including the [first-press quirk of some SNES pads](#one-quirk-with-some-snes-pads-in-these-sockets). A SNES Mouse works in either port on v2.8 |
| USB pad, keyboard or mouse | Through the OTG Y-cable |
| Sound | Over HDMI. The optional audio module mentioned in [Which board](#which-board) is a breadboard extra; this PCB has no place for it |
| Lights | The onboard LED blinks while frames are being drawn. No sound-level meter — that needs the Fruit Jam's LEDs |
| Wii Classic pads | Not on this board; there is no Wii connector in this design |

### 3D printed case

Gavin Knight ([DynaMight1124](https://github.com/DynaMight1124)) designed an NES-like case for the v2.6 PCB: [thingiverse.com/thing:6689537](https://www.thingiverse.com/thing:6689537). It does not fit the v2.8 PCB. Two top covers are available, one with a button that reaches the BOOT button so firmware can be updated without opening the case, and one without. The button is worth having, since flashing means holding BOOT while plugging the board in. All parts are shown in the [PCB README](https://github.com/PicoPlus-devel/pico_shared/blob/main/PCB/README.md#3d-printed-case).

> [!IMPORTANT]
> Download the **latest** top cover. Headers are required for this build, and they raise the module; only the newest cover leaves room for the USB cable, as the older ones assume a board soldered flat onto the PCB.

| Top cover | Base |
| --------- | ---- |
| <img src="https://github.com/user-attachments/assets/c6205db3-580e-41e9-83e4-66c9534c6519" width="380" alt="Top cover"> | <img src="https://github.com/user-attachments/assets/256bbd1b-b6db-485d-a59c-fd22fd017887" width="380" alt="Base with the PCB fitted"> |

## Installing

1. Download the file for your board from the
   [releases page](https://github.com/fhoedemakers/pico-duke3D/releases).
2. Hold the **BOOT** button on the board while connecting it to a computer. It appears
   as a USB drive. (Once the game is on there, **OPTIONS → [BOOTSEL MODE](#bootsel-mode)**
   brings that drive back without unplugging anything.)
3. Copy the downloaded file onto that drive. The board restarts into the game.
4. Copy `DUKE3D.GRP` onto the SD card in a folder named `roms/duke3d`, and insert the
   card.

Then connect the HDMI cable and a controller.

Savegames, settings and screenshots are written to the card as you play:

```
roms/duke3d/DUKE3D.GRP      the game data
roms/duke3d/game0.sav       savegames
roms/duke3d/duke3d.cfg      settings and key bindings
screenshots/duke0000.png    F12 captures
```

If you use [pico-bootLoader](https://github.com/fhoedemakers/pico-bootLoader) to
select between several games, use its build of Duke instead: download it from that
project and place it in the folder for your board given in the table above. Quitting
Duke, or pressing reset, returns to the menu.

To build from source, see [TECHNICAL.md](TECHNICAL.md).

## Controls

A gamepad, a keyboard and a mouse work at the same time, and every controller connected
drives the same player.

In menus, **A confirms and B goes back on every controller**. START also goes back.

### Gamepad

There is one layout, and it follows Retro-Go's
[duke3d-go](https://github.com/DynaMight1124/retro-go/blob/megapack/duke3d-go/CONTROLS.md):
fire on **A**, use on **START**.

| Button | In game | In menus |
|---|---|---|
| D-pad / left stick | move and turn | navigate |
| **A** | **fire** | **select / confirm** |
| **B** | jump | back |
| **X** | crouch | — |
| **Y** | jetpack | — |
| **L** / **R** | strafe left / right | — |
| **SELECT** | next weapon | — |
| **START** | open / use / flip a switch | back |
| **L+R** | **open the menu** | — |
| **SELECT+START** | open the menu, for a pad with no shoulder buttons | — |

L and R together open the menu. They are strafe left and strafe right, so pressing both
cancels out and the player does not move. On a pad with no shoulder buttons,
SELECT+START does the same, at the cost of one weapon change on the way in.

There is no Run button, since L and R are strafe, so the player always runs. Caps Lock
turns that off for the rest of the session.

#### Shift mode

Holding START for half a second turns the D-pad into the inventory.

| Button | While START is held |
|---|---|
| **D-pad ↑** | use the selected item — medkit, steroids… |
| **D-pad ↓** | jetpack |
| **D-pad ←→** | previous / next item |
| **B** | look down |
| **X** | look up |

**A**, **Y**, **L** and **R** keep firing, jetpacking and strafing, so only the six
buttons above change. **SELECT** is the exception: START is held already, so pressing
SELECT opens the menu, which is the way in on a pad with no shoulder buttons.

Three things to note:

* Nothing happens during that half second, so movement continues while START is held.
  Once the inventory takes over, the D-pad stops moving the player, and a direction
  already held is ignored until released — otherwise moving forward would immediately
  use an inventory item.
* Use happens on release of START rather than on press, because until the half second
  is up the game cannot tell which was meant. A short tap still works; it takes effect
  a fraction later.
* Releasing START after the inventory has appeared does not use anything.

A button already held when the menu opens or closes is ignored until you let go of it —
otherwise opening the menu while firing would immediately select the highlighted item.

#### Which controllers work

**USB pads**, up to two, on the boards with a USB socket. The D-pad and the left
thumbstick both steer. Connect everything before switching on; USB controllers are not
detected afterwards.

The inexpensive SNES pads sold in a NES-shaped shell (sometimes as "MantaPad") also
work, with all four face buttons available from the first frame.

**The NES/SNES controller sockets** on the Murmulator M2 and Adafruit DVI + SD, and the
UEXT connector of the Olimex RP2040-PICO-PC. The first two boards have two sockets, the
Olimex board one, and a pad in any of them behaves the same as a USB one, so the table
above applies unchanged.

A real NES controller works too, original or aftermarket: the port works out that it is a
NES pad rather than a SNES one, so its two buttons land where any other pad's A and B do
— A fires and confirms, B jumps and goes back. Having no shoulder buttons, it opens the
menu with **SELECT+START** instead of L+R. Crouch and strafe are the two things it cannot
reach.

##### One quirk with some SNES pads in these sockets

**An original Nintendo SNES controller is recognised as soon as it is plugged in.** Some
aftermarket SNES pads are not, and their first press after switching on may be read as a
NES pad's. If that first press is **B** it fires instead of jumping, and if it is **Y**
it jumps instead of using the jetpack. **Pressing A, X, L or R once settles it**, and
every button behaves normally from then until the board is switched off. Pads in the two
sockets are handled separately, so settling one does not settle the other. Nothing needs
configuring, and there is no setting to get wrong.

This is the price of supporting both pads in one socket with no switch to throw. The two
share a connector and a protocol. An original NES pad announces itself by pulling four
wires low at the end of each read, and an original SNES pad by holding the line low after
its last button; aftermarket pads often do neither. An aftermarket NES pad and an
untouched aftermarket SNES pad are then identical on the wire, and the port cannot know
which it is holding until a button arrives that only one of them has: **A, X, L and R do
not exist on a NES pad**, so the first press of any of them is proof. Until that proof
arrives the port assumes a NES pad, because guessing wrong that way costs a SNES player
one press, while guessing the other way leaves a NES player with no fire button at all.

**Wii Classic pads** on the Fruit Jam and Murmulator M2: a NES Classic Mini, SNES
Classic Mini or Wii Classic Controller (Pro), on an adapter such as the
[Adafruit Wii Nunchuck Adapter](https://www.adafruit.com/product/4836). Unlike USB,
these can be connected at any time, including during play; the pad is detected within a
second, after a brief pause.

* A Wii Classic Pro or SNES Classic Mini behaves the same as a USB SNES pad, so the
  default table applies unchanged.
* A NES Classic Mini has only two face buttons, which are fire and jump. Everything else
  works — SELECT+START opens the menu, and a held START gives the inventory — but crouch
  and strafe have nowhere to go.

If there is no sound on the Fruit Jam with a Wii pad attached, see
[When something goes wrong](#when-something-goes-wrong).

### Keyboard

These are Duke's own defaults, and you can **change any of them** in
*Options → Setup Keyboard*. Where the table says `Kpad`, that means the number pad on
the right of the keyboard — it works whether or not Num Lock is on.

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
| **F12** | [screenshot](#screenshots) |

A few keys do nothing here:

* **Insert, Home, PageUp, Delete, End, PageDown**, PrintScreen and the Windows keys.
  Duke's default bindings put look and aim on the first six, which is why the tables
  above use the keypad for those instead.
* **`` ` ``** and **`T`** / **`W`** / **`K`** are for the console and multiplayer chat,
  neither of which exists on this port.

Left and right Ctrl, Shift and Alt are equivalent.

### Mouse

A USB mouse works on every board, next to the keyboard or a gamepad. Connect it before
switching on, as with USB pads. On the Adafruit DVI + SD board an original **SNES Mouse**
(the one sold with Mario Paint) also works, in either controller port, and can be plugged
in at any time. With both connected, either one steers.

| Mouse | Does |
|---|---|
| Move left / right | Turn |
| Move forward / back | Look down / up (mouse aiming) |
| Left button | Fire |
| Right button | Open / use / flip a switch |
| Middle button | Jump |
| Wheel up / down | Previous / next weapon |

A SNES Mouse has the left and right buttons only.

The mouse turns and aims but does not walk: walking stays on the keyboard or a gamepad,
both of which work alongside it. To walk with the mouse alone, bind a button to
*Move_Forward* in *Options → Setup Mouse → Button Setup*.

In menus, moving the mouse forward and back or turning the wheel moves the cursor, the
left button confirms and the right button goes back.

All of this can be changed in *Options → Setup Mouse*: the speed of each direction, which
button does what (including the two side buttons some mice have), and mouse aiming.

**Mouse aiming is on from the start.** **`U`** turns it off and on again, as does
*Mouse Aiming* in the same menu. With it off, moving the mouse forward and back does
nothing. With it on, the view no longer tilts by itself on slopes, also when playing with
a gamepad. Pushing the mouse forward looks down, as in the original game; *Mouse Aiming
Flip* in the same menu reverses that.

## Saving and loading

A keyboard is not required. With a gamepad alone:

1. **L+R** to open the menu (SELECT+START in the NES layout, or on a pad with no
   shoulder buttons)
2. D-pad to *Save Game*, then **A**
3. select a slot, then **A**
4. **A** again to accept a blank name; the save is written to the SD card

Loading works the same way through *Load Game*. The only limitation is that the save
cannot be named, so the slot appears blank. With a keyboard, a name can be typed at
step 4.

**F6** and **F9** are quicksave and quickload, available once one ordinary save has been
made. They are keyboard-only, as every gamepad button is already assigned.

## Screenshots

**F12** writes the current frame to the SD card as `/screenshots/duke0000.png`,
`duke0001.png` and so on. The folder is created on first use, at the root of the card.
Numbering continues from the lowest free slot, so it carries on after a reboot.
*SCREEN SAVED* appears on screen, or *CAN'T WRITE FILE!* if the card is full or
write-protected. It also works on the intro logo and the end-of-level bonus screen.

The files are PNGs at the game's own 320×200 resolution and palette, saved without
conversion, so they include the status bar and any screen tint such as the red flash
when hit or the blue underwater. Expect 10–25 KB each.

Two things to note. The game pauses briefly while the file is written, though the music
continues. And the image will look slightly tall in a viewer, because the pixels are not
square: a viewer that takes the 320×200 size literally will stretch the 4:3 picture seen
on the screen.

## Bootsel mode

**OPTIONS → BOOTSEL MODE** restarts the board as the USB drive you flash firmware onto,
without holding **BOOT** while replugging it. Handy once the board is in a case, where
that button can be hard to reach, or awkward to hold while plugging in a cable.

It takes effect immediately — there is no *are you sure?*. That is why it sits at the
bottom of the list, furthest from where the cursor starts, with a warning line under it.
It is **not** *Quit Game*: quitting restarts Duke, or returns to the pico-bootLoader menu
if you use that. This one goes to the firmware drive, and only a computer gets you out of
it again.

Settings are written to the SD card on the way out, so anything you changed in the menu
first is kept. A game in progress is not saved — save it before using this.

To leave, copy a `.uf2` onto the drive, or just unplug and power the board back on.

## When something goes wrong

Errors are reported on the screen, so no serial cable is needed. A text console is shown
from the moment the board is switched on, and anything fatal is printed on it:

```
              FATAL ERROR

 *** FATAL ERROR ***
 No SD card (FatFs error 3).
 Insert a FAT/exFAT-formatted card with
 DUKE3D.GRP in /roms/duke3d.

 Press RESET (returns to the boot menu).
```

The board then halts rather than restarting, so the message stays readable. What the
messages mean:

| Message | Cause |
|---|---|
| No SD card | Card not inserted, or not formatted FAT or exFAT |
| `/roms/duke3d` or `DUKE3D.GRP` missing | Wrong folder name, or the file is not on the card |
| A message about memory | Wrong module — see [Which board](#which-board) |
| Corrupt or unrecognised game data | `DUKE3D.GRP` is damaged or incomplete; copy it again |

Failures during a level are reported the same way, with the last few lines of log still
on screen.

Two things to check on the board itself:

* The onboard LED blinks roughly once a second while the game is drawing frames. If it
  stops while a picture is still on screen, the game has stopped; if it never lights at
  all, it never started.
* If there is no sound on the Fruit Jam with a Wii pad attached, the cause is that the
  Wii socket and the sound chip share a connection, and some pads hold it low until they
  are initialised. This is handled automatically, but if there is still no sound, unplug
  the pad and reset to confirm.
* If an aftermarket SNES pad in a **NES/SNES socket** fires when you meant to jump, and
  only on the first press after switching on, that is expected — press A once and it
  corrects itself for the rest of the session. See
  [One quirk with some SNES pads](#one-quirk-with-some-snes-pads-in-these-sockets).

## Technical documentation

Design, build instructions, the differences between the boards at pin level, and notes
for anyone changing the code: [TECHNICAL.md](TECHNICAL.md).

## Credits

- Ported by Frank Hoedemakers, with most of the code written by
  [Claude Code](https://claude.com/claude-code)
- Testing by Gavin Knight ([DynaMight1124](https://github.com/DynaMight1124))
- Built on [Chocolate Duke3D](https://github.com/fabiensanglard/chocolate_duke3D) and
  Ken Silverman's BUILD engine

Duke Nukem 3D is © 3D Realms. No game data is included with this port.
