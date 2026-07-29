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
//  Pad mapping (hid_app folds D-pad hat + left stick into the direction bits).
//  Full table, the SNES-pad caveat and the save/load procedure: see README.md.
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
//  MantaPad note: that pad boots in NES mode, where only two face buttons are
//  reported and its "NES B" is physically the SNES X -- so X arrived as logical
//  A and A arrived as logical B (= Escape), and fire opened the menu until Y was
//  pressed. Defaulted to SNES mode via MANTAPAD_DEFAULT_SNES_MODE.
//
//  Init/poll pattern and the PIO-USB configuration mirror fruitjam-doom's
//  d_main.c / i_usbhid.cpp (proven on this exact board).
//
#include <stdio.h>

#include "pico/stdlib.h"
#include "tusb.h"
#include "pio_usb_configuration.h"
#include "gamepad.h"

extern "C" {
void pico_display_post_key(uint8_t rawcode);   // pico_display.c -> keyhandler()
void duke_usb_init(void);
void duke_usb_poll(void);
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
    constexpr uint8_t Up     = 0x5a;
    constexpr uint8_t Down   = 0x6a;
    constexpr uint8_t Left   = 0x6b;
    constexpr uint8_t Right  = 0x6c;
}

namespace
{
    struct PadMap { uint32_t button; uint8_t scancode; };

    // io::GamePadState::Button bit -> scancode. (The direction bits use the
    // high bits incl. 1<<31, so cast through uint32_t.)
    constexpr uint32_t B(int v) { return static_cast<uint32_t>(v); }
    constexpr PadMap padmap[] = {
        { B(io::GamePadState::Button::UP),     sc::Up     },
        { B(io::GamePadState::Button::DOWN),   sc::Down   },
        { B(io::GamePadState::Button::LEFT),   sc::Left   },
        { B(io::GamePadState::Button::RIGHT),  sc::Right  },
        { B(io::GamePadState::Button::A),      sc::Return },
        { B(io::GamePadState::Button::B),      sc::Escape },
        { B(io::GamePadState::Button::START),  sc::Escape },
        { B(io::GamePadState::Button::X),      sc::LCtrl  },
        { B(io::GamePadState::Button::Y),      sc::Space  },
        { B(io::GamePadState::Button::L),      sc::LShift },
        { B(io::GamePadState::Button::R),      sc::LAlt   },
        { B(io::GamePadState::Button::SELECT), sc::KeyA   },
        { B(io::GamePadState::Button::C),      sc::KeyZ   },
    };

    void postPadDiff(uint32_t cur, uint32_t prev)
    {
        // A scancode can be reached from two buttons (B and START -> Escape):
        // compute the effective per-scancode state, then diff.
        for (const auto &m : padmap)
        {
            if ((cur ^ prev) & m.button)
            {
                bool down = (cur & m.button) != 0;
                pico_display_post_key(down ? m.scancode : (m.scancode | 0x80));
            }
        }
    }

    void pollGamePads()
    {
        static uint32_t prev[2] = {0, 0};
        for (int i = 0; i < 2; i++)
        {
            auto &gp = io::getCurrentGamePadState(i);
            uint32_t cur = gp.isConnected() ? gp.buttons : 0;
            if (cur != prev[i])
            {
                postPadDiff(cur, prev[i]);
                prev[i] = cur;
            }
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
