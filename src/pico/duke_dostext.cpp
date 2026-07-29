//
//  duke_dostext.c — the DOS startup sequence on the HDMI output.
//
//  Duke's DOS build printed its whole startup ("Using: 'DUKE3D.GRP'",
//  "Compiling: 'GAME.CON'", "Loading art header." ...) to the text screen
//  before switching to graphics mode. Chocolate Duke3D dropped DOS text mode,
//  so that output only reached the UART. This puts it back on screen.
//
//  Geometry is 40 columns rather than the DOS 80: the engine's framebuffer is
//  320x200, and the pico_shared font cell is 8x8, so 40x25 characters map 1:1
//  onto it with no scaling and no leftover pixels. Lines longer than 40 columns
//  wrap instead of being clipped, so nothing is lost.
//
//  Rendering goes straight into the RGB555 scanout surface, not through the
//  engine's 8bpp framebuffer, because at startup Duke has not loaded a palette
//  yet -- there is no index that reliably means "white". Writing RGB555 also
//  matches how the pico_shared emulators draw their menus.
//
//  Glyphs come from pico_shared's font (3rdparty/pico_shared_drivers/fonts,
//  getcharslicefrom8x8font), the same one the bootloader and every emulator in
//  the family uses, so the startup screen looks like they do.
//
#include <string.h>

#include "duke_dostext.h"
#include "FrensFonts.h"

#define RGB555(r, g, b) ((uint16_t)((((r) >> 3) << 10) | (((g) >> 3) << 5) | ((b) >> 3)))

// DOS console colours: light grey on black, with a red header bar.
#define COL_FG     RGB555(170, 170, 170)
#define COL_BG     RGB555(0, 0, 0)
#define COL_HDR_FG RGB555(255, 255, 255)
#define COL_HDR_BG RGB555(168, 0, 0)

#define HEADER_ROW  0     // row 0 is the title bar; text scrolls in rows 1..24
#define FIRST_TEXT_ROW 1

static uint16_t *s_surface;
static int       s_stride;
static bool      s_active;
static int       s_col, s_row;
static char      s_cells[DOSTEXT_ROWS][DOSTEXT_COLS];

static void blit_cell(int col, int row, char c, uint16_t fg, uint16_t bg)
{
    if (!s_surface) return;
    // The font holds ASCII 32..126 only; anything else shows as a space.
    if (c < FONT_FIRST_ASCII || c >= FONT_FIRST_ASCII + FONT_N_CHARS) c = ' ';
    for (int y = 0; y < FONT_CHAR_HEIGHT; y++) {
        uint16_t *px = s_surface + (row * FONT_CHAR_HEIGHT + y) * s_stride
                                 + col * FONT_CHAR_WIDTH;
        // Slices are stored LSB-first (same walk as pico_shared's menu blit).
        unsigned slice = (unsigned char)getcharslicefrom8x8font(c, y);
        for (int x = 0; x < FONT_CHAR_WIDTH; x++) {
            px[x] = (slice & 1u) ? fg : bg;
            slice >>= 1;
        }
    }
}

static void draw_header(void)
{
    static const char title[] = "Duke Nukem 3D";
    char bar[DOSTEXT_COLS];
    memset(bar, ' ', sizeof(bar));
    int len = (int)strlen(title);
    int start = (DOSTEXT_COLS - len) / 2;
    memcpy(bar + start, title, (size_t)len);
    for (int c = 0; c < DOSTEXT_COLS; c++)
        blit_cell(c, HEADER_ROW, bar[c], COL_HDR_FG, COL_HDR_BG);
}

// Redraw the scrolling region from s_cells. Used after a scroll, which is the
// only case where more than one cell changes at a time.
static void repaint_text(void)
{
    for (int r = FIRST_TEXT_ROW; r < DOSTEXT_ROWS; r++)
        for (int c = 0; c < DOSTEXT_COLS; c++)
            blit_cell(c, r, s_cells[r][c], COL_FG, COL_BG);
}

void duke_dostext_init(uint16_t *surface, int stride)
{
    s_surface = surface;
    s_stride  = stride;
    s_col = 0;
    s_row = FIRST_TEXT_ROW;
    memset(s_cells, ' ', sizeof(s_cells));
    if (s_surface) {
        // Clear the whole surface, including any pixels outside the character
        // grid (there are none at 40x25, but the surface may be larger).
        for (int y = 0; y < DOSTEXT_ROWS * FONT_CHAR_HEIGHT; y++)
            for (int x = 0; x < s_stride; x++)
                s_surface[y * s_stride + x] = COL_BG;
    }
    draw_header();
    repaint_text();
    s_active = true;
}

void duke_dostext_stop(void) { s_active = false; }
bool duke_dostext_active(void) { return s_active; }

static void scroll_up(void)
{
    memmove(&s_cells[FIRST_TEXT_ROW], &s_cells[FIRST_TEXT_ROW + 1],
            (size_t)(DOSTEXT_ROWS - FIRST_TEXT_ROW - 1) * DOSTEXT_COLS);
    memset(&s_cells[DOSTEXT_ROWS - 1], ' ', DOSTEXT_COLS);
    s_row = DOSTEXT_ROWS - 1;
    repaint_text();
}

static void newline(void)
{
    s_col = 0;
    if (++s_row >= DOSTEXT_ROWS) scroll_up();
}

void duke_dostext_write(const char *s, size_t len)
{
    if (!s_active || !s_surface) return;

    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        switch (c) {
            case '\n': newline(); continue;
            case '\r': s_col = 0;  continue;
            case '\t':
                do {
                    s_cells[s_row][s_col] = ' ';
                    blit_cell(s_col, s_row, ' ', COL_FG, COL_BG);
                    if (++s_col >= DOSTEXT_COLS) { newline(); break; }
                } while (s_col & 7);
                continue;
            default:
                if ((unsigned char)c < 32) continue;   // drop other controls
                break;
        }
        if (s_col >= DOSTEXT_COLS) newline();          // wrap, do not clip
        s_cells[s_row][s_col] = c;
        blit_cell(s_col, s_row, c, COL_FG, COL_BG);
        s_col++;
    }
}
