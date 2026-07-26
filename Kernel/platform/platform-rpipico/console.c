/*
 * PC3 video console: the Fuzix kernel vt layer (VT52+ escapes, cursor,
 * scrolling, ink/paper) rendered into the 1bpp display with RGB332
 * colours per 8x12 cell. Because the framebuffer is cell-aligned and the
 * MMBasic console font is one byte per row, plot_char is twelve byte
 * stores and scrolling is a memmove.
 *
 * The console tty mirrors the serial console: console_putc sends every
 * byte to the CH340 uart *and* the screen, so local and remote views are
 * identical. Input still comes from the uart (the USB keyboard comes
 * next); the vt input hooks are not used yet.
 */

#include <kernel.h>
#include <kdata.h>
#include <printf.h>
#include <vt.h>
#include <tty.h>
#include "picosdk.h"
#include "config.h"
#include "display.h"
#include "rawuart.h"
#include "console_font.h"

#ifdef CONFIG_PC3_DISPLAY

#define FONT_FIRST 32
#define FONT_H     12

/* vt ink/paper codes 0-15 -> RGB332 (classic 8 + bright repeats; 8 is
 * dark grey so "bright black" is visible) */
static const uint8_t vtcolours[16] = {
    0x00, 0xA0, 0x14, 0xB4, 0x02, 0xA2, 0x16, 0xB6,   /* dim  */
    0x49, 0xE0, 0x1C, 0xFC, 0x03, 0xE3, 0x1F, 0xFF    /* bright */
};

static int cursor_x = -1, cursor_y = -1;

static void cell_colours(uint8_t *fg, uint8_t *bg)
{
    uint8_t f = vtcolours[vtink & 15];
    uint8_t p = vtcolours[vtpaper & 15];
    if (vtattr & VTA_INVERSE) {
        *fg = p;
        *bg = f;
    } else {
        *fg = f;
        *bg = p;
    }
}

void plot_char(int8_t y, int8_t x, uint16_t c)
{
    uint8_t *fb = &disp_fb[(int)y * FONT_H * DISP_STRIDE + x];
    const uint8_t *glyph;
    uint8_t fg, bg;
    int row;

    c &= 0xFF;
    if (c < FONT_FIRST)
        c = ' ';
    glyph = &font1[4 + (c - FONT_FIRST) * FONT_H];

    for (row = 0; row < FONT_H; row++) {
        *fb = glyph[row];
        fb += DISP_STRIDE;
    }
    cell_colours(&fg, &bg);
    disp_tile_fg[(int)y * DISP_COLS + x] = fg;
    disp_tile_bg[(int)y * DISP_COLS + x] = bg;
}

void clear_across(int8_t y, int8_t x, int16_t l)
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
        disp_tile_fg[(int)y * DISP_COLS + x + i] = fg;
        disp_tile_bg[(int)y * DISP_COLS + x + i] = bg;
    }
}

void clear_lines(int8_t y, int8_t ct)
{
    while (ct--) {
        clear_across(y, 0, DISP_COLS);
        y++;
    }
}

void scroll_up(void)
{
    memmove(disp_fb, disp_fb + FONT_H * DISP_STRIDE,
        (DISP_HEIGHT - FONT_H) * DISP_STRIDE);
    memmove(disp_tile_fg, disp_tile_fg + DISP_COLS,
        (DISP_ROWS - 1) * DISP_COLS);
    memmove(disp_tile_bg, disp_tile_bg + DISP_COLS,
        (DISP_ROWS - 1) * DISP_COLS);
}

void scroll_down(void)
{
    memmove(disp_fb + FONT_H * DISP_STRIDE, disp_fb,
        (DISP_HEIGHT - FONT_H) * DISP_STRIDE);
    memmove(disp_tile_fg + DISP_COLS, disp_tile_fg,
        (DISP_ROWS - 1) * DISP_COLS);
    memmove(disp_tile_bg + DISP_COLS, disp_tile_bg,
        (DISP_ROWS - 1) * DISP_COLS);
}

/* Cursor: swap the cell's fg/bg attributes (no bitmap change) */
static void cursor_swap(int y, int x)
{
    uint8_t *fg = &disp_tile_fg[y * DISP_COLS + x];
    uint8_t *bg = &disp_tile_bg[y * DISP_COLS + x];
    uint8_t t = *fg;
    *fg = *bg;
    *bg = t;
}

void cursor_off(void)
{
    if (cursor_x >= 0)
        cursor_swap(cursor_y, cursor_x);
    cursor_x = -1;
}

void cursor_disable(void)
{
    cursor_off();
}

void cursor_on(int8_t y, int8_t x)
{
    cursor_off();
    cursor_x = x;
    cursor_y = y;
    cursor_swap(y, x);
}

void do_beep(void)
{
}

/* Attribute changes are picked up at plot time (cell_colours) */
void vtattr_notify(void)
{
}

/* Console tty: every byte to the uart and the screen */
void console_putc(uint8_t devn, uint8_t c)
{
    rawuart_putc(devn, c);
    if (devn == 1) {
        unsigned char ch = c;
        vtoutput(&ch, 1);
    }
}

void console_init(void)
{
    display_init();
    vtinit();
    clear_lines(0, DISP_ROWS);
}

#endif /* CONFIG_PC3_DISPLAY */
