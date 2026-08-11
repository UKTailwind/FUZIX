/*
 * DS3231 real-time clock on the Pico Computer 3 (I2C0, GP20=SDA/GP21=SCL,
 * address 0x68). Battery backed, so the system gets valid wall-clock time
 * at boot: setdate (run from rc) reads /dev/rtc and calls stime() without
 * prompting; `setdate -w` writes system time back to the chip.
 *
 * /dev/rtc traffic uses struct cmos_rtc in CMOS_RTC_DEC form: year as two
 * little-endian bytes (AD), then month (0-11, tm_mon convention), day of
 * month, hour, minute, second; writes carry tm_wday in the last byte.
 */

#include <kernel.h>
#include <kdata.h>
#include <printf.h>
#include <rtc.h>
#include "picosdk.h"
#include <hardware/i2c.h>
#include "config.h"
#include "ds3231.h"

#define DS3231_I2C        i2c0
#define DS3231_SDA        20
#define DS3231_SCL        21
#define DS3231_ADDR       0x68
#define DS3231_TIMEOUT_US 20000

#define REG_TIME   0x00 /* sec min hour wday mday month year, BCD */
#define REG_CONTROL 0x0E /* bit 7 = EOSC (stop), 2 = INTCN, 0 = A1IE */
#define REG_STATUS 0x0F /* bit 7 = OSF, 3 = EN32kHz (BOARD DETECTION), 0 = A1F */

/* What userland's struct cmos_rtc actually looks like on ARM: its time_t
 * is int64_t, so the data union is 8-aligned (offset 8, total 16 bytes).
 * The kernel's time_t is a pair of 32-bit words, giving offset 4 / size
 * 12 - so the kernel struct cannot be used on the wire. Byte-aligned
 * 8-bit targets never see the difference; this is a latent ABI issue for
 * every aligned target. */
struct cmos_rtc_wire {
    uint8_t type;
    uint8_t pad[7];
    uint8_t bytes[8];
};

static uint8_t frombcd(uint8_t v)
{
    return (v >> 4) * 10 + (v & 0x0F);
}

static uint8_t tobcd(uint8_t v)
{
    return ((v / 10) << 4) | (v % 10);
}

static uint8_t rtc_present;    /* found at init */
static uint8_t rtc_quiet;      /* suppress diagnostics (IRQ-context poll) */

/* Standard I2C bus recovery. The DS3231 is battery backed and never
 * resets: a transaction interrupted mid-bit (reboot at the wrong
 * moment) can leave the chip driving SDA low forever - across power
 * cycles - and every later transaction then times out. Clock the bus
 * with up to 9 SCL pulses until the slave releases SDA, then send a
 * STOP, then hand the pins back to the I2C block. */
uint8_t i2c0_user_busy;         /* userland holds the bus - see ds3231.h */

void ds3231_bus_recover(void)
{
    int i;

    gpio_init(DS3231_SDA);              /* SIO, input, pulled up */
    gpio_init(DS3231_SCL);
    gpio_set_input_enabled(DS3231_SDA, true);
    gpio_set_input_enabled(DS3231_SCL, true);
    gpio_pull_up(DS3231_SDA);
    gpio_pull_up(DS3231_SCL);
    busy_wait_us_32(10);

    if (!((gpio_get_all64() >> DS3231_SDA) & 1)) {
        kputs("ds3231: SDA held low, clocking bus free\n");
        for (i = 0; i < 9; i++) {
            /* drive SCL low, release high - open-drain style */
            gpio_put(DS3231_SCL, 0);
            gpio_set_dir(DS3231_SCL, true);
            busy_wait_us_32(50);
            gpio_set_dir(DS3231_SCL, false);
            busy_wait_us_32(50);
            if ((gpio_get_all64() >> DS3231_SDA) & 1)
                break;
        }
        /* STOP: SDA low -> high while SCL high */
        gpio_put(DS3231_SDA, 0);
        gpio_set_dir(DS3231_SDA, true);
        busy_wait_us_32(50);
        gpio_set_dir(DS3231_SDA, false);
        busy_wait_us_32(50);
    }

    gpio_set_function(DS3231_SDA, GPIO_FUNC_I2C);
    gpio_set_function(DS3231_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(DS3231_SDA);
    gpio_pull_up(DS3231_SCL);
}

/* All bus transactions run with interrupts off and retry: the timer
 * interrupt polls plt_rtc_secs() every few seconds (CONFIG_RTC_INTERVAL),
 * so a process-context transaction on the same controller could
 * otherwise be interleaved with an IRQ-context one and both aborted.
 * A transaction is ~300us at 400kHz - acceptable with interrupts held. */
static int ds3231_read_regs(uint8_t reg, uint8_t *buf, unsigned int n)
{
    int tries, r1, r2;
    irqflags_t irq;

    for (tries = 0; tries < 3; tries++) {
        if (tries) {    /* previous try failed: unwedge bus + controller */
            ds3231_bus_recover();
            i2c_init(DS3231_I2C, 400 * 1000);
        }
        irq = di();
        r1 = i2c_write_timeout_us(DS3231_I2C, DS3231_ADDR, &reg, 1, true,
                                  DS3231_TIMEOUT_US);
        r2 = (r1 == 1) ?
             i2c_read_timeout_us(DS3231_I2C, DS3231_ADDR, buf, n, false,
                                 DS3231_TIMEOUT_US) : -1;
        irqrestore(irq);
        if (r1 == 1 && r2 == (int)n)
            return 0;
    }
    if (!rtc_quiet)
        kprintf("ds3231: read reg %d fail (w=%d r=%d)\n", reg, r1, r2);
    return -1;
}

static int ds3231_write_regs(uint8_t reg, const uint8_t *buf, unsigned int n)
{
    uint8_t tmp[8];
    int tries, r1;
    irqflags_t irq;

    if (n > sizeof(tmp) - 1)
        return -1;
    tmp[0] = reg;
    memcpy(tmp + 1, buf, n);
    for (tries = 0; tries < 3; tries++) {
        if (tries) {    /* previous try failed: unwedge bus + controller */
            ds3231_bus_recover();
            i2c_init(DS3231_I2C, 400 * 1000);
        }
        irq = di();
        r1 = i2c_write_timeout_us(DS3231_I2C, DS3231_ADDR, tmp, n + 1, false,
                                  DS3231_TIMEOUT_US);
        irqrestore(irq);
        if (r1 == (int)(n + 1))
            return 0;
    }
    kprintf("ds3231: write reg %d fail (%d)\n", reg, r1);
    return -1;
}

/* Called from the timer interrupt every CONFIG_RTC_INTERVAL deciseconds
 * (5s) to drift-correct the tick-driven system clock. System time runs
 * from the crystal-derived tick; touching the I2C bus that often buys
 * nothing, so resync about hourly as MMBasic does (255 = no data, skip).
 * The interval must be a multiple of 3840s: updatetod's expected-seconds
 * counter wraps mod 256 while the chip's wraps mod 60, and lcm(256,60)
 * keeps the two congruent so the computed slide is pure drift. */
#define RTC_SYNC_SECS  3840
#define RTC_SYNC_CALLS (RTC_SYNC_SECS / (CONFIG_RTC_INTERVAL / 10))

uint_fast8_t plt_rtc_secs(void)
{
    static uint16_t calls;
    uint8_t s;
    uint8_t r;

    if (!rtc_present)
        return 255;
    /*
     * Userland has the bus.  Skip - do NOT wait, because there is
     * nothing interrupt context can wait on and a half-finished user
     * transaction is exactly what must not be walked into.  255 is
     * "no data", which updatetod() already handles, and the cost is
     * one hour of crystal drift.  See ds3231.h and PC3-IO-PLAN.md.
     */
    if (i2c0_user_busy)
        return 255;
    if (++calls < RTC_SYNC_CALLS)
        return 255;
    calls = 0;
    rtc_quiet = 1;
    r = ds3231_read_regs(REG_TIME, &s, 1) ? 255 : frombcd(s & 0x7F);
    rtc_quiet = 0;
    return r;
}

int plt_rtc_read(void)
{
    struct cmos_rtc_wire cmos;
    register uint8_t *p = cmos.bytes;
    uint8_t r[7];
    uint16_t year;
    uint16_t len = sizeof(struct cmos_rtc_wire);

    memset(&cmos, 0, sizeof(cmos));
    if (udata.u_count < len)
        len = udata.u_count;

    /* Note: reads succeed even with the oscillator-stop flag set (the
     * time may then be stale, and boot warns) - setdate -w must be able
     * to read the wire type before it can write and clear the flag. */
    if (ds3231_read_regs(REG_TIME, r, 7)) {
        udata.u_error = EIO;
        return -1;
    }

    year = 2000 + frombcd(r[6]);
    *p++ = year & 0xFF;
    *p++ = year >> 8;
    *p++ = frombcd(r[5] & 0x1F) - 1;    /* month 1-12 -> tm_mon 0-11 */
    *p++ = frombcd(r[4] & 0x3F);        /* day of month */
    *p++ = frombcd(r[2] & 0x3F);        /* hour (24h mode) */
    *p++ = frombcd(r[1] & 0x7F);        /* minute */
    *p = frombcd(r[0] & 0x7F);          /* second */
    cmos.type = CMOS_RTC_DEC;

    if (uput(&cmos, udata.u_base, len) == -1)
        return -1;
    return len;
}

int plt_rtc_write(void)
{
    struct cmos_rtc_wire cmos;
    register uint8_t *p = cmos.bytes;
    uint8_t r[7];
    uint8_t st;
    uint16_t year;

    if (udata.u_count != sizeof(struct cmos_rtc_wire)) {
        udata.u_error = EINVAL;
        return -1;
    }
    if (uget(udata.u_base, &cmos, sizeof(struct cmos_rtc_wire)) == -1)
        return -1;
    if (cmos.type != CMOS_RTC_DEC) {
        udata.u_error = EINVAL;
        return -1;
    }

    year = p[0] | (p[1] << 8);
    r[0] = tobcd(p[6]);                 /* second */
    r[1] = tobcd(p[5]);                 /* minute */
    r[2] = tobcd(p[4]);                 /* hour, bit 6 clear = 24h mode */
    r[3] = p[7] + 1;                    /* tm_wday 0-6 -> 1-7 */
    r[4] = tobcd(p[3]);                 /* day of month */
    r[5] = tobcd(p[2] + 1);             /* tm_mon 0-11 -> 1-12 */
    r[6] = tobcd(year % 100);

    if (ds3231_write_regs(REG_TIME, r, 7)) {
        udata.u_error = EIO;
        return -1;
    }

    /* Clear the oscillator-stop flag so the time reads as valid */
    if (ds3231_read_regs(REG_STATUS, &st, 1) == 0 && (st & 0x80)) {
        st &= 0x7F;
        ds3231_write_regs(REG_STATUS, &st, 1);
    }
    return sizeof(struct cmos_rtc_wire);
}

void ds3231_init(void)
{
    uint8_t st;

    ds3231_bus_recover();
    i2c_init(DS3231_I2C, 400 * 1000);

    if (ds3231_read_regs(REG_STATUS, &st, 1)) {
        kputs("DS3231 RTC: not responding\n");
        return;
    }
    rtc_present = 1;
    if (st & 0x80)
        kputs("DS3231 RTC: oscillator was stopped, time needs setting (setdate -w)\n");

    /*
     * TURN THE 32 kHz OUTPUT ON, EVERY BOOT.
     *
     * board_detect() runs immediately after this and identifies the
     * machine by watching that square wave on GP27.  If EN32kHz is
     * clear the pin is quiet, a PC3 is taken for a PC2, and the SD card
     * is then probed on the wrong MISO - "SD drive 0: no card found",
     * on a machine whose card is perfectly good.
     *
     * The bit is in a battery-backed register, so whatever cleared it
     * outlives the power cycle, and the machine cannot be talked out of
     * it afterwards because it no longer boots.  Anything can have
     * cleared it: an older kernel, another firmware, or a BASIC program
     * clearing the alarm flag with a careless write of the whole
     * register - which is exactly how a board here was lost.
     *
     * So this does not ask why; it just puts it back, before the answer
     * matters.  Benign on a PC2, where GP27 is an ordinary pin and
     * nothing reads the output.  Doing it HERE is what makes it work at
     * all: the write goes over I2C0 on GP20/21, which has nothing to do
     * with the GP27 line detection is about to read.
     *
     * Writing the value just read, with bit 3 forced, is safe: the
     * flags in this register are write-0-to-clear, so putting each one
     * back as it was leaves it as it was.
     */
    if (!(st & 0x08)) {
        uint8_t v = (uint8_t)(st | 0x08);

        kputs("DS3231 RTC: 32kHz output was off, re-enabling for board detect\n");
        if (ds3231_write_regs(REG_STATUS, &v, 1) == 0) {
            /* let the output start before board_detect() counts edges;
               a 32 kHz cycle is 30us and it wants four of them */
            busy_wait_us_32(200);
        }
    }
    inittod();
}

/*
 *	One register, read or written, for userland.
 *
 *	This is MMBasic's RTC GETREG / RTC SETREG (I2C.c cmd_rtc), and it
 *	is how an ALARM is armed there: write the alarm registers 0x07 to
 *	0x0A, then set INTCN and A1IE in the control register 0x0E, and
 *	the DS3231 pulls its INT line - GP32 here - low when the time
 *	matches.  There is no separate alarm command to copy because
 *	MMBasic does not have one.
 *
 *	It has to be a kernel call rather than /dev/i2c: that driver
 *	refuses address 0x68 outright, because the chip is the system
 *	clock and a program writing its registers blind could stop it.
 *	Here the writes go through the same retry-and-unwedge path as the
 *	kernel's own, with one refusal kept.
 *
 *	EOSC - bit 7 of the control register - is masked out of a write.
 *	Setting it stops the oscillator, and on a part with a battery that
 *	is not a mistake that ends at the next power cycle: the clock
 *	stays stopped until something clears it, and the machine boots
 *	with no idea what time it is.  Everything else, including the time
 *	itself and the whole alarm block, is the program's to change - as
 *	it is on a PicoMite.
 */
int ds3231_user_reg(uint8_t reg, uint8_t *val, int write)
{
    int r;

    if (!rtc_present)
        return -1;
    i2c0_user_busy = 1;
    if (write) {
        uint8_t v = *val;

        if (reg == REG_CONTROL)
            v &= (uint8_t)~0x80;        /* never stop the oscillator */
        /*
         * And never turn off the 32kHz output, for the same reason and
         * with a worse result.  Bit 3 of the status register is
         * EN32kHz, and that square wave on GP27 is HOW THIS KERNEL
         * KNOWS WHICH MACHINE IT IS: board_is_pc2() reads it.  Clear it
         * and the next boot decides it is a PC2, where GP32 is the SD
         * card's MISO - so the card is looked for on the wrong pin and
         * the machine comes up saying "no card found".
         *
         * It is battery-backed, so it outlives the power cycle, and the
         * program that did it cannot undo it because the machine no
         * longer boots.  A BASIC program clearing the alarm flag with
         * the obvious "RTC SETREG &H0F, 0" bricked a board exactly that
         * way; MMBasic on the same board then would not recognise a PC3
         * either.  The flags in this register are write-0-to-clear, so
         * forcing this one bit to 1 costs a program nothing it wanted.
         */
        if (reg == REG_STATUS)
            v |= 0x08;                  /* EN32kHz: board detection */
        r = ds3231_write_regs(reg, &v, 1);
    } else {
        r = ds3231_read_regs(reg, val, 1);
    }
    i2c0_user_busy = 0;
    return r;
}
