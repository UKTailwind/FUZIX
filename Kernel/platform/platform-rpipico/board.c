/*
 * Runtime board identification: one kernel image serves three machines.
 *
 *   Pico Computer 3   DS3231 32 kHz output wired to GP27; PSRAM on GP47;
 *                     SD on hardware SPI1 (SCK 30 / MOSI 31 / MISO 28 /
 *                     CS 33); console UART1 on GP8/GP9; I2S DAC; radio.
 *   Pico Computer 2   32 kHz not connected; PSRAM on GP47; SD bit-banged
 *                     on SCK 30 / MOSI 31 / MISO 32 / CS 29 (MISO is a
 *                     SPI0 pin, so no hardware instance covers the set).
 *   Olimex RP2350pc   PSRAM on GP8; SD on SPI1 (SCK 10 / MOSI 11 /
 *                     MISO 24 / CS 9); console UART0 on GP0/GP1; HDMI
 *                     in the default lane order; PWM audio on GP26/GP27
 *                     into a PAM8003 (shutdown GP35, volume GP34); no
 *                     RTC, no radio.
 *
 * The tests are PicoMite's (TestPicoComputer3, 225bff0): a 32 kHz clock
 * on GP27 is a PC3; otherwise a PSRAM answering the read-ID with its
 * chip select on GP47 is a PC2, and one on GP8 is the Olimex.  Here
 * they run in two places, because they have to:
 *
 *  - board_early_detect(), from plt_init before the PSRAM and the
 *    console UART are started, asks the PSRAM.  On the Olimex GP8 is
 *    the PSRAM's chip select (on the flash's QSPI bus) and GP9 the SD
 *    card's, and the PC3's console is UART1 on exactly those pins: the
 *    first boot message would select the PSRAM underneath flash XIP and
 *    stall the board, which is what PicoMite's HDMIUSB build does there.
 *    GP47 answering means the PC2/PC3 family; GP8 answering means the
 *    Olimex, and its own pins are set up here.  Nothing answering keeps
 *    the old default and the old behaviour.
 *
 *  - board_detect(), from device_init after ds3231_init, tells the PC2
 *    from the PC3 by the clock on GP27.  It has to wait for ds3231_init,
 *    which turns the 32 kHz output back on if it was off: the output is
 *    in a battery-backed register that another firmware, an older
 *    kernel or a careless BASIC write can clear, and a PC3 in that state
 *    used to be taken for a PC2 for ever - its SD card probed on the
 *    wrong MISO, and no way in because the machine would not boot.
 *    What is left is a genuinely stopped oscillator, which no amount of
 *    asking will fix.  ds3231_init must not run on the Olimex, whose
 *    GP20/GP21 are HDMI CEC and hot plug, which is why the PSRAM is
 *    asked first.
 *
 * The SD wiring is the most immediate consequence of the answer, and
 * not the only one: net_cyw43.c asks board_has_radio() before it powers
 * the radio, because the CYW43's clock and chip select are GP29 and
 * GP25 - on a Pico Computer 2 GP29 is the SD card's chip select and
 * GP25 the LED.  It asks at NETIOC_UP, long after this has run.
 */

#include <kernel.h>
#include <kdata.h>
#include <printf.h>
#include "picosdk.h"
#include <hardware/pwm.h>
#include "config.h"
#include "psram.h"
#include "board.h"

#define DETECT_PIN 27
#define DETECT_US  200

#define PSRAM_PIN_PC     47
#define PSRAM_PIN_OLIMEX 8

static uint8_t board = BOARD_PC3;

int board_kind(void)
{
    return board;
}

int board_is_pc2(void)
{
    return board == BOARD_PC2;
}

int board_is_olimex(void)
{
    return board == BOARD_OLIMEX;
}

int board_has_radio(void)
{
    return board == BOARD_PC3;
}

unsigned board_psram_pin(void)
{
    return board == BOARD_OLIMEX ? PSRAM_PIN_OLIMEX : PSRAM_PIN_PC;
}

const char *board_name(void)
{
    switch (board) {
    case BOARD_PC2:
        return "PICO COMPUTER 2";
    case BOARD_OLIMEX:
        return "OLIMEX RP2350PC";
    default:
        return "PICO COMPUTER 3";
    }
}

/*
 * The Olimex's own pins, as PicoMite's InitReservedIO sets them up for
 * OPTION RESET RP2350PC:
 *
 *   GP32/GP33  the ES8311 codec's I2C, shared with the HDMI DDC: pulled
 *              high and left alone, so the bus stays idle and the codec
 *              does nothing.  The audio is PWM (sound_hw.c).
 *   GP35       the PAM8003's shutdown, through an inverting transistor:
 *              held low, so the amplifier is on.
 *   GP34       its volume, PWM slice 9 A through a transistor and a
 *              filter into the VOL pin: 25 kHz, above the audio band,
 *              at 50%.  pinlock.c keeps slice 9 from user programs.
 */
static void olimex_pins(void)
{
    uint slice;
    uint32_t wrap;
    pwm_config c;

    for (uint gp = 32; gp <= 33; gp++) {
        gpio_init(gp);
        gpio_pull_up(gp);
    }

    gpio_init(35);
    gpio_put(35, 0);
    gpio_set_dir(35, GPIO_OUT);

    slice = pwm_gpio_to_slice_num(34);
    wrap = clock_get_hz(clk_sys) / 25000;
    c = pwm_get_default_config();
    pwm_config_set_wrap(&c, wrap - 1);
    pwm_init(slice, &c, false);
    pwm_set_chan_level(slice, PWM_CHAN_A, wrap / 2);
    gpio_set_function(34, GPIO_FUNC_PWM);
    pwm_set_enabled(slice, true);
}

void board_early_detect(void)
{
    if (psram_probe(PSRAM_PIN_PC))
        return;                 /* PC2 or PC3: board_detect() says which */
    if (psram_probe(PSRAM_PIN_OLIMEX)) {
        board = BOARD_OLIMEX;
        olimex_pins();
    }
}

void board_detect(void)
{
    uint64_t timeout;
    int pc3;

    if (board == BOARD_OLIMEX) {
        /* settled before the PSRAM came up; GP27 is its right channel */
        kprintf("board: %s\n", board_name());
        return;
    }

    gpio_init(DETECT_PIN);
    gpio_set_input_enabled(DETECT_PIN, true);
    gpio_set_dir(DETECT_PIN, false);
    gpio_pull_up(DETECT_PIN);
    gpio_set_input_hysteresis_enabled(DETECT_PIN, true);

    /* let the pull-up charge the trace before watching for edges: a
     * floating pin crossing the threshold could otherwise fake the
     * clock and misidentify the board */
    timeout = time_us_64() + 50;
    while (time_us_64() < timeout)
        ;

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

    pc3 = time_us_64() < timeout;
    board = pc3 ? BOARD_PC3 : BOARD_PC2;
    if (!pc3)
        gpio_disable_pulls(DETECT_PIN);  /* ordinary GPIO on the PC2 */

    kprintf("board: %s\n", board_name());
}
