/* CP-H: switch (translate-time-decoded compare chain) and aggregates
   (COPY / PUSHN through helper_op) - the constructs that blocked the
   rest of Dhrystone. */
#include <stdio.h>

int classify(int x)
{
	switch (x) {
	case 0:
		return 100;
	case 1:
		return 101;
	case 7:
		return 107;
	case -3:
		return 97;
	case 1000000:
		return 1;
	default:
		return -1;
	}
}

/* fallthrough and cases sharing a body */
int density(int c)
{
	int n = 0;
	switch (c) {
	case 'a':
	case 'e':
	case 'i':
	case 'o':
	case 'u':
		n = 2;
		/* fall through */
	case 'y':
		n++;
		break;
	case ' ':
		n = -1;
		break;
	default:
		n = 0;
	}
	return n;
}

/* switch inside a loop, backward and forward targets mixed */
long collatz_steps(long n)
{
	long c = 0;
	while (n != 1) {
		switch (n & 1) {
		case 0:
			n /= 2;
			break;
		default:
			n = 3 * n + 1;
		}
		c++;
	}
	return c;
}

/* switch on a call result (the low-32 contract at work) */
int pick(int x) { return x * 3; }
int dispatch(int x)
{
	switch (pick(x)) {
	case 3:
		return 1;
	case 6:
		return 2;
	case 9:
		return 3;
	}
	return 0;
}

/* aggregates: assignment, pass by value, return by value */
struct rec {
	int a;
	char tag[6];
	long v;
};

struct pair {
	int x, y;
};

struct rec mk(int a, long v)
{
	struct rec r;
	int i;
	r.a = a;
	for (i = 0; i < 5; i++)
		r.tag[i] = 'A' + (a + i) % 26;
	r.tag[5] = 0;
	r.v = v;
	return r;
}

int consume(struct rec r, int scale)
{
	return r.a * scale + (int)r.v + r.tag[0];
}

struct pair swap(struct pair p)
{
	struct pair q;
	q.x = p.y;
	q.y = p.x;
	return q;
}

int main(void)
{
	struct rec r1, r2;
	struct pair p;
	int i, acc;

	for (i = -4; i <= 8; i++)
		printf("%d ", classify(i));
	printf("%d\n", classify(1000000));

	printf("%d %d %d %d %d\n", density('e'), density('y'),
	       density(' '), density('z'), density('u'));

	printf("%ld %ld %ld\n", collatz_steps(27), collatz_steps(97),
	       collatz_steps(871));

	acc = 0;
	for (i = 0; i < 5; i++)
		acc = acc * 10 + dispatch(i);
	printf("%d\n", acc);

	r1 = mk(5, 1234567L);
	r2 = r1;		/* struct assignment: BC_COPY */
	r2.a = 9;
	printf("%d %d %s %s\n", r1.a, r2.a, r1.tag, r2.tag);
	printf("%d %d\n", consume(r1, 100), consume(r2, 100));

	p.x = 3;
	p.y = 44;
	p = swap(p);
	printf("%d %d\n", p.x, p.y);

	printf("%d\n", consume(mk(2, 42L), 7));
	return 0;
}
