/*
 * PC3 video console: a self-contained ANSI/VT100-subset terminal engine
 * rendered into the 1bpp display with RGB332 colours per 8x12 cell.
 *
 * The kernel vt layer (vt.c) is VT52; nothing modern speaks VT52, so the
 * console implements the CSI grammar instead and the filesystem ships a
 * matching termcap entry ("pc3", 80x40). Unknown sequences are parsed
 * and swallowed, never printed.
 *
 * Implemented: BEL BS TAB LF CR; ESC 7/8 (save/restore cursor), ESC E
 * (NEL), ESC M (reverse index), ESC c (reset), ESC ( / ESC ) (charset,
 * ignored); CSI A/B/C/D (counted moves), E/F (next/prev line), G (column),
 * d (row), H/f (position), J (0/1/2 erase display), K (0/1/2 erase line),
 * m (SGR: 0,1,7,22,27,30-37,39,40-47,49,90-97), s/u, ?25l/h (cursor)
 * and ?7l/h (autowrap).  The last column defers its wrap the way a real
 * VT100 does - see wrap_pend.
 *
 * Because the framebuffer is cell-aligned and the MMBasic console font is
 * one byte per row, plot_char is twelve byte stores and scrolling is a
 * memmove. The console tty mirrors the serial console: console_putc sends
 * every byte to the CH340 uart *and* the screen - both ends parse ANSI,
 * so both render the same. At init an xterm window-resize sequence
 * (CSI 8;40;80 t) is sent to the uart so TeraTerm matches the 80x40
 * geometry.
 */

#include <kernel.h>
#include <kdata.h>
#include <printf.h>
#include <tty.h>
#include "picosdk.h"
#include "config.h"
#include "display.h"
#include "rawuart.h"
#include "console_font.h"

#ifdef CONFIG_PC3_USB_KBD
extern void kbd_push(uint8_t c);    /* usbkbd.c input ring */
#endif

#ifdef CONFIG_PC3_DISPLAY

#define FONT_FIRST 32
#define FONT_H     12

/* The terminal's geometry is not fixed: the text console is 80x40 of
 * 8x12 cells, and a graphics mode gives whatever its resolution allows
 * (MODE 2 - 320x240 - is 40x20).  Everything in the engine below works
 * in cells and reads these, so the same CSI parser, scrolling and
 * clamping serve both. */
static int16_t con_cols = DISP_COLS;
static int16_t con_rows = DISP_ROWS;

/* Set while a graphics mode owns the framebuffer.  It used to mean "the
 * console renders nothing"; it now means "render as pixels, not tiles",
 * so a program that changes MODE keeps its console - which is what
 * MMBasic does, and what PRINT in MODE 2 needs. */
static volatile uint8_t con_gfx_active;

/* Does console output reach the DISPLAY as well as the uart?
 *
 * One by default, which is the mirrored console this machine is built
 * around.  A program that owns the screen turns it off for the duration
 * (PICOIOC_CONMIRROR, from OPTION CONSOLE SERIAL), and the kernel turns
 * it back on when that process ends - see console_mirror_release. */
static volatile uint8_t con_to_display = 1;

/* WHO turned it off.  The flag is one piece of global state but it
 * BELONGS to a process, exactly as the framebuffer layer does
 * (display.c's fb_owner), and for the same reason: the kernel has to
 * give it back when that process ends without giving it back on behalf
 * of a process that never had it.
 *
 * It used to be given back by ANY exec and ANY exit.  A graphics
 * program that ran a child - LOAD IMAGE, LOAD JPG/PNG, SPRITE LOADPNG
 * and LOADBMP, BLIT LOAD, all of which decode in a separate binary -
 * had the mirror switched back on underneath it the moment that child
 * exec'd, which painted the console CURSOR into the program's picture:
 * a black 8x12 cell at the text cursor, at (0,0) on a screen the
 * program had just cleared.  Worse than the mark, and invisible until
 * something printed: from then on the program's own console output was
 * mirrored onto the screen it thought it owned, so OPTION CONSOLE
 * SERIAL had quietly stopped meaning anything.  Found 2026-08-29 while
 * checking SPRITE LOADBMP's transparency, which is exactly the shape
 * of thing this looks like from a BASIC program. */
static struct p_tab *con_mirror_owner;

static void con_cursor_off(void);
static void con_cursor_on(void);

/* The CURSOR goes with the flag, and that is not decoration.
 *
 * Everything else the console paints arrives through con_output, which
 * this flag gates - but the cursor is painted by whoever moved it, and
 * a MODE change paints one at (0,0) before the program has asked for
 * anything.  In a graphics mode that is an inverted 8x12 CELL in the
 * program's framebuffer: bbcbasic drew its own 8x8 glyph over the top
 * four fifths of it and left a white 8x4 block under the prompt until
 * something happened to redraw that row.
 *
 * So going quiet TAKES THE CELL BACK (the graphics cursor is an XOR,
 * so the second invert restores exactly what was under it), and coming
 * back puts it where the cursor now is.  That makes the order the
 * caller uses irrelevant: a program may ask before or after its mode
 * change and get the same screen either way. */
void console_mirror(int on)
{
    if (on) {
        con_to_display = 1;
        con_cursor_on();
    } else {
        con_cursor_off();
        con_to_display = 0;
    }
}

/* PICOIOC_CONMIRROR: the ask, and who is asking.  Turning it back on
 * releases the claim - a program that has handed the screen back does
 * not still own it. */
void console_mirror_claim(struct p_tab *who, int on)
{
    con_mirror_owner = on ? NULL : who;
    console_mirror(on);
}

/* On the way out of a process, and on exec: a program that died holding
 * the screen must not leave the machine with a console nobody can see -
 * but only the holder gives it back.  A child, or any unrelated program
 * that happens to exit while a graphics program is running, leaves the
 * screen exactly as it found it. */
void console_mirror_release(struct p_tab *who)
{
    if (con_mirror_owner == NULL || con_mirror_owner != who)
        return;
    con_mirror_owner = NULL;
    console_mirror(1);
}

#define CON_COLS con_cols
#define CON_ROWS con_rows

/* ANSI colours 0-7 dim, 8-15 bright, in SGR order */
static const uint8_t concolours[16] = {
    0x00, 0xA0, 0x14, 0xB4, 0x02, 0xA2, 0x16, 0xB6,
    0x49, 0xE0, 0x1C, 0xFC, 0x03, 0xE3, 0x1F, 0xFF
};

/* Terminal state */
/* 16-bit: CSI parameters can be huge (BBC BASIC probes the width with
 * CSI 999 G-style moves) and the clamping must happen before anything
 * narrows - an int8_t here wrapped 998 to -26, slipped past the >= 80
 * clamp, and made the DSR reply unparseable garbage. */
static int16_t cx, cy;
static uint8_t con_ink = 7, con_paper = 0;
static uint8_t con_bright, con_inverse;
static int16_t saved_x, saved_y;
static uint8_t saved_ink, saved_paper, saved_bright, saved_inverse;
static uint8_t cursorhide;

/* Autowrap (DECAWM, CSI ?7 h/l) and the VT100 "last column" flag.
 *
 * A real terminal does NOT move the cursor when the last column is
 * written: the cursor stays there and the wrap happens only when the
 * next printable character arrives.  Wrapping immediately - which is
 * what this console used to do - means a full-width line moves the
 * cursor to the next row, and writing the bottom right cell scrolls
 * the screen.  Full-screen programs (ue, and the MMBasic editor) paint
 * the last column routinely, so they came out a line adrift.
 *
 * The pending wrap is cancelled by anything that moves the cursor, but
 * NOT by SGR - an editor that changes colour at the right margin must
 * still wrap on the following character. */
static uint8_t con_wrap = 1;    /* DECAWM: set after reset, as on a VT */
static uint8_t wrap_pend;

/* Parser state */
#define ST_NORM 0
#define ST_ESC  1
#define ST_CSI  2
#define ST_SKIP 3 /* consume one byte (charset selectors) */
#define MAXPARM 4
static uint8_t pstate;
static uint8_t priv;
static int16_t parm[MAXPARM];
static uint8_t nparm;

/* Re-entrancy guard (pattern from kernel vt.c): tty echo can re-enter
 * from interrupt context mid-sequence. Queue the one echoed byte and
 * emit it from the interrupted thread. */
static volatile uint8_t conbusy;
static volatile uint8_t conpend;

static int cursor_px = -1, cursor_py;

/* --- rendering ----------------------------------------------------------- */

static void cell_colours(uint8_t *fg, uint8_t *bg)
{
    uint8_t f = concolours[(con_ink & 7) | (con_bright ? 8 : 0)];
    uint8_t p = concolours[con_paper & 15]; /* 8-15: bright backgrounds */
    if (con_inverse) {
        *fg = p;
        *bg = f;
    } else {
        *fg = f;
        *bg = p;
    }
}

/*
 * Rendering in a graphics mode.
 *
 * The text console is TILED: disp_fb holds one byte per 8 pixels of
 * glyph and disp_tile_fg/bg carry an RGB332 pair per cell, which the
 * console expander combines.  A graphics mode has no tiles - disp_fb is
 * pixels - so the same characters have to be drawn AS pixels, which is
 * what MMBasic does when you PRINT in a graphics mode.
 *
 * display_gfx_bitmap already blits a 1-bit source into whatever mode is
 * live, and the console font is exactly that: one byte per row, MSB
 * leftmost, 8x12 = 96 bits, so its bit order matches.  Colours go
 * through display_gfx_map, which picks the nearest palette entry in a
 * 4bpp mode and "anything not black is ink" in a 1bpp one.
 */
/* Geometry of the live graphics mode, read once at the mode change
 * rather than per character: cursor and scroll both need the stride. */
static uint16_t con_gfx_stride, con_gfx_h;
static uint8_t con_gfx_bpp;

/*
 * ANSI colour index to RGB888, fully saturated.
 *
 * NOT the console's own concolours[]: that is an RGB332 set whose
 * "white" is ANSI 7, a light grey (5,5,2 out of 7,7,3).  MODE 2's
 * palette is RGB121 - red in {0,7}, green in {0,2,4,7}, blue in {0,3} -
 * and contains no greys at all, so the nearest entry to that light grey
 * comes out as (7,4,3), a pink.  Text rendered pink instead of white.
 *
 * An ANSI index is one bit per primary, so building the colour straight
 * from those bits gives the eight saturated colours, and every one of
 * them exists exactly in the RGB121 palette.  No table, and white is
 * white.  Bright (SGR 1) adds nothing a saturated palette can show, so
 * it is ignored here rather than faked.
 */
static uint32_t con_ansi888(uint8_t idx)
{
    return ((idx & 1) ? 0xFF0000u : 0u) |
           ((idx & 2) ? 0x00FF00u : 0u) |
           ((idx & 4) ? 0x0000FFu : 0u);
}

/*
 * Cached, because this is per CHARACTER and display_gfx_map() searches
 * all sixteen palette entries: uncached it was 32 comparisons for every
 * glyph, on top of the blit, and tty echo runs in interrupt context.
 * The key is the whole of the colour state, so any SGR change misses
 * the cache and recomputes; console_gfx() clears it so a mode change
 * (with a different palette) cannot serve a stale pair.
 */
static uint8_t con_gfx_key = 0xFF, con_gfx_cfg, con_gfx_cbg;

static void con_gfx_colours(uint8_t *fg, uint8_t *bg)
{
    uint8_t key = (con_ink & 7) | ((con_paper & 7) << 3)
                | (con_inverse ? 0x40 : 0);

    if (key != con_gfx_key) {
        uint8_t f = con_ink & 7;
        uint8_t p = con_paper & 7;

        if (con_inverse) {
            uint8_t t = f;
            f = p;
            p = t;
        }
        con_gfx_cfg = display_gfx_map(con_ansi888(f));
        con_gfx_cbg = display_gfx_map(con_ansi888(p));
        con_gfx_key = key;
    }
    *fg = con_gfx_cfg;
    *bg = con_gfx_cbg;
}

static void con_gfx_plot(int y, int x, uint8_t c)
{
    uint8_t fg, bg;

    con_gfx_colours(&fg, &bg);
    display_gfx_bitmap(x * DISP_CELL_W, y * FONT_H, DISP_CELL_W, FONT_H, 1,
                       fg, bg, &font1[4 + (c - FONT_FIRST) * FONT_H]);
}

static void con_gfx_clear(int y, int x, int l)
{
    uint8_t fg, bg;

    con_gfx_colours(&fg, &bg);
    display_gfx_rect(x * DISP_CELL_W, y * FONT_H,
                     (x + l) * DISP_CELL_W - 1, y * FONT_H + FONT_H - 1, bg);
}

/*
 * The cursor inverts its cell.  Inverting the BYTES does it for both
 * depths: at 4bpp a byte is two pixels and ~b is 15-c in each nibble,
 * at 1bpp ~b flips eight pixels.  A cell starts on a byte boundary
 * either way (x*8 pixels is x*4 bytes at 4bpp, x bytes at 1bpp), and
 * applying it twice restores the cell - which is the same contract the
 * tiled cursor has.
 */
static void con_gfx_cursor(int y, int x)
{
    int row, i, n, off;

    if (!con_gfx_stride)
        return;
    n = (DISP_CELL_W * con_gfx_bpp) / 8;   /* bytes in one row of the cell */
    off = x * n;
    if (off + n > con_gfx_stride)
        return;
    for (row = 0; row < FONT_H; row++) {
        uint8_t *p;

        if ((y * FONT_H + row) >= con_gfx_h)
            break;
        p = &disp_fb[(y * FONT_H + row) * con_gfx_stride + off];
        for (i = 0; i < n; i++)
            p[i] = ~p[i];
    }
}

/* One text line, through the shared scroll in display.c - the same one
 * GFXIOC_SCROLL gives userland, so a program's PRINT running off the
 * bottom and the console's do the same thing.  It moves the WRITE
 * TARGET; this used to memmove disp_fb while con_gfx_plot and
 * con_gfx_clear drew through the target, which would have scrolled the
 * screen out from under a program drawing off-screen. */
static void con_gfx_scroll(int down)
{
    uint8_t fg, bg;

    if (!con_gfx_stride)
        return;
    con_gfx_colours(&fg, &bg);
    display_gfx_scroll(down ? -FONT_H : FONT_H, bg);
}

static void con_plot(int8_t y, int8_t x, uint8_t c)
{
    uint8_t *fb = &disp_fb[(int)y * FONT_H * DISP_STRIDE + x];
    const uint8_t *glyph;
    uint8_t fg, bg;
    int row;

    if (c < FONT_FIRST)
        c = ' ';
    if (con_gfx_active) {
        con_gfx_plot(y, x, c);
        return;
    }
    glyph = &font1[4 + (c - FONT_FIRST) * FONT_H];
    for (row = 0; row < FONT_H; row++) {
        *fb = glyph[row];
        fb += DISP_STRIDE;
    }
    cell_colours(&fg, &bg);
    disp_tile_fg[(int)y * CON_COLS + x] = fg;
    disp_tile_bg[(int)y * CON_COLS + x] = bg;
}

static void con_clear_across(int8_t y, int8_t x, int16_t l)
{
    uint8_t *fb = &disp_fb[(int)y * FONT_H * DISP_STRIDE + x];
    uint8_t fg, bg;
    int row, i;

    if (con_gfx_active) {
        con_gfx_clear(y, x, l);
        return;
    }
    for (row = 0; row < FONT_H; row++) {
        memset(fb, 0, l);
        fb += DISP_STRIDE;
    }
    cell_colours(&fg, &bg);
    for (i = 0; i < l; i++) {
        disp_tile_fg[(int)y * CON_COLS + x + i] = fg;
        disp_tile_bg[(int)y * CON_COLS + x + i] = bg;
    }
}

static void con_clear_lines(int8_t y, int8_t ct)
{
    while (ct--)
        con_clear_across(y++, 0, CON_COLS);
}

static void con_scroll_up(void)
{
    if (con_gfx_active) {
        con_gfx_scroll(0);
        return;
    }
    memmove(disp_fb, disp_fb + FONT_H * DISP_STRIDE,
        (DISP_HEIGHT - FONT_H) * DISP_STRIDE);
    memmove(disp_tile_fg, disp_tile_fg + CON_COLS, (CON_ROWS - 1) * CON_COLS);
    memmove(disp_tile_bg, disp_tile_bg + CON_COLS, (CON_ROWS - 1) * CON_COLS);
    con_clear_lines(CON_ROWS - 1, 1);
}

static void con_scroll_down(void)
{
    if (con_gfx_active) {
        con_gfx_scroll(1);
        return;
    }
    memmove(disp_fb + FONT_H * DISP_STRIDE, disp_fb,
        (DISP_HEIGHT - FONT_H) * DISP_STRIDE);
    memmove(disp_tile_fg + CON_COLS, disp_tile_fg, (CON_ROWS - 1) * CON_COLS);
    memmove(disp_tile_bg + CON_COLS, disp_tile_bg, (CON_ROWS - 1) * CON_COLS);
    con_clear_lines(0, 1);
}

/* Cursor: swap the cell's fg/bg attributes */
static void cursor_swap(int y, int x)
{
    uint8_t *fg, *bg, t;

    if (con_gfx_active) {
        con_gfx_cursor(y, x);
        return;
    }
    fg = &disp_tile_fg[y * CON_COLS + x];
    bg = &disp_tile_bg[y * CON_COLS + x];
    t = *fg;
    *fg = *bg;
    *bg = t;
}

static void con_cursor_off(void)
{
    if (cursor_px >= 0)
        cursor_swap(cursor_py, cursor_px);
    cursor_px = -1;
}

static void con_cursor_on(void)
{
    /* Not while the screen half is off: the cell belongs to whatever
     * program asked for the display to itself.  con_cursor_off is NOT
     * gated - a cursor painted while mirroring was on must still be
     * takeable back afterwards. */
    if (cursorhide || !con_to_display)
        return;
    con_cursor_off();
    cursor_px = cx;
    cursor_py = cy;
    cursor_swap(cy, cx);
}

/* --- terminal engine ----------------------------------------------------- */

static void sgr_reset(void)
{
    con_ink = 7;
    con_paper = 0;
    con_bright = 0;
    con_inverse = 0;
}

static void con_reset(void)
{
    cx = cy = 0;
    saved_x = saved_y = 0;
    cursorhide = 0;
    con_wrap = 1;               /* DECAWM is set after a reset */
    wrap_pend = 0;
    pstate = ST_NORM;
    sgr_reset();
    con_clear_lines(0, CON_ROWS);
}

static void do_sgr(void)
{
    uint8_t i;
    if (nparm == 0) {
        sgr_reset();
        return;
    }
    for (i = 0; i < nparm; i++) {
        int16_t v = parm[i];
        if (v <= 0)
            sgr_reset();
        else if (v == 1)
            con_bright = 1;
        else if (v == 22)
            con_bright = 0;
        else if (v == 7)
            con_inverse = 1;
        else if (v == 27)
            con_inverse = 0;
        else if (v >= 30 && v <= 37)
            con_ink = v - 30;
        else if (v == 39)
            con_ink = 7;
        else if (v >= 40 && v <= 47)
            con_paper = v - 40;
        else if (v == 49)
            con_paper = 0;
        else if (v >= 90 && v <= 97) {
            con_ink = v - 90;
            con_bright = 1;
        }
        else if (v >= 100 && v <= 107)
            con_paper = v - 100 + 8;    /* bright background */
        /* 4/24 underline, 5, etc: accepted, not rendered */
    }
}

static void do_csi(uint8_t c)
{
    int16_t n = parm[0] > 0 ? parm[0] : 1;

    if (priv) {
        /* DEC private modes: cursor visibility and autowrap */
        if (parm[0] == 25) {
            if (c == 'l') {
                cursorhide = 1;
                con_cursor_off();
            } else if (c == 'h') {
                cursorhide = 0;
            }
        } else if (parm[0] == 7) {
            if (c == 'h' || c == 'l') {
                con_wrap = (c == 'h');
                if (!con_wrap)
                    wrap_pend = 0;
            }
        }
        return;
    }

    /* Everything that moves the cursor cancels a pending wrap.  SGR
     * (m), save cursor (s) and the status reports (n) deliberately do
     * not - see the note by wrap_pend. */
    switch (c) {
    case 'A': case 'B': case 'C': case 'D':
    case 'E': case 'F': case 'G': case 'd':
    case 'H': case 'f': case 'J': case 'K': case 'u':
        wrap_pend = 0;
        break;
    default:
        break;
    }

    switch (c) {
    case 'A':
        cy -= n;
        if (cy < 0)
            cy = 0;
        break;
    case 'B':
        cy += n;
        if (cy >= CON_ROWS)
            cy = CON_ROWS - 1;
        break;
    case 'C':
        cx += n;
        if (cx >= CON_COLS)
            cx = CON_COLS - 1;
        break;
    case 'D':
        cx -= n;
        if (cx < 0)
            cx = 0;
        break;
    case 'E':
        cx = 0;
        cy += n;
        if (cy >= CON_ROWS)
            cy = CON_ROWS - 1;
        break;
    case 'F':
        cx = 0;
        cy -= n;
        if (cy < 0)
            cy = 0;
        break;
    case 'G':
        cx = n - 1;
        if (cx < 0)
            cx = 0;
        if (cx >= CON_COLS)
            cx = CON_COLS - 1;
        break;
    case 'd':
        cy = n - 1;
        if (cy < 0)
            cy = 0;
        if (cy >= CON_ROWS)
            cy = CON_ROWS - 1;
        break;
    case 'H':
    case 'f':
        cy = (parm[0] > 0 ? parm[0] : 1) - 1;
        cx = (nparm > 1 && parm[1] > 0 ? parm[1] : 1) - 1;
        if (cy >= CON_ROWS)
            cy = CON_ROWS - 1;
        if (cx >= CON_COLS)
            cx = CON_COLS - 1;
        break;
    case 'J':
        if (parm[0] == 2) {
            con_clear_lines(0, CON_ROWS);
        } else if (parm[0] == 1) {
            if (cy)
                con_clear_lines(0, cy);
            con_clear_across(cy, 0, cx + 1);
        } else {
            con_clear_across(cy, cx, CON_COLS - cx);
            if (cy < CON_ROWS - 1)
                con_clear_lines(cy + 1, CON_ROWS - 1 - cy);
        }
        break;
    case 'K':
        if (parm[0] == 2)
            con_clear_across(cy, 0, CON_COLS);
        else if (parm[0] == 1)
            con_clear_across(cy, 0, cx + 1);
        else
            con_clear_across(cy, cx, CON_COLS - cx);
        break;
    case 'm':
        do_sgr();
        break;
    case 's':
        saved_x = cx;
        saved_y = cy;
        break;
    case 'u':
        cx = saved_x;
        cy = saved_y;
        break;
#ifdef CONFIG_PC3_USB_KBD
    case 'n':
        /* Device status report: answer on the merged keyboard/uart
         * input ring so cursor queries work standalone (a serial
         * terminal may answer too; duplicates are benign). */
        if (parm[0] == 6) {
            uint8_t row = cy + 1, col = cx + 1;
            kbd_push(0x1B);
            kbd_push('[');
            if (row >= 10)
                kbd_push('0' + row / 10);
            kbd_push('0' + row % 10);
            kbd_push(';');
            if (col >= 10)
                kbd_push('0' + col / 10);
            kbd_push('0' + col % 10);
            kbd_push('R');
        } else if (parm[0] == 5) {
            kbd_push(0x1B);
            kbd_push('[');
            kbd_push('0');
            kbd_push('n');
        }
        break;
#endif
    default:
        /* r, L, M, S, T, t, h, l, ... : accepted, ignored */
        break;
    }
}

static void charout(uint8_t c)
{
    switch (pstate) {
    case ST_NORM:
        if (c >= 32 && c != 0x7F) {
            /* Take the wrap the PREVIOUS character deferred, scrolling
             * first so the plot below is always on a real row. */
            if (wrap_pend) {
                wrap_pend = 0;
                cx = 0;
                cy++;
                if (cy >= CON_ROWS) {
                    con_scroll_up();
                    cy = CON_ROWS - 1;
                }
            }
            con_plot(cy, cx, c);
            if (cx + 1 >= CON_COLS) {
                /* Last column: the cursor stays put.  With autowrap on
                 * the next printable character wraps; with it off, the
                 * next one overwrites this cell. */
                if (con_wrap)
                    wrap_pend = 1;
            } else {
                cx++;
            }
        } else if (c == 13) {
            wrap_pend = 0;
            cx = 0;
        } else if (c == 10) {
            wrap_pend = 0;
            cy++;
        } else if (c == 8) {
            wrap_pend = 0;
            if (cx)
                cx--;
        } else if (c == 9) {
            wrap_pend = 0;
            do {
                con_plot(cy, cx, ' ');
                cx++;
            } while ((cx & 7) && cx < CON_COLS);
            if (cx >= CON_COLS) {
                cx = CON_COLS - 1;
                if (con_wrap)
                    wrap_pend = 1;
            }
        } else if (c == 7) {
            /* no beeper */
        } else if (c == 27) {
            pstate = ST_ESC;
        }
        if (cy >= CON_ROWS) {
            con_scroll_up();
            cy = CON_ROWS - 1;
        }
        return;
    case ST_ESC:
        pstate = ST_NORM;
        switch (c) {
        case '[':
            pstate = ST_CSI;
            nparm = 0;
            priv = 0;
            memset(parm, 0, sizeof(parm));
            break;
        case '7':
            saved_x = cx;
            saved_y = cy;
            saved_ink = con_ink;
            saved_paper = con_paper;
            saved_bright = con_bright;
            saved_inverse = con_inverse;
            break;
        case '8':
            wrap_pend = 0;
            cx = saved_x;
            cy = saved_y;
            con_ink = saved_ink;
            con_paper = saved_paper;
            con_bright = saved_bright;
            con_inverse = saved_inverse;
            break;
        case 'E':
            wrap_pend = 0;
            cx = 0;
            cy++;
            if (cy >= CON_ROWS) {
                con_scroll_up();
                cy = CON_ROWS - 1;
            }
            break;
        case 'D':
            wrap_pend = 0;
            cy++;
            if (cy >= CON_ROWS) {
                con_scroll_up();
                cy = CON_ROWS - 1;
            }
            break;
        case 'M':
            wrap_pend = 0;
            if (cy)
                cy--;
            else
                con_scroll_down();
            break;
        case 'c':
            con_reset();
            break;
        case '(':
        case ')':
            pstate = ST_SKIP;
            break;
        default:
            /* =, >, and friends: ignored */
            break;
        }
        return;
    case ST_CSI:
        if (c >= '0' && c <= '9') {
            if (nparm == 0)
                nparm = 1;
            if (nparm <= MAXPARM)
                parm[nparm - 1] = parm[nparm - 1] * 10 + (c - '0');
        } else if (c == ';') {
            if (nparm < MAXPARM)
                nparm++;
        } else if (c == '?') {
            priv = 1;
        } else if (c >= 0x40 && c <= 0x7E) {
            pstate = ST_NORM;
            do_csi(c);
        }
        /* intermediate bytes (space..'/') just accumulate silently */
        return;
    case ST_SKIP:
        pstate = ST_NORM;
        return;
    }
}

/*
 * Handing the framebuffer to a graphics mode, or taking it back.
 *
 * The console used to fall silent for the duration - output still went
 * to the serial mirror, but nothing appeared on screen.  MMBasic does
 * not do that: text and graphics share the screen in a graphics mode,
 * and a program's PRINT is expected to show.  So this now re-points the
 * renderer at pixels and re-reads the geometry (MODE 2's 320x240 is
 * 40x20 cells against the text console's 80x40), and the terminal
 * engine carries on unchanged.
 *
 * Called with the new mode already live, so display_gfx_geom() answers
 * for the mode we are entering rather than the one we are leaving.
 */
void console_gfx(int active)
{
    con_gfx_active = active;
    conbusy = 0;
    conpend = 0;
    cursor_px = -1;
    con_gfx_key = 0xFF;         /* new palette: drop the cached pair */

    if (active) {
        uint16_t w;
        uint8_t mode;

        display_gfx_geom(&w, &con_gfx_h, &con_gfx_stride, &con_gfx_bpp,
                         &mode);
        con_cols = w ? w / DISP_CELL_W : DISP_COLS;
        con_rows = con_gfx_h ? con_gfx_h / FONT_H : DISP_ROWS;
    } else {
        con_gfx_stride = 0;
        con_cols = DISP_COLS;
        con_rows = DISP_ROWS;
    }
    con_reset();
    con_cursor_on();
}

static void con_output(uint8_t c)
{
    irqflags_t irq;
    uint8_t cq;

    irq = di();
    if (conbusy) {
        conpend = c;
        irqrestore(irq);
        return;
    }
    conbusy = 1;
    irqrestore(irq);

    con_cursor_off();
    do {
        charout(c);
        cq = conpend;
        conpend = 0;
        c = cq;
    } while (cq);
    con_cursor_on();
    conbusy = 0;
}

/* --- glue ---------------------------------------------------------------- */

/* Console tty: every byte to the uart and the screen.
 *
 * Escape sequences are held back from the uart until their final byte
 * so that cursor-position queries (CSI ... n) never reach the serial
 * terminal: the screen engine answers those itself, and a second,
 * stale answer from a terminal emulator lands in the program's input
 * queue where it reads as junk keys and a spurious Escape - each
 * query then consumes the previous stale reply and the session
 * corrupts progressively.  Everything else is forwarded intact. */
/*
 * Write to the SCREEN ONLY, never the uart.
 *
 * When console output wedges, the uart is exactly what cannot be
 * trusted to report it - and because console_putc mirrors every byte to
 * both, an ordinary kprintf dies with it.  The screen is painted by
 * core1 out of its own framebuffer and does not care, so this is the one
 * channel that still works.  Diagnostics only.
 */
void console_screen_puts(const char *s)
{
    while (*s)
        con_output((uint8_t)*s++);
}

void console_putc(uint8_t devn, uint8_t c)
{
    /* Holdback state: shared between process context and the tick IRQ
     * (tty echo), so every touch happens under di() and the flush
     * works on a snapshot.  An unprotected version of this let a
     * concurrent nheld++ step past the buffer-full test and scribble
     * over the console state - a silent output wedge. */
    static uint8_t held[24];
    static volatile uint8_t nheld;
    uint8_t copy[24];
    uint8_t i, n;
    irqflags_t irq;

    if (devn != 1) {
        rawuart_putc(devn, c);
        return;
    }

    /*
     * The screen half is skippable.  This console is mirrored - the
     * same byte goes to the display and to the uart - which is what
     * makes the machine usable from either, and it is exactly wrong
     * once a program owns the screen: in a graphics mode the console
     * renders as pixels, so a PRINT meant for a terminal lands on top
     * of the picture.
     *
     * MMBasic has two independent devices and OPTION CONSOLE SERIAL
     * means the uart alone; here that has to be asked for, because
     * there is one device that is both.  PICOIOC_CONMIRROR is the ask,
     * and the kernel puts it back when the process ends - a program
     * that dies holding it must not leave the machine with no console.
     */
    if (con_to_display)
        con_output(c);  /* the screen sees every byte, in order */

    irq = di();
    if (nheld == 0) {
        if (c == 0x1B) {
            held[0] = 0x1B;
            nheld = 1;
            irqrestore(irq);
            return;
        }
        irqrestore(irq);
        rawuart_putc(1, c);
        return;
    }

    n = nheld;
    held[n++] = c;
    nheld = n;
    if ((n == 2 && c != '[') ||
        (n > 2 && c >= 0x40 && c <= 0x7E) ||
        (n >= sizeof held)) {
        /* sequence complete, non-CSI, or implausibly long */
        nheld = 0;
        if (n > 2 && c == 'n') {        /* DSR query: drop */
            irqrestore(irq);
            return;
        }
        memcpy(copy, (void *)held, n);  /* flush from a snapshot */
        irqrestore(irq);
        for (i = 0; i < n; i++)
            rawuart_putc(1, copy[i]);
        return;
    }
    irqrestore(irq);
}

void console_init(void)
{
    static const char resize[] = "\033[8;40;80t"; /* xterm window ops */
    const char *p;

    display_init();
    con_reset();
    con_cursor_on();

    /* Ask the terminal emulator on the serial side to match our 80x40
     * geometry (TeraTerm honours xterm CSI 8 when resizing is allowed) */
    for (p = resize; *p; p++)
        rawuart_putc(1, *p);
}

#endif /* CONFIG_PC3_DISPLAY */
