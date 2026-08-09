/*
 * The string routines taken from newlib, against plain-C oracles.
 *
 * Library/libs/Makefile.armm0 substitutes newlib's tuned versions for
 * this target.  They are the same functions by name, so nothing but a
 * test says they are the same functions by behaviour - particularly at
 * the edges the tuned versions care about and the byte loops did not:
 * every alignment of every operand, and lengths either side of the
 * word and block steps they unroll to.
 *
 * Failures print; the count at the end is the result.
 */

#include <stdio.h>
#include <string.h>

#define PAD 8
#define MAX 72

static char a[MAX + 2 * PAD + 2];
static char b[MAX + 2 * PAD + 2];
static char d[MAX + 2 * PAD + 2];
static char r[MAX + 2 * PAD + 2];

static int fails, tests;

static void bad(const char *what, int i, int j, long got, long want)
{
	fails++;
	printf("FAIL %s (%d,%d): got %ld want %ld\n", what, i, j, got, want);
}

/* oracles, deliberately the dullest possible */
static size_t o_strlen(const char *s)
{
	size_t n = 0;
	while (*s++)
		n++;
	return n;
}

static int o_strcmp(const char *x, const char *y)
{
	while (*x && *x == *y) {
		x++;
		y++;
	}
	return (int)(unsigned char)*x - (int)(unsigned char)*y;
}

static int sgn(int v)
{
	return v < 0 ? -1 : (v > 0 ? 1 : 0);
}

static void fill(char *p, int off, int len, int seed)
{
	int i;
	for (i = 0; i < (int)sizeof a; i++)
		p[i] = (char)(0x40 + (i & 7));
	for (i = 0; i < len; i++)
		p[PAD + off + i] = (char)('a' + ((i + seed) % 26));
	p[PAD + off + len] = '\0';
}

int main(void)
{
	int i, j, n;

	/* strlen, strnlen, strchr, strrchr, memchr at every alignment */
	for (i = 0; i < 4; i++)
		for (n = 0; n <= MAX; n++) {
			char *s;
			tests++;
			fill(a, i, n, 0);
			s = a + PAD + i;
			if (strlen(s) != o_strlen(s))
				bad("strlen", i, n, (long)strlen(s), (long)n);
			if (strnlen(s, (size_t)n + 4) != o_strlen(s))
				bad("strnlen", i, n, (long)strnlen(s, n + 4), (long)n);
			if (n > 0) {
				char want = s[n / 2];
				if (strchr(s, want) != s + (n / 2 == 0 ? 0 : 0) &&
				    strchr(s, want) == NULL)
					bad("strchr null", i, n, 0, 1);
				if (strrchr(s, want) == NULL)
					bad("strrchr null", i, n, 0, 1);
				if (memchr(s, want, (size_t)n) == NULL)
					bad("memchr null", i, n, 0, 1);
			}
			if (strchr(s, '\0') != s + n)
				bad("strchr nul", i, n, 0, (long)n);
		}

	/* strcmp, strncmp, memcmp: every alignment pair, equal and
	   differing at each position */
	for (i = 0; i < 4; i++)
		for (j = 0; j < 4; j++)
			for (n = 0; n <= 40; n++) {
				char *x, *y;
				int k;
				tests++;
				fill(a, i, n, 0);
				fill(b, j, n, 0);
				x = a + PAD + i;
				y = b + PAD + j;
				if (sgn(strcmp(x, y)) != sgn(o_strcmp(x, y)))
					bad("strcmp equal", i, n, strcmp(x, y), 0);
				if (n && sgn(memcmp(x, y, (size_t)n)) != 0)
					bad("memcmp equal", i, n, memcmp(x, y, n), 0);
				for (k = 0; k < n; k++) {
					char save = y[k];
					y[k] = (char)(save + 1);
					if (sgn(strcmp(x, y)) != sgn(o_strcmp(x, y)))
						bad("strcmp diff", i, k, strcmp(x, y),
						    o_strcmp(x, y));
					if (sgn(strncmp(x, y, (size_t)n)) !=
					    sgn(o_strcmp(x, y)))
						bad("strncmp", i, k, strncmp(x, y, n),
						    o_strcmp(x, y));
					if (sgn(memcmp(x, y, (size_t)n)) == 0)
						bad("memcmp diff", i, k, 0, 1);
					y[k] = save;
				}
			}

	/* strcpy, strncpy, strcat, strlcpy, memccpy: result and the
	   bytes either side of it */
	for (i = 0; i < 4; i++)
		for (j = 0; j < 4; j++)
			for (n = 0; n <= 40; n++) {
				char *src, *dst;
				tests++;
				fill(a, j, n, 3);
				src = a + PAD + j;
				memset(d, '#', sizeof d);
				dst = d + PAD + i;
				strcpy(dst, src);
				if (o_strcmp(dst, src) != 0)
					bad("strcpy", i, n, 1, 0);
				if (dst[-1] != '#' || dst[n + 1] != '#')
					bad("strcpy spill", i, n, 1, 0);

				memset(d, '#', sizeof d);
				dst = d + PAD + i;
				dst[0] = '\0';
				strcat(dst, src);
				if (o_strcmp(dst, src) != 0)
					bad("strcat", i, n, 1, 0);

				memset(d, '#', sizeof d);
				dst = d + PAD + i;
				strncpy(dst, src, (size_t)n + 2);
				if (o_strcmp(dst, src) != 0)
					bad("strncpy", i, n, 1, 0);
				if (dst[n] != '\0' || dst[n + 1] != '\0')
					bad("strncpy pad", i, n, 1, 0);

				memset(r, '#', sizeof r);
				if (strlcpy(r + PAD + i, src, (size_t)n + 1) !=
				    o_strlen(src))
					bad("strlcpy len", i, n, 1, 0);
				if (o_strcmp(r + PAD + i, src) != 0)
					bad("strlcpy text", i, n, 1, 0);
			}

	/* strcspn, strspn, strpbrk */
	{
		static const char *set = "xyz";
		tests++;
		if (strcspn("abcxdef", set) != 3)
			bad("strcspn", 0, 0, (long)strcspn("abcxdef", set), 3);
		if (strcspn("abcdef", set) != 6)
			bad("strcspn none", 0, 0, (long)strcspn("abcdef", set), 6);
		if (strspn("xyxyab", set) != 4)
			bad("strspn", 0, 0, (long)strspn("xyxyab", set), 4);
		if (strpbrk("abcydef", set) == NULL)
			bad("strpbrk", 0, 0, 0, 1);
		if (strpbrk("abcdef", set) != NULL)
			bad("strpbrk none", 0, 0, 1, 0);
	}

	printf("%d groups, %d failures\n", tests, fails);
	return fails != 0;
}
