#ifndef MMB_GPIO_H
#define MMB_GPIO_H
/*
 *	SETPIN and PIN.
 *
 *	These are NOT in bcrun, for the same reason the drawing
 *	primitives are not: bcrun is loaded for every translated program
 *	on the machine, so a byte added there is a byte taken from
 *	programs that never touch a pin.  Here they are file scope
 *	statics, and cc1 generates nothing for a static that nothing
 *	names (hosttest/deadstatic.sh), so a program pays for SETPIN only
 *	if it says SETPIN.
 *
 *	NO SYSCALL ON THE PIN PATH ANY MORE.  There is no MMU on the PC3
 *	and the kernel never drops privilege, so a program reaches the
 *	pin registers with a store; the ioctl this used to make cost
 *	1.488us against about ten nanoseconds for the store it wrapped.
 *	What crosses into the kernel now is one CLAIM per pin, once, at
 *	SETPIN - see <sys/pc3io.h> and the kernel's pinlock.c.  The claim
 *	is what makes the pin come back, reset, when the program exits or
 *	dies; nothing has to give it back by hand.
 *
 *	MMBasic's SETPIN has many more modes than these, and its own pin
 *	NUMBERING (connector pins, not GPIO numbers).  This is the GPIO
 *	number, because that is what the PC3's documentation, its
 *	schematic and every other tool on the machine use, and inventing
 *	a second numbering for one command would be worse than the small
 *	incompatibility.
 *
 *	The bodies below are the SAME on the board and on the host - the
 *	host just gets stubs for the hardware primitives.  That is what
 *	makes a translated program using SETPIN still run under the
 *	gates, and keeps the arithmetic (which is where the interesting
 *	part of AIN is) on the tested path.
 */

#include "mmb_runtime.h"

/*	A program that says SETPIN but never PIN(n) = v names only some of
 *	these, and gcc warns about the rest.  The on-board cc has no
 *	attributes and discards an unnamed static anyway, which is the
 *	whole bargain this header rests on, so it just gets "static".
 *
 *	Keyed on which compiler compiles the OUTPUT, not on __GNUC__:
 *	fccbuild.sh preprocesses with gcc -E and then feeds cc1, so
 *	__GNUC__ is defined while the compiler that has to swallow this is
 *	cc1.  Keying on __GNUC__ handed it __attribute__ and it said
 *	"missing semicolon" twenty times. */
#if defined(MM_FCC) || defined(MM_PC3)
#define MMG_FN static
#else
#define MMG_FN static __inline__ __attribute__((unused))
#endif

/*	The registers, but not the claim wrappers - PC3IO_NO_SYSCALLS
 *	leaves out everything in that header needing open() and ioctl(),
 *	which the on-board cc does not have.  Claiming goes through
 *	mm_gpio instead, once per SETPIN.
 *
 *	Two spellings of the same file: the on-board cc searches ONE flat
 *	include directory (mkccimage.sh builds it, deliberately not
 *	/usr/include), while a native program gets it from the C library's
 *	<sys/>.  Same file, staged twice, so there is nothing to drift. */
#define PC3IO_NO_SYSCALLS 1
#if defined(MM_PC3) || defined(__FUZIX__)
#ifdef __GNUC__
#include <sys/pc3io.h>		/* a cross build: the C library layout */
#else
#include <pc3io.h>		/* the on-board cc's one flat include dir */
#endif
#else
/*	No pins here.  Silent rather than failing, as the rest of the
 *	host build is: a program using SETPIN and PIN runs under the
 *	gates and a read answers 0. */
static void pc3_pin_out(int p) { (void)p; }
static void pc3_pin_in(int p, int u) { (void)p; (void)u; }
static void pc3_pin_put(int p, int v) { (void)p; (void)v; }
static int pc3_pin_get(int p) { (void)p; return 0; }
static void pc3_adc_enable(void) {}
static void pc3_adc_pin(int c) { (void)c; }
static void pc3_adc_select(int c) { (void)c; }
static int pc3_adc_conv(void) { return 0; }
static int pc3_adc_read(int c) { (void)c; return 0; }
#endif

/*	The one crossing.  mm_gpio does the ioctl because the on-board cc
 *	cannot; everything else on this page is a register. */
MMG_FN int mmg_claim(MMINTEGER pin, MMINTEGER cls)
{
	return (int)mm_gpio(MM_GPIO_CLAIM, pin, cls);
}

/*	SETPIN's modes.  The numbers are ours - MMBasic's tokens are
 *	words, and the translator turns them into these.  OFF is zero so
 *	that the table below starts out saying "not configured", which is
 *	what lets PIN() refuse a pin nobody has set up, as MMBasic does. */
#define MMG_PIN_OFF	0
#define MMG_PIN_DIN	1
#define MMG_PIN_DOUT	2
#define MMG_PIN_AIN	3
#define MMG_PIN_ARAW	4
/*	The three interrupt modes are DIGITAL INPUTS that also carry an
 *	edge test - MMBasic's EXT_INT_HI/LO/BOTH sit in the digital list
 *	in fun_pin, so PIN() on one reads the level exactly as DIN does.
 *	The edge itself is polled, in mmb_int.h; nothing here knows about
 *	handlers. */
#define MMG_PIN_INTH	5
#define MMG_PIN_INTL	6
#define MMG_PIN_INTB	7

static unsigned char mmg_mode[MM_GPIO_NPINS];

/*	MMBasic's analogue constants, from PicoMite's External.c - not
 *	invented here.  ANA_AVERAGE readings are sorted, ANA_DISCARD are
 *	thrown away from EACH end, and what is left is averaged and
 *	scaled by VCC.  MMBasic's VCC is an OPTION defaulting to 3.3; we
 *	have no OPTION VCC, so it is the default. */
#define MMG_ANA_AVERAGE	10
#define MMG_ANA_DISCARD	2
#define MMG_VCC		3.3
#define MMG_ADC_FULL	4095.0

/*	RP2350B: ADC channel n is GP40+n.  The PC3's I/O header brings out
 *	GP34-GP46, so channels 0-6 are reachable; the kernel's claim is
 *	the authority on which, and refuses the rest. */
MMG_FN int mmg_adc_chan(MMINTEGER pin)
{
	if (pin >= 40 && pin <= 47)
		return (int)(pin - 40);
	return -1;
}

MMG_FN void mmg_setpin(MMINTEGER pin, MMINTEGER mode)
{
	int ch;

	if (pin < 0 || pin >= MM_GPIO_NPINS) {
		mm_error("Invalid pin");
		return;
	}
	if (mode == MMG_PIN_AIN || mode == MMG_PIN_ARAW) {
		ch = mmg_adc_chan(pin);
		/*	One converter shared by every channel, so the ADC
		 *	is claimed as well as the pin - two programs on two
		 *	analogue pins would otherwise take turns changing
		 *	each other's channel. */
		if (ch < 0 || mmg_claim(0, MM_PLK_ADC)
		    || mmg_claim(pin, MM_PLK_PIN)) {
			mm_error("Pin cannot do that");
			return;
		}
		pc3_adc_enable();
		pc3_adc_pin(ch);
	} else {
		if (mmg_claim(pin, MM_PLK_PIN)) {
			mm_error("Pin cannot do that");
			return;
		}
		if (mode == MMG_PIN_DOUT)
			pc3_pin_out((int)pin);
		else
			pc3_pin_in((int)pin, 0);	/* MMBasic's DIN floats,
						   and so do the INT modes */
	}
	mmg_mode[pin] = (unsigned char)mode;
}

/*	PIN(n) = v.  Kept separate from the read so that a program doing
 *	only one of them carries only that one.
 *
 *	Refuses a pin that is not an output, which is MMBasic's rule.
 *	The old version quietly turned any pin into an output because
 *	that was what the ioctl did; that was more forgiving and less
 *	honest - writing to an input is a bug in the BASIC, and saying so
 *	is the whole reason SETPIN exists. */
MMG_FN void mmg_pin_put(MMINTEGER pin, MMINTEGER val)
{
	if (pin < 0 || pin >= MM_GPIO_NPINS)
		mm_error("Invalid pin");
	else if (mmg_mode[pin] != MMG_PIN_DOUT)
		mm_error("Pin is not an output");
	else
		pc3_pin_put((int)pin, val ? 1 : 0);
}

/*
 *	PIN(n).
 *
 *	MMFLOAT, always - and that is a deliberate divergence worth
 *	naming.  MMBasic decides the type of PIN() at RUN time: T_INT for
 *	a digital pin or ARAW, T_NBR for AIN.  Translated C has to know
 *	the type when it is generated, and nothing at translation time
 *	knows what mode a pin will be in - SETPIN's pin can be an
 *	expression and its mode can change.  So one type has to cover
 *	both, and it has to be the float: a double holds 0, 1 and every
 *	12-bit count exactly, while an integer cannot hold 1.6523 volts.
 *
 *	Nothing observable changes for a digital program.  PRINT PIN(2)
 *	prints "1" either way, comparisons and array indices behave the
 *	same, and MMBasic prints an integral float without a decimal
 *	point.
 */
MMG_FN MMFLOAT mmg_pin_get(MMINTEGER pin)
{
	int b[MMG_ANA_AVERAGE];
	int i, j, ch, t;

	if (pin < 0 || pin >= MM_GPIO_NPINS) {
		mm_error("Invalid pin");
		return 0;
	}
	switch (mmg_mode[pin]) {
	case MMG_PIN_DIN:
	case MMG_PIN_DOUT:
	case MMG_PIN_INTH:
	case MMG_PIN_INTL:
	case MMG_PIN_INTB:
		/*	MMBasic reads back an output pin too, and returns
		 *	what it is driving; an interrupt pin is a digital
		 *	input and reads as one. */
		return (MMFLOAT)pc3_pin_get((int)pin);

	case MMG_PIN_ARAW:
		/*	The raw count, one conversion, no averaging - which
		 *	is exactly what ARAW is for. */
		return (MMFLOAT)pc3_adc_read(mmg_adc_chan(pin));

	case MMG_PIN_AIN:
		/*	MMBasic's own filter, step for step (External.c,
		 *	fun_pin): take ANA_AVERAGE readings, sort them
		 *	descending, throw away ANA_DISCARD from each end,
		 *	and average what is left.  The point is the
		 *	DISCARD, not the averaging - it removes the
		 *	occasional wild sample that an averaging filter
		 *	would smear across every reading instead. */
		ch = mmg_adc_chan(pin);
		pc3_adc_select(ch);
		pc3_adc_conv();		/* discard: mux settle */
		for (i = 0; i < MMG_ANA_AVERAGE; i++) {
			b[i] = pc3_adc_conv();
			for (j = i; j > 0; j--) {
				if (b[j - 1] < b[j]) {
					t = b[j - 1];
					b[j - 1] = b[j];
					b[j] = t;
				} else
					break;
			}
		}
		j = 0;
		for (i = MMG_ANA_DISCARD;
		     i < MMG_ANA_AVERAGE - MMG_ANA_DISCARD; i++)
			j += b[i];
		return ((MMFLOAT)j * MMG_VCC)
			/ (MMG_ADC_FULL
			   * (MMFLOAT)(MMG_ANA_AVERAGE - MMG_ANA_DISCARD * 2));

	default:
		mm_error("Pin is not an input");
		return 0;
	}
}

#endif /* MMB_GPIO_H */
