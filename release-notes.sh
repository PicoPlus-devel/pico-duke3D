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
        echo "> **Not every board has been run on hardware.** The Adafruit Fruit Jam is the"
        echo "> verified one; the others build clean and are believed correct, but are"
        echo "> untested. Reports welcome."
        echo
        echo "| Board | Status |"
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
    echo
    echo "### Controls"
    echo
    echo "USB gamepad, USB keyboard, and the NES/SNES ports on the boards that have them --"
    echo "all folded into one scancode stream, so a pad in a DE-9 port behaves exactly like"
    echo "a USB pad. There are three pad layouts, chosen with **PAD LAYOUT** in *Options →"
    echo "Game Options → Gamepad Setup*: the default six-button SNES one, a four-button NES"
    echo "layout where SELECT acts as a shift layer, and a Retro-Go layout with fire on A,"
    echo "use on START and the menu on L+R -- plus **SHIFT MODE**, where holding START for"
    echo "500 ms turns the D-pad into the inventory. Saving needs no keyboard -- **A** accepts an empty"
    echo "save name. The full tables are in"
    echo "[README.md](https://github.com/${GITHUB_REPOSITORY:-fhoedemakers/pico-duke3D}/blob/${TAG}/README.md#controls)."
    echo
    echo "### Using the pico-bootLoader"
    echo
    echo "The files in **this** release are for flashing a board directly over USB. The"
    echo "bootloader variants -- the ones that live on the SD card under \`/emu/8/\`,"
    echo "\`/emu/13/\` or \`/emu/2/\` as \`duke3d_game.uf2\` and are launched from the picker --"
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

echo "Wrote $OUT"
