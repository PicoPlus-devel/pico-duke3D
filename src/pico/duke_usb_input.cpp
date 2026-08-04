//
//  duke_usb_input.cpp — USB gamepad (and keyboard) input for the Duke3D port.
//
//  Bridges the pico_shared USB HID host driver (hid_app.cpp: generic HID pads,
//  DualShock, XInput/Xbox via tusb_xinput, plus boot keyboards) to Duke's
//  keyboard layer. Everything is delivered as DOS scancodes through
//  pico_display_post_key() -> keyhandler(), which updates both KB_KeyDown[]
//  (menus) and CONTROL_UpdateKeyboardState (in-game bindings) — so one mapping
//  drives menu navigation AND gameplay.
//
//  There are three layouts, picked live by the PAD LAYOUT option in GAMEPAD
//  SETUP (PadLayout, Game/config.c). All produce an *effective set* of scancodes
//  which is diffed once per poll, so a scancode reached from two buttons -- or
//  from two different buttons before and after a layout change -- can never be
//  released while something still asks for it.
//
//  Default layout, six-button SNES (hid_app folds the D-pad hat and the left
//  stick into the direction bits). Full table and the save/load procedure: see
//  README.md.
//    D-pad/stick  -> arrow keys        (menu nav + move/turn)
//    A            -> Enter             (menu select; also accepts a save name)
//    B            -> Escape            (menu back / menu open)
//    START        -> Escape            (same as B on purpose)
//    X            -> LeftControl       (fire)
//    Y            -> Space             (open/use)
//    L            -> LeftShift         (run)
//    R            -> LeftAlt           (strafe)
//    SELECT       -> A key             (jump)
//    C            -> Z key             (crouch -- NOT present on a SNES pad, so
//                                       crouch is unreachable on one; move it to
//                                       B if wanted, B duplicates START)
//
//  NES layout, four buttons. Duke needs fire, open, jump, crouch, weapons,
//  strafe and the menu, which is more than four, so SELECT is a shift layer
//  rather than a button of its own -- it emits nothing by itself, which means
//  there is no press-versus-hold to tell apart.
//                    base                 SELECT held
//    A               fire  (LeftControl)  next weapon (')
//    B               open  (Space)        crouch      (Z)
//    START           jump  (A key)        MENU        (Escape)
//    D-pad left/rt   turn  (arrows)       strafe      (, and .)
//    D-pad up/down   move  (arrows)       move        (arrows, unchanged)
//  In a menu the layer is ignored: A -> Enter, B and START -> Escape, D-pad ->
//  arrows. That is what keeps menues.c untouched -- probe() already takes Enter
//  to confirm and Escape to go back. There is no button left for Run, so
//  enabling the option forces Duke's own AutoRun on (see Game/config.c).
//  This SELECT layer is ALWAYS ON and is not the SHIFT MODE option -- see the
//  PadShiftLayer declaration below.
//
//  RETRO layout, Retro-Go's duke3d-go arrangement (see its CONTROLS.md). Their
//  handhelds have ten buttons and a SNES pad has eight: OPTION only duplicated
//  crouch so it is dropped, and MENU becomes a chord.
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
//  SHIFT layer (the SHIFT MODE option -- RETRO ONLY, it is read nowhere else).
//  Duke wants more actions than a pad has buttons, so holding START for 500 ms
//  turns the D-pad into the inventory:
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
//  Four things about that mechanism, in the order they will bite:
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
//   3. SHIFT MODE off removes the layer and the release latency both -- START is
//      then a plain immediate Use, because there is nothing to disambiguate.
//   4. Like the NES layout, RETRO has no button left for Run, so selecting it
//      forces Duke's own AutoRun on (see Game/config.c).
//
//  MantaPad note: that pad boots in NES mode, where only two face buttons are
//  reported and its "NES B" is physically the SNES X -- so X arrived as logical
//  A and A arrived as logical B (= Escape), and fire opened the menu until Y was
//  pressed. Defaulted to SNES mode via MANTAPAD_DEFAULT_SNES_MODE.
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
//                 way. Its face buttons are translated per PAD LAYOUT; see
//                 wiiToButtons() for why that cannot be one fixed table.
//
#include <stdio.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "tusb.h"
#ifdef HAS_USBPIO
#include "pio_usb_configuration.h"
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

// The two GAMEPAD SETUP options, read live so the mapping changes the moment
// they are toggled (Game/config.c, persisted as Misc/NesPadLayout -- a legacy
// key name -- and Misc/PadShiftLayer).
extern int32_t PadLayout;
// PadShiftLayer is read ONLY by retroLayout(), in two places: the hold.reset()
// guard and the Use-on-release-vs-on-press choice. It is meaningless in the other
// two layouts, and menues.c prints "SHIFT MODE: RETRO LAYOUT ONLY" to say so. The
// reason is that only RETRO gives START two jobs (tap = Use, long hold = layer),
// which needs a timer to separate and costs the 100 ms Use pulse -- a trade-off
// worth making optional. nesLayout's SELECT layer needs no such switch: SELECT
// emits nothing on its own, so it is always on and is NOT this setting.
extern int32_t PadShiftLayer;
// Game/menues.c. The NES and RETRO layouts need this because one button has to
// mean fire in game and confirm in a menu.
int duke_menu_is_active(void);
}

// Mirrors PADLAYOUT_* in Game/config.h, which cannot be included here (it comes
// with the int32 typedefs and the rest of the game headers). Keep the two in step.
enum { PAD_SNES = 0, PAD_NES = 1, PAD_RETRO = 2, PAD_COUNT = 3 };

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
    // reached from several buttons -- B and START both mean Escape, and A means
    // Enter or Left Ctrl depending on whether a menu is up -- without a release
    // from one source cancelling a press that another source still holds.
    enum ScIdx {
        SC_UP, SC_DOWN, SC_LEFT, SC_RIGHT,
        SC_RETURN, SC_ESCAPE, SC_LCTRL, SC_SPACE,
        SC_LSHIFT, SC_LALT, SC_KEYA, SC_KEYZ,
        SC_QUOTE, SC_COMMA, SC_PERIOD,
        SC_KEYJ, SC_LBRACK, SC_RBRACK, SC_PGUP, SC_PGDN,
        SC_COUNT
    };
    constexpr uint8_t kScancode[SC_COUNT] = {
        sc::Up,     sc::Down,   sc::Left,   sc::Right,
        sc::Return, sc::Escape, sc::LCtrl,  sc::Space,
        sc::LShift, sc::LAlt,   sc::KeyA,   sc::KeyZ,
        sc::Quote,  sc::Comma,  sc::Period,
        sc::KeyJ,   sc::LBrack, sc::RBrack, sc::PgUp,  sc::PgDn,
    };
    static_assert(SC_COUNT <= 32, "postKeyDiff carries the whole set in a uint32_t");
    constexpr uint32_t K(ScIdx i) { return 1u << i; }

    void postKeyDiff(uint32_t cur)
    {
        static uint32_t prev = 0;
        const uint32_t changed = cur ^ prev;
        if (!changed) return;
        for (int i = 0; i < SC_COUNT; i++)
        {
            const uint32_t bit = 1u << i;
            if (changed & bit)
                pico_display_post_key((cur & bit) ? kScancode[i]
                                                  : (kScancode[i] | 0x80));
        }
        prev = cur;
    }

    constexpr uint32_t kDirButtons =
        B(io::GamePadState::Button::UP)   | B(io::GamePadState::Button::DOWN) |
        B(io::GamePadState::Button::LEFT) | B(io::GamePadState::Button::RIGHT);

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

    // The stock six-button layout (see the table at the top of this file).
    uint32_t snesLayout(uint32_t b)
    {
        using Button = io::GamePadState::Button;
        uint32_t k = arrowKeys(b);
        if (b & B(Button::A))      k |= K(SC_RETURN);
        if (b & B(Button::B))      k |= K(SC_ESCAPE);
        if (b & B(Button::START))  k |= K(SC_ESCAPE);
        if (b & B(Button::X))      k |= K(SC_LCTRL);
        if (b & B(Button::Y))      k |= K(SC_SPACE);
        if (b & B(Button::L))      k |= K(SC_LSHIFT);
        if (b & B(Button::R))      k |= K(SC_LALT);
        if (b & B(Button::SELECT)) k |= K(SC_KEYA);
        if (b & B(Button::C))      k |= K(SC_KEYZ);
        return k;
    }

    enum { NES_A = 1 << 0, NES_B = 1 << 1, NES_SELECT = 1 << 2, NES_START = 1 << 3 };

    // The four-button layout (see the table at the top of this file).
    uint32_t nesLayout(uint32_t b)
    {
        using Button = io::GamePadState::Button;

        // Fold the pad down to the four buttons a NES controller actually has.
        // A NES-shelled MantaPad in SNES mode reports its A on io A and its B
        // on io X (NESB == X in hid_app.cpp); a real NES pad in a DE-9 port
        // arrives through nesToButtons() on io B and io Y, the SNES serial
        // positions. Accept both pairs, which also makes the layout usable from
        // a SNES-shaped pad -- there B and Y sit where a NES pad's buttons are.
        int nes = 0;
        if (b & (B(Button::A) | B(Button::B))) nes |= NES_A;
        if (b & (B(Button::X) | B(Button::Y))) nes |= NES_B;
        if (b & B(Button::SELECT))             nes |= NES_SELECT;
        if (b & B(Button::START))              nes |= NES_START;

        const bool in_menu = duke_menu_is_active() != 0;
        const bool layer   = (nes & NES_SELECT) != 0;

        // A button already down when the menu opens or closes, or when the
        // SELECT layer goes up or down, would immediately act out its new
        // meaning: chording the menu open while firing would confirm the
        // highlighted item on the very next poll, and letting go of SELECT with
        // A still held would start firing. Ignore whatever is held across such
        // a transition until it is released. This doubles as the chord
        // suppressor -- pressing SELECT is itself a transition, so START cannot
        // slip a stray Jump in on the way into SELECT+START.
        static int8_t was_state = -1;
        static int held_over = 0;
        const int8_t state = (in_menu ? 1 : 0) | (layer ? 2 : 0);
        if (was_state != state)
        {
            was_state = state;
            held_over = nes;
        }
        held_over &= nes;
        const int a = nes & ~held_over;

        uint32_t k = arrowKeys(b);
        if (in_menu)
        {
            // The layer is ignored here: a menu only needs confirm and back,
            // and probe() already takes Enter for one and Escape for the other,
            // so menues.c needs no changes of its own.
            if (a & NES_A)     k |= K(SC_RETURN);
            if (a & NES_B)     k |= K(SC_ESCAPE);
            if (a & NES_START) k |= K(SC_ESCAPE);
        }
        else if (layer)
        {
            if (a & NES_A)     k |= K(SC_QUOTE);    // next weapon
            if (a & NES_B)     k |= K(SC_KEYZ);     // crouch
            if (a & NES_START) k |= K(SC_ESCAPE);   // open the menu
            // Left/right strafe instead of turning; up/down still move, so the
            // layer can be held while walking.
            k &= ~(K(SC_LEFT) | K(SC_RIGHT));
            if (b & B(Button::LEFT))  k |= K(SC_COMMA);
            if (b & B(Button::RIGHT)) k |= K(SC_PERIOD);
        }
        else
        {
            if (a & NES_A)     k |= K(SC_LCTRL);    // fire
            if (a & NES_B)     k |= K(SC_SPACE);    // open / use
            if (a & NES_START) k |= K(SC_KEYA);     // jump
        }
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
        if (in_menu || chord || !PadShiftLayer)
            hold.reset();   // keep the pulse and the layer out of menus, and stop
                            // the SELECT+START chord ending in a stray Use
        else
            shift = hold.update((b & B(Button::START)) != 0, time_us_64(), use_pulse);

        // As nesLayout's latch, but over the whole button word including the
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

        if (in_menu)
        {
            // Confirm and back are all a menu needs, and probe() already takes
            // Enter for one and Escape for the other -- so menues.c needs no
            // changes of its own, exactly as with the other two layouts.
            uint32_t k = arrowKeys(b);
            if (a & B(Button::A))     k |= K(SC_RETURN);
            if (a & B(Button::B))     k |= K(SC_ESCAPE);
            if (a & B(Button::START)) k |= K(SC_ESCAPE);
            return k;
        }

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
        // Open/use: on release through the pulse when the layer is armed, on press
        // when it is not -- with nothing to disambiguate there is no reason to add
        // the latency.
        if (PadShiftLayer) { if (use_pulse)            k |= K(SC_SPACE); }
        else               { if (a & B(Button::START)) k |= K(SC_SPACE); }

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
    // Lazy-init on first poll, then harvest the PIO read started on the
    // previous poll and immediately kick off the next one (~200 us per
    // transfer, one poll cycle of latency — negligible).
    //
    // Two robustness rules carried over from fruitjam-doom's i_usbhid.cpp,
    // both learned the hard way on hardware:
    //  - After nespad_begin() the SM runs at a 1 MHz PIO clock, so its first
    //    instruction ("irq wait 0", set-flag-then-park) lands ~1 us after
    //    enable. The 378 MHz core reaches nespad_read_start() first, its clear
    //    outruns the SM's set, the release is lost and the SM parks forever.
    //    Wait out that race before the first start.
    //  - Never call nespad_read_finish() (blocking FIFO reads) unless
    //    nespad_read_ready() says data is waiting — a controller port must not
    //    be able to hang the game loop.
    uint16_t nesButtons()
    {
        static bool inited = false, dead = false;
        static uint16_t last = 0;
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
        last = nespad_states_ext[0] | nespad_states_ext[1];
        nespad_read_start();
        return last;
    }

    // nespad_states_ext is in SNES serial order; translate it into the same
    // io::GamePadState::Button bits the USB pads produce, so a SNES pad in a
    // Murmulator port behaves EXACTLY like a USB SNES pad and there is one set
    // of layouts (snesLayout/nesLayout/retroLayout above) to reason about.
    //
    // A plain NES pad only populates bits 0-7 — as A,B,Select,Start,dpad, which
    // land on SNES B,Y,Select,Start. Under the default layout that leaves it
    // with menu/back (A), open-use (B), jump (Select), menu (Start) and
    // movement, but NO fire: fire lives on SNES X, which a NES pad does not
    // have. The driver masks the ID bits that would distinguish the two pad
    // shapes before we see them, so it cannot be detected and corrected here —
    // that is what the PAD LAYOUT option in GAMEPAD SETUP is for. Such a pad has
    // no L/R either, so in the RETRO layout SELECT+START is its only way into the
    // menu.
    uint32_t nesToButtons(uint16_t nes)
    {
        using Button = io::GamePadState::Button;
        uint32_t b = 0;
        if (nes & 0x0001) b |= Button::B;        // SNES B   (NES A)
        if (nes & 0x0002) b |= Button::Y;        // SNES Y   (NES B)
        if (nes & 0x0004) b |= Button::SELECT;
        if (nes & 0x0008) b |= Button::START;
        if (nes & 0x0010) b |= Button::UP;
        if (nes & 0x0020) b |= Button::DOWN;
        if (nes & 0x0040) b |= Button::LEFT;
        if (nes & 0x0080) b |= Button::RIGHT;
        if (nes & 0x0100) b |= Button::A;        // SNES-only from here down
        if (nes & 0x0200) b |= Button::X;
        if (nes & 0x0400) b |= Button::L;
        if (nes & 0x0800) b |= Button::R;
        return b;
    }
#endif // DUKE_HAS_NESPAD

#if DUKE_HAS_WIIPAD
    // Same job as nesToButtons() for the I2C pads, but it CANNOT share the table:
    // wiipad_read() reports bit0=A, 1=B, 2=Select, 3=Start, 4-7=dpad, 8=X, 9=Y,
    // 10=L, 11=R — its own order, not nespad's SNES serial order. Select, Start,
    // the D-pad and the shoulders line up and pass straight through; the four
    // face buttons are the whole problem.
    //
    // A NES Classic Mini only ever sets bits 0 and 1, and nesLayout() folds
    // (io A | io B) into its NES A and (io X | io Y) into its NES B — so the two
    // face buttons of a NES Classic have to land one in each pair. A Wii Classic
    // Controller Pro (and an SNES Classic Mini, which is the same thing on the
    // wire — they share the Classic Controller Pro protocol AND identity block,
    // so they cannot be told apart) wants all four buttons on their namesakes
    // instead, which puts A and B in the same pair. No single table does both.
    //
    // So pick the table from the layout that is actually selected. The result is
    // that a NES Classic in the NES layout is byte-for-byte a real NES
    // controller, and a Wii Classic Pro in the SNES or RETRO layout is
    // byte-for-byte a USB SNES pad — fire on X, confirm on A. (The SNES/RETRO row
    // is also exactly upstream's own ctFromWii() in pico_shared/menu.cpp.)
    //
    // Switching layout therefore changes what a physical button reports. That is
    // safe because pollGamePads' `settling` latch already suppresses every
    // non-direction button until they are all released after PadLayout moves —
    // the same reason it exists for the USB pads.
    uint32_t wiiToButtons(uint16_t w, int8_t layout)
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
        if (layout == PAD_NES)
        {
            // One face button in each of nesLayout's two pairs, so a NES Classic
            // gets A = fire and B = open. X and Y do not exist on that pad; on a
            // Classic Pro they land as duplicates of the same two actions, which
            // is harmless.
            if (w & 0x0001) b |= Button::A;      // wii A -> NES A (fire)
            if (w & 0x0002) b |= Button::Y;      // wii B -> NES B (open)
            if (w & 0x0100) b |= Button::X;      // wii X -> NES B as well
            if (w & 0x0200) b |= Button::B;      // wii Y -> NES A as well
        }
        else
        {
            // Namesakes, i.e. indistinguishable from a USB SNES pad.
            if (w & 0x0001) b |= Button::A;
            if (w & 0x0002) b |= Button::B;
            if (w & 0x0100) b |= Button::X;
            if (w & 0x0200) b |= Button::Y;
        }
        return b;
    }
#endif // DUKE_HAS_WIIPAD

    void pollGamePads()
    {
        // Clamped here as well as in CONFIG_ReadSetup: that clamps the file, but
        // this layer reads the option live and must not dispatch on a value it
        // does not know. Read before the gather because the Wii pad's face-button
        // translation depends on it (see wiiToButtons).
        const int8_t layout = (PadLayout > PAD_SNES && PadLayout < PAD_COUNT)
                                  ? (int8_t)PadLayout : (int8_t)PAD_SNES;

        // Every source is OR-ed into one button word: the two USB pads, both
        // NES/SNES ports on the boards that have them (nesButtons already merges
        // those two), and a Wii extension pad on the boards that have that. So
        // several pads drive one Duke, and a button held on one of them stays
        // held even as another releases the same button.
        uint32_t buttons = 0;
        for (int i = 0; i < 2; i++)
        {
            auto &gp = io::getCurrentGamePadState(i);
            if (gp.isConnected()) buttons |= gp.buttons;
        }
#if DUKE_HAS_NESPAD
        buttons |= nesToButtons(nesButtons());
#endif
#if DUKE_HAS_WIIPAD
        // Rate-limited to 10 ms inside duke_wiipad_read(): the I2C transaction
        // blocks ~400 us and this runs from duke_pico_idle()'s 2 ms gate.
        buttons |= wiiToButtons(duke_wiipad_read(), layout);
#endif

        // The only way to change layout is to press A on the menu item, so a
        // button is always still held at the switch -- and it means something
        // different on the other side of it. Report nothing until every button
        // has been let go. The D-pad stays live: it navigates and moves the
        // same way in every layout. (The shift layer has its own latch, inside
        // retroLayout, because it also has to catch the directions.)
        static int8_t prev_layout = -1;
        static bool settling = false;
        if (prev_layout != layout)
        {
            prev_layout = layout;
            settling = true;
        }
        if (settling)
        {
            if (buttons & ~kDirButtons) buttons &= kDirButtons;
            else settling = false;
        }

        switch (layout)
        {
            case PAD_NES:   postKeyDiff(nesLayout(buttons));   break;
            case PAD_RETRO: postKeyDiff(retroLayout(buttons)); break;
            default:        postKeyDiff(snesLayout(buttons));  break;
        }
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

    tuh_configure(CFG_TUH_RPI_PIO_USB, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);
    printf("usb: PIO-USB host on D+/D- GP%d/GP%d\n", PIN_USB_HOST_DP, PIN_USB_HOST_DM);
    tuh_init(CFG_TUH_RPI_PIO_USB);
#else
    // Native RP2350 USB controller (rhport 0), selected by tusb_config.h when
    // the board header omits HAS_USBPIO. No PIO program, no VBUS switch and no
    // pin config — the port is the module's own micro-USB socket, so a pad
    // needs an OTG/host adapter. PLL_USB is left at its stock 48 MHz for this;
    // that is why duke_boot.c derives clk_hstx from clk_sys on these boards.
    printf("usb: native host controller (rhport 0)\n");
    tuh_init(0);
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
