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

#define CON_COLS DISP_COLS   /* 80 */
#define CON_ROWS DISP_ROWS   /* 40 */

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

/* While a BBC graphics mode owns the framebuffer the console renders
 * nothing (output still reaches the serial mirror); returning rebuilds
 * a clean screen. */
static volatile uint8_t con_gfx_active;

void console_gfx(int active)
{
    con_gfx_active = active;
    if (!active) {
        conbusy = 0;
        conpend = 0;
        cursor_px = -1;
        con_reset();
        con_cursor_on();
    }
}

static void con_output(uint8_t c)
{
    irqflags_t irq;
    uint8_t cq;

    if (con_gfx_active)
        return;

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

    con_output(c);      /* the screen sees every byte, in order */

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
