#ifndef MMB_PORT_H
#define MMB_PORT_H
/*
 *	PORT(pin, nbits [, pin, nbits]...) = value
 *	v = PORT(pin, nbits [, pin, nbits]...)
 *
 *	Several pins read or written as one number - MMBasic's cmd_port
 *	and fun_port (misc/External.c).  What SETPIN and PIN are for one
 *	line, this is for a bus: a parallel display, a set of address
 *	lines, eight switches read in one go.
 *
 *	THE BIT ORDER IS THE PART TO GET RIGHT, and it is not the obvious
 *	one.  Within a group the FIRST pin is the LEAST significant bit:
 *
 *	    PORT(10, 4) = &b1010    ->  GP10=0 GP11=1 GP12=0 GP13=1
 *
 *	cmd_port walks the pins upwards taking value & 1 and shifting
 *	right, so the low bit lands on the low pin.  With several groups
 *	the FIRST group takes the low bits and later groups the higher
 *	ones, because the shifting simply carries on.
 *
 *	fun_port is written the other way round on purpose - it walks the
 *	groups backwards and, within a group, starts at pin + nbits - 1
 *	shifting LEFT - so that reading gives back exactly what writing
 *	took.  Both directions are transcribed rather than reasoned about
 *	a second time; a round trip is the test that matters and
 *	samples/port.bas does it.
 *
 *	EVERY PIN CHANGES ON THE SAME EDGE, and that is the whole reason
 *	PORT exists rather than a FOR loop over PIN().  Eight data lines
 *	written one at a time are eight different values on the bus, and
 *	a latch or a display watching them sees all eight.  So the bits
 *	are accumulated into a mask and a value, and pc3_port_put posts
 *	the difference as a single OUT_XOR per bank - MMBasic's
 *	gpio_xor_mask64, arrived at the same way.  A port that spans
 *	GP31/GP32 takes one store per bank; the hardware has no single
 *	register covering both.
 *
 *	The read is one snapshot too: pc3_pins_in and pc3_pins_out are
 *	sampled once and the bits picked out afterwards, so a bus cannot
 *	be caught half way through a change.
 *
 *	Pin validation is MMBasic's: writing needs every pin to be a
 *	DOUT, reading takes a DIN, a DOUT (which reads back what it is
 *	driving) or any of the interrupt modes.
 */

#include "mmb_runtime.h"
#include "mmb_gpio.h"

#define MMG_PORT_MAXG	8		/* groups; MMBasic allows NBRPINS */

/*
 *	THE GROUPS ARRIVE ONE CALL AT A TIME, and that shape is forced by
 *	the compiler this has to run on.  The obvious C for a variable
 *	number of pairs is a compound literal - mmg_port_put((MMINTEGER[])
 *	{ 0, 8 }, 1, v) - and it is what the first version emitted.  It
 *	compiles under gcc, passes every host gate, and the board's cc
 *	rejects it: FCC HAS NO COMPOUND LITERALS.  The translator already
 *	knew that for array bounds (mmb2c.py hoists a static table there),
 *	but that trick needs compile-time constants and a pin number can
 *	be any expression.
 *
 *	So the pairs are written into a small table first and the count
 *	passed after.  As a statement that is three plain calls; inside an
 *	expression it is a comma sequence, which C sequences left to right:
 *
 *	    (mmg_port_group(0, 0, 4), mmg_port_group(1, 8, 4),
 *	     mmg_port_get(2))
 *
 *	One table, so a PORT nested inside another PORT's arguments would
 *	overwrite the outer one's groups.  Nothing writes that and MMBasic
 *	has the same one-shot argument buffer, but it is a limit rather
 *	than an oversight.
 */
static MMINTEGER mmg_port_tab[MMG_PORT_MAXG * 2];

MMG_FN void mmg_port_group(MMINTEGER idx, MMINTEGER pin, MMINTEGER nbits)
{
	if (idx < 0 || idx >= MMG_PORT_MAXG) {
		mm_error("Invalid PORT group count");
		return;
	}
	mmg_port_tab[idx * 2] = pin;
	mmg_port_tab[idx * 2 + 1] = nbits;
}

MMG_FN void mmg_port_put(MMINTEGER ngroups, MMINTEGER value)
{
	unsigned long long mask = 0, val = 0;
	MMINTEGER v = value;
	int i, k, pin, nbits;

	if (ngroups < 1 || ngroups > MMG_PORT_MAXG) {
		mm_error("Invalid PORT group count");
		return;
	}
	/* Check every pin BEFORE driving any of them: half a bus written
	   and then an error is worse than the error alone. */
	for (i = 0; i < (int)ngroups; i++) {
		pin = (int)mmg_port_tab[i * 2];
		nbits = (int)mmg_port_tab[i * 2 + 1];
		if (nbits < 0) {
			mm_error("Invalid PORT bit count");
			return;
		}
		for (k = 0; k < nbits; k++) {
			int p = pin + k;

			if (p < 0 || p >= MM_GPIO_NPINS ||
			    mmg_mode[p] != MMG_PIN_DOUT) {
				mm_error("Invalid output pin");
				return;
			}
		}
	}

	for (i = 0; i < (int)ngroups; i++) {
		pin = (int)mmg_port_tab[i * 2];
		nbits = (int)mmg_port_tab[i * 2 + 1];
		for (k = 0; k < nbits; k++) {
			int p = pin + k;

			mask |= 1ULL << p;
			if (v & 1)
				val |= 1ULL << p;
			v >>= 1;
		}
	}
	pc3_port_put(mask, val);
}

MMG_FN MMINTEGER mmg_port_get(MMINTEGER ngroups)
{
	unsigned long long in, out;
	MMINTEGER value = 0;
	int i, k, pin, nbits;

	if (ngroups < 1 || ngroups > MMG_PORT_MAXG) {
		mm_error("Invalid PORT group count");
		return 0;
	}
	/*	Both banks of both registers sampled before anything is
	 *	picked out of them, so every bit of the answer belongs to
	 *	the same instant.  MMBasic's fun_port opens the same way.
	 *	An output pin reads back what it is being DRIVEN to (the
	 *	latch) rather than what the pad is at, which is what makes
	 *	a shorted or loaded output still read as the program set
	 *	it. */
	in = pc3_pins_in();
	out = pc3_pins_out();
	/* Backwards through the groups, and from the top pin down inside
	   each - fun_port's order, so this undoes mmg_port_put. */
	for (i = (int)ngroups - 1; i >= 0; i--) {
		pin = (int)mmg_port_tab[i * 2];
		nbits = (int)mmg_port_tab[i * 2 + 1];
		if (nbits < 0) {
			mm_error("Invalid PORT bit count");
			return 0;
		}
		for (k = nbits - 1; k >= 0; k--) {
			int p = pin + k;
			int m;

			if (p < 0 || p >= MM_GPIO_NPINS) {
				mm_error("Invalid input pin");
				return 0;
			}
			m = mmg_mode[p];
			if (m != MMG_PIN_DIN && m != MMG_PIN_DOUT &&
			    m != MMG_PIN_INTH && m != MMG_PIN_INTL &&
			    m != MMG_PIN_INTB) {
				mm_error("Invalid input pin");
				return 0;
			}
			value <<= 1;
			value |= ((m == MMG_PIN_DOUT ? out : in) >>
				  p) & 1ULL;
		}
	}
	return value;
}

#endif /* MMB_PORT_H */
