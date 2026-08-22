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

#endif /* MMB_MATH_H */
