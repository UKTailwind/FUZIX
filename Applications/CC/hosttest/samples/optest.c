/* Opcode coverage test.
 *
 * Written in the subset both gcc and FCC accept, so gcc's output is the
 * reference: compile and run both, diff the output, and any difference
 * is a bug in the compiler or the interpreter.
 *
 * Deliberately avoids what FCC cannot do: long long, float, bitfields,
 * struct passing/returning, and block-scoped locals that shadow.
 */
int printf();

/* globals of every width, signed and unsigned, for the load/store ops */
char           gc  = -100;
signed char    gsc = -100;      /* plain char is unsigned here: LOAD8S */
unsigned char  guc = 200;
short          gs  = -30000;
unsigned short gus = 60000;
int            gi  = -100000;
unsigned int   gui = 3000000000u;

int arr[8];
char big[300];                  /* forces LOCAL16-sized frames elsewhere */

int addfn(int a, int b) { return a + b; }
int subfn(int a, int b) { return a - b; }

int apply(int (*fp)(), int a, int b)    /* CALLA: call through a pointer */
{
	return fp(a, b);
}

void consts(void)
{
	int a = 5;              /* CONST8  */
	int b = 1000;           /* CONST16 */
	int c = 100000;         /* CONST32 */
	int d = -5;
	int e = -1000;
	int f = -100000;
	printf("const %d %d %d %d %d %d\n", a, b, c, d, e, f);
}

void loadstore(void)
{
	printf("gload %d %d %d %d %d %u\n", gc, guc, gs, gus, gi, gui);
	gc = -1; guc = 255; gs = -2; gus = 65535; gi = -3; gui = 4000000000u;
	printf("gstor %d %d %d %d %d %u\n", gc, guc, gs, gus, gi, gui);
}

void arith(void)
{
	int a = 47, b = 5, c = -47;
	unsigned int u = 4000000000u, v = 7;

	printf("arith %d %d %d %d %d\n", a + b, a - b, a * b, a / b, a % b);
	printf("sdiv  %d %d %d %d\n", c / b, c % b, a / -b, a % -b);
	printf("udiv  %u %u\n", u / v, u % v);
	printf("bits  %d %d %d %d %d\n", a & b, a | b, a ^ b, ~a, -a);
	printf("shift %d %d %d %u\n", a << 3, a >> 2, c >> 2, u >> 3);
	printf("lnot  %d %d\n", !a, !0);
}

void compare(void)
{
	int a = 5, b = -5;
	unsigned int u = 5, w = 4000000000u;

	printf("scmp  %d%d%d%d%d%d\n", a == b, a != b, a < b, a > b, a <= b, a >= b);
	printf("ucmp  %d%d%d%d%d%d\n", u == w, u != w, u < w, u > w, u <= w, u >= w);
	printf("bool  %d %d\n", a ? 1 : 0, 0 ? 1 : 0);
	printf("andor %d %d\n", (a > 0) && (b < 0), (a < 0) || (b < 0));
}

void casts(void)
{
	int i = 300;
	int n = -1;
	char c;
	unsigned char uc;
	short s;
	unsigned short us;

	c = (char)i;    uc = (unsigned char)i;
	s = (short)70000; us = (unsigned short)70000;
	printf("cast  %d %d %d %d\n", c, uc, s, us);
	printf("sext  %d %d\n", (int)(signed char)n, (int)(short)n);
	printf("zext  %d %d\n", (int)(unsigned char)n, (int)(unsigned short)n);
	printf("schar %d %d\n", gsc, (int)(signed char)200);
}

void frames(void)
{
	char local[280];        /* big frame: LOCAL16 */
	int i, sum;

	for (i = 0; i < 280; i++)
		local[i] = i & 0x7F;
	sum = 0;
	for (i = 0; i < 280; i++)
		sum += local[i];
	printf("frame %d %d %d\n", sum, local[0], local[279]);
}

void control(void)
{
	int i, t;

	t = 0;
	for (i = 0; i < 5; i++) {
		switch (i) {
		case 0: t += 1; break;
		case 1: t += 10; break;
		case 2: t += 100; break;
		default: t += 1000; break;
		}
	}
	printf("switch %d\n", t);

	t = 0; i = 0;
	while (i < 10) { if (i == 5) { i++; continue; } t += i; i++; }
	printf("while  %d\n", t);

	t = 0; i = 0;
	do { t += 2; i++; } while (i < 4);
	printf("do     %d\n", t);
}

void pointers(void)
{
	int *p = arr;
	int i;

	for (i = 0; i < 8; i++)
		arr[i] = i * i;
	printf("ptr   %d %d %d %d\n", *p, *(p + 3), p[7], (int)(&arr[5] - arr));
	printf("call  %d %d\n", apply(addfn, 9, 4), apply(subfn, 9, 4));
}

int main(void)
{
	consts();
	loadstore();
	arith();
	compare();
	casts();
	frames();
	control();
	pointers();
	printf("done\n");
	return 0;
}
