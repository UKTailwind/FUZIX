#ifndef MMB_I2C_H
#define MMB_I2C_H
/*
 *	I2C2 - the second controller, on header pins.
 *
 *	    SETPIN sda, scl, I2C2
 *	    I2C2 OPEN speed, timeout
 *	    I2C2 WRITE addr, option, count, d1 [, d2 ...]
 *	    I2C2 READ  addr, option, count, var
 *	    I2C2 CLOSE
 *
 *	MMBasic's split, and its reason: the FIXED bus (I2C0, GP20/21,
 *	the QWIIC socket) is always there and needs no OPEN, while this
 *	one has no pins until a program says which.  So SETPIN assigns
 *	them and OPEN starts the controller.
 *
 *	UNLIKE THE PIN WORK, THIS IS A SYSCALL PER TRANSACTION, and that
 *	is deliberate rather than unfinished.  The argument that moved
 *	pins into userland was arithmetic: an ioctl costs 1.488us against
 *	about ten nanoseconds for the store it wrapped.  An I2C
 *	transaction is ~300us of bus time, so the same crossing is half a
 *	percent - and on the other side of it is the SDK's controller
 *	driver, which handles the DesignWare block's FIFO, its abort
 *	flags and its clock timing.  Reimplementing that in a header to
 *	save 0.5% would be trading proven code for a new source of subtle
 *	bugs.
 *
 *	The transfers go through /dev/i2c's ordinary I2C_MSG with bus 1;
 *	only the OPEN is platform-specific.
 */

#include "mmb_runtime.h"

/*	The same bargain mmb_gpio.h makes, and the same reasoning: keyed
 *	on which compiler compiles the OUTPUT, because fccbuild.sh
 *	preprocesses with gcc and then feeds cc1.  Guarded so a program
 *	including both headers defines it once. */
#ifndef MMG_FN
#if defined(MM_FCC) || defined(MM_PC3)
#define MMG_FN static
#else
#define MMG_FN static __inline__ __attribute__((unused))
#endif
#endif

#define MMI2C_BUS	1		/* MMBasic's I2C2 = the second one */
#define MMI2C_MAXLEN	64		/* the kernel driver's buffer */

/*	MMBasic's option argument: bit 0 holds the bus rather than
 *	sending a STOP, so a write can be followed by a read of the same
 *	device without releasing it.  The kernel interface is one
 *	transaction per call and has no repeated START, so a hold is
 *	accepted and ignored - and that is written down here because a
 *	device needing a true combined transfer will read wrong rather
 *	than fail, which is the kind of thing that costs an afternoon.
 *	Nearly every register-file device (the BME280 included) is happy
 *	with write-STOP-read, because it keeps its address pointer. */
#define MMI2C_HOLD	1

MMG_FN void mmi2c_open(MMINTEGER sda, MMINTEGER scl, MMINTEGER speed,
		       MMINTEGER timeout)
{
	(void)timeout;			/* the kernel's own is fixed */
	if (mm_i2c_open((int)sda, (int)scl, (int)speed))
		mm_error("I2C2 cannot open on those pins");
}

MMG_FN void mmi2c_close(void)
{
	mm_i2c_close();
}

/*	addr is the 7-bit address, as MMBasic takes it. */
MMG_FN void mmi2c_write(MMINTEGER addr, MMINTEGER opt, MMINTEGER n,
			const unsigned char *buf)
{
	(void)opt;
	if (n < 1 || n > MMI2C_MAXLEN) {
		mm_error("I2C count out of range");
		return;
	}
	if (mm_i2c_xfer((int)addr, 0, (int)n, (unsigned char *)buf))
		mm_error("I2C2 write failed");
}

MMG_FN void mmi2c_read(MMINTEGER addr, MMINTEGER opt, MMINTEGER n,
		       unsigned char *buf)
{
	int i;

	(void)opt;
	if (n < 1 || n > MMI2C_MAXLEN) {
		mm_error("I2C count out of range");
		return;
	}
	/*	Cleared FIRST, so a read that fails leaves zeros rather
	 *	than whatever was on the stack.  With ON ERROR SKIP the
	 *	program carries on and copies this buffer into its array
	 *	either way - and the first version handed it the bytes a
	 *	previous WRITE had left at the same address, which looks
	 *	exactly like a device answering. */
	for (i = 0; i < (int)n; i++)
		buf[i] = 0;
	if (mm_i2c_xfer((int)addr, 1, (int)n, buf))
		mm_error("I2C2 read failed");
}

#endif /* MMB_I2C_H */
