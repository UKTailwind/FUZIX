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
 * m (SGR: 0,1,7,22,27,30-37,39,40-47,49,90-97), s/u and ?25l/h.
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

#ifdef CONFIG_PC3_DISPLAY

#define FONT_FIRST 32
#define FONT_H     12

#define CON_COLS DISP_COLS   /* 80 */
#define CON_ROWS DISP_ROWS   /* 40 */

/* ANSI colours 0-7 dim, 8-15 bright, in SGR order */
static const uint8_t concolours[16] = {
    0x00, 0xA0, 0x14, 0xB4, 0x02, 0xA2, 0x16, 0xB6,
    0x49, 0xE0, 0x1C, 0xFC, 0x03, 0xE3, 0x1F, 0xFF
};

/* Terminal state */
static int8_t cx, cy;
static uint8_t con_ink = 7, con_paper = 0;
static uint8_t con_bright, con_inverse;
static int8_t saved_x, saved_y;
static uint8_t saved_ink, saved_paper, saved_bright, saved_inverse;
static uint8_t cursorhide;

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
    uint8_t p = concolours[con_paper & 7];
    if (con_inverse) {
        *fg = p;
        *bg = f;
    } else {
        *fg = f;
        *bg = p;
    }
}

static void con_plot(int8_t y, int8_t x, uint8_t c)
{
    uint8_t *fb = &disp_fb[(int)y * FONT_H * DISP_STRIDE + x];
    const uint8_t *glyph;
    uint8_t fg, bg;
    int row;

    if (c < FONT_FIRST)
        c = ' ';
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
    memmove(disp_fb, disp_fb + FONT_H * DISP_STRIDE,
        (DISP_HEIGHT - FONT_H) * DISP_STRIDE);
    memmove(disp_tile_fg, disp_tile_fg + CON_COLS, (CON_ROWS - 1) * CON_COLS);
    memmove(disp_tile_bg, disp_tile_bg + CON_COLS, (CON_ROWS - 1) * CON_COLS);
    con_clear_lines(CON_ROWS - 1, 1);
}

static void con_scroll_down(void)
{
    memmove(disp_fb + FONT_H * DISP_STRIDE, disp_fb,
        (DISP_HEIGHT - FONT_H) * DISP_STRIDE);
    memmove(disp_tile_fg + CON_COLS, disp_tile_fg, (CON_ROWS - 1) * CON_COLS);
    memmove(disp_tile_bg + CON_COLS, disp_tile_bg, (CON_ROWS - 1) * CON_COLS);
    con_clear_lines(0, 1);
}

/* Cursor: swap the cell's fg/bg attributes */
static void cursor_swap(int y, int x)
{
    uint8_t *fg = &disp_tile_fg[y * CON_COLS + x];
    uint8_t *bg = &disp_tile_bg[y * CON_COLS + x];
    uint8_t t = *fg;
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
    if (cursorhide)
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
        /* 4/24 underline, 5, etc: accepted, not rendered */
    }
}

static void do_csi(uint8_t c)
{
    int16_t n = parm[0] > 0 ? parm[0] : 1;

    if (priv) {
        /* DEC private: only cursor visibility is rendered */
        if (parm[0] == 25) {
            if (c == 'l') {
                cursorhide = 1;
                con_cursor_off();
            } else if (c == 'h') {
                cursorhide = 0;
            }
        }
        return;
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
    default:
        /* r, L, M, S, T, t, n, h, l, ... : accepted, ignored */
        break;
    }
}

static void charout(uint8_t c)
{
    switch (pstate) {
    case ST_NORM:
        if (c >= 32 && c != 0x7F) {
            con_plot(cy, cx, c);
            if (++cx >= CON_COLS) {
                cx = 0;
                cy++;
            }
        } else if (c == 13) {
            cx = 0;
        } else if (c == 10) {
            cy++;
        } else if (c == 8) {
            if (cx)
                cx--;
        } else if (c == 9) {
            do {
                con_plot(cy, cx, ' ');
                cx++;
            } while ((cx & 7) && cx < CON_COLS);
            if (cx >= CON_COLS) {
                cx = 0;
                cy++;
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
            cx = saved_x;
            cy = saved_y;
            con_ink = saved_ink;
            con_paper = saved_paper;
            con_bright = saved_bright;
            con_inverse = saved_inverse;
            break;
        case 'E':
            cx = 0;
            cy++;
            if (cy >= CON_ROWS) {
                con_scroll_up();
                cy = CON_ROWS - 1;
            }
            break;
        case 'D':
            cy++;
            if (cy >= CON_ROWS) {
                con_scroll_up();
                cy = CON_ROWS - 1;
            }
            break;
        case 'M':
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

/* Console tty: every byte to the uart and the screen */
void console_putc(uint8_t devn, uint8_t c)
{
    rawuart_putc(devn, c);
    if (devn == 1)
        con_output(c);
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
