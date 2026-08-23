#ifndef MMB_CRC_H
#define MMB_CRC_H
/*
 *	MATH(CRC8 ...) / MATH(CRC12 ...) / MATH(CRC16 ...) / MATH(CRC32 ...)
 *
 *	    MATH(CRC16 v [,length [,polynomial [,startmask
 *	                          [,endmask [,reverseIn [,reverseOut]]]]]])
 *
 *	A bit-serial CRC over a numeric array or a string, with every
 *	parameter of the algorithm exposed and defaulted the way MMBasic
 *	defaults it (core/MATHS.c, fun_math's four CRC branches):
 *
 *	    width   polynomial   masks    argument range
 *	      8       0x07         0        0..255
 *	     12       0x80D        0        0..4095
 *	     16       0x1021       0        0..65535
 *	     32       0x04C11DB7   0        0..0xFFFFFFFF
 *
 *	An omitted argument - and an EMPTY one, `MATH(CRC16 a(), , , 3)`,
 *	which MMBasic allows and programs use - takes the default.
 *
 *	WHY A HEADER rather than the runtime: the same argument mmb_math.h
 *	makes.  Four engines and their reversal helpers are a few hundred
 *	bytes in the ONE program that says CRC, instead of four more names
 *	in bcrun's table carried by every program on the machine.
 *
 *	ONE ENGINE, not four.  MMBasic has four functions differing only
 *	in width - and, where they should not differ, in two details this
 *	implementation deliberately does not copy (below).  With those
 *	settled, crc8/12/16/32 are one loop with a width parameter, which
 *	is also the only way the four stay in step.
 *
 *	THREE DELIBERATE DIVERGENCES from PicoMite 6.03.00, agreed with
 *	the author, who is fixing the reference.  Until that ships, a
 *	side-by-side against 6.03.00 will differ HERE and nowhere else:
 *
 *	1. CRC12 is masked to 12 bits.  The reference shifts a uint16_t
 *	   while testing bit 11 and never masks (MATHS.c:504-536), so
 *	   bits 12-15 accumulate and the answer carries a junk top nibble:
 *	   MATH(CRC12) of "123456789" returns &HFEFB there and &HEFB here,
 *	   which is the canonical CRC-12/CDMA2000 value.
 *
 *	2. reverseOut is read from the seventh argument.  All four
 *	   reference branches guard on argv[12] and then read argv[10]
 *	   (MATHS.c:3042, 3096, 3150, 3204), so passing reverseOut there
 *	   re-reads reverseIn and the parameter cannot be used at all.
 *
 *	3. The end mask is applied AFTER the output reversal, for all four
 *	   widths.  The reference reverses then XORs in crc12/crc16
 *	   (:532, :565) and XORs then reverses in crc8/crc32 (:497, :604).
 *	   Reverse-then-XOR is the definition every published CRC uses -
 *	   it is what makes CRC16 with startmask &HFFFF equal CCITT-FALSE
 *	   - so the other two follow it rather than the other way round.
 *	   With the default endmask of 0 the orders agree, so this shows
 *	   only for a program that passes an end mask AND a reversal.
 *
 *	Everything else is the reference exactly, including what it does
 *	not check: an array element ABOVE 255 raises "Variable > 255"
 *	(StandardError(39)) and a NEGATIVE one is cast to a byte in
 *	silence, as MATHS.c:3143-3163 casts it.
 *
 *	Argument 1 is a 1-D numeric array or a string.  MMBasic requires a
 *	named variable there (parseany goes straight to findvar), where a
 *	string EXPRESSION is accepted here - the same deliberate
 *	permissiveness MATH(BASE64) already has, and a superset, so every
 *	program that works on a PicoMite works here.
 */

/* Bit-reverse, the reference's own helpers folded into one: reverse12
 * is reverse16 >> 4 there (MATHS.c:421), and reverse16 of a 16-bit
 * value is reverse32 >> 16, so every width is one shift off reverse32
 * (MATHS.c:401-434). */
static unsigned long mmg_crc_rev32(unsigned long x)
{
	x = ((x & 0xAAAAAAAAUL) >> 1) | ((x & 0x55555555UL) << 1);
	x = ((x & 0xCCCCCCCCUL) >> 2) | ((x & 0x33333333UL) << 2);
	x = ((x & 0xF0F0F0F0UL) >> 4) | ((x & 0x0F0F0F0FUL) << 4);
	x = ((x & 0xFF00FF00UL) >> 8) | ((x & 0x00FF00FFUL) << 8);
	return ((x >> 16) | (x << 16)) & 0xFFFFFFFFUL;
}

/* The parameters, gathered once so the four entry points below can
 * share both the validation and the loop. */
struct mmg_crc_arg {
	int bits;
	unsigned long poly, start, end, mask;
	int revin, revout;
	unsigned long crc;
};

static int mmg_crc_open(struct mmg_crc_arg *c, MMINTEGER bits,
			MMINTEGER poly, MMINTEGER start, MMINTEGER end,
			MMINTEGER revin, MMINTEGER revout)
{
	c->bits = (int)bits;
	c->mask = (bits == 32) ? 0xFFFFFFFFUL
			       : ((1UL << (int)bits) - 1UL);
	/* getint's range in each branch is exactly the width
	 * (MATHS.c:3034, 3088, 3142, 3196), and out of it MMBasic says
	 * "Number out of bounds". */
	if (poly < 0 || (unsigned long)poly > c->mask
	    || start < 0 || (unsigned long)start > c->mask
	    || end < 0 || (unsigned long)end > c->mask
	    || revin < 0 || revin > 1 || revout < 0 || revout > 1) {
		mm_error("Number out of bounds");
		return 0;
	}
	c->poly = (unsigned long)poly;
	c->start = (unsigned long)start;
	c->end = (unsigned long)end;
	c->revin = (int)revin;
	c->revout = (int)revout;
	c->crc = c->start;
	return 1;
}

/* One byte through the shift register.  MMBasic copies the whole
 * source into GetTempMainMemory first and then runs the engine; going
 * a byte at a time reaches the same answer with no buffer, and raises
 * the same error at the same element. */
static void mmg_crc_byte(struct mmg_crc_arg *c, unsigned d)
{
	unsigned long top = 1UL << (c->bits - 1);
	int i;

	if (c->revin)
		d = (unsigned)(mmg_crc_rev32(d) >> 24);
	c->crc ^= ((unsigned long)(d & 0xFF)) << (c->bits - 8);
	for (i = 8; i; i--) {
		if (c->crc & top)
			c->crc = ((c->crc << 1) ^ c->poly) & c->mask;
		else
			c->crc = (c->crc << 1) & c->mask;
	}
}

static MMINTEGER mmg_crc_close(struct mmg_crc_arg *c)
{
	if (c->revout)
		c->crc = mmg_crc_rev32(c->crc) >> (32 - c->bits);
	c->crc ^= c->end;
	return (MMINTEGER)(c->crc & c->mask);
}

/* How many elements to run over.  MMBasic's parseany: 0 means the
 * whole thing, and asking for more than there is is "Array size"
 * (StandardError(17)) for an array and "String size" for a string. */
static int mmg_crc_len(MMINTEGER len, int have, int is_str)
{
	if (len == 0)
		return have;
	if (len < 0 || len > have) {
		mm_error(is_str ? "String size" : "Array size");
		return -1;
	}
	return (int)len;
}

static MMINTEGER mmg_crc_i(MMINTEGER bits, const MMINTEGER *a, int cnt,
			   MMINTEGER len, MMINTEGER poly, MMINTEGER start,
			   MMINTEGER end, MMINTEGER revin, MMINTEGER revout)
{
	struct mmg_crc_arg c;
	int n, i;

	if (!mmg_crc_open(&c, bits, poly, start, end, revin, revout))
		return 0;
	if ((n = mmg_crc_len(len, cnt, 0)) < 0)
		return 0;
	for (i = 0; i < n; i++) {
		if (a[i] > 255)
			MM_RAISEV("Variable > 255", 0);
		mmg_crc_byte(&c, (unsigned)(unsigned char)a[i]);
	}
	return mmg_crc_close(&c);
}

static MMINTEGER mmg_crc_f(MMINTEGER bits, const MMFLOAT *a, int cnt,
			   MMINTEGER len, MMINTEGER poly, MMINTEGER start,
			   MMINTEGER end, MMINTEGER revin, MMINTEGER revout)
{
	struct mmg_crc_arg c;
	int n, i;

	if (!mmg_crc_open(&c, bits, poly, start, end, revin, revout))
		return 0;
	if ((n = mmg_crc_len(len, cnt, 0)) < 0)
		return 0;
	for (i = 0; i < n; i++) {
		if (a[i] > 255.0)
			MM_RAISEV("Variable > 255", 0);
		mmg_crc_byte(&c, (unsigned)(unsigned char)(int)a[i]);
	}
	return mmg_crc_close(&c);
}

static MMINTEGER mmg_crc_s(MMINTEGER bits, const char *s, MMINTEGER len,
			   MMINTEGER poly, MMINTEGER start, MMINTEGER end,
			   MMINTEGER revin, MMINTEGER revout)
{
	struct mmg_crc_arg c;
	int n, i;

	if (!mmg_crc_open(&c, bits, poly, start, end, revin, revout))
		return 0;
	if ((n = mmg_crc_len(len, (int)mm_slen(s), 1)) < 0)
		return 0;
	for (i = 0; i < n; i++)
		mmg_crc_byte(&c, (unsigned char)s[1 + i]);
	return mmg_crc_close(&c);
}

#endif /* MMB_CRC_H */
