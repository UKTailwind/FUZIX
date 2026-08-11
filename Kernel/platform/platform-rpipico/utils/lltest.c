/* Fuzix libc's printf had no long-long conversion: "%llX" read the
   vararg alignment padding and printed 0, silently.  That is what put a
   zero where every &H constant should have been in a program translated
   on the board.  This is the gate for the fix - a native program, so it
   is the LIBC being tested and not bcrun's own formatter. */
#include <stdio.h>

int main(void)
{
	unsigned long long u = 0x77ULL;
	unsigned long long big = 0x123456789ABCDEFULL;
	long long neg = -1234567890123LL;
	int bad = 0;
	char b[64];

	printf("hex small   %%llX -> %llX        (want 77)\n", u);
	printf("hex big     %%llX -> %llX (want 123456789ABCDEF)\n", big);
	printf("dec big     %%llu -> %llu\n", (unsigned long long)12345678901234ULL);
	printf("dec signed  %%lld -> %lld     (want -1234567890123)\n", neg);
	printf("still ok    %%d %%ld %%s -> %d %ld %s\n", 42, 123456L, "str");

	/* and through sprintf, which is the path mmbc's sfmt uses */
	sprintf(b, "%llX", big);
	printf("sprintf     %%llX -> %s\n", b);
	if (b[0] != '1' || b[1] != '2')
		bad = 1;

	sprintf(b, "%llu", 12345678901234ULL);
	printf("sprintf     %%llu -> %s\n", b);
	if (b[0] != '1' || b[1] != '2')
		bad = 1;

	printf(bad ? "FAILED\n" : "all correct\n");
	return bad;
}
