#ifndef MMB_MT_H
#define MMB_MT_H
/*
 *	MATH RANDOMIZE and MATH(RAND) - the Mersenne Twister pair.
 *
 *	THESE TWO ARE A PAIR AND HAVE NOTHING TO DO WITH `RND`.  That is
 *	worth saying at the top because the names invite the opposite
 *	assumption, and this translator acted on the wrong one until
 *	2026-08-24: `MATH RANDOMIZE` used to seed the generator `RND`
 *	draws from, so a program that seeded got a reproducible `RND`
 *	here and a hardware-random one on a PicoMite.
 *
 *	The division of labour in MMBasic on the RP2350:
 *
 *	  RND / RND(n)     rand(), reseeded every hundred calls from the
 *	                   hardware generator.  Not reproducible, not
 *	                   seedable.
 *	  RANDOMIZE        a NO-OP.  It is an RP2040 statement; the
 *	                   RP2350 has nothing for it to do.
 *	  MATH RANDOMIZE   seeds THIS generator, and only this one.
 *	  MATH(RAND)       draws from it.  Unseeded, the first call seeds
 *	                   from the microseconds since boot.
 *
 *	A SEPARATE HEADER because the state is 624 words - 2.5K of BSS -
 *	and only a program that says RAND or MATH RANDOMIZE should carry
 *	it.  mmb_math.h is included for any MATH member at all, so this
 *	could not live there without charging every one of them for it.
 *	Same bargain as mmb_crc.h and mmb_sort.h.
 *
 *	THE SEEDER USES 69069, Knuth's constant (TAOCP Vol 2, 2nd ed,
 *	p.102, Table 1 line 25).  This used it while the reference still
 *	had the typo 6069, on the author's ruling that MMBasic would be
 *	corrected; 4861a1a corrected it, so the two now agree.  The same
 *	commit made seed 0 seed as 4357 (an all-zero state stays zero, so
 *	RAND was 0 for ever), divided by 2^32 so RAND is below 1.0 as the
 *	manual says, ranged MATH RANDOMIZE's seed to 0..4294967295, and made
 *	it reseed from the clock when given none.  All copied.
 */

#ifndef MMG_FN
#if defined(MM_FCC) || defined(MM_PC3)
#define MMG_FN static
#else
#define MMG_FN static __inline__ __attribute__((unused))
#endif
#endif

#define MMG_MT_N 624
#define MMG_MT_M 397
#define MMG_MT_UPPER 0x80000000UL
#define MMG_MT_LOWER 0x7fffffffUL
#define MMG_MT_TB    0x9d2c5680UL
#define MMG_MT_TC    0xefc60000UL

static unsigned long mmg_mt[MMG_MT_N];
static int mmg_mt_index = MMG_MT_N + 1;   /* "never seeded" */

MMG_FN void mmg_mt_seed(unsigned long seed)
{
	int i;

	/* Knuth, TAOCP Vol 2 (2nd ed) p.102, Table 1 line 25; an all-zero
	   state stays zero, so 0 seeds as the generator's default 4357 */
	mmg_mt[0] = (seed & 0xffffffffUL) ? (seed & 0xffffffffUL) : 4357;
	for (i = 1; i < MMG_MT_N; i++)
		mmg_mt[i] = (69069UL * mmg_mt[i - 1]) & 0xffffffffUL;
	mmg_mt_index = i;
}

MMG_FN unsigned long mmg_mt_long(void)
{
	static unsigned long mag[2] = { 0x0UL, 0x9908b0dfUL };
	unsigned long y;
	int kk;

	if (mmg_mt_index >= MMG_MT_N || mmg_mt_index < 0) {
		if (mmg_mt_index >= MMG_MT_N + 1 || mmg_mt_index < 0)
			mmg_mt_seed(4357);
		for (kk = 0; kk < MMG_MT_N - MMG_MT_M; kk++) {
			y = (mmg_mt[kk] & MMG_MT_UPPER)
			  | (mmg_mt[kk + 1] & MMG_MT_LOWER);
			mmg_mt[kk] = mmg_mt[kk + MMG_MT_M] ^ (y >> 1)
				   ^ mag[y & 0x1];
		}
		for (; kk < MMG_MT_N - 1; kk++) {
			y = (mmg_mt[kk] & MMG_MT_UPPER)
			  | (mmg_mt[kk + 1] & MMG_MT_LOWER);
			mmg_mt[kk] = mmg_mt[kk + (MMG_MT_M - MMG_MT_N)]
				   ^ (y >> 1) ^ mag[y & 0x1];
		}
		y = (mmg_mt[MMG_MT_N - 1] & MMG_MT_UPPER)
		  | (mmg_mt[0] & MMG_MT_LOWER);
		mmg_mt[MMG_MT_N - 1] = mmg_mt[MMG_MT_M - 1] ^ (y >> 1)
				     ^ mag[y & 0x1];
		mmg_mt_index = 0;
	}
	y = mmg_mt[mmg_mt_index++];
	y ^= (y >> 11);
	y ^= (y << 7) & MMG_MT_TB;
	y ^= (y << 15) & MMG_MT_TC;
	y ^= (y >> 18);
	return y & 0xffffffffUL;
}

/*	MATH RANDOMIZE [seed]: a seed is 0..4294967295 (getint's wording);
 *	none takes the microseconds since boot, kept positive as the
 *	reference keeps time_us_32() (MATHS.c:2741). */
MMG_FN void mmg_mt_randomize(MMINTEGER seed, int given)
{
	if (given) {
		if (mm_range(seed, 0, 0xFFFFFFFFLL))
			return;
	} else
		seed = (MMINTEGER)(mm_us() & 0x7FFFFFFFUL);
	mmg_mt_seed((unsigned long)seed);
}

/*	MATH(RAND)
 *
 *	Unseeded, the first call takes the microseconds since boot, which
 *	is what the reference does.  The divisor is 2^32, so 0.0 <= n < 1.0
 *	as the manual says (4861a1a: 0xffffffff gave 1.0 for an all-ones
 *	draw).
 */
MMG_FN MMFLOAT mmg_mt_rand(void)
{
	if (mmg_mt_index >= MMG_MT_N + 1) {
		/* Unseeded: the reference takes the microseconds since
		   boot.  The hardware generator is better and is what an
		   unseeded draw actually wants, so it is preferred where
		   there is a kernel to ask; the clock is the fallback,
		   and is what the reference uses. */
		MMINTEGER e = mm_rand32();

		mmg_mt_seed((unsigned long)(e ? e : mm_us()));
	}
	return (MMFLOAT)mmg_mt_long() / 4294967296.0;
}

#endif /* MMB_MT_H */
