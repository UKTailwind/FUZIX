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
#include <math.h>
#include <time.h>
#ifdef __linux__
#include <sys/mman.h>		/* executable code buffer for native fns */
#ifdef MM_PC3
#include <sys/ioctl.h>		/* the PSRAM heap request, once, at load */
#endif
#endif
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
/*
 *	On the development machine the program gets a full 128K.  On the
 *	board bcrun is itself a 256K Fuzix process which must also hold
 *	the loaded code (the mmb2c runtime plus a large program is ~90K),
 *	so the data space gives way: 64K, of which a typical translated
 *	program touches a fraction.  BIG_TABLES is the existing "built
 *	for a machine with room" switch.
 */
#ifdef BIG_TABLES
#define MEMSIZE		131072		/* program address space */
#else
/* 48K: the largest translated program so far uses ~8K of data+bss and
   the stack; what remains is heap.  Every byte given here is taken
   from the loader's mallocs (code, symbols, string table) in the same
   256K process. */
#define MEMSIZE		49152
#endif
/* Dead space at the bottom so that no object can live at address 0 and
   a null pointer stays distinguishable from a real one. */
#define NULLGUARD	16
#define STACKROOM	8192		/* kept clear at the top for the stack */

static unsigned char *code;
/* Aligned, not by linker luck: the native mm runtime (and native code
   to come) dereference VM offsets through real pointers, so mem's own
   base must not break the alignment the offsets were given. */
#if UINTPTR_MAX > 0xFFFFFFFFu
/*
 *	A VM address is 32 bits wide because the bytecode says so, and now
 *	that it is also a machine address the backing store has to live
 *	where 32 bits can name it.  On the board that is free - everything
 *	is below 4G.  On a 64-bit development host it has to be asked for.
 */
static unsigned char *mem;

static void mem_init(void)
{
	int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#ifdef MAP_32BIT
	flags |= MAP_32BIT;
#endif
	/* The hint is what does the work where MAP_32BIT does not exist;
	   the check afterwards is what makes it honest either way. */
	mem = mmap((void *)0x30000000UL, MEMSIZE, PROT_READ | PROT_WRITE,
		   flags, -1, 0);
	if (mem == MAP_FAILED || (uintptr_t)mem > 0xFFFFFFFFUL - MEMSIZE) {
		fprintf(stderr, "bcrun: no low memory for the program's "
				"address space\n");
		exit(1);
	}
}
#else
/*
 *	SIZED TO THE PROGRAM, not to MEMSIZE.
 *
 *	This used to be a static 49,152-byte array, and it was the single
 *	biggest thing in a running bcrun: 154,469 bytes of process image,
 *	of which 48K was this whether the program needed it or not.  Two
 *	BASIC programs then nearly filled the 340K pool.
 *
 *	Almost none of it was wanted.  On the board the HEAP is in PSRAM
 *	(heap_init below), so this has to hold only the null guard, the
 *	program's data and bss, and the stack - a few hundred bytes plus
 *	STACKROOM for a program that blinks an LED.
 *
 *	malloc, so it grows the process through sbrk -> brk_extend ->
 *	pagemap_realloc, which is the same path BBC BASIC uses to size
 *	its workspace.  MEMSIZE survives as the CAP.
 */
#ifdef MM_PC3
static unsigned char *mem;		/* sized in load(), see there */
#else
/* Everywhere else the heap is still in here, so it stays the full
   fixed size - see the note in load(). */
static unsigned char mem[MEMSIZE] __attribute__((aligned(8)));
#endif
#define mem_init()	do { } while (0)
#endif

/*
 *	How much of mem[] was actually obtained.  MEMTOP is derived from
 *	it rather than from MEMSIZE, because the two are no longer the
 *	same thing.
 */
static unsigned long memsize = MEMSIZE;

/*
 *	Spare room above everything else, so that a PSRAM heap which does
 *	not answer leaves the program something rather than nothing.  On
 *	the board heap_init moves the heap out to PSRAM and this is never
 *	touched.
 */
#define MEM_SLACK	2048

/*
 *	The by-ref and string-temp pools mmrt_reserve carves out of VM
 *	memory between bss and the heap.  Defined with them in
 *	bcrun_mm.c, which is included at the end of this file - sizing
 *	mem[] has to know about them, and the alternative was repeating
 *	MM_TMPN * MM_STRSZ here and watching the two drift apart.  It
 *	was 4,240 bytes against a 4,096-byte guess, which cost a board
 *	round trip to discover.
 */
static unsigned long mmrt_bytes(void);
static struct bc_header h;
static struct bc_sym *sym;
static char *strtab;

/*
 *	The mm_* BASIC runtime lives in bcrun natively - bcrun_mm.c,
 *	included at the end of this file.  libbind caches the wrapper
 *	resolved for each library symbol so the name is matched once per
 *	run, not once per call.
 */
static void (**libbind)(void);
static void (*mm_wrap_lookup(const char *name))(void);
static unsigned long mmrt_reserve(unsigned long base);

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

/* Machine state. sp and fp are machine addresses, and the stack grows
   down from the top of mem[]. */
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

/*
 *	A program address IS a machine address.  There is one kind, and
 *	mem[] is only where the loader happens to put data and bss.
 *
 *	It used to be an offset into mem[], which meant every access was
 *	base+index.  That model broke the moment a program could hold
 *	memory the VM does not own: mmb2c puts every array and string in
 *	one block from the kernel's PSRAM heap, because 48K of VM address
 *	space cannot hold a 38,400 byte array and never could.  Teaching
 *	the C paths to recognise a real address was not enough - native
 *	code reaches memory as "ldr r3, [r6, r2]" with r6 = mem, in
 *	hardware, where no test can be inserted.  So the offset goes: the
 *	loader relocates the program to where mem[] actually is, once, and
 *	from then on an address is an address.  Native code needs no
 *	change at all, because r6 becomes 0.
 *
 *	Cost: nothing bounds-checks a program address any more.  That was
 *	already true of every heap pointer, and the alternative was a
 *	compare and branch on every load and store in generated code.
 */
#define MEMBASE		((unsigned long)(uintptr_t)mem)
#define MEMTOP		(MEMBASE + memsize)

static unsigned char *vptr(unsigned long a)
{
	return (unsigned char *)(uintptr_t)a;
}

/* Kept as names so the call sites still say what they mean, but there
 * is nothing left to check: the heap a program may address extends
 * beyond mem[] and only the kernel knows where it ends. */
#define VM_OOB(a, n)	(0)
#define VM_OOBN(a, n)	(0)

static unsigned long rd32(unsigned long a)
{
	unsigned char *p;
	if (VM_OOB(a, 3))
		fault("bad address");
	p = vptr(a);
	return p[0] | ((unsigned long)p[1] << 8) |
	    ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

static void wr32(unsigned long a, unsigned long v)
{
	unsigned char *p;
	if (VM_OOB(a, 3))
		fault("bad address");
	p = vptr(a);
	p[0] = v;
	p[1] = v >> 8;
	p[2] = v >> 16;
	p[3] = v >> 24;
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
	unsigned char *p;
	if (VM_OOB(a, 1))
		fault("bad address");
	p = vptr(a);
	return p[0] | (p[1] << 8);
}

static void wr16(unsigned long a, unsigned v)
{
	unsigned char *p;
	if (VM_OOB(a, 1))
		fault("bad address");
	p = vptr(a);
	p[0] = v;
	p[1] = v >> 8;
}

static unsigned rd8(unsigned long a)
{
	if (VM_OOB(a, 0))
		fault("bad address");
	return *vptr(a);
}

static void wr8(unsigned long a, unsigned v)
{
	if (VM_OOB(a, 0))
		fault("bad address");
	*vptr(a) = v;
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
 *	See BYTECODE.md.  The name encodes everything - "pluseq4s",
 *	"shreq8u", "postincd" - and is parsed ONCE per symbol into a
 *	descriptor cached in eqbind: these are the hottest libcalls a
 *	program has (every += on a double or long long), and the old
 *	per-call string parse was a measurable slice of the eclipse.
 *
 *	Descriptor: bit15 valid, bit14 fp (size 4 = float, 8 = double),
 *	bits 13-12 log2 size, bit11 unsigned, bits 3-0 kind.
 */
#define EQ_VALID	0x8000
#define EQ_FP		0x4000
#define EQ_SZLOG(d)	(((d) >> 12) & 3)
#define EQ_UNS		0x0800
#define EQ_KIND(d)	((d) & 15)

enum {
	EQK_PLUS, EQK_MINUS, EQK_MUL, EQK_DIV, EQK_REM, EQK_AND,
	EQK_OR, EQK_XOR, EQK_SHL, EQK_SHR, EQK_POSTINC, EQK_POSTDEC
};

static const char *const eqnames[] = {
	"pluseq", "minuseq", "muleq", "diveq", "remeq", "andeq",
	"oreq", "xoreq", "shleq", "shreq", "postinc", "postdec", NULL
};

static unsigned parse_eqop(const char *name)
{
	unsigned k, szlog, uns = 0;
	const char *t;

	for (k = 0; eqnames[k]; k++)
		if (strncmp(name, eqnames[k], strlen(eqnames[k])) == 0)
			break;
	if (!eqnames[k])
		return 0;
	t = name + strlen(eqnames[k]);
	if ((t[0] == 'd' || t[0] == 'f') && !t[1]) {
		/* the fp family has no remainder, logic or shifts */
		if (k >= EQK_REM && k <= EQK_SHR)
			return 0;
		return EQ_VALID | EQ_FP | ((t[0] == 'd' ? 3u : 2u) << 12) | k;
	}
	switch (t[0]) {
	case '1': szlog = 0; break;
	case '2': szlog = 1; break;
	case '4': szlog = 2; break;
	case '8': szlog = 3; break;
	default: return 0;
	}
	if (t[1] == 'u')
		uns = EQ_UNS;
	else if (t[1] != 's')
		return 0;
	if (t[2])
		return 0;
	return EQ_VALID | (szlog << 12) | uns | k;
}

static void exec_eqop(unsigned d)
{
	unsigned long addr = (unsigned long)pop();
	unsigned k = EQ_KIND(d);
	unsigned post = (k == EQK_POSTINC || k == EQK_POSTDEC);
	unsigned uns = (d & EQ_UNS) != 0;
	int64_t old, v, res;

	if (d & EQ_FP) {
		/* both float and double compute in double, exactly as the
		   original per-name code did */
		int dbl = (EQ_SZLOG(d) == 3);
		double fv = dbl ? dget(A) : (double)fget(A);
		double fold = dbl ? dget((int64_t)rd64(addr))
				  : (double)fget(S32(rd32(addr)));
		double fres;

		switch (k) {
		case EQK_PLUS: case EQK_POSTINC:
			fres = fold + fv; break;
		case EQK_MINUS: case EQK_POSTDEC:
			fres = fold - fv; break;
		case EQK_MUL:
			fres = fold * fv; break;
		default:
			fres = fv ? fold / fv : 0.0; break;
		}
		fv = post ? fold : fres;
		if (dbl) {
			wr64(addr, (uint64_t)dput(fres));
			A = dput(fv);
		} else {
			wr32(addr, (unsigned long)(uint32_t)fput((float)fres));
			A = fput((float)fv);
		}
		return;
	}

	/* The amount for a 32-bit-or-narrower object is itself a 32-bit
	   value: read it at that width, per the low-32 contract (the
	   high word is meaningless when it came through the native seam) */
	if (EQ_SZLOG(d) == 3)
		v = A;
	else
		v = uns ? (int64_t)U32(A) : S32(A);

	switch (EQ_SZLOG(d)) {
	case 0:
		old = uns ? (int64_t)rd8(addr)
			  : (int64_t)(signed char)rd8(addr);
		break;
	case 1:
		old = uns ? (int64_t)rd16(addr)
			  : (int64_t)(short)rd16(addr);
		break;
	case 2:
		old = uns ? (int64_t)(uint32_t)rd32(addr)
			  : (int64_t)S32(rd32(addr));
		break;
	default:
		old = (int64_t)rd64(addr);
	}

	switch (k) {
	case EQK_PLUS: case EQK_POSTINC:
		res = old + v; break;
	case EQK_MINUS: case EQK_POSTDEC:
		res = old - v; break;
	case EQK_MUL:
		res = old * v; break;
	case EQK_DIV:
		res = v ? (uns ? (int64_t)((uint64_t)old / (uint64_t)v)
			       : old / v) : 0;
		break;
	case EQK_REM:
		res = v ? (uns ? (int64_t)((uint64_t)old % (uint64_t)v)
			       : old % v) : 0;
		break;
	case EQK_AND:	res = old & v; break;
	case EQK_OR:	res = old | v; break;
	case EQK_XOR:	res = old ^ v; break;
	case EQK_SHL:	res = old << v; break;
	default:
		res = uns ? (int64_t)((uint64_t)old >> v) : (old >> v);
	}

	/* Store at the object's own width, so a carry cannot escape into
	   whatever lives next to it. */
	switch (EQ_SZLOG(d)) {
	case 0:	wr8(addr, res); break;
	case 1:	wr16(addr, res); break;
	case 2:	wr32(addr, (unsigned long)(uint32_t)res); break;
	default: wr64(addr, (uint64_t)res);
	}

	A = post ? old : res;
	/* A carries 32-bit values sign extended, whatever their type */
	if (EQ_SZLOG(d) == 2)
		A = S32((uint32_t)A);
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
	const unsigned char *s = vptr(a);
	unsigned i = 0;
	unsigned lim = 511;		/* the buffer, nothing else */

	while (i < lim && s[i])
		b[i] = s[i], i++;
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
 *	A long long argument, likewise two slots.
 */
static long long argll(unsigned n)
{
	return (long long)rd64(sp + 4 * n);
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
	if (out_to_mem)
		*vptr(out_at++) = (uint8_t) c;
	else if (out_fd == 1)
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

/*
 *	64-bit integer conversions are formatted here, never delegated
 *	to the host sprintf: the board's libc printf reads a 'long' for
 *	"ll" and prints the low word.  One code path on every host also
 *	means one output.
 */
static void fmt64(char *tmp, uint64_t v, unsigned radix, int upper, int neg)
{
	char buf[24];
	char *p = buf + sizeof(buf);
	const char *d = upper ? "0123456789ABCDEF" : "0123456789abcdef";

	*--p = 0;
	do {
		*--p = d[v % radix];
		v /= radix;
	} while (v);
	if (neg)
		*--p = '-';
	strcpy(tmp, p);
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
		int left = 0, zero = 0, width = 0, prec = -1, lng = 0;

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

		/*
		 * Length modifiers. These had no handling at all, so "%ld"
		 * fell through to the default below and printed itself
		 * literally - and, worse, consumed no argument, so every
		 * conversion after it took the wrong one.
		 *
		 * h and l are both no-ops here: char and short promote to
		 * int in a variadic call, and long is 32 bits on this
		 * target, the same as int. Only ll changes anything,
		 * because a long long really does occupy two slots.
		 */
		if (*f == 'h') {
			f++;
			if (*f == 'h')
				f++;
		} else if (*f == 'l') {
			f++;
			lng = 1;
			if (*f == 'l') {
				f++;
				lng = 2;
			}
		}

		switch (*f) {
		case 'd':
		case 'i':
			{
				int64_t v;
				uint64_t uv;
				if (lng == 2) {
					v = argll(a);
					a += 2;
				} else
					v = arg(a++);
				uv = (uint64_t)v;
				if (v < 0)
					uv = (uint64_t)0 - uv;
				fmt64(tmp, uv, 10, 0, v < 0);
			}
			padout(tmp, width, left, zero);
			break;
		case 'u':
			if (lng == 2) {
				fmt64(tmp, (uint64_t)argll(a), 10, 0, 0);
				a += 2;
			} else
				fmt64(tmp, U32(arg(a++)), 10, 0, 0);
			padout(tmp, width, left, zero);
			break;
		case 'x':
		case 'X':
			if (lng == 2) {
				fmt64(tmp, (uint64_t)argll(a), 16,
				      *f == 'X', 0);
				a += 2;
			} else
				fmt64(tmp, U32(arg(a++)), 16, *f == 'X', 0);
			padout(tmp, width, left, zero);
			break;
		case 'o':
			if (lng == 2) {
				fmt64(tmp, (uint64_t)argll(a), 8, 0, 0);
				a += 2;
			} else
				fmt64(tmp, U32(arg(a++)), 8, 0, 0);
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

	if (fd < 0 || size == 0 || VM_OOBN(b, size * n)) {
		A = 0;
		return;
	}
	got = read(fd, vptr(b), size * n);
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

	if (fd < 0 || size == 0 || VM_OOBN(b, size * n)) {
		A = 0;
		return;
	}
	if (fd == 1)
		fflush(stdout);		/* keep printf and fwrite in order */
	put = write(fd, vptr(b), size * n);
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
	unsigned char *p;

	if (fd < 0 || n <= 0 || VM_OOBN(b, n)) {
		A = 0;
		return;
	}
	p = vptr(b);
	while (i < n - 1) {
		if (read(fd, &c, 1) != 1) {
			file_eof[fd] = 1;
			break;
		}
		p[i++] = c;
		if (c == '\n')
			break;
	}
	if (i == 0) {
		A = 0;			/* NULL */
		return;
	}
	p[i] = 0;
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
	*vptr(out_at) = 0;		/* terminate, not counted */
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

/*
 *	The heap does not have to be inside mem[] any more.
 *
 *	It used to be the gap between bss and the stack, which capped
 *	every allocation a translated program made at whatever was left
 *	of 48K - and mmb2c now puts all of a program's arrays and strings
 *	in one such allocation.  A program address is a machine address
 *	now, so heap_base and heap_top are just addresses: ask the kernel
 *	for PSRAM once, here, and the same first-fit allocator serves
 *	megabytes at exactly the cost it served kilobytes.
 *
 *	Once, and not per allocation, because the syscall is the
 *	expensive part.  Measured on the PC2: an alloc/free pair through
 *	the kernel is 5142ns, of which 3146 is the two ioctls; the same
 *	pair here is 350ns.  Over the eclipse's 219,063 routine calls
 *	that is the difference between per-call locals costing 40% and
 *	costing 3%.
 *
 *	BCRUN_HEAP overrides the size in KB.  Falling back to the mem[]
 *	gap is not an error: it is what the development host and any
 *	board without PSRAM get, and small programs never notice.
 */
#define VM_HEAP_KB	512

#ifdef MM_PC3
#define PSRAMIOC_ALLOC	0x000A
struct vm_psram_req {
	unsigned long len;
	unsigned long base;
};

static unsigned long psram_heap(unsigned long want)
{
	struct vm_psram_req rq;
	int fd = open("/dev/sys", O_RDWR);

	if (fd < 0)
		return 0;
	rq.len = want;
	rq.base = 0;
	if (ioctl(fd, PSRAMIOC_ALLOC, &rq) != 0)
		rq.base = 0;
	close(fd);
	return rq.base;
}
#endif

static void heap_init(unsigned long base)
{
	heap_base = (base + 3) & ~3UL;
	heap_top = MEMTOP - STACKROOM;
#ifdef MM_PC3
	{
		const char *e = getenv("BCRUN_HEAP");
		unsigned long kb = e ? (unsigned long)atoi(e) : VM_HEAP_KB;
		unsigned long region = kb ? psram_heap(kb << 10) : 0;
		if (region) {
			heap_base = region;
			heap_top = region + (kb << 10);
		}
	}
#endif
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

/*
 *	Microseconds since boot, all 64 bits.  ADVAL(-9) returns only 31
 *	of them in the ioctl result and so wraps every 36 minutes; -10 is
 *	the same counter written back through an 8-byte buffer whose low
 *	word carried the selector.  A kernel that predates -10 returns 0
 *	and writes nothing, which leaves the sentinel in place - hence
 *	the compare rather than a check of the ioctl result.  Falls back
 *	to the wall clock so the same program times on the development
 *	machine as well as on the PC3.
 */
static long long lib_us64(void)
{
	int fd = sys_open();
	union { long long v; int sel; } u;	/* low word = the selector */

	if (fd >= 0) {
		u.v = -10;
		ioctl(fd, PICOIOC_ADVAL, &u);
		if (u.v != -10)
			return u.v;
	}
	{
		struct timeval tv;
		gettimeofday(&tv, NULL);
		return (long long)tv.tv_sec * 1000000LL + tv.tv_usec;
	}
}

/* ---- string and memory, all working in the program's address space -- */

static unsigned long vstrlen(unsigned long a)
{
	return (unsigned long)strlen((const char *)vptr(a));
}

static void vcopy(unsigned long d, unsigned long s, unsigned long n)
{
	if (VM_OOBN(d, n) || VM_OOBN(s, n))
		fault("bad address");
	memmove(vptr(d), vptr(s), n);
}

/*
 *	Word-at-a-time strcpy/strcmp/strlen.  These are both the
 *	interpreter's string family (through the lib_* wrappers below)
 *	and the version-4 fast helper slots the translator BLs into
 *	directly, so the two paths cannot disagree about semantics.
 *	strcmp keeps the byte-difference return of the original loop.
 *	The zero-in-word test is the usual (w - 0x01010101) & ~w & 0x80..
 */
/* uint32_t, NOT unsigned long: the zero-in-word masks are 32-bit
   patterns, and unsigned long is 64 bits on the development host -
   a NUL in the upper half of a 64-bit word sails straight past the
   test.  The suite caught it (strlen 17 for a 15-byte string).  The
   same class of bug is in STATE.md twice; this makes three. */
static char *ns_strcpy(char *d, const char *s)
{
	char *d0 = d;

	if ((((uintptr_t)d | (uintptr_t)s) & 3) == 0) {
		uint32_t *dw = (uint32_t *)d;
		const uint32_t *sw = (const uint32_t *)s;
		uint32_t w;

		for (;;) {
			w = *sw;
			if ((w - 0x01010101UL) & ~w & 0x80808080UL)
				break;
			*dw++ = w;
			sw++;
		}
		d = (char *)dw;
		s = (const char *)sw;
	}
	while ((*d++ = *s++) != 0)
		;
	return d0;
}

static int ns_strcmp(const char *a, const char *b)
{
	if ((((uintptr_t)a | (uintptr_t)b) & 3) == 0) {
		const uint32_t *aw = (const uint32_t *)a;
		const uint32_t *bw = (const uint32_t *)b;

		for (;;) {
			uint32_t x = *aw;

			if (x != *bw)
				break;
			if ((x - 0x01010101UL) & ~x & 0x80808080UL)
				return 0;
			aw++;
			bw++;
		}
		a = (const char *)aw;
		b = (const char *)bw;
	}
	while (*a && *a == *b) {
		a++;
		b++;
	}
	return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static unsigned long ns_strlen(const char *s)
{
	const char *s0 = s;

	if (((uintptr_t)s & 3) == 0) {
		const uint32_t *sw = (const uint32_t *)s;

		while (!((*sw - 0x01010101UL) & ~*sw & 0x80808080UL))
			sw++;
		s = (const char *)sw;
	}
	while (*s)
		s++;
	return (unsigned long)(s - s0);
}

/* The memcpy slot: lib_memcpy has always been memmove underneath
   (vcopy), so the fast slot is too - same answer for the overlapping
   copies that memcpy proper would be allowed to garble. */
static void *ns_memcpy(void *d, const void *s, unsigned long n)
{
	return memmove(d, s, n);
}

/*
 *	64-bit strtol, both signednesses.  Fuzix libc stops at 32 bits,
 *	so this is done here once for every host rather than sometimes
 *	by the C library.  C90 strtol rules: optional space, optional
 *	sign, 0x/0 prefixes when base is 0 or 16.
 */
static int64_t bc_strtoll(const char *s, char **end, int base, int uns)
{
	uint64_t v = 0;
	int neg = 0;
	const char *p = s;
	const char *digits_start;

	while (*p == ' ' || *p == '\t')
		p++;
	if (*p == '+' || *p == '-')
		neg = (*p++ == '-');
	if ((base == 0 || base == 16) && p[0] == '0' &&
	    (p[1] == 'x' || p[1] == 'X')) {
		p += 2;
		base = 16;
	} else if (base == 0)
		base = (*p == '0') ? 8 : 10;
	digits_start = p;
	for (;;) {
		int c = *p;
		int d;
		if (c >= '0' && c <= '9')
			d = c - '0';
		else if (c >= 'a' && c <= 'z')
			d = c - 'a' + 10;
		else if (c >= 'A' && c <= 'Z')
			d = c - 'A' + 10;
		else
			break;
		if (d >= base)
			break;
		v = v * base + d;
		p++;
	}
	if (end)
		*end = (char *)(p == digits_start ? s : p);
	if (uns)
		return (int64_t)(neg ? (uint64_t)0 - v : v);
	return neg ? -(int64_t)v : (int64_t)v;
}

/*
 *	The mathematics library, table driven because it is twenty
 *	functions with two shapes. Doubles travel as their bit pattern
 *	(dget/dput) and a double argument takes two stack slots, so the
 *	second argument of the two-argument forms lives at slot 2.
 *
 *	These run at native speed, which is the point: a translated BASIC
 *	program spends its time in here and in the string runtime, not in
 *	the bytecode.
 */
/*
 *	Not const: on the board these are filled from the kernel's shared
 *	libm at startup (mfns_share).
 *
 *	And EMPTY there, which is the point.  Naming sin here would make
 *	the linker keep bcrun's own copy of it and of the range reducers
 *	behind it - __rem_pio2 and __rem_pio2_large are 2.9K between them
 *	- in every process, for functions the kernel already has and
 *	executes faster.  Left null, nothing references them and they are
 *	dropped.
 *
 *	Off the board there is no kernel to ask, so the local ones stay.
 */
#ifdef MM_PC3
#define MFN(f)	NULL
#else
#define MFN(f)	f
#endif

static struct mfn {
	const char *name;
	double (*f1)(double);
	double (*f2)(double, double);
} mfns[] = {
	{ "sin",   MFN(sin),   NULL  },
	{ "cos",   MFN(cos),   NULL  },
	{ "tan",   MFN(tan),   NULL  },
	{ "asin",  MFN(asin),  NULL  },
	{ "acos",  MFN(acos),  NULL  },
	{ "atan",  MFN(atan),  NULL  },
	{ "sinh",  MFN(sinh),  NULL  },
	{ "cosh",  MFN(cosh),  NULL  },
	{ "tanh",  MFN(tanh),  NULL  },
	{ "sqrt",  MFN(sqrt),  NULL  },
	{ "exp",   MFN(exp),   NULL  },
	{ "log",   MFN(log),   NULL  },
	{ "log10", MFN(log10), NULL  },
	{ "floor", MFN(floor), NULL  },
	{ "ceil",  MFN(ceil),  NULL  },
	{ "fabs",  MFN(fabs),  NULL  },
	{ "pow",   NULL,  MFN(pow) },
	{ "atan2", NULL,  MFN(atan2) },
	{ "fmod",  NULL,  MFN(fmod) },
	{ NULL,    NULL,  NULL  }
};

#if defined(MM_PC3) && !defined(MM_HOSTED_ONLY)
/*
 *	Point the maths table at the kernel's shared copy.
 *
 *	The kernel exports one libm from flash (libm_table.c) so that
 *	every program could call it instead of linking 13K of its own.
 *	Whether that is a good idea is a question about SPEED, because
 *	the shared copy runs from XIP flash: a tight sin/cos loop
 *	measured 2.7x slower than a program's own RAM copy.  A real
 *	program does other work between maths calls, so the honest
 *	number comes from a real program.
 *
 *	Hence the switch rather than a decision: BCRUN_SHAREDM=1 makes
 *	the SAME binary dispatch to the kernel's copy, so the two can be
 *	compared without changing anything else.  Off, nothing moves.
 */
#define PICOIOC_LIBM	0x0020
#define PC3_LIBM_MAGIC	0x50433350UL
#define PC3_LIBM_VERSION 1

struct pc3_libm_v {
	unsigned long magic;
	unsigned short version, count;
	void *fn[19];
};

static void mfns_share(void)
{
	const struct pc3_libm_v *t;
	void *p = 0;
	unsigned i;
	int fd;

	fd = sys_open();
	if (fd < 0 || ioctl(fd, PICOIOC_LIBM, &p) < 0)
		p = NULL;
	t = (const struct pc3_libm_v *)p;
	/*
	 * Fatal, not a fallback.  mfns[] is empty in this build, so there
	 * is nothing to fall back TO - and even if there were, quietly
	 * using it would hide the 21% the shared copy is worth (eclipse
	 * 2.82s -> 2.23s) behind a program that merely still works.
	 */
	if (t == NULL || t->magic != PC3_LIBM_MAGIC ||
	    t->version != PC3_LIBM_VERSION || t->count != 19) {
		fprintf(stderr, "bcrun: this kernel has no shared libm "
				"(needs PICOIOC_LIBM v%d)\n", PC3_LIBM_VERSION);
		exit(1);
	}
	/* mfns[] is in the table's order by construction - the sixteen
	   one-argument functions, then the three two-argument ones. */
	for (i = 0; i < 16; i++)
		mfns[i].f1 = (double (*)(double))t->fn[i];
	for (i = 16; i < 19; i++)
		mfns[i].f2 = (double (*)(double, double))t->fn[i];
}
#else
#define mfns_share()	do { } while (0)
#endif

static int lib_math_find(const char *name)
{
	unsigned i;

	for (i = 0; mfns[i].name; i++)
		if (!strcmp(mfns[i].name, name))
			return (int)i;
	return -1;
}

static void math_exec(unsigned i)
{
	A = dput(mfns[i].f1 ? mfns[i].f1(argd(0))
			    : mfns[i].f2(argd(0), argd(2)));
}

/*
 *	The string and memory family, factored out of the dispatch chain
 *	so they can bind: Dhrystone spends its life in strcpy/strcmp and
 *	paid ~30 failed strcmps of dispatch per call.  strcmp/strncmp
 *	compare the program's bytes directly instead of copying both
 *	strings out through getstr - same result, without the two copies
 *	and the 511-byte truncation.
 */
static void lib_strlen(void)
{
	A = (long)ns_strlen((const char *)vptr((unsigned long)arg(0)));
}

static void lib_strcpy(void)
{
	unsigned long d = arg(0);
	ns_strcpy((char *)vptr(d), (const char *)vptr(arg(1)));
	A = d;
}

static void lib_strncpy(void)
{
	unsigned long d = arg(0), s = arg(1), n = arg(2), l = vstrlen(s);
	if (l > n)
		l = n;
	vcopy(d, s, l);
	while (l < n)
		wr8(d + l++, 0);
	A = d;
}

static void lib_strcat(void)
{
	unsigned long d = arg(0), s = arg(1);
	vcopy(d + vstrlen(d), s, vstrlen(s) + 1);
	A = d;
}

static void lib_strcmp(void)
{
	A = ns_strcmp((const char *)vptr(arg(0)),
		      (const char *)vptr(arg(1)));
}

static void lib_strncmp(void)
{
	unsigned long a = (unsigned long)arg(0), b = (unsigned long)arg(1);
	unsigned long n = (unsigned long)arg(2);
	unsigned ca, cb;

	A = 0;
	while (n--) {
		ca = rd8(a++);
		cb = rd8(b++);
		if (ca != cb) {
			A = (int)ca - (int)cb;
			return;
		}
		if (!ca)
			return;
	}
}

static void lib_strchr(void)
{
	unsigned long s = arg(0);
	int c = (int)arg(1);
	unsigned long i = 0, l = vstrlen(s);
	const unsigned char *p = vptr(s);

	A = 0;
	for (; i <= l; i++)
		if (p[i] == c) {
			A = (long)(s + i);
			break;
		}
}

static void lib_strrchr(void)
{
	unsigned long s = arg(0);
	int c = (int)arg(1);
	long i = (long)vstrlen(s);
	const unsigned char *p = vptr(s);

	A = 0;
	for (; i >= 0; i--)
		if (p[i] == c) {
			A = (long)(s + i);
			break;
		}
}

static void lib_memset(void)
{
	unsigned long d = arg(0), n = arg(2);
	if (VM_OOBN(d, n))
		fault("bad address");
	memset(vptr(d), (int)arg(1), n);
	A = d;
}

static void lib_memcpy(void)
{
	vcopy(arg(0), arg(1), arg(2));
	A = arg(0);
}

static void lib_memcmp(void)
{
	unsigned long a = arg(0), b = arg(1), n = arg(2);
	if (VM_OOBN(a, n) || VM_OOBN(b, n))
		fault("bad address");
	A = memcmp(vptr(a), vptr(b), n);
}

static void lib_putchar(void)
{
	putchar((int)arg(0));
	fflush(stdout);
	A = arg(0);
}

static void lib_puts(void)
{
	puts(getstr((unsigned long)arg(0)));
	A = 0;
}

/*
 *	The rest of the runtime, converted from libcall's old strcmp
 *	chain into bindable functions.  The chain never memoised - only
 *	lib_fast/math/eqop did - so every malloc or rand paid ~30 failed
 *	strcmps per CALL, and the names had to stay resident to pay them.
 *	As table entries they bind once (at load, see lib_bind_all), the
 *	symbol and string tables can be freed, and an unknown name is
 *	refused before the program runs instead of mid-run.
 */
static void lc_rewind(void)
{
	int fd = fh(arg(0));
	if (fd >= 0) {
		lseek(fd, 0, SEEK_SET);
		file_eof[fd] = 0;
	}
	A = 0;
}

static void lc_fflush(void)
{
	fflush(stdout);
	A = 0;
}

static void lc_remove(void)
{
	A = unlink(getstr((unsigned long)arg(0)));
}

static void lc_rename(void)
{
	A = rename(getstr((unsigned long)arg(0)),
		   getstr((unsigned long)arg(1)));
}

static void lc_exit(void)
{
	exit((int)arg(0));
}

static void lc_malloc(void)
{
	A = lib_malloc((unsigned long)arg(0));
}

static void lc_calloc(void)
{
	unsigned long n = (unsigned long)arg(0) * (unsigned long)arg(1);
	A = lib_malloc(n);
	if (A)
		memset(vptr(A), 0, n);
}

static void lc_free(void)
{
	lib_free((unsigned long)arg(0));
	A = 0;
}

static void lc_realloc(void)
{
	unsigned long old = (unsigned long)arg(0);
	unsigned long n = (unsigned long)arg(1);
	long np = lib_malloc(n);
	if (np && old) {
		unsigned long osz = rd32(old - HDR) - HDR;
		vcopy((unsigned long)np, old, osz < n ? osz : n);
		lib_free(old);
	}
	A = np;
}

static void lc_atoi(void)
{
	A = atoi(getstr((unsigned long)arg(0)));
}

static void lc_atol(void)
{
	A = atol(getstr((unsigned long)arg(0)));
}

static void lc_atof(void)
{
	A = dput(atof(getstr((unsigned long)arg(0))));
}

static void lc_strtod(void)
{
	/* The end pointer the caller sees must be an address in the
	   program's space, so it is rebuilt from the host one as an
	   offset from the start of the string. */
	unsigned long s = (unsigned long)arg(0);
	unsigned long ep = (unsigned long)arg(1);
	char *str = getstr(s), *end;
	double d = strtod(str, &end);
	if (ep)
		wr32(ep, s + (unsigned long)(end - str));
	A = dput(d);
}

/* Fuzix libc has no 64-bit strtoll, so parse here - one
   implementation for every host. */
static void lc_strtol_any(int uns)
{
	unsigned long s = (unsigned long)arg(0);
	unsigned long ep = (unsigned long)arg(1);
	int base = (int)arg(2);
	char *str = getstr(s), *end;
	A = bc_strtoll(str, &end, base, uns);
	if (ep)
		wr32(ep, s + (unsigned long)(end - str));
}

static void lc_strtol(void)  { lc_strtol_any(0); }
static void lc_strtoul(void) { lc_strtol_any(1); }

static void lc_abs(void)
{
	long v = arg(0);
	A = v < 0 ? -v : v;
}

static void lc_llabs(void)
{
	long long v = argll(0);
	A = v < 0 ? -v : v;
}

static void lc_rand(void)
{
	A = rand() & 0x7FFFFFFF;
}

static void lc_srand(void)
{
	srand((unsigned)arg(0));
	A = 0;
}

static void lc_time(void)
{
	long t = (long)time(NULL);
	if (arg(0))
		wr32((unsigned long)arg(0), (unsigned long)t);
	A = t;
}

static void lc_open(void)
{
	A = open(getstr((unsigned long)arg(0)), (int)arg(1), 0666);
}

static void lc_creat(void)
{
	A = creat(getstr((unsigned long)arg(0)), 0666);
}

static void lc_close(void)
{
	A = close((int)arg(0));
}

static void lc_read(void)
{
	unsigned long b = arg(1), n = arg(2);
	if (VM_OOBN(b, n)) fault("bad address");
	A = read((int)arg(0), vptr(b), n);
}

static void lc_write(void)
{
	unsigned long b = arg(1), n = arg(2);
	if (VM_OOBN(b, n)) fault("bad address");
	A = write((int)arg(0), vptr(b), n);
}

static void lc_lseek(void)
{
	A = lseek((int)arg(0), arg(1), (int)arg(2));
}

static void lc_unlink(void)
{
	A = unlink(getstr((unsigned long)arg(0)));
}

static void lc_adval(void)
{
	A = lib_adval((int)arg(0));
}

static void lc_time_us(void)
{
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
}

static void lc_time_us64(void)
{
	A = lib_us64();
}

/*
 *	First-call binding.  libbind (function), mathbind (mfns index+1)
 *	and eqbind (parsed descriptor) memoise per symbol index, so the
 *	name is examined once per program run, not once per call - the
 *	dispatch chain below only ever runs for a symbol's first call.
 */
static const struct {
	const char *name;
	void (*fn)(void);
} lib_fast[] = {
	{ "strlen", lib_strlen }, { "strcpy", lib_strcpy },
	{ "strncpy", lib_strncpy }, { "strcat", lib_strcat },
	{ "strcmp", lib_strcmp }, { "strncmp", lib_strncmp },
	{ "strchr", lib_strchr }, { "strrchr", lib_strrchr },
	{ "memset", lib_memset }, { "memcpy", lib_memcpy },
	{ "memmove", lib_memcpy }, { "memcmp", lib_memcmp },
	{ "printf", lib_printf }, { "sprintf", lib_sprintf },
	{ "fprintf", lib_fprintf }, { "fopen", lib_fopen },
	{ "fclose", lib_fclose }, { "fread", lib_fread },
	{ "fwrite", lib_fwrite }, { "fgetc", lib_fgetc },
	{ "getc", lib_fgetc }, { "fputc", lib_fputc },
	{ "putc", lib_fputc }, { "fgets", lib_fgets },
	{ "fputs", lib_fputs }, { "feof", lib_feof },
	{ "fseek", lib_fseek }, { "ftell", lib_ftell },
	{ "putchar", lib_putchar }, { "puts", lib_puts },
	{ "rewind", lc_rewind }, { "fflush", lc_fflush },
	{ "remove", lc_remove }, { "rename", lc_rename },
	{ "exit", lc_exit },
	{ "malloc", lc_malloc }, { "calloc", lc_calloc },
	{ "free", lc_free }, { "realloc", lc_realloc },
	{ "atoi", lc_atoi }, { "atol", lc_atol }, { "atof", lc_atof },
	{ "strtod", lc_strtod },
	{ "strtol", lc_strtol }, { "strtoll", lc_strtol },
	{ "strtoul", lc_strtoul }, { "strtoull", lc_strtoul },
	{ "abs", lc_abs }, { "labs", lc_abs }, { "llabs", lc_llabs },
	{ "rand", lc_rand }, { "srand", lc_srand }, { "time", lc_time },
	{ "open", lc_open }, { "creat", lc_creat }, { "close", lc_close },
	{ "read", lc_read }, { "write", lc_write }, { "lseek", lc_lseek },
	{ "unlink", lc_unlink },
	{ "adval", lc_adval }, { "time_us", lc_time_us },
	{ "time_us64", lc_time_us64 },
	{ NULL, NULL }
};

static unsigned char *mathbind;		/* symbol -> mfns index + 1 */
static unsigned short *eqbind;		/* symbol -> eqop descriptor */

/*
 *	Bind one library symbol into libbind/mathbind/eqbind by name.
 *	Returns 0 for a name the runtime does not provide.  Called for
 *	every symbol at load time (lib_bind_all) - after which the
 *	symbol and string tables are freed - or lazily on first call
 *	under BCRUN_LAZYBIND=1.
 */
static int lib_resolve(unsigned idx)
{
	const char *name = strtab + sym[idx].s_name;
	unsigned i;
	int mi;
	unsigned d;

	if (name[0] == 'm' && name[1] == 'm' && name[2] == '_') {
		void (*fn)(void) = mm_wrap_lookup(name);
		if (fn) {
			libbind[idx] = fn;
			return 1;
		}
	}
	for (i = 0; lib_fast[i].name; i++) {
		if (!strcmp(lib_fast[i].name, name)) {
			libbind[idx] = lib_fast[i].fn;
			return 1;
		}
	}
	mi = lib_math_find(name);
	if (mi >= 0) {
		mathbind[idx] = mi + 1;
		return 1;
	}
	d = parse_eqop(name);
	if (d) {
		eqbind[idx] = d;
		return 1;
	}
	return 0;
}

static void libcall(unsigned idx)
{
	if (idx >= h.h_nsym)
		fault("bad library index");
	for (;;) {
		if (libbind[idx]) {
			libbind[idx]();
			return;
		}
		if (mathbind[idx]) {
			math_exec(mathbind[idx] - 1);
			return;
		}
		if (eqbind[idx]) {
			exec_eqop(eqbind[idx]);
			return;
		}
		/* only reachable under BCRUN_LAZYBIND: eager binding
		   resolved or refused every symbol at load */
		if (strtab == NULL || !lib_resolve(idx)) {
			fprintf(stderr,
				"bcrun: no runtime function \"%s\"\n",
				strtab ? strtab + sym[idx].s_name : "?");
			exit(1);
		}
	}

}

/* ---- loader -------------------------------------------------------- */

static unsigned long database, bssbase;
/*
 *	The address the stack must not grow below.  Without it, a runaway
 *	recursion walks sp down through the pools, bss and data, quietly
 *	rewriting the program's own globals - every accessor checks the
 *	VM address space but nothing checks the stack - and only faults
 *	when it finally reaches address zero, long after the damage.
 *	Set once the segments are placed; 0 until then, which disables
 *	the test while the loader is still working.
 */
static unsigned long stack_floor;

/*
 *	Hand the floor to native code.  Defined beside native_helpers[],
 *	which is a long way below this - the load path settles the floor
 *	before the table is in scope, so it goes through here rather than
 *	reaching into the array from up here.  A no-op where there is no
 *	native code to guard.
 */
#if defined(__arm__) || defined(__thumb__)
static void native_set_floor(unsigned long f);
#else
#define native_set_floor(f)	do { } while (0)
#endif

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

static long file_base;		/* header offset: 0, or past a #! line */

static void load(const char *path)
{
	FILE *f = fopen(path, "rb");
	unsigned long i;

	if (f == NULL) {
		perror(path);
		exit(1);
	}
	/* cc marks its output "#!/usr/bin/bcrun" so the kernel can exec
	   it directly; skip any #! line before the header. */
	if (fread(&h, sizeof(h), 1, f) == 1 &&
	    h.h_magic[0] == '#' && h.h_magic[1] == '!') {
		int c;
		rewind(f);
		while ((c = getc(f)) != EOF && c != '\n' && file_base < 127)
			file_base++;
		if (c != '\n') {
			fprintf(stderr, "%s: not a bytecode object\n", path);
			exit(1);
		}
		file_base++;
		if (fseek(f, file_base, SEEK_SET))
			fault("seek");
		if (fread(&h, sizeof(h), 1, f) != 1)
			fault("short header");
	}
	if (memcmp(h.h_magic, BC_MAGIC, 4) != 0) {
		fprintf(stderr, "%s: not a bytecode object\n", path);
		exit(1);
	}
	/* Version 2 objects differ only in containing native code; the
	   bump exists so interpreters that predate mixed mode reject
	   them cleanly rather than faulting on the marker mid-run.
	   Version 3 native code additionally calls through helper-vector
	   slots 4+ (direct DCP arithmetic) - same rule, older bcrun
	   must refuse it. */
	if (h.h_version != BC_VERSION && h.h_version != BC_VERSION_NATIVE
	    && h.h_version != BC_VERSION_NATIVE3
	    && h.h_version != BC_VERSION_NATIVE4
	    && h.h_version != BC_VERSION_NATIVE5) {
		fprintf(stderr, "%s: version %u, expected %u\n", path,
			h.h_version, BC_VERSION);
		exit(1);
	}
	code = malloc(h.h_code ? h.h_code : 1);
	sym = malloc((h.h_nsym ? h.h_nsym : 1) * sizeof(struct bc_sym));
	if (code == NULL || sym == NULL) {
		/* Unchecked, this surfaced as "short code at pc 0", which
		   reads like a truncated file rather than what it is. */
		fprintf(stderr, "%s: out of memory (%lu bytes of code)\n",
			path, (unsigned long)h.h_code);
		exit(1);
	}

	if (fread(code, 1, h.h_code, f) != h.h_code)
		fault("short code");

	/* BCRUN_MAP=1: where the code landed, so a faulting PC from a
	   debugger or qemu -d cpu can be turned back into an offset in
	   the object (and from there a native span to disassemble). */
	if (getenv("BCRUN_MAP"))
		fprintf(stderr, "bcrun: code %p + %lu, entry %lu\n",
			(void *)code, (unsigned long)h.h_code,
			(unsigned long)h.h_entry);

#ifdef __linux__
	/* Linux (and so qemu-arm, the development-side executor for
	   native code) enforces NX on malloc'd memory; the board's flat
	   memory does not.  Harmless for pure-bytecode objects. */
	{
		uintptr_t p0 = (uintptr_t)code & ~4095UL;
		uintptr_t p1 = ((uintptr_t)code + (h.h_code ? h.h_code : 1)
				+ 4095) & ~4095UL;
		mprotect((void *)p0, p1 - p0,
			 PROT_READ | PROT_WRITE | PROT_EXEC);
	}
#endif

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
	/* Absolute from here down: every program address the fixups
	   produce comes through symval(), which adds one of these two, so
	   rebasing them relocates the whole program in one place. */
#if defined(MM_PC3) && UINTPTR_MAX <= 0xFFFFFFFFu
	/*
	 * Now that the header is read, ask for exactly what this program
	 * needs.  The +8 covers the alignment bssbase applies below; the
	 * slack is the heap that is only used if PSRAM does not answer.
	 *
	 * MM_PC3 ONLY, and that is the whole basis of it: there the heap
	 * is moved out to PSRAM by heap_init, so mem[] holds nothing but
	 * the guard, data, bss and the stack.  Everywhere else - the
	 * qemu harness, the development host - the heap is still IN
	 * mem[], and sizing it to data+bss+stack leaves a program with
	 * four kilobytes of heap.  Which is exactly what happened: 16 of
	 * 17 qemu tests failed the moment this was not conditional.
	 */
	{
		unsigned long need = NULLGUARD + h.h_data + 8 + h.h_bss
				     + mmrt_bytes() + STACKROOM + MEM_SLACK;
		const char *e = getenv("BCRUN_MEM");

		if (e)
			need = (unsigned long)atoi(e) << 10;
		if (need > MEMSIZE)
			need = MEMSIZE;
		/* Round to 8: run() puts the initial stack at MEMTOP - 4,
		   so an unrounded size misaligns EVERY frame in the
		   program.  The M33 shrugs at unaligned words, so it all
		   quietly works until native runtime code does an STRD
		   through a by-ref frame local - mm_fontinfo, first
		   reached by TEXT - and that is an UNALIGNED UsageFault
		   escalated to a HardFault, which is a panic here.  The
		   static mem[] build never sees this: 48K is 0 mod 8. */
		need = (need + 7) & ~7UL;
		mem = malloc(need + 8);
		if (mem == NULL) {
			fprintf(stderr, "bcrun: no room for a %lu byte "
					"program space\n", need);
			exit(1);
		}
		/* 8-aligned by hand: cc2 aligns objects as offsets within
		   the segments, which only means anything if the base is
		   aligned too - an odd base gave a SIGBUS on ARM once. */
		mem = (unsigned char *)(((uintptr_t)mem + 7) & ~(uintptr_t)7);
		memsize = need;
	}
#endif
	database = MEMBASE + NULLGUARD;
	/* Rounded up: cc2 aligns objects as offsets WITHIN bss, which
	   only means anything if the segment base is itself aligned.
	   Found by the qemu harness as a SIGBUS: an odd h_data put the
	   whole bss segment - and its "8-aligned" int64 arrays - at an
	   odd address, which x86 shrugged at and ARM does not. */
	bssbase = (database + h.h_data + 7) & ~7UL;
	if (bssbase + h.h_bss + STACKROOM > MEMTOP) {
		fprintf(stderr, "bcrun: program too large\n");
		exit(1);
	}
	if (h.h_data && fread(vptr(database), 1, h.h_data, f) != h.h_data)
		fault("short data");
	memset(vptr(bssbase), 0, h.h_bss);

	/* Symbols first - fixups are streamed in a second pass below,
	   one record at a time, so the fixup table never needs memory
	   of its own: 8 bytes x thousands of fixups was a real slice
	   of a 256K process once whole programs went native. */
	if (fseek(f, (long)(h.h_nfixup * sizeof(struct bc_fixup)), SEEK_CUR))
		fault("seek");
	for (i = 0; i < h.h_nsym; i++)
		if (fread(&sym[i], sizeof(struct bc_sym), 1, f) != 1)
			fault("short symbols");
	strtab = malloc(h.h_strsize ? h.h_strsize : 1);
	if (strtab == NULL) {
		fprintf(stderr, "%s: out of memory (string table)\n", path);
		exit(1);
	}
	if (h.h_strsize && fread(strtab, 1, h.h_strsize, f) != h.h_strsize)
		fault("short string table");

	libbind = calloc(h.h_nsym ? h.h_nsym : 1, sizeof(*libbind));
	mathbind = calloc(h.h_nsym ? h.h_nsym : 1, sizeof(*mathbind));
	eqbind = calloc(h.h_nsym ? h.h_nsym : 1, sizeof(*eqbind));
	if (libbind == NULL || mathbind == NULL || eqbind == NULL) {
		fprintf(stderr, "%s: out of memory (bind table)\n", path);
		exit(1);
	}

	/* The heap is whatever is left between bss, the mm runtime's
	   pools if this program uses it, and the stack.  Needs the
	   symbol and string tables, hence down here. */
	stack_floor = mmrt_reserve(bssbase + h.h_bss);
	heap_init(stack_floor);
	/* On the host and in qemu the heap is what lies above that, and
	   the stack must stop before IT, not before bss.  On the board
	   heap_init moves the heap out to PSRAM and leaves this alone. */
	if (heap_base >= MEMBASE && heap_top <= MEMTOP && heap_top > heap_base)
		stack_floor = heap_top;
	/* Native prologues read the floor from the helper vector.  A
	   program address is a machine address, so the interpreter's
	   floor serves both without conversion.  Zero leaves the check
	   inert, exactly as it does in BC_ENTER. */
	native_set_floor(stack_floor);

	/* Apply fixups, streamed straight off the file: add the symbol's
	   value to the 32bit field. */
	if (fseek(f, file_base + (long)(sizeof(h) + h.h_code + h.h_data),
		  SEEK_SET))
		fault("seek");
	for (i = 0; i < h.h_nfixup; i++) {
		struct bc_fixup fx;
		unsigned long v, o;

		if (fread(&fx, sizeof(fx), 1, f) != 1)
			fault("short fixups");
		v = symval(fx.f_sym);
		o = fx.f_offset;

		/*
		 * Flag 2 marks a movw/movt pair in native code: the pair
		 * loads a 32-bit value whose bits are scattered across the
		 * two Thumb-2 instructions.  Decode the assembled value
		 * (the addend), add the symbol - or, for a library symbol,
		 * which has no address, store the tagged index helper_call
		 * turns back into a libcall - and re-encode.
		 */
		if (fx.f_pad & 2) {
			unsigned hw[4];
			unsigned j;
			for (j = 0; j < 4; j++)
				hw[j] = code[o + 2 * j] |
				    ((unsigned)code[o + 2 * j + 1] << 8);
			v += ((unsigned long)(hw[0] & 0xF) << 12) |
			    ((unsigned long)((hw[0] >> 10) & 1) << 11) |
			    (((hw[1] >> 12) & 7) << 8) | (hw[1] & 0xFF) |
			    ((unsigned long)(hw[2] & 0xF) << 28) |
			    ((unsigned long)((hw[2] >> 10) & 1) << 27) |
			    ((unsigned long)((hw[3] >> 12) & 7) << 24) |
			    ((unsigned long)(hw[3] & 0xFF) << 16);
			if (sym[fx.f_sym].s_type == BC_SYM_LIB)
				v = 0x80000000UL | fx.f_sym;
			hw[0] = (hw[0] & 0xFBF0) | ((v >> 12) & 0xF) |
			    (((v >> 11) & 1) << 10);
			hw[1] = (hw[1] & 0x8F00) | (((v >> 8) & 7) << 12) |
			    (v & 0xFF);
			hw[2] = (hw[2] & 0xFBF0) | ((v >> 28) & 0xF) |
			    (((v >> 27) & 1) << 10);
			hw[3] = (hw[3] & 0x8F00) | (((v >> 24) & 7) << 12) |
			    ((v >> 16) & 0xFF);
			for (j = 0; j < 4; j++) {
				code[o + 2 * j] = hw[j];
				code[o + 2 * j + 1] = hw[j] >> 8;
			}
			continue;
		}

		/*
		 * Flag 1 marks a literal-pool word in native code: a plain
		 * value slot that must never be mistaken for a BC_CALL
		 * operand by the rewrite below, whatever byte happens to
		 * precede it.  A library symbol has no address at all, so
		 * the slot gets a tagged index that helper_call turns back
		 * into a libcall at the call site.  (Superseded by the
		 * movw/movt pair fixup above, whose reach is unlimited;
		 * still honoured so older mixed objects keep loading.)
		 */
		if (fx.f_pad & 1) {
			if (sym[fx.f_sym].s_type == BC_SYM_LIB)
				v = 0x80000000UL | fx.f_sym;
			else
				v += code[o] |
				    ((unsigned long)code[o + 1] << 8) |
				    ((unsigned long)code[o + 2] << 16) |
				    ((unsigned long)code[o + 3] << 24);
			code[o] = v;
			code[o + 1] = v >> 8;
			code[o + 2] = v >> 16;
			code[o + 3] = v >> 24;
			continue;
		}

		/*
		 * A call to a name this module never defines is a library
		 * call: the compiler cannot tell them apart when it emits
		 * the call, because the definition may come later. Rewrite
		 * it here. BC_CALL's operand is four bytes and BC_LIBCALL's
		 * is two, so the two spare bytes become NOPs and the
		 * instruction keeps its length.
		 */
		if (fx.f_seg == BC_SEG_CODE && o > 0 &&
		    code[o - 1] == BC_CALL && sym[fx.f_sym].s_type == BC_SYM_LIB) {
			code[o - 1] = BC_LIBCALL;
			code[o] = fx.f_sym;
			code[o + 1] = fx.f_sym >> 8;
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
		if (sym[fx.f_sym].s_type == BC_SYM_LIB) {
			fprintf(stderr,
				"bcrun: cannot take the address of library "
				"function \"%s\"\n",
				strtab + sym[fx.f_sym].s_name);
			exit(1);
		}

		if (fx.f_seg == BC_SEG_CODE) {
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
	fclose(f);

	/*
	 *	Bind every library symbol now, then free the names: after
	 *	this point a library call is a table lookup by index and
	 *	the symbol and string tables - kept for the whole run until
	 *	now, ~12K on a big program - go back to the heap.  A name
	 *	the runtime does not provide is refused HERE, with the
	 *	program named, instead of exit(1) in the middle of a run
	 *	that happened to reach it.  BCRUN_LAZYBIND=1 restores
	 *	first-call binding (and keeps the tables) for A/B.
	 */
	if (!getenv("BCRUN_LAZYBIND")) {
		for (i = 0; i < h.h_nsym; i++) {
			if (sym[i].s_type != BC_SYM_LIB)
				continue;
			if (!lib_resolve((unsigned)i)) {
				fprintf(stderr,
					"%s: no runtime function \"%s\"\n",
					path, strtab + sym[i].s_name);
				exit(1);
			}
		}
		free(strtab);
		strtab = NULL;
		free(sym);
		sym = NULL;
	}
}

/* ---- native code ---------------------------------------------------- */

/* Declared here rather than beside prof_dump: native_enter below is
   the only thing that counts a native entry, and it comes first. */
static uint32_t prof_call, prof_libcall, prof_eqop, prof_enter;

/*
 *	Mixed mode: a function whose first code byte is BC_NATIVE is
 *	Thumb machine code, entered here.  Register file per
 *	PLAN-arm-backend.md: r4 = VM stack pointer as a native pointer,
 *	r5 = helper vector, r6 = mem[] base; the result comes back in
 *	r0/r1, exactly the accumulator's convention.  r4-r6 are dead on
 *	return - they are reloaded on every entry, never trusted after.
 *
 *	The helper vector is how native code reaches back into C: a
 *	call site loads the pointer from [r5, #index*4], passes its vsp
 *	in r1 so the C side can sync the global sp, and BLXes.  C
 *	helpers preserve r4-r6 by the AAPCS, so the native caller's
 *	register file survives.
 */
static int64_t helper_call(unsigned long target, unsigned char *vsp);
static int64_t helper_libcall(unsigned long idx, unsigned char *vsp);
static int64_t helper_op(unsigned long op, unsigned char *vsp, int64_t a);

#if defined(__arm__) || defined(__thumb__)
/*
 *	Called from a native prologue that has just taken its frame and
 *	found r4 below the floor.  It does not return - the program is
 *	out of stack and there is nothing to carry on with.
 */
static void helper_stackfault(void)
{
	fault("stack overflow - recursion too deep?");
}
#endif
static int64_t helper_eqop(unsigned long idx, unsigned char *vsp, int64_t a);

#define NH_CALL		0
#define NH_LIBCALL	1
#define NH_OP		2
#define NH_EQOP		3

/*
 *	Slots 4+ (version-3 objects): the double arithmetic, compares
 *	and int64 converts as DIRECT targets - on the board these are
 *	the DCP-backed aeabi routines, and translated code BLs them
 *	with a two-instruction marshal instead of a helper_op round
 *	trip, which costs more than the arithmetic itself in a tight
 *	loop.  Order is baked into backend-thumb.c (NHS_*): keep the
 *	two lists in step or new objects jump through the wrong slot.
 */
#if defined(__arm__) || defined(__thumb__)
extern double __aeabi_dadd(double, double);
extern double __aeabi_dsub(double, double);
extern double __aeabi_dmul(double, double);
extern double __aeabi_ddiv(double, double);
extern int __aeabi_dcmpeq(double, double);
extern int __aeabi_dcmplt(double, double);
extern int __aeabi_dcmple(double, double);
extern int __aeabi_dcmpge(double, double);
extern int __aeabi_dcmpgt(double, double);
extern double __aeabi_l2d(long long);
extern double __aeabi_ul2d(unsigned long long);
extern long long __aeabi_d2lz(double);
extern unsigned long long __aeabi_d2ulz(double);
#endif

static void *native_helpers[] = {
	(void *)helper_call,
	(void *)helper_libcall,
	(void *)helper_op,
	(void *)helper_eqop,
#if defined(__arm__) || defined(__thumb__)
	(void *)__aeabi_dadd,	/* 4  */
	(void *)__aeabi_dsub,	/* 5  */
	(void *)__aeabi_dmul,	/* 6  */
	(void *)__aeabi_ddiv,	/* 7  */
	(void *)__aeabi_dcmpeq,	/* 8  */
	(void *)__aeabi_dcmplt,	/* 9  */
	(void *)__aeabi_dcmple,	/* 10 */
	(void *)__aeabi_dcmpge,	/* 11 */
	(void *)__aeabi_dcmpgt,	/* 12 */
	(void *)__aeabi_l2d,	/* 13 */
	(void *)__aeabi_ul2d,	/* 14 */
	(void *)__aeabi_d2lz,	/* 15 */
	(void *)__aeabi_d2ulz,	/* 16 */
	/*
	 *	Version 4: the string family as direct slots.  A translated
	 *	strcpy/strcmp/strlen/memcpy loads its arguments straight off
	 *	the VM stack and BLs here - no helper_call, no name dispatch,
	 *	no byte-assembled arg() reads.  Arguments are machine
	 *	addresses (absolute addressing), so these are the same
	 *	functions the interpreter's lib_* wrappers use.  Append only;
	 *	an object that uses a slot carries BC_VERSION_NATIVE4, so a
	 *	bcrun without this table refuses it at load rather than
	 *	jumping through slot 17 of a 17-entry table.
	 */
	(void *)ns_strcpy,	/* 17 */
	(void *)ns_strcmp,	/* 18 */
	(void *)ns_strlen,	/* 19 */
	(void *)ns_memcpy,	/* 20 */
	/*
	 *	Version 5: the stack guard.  Slot 21 is a VALUE, not a
	 *	function - the lowest machine address the VM stack may
	 *	reach - and is filled in at load time once the heap has
	 *	been placed.  Slot 22 is what a native prologue calls when
	 *	it finds it has gone below it.  Zero here and patched
	 *	below; a zero floor disables the check, exactly as it does
	 *	in the interpreter's BC_ENTER.
	 */
	(void *)0,		/* 21 - stack floor, patched at load */
	(void *)helper_stackfault,	/* 22 */
#endif
};

#define NHS_STACKFLOOR	21

#if defined(__arm__) || defined(__thumb__)
static void native_set_floor(unsigned long f)
{
	native_helpers[NHS_STACKFLOOR] = (void *)(uintptr_t)f;
}
#endif

static int64_t bc_exec(unsigned long entry);
static void libcall(unsigned idx);

#if defined(__arm__) || defined(__thumb__)
static int64_t native_enter(unsigned long off)
{
	unsigned long saved_sp = sp;
	prof_enter++;
	register uint32_t r0v asm("r0");
	register uint32_t r1v asm("r1");
	register uint32_t fn asm("r3") =
	    (uint32_t)(uintptr_t)(code + BC_NATIVE_ENTRY(off)) | 1;
	register unsigned char *vsp asm("r4") = vptr(sp);
	register void **hv asm("r5") = native_helpers;
	/*
	 *	Zero, and that is the whole of the native backend's share of
	 *	the absolute-address change.  Generated code reaches memory
	 *	as "ldr r3, [r6, r2]" and converts with "subs r2, r4, r6" /
	 *	"adds r0, r6, r0"; with r6 = 0 those become plain absolute
	 *	addressing and identities respectively, so every object
	 *	already compiled stays correct.  r6 is now a spare register
	 *	the backend could reclaim.
	 */
	register unsigned char *mb asm("r6") = NULL;

	/*
	 *	The explicit "+m" operands are not decoration.  The
	 *	native callee reaches every piece of VM state through the
	 *	helper vector, and gcc's IPA analysis at -Os looked
	 *	straight through the bare "memory" clobber, decided this
	 *	function never touches the global sp, and deleted the
	 *	callers' save/restore of it around the call - which cost
	 *	an afternoon on the board.  Declaring the state as
	 *	operands makes the dependency visible to every pass.
	 */
	asm volatile("blx %[fn]"
		: "=r" (r0v), "=r" (r1v), [fn] "+r" (fn),
		  "+r" (vsp), "+r" (hv), "+r" (mb),
		  "+m" (sp), "+m" (A), "+m" (pc), "+m" (mem)
		:
		: "r2", "r7", "r12", "lr", "cc", "memory");
	/* Contract: native_enter preserves the interpreter's sp.  The
	   callee's own helper calls leave it wherever they please; the
	   caller's truth is its r4. */
	sp = saved_sp;
	return (int64_t)(((uint64_t)r1v << 32) | r0v);
}
#else
static int64_t native_enter(unsigned long off)
{
	(void)off;
	fault("object contains native code this host cannot execute");
	return 0;
}
#endif

/*
 *	Every transfer to a function goes through here, so nothing else
 *	ever needs to know what the callee compiled to.  The frame is
 *	identical either way: the caller has pushed the args, this slot
 *	is the return-pc a bytecode callee pops with BC_RET - a native
 *	callee ignores it and the slot is popped here instead - and the
 *	offsets cc2 bakes into BC_LOCAL agree in both worlds.
 */
static int force_bytecode;	/* BCRUN_BYTECODE=1: ignore native code */

/*
 *	BCRUN_PROF=1: count every crossing from native code back into
 *	the runtime - the whole remaining cost surface of translated
 *	code - and dump the histogram at exit.  Counted at the helper
 *	entries, so interpreter execution is invisible: this measures
 *	what NATIVE code still pays for.
 */
/*
 *	BCRUN_SITES=1 is the other half: a counter per code offset,
 *	incremented in the interpreter's dispatch.  A pure-bytecode
 *	build (BCODE_ONLY) then reports how often each op SITE really
 *	runs, which is what turns a static count of peephole
 *	opportunities into a weighted one.  Interpreted, so the timings
 *	mean nothing - only the ratios do.
 */
static uint32_t *prof_site;
static unsigned long prof_site_n;

static int prof_on;
/* Allocated only under BCRUN_PROF (review item R3): 2K of bss in
   every process for arrays only the profiler reads was the last
   always-resident diagnostic. prof_site was already lazy. */
static uint32_t *prof_op;		/* helper_op by opcode      */
static uint32_t *prof_lib;		/* helper_libcall by index  */

static void prof_dump(void)
{
	unsigned i;

	if (prof_site) {
		unsigned long o;
		FILE *f = fopen(getenv("BCRUN_SITES"), "w");
		if (f) {
			for (o = 0; o < prof_site_n; o++)
				if (prof_site[o])
					fprintf(f, "%lu %lu\n", o,
						(unsigned long)prof_site[o]);
			fclose(f);
		}
	}
	if (!prof_on)
		return;
	fprintf(stderr, "-- bcrun profile --\n");
	fprintf(stderr, "native entries %lu, helper_call %lu, "
		"libcall %lu, eqop %lu\n",
		(unsigned long)prof_enter, (unsigned long)prof_call,
		(unsigned long)prof_libcall, (unsigned long)prof_eqop);
	for (i = 0; prof_op && i < 256; i++)
		if (prof_op[i])
			fprintf(stderr, "op %02x %lu\n", i,
				(unsigned long)prof_op[i]);
	for (i = 0; prof_lib && i < 256; i++)
		if (prof_lib[i])
			fprintf(stderr, "lib %u %lu\n", i,
				(unsigned long)prof_lib[i]);
}

/*
 *	A call FROM native code, any callee.  The stub at the call site
 *	synced the global sp from its r4 before coming here.  A native
 *	callee goes through native_enter exactly as the interpreter's
 *	dispatch does; a bytecode callee runs to completion under its own
 *	bc_exec activation, whose sentinel is also the frame-parity slot.
 *	A target with the top bit set is a library symbol index - the
 *	loader marks pool words that resolved to BC_SYM_LIB that way,
 *	since a library function has no address to call.
 */
static int64_t helper_call(unsigned long target, unsigned char *vsp)
{
	sp = (unsigned long)(uintptr_t)vsp;
	prof_call++;
	if (target & 0x80000000UL) {
		libcall((unsigned)(target & 0x7FFFFFFFUL));
		return A;
	}
	if (target < h.h_code && code[target] == BC_NATIVE) {
		unsigned long alias = code[target + 1] |
		    ((unsigned long)code[target + 2] << 8) |
		    ((unsigned long)code[target + 3] << 16) |
		    ((unsigned long)code[target + 4] << 24);
		if (!force_bytecode || alias == 0xFFFFFFFFUL) {
			unsigned long saved;
			push(0xFFFFFFFEUL);
			saved = sp;
			A = native_enter(target);
			/* The callee's own helper calls resync the global
			   sp as they please; the caller's truth is its r4,
			   so restore the slot level before popping it. */
			sp = saved;
			pop();
			return A;
		}
		target = alias;
	}
	return bc_exec(target);
}

static int64_t helper_libcall(unsigned long idx, unsigned char *vsp)
{
	sp = (unsigned long)(uintptr_t)vsp;
	prof_libcall++;
	if (prof_lib)
		prof_lib[idx & 0xFF]++;
	libcall((unsigned)idx);
	return A;
}

/*
 *	The compound-assign libcalls are the one family that takes an
 *	input in A (the amount; the address is on the stack) - a generic
 *	libcall reads all its arguments from the stack, which is why
 *	helper_libcall does not carry A across.  The native emitter
 *	routes the non-inlined eqop forms (64-bit and floating) here
 *	instead, with A in r2/r3 as helper_op takes it.  lib_eqop pops
 *	the address slot from the global sp; the native caller applies
 *	that fixed one-slot pop to its own r4 afterwards.
 */
static int64_t helper_eqop(unsigned long idx, unsigned char *vsp, int64_t a)
{
	sp = (unsigned long)(uintptr_t)vsp;
	prof_eqop++;
	A = a;
	libcall((unsigned)idx);
	return A;
}

/*
 *	One bytecode operation, for native code: the fp and wide-integer
 *	arithmetic the emitter does not inline.  A arrives in r2/r3 by the
 *	AAPCS (op in r0, vsp in r1) and the result goes back as A does.
 *
 *	Pure: the stacked operand, where the op has one, is read through
 *	vsp - never through the global sp, which is not synced for this
 *	call - and the native caller adjusts its own r4 afterwards by the
 *	op's fixed pop count (8 for a double or 64-bit operand, 4 for a
 *	float, 0 for a conversion).  The bodies mirror the interpreter's
 *	cases exactly, divide-by-zero-is-0 included; on the board the
 *	double arithmetic lands in the same DCP aeabi routines the
 *	interpreter uses, which is the whole point of routing FP through
 *	bcrun rather than emitting soft-float calls of the program's own.
 */
static int64_t helper_op(unsigned long op, unsigned char *vsp, int64_t a)
{
	unsigned long boff = (unsigned long)(uintptr_t)vsp;
	unsigned long imm = op >> 16;	/* COPY/PUSHN length */
	int64_t b;

	op &= 0xFFFF;
	if (prof_op)
		prof_op[op & 0xFF]++;
	switch (op) {
	/*
	 * Aggregates carry their length in the op word's high half - the
	 * only operations with an immediate.  COPY pops the destination
	 * (the native call site adds the slot to its r4); PUSHN writes
	 * below vsp and the call site lowers r4 by the rounded length.
	 */
	case BC_COPY: {
		unsigned long dst = rd32(boff);
		vcopy(dst, U32(a), imm);
		return S32(dst);
	}
	case BC_PUSHN: {
		unsigned long n = (imm < 4) ? 4 : ((imm + 3) & ~3UL);
		vcopy(boff - n, U32(a), imm);
		return a;
	}
	case BC_MUL64:	return (int64_t)rd64(boff) * a;
	case BC_DIVS64:	b = (int64_t)rd64(boff);
			return a ? b / a : 0;
	case BC_DIVU64:	b = (int64_t)rd64(boff);
			return a ? (int64_t)((uint64_t)b / (uint64_t)a) : 0;
	case BC_REMS64:	b = (int64_t)rd64(boff);
			return a ? b % a : 0;
	case BC_REMU64:	b = (int64_t)rd64(boff);
			return a ? (int64_t)((uint64_t)b % (uint64_t)a) : 0;
	case BC_SHL64:	return (int64_t)rd64(boff) << (U32(a) & 63);
	case BC_SHRS64:	return (int64_t)rd64(boff) >> (U32(a) & 63);
	case BC_SHRU64:	return (int64_t)(rd64(boff) >> (U32(a) & 63));

	case BC_ADDD:	return dput(dget((int64_t)rd64(boff)) + dget(a));
	case BC_SUBD:	return dput(dget((int64_t)rd64(boff)) - dget(a));
	case BC_MULD:	return dput(dget((int64_t)rd64(boff)) * dget(a));
	case BC_DIVD:	return dput(dget((int64_t)rd64(boff)) / dget(a));
	case BC_EQD:	return (dget((int64_t)rd64(boff)) == dget(a));
	case BC_NED:	return (dget((int64_t)rd64(boff)) != dget(a));
	case BC_LTD:	return (dget((int64_t)rd64(boff)) <  dget(a));
	case BC_GTD:	return (dget((int64_t)rd64(boff)) >  dget(a));
	case BC_LED:	return (dget((int64_t)rd64(boff)) <= dget(a));
	case BC_GED:	return (dget((int64_t)rd64(boff)) >= dget(a));

	case BC_ADDF:	return fput(fget(S32(rd32(boff))) + fget(a));
	case BC_SUBF:	return fput(fget(S32(rd32(boff))) - fget(a));
	case BC_MULF:	return fput(fget(S32(rd32(boff))) * fget(a));
	case BC_DIVF:	return fput(fget(S32(rd32(boff))) / fget(a));
	case BC_EQF:	return (fget(S32(rd32(boff))) == fget(a));
	case BC_NEF:	return (fget(S32(rd32(boff))) != fget(a));
	case BC_LTF:	return (fget(S32(rd32(boff))) <  fget(a));
	case BC_GTF:	return (fget(S32(rd32(boff))) >  fget(a));
	case BC_LEF:	return (fget(S32(rd32(boff))) <= fget(a));
	case BC_GEF:	return (fget(S32(rd32(boff))) >= fget(a));

	case BC_I2D:	return dput((double)a);
	case BC_U2D:	return dput((double)(uint64_t)a);
	case BC_D2I:	return (int64_t)dget(a);
	case BC_D2U:	return (int64_t)(uint64_t)dget(a);
	case BC_I2F:	return fput((float)a);
	case BC_U2F:	return fput((float)(uint64_t)a);
	case BC_F2I:	return (int64_t)fget(a);
	case BC_F2U:	return (int64_t)(uint64_t)fget(a);
	case BC_F2D:	return dput((double)fget(a));
	case BC_D2F:	return fput((float)dget(a));
	}
	fault("bad helper op");
	return 0;
}

static void call_target(unsigned long target)
{
	if (target < h.h_code && code[target] == BC_NATIVE) {
		unsigned long alias = code[target + 1] |
		    ((unsigned long)code[target + 2] << 8) |
		    ((unsigned long)code[target + 3] << 16) |
		    ((unsigned long)code[target + 4] << 24);
#if defined(__arm__) || defined(__thumb__)
		if (!force_bytecode || alias == 0xFFFFFFFFUL) {
			unsigned long saved;
			push(0xFFFFFFFEUL);
			saved = sp;
			A = native_enter(target);
			/* see helper_call: the callee's helper calls move
			   the global sp; restore the slot level */
			sp = saved;
			pop();
			return;
		}
#else
		if (alias == 0xFFFFFFFFUL) {
			native_enter(target);	/* faults with the message */
			return;
		}
#endif
		/* Interpret the function's still-present bytecode. */
		push(pc);
		pc = alias;
		return;
	}
	push(pc);
	pc = target;
}

/* ---- the interpreter ------------------------------------------------ */

/*
 *	Run bytecode from entry until the frame that entered returns.
 *	The sentinel pushed here comes back through BC_RET; re-entrant,
 *	so a native function calling back into bytecode gets its own
 *	sentinel and its own loop, stacked on the caller's.
 */
static int64_t bc_exec(unsigned long entry)
{
	unsigned long saved_pc = pc;

	pc = entry;
	/* A return to this impossible address ends this activation. */
	push(0xFFFFFFFFUL);

	for (;;) {
		unsigned char op;
		int64_t b;

		if (trace)
			fprintf(stderr, "%04lx: op %02x A=%ld sp=%lx\n",
				pc, code[pc], (long)A, sp);
		if (prof_site && pc < prof_site_n)
			prof_site[pc]++;
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

		/*
		 * Every 32-bit operation reads only the low 32 bits of A.
		 * This is the contract that lets a native (Thumb) function
		 * return a 32-bit result in r0 with whatever the high half
		 * of the pair happens to hold: the interpreter's own ops
		 * keep A sign extended, but a value that crossed the native
		 * seam arrives with a meaningless high word, and these are
		 * the sites that would otherwise see it.  64-bit values only
		 * ever reach the 64-bit ops through CONST64/LOAD64/POP64/
		 * SEXT32/ZEXT32, which establish the high word properly.
		 */
		case BC_LOAD8S:
			A = (signed char)rd8(U32(A));
			break;
		case BC_LOAD8U:
			A = rd8(U32(A));
			break;
		case BC_LOAD16S:
			A = (short)rd16(U32(A));
			break;
		case BC_LOAD16U:
			A = rd16(U32(A));
			break;
		case BC_LOAD32:
			A = S32(rd32(U32(A)));
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
		case BC_DIVS:	b = pop();
				A = S32(A) ? S32(b / S32(A)) : 0;
				break;
		case BC_DIVU:	b = pop();
				A = U32(A) ? S32(U32(b) / U32(A)) : 0;
				break;
		case BC_REMS:	b = pop();
				A = S32(A) ? S32(b % S32(A)) : 0;
				break;
		case BC_REMU:	b = pop();
				A = U32(A) ? S32(U32(b) % U32(A)) : 0;
				break;
		case BC_AND:	A = S32(pop() & A); break;
		case BC_OR:	A = S32(pop() | A); break;
		case BC_XOR:	A = S32(pop() ^ A); break;
		/* Shift counts are the low 32 bits of A; 63 caps the int64
		   shift below at defined behaviour.  A count of 32..63 falls
		   out of the S32 truncation as 0 for SHL/SHRU and as the
		   sign for SHRS, which is what the hardware's 8-bit-count
		   register shifts do too; larger counts are the program's
		   own undefined behaviour. */
		case BC_SHL:	A = S32(pop() << (U32(A) & 63)); break;
		case BC_SHRS:	A = S32(pop() >> (U32(A) & 63)); break;
		case BC_SHRU:	b = pop();
				A = S32(U32(b) >> (U32(A) & 63));
				break;
		case BC_NEG:	A = S32(-A); break;
		case BC_NOT:	A = S32(~A); break;
		case BC_LNOT:	A = !S32(A); break;

		case BC_EQ:	A = (pop() == S32(A)); break;
		case BC_NE:	A = (pop() != S32(A)); break;
		case BC_LTS:	A = (pop() < S32(A)); break;
		case BC_LTU:	b = pop();
				A = (U32(b) < U32(A));
				break;
		case BC_GTS:	A = (pop() > S32(A)); break;
		case BC_GTU:	b = pop();
				A = (U32(b) > U32(A));
				break;
		case BC_LES:	A = (pop() <= S32(A)); break;
		case BC_LEU:	b = pop();
				A = (U32(b) <= U32(A));
				break;
		case BC_GES:	A = (pop() >= S32(A)); break;
		case BC_GEU:	b = pop();
				A = (U32(b) >= U32(A));
				break;
		case BC_BOOL:	A = (S32(A) != 0); break;

		/* ---- 64-bit: no truncation, two stack slots ---------- */
		case BC_CONST64:	A = fetch64(); break;
		case BC_LOAD64:		A = (int64_t)rd64(U32(A)); break;
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
		/*
		 * Truthiness by bits, not by comparing against 0.0: shifting
		 * out the sign leaves zero for +-0.0 and non-zero for
		 * everything else including NaN, exactly IEEE's != 0.0 -
		 * and unlike a real compare it is denormal-exact on every
		 * engine.  The RP2350's DCP flushes denormals to zero, and
		 * cc2 emitted BOOLD on the *integer* result of a double
		 * comparison (int 1 = 5e-324, a denormal), so on the DCP
		 * every comparison result died right here.
		 */
		case BC_BOOLD:	A = (((uint64_t)A << 1) != 0); break;
		case BC_LNOTD:	A = (((uint64_t)A << 1) == 0); break;

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
		/* same by-bits truthiness as BOOLD, for the same reasons */
		case BC_BOOLF:	A = ((A & 0x7FFFFFFF) != 0); break;
		case BC_LNOTF:	A = ((A & 0x7FFFFFFF) == 0); break;

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
			unsigned long src = U32(A);
			unsigned long dst = (unsigned long)pop();
			vcopy(dst, src, len);
			A = dst;
			break;
		}

		case BC_PUSHN: {
			unsigned long len = fetch16();
			unsigned long src = U32(A);
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
		/* Conditions are always BOOL/relational-normalised 0 or 1,
		   but test only the low word anyway - it is what the native
		   translation of these does (cmp r0, #0). */
		case BC_JFALSE:
			b = (short)fetch16();
			if (!S32(A))
				pc += b;
			break;
		case BC_JTRUE:
			b = (short)fetch16();
			if (S32(A))
				pc += b;
			break;
		case BC_CALL:
			call_target(fetch32());
			break;
		case BC_CALLA:
			call_target((unsigned long)A & 0xFFFFFFFFUL);
			break;
		case BC_RET:
			/* Mask: pop() sign extends, so the sentinel comes
			   back as -1 and would widen past 32 bits here. */
			pc = (unsigned long)pop() & 0xFFFFFFFFUL;
			if (pc == 0xFFFFFFFFUL) {
				pc = saved_pc;
				return A;
			}
			break;
		case BC_ENTER:
			sp -= fetch16();
			/*
			 * Where an interpreted frame is taken, and so
			 * where recursion is told it has gone too far
			 * rather than left to overwrite the data below.
			 * A translated function does not pass here - it
			 * allocates its frame with a bare "sub r4, #n" -
			 * so it carries the same test inline in its
			 * prologue against helper slot 21.  That was
			 * once judged a poor trade; it is not, because
			 * a small recursive SUB is exactly what the
			 * translator takes, so leaving it out left the
			 * common case uncovered and it took the board
			 * down, video and all.
			 */
			if (stack_floor && sp < stack_floor)
				fault("stack overflow - recursion too deep?");
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
					if (S32(rd32(tab + 4 + 8 * i)) == S32(A)) {
						target = rd32(tab + 8 + 8 * i);
						break;
					}
				}
				pc = target;
			}
			break;
		case BC_NATIVE:
			/* Only reachable by falling INTO a native function
			   rather than calling it - a dispatch bug. */
			fault("fell into native code");
		default:
			fault("bad opcode");
		}
	}
}

static int run(void)
{
	sp = MEMTOP - 4;
	/* Dispatch the entry exactly like a call site: h_entry points
	   at main's BC_NATIVE marker when it was translated, and a
	   BASIC program lives in its main line - entering through the
	   bytecode quietly interpreted the whole program while every
	   benchmark with the work in called functions stayed fast. */
	if (h.h_entry < h.h_code && code[h.h_entry] == BC_NATIVE) {
		unsigned long alias = code[h.h_entry + 1] |
		    ((unsigned long)code[h.h_entry + 2] << 8) |
		    ((unsigned long)code[h.h_entry + 3] << 16) |
		    ((unsigned long)code[h.h_entry + 4] << 24);
#if defined(__arm__) || defined(__thumb__)
		if (!force_bytecode || alias == 0xFFFFFFFFUL) {
			unsigned long saved;
			int64_t r;
			push(0xFFFFFFFEUL);	/* the return-pc slot the
						   frame layout expects */
			saved = sp;
			r = native_enter(h.h_entry);
			sp = saved;
			pop();
			return (int)r;
		}
#else
		if (alias == 0xFFFFFFFFUL) {
			native_enter(h.h_entry);	/* faults */
			return 1;
		}
#endif
		return (int)bc_exec(alias);
	}
	return (int)bc_exec(h.h_entry);
}

/*
 *	The arguments the PROGRAM was given - everything after the .bc
 *	name.  A translated program's main() is dispatched with no
 *	arguments (see run()), so MM.CMDLINE$ cannot come from its own
 *	argv the way it does in the hosted build; bcrun holds the real
 *	command line and hands it over through w_argv_bind.
 */
static int prog_argc;
static char **prog_argv;

/*
 *	The arguments the PROGRAM was given - everything after the .bc
 *	name.  A translated program's main() is dispatched with no
 *	arguments (see run()), so MM.CMDLINE$ cannot come from its own
 *	argv the way it does in the hosted build; bcrun holds the real
 *	command line and hands it over through w_argv_bind.
 */
static int prog_argc;
static char **prog_argv;

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
	mem_init();
	mfns_share();
	force_bytecode = getenv("BCRUN_BYTECODE") != NULL;
	prof_on = getenv("BCRUN_PROF") != NULL;
	if (prof_on) {
		prof_op = calloc(256, sizeof(uint32_t));
		prof_lib = calloc(256, sizeof(uint32_t));
	}
	if (prof_on || getenv("BCRUN_SITES"))
		atexit(prof_dump);	/* mm_end() exits through here */
	prog_argc = argc - i;
	prog_argv = argv + i;
	prog_argc = argc - i;
	prog_argv = argv + i;
	load(argv[i]);
	if (getenv("BCRUN_SITES")) {
		prof_site_n = h.h_code;
		prof_site = calloc(prof_site_n, sizeof(*prof_site));
	}
	return run();
}

/*
 *	The mm_* BASIC runtime, one translation unit with the
 *	interpreter: the wrappers want arg()/dput()/mem and the loader
 *	wants mmrt_reserve(), and none of it needs external linkage.
 */
#include "bcrun_mm.c"
