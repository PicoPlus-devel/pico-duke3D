# The board configurations a release covers.
#
# Sourced by buildAll.sh and release-notes.sh so the built artifacts and the
# "what do I flash?" table in the release notes cannot drift apart.
#
# Each board has a <tag>_cflags.h header and a <tag>-build*.sh script pair. Only
# the standalone variants are released here: the pico-bootLoader variants
# (<tag>-build-forbootloader.sh) are built in the pico-bootLoader repo, which
# invokes those scripts itself and puts the result on the SD card under
# /emu/<HW_CONFIG>/.

RELEASE_BOARDS="fruitjam murmulatorm2 adafruitdvisd"

# Human-readable name, for the release notes.
board_name() {
    case "$1" in
        fruitjam)      echo "Adafruit Fruit Jam" ;;
        murmulatorm2)  echo "Murmulator M2 (RP2350)" ;;
        adafruitdvisd) echo "Pico Plus 2 + Adafruit DVI + SD breakouts" ;;
        *)             echo "$1" ;;
    esac
}

# pico_shared HW_CONFIG number. Comes from <tag>_cflags.h and is what the
# pico-bootLoader picker scans for (/emu/<HW_CONFIG>/), so it is worth showing.
board_hwconfig() {
    case "$1" in
        fruitjam)      echo 8 ;;
        murmulatorm2)  echo 13 ;;
        adafruitdvisd) echo 2 ;;
        *)             echo "?" ;;
    esac
}

# Does this board drive USB host over PIO? The others pass -DENABLE_PIO_USB=0 in
# their build script and never look at PICO_PIO_USB_PATH. Must agree with
# HAS_USBPIO in <tag>_cflags.h, which duke_usb_input.cpp cross-checks.
board_needs_pio_usb() {
    case "$1" in
        fruitjam) return 0 ;;
        *)        return 1 ;;
    esac
}

# Caveat for the release notes, empty when there is none. Keep in step with the
# "Which board" table in README.md and "Status" in TECHNICAL.md: all three boards are
# now hardware-verified, so only genuine hardware requirements are left here.
board_note() {
    case "$1" in
        adafruitdvisd) echo "needs a Pimoroni Pico Plus 2" ;;
        *)             echo "" ;;
    esac
}
