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
#define REG_STATUS 0x0F /* bit 7 = oscillator stop flag */

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

/* Standard I2C bus recovery. The DS3231 is battery backed and never
 * resets: a transaction interrupted mid-bit (reboot at the wrong
 * moment) can leave the chip driving SDA low forever - across power
 * cycles - and every later transaction then times out. Clock the bus
 * with up to 9 SCL pulses until the slave releases SDA, then send a
 * STOP, then hand the pins back to the I2C block. */
static void ds3231_bus_recover(void)
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

/* Every transaction here now runs in PROCESS context - see
 * plt_rtc_secs() below - and the kernel is non-preemptive, so a syscall
 * that does not sleep is already atomic against every other process and
 * none of these sleep.  Interrupts stay ON: the reason they were held
 * was the IRQ-context poll that no longer exists, the timeout is 20ms
 * against a ~300us transaction so a tick landing mid-transfer cannot
 * cause a timeout, and holding them off for 300us (x3 retries, plus bus
 * recovery) hurt a 200Hz tick and the console for nothing.
 *
 * The retries stay.  They are for a wedged bus, which is a real
 * condition on a connector users can plug things into. */
static int ds3231_read_regs(uint8_t reg, uint8_t *buf, unsigned int n)
{
    int tries, r1, r2;

    for (tries = 0; tries < 3; tries++) {
        if (tries) {    /* previous try failed: unwedge bus + controller */
            ds3231_bus_recover();
            i2c_init(DS3231_I2C, 400 * 1000);
        }
        r1 = i2c_write_timeout_us(DS3231_I2C, DS3231_ADDR, &reg, 1, true,
                                  DS3231_TIMEOUT_US);
        r2 = (r1 == 1) ?
             i2c_read_timeout_us(DS3231_I2C, DS3231_ADDR, buf, n, false,
                                 DS3231_TIMEOUT_US) : -1;
        if (r1 == 1 && r2 == (int)n)
            return 0;
    }
    /* Always reported now.  The one caller that had to be silent was
       the hourly interrupt-context poll, and it is gone; everything
       left is a user asking for the time, where a failure is news. */
    kprintf("ds3231: read reg %d fail (w=%d r=%d)\n", reg, r1, r2);
    return -1;
}

static int ds3231_write_regs(uint8_t reg, const uint8_t *buf, unsigned int n)
{
    uint8_t tmp[8];
    int tries, r1;

    if (n > sizeof(tmp) - 1)
        return -1;
    tmp[0] = reg;
    memcpy(tmp + 1, buf, n);
    for (tries = 0; tries < 3; tries++) {
        if (tries) {    /* previous try failed: unwedge bus + controller */
            ds3231_bus_recover();
            i2c_init(DS3231_I2C, 400 * 1000);
        }
        r1 = i2c_write_timeout_us(DS3231_I2C, DS3231_ADDR, tmp, n + 1, false,
                                  DS3231_TIMEOUT_US);
        if (r1 == (int)(n + 1))
            return 0;
    }
    kprintf("ds3231: write reg %d fail (%d)\n", reg, r1);
    return -1;
}

/*
 *	NOTHING here touches the bus any more.
 *
 *	updatetod() calls this from timer_interrupt() to drift-correct the
 *	tick-driven clock, and it used to answer with a real I2C read
 *	about hourly.  That was a whole transaction in INTERRUPT context,
 *	on the controller the QWIIC connector is wired to - so the moment
 *	userland can drive that bus (PC3-IO-PLAN.md), an hourly interrupt
 *	could land in the middle of a user transaction and spoil both.
 *	Arbitration cannot fix it: interrupt context can neither take a
 *	sleeping lock nor wait for one that is held.
 *
 *	So the answer is deletion, not arbitration.  255 means "no data,
 *	skip", which updatetod already handles - it is what a machine with
 *	no RTC returns - and the kernel's drift correction goes with it.
 *
 *	What replaces it: /etc/rc reads the chip once at boot (setdate),
 *	which is where Linux does it too, and /etc/rtcsync resyncs on a
 *	timer from userland, in process context, where a lock is possible
 *	and a wedged bus is survivable.  The cost of the swap is that the
 *	correction is now a step rather than a continuous slide, and that
 *	the clock is crystal-accurate between steps - a few seconds a day.
 *
 *	This also retires a subtlety worth recording: the old sync period
 *	had to be a multiple of 3840s, because updatetod's expected-seconds
 *	counter wraps mod 256 while the chip's wraps mod 60, and lcm(256,60)
 *	kept them congruent so the computed slide was pure drift.  Nothing
 *	samples the chip on a schedule now, so nothing has to satisfy it.
 */
uint_fast8_t plt_rtc_secs(void)
{
    return 255;
}

int plt_rtc_read(void)
{
    struct cmos_rtc_wire cmos;
    register uint8_t *p = cmos.bytes;
    uint8_t r[7];
    uint16_t year;
    uint16_t len = sizeof(struct cmos_rtc_wire);

    /* No chip: say so at once.  Without this every read costs three
       tries of a 20ms timeout plus two bus recoveries before failing,
       and /etc/rc runs setdate on a machine that may not have one. */
    if (!rtc_present) {
        udata.u_error = EIO;
        return -1;
    }

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

    if (!rtc_present) {
        udata.u_error = EIO;
        return -1;
    }
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
    inittod();
}
