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

static int ds3231_read_regs(uint8_t reg, uint8_t *buf, unsigned int n)
{
    if (i2c_write_timeout_us(DS3231_I2C, DS3231_ADDR, &reg, 1, true,
                             DS3231_TIMEOUT_US) != 1)
        return -1;
    if (i2c_read_timeout_us(DS3231_I2C, DS3231_ADDR, buf, n, false,
                            DS3231_TIMEOUT_US) != (int)n)
        return -1;
    return 0;
}

static int ds3231_write_regs(uint8_t reg, const uint8_t *buf, unsigned int n)
{
    uint8_t tmp[8];

    if (n > sizeof(tmp) - 1)
        return -1;
    tmp[0] = reg;
    memcpy(tmp + 1, buf, n);
    if (i2c_write_timeout_us(DS3231_I2C, DS3231_ADDR, tmp, n + 1, false,
                             DS3231_TIMEOUT_US) != (int)(n + 1))
        return -1;
    return 0;
}

uint_fast8_t plt_rtc_secs(void)
{
    uint8_t s;

    if (ds3231_read_regs(REG_TIME, &s, 1))
        return 255;
    return frombcd(s & 0x7F);
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

    gpio_set_function(DS3231_SDA, GPIO_FUNC_I2C);
    gpio_set_function(DS3231_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(DS3231_SDA);
    gpio_pull_up(DS3231_SCL);
    i2c_init(DS3231_I2C, 400 * 1000);

    if (ds3231_read_regs(REG_STATUS, &st, 1)) {
        kputs("DS3231 RTC: not responding\n");
        return;
    }
    if (st & 0x80)
        kputs("DS3231 RTC: oscillator was stopped, time needs setting (setdate -w)\n");
    inittod();
}
