/* Tests for the Fuzix libc strtol()/strtoul() implementation.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#if 1
#define strtol fuzix_strtol
#define strtoul fuzix_strtoul
#include "../libs/strtol.c"
#undef strtol
#undef strtoul
#else
#define fuzix_strtol strtol
#define fuzix_strtoul strtoul
#endif

static int failed;

/* Check value returned and number of characters consumed (endptr). */

#define L(s, base, val, end) do { \
		const char *p = (s); char *e; \
		long v = fuzix_strtol(p, &e, (base)); \
		if (v != (val) || e - p != (end)) { \
			printf("FAIL strtol(\"%s\", %d) = %ld end+%ld, want %ld end+%d\n", \
				p, (base), v, (long)(e - p), (long)(val), (end)); \
			failed = 1; } \
	} while (0)

#define UL(s, base, val, end) do { \
		const char *p = (s); char *e; \
		unsigned long v = fuzix_strtoul(p, &e, (base)); \
		if (v != (val) || e - p != (end)) { \
			printf("FAIL strtoul(\"%s\", %d) = %lu end+%ld, want %lu end+%d\n", \
				p, (base), v, (long)(e - p), (unsigned long)(val), (end)); \
			failed = 1; } \
	} while (0)

int main(void)
{
	char big[40];

	/* A digit whose value equals the base is invalid and must stop the scan. */
	L("42abc", 10, 42, 2);
	L("1g", 16, 1, 1);
	L("z0", 35, 0, 0);
	L("8", 8, 0, 0);

	/* ...but the largest legal digit (base - 1) is still accepted. */
	L("z", 36, 35, 1);
	L("ff", 16, 255, 2);
	L("10", 2, 2, 2);

	/* Ordinary conversions. */
	L("  -42", 10, -42, 5);
	L("0x1F", 0, 31, 4);
	L("0777", 0, 511, 4);

	/* Overflow clamps to the type limit; strtol and strtoul clamp differently. */
	L("999999999999999999999999999999", 10, LONG_MAX, 30);
	L("-999999999999999999999999999999", 10, LONG_MIN, 31);
	UL("999999999999999999999999999999", 10, ULONG_MAX, 30);

	/* Above LONG_MAX but within unsigned long: strtol clamps, strtoul does not. */
	sprintf(big, "%lu", (unsigned long)LONG_MAX + 100);
	L(big, 10, LONG_MAX, (int)strlen(big));
	UL(big, 10, (unsigned long)LONG_MAX + 100, (int)strlen(big));

	/* Overflow must also be caught in a non-decimal base, where number * base
	   can wrap the whole range without the running value getting smaller. */
	UL("9999999999999999999999", 16, ULONG_MAX, 22);
	UL("-9999999999999999999999", 16, ULONG_MAX, 23);

	L("   ", 10, 0, 0);	/* leading whitespace, nothing else */
	L("+", 10, 0, 0);	/* sign only */
	L("-", 10, 0, 0);
	L("  +", 10, 0, 0);	/* whitespace and sign */
	L("-z", 10, 0, 0);	/* sign, then a non-digit */
	L("0x", 16, 0, 1);	/* no hex digit: converts 0, leaves "x" */

	return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
