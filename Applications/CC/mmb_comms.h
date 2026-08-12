#ifndef MMB_COMMS_H
#define MMB_COMMS_H
/*
 *	The data arguments shared by I2C, SPI and one-wire.
 *
 *	MMBasic has ONE implementation of this and three callers.
 *	GetCommsTxData (MMBasic.c:6601), GetCommsRxDest (:6660) and
 *	PutCommsRxData (:6745) are reached from I2C.c, Onewire.c and -
 *	through GetSendDataList and GetReceiveDataBuffer - SPI.c.  That is
 *	not tidiness for its own sake: the three buses take the SAME data
 *	forms, and a program that learns `I2C WRITE n, a()` expects
 *	`SPI WRITE n, a()` to mean the same thing.
 *
 *	This file is that one implementation.  Before it, the forms were
 *	written out twice - once in mmb_i2c.h and once in mmb_spi.h - and
 *	the two copies had drifted in five ways, every one of which is
 *	fixed by having a single copy:
 *
 *	  - a TX list did not check its count against the length, so
 *	    `I2C2 WRITE addr,0,3, 1,2` built a two-byte buffer, told the
 *	    driver three, and the third byte came off the stack;
 *	  - the RX list-of-lvalues form did not exist at all;
 *	  - neither array form was bounds checked;
 *	  - SPI's string path reported I2C2's error messages, because it
 *	    called I2C2's helper;
 *	  - and the buffers were bytes, so `SPI OPEN speed, mode, 16`
 *	    sent the low half of each 16-bit word and dropped the rest.
 *
 *	THE BUFFER HOLDS VALUES, NOT BYTES, which is the last of those.
 *	MMBasic's is `unsigned int buf[]` for exactly one reason: an SPI
 *	word can be up to 16 bits.  Each bus narrows on the way out.
 *
 *	The error numbers are MMBasic's, and so is the wording:
 *	    2   Argument count            a list whose length is wrong
 *	    6   Invalid variable          a target that cannot hold it
 *	    28  Insufficient data         a source with less than asked
 *	    32  Insufficient space in array   a target too small
 *
 *	One deliberate difference: a STRING source is not copied through
 *	the buffer.  Its bytes are already contiguous and already bytes,
 *	so the bus reads them where they lie.  MMBasic copies because its
 *	buffer is the only path it has; skipping the copy changes nothing
 *	a program can see and is what lets a display row go out in one
 *	call.
 */

#include "mmb_runtime.h"

/*	Its own, guarded, because this header can be the first of the
 *	family a program includes - mmb_peek.h left this out and compiled
 *	everywhere except the one-page example that used nothing else. */
#ifndef MMG_FN
#if defined(MM_FCC) || defined(MM_PC3)
#define MMG_FN static
#else
#define MMG_FN static __inline__ __attribute__((unused))
#endif
#endif

/*	256 values, which is MMBasic's own I2C buffer size (`unsigned int
 *	buf[256]` in i2cSend).  SPI allocates as much as asked for there;
 *	here a longer transfer wants the string or long-string form, which
 *	needs no buffer at all. */
#define MMC_MAXN	256

static unsigned int mmc_buf[MMC_MAXN];

/*	The shared buffer, with the length checked once so that no caller
 *	has to remember to. */
MMG_FN unsigned int *mmc_buf_for(MMINTEGER n)
{
	if (n < 1 || n > MMC_MAXN) {
		mm_error("Number out of bounds");
		return mmc_buf;		/* mm_error does not return */
	}
	return mmc_buf;
}

/*	A list of expressions: the translator has already stored them, and
 *	this only checks that it stored as many as the length asked for.
 *	MMBasic tests `len != ((argc - dataidx + 1) >> 1)` and raises 2. */
MMG_FN void mmc_count(MMINTEGER n, MMINTEGER given)
{
	if (n != given)
		mm_error("Argument count");
}

/*	A string source.  MMBasic raises 28 when the string is shorter
 *	than the length asked for - it does not pad, and it does not send
 *	a short transfer. */
MMG_FN const unsigned char *mmc_tx_str(const char *s, MMINTEGER n)
{
	if ((MMINTEGER)mm_slen(s) < n) {
		mm_error("Insufficient data");
		return (const unsigned char *)"";
	}
	/*	Element 0 is the length byte, so the data starts at 1. */
	return (const unsigned char *)(s + 1);
}

MMG_FN void mmc_tx_arr_i(unsigned int *b, MMINTEGER n, const MMINTEGER *a,
			 MMINTEGER cells)
{
	MMINTEGER i;

	if (n > cells) {
		mm_error("Insufficient data");
		return;
	}
	for (i = 0; i < n; i++)
		b[i] = (unsigned int)a[i];
}

MMG_FN void mmc_tx_arr_f(unsigned int *b, MMINTEGER n, const MMFLOAT *a,
			 MMINTEGER cells)
{
	MMINTEGER i;

	if (n > cells) {
		mm_error("Insufficient data");
		return;
	}
	/*	MMBasic puts a float through FloatToInt32, which ROUNDS
	 *	rather than truncating; mm_toint is the runtime's version of
	 *	that same rounding. */
	for (i = 0; i < n; i++)
		b[i] = (unsigned int)mm_toint(a[i]);
}

/*
 *	THE DESTINATION IS CHECKED BEFORE THE TRANSFER, not after, and
 *	that is not fussiness.  GetCommsRxDest runs first in MMBasic for
 *	the same reason: a read on either bus has SIDE EFFECTS - it
 *	addresses a device, and may advance a register pointer inside it -
 *	so a program that asks for forty bytes into an array of sixteen
 *	must be refused before the bus moves, not after.  The first
 *	version of this file scattered and checked at the end, and the
 *	transaction had already happened.
 */
MMG_FN void mmc_rx_fits(MMINTEGER cells, MMINTEGER n)
{
	if (n > cells)
		mm_error("Insufficient space in array");
}

MMG_FN void mmc_rx_strfits(MMINTEGER n)
{
	if (n < 1 || n > MM_STRLEN)
		mm_error("Number out of bounds");
}

/*	A string target has its length byte set to the count, exactly as
 *	GetCommsRxDest does before the transfer. */
MMG_FN void mmc_rx_str(char *s, const unsigned int *b, MMINTEGER n)
{
	MMINTEGER i;

	if (n < 1 || n > MM_STRLEN) {
		mm_error("Number out of bounds");
		return;
	}
	s[0] = (char)(unsigned char)n;
	for (i = 0; i < n; i++)
		s[i + 1] = (char)(unsigned char)b[i];
	s[n + 1] = 0;
}

MMG_FN void mmc_rx_arr_i(MMINTEGER *a, MMINTEGER cells, const unsigned int *b,
			 MMINTEGER n)
{
	MMINTEGER i;

	if (n > cells) {
		mm_error("Insufficient space in array");
		return;
	}
	for (i = 0; i < n; i++)
		a[i] = (MMINTEGER)b[i];
}

MMG_FN void mmc_rx_arr_f(MMFLOAT *a, MMINTEGER cells, const unsigned int *b,
			 MMINTEGER n)
{
	MMINTEGER i;

	if (n > cells) {
		mm_error("Insufficient space in array");
		return;
	}
	for (i = 0; i < n; i++)
		a[i] = (MMFLOAT)b[i];
}

#endif /* MMB_COMMS_H */
