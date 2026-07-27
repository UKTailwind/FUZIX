/* string.c
 * Copyright (C) 1995,1996 Robert de Bath <rdebath@cix.compulink.co.uk>
 * This file is part of the Linux-8086 C library and is distributed
 * under the GNU Library General Public License.
 */

#include <string.h>

/* FIXME: asm version ?? */
/********************** Function strchr ************************************/
char *strchr(const char *s, int c)
{
	register char ch;

	/* POSIX: c is interpreted as a char.  Without the cast the
	 * comparison fails for bytes >= 0x80 on unsigned-char targets
	 * (ARM) whenever the caller passes a negative signed char. */
	for (;;) {
		if ((ch = *s) == (char)c)
			return (char *)s;
		if (ch == 0)
			return 0;
		s++;
	}
}
