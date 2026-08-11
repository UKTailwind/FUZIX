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
#include "pico_ioctl.h"		/* PLK_PIN, PLK_I2C */
#include "pinlock.h"

#ifdef CONFIG_DEV_I2C

#define USER_I2C     i2c0
#define RTC_ADDR     0x68          /* the DS3231 - kernel property */
#define XFER_TIMEOUT 20000         /* us, as the RTC path uses */

/*
 *	Bus 1's timeout is the program's - MMBasic's I2C2 OPEN takes it -
 *	and these two bound it.
 *
 *	MMBasic's floor is 100ms and this keeps it, because the number is
 *	documented and programs are written to it.  Its 0 ("no timeout")
 *	becomes the CAP instead: a non-preemptive kernel that waits
 *	forever on a bus takes the console and the display down with the
 *	caller, so "forever" is not a thing this machine can offer.  Five
 *	seconds is far longer than any transfer that is going to succeed -
 *	a 255-byte transfer at 100kHz is about 23ms - so the only programs
 *	that can tell the difference are the ones already talking to a bus
 *	that is not answering.
 */
#define I2C_MIN_TIMEOUT_MS  100
#define I2C_MAX_TIMEOUT_MS  5000

/*
 *	Bus 1 - MMBasic's I2C2 - has no pins until a program says which,
 *	which is why it needs opening and the fixed bus does not.  This
 *	says which pins, how fast, and whether it is usable at all; WHO has
 *	it is the pin lock's job, and the two are not interchangeable.
 *
 *	The first version had only these statics, and an open that found
 *	i2c1_open set returned EBUSY.  Nothing ever cleared it: a program
 *	that exited - or was killed, or hit an error before its I2C2 CLOSE
 *	- left the flag set, and every later open on the machine failed
 *	until the next reboot.  The whole point of the pin lock is that a
 *	process cannot leak a resource by dying, so bus 1 is claimed
 *	through it now and released the same way as everything else.
 */
static uint8_t i2c1_open;
static uint8_t i2c1_sda, i2c1_scl;
static uint32_t i2c1_timeout_us = I2C_MAX_TIMEOUT_MS * 1000;
/*	Kept because the recovery path re-initialises the controller, and it
 *	used to do that at a hard-coded 400kHz: a bus opened at 100 came
 *	back at 400 after its first failed transfer, and a device that could
 *	not take 400 then failed for a new reason. */
static uint32_t i2c1_khz = 400;
/*	Set by a transfer that ended without a STOP.  The bus is then MID
 *	TRANSACTION and the addressed device may be holding SDA down, so
 *	the next thing to touch this controller has to finish what the last
 *	one started - see i2c1_unwedge. */
static uint8_t i2c1_held;

/*	SDA and SCL are not interchangeable and not free: the RP2350 muxes
 *	I2C1's SDA only onto pins where (pin & 3) == 2 and its SCL only
 *	where (pin & 3) == 3.  On the PC3's header that is GP38/GP39 and
 *	GP42/GP43.  Refusing the wrong pair here beats a silent bus that
 *	never answers.
 *
 *	The controller AND both pins are claimed, so a second program
 *	asking for the bus gets an honest EBUSY, and so does one that has
 *	GP38 as a plain output; re-opening what this process already holds
 *	succeeds, because pinlock_claim treats "already yours" as success
 *	and a runtime must not have to remember what it has claimed. */
/*
 *	Finish a transaction the caller did not.  I2CF_HOLD deliberately
 *	ends a transfer without a STOP so the next one can be a repeated
 *	START; the bus is then still owned, and if the program errors out
 *	or is killed between the two halves the addressed device can be
 *	left holding SDA down.  Nothing else on this board will free it.
 *
 *	This is ds3231_bus_recover's routine on whichever pins bus 1 was
 *	given - the same nine clocks and the same manufactured STOP, since
 *	that one is proven and a second idiom for the same job would be a
 *	second thing to get wrong.  Silent when the bus is already idle,
 *	which is the normal case: a hold followed by its read needs no
 *	recovery at all.
 */
static void i2c1_unwedge(void)
{
    uint8_t sda = i2c1_sda, scl = i2c1_scl;
    int i;

    i2c1_held = 0;
    gpio_init(sda);
    gpio_init(scl);
    gpio_set_input_enabled(sda, true);
    gpio_set_input_enabled(scl, true);
    gpio_pull_up(sda);
    gpio_pull_up(scl);
    busy_wait_us_32(10);

    if (!((gpio_get_all64() >> sda) & 1)) {
        kputs("i2c1: SDA held low, clocking bus free\n");
        for (i = 0; i < 9; i++) {
            gpio_put(scl, 0);
            gpio_set_dir(scl, true);
            busy_wait_us_32(50);
            gpio_set_dir(scl, false);
            busy_wait_us_32(50);
            if ((gpio_get_all64() >> sda) & 1)
                break;
        }
        /* STOP: SDA low -> high while SCL is high */
        gpio_put(sda, 0);
        gpio_set_dir(sda, true);
        busy_wait_us_32(50);
        gpio_set_dir(sda, false);
        busy_wait_us_32(50);
    }

    gpio_set_function(sda, GPIO_FUNC_I2C);
    gpio_set_function(scl, GPIO_FUNC_I2C);
    gpio_pull_up(sda);
    gpio_pull_up(scl);
}

int plt_i2c_open(uint8_t bus, uint8_t sda, uint8_t scl, uint32_t khz,
                 uint16_t timeout_ms)
{
    int mine, r;
    uint8_t got_ctrl = 0, got_sda = 0;

    if (bus != 1)
        return -ENODEV;
    if ((sda & 3) != 2 || (scl & 3) != 3 || scl != sda + 1)
        return -EINVAL;
    if (khz != 100 && khz != 400 && khz != 1000)
        return -EINVAL;
    /*	MMBasic's own test, kept to the digit: 0, or 100 and up
	(i2cEnable in I2C.c).  0 is the cap here rather than "forever";
	the struct comment in pico_ioctl.h says why. */
    if (timeout_ms != 0 && timeout_ms < I2C_MIN_TIMEOUT_MS)
        return -EINVAL;

    /* Undo only what THIS call took.  A re-open by a process that
       already holds the controller must not hand it back because the
       new pin pair happened to be someone else's. */
    mine = udata.u_ptab->p_pid;
    got_ctrl = (pinlock_owner(PLK_I2C, 1) != mine);
    got_sda = (pinlock_owner(PLK_PIN, sda) != mine);

    r = pinlock_claim(udata.u_ptab, PLK_I2C, 1);
    if (r)
        return r;
    r = pinlock_claim(udata.u_ptab, PLK_PIN, sda);
    if (r)
        goto undo_ctrl;
    r = pinlock_claim(udata.u_ptab, PLK_PIN, scl);
    if (r)
        goto undo_sda;

    /* Moved to a different pair: the old one goes back, or the header
       keeps two pins pulled up with a controller no longer on them. */
    if (i2c1_open && (i2c1_sda != sda || i2c1_scl != scl)) {
        pinlock_free(udata.u_ptab, PLK_PIN, i2c1_sda);
        pinlock_free(udata.u_ptab, PLK_PIN, i2c1_scl);
    }

    i2c_init(i2c1, (uint)khz * 1000);
    gpio_set_function(sda, GPIO_FUNC_I2C);
    gpio_set_function(scl, GPIO_FUNC_I2C);
    gpio_pull_up(sda);
    gpio_pull_up(scl);
    i2c1_sda = sda;
    i2c1_scl = scl;
    i2c1_khz = khz;
    i2c1_timeout_us = (timeout_ms ? (uint32_t)timeout_ms
                                  : I2C_MAX_TIMEOUT_MS) * 1000UL;
    i2c1_held = 0;
    i2c1_open = 1;
    return 0;

undo_sda:
    if (got_sda)
        pinlock_free(udata.u_ptab, PLK_PIN, sda);
undo_ctrl:
    if (got_ctrl)
        pinlock_free(udata.u_ptab, PLK_I2C, 1);
    return r;
}

/*	Shut the controller.  Called by the pin lock when the owner dies -
 *	a controller left enabled on pins that have been reset would drive
 *	nothing and answer nothing, which is worse than being shut - and
 *	that is the ONLY caller that may use it, because it deliberately
 *	does not touch the locks: pinlock_free calls it. */
void plt_i2c_close(uint8_t bus)
{
    if (bus != 1 || !i2c1_open)
        return;
    /*	A hold that was never finished is finished here, BEFORE the
	block goes away: deinitialising the controller releases the
	pins from it but does not make the device let go of SDA, and
	the pin lock resets them to inputs straight afterwards.  This is
	the last moment anything can drive the bus. */
    if (i2c1_held)
        i2c1_unwedge();
    i2c_deinit(i2c1);
    i2c1_open = 0;
}

/*	I2C2 CLOSE: give the whole thing back, which is what the ioctl
 *	wants and what plt_i2c_close on its own does not do.  Freeing the
 *	controller lock is what shuts the hardware, so the pins are read
 *	out first.  A process that does not own it gets silence rather
 *	than an error - MMBasic's CLOSE on a bus that is not open is not
 *	a fault - but it does not shut anyone else's bus either. */
void plt_i2c_release(uint8_t bus)
{
    uint8_t sda = i2c1_sda, scl = i2c1_scl;

    if (bus != 1)
        return;
    if (pinlock_free(udata.u_ptab, PLK_I2C, 1))
        return;
    pinlock_free(udata.u_ptab, PLK_PIN, sda);
    pinlock_free(udata.u_ptab, PLK_PIN, scl);
}

/*
 *	flags is MMBasic's option word: I2CF_HOLD ends the transfer without
 *	a STOP so the next is a repeated START, which is the SDK's nostop
 *	argument and exactly what MMBasic passes it.  plt_i2c_msg below is
 *	the flagless entry upstream's /dev/i2c uses, so a portable program
 *	behaves as it always did.
 */
int plt_i2c_msg_flags(struct i2c_msg *msg, uint8_t *kbuf, uint8_t flags)
{
    unsigned addr = (msg->addr >> 1) & 0x7F;
    int read = msg->addr & 1;
    int nostop = (flags & I2CF_HOLD) ? 1 : 0;
    uint32_t tmo = XFER_TIMEOUT;
    i2c_inst_t *port;
    int r;

    if (msg->bus == 1) {
        if (!i2c1_open)
            return -ENODEV;         /* no pins assigned: I2C2 not OPEN */
        port = i2c1;
        tmo = i2c1_timeout_us;      /* the program's, from OPEN */
        /*	No recovery needed HERE even if the last transfer held the
		bus: whatever this one is, the controller issues a START,
		and a START while holding is a repeated START, which is
		legal to any address.  A hold only strands the bus when
		the program stops issuing transfers altogether - an error
		or a death - and plt_i2c_close is where that is caught. */
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

        /* A probe never holds: it is asking whether anything is there,
           and a scan that left the bus held on every address it tried
           would strand it on the first device that did not answer. */
        r = i2c_read_timeout_us(port, addr, &junk, 1, false, tmo);
        if (msg->bus == 0)
            i2c0_user_busy = 0;
        return (r == 1) ? 0 : -EIO;
    }

    if (read)
        r = i2c_read_timeout_us(port, addr, kbuf, msg->len, nostop, tmo);
    else
        r = i2c_write_timeout_us(port, addr, kbuf, msg->len, nostop, tmo);
    if (msg->bus == 0)
        i2c0_user_busy = 0;

    if (r == (int)msg->len) {
        /* Remember an outstanding hold, so a program that stops here
           does not leave the bus stranded - see plt_i2c_close. */
        if (msg->bus == 1)
            i2c1_held = (uint8_t)nostop;
        return 0;
    }

    /* The controller can be left wedged by a device that died mid
       transaction - the RTC path has always had to cope with this and
       the recovery is shared.  Do it here rather than leaving the next
       caller, or the hourly RTC poll, to find a dead bus.
       Bus 1 used to get only the controller reset, because it had no
       routine that knew its pins; i2c1_unwedge is that routine, and a
       failed transfer is exactly when it is wanted - the SDK reports a
       timeout for a device that stopped mid-byte still holding SDA, and
       resetting the block alone does not make it let go. */
    if (r < 0) {
        if (msg->bus == 0) {
            ds3231_bus_recover();
            i2c_init(USER_I2C, 400 * 1000);
        } else {
            i2c1_unwedge();
            i2c_init(i2c1, i2c1_khz * 1000);
        }
    }
    /* MMBasic tells these two apart - mmI2Cvalue is 1 for
       PICO_ERROR_GENERIC and 2 for PICO_ERROR_TIMEOUT - and a program
       wants to as well: "nothing at that address" is a wiring or
       address mistake, "timed out" is a device that answered and then
       stopped, or a bus being held down.  Collapsing both into EIO made
       every I2C fault look like the first one. */
    return (r == PICO_ERROR_TIMEOUT) ? -ETIMEDOUT : -EIO;
}

/*	Upstream's entry: /dev/i2c's I2C_MSG, which has no flags word and
 *	never held the bus.  Unchanged behaviour for a portable program. */
int plt_i2c_msg(struct i2c_msg *msg, uint8_t *kbuf)
{
    return plt_i2c_msg_flags(msg, kbuf, 0);
}

#endif
