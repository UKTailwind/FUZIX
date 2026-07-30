/*
 * strtod.c - This file is part of the libc-8086 package for ELKS,
 * Copyright (C) 1995, 1996 Nat Friedman <ndf@linux.mit.edu>.
 * 
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#include <stdlib.h>
#include <ctype.h>

/*
 * Two long-standing bugs fixed here.  The fraction loop was
 *
 *	fp_part = fp_part / 10.0 + (*nptr - '0') / 10.0;
 *
 * which builds the fraction back to front: ".25" parsed as 0.52, so
 * every decimal that passed through here came out with its digits
 * reversed.  And the exponent loop did "exponent = exponent * 10 + d;
 * exponent++", adding one per digit - "1e2" was a thousand.
 */
double strtod(const char *nptr, char **endptr)
{
	unsigned short negative;
	double number;
	double scale;
	int exponent;
	unsigned short exp_negative;

	/* advance beyond any leading whitespace */
	while (isspace(*nptr))
		nptr++;

	/* check for optional '+' or '-' */
	negative = 0;
	if (*nptr == '-') {
		negative = 1;
		nptr++;
	} else if (*nptr == '+')
		nptr++;

	number = 0;
	while (isdigit(*nptr)) {
		number = number * 10 + (*nptr - '0');
		nptr++;
	}

	if (*nptr == '.') {
		nptr++;
		scale = 0.1;
		while (isdigit(*nptr)) {
			number += (*nptr - '0') * scale;
			scale /= 10.0;
			nptr++;
		}
	}

	if (*nptr == 'e' || *nptr == 'E') {
		nptr++;
		exp_negative = 0;
		if (*nptr == '-') {
			exp_negative = 1;
			nptr++;
		} else if (*nptr == '+')
			nptr++;

		exponent = 0;
		while (isdigit(*nptr)) {
			exponent = exponent * 10 + (*nptr - '0');
			nptr++;
		}

		while (exponent) {
			if (exp_negative)
				number /= 10;
			else
				number *= 10;
			exponent--;
		}
	}
	if (endptr)
		*endptr = (char *) nptr;
	return (negative ? -number : number);
}
