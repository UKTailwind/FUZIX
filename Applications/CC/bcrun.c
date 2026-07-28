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
#include <stdint.h>
#include "bytecode.h"

/* The machine is 32bit. On a 64bit host every value that enters A or
   the stack must be sign extended from 32 bits, or negative numbers
   read back as huge positive ones. */
#define S32(x)	((long)(int32_t)(x))

#define MEMSIZE		65536		/* program address space */
#define STACKROOM	4096

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
	unsigned long addr = (unsigned long)pop();
	long old = (long)rd32(addr);
	long v = A;

	if (!strcmp(name, "postinc")) {
		wr32(addr, old + v);
		A = old;
	} else if (!strcmp(name, "postdec")) {
		wr32(addr, old - v);
		A = old;
	} else if (!strcmp(name, "pluseq")) {
		A = old + v;
		wr32(addr, A);
	} else if (!strcmp(name, "minuseq")) {
		A = old - v;
		wr32(addr, A);
	} else if (!strcmp(name, "muleq")) {
		A = old * v;
		wr32(addr, A);
	} else if (!strcmp(name, "diveq")) {
		A = v ? old / v : 0;
		wr32(addr, A);
	} else if (!strcmp(name, "remeq")) {
		A = v ? old % v : 0;
		wr32(addr, A);
	} else if (!strcmp(name, "andeq")) {
		A = old & v;
		wr32(addr, A);
	} else if (!strcmp(name, "oreq")) {
		A = old | v;
		wr32(addr, A);
	} else if (!strcmp(name, "xoreq")) {
		A = old ^ v;
		wr32(addr, A);
	} else if (!strcmp(name, "shleq")) {
		A = old << v;
		wr32(addr, A);
	} else if (!strcmp(name, "shreq")) {
		A = old >> v;
		wr32(addr, A);
	} else {
		fprintf(stderr, "bcrun: no runtime function \"%s\"\n", name);
		exit(1);
	}
}

/* Copy a NUL terminated string out of the program's memory. */
static char *getstr(unsigned long a)
{
	static char buf[512];
	unsigned i = 0;
	while (i < sizeof(buf) - 1 && a + i < MEMSIZE && mem[a + i])
		buf[i] = mem[a + i], i++;
	buf[i] = 0;
	return buf;
}

/*
 *	Arguments to a library call are on the stack exactly as for a
 *	bytecode call: arg(0) is nearest the stack pointer.
 */
static long arg(unsigned n)
{
	return S32(rd32(sp + 4 * n));
}

static void lib_printf(void)
{
	const char *f = getstr((unsigned long)arg(0));
	unsigned a = 1;
	while (*f) {
		if (*f != '%') {
			putchar(*f++);
			continue;
		}
		f++;
		switch (*f) {
		case 'd':
			printf("%ld", arg(a++));
			break;
		case 'u':
			printf("%lu", (unsigned long)arg(a++));
			break;
		case 'x':
			printf("%lx", (unsigned long)arg(a++));
			break;
		case 'c':
			putchar((int)arg(a++));
			break;
		case 's':
			fputs(getstr((unsigned long)arg(a++)), stdout);
			break;
		case '%':
			putchar('%');
			break;
		default:
			putchar('%');
			putchar(*f);
			break;
		}
		if (*f)
			f++;
	}
	A = 0;
}

static void libcall(unsigned idx)
{
	const char *name;

	if (idx >= h.h_nsym)
		fault("bad library index");
	name = strtab + sym[idx].s_name;

	if (!strcmp(name, "putchar")) {
		putchar((int)arg(0));
		A = arg(0);
	} else if (!strcmp(name, "puts")) {
		puts(getstr((unsigned long)arg(0)));
		A = 0;
	} else if (!strcmp(name, "printf")) {
		lib_printf();
	} else if (!strcmp(name, "exit")) {
		exit((int)arg(0));
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
			fault("switch not implemented");
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
