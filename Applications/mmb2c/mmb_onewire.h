#ifndef MMB_ONEWIRE_H
#define MMB_ONEWIRE_H
/*
 *	One-wire, and the DS18B20 that is nearly always on the end of it.
 *
 *	    ONEWIRE RESET pin
 *	    ONEWIRE WRITE pin, flag, count, <data>
 *	    ONEWIRE READ  pin, flag, count, <destination>
 *	    TEMPR START pin [, precision [, timeout]]
 *	    t = TEMPR(pin [, timeout])
 *
 *	<data> and <destination> are the forms every bus here shares -
 *	mmb_comms.h - which is the whole reason one-wire waited for that
 *	file rather than growing a third copy of them.  MMBasic's owWrite
 *	and owRead call GetCommsTxData and GetCommsRxDest at argument 6,
 *	exactly as I2C does at 6 and SPI at 2.
 *
 *	The flag is MMBasic's, bit for bit (Onewire.c:308):
 *	    1  reset first        4  send only the first bit of each byte
 *	    2  reset afterwards   8  strong pull-up when finished
 *
 *	THE TIMING IS BIT-BANGED IN USERLAND, which is possible here only
 *	because pins are register writes and pc3_us64 is three loads.  A
 *	one-wire slot is 60 us and the sample point inside it is 10 us
 *	wide, so a syscall per edge - 1.488 us each - would not fit twice
 *	over.  The numbers below are Dallas's and MMBasic's, copied rather
 *	than recomputed.
 *
 *	What this CANNOT copy is MMBasic's disable_interrupts_pico()
 *	around a byte.  A userland program cannot mask interrupts, and
 *	should not be able to.  The kernel is non-preemptive, so nothing
 *	takes the processor away between two of these instructions, but a
 *	kernel timer interrupt can still land mid-slot and stretch it.
 *	One-wire tolerates a slot being LONG rather than short - the
 *	device samples early - so a stretched write is read correctly and
 *	the failure mode is a late read sample, which is why the read path
 *	samples as early as Dallas allows.  Say so rather than pretend:
 *	a busy machine can corrupt a transfer, and a program that cares
 *	should check the CRC the device provides.
 */

#include "mmb_runtime.h"
#include "mmb_gpio.h"
#include "mmb_comms.h"

#ifndef MMG_FN
#if defined(MM_FCC) || defined(MM_PC3)
#define MMG_FN static
#else
#define MMG_FN static __inline__ __attribute__((unused))
#endif
#endif

/*	The clock: the board's own three loads, the runtime's elsewhere so
 *	the gates can run this code with a real clock behind it. */
#if defined(MM_PC3) || defined(__FUZIX__)
#define MMOW_US()	pc3_us64()
#else
#define MMOW_US()	((long long)mm_us())
#endif

/*	Spin for n microseconds.  A sleep is out of the question at this
 *	scale - the whole slot is 60 us and a syscall is 1.5. */
MMG_FN void mmow_us(long long n)
{
	long long end = MMOW_US() + n;

	while (MMOW_US() < end)
		;
}

/*	Open drain, as one-wire requires: the bus is pulled up and every
 *	device only ever pulls it DOWN.  Driving it high would fight
 *	another device doing the same, so "release" means become an input
 *	and let the resistor do it. */
MMG_FN void mmow_low(int pin)
{
	pc3_pin_out(pin);
	pc3_pin_put(pin, 0);
}

MMG_FN void mmow_release(int pin)
{
	pc3_pin_in(pin, 0);
}

/*	What the last reset saw, which is MMBasic's MM.ONEWIRE (its
 *	mmOWvalue, Onewire.c:97). */
static MMINTEGER mmow_value;

/*	481 low, 70 to the sample, 411 to finish - MMBasic's ow_reset to
 *	the microsecond.  1 means a device answered. */
MMG_FN MMINTEGER mmow_reset(MMINTEGER pin)
{
	int p = (int)pin;
	int present;

	if (pin < 0 || pin >= MM_GPIO_NPINS) {
		mm_error("Invalid pin");
		return 0;
	}
	mmow_low(p);
	mmow_us(481);
	mmow_release(p);
	mmow_us(70);
	present = pc3_pin_get(p) ? 0 : 1;	/* a device pulls it DOWN */
	mmow_us(411);
	mmow_value = present;
	return present;
}

/*	MM.ONEWIRE. */
MMG_FN MMINTEGER mmow_last(void)
{
	return mmow_value;
}

MMG_FN void mmow_wbit(int p, int bit)
{
	if (bit) {
		mmow_low(p);
		mmow_us(6);
		mmow_release(p);
		mmow_us(64);
	} else {
		mmow_low(p);
		mmow_us(60);
		mmow_release(p);
		mmow_us(10);
	}
}

MMG_FN int mmow_rbit(int p)
{
	int r;

	mmow_low(p);
	mmow_us(3);
	mmow_release(p);
	mmow_us(10);
	r = pc3_pin_get(p);
	mmow_us(53);
	return r;
}

MMG_FN void mmow_wbyte(int p, int v)
{
	int i;

	for (i = 0; i < 8; i++) {
		mmow_wbit(p, v & 1);
		v >>= 1;
	}
}

MMG_FN int mmow_rbyte(int p)
{
	int i, r = 0;

	for (i = 0; i < 8; i++) {
		r >>= 1;
		if (mmow_rbit(p))
			r |= 0x80;
	}
	return r;
}

/*	The strong pull-up of flag bit 3: the bus is driven HIGH and left
 *	there, which is how a parasite-powered device is given the current
 *	to finish a conversion. */
MMG_FN void mmow_strong(int p)
{
	pc3_pin_out(p);
	pc3_pin_put(p, 1);
}

MMG_FN void mmow_write(MMINTEGER pin, MMINTEGER flag, MMINTEGER n,
		       const unsigned int *buf)
{
	int p = (int)pin, i;

	if (pin < 0 || pin >= MM_GPIO_NPINS) {
		mm_error("Invalid pin");
		return;
	}
	if (flag < 0 || flag > 15) {
		mm_error("Number out of bounds");
		return;
	}
	if (buf == 0)
		return;
	if (flag & 1)
		mmow_reset(pin);
	for (i = 0; i < (int)n; i++) {
		if (flag & 4)
			mmow_wbit(p, (int)(buf[i] & 1));  /* one bit only */
		else
			mmow_wbyte(p, (int)buf[i]);
	}
	if (flag & 2)
		mmow_reset(pin);
	if (flag & 8)
		mmow_strong(p);
}

MMG_FN void mmow_read(MMINTEGER pin, MMINTEGER flag, MMINTEGER n,
		      unsigned int *buf)
{
	int p = (int)pin, i;

	if (pin < 0 || pin >= MM_GPIO_NPINS) {
		mm_error("Invalid pin");
		return;
	}
	if (flag < 0 || flag > 15) {
		mm_error("Number out of bounds");
		return;
	}
	if (buf == 0)
		return;
	if (flag & 1)
		mmow_reset(pin);
	for (i = 0; i < (int)n; i++)
		buf[i] = (flag & 4) ? (unsigned int)mmow_rbit(p)
				    : (unsigned int)mmow_rbyte(p);
	if (flag & 2)
		mmow_reset(pin);
	if (flag & 8)
		mmow_strong(p);
}

/*	The byte-taking pair, for a string or a long string source: the
 *	bytes are already bytes, so there is nothing to narrow.  NULL means
 *	a helper in mmb_comms.h already raised - see the note there; under
 *	ON ERROR SKIP the generated block runs on regardless. */
MMG_FN void mmow_write_bytes(MMINTEGER pin, MMINTEGER flag, MMINTEGER n,
			     const unsigned char *b)
{
	int p = (int)pin, i;

	if (b == 0)
		return;
	if (pin < 0 || pin >= MM_GPIO_NPINS) {
		mm_error("Invalid pin");
		return;
	}
	if (flag < 0 || flag > 15) {
		mm_error("Number out of bounds");
		return;
	}
	if (flag & 1)
		mmow_reset(pin);
	for (i = 0; i < (int)n; i++) {
		if (flag & 4)
			mmow_wbit(p, b[i] & 1);
		else
			mmow_wbyte(p, b[i]);
	}
	if (flag & 2)
		mmow_reset(pin);
	if (flag & 8)
		mmow_strong(p);
}

MMG_FN void mmow_read_bytes(MMINTEGER pin, MMINTEGER flag, MMINTEGER n,
			    unsigned char *b)
{
	int p = (int)pin, i;

	if (b == 0)
		return;
	if (pin < 0 || pin >= MM_GPIO_NPINS) {
		mm_error("Invalid pin");
		return;
	}
	if (flag < 0 || flag > 15) {
		mm_error("Number out of bounds");
		return;
	}
	if (flag & 1)
		mmow_reset(pin);
	for (i = 0; i < (int)n; i++)
		b[i] = (unsigned char)((flag & 4) ? mmow_rbit(p)
						  : mmow_rbyte(p));
	if (flag & 2)
		mmow_reset(pin);
	if (flag & 8)
		mmow_strong(p);
}

/*
 *	TEMPR - the DS18B20.
 *
 *	    TEMPR START pin [, precision [, timeout]]   begin a conversion
 *	    t = TEMPR(pin [, timeout])                  the answer
 *
 *	AND HERE OURS SLEEPS WHERE MMBASIC SPINS, which is the one real
 *	difference and it is deliberate.  A 12-bit conversion takes 750
 *	ms.  MMBasic's fun_ds18b20 either calls uSec(200000) outright or
 *	loops on `while (ds18b20Timer < ds18b20Timers[pin]);` - three
 *	quarters of a second of a processor doing nothing, which on
 *	firmware with one program costs nothing at all.
 *
 *	This machine runs several programs.  Spinning out a conversion
 *	would starve every one of them, so the wait is SLEPT - the same
 *	trade PAUSE makes, and it goes through the same serviced wait when
 *	the program has interrupts armed, so a SETTICK handler still fires
 *	while a temperature is being measured.
 *
 *	The precision is MMBasic's 0 to 3, and so is the default timeout
 *	it implies: 100 << precision, which is 100, 200, 400 and 800 ms
 *	against the device's own 94, 188, 375 and 750.
 */
#define MMOW_MAXPIN	MM_GPIO_NPINS
static long long mmow_due[MMOW_MAXPIN];	/* 0 = no conversion running */

MMG_FN void mmow_init_conv(int p, int precision)
{
	mmow_reset((MMINTEGER)p);
	mmow_wbyte(p, 0xCC);		/* skip ROM - the one device */
	mmow_wbyte(p, 0x4E);		/* write scratchpad */
	mmow_wbyte(p, 0);		/* TH */
	mmow_wbyte(p, 0);		/* TL */
	mmow_wbyte(p, (int)(0x1F | (precision << 5)));	/* config */
	mmow_reset((MMINTEGER)p);
	mmow_wbyte(p, 0xCC);
	mmow_wbyte(p, 0x44);		/* convert T */
}

MMG_FN void mmow_tempr_start(MMINTEGER pin, MMINTEGER precision,
			     MMINTEGER timeout)
{
	int p = (int)pin;

	if (pin < 0 || pin >= MM_GPIO_NPINS) {
		mm_error("Invalid pin");
		return;
	}
	if (precision < 0 || precision > 3) {
		mm_error("Number out of bounds");
		return;
	}
	if (timeout < 0)
		timeout = 100 << precision;
	else if (timeout < 100 || timeout > 2000) {
		mm_error("Number out of bounds");
		return;
	}
	mmow_init_conv(p, (int)precision);
	mmow_due[p] = MMOW_US() + timeout * 1000;
}

/*	The wait.  mm_wait when the program has anything armed, so ticks
 *	keep running through it, and mm_pause otherwise - the same choice
 *	the PAUSE statement makes, decided here because this is a function
 *	and cannot emit a statement before itself. */
MMG_FN void mmow_sleep_until(long long due)
{
	long long left = due - MMOW_US();

	if (left <= 0)
		return;
#ifdef MMB_WAIT_H
	mm_wait((MMFLOAT)left / 1000.0);
#else
	mm_pause((MMFLOAT)left / 1000.0);
#endif
}

MMG_FN MMFLOAT mmow_tempr(MMINTEGER pin, MMINTEGER timeout)
{
	int p = (int)pin, b1, b2;
	short raw;

	if (pin < 0 || pin >= MM_GPIO_NPINS) {
		mm_error("Invalid pin");
		return 0;
	}
	if (mmow_due[p] == 0) {
		/*	No TEMPR START: begin one now at MMBasic's default of
		 *	10 bits, and wait the whole conversion. */
		if (timeout < 0)
			timeout = 200;
		else if (timeout < 100 || timeout > 2000) {
			mm_error("Number out of bounds");
			return 0;
		}
		mmow_init_conv(p, 1);
		mmow_sleep_until(MMOW_US() + timeout * 1000);
	} else {
		mmow_sleep_until(mmow_due[p]);
		mmow_due[p] = 0;
	}
	/*	1000 is MMBasic's "no sensible answer" - not an error, so a
	 *	program polling a disconnected probe carries on. */
	if (!mmow_rbit(p))
		return 1000.0;
	mmow_reset(pin);
	mmow_wbyte(p, 0xCC);
	mmow_wbyte(p, 0xBE);		/* read scratchpad */
	b1 = mmow_rbyte(p);
	b2 = mmow_rbyte(p);
	mmow_reset(pin);
	if (b1 == 255 && b2 == 255)
		return 1000.0;
	raw = (short)(((unsigned short)b2 << 8) | (unsigned short)b1);
	return (MMFLOAT)raw / 16.0;
}

#endif /* MMB_ONEWIRE_H */
