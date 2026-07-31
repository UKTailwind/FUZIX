/*
 *	Support for the NATIVE Fuzix build of Dhrystone - the reference
 *	point for "how far is the bytecode world off a real compiler".
 *
 *	Two things the Fuzix libc cannot provide:
 *
 *	time_us(): the same 31-bit SDK microsecond clock the bcrun
 *	libcall uses, via PICOIOC_ADVAL on /dev/sys, so both worlds
 *	measure with one clock.  Falls back to gettimeofday.
 *
 *	printf(): the libc printf has no %f, and Dhrystone's timing
 *	lines are "%6.1f".  Dhrystone only ever uses %d %c %s and
 *	%6.1f, so a ~70 line printf covers it exactly and overrides
 *	libc by link order.  dhry_1.c stays untouched.
 */
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#define PICOIOC_ADVAL	0x0009

long time_us(void)
{
	static int sysfd = -2;
	int n = -9;
	long t;

	if (sysfd == -2)
		sysfd = open("/dev/sys", O_RDWR);
	if (sysfd >= 0) {
		t = ioctl(sysfd, PICOIOC_ADVAL, &n);
		if (t >= 0)
			return t;
	}
	{
		struct timeval tv;
		gettimeofday(&tv, 0);
		return (long)((tv.tv_sec * 1000000L + tv.tv_usec)
			      & 0x7FFFFFFF);
	}
}

static char obuf[256];
static int olen;

static void oflush(void)
{
	if (olen)
		write(1, obuf, olen);
	olen = 0;
}

static void oc(char c)
{
	if (olen == sizeof(obuf))
		oflush();
	obuf[olen++] = c;
}

static void opad(const char *s, int width)
{
	int n = strlen(s);
	while (width-- > n)
		oc(' ');
	while (*s)
		oc(*s++);
}

static char *ltoa10(char *end, long v)
{
	char *p = end;
	unsigned long u = (v < 0) ? -(unsigned long)v : (unsigned long)v;
	*--p = 0;
	do {
		*--p = '0' + (u % 10);
		u /= 10;
	} while (u);
	if (v < 0)
		*--p = '-';
	return p;
}

int printf(const char *fmt, ...)
{
	va_list ap;
	char tmp[24];

	va_start(ap, fmt);
	while (*fmt) {
		int width = 0, prec = -1;
		if (*fmt != '%') {
			oc(*fmt++);
			continue;
		}
		fmt++;
		while (*fmt >= '0' && *fmt <= '9')
			width = width * 10 + (*fmt++ - '0');
		if (*fmt == '.') {
			fmt++;
			prec = 0;
			while (*fmt >= '0' && *fmt <= '9')
				prec = prec * 10 + (*fmt++ - '0');
		}
		switch (*fmt) {
		case 'd':
			opad(ltoa10(tmp + sizeof(tmp), va_arg(ap, int)),
			     width);
			break;
		case 'c':
			tmp[0] = va_arg(ap, int);
			tmp[1] = 0;
			opad(tmp, width);
			break;
		case 's':
			opad(va_arg(ap, char *), width);
			break;
		case 'f': {
			/* fixed point, enough for "%6.1f" */
			double v = va_arg(ap, double);
			long scale = 1, i, d;
			char num[32];
			char *p;
			if (prec < 0)
				prec = 6;
			if (prec > 9)
				prec = 9;
			for (i = 0; i < prec; i++)
				scale *= 10;
			i = (long)(v * scale + (v < 0 ? -0.5 : 0.5));
			p = ltoa10(num + 16, i / scale);
			if (prec) {
				char *e = num + 15;	/* over the NUL */
				long f = (i < 0 ? -i : i) % scale;
				*e++ = '.';
				for (d = scale / 10; d; d /= 10)
					*e++ = '0' + (f / d) % 10;
				*e = 0;
			}
			opad(p, width);
			break;
		}
		case '%':
			oc('%');
			break;
		default:
			oc('%');
			if (*fmt)
				oc(*fmt);
		}
		if (*fmt)
			fmt++;
	}
	va_end(ap);
	oflush();
	return 0;
}
