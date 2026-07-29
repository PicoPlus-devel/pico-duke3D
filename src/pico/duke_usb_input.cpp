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
//  Full table, the SNES-pad caveat and the save/load procedure: docs/CONTROLS.md.
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

    // USB keyboards work alongside the pad for free: diff the boot-keyboard
    // state and post the (small) set of keys Duke needs most. Full keyboard
    // translation (typing save names etc.) can come later.
    bool findKey(const io::KeyboardState &st, uint8_t code)
    {
        for (int i = 0; i < 6; i++)
            if (st.keycode[i] == code) return true;
        return false;
    }

    // HID usage -> DOS scancode for the common control keys.
    uint8_t hid2sc(uint8_t hid)
    {
        switch (hid) {
            case 0x29: return sc::Escape;   // HID_KEY_ESCAPE
            case 0x28: return sc::Return;   // ENTER
            case 0x2C: return sc::Space;
            case 0x52: return sc::Up;       // arrows
            case 0x51: return sc::Down;
            case 0x50: return sc::Left;
            case 0x4F: return sc::Right;
            case 0x04: return sc::KeyA;     // 'a'
            case 0x1D: return sc::KeyZ;     // 'z'
            default:
                // letters/digits handled later; ignore for now
                return 0;
        }
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
