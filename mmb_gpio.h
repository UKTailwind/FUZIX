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
 *	What crosses into the kernel is one call, mm_gpio - the on-board
 *	cc has no ioctl, so the crossing itself cannot live in a header.
 *	Everything else - the pin check, the mode names, the errors - is
 *	arithmetic and belongs on this side.
 *
 *	MMBasic's SETPIN has many more modes than these two, and its own
 *	pin NUMBERING (connector pins, not GPIO numbers).  This is the
 *	GPIO number, because that is what the PC3's documentation, its
 *	schematic and every other tool on the machine use, and inventing
 *	a second numbering for one command would be worse than the small
 *	incompatibility.
 */

#include "mmb_runtime.h"

/*	SETPIN's modes.  The numbers are ours - MMBasic's tokens are
 *	words, and the translator turns them into these. */
#define MMG_PIN_DIN	0
#define MMG_PIN_DOUT	1

static void mmg_setpin(MMINTEGER pin, MMINTEGER mode)
{
	if (pin < 0 || pin >= MM_GPIO_NPINS)
		mm_error("Invalid pin");
	else if (mm_gpio(MM_GPIO_DIR, pin,
			 mode == MMG_PIN_DOUT ? 1 : 0) < 0)
		mm_error("Pin cannot do that");
}

/*	PIN(n) = v.  Kept separate from the read so that a program doing
 *	only one of them carries only that one. */
static void mmg_pin_put(MMINTEGER pin, MMINTEGER val)
{
	if (pin < 0 || pin >= MM_GPIO_NPINS)
		mm_error("Invalid pin");
	else
		mm_gpio(MM_GPIO_PUT, pin, val ? 1 : 0);
}

static MMINTEGER mmg_pin_get(MMINTEGER pin)
{
	MMINTEGER v;

	if (pin < 0 || pin >= MM_GPIO_NPINS) {
		mm_error("Invalid pin");
		return 0;
	}
	v = mm_gpio(MM_GPIO_GET, pin, 0);
	return v < 0 ? 0 : v;
}

#endif /* MMB_GPIO_H */
