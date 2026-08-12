/*
 *	towupper() and towlower() for Fuzix.
 *
 *	These are libc functions, and the awkward part is that the
 *	toolchain's own <wctype.h> DECLARES them - it ships with newlib,
 *	whose copies we do not link - while libcarmm0.a does not define
 *	them.  So they are neither missing nor present: declared, and
 *	undefined at link time.
 *
 *	That rules out both of the obvious fixes.  A macro in a header
 *	cannot stand in, because run.c passes them to nawk_convert as
 *	function pointers.  And awk's own copies, which it carries for
 *	DJGPP for exactly this reason, are `static' and so collide with
 *	the non-static declaration already in scope.
 *
 *	A definition matching that declaration is what is left, and it is
 *	the honest one anyway: Fuzix has one locale, C, in which the wide
 *	case conversions ARE the narrow ones for the single byte range
 *	and the identity everywhere else.  The bodies are awk's own
 *	DJGPP ones (run.c), which is the same conclusion reached by the
 *	same argument.
 *
 *	Here rather than in the C library because wctype.h is the
 *	toolchain's and Fuzix has no wide-character support to belong to.
 *	If Fuzix ever grows a real one, this file is what it replaces.
 */

#include <wctype.h>
#include <ctype.h>

wint_t towupper(wint_t wc)
{
	if (wc >= 0 && wc < 256)
		return toupper((int)(wc & 0xFF));
	return wc;
}

wint_t towlower(wint_t wc)
{
	if (wc >= 0 && wc < 256)
		return tolower((int)(wc & 0xFF));
	return wc;
}
