/*
 *	Tokenizer
 *
 *	It might be nicer to switch to an algorithm with less meta-data
 *	but we have to balance code size/data size/speed
 */


#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>

#include "symtab.h"
#include "token.h"
#include "target.h"

#if defined(__linux__)
/* _itoa */
static char buf[7];

char *_uitoa(unsigned int i)
{
	char *p = buf + sizeof(buf);
	int c;

	*--p = '\0';
	do {
		c = i % 10;
		i /= 10;
		*--p = '0' + c;
	} while (i);
	return p;
}

char *_itoa(int i)
{
	char *p;
	if (i >= 0)
		return _uitoa(i);
	p = _uitoa(-i);
	*--p = '-';
	return p;
}

#endif


static unsigned char filename[33] = { "<stdin>" };

static unsigned filechange = 1;

static int isoctal(unsigned char c)
{
	if (c >= '0' && c <= '7')
		return 1;
	return 0;
}

static int iscsymbol(unsigned char c)
{
	if (c == '_' || isalnum(c))
		return 1;
	return 0;
}

static int iscsymstart(unsigned char c)
{
	if (c == '_' || isalpha(c))
		return 1;
	return 0;
}

/*
 *	Glue for now
 */

static unsigned err;
static unsigned line = 1;
static unsigned oldline = 0;

static void colonspace(void)
{
	write(2, ": ", 2);
}

static void writes(const char *p)
{
	unsigned len = strlen(p);
	write(2, p, len);
}

static void report(char code, const char *p)
{
	writes((const char *) filename);
	colonspace();
	writes(_itoa(line));
	colonspace();
	write(2, &code, 1);
	colonspace();
	writes(p);
	write(2, "\n", 1);
}

void error(const char *p)
{
	report('E', p);
	err++;
}

void warning(const char *p)
{
	report('W', p);
}

void fatal(const char *p)
{
	error(p);
	exit(1);
}

#define BLOCK 512

static uint8_t buffer[BLOCK];	/* 128 for CPM */
static uint8_t *bufptr = buffer + BLOCK;
static uint16_t bufleft = 0;

/* Pull the input stream in blocks and optimize for our case as this
   is of course a very hot path. This design allows for future running
   on things like CP/M and with the right block size is also optimal for
   Fuzix */

static unsigned bgetc(void)
{
	if (bufleft == 0) {
		bufleft = read(0, buffer, BLOCK);
		if (bufleft == 0)
			return EOF;
		bufptr = buffer;
	}
	bufleft--;
	return *bufptr++;
}

static unsigned pushback;
static unsigned pbstack[2];
static unsigned isnl = 1;
static unsigned lastbslash;

static void directive(void);

unsigned get(void)
{
	int c;
	if (pushback) {
		c = pbstack[--pushback];
		pushback = 0;
		if (c == '\n') {
			isnl = 1;
			line++;
		}
		return c;
	}
	c = bgetc();
	while (c == '#' && isnl) {
		directive();
		c = bgetc();
	}
	isnl = 0;
	if (c == '\n') {
		line++;
		isnl = 1;
	}
	/* backslash newline continuation */
	if (lastbslash && c == '\n')
		c = bgetc();

	if (c == '\\')
		lastbslash = 1;
	else
		lastbslash = 0;

	if (c == EOF)
		return 0;
	return c;
}

unsigned get_nb(void)
{
	unsigned c;
	do {
		c = get();
	} while (c && isspace(c));
	return c;
}

void unget(unsigned c)
{
	if (pushback > 2)
		fatal("double pushback");
	pbstack[pushback++] = c;
	if (c == '\n')
		line--;
}

void required(unsigned cr)
{
	unsigned c = get();
	if (c != cr) {
		error("expected quote");
		unget(c);
	}
}

/* # directive from cpp # line file - # line "file" */
/* TODO file name saving */
static void directive(void)
{
	unsigned char *p = filename;
	unsigned c;

	line = 0;

	do {
		c = bgetc();
	} while (isspace(c));

	while (isdigit(c)) {
		line = 10 * line + c - '0';
		c = bgetc();
	}
	if (c == '\n')
		return;

	/* Should be a quote next */
	c = bgetc();
	if (c == '"') {
		while ((c = bgetc()) != EOF && c != '"') {
			/* Skip magic names */
			if (p == filename && c == '<')
				p = filename + 32;
			if (c == '/')
				p = filename;
			else if (p < filename + 32)
				*p++ = c;
		}
		filechange = 1;
	}
	*p = 0;
	while ((c = bgetc()) != EOF) {
		if (c == '\n')
			return;
	}
	fatal("bad cpp");
}


#define NHASH	64

/* We could infer the symbol number from the table position in theory */

static struct name symbols[MAXNAME];
static struct name *nextsym = symbols;
static struct name *symbase;	/* Base of post keyword symbols */
static struct name *symhash[NHASH];
/* Start of symbol range */
static unsigned symnum = T_SYMBOL;

/*
 *	Add a symbol to our symbol tables as we discover it. Log the
 *	fact if tracing.
 */
static struct name *new_symbol(const char *name, unsigned hash, unsigned id)
{
	struct name *s;
	if (nextsym == symbols + MAXNAME)
		fatal("too many sybmols");
	s = nextsym++;
	strncpy(s->name, name, NAMELEN);
	s->next = symhash[hash];
	s->id = id;
	symhash[hash] = s;
	return s;
}

/*
 *	Find a symbol in a given has table	
 */
static struct name *find_symbol(const char *name, unsigned hash)
{
	struct name *s = symhash[hash];
	while (s) {
		if (strncmp(s->name, name, NAMELEN) == 0)
			return s;
		s = s->next;
	}
	return NULL;
}

/*
 *	A simple but adequate hashing algorithm. A better one would
 *	be worth it for performance.
 */
static unsigned hash_symbol(const char *name)
{
	int hash = 0;
	uint8_t n = 0;

	while (*name && n++ < NAMELEN)
		hash += *name++;
	return (hash & (NHASH - 1));
}

static void write_symbol_table(void)
{
	unsigned len = (uint8_t *) nextsym - (uint8_t *) symbase;
	uint8_t n[2];

	/* FIXME: proper temporary file! */
	int fd = open(".symtmp", O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd == -1) {
		perror(".symtmp");
		exit(1);
	}
	n[0] = len;
	n[1] = len >> 8;
	if (write(fd, n, 2) != 2 || write(fd, symbase, len) != len)
		error("symbol I/O");
	close(fd);
}

/*
 *	Token stream writing. We have a single special case to handle
 *	which is strings.
 */

static uint8_t outbuf[BLOCK];
static uint8_t *outptr = outbuf;

static void outbyte(unsigned char c)
{
	*outptr++ = c;
	if (outptr == outbuf + BLOCK) {
		outptr = outbuf;
		if (write(1, outbuf, BLOCK) != BLOCK)
			error("I/O");
	}
}

static void outflush(void)
{
	unsigned len = outptr - outbuf;
	if (len && write(1, outbuf, len) != len)
		error("I/O");
}

static void outbyte_quoted(unsigned char c)
{
	if (c == 0 || c == 0xFF)
		outbyte(0xFF);
	if (c == 0)
		outbyte(0xFE);
	else
		outbyte(c);
}

static unsigned char tokdata[8];
static unsigned char *tokptr = tokdata;

static void encode_byte(unsigned c)
{
	*tokptr++ = c;
}

static void write_token(unsigned c)
{
	unsigned char *tp;
	unsigned n = 0;
	if (oldline != line || filechange) {
		oldline = line;
		outbyte(T_LINE & 0xFF);
		outbyte(T_LINE >> 8);
		outbyte(line);
		if (filechange) {
			outbyte(0x80 | (line >> 8));
			tp = filename;
			while (*tp && n++ < 32)
				outbyte(*tp++);
			outbyte(0);
		} else
			outbyte(line >> 8);
		filechange = 0;
	}
	/* Write the token, then any data for it */
	outbyte(c);
	outbyte(c >> 8);
	tp = tokdata;
	while (tp < tokptr)
		outbyte(*tp++);
	/* Reset the data pointer */
	tokptr = tokdata;
}

/* C keywords, ignoring all the modern crap */

static const char *keytab[] = {
	/* Types */
	"char",
	"double",
	"enum",
	"float",
	"int",
	"long",
	"short",
	"signed",
	"struct",
	"union",
	"unsigned",
	"void",
	/* Storage classes */
	"auto",
	"extern",
	"register",
	"static",
	/* Modifiers */
	"const",
	"volatile",
	/* Then the rest */
	"break",
	"case",
	"continue",
	"default",
	"do",
	"else",
	"for",
	"goto",
	"if",
	"return",
	"sizeof",
	"switch",
	"typedef",
	"while",
	/* Nonsense */
	"restrict",
	NULL
};

/* Add keywords. These get added first so they head the hash lists */
static void keywords(void)
{
	const char **p = keytab;
	int i = T_KEYWORD;
	unsigned id;

	while (*p) {
		new_symbol(*p, hash_symbol(*p), i++);
		p++;
	}
	symbase = nextsym;

	/*
	 * "main" is interned first, so it always has the id T_MAIN.
	 *
	 * Names reach the later passes as ids, not strings - cc1 has no
	 * name table at all - so without a fixed id there is no way for
	 * it to know it is compiling main. It needs to, in order to give
	 * the implicit "return 0" that C guarantees when control falls off
	 * the end. Costs one table entry in every object.
	 */
	id = new_symbol("main", hash_symbol("main"), symnum++)->id;
	if (id != T_MAIN)
		fatal("main is not T_MAIN");
}

/* Read up to 14 more bytes into the symbol name, plus a terminator */
/*
 *	Collect the rest of an identifier, the caller having taken the
 *	first character.
 *
 *	The limit comes from NAMELEN rather than a number written here:
 *	the two disagreeing is how identifiers ended up significant to
 *	fifteen characters when the table had room for sixteen. Say so
 *	when characters are dropped - a silently truncated name turns two
 *	variables into one and the program still builds.
 */
static void get_symbol_tail(char *p)
{
	unsigned n = NAMELEN - 2;	/* less the first character and the NUL */
	unsigned c;
	unsigned lost = 0;

	while ((c = get()) != 0) {
		if (!iscsymbol(c))
			break;
		if (n) {
			n--;
			*p++ = c;
		} else
			lost = 1;
	}
	*p = 0;
	if (lost)
		warning("identifier truncated");
	unget(c);
}

/* Also does keywords */
static unsigned tokenize_symbol(unsigned c)
{
	char symstr[NAMELEN];
	unsigned h;
	struct name *s;
	*symstr = c;
	get_symbol_tail(symstr + 1);
	/* We can't do cunning tricks to spot labels in this pass because
	   foo: is ambiguous between a label and a ?: */
	h = hash_symbol(symstr);
	s = find_symbol(symstr, h);
	if (s)
		return s->id;
	return new_symbol(symstr, h, symnum++)->id;
}

/*
 *	Software floating point encoding. This is a bit long winded
 *	because we want it to work on an 8bit micro on a compiler that
 *	has no floating point, so that you can bootstrap an FP compiler
 *	with an integer only one. It does now need 64bit integers, which
 *	the token stream work already required.
 *
 *	Everything is encoded as an IEEE754 double and narrowed to a
 *	float afterwards if that is what the target or the suffix asked
 *	for. One path, so one thing to get right and one thing to test.
 */

static uint16_t rtype;
/*
 *	Wide enough for a long long literal, and later for a double. The
 *	token stream carries four bytes for the narrow types and eight for
 *	T_LONGLONGVAL, T_ULONGLONGVAL and T_DOUBLEVAL - see encode_value().
 */
static uint64_t result;

/*
 *	Write the constant into the token stream: four bytes for the
 *	narrow types, eight for the wide ones. cc1's next_token() reads
 *	the matching number back, keyed on the same token codes.
 */
static void encode_value(void)
{
	encode_byte(result);
	encode_byte(result >> 8);
	encode_byte(result >> 16);
	encode_byte(result >> 24);
	if (rtype == T_LONGLONGVAL || rtype == T_ULONGLONGVAL ||
	    rtype == T_DOUBLEVAL) {
		encode_byte(result >> 32);
		encode_byte(result >> 40);
		encode_byte(result >> 48);
		encode_byte(result >> 56);
	}
}

static void overflow(void)
{
	error("overflow");
}

static void exp_overflow(void)
{
	warning("exponent under/overflow");
}

static unsigned is_fp_token(unsigned t)
{
	return t == T_FLOATVAL || t == T_DOUBLEVAL;
}

/*
 *	The working value while a floating constant is being built. It is a
 *	128bit fixed point number held as two 64bit halves and a binary
 *	exponent, and it means
 *
 *		(dhi + dlo / 2^64) * 2^dexp
 *
 *	The parsers put the significant digits into dhi and set dexp, then
 *	call convert_double() with whatever power of ten is left over.
 *
 *	128 bits sounds excessive for a 53 bit mantissa, but scaling by a
 *	decimal exponent means up to 300-odd multiplies or divides by ten,
 *	each of which throws away the bottom bit or so. The 75 spare bits
 *	are what stops that reaching the mantissa.
 */
static uint64_t dhi, dlo;
static int dexp;

/*
 *	dhi is kept normalised with bit 59 set, so it is a 60bit value and
 *	ten times it still fits in 64 bits. That is the whole reason for
 *	the odd looking constant.
 */
#define DNORM		0x0800000000000000ULL	/* bit 59 */
#define DMANT		0x0010000000000000ULL	/* bit 52, the mantissa top */
#define DMANTMASK	0x000FFFFFFFFFFFFFULL
#define DHALF		0x8000000000000000ULL	/* half an ulp of dhi */

static void dshl(void)
{
	dhi = (dhi << 1) | (dlo >> 63);
	dlo <<= 1;
	dexp--;
}

static void dshr(void)
{
	dlo = (dlo >> 1) | (dhi << 63);
	dhi >>= 1;
	dexp++;
}

/* Caller guarantees the value is not zero, or this does not terminate */
static void dnorm(void)
{
	while (dhi >= (DNORM << 1))
		dshr();
	while (!(dhi & DNORM))
		dshl();
}

/* Multiply the 128bit value by ten, in 32bit pieces so that no step
   needs more than 64 bits of intermediate */
static void dmul10(void)
{
	uint32_t l0 = (uint32_t) dlo;
	uint32_t l1 = (uint32_t) (dlo >> 32);
	uint64_t t;

	t = (uint64_t) l0 * 10;
	l0 = (uint32_t) t;
	t = (uint64_t) l1 * 10 + (t >> 32);
	l1 = (uint32_t) t;
	dlo = ((uint64_t) l1 << 32) | l0;
	/* Normalised, so dhi is under 2^60 and this cannot overflow */
	dhi = dhi * 10 + (t >> 32);
}

/* And divide it by ten, long division carrying the remainder down */
static void ddiv10(void)
{
	uint64_t r, t;

	r = dhi % 10;
	dhi /= 10;
	t = (r << 32) | (dlo >> 32);
	r = t % 10;
	t /= 10;
	dlo = (t << 32) | (((r << 32) | (dlo & 0xFFFFFFFFULL)) / 10);
}

/*
 *	Scale the working value by ten to the uexp and assemble an IEEE754
 *	double. This is the only part that knows the format, along with
 *	narrow_float() and the sign flip in tokenize_numeric().
 *
 *	Once normalised to bit 52 the value is 1.m * 2^(52 + dexp), so the
 *	stored exponent is dexp + 52 + 1023. That is where the 1075 comes
 *	from; the float path used to spell the same sum 22 + 128, being
 *	23 + 127.
 */
static void convert_double(int uexp)
{
	int e;

	rtype = T_DOUBLEVAL;
	if (dhi == 0 && dlo == 0) {
		result = 0;
		return;
	}
	dnorm();
	while (uexp > 0) {
		dmul10();
		dnorm();
		uexp--;
	}
	while (uexp < 0) {
		ddiv10();
		dnorm();
		uexp++;
	}
	/* Bit 59 down to bit 52, keeping what falls off it for rounding */
	for (e = 0; e < 7; e++)
		dshr();
	e = dexp + 1075;
	if (e < 1) {
		/*
		 * Denormal. Shift down to the fixed exponent of 2^-1074 and
		 * let the rounding below happen there - rounding at bit 52
		 * first and then shifting would round twice, and two
		 * roundings do not add up to one.
		 */
		int s = 1 - e;
		if (s > 54) {
			exp_overflow();
			result = 0;
			return;
		}
		while (s--)
			dshr();
		e = 0;
	}
	/* Round to nearest, ties to even, as gcc does */
	if (dlo > DHALF || (dlo == DHALF && (dhi & 1))) {
		dhi++;
		if (dhi & (DMANT << 1)) {	/* carried out of the mantissa */
			dhi >>= 1;
			e++;
		}
	}
	if (e > 2046) {
		exp_overflow();
		result = 0x7FF0000000000000ULL;		/* Infinity */
		return;
	}
	if (e == 0) {
		/* The exponent field sits directly above the mantissa, so a
		   round up into bit 52 turns the largest denormal into the
		   smallest normal on its own */
		result = dhi;
		return;
	}
	result = (dhi & DMANTMASK) | ((uint64_t) e << 52);
}

/*
 *	Narrow the double in result to an IEEE754 float, rounding the same
 *	way. Used when the target has no double, or for an F suffix.
 */
static void narrow_float(void)
{
	uint64_t m = (result & DMANTMASK) | DMANT;	/* implicit bit back */
	uint32_t sign = (uint32_t) ((result >> 32) & 0x80000000UL);
	int e = (int) ((result >> 52) & 0x7FF);
	uint64_t drop, half;
	uint32_t f;
	int s;

	rtype = T_FLOATVAL;
	if (e == 0) {			/* zero, or a double denormal */
		result = sign;
		return;
	}
	if (e == 0x7FF) {
		result = sign | 0x7F800000UL;
		return;
	}
	e -= 896;			/* 1023 - 127 */
	s = 29;				/* 52 - 23 mantissa bits to drop */
	if (e < 1) {
		/* Denormal float: drop more bits instead of lowering the
		   exponent, and round once at the end like the double case */
		s += 1 - e;
		if (s > 55) {
			exp_overflow();
			result = sign;
			return;
		}
		e = 0;
	}
	drop = m & ((1ULL << s) - 1);
	half = 1ULL << (s - 1);
	f = (uint32_t) (m >> s);
	if (drop > half || (drop == half && (f & 1))) {
		f++;
		if (f & 0x01000000UL) {	/* carried out of the mantissa */
			f >>= 1;
			e++;
		}
	}
	if (e > 254) {
		exp_overflow();
		result = sign | 0x7F800000UL;
		return;
	}
	if (e == 0)			/* As above: the carry sorts itself out */
		result = sign | f;
	else
		result = sign | ((uint32_t) e << 23) | (f & 0x007FFFFFUL);
}

/* An integer constant that has picked up a floating point suffix */
static void int_to_double(void)
{
	dhi = result;
	dlo = 0;
	dexp = 0;
	convert_double(0);
}

/* Assumes IEEE754, which is the whole point of the above */
static void negate_constant(void)
{
	if (rtype == T_DOUBLEVAL)
		result ^= 0x8000000000000000ULL;
	else if (rtype == T_FLOATVAL)
		result ^= 0x80000000UL;
	else
		result = -result;
}

/* After the E or P in a floating point value is a signed decimal exponent.
   Parse this */
static int parse_exponent(void)
{
	uint32_t sum = 0, n;
	int neg = 1;
	unsigned c;

	c = get();
	if (c == '-') {
		neg = -1;
		c = get();
	} else if (c == '+')
		c = get();

	/* Parse integer digits only */
	while (isdigit(c)) {
		c -= '0';
		n = sum * 10 + c;
		if (n < sum)
			overflow();
		sum = n;
		c = get();
	}
	unget(c);
	/*
	 * A double reaches 1e308, so the old limit of 128 was a float
	 * limit and is now wrong. Clamp rather than just warn: the value
	 * becomes the trip count of the scaling loop, and 1e99999999 would
	 * otherwise sit there multiplying by ten until the heat death of
	 * the machine.
	 */
	if (sum > 4096) {
		exp_overflow();
		sum = 4096;
	}
	return sum * neg;
}

/*
 *	Decimal, in the form digits.digitsEdigits.
 *
 *	There is no table of decimal fractions any more. Every significant
 *	digit, on either side of the point, goes into the same 64bit
 *	significand and the point is just a place where the decimal
 *	exponent starts counting down, so the value is always
 *
 *		mant * 10^decexp
 *
 *	and 0.0000000000000000000000000000000001 is no harder than 1.0.
 *	The old table had nine entries ending in 0x2A and 0x4, so anything
 *	past the ninth decimal place was thrown away before precision was
 *	even the question.
 *
 *	64 bits holds 19 digits and 17 are enough to pin down a double, so
 *	digits past the nineteenth only move decexp.
 */

/* True while the significand can still take another digit */
#define FITS(m, c)	((m) <= (0xFFFFFFFFFFFFFFFFULL - (c)) / 10)

/*
 *	Everything from the point onwards, given the digits already seen as
 *	mant * 10^decexp and the character that ended them.
 *
 *	This is shared because a decimal float can arrive with a leading
 *	zero - 015.5 is perfectly good C - and that goes to the octal
 *	scanner first. It used to come back out as the octal integer 13
 *	followed by a second constant .5, silently.
 */
static void float_tail(unsigned c, uint64_t mant, int decexp)
{
	int uex = 0;

	if (c == '.') {
		while (1) {
			c = get();
			if (c == 'E' || c == 'e')
				break;
			if (!isdigit(c)) {
				unget(c);
				break;
			}
			c -= '0';
			if (FITS(mant, c)) {
				mant = mant * 10 + c;
				decexp--;
			}
			/* Otherwise it is below the mantissa: drop it, and
			   the exponent does not move because the digit was
			   after the point */
		}
	}
	if (c == 'E' || c == 'e')
		uex = parse_exponent();

	dhi = mant;
	dlo = 0;
	dexp = 0;
	convert_double(decexp + uex);
}

static void dec_format(unsigned c)
{
	/*
	 * The integer value at full width. This is the one that has to be
	 * exact for an integer literal - 5000000000 needs more than 32
	 * bits and used to lose them here.
	 */
	uint64_t isum = 0;
	unsigned iov = 0;
	uint64_t mant = 0;
	int decexp = 0;

	/* Parse digits before . : could be integer or float */
	while (c != 'E' && c != 'e' && c != '.') {
		if (!isdigit(c)) {
			/* Done */
			unget(c);
			if (iov) {
				overflow();
				return;
			}
			result = isum;
			rtype = T_INTVAL;
			return;
		}
		c -= '0';
		if (isum > (0xFFFFFFFFFFFFFFFFULL - c) / 10)
			iov = 1;
		else
			isum = isum * 10 + c;
		if (FITS(mant, c))
			mant = mant * 10 + c;
		else
			decexp++;	/* Digit is below the mantissa */
		c = get();
	}
	/* We have done the integer part, and found floaty stuff */
	float_tail(c, mant, decexp);
}

/*
 *	Hex format
 *	- parse a hex number
 *	- if we find a P or a . then it's a float
 */

static unsigned unhex(unsigned c)
{
	c = toupper(c);
	c -= '0';
	if (c > 9)
		c -= 7;
	return c;
}

/*
 *	Hex, and hex float in the form 0xdigits.digitsPdigits.
 *
 *	A hex digit is four binary bits, so unlike the decimal case there
 *	is no scaling to do: the digits go straight into the mantissa and
 *	the point and the P exponent only move the binary exponent. The
 *	value is exact if it fits in 60 bits, which every sane hex float
 *	does.
 */
static void hex_format(void)
{
	uint64_t sum = 0;
	unsigned c;
	unsigned iov = 0;
	int bexp = 0;
	int uex = 0;

	/* Parse digits before . : could be integer or float */
	while (1) {
		c = get();
		if (c == '.' || c == 'P' || c == 'p')
			break;
		if (!isxdigit(c)) {
			/* Done */
			unget(c);
			if (iov)
				overflow();
			result = sum;
			rtype = T_INTVAL;
			return;
		}
		c = unhex(c);
		if (sum <= 0x0FFFFFFFFFFFFFFFULL)
			sum = (sum << 4) | c;
		else {
			/* Below the mantissa, but the shift still counts */
			iov = 1;
			bexp += 4;
		}
	}
	if (c == '.') {
		while (1) {
			c = get();
			if (c == 'P' || c == 'p')
				break;
			if (!isxdigit(c)) {
				unget(c);
				break;
			}
			c = unhex(c);
			/* Digits after the point buy bits at the bottom */
			if (sum <= 0x0FFFFFFFFFFFFFFFULL) {
				sum = (sum << 4) | c;
				bexp -= 4;
			}
		}
	}
	/* Now look for an exponent */
	if (c == 'P' || c == 'p')
		uex = parse_exponent();

	dhi = sum;
	dlo = 0;
	dexp = bexp + uex;
	convert_double(0);
}

/*
 *	We parsed a 0, so this is 0x, octal, or a decimal float with a
 *	leading zero. Which of the three is not known until the digits run
 *	out, so accumulate it both ways and decide at the end.
 */
static void oct_format(void)
{
	uint64_t sum = 0;		/* As octal */
	uint64_t mant = 0;		/* As the significand of a float */
	int decexp = 0;
	unsigned iov = 0, bad = 0;
	unsigned c;

	c = get();
	if (c == 'x' || c == 'X') {
		hex_format();
		return;
	}

	while (isdigit(c)) {
		c -= '0';
		if (c > 7)
			bad = 1;	/* Only matters if this is an integer */
		if (sum > (0xFFFFFFFFFFFFFFFFULL >> 3))
			iov = 1;
		else
			sum = (sum << 3) | c;
		if (FITS(mant, c))
			mant = mant * 10 + c;
		else
			decexp++;
		c = get();
	}
	if (c == '.' || c == 'E' || c == 'e') {
		float_tail(c, mant, decexp);
		return;
	}
	/* Done, and it really was an integer */
	unget(c);
	if (bad)
		error("invalid octal digit");
	if (iov)
		overflow();
	result = sum;
	rtype = T_INTVAL;
}

/*
 *	Leading digit
 *	0	octal or hex
 *	other	decimal
 *
 *	Parse a C number. The statics result and rtype
 *	are set up as the bits and the float/int status
 */
static void parse_digits(unsigned c)
{
	if (c == '0')
		oct_format();
	else
		dec_format(c);
}


/*
 *	TODO longlong if we add it to the compiler
 */
static unsigned tokenize_numeric(unsigned c, unsigned neg)
{
	unsigned force_unsigned = 0;
	unsigned force_long = 0;
	unsigned force_longlong = 0;
	unsigned force_float = 0;
	unsigned cup;

	parse_digits(c);

	/* Look for trailing type information */
	while (1) {
		c = get();
		cup = toupper(c);
		if (cup == 'F' && !force_float)
			force_float = 1;
		else if (cup == 'U' && !force_unsigned)
			force_unsigned = 1;
		else if (cup == 'L' && !force_long)
			force_long = 1;
		else if (cup == 'L' && !force_longlong)
			force_longlong = 1;	/* the second L of 5000000000LL */
		else {
			unget(c);
			break;
		}
	}
	/* UF is not valid. LF or FL is a long double, which we make a
	   double - it is the widest thing we have */
	if (force_float && force_unsigned)
		error("invalid type specifiers");

	/* An integer constant that has picked up an F suffix */
	if (force_float && !is_fp_token(rtype))
		int_to_double();	/* This also sets rtype */
	/*
	 * A floating constant is a double in C unless the F suffix says
	 * otherwise. Narrow it for the suffix, or unconditionally on a
	 * target that has no double and has to do everything in single
	 * precision.
	 */
#ifdef TARGET_HAS_DOUBLE
	if (rtype == T_DOUBLEVAL && force_float)
		narrow_float();
#else
	if (rtype == T_DOUBLEVAL)
		narrow_float();
#endif
	if (neg)
		negate_constant();
	if (!is_fp_token(rtype) && force_longlong) {
		/* An explicit LL keeps its width whatever the value is */
		rtype = force_unsigned ? T_ULONGLONGVAL : T_LONGLONGVAL;
	} else if (!is_fp_token(rtype) && result > 0xFFFFFFFFUL) {
		/* Too wide for the narrow forms even without a suffix */
		rtype = force_unsigned ? T_ULONGLONGVAL : T_LONGLONGVAL;
	} else if (!is_fp_token(rtype)) {
		/* Anything can be shoved in a ulong */
		rtype = T_ULONGVAL;
		/* FIXME: this needs review for the -32768 case */
		/* Will it fit in a uint ? */
		if (!force_long && result <= TARGET_MAX_UINT) {
			rtype = T_UINTVAL;
			if (!force_unsigned && result <= TARGET_MAX_INT)
				rtype = T_INTVAL;
		} else if (!force_unsigned) {
			/* Maybe a signed long then ? */
			if (result <= TARGET_MAX_LONG)
				rtype = T_LONGVAL;
			/* Will it fit in a signed integer ? */
			if (!force_long && result <= TARGET_MAX_INT)
				rtype = T_INTVAL;
		}
	}
	if (neg)
		negate_constant();
	/* Order really doesn't matter here so stick to LE. We will worry about
	   actual byte order in the code generation */
	encode_value();
	return rtype;
}

static unsigned tokenize_number(unsigned c)
{
	return tokenize_numeric(c, 0);
}

static unsigned tokenize_neg(unsigned c)
{
	return tokenize_numeric(c, 1);
}

/*
 *	\x followed by hex digits.
 *
 *	This used to read exactly two and then assemble them the wrong way
 *	round - "\x40" came out as 0x04 - so every hex escape in every
 *	character constant and string literal was silently wrong.
 *
 *	Unlike an octal escape, which takes at most three digits, C89 puts
 *	no limit on the number of hex digits: \x consumes every one that
 *	follows and the value is truncated to the character type. So "\x1"
 *	is valid too, which the two-digit version rejected.
 */
static unsigned hexpair(void)
{
	unsigned c;
	unsigned n = 0;
	unsigned digits = 0;

	for (;;) {
		c = get();
		if (!isxdigit(c)) {
			unget(c);
			break;
		}
		n = (n << 4) | unhex(c);
		digits++;
	}
	if (!digits) {
		warning("invalid hexadecimal escape");
		return T_INVALID;
	}
	return n & 0xFF;
}

static unsigned octalset(unsigned c)
{
	unsigned int n = c - '0';
	int ct = 1;
	while (ct++ < 3) {
		c = get();
		if (!isoctal(c)) {
			unget(c);
			return n;
		}
		n <<= 3;
		n |= c - '0';
	}
	return n;
}

static unsigned escaped(unsigned c)
{
	/* Simple cases first */
	switch (c) {
	case 'a':
		return 0x07;
	case 'b':
		return 0x08;
	case 'e':
		return 0x1B;	/* Non standard but common */
	case 'f':
		return 0x0C;
	case 'n':
		return 0x0A;
	case 'r':
		return 0x0D;
	case 't':
		return 0x09;
	case 'v':
		return 0x0B;
	case '\\':
		return '\\';
	case '\'':
		return '\'';
	case '"':
		return '"';
	case '?':
		return '?';	/* Not that we suport the trigraph nonsense */
	}
	/* Now the numerics */
	if (c == 'x')
		return hexpair();
	if (isdigit(c))
		return octalset(c);
	warning("invalid escape code");
	return T_INVALID;
}

static unsigned tokenize_char(void)
{
	unsigned c = get();
	unsigned c2;
	if (c != '\\') {
		/* Encode as a value */
		encode_byte(c);
		encode_byte(0);
		encode_byte(0);
		encode_byte(0);
		c = get();
		if (c != '`') {
			unget(c);
			required('\'');
		}
		return T_INTVAL;
	}
	c2 = get();
	c = escaped(c2);
	required('\'');
	if (c == T_INVALID)
		/* Not a valid escape */
		encode_byte(c2);
	else
		encode_byte(c);
	encode_byte(0);
	encode_byte(0);
	encode_byte(0);
	return T_INTVAL;
}

static unsigned tokenize_string(void)
{
	/* We escape any internal \0 or \FF so we can parse this without
	   buffers, and likewise write it to data the other end the same way */
	unsigned c, c2;
	write_token(T_STRING);

	/* This is slightly odd because we do the string catenation here too */
	do {
		while ((c = get()) != '"') {
			if (c != '\\') {
				outbyte_quoted(c);
			} else {
				c2 = get();
				c = escaped(c2);
				if (c == T_INVALID)
					outbyte_quoted(c2);
				else
					outbyte_quoted(c);
			}
		}
		c = get_nb();
	} while (c == '"');
	unget(c);
	outbyte_quoted(0);
	outbyte(0);
	return T_STRING_END;
}

static char *doublesym = "+-=<>|&";
static char *symeq = "+-/*^!|&%<>";
static char *unibyte = "()[]{}&*/%+-?:^<>|~!=;.,";

static unsigned tokenize(void)
{
	unsigned c, c2, c3;
	char *p;

	c = get_nb();
	if (c == 0)
		return T_EOF;
	if (iscsymstart(c))
		return tokenize_symbol(c);
	if (isdigit(c))
		return tokenize_number(c);
	if (c == '\'')
		return tokenize_char();
	if (c == '"')
		return tokenize_string();
	/* Look for things like ++ and the special case of -n for constants */
	c2 = get();
/*	if (c == '-' && isdigit(c2))
		return tokenize_neg(c2); */
	/* Until we fix the negative handling we need to deal with the
	   a = -.1 case specially. When we fix minus parsing this all goes
	   away */
	if (c == '-' && c2 == '.')
		return tokenize_neg(c2);
	/* Funny case - whilst . is a token . followed by a digit is part
	   of a number */
	if (c == '.' && isdigit(c2)) {
		unget(c2);
		return tokenize_number(c);
	}
	if (c2 == c) {
		p = strchr(doublesym, c);
		if (p) {
			if (c == '<' || c == '>') {
				c3 = get();
				if (c3 == '=') {
					if (c == '<')
						return T_SHLEQ;
					return T_SHREQ;
				}
				unget(c3);
			}
			/* Double sym */
			return T_DOUBLESYM + p - doublesym;
		}
	}
	/* Now deal with the other double symbol cases */
	if (c == '-' && c2 == '>')
		return T_POINTSTO;
	if (c == '.' && c2 == '.') {
		c3 = get();
		if (c3 == '.')
			return T_ELLIPSIS;
		unget(c3);
	}
	/* The '=' cases */
	if (c2 == '=') {
		p = strchr(symeq, c);
		if (p)
			return T_SYMEQ + p - symeq;
	}
	unget(c2);
	/* Symbols that only have a 1 byte form */
	p = strchr(unibyte, c);
	if (p)
		return c;	/* Map to self */
	/* Not valid C */
	error("nonsense in C");
	/* I'm a teapot */
	return T_POT;
}

/* Tokenizer as a standalone pass */
int main(int argc, char *argv[])
{
	unsigned t;
	keywords();
	do {
		t = tokenize();
		write_token(t);
	} while (t != T_EOF);
	/* Write the remaining decode */
	outflush();
	write_symbol_table();
	return err;
}
