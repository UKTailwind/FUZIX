#ifndef PC3_DISPLAY_H
#define PC3_DISPLAY_H

#include <stdint.h>
#include <stdbool.h>

/* 640x480, 1 bit per pixel, MSB leftmost: 80 bytes per line */
#define DISP_WIDTH   640
#define DISP_HEIGHT  480
#define DISP_STRIDE  (DISP_WIDTH / 8)

/* Colour attributes per 8x12 character cell (RGB332 fg/bg): 80x40 cells */
#define DISP_CELL_W  8
#define DISP_CELL_H  12
#define DISP_COLS    (DISP_WIDTH / DISP_CELL_W)    /* 80 */
#define DISP_ROWS    (DISP_HEIGHT / DISP_CELL_H)   /* 40 */

extern uint8_t disp_fb[DISP_HEIGHT * DISP_STRIDE]; /* 38400 bytes */
extern uint8_t disp_tile_fg[DISP_ROWS * DISP_COLS];
extern uint8_t disp_tile_bg[DISP_ROWS * DISP_COLS];

/* Start the scanout on core1 (test pattern preloaded) */
void display_init(void);

bool display_in_blanking(void);
bool display_stack_ok(void);

#endif
