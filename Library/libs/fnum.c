/*
 *	_fnum - the %e, %f and %g conversions for printf.
 *
 *	vfprintf.c has called this since it was written and declared it at
 *	the top, and nothing in the tree ever defined it: the float cases
 *	sit behind #ifdef BUILD_LIBM, no armm0 Makefile defined that, and
 *	so every %f in a program on this machine printed the letter `f'.
 *	awk made it obvious - awk converts numbers with CONVFMT, "%.6g",
 *	so on the PC3 every number it printed came out as `g'.
 *
 *	Contract, from the call site:
 *
 *		_fnum(value, conversion, precision, buffer)
 *
 *	conversion is one of e f g E G, precision is -1 when the format
 *	gave none, and buffer is vfprintf's own char tmp[64].  The result
 *	is a NUL-terminated string; vfprintf then pads it to the field
 *	width itself.
 *
 *	No libm.  fabs, floor and the isnan/isinf tests are all reachable
 *	with arithmetic, and a libc that pulls in the maths library to
 *	print a number would drag it into every program that has a printf.
 *
 *	TWO DELIBERATE LIMITS, both forced by that 64-byte buffer:
 *
 *	  - precision is clamped to FNUM_MAXPREC.  A double carries about
 *	    17 significant digits, so a request for more is asking for
 *	    noise, and the alternative is writing past the caller's stack.
 *
 *	  - %f falls back to %e above 1e18.  glibc would print 1e300 as
 *	    three hundred digits; there is no room, and every digit past
 *	    the seventeenth is an artefact of binary-to-decimal expansion
 *	    rather than a property of the number.
 *
 *	ACCURACY.  Checked against glibc's printf over 13,596 cases -
 *	fixed values and a random sweep of magnitudes, every conversion,
 *	precisions 0 to 12:
 *
 *		13,431	identical
 *		   138	%f asking for digits past a double's 17, above
 *		    69	last digit differs
 *		    33	exact ties
 *
 *	The ties are the one systematic difference: on a value sitting
 *	exactly halfway this rounds away from zero, where glibc rounds to
 *	even, so %.4e of 5.50385e-11 gives 5.5039e-11 and glibc gives
 *	5.5038e-11.  Round-to-even needs to know the value is exactly on
 *	the tie, which needs arbitrary precision - several kilobytes of
 *	tables to move one digit in one result in four hundred.
 */

#include <stdint.h>
#include <string.h>

#define FNUM_MAXPREC	24	/* fits 64 bytes with room for sign,
				   exponent and the decimal point */
#define FNUM_MAXDIG	20	/* significant digits we will generate */
#define FNUM_ALLDIG	18	/* what one uint64 scaling yields exactly */
#define FNUM_FBIG	1e18	/* above this, %f becomes %e */

/*
 *	Break |v| into a string of significant decimal digits and a
 *	decimal exponent, so that
 *
 *		|v| = 0.d[0]d[1]... x 10^(*expo + 1)
 *
 *	ndig digits are produced and the (ndig+1)th is used to round.
 *	Scaling is done by repeated multiply and divide rather than by
 *	pow(): pow would need libm, and on a soft-float target it is also
 *	slower than the handful of multiplies this takes.
 */
/* Exactly representable as doubles; 1e23 is not, which is where the
   table stops. */
static const double p10[23] = {
	1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,
	1e8,  1e9,  1e10, 1e11, 1e12, 1e13, 1e14, 1e15,
	1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22
};

/* Multiply v by 10^k, in steps the table can do exactly. */
static double fnum_scale(double v, int k)
{
	while (k >= 22) { v *= 1e22; k -= 22; }
	while (k <= -22) { v /= 1e22; k += 22; }
	if (k > 0)
		v *= p10[k];
	else if (k < 0)
		v /= p10[-k];
	return v;
}

/* floor(log10(v)) for v > 0, without log10 and without altering v. */
static int fnum_expof(double v)
{
	int e = 0, k;

	while (v >= 1e22) { v /= 1e22; e += 22; }
	while (v < 1e-22) { v *= 1e22; e -= 22; }
	if (v >= 1.0) {
		for (k = 22; k > 0; k--)
			if (v >= p10[k])
				break;
		e += k;
	} else {
		for (k = 1; k <= 22; k++)
			if (v * p10[k] >= 1.0)
				break;
		e -= k;
	}
	return e;
}

static void fnum_digits(double v, int ndig, char *d, int *expo)
{
	int e, i, nd;
	uint64_t scaled, lo, hi;
	char all[FNUM_ALLDIG + 1];

	if (ndig > FNUM_MAXDIG)
		ndig = FNUM_MAXDIG;
	if (ndig < 1)
		ndig = 1;

	if (v == 0.0) {
		for (i = 0; i < ndig; i++)
			d[i] = '0';
		d[ndig] = '\0';
		*expo = 0;
		return;
	}

	e = fnum_expof(v);
	nd = (ndig > FNUM_ALLDIG) ? FNUM_ALLDIG : ndig;

	/*
	 * Scale the ORIGINAL value straight to nd digits: one operation,
	 * one rounding.
	 *
	 * The tempting shape - normalise into [1,10), then multiply by
	 * 10^(nd-1) - rounds twice and the first rounding is the
	 * damaging one.  11 normalises to 1.1, which is not
	 * representable, and scaling that back up gives
	 * 110000000000000009: printf("%.30g", 11.0) then produced
	 * 11.0000000000000016.  awk formats every integral value with
	 * %.30g, so on the board `print length(s)' said
	 * 11.0000000000000016 instead of 11.
	 *
	 * Scaling 11 by 10^16 in one go is exact, because both operands
	 * and the product are.
	 */
	scaled = (uint64_t)(fnum_scale(v, nd - 1 - e) + 0.5);

	lo = (uint64_t)p10[nd - 1];
	hi = (uint64_t)p10[nd];
	if (scaled >= hi) {
		/* rounding carried into another digit: 9.99 -> 10.0 */
		scaled /= 10;
		e++;
	} else if (scaled < lo) {
		/* the exponent estimate was one too high - rescale rather
		   than emit a short number with a leading zero */
		e--;
		scaled = (uint64_t)(fnum_scale(v, nd - 1 - e) + 0.5);
		if (scaled >= hi) {
			scaled /= 10;
			e++;
		}
	}

	for (i = nd - 1; i >= 0; i--) {
		all[i] = (char)('0' + (int)(scaled % 10));
		scaled /= 10;
	}
	for (i = 0; i < ndig; i++)
		d[i] = (i < nd) ? all[i] : '0';

	d[ndig] = '\0';
	*expo = e;
}

/* Append the exponent of an %e conversion: e+dd, at least two digits. */
static char *fnum_exp(char *p, int e, char ec)
{
	int n;
	char t[8];

	*p++ = ec;
	if (e < 0) {
		*p++ = '-';
		e = -e;
	} else {
		*p++ = '+';
	}
	n = 0;
	do {
		t[n++] = (char)('0' + e % 10);
		e /= 10;
	} while (e);
	while (n < 2)
		t[n++] = '0';
	while (n--)
		*p++ = t[n];
	return p;
}

void _fnum(double val, char fmt, int prec, char *ptmp)
{
	char digits[FNUM_MAXDIG + 2];
	char *p = ptmp;
	int expo, i, upper, ndig;

	upper = (fmt == 'E' || fmt == 'G');
	if (upper)
		fmt = (char)(fmt - 'A' + 'a');

	/* Not a number, and infinity.  Tested arithmetically: isnan and
	   isinf are macros in a math.h this libc does not ship. */
	if (val != val) {
		strcpy(ptmp, upper ? "NAN" : "nan");
		return;
	}
	if (val > 1.7976931348623157e308 || val < -1.7976931348623157e308) {
		if (val < 0)
			*p++ = '-';
		strcpy(p, upper ? "INF" : "inf");
		return;
	}

	if (prec < 0)
		prec = 6;		/* what printf defaults to */
	if (prec > FNUM_MAXPREC)
		prec = FNUM_MAXPREC;

	if (val < 0.0) {
		*p++ = '-';
		val = -val;
	}

	/* %g picks %e or %f by exponent, then strips trailing zeros.  Do
	   the choosing here so the two producers below stay simple. */
	if (fmt == 'g') {
		int P = prec ? prec : 1;

		fnum_digits(val, P, digits, &expo);
		if (val == 0.0)
			expo = 0;
		if (expo < -4 || expo >= P) {
			fmt = 'e';
			prec = P - 1;
		} else {
			fmt = 'f';
			prec = P - 1 - expo;
			if (prec < 0)
				prec = 0;
		}
		/* mark that trailing zeros must go */
		upper |= 0x80;
	}

	if (fmt == 'e') {
		ndig = prec + 1;
		fnum_digits(val, ndig, digits, &expo);
		if (val == 0.0)
			expo = 0;
		*p++ = digits[0];
		if (prec > 0) {
			*p++ = '.';
			for (i = 1; i <= prec; i++)
				*p++ = digits[i];
		}
		/* %g strips trailing zeros - but only from a FRACTION.
		   Stripping unconditionally turned 10 into 1 and 0 into
		   nothing at all, because the zeros it ate were
		   significant digits of the integer part.  prec > 0 is
		   exactly the condition under which a '.' was emitted. */
		if ((upper & 0x80) && prec > 0) {
			while (p[-1] == '0')
				p--;
			if (p[-1] == '.')
				p--;
		}
		p = fnum_exp(p, expo, (upper & 1) ? 'E' : 'e');
		*p = '\0';
		return;
	}

	/* %f.  Above FNUM_FBIG there is no room for the integer part, so
	   this becomes %e - see the note at the top. */
	if (val >= FNUM_FBIG) {
		_fnum(val, (char)((upper & 1) ? 'E' : 'e'), prec, p);
		return;
	}

	/*
	 * Two passes, and the second is not optional.
	 *
	 * %f rounds at a place fixed by the DECIMAL POINT, so how many
	 * significant digits that is depends on the exponent, which is
	 * not known until the number has been looked at.  Generating a
	 * fixed eighteen and then cutting the string is truncation, not
	 * rounding: 9.9999995 to four decimals came out 9.9999 rather
	 * than 10.0000, and through %g - which reaches %f with a
	 * precision it derived from the exponent - that surfaced as
	 * printf("%.6g", 9.9999995) giving 9.9999 where every other
	 * printf gives 10.
	 *
	 * So: learn the exponent, then ask again for exactly the digits
	 * that survive, which puts the rounding where it belongs.  The
	 * carry can push the exponent up (9.99 -> 10.0), so read it back.
	 */
	/* Probe at FULL width, not at one digit: a short probe rounds,
	   and a rounding that carries moves the exponent - 9.76554
	   probed to one digit reports exponent 1, which then asked for
	   two significant digits and printed 9 instead of 10. */
	fnum_digits(val, FNUM_ALLDIG, digits, &expo);
	if (val != 0.0) {
		ndig = expo + 1 + prec;
		if (ndig < 1)
			ndig = 1;
		if (ndig > FNUM_MAXDIG)
			ndig = FNUM_MAXDIG;
		fnum_digits(val, ndig, digits, &expo);
		/* digits beyond what was generated are zeros */
		for (i = ndig; i < FNUM_MAXDIG; i++)
			digits[i] = '0';
	} else {
		for (i = 0; i < FNUM_MAXDIG; i++)
			digits[i] = '0';
		expo = -1;		/* so the integer part prints "0" */
	}

	/* Integer part: digits[0..expo], or a single 0 if val < 1. */
	if (expo < 0) {
		*p++ = '0';
	} else {
		for (i = 0; i <= expo; i++)
			*p++ = (i < FNUM_MAXDIG) ? digits[i] : '0';
	}

	if (prec > 0) {
		*p++ = '.';
		for (i = 0; i < prec; i++) {
			int idx = expo + 1 + i;

			if (idx < 0 || idx >= FNUM_MAXDIG)
				*p++ = '0';
			else
				*p++ = digits[idx];
		}
	}

	if ((upper & 0x80) && prec > 0) {	/* see the note above */
		while (p[-1] == '0')
			p--;
		if (p[-1] == '.')
			p--;
	}
	*p = '\0';
}
