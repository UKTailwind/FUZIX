#ifndef __LIMITS_H
#define __LIMITS_H

#define CHAR_MAX	127		/* maximum char value */
#define CHAR_MIN	(-127)		/* mimimum char value */
#define SCHAR_MAX	127		/* maximum signed char value */
#define SCHAR_MIN	(-127)		/* minimum signed char value */
#define CHAR_BIT	8		/* number of bits in a char */

#define SHRT_MAX	32767		/* maximum (signed) short value */
#define SHRT_MIN	(-32767)	/* minimum (signed) short value */

/*
 * int is not 16 bits everywhere.  These were flat 32767/0xffff for
 * every target, which is right for z80 and 6502 and wrong by a factor
 * of 65536 on armm0, where int is 32 bits - and wrong SILENTLY, because
 * the values are still legal C and every range check built on them
 * simply comes out the wrong way.
 *
 * Found via dr_mp3: it guards its input buffer with a
 * "dataSize > INT_MAX means the file is too big" test and reads in
 * 32768 byte chunks, so on the very first chunk 32768 > 32767 and a
 * perfectly good 4MB MP3 was rejected before a frame was decoded.
 * The symptom was "not a valid MP3 file", which sent the search to the
 * file, the transfer and the decoder before the C library.
 *
 * The compiler already knows the answer, so ask it rather than guess.
 * gcc predefines __INT_MAX__ on every target; the literals remain as
 * the fallback for a compiler that does not.
 */
#ifdef __INT_MAX__
#define INT_MAX		__INT_MAX__
#define INT_MIN		(-__INT_MAX__ - 1)
#else
#define INT_MAX 	32767		/* maximum (signed) int value */
#define INT_MIN 	(-32767)	/* minimum (signed) int value */
#endif

#define LONG_MAX	2147483647	/* maximum (signed) long value */
#define LONG_MIN	(-2147483647)	/* minimum (signed) long value */

#define UCHAR_MAX	255		/* maximum unsigned char value */
#define USHRT_MAX	0xffff		/* maximum unsigned short value */
/* Same bug, same fix: unsigned int is as wide as int. */
#ifdef __INT_MAX__
#define UINT_MAX	(__INT_MAX__ * 2U + 1U)
#else
#define UINT_MAX	0xffff		/* maximum unsigned int value */
#endif
#define ULONG_MAX	0xffffffff	/* maximum unsigned long value */

#endif
