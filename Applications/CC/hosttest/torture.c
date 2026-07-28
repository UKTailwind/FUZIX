/* Constructs whose meaning depends on the width of int. Run under both
   models; the 32-bit one must not produce errors the 16-bit one doesn't,
   and must accept the things 16-bit necessarily rejects. */

/* constants that only fit in a 32-bit int */
int big = 100000;
int neg = -100000;
unsigned ubig = 3000000000U;
long lbig = 2147483647L;

/* the known -32768 typing bug, both models */
int edge = -32768;
int edge2 = 32767;
int edge3 = 65535;

/* shifts past 16 bits are meaningful now */
unsigned shift_test(unsigned v)
{
	return (v << 24) | (v >> 8);
}

/* pointer arithmetic must scale by 4 */
int *scale(int *p)
{
	return p + 3;
}

int index_it(int *p, int n)
{
	return p[n];
}

/* struct offsets follow int width and alignment */
struct s {
	char a;
	int b;
	char c;
	int d;
};

int offs(struct s *p)
{
	return p->d;
}

/* promotion: char/short -> int */
int promote(char c, short s)
{
	return c + s;
}

/* casts across every width */
long widen_u(unsigned char c)
{
	return (long)c;
}

unsigned char narrow(long v)
{
	return (unsigned char)v;
}

int truncate_test(long v)
{
	return (int)v;
}

/* comparisons that depend on int width */
int cmp(unsigned a, int b)
{
	if (a > 40000U)
		return 1;
	if (b < -40000)
		return 2;
	return 0;
}

/* array of pointers: element size 4 */
char *table[8];

char *pick(int n)
{
	return table[n];
}

/* sizeof in expressions */
int howbig(void)
{
	return sizeof(struct s) + sizeof(table) + sizeof(int);
}
