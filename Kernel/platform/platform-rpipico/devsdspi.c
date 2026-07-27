#include <kernel.h>
#include <kdata.h>
#include <printf.h>
#include <timer.h>
#include <stdbool.h>
#include <stdlib.h>
#include <blkdev.h>
#include "dev/devsd.h"
#include "picosdk.h"
#include "globals.h"
#include "config.h"
#include <hardware/spi.h>

#ifdef CONFIG_RC2040

/* RC2040 board */
/* Pico SPI GPIO connected to SD SPI1 */
#define Pico_SD_SCK 14
#define Pico_SD_TX  15
#define Pico_SD_RX  12
#define Pico_SD_CS  13

//Pico spi0 or spi1 must match GPIO pins used above.
#define Pico_SD_SPI_MOD spi1

#elif defined(CONFIG_MAKER_PI)
// Maker pico board
// https://www.adafruit.com/product/5160
#define Pico_SD_SCK 10
#define Pico_SD_TX  11
#define Pico_SD_RX  12
#define Pico_SD_CS  15

#define Pico_SD_SPI_MOD spi1

#elif defined(CONFIG_PICOCALC)

// Picocalc
// https://www.clockworkpi.com/product-page/picocalc
#define Pico_SD_SCK 18 //
#define Pico_SD_TX  19 // MOSI
#define Pico_SD_RX  16 // MISO
#define Pico_SD_CS  17 // SD_CS

#elif defined(CONFIG_PICO_COMPUTER_3)

// Pico Computer 3 (RP2350B): SD on the hardware SPI1 block.  The same
// kernel also runs the Pico Computer 2 (board.c runtime detection),
// whose card sits on SCK 30 / MOSI 31 / MISO 32 / CS 29 - MISO is a
// SPI0 pin, so that set is bit-banged, as in MMBasic and MicroPython.
#define Pico_SD_SCK 30
#define Pico_SD_TX  31 // MOSI
#define Pico_SD_RX  28 // MISO
#define Pico_SD_CS  33

#define Pico_SD_SPI_MOD spi1

#define PC2_SD_SCK  30
#define PC2_SD_TX   31 // MOSI
#define PC2_SD_RX   32 // MISO
#define PC2_SD_CS   29

#else

/* Pico SPI GPIO connected to SD SPIO - David Given's Arrangement */
#define Pico_SD_SCK 2
#define Pico_SD_TX  3
#define Pico_SD_RX  4
#define Pico_SD_CS  5

//Pico spi0 or spi1 must match GPIO pins used above.
#define Pico_SD_SPI_MOD spi0

#endif

#define SLOW_SPEED 250000
#define FAST_SPEED 4000000

#ifdef CONFIG_PICO_COMPUTER_3
/* --- Pico Computer 2: bit-banged transport ----------------------------- */
extern int board_is_pc2(void);

static uint8_t sd_bb;           /* nonzero: bit-bang on the PC2 pin set */
static uint8_t sd_bb_fast;      /* 0 = identification speed */

/* A straight port of MMBasic's BitBangSwapSPI as proven on this board
 * (via the MicroPython machine_sdcard bit-bang transport): mode 0,
 * MSB first, 20 us half-bits while identifying the card, NOP-padded
 * thereafter (the 3-NOP flavour: clk_sys is above 200 MHz), and MISO
 * sampled late in the clock-high phase for settling margin. */
#define SD_BB_NOP() __asm__ volatile ("nop")

static uint8_t bb_xfer(uint8_t out)
{
    uint8_t in = 0;
    int bit;
    if (!sd_bb_fast) {
        for (bit = 0; bit < 8; bit++) {
            gpio_put(PC2_SD_TX, out & 0x80);
            busy_wait_us_32(20);
            in <<= 1;
            gpio_put(PC2_SD_SCK, 1);
            busy_wait_us_32(20);
            in += gpio_get(PC2_SD_RX) ? 1 : 0;
            gpio_put(PC2_SD_SCK, 0);
            out <<= 1;
        }
    } else {
        for (bit = 0; bit < 8; bit++) {
            gpio_put(PC2_SD_TX, out & 0x80);
            SD_BB_NOP();
            SD_BB_NOP();
            SD_BB_NOP();
            in <<= 1;
            gpio_put(PC2_SD_SCK, 1);
            SD_BB_NOP();
            SD_BB_NOP();
            SD_BB_NOP();
            in += gpio_get(PC2_SD_RX) ? 1 : 0;
            gpio_put(PC2_SD_SCK, 0);
            out <<= 1;
        }
    }
    return in;
}
#endif

void sd_rawinit(void)
{
#ifdef CONFIG_PICO_COMPUTER_3
    if (board_is_pc2()) {
        sd_bb = 1;
        sd_bb_fast = 0;                     /* identification speed */
        gpio_init(PC2_SD_SCK);
        gpio_set_dir(PC2_SD_SCK, true);
        gpio_init(PC2_SD_TX);
        gpio_put(PC2_SD_TX, true);      /* level set before the pin drives */
        gpio_set_dir(PC2_SD_TX, true);
        gpio_init(PC2_SD_RX);
        gpio_set_dir(PC2_SD_RX, false);
        gpio_set_input_enabled(PC2_SD_RX, true);
        gpio_pull_up(PC2_SD_RX);
        gpio_set_input_hysteresis_enabled(PC2_SD_RX, true);
        gpio_init(PC2_SD_CS);
        gpio_put(PC2_SD_CS, true);      /* never glitch CS low at init */
        gpio_set_dir(PC2_SD_CS, true);
        return;
    }
#endif
    //initilase GPIO ports
    gpio_init(Pico_SD_SCK );
    gpio_init(Pico_SD_TX);
    gpio_init(Pico_SD_RX);
    gpio_init(Pico_SD_CS);

    //set GPIO post function
    gpio_set_function(Pico_SD_SCK, GPIO_FUNC_SPI); // SCK
    gpio_set_function(Pico_SD_TX, GPIO_FUNC_SPI);  // TX
    gpio_set_function(Pico_SD_RX, GPIO_FUNC_SPI);  // RX
    gpio_set_function(Pico_SD_CS, GPIO_FUNC_SIO);  // CS
    gpio_set_dir(Pico_SD_CS, true);

    //initalise SPI module

    spi_init(Pico_SD_SPI_MOD, SLOW_SPEED);
    spi_set_format(Pico_SD_SPI_MOD, 8, 0, 0, SPI_MSB_FIRST);
}

void sd_spi_clock(bool go_fast)
{
#ifdef CONFIG_PICO_COMPUTER_3
    if (sd_bb) {
        sd_bb_fast = go_fast;
        return;
    }
#endif
    spi_set_baudrate(Pico_SD_SPI_MOD,
        go_fast ? FAST_SPEED : SLOW_SPEED);
}

void sd_spi_raise_cs(void)
{
#ifdef CONFIG_PICO_COMPUTER_3
    if (sd_bb) {
        gpio_put(PC2_SD_CS, true);
        return;
    }
#endif
    gpio_put(Pico_SD_CS, true);
}

void sd_spi_lower_cs(void)
{
#ifdef CONFIG_PICO_COMPUTER_3
    if (sd_bb) {
        gpio_put(PC2_SD_CS, false);
        return;
    }
#endif
    gpio_put(Pico_SD_CS, false);
}

void sd_spi_transmit_byte(uint_fast8_t b)
{
#ifdef CONFIG_PICO_COMPUTER_3
    if (sd_bb) {
        bb_xfer(b);
        return;
    }
#endif
    spi_write_blocking(Pico_SD_SPI_MOD, (uint8_t*) &b, 1);
}

uint_fast8_t sd_spi_receive_byte(void)
{
    uint8_t b;
#ifdef CONFIG_PICO_COMPUTER_3
    if (sd_bb)
        return bb_xfer(0xFF);
#endif
    spi_read_blocking(Pico_SD_SPI_MOD, 0xff, (uint8_t*) &b, 1);
    return b;
}

bool sd_spi_receive_sector(void)
{
#ifdef CONFIG_PICO_COMPUTER_3
    if (sd_bb) {
        uint8_t *p = (uint8_t *) blk_op.addr;
        int i;
        for (i = 0; i < 512; i++)
            *p++ = bb_xfer(0xFF);
        return 0;
    }
#endif
    spi_read_blocking(Pico_SD_SPI_MOD, 0xff, (uint8_t*) blk_op.addr, 512);
        return 0;
}

bool sd_spi_transmit_sector(void)
{
#ifdef CONFIG_PICO_COMPUTER_3
    if (sd_bb) {
        uint8_t *p = (uint8_t *) blk_op.addr;
        int i;
        for (i = 0; i < 512; i++)
            bb_xfer(*p++);
        return 0;
    }
#endif
    spi_write_blocking(Pico_SD_SPI_MOD,  (uint8_t*) blk_op.addr, 512);
        return 0;
}
