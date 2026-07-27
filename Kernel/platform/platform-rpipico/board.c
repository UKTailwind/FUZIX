/*
 * Runtime board identification for the Pico Computer family: one
 * kernel image serves both machines.
 *
 *   Pico Computer 3   DS3231 32 kHz output wired to GP27; SD on
 *                     hardware SPI1 (SCK 30 / MOSI 31 / MISO 28 / CS 33).
 *   Pico Computer 2   32 kHz not connected; SD bit-banged on
 *                     SCK 30 / MOSI 31 / MISO 32 / CS 29 (MISO is a
 *                     SPI0 pin, so no hardware instance covers the set).
 *
 * The 32 kHz clock on GP27 is the signature - MMBasic's
 * TestPicoComputer3() test, as also ported to MicroPython: pull GP27
 * up, wait for four edges of the square wave inside a 200 us window.
 * A quiet pin means Pico Computer 2 (or a PC3 with a stopped DS3231 -
 * the only ambiguity, accepted as in the other firmwares).
 *
 * Runs before the SD card comes up; the CYW43 (PC3 only) is unused by
 * Fuzix, so the SD wiring is the only consequence.
 */

#include <kernel.h>
#include <kdata.h>
#include <printf.h>
#include "picosdk.h"
#include "config.h"

#define DETECT_PIN 27
#define DETECT_US  200

static uint8_t board_pc3 = 1;

int board_is_pc2(void)
{
    return !board_pc3;
}

const char *board_name(void)
{
    return board_pc3 ? "PICO COMPUTER 3" : "PICO COMPUTER 2";
}

void board_detect(void)
{
    uint64_t timeout;

    gpio_init(DETECT_PIN);
    gpio_set_input_enabled(DETECT_PIN, true);
    gpio_set_dir(DETECT_PIN, false);
    gpio_pull_up(DETECT_PIN);

    /* two full cycles: high -> low -> high -> low */
    timeout = time_us_64() + DETECT_US;
    while (gpio_get(DETECT_PIN) && time_us_64() < timeout)
        ;
    while (!gpio_get(DETECT_PIN) && time_us_64() < timeout)
        ;
    while (gpio_get(DETECT_PIN) && time_us_64() < timeout)
        ;
    while (!gpio_get(DETECT_PIN) && time_us_64() < timeout)
        ;

    board_pc3 = time_us_64() < timeout;
    if (!board_pc3)
        gpio_disable_pulls(DETECT_PIN);  /* ordinary GPIO on the PC2 */

    kprintf("board: %s\n", board_name());
}
