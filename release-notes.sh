#!/bin/sh
# Generate release-notes.md, the body of a GitHub release.
#
#   ./release-notes.sh v1.0 && cat release-notes.md
#
# Four parts, three of them generated:
#   1. a "what do I flash?" table, built from the board list in boards.sh so it
#      cannot drift away from what buildAll.sh actually produced;
#   2. fixed sections covering the SD card, the board caveats and the
#      pico-bootLoader variants (which are NOT released from this repository);
#   3. the CHANGELOG.md section for this tag (the only hand-written part);
#   4. a provenance footer.
cd "$(dirname "$0")" || exit 1

. ./boards.sh

TAG=${1:-}
if [ -z "$TAG" ]; then
    echo "usage: $0 <tag>" >&2
    exit 1
fi

OUT=release-notes.md
: > "$OUT"

# --- 1. what to flash -------------------------------------------------------
{
    echo "## What to flash"
    echo
    echo "Hold BOOTSEL, plug in USB, then drag your board's firmware onto the RPI-RP2"
    echo "drive. The game data is **not** in these files -- it streams from the SD card,"
    echo "so there is nothing else to flash."
    echo
    echo "If the board is already running Duke, **OPTIONS -> BOOTSEL MODE** brings that"
    echo "drive back without holding the button or unplugging anything."
    echo
    echo "| Board | \`HW_CONFIG\` | Firmware |"
    echo "|---|---|---|"
} >> "$OUT"
for TAG_B in $RELEASE_BOARDS; do
    echo "| $(board_name "$TAG_B") | $(board_hwconfig "$TAG_B") | \`duke3d_game_${TAG_B}.uf2\` |" >> "$OUT"
done

# Caveats, if any board has one.
notes=""
for TAG_B in $RELEASE_BOARDS; do
    note=$(board_note "$TAG_B")
    [ -n "$note" ] && notes="${notes}| $(board_name "$TAG_B") | ${note} |
"
done
if [ -n "$notes" ]; then
    {
        echo
        echo "> The boards listed above are tested and working on real hardware, except where"
        echo "> noted below. A few have hardware requirements worth checking before you flash:"
        echo
        echo "| Board | Requirement |"
        echo "|---|---|"
        printf '%s' "$notes"
    } >> "$OUT"
fi

# --- 2. fixed sections ------------------------------------------------------
{
    echo
    echo "### You need an SD card"
    echo
    echo "Duke keeps **nothing** in flash. \`DUKE3D.GRP\` is streamed from the card through"
    echo "BUILD's \`cache1d\`, and savegames and settings sit next to it:"
    echo
    echo "| What | Where |"
    echo "|---|---|"
    echo "| Game data | \`/roms/duke3d/DUKE3D.GRP\` (shareware, or registered/Atomic) |"
    echo "| Saved games | \`/roms/duke3d/game0.sav\` … \`game9.sav\` |"
    echo "| Settings | \`/roms/duke3d/duke3d.cfg\` |"
    echo "| Display mode | \`/settings_DUKE3D.dat\` |"
    echo
    echo "Format the card FAT32 or exFAT. Without a card -- or without the GRP -- the game"
    echo "cannot run at all, and says so **on the HDMI output**: the DOS-style startup"
    echo "screen is live from the first line of boot, and anything fatal is painted on it"
    echo "under the log that led to it. No serial cable needed."
    echo
    echo "### Board notes"
    echo
    echo "* **Adafruit Fruit Jam** -- nothing to add: onboard PSRAM, onboard SD, USB host"
    echo "  over PIO, TLV320 codec with headphone detect (plug in and HDMI audio mutes)."
    echo "* **Murmulator M2** -- USB host is the RP2350's **native** controller, so a pad or"
    echo "  keyboard needs an OTG/host adapter on the module's own socket. The two DE-9"
    echo "  ports work as well. There is no serial console (GP0/1 are the Wii connector);"
    echo "  the startup screen on HDMI is the log. PSRAM smaller than 8 MB still boots --"
    echo "  the tile cache shrinks to fit -- but below about 4 MB it is not worth running."
    echo "* **Pico Plus 2 + Adafruit DVI + SD breakouts** -- the module **must be a Pimoroni"
    echo "  Pico Plus 2** (RP2350B), not a stock Pico 2: Duke cannot run without PSRAM, and"
    echo "  this configuration puts the PSRAM chip select on GP47 = QMI CS1, a pin that"
    echo "  only exists on the B. Its HSTX lanes are the only ones here that are *not*"
    echo "  inverted, so a garbled or absent picture points at \`GPIOHSTXINVERTED\` first."
    echo "  USB host is native, as on the Murmulator."
    echo "* **Olimex RP2040-PICO-PC + Pico 2** -- a stock Pico 2 has no PSRAM, so a PSRAM"
    echo "  chip has to be fitted on GPIO 8 before Duke will start. Sound plays on HDMI and"
    echo "  the board's audio jack at the same time (no I2S DAC). USB host is native, on"
    echo "  the board's USB-A socket; one NES/SNES pad works on the UEXT connector. There is"
    echo "  no serial console (GP0/1 are the PS/2 port, which is not used)."
    echo "* **Waveshare RP2350-PiZero on the PCB by Liu Szuping** -- the PiZero has no PSRAM"
    echo "  unless a chip is soldered on (GPIO 47), and Duke will not start without it. A"
    echo "  PiZero with PSRAM and a non-Winbond flash chip locks up when overclocked until"
    echo "  the flash's QE bit is set once with \`FLASH_QE_SET_1.uf2\`; see"
    echo "  [PSRAM with a non-Winbond flash chip](https://github.com/PicoPlus-devel/pico-infonesPlus#psram-with-a-non-winbond-flash-chip)."
    echo "  Picture on the PCB's HDMI port, sound on HDMI and the audio jack at the same time."
    echo "  USB host runs over PIO on the PiZero's PIO-USB port; a USB hub there does not"
    echo "  work, so only one USB device can be used. The two controller ports work as well"
    echo "  (remove the ESP32 module from its socket to use them)."
    echo "* **Pico Computer 3 (PC3)** -- not yet tested on hardware. Onboard PSRAM, onboard SD."
    echo "  Sound on HDMI and the audio jack at the same time. USB host is the RP2350's native"
    echo "  controller, behind the board's hub: up to three USB devices on its USB-A ports."
    echo "  Two NES/SNES controllers and a Wii Classic controller connect to the I/O header."
    echo "  The serial console is UART1 on GP8/9. The pin assignment may still change."
    echo
    echo "### Controls"
    echo
    echo "USB gamepad, USB keyboard, and the NES/SNES ports on the boards that have them --"
    echo "all folded into one scancode stream, so a pad in a DE-9 port behaves exactly like"
    echo "a USB pad -- a real NES controller included, which is detected as such so that its"
    echo "two buttons land on A and B. The pad layout is Retro-Go's: fire on A, jump on B,"
    echo "crouch on X, next weapon on SELECT, use on START and the menu on L+R, with a shift"
    echo "layer on a held START that turns the D-pad into the inventory. There is nothing to"
    echo "configure. Saving needs no keyboard -- **A** accepts an empty"
    echo "save name. The full tables are in"
    echo "[README.md](https://github.com/${GITHUB_REPOSITORY:-fhoedemakers/pico-duke3D}/blob/${TAG}/README.md#controls)."
    echo
    echo "### Using the pico-bootLoader"
    echo
    echo "The files in **this** release are for flashing a board directly over USB. The"
    echo "bootloader variants -- the ones that live on the SD card under \`/emu/8/\`,"
    echo "\`/emu/13/\`, \`/emu/2/\`, \`/emu/15/\` or \`/emu/18/\` as \`duke3d_game.uf2\` and are"
    echo "launched from the picker --"
    echo "are built and released by the"
    echo "[pico-bootLoader](https://github.com/${GITHUB_REPOSITORY_OWNER:-fhoedemakers}/pico-bootLoader)"
    echo "repository, which links them into its app partition itself."
    echo
} >> "$OUT"

# --- 3. changelog for this tag ----------------------------------------------
if [ -f CHANGELOG.md ]; then
    section=$(awk -v tag="$TAG" '
        # Find the heading whose text is exactly the tag, and print until the
        # next heading of the same or higher level -- deeper (###) subsections
        # belong to the release and must be kept. Exact match, so releasing v1.0
        # cannot accidentally pick up a v1.0.1 section.
        /^#+[ \t]/ {
            level = match($0, /[^#]/) - 1
            text = $0
            sub(/^#+[ \t]+/, "", text)
            sub(/[ \t]+$/, "", text)
            if (found) {
                if (level <= found_level) { exit }
            } else if (text == tag) {
                found = 1; found_level = level; print; next
            }
        }
        found { print }
    ' CHANGELOG.md)
    if [ -n "$section" ]; then
        printf '%s\n\n' "$section" >> "$OUT"
    else
        echo "NOTE: CHANGELOG.md has no section for '$TAG'; appending the whole file." >&2
        echo "## Changes" >> "$OUT"
        echo >> "$OUT"
        cat CHANGELOG.md >> "$OUT"
        echo >> "$OUT"
    fi
else
    echo "NOTE: no CHANGELOG.md; release notes carry no change list." >&2
fi

# --- 4. provenance ----------------------------------------------------------
{
    echo "---"
    echo
    printf 'Built from `%s`' "${GITHUB_SHA:-$(git rev-parse --short HEAD 2>/dev/null || echo unknown)}"
    if [ -n "$PICO_SDK_PATH" ] && [ -d "$PICO_SDK_PATH" ]; then
        sdk=$(git -C "$PICO_SDK_PATH" describe --tags 2>/dev/null)
        [ -n "$sdk" ] && printf ' against Pico SDK `%s`' "$sdk"
    fi
    printf ', version stamp `%s`.\n' "$TAG"
} >> "$OUT"

# GitHub shows release notes like a comment, where every line break stays a
# break, so join the wrapped lines of each paragraph, list item and quote
# (.github/scripts/unwrap_markdown.py, the same as pico-bootLoader's). Headings,
# tables and code are left alone; CHANGELOG.md and the text above stay wrapped.
python3 -I .github/scripts/unwrap_markdown.py "$OUT" > "$OUT.tmp" && mv "$OUT.tmp" "$OUT" || exit 1

echo "Wrote $OUT"
