# pico-duke3D

A port of Duke Nukem 3D to three small RP2350 hobby boards. Output is HDMI, input is
a USB gamepad or keyboard, and the game data comes off an SD card.

The game data stays on the card, so switching between the shareware episode and the
full Atomic Edition means replacing one file.

## What works

- Video over HDMI
- Sound effects and Adlib / Sound Blaster music
- USB gamepads and keyboards, plus NES, SNES and Wii Classic controllers on the boards
  that have those connectors
- Savegames, settings and screenshots on the SD card
- Errors are reported on the screen, so no serial cable or computer is needed to find
  out what went wrong
- Runs standalone, or from the
  [pico-bootLoader](https://github.com/fhoedemakers/pico-bootLoader) menu

## What you need

- One of the three [boards](#which-board) below
- A micro SD card, formatted FAT or exFAT
- `DUKE3D.GRP` — the game's data file. The shareware version works, as does the one
  from the registered or Atomic Edition.
- An HDMI screen, and a gamepad or a keyboard. Either input is sufficient on its own:
  the game can be played, saved and loaded with a gamepad alone.

## Which board

All three are tested and working on real hardware.

| | Fruit Jam | Murmulator M2 | Adafruit DVI + SD |
|---|---|---|---|
| Video | HDMI 640×480 | same | same |
| Sound | headphones **or** HDMI — switches when you plug in | headphones and HDMI at once | headphones (optional module) and HDMI at once |
| USB pad / keyboard | plug straight in | needs an OTG adapter | needs an OTG adapter |
| NES/SNES controller ports | — | two | two |
| Wii Classic pad | yes | yes | — |
| Lights | blinking LED, plus five LEDs used as a sound-level meter | blinking LED | blinking LED |
| Also needed | — | — | the module must be a **Pimoroni Pico Plus 2** |
| Folder on the SD card | `/emu/8/` | `/emu/13/` | `/emu/2/` |

The Adafruit DVI + SD entry is a self-built option: a Pico-shaped module on a
breadboard or a small circuit board, with an
[Adafruit DVI breakout](https://www.adafruit.com/product/4984) for video and a
[MicroSD breakout](https://www.adafruit.com/product/254) for the card. The module must
be a Pimoroni Pico Plus 2 and not a plain Pico 2, because the game needs the extra
memory that module carries and will not start without it.

## Installing

1. Download the file for your board from the
   [releases page](https://github.com/fhoedemakers/pico-duke3D/releases).
2. Hold the **BOOT** button on the board while connecting it to a computer. It appears
   as a USB drive.
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

A gamepad and a keyboard work at the same time, and every controller connected drives
the same player.

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

**The NES/SNES controller sockets** on the Murmulator M2 and Adafruit DVI + SD. There
are two, and a pad in either behaves the same as a USB one, so the table above applies
unchanged.

A real NES controller works too: the port detects that it is a NES pad rather than a SNES
one, so its two buttons land where any other pad's A and B do — A fires and confirms, B
jumps and goes back. Having no shoulder buttons, it opens the menu with **SELECT+START**
instead of L+R. Crouch and strafe are the two things it cannot reach.

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
* **`` ` ``**, **`U`** and **`T`** / **`W`** / **`K`** are for the console, mouse aiming
  and multiplayer chat, none of which exist on this port.

Left and right Ctrl, Shift and Alt are equivalent.

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

## Technical documentation

Design, build instructions, the differences between the boards at pin level, and notes
for anyone changing the code: [TECHNICAL.md](TECHNICAL.md).

Duke Nukem 3D is © 3D Realms. No game data is included with this port.
