/*
 * Signed integer constant folding.
 *
 * constify() carries every constant in cval_t, which is UNSIGNED, so a
 * signed operation has to say so.  The relational quartet cast both
 * sides; '/' and '%' cast only the left one, and C's usual arithmetic
 * conversions promptly converted it back - so -7 / 2 folded to
 * 9223372036854775804 and -7 % 2 to 1, on both machines, while the same
 * expressions computed at runtime were right.  A translated BASIC "-7 \
 * 2" found it the moment MOD and \ stopped going through the runtime's
 * mm_idiv/mm_mod for a literal divisor.
 *
 * The casts were also "signed long", which is 64 bits on this host and
 * 32 on the board - the very trap cval_t exists to close (target.h) -
 * so a fold of anything above 2^31 was right when cross compiled and
 * wrong when compiled on the machine.  Both now use scval_t.
 *
 * gcc is the oracle: every line must match the native build.
 */

int printf();

static long long sq = -7 / 2;
static long long sr = -7 % 2;

int main(void)
{
	long long v = -7;
	int i = -7;

	/* the fold that was unsigned */
	printf("%lld\n", (long long)(-7 / 2));
	printf("%lld\n", (long long)(-7 % 2));
	printf("%lld\n", -7LL / 2LL);
	printf("%lld\n", -7LL % 2LL);
	printf("%lld\n", 7LL / -2LL);
	printf("%lld\n", 7LL % -2LL);
	printf("%lld\n", -8LL / 2LL);
	printf("%lld\n", sq);
	printf("%lld\n", sr);

	/* and the same arithmetic at runtime, which was always right:
	   fold and runtime must agree */
	printf("%lld\n", v / 2);
	printf("%lld\n", v % 2);
	printf("%d\n", i / 2);
	printf("%d\n", i % 2);

	/* unsigned division still folds unsigned */
	printf("%llu\n", 7ULL / 2ULL);
	printf("%llu\n", 7ULL % 2ULL);

	/* wider than 32 bits: the "signed long" casts truncated these
	   when cc1 itself was compiled for the board */
	printf("%lld\n", -5000000000LL / 1000LL);
	printf("%lld\n", -5000000000LL % 7LL);
	printf("%lld\n", -5000000000LL >> 4);
	printf("%d\n", -5000000000LL < -4999999999LL);
	printf("%d\n", -5000000000LL > 1LL);

	/* arithmetic shift right of a negative constant */
	printf("%lld\n", -16LL >> 2);
	printf("%d\n", -16 >> 2);

	/* the relational quartet, both signs */
	printf("%d\n", -1 < 1);
	printf("%d\n", -1 > 1);
	printf("%d\n", -2 <= -2);
	printf("%d\n", -2 >= -1);
	return 0;
}
