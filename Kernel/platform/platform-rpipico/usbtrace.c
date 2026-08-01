/*
 *	TinyUSB's own trace, routed to the kernel console.
 *
 *	Built only when PC3_USB_TRACE is defined - a hardware debugging
 *	kernel, not a shipping one.  The stack's trace is the only thing
 *	that says WHERE enumeration stops when a device attaches and
 *	never appears, and it needs a printf: the kernel's kprintf
 *	implements a handful of conversions and returns void, where
 *	TinyUSB wants an int-returning printf with the usual set.  This
 *	is that printf, kept deliberately small.
 *
 *	It is called from thread context (tuh_task, on the dedicated USB
 *	stack) and from the USB interrupt handler, where the kernel
 *	stack is about 1.2K - hence the static buffer rather than an
 *	automatic one, at the price of being unfit for reentry.  For a
 *	debugging session that is the right trade.
 */

#include <kernel.h>
#include <printf.h>
#include "config.h"

#ifdef PC3_USB_TRACE

#include <stdarg.h>

static char tbuf[160];

static char *tnum(char *p, char *end, unsigned long v, unsigned base,
		  int upper)
{
	static const char *ld = "0123456789abcdef";
	static const char *ud = "0123456789ABCDEF";
	const char *d = upper ? ud : ld;
	char rev[24];
	int n = 0;

	if (!v)
		rev[n++] = '0';
	while (v) {
		rev[n++] = d[v % base];
		v /= base;
	}
	while (n && p < end)
		*p++ = rev[--n];
	return p;
}

int usb_trace_printf(const char *fmt, ...)
{
	va_list ap;
	char *p = tbuf;
	char *end = tbuf + sizeof(tbuf) - 1;

	va_start(ap, fmt);
	while (*fmt && p < end) {
		if (*fmt != '%') {
			*p++ = *fmt++;
			continue;
		}
		fmt++;
		/* width, flags and length modifiers are consumed and
		   ignored: the trace is for reading, not formatting */
		while (*fmt == '-' || *fmt == '0' || *fmt == '+' ||
		       *fmt == ' ' || *fmt == '#' || *fmt == '.' ||
		       (*fmt >= '1' && *fmt <= '9'))
			fmt++;
		while (*fmt == 'l' || *fmt == 'h' || *fmt == 'z')
			fmt++;
		switch (*fmt) {
		case 'd': case 'i': {
			long v = va_arg(ap, long);
			if (v < 0) {
				if (p < end)
					*p++ = '-';
				v = -v;
			}
			p = tnum(p, end, (unsigned long)v, 10, 0);
			break;
		}
		case 'u':
			p = tnum(p, end, va_arg(ap, unsigned long), 10, 0);
			break;
		case 'x':
			p = tnum(p, end, va_arg(ap, unsigned long), 16, 0);
			break;
		case 'X':
			p = tnum(p, end, va_arg(ap, unsigned long), 16, 1);
			break;
		case 'p':
			p = tnum(p, end, (unsigned long)va_arg(ap, void *),
				 16, 0);
			break;
		case 'c':
			*p++ = (char)va_arg(ap, int);
			break;
		case 's': {
			const char *s = va_arg(ap, const char *);
			if (!s)
				s = "(null)";
			while (*s && p < end)
				*p++ = *s++;
			break;
		}
		case '%':
			*p++ = '%';
			break;
		default:		/* unknown: show it and move on */
			if (p < end)
				*p++ = '%';
			if (*fmt && p < end)
				*p++ = *fmt;
			break;
		}
		if (*fmt)
			fmt++;
	}
	va_end(ap);
	*p = 0;
	kputs(tbuf);
	return (int)(p - tbuf);
}

#endif /* PC3_USB_TRACE */
