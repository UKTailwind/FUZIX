#ifndef MMB_SPI_H
#define MMB_SPI_H
/*
 *	SPI - MMBasic's first controller, on header pins.
 *
 *	    SETPIN p1, p2, p3, SPI
 *	    SPI OPEN speed, mode [, bits]
 *	    SPI WRITE n, d1 [, d2 ...]   |  n, array()  |  n, string$
 *	    SPI READ  n, var
 *	    v = SPI(x)
 *	    SPI CLOSE
 *
 *	SPI2 is NOT offered: that is the second controller, and on this
 *	board it is the SD card's.  A program that could take it could
 *	take the filesystem out from under itself.
 *
 *	CHIP SELECT IS THE PROGRAM'S, exactly as on a PicoMite - MMBasic
 *	does not drive it either.  A display needs CS held across a whole
 *	command-and-data sequence rather than per transfer, so only the
 *	program knows when to move it; and now that pins are userland it
 *	is a register write rather than a call.
 */

#include "mmb_runtime.h"

/*	The same bargain the other mmb_*.h headers make: keyed on which
 *	compiler compiles the OUTPUT, because fccbuild.sh preprocesses
 *	with gcc and then feeds cc1.  Guarded so a program including
 *	several of these defines it once. */
#ifndef MMG_FN
#if defined(MM_FCC) || defined(MM_PC3)
#define MMG_FN static
#else
#define MMG_FN static __inline__ __attribute__((unused))
#endif
#endif

/*	errno numbers, written out because there is no errno.h in the
 *	on-board include set.  Library/include/errno.h's, which are also
 *	Kernel/include/kernel.h's. */
#ifndef MMI2C_EIO
#define MMI2C_EIO	5
#define MMI2C_EBUSY	16
#define MMI2C_ENODEV	19
#define MMI2C_EINVAL	22
#endif

/*
 *	Which SPI signal a pin carries.  The RP2350 decides this, not the
 *	program: the INSTANCE is (pin >> 3) & 1 - 0 for SPI0, 1 for SPI1 -
 *	and the ROLE is pin & 3.  Checked against the SDK's own
 *	io_bank0.h for every header pin.
 *
 *	This is why SETPIN takes three pins in ANY ORDER.  MMBasic does
 *	the same thing: it asks each pin what it can be (PinDef[pin].mode
 *	& SPI0RX/TX/SCK in External.c) rather than fixing an order, and
 *	errors only if two of them come out the same.
 */
#define MMSPI_ROLE(p)	((p) & 3)
#define MMSPI_RX	0
#define MMSPI_SCK	2
#define MMSPI_TX	3
#define MMSPI_IS_SPI0(p) ((((p) >> 3) & 1) == 0)

static MMINTEGER mmspi_actual_hz;

MMG_FN void mmspi_failed(MMINTEGER r)
{
	switch ((int)-r) {
	case MMI2C_EBUSY:
		mm_error("SPI is already in use");
		break;
	case MMI2C_EINVAL:
		mm_error("SPI needs a SCLK, MOSI and MISO pin of SPI0, "
			 "mode 0 to 3 and 4 to 16 bits");
		break;
	case MMI2C_ENODEV:
		mm_error("SPI is not open");
		break;
	default:
		mm_error("SPI transfer failed");
		break;
	}
}

/*
 *	speed, mode and bits are MMBasic's OPEN arguments.  It validates
 *	only bits (4 to 16) and passes speed and mode straight to the SDK;
 *	the kernel checks the rest, since it is the one that can say
 *	whether the pins can carry the signals at all.
 */
MMG_FN void mmspi_open(MMINTEGER p1, MMINTEGER p2, MMINTEGER p3,
		       MMINTEGER speed, MMINTEGER mode, MMINTEGER bits)
{
	MMINTEGER pins[3];
	int sck = -1, tx = -1, rx = -1;
	MMINTEGER r;
	int i;

	pins[0] = p1;
	pins[1] = p2;
	pins[2] = p3;
	for (i = 0; i < 3; i++) {
		int p = (int)pins[i];

		if (p < 0 || p > 47 || !MMSPI_IS_SPI0(p)) {
			mm_error("that pin cannot carry an SPI0 signal");
			return;
		}
		switch (MMSPI_ROLE(p)) {
		case MMSPI_SCK: sck = p; break;
		case MMSPI_TX:  tx = p;  break;
		case MMSPI_RX:  rx = p;  break;
		default:
			/*	role 1 is SS, which this does not use: MMBasic
			 *	leaves chip select to the program and so does
			 *	this. */
			mm_error("that pin is the SPI chip select, which the "
				 "program drives itself");
			return;
		}
	}
	if (sck < 0 || tx < 0 || rx < 0) {
		mm_error("SETPIN for SPI needs one SCLK, one MOSI and one "
			 "MISO pin, and no two the same");
		return;
	}
	if (bits < 4 || bits > 16) {
		mm_error("SPI bits must be 4 to 16");
		return;
	}
	if (mode < 0 || mode > 3) {
		mm_error("SPI mode must be 0 to 3");
		return;
	}
	r = mm_spi_open(sck, tx, rx, (int)speed, (int)mode, (int)bits);
	if (r < 0) {
		mmspi_failed(r);
		return;
	}
	/*	What the controller could actually reach.  The divisor is
	 *	clk_peri / (CPSDVSR * (1 + SCR)) with CPSDVSR even, so a
	 *	request usually lands on a neighbouring value, and anything
	 *	above clk_peri / 2 quietly becomes clk_peri / 2.  Kept so a
	 *	program can ask rather than guess - MM.SPISPEED below. */
	mmspi_actual_hz = r;
}

/*	The clock the last OPEN actually got, for MM.SPISPEED. */
MMG_FN MMINTEGER mmspi_speed(void)
{
	return mmspi_actual_hz;
}

MMG_FN void mmspi_close(void)
{
	mm_spi_close();
}

MMG_FN void mmspi_write(MMINTEGER n, const unsigned char *buf)
{
	MMINTEGER r;

	if (n < 1)
		return;
	r = mm_spi_xfer((unsigned char *)buf, (unsigned char *)0, (int)n);
	if (r < 0)
		mmspi_failed(r);
}

MMG_FN void mmspi_read(MMINTEGER n, unsigned char *buf)
{
	MMINTEGER r;
	int i;

	if (n < 1)
		return;
	/*	Cleared first, so a transfer that fails leaves zeros rather
	 *	than whatever was on the stack - the same trap I2C2 READ
	 *	fell into, where the buffer still held a previous write's
	 *	bytes and looked exactly like a device answering. */
	for (i = 0; i < (int)n; i++)
		buf[i] = 0;
	r = mm_spi_xfer((unsigned char *)0, buf, (int)n);
	if (r < 0)
		mmspi_failed(r);
}

/*	The SPI() function: send one unit, return what came back. */
MMG_FN MMINTEGER mmspi_xfer1(MMINTEGER v)
{
	unsigned char out[2], in[2];
	MMINTEGER r;

	out[0] = (unsigned char)v;
	out[1] = (unsigned char)(v >> 8);
	in[0] = in[1] = 0;
	r = mm_spi_xfer(out, in, 1);
	if (r < 0) {
		mmspi_failed(r);
		return 0;
	}
	return (MMINTEGER)in[0] | ((MMINTEGER)in[1] << 8);
}

#endif /* MMB_SPI_H */
