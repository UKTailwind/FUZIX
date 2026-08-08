#ifdef CPU_armm0

/*
 *	ARM Cortex-M. ILP32: short 16, int 32, long 32, long long 64,
 *	pointers 32. Note that int and long therefore share a type code -
 *	C does not require them to be distinct types, only that long is
 *	at least as wide, and the compiler's type encoding has one slot
 *	per width.
 *
 */
#define TARGET_MAX_INT		2147483647L
#define TARGET_MAX_LONG		2147483647UL
#define TARGET_MAX_UINT		4294967295UL
#define TARGET_MAX_PTR		TARGET_MAX_UINT

/*
 *	Double is a real 64bit double here, not an alias for float. cc0
 *	emits T_DOUBLEVAL for an unsuffixed floating constant, and
 *	target_type_remap() has to leave DOUBLE alone to match.
 */
#define TARGET_HAS_DOUBLE

/*
 *	Structs and unions may be passed by value. The other targets keep
 *	the refusal in type_iterator.c: accepting the declaration without
 *	a backend that copies the object would turn a loud error into a
 *	silent miscompile, which is much the worse of the two.
 */
#define TARGET_HAS_STRUCTARG

/*
 *	The width a constant is carried at through cc1 and cc2.
 *
 *	This target has long long and double, so it needs 64 bits.
 *	Everything used to say "unsigned long", which is 64 bits on the
 *	x86-64 development host and 32 on the board, so cross compiled
 *	code was right and the same source compiled on the machine itself
 *	silently lost the top half: 5000000000LL came out as 705032704.
 *	It was correct by accident, and only on one of the two machines it
 *	has to be correct on.
 *
 *	Other targets keep the narrow form. Four more bytes on every node
 *	is not free on an 8bit machine, and none of them have a 64 bit
 *	type to carry.
 */
typedef unsigned long long cval_t;
typedef signed long long scval_t;

#else

typedef unsigned long cval_t;
typedef signed long scval_t;

#endif

#ifndef CPU_armm0

#define TARGET_MAX_INT		32767L
#define TARGET_MAX_LONG		2147483647UL	/* and a double persenne prime too */
#define TARGET_MAX_UINT		65535UL
#define TARGET_MAX_PTR		TARGET_MAX_UINT

#endif

#define TARGET_CHAR_MASK	0x00FFU
#define TARGET_SHORT_MASK	0xFFFFU
#define TARGET_LONG_MASK	0xFFFFFFFFUL

extern unsigned target_sizeof(unsigned t);
extern unsigned target_alignof(unsigned t, unsigned storage);
extern unsigned target_argsize(unsigned t);
extern unsigned target_ptr_arith(unsigned t);
extern unsigned target_scale_ptr(unsigned t, unsigned scale);
extern unsigned target_type_remap(unsigned t);
extern unsigned target_register(unsigned t);
extern void target_reginit(void);

#ifdef CPU_armm0

/* Integer type is 4 byte, and is the same width as long */
#define CINT	CLONG
#define UINT	ULONG

#else

/* Default integer type is 2 byte */
#define CINT	CSHORT
#define UINT	USHORT

#endif

/* gcc's default for ARM is an unsigned plain char and Target/rules.armm0
   does not override it, so this suits both. */
#define TARGET_CHAR_UNSIGNED
