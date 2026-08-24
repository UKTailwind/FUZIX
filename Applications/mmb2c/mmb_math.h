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

/*
 *	MATH(BASE64 ENCODE in$, out$)  and  MATH(BASE64 DECODE in$, out$)
 *
 *	The function returns the encoded/decoded LENGTH and writes the
 *	result string into the second argument - fun_math's own odd call
 *	shape (core/Maths.c BASE64), kept because that is how a WebMite
 *	program writes it: retic.bas does
 *	    Local integer n = Math(base64 encode si, base64Encode)
 *	inside a Function, the output argument being the function's own
 *	result variable.
 *
 *	The loops are b64_encode/b64_decode from Maths.c to the letter,
 *	including the parts one might be tempted to tidy: DECODE maps an
 *	unknown input character to 0 rather than erroring (b64_int's
 *	final return), '=' is value 64 and shortens the group, and a
 *	trailing partial group of fewer than 4 characters is silently
 *	ignored.  The size gate is the reference's too: computed from the
 *	input length BEFORE the loop runs ("Output exceeds string size"),
 *	so DECODE of a padded group can deliver less than it reserved.
 *
 *	A scratch buffer, not in-place: MATH(BASE64 ENCODE a$, a$) is
 *	legal, and the reference also encodes into temp memory first.
 *	The translator passes the target's capacity - 255 for a plain
 *	string variable, the LENGTH of a bounded array element - and the
 *	NUL after the data keeps the every-string-has-a-NUL invariant
 *	when there is room for it, exactly as mm_ssetm does.
 */

MMG_FN MMINTEGER mmg_b64_enc(const char *in, char *out, int cap)
{
	static const char *chr =
	    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
	    "0123456789+/";
	unsigned char buf[344];		/* b64e_size(255) = 340 */
	int n = (unsigned char)in[0];
	const unsigned char *p = (const unsigned char *)in + 1;
	int i, j = 0, k = 0;
	unsigned int s[3];

	if (4 * ((n + 2) / 3) > cap)
		MM_RAISEV("Output exceeds string size", 0);
	for (i = 0; i < n; i++) {
		s[j++] = p[i];
		if (j == 3) {
			buf[k + 0] = chr[(s[0] & 255) >> 2];
			buf[k + 1] = chr[((s[0] & 0x03) << 4)
					 + ((s[1] & 0xF0) >> 4)];
			buf[k + 2] = chr[((s[1] & 0x0F) << 2)
					 + ((s[2] & 0xC0) >> 6)];
			buf[k + 3] = chr[s[2] & 0x3F];
			j = 0;
			k += 4;
		}
	}
	if (j) {
		if (j == 1)
			s[1] = 0;
		buf[k + 0] = chr[(s[0] & 255) >> 2];
		buf[k + 1] = chr[((s[0] & 0x03) << 4) + ((s[1] & 0xF0) >> 4)];
		if (j == 2)
			buf[k + 2] = chr[(s[1] & 0x0F) << 2];
		else
			buf[k + 2] = '=';
		buf[k + 3] = '=';
		k += 4;
	}
	out[0] = (char)(unsigned char)k;
	for (i = 0; i < k; i++)	/* not memcpy: this header includes no */
		out[i + 1] = (char)buf[i];	/* libc headers of its own */
	if (k < cap)
		out[k + 1] = 0;
	return k;
}

/* b64_int: ASCII to the 6-bit value, '=' to 64, anything else to 0. */
MMG_FN int mmg_b64_int(int ch)
{
	if (ch == 43)
		return 62;
	if (ch == 47)
		return 63;
	if (ch == 61)
		return 64;
	if (ch > 47 && ch < 58)
		return ch + 4;
	if (ch > 64 && ch < 91)
		return ch - 'A';
	if (ch > 96 && ch < 123)
		return (ch - 'a') + 26;
	return 0;
}

MMG_FN MMINTEGER mmg_b64_dec(const char *in, char *out, int cap)
{
	unsigned char buf[344];
	int n = (unsigned char)in[0];
	const unsigned char *p = (const unsigned char *)in + 1;
	int i, j = 0, k = 0;
	unsigned int s[4];

	if ((3 * n) / 4 > cap)
		MM_RAISEV("Output exceeds string size", 0);
	for (i = 0; i < n; i++) {
		s[j++] = (unsigned int)mmg_b64_int(p[i]);
		if (j == 4) {
			buf[k + 0] = (unsigned char)(((s[0] & 255) << 2)
						     + ((s[1] & 0x30) >> 4));
			if (s[2] != 64) {
				buf[k + 1] = (unsigned char)
				    (((s[1] & 0x0F) << 4)
				     + ((s[2] & 0x3C) >> 2));
				if (s[3] != 64) {
					buf[k + 2] = (unsigned char)
					    (((s[2] & 0x03) << 6) + s[3]);
					k += 3;
				} else {
					k += 2;
				}
			} else {
				k += 1;
			}
			j = 0;
		}
	}
	out[0] = (char)(unsigned char)k;
	for (i = 0; i < k; i++)
		out[i + 1] = (char)buf[i];
	if (k < cap)
		out[k + 1] = 0;
	return k;
}


/*
 *	MATH SHIFT / POWER / V_NORMALISE / V_CROSS / V_PRINT / M_PRINT,
 *	and the MATH() functions MAGNITUDE and DOTPRODUCT.
 *
 *	The same bargain as the C_ operations above: pure arithmetic over
 *	whole arrays, compiled into the program that asks for one and
 *	costing every other program nothing.
 *
 *	Every error string here came out of a real MMBasic 6.03.02, not
 *	out of the source - run the failing statement under ON ERROR SKIP
 *	and print MM.ERRMSG$.  "Size mismatch" for SHIFT and "Array size
 *	mismatch" for the rest is not a typo: cmd_math really does use
 *	error() for one and StandardError(16) for the others.
 *
 *	The TYPE errors MMBasic raises here ("Argument 1 must be an
 *	integer array", "... a floating point array") are the translator's
 *	job rather than this header's: it knows the types before the
 *	program runs, so it refuses in its own words instead of emitting
 *	a check that can never fire.
 */

/* <math.h> is included AFTER this header, so declare what is used here
 * rather than let it be guessed: an implicit declaration makes sqrt()
 * return int, which is not a warning but a wrong answer - and cc1 says
 * so out loud when math.h's real declaration arrives ("type mismatch"),
 * which is how the missing sin/cos here were found.  Anything this
 * header calls out of libm belongs in this list. */
double sqrt(double);
double pow(double, double);
double sin(double);
double cos(double);

/* No <string.h> here either - see the note on mmg_carr_i above. */
MMG_FN char *mmg_mcpy(char *d, const char *s)
{
	while ((*d = *s++) != 0)
		d++;
	return d;
}

MMG_FN void mmg_puts(const char *s)
{
	while (*s)
		mm_putc((unsigned char)*s++);
}

/*	MATH SHIFT a%(), n, b%() [, "U"]
 *
 *	b(i) = a(i) << n, or >> -n when n is negative.  The optional "U"
 *	makes a right shift logical rather than arithmetic; MMBasic looks
 *	at it only when the shift is negative, and so does this.
 *
 *	The value is cast through uint64_t on the way LEFT in the
 *	reference as well - shifting a negative int64_t left is undefined
 *	in C, and this is one of the places the firmware got that right.
 */
MMG_FN void mmg_shift(const MMINTEGER *a, int na, MMINTEGER sh,
                      MMINTEGER *b, int nb, int unsgn)
{
	int i;

	if (sh < -63 || sh > 63) {
		char m[64];
		char *q = m;

		mm_int_to_str(q, (long long)sh, 10);
		while (*q)
			q++;
		mmg_mcpy(q, " is invalid (valid is -63 to 63)");
		MM_RAISE(m);
	}
	if (na != nb)
		MM_RAISE("Size mismatch");
	if (sh > 0)
		for (i = 0; i < na; i++)
			b[i] = (MMINTEGER)(((unsigned long long)a[i]) << sh);
	else if (unsgn)
		for (i = 0; i < na; i++)
			b[i] = (MMINTEGER)(((unsigned long long)a[i]) >> (-sh));
	else
		for (i = 0; i < na; i++)
			b[i] = a[i] >> (-sh);
}

/*	MATH POWER a(), n, b()      b(i) = a(i) ^ n
 *
 *	Two things copied from cmd_math that look like mistakes and are
 *	not:
 *
 *	  - n == 1.0 is a COPY, not pow(x, 1).  The reference branches on
 *	    it, and pow() is not required to return x exactly.
 *	  - into an INTEGER array the exponent is rounded to a whole
 *	    number FIRST, so POWER a%(), 2.7, b%() cubes rather than
 *	    raising to 2.7 and rounding the result.  Into a float array
 *	    the exponent is used as written.
 *
 *	Both arrays must be the same type, which is ARRAY ADD's rule here
 *	rather than MMBasic's - the reference converts between them.
 */
MMG_FN void mmg_pow_f(const MMFLOAT *a, int na, MMFLOAT p,
                      MMFLOAT *b, int nb)
{
	int i;

	if (na != nb)
		MM_RAISE("Array size mismatch");
	if (p == 1.0)
		for (i = 0; i < na; i++)
			b[i] = a[i];
	else
		for (i = 0; i < na; i++)
			b[i] = pow(a[i], p);
}

MMG_FN void mmg_pow_i(const MMINTEGER *a, int na, MMFLOAT p,
                      MMINTEGER *b, int nb)
{
	MMFLOAT e;
	int i;

	if (na != nb)
		MM_RAISE("Array size mismatch");
	if (p == 1.0) {
		for (i = 0; i < na; i++)
			b[i] = a[i];
		return;
	}
	e = (MMFLOAT)mm_toint(p);
	for (i = 0; i < na; i++)
		b[i] = mm_toint(pow((MMFLOAT)a[i], e));
}

/*	MATH(MAGNITUDE a())         sqrt of the sum of the squares
 *
 *	Any rank: parsefloatarray is called with 0 for its dimension
 *	count, so a 2-D array is legal and is read as the flat vector it
 *	is.
 */
MMG_FN MMFLOAT mmg_magnitude(const MMFLOAT *a, int n)
{
	MMFLOAT m = 0.0;
	int i;

	for (i = 0; i < n; i++)
		m += a[i] * a[i];
	return sqrt(m);
}

/*	MATH(DOTPRODUCT a(), b())   one-dimensional, equal length */
MMG_FN MMFLOAT mmg_dot(const MMFLOAT *a, int na, const MMFLOAT *b, int nb)
{
	MMFLOAT d = 0.0;
	int i;

	if (na != nb)
		MM_RAISEV("Array size mismatch", 0.0);
	for (i = 0; i < na; i++)
		d += a[i] * b[i];
	return d;
}

/*	MATH V_NORMALISE a(), b()   b = a / |a|
 *
 *	NO ZERO CHECK, and deliberately: the reference divides by the
 *	magnitude with a bare '/', so an all-zero vector gives IEEE
 *	infinities there and gives them here.  Adding the check would
 *	stop a program that runs on a real PicoMite.
 *
 *	The magnitude is summed over the whole source before anything is
 *	written, which is what lets b() and a() be the same array.
 */
MMG_FN void mmg_vnorm(const MMFLOAT *a, int na, MMFLOAT *b, int nb)
{
	MMFLOAT m = 0.0;
	int i;

	if (na != nb)
		MM_RAISE("Array size mismatch");
	for (i = 0; i < na; i++)
		m += a[i] * a[i];
	m = sqrt(m);
	for (i = 0; i < na; i++)
		b[i] = a[i] / m;
}

/*	MATH V_CROSS a(), b(), c()  c = a x b, three elements each
 *
 *	a and b are copied out first, so the destination may be either
 *	source - cmd_math does the same, and a program that writes
 *	V_CROSS a(), b(), a() depends on it.
 */
MMG_FN void mmg_vcross(const MMFLOAT *a, int na, const MMFLOAT *b, int nb,
                       MMFLOAT *c, int nc)
{
	MMFLOAT u[3], v[3];
	int i;

	if (na != 3)
		MM_RAISE("Argument 1 must be a 3 element floating point array");
	if (nb != 3)
		MM_RAISE("Argument 2 must be a 3 element floating point array");
	if (nc != 3)
		MM_RAISE("Argument 3 must be a 3 element floating point array");
	for (i = 0; i < 3; i++) {
		u[i] = a[i];
		v[i] = b[i];
	}
	c[0] = u[1] * v[2] - u[2] * v[1];
	c[1] = u[2] * v[0] - u[0] * v[2];
	c[2] = u[0] * v[1] - u[1] * v[0];
}

/*	MATH V_PRINT a() [, HEX]  and  MATH M_PRINT a()
 *
 *	The formats are PFlt and PInt from PicoMite.c: a float through
 *	FloatToStr(s, v, 4, 4, ' '), an integer through IntToStr in base
 *	10 or base 16, ", " between them and CRLF at the end.  They go
 *	straight out rather than through PRINT, so the console's column
 *	tracking is not involved - which is what the reference does too.
 *
 *	M_PRINT walks a row at a time.  A row is contiguous here because
 *	the first BASIC subscript is the adjacent one, so `stride` is the
 *	size of the FIRST dimension: equal to nc under OPTION BASE 0, and
 *	one more than it under BASE 1, where our arrays keep an
 *	unreachable element 0 and MMBasic's do not.
 */
MMG_FN void mmg_pflt(MMFLOAT v)
{
	char b[80];

	mm_float_to_str(b, v, 4, 4, ' ');
	mmg_puts(b);
}

MMG_FN void mmg_pint(MMINTEGER v, int base)
{
	char b[80];

	mm_int_to_str(b, (long long)v, base);
	mmg_puts(b);
}

MMG_FN void mmg_vprint_f(const MMFLOAT *a, int n)
{
	int i;

	for (i = 0; i < n; i++) {
		if (i)
			mmg_puts(", ");
		mmg_pflt(a[i]);
	}
	mmg_puts("\r\n");
}

MMG_FN void mmg_vprint_i(const MMINTEGER *a, int n, int base)
{
	int i;

	for (i = 0; i < n; i++) {
		if (i)
			mmg_puts(", ");
		mmg_pint(a[i], base);
	}
	mmg_puts("\r\n");
}

MMG_FN void mmg_mprint_f(const MMFLOAT *a, int nc, int nr, int stride)
{
	int i, j;

	for (i = 0; i < nr; i++) {
		for (j = 0; j < nc; j++) {
			if (j)
				mmg_puts(", ");
			mmg_pflt(a[i * stride + j]);
		}
		mmg_puts("\r\n");
	}
}

MMG_FN void mmg_mprint_i(const MMINTEGER *a, int nc, int nr, int stride)
{
	int i, j;

	for (i = 0; i < nr; i++) {
		for (j = 0; j < nc; j++) {
			if (j)
				mmg_puts(", ");
			mmg_pint(a[i * stride + j], 10);
		}
		mmg_puts("\r\n");
	}
}

/*
 *	MATH M_TRANSPOSE / M_MULT / M_INVERSE / V_MULT / V_ROTATE, and
 *	the MATH() function M_DETERMINANT - the matrix family.
 *
 *	These read a 2-D array FLAT, and since the storage-order change
 *	our flat order IS MMBasic's, so each is a transcription of
 *	cmd_math rather than a strided rewrite.  MMBasic's own names are
 *	kept: dims[0] is the COLUMN count and dims[1] the row count, so
 *	the FIRST subscript is the column, a(col, row), and a row is
 *	contiguous.  A program that writes a(row, col) gets the transpose
 *	of what it meant - on a real PicoMite too.
 *
 *	THE DETERMINANT IS THE REFERENCE'S, cofactor expansion and all.
 *	It is O(n!) where an LU factorisation would be O(n^3), but the
 *	two do not round alike and a determinant is something a program
 *	PRINTS.  Being the same number matters more here than being the
 *	faster one, and nobody inverts a large matrix this way.
 *
 *	Hence MMG_MDIM.  The reference has no size limit because it
 *	allocates as it recurses; a header cannot, so there is a bound,
 *	and 8 is past the point where the algorithm is the problem - an
 *	8x8 inverse is 64 cofactors of 5040 expansions each.
 */

#define MMG_MDIM 8

/*	Scratch layout is the reference's: m[a * n + b] is (column a, row
 *	b), which is `matrix[a][b]` there.  The load transposes into it
 *	because cmd_math's does (`matrix[j][i] = *a1float++`), and the
 *	arithmetic order follows from that - which is what keeps the last
 *	bits the same.
 */
MMG_FN void mmg_m_load(const MMFLOAT *src, MMFLOAT *m, int n)
{
	int i, j;

	for (i = 0; i < n; i++)
		for (j = 0; j < n; j++)
			m[j * n + i] = src[i * n + j];
}

/*	determinant(), transcribed.  The minor is built by the
 *	reference's own running (m, n) walk rather than by index
 *	arithmetic, so the terms are summed in the same order.
 *
 *	The reference recurses on an array of ROW POINTERS, so its minor
 *	can sit in the top-left of a wider block and be read as if it
 *	were its own size.  A flat array has no such freedom, so the
 *	minor is repacked to its own width before the call - a copy the
 *	reference does not make, and no arithmetic either way.
 */
MMG_FN MMFLOAT mmg_det(const MMFLOAT *matrix, int size)
{
	MMFLOAT m_minor[MMG_MDIM * MMG_MDIM];
	MMFLOAT packed[MMG_MDIM * MMG_MDIM];
	MMFLOAT s = 1, det = 0;
	int i, j, m, n, c;

	if (size == 1)
		return matrix[0];
	for (c = 0; c < size; c++) {
		m = 0;
		n = 0;
		for (i = 0; i < size; i++) {
			for (j = 0; j < size; j++) {
				m_minor[i * MMG_MDIM + j] = 0;
				if (i != 0 && j != c) {
					m_minor[m * MMG_MDIM + n] =
						matrix[i * size + j];
					if (n < size - 2) {
						n++;
					} else {
						n = 0;
						m++;
					}
				}
			}
		}
		for (i = 0; i < size - 1; i++)
			for (j = 0; j < size - 1; j++)
				packed[i * (size - 1) + j] =
					m_minor[i * MMG_MDIM + j];
		det = det + s * (matrix[c] * mmg_det(packed, size - 1));
		s = -1 * s;
	}
	return det;
}

/*	cofactor() and the transpose() it ends with, which together give
 *	the INVERSE rather than the cofactor matrix - the reference's
 *	shape, kept. */
MMG_FN void mmg_cofactor(const MMFLOAT *matrix, MMFLOAT *newmatrix, int size)
{
	MMFLOAT m_cofactor[MMG_MDIM * MMG_MDIM];
	MMFLOAT matrix_cofactor[MMG_MDIM * MMG_MDIM];
	MMFLOAT packed[MMG_MDIM * MMG_MDIM];
	MMFLOAT d;
	int p, q, m, n, i, j;

	for (q = 0; q < size; q++) {
		for (p = 0; p < size; p++) {
			m = 0;
			n = 0;
			for (i = 0; i < size; i++) {
				for (j = 0; j < size; j++) {
					if (i != q && j != p) {
						m_cofactor[m * MMG_MDIM + n] =
							matrix[i * size + j];
						if (n < size - 2) {
							n++;
						} else {
							n = 0;
							m++;
						}
					}
				}
			}
			for (i = 0; i < size - 1; i++)
				for (j = 0; j < size - 1; j++)
					packed[i * (size - 1) + j] =
						m_cofactor[i * MMG_MDIM + j];
			matrix_cofactor[q * size + p] =
				pow(-1, q + p) * mmg_det(packed, size - 1);
		}
	}
	d = mmg_det(matrix, size);
	for (i = 0; i < size; i++)
		for (j = 0; j < size; j++)
			newmatrix[i * size + j] =
				matrix_cofactor[j * size + i] / d;
}

/*	MATH M_TRANSPOSE a(), b()
 *
 *	b's columns are a's rows and b's rows are a's columns.  Written
 *	directly rather than through the reference's two scratch copies:
 *	a move, not arithmetic, so nothing can round differently. */
MMG_FN void mmg_mtrans(const MMFLOAT *a, int c1, int r1,
                       MMFLOAT *b, int c2, int r2)
{
	int i, j;

	if (c2 != r1 || r2 != c1)
		MM_RAISE("Array size mismatch");
	for (i = 0; i < r2; i++)
		for (j = 0; j < c2; j++)
			b[i * c2 + j] = a[j * c1 + i];
}

/*	MATH M_MULT a(), b(), c()      c = a x b
 *
 *	b's rows must equal a's columns; c is b's columns by a's rows.
 *	The destination is refused as either source, which is the
 *	reference's own check - the sum is accumulated in place.
 *
 *	A ONE ELEMENT ANSWER is legal here and is not on a real PicoMite.
 *	MMBasic keeps an array's rank in the same table entry as its
 *	bounds, where 0 means "simple variable", so no dimension can have
 *	an extent of 1 under either OPTION BASE - DIM a(1) is refused
 *	under BASE 1 for exactly the reason DIM a(0) is under BASE 0.  We
 *	carry the rank separately, so DIM c(0,0) is an honest 1x1 matrix
 *	and a row vector times a column vector lands in it.  A program
 *	that wants the NUMBER rather than the matrix should say
 *	MATH(DOTPRODUCT), which is the same arithmetic and returns one. */
MMG_FN void mmg_mmult(const MMFLOAT *a, int c1, int r1,
                      const MMFLOAT *b, int c2, int r2,
                      MMFLOAT *c, int c3, int r3)
{
	int i, j, k;

	if (r2 != c1)
		MM_RAISE("Input array size mismatch");
	if (c3 != c2 || r3 != r1)
		MM_RAISE("Output array size mismatch");
	if (c == a || c == b)
		MM_RAISE("Destination array same as source");
	for (i = 0; i < r3; i++) {
		for (j = 0; j < c3; j++) {
			MMFLOAT s = 0.0;

			for (k = 0; k < c1; k++)
				s += a[i * c1 + k] * b[k * c2 + j];
			c[i * c3 + j] = s;
		}
	}
}

/*	MATH M_INVERSE a(), b()      square, and not the same array
 *
 *	1x1 is ours alone and is DEFINED rather than copied: the
 *	reference's cofactor() would answer 0 for it, its inner
 *	determinant of a 0x0 minor falling through to zero.  Unreachable
 *	there, a 1x1 array being undeclarable; reachable here, so it is
 *	the reciprocal, which is what the inverse of [x] is. */
MMG_FN void mmg_minv(const MMFLOAT *a, int c1, int r1,
                     MMFLOAT *b, int c2, int r2)
{
	MMFLOAT m[MMG_MDIM * MMG_MDIM], inv[MMG_MDIM * MMG_MDIM];
	int i, j;

	if (c2 != c1 || r2 != r1)
		MM_RAISE("Array size mismatch");
	if (c1 != r1)
		MM_RAISE("Array must be square");
	if (a == b)
		MM_RAISE("Same array specified for input and output");
	if (c1 > MMG_MDIM)
		MM_RAISE("Array too large to invert");
	if (c1 == 1) {
		if (a[0] == 0.0)
			MM_RAISE("Determinant of array is zero");
		b[0] = 1.0 / a[0];
		return;
	}
	mmg_m_load(a, m, c1);
	if (mmg_det(m, c1) == 0.0)
		MM_RAISE("Determinant of array is zero");
	mmg_cofactor(m, inv, c1);
	for (i = 0; i < r1; i++)
		for (j = 0; j < c1; j++)
			b[i * c1 + j] = inv[j * c1 + i];
}

/*	MATH(M_DETERMINANT a())      square */
MMG_FN MMFLOAT mmg_mdet(const MMFLOAT *a, int nc, int nr)
{
	MMFLOAT m[MMG_MDIM * MMG_MDIM];

	if (nc != nr)
		MM_RAISEV("Array must be square", 0.0);
	if (nc > MMG_MDIM)
		MM_RAISEV("Array too large for a determinant", 0.0);
	mmg_m_load(a, m, nc);
	return mmg_det(m, nc);
}

/*	MATH V_MULT a(), b(), c()     a matrix by a vector
 *
 *	b is one-dimensional and as long as a's COLUMN count; c is
 *	one-dimensional and as long as a's row count. */
MMG_FN void mmg_vmult(const MMFLOAT *a, int nc, int nr,
                      const MMFLOAT *b, int nb, MMFLOAT *c, int ncv)
{
	int i, j;

	if (nb != nc || ncv != nr)
		MM_RAISE("Array size mismatch");
	if (c == a || c == b)
		MM_RAISE("Destination array same as source");
	for (i = 0; i < nr; i++) {
		MMFLOAT s = 0.0;

		for (j = 0; j < nc; j++)
			s += a[i * nc + j] * b[j];
		c[i] = s;
	}
}

/*	MATH V_ROTATE xo, yo, angle, xin(), yin(), xout(), yout()
 *
 *	Four one-dimensional arrays of the same length.  Both inputs of a
 *	point are read before either output is written, so an output may
 *	be an input and a shape can be rotated in place - which is what
 *	the reference buys with its GetTempMainMemory copies, without the
 *	copies, and without a bound on the array length.
 *
 *	The angle arrives already divided by OPTION ANGLE's multiplier;
 *	the translator does that, as it does for SIN and COS.
 *
 *	ALL FOUR ARRAYS ARE ONE TYPE, float or integer.  The reference
 *	takes any mix of the two - parsenumberarray on each - and the mix
 *	that matters, exact geometry in and pixels out, is the one this
 *	refuses.  An honest gap rather than sixteen combinations; say so
 *	if a program wants it.
 */
MMG_FN void mmg_vrotate(MMFLOAT ox, MMFLOAT oy, MMFLOAT ang,
                        const MMFLOAT *xi, int nxi, const MMFLOAT *yi,
                        int nyi, MMFLOAT *xo, int nxo,
                        MMFLOAT *yo, int nyo)
{
	MMFLOAT ca = cos(ang), sa = sin(ang);
	int i;

	if (nyi != nxi || nxo != nxi || nyo != nxi)
		MM_RAISE("Array size mismatch");
	for (i = 0; i < nxi; i++) {
		MMFLOAT x = xi[i] - ox;
		MMFLOAT y = yi[i] - oy;

		xo[i] = x * ca - y * sa + ox;
		yo[i] = y * ca + x * sa + oy;
	}
}

MMG_FN void mmg_vrotate_i(MMFLOAT ox, MMFLOAT oy, MMFLOAT ang,
                          const MMINTEGER *xi, int nxi,
                          const MMINTEGER *yi, int nyi,
                          MMINTEGER *xo, int nxo,
                          MMINTEGER *yo, int nyo)
{
	MMFLOAT ca = cos(ang), sa = sin(ang);
	int i;

	if (nyi != nxi || nxo != nxi || nyo != nxi)
		MM_RAISE("Array size mismatch");
	for (i = 0; i < nxi; i++) {
		MMFLOAT x = (MMFLOAT)xi[i] - ox;
		MMFLOAT y = (MMFLOAT)yi[i] - oy;

		/* round(), as the reference does for an integer target */
		xo[i] = mm_toint(x * ca - y * sa + ox);
		yo[i] = mm_toint(y * ca + x * sa + oy);
	}
}
#endif /* MMB_MATH_H */
