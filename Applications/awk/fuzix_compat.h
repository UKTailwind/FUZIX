#ifndef FUZIX_COMPAT_H
#define FUZIX_COMPAT_H
/*
 *	Everything this port needs that Lucent's awk expects and the Fuzix
 *	C library does not have.  Kept in one file, and included from the
 *	single line added to awk.h, so that the eight upstream .c files
 *	are byte-for-byte the ones from onetrueawk and a future update is
 *	a copy rather than a merge.  PORTING lists every difference.
 */

#include <stdlib.h>

/*
 *	MB_CUR_MAX.  Fuzix is single-byte and has no locale machinery, so
 *	this is 1 and cannot be anything else.
 *
 *	That is not a limitation being papered over: awk already carries
 *	`awk_mb_cur_max', initialised to 1 in main.c, and every hot path
 *	in b.c and run.c is written as `if (c < 128 || awk_mb_cur_max ==
 *	1)' - the single-byte case is the one it takes first.  Setting it
 *	to 1 selects a path upstream maintains, not a stub.
 *
 *	UTF-8 input therefore passes through as bytes: length("é") is
 *	2 rather than 1, and substr splits inside a character.  For a
 *	machine whose console font is 8x12 ASCII that is the honest
 *	answer.
 */
#ifndef MB_CUR_MAX
#define MB_CUR_MAX	((size_t)1)
#endif

/*
 *	NCHARS - the width of one row of the regular expression DFA.
 *
 *	Upstream's 1256+3 is sized for Unicode runes.  Each state in b.c
 *	callocs NCHARS gototab entries of eight bytes, and resize_state
 *	takes ten states at a time, so the first growth of a DFA asks for
 *	about a hundred kilobytes in one go.  On the board that calloc
 *	fails and awk reports "regular expression too big" for gsub(/o/,
 *	"0", s) - a one-character pattern.
 *
 *	256+3 here, and nothing is lost: awk_mb_cur_max is 1 on this
 *	machine (see above), so the matcher only ever indexes a row by a
 *	byte and the thousand entries above 255 cannot be reached.  A row
 *	goes from 10,072 bytes to 2,072.
 *
 *	The +3 is upstream's and is kept: HAT is defined as NCHARS+2 and
 *	the table needs room above the character values for it.
 */
#define NCHARS	(256+3)

/*
 *	random()/srandom().  Fuzix has the drand48 family instead, and
 *	lrand48() is an EXACT substitute rather than an approximation:
 *	both return a value in [0, 2^31-1], and run.c's FRAND divides by
 *	0x80000000 to reach [0, 1) rather than by RAND_MAX.
 *
 *	rand() would have been the obvious choice and the wrong one -
 *	Fuzix defines RAND_MAX as 32767, so awk's rand() could have
 *	produced only 32768 distinct values and srand() would have been
 *	visibly periodic.
 */
#define random()	lrand48()
#define srandom(s)	srand48((long)(s))

/*
 *	FOPEN_MAX.  Only the initial size of awk's own table of open
 *	redirections and the amount it grows by - run.c reallocs when it
 *	fills - so this is a starting point rather than a ceiling.
 *
 *	The real ceiling is the KERNEL's: UFTSIZE is 10 files per process
 *	(Kernel/include/kernel.h), three of them already spent on stdin,
 *	stdout and stderr.  An awk program that opens an eighth
 *	redirection gets a failed fopen and awk's own error, which is the
 *	right place for it to come from.  Matching the number here means
 *	the table never grows on this machine.
 */
#ifndef FOPEN_MAX
#define FOPEN_MAX	10
#endif

/*
 *	Multibyte conversion.  Fuzix has no wchar.h and no locale beyond
 *	C, so these are the single-byte definitions the C locale requires
 *	- not stubs: in the C locale a character IS a byte, and that is
 *	exactly what the standard says these must do.
 *
 *	run.c calls them only on the path guarded by awk_mb_cur_max != 1,
 *	which cannot be taken here, but it still has to link.
 *
 *	wchar_t is NOT declared here: Fuzix's headers do not have it, but
 *	the compiler's own stddef.h does, and typedef'ing it again is a
 *	conflicting-types error rather than a harmless repeat.
 */
static __inline__ __attribute__((unused))
int mbtowc(wchar_t *pwc, const char *s, size_t n)
{
	if (s == NULL)		/* "are the encodings state-dependent?" - no */
		return 0;
	if (n == 0)
		return -1;
	if (pwc != NULL)
		*pwc = (wchar_t)(unsigned char)*s;
	return *s != '\0';	/* 0 for NUL, 1 otherwise, as specified */
}

static __inline__ __attribute__((unused))
int wctomb(char *s, wchar_t wc)
{
	if (s == NULL)
		return 0;
	if (wc < 0 || wc > 255)
		return -1;	/* not representable in a single byte */
	*s = (char)wc;
	return 1;
}

/*
 *	towupper/towlower are NOT here.  They are passed to nawk_convert
 *	as function pointers, so a macro cannot stand in for them, and
 *	wint_t is only in scope inside run.c.  upstream already carries
 *	the two functions for DJGPP, which lacks them for the same
 *	reason; run.c's guard names __FUZIX__ alongside it.  That is the
 *	second and last line of divergence in an upstream file.
 */

#endif /* FUZIX_COMPAT_H */
