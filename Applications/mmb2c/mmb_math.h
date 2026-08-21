#ifndef MMB_MATH_H
#define MMB_MATH_H
/*
 *	MATH C_ADD / C_SUB / C_MUL / C_DIV / C_AND / C_OR / C_XOR
 *
 *	    MATH C_ADD a(), b(), c()      c(i) = a(i) + b(i)
 *
 *	Element by element between two arrays into a third.  The "C_" is
 *	for component, and MMBasic implements all seven in cmd_math
 *	(core/MATHS.c) and only there - ARRAY C_ADD is a syntax error in
 *	MMBasic, and the translator refuses it here for the same reason.
 *
 *	WHY A HEADER rather than the runtime.  Seven operations over two
 *	types is fourteen one-line loops.  As runtime entry points they
 *	would be fourteen more wrappers in bcrun and fourteen more names
 *	in its lookup table, carried by every program on the machine
 *	whether or not it ever says C_ADD - and bcrun is the floor under
 *	what a BASIC program has left to work in.  Here the code is
 *	compiled into the program that asked for it, exactly as the
 *	graphics and pin headers are, and costs everything else nothing.
 *
 *	All three arrays must be the same length and the same type.  The
 *	type is settled by the translator, which refuses to mix them and
 *	refuses strings; the length is a run-time fact, so it is tested
 *	here.  "Array size mismatch" is MMBasic's own wording for it
 *	(StandardError(16), from parsearrays).
 *
 *	NO DIVISION CHECK, and that is deliberate.  Everywhere else a
 *	divide tests its divisor first - mm_idiv and mm_fdiv both raise
 *	"Divide by zero", because op_div does, and because every MMBasic
 *	error is a check that runs first.  C_DIV is the exception in
 *	MMBasic itself: cmd_math divides with a bare C '/' and tests
 *	nothing, so a zero divisor gives IEEE infinity in a float array
 *	and whatever the division does in an integer one.  Adding the
 *	check here would stop a program that runs on a real PicoMite,
 *	which is the divergence that matters more than the tidiness.
 *
 *	The switch is outside the loop, as MMBasic's separate loops are:
 *	these run over whole arrays, and a test per element would be paid
 *	on every one of them.
 */

#ifndef MMG_FN
#if defined(MM_FCC) || defined(MM_PC3)
#define MMG_FN static
#else
#define MMG_FN static __inline__ __attribute__((unused))
#endif
#endif

MMG_FN void mmg_carr_i(const MMINTEGER *a, int na, const MMINTEGER *b,
                       int nb, MMINTEGER *c, int nc, int op)
{
	int i;

	if (na != nb || na != nc)
		MM_RAISE("Array size mismatch");
	switch (op) {
	case '+': for (i = 0; i < na; i++) c[i] = a[i] + b[i]; break;
	case '-': for (i = 0; i < na; i++) c[i] = a[i] - b[i]; break;
	case '*': for (i = 0; i < na; i++) c[i] = a[i] * b[i]; break;
	case '/': for (i = 0; i < na; i++) c[i] = a[i] / b[i]; break;
	case '&': for (i = 0; i < na; i++) c[i] = a[i] & b[i]; break;
	case '|': for (i = 0; i < na; i++) c[i] = a[i] | b[i]; break;
	default:  for (i = 0; i < na; i++) c[i] = a[i] ^ b[i]; break;
	}
}

MMG_FN void mmg_carr_f(const MMFLOAT *a, int na, const MMFLOAT *b,
                       int nb, MMFLOAT *c, int nc, int op)
{
	int i;

	if (na != nb || na != nc)
		MM_RAISE("Array size mismatch");
	switch (op) {
	case '+': for (i = 0; i < na; i++) c[i] = a[i] + b[i]; break;
	case '-': for (i = 0; i < na; i++) c[i] = a[i] - b[i]; break;
	case '*': for (i = 0; i < na; i++) c[i] = a[i] * b[i]; break;
	case '/': for (i = 0; i < na; i++) c[i] = a[i] / b[i]; break;
	/* Through 64-bit integers and back, which is what MMBasic does
	   with the bitwise three on a float array. */
	case '&': for (i = 0; i < na; i++)
			c[i] = (MMFLOAT)((MMINTEGER)a[i] & (MMINTEGER)b[i]);
		  break;
	case '|': for (i = 0; i < na; i++)
			c[i] = (MMFLOAT)((MMINTEGER)a[i] | (MMINTEGER)b[i]);
		  break;
	default:  for (i = 0; i < na; i++)
			c[i] = (MMFLOAT)((MMINTEGER)a[i] ^ (MMINTEGER)b[i]);
		  break;
	}
}

#endif /* MMB_MATH_H */
