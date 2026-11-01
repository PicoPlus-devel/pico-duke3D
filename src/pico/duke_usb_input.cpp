//
//  duke_usb_input.cpp — USB gamepad, keyboard and mouse input for the Duke3D port.
//
//  Bridges the pico_shared USB HID host driver (hid_app.cpp: generic HID pads,
//  DualShock, XInput/Xbox via tusb_xinput, plus boot keyboards) to Duke's
//  keyboard layer. Everything is delivered as DOS scancodes through
//  pico_display_post_key() -> keyhandler(), which updates both KB_KeyDown[]
//  (menus) and CONTROL_UpdateKeyboardState (in-game bindings) — so one mapping
//  drives menu navigation AND gameplay.
//
//  ONE layout, Retro-Go's, for every pad. It produces an *effective set* of
//  scancodes which is diffed once per poll, so a scancode reached from two buttons
//  can never be released while something still asks for it. There is no button
//  left for Run (L and R are strafe), so it leans on Duke's own AutoRun, which
//  Game/config.c forces on at startup.
//
//  There used to be a second, four-button "NES" layout behind a PAD LAYOUT option,
//  because a real NES pad in a DE-9 port was translated as if its two buttons were
//  a SNES pad's B and Y and so had no fire button here. padIsNes()/nesToButtons()
//  now work out the pad shape and put them on io A and io B, which removed the
//  reason for the option -- and with it a mode where SELECT stopped cycling
//  weapons and which persisted silently in duke3d.cfg.
//
//  Retro-Go's duke3d-go arrangement (see its CONTROLS.md). Their
//  handhelds have ten buttons and a SNES pad has eight: OPTION only duplicated
//  crouch so it is dropped, and MENU becomes a chord. hid_app folds the D-pad hat
//  and the left stick into the direction bits, so either works. Full table and the
//  save/load procedure: see README.md.
//    D-pad/stick  -> arrow keys        (menu nav + move/turn)
//    A            -> LeftControl       (fire)
//    B            -> A key             (jump)
//    X            -> Z key             (crouch)
//    Y            -> J key             (jetpack)
//    L / R        -> , and .           (strafe left / right)
//    SELECT       -> ' (next weapon)
//    START        -> Space             (open/use -- on release, see below)
//    L+R          -> Escape            (open the menu)
//    SELECT+START -> Escape            (same, for a pad with no shoulders)
//    C            -> Z key             (crouch again, as Retro-Go's OPTION did;
//                                       only Genesis pads report a C at all)
//  L+R is the chord to reach for: player.c gives Strafe_Left svel += keymove and
//  Strafe_Right svel += -keymove, so held together they cancel exactly and
//  forming the chord has no visible effect. SELECT+START is accepted as well,
//  because a plain NES pad in a DE-9 port reports no L/R at all and would
//  otherwise be locked out of the menu -- it costs one visible weapon switch on
//  the way in, which is why it is not the primary chord.
//
//  Inside a menu (menuKeys):
//    D-pad          -> arrow keys      (navigate)
//    A              -> Enter           (confirm; also accepts a save name)
//    B  or  START   -> Escape          (back / close)
//
//  SHIFT layer, always on -- it is part of Retro-Go's arrangement,
//  not an option. Duke wants more actions than a pad has buttons, so holding START
//  for 500 ms turns the D-pad into the inventory:
//    D-pad up     -> Enter             (use inventory item)
//    D-pad down   -> J key             (jetpack)
//    D-pad l/r    -> [ and ]           (previous / next item)
//    B            -> PgDn              (look down)
//    X            -> PgUp              (look up)
//  A, Y, L and R keep firing, jetpacking and strafing, so only the six above
//  change. SELECT is the exception: START is held by definition here, so SELECT
//  completes the SELECT+START menu chord instead of cycling weapons -- which is
//  how a pad with no shoulder buttons reaches the menu from inside the layer.
//  Nothing happens during the 500 ms itself -- Retro-Go calls that the
//  transparent hold -- so you keep running while the timer expires. Once the
//  layer engages the D-pad no longer walks, and a direction held across that
//  moment stays ignored until released, or walking forward would instantly use
//  an inventory item.
//
//  Two things about that mechanism, in the order they will bite:
//   1. START can no longer act on press, since we have to see whether the hold
//      reaches 500 ms. So Use fires on *release*, its press length is then
//      arbitrary, and Duke samples KB_KeyDown[] once per TICSPERFRAME (~38 ms at
//      worst) -- a quick tap would be missed entirely. Hence the 100 ms pulse.
//      It cannot double-fire: Open is bit 29 and sector.c edge-triggers that
//      whole bit group through p->interface_toggle_flag, so a held Open acts
//      exactly once. The same is true of Jetpack, Inventory, Inventory_Left/
//      Right, the weapon nibble and Escape, which is why none of the hotkeys
//      above need repeat suppression of their own. Look_Up/Look_Down are
//      deliberately *outside* that mask, i.e. continuous.
//   2. probe() reads PgUp/PgDn as menu up/down AND takes Space as confirm, so
//      neither the look keys nor a draining Use pulse may ever reach a menu. The
//      hold.reset() on duke_menu_is_active() is not optional: drop it and the
//      pad scrolls and confirms menus by itself.
//
//  MantaPad note: that pad boots in NES mode, where only two face buttons are
//  reported and its "NES B" is physically the SNES X -- so X arrives as logical A
//  and A arrives as logical B, i.e. fire and jump swap over and the pad looked
//  broken until Y was pressed. Defaulted to SNES mode via
//  MANTAPAD_DEFAULT_SNES_MODE, so all four face buttons report from the first
//  frame despite the NES shell.
//
//  Init/poll pattern and the PIO-USB configuration mirror fruitjam-doom's
//  d_main.c / i_usbhid.cpp (proven on this exact board).
//
//  Three things vary by board, all keyed off the force-included cflags header:
//    HAS_USBPIO   present -> Pico-PIO-USB host on PIN_USB_HOST_DP/DM;
//                 absent  -> the RP2350's native USB controller (Murmulator M2,
//                 where a pad plugs in through an OTG adapter).
//    NES_PIN_CLK  != -1 -> legacy NES/SNES ports polled over PIO, folded
//                 into the same scancode stream as the USB pads.
//    WII_PIN_SDA  != -1 -> a Wii extension pad (NES/SNES Classic Mini, Wii
//                 Classic Controller (Pro)) polled over I2C, folded in the same
//                 way, on their namesakes; see wiiToButtons().
//
//  The mouse is the one input that is NOT a scancode. Duke reads it itself, once
//  per CONTROL_GetInput(), through BUILD's getmousevalues() -> readmousexy() and
//  readmousebstatus(), both at the bottom of this file. That one path already
//  does turning, mouse aiming, the bindings in Options -> Setup Mouse and mouse
//  navigation in the menus, so all this file has to do is hand over the motion
//  and the buttons. A USB mouse and a SNES Mouse in a controller port (boards with
//  NESPAD_SNES_MOUSE) are merged into one, so Duke cannot tell them apart.
//
#include <stdio.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "tusb.h"
#ifdef HAS_USBPIO
#include "pio_usb_configuration.h"
#include "duke_usb_frame.h"
#endif
#include "gamepad.h"

// The CMake USB transport choice (ENABLE_PIO_USB) and the board header's
// HAS_USBPIO have to agree: they configure different halves of the same
// decision (which hcd_*.c is linked vs. which one tusb_config.h asks for), and
// disagreeing versions link cleanly and boot a board with no input at all.
#if defined(DUKE_PIO_USB)
#if DUKE_PIO_USB && !defined(HAS_USBPIO)
#error "ENABLE_PIO_USB=1 but the board header does not define HAS_USBPIO — pass -DENABLE_PIO_USB=0."
#elif !DUKE_PIO_USB && defined(HAS_USBPIO)
#error "ENABLE_PIO_USB=0 but the board header defines HAS_USBPIO — drop -DENABLE_PIO_USB=0."
#endif
#endif

// Legacy NES/SNES pads on PIO (pico_shared nespad, vendored under
// 3rdparty/pico_shared_drivers/nespad; linked in by -DDUKE_NESPAD=ON).
#if DUKE_NESPAD && defined(NES_PIN_CLK) && NES_PIN_CLK != -1
#define DUKE_HAS_NESPAD 1
#include "nespad.h"
#include "hardware/clocks.h"
#else
#define DUKE_HAS_NESPAD 0
#endif
#if DUKE_HAS_NESPAD && defined(NES_PIN_CLK_1) && NES_PIN_CLK_1 != -1
#define DUKE_HAS_NESPAD_1 1
#else
#define DUKE_HAS_NESPAD_1 0
#endif
// SNES Mouse in either port: opted into by the board header, which nespad.h
// (default 0) and nespad.cpp both see through the force-include.
#if DUKE_HAS_NESPAD && NESPAD_SNES_MOUSE
#define DUKE_HAS_SNES_MOUSE 1
#else
#define DUKE_HAS_SNES_MOUSE 0
#endif

// Wii-extension pads on I2C (NES Classic Mini, SNES Classic Mini, Wii Classic
// Controller (Pro)), through the duke_wiipad.cpp glue. Same -1-disables
// convention on the pin macros, and folded into the same button word below.
#if defined(WII_PIN_SDA) && WII_PIN_SDA >= 0 && \
    defined(WII_PIN_SCL) && WII_PIN_SCL >= 0
#define DUKE_HAS_WIIPAD 1
#include "duke_wiipad.h"
#else
#define DUKE_HAS_WIIPAD 0
#endif

extern "C" {
void pico_display_post_key(uint8_t rawcode);   // pico_display.c -> keyhandler()
void duke_usb_init(void);
void duke_usb_poll(void);

// Engine/display.h, called by BUILD's getmousevalues() (Engine/engine.c).
void readmousexy(short *x, short *y);
void readmousebstatus(short *bstatus);

// Game/menues.c. Needed because one button has to mean fire in game and confirm
// in a menu.
int duke_menu_is_active(void);
}

// DOS scancodes (values from Game/src/keyboard.h; arrows are Duke's remapped
// extended codes, all < 0x80 so they can be posted directly).
namespace sc
{
    constexpr uint8_t Escape = 0x01;
    constexpr uint8_t Return = 0x1c;
    constexpr uint8_t LCtrl  = 0x1d;
    constexpr uint8_t KeyA   = 0x1e;
    constexpr uint8_t LShift = 0x2a;
    constexpr uint8_t KeyZ   = 0x2c;
    constexpr uint8_t LAlt   = 0x38;
    constexpr uint8_t Space  = 0x39;
    constexpr uint8_t Quote  = 0x28;   // Next_Weapon
    constexpr uint8_t Comma  = 0x33;   // Strafe_Left
    constexpr uint8_t Period = 0x34;   // Strafe_Right
    constexpr uint8_t KeyJ   = 0x24;   // Jetpack
    constexpr uint8_t LBrack = 0x1a;   // Inventory_Left
    constexpr uint8_t RBrack = 0x1b;   // Inventory_Right
    constexpr uint8_t Up     = 0x5a;
    constexpr uint8_t Down   = 0x6a;
    constexpr uint8_t Left   = 0x6b;
    constexpr uint8_t Right  = 0x6c;
    constexpr uint8_t PgUp   = 0x63;   // Look_Up   (remapped extended code, as the arrows)
    constexpr uint8_t PgDn   = 0x64;   // Look_Down (ditto)
}

namespace
{
    // io::GamePadState::Button bit as a uint32_t. (The direction bits use the
    // high bits incl. 1<<31, so they have to be cast.)
    constexpr uint32_t B(int v) { return static_cast<uint32_t>(v); }

    // Every scancode a layout can produce. A layout returns a set of these
    // rather than posting keys itself, which is what lets one scancode be
    // reached from several buttons -- B and START both mean Escape inside a menu,
    // and A means Enter or Left Ctrl depending on whether a menu is up -- without
    // a release from one source cancelling a press that another still holds.
    enum ScIdx {
        SC_UP, SC_DOWN, SC_LEFT, SC_RIGHT,
        SC_RETURN, SC_ESCAPE, SC_LCTRL, SC_SPACE,
        SC_KEYA, SC_KEYZ,
        SC_QUOTE, SC_COMMA, SC_PERIOD,
        SC_KEYJ, SC_LBRACK, SC_RBRACK, SC_PGUP, SC_PGDN,
        SC_COUNT
    };
    // Deliberately unsized: written as kScancode[SC_COUNT], a short initialiser
    // list would zero-fill the tail SILENTLY and those scancodes would just stop
    // arriving. The static_assert below is the only thing that catches it, so keep
    // both if you add or remove an entry.
    constexpr uint8_t kScancode[] = {
        sc::Up,     sc::Down,   sc::Left,   sc::Right,
        sc::Return, sc::Escape, sc::LCtrl,  sc::Space,
        sc::KeyA,   sc::KeyZ,
        sc::Quote,  sc::Comma,  sc::Period,
        sc::KeyJ,   sc::LBrack, sc::RBrack, sc::PgUp,  sc::PgDn,
    };
    static_assert(sizeof(kScancode) / sizeof(kScancode[0]) == SC_COUNT,
                  "kScancode[] and ScIdx are out of step");
    static_assert(SC_COUNT <= 32, "postKeyDiff carries the whole set in a uint32_t");
    constexpr uint32_t K(ScIdx i) { return 1u << i; }

    void postKeyDiff(uint32_t cur)
    {
        static uint32_t prev = 0;
        const uint32_t changed = cur ^ prev;
        if (!changed) return;
#if DUKE_PAD_DIAG
        // The other half of the picture: what the layout actually turned the
        // buttons into. If a button arrives (the "pad:" line) but its scancode
        // never appears here, the fault is in the layout; if the scancode IS
        // posted and the game still does nothing, the fault is Duke-side --
        // most likely the binding in duke3d.cfg's [KeyDefinitions].
        printf("key:");
        for (int i = 0; i < SC_COUNT; i++)
            if (cur & (1u << i)) printf(" %02x", kScancode[i]);
        printf("%s\n", cur ? "" : " (none)");
#endif
        for (int i = 0; i < SC_COUNT; i++)
        {
            const uint32_t bit = 1u << i;
            if (changed & bit)
                pico_display_post_key((cur & bit) ? kScancode[i]
                                                  : (kScancode[i] | 0x80));
        }
        prev = cur;
    }

    uint32_t arrowKeys(uint32_t b)
    {
        using Button = io::GamePadState::Button;
        uint32_t k = 0;
        if (b & B(Button::UP))    k |= K(SC_UP);
        if (b & B(Button::DOWN))  k |= K(SC_DOWN);
        if (b & B(Button::LEFT))  k |= K(SC_LEFT);
        if (b & B(Button::RIGHT)) k |= K(SC_RIGHT);
        return k;
    }

    // The menu mapping: A confirms, B and START go back, on every pad.
    //
    // This is this short only because every source puts a pad's A on io A and its
    // B on io B -- see nesToButtons() and wiiToButtons(). It was once a pair of
    // per-layout tables that disagreed with each other, plus io X/io Y accepted as
    // spare confirm/back slots, all of it papering over a DE-9 NES pad arriving on
    // io B and io Y. Fixed at the source instead. Do NOT add io X or io Y back: if
    // some pad's A fails to confirm, build with -DDUKE_PAD_DIAG=1, find which io
    // bit it actually reports, and fix the translation rather than this table.
    //
    // probe() takes Enter to confirm and Escape to go back, which is why
    // menues.c needs no changes of its own.
    uint32_t menuKeys(uint32_t b, uint32_t a)
    {
        using Button = io::GamePadState::Button;
        uint32_t k = arrowKeys(b);
        if (a & B(Button::A))     k |= K(SC_RETURN);   // confirm
        if (a & B(Button::B))     k |= K(SC_ESCAPE);   // back
        if (a & B(Button::START)) k |= K(SC_ESCAPE);
        return k;
    }

    // Retro-Go's shift activation, for the RETRO layout. START cannot act on
    // press any more -- we have to wait and see whether the hold reaches
    // kShiftHoldUs -- so a short tap fires Use on release, and because the press
    // length is then arbitrary it gets stretched to kUsePulseUs. See the notes at
    // the top of this file for why both numbers are needed.
    constexpr uint64_t kShiftHoldUs = 500000;
    constexpr uint64_t kUsePulseUs  = 100000;

    struct StartHold
    {
        enum State { IDLE, WAIT, SHIFT };
        State state = IDLE;
        uint64_t pressed_at = 0;
        uint64_t pulse_until = 0;

        // A draining Use pulse is Space and probe() takes Space as confirm, so a
        // pulse must never survive into a menu. Same for the layer itself, whose
        // look keys are PgUp/PgDn -- menu up/down as far as probe() is concerned.
        void reset() { state = IDLE; pulse_until = 0; }

        // True while the layer is engaged; sets `use` while the Use pulse runs.
        bool update(bool start_down, uint64_t now, bool &use)
        {
            switch (state)
            {
                case IDLE:
                    if (start_down) { state = WAIT; pressed_at = now; }
                    break;
                case WAIT:
                    // The transparent hold: nothing happens yet, so movement and
                    // every other button keep working while the timer runs.
                    if (!start_down) { state = IDLE; pulse_until = now + kUsePulseUs; }
                    else if (now - pressed_at >= kShiftHoldUs) state = SHIFT;
                    break;
                case SHIFT:
                    if (!start_down) state = IDLE;   // no Use on the way out
                    break;
            }
            use = now < pulse_until;
            return state == SHIFT;
        }
    };

    // The Retro-Go layout and its shift layer (see the tables at the top).
    uint32_t retroLayout(uint32_t b)
    {
        using Button = io::GamePadState::Button;

        const bool in_menu = duke_menu_is_active() != 0;

        // Nine in-game jobs, eight buttons, so the menu is a chord. Both are
        // accepted; see the header comment for which to reach for and why.
        const uint32_t kShoulders = B(Button::L) | B(Button::R);
        const uint32_t kSelStart  = B(Button::SELECT) | B(Button::START);
        const bool chord = (b & kShoulders) == kShoulders ||
                           (b & kSelStart)  == kSelStart;

        static StartHold hold;
        bool use_pulse = false;
        bool shift = false;
        if (in_menu || chord)
            hold.reset();   // keep the pulse and the layer out of menus, and stop
                            // the SELECT+START chord ending in a stray Use
        else
            shift = hold.update((b & B(Button::START)) != 0, time_us_64(), use_pulse);

        // A held-button latch over the whole button word including the
        // directions: with the layer up those are inventory actions, so a
        // direction still held as the layer engages -- you were running -- would
        // fire one. The state carries the chord and layer bits, so forming or
        // breaking either counts as a transition, which is also what stops an
        // uneven release of L+R leaving a stray strafe behind.
        static int8_t was_state = -1;
        static uint32_t held_over = 0;
        const int8_t state = (in_menu ? 1 : 0) | (chord ? 2 : 0) | (shift ? 4 : 0);
        if (was_state != state)
        {
            was_state = state;
            held_over = b;
        }
        held_over &= b;
        const uint32_t a = b & ~held_over;

        if (in_menu) return menuKeys(b, a);

        if (chord) return K(SC_ESCAPE);   // open the menu, nothing else

        // Untouched by the shift layer.
        uint32_t k = 0;
        if (a & B(Button::A))      k |= K(SC_LCTRL);    // fire
        if (a & B(Button::Y))      k |= K(SC_KEYJ);     // jetpack
        if (a & B(Button::L))      k |= K(SC_COMMA);    // strafe left
        if (a & B(Button::R))      k |= K(SC_PERIOD);   // strafe right
        if (a & B(Button::SELECT)) k |= K(SC_QUOTE);    // next weapon
        if (a & B(Button::C))      k |= K(SC_KEYZ);     // crouch (Retro-Go's OPTION,
                                                        // which duplicated it too)
        // Open/use: on release, through the pulse -- START also arms the layer, so
        // it cannot act until we know whether the hold reached kShiftHoldUs.
        if (use_pulse) k |= K(SC_SPACE);

        if (!shift)
        {
            k |= arrowKeys(a);                             // move + turn
            if (a & B(Button::B)) k |= K(SC_KEYA);         // jump
            if (a & B(Button::X)) k |= K(SC_KEYZ);         // crouch
        }
        else
        {
            if (a & B(Button::UP))    k |= K(SC_RETURN);   // use inventory item
            if (a & B(Button::DOWN))  k |= K(SC_KEYJ);     // jetpack
            if (a & B(Button::LEFT))  k |= K(SC_LBRACK);   // previous item
            if (a & B(Button::RIGHT)) k |= K(SC_RBRACK);   // next item
            if (a & B(Button::B))     k |= K(SC_PGDN);     // look down
            if (a & B(Button::X))     k |= K(SC_PGUP);     // look up
        }
        return k;
    }

#if DUKE_HAS_NESPAD
    uint32_t nesToButtons(uint16_t nes, bool is_nes);   // defined below
    bool padIsNes(int port);                            // defined below
#if DUKE_HAS_SNES_MOUSE
    void snesMouseCollect();                            // defined below
#endif

    // Lazy-init on first poll, then harvest the PIO read started on the
    // previous poll and immediately kick off the next one (~200 us per
    // transfer, one poll cycle of latency — negligible).
    //
    // Returns io::GamePadState::Button bits, not the raw serial word: the two
    // ports can hold different pad shapes, so each is translated separately
    // before they are merged.
    //
    // Two robustness rules carried over from fruitjam-doom's i_usbhid.cpp,
    // both learned the hard way on hardware:
    //  - After nespad_begin() the SM runs at a 1 MHz PIO clock, so its first
    //    instruction ("irq wait 0", set-flag-then-park) lands ~1 us after
    //    enable. The 384 MHz core reaches nespad_read_start() first, its clear
    //    outruns the SM's set, the release is lost and the SM parks forever.
    //    Wait out that race before the first start.
    //  - Never call nespad_read_finish() (blocking FIFO reads) unless
    //    nespad_read_ready() says data is waiting — a controller port must not
    //    be able to hang the game loop.
    uint32_t nesButtons()
    {
        static bool inited = false, dead = false;
        static uint32_t last = 0;   // io bits, see the note above
        static uint64_t not_ready_since = 0;
        if (dead) return 0;
        if (!inited)
        {
            inited = true;
            const uint32_t cpu_khz = clock_get_hz(clk_sys) / 1000;
            bool ok = nespad_begin(0, cpu_khz, NES_PIN_CLK, NES_PIN_DATA, NES_PIN_LAT, NES_PIO);
#if DUKE_HAS_NESPAD_1
            ok = nespad_begin(1, cpu_khz, NES_PIN_CLK_1, NES_PIN_DATA_1, NES_PIN_LAT_1, NES_PIO_1) && ok;
#endif
            if (!ok)
            {
                printf("nespad: init failed — NES/SNES ports disabled\n");
                dead = true;
                return 0;
            }
            busy_wait_us(100);   // let both SMs reach their irq-wait park
            nespad_read_start();
            printf("nespad: NES/SNES ports up (CLK GP%d, LAT GP%d, DATA GP%d/GP%d)\n",
                   NES_PIN_CLK, NES_PIN_LAT, NES_PIN_DATA, NES_PIN_DATA_1);
            return 0;
        }
        if (!nespad_read_ready())
        {
            // Reads complete in ~200 us and polls are further apart than that,
            // so transiently not-ready just means "keep the previous state".
            // Never-ready means a dead state machine — give up loudly.
            const uint64_t now = time_us_64();
            if (not_ready_since == 0) not_ready_since = now;
            else if (now - not_ready_since > 1000000)
            {
                printf("nespad: read never completed — NES/SNES ports disabled\n");
                dead = true;
            }
            return last;
        }
        not_ready_since = 0;
        nespad_read_finish();
#if DUKE_PAD_DIAG
        // Driver-level truth, before any translation: the 12-bit serial word per
        // port and the driver's verdict on what is plugged in (`?` = it cannot
        // tell yet, which padIsNes() translates as NES). An EMPTY port must read
        // 0000 (its DATA pin is pulled up, so a disconnected socket inverts to all
        // zeros), and so does a SNES Mouse, which reports through nespad_mouse[].
        // Anything else there is phantom input being OR-ed into the merged button
        // word, which can mask the real pad's presses.
        {
            static const char *const kType[] = { "?", "NES", "SNES", "MOUSE" };
            static uint16_t p0 = 0xffff, p1 = 0xffff;
            static uint8_t t0 = 0xff, t1 = 0xff;
            if (nespad_states_ext[0] != p0 || nespad_states_ext[1] != p1 ||
                nespad_padtype[0] != t0 || nespad_padtype[1] != t1)
            {
                p0 = nespad_states_ext[0];
                p1 = nespad_states_ext[1];
                t0 = nespad_padtype[0];
                t1 = nespad_padtype[1];
                printf("nespad: port0=%04x %s  port1=%04x %s\n",
                       p0, t0 < 4 ? kType[t0] : "!", p1, t1 < 4 ? kType[t1] : "!");
            }
        }
#endif
        // Translated per port BEFORE the merge, because the two ports can hold
        // different pad shapes and the verdict is per port. Merging the raw words
        // first would lose that.
        last = nesToButtons(nespad_states_ext[0], padIsNes(0))
             | nesToButtons(nespad_states_ext[1], padIsNes(1));
#if DUKE_HAS_SNES_MOUSE
        snesMouseCollect();   // a mouse port read as no buttons just above
#endif
        // Straight after the finish is safe here, unlike pico-quake2's timer
        // tick: a state machine is back on its "irq wait" ~5 us after handing
        // its word over, and this read was started >= 2 ms ago (the idle hook's
        // gate), never microseconds.
        nespad_read_start();
        return last;
    }

    // Which shape to translate a port's serial word as -- the answer nesToButtons()
    // needs. Anything the driver has not positively identified as a SNES pad is
    // read as a NES pad; pico-quake2's to_buttons() asks the same question.
    //
    // nespad_padtype[] (pico_shared fb9ad91) knows three things about a port:
    //  - NES: clocks 13-16 all read low, which an ORIGINAL NES pad guarantees
    //    because its 4021's serial input is grounded.
    //  - SNES: either clocks 17-18 read low -- an official SNES pad pulls the line
    //    low after its 16 bits even while idle, where an empty port leaves it high
    //    -- or A, X, L or R has been down (bits 8-11), which a two-button pad can
    //    never set. Sticky until a NES ID replaces it.
    //  - neither: an empty port, an aftermarket NES pad (whose 4021 input idles
    //    high, so it reads bit for bit like a SNES pad with nothing pressed:
    //    A -> 0x0001, B -> 0x0002, bits 8-11 dead), or an idle aftermarket SNES
    //    pad that does not pull the line low.
    //
    // Why "neither" has to mean NES: translated as a SNES pad, an aftermarket NES
    // pad's A lands on io B and its B on io Y -- no fire button, confirm and back
    // swapped in menus. pico-doom never hit this because it maps bit 0 to fire for
    // both shapes at once; Duke's four-button layout cannot do that without giving
    // a SNES pad two fire buttons and no crouch or jetpack.
    //
    // The cost, and it is now the whole cost: an aftermarket SNES pad that does not
    // pull the line low, whose FIRST press is B or Y (before any A, X, L or R), has
    // that one press read as a NES pad's A or B -- fire instead of jump. The next A
    // press corrects it until reboot. An original SNES pad never pays it. (This
    // latch used to live here, on a local nespad_is_nes[]; the driver does it now.)
    //
    // A SNES Mouse is NESPAD_TYPE_MOUSE and reads 0 here, so its port adds no
    // buttons whichever way it is translated.
    bool padIsNes(int port)
    {
        return nespad_padtype[port] != NESPAD_TYPE_SNES;
    }

    // nespad_states_ext is in SNES serial order; translate it into the same
    // io::GamePadState::Button bits the USB pads produce, so a pad in a DE-9 port
    // behaves EXACTLY like the USB pad with the same buttons on it.
    //
    // is_nes comes from padIsNes() above -- read its notes before touching this.
    // A NES pad shifts out only 8 buttons, so its A and B land in the SNES *serial*
    // positions B and Y -- i.e. exactly where a SNES pad's B and Y are. Knowing the
    // shape is what lets a real NES pad's buttons go on io A and io B, where an
    // ordinary two-button pad has them. It then needs no special handling anywhere
    // else.
    uint32_t nesToButtons(uint16_t nes, bool is_nes)
    {
        using Button = io::GamePadState::Button;
        uint32_t b = 0;
        if (nes & 0x0004) b |= Button::SELECT;
        if (nes & 0x0008) b |= Button::START;
        if (nes & 0x0010) b |= Button::UP;
        if (nes & 0x0020) b |= Button::DOWN;
        if (nes & 0x0040) b |= Button::LEFT;
        if (nes & 0x0080) b |= Button::RIGHT;
        if (is_nes)
        {
            // Two buttons, on their namesakes. In RETRO that gives it fire on A
            // and jump on B; in a menu, confirm on A and back on B.
            if (nes & 0x0001) b |= Button::A;    // NES A
            if (nes & 0x0002) b |= Button::B;    // NES B
            return b;
        }
        if (nes & 0x0001) b |= Button::B;        // SNES B
        if (nes & 0x0002) b |= Button::Y;        // SNES Y
        if (nes & 0x0100) b |= Button::A;        // SNES-only from here down
        if (nes & 0x0200) b |= Button::X;
        if (nes & 0x0400) b |= Button::L;
        if (nes & 0x0800) b |= Button::R;
        return b;
    }

#if DUKE_HAS_SNES_MOUSE
    // SNES Mouse, either port. The driver keeps it at its lowest sensitivity,
    // about 50 counts per inch -- a sixteenth of a USB mouse -- and its own higher
    // settings only boost movements of 4 counts or more between two reads, which
    // at a read every 2 ms takes a swipe of 40 inches a second. So the boost is
    // done here, as pico-quake2 does it: a slow movement gets kSnesGainSlow USB
    // counts per SNES count, for aiming, rising to kSnesGainFast from
    // kSnesSpeedSlow to kSnesSpeedFast. Fast, that is a USB mouse of 800 counts
    // per inch, which Duke at its default sensitivity turns about 140 degrees per
    // inch; the Setup Mouse bars scale both mice alike.
    //
    // Speeds are counts per second, averaged over about 32 ms. pico-quake2
    // measures per read because its reads come from a fixed 4 ms timer; here they
    // come from the idle hook, at least 2 ms apart but irregular (_nextpage()'s
    // frame pacing does not run it), so the elapsed time is measured instead.
    constexpr int32_t kSnesGainSlow  = 4;      // 200 counts/inch equivalent
    constexpr int32_t kSnesGainFast  = 16;     // 800 counts/inch equivalent
    constexpr int32_t kSnesSpeedSlow = 50;     // counts/s: 1 inch a second
    constexpr int32_t kSnesSpeedFast = 250;    // counts/s: 5 inches a second
    constexpr uint32_t kSnesAverageUs = 32000;

    int32_t snesUnits[2];      // motion not handed to Duke yet, in 1/16 USB counts
    uint8_t snesButtons;       // bit 0 left, bit 1 right -- Duke's own bits 0/1

    // Called once per completed read: takes the counts the driver accumulated
    // and converts them at the gain for the current speed.
    void snesMouseCollect()
    {
        static uint32_t last_us;
        static int32_t speed;  // counts per second, averaged
        int32_t cx = 0, cy = 0;
        for (int i = 0; i < 2; i++)
        {
            cx += nespad_mouse[i].dx;
            cy += nespad_mouse[i].dy;
            nespad_mouse[i].dx = nespad_mouse[i].dy = 0;
        }
        snesButtons = nespad_mouse[0].buttons | nespad_mouse[1].buttons;

        const uint32_t now = time_us_32();
        const uint32_t dt = (now - last_us) ? (now - last_us) : 1;
        last_us = now;
        const int32_t c = (cx < 0 ? -cx : cx) + (cy < 0 ? -cy : cy);
        const int32_t rate = (int32_t)((int64_t)c * 1000000 / dt);
        // Weighted by the time this read covers; a read after a stall of 32 ms or
        // more replaces the average outright.
        const uint32_t w = dt < kSnesAverageUs ? dt : kSnesAverageUs;
        speed += (int32_t)((int64_t)(rate - speed) * w / kSnesAverageUs);

        int32_t gain;          // in 1/16 USB counts per SNES count
        if (speed <= kSnesSpeedSlow)
            gain = kSnesGainSlow * 16;
        else if (speed >= kSnesSpeedFast)
            gain = kSnesGainFast * 16;
        else
            gain = kSnesGainSlow * 16 + (speed - kSnesSpeedSlow) *
                   (kSnesGainFast - kSnesGainSlow) * 16 / (kSnesSpeedFast - kSnesSpeedSlow);
        snesUnits[0] += cx * gain;
        snesUnits[1] += cy * gain;
    }

    // Whole USB counts of one axis; the fraction stays for the next call.
    int32_t snesMouseTake(int axis)
    {
        const int32_t n = snesUnits[axis] / 16;
        snesUnits[axis] -= n * 16;
        return n;
    }
#endif // DUKE_HAS_SNES_MOUSE
#endif // DUKE_HAS_NESPAD

#if DUKE_HAS_WIIPAD
    // Same job as nesToButtons() for the I2C pads, but it CANNOT share the table:
    // wiipad_read() reports bit0=A, 1=B, 2=Select, 3=Start, 4-7=dpad, 8=X, 9=Y,
    // 10=L, 11=R — its own order, not nespad's SNES serial order. Select, Start,
    // the D-pad and the shoulders line up and pass straight through; the four
    // face buttons are the whole problem.
    //
    // Straight namesakes, so a Wii pad is indistinguishable from the USB pad with
    // the same buttons on it. (This row is also exactly upstream's own
    // ctFromWii() in pico_shared/menu.cpp.) A NES Classic Mini only ever sets
    // bits 0 and 1, which land on io A and io B -- correct for a two-button pad
    // in either layout, and nothing else has to know.
    //
    // This used to take a layout and switch between two face-button tables, to land
    // a NES Classic's two buttons in the two pairs the old NES layout folded. Both
    // the layout and the folding are gone, so no physical button's meaning depends
    // on a setting any more.
    uint32_t wiiToButtons(uint16_t w)
    {
        using Button = io::GamePadState::Button;
        uint32_t b = 0;
        if (w & 0x0004) b |= Button::SELECT;
        if (w & 0x0008) b |= Button::START;
        if (w & 0x0010) b |= B(Button::UP);
        if (w & 0x0020) b |= B(Button::DOWN);
        if (w & 0x0040) b |= B(Button::LEFT);
        if (w & 0x0080) b |= B(Button::RIGHT);
        if (w & 0x0400) b |= Button::L;
        if (w & 0x0800) b |= Button::R;
        if (w & 0x0001) b |= Button::A;
        if (w & 0x0002) b |= Button::B;
        if (w & 0x0100) b |= Button::X;
        if (w & 0x0200) b |= Button::Y;
        return b;
    }
#endif // DUKE_HAS_WIIPAD

    void pollGamePads()
    {
        // Every source is OR-ed into one button word: the two USB pads, both
        // NES/SNES ports on the boards that have them (nesButtons already merges
        // those two), and a Wii extension pad on the boards that have that. So
        // several pads drive one Duke, and a button held on one of them stays
        // held even as another releases the same button.
        uint32_t usb_b = 0, nes_b = 0, wii_b = 0;
        for (int i = 0; i < 2; i++)
        {
            auto &gp = io::getCurrentGamePadState(i);
            if (gp.isConnected()) usb_b |= gp.buttons;
        }
#if DUKE_HAS_NESPAD
        nes_b = nesButtons();   // already io bits, translated per port
#endif
#if DUKE_HAS_WIIPAD
        // Rate-limited to 10 ms inside duke_wiipad_read(): the I2C transaction
        // blocks ~400 us and this runs from duke_pico_idle()'s 2 ms gate.
        wii_b = wiiToButtons(duke_wiipad_read());
#endif
        uint32_t buttons = usb_b | nes_b | wii_b;

#if DUKE_PAD_DIAG
        // -DDUKE_PAD_DIAG=1. Pads do NOT agree on which io bit a face button
        // reports -- several land their A on io Y -- so when a mapping "does not
        // work", press one button at a time and read which name appears here
        // before changing any table. Logged on change only.
        {
            static uint32_t prev_diag = ~0u;
            if (buttons != prev_diag)
            {
                prev_diag = buttons;
                char names[96];
                int n = 0;
                static const struct { uint32_t bit; const char *nm; } kNames[] = {
                    { (uint32_t)io::GamePadState::Button::A,      "A"      },
                    { (uint32_t)io::GamePadState::Button::B,      "B"      },
                    { (uint32_t)io::GamePadState::Button::X,      "X"      },
                    { (uint32_t)io::GamePadState::Button::Y,      "Y"      },
                    { (uint32_t)io::GamePadState::Button::C,      "C"      },
                    { (uint32_t)io::GamePadState::Button::Z,      "Z"      },
                    { (uint32_t)io::GamePadState::Button::SELECT, "SELECT" },
                    { (uint32_t)io::GamePadState::Button::START,  "START"  },
                    { (uint32_t)io::GamePadState::Button::L,      "L"      },
                    { (uint32_t)io::GamePadState::Button::R,      "R"      },
                    { (uint32_t)io::GamePadState::Button::UP,     "UP"     },
                    { (uint32_t)io::GamePadState::Button::DOWN,   "DOWN"   },
                    { (uint32_t)io::GamePadState::Button::LEFT,   "LEFT"   },
                    { (uint32_t)io::GamePadState::Button::RIGHT,  "RIGHT"  },
                };
                names[0] = 0;
                for (auto &e : kNames)
                    if (buttons & e.bit)
                        n += snprintf(names + n, sizeof(names) - n, "%s%s",
                                      n ? "+" : "", e.nm);
                printf("pad: usb=%08lx nes=%08lx wii=%08lx -> %08lx [%s]\n",
                       (unsigned long)usb_b, (unsigned long)nes_b,
                       (unsigned long)wii_b, (unsigned long)buttons,
                       names[0] ? names : "-");
            }
        }
#endif

        postKeyDiff(retroLayout(buttons));
    }

    // USB keyboards work alongside the pad. HID usage (page 0x07) -> DOS set-1
    // scancode, complete enough to TYPE: letters, digits, punctuation, the
    // function keys and the keypad. That is what a partial table cost us --
    // arrows/Enter/Escape navigated the menus fine, but a save slot could not be
    // named because no letter ever arrived, and F6/F9 quicksave were unreachable.
    //
    // The engine side already handles the rest: KB_Startup() fills
    // scancodeToASCII[] for every letter and digit, and KB_Getch() reads the
    // shifted table when a shift scancode is held -- so posting the correct
    // scancode is all that is needed for strget() to receive characters.
    //
    // Arrows deliberately use Duke's REMAPPED extended codes (sc::Up etc, see
    // the sc namespace) rather than 0xE0-prefixed pairs, since we post single
    // bytes. The keypad keeps its own set-1 codes, which is why the keypad and
    // arrow entries differ.
    bool findKey(const io::KeyboardState &st, uint8_t code)
    {
        for (int i = 0; i < 6; i++)
            if (st.keycode[i] == code) return true;
        return false;
    }

    constexpr uint8_t kHid2Sc[] = {
    //  0x00 reserved / error rollover
        0,    0,    0,    0,
    //  0x04 a b c d e f g h i j k l m n o p q r s t u v w x y z
        0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22, 0x23,
        0x17, 0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19,
        0x10, 0x13, 0x1f, 0x14, 0x16, 0x2f, 0x11, 0x2d,
        0x15, 0x2c,
    //  0x1e 1 2 3 4 5 6 7 8 9 0
        0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
    //  0x28 Enter Esc Backspace Tab Space - = [ ] backslash
        0x1c, 0x01, 0x0e, 0x0f, 0x39, 0x0c, 0x0d, 0x1a, 0x1b, 0x2b,
    //  0x32 non-US #  ;  '  `  ,  .  /  CapsLock
        0x2b, 0x27, 0x28, 0x29, 0x33, 0x34, 0x35, 0x3a,
    //  0x3a F1..F10                                     F11   F12
        0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40, 0x41, 0x42, 0x43, 0x44, 0x57, 0x58,
    //  0x46 PrintScreen ScrollLock Pause
        0,    0x46, 0x59,
    //  0x49 Insert Home PageUp Delete End PageDown -- extended-only on a PC
    //  keyboard, and Duke reaches them through extscanToSC, so leave them out
    //  rather than post a code that means something else.
        0,    0,    0,    0,    0,    0,
    //  0x4f Right Left Down Up  (Duke's remapped extended codes)
        sc::Right, sc::Left, sc::Down, sc::Up,
    //  0x53 NumLock  kp/   kp*   kp-   kp+   kpEnter
        0x45, 0x35, 0x37, 0x4a, 0x4e, 0x1c,
    //  0x59 kp1 kp2 kp3 kp4 kp5 kp6 kp7 kp8 kp9 kp0 kp.
        0x4f, 0x50, 0x51, 0x4b, 0x4c, 0x4d, 0x47, 0x48, 0x49, 0x52, 0x53,
    };

    uint8_t hid2sc(uint8_t hid)
    {
        return (hid < sizeof(kHid2Sc)) ? kHid2Sc[hid] : 0;
    }

    void pollKeyboard()
    {
        static io::KeyboardState prev = {};
        const io::KeyboardState cur = io::getCurrentKeyboardState();

        for (int i = 0; i < 6; i++)
        {
            uint8_t code = cur.keycode[i];
            uint8_t s;
            if (code && !findKey(prev, code) && (s = hid2sc(code)) != 0)
                pico_display_post_key(s);
            code = prev.keycode[i];
            if (code && !findKey(cur, code) && (s = hid2sc(code)) != 0)
                pico_display_post_key(s | 0x80);
        }

        // Modifiers: bit n = HID usage 0xE0+n (LCtrl, LShift, LAlt, ...).
        const uint8_t changed = cur.modifier ^ prev.modifier;
        static constexpr uint8_t modsc[8] = {
            sc::LCtrl, sc::LShift, sc::LAlt, 0, sc::LCtrl, sc::LShift, sc::LAlt, 0
        };
        for (int i = 0; i < 8; i++)
        {
            const uint8_t mask = 1u << i;
            if ((changed & mask) && modsc[i])
                pico_display_post_key((cur.modifier & mask) ? modsc[i]
                                                           : (modsc[i] | 0x80));
        }
        prev = cur;
    }
} // namespace

void duke_usb_init(void)
{
#ifdef HAS_USBPIO
#ifdef PIN_USB_HOST_VBUS
    printf("usb: VBUS power on GP%d\n", PIN_USB_HOST_VBUS);
    gpio_init(PIN_USB_HOST_VBUS);
    gpio_set_dir(PIN_USB_HOST_VBUS, GPIO_OUT);
    gpio_put(PIN_USB_HOST_VBUS, 1);
#endif

    pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
    static_assert(PIN_USB_HOST_DP + 1 == PIN_USB_HOST_DM, "D+/D- must be adjacent");
    pio_cfg.pinout = PIO_USB_PINOUT_DPDM;
    pio_cfg.pin_dp = PIN_USB_HOST_DP;
    pio_cfg.tx_ch = 9;   // stay clear of the DMA channels HSTX/audio claim low
    pio_cfg.skip_alarm_pool = true;  // duke_usb_frame.c runs the 1 ms frame (RP2350-E9 workaround)

    tuh_configure(CFG_TUH_RPI_PIO_USB, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);
    printf("usb: PIO-USB host on D+/D- GP%d/GP%d\n", PIN_USB_HOST_DP, PIN_USB_HOST_DM);
    // tuh_init(rhport) is deprecated since TinyUSB 0.20 in favour of
    // tusb_init(rhport, rh_init); same shape pico_shared uses (FrensHelpers.cpp,
    // pio_usb_board_init/initAll). It is what tuh_init() expanded to anyway, and
    // hcd_pio_usb.c ignores rh_init->speed, so the roothub still comes up
    // full-speed as before.
    const tusb_rhport_init_t host_init = {
        .role  = TUSB_ROLE_HOST,
        .speed = TUSB_SPEED_AUTO,
    };
    tusb_init(CFG_TUH_RPI_PIO_USB, &host_init);
    duke_usb_frame_start();
#else
    // Native RP2350 USB controller (rhport 0), selected by tusb_config.h when
    // the board header omits HAS_USBPIO. No PIO program, no VBUS switch and no
    // pin config — the port is the module's own micro-USB socket, so a pad
    // needs an OTG/host adapter. At 384 MHz its 48 MHz is divided from PLL_SYS
    // (duke_boot.c).
    printf("usb: native host controller (rhport 0)\n");
    // No-argument form (pico_shared's non-PIO branch): it expands to
    // tusb_rhport_init(0, NULL), which brings up the host stack on
    // TUH_OPT_RHPORT (0 here) at full speed — identical to the deprecated
    // tuh_init(0), without the deprecation warning.
    tusb_init();
#endif

    // Let already-plugged devices enumerate before the game starts polling
    // (TinyUSB grinds during connect; fruitjam-doom precedent).
    absolute_time_t end = make_timeout_time_ms(1500);
    while (!time_reached(end)) tuh_task();
    printf("usb: host ready\n");
}

void duke_usb_poll(void)
{
    tuh_task();
    pollGamePads();
    pollKeyboard();
}

// ---------------------------------------------------------------------------
// Mouse: BUILD's getmousevalues() (Engine/engine.c) calls these two, once per
// CONTROL_GetInput(), as long as moustat is set (setupmouse() in pico_display.c).
// io::MouseState is filled by the HID callbacks inside tuh_task(), i.e. in
// duke_usb_poll() on this same core and thread, so it is read and zeroed here
// without locking -- the same single-consumer contract pico-doom's pollMouse()
// relies on.
// ---------------------------------------------------------------------------

// Motion older than this is dropped rather than handed over. In game and in the
// menus Duke reads every frame; a longer gap means nothing was reading -- a level
// loading, the title screen -- and the mouse moved meanwhile would arrive as one
// sudden spin.
constexpr uint32_t kMouseStaleUs = 250000;

void readmousexy(short *x, short *y)
{
    io::MouseState &m = io::getCurrentMouseState();
    int32_t dx = m.dx, dy = m.dy;
    m.dx = m.dy = 0;
#if DUKE_HAS_SNES_MOUSE
    dx += snesMouseTake(0);
    dy += snesMouseTake(1);
#endif

    static uint32_t last_us;
    const uint32_t now = time_us_32();
    if (now - last_us > kMouseStaleUs) dx = dy = 0;
    last_us = now;

    // << 2 is SDL display.c's own scaling, so Duke's sensitivity bars keep the
    // meaning they were tuned for: one count turns one BUILD angle unit (of 2048)
    // at the default setting. Clamped so the shift still fits a short.
    if (dx > 8191) dx = 8191; else if (dx < -8191) dx = -8191;
    if (dy > 8191) dy = 8191; else if (dy < -8191) dy = -8191;
    if (x) *x = (short)(dx << 2);
    if (y) *y = (short)(dy << 2);
}

// Duke's button bits are SDL 1.2's, which numbered the wheel as buttons 4 and 5:
// 0 left, 1 right, 2 middle, 3 wheel up, 4 wheel down, 5-6 the side buttons --
// the order Options -> Setup Mouse -> Button Setup names them in. The wheel bits
// are one-shot, set for a single read per scroll, as SDL display.c made them;
// CONTROL_GetInput() turns that into one press of whatever they are bound to,
// and probe() into one step of the menu cursor.
void readmousebstatus(short *bstatus)
{
    io::MouseState &m = io::getCurrentMouseState();
    short b = m.buttons & 0x07;              // HID: left, right, middle
    if (m.buttons & 0x08) b |= 0x20;         // HID button 4 (back)
    if (m.buttons & 0x10) b |= 0x40;         // HID button 5 (forward)
    if (m.wheel > 0)      b |= 0x08;         // HID wheel: positive = away = up
    else if (m.wheel < 0) b |= 0x10;
    m.wheel = 0;
#if DUKE_HAS_SNES_MOUSE
    b |= snesButtons;                        // left, right
#endif
#if DUKE_PAD_DIAG
    {
        static short prev_diag = -1;
        if (b != prev_diag)
        {
            prev_diag = b;
            printf("mouse: usb=%02x", (unsigned)m.buttons);
#if DUKE_HAS_SNES_MOUSE
            printf(" snes=%02x", (unsigned)snesButtons);
#endif
            printf(" -> %02x\n", (unsigned)b);
        }
    }
#endif
    if (bstatus) *bstatus = b;
}
