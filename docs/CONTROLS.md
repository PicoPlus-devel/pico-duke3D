# pico-duke3D controls

Everything reaches the game as **DOS scancodes**. `duke_usb_input.cpp` posts them
to `pico_display_post_key()` → Duke's `keyhandler()`, which updates both
`KB_KeyDown[]` (menus) and `CONTROL_UpdateKeyboardState()` (in-game bindings). So
one table drives menu navigation *and* gameplay, and a gamepad button is
indistinguishable from the key it stands for.

## Gamepad

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
* **`C` does not exist on a SNES-shaped pad**, so crouch is currently
  unreachable on one. Every other action has a button. If you want crouch on a
  SNES pad, the cheapest change is to move it onto `B` (which duplicates START
  anyway) — one line in `padmap[]` in `src/pico/duke_usb_input.cpp`.

### MantaPad (cheap AliExpress SNES pad, VID 081f)

This pad can act as either a NES or a SNES controller, and pico_shared defaults
it to **NES mode** until the player presses Y. NES mode reports only two face
buttons, and because its "NES B" *is* physically the SNES **X**, the mapping
came out shuffled: the pad's X arrived as logical A, and its A arrived as logical
**B → Escape**. That is why pressing what looked like fire opened the menu, and
why pressing Y — or navigating a menu, which makes you press Y — appeared to
"fix" it.

Duke needs all four face buttons regardless, so this port defaults the pad to
SNES mode via `MANTAPAD_DEFAULT_SNES_MODE` in `fruitjam_cflags.h`. The boot log
now reads `defaulting to SNES mode` instead of `Press Y to activate SNES mode`,
and X is fire from the first frame. **No Y press, no level restart.**

## USB keyboard

A keyboard works alongside the pad, and translation is now complete enough to
type: all letters, digits and punctuation, F1–F12, the keypad, Backspace, Tab,
Enter, Escape, Space and the arrows. Shift is tracked as a real scancode, so
`KB_Getch()` picks the shifted ASCII table and capitals work.

This was the reason a save slot could not be named: the arrows, Enter and Escape
were mapped — enough to navigate every menu — but no letter key ever reached the
game, so `strget()` received nothing to insert. The engine side never needed
changing; `KB_Startup()` already fills `scancodeToASCII[]` for every letter and
digit.

Two deliberate gaps:

* **Insert / Home / PageUp / Delete / End / PageDown** are omitted. They are
  extended (`0xE0`-prefixed) on a PC keyboard and Duke reaches them through its
  own `extscanToSC` table; posting a bare code would mean a different key. The
  keypad equivalents do work.
* **Arrows** use Duke's remapped extended codes rather than `0xE0` pairs, since
  this layer posts single bytes. That is why the arrow and keypad entries in the
  table differ even though a PC keyboard shares their scancodes.

`F6` quicksave and `F9` quickload now work from the keyboard.

## Saving and loading — a keyboard is optional

Duke's save flow ends in `strget()`, which asks for a save name. That looks like
it needs a keyboard, but it doesn't: `strget()` returns "accept" as soon as it
sees Enter, **whether or not anything was typed**, and `KB_Getch()` derives its
ASCII from `KB_LastScan` through `scancodeToASCII[]`. The pad's **A** button
sends Enter, so it lands in that queue like a real key.

So, with the pad alone:

1. **START** or **B** — open the menu
2. navigate to *Save Game* with the D-pad, **A** to select
3. pick a slot, **A** to select
4. **A** again to accept the (empty) name → the game is written to
   `/roms/duke3d/game0.sav`

Loading is the same via *Load Game*. The only thing you give up is a *named*
save — the slot shows blank. With a keyboard attached you can type a name at
step 4 instead, then press Enter.

`F6` quicksave and `F9` quickload are handled by the engine and skip the name
prompt once you have made one ordinary save. They are reachable from a keyboard
but **not from the pad** — a SNES pad has no spare button, so it would need a
chord. Say the word if you want one (e.g. SELECT + L / SELECT + R).

## Where this lives

| What | Where |
|---|---|
| pad + keyboard → scancode tables | `src/pico/duke_usb_input.cpp` |
| scancode → Duke (`keyhandler`) | `src/pico/pico_display.c`, `src/Game/keyboard.c` |
| scancode values | `src/Game/keyboard.h` |
| pad decoding, per-device quirks | `3rdparty/pico_shared_drivers/usb_hid/hid_app.cpp` |
| MantaPad SNES default | `fruitjam_cflags.h` |
