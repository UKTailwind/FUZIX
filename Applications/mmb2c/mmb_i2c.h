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
 *
 *	NO TRANSFER RAISES, on either bus.  MMBasic's i2c_masterCommand
 *	and i2c2_masterCommand set mmI2Cvalue and return - there is no
 *	error() on any path through them - and the program reads MM.I2C:
 *	0 it worked, 1 nothing answered, 2 it started and stopped.
 *
 *	This USED to raise, which was louder than MMBasic rather than
 *	quieter, and still wrong: a program written against the
 *	interpreter carries on past a device that did not answer, and
 *	here it stopped.  A bus scan cannot be written at all against a
 *	version that raises.
 *
 *	OPEN still raises, and that is the line: bad pins or a bad speed
 *	is a mistake in the program, where a device that did not answer
 *	is a fact about the world.  MMBasic draws it in the same place
 *	(i2c2Enable really does error("Pin not set for I2C2")).
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

/*	MMBasic's option argument: bit 0 holds the bus rather than sending
 *	a STOP, so a write can be followed by a read of the same device
 *	without releasing it - a genuine combined transfer.  It is passed
 *	through to the controller, where it is the SDK's nostop argument,
 *	which is exactly what MMBasic does with it (I2C.c).
 *
 *	It was accepted and IGNORED at first, on the grounds that nearly
 *	every register-file device is happy with write-STOP-read because
 *	it keeps its address pointer.  That is true and it is not good
 *	enough: the ones that are not happy read WRONG rather than fail,
 *	and a silent difference from MMBasic is worse than a missing
 *	feature. */
#define MMI2C_HOLD	1

/*	The runtime returns 0 or a NEGATIVE ERRNO, and this turns it into a
 *	sentence a program can act on.  "cannot open on those pins" was the
 *	only message the first version had, and it named the one thing that
 *	was right: the bus was busy and the pins were fine.  There is no
 *	errno.h in the on-board include set, so the numbers are written
 *	out - Library/include/errno.h's, which are Kernel/include/kernel.h's
 *	too, and those two have to agree anyway. */
/*	EIO, EFAULT and ETIMEDOUT are what a TRANSFER returns, and nothing
 *	looks at them here any more - the runtime turns them into MM.I2C.
 *	Kept because they name the numbers the kernel sends back and the
 *	next reader of this file will want them. */
#define MMI2C_EIO	5
#define MMI2C_EFAULT	14
#define MMI2C_EBUSY	16
#define MMI2C_ENODEV	19
#define MMI2C_EINVAL	22
#define MMI2C_ETIMEDOUT	48


MMG_FN void mmi2c_open(MMINTEGER sda, MMINTEGER scl, MMINTEGER speed,
		       MMINTEGER timeout)
{
	MMINTEGER r;

	/*	MMBasic's own test, to the digit (i2cEnable in I2C.c): the
	 *	timeout is 0, or 100 and up.  Checked here rather than only
	 *	in the kernel so the message is MMBasic's. */
	if (timeout < 0 || (timeout > 0 && timeout < 100)) {
		mm_error("I2C2 timeout must be 0 or 100 ms and up");
		return;
	}
	r = mm_i2c_open((int)sda, (int)scl, (int)speed, (int)timeout);
	if (r == 0)
		return;
	switch ((int)-r) {
	case MMI2C_EBUSY:
		mm_error("I2C2 is already in use");
		break;
	case MMI2C_EINVAL:
		/*	Both of the things the kernel checks, because a
		 *	program that gets this has one of them wrong and no
		 *	way to tell which from a shorter message. */
		mm_error("I2C2 needs SDA on GP38 or GP42 with SCL the next "
			 "pin, and 100, 400 or 1000 kHz");
		break;
	case MMI2C_ENODEV:
		mm_error("I2C2 is not available on this machine");
		break;
	default:
		mm_error("I2C2 cannot open");
		break;
	}
}

MMG_FN void mmi2c_close(void)
{
	mm_i2c_close();
}

/*	addr is the 7-bit address, as MMBasic takes it. */
/*	The driver takes bytes; the shared buffer holds values, because
 *	SPI needs 16 bits (mmb_comms.h).  Narrowed here, into the bounce
 *	buffer the kernel's shared I2C driver wants anyway. */
static unsigned char mmi2c_bytes[MMI2C_MAXLEN];

MMG_FN void mmi2c_write(MMINTEGER addr, MMINTEGER opt, MMINTEGER n,
			const unsigned int *buf)
{
	MMINTEGER i;

	/*	MMBasic allows 0-3 on a WRITE and only 0-1 on a READ; bit 0
	 *	is the hold in both.  Same split here. */
	if (opt < 0 || opt > 3) {
		mm_error("I2C option must be 0 to 3");
		return;
	}
	if (n < 1 || n > MMI2C_MAXLEN) {
		mm_error("I2C count out of range");
		return;
	}
	for (i = 0; i < n; i++)
		mmi2c_bytes[i] = (unsigned char)buf[i];
	(void)mm_i2c_msg(MMI2C_BUS, (int)addr, 0, (int)n, mmi2c_bytes,
			 (int)(opt & MMI2C_HOLD));
}

/*	Bytes straight from a string, with no value buffer in the way -
 *	the same shortcut SPI takes, and for the same reason: the bytes
 *	are already bytes and already contiguous. */
MMG_FN void mmi2c_write_bytes(MMINTEGER addr, MMINTEGER opt, MMINTEGER n,
			      const unsigned char *b)
{
	/*	NULL means a helper above already raised.  ON ERROR SKIP
	 *	makes mm_error RETURN rather than stop the statement, so the
	 *	rest of this generated block runs regardless - and without
	 *	this guard a refused length still wrote that many bytes
	 *	through the caller's pointer.  A 400-byte read into a
	 *	24-byte long string overwrote stdio's buffer and the
	 *	program's own output came out as NULs. */

	if (b == 0)
		return;
	if (opt < 0 || opt > 3) {
		mm_error("I2C option must be 0 to 3");
		return;
	}
	if (n < 1 || n > MMI2C_MAXLEN) {
		mm_error("I2C count out of range");
		return;
	}
	(void)mm_i2c_msg(MMI2C_BUS, (int)addr, 0, (int)n,
			 (unsigned char *)b, (int)(opt & MMI2C_HOLD));
}

/*	Straight into a caller's bytes.  Still capped at MMI2C_MAXLEN -
 *	that is the kernel driver's own bounce buffer, not a limit of the
 *	data argument, so a long string buys nothing here.  It is accepted
 *	anyway because the forms are shared and a program should not have
 *	to remember which bus allows which. */
MMG_FN void mmi2c_read_bytes(MMINTEGER addr, MMINTEGER opt, MMINTEGER n,
			     unsigned char *b)
{
	/*	NULL means a helper above already raised.  ON ERROR SKIP
	 *	makes mm_error RETURN rather than stop the statement, so the
	 *	rest of this generated block runs regardless - and without
	 *	this guard a refused length still wrote that many bytes
	 *	through the caller's pointer.  A 400-byte read into a
	 *	24-byte long string overwrote stdio's buffer and the
	 *	program's own output came out as NULs. */
	int i;

	if (b == 0)
		return;
	if (opt < 0 || opt > 1) {
		mm_error("I2C option must be 0 or 1");
		return;
	}
	if (n < 1 || n > MMI2C_MAXLEN) {
		mm_error("I2C count out of range");
		return;
	}
	for (i = 0; i < (int)n; i++)
		b[i] = 0;
	(void)mm_i2c_msg(MMI2C_BUS, (int)addr, 1, (int)n, b,
			 (int)(opt & MMI2C_HOLD));
}

MMG_FN void mmi2c_read(MMINTEGER addr, MMINTEGER opt, MMINTEGER n,
		       unsigned int *buf)
{
	int i;

	if (opt < 0 || opt > 1) {
		mm_error("I2C option must be 0 or 1");
		return;
	}
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
		mmi2c_bytes[i] = 0;
	(void)mm_i2c_msg(MMI2C_BUS, (int)addr, 1, (int)n, mmi2c_bytes,
			 (int)(opt & MMI2C_HOLD));
	/*	Copied out even when it failed: the buffer was zeroed
	 *	first, so the destination is DEFINED either way.  Nothing
	 *	raises here now, so a program that did not look at MM.I2C
	 *	would otherwise carry whatever was in its array before. */
	for (i = 0; i < (int)n; i++)
		buf[i] = mmi2c_bytes[i];
}

/*
 *	I2C0 - the FIXED bus: GP20/GP21, the QWIIC socket and the DS3231
 *	together.
 *
 *	    I2C WRITE addr, option, count, d1 [, d2 ...]
 *	    I2C READ  addr, option, count, var
 *	    I2C CHECK addr
 *
 *	NO SETPIN, NO OPEN, NO CLOSE, and that is not a simplification -
 *	the pins are the board's and the controller is already running
 *	for the clock.  MMBasic draws the same line for the same reason
 *	(I2C.c cmd_i2c has no pin test where cmd_i2c2 errors "Pin not set
 *	for I2C2"), and the kernel keeps it behind /dev/i2c so a program
 *	and the RTC poll in interrupt context cannot collide.
 *
 *	AND NOTHING HERE RAISES.  MMBasic's I2C transfers set mmI2Cvalue
 *	- 0 it worked, 1 nothing answered, 2 it started and stopped - and
 *	return; the program reads MM.I2C.  That is the whole point of
 *	I2C CHECK, and it is what a scan loop over 128 addresses needs.
 *
 *	This is a DIVERGENCE from our own I2C2, which raises.  Both
 *	cannot be right and MMBasic is the reference, so the fixed bus
 *	follows it; I2C2's behaviour is left alone here rather than
 *	changed underneath programs that already rely on it.
 */
#define MMI2C0_BUS	0

MMG_FN void mmi2c0_write(MMINTEGER addr, MMINTEGER opt, MMINTEGER n,
			 const unsigned int *buf)
{
	MMINTEGER i;

	if (opt < 0 || opt > 3) {
		mm_error("I2C option must be 0 to 3");
		return;
	}
	if (n < 1 || n > MMI2C_MAXLEN) {
		mm_error("I2C count out of range");
		return;
	}
	for (i = 0; i < n; i++)
		mmi2c_bytes[i] = (unsigned char)buf[i];
	(void)mm_i2c_msg(MMI2C0_BUS, (int)addr, 0, (int)n, mmi2c_bytes,
			 (int)(opt & MMI2C_HOLD));
}

MMG_FN void mmi2c0_write_bytes(MMINTEGER addr, MMINTEGER opt, MMINTEGER n,
			       const unsigned char *b)
{
	if (b == 0)
		return;
	if (opt < 0 || opt > 3) {
		mm_error("I2C option must be 0 to 3");
		return;
	}
	if (n < 1 || n > MMI2C_MAXLEN) {
		mm_error("I2C count out of range");
		return;
	}
	(void)mm_i2c_msg(MMI2C0_BUS, (int)addr, 0, (int)n,
			 (unsigned char *)b, (int)(opt & MMI2C_HOLD));
}

MMG_FN void mmi2c0_read_bytes(MMINTEGER addr, MMINTEGER opt, MMINTEGER n,
			      unsigned char *b)
{
	int i;

	if (b == 0)
		return;
	if (opt < 0 || opt > 1) {
		mm_error("I2C option must be 0 or 1");
		return;
	}
	if (n < 1 || n > MMI2C_MAXLEN) {
		mm_error("I2C count out of range");
		return;
	}
	/*	Zeroed first, for the reason the I2C2 read gives at
	 *	length: nothing raises here, so a failed read that left
	 *	the previous transfer's bytes in place would look exactly
	 *	like a device answering. */
	for (i = 0; i < (int)n; i++)
		b[i] = 0;
	(void)mm_i2c_msg(MMI2C0_BUS, (int)addr, 1, (int)n, b,
			 (int)(opt & MMI2C_HOLD));
}

MMG_FN void mmi2c0_read(MMINTEGER addr, MMINTEGER opt, MMINTEGER n,
			unsigned int *buf)
{
	int i;

	if (opt < 0 || opt > 1) {
		mm_error("I2C option must be 0 or 1");
		return;
	}
	if (n < 1 || n > MMI2C_MAXLEN) {
		mm_error("I2C count out of range");
		return;
	}
	for (i = 0; i < (int)n; i++)
		mmi2c_bytes[i] = 0;
	(void)mm_i2c_msg(MMI2C0_BUS, (int)addr, 1, (int)n, mmi2c_bytes,
			 (int)(opt & MMI2C_HOLD));
	for (i = 0; i < (int)n; i++)
		buf[i] = mmi2c_bytes[i];
}

/*
 *	I2C CHECK addr - is anything at that address?
 *
 *	MMBasic's i2cCheck (I2C.c:1534) reads ONE byte and records the
 *	result in mmI2Cvalue; it neither returns anything nor raises, so
 *	the answer is read with MM.I2C afterwards.  A one-byte read is
 *	also the only probe that works on a device with no safe register
 *	to write.
 */
MMG_FN void mmi2c0_check(MMINTEGER addr)
{
	unsigned char d = 0;

	if (addr < 0 || addr > 0x7F) {
		mm_error("Invalid I2C address");
		return;
	}
	(void)mm_i2c_msg(MMI2C0_BUS, (int)addr, 1, 1, &d, 0);
}

#endif /* MMB_I2C_H */
