#ifndef PC3_DS3231_H
#define PC3_DS3231_H

void ds3231_init(void);

/*
 *	I2C0 is shared: the DS3231 is on it at 0x68 and so is the QWIIC
 *	socket, which is where a user plugs a sensor.  ds3231.c owns the
 *	controller - it initialises it and knows how to unwedge it - and
 *	these are what the user-facing driver (i2cuser.c) borrows.
 */
void ds3231_bus_recover(void);

/*
 *	Set while a USERLAND transaction is in flight on I2C0.
 *
 *	plt_rtc_secs() runs in interrupt context and cannot wait for
 *	anything, so it does not try: it reads this and skips its hourly
 *	sample if userland has the bus.  The kernel is non-preemptive, so
 *	a user transaction completes inside one syscall and this is set
 *	for about 300us at a time - against a poll that comes once an
 *	hour.  Missing one costs an hour of crystal drift, which is
 *	milliseconds, and that is why declining is honest here where
 *	waiting would be impossible.
 */
extern uint8_t i2c0_user_busy;

/*
 *	One DS3231 register for userland - MMBasic's RTC GETREG/SETREG,
 *	and the only way to arm an alarm, because MMBasic has no alarm
 *	command either: you write the alarm registers and the control
 *	register yourself.  0 on success.
 *
 *	Not through /dev/i2c, which refuses 0x68 outright: this is the
 *	system clock, so the access goes through the kernel's own
 *	retry-and-unwedge path.  A write cannot set EOSC - stopping a
 *	battery-backed oscillator outlives the power cycle.
 */
int ds3231_user_reg(uint8_t reg, uint8_t *val, int write);

#endif
