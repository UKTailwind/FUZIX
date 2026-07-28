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
#define STACKROOM	8192		/* kept clear at the top for the stack */

static unsigned char *code;
static unsigned char mem[MEMSIZE];
static struct bc_header h;
static struct bc_sym *sym;
static struct bc_fixup *fix;
static char *strtab;

/* Machine state. sp and fp are offsets into mem[], and the stack grows
   down from the top. */
static long A;
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
static void lib_eqop(const char *name)
{
	char base[24];
	unsigned len, sz = 4;
	int uns = 0, post;
	unsigned long addr;
	long old, v, res;

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

/* Pad a already-formatted item to the requested width. */
static void padout(const char *s, int width, int left, int zero)
{
	int n = (int)strlen(s);
	int pad = width - n;
	if (!left)
		while (pad-- > 0)
			putchar(zero ? '0' : ' ');
	fputs(s, stdout);
	if (left)
		while (pad-- > 0)
			putchar(' ');
}

static void lib_printf(void)
{
	const char *f = getstr((unsigned long)arg(0));
	char tmp[544];
	unsigned a = 1;

	while (*f) {
		int left = 0, zero = 0, width = 0;

		if (*f != '%') {
			putchar(*f++);
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

		switch (*f) {
		case 'd':
			sprintf(tmp, "%ld", arg(a++));
			padout(tmp, width, left, zero);
			break;
		case 'u':
			sprintf(tmp, "%lu", (unsigned long)arg(a++));
			padout(tmp, width, left, zero);
			break;
		case 'x':
			sprintf(tmp, "%lx", (unsigned long)arg(a++));
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
		case '%':
			putchar('%');
			break;
		default:
			putchar('%');
			if (*f)
				putchar(*f);
			break;
		}
		if (*f)
			f++;
	}
	A = 0;
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

	/* Data goes at the bottom of the program's address space, bss
	   directly after it, and the stack starts at the top. */
	database = 0;
	bssbase = h.h_data;
	/* The heap is whatever is left between bss and the stack. */
	heap_init(h.h_data + h.h_bss);
	if (h.h_data + h.h_bss + STACKROOM > MEMSIZE) {
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
		long b;

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

		case BC_ADD:	A = pop() + A; break;
		case BC_SUB:	A = pop() - A; break;
		case BC_MUL:	A = pop() * A; break;
		case BC_DIVS:	b = pop(); A = A ? b / A : 0; break;
		case BC_DIVU:	b = pop();
				A = A ? (long)((unsigned long)b / (unsigned long)A) : 0;
				break;
		case BC_REMS:	b = pop(); A = A ? b % A : 0; break;
		case BC_REMU:	b = pop();
				A = A ? (long)((unsigned long)b % (unsigned long)A) : 0;
				break;
		case BC_AND:	A = pop() & A; break;
		case BC_OR:	A = pop() | A; break;
		case BC_XOR:	A = pop() ^ A; break;
		case BC_SHL:	A = pop() << A; break;
		case BC_SHRS:	A = pop() >> A; break;
		case BC_SHRU:	b = pop();
				A = (long)((unsigned long)b >> A);
				break;
		case BC_NEG:	A = -A; break;
		case BC_NOT:	A = ~A; break;
		case BC_LNOT:	A = !A; break;

		case BC_EQ:	A = (pop() == A); break;
		case BC_NE:	A = (pop() != A); break;
		case BC_LTS:	A = (pop() < A); break;
		case BC_LTU:	b = pop();
				A = ((unsigned long)b < (unsigned long)A);
				break;
		case BC_GTS:	A = (pop() > A); break;
		case BC_GTU:	b = pop();
				A = ((unsigned long)b > (unsigned long)A);
				break;
		case BC_LES:	A = (pop() <= A); break;
		case BC_LEU:	b = pop();
				A = ((unsigned long)b <= (unsigned long)A);
				break;
		case BC_GES:	A = (pop() >= A); break;
		case BC_GEU:	b = pop();
				A = ((unsigned long)b >= (unsigned long)A);
				break;
		case BC_BOOL:	A = (A != 0); break;

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
