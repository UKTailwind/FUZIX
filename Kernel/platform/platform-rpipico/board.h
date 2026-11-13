#ifndef PC3_BOARD_H
#define PC3_BOARD_H

/*
 * Which machine this kernel is running on - board.c.  One kernel image
 * serves the Pico Computer 3, the Pico Computer 2 and the Olimex
 * RP2350pc.  The values are the number PICOIOC_BOARD answers with.
 */
#define BOARD_PC2       2
#define BOARD_PC3       3
#define BOARD_OLIMEX    4       /* Olimex RP2350pc */

void board_early_detect(void);  /* plt_init: before the PSRAM and the console */
void board_detect(void);        /* device_init: after ds3231_init */

int board_kind(void);           /* BOARD_* */
int board_is_pc2(void);
int board_is_olimex(void);
int board_has_radio(void);      /* the CYW43 is the PC3's alone */
unsigned board_psram_pin(void); /* QMI CS1: GP47, or GP8 on the Olimex */
const char *board_name(void);

#endif
