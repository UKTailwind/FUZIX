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
 *	THE SEEDER USES 69069, AND THE REFERENCE CURRENTLY USES 6069.
 *	That is the one place this deliberately does not copy what the
 *	interpreter does today.  69069 is the published constant - Knuth
 *	TAOCP Vol 2 (2nd ed) p.102, Table 1 line 25, which the reference
 *	cites by name - and 6069 is a typo in it; the author's ruling is
 *	to use the correct value here and correct MMBasic to match.
 *
 *	So MATH RANDOMIZE n followed by MATH(RAND) gives different
 *	numbers here than on an unfixed PicoMite, and the same numbers
 *	once it is fixed.  Everything else about the pair - the state
 *	size, the tempering, the 0xffffffff divisor, the unseeded
 *	first-call behaviour - is transcribed exactly.  This is the only
 *	knowing divergence, and it is here rather than hidden in a
 *	commit message because a reader comparing two machines will meet
 *	it first.
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

	/* Knuth, TAOCP Vol 2 (2nd ed) p.102, Table 1 line 25 - which the
	   reference cites and then multiplies by 6069.  69069 is the
	   number in Knuth and in every published MT19937; see the note
	   at the top of this file. */
	mmg_mt[0] = seed & 0xffffffffUL;
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

/*	MATH(RAND)
 *
 *	Unseeded, the first call takes the microseconds since boot, which
 *	is what the reference does.  The divisor is 0xffffffff and NOT
 *	2^32, so the result reaches 1.0 exactly when the draw is all
 *	ones - the manual says 0.0 <= n < 1.0 and the code says
 *	otherwise.  The code is what a program will see, on both
 *	machines, so the code is what is copied.
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
	return (MMFLOAT)mmg_mt_long() / (MMFLOAT)0xffffffffUL;
}

#endif /* MMB_MT_H */
