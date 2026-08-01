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
//  Two things vary by board, both keyed off the force-included cflags header:
//    HAS_USBPIO   present -> Pico-PIO-USB host on PIN_USB_HOST_DP/DM;
//                 absent  -> the RP2350's native USB controller (Murmulator M2,
//                 where a pad plugs in through an OTG adapter).
//    NES_PIN_CLK  present -> legacy NES/SNES ports polled over PIO, folded
//                 into the same scancode stream as the USB pads.
//
#include <stdio.h>

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
    // Murmulator port behaves EXACTLY like a USB SNES pad and there is one
    // mapping table (padmap above) to reason about.
    //
    // A plain NES pad only populates bits 0-7 — as A,B,Select,Start,dpad, which
    // land on SNES B,Y,Select,Start. So it gets menu/back (A), open-use (B),
    // jump (Select), menu (Start) and movement, but NO fire: fire lives on
    // SNES X, which a NES pad does not have. The driver masks the ID bits that
    // would distinguish the two pad shapes before we see them, so this cannot
    // be auto-corrected here — use a SNES pad, or edit this table.
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

    void pollGamePads()
    {
        // Slots 0/1 are the USB pads; slot 2 is both NES/SNES ports merged
        // (they are OR-ed by nesButtons, so two players share one Duke).
        static uint32_t prev[3] = {0, 0, 0};
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
#if DUKE_HAS_NESPAD
        const uint32_t nes = nesToButtons(nesButtons());
        if (nes != prev[2])
        {
            postPadDiff(nes, prev[2]);
            prev[2] = nes;
        }
#endif
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
