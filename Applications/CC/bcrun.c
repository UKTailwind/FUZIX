/*
 *	Interpreter for the PC3 bytecode (see BYTECODE.md).
 *
 *	  bcrun program.bc [args]
 *
 *	The virtual machine has its own address space: a single mem[]
 *	array holding data, bss and the stack, and a program pointer is an
 *	offset into it. That keeps program pointers 32bit whatever the
 *	host is, so this runs identically on the development machine and
 *	on the PC3.
 *
 *	Code lives outside that space and is not addressable; pc is an
 *	index into code[]. A function pointer is therefore a code offset,
 *	which is fine as long as nobody tries to read one as data.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <stdint.h>
#include "bytecode.h"

/* The machine is 32bit. On a 64bit host every value that enters A or
   the stack must be sign extended from 32 bits, or negative numbers
   read back as huge positive ones. */
#define S32(x)	((long)(int32_t)(x))
/* And unsigned operations must work at 32 bits too: A holds a sign
   extended value, so casting it straight to unsigned long on a 64bit
   host gives an enormous number instead of the intended one. */
#define U32(x)	((uint32_t)(x))

/*
 *	The program's address space.
 *
 *	  [ data ][ bss ][ heap -> ........... <- stack ]
 *
 *	Everything the program can address is an offset into mem[], which
 *	is why the allocator below deals purely in offsets. It also means
 *	this memory can live anywhere without the library caring: putting
 *	it in PSRAM, as the MicroPython port does on this board, is a
 *	matter of pointing mem at a reserved region rather than declaring
 *	an array - see PSRAM_RESERVE in the kernel's psram.h.
 */
#define MEMSIZE		131072		/* program address space */
/* Dead space at the bottom so that no object can live at address 0 and
   a null pointer stays distinguishable from a real one. */
#define NULLGUARD	16
#define STACKROOM	8192		/* kept clear at the top for the stack */

static unsigned char *code;
static unsigned char mem[MEMSIZE];
static struct bc_header h;
static struct bc_sym *sym;
static struct bc_fixup *fix;
static char *strtab;

/*
 * A double is carried in the accumulator as its bit pattern and a float
 * in the low 32 bits, so these are how the arithmetic gets at it. The
 * union is not decoration: reading an int64_t through a double * is an
 * aliasing violation that gcc is entitled to optimise on.
 */
static double dget(int64_t v)
{
	union { int64_t i; double d; } u;
	u.i = v;
	return u.d;
}

static int64_t dput(double d)
{
	union { int64_t i; double d; } u;
	u.d = d;
	return u.i;
}

static float fget(int64_t v)
{
	union { uint32_t i; float f; } u;
	u.i = (uint32_t)v;
	return u.f;
}

static int64_t fput(float f)
{
	union { uint32_t i; float f; } u;
	u.f = f;
	/* Sign extended, because everything else in A is */
	return S32(u.i);
}

/* Machine state. sp and fp are offsets into mem[], and the stack grows
   down from the top. */
static int64_t A;
static unsigned long pc;
static unsigned long sp;

static int trace;

static void fault(const char *msg)
{
	fprintf(stderr, "bcrun: %s at pc %lu\n", msg, pc);
	exit(1);
}

/* ---- memory access, all bounds checked ---------------------------- */

static unsigned long rd32(unsigned long a)
{
	if (a + 3 >= MEMSIZE)
		fault("bad address");
	return mem[a] | ((unsigned long)mem[a + 1] << 8) |
	    ((unsigned long)mem[a + 2] << 16) | ((unsigned long)mem[a + 3] << 24);
}

static void wr32(unsigned long a, unsigned long v)
{
	if (a + 3 >= MEMSIZE)
		fault("bad address");
	mem[a] = v;
	mem[a + 1] = v >> 8;
	mem[a + 2] = v >> 16;
	mem[a + 3] = v >> 24;
}

static uint64_t rd64(unsigned long a)
{
	return (uint64_t)rd32(a) | ((uint64_t)rd32(a + 4) << 32);
}

static void wr64(unsigned long a, uint64_t v)
{
	wr32(a, (unsigned long)(v & 0xFFFFFFFFu));
	wr32(a + 4, (unsigned long)(v >> 32));
}

static unsigned rd16(unsigned long a)
{
	if (a + 1 >= MEMSIZE)
		fault("bad address");
	return mem[a] | (mem[a + 1] << 8);
}

static void wr16(unsigned long a, unsigned v)
{
	if (a + 1 >= MEMSIZE)
		fault("bad address");
	mem[a] = v;
	mem[a + 1] = v >> 8;
}

static unsigned rd8(unsigned long a)
{
	if (a >= MEMSIZE)
		fault("bad address");
	return mem[a];
}

static void wr8(unsigned long a, unsigned v)
{
	if (a >= MEMSIZE)
		fault("bad address");
	mem[a] = v;
}

static void push(long v)
{
	sp -= 4;
	wr32(sp, v);
}

static long pop(void)
{
	long v = S32(rd32(sp));
	sp += 4;
	return v;
}

static void push64(int64_t v)
{
	sp -= 8;
	wr64(sp, (uint64_t)v);
}

static int64_t pop64(void)
{
	int64_t v = (int64_t)rd64(sp);
	sp += 8;
	return v;
}

static int64_t fetch64(void)
{
	uint64_t lo, hi;
	lo = code[pc] | ((uint32_t)code[pc+1] << 8) |
	     ((uint32_t)code[pc+2] << 16) | ((uint32_t)code[pc+3] << 24);
	hi = code[pc+4] | ((uint32_t)code[pc+5] << 8) |
	     ((uint32_t)code[pc+6] << 16) | ((uint32_t)code[pc+7] << 24);
	pc += 8;
	return (int64_t)(lo | (hi << 32));
}

/* ---- instruction stream ------------------------------------------- */

static unsigned char fetch8(void)
{
	if (pc >= h.h_code)
		fault("pc out of range");
	return code[pc++];
}

static unsigned fetch16(void)
{
	unsigned v = code[pc] | (code[pc + 1] << 8);
	pc += 2;
	return v;
}

static unsigned long fetch32(void)
{
	unsigned long v = code[pc] | ((unsigned long)code[pc + 1] << 8) |
	    ((unsigned long)code[pc + 2] << 16) |
	    ((unsigned long)code[pc + 3] << 24);
	pc += 4;
	return v;
}

/* ---- the runtime library ------------------------------------------ */

/*
 *	Compound assignment and increment arrive as library calls rather
 *	than opcodes: the address is on the stack and the amount is in A.
 *	See BYTECODE.md.
 *
 *	These assume a 32bit operand. The emitter does not yet encode the
 *	width in the call, so char and short compound assignment is wrong;
 *	that is a known gap, recorded in PC3-COMPILER-PLAN.md.
 */
/*
 *	The floating point forms of the above. The emitter marks these with
 *	a trailing 'd' or 'f' instead of a width and a signedness, because
 *	neither means anything here.
 */
static int lib_eqop_fp(const char *name)
{
	char base[24];
	unsigned len;
	int dbl;
	unsigned long addr;
	double old, v, res;

	strncpy(base, name, sizeof(base) - 1);
	base[sizeof(base) - 1] = 0;
	len = strlen(base);
	if (len == 0)
		return 0;
	dbl = (base[len - 1] == 'd');
	if (!dbl && base[len - 1] != 'f')
		return 0;
	base[--len] = 0;

	if (strcmp(base, "pluseq") && strcmp(base, "minuseq") &&
	    strcmp(base, "muleq") && strcmp(base, "diveq") &&
	    strcmp(base, "postinc") && strcmp(base, "postdec"))
		return 0;

	addr = (unsigned long)pop();
	v = dbl ? dget(A) : (double)fget(A);
	old = dbl ? dget((int64_t)rd64(addr)) : (double)fget(S32(rd32(addr)));

	if (!strcmp(base, "pluseq") || !strcmp(base, "postinc"))
		res = old + v;
	else if (!strcmp(base, "minuseq") || !strcmp(base, "postdec"))
		res = old - v;
	else if (!strcmp(base, "muleq"))
		res = old * v;
	else
		res = v ? old / v : 0.0;

	/* postinc and postdec yield the old value, the rest the new one */
	if (!strcmp(base, "postinc") || !strcmp(base, "postdec"))
		v = old;
	else
		v = res;

	if (dbl) {
		wr64(addr, (uint64_t)dput(res));
		A = dput(v);
	} else {
		wr32(addr, (unsigned long)(uint32_t)fput((float)res));
		A = fput((float)v);
	}
	return 1;
}

static void lib_eqop(const char *name)
{
	char base[24];
	unsigned len, sz = 4;
	int uns = 0, post;
	unsigned long addr;
	long old, v, res;

	if (lib_eqop_fp(name))
		return;

	/* The emitter appends the operand width in bytes and 's' or 'u',
	   e.g. "pluseq1u" or "shreq4s". */
	strncpy(base, name, sizeof(base) - 1);
	base[sizeof(base) - 1] = 0;
	len = strlen(base);
	if (len && (base[len - 1] == 's' || base[len - 1] == 'u')) {
		uns = (base[len - 1] == 'u');
		base[--len] = 0;
	}
	if (len && base[len - 1] >= '1' && base[len - 1] <= '8') {
		sz = base[len - 1] - '0';
		base[--len] = 0;
	}

	addr = (unsigned long)pop();
	v = A;

	if (sz == 1)
		old = uns ? (long)rd8(addr) : (long)(signed char)rd8(addr);
	else if (sz == 2)
		old = uns ? (long)rd16(addr) : (long)(short)rd16(addr);
	else
		old = S32(rd32(addr));

	post = 0;
	if (!strcmp(base, "postinc")) { res = old + v; post = 1; }
	else if (!strcmp(base, "postdec")) { res = old - v; post = 1; }
	else if (!strcmp(base, "pluseq")) res = old + v;
	else if (!strcmp(base, "minuseq")) res = old - v;
	else if (!strcmp(base, "muleq")) res = old * v;
	else if (!strcmp(base, "diveq"))
		res = v ? (uns ? (long)((unsigned long)old / (unsigned long)v)
			       : old / v) : 0;
	else if (!strcmp(base, "remeq"))
		res = v ? (uns ? (long)((unsigned long)old % (unsigned long)v)
			       : old % v) : 0;
	else if (!strcmp(base, "andeq")) res = old & v;
	else if (!strcmp(base, "oreq")) res = old | v;
	else if (!strcmp(base, "xoreq")) res = old ^ v;
	else if (!strcmp(base, "shleq")) res = old << v;
	else if (!strcmp(base, "shreq"))
		res = uns ? (long)((unsigned long)old >> v) : (old >> v);
	else {
		fprintf(stderr, "bcrun: no runtime function \"%s\"\n", name);
		exit(1);
	}

	/* Store at the object's own width, so a carry cannot escape into
	   whatever lives next to it. */
	if (sz == 1)
		wr8(addr, res);
	else if (sz == 2)
		wr16(addr, res);
	else
		wr32(addr, res);

	A = post ? old : res;
}

/*
 *	Copy a NUL terminated string out of the program's memory.
 *
 *	Rotates between a few buffers: printf needs the format string and
 *	a %s argument live at the same time, and with one static buffer
 *	the argument overwrote the format halfway through scanning it.
 */
static char *getstr(unsigned long a)
{
	static char buf[4][512];
	static unsigned which;
	char *b = buf[which++ & 3];
	unsigned i = 0;
	while (i < 511 && a + i < MEMSIZE && mem[a + i])
		b[i] = mem[a + i], i++;
	b[i] = 0;
	return b;
}

/*
 *	Arguments to a library call are on the stack exactly as for a
 *	bytecode call: arg(0) is nearest the stack pointer.
 */
static long arg(unsigned n)
{
	return S32(rd32(sp + 4 * n));
}

/*
 *	A double argument occupies two stack slots, not one.
 */
static double argd(unsigned n)
{
	uint64_t v = rd64(sp + 4 * n);
	double d;
	memcpy(&d, &v, sizeof(d));
	return d;
}

/*
 *	%f, by hand.
 *
 *	Everything else here hands the work to the host's sprintf, but
 *	that is not available for this: bcrun runs on the PC3 against
 *	Fuzix libc, whose printf has no floating point at all. So the
 *	conversion has to be done here or "%f" cannot work on the machine
 *	this is for - which is the whole point of it.
 *
 *	Straight fixed point: round at the requested precision, split into
 *	integer and fraction, then peel digits. Exact for any value whose
 *	integer part fits a 64-bit unsigned, which is everything anyone
 *	will realistically print. Above that a correct %f needs arbitrary
 *	precision arithmetic to expand the binary value exactly, which is
 *	not worth carrying here, so it degrades to exponent form and says
 *	so rather than printing confident rubbish.
 */
static void fmt_double(char *out, double v, int prec)
{
	char digits[64];
	int nd = 0;
	int intlen = 0;
	uint64_t ip;
	double fp;
	int i;
	char *p = out;

	if (v != v) {
		strcpy(out, "nan");
		return;
	}
	/* By the sign bit, not "v < 0": negative zero is not less than
	   zero but still prints with a minus. */
	{
		uint64_t bits;
		memcpy(&bits, &v, sizeof(bits));
		if (bits >> 63) {
			*p++ = '-';
			v = -v;
		}
	}
	if (v > 1.7e308) {
		strcpy(p, "inf");
		return;
	}

	if (v >= 18446744073709551615.0) {
		/* Beyond exact fixed point here - see the note above */
		int e = 0;
		while (v >= 10.0) { v /= 10.0; e++; }
		ip = (uint64_t) v;
		fp = v - (double) ip;
		p += 1;
		out[p - out - 1] = (char) ('0' + ip);
		*p++ = '.';
		for (i = 0; i < 6; i++) {
			fp *= 10.0;
			*p++ = (char) ('0' + (int) fp);
			fp -= (double) (int) fp;
		}
		*p++ = 'e';
		if (e >= 100) *p++ = (char) ('0' + e / 100);
		if (e >= 10) *p++ = (char) ('0' + (e / 10) % 10);
		*p++ = (char) ('0' + e % 10);
		*p = 0;
		return;
	}

	ip = (uint64_t) v;
	fp = v - (double) ip;

	/* Integer digits, most significant first */
	if (ip == 0)
		digits[nd++] = '0';
	else {
		char rev[24];
		int nr = 0;
		while (ip) {
			rev[nr++] = (char) ('0' + (int) (ip % 10));
			ip /= 10;
		}
		while (nr)
			digits[nd++] = rev[--nr];
	}
	intlen = nd;

	/* Then the fraction, one digit at a time */
	for (i = 0; i < prec; i++) {
		int d;
		fp *= 10.0;
		d = (int) fp;
		if (d < 0) d = 0;
		if (d > 9) d = 9;
		digits[nd++] = (char) ('0' + d);
		fp -= (double) d;
	}

	/*
	 * Round half up, on the remainder still in hand.
	 *
	 * A full C library rounds half to *even*, and this does not, for
	 * one specific case: a fraction that is an exact binary half at
	 * the rounding digit. "%.2f" of 0.125 gives 0.13 here and 0.12
	 * from glibc.
	 *
	 * Getting that right needs the exact decimal expansion of the
	 * binary value, which needs arbitrary precision arithmetic - the
	 * digits cannot be peeled off with double multiplies, because
	 * 0.05 * 10.0 rounds to exactly 0.5 and an exact half then looks
	 * identical to a value just above one. Trying it that way fixed
	 * 0.125 and broke 0.05, which is much the more common shape.
	 *
	 * So: half up, which is what small C libraries generally do, and
	 * the difference is confined to exact binary halves.
	 */
	{
		int up = (fp >= 0.5);
		if (up) {
			int k = nd - 1;
			while (k >= 0 && digits[k] == '9')
				digits[k--] = '0';
			if (k < 0) {
				/* 9.99 -> 10.0: the carry ran off the front */
				for (k = nd; k > 0; k--)
					digits[k] = digits[k - 1];
				digits[0] = '1';
				nd++;
				intlen++;
			} else
				digits[k]++;
		}
	}

	for (i = 0; i < intlen; i++)
		*p++ = digits[i];
	if (prec > 0) {
		*p++ = '.';
		for (i = intlen; i < nd; i++)
			*p++ = digits[i];
	}
	*p = 0;
}

/*
 *	Where formatted output goes. printf sends it to stdout, sprintf
 *	into the program's own address space; everything between the two
 *	is identical, so the formatter writes through here.
 */
static unsigned long out_at;		/* destination for sprintf */
static int out_to_mem;
static int out_fd = 1;			/* when not to memory */
static unsigned long out_len;

/* Bytes bound for a descriptor other than stdout, batched so a format
   run is one write rather than one per character. */
static char out_buf[256];
static unsigned out_n;

static void out_flush(void)
{
	if (out_n) {
		if (write(out_fd, out_buf, out_n) < 0)
			/* nothing useful to do about it here */;
		out_n = 0;
	}
}

static void emit(char c)
{
	if (out_to_mem) {
		if (out_at < MEMSIZE)
			mem[out_at++] = (uint8_t) c;
	} else if (out_fd == 1)
		putchar(c);		/* stays in step with printf */
	else {
		if (out_n == sizeof(out_buf))
			out_flush();
		out_buf[out_n++] = c;
	}
	out_len++;
}

static void emits(const char *s)
{
	while (*s)
		emit(*s++);
}

/* Pad a already-formatted item to the requested width. */
static void padout(const char *s, int width, int left, int zero)
{
	int n = (int)strlen(s);
	int pad = width - n;
	if (!left)
		while (pad-- > 0)
			emit(zero ? '0' : ' ');
	emits(s);
	if (left)
		while (pad-- > 0)
			emit(' ');
}

/*
 *	The shared formatter. "abase" is the stack slot the format string
 *	sits in, so printf passes 0 and sprintf 1.
 */
static void do_format(unsigned abase)
{
	const char *f = getstr((unsigned long)arg(abase));
	char tmp[544];
	unsigned a = abase + 1;

	while (*f) {
		int left = 0, zero = 0, width = 0, prec = -1;

		if (*f != '%') {
			emit(*f++);
			continue;
		}
		f++;
		/* Flags and a numeric width: "%-2d", "%04x" and friends were
		   printed literally before, which also threw the argument
		   numbering out. */
		for (;;) {
			if (*f == '-') { left = 1; f++; }
			else if (*f == '0') { zero = 1; f++; }
			else break;
		}
		while (*f >= '1' && *f <= '9') {
			width = width * 10 + (*f - '0');
			f++;
		}
		if (*f == '0') { width *= 10; f++; }	/* e.g. %10d */

		/* Precision: ".<digits>" or ".*" taking it from an argument.
		   Needed for %f, where it also has a default of 6. */
		if (*f == '.') {
			f++;
			prec = 0;
			if (*f == '*') {
				prec = (int) arg(a++);
				f++;
			} else
				while (*f >= '0' && *f <= '9') {
					prec = prec * 10 + (*f - '0');
					f++;
				}
			if (prec < 0)
				prec = 0;
		}

		switch (*f) {
		case 'd':
			sprintf(tmp, "%ld", (long)arg(a++));
			padout(tmp, width, left, zero);
			break;
		case 'u':
			sprintf(tmp, "%lu", (unsigned long)U32(arg(a++)));
			padout(tmp, width, left, zero);
			break;
		case 'x':
			sprintf(tmp, "%lx", (unsigned long)U32(arg(a++)));
			padout(tmp, width, left, zero);
			break;
		case 'c':
			tmp[0] = (char)arg(a++);
			tmp[1] = 0;
			padout(tmp, width, left, 0);
			break;
		case 's':
			padout(getstr((unsigned long)arg(a++)), width, left, 0);
			break;
		case 'f':
			/* A double is two slots, and the default precision
			   for %f is six. Floats reaching a variadic call
			   have already been promoted to double by the front
			   end's typeconv_implicit, so there is only ever a
			   double here. */
			fmt_double(tmp, argd(a), prec < 0 ? 6 : prec);
			a += 2;
			padout(tmp, width, left, zero);
			break;
		case '%':
			emit('%');
			break;
		default:
			emit('%');
			if (*f)
				emit(*f);
			break;
		}
		if (*f)
			f++;
	}
}

static void lib_printf(void)
{
	out_to_mem = 0;
	out_fd = 1;
	out_len = 0;
	do_format(0);
	A = (int64_t) out_len;		/* printf returns the count written */
}

/* ---- stdio --------------------------------------------------------- */

/*
 *	A FILE * is the descriptor plus one.
 *
 *	That keeps stdin, stdout and stderr as the constants 1, 2 and 3,
 *	which the header can define directly - our object format has no
 *	way to import a *data* symbol from the runtime, only functions, so
 *	they could not have been real objects. It also means no open file
 *	table: every operation is the matching syscall on handle - 1.
 *
 *	Unbuffered. The kernel is doing the buffering underneath and this
 *	is not the place to add another layer.
 */
#define MAXFD	64
static uint8_t file_eof[MAXFD];

static int fh(long handle)
{
	int fd = (int) handle - 1;
	if (fd < 0 || fd >= MAXFD)
		return -1;
	return fd;
}

static void lib_fopen(void)
{
	const char *path = getstr((unsigned long) arg(0));
	const char *mode = getstr((unsigned long) arg(1));
	int flags = 0, fd;

	/* "b" is meaningless here and is simply ignored, as C allows */
	if (mode[0] == 'r')
		flags = strchr(mode, '+') ? O_RDWR : O_RDONLY;
	else if (mode[0] == 'w')
		flags = (strchr(mode, '+') ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC;
	else if (mode[0] == 'a')
		flags = (strchr(mode, '+') ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND;
	else {
		A = 0;
		return;
	}
	fd = open(path, flags, 0666);
	if (fd < 0 || fd >= MAXFD) {
		if (fd >= 0)
			close(fd);
		A = 0;			/* NULL */
		return;
	}
	file_eof[fd] = 0;
	A = fd + 1;
}

static void lib_fclose(void)
{
	int fd = fh(arg(0));
	A = (fd < 0) ? -1 : close(fd);
}

/* fread/fwrite count items, not bytes, and return the item count */
static void lib_fread(void)
{
	unsigned long b = (unsigned long) arg(0);
	unsigned long size = (unsigned long) arg(1);
	unsigned long n = (unsigned long) arg(2);
	int fd = fh(arg(3));
	long got;

	if (fd < 0 || size == 0 || b + size * n > MEMSIZE) {
		A = 0;
		return;
	}
	got = read(fd, mem + b, size * n);
	if (got <= 0) {
		file_eof[fd] = 1;
		A = 0;
		return;
	}
	if ((unsigned long) got < size * n)
		file_eof[fd] = 1;
	A = got / (long) size;
}

static void lib_fwrite(void)
{
	unsigned long b = (unsigned long) arg(0);
	unsigned long size = (unsigned long) arg(1);
	unsigned long n = (unsigned long) arg(2);
	int fd = fh(arg(3));
	long put;

	if (fd < 0 || size == 0 || b + size * n > MEMSIZE) {
		A = 0;
		return;
	}
	if (fd == 1)
		fflush(stdout);		/* keep printf and fwrite in order */
	put = write(fd, mem + b, size * n);
	A = (put <= 0) ? 0 : put / (long) size;
}

static void lib_fgetc(void)
{
	int fd = fh(arg(0));
	unsigned char c;

	if (fd < 0 || read(fd, &c, 1) != 1) {
		if (fd >= 0)
			file_eof[fd] = 1;
		A = -1;			/* EOF */
		return;
	}
	A = c;
}

static void lib_fputc(void)
{
	int fd = fh(arg(1));
	unsigned char c = (unsigned char) arg(0);

	if (fd < 0) {
		A = -1;
		return;
	}
	if (fd == 1) {
		putchar(c);
		A = c;
		return;
	}
	A = (write(fd, &c, 1) == 1) ? c : -1;
}

/*
 *	Up to n-1 bytes, stopping after a newline, NUL terminated.
 *	Returns the buffer, or NULL at end of file with nothing read.
 */
static void lib_fgets(void)
{
	unsigned long b = (unsigned long) arg(0);
	long n = arg(1);
	int fd = fh(arg(2));
	long i = 0;
	unsigned char c;

	if (fd < 0 || n <= 0 || b + n > MEMSIZE) {
		A = 0;
		return;
	}
	while (i < n - 1) {
		if (read(fd, &c, 1) != 1) {
			file_eof[fd] = 1;
			break;
		}
		mem[b + i++] = c;
		if (c == '\n')
			break;
	}
	if (i == 0) {
		A = 0;			/* NULL */
		return;
	}
	mem[b + i] = 0;
	A = (int64_t) b;
}

static void lib_fputs(void)
{
	const char *s = getstr((unsigned long) arg(0));
	int fd = fh(arg(1));
	size_t len = strlen(s);

	if (fd < 0) {
		A = -1;
		return;
	}
	if (fd == 1) {
		fputs(s, stdout);
		A = (int64_t) len;
		return;
	}
	A = (write(fd, s, len) == (long) len) ? (int64_t) len : -1;
}

static void lib_fprintf(void)
{
	int fd = fh(arg(0));

	if (fd < 0) {
		A = -1;
		return;
	}
	out_to_mem = 0;
	out_fd = fd;
	out_n = 0;
	out_len = 0;
	if (fd == 1)
		do_format(1);
	else {
		do_format(1);
		out_flush();
	}
	out_fd = 1;
	A = (int64_t) out_len;
}

static void lib_feof(void)
{
	int fd = fh(arg(0));
	A = (fd < 0) ? 1 : file_eof[fd];
}

static void lib_fseek(void)
{
	int fd = fh(arg(0));
	if (fd < 0) {
		A = -1;
		return;
	}
	file_eof[fd] = 0;
	A = (lseek(fd, arg(1), (int) arg(2)) < 0) ? -1 : 0;
}

static void lib_ftell(void)
{
	int fd = fh(arg(0));
	A = (fd < 0) ? -1 : lseek(fd, 0, SEEK_CUR);
}

static void lib_sprintf(void)
{
	out_to_mem = 1;
	out_at = (unsigned long) arg(0);
	out_len = 0;
	do_format(1);
	if (out_at < MEMSIZE)
		mem[out_at] = 0;	/* terminate, not counted */
	out_to_mem = 0;
	A = (int64_t) out_len;
}

/* ---- heap ---------------------------------------------------------- */

/*
 *	A first-fit allocator over the gap between bss and the stack.
 *	Every block carries an 8-byte header: size (including header) and
 *	a used flag. Free blocks coalesce forwards on release, which is
 *	enough to stop simple alloc/free loops fragmenting.
 */
#define HDR	8

static unsigned long heap_base, heap_top;

static void heap_init(unsigned long base)
{
	heap_base = (base + 3) & ~3UL;
	heap_top = MEMSIZE - STACKROOM;
	if (heap_top <= heap_base + HDR) {
		heap_base = heap_top = 0;
		return;
	}
	/* one free block covering everything */
	wr32(heap_base, heap_top - heap_base);
	wr32(heap_base + 4, 0);
}

static long lib_malloc(unsigned long want)
{
	unsigned long p = heap_base;

	if (!heap_base || want == 0)
		return 0;
	want = (want + HDR + 3) & ~3UL;

	while (p < heap_top) {
		unsigned long sz = rd32(p);
		unsigned long used = rd32(p + 4);
		if (sz < HDR || p + sz > heap_top)
			return 0;			/* corrupt */
		if (!used && sz >= want) {
			if (sz >= want + HDR + 8) {	/* split */
				wr32(p + want, sz - want);
				wr32(p + want + 4, 0);
				wr32(p, want);
			}
			wr32(p + 4, 1);
			return (long)(p + HDR);
		}
		p += sz;
	}
	return 0;					/* out of memory */
}

static void lib_free(unsigned long ptr)
{
	unsigned long p, q;

	if (!ptr || !heap_base)
		return;
	p = ptr - HDR;
	if (p < heap_base || p >= heap_top)
		return;
	wr32(p + 4, 0);

	/* coalesce forwards */
	q = heap_base;
	while (q < heap_top) {
		unsigned long sz = rd32(q);
		if (sz < HDR)
			return;
		if (!rd32(q + 4)) {
			unsigned long n = q + sz;
			while (n < heap_top && !rd32(n + 4)) {
				sz += rd32(n);
				wr32(q, sz);
				n = q + sz;
			}
		}
		q += rd32(q);
	}
}

/* ---- platform services --------------------------------------------- */

/*
 *	Reaching the hardware.
 *
 *	MMBasic gives CFunctions a table of SDK entry points because those
 *	are native code sharing the firmware's address space. Nothing here
 *	executes native code: a bytecode program can only reach the
 *	outside world through BC_LIBCALL, and the interpreter is an
 *	ordinary Fuzix process, so it uses the same syscalls and ioctls
 *	any other program would.
 *
 *	BC_LIBCALL already resolves by name at load time, so the libcall
 *	table *is* the call table - and declaring "int time_us();" in the
 *	C source is all the header a program needs, because the emitter
 *	turns any undefined name into a libcall.
 *
 *	SDK routines the kernel does not already wrap need a new ioctl;
 *	they cannot be called directly, as the SDK is linked into the
 *	kernel and not into userland.
 */
extern int ioctl(int, int, ...);
/* Fuzix's sys/time.h defines struct timeval but does not declare this. */
extern int gettimeofday(struct timeval *, void *);

#define PICOIOC_ADVAL	0x0009

static int sysfd = -1;

static int sys_open(void)
{
	if (sysfd < 0)
		sysfd = open("/dev/sys", O_RDWR);
	return sysfd;
}

/* ADVAL(n): joystick, ADC channels, sound queue, and the microsecond
 * counter on -9. Returns -1 where there is no /dev/sys. */
static long lib_adval(int sel)
{
	int fd = sys_open();
	int n = sel;
	if (fd < 0)
		return -1;
	return ioctl(fd, PICOIOC_ADVAL, &n);
}

/* ---- string and memory, all working in the program's address space -- */

static unsigned long vstrlen(unsigned long a)
{
	unsigned long n = 0;
	while (a + n < MEMSIZE && mem[a + n])
		n++;
	return n;
}

static void vcopy(unsigned long d, unsigned long s, unsigned long n)
{
	if (d + n > MEMSIZE || s + n > MEMSIZE)
		fault("bad address");
	memmove(mem + d, mem + s, n);
}

static void libcall(unsigned idx)
{
	const char *name;

	if (idx >= h.h_nsym)
		fault("bad library index");
	name = strtab + sym[idx].s_name;

	if (!strcmp(name, "putchar")) {
		putchar((int)arg(0));
		fflush(stdout);
		A = arg(0);
	} else if (!strcmp(name, "puts")) {
		puts(getstr((unsigned long)arg(0)));
		A = 0;
	} else if (!strcmp(name, "printf")) {
		lib_printf();
	} else if (!strcmp(name, "sprintf")) {
		lib_sprintf();
	} else if (!strcmp(name, "fopen")) {
		lib_fopen();
	} else if (!strcmp(name, "fclose")) {
		lib_fclose();
	} else if (!strcmp(name, "fread")) {
		lib_fread();
	} else if (!strcmp(name, "fwrite")) {
		lib_fwrite();
	} else if (!strcmp(name, "fgetc") || !strcmp(name, "getc")) {
		lib_fgetc();
	} else if (!strcmp(name, "fputc") || !strcmp(name, "putc")) {
		lib_fputc();
	} else if (!strcmp(name, "fgets")) {
		lib_fgets();
	} else if (!strcmp(name, "fputs")) {
		lib_fputs();
	} else if (!strcmp(name, "fprintf")) {
		lib_fprintf();
	} else if (!strcmp(name, "feof")) {
		lib_feof();
	} else if (!strcmp(name, "fseek")) {
		lib_fseek();
	} else if (!strcmp(name, "ftell")) {
		lib_ftell();
	} else if (!strcmp(name, "rewind")) {
		int fd = fh(arg(0));
		if (fd >= 0) {
			lseek(fd, 0, SEEK_SET);
			file_eof[fd] = 0;
		}
		A = 0;
	} else if (!strcmp(name, "fflush")) {
		fflush(stdout);
		A = 0;
	} else if (!strcmp(name, "remove")) {
		A = unlink(getstr((unsigned long)arg(0)));
	} else if (!strcmp(name, "exit")) {
		exit((int)arg(0));

	/* --- memory --------------------------------------------------- */
	} else if (!strcmp(name, "malloc")) {
		A = lib_malloc((unsigned long)arg(0));
	} else if (!strcmp(name, "calloc")) {
		unsigned long n = (unsigned long)arg(0) * (unsigned long)arg(1);
		A = lib_malloc(n);
		if (A)
			memset(mem + A, 0, n);
	} else if (!strcmp(name, "free")) {
		lib_free((unsigned long)arg(0));
		A = 0;
	} else if (!strcmp(name, "realloc")) {
		unsigned long old = (unsigned long)arg(0);
		unsigned long n = (unsigned long)arg(1);
		long np = lib_malloc(n);
		if (np && old) {
			unsigned long osz = rd32(old - HDR) - HDR;
			vcopy((unsigned long)np, old, osz < n ? osz : n);
			lib_free(old);
		}
		A = np;

	/* --- strings -------------------------------------------------- */
	} else if (!strcmp(name, "strlen")) {
		A = (long)vstrlen((unsigned long)arg(0));
	} else if (!strcmp(name, "strcpy")) {
		unsigned long d = arg(0), s = arg(1);
		vcopy(d, s, vstrlen(s) + 1);
		A = d;
	} else if (!strcmp(name, "strncpy")) {
		unsigned long d = arg(0), s = arg(1), n = arg(2), l = vstrlen(s);
		if (l > n) l = n;
		vcopy(d, s, l);
		while (l < n) wr8(d + l++, 0);
		A = d;
	} else if (!strcmp(name, "strcat")) {
		unsigned long d = arg(0), s = arg(1);
		vcopy(d + vstrlen(d), s, vstrlen(s) + 1);
		A = d;
	} else if (!strcmp(name, "strcmp")) {
		A = strcmp(getstr((unsigned long)arg(0)),
			   getstr((unsigned long)arg(1)));
	} else if (!strcmp(name, "strncmp")) {
		A = strncmp(getstr((unsigned long)arg(0)),
			    getstr((unsigned long)arg(1)),
			    (size_t)arg(2));
	} else if (!strcmp(name, "strchr")) {
		unsigned long s = arg(0);
		int c = (int)arg(1);
		unsigned long i = 0, l = vstrlen(s);
		A = 0;
		for (; i <= l; i++)
			if (mem[s + i] == c) { A = (long)(s + i); break; }
	} else if (!strcmp(name, "strrchr")) {
		unsigned long s = arg(0);
		int c = (int)arg(1);
		long i = (long)vstrlen(s);
		A = 0;
		for (; i >= 0; i--)
			if (mem[s + i] == c) { A = (long)(s + i); break; }

	/* --- memory blocks -------------------------------------------- */
	} else if (!strcmp(name, "memset")) {
		unsigned long d = arg(0), n = arg(2);
		if (d + n > MEMSIZE) fault("bad address");
		memset(mem + d, (int)arg(1), n);
		A = d;
	} else if (!strcmp(name, "memcpy") || !strcmp(name, "memmove")) {
		vcopy(arg(0), arg(1), arg(2));
		A = arg(0);
	} else if (!strcmp(name, "memcmp")) {
		unsigned long a = arg(0), b = arg(1), n = arg(2);
		if (a + n > MEMSIZE || b + n > MEMSIZE) fault("bad address");
		A = memcmp(mem + a, mem + b, n);

	/* --- conversion ----------------------------------------------- */
	} else if (!strcmp(name, "atoi")) {
		A = atoi(getstr((unsigned long)arg(0)));
	} else if (!strcmp(name, "abs")) {
		long v = arg(0);
		A = v < 0 ? -v : v;

	/* --- files, straight onto the host's descriptors --------------- */
	} else if (!strcmp(name, "open")) {
		A = open(getstr((unsigned long)arg(0)), (int)arg(1), 0666);
	} else if (!strcmp(name, "creat")) {
		A = creat(getstr((unsigned long)arg(0)), 0666);
	} else if (!strcmp(name, "close")) {
		A = close((int)arg(0));
	} else if (!strcmp(name, "read")) {
		unsigned long b = arg(1), n = arg(2);
		if (b + n > MEMSIZE) fault("bad address");
		A = read((int)arg(0), mem + b, n);
	} else if (!strcmp(name, "write")) {
		unsigned long b = arg(1), n = arg(2);
		if (b + n > MEMSIZE) fault("bad address");
		A = write((int)arg(0), mem + b, n);
	} else if (!strcmp(name, "lseek")) {
		A = lseek((int)arg(0), arg(1), (int)arg(2));
	} else if (!strcmp(name, "unlink")) {
		A = unlink(getstr((unsigned long)arg(0)));

	/* --- platform ------------------------------------------------- */
	} else if (!strcmp(name, "adval")) {
		A = lib_adval((int)arg(0));
	} else if (!strcmp(name, "time_us")) {
		/* 31 bits of the SDK's time_us_64, via the kernel. Falls back
		 * to a host clock so the same program benchmarks on the
		 * development machine as well as on the PC3. */
		long t = lib_adval(-9);
		if (t < 0) {
			struct timeval tv;
			gettimeofday(&tv, NULL);
			t = (long)((tv.tv_sec * 1000000L + tv.tv_usec)
				   & 0x7FFFFFFF);
		}
		A = t;

	} else {
		lib_eqop(name);
	}
}

/* ---- loader -------------------------------------------------------- */

static unsigned long database, bssbase;

/* Resolve a symbol to whatever the machine needs it to be. */
static unsigned long symval(unsigned s)
{
	switch (sym[s].s_type) {
	case BC_SYM_CODE:
		return sym[s].s_value;		/* a code offset */
	case BC_SYM_DATA:
		return database + sym[s].s_value;
	case BC_SYM_BSS:
		return bssbase + sym[s].s_value;
	case BC_SYM_LIB:
		return s;			/* resolved by index */
	}
	return 0;
}

static void load(const char *path)
{
	FILE *f = fopen(path, "rb");
	unsigned long i;

	if (f == NULL) {
		perror(path);
		exit(1);
	}
	if (fread(&h, sizeof(h), 1, f) != 1 ||
	    memcmp(h.h_magic, BC_MAGIC, 4) != 0) {
		fprintf(stderr, "%s: not a bytecode object\n", path);
		exit(1);
	}
	if (h.h_version != BC_VERSION) {
		fprintf(stderr, "%s: version %u, expected %u\n", path,
			h.h_version, BC_VERSION);
		exit(1);
	}
	code = malloc(h.h_code ? h.h_code : 1);
	sym = malloc((h.h_nsym ? h.h_nsym : 1) * sizeof(struct bc_sym));
	fix = malloc((h.h_nfixup ? h.h_nfixup : 1) * sizeof(struct bc_fixup));

	if (fread(code, 1, h.h_code, f) != h.h_code)
		fault("short code");

	/*
	 * Data goes near the bottom of the program's address space, bss
	 * directly after it, and the stack starts at the top.
	 *
	 * Near the bottom, not at it: address zero must not be a valid
	 * object, or a null pointer is indistinguishable from whatever
	 * the linker happened to put first. It put a string literal
	 * there, so "abc" == (void *)0 was true. No real implementation
	 * places an object at zero and C leans on that everywhere.
	 */
	database = NULLGUARD;
	bssbase = database + h.h_data;
	/* The heap is whatever is left between bss and the stack. */
	heap_init(bssbase + h.h_bss);
	if (bssbase + h.h_bss + STACKROOM > MEMSIZE) {
		fprintf(stderr, "bcrun: program too large\n");
		exit(1);
	}
	if (h.h_data && fread(mem + database, 1, h.h_data, f) != h.h_data)
		fault("short data");
	memset(mem + bssbase, 0, h.h_bss);

	for (i = 0; i < h.h_nfixup; i++)
		if (fread(&fix[i], sizeof(struct bc_fixup), 1, f) != 1)
			fault("short fixups");
	for (i = 0; i < h.h_nsym; i++)
		if (fread(&sym[i], sizeof(struct bc_sym), 1, f) != 1)
			fault("short symbols");
	strtab = malloc(h.h_strsize ? h.h_strsize : 1);
	if (h.h_strsize && fread(strtab, 1, h.h_strsize, f) != h.h_strsize)
		fault("short string table");
	fclose(f);

	/* Apply fixups: add the symbol's value to the 32bit field. */
	for (i = 0; i < h.h_nfixup; i++) {
		unsigned long v = symval(fix[i].f_sym);
		unsigned long o = fix[i].f_offset;

		/*
		 * A call to a name this module never defines is a library
		 * call: the compiler cannot tell them apart when it emits
		 * the call, because the definition may come later. Rewrite
		 * it here. BC_CALL's operand is four bytes and BC_LIBCALL's
		 * is two, so the two spare bytes become NOPs and the
		 * instruction keeps its length.
		 */
		if (fix[i].f_seg == BC_SEG_CODE && o > 0 &&
		    code[o - 1] == BC_CALL && sym[fix[i].f_sym].s_type == BC_SYM_LIB) {
			code[o - 1] = BC_LIBCALL;
			code[o] = fix[i].f_sym;
			code[o + 1] = fix[i].f_sym >> 8;
			code[o + 2] = BC_NOP;
			code[o + 3] = BC_NOP;
			continue;
		}

		/*
		 * A runtime library symbol has no address: it is resolved
		 * by index, and the rewrite above is the only thing that
		 * can use one. Anywhere else - "&fprintf", or a table of
		 * them - the index would be stored as though it were a code
		 * address and an indirect call through it would jump into
		 * nowhere. Say so rather than letting it run.
		 */
		if (sym[fix[i].f_sym].s_type == BC_SYM_LIB) {
			fprintf(stderr,
				"bcrun: cannot take the address of library "
				"function \"%s\"\n",
				strtab + sym[fix[i].f_sym].s_name);
			exit(1);
		}

		if (fix[i].f_seg == BC_SEG_CODE) {
			unsigned long old = code[o] |
			    ((unsigned long)code[o + 1] << 8) |
			    ((unsigned long)code[o + 2] << 16) |
			    ((unsigned long)code[o + 3] << 24);
			v += old;
			code[o] = v;
			code[o + 1] = v >> 8;
			code[o + 2] = v >> 16;
			code[o + 3] = v >> 24;
		} else {
			wr32(database + o, rd32(database + o) + v);
		}
	}
}

/* ---- the interpreter ------------------------------------------------ */

static int run(void)
{
	sp = MEMSIZE - 4;
	pc = h.h_entry;
	/* A return to this impossible address ends the program. */
	push(0xFFFFFFFFUL);

	for (;;) {
		unsigned char op;
		int64_t b;

		if (trace)
			fprintf(stderr, "%04lx: op %02x A=%ld sp=%lx\n",
				pc, code[pc], A, sp);
		op = fetch8();
		switch (op) {
		case BC_NOP:
			break;
		case BC_CONST8:
			A = (signed char)fetch8();
			break;
		case BC_CONST16:
			A = (short)fetch16();
			break;
		case BC_CONST32:
			A = S32(fetch32());
			break;
		case BC_ADDR:
			A = (long)fetch32();
			break;
		case BC_LOCAL8:
			A = sp + fetch8();
			break;
		case BC_LOCAL16:
			A = sp + fetch16();
			break;

		case BC_PUSH:
			push(A);
			break;
		case BC_POP:
			A = pop();
			break;
		case BC_DUP:
			push((long)rd32(sp));
			break;
		case BC_SWAP:
			b = (long)rd32(sp);
			wr32(sp, A);
			A = b;
			break;
		case BC_DROP:
			sp += 4;
			break;

		case BC_LOAD8S:
			A = (signed char)rd8(A);
			break;
		case BC_LOAD8U:
			A = rd8(A);
			break;
		case BC_LOAD16S:
			A = (short)rd16(A);
			break;
		case BC_LOAD16U:
			A = rd16(A);
			break;
		case BC_LOAD32:
			A = S32(rd32(A));
			break;

		case BC_STORE8:
			wr8((unsigned long)pop(), A);
			break;
		case BC_STORE16:
			wr16((unsigned long)pop(), A);
			break;
		case BC_STORE32:
			wr32((unsigned long)pop(), A);
			break;

		/*
		 * The accumulator is 64 bits, so every 32-bit result has to
		 * be truncated back or "int" stops wrapping the way C
		 * requires: 2000000000 + 2000000000 must give -294967296,
		 * not 4000000000.
		 */
		case BC_ADD:	A = S32(pop() + A); break;
		case BC_SUB:	A = S32(pop() - A); break;
		case BC_MUL:	A = S32(pop() * A); break;
		case BC_DIVS:	b = pop(); A = A ? S32(b / A) : 0; break;
		case BC_DIVU:	b = pop();
				A = A ? S32(U32(b) / U32(A)) : 0;
				break;
		case BC_REMS:	b = pop(); A = A ? S32(b % A) : 0; break;
		case BC_REMU:	b = pop();
				A = A ? S32(U32(b) % U32(A)) : 0;
				break;
		case BC_AND:	A = S32(pop() & A); break;
		case BC_OR:	A = S32(pop() | A); break;
		case BC_XOR:	A = S32(pop() ^ A); break;
		case BC_SHL:	A = S32(pop() << A); break;
		case BC_SHRS:	A = S32(pop() >> A); break;
		case BC_SHRU:	b = pop();
				A = S32(U32(b) >> A);
				break;
		case BC_NEG:	A = S32(-A); break;
		case BC_NOT:	A = S32(~A); break;
		case BC_LNOT:	A = !A; break;

		case BC_EQ:	A = (pop() == A); break;
		case BC_NE:	A = (pop() != A); break;
		case BC_LTS:	A = (pop() < A); break;
		case BC_LTU:	b = pop();
				A = (U32(b) < U32(A));
				break;
		case BC_GTS:	A = (pop() > A); break;
		case BC_GTU:	b = pop();
				A = (U32(b) > U32(A));
				break;
		case BC_LES:	A = (pop() <= A); break;
		case BC_LEU:	b = pop();
				A = (U32(b) <= U32(A));
				break;
		case BC_GES:	A = (pop() >= A); break;
		case BC_GEU:	b = pop();
				A = (U32(b) >= U32(A));
				break;
		case BC_BOOL:	A = (A != 0); break;

		/* ---- 64-bit: no truncation, two stack slots ---------- */
		case BC_CONST64:	A = fetch64(); break;
		case BC_LOAD64:		A = (int64_t)rd64((unsigned long)A); break;
		case BC_STORE64:	wr64((unsigned long)pop(), (uint64_t)A);
					break;
		case BC_PUSH64:		push64(A); break;
		case BC_POP64:		A = pop64(); break;
		case BC_SEXT32:		A = (int64_t)(int32_t)A; break;
		case BC_ZEXT32:		A = (int64_t)(uint32_t)A; break;
		case BC_TRUNC64:	A = S32(A); break;

		case BC_ADD64:	A = pop64() + A; break;
		case BC_SUB64:	A = pop64() - A; break;
		case BC_MUL64:	A = pop64() * A; break;
		case BC_DIVS64:	b = pop64(); A = A ? b / A : 0; break;
		case BC_DIVU64:	b = pop64();
				A = A ? (int64_t)((uint64_t)b / (uint64_t)A) : 0;
				break;
		case BC_REMS64:	b = pop64(); A = A ? b % A : 0; break;
		case BC_REMU64:	b = pop64();
				A = A ? (int64_t)((uint64_t)b % (uint64_t)A) : 0;
				break;
		case BC_AND64:	A = pop64() & A; break;
		case BC_OR64:	A = pop64() | A; break;
		case BC_XOR64:	A = pop64() ^ A; break;
		case BC_SHL64:	A = pop64() << A; break;
		case BC_SHRS64:	A = pop64() >> A; break;
		case BC_SHRU64:	b = pop64();
				A = (int64_t)((uint64_t)b >> A);
				break;
		case BC_NEG64:	A = -A; break;
		case BC_NOT64:	A = ~A; break;
		case BC_LNOT64:	A = !A; break;

		case BC_EQ64:	A = (pop64() == A); break;
		case BC_NE64:	A = (pop64() != A); break;
		case BC_LTS64:	A = (pop64() < A); break;
		case BC_LTU64:	b = pop64();
				A = ((uint64_t)b < (uint64_t)A); break;
		case BC_GTS64:	A = (pop64() > A); break;
		case BC_GTU64:	b = pop64();
				A = ((uint64_t)b > (uint64_t)A); break;
		case BC_LES64:	A = (pop64() <= A); break;
		case BC_LEU64:	b = pop64();
				A = ((uint64_t)b <= (uint64_t)A); break;
		case BC_GES64:	A = (pop64() >= A); break;
		case BC_GEU64:	b = pop64();
				A = ((uint64_t)b >= (uint64_t)A); break;
		case BC_BOOL64:	A = (A != 0); break;

		/*
		 * Floating point. The accumulator holds the bit pattern, so
		 * every one of these unpacks, operates and repacks. dget and
		 * friends go through a union: casting an int64_t* to double*
		 * would be an aliasing violation and gcc is entitled to
		 * assume it never happens.
		 */
		case BC_ADDD:	A = dput(dget(pop64()) + dget(A)); break;
		case BC_SUBD:	A = dput(dget(pop64()) - dget(A)); break;
		case BC_MULD:	A = dput(dget(pop64()) * dget(A)); break;
		case BC_DIVD:	A = dput(dget(pop64()) / dget(A)); break;
		case BC_NEGD:	A = dput(-dget(A)); break;
		case BC_EQD:	A = (dget(pop64()) == dget(A)); break;
		case BC_NED:	A = (dget(pop64()) != dget(A)); break;
		case BC_LTD:	A = (dget(pop64()) <  dget(A)); break;
		case BC_GTD:	A = (dget(pop64()) >  dget(A)); break;
		case BC_LED:	A = (dget(pop64()) <= dget(A)); break;
		case BC_GED:	A = (dget(pop64()) >= dget(A)); break;
		case BC_BOOLD:	A = (dget(A) != 0.0); break;
		case BC_LNOTD:	A = (dget(A) == 0.0); break;

		case BC_ADDF:	A = fput(fget(pop()) + fget(A)); break;
		case BC_SUBF:	A = fput(fget(pop()) - fget(A)); break;
		case BC_MULF:	A = fput(fget(pop()) * fget(A)); break;
		case BC_DIVF:	A = fput(fget(pop()) / fget(A)); break;
		case BC_NEGF:	A = fput(-fget(A)); break;
		case BC_EQF:	A = (fget(pop()) == fget(A)); break;
		case BC_NEF:	A = (fget(pop()) != fget(A)); break;
		case BC_LTF:	A = (fget(pop()) <  fget(A)); break;
		case BC_GTF:	A = (fget(pop()) >  fget(A)); break;
		case BC_LEF:	A = (fget(pop()) <= fget(A)); break;
		case BC_GEF:	A = (fget(pop()) >= fget(A)); break;
		case BC_BOOLF:	A = (fget(A) != 0.0f); break;
		case BC_LNOTF:	A = (fget(A) == 0.0f); break;

		case BC_I2D:	A = dput((double)A); break;
		case BC_U2D:	A = dput((double)(uint64_t)A); break;
		case BC_D2I:	A = (int64_t)dget(A); break;
		case BC_D2U:	A = (int64_t)(uint64_t)dget(A); break;
		case BC_I2F:	A = fput((float)A); break;
		case BC_U2F:	A = fput((float)(uint64_t)A); break;
		case BC_F2I:	A = (int64_t)fget(A); break;
		case BC_F2U:	A = (int64_t)(uint64_t)fget(A); break;
		case BC_F2D:	A = dput((double)fget(A)); break;
		case BC_D2F:	A = fput((float)dget(A)); break;

		/*
		 * Block copy: a struct is moved by address because it does
		 * not fit in the accumulator. Destination is left in A so
		 * that an assignment still yields the object it assigned to.
		 */
		case BC_COPY: {
			unsigned long len = fetch16();
			unsigned long src = (unsigned long)A;
			unsigned long dst = (unsigned long)pop();
			vcopy(dst, src, len);
			A = dst;
			break;
		}

		case BC_PUSHN: {
			unsigned long len = fetch16();
			unsigned long src = (unsigned long)A;
			unsigned long n = (len < 4) ? 4 : ((len + 3) & ~3UL);
			sp -= n;
			vcopy(sp, src, len);
			break;
		}

		case BC_SEXT8:	A = (signed char)A; break;
		case BC_SEXT16:	A = (short)A; break;
		case BC_ZEXT8:	A = A & 0xFF; break;
		case BC_ZEXT16:	A = A & 0xFFFF; break;

		case BC_JUMP:
			b = (short)fetch16();
			pc += b;
			break;
		case BC_JFALSE:
			b = (short)fetch16();
			if (!A)
				pc += b;
			break;
		case BC_JTRUE:
			b = (short)fetch16();
			if (A)
				pc += b;
			break;
		case BC_CALL:
			b = (long)fetch32();
			push(pc);
			pc = b;
			break;
		case BC_CALLA:
			push(pc);
			pc = A;
			break;
		case BC_RET:
			/* Mask: pop() sign extends, so the sentinel comes
			   back as -1 and would widen past 32 bits here. */
			pc = (unsigned long)pop() & 0xFFFFFFFFUL;
			if (pc == 0xFFFFFFFFUL)
				return (int)A;
			break;
		case BC_ENTER:
			sp -= fetch16();
			break;
		case BC_LEAVE:
			sp += fetch16();
			break;
		case BC_ARGS:
			sp += fetch8();
			break;
		case BC_LIBCALL:
			libcall(fetch16());
			break;
		case BC_SWITCH:
			/*
			 * Table, all 32-bit words, built by gen_switchdata /
			 * gen_case_data:
			 *
			 *   [count][value0][label0]...[valueN-1][labelN-1][default]
			 *
			 * The labels are code offsets, already resolved by the
			 * fixups. Values are word sized because C promotes the
			 * switch expression to at least int.
			 */
			{
				unsigned long tab = fetch32();
				unsigned long cases = rd32(tab);
				unsigned long i;
				unsigned long target = rd32(tab + 4 + 8 * cases);

				for (i = 0; i < cases; i++) {
					if (S32(rd32(tab + 4 + 8 * i)) == A) {
						target = rd32(tab + 8 + 8 * i);
						break;
					}
				}
				pc = target;
			}
			break;
		default:
			fault("bad opcode");
		}
	}
}

int main(int argc, char *argv[])
{
	int i = 1;

	while (i < argc && argv[i][0] == '-') {
		if (!strcmp(argv[i], "-t"))
			trace = 1;
		i++;
	}
	if (i >= argc) {
		fprintf(stderr, "usage: bcrun [-t] program.bc\n");
		return 1;
	}
	load(argv[i]);
	return run();
}
