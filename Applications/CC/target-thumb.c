/*
 *	Compiler pass support for ARM Thumb (Cortex-M)
 *
 *	ILP32 to match the gcc-built userland this has to share headers
 *	with: short 16, int 32, long 32, long long 64, pointers 32.
 *	See PC3-COMPILER-PLAN.md.
 */

#include "compiler.h"


/* Size of primitive types for this target */
static unsigned sizetab[16] = {
	1, 2, 4, 8,		/* char, short, long, longlong */
	1, 2, 4, 8,		/* unsigned forms */
	4, 8, 0, 0,		/* float, double, void, unused.. */
	0, 0, 0, 0		/* unused */
};

unsigned target_sizeof(unsigned t)
{
	unsigned s;

	if (PTR(t))
		return 4;

	s = sizetab[(t >> 4) & 0x0F];
	if (s == 0) {
		error("cannot size type");
		s = 1;
	}
	return s;
}

/*
 *	AAPCS alignment: naturally aligned up to 8 bytes. Arguments occupy
 *	whole words.
 *
 *	This is called for aggregates as well as simple types, and
 *	target_sizeof only understands simple ones, so decode the type
 *	rather than going via the size. Aggregates get word alignment,
 *	which is safe but over-aligns a struct of chars compared to gcc.
 *	That only matters once we share structs with gcc-built objects,
 *	which is out of scope at present.
 */
unsigned target_alignof(unsigned t, unsigned storage)
{
	if (storage == S_ARGUMENT)
		return 4;
	if (PTR(t))
		return 4;
	if (!IS_SIMPLE(t))
		return 4;
	switch (BASE_TYPE(t)) {
	case CCHAR:
	case UCHAR:
		return 1;
	case CSHORT:
	case USHORT:
		return 2;
	case CLONGLONG:
	case ULONGLONG:
	case DOUBLE:
		return 8;
	default:
		return 4;
	}
}

/*
 *	Arguments are passed in whole words.
 *
 *	A struct or union argument is copied onto the stack whole, so its
 *	size has to come from type_sizeof - the struct index in this
 *	pass's symbol table - because target_sizeof only knows the
 *	primitives and calls error() on anything else.
 *
 *	This must agree exactly with what gen_push() adds to the code
 *	generator's stack depth, since the total lands in T_CLEANUP and a
 *	disagreement shows up as "sp" at the epilogue rather than as bad
 *	code.
 */
unsigned target_argsize(unsigned t)
{
	unsigned s;

	if (!PTR(t) && IS_STRUCT(t))
		s = type_sizeof(t);
	else
		s = target_sizeof(t);
	if (s < 4)
		return 4;
	return (s + 3) & ~3;
}

/* integer type for a pointer of type t. For most platforms this is trivial
   but strange boxes with word addressing and byte pointers may need help */
unsigned target_ptr_arith(unsigned t)
{
	return CINT;
}

/* Adjust scaling for a pointer of type t. Byte addressed, so a no-op. */
unsigned target_scale_ptr(unsigned t, unsigned scale)
{
	return scale;
}

/* Remap any base types for simplicity on the platform */

unsigned target_type_remap(unsigned type)
{
	/*
	 * Every other target folds DOUBLE onto FLOAT, so code using double
	 * silently gets single precision. We do not: the machine has 64bit
	 * integers to build on and the literal encoder in cc0 produces
	 * real IEEE754 doubles, so a double here is eight bytes and 53
	 * bits of mantissa. TARGET_HAS_DOUBLE in target.h is the other
	 * half of this and the two have to agree.
	 */
	return type;
}

/*
 *	Register variables. The machine has r4-r7 going spare under
 *	AAPCS, but nothing generates code yet and an unused hint would
 *	only mislead the backend, so decline for now.
 */

unsigned target_register(unsigned type)
{
	return 0;
}

void target_reginit(void)
{
}
