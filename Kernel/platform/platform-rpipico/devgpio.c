#include <kernel.h>
#include <kdata.h>
#include <gpio.h>
#include "picosdk.h"
#include "pico_ioctl.h"
#include "countpin.h"
#include "pioout.h"

/*
 * Raspberry Pi Pico GPIO.
 *
 * The RP2350B on this board has FORTY-EIGHT GPIOs, not the 28 of an
 * RP2040 - and it matters: the PC3 puts the DS3231 alarm on GP32, which
 * the old limit made unreachable.
 *
 * The interface here is PER PIN (struct gpioreq: pin, val) rather than
 * upstream's byte-and-group model (struct gpio, GPIOC_SETBYTE).  Forty
 * eight pins do not divide into meaningful 8-bit ports on this part,
 * and every user of them here - a BASIC SETPIN, an LED, an LCD strobe -
 * addresses one pin at a time.
 *
 * NO LOCKING, deliberately.  Two things make it unnecessary:
 *
 *   - the kernel is non-preemptive, so an ioctl that does not sleep is
 *     atomic against every other process, and none of these sleep;
 *   - gpio_put(), gpio_set_dir() and gpio_init() reach the hardware
 *     through the RP2350's ATOMIC set/clear/OE registers and a per-pin
 *     function-select, so there is no read-modify-write for an
 *     interrupt or core1 to land in the middle of.
 *
 * Two processes driving two pins therefore cannot corrupt each other,
 * and two driving ONE pin get last-writer-wins - nonsense, but not
 * damage.  Measured with utils/gpiotog.c on GP2 and GP3.
 */

/* RP2350B.  The A-part has 30; asking for a pin that is not bonded out
   is harmless, so the larger number is the useful one here. */
#define NUM_PINS 48

int gpio_ioctl(uarg_t request, char *data)
{
    struct gpioreq gr;

    if (request == GPIOC_COUNT)
        return NUM_PINS;

    /* The counting inputs, GP4-GP7 (countpin.c).  Routed before the
       uget below because their requests carry struct cntreq, not
       struct gpioreq. */
    if (request >= GPIOC_CNT_FIN && request <= GPIOC_CNT_OFF)
        return countpin_ioctl(request, data);

    /* The PIO output word buffer (pioout.c) - same reasoning. */
    if (request == GPIOC_PIOOUT_BUF)
        return pioout_ioctl(request, data);

    if (uget(data, &gr, sizeof(struct gpioreq)) == -1)
        return -1;

    if (gr.pin >= NUM_PINS) {
        udata.u_error = ENODEV;
        return -1;
    }

    switch (request) {
    case GPIOC_SETRW:
        /* Direction: val 0 = input, anything else = output.
         *
         * gpio_set_input_enabled() is NOT optional and not implied by
         * gpio_init() or gpio_set_dir(): on the RP2350 the pad's input
         * enable is a separate bit, and without it gpio_get() reads a
         * steady 0 whatever is on the wire.  A GP33-to-GP35 loopback
         * read 0 both driven high and driven low until this was added,
         * which is a symptom easily mistaken for a dead output.
         *
         * The joystick code in misc.c does the same thing and is where
         * this came from - it also pulls up and enables hysteresis,
         * which suit a switch but not a general purpose input, so they
         * are left out here.  MMBasic's plain DIN floats too. */
        gpio_init(gr.pin);
        if (gr.val) {
            gpio_set_dir(gr.pin, GPIO_OUT);
        } else {
            gpio_set_dir(gr.pin, GPIO_IN);
            gpio_set_input_enabled(gr.pin, true);
        }
        return 0;
    case GPIOC_SET:
        /* Drive the pin.  Kept doing its own init and direction so a
           caller that only wants an output need not do two calls -
           which is what every existing user of this driver expects. */
        gpio_init(gr.pin);
        gpio_set_dir(gr.pin, GPIO_OUT);
        gpio_put(gr.pin, gr.val != 0);
        return 0;
    case GPIOC_CLR:
        gpio_init(gr.pin);
        gpio_set_dir(gr.pin, GPIO_OUT);
        gpio_put(gr.pin, 0);
        return 0;
    case GPIOC_GETBYTE:
        /* The level, as the return value - an ioctl returns an int and
           GPIOC_COUNT already answers that way.
         *
         * gpio_get_all64() rather than gpio_get(), and that is not a
         * matter of taste: with a GP33-to-GP35 loopback, gpio_get(35)
         * returned 0 both driven high and driven low, while the
         * joystick read in misc.c - same pin, same moment, through
         * gpio_get_all64() - followed the wire exactly.  So this is
         * the call that is known to work on this part, and it is what
         * the one other piece of working high-bank input code uses. */
        return (gpio_get_all64() >> gr.pin) & 1;
    }

    /* Anything else really is unsupported.  Say so: the old code fell
       out of the switch returning -1 with errno left as it was, so a
       failed ioctl reported whatever had gone wrong previously. */
    udata.u_error = EINVAL;
    return -1;
}
