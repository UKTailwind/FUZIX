/* bbcstdio.c - sprintf/sscanf replacements for BBC BASIC on Fuzix.
 *
 * The Fuzix libc printf has no floating point and no long long support,
 * and its sscanf lacks %n; the interpreter's number formatting needs
 * %.*E / %.*f / %.*G, %lld, %llX, and its tokeniser needs %n.  These
 * replacements cover exactly the conversions the interpreter uses and
 * are linked ahead of the libc versions.
 *
 * Decimal conversion works by normalising into [1,10) with a table of
 * exact powers of ten and extracting up to 18 significant digits in a
 * 64-bit integer.  The scaling can be off by one ulp, so the last
 * digit of a 17-digit conversion may occasionally differ from a
 * correctly-rounded glibc - harmless at BASIC's default 9/10
 * significant figures.
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

/* Not in the Fuzix libc: */
long long llabs (long long n)
{
	return (n < 0) ? -n : n ;
}

unsigned long long strtoull (const char *s, char **endp, int base)
{
	unsigned long long v = 0 ;

	while (isspace ((unsigned char) *s))
		s++ ;
	if ((base == 0 || base == 16) && (s[0] == '0') &&
	    ((s[1] == 'x') || (s[1] == 'X')))
	    {
		base = 16 ;
		s += 2 ;
	    }
	else if (base == 0)
		base = (s[0] == '0') ? 8 : 10 ;
	for (;;)
	    {
		int c = (unsigned char) *s, dv ;
		if ((c >= '0') && (c <= '9'))
			dv = c - '0' ;
		else if ((c >= 'a') && (c <= 'z'))
			dv = c - 'a' + 10 ;
		else if ((c >= 'A') && (c <= 'Z'))
			dv = c - 'A' + 10 ;
		else
			break ;
		if (dv >= base)
			break ;
		v = v * base + dv ;
		s++ ;
	    }
	if (endp)
		*endp = (char *) s ;
	return v ;
}

/* Powers of ten exactly representable in a double. */
static const double tenpow[9] =
	{ 1e1, 1e2, 1e4, 1e8, 1e16, 1e32, 1e64, 1e128, 1e256 } ;

static double pow10d (int n)	/* 10^n, 0 <= n <= 511 */
{
	double r = 1.0 ;
	int i ;
	for (i = 0; i < 9; i++)
		if (n & (1 << i))
			r *= tenpow[i] ;
	return r ;
}

static unsigned long long upow10 (int n)	/* 10^n, n <= 19 */
{
	unsigned long long r = 1 ;
	while (n-- > 0)
		r *= 10 ;
	return r ;
}

/* Extract nd significant decimal digits of x (x > 0, finite) into dig
 * (NUL terminated).  Returns the decimal exponent of the first digit,
 * i.e. x ~= dig[0].dig[1..] * 10^e. */
static int digits (double x, int nd, char *dig)
{
	int e = 0, i ;
	unsigned long long m ;

	if (x >= 10.0)
	    {
		for (i = 8; i >= 0; i--)
			if (x >= tenpow[i])
			    {
				x /= tenpow[i] ;
				e += 1 << i ;
			    }
	    }
	else if (x < 1.0)
	    {
		for (i = 8; i >= 0; i--)
			if (x * tenpow[i] < 10.0)
			    {
				x *= tenpow[i] ;
				e -= 1 << i ;
			    }
		if (x < 1.0)
		    {
			x *= 10.0 ;
			e-- ;
		    }
	    }

	if (nd > 18)
		nd = 18 ;
	if (nd < 1)
		nd = 1 ;
	m = (unsigned long long) (x * pow10d (nd - 1) + 0.5) ;
	if (m >= upow10 (nd))
	    {
		m /= 10 ;	/* 9.99... rounded up a decade */
		e++ ;
	    }
	for (i = nd - 1; i >= 0; i--)
	    {
		dig[i] = '0' + (int)(m % 10) ;
		m /= 10 ;
	    }
	dig[nd] = 0 ;
	return e ;
}

/* Classify without relying on fpclassify (no libm dependency): */
static int special (double x, char *dst)
{
	if (x != x)
	    {
		strcpy (dst, "NaN") ;
		return 1 ;
	    }
	if (x != 0.0 && x - x != 0.0)
	    {
		strcpy (dst, x < 0 ? "-Inf" : "Inf") ;
		return 1 ;
	    }
	return 0 ;
}

/* %.*E : d.dddddE[-+]XX (two exponent digits minimum, like glibc). */
static int fmt_e (char *dst, double x, int prec)
{
	char dig[20] ;
	char *d = dst ;
	int e, i ;

	if (special (x, dst))
		return strlen (dst) ;
	if (x < 0)
	    {
		*d++ = '-' ;
		x = -x ;
	    }
	if (x == 0.0)
	    {
		memset (dig, '0', prec + 1) ;
		dig[prec + 1] = 0 ;
		e = 0 ;
	    }
	else
		e = digits (x, prec + 1, dig) ;
	*d++ = dig[0] ;
	if (prec > 0)
	    {
		*d++ = '.' ;
		for (i = 1; i <= prec; i++)
			*d++ = dig[i] ;
	    }
	*d++ = 'E' ;
	*d++ = (e < 0) ? '-' : '+' ;
	if (e < 0)
		e = -e ;
	if (e >= 100)
	    {
		*d++ = '0' + e / 100 ;
		e %= 100 ;
	    }
	*d++ = '0' + e / 10 ;
	*d++ = '0' + e % 10 ;
	*d = 0 ;
	return d - dst ;
}

static char *putull (char *d, unsigned long long v)
{
	char tmp[20] ;
	int n = 0 ;
	do
	    {
		tmp[n++] = '0' + (int)(v % 10) ;
		v /= 10 ;
	    }
	while (v) ;
	while (n)
		*d++ = tmp[--n] ;
	return d ;
}

/* %.*f */
static int fmt_f (char *dst, double x, int prec)
{
	char *d = dst ;
	int i ;

	if (special (x, dst))
		return strlen (dst) ;
	if (x < 0)
	    {
		*d++ = '-' ;
		x = -x ;
	    }
	if (prec <= 18 && x * pow10d (prec) < 4e18)
	    {
		/* value rounds within a 64-bit integer */
		unsigned long long m =
			(unsigned long long) (x * pow10d (prec) + 0.5) ;
		unsigned long long ip = m / upow10 (prec) ;
		d = putull (d, ip) ;
		if (prec > 0)
		    {
			unsigned long long fp = m % upow10 (prec) ;
			*d++ = '.' ;
			for (i = prec - 1; i >= 0; i--)
			    {
				d[i] = '0' + (int)(fp % 10) ;
				fp /= 10 ;
			    }
			d += prec ;
		    }
	    }
	else
	    {
		/* magnitude beyond 64-bit precision: digits then zeros */
		char dig[20] ;
		int e = digits (x, 18, dig) ;
		for (i = 0; i <= e; i++)
			*d++ = (i < 18) ? dig[i] : '0' ;
		if (prec > 0)
		    {
			*d++ = '.' ;
			for (i = 0; i < prec; i++)
				*d++ = '0' ;
		    }
	    }
	*d = 0 ;
	return d - dst ;
}

/* %.*G : prec significant digits, trailing zeros stripped. */
static int fmt_g (char *dst, double x, int prec)
{
	char dig[20] ;
	char *d = dst ;
	int e, i, last ;

	if (prec <= 0)
		prec = 1 ;
	if (prec > 17)
		prec = 17 ;
	if (special (x, dst))
		return strlen (dst) ;
	if (x == 0.0)
	    {
		strcpy (dst, "0") ;
		return 1 ;
	    }
	if (x < 0)
	    {
		*d++ = '-' ;
		x = -x ;
	    }
	e = digits (x, prec, dig) ;

	/* strip trailing zeros of the significant digits */
	last = prec - 1 ;
	while (last > 0 && dig[last] == '0')
		last-- ;

	if ((e < -4) || (e >= prec))
	    {
		/* exponent form */
		*d++ = dig[0] ;
		if (last > 0)
		    {
			*d++ = '.' ;
			for (i = 1; i <= last; i++)
				*d++ = dig[i] ;
		    }
		*d++ = 'E' ;
		*d++ = (e < 0) ? '-' : '+' ;
		if (e < 0)
			e = -e ;
		if (e >= 100)
		    {
			*d++ = '0' + e / 100 ;
			e %= 100 ;
		    }
		*d++ = '0' + e / 10 ;
		*d++ = '0' + e % 10 ;
	    }
	else if (e >= 0)
	    {
		/* 100, 1.5, 12.34 */
		for (i = 0; i <= e; i++)
			*d++ = (i <= last) ? dig[i] : '0' ;
		if (last > e)
		    {
			*d++ = '.' ;
			for (i = e + 1; i <= last; i++)
				*d++ = dig[i] ;
		    }
	    }
	else
	    {
		/* 0.05 */
		*d++ = '0' ;
		*d++ = '.' ;
		for (i = -1; i > e; i--)
			*d++ = '0' ;
		for (i = 0; i <= last; i++)
			*d++ = dig[i] ;
	    }
	*d = 0 ;
	return d - dst ;
}

static char *ulltostr (unsigned long long v, unsigned int base, int upper,
		       char *end)
{
	static const char lc[] = "0123456789abcdef" ;
	static const char uc[] = "0123456789ABCDEF" ;
	const char *set = upper ? uc : lc ;
	*--end = 0 ;
	do
	    {
		*--end = set[v % base] ;
		v /= base ;
	    }
	while (v) ;
	return end ;
}

int sprintf (char *dst, const char *fmt, ...)
{
	va_list ap ;
	char *d = dst ;
	char buf[48] ;

	va_start (ap, fmt) ;
	while (*fmt)
	    {
		int left = 0, zero = 0, width = 0, prec = -1 ;
		int ll = 0, hh = 0 ;
		char conv ;
		char *body = buf ;
		int blen ;

		if (*fmt != '%')
		    {
			*d++ = *fmt++ ;
			continue ;
		    }
		fmt++ ;
		for (;;)
		    {
			if (*fmt == '-')
				left = 1 ;
			else if (*fmt == '0')
				zero = 1 ;
			else if ((*fmt == '+') || (*fmt == ' ') || (*fmt == '#'))
				;	/* unused by the interpreter */
			else
				break ;
			fmt++ ;
		    }
		if (*fmt == '*')
		    {
			width = va_arg (ap, int) ;
			if (width < 0)
			    {
				left = 1 ;
				width = -width ;
			    }
			fmt++ ;
		    }
		else
			while ((*fmt >= '0') && (*fmt <= '9'))
				width = width * 10 + *fmt++ - '0' ;
		if (*fmt == '.')
		    {
			fmt++ ;
			if (*fmt == '*')
			    {
				prec = va_arg (ap, int) ;
				fmt++ ;
			    }
			else
			    {
				prec = 0 ;
				while ((*fmt >= '0') && (*fmt <= '9'))
					prec = prec * 10 + *fmt++ - '0' ;
			    }
		    }
		while (*fmt == 'l')
		    {
			ll++ ;
			fmt++ ;
		    }
		while (*fmt == 'h')
		    {
			hh = 1 ;
			fmt++ ;
		    }
		conv = *fmt++ ;
		switch (conv)
		    {
			case 'd':
			case 'i':
			    {
				long long v ;
				if (ll >= 2)
					v = va_arg (ap, long long) ;
				else if (ll == 1)
					v = va_arg (ap, long) ;
				else
					v = va_arg (ap, int) ;
				if (hh)
					v = (short) v ;
				if (v < 0)
				    {
					body = ulltostr (-(unsigned long long)v,
							10, 0, buf + sizeof buf) ;
					*--body = '-' ;
				    }
				else
					body = ulltostr (v, 10, 0,
							buf + sizeof buf) ;
			    }
				break ;

			case 'u':
			case 'x':
			case 'X':
			    {
				unsigned long long v ;
				if (ll >= 2)
					v = va_arg (ap, unsigned long long) ;
				else if (ll == 1)
					v = va_arg (ap, unsigned long) ;
				else
					v = va_arg (ap, unsigned int) ;
				if (hh)
					v = (unsigned short) v ;
				body = ulltostr (v, (conv == 'u') ? 10 : 16,
						(conv == 'X'), buf + sizeof buf) ;
			    }
				break ;

			case 'c':
				buf[0] = (char) va_arg (ap, int) ;
				buf[1] = 0 ;
				break ;

			case 's':
				body = va_arg (ap, char *) ;
				break ;

			case 'E':
			case 'e':
				fmt_e (buf, va_arg (ap, double),
					(prec < 0) ? 6 : prec) ;
				break ;

			case 'f':
			case 'F':
				fmt_f (buf, va_arg (ap, double),
					(prec < 0) ? 6 : prec) ;
				break ;

			case 'G':
			case 'g':
				fmt_g (buf, va_arg (ap, double),
					(prec < 0) ? 6 : prec) ;
				break ;

			case '%':
				buf[0] = '%' ;
				buf[1] = 0 ;
				break ;

			default:	/* unknown: emit literally */
				buf[0] = conv ;
				buf[1] = 0 ;
				break ;
		    }

		blen = strlen (body) ;
		if ((conv == 's') && (prec >= 0) && (blen > prec))
			blen = prec ;
		if (!left && (width > blen))
		    {
			int pad = width - blen ;
			if (zero && (*body == '-'))
			    {
				*d++ = *body++ ;
				blen-- ;
			    }
			while (pad--)
				*d++ = zero ? '0' : ' ' ;
		    }
		memcpy (d, body, blen) ;
		d += blen ;
		if (left && (width > blen))
		    {
			int pad = width - blen ;
			while (pad--)
				*d++ = ' ' ;
		    }
	    }
	*d = 0 ;
	va_end (ap) ;
	return d - dst ;
}

/* sscanf supporting %d %u %x %i (with h/l), %n and literal matching -
 * everything the interpreter and tokeniser use. */
int sscanf (const char *src, const char *fmt, ...)
{
	va_list ap ;
	const char *s = src ;
	int assigned = 0 ;
	int eof = 0 ;

	va_start (ap, fmt) ;
	while (*fmt)
	    {
		int suppress = 0, hh = 0, base ;
		char conv ;

		if (isspace ((unsigned char) *fmt))
		    {
			while (isspace ((unsigned char) *s))
				s++ ;
			fmt++ ;
			continue ;
		    }
		if (*fmt != '%')
		    {
			if (*s != *fmt)
				break ;
			s++ ;
			fmt++ ;
			continue ;
		    }
		fmt++ ;
		if (*fmt == '*')
		    {
			suppress = 1 ;
			fmt++ ;
		    }
		while ((*fmt >= '0') && (*fmt <= '9'))
			fmt++ ;	/* field width: unused by callers */
		while (*fmt == 'h')
		    {
			hh = 1 ;
			fmt++ ;
		    }
		while (*fmt == 'l')
			fmt++ ;
		conv = *fmt++ ;

		if (conv == 'n')
		    {
			if (!suppress)
				*va_arg (ap, int *) = (int) (s - src) ;
			continue ;
		    }
		if (conv == '%')
		    {
			if (*s != '%')
				break ;
			s++ ;
			continue ;
		    }

		/* numeric conversions */
		while (isspace ((unsigned char) *s))
			s++ ;
		if (*s == 0)
		    {
			eof = 1 ;
			break ;
		    }
		base = (conv == 'x') ? 16 : (conv == 'i') ? 0 : 10 ;
		    {
			const char *start = s ;
			unsigned long v = 0 ;
			int neg = 0, ndig = 0 ;

			if ((*s == '+') || (*s == '-'))
			    {
				neg = (*s == '-') ;
				s++ ;
			    }
			if (base == 0)
			    {
				if ((s[0] == '0') &&
				    ((s[1] == 'x') || (s[1] == 'X')))
				    {
					base = 16 ;
					s += 2 ;
				    }
				else if (s[0] == '0')
					base = 8 ;
				else
					base = 10 ;
			    }
			else if ((base == 16) && (s[0] == '0') &&
				 ((s[1] == 'x') || (s[1] == 'X')))
				s += 2 ;
			for (;;)
			    {
				int c = (unsigned char) *s, dv ;
				if ((c >= '0') && (c <= '9'))
					dv = c - '0' ;
				else if ((c >= 'a') && (c <= 'f'))
					dv = c - 'a' + 10 ;
				else if ((c >= 'A') && (c <= 'F'))
					dv = c - 'A' + 10 ;
				else
					break ;
				if (dv >= base)
					break ;
				v = v * base + dv ;
				s++ ;
				ndig++ ;
			    }
			if (ndig == 0)
			    {
				s = start ;
				break ;
			    }
			if (neg)
				v = -v ;
			if (!suppress)
			    {
				if (hh)
					*va_arg (ap, unsigned short *) =
						(unsigned short) v ;
				else
					*va_arg (ap, unsigned int *) =
						(unsigned int) v ;
				assigned++ ;
			    }
		    }
	    }
	va_end (ap) ;
	if (eof && (assigned == 0))
		return -1 ;	/* EOF, as the C library would return */
	return assigned ;
}
