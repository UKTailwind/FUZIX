/*
 *	/dev/i2c for the Pico Computer 3 - userland access to I2C0.
 *
 *	I2C0 is GP20/GP21, which is both the QWIIC socket on the front of
 *	the board (with its own 10K pullups) and the DS3231's bus.  So the
 *	interesting question here is not how to talk to the controller -
 *	the SDK does that - but how to share it.
 *
 *	Three kinds of sharing, and only one needed anything built:
 *
 *	1. Process against process.  The kernel is NON-PREEMPTIVE: a
 *	   syscall runs to completion unless it sleeps, and nothing below
 *	   sleeps.  One ioctl is therefore one whole transaction, atomic
 *	   against every other process, with no lock of any kind.
 *	2. Process against interrupt.  plt_rtc_secs() samples the DS3231
 *	   from the timer interrupt about once an hour.  It cannot take a
 *	   lock and it cannot wait, so it does neither: i2c0_user_busy is
 *	   set across the transaction below and the poll SKIPS when it is
 *	   set.  Declining costs an hour of crystal drift; interleaving
 *	   would cost both transactions.
 *	3. Core0 against core1.  core1 belongs to the display and never
 *	   touches I2C.
 *
 *	The DS3231's own address is kernel property.  Linux does the same
 *	thing - the RTC driver owns 0x68 and I2C_SLAVE on it returns EBUSY
 *	while the rest of the bus stays free - and the reason is the same:
 *	the kernel is still reading that chip, and a user program writing
 *	its registers would corrupt the system clock, or worse, stop the
 *	oscillator on a battery-backed part that then keeps bad time
 *	across power cycles.
 *
 *	Interface: upstream's, unchanged (Kernel/dev/devi2c.c, minor 7 of
 *	/dev/sys).  ONE transaction per ioctl - a write or a read, with a
 *	STOP at the end - and no repeated START.  Nearly every device is
 *	happy with "write the register number, stop, read the data",
 *	which is what the BME680 on this board wants; the ones that are
 *	not will need a combined-transfer extension, and that is a change
 *	to the shared interface rather than something to invent here.
 */

#include <kernel.h>
#include <kdata.h>
#include <printf.h>
#include <i2c.h>
#include "picosdk.h"
#include <hardware/i2c.h>
#include "config.h"
#include "ds3231.h"

#ifdef CONFIG_DEV_I2C

#define USER_I2C     i2c0
#define RTC_ADDR     0x68          /* the DS3231 - kernel property */
#define XFER_TIMEOUT 20000         /* us, as the RTC path uses */

/*
 *	Bus 1 - MMBasic's I2C2 - has no pins until a program says which,
 *	which is why it needs opening and the fixed bus does not.  Held
 *	here rather than in the pin lock because the LOCK knows about
 *	ownership and this is configuration: which pins, how fast, and
 *	whether it is usable at all.
 */
static uint8_t i2c1_open;
static uint8_t i2c1_sda, i2c1_scl;

/*	SDA and SCL are not interchangeable and not free: the RP2350 muxes
 *	I2C1's SDA only onto pins where (pin & 3) == 2 and its SCL only
 *	where (pin & 3) == 3.  On the PC3's header that is GP38/GP39 and
 *	GP42/GP43.  Refusing the wrong pair here beats a silent bus that
 *	never answers. */
int plt_i2c_open(uint8_t bus, uint8_t sda, uint8_t scl, uint32_t khz)
{
    if (bus != 1)
        return -ENODEV;
    if ((sda & 3) != 2 || (scl & 3) != 3 || scl != sda + 1)
        return -EINVAL;
    if (khz != 100 && khz != 400 && khz != 1000)
        return -EINVAL;
    if (i2c1_open)
        return -EBUSY;

    i2c_init(i2c1, (uint)khz * 1000);
    gpio_set_function(sda, GPIO_FUNC_I2C);
    gpio_set_function(scl, GPIO_FUNC_I2C);
    gpio_pull_up(sda);
    gpio_pull_up(scl);
    i2c1_sda = sda;
    i2c1_scl = scl;
    i2c1_open = 1;
    return 0;
}

/*	Called by CLOSE and by the pin lock when the owner dies - a
 *	controller left enabled on pins that have been reset would drive
 *	nothing and answer nothing, which is worse than being shut. */
void plt_i2c_close(uint8_t bus)
{
    if (bus != 1 || !i2c1_open)
        return;
    i2c_deinit(i2c1);
    i2c1_open = 0;
}

int plt_i2c_msg(struct i2c_msg *msg, uint8_t *kbuf)
{
    unsigned addr = (msg->addr >> 1) & 0x7F;
    int read = msg->addr & 1;
    i2c_inst_t *port;
    int r;

    if (msg->bus == 1) {
        if (!i2c1_open)
            return -ENODEV;         /* no pins assigned: I2C2 not OPEN */
        port = i2c1;
    } else if (msg->bus == 0) {
        port = USER_I2C;
        /* The DS3231's own address is kernel property - see the note at
           the top.  Only on bus 0: the second controller has no clock
           on it and 0x68 there is just a device. */
        if (addr == RTC_ADDR)
            return -EBUSY;
    } else {
        return -ENODEV;
    }

    /* The busy flag is bus 0's alone: it exists to stop the RTC poll
       interleaving with userland on the controller the clock is on. */
    if (msg->bus == 0)
        i2c0_user_busy = 1;

    /* A zero-length message is a bus probe: does anything answer at
       this address?  The SDK has no zero-length transfer, so probe by
       reading one byte and throwing it away - which is what every
       scanner does, and is harmless on a register-file device. */
    if (msg->len == 0) {
        uint8_t junk;

        r = i2c_read_timeout_us(port, addr, &junk, 1, false, XFER_TIMEOUT);
        if (msg->bus == 0)
            i2c0_user_busy = 0;
        return (r == 1) ? 0 : -EIO;
    }

    if (read)
        r = i2c_read_timeout_us(port, addr, kbuf, msg->len, false,
                                XFER_TIMEOUT);
    else
        r = i2c_write_timeout_us(port, addr, kbuf, msg->len, false,
                                 XFER_TIMEOUT);
    if (msg->bus == 0)
        i2c0_user_busy = 0;

    if (r == (int)msg->len)
        return 0;

    /* The controller can be left wedged by a device that died mid
       transaction - the RTC path has always had to cope with this and
       the recovery is shared.  Do it here rather than leaving the next
       caller, or the hourly RTC poll, to find a dead bus.
       ds3231_bus_recover() clocks out bus 0's pins specifically, so on
       bus 1 the controller reset is all there is. */
    if (r < 0) {
        if (msg->bus == 0) {
            ds3231_bus_recover();
            i2c_init(USER_I2C, 400 * 1000);
        } else {
            i2c_init(i2c1, 400 * 1000);
        }
    }
    return -EIO;
}

#endif
