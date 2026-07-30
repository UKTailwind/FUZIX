/*
 *	cc2 backend emitting the PC3 bytecode (see BYTECODE.md).
 *
 *	Unlike the other backends this writes a binary object rather than
 *	assembler text: there is no assembler and no linker for this
 *	target. Code and data are built in memory, jumps are backpatched
 *	when the module ends, and everything is written out by gen_end().
 *
 *	Anything the emitter has no opcode for falls back to a named
 *	runtime helper via BC_LIBCALL, which is the same escape hatch the
 *	other backends use with their helper calls.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "compiler.h"
#include "backend.h"
#include "bytecode.h"

#define T_NREF		(T_USER)		/* Load of C global/static */
#define T_CALLNAME	(T_USER+1)		/* Function call by name */
#define T_NSTORE	(T_USER+2)		/* Store to a C global/static */
#define T_LREF		(T_USER+3)		/* Ditto for local */
#define T_LSTORE	(T_USER+4)
#define T_LBREF		(T_USER+5)		/* Labelled strings/local static */
#define T_LBSTORE	(T_USER+6)

/*
 *	Output buffers. A program that will not fit in memory here will
 *	not fit in a 255K process anyway.
 */
#define CODEMAX		131072
#define DATAMAX		65536
/* 512 was inherited from the 8-bit targets, where the table really is
   scarce. Here cc2 runs in a 256K process, so the cost of 2048 is a few
   tens of K against a program that would otherwise simply not compile
   ("too many symbols" on c-testsuite 00200). */
#define MAXSYM		4096
#define MAXFIX		8192
#define MAXLAB		4096

static unsigned char codebuf[CODEMAX];
static unsigned long codelen;
static unsigned char databuf[DATAMAX];
static unsigned long datalen;
static unsigned long bsslen;

/*
 *	Literals are a separate segment, appended to data when the module
 *	is written out. They cannot share datalen: the frontend declares
 *	a data label, switches to the literal segment, emits the string,
 *	then switches back and emits the initialiser. With one counter the
 *	label records an offset the object never occupies and the two
 *	overlap -- "char *msg = \"...\"" put both msg and the string at 0.
 */
static unsigned char litbuf[DATAMAX];
static unsigned long litlen;
static unsigned char sym_in_lit[MAXSYM];
static unsigned char fix_in_lit[MAXFIX];

#define STRMAX		32768
static char strtab[STRMAX];
static unsigned long strtablen;

static struct bc_sym symtab[MAXSYM];
static char *bc_symname[MAXSYM];
static unsigned nsym;

static struct bc_fixup fixtab[MAXFIX];
static unsigned nfix;

/* Labels are (tail, number) pairs scoped to the module. */
/*
 *	A label is a tag number plus a short name: "_b" and "_c" for the
 *	break and continue of a loop, "_g<n>" for a user goto label.
 *
 *	The tail used to be four bytes and was compared over three, which
 *	is fine for the compiler's own two character names and silently
 *	wrong for goto: "_g32769", "_g32770" and "_g32771" are all "_g3",
 *	so every goto label in a function was the same label and the last
 *	one defined won. Two labels in one function was enough to generate
 *	a jump to itself. Frontend symbol numbers reach five digits, so
 *	"_g" plus five plus the NUL.
 */
#define LABTAIL	12

struct label {
	unsigned num;
	char tail[LABTAIL];
	unsigned long addr;
	unsigned defined;
};
static struct label labtab[MAXLAB];
static unsigned nlab;

/* Jump sites waiting for their label. */
struct patch {
	unsigned long at;	/* code offset of the 16bit displacement */
	unsigned lab;		/* index into labtab */
};
static struct patch patchtab[MAXFIX];
static unsigned npatch;

static unsigned frame_len;
static unsigned sp;
static unsigned long entry;

/* ------------------------------------------------------------------ */

static void cbyte(unsigned v)
{
	if (codelen >= CODEMAX) {
		error("code overflow");
		return;
	}
	codebuf[codelen++] = v;
}

static void cword(unsigned v)
{
	cbyte(v & 0xFF);
	cbyte((v >> 8) & 0xFF);
}

static void clong(unsigned long v)
{
	cbyte(v & 0xFF);
	cbyte((v >> 8) & 0xFF);
	cbyte((v >> 16) & 0xFF);
	cbyte((v >> 24) & 0xFF);
}

static unsigned in_literal(void);

static void dbyte(unsigned v)
{
	if (in_literal()) {
		if (litlen >= DATAMAX) {
			error("literal overflow");
			return;
		}
		litbuf[litlen++] = v;
		return;
	}
	if (datalen >= DATAMAX) {
		error("data overflow");
		return;
	}
	databuf[datalen++] = v;
}

/* Offset within whichever of the two segments is being filled. */
static unsigned long dhere(void)
{
	return in_literal() ? litlen : datalen;
}

static void dword(unsigned v)
{
	dbyte(v & 0xFF);
	dbyte((v >> 8) & 0xFF);
}

static void dlong(unsigned long v)
{
	dbyte(v & 0xFF);
	dbyte((v >> 8) & 0xFF);
	dbyte((v >> 16) & 0xFF);
	dbyte((v >> 24) & 0xFF);
}

/* ------------------------------------------------------------------ */

/*
 *	Symbols. A name that is never defined in this module is a runtime
 *	library entry point: there is no linker to resolve it against, and
 *	the interpreter provides the library.
 */
static unsigned symref(const char *name)
{
	unsigned i;
	for (i = 0; i < nsym; i++) {
		if (strcmp(bc_symname[i], name) == 0)
			return i;
	}
	if (nsym >= MAXSYM) {
		error("too many symbols");
		return 0;
	}
	bc_symname[nsym] = strdup(name);
	symtab[nsym].s_type = BC_SYM_LIB;
	symtab[nsym].s_value = 0;
	symtab[nsym].s_name = strtablen;
	if (strtablen + strlen(name) + 1 < STRMAX) {
		strcpy(strtab + strtablen, name);
		strtablen += strlen(name) + 1;
	} else
		error("string table full");
	return nsym++;
}

static void symdef(const char *name, unsigned type, unsigned long value)
{
	unsigned s = symref(name);
	symtab[s].s_type = type;
	symtab[s].s_value = value;
	if (type == BC_SYM_DATA)
		sym_in_lit[s] = in_literal();
}

/* Labels get symbols too, so a switch table or a string can name one. */
static unsigned labsym(const char *tail, unsigned n)
{
	char buf[24];
	sprintf(buf, "L%u%s", n, tail);
	return symref(buf);
}

static void fixup(unsigned seg, unsigned long off, unsigned sym)
{
	if (nfix >= MAXFIX) {
		error("too many fixups");
		return;
	}
	fixtab[nfix].f_offset = off;
	fixtab[nfix].f_sym = sym;
	fixtab[nfix].f_seg = seg;
	fixtab[nfix].f_pad = 0;
	if (seg == BC_SEG_DATA)
		fix_in_lit[nfix] = in_literal();
	nfix++;
}

/* ------------------------------------------------------------------ */

static unsigned labref(const char *tail, unsigned n)
{
	unsigned i;
	for (i = 0; i < nlab; i++) {
		if (labtab[i].num == n && strcmp(labtab[i].tail, tail) == 0)
			return i;
	}
	if (nlab >= MAXLAB) {
		error("too many labels");
		return 0;
	}
	if (strlen(tail) >= LABTAIL)
		error("label name too long");
	labtab[nlab].num = n;
	strncpy(labtab[nlab].tail, tail, LABTAIL - 1);
	labtab[nlab].tail[LABTAIL - 1] = 0;
	labtab[nlab].addr = 0;
	labtab[nlab].defined = 0;
	return nlab++;
}

/* Emit a jump-class opcode with a placeholder displacement. */
static void jumpto(unsigned op, const char *tail, unsigned n)
{
	unsigned l = labref(tail, n);
	cbyte(op);
	if (npatch >= MAXFIX) {
		error("too many jumps");
		return;
	}
	patchtab[npatch].at = codelen;
	patchtab[npatch].lab = l;
	npatch++;
	cword(0);
}

static void resolve_jumps(void)
{
	unsigned i;
	for (i = 0; i < npatch; i++) {
		struct label *l = &labtab[patchtab[i].lab];
		long disp;
		unsigned long at = patchtab[i].at;
		if (!l->defined) {
			error("undefined label");
			continue;
		}
		/* Relative to the end of the instruction */
		disp = (long)l->addr - (long)(at + 2);
		if (disp < -32768 || disp > 32767) {
			error("jump out of range");
			continue;
		}
		codebuf[at] = disp & 0xFF;
		codebuf[at + 1] = (disp >> 8) & 0xFF;
	}
}

/* ------------------------------------------------------------------ */

static unsigned typesize(unsigned t)
{
	if (PTR(t))
		return 4;
	if (t == CCHAR || t == UCHAR)
		return 1;
	if (t == CSHORT || t == USHORT)
		return 2;
	if (t == CLONG || t == ULONG || t == FLOAT)
		return 4;
	if (t == CLONGLONG || t == ULONGLONG || t == DOUBLE)
		return 8;
	if (t == VOID)
		return 0;
	return 4;
}

/* Stack slots are whole words */
static unsigned stack_size(unsigned t)
{
	unsigned s = typesize(t);
	return (s < 4) ? 4 : ((s + 3) & ~3);
}

static void emit_load(unsigned t)
{
	if (PTR(t)) {
		cbyte(BC_LOAD32);
		return;
	}
	if (typesize(t) == 8) {
		cbyte(BC_LOAD64);
		return;
	}
	switch (typesize(t)) {
	case 1:
		cbyte((t & UNSIGNED) ? BC_LOAD8U : BC_LOAD8S);
		break;
	case 2:
		cbyte((t & UNSIGNED) ? BC_LOAD16U : BC_LOAD16S);
		break;
	default:
		cbyte(BC_LOAD32);
		break;
	}
}

static void emit_store(unsigned t)
{
	if (PTR(t)) {
		cbyte(BC_STORE32);
		return;
	}
	if (typesize(t) == 8) {
		cbyte(BC_STORE64);
		return;
	}
	switch (typesize(t)) {
	case 1:
		cbyte(BC_STORE8);
		break;
	case 2:
		cbyte(BC_STORE16);
		break;
	default:
		cbyte(BC_STORE32);
		break;
	}
}

static void emit_const(cval_t v, unsigned t)
{
	long s = (long)v;

	/*
	 * A 64-bit constant has to arrive as one: the short forms all sign
	 * extend into a 32-bit value, so "5000000000LL" would lose its top
	 * half before anything could use it.
	 */
	if (typesize(t) == 8) {
		cbyte(BC_CONST64);
		clong(v & 0xFFFFFFFFUL);
		clong((unsigned long)(((unsigned long long)v) >> 32));
		return;
	}
	if (s >= -128 && s < 128) {
		cbyte(BC_CONST8);
		cbyte(v & 0xFF);
	} else if (s >= -32768 && s < 32768) {
		cbyte(BC_CONST16);
		cword(v & 0xFFFF);
	} else {
		cbyte(BC_CONST32);
		clong(v);
	}
}

static void emit_local(unsigned off)
{
	if (off < 256) {
		cbyte(BC_LOCAL8);
		cbyte(off);
	} else {
		cbyte(BC_LOCAL16);
		cword(off);
	}
}

/* Address of a symbol plus an offset, left in A. */
static void emit_addr(unsigned sym, unsigned long off)
{
	cbyte(BC_ADDR);
	fixup(BC_SEG_CODE, codelen, sym);
	clong(off);
}

/* Fall back to a named runtime helper. */
static void libcall(const char *name)
{
	unsigned s = symref(name);
	cbyte(BC_LIBCALL);
	cword(s);
}

/* ------------------------------------------------------------------ */

struct node *gen_rewrite_node(struct node *n)
{
	struct node *r = n->right;
	unsigned op = n->op;

	/* Turn a call of a name into a direct call */
	if (op == T_FUNCCALL && r && r->op == T_NAME && PTR(r->type) == 1) {
		n->op = T_CALLNAME;
		n->snum = r->snum;
		n->value = r->value;
		free_node(r);
		n->right = NULL;
	}
	return n;
}

void gen_export(const char *name)
{
}

/*
 *	Which segment subsequent labels and space belong to. Without this
 *	an uninitialised global gets a data symbol while its storage is
 *	counted in bss, and the loader resolves it to the wrong place.
 */
static unsigned curseg = A_CODE;

static unsigned in_literal(void)
{
	return curseg == A_LITERAL;
}

/*
 *	Set while the body of a switch table is being emitted. The case
 *	values otherwise go out at the switch expression's own width, so
 *	"switch (c)" on a char produced one-byte values and a five-byte
 *	stride, which the interpreter - reasonably assuming a word - read
 *	as garbage and jumped into hyperspace. C promotes the switch
 *	expression to at least int, so widening the values to a word
 *	loses nothing and keeps the table uniform.
 */
static unsigned in_switchtab;

void gen_segment(unsigned s)
{
	/* The table ends when its area is popped. */
	in_switchtab = 0;
	curseg = s;
}

void gen_prologue(const char *name)
{
	symdef(name, BC_SYM_CODE, codelen);
	if (strcmp(name, "main") == 0)
		entry = codelen;
}

/*
 *	Frame layout. BC_ENTER n moves the stack pointer down by n, so
 *	the locals occupy [sp, sp+n) and the return address and arguments
 *	are above them:
 *
 *	    sp -> [ locals            ]  n bytes
 *	          [ return address    ]  <- sp on entry
 *	          [ argument 0        ]
 *	          [ argument 1        ]  ...
 *
 *	"sp" below counts only what expression evaluation has pushed, not
 *	the frame, because a local at frame offset v lives at sp + v +
 *	pushes. Adding the frame size here as the other backends do puts
 *	every local above the return address and in the caller's frame,
 *	which loops forever rather than failing cleanly.
 *
 *	An argument at offset v lives at sp + v + frame_len + pushes,
 *	which is what T_ARGUMENT emits.
 */
void gen_frame(unsigned size, unsigned aframe)
{
	frame_len = size + 4;		/* locals plus the return address */
	cbyte(BC_ENTER);
	cword(size);
}

void gen_epilogue(unsigned size, unsigned argsize)
{
	if (sp)
		error("sp");
	cbyte(BC_LEAVE);
	cword(size);
	cbyte(BC_RET);
}

void gen_label(const char *tail, unsigned n)
{
	unsigned l = labref(tail, n);
	unsigned s;
	labtab[l].addr = codelen;
	labtab[l].defined = 1;
	/* Also give it a symbol, so a switch table can name it */
	s = labsym(tail, n);
	symtab[s].s_type = BC_SYM_CODE;
	symtab[s].s_value = codelen;
}

unsigned gen_exit(const char *tail, unsigned n)
{
	jumpto(BC_JUMP, tail, n);
	return 0;
}

void gen_jump(const char *tail, unsigned n)
{
	jumpto(BC_JUMP, tail, n);
}

void gen_jfalse(const char *tail, unsigned n)
{
	jumpto(BC_JFALSE, tail, n);
}

void gen_jtrue(const char *tail, unsigned n)
{
	jumpto(BC_JTRUE, tail, n);
}

void gen_switch(unsigned n, unsigned type)
{
	char buf[24];
	sprintf(buf, "Sw%u", n);
	cbyte(BC_SWITCH);
	fixup(BC_SEG_CODE, codelen, symref(buf));
	clong(0);
}

void gen_switchdata(unsigned n, unsigned size)
{
	char buf[24];
	sprintf(buf, "Sw%u", n);
	symdef(buf, BC_SYM_DATA, dhere());
	dlong(size);
	in_switchtab = 1;
}

void gen_case(unsigned tag, unsigned entry)
{
	char buf[24];
	sprintf(buf, "Sw%u_%u", tag, entry);
	symdef(buf, BC_SYM_CODE, codelen);
}

void gen_case_label(unsigned tag, unsigned entry)
{
	gen_case(tag, entry);
}

void gen_case_data(unsigned tag, unsigned entry)
{
	char buf[24];
	sprintf(buf, "Sw%u_%u", tag, entry);
	fixup(BC_SEG_DATA, dhere(), symref(buf));
	dlong(0);
}

void gen_helpcall(struct node *n)
{
}

void gen_helpclean(struct node *n)
{
}

void gen_data_label(const char *name, unsigned align)
{
	if (curseg == A_BSS)
		symdef(name, BC_SYM_BSS, bsslen);
	else
		symdef(name, BC_SYM_DATA, dhere());
}

void gen_space(unsigned value)
{
	if (curseg == A_BSS)
		bsslen += value;
	else {
		/* Reserved space inside initialised data still occupies
		   the file, so it has to be written out as zeroes. */
		while (value--)
			dbyte(0);
	}
}

void gen_text_data(unsigned n)
{
	char buf[24];
	sprintf(buf, "T%u", n);
	fixup(BC_SEG_DATA, dhere(), symref(buf));
	dlong(0);
}

void gen_literal(unsigned n)
{
	char buf[24];
	if (n) {
		sprintf(buf, "T%u", n);
		/*
		 * Respect the segment, exactly as gen_data_label does.
		 * This always said DATA, so a numbered label emitted into
		 * bss - which is how a static local is written out - got a
		 * data address instead of a bss one. A "static int d[4]"
		 * inside a function then aliased the string literal area:
		 * assigning to d[3] rewrote the fourth word of the
		 * literals, and a printf format string turned into
		 * whatever had just been stored.
		 */
		if (curseg == A_BSS)
			symdef(buf, BC_SYM_BSS, bsslen);
		else
			symdef(buf, BC_SYM_DATA, dhere());
	}
}

void gen_name(struct node *n)
{
	fixup(BC_SEG_DATA, dhere(), symref(namestr(n->snum)));
	dlong(n->value);
}

void gen_value(unsigned type, cval_t value)
{
	/* Case values are always a word: see in_switchtab. */
	if (in_switchtab || PTR(type)) {
		dlong(value);
		return;
	}
	switch (typesize(type)) {
	case 1:
		dbyte(value & 0xFF);
		break;
	case 2:
		dword(value & 0xFFFF);
		break;
	case 8:
		/* An initialised long long or double in the data segment.
		   Writing one word here left the object four bytes short of
		   what the symbol claimed, so everything after it moved. */
		dlong(value & 0xFFFFFFFFUL);
		dlong((unsigned long)(((unsigned long long)value) >> 32));
		break;
	default:
		dlong(value);
		break;
	}
}

void gen_start(void)
{
}

void gen_end(void)
{
	struct bc_header h;
	unsigned i;

	resolve_jumps();

	/* Literals sit after data in the emitted image, so everything
	   recorded against the literal buffer moves up by datalen. */
	for (i = 0; i < nsym; i++)
		if (sym_in_lit[i])
			symtab[i].s_value += datalen;
	for (i = 0; i < nfix; i++)
		if (fix_in_lit[i])
			fixtab[i].f_offset += datalen;

	memcpy(h.h_magic, BC_MAGIC, 4);
	h.h_version = BC_VERSION;
	h.h_pad = 0;
	h.h_nsym = nsym;
	h.h_code = codelen;
	h.h_data = datalen + litlen;
	h.h_bss = bsslen;
	h.h_entry = entry;
	h.h_nfixup = nfix;
	h.h_strsize = strtablen;

	fwrite(&h, sizeof(h), 1, stdout);
	fwrite(codebuf, 1, codelen, stdout);
	fwrite(databuf, 1, datalen, stdout);
	fwrite(litbuf, 1, litlen, stdout);
	for (i = 0; i < nfix; i++)
		fwrite(&fixtab[i], sizeof(struct bc_fixup), 1, stdout);
	for (i = 0; i < nsym; i++)
		fwrite(&symtab[i], sizeof(struct bc_sym), 1, stdout);
	fwrite(strtab, 1, strtablen, stdout);
	fflush(stdout);
}

void gen_tree(struct node *n)
{
	codegen_lr(n);
}

/* ------------------------------------------------------------------ */

unsigned gen_push(struct node *n)
{
	unsigned t = n->type;

	/*
	 * A struct or union argument is copied onto the stack whole. The
	 * accumulator holds its address, and the length came from the
	 * front end in the node's value because this pass cannot size an
	 * aggregate. The rounding here has to match target_argsize(),
	 * which is what T_CLEANUP eventually takes back off.
	 */
	if (IS_STRUCT(t) && !PTR(t)) {
		unsigned len = n->value;
		if (len == 0 || len > 0xFFFF)
			error("struct size");
		cbyte(BC_PUSHN);
		cword(len);
		sp += (len < 4) ? 4 : ((len + 3) & ~3);
		return 1;
	}
	sp += stack_size(t);
	/* A 64-bit value takes two slots and needs the wide push. */
	cbyte(typesize(t) == 8 ? BC_PUSH64 : BC_PUSH);
	return 1;
}

unsigned gen_direct(struct node *n)
{
	cval_t v;
	switch (n->op) {
	/*
	 * Cleanup must be handled here, not in gen_node. It carries the
	 * function's return type, so the byte count to discard is in
	 * n->right->value and not derivable from the node's own type.
	 * Missing this leaves the pushed arguments in the stack-depth
	 * accounting and the epilogue check fails with "sp".
	 */
	case T_CLEANUP:
		v = n->right->value;
		if (v) {
			cbyte(BC_ARGS);
			cbyte(v & 0xFF);
		}
		sp -= v;
		return 1;
	}
	return 0;
}

unsigned gen_uni_direct(struct node *n)
{
	return 0;
}

unsigned gen_shortcut(struct node *n)
{
	/*
	 * The comma operator. Nothing generated it before, so it reached
	 * the fallback and came out as a call to "__op2c" that does not
	 * exist - "(void)f(), g();" and "for (i = 0, j = 0; ...)" simply
	 * did not work.
	 *
	 * Handled here rather than in gen_node because the generic walk
	 * would push the left operand's value first, and the whole point
	 * of a comma is that the left value is thrown away. Marking it
	 * NORETURN also lets a left side with no side effects disappear
	 * completely.
	 */
	if (n->op == T_COMMA) {
		n->left->flags |= NORETURN;
		codegen_lr(n->left);
		codegen_lr(n->right);
		return 1;
	}
	return 0;
}

/*
 *	Floating point.
 *
 *	A double is carried in the accumulator as its bit pattern and a
 *	float in the low 32 bits, so loads, stores, pushes and constants
 *	are the existing 64 and 32 bit ones and only the operations that
 *	interpret the bits are floating point specific.
 *
 *	The trap this exists to avoid is silence rather than failure:
 *	typesize(DOUBLE) is 8, so without a branch of its own "a + b" on
 *	two doubles falls into the 64-bit *integer* cases and adds the bit
 *	patterns, printing a wrong answer instead of stopping.
 */
static unsigned isdouble(unsigned t)
{
	return BASE_TYPE(t) == DOUBLE;
}

static unsigned isfp(unsigned t)
{
	return !PTR(t) && !IS_INTARITH(t) && IS_ARITH(t);
}

/* Anything floating that is still not generated inline */
static unsigned fpcall(const char *base, unsigned t)
{
	char buf[16];

	sprintf(buf, "%s%c", base, isdouble(t) ? 'd' : 'f');
	libcall(buf);
	return 1;
}

/*
 *	Binary operators. Signedness is decided by the left operand's
 *	type, as the tree has already applied the usual conversions.
 */
static unsigned binop(struct node *n)
{
	unsigned t = n->left ? n->left->type : n->type;
	unsigned u = (PTR(t) || (t & UNSIGNED));
	/* Operand width decides which family of opcodes to use. The 64-bit
	   forms do not truncate; the 32-bit ones do. */
	unsigned w = typesize(t);

	/* Floating point has its own set: see the note above binop's
	   helpers. %  & | ^ << >> are not valid on it, so they fall
	   through to the runtime call and are reported. */
	if (isfp(t)) {
		unsigned d = isdouble(t);

#define FP(od, of)	do { cbyte(d ? (od) : (of)); return 1; } while (0)

		switch (n->op) {
		case T_PLUS:	FP(BC_ADDD, BC_ADDF);
		case T_MINUS:	FP(BC_SUBD, BC_SUBF);
		case T_STAR:	FP(BC_MULD, BC_MULF);
		case T_SLASH:	FP(BC_DIVD, BC_DIVF);
		case T_EQEQ:	FP(BC_EQD, BC_EQF);
		case T_BANGEQ:	FP(BC_NED, BC_NEF);
		case T_LT:	FP(BC_LTD, BC_LTF);
		case T_GT:	FP(BC_GTD, BC_GTF);
		case T_LTEQ:	FP(BC_LED, BC_LEF);
		case T_GTEQ:	FP(BC_GED, BC_GEF);
		}
#undef FP
		return 0;
	}

#define OP(o32, o64)	do { cbyte(w == 8 ? (o64) : (o32)); return 1; } while (0)

	switch (n->op) {
	case T_PLUS:
		OP(BC_ADD, BC_ADD64);
	case T_MINUS:
		OP(BC_SUB, BC_SUB64);
	case T_STAR:
		OP(BC_MUL, BC_MUL64);
	case T_SLASH:
		if (u)
			OP(BC_DIVU, BC_DIVU64);
		OP(BC_DIVS, BC_DIVS64);
	case T_PERCENT:
		if (u)
			OP(BC_REMU, BC_REMU64);
		OP(BC_REMS, BC_REMS64);
	case T_AND:
		OP(BC_AND, BC_AND64);
	case T_OR:
		OP(BC_OR, BC_OR64);
	case T_HAT:
		OP(BC_XOR, BC_XOR64);
	case T_LTLT:
		OP(BC_SHL, BC_SHL64);
	case T_GTGT:
		if (u)
			OP(BC_SHRU, BC_SHRU64);
		OP(BC_SHRS, BC_SHRS64);
	case T_EQEQ:
		OP(BC_EQ, BC_EQ64);
	case T_BANGEQ:
		OP(BC_NE, BC_NE64);
	case T_LT:
		if (u)
			OP(BC_LTU, BC_LTU64);
		OP(BC_LTS, BC_LTS64);
	case T_GT:
		if (u)
			OP(BC_GTU, BC_GTU64);
		OP(BC_GTS, BC_GTS64);
	case T_LTEQ:
		if (u)
			OP(BC_LEU, BC_LEU64);
		OP(BC_LES, BC_LES64);
	case T_GTEQ:
		if (u)
			OP(BC_GEU, BC_GEU64);
		OP(BC_GES, BC_GES64);
	}
#undef OP
	return 0;
}

/*
 *	gen_node must never return 0. The shared make_node() falls back to
 *	helper(), which printf()s the helper name -- fine for a backend
 *	emitting assembler text, fatal for one writing a binary object,
 *	because the name lands in the middle of the code stream.
 *
 *	So anything not generated inline becomes a runtime call, using
 *	FCC's own helper names so the interpreter and the other backends
 *	agree on what each one means.
 */
static unsigned fallback(struct node *n)
{
	const char *name = NULL;
	char buf[16];

	switch (n->op) {
	case T_NULL:
		return 1;
	case T_ARGCOMMA:
		/* Structural: the arguments have already been pushed by the
		   tree walk and there is nothing to emit. Without this it
		   became a bogus runtime call between the last argument and
		   the call itself. */
		return 1;
	case T_PLUSEQ:		name = "pluseq";	break;
	case T_MINUSEQ:		name = "minuseq";	break;
	case T_STAREQ:		name = "muleq";		break;
	case T_SLASHEQ:		name = "diveq";		break;
	case T_PERCENTEQ:	name = "remeq";		break;
	case T_ANDEQ:		name = "andeq";		break;
	case T_OREQ:		name = "oreq";		break;
	case T_HATEQ:		name = "xoreq";		break;
	case T_SHLEQ:		name = "shleq";		break;
	case T_SHREQ:		name = "shreq";		break;
	case T_PLUSPLUS:	name = "postinc";	break;
	case T_MINUSMINUS:	name = "postdec";	break;
	default:
		/* Unknown, but still must not fall through to text. */
		sprintf(buf, "__op%x", n->op);
		libcall(buf);
		return 1;
	}

	/* Floating point has no width and signedness, and no runtime
	   function either yet, so name it as such rather than emitting
	   "pluseq8s" and having it run as an integer */
	if (!IS_INTARITH(n->type) && !PTR(n->type))
		return fpcall(name, n->type);

	/*
	 * Append the operand width and signedness.
	 *
	 * Without it the runtime cannot know whether it is updating a
	 * char, a short or an int, and did a 32bit read-modify-write in
	 * every case. That silently works until a carry leaves the object:
	 * "char a = 255, b = 2; a += 1;" wrapped a correctly but also
	 * incremented b. Signedness matters too, for /= %= and >>=.
	 */
	sprintf(buf, "%s%u%c", name, typesize(n->type),
		(PTR(n->type) || (n->type & UNSIGNED)) ? 'u' : 's');
	libcall(buf);
	return 1;
}

unsigned gen_node(struct node *n)
{
	cval_t v = n->value;
	unsigned nr = n->flags & NORETURN;

	/* Arguments are removed by the call, reported via T_CLEANUP */
	if (n->left && n->op != T_ARGCOMMA && n->op != T_FUNCCALL &&
	    n->op != T_CALLNAME)
		sp -= stack_size(n->left->type);

	switch (n->op) {
	case T_CONSTANT:
		emit_const(v, n->type);
		return 1;
	case T_NAME:
		emit_addr(symref(namestr(n->snum)), v);
		return 1;
	case T_LABEL:
		{
			char buf[24];
			sprintf(buf, "T%u", (unsigned)n->val2);
			emit_addr(symref(buf), v);
			return 1;
		}
	case T_NREF:
		emit_addr(symref(namestr(n->snum)), v);
		emit_load(n->type);
		return 1;
	case T_LBREF:
		{
			char buf[24];
			sprintf(buf, "T%u", (unsigned)n->val2);
			emit_addr(symref(buf), v);
			emit_load(n->type);
			return 1;
		}
	case T_LREF:
		if (nr)
			return 1;
		emit_local(v + sp);
		emit_load(n->type);
		return 1;
	case T_NSTORE:
		emit_addr(symref(namestr(n->snum)), v);
		cbyte(BC_PUSH);
		cbyte(BC_SWAP);
		emit_store(n->type);
		return 1;
	case T_LSTORE:
		if (nr)
			return 1;
		emit_local(v + sp);
		cbyte(BC_PUSH);
		cbyte(BC_SWAP);
		emit_store(n->type);
		return 1;
	case T_LOCAL:
		emit_local(v + sp);
		return 1;
	case T_ARGUMENT:
		emit_local(v + frame_len + sp);
		return 1;
	case T_DEREF:
		emit_load(n->type);
		return 1;
	case T_ARGSTRUCT:
		/* Structural only: the child left the struct's address in
		   the accumulator, which is what gen_push wants. It exists
		   to carry the length, which is read there. */
		return 1;
	case T_EQ:
		/* A whole struct or union: both sides are addresses and the
		   size came from the front end, which is the only pass that
		   can work it out */
		if (IS_STRUCT(n->type) && !PTR(n->type)) {
			cbyte(BC_COPY);
			cword(v & 0xFFFF);
			return 1;
		}
		emit_store(n->type);
		return 1;
	case T_CALLNAME:
		cbyte(BC_CALL);
		fixup(BC_SEG_CODE, codelen, symref(namestr(n->snum)));
		clong(v);
		return 1;
	case T_FUNCCALL:
		cbyte(BC_CALLA);
		return 1;
	case T_CLEANUP:
		if (v) {
			cbyte(BC_ARGS);
			cbyte(v & 0xFF);
		}
		return 1;
	/* Each of these keys off the operand width, and a double is eight
	   bytes wide, so each needs saving from the 64-bit integer form */
	case T_NEGATE:
		if (isfp(n->type)) {
			cbyte(isdouble(n->type) ? BC_NEGD : BC_NEGF);
			return 1;
		}
		cbyte(typesize(n->type) == 8 ? BC_NEG64 : BC_NEG);
		return 1;
	case T_TILDE:
		/* Not valid on a float, so no case for one */
		cbyte(typesize(n->type) == 8 ? BC_NOT64 : BC_NOT);
		return 1;
	/*
	 * These two produce an int from whatever they are given, so the
	 * node's own type is int and says nothing about the operand. Key
	 * off the operand instead: "!x" on a double was emitting the
	 * 64-bit integer form, which gets the right answer for 0.0 and the
	 * wrong one for -0.0.
	 */
	case T_BANG:
		{
			unsigned ot = n->right ? n->right->type : n->type;
			if (isfp(ot)) {
				cbyte(isdouble(ot) ? BC_LNOTD : BC_LNOTF);
				return 1;
			}
			cbyte(typesize(ot) == 8 ? BC_LNOT64 : BC_LNOT);
			return 1;
		}
	case T_BOOL:
		{
			unsigned ot = n->right ? n->right->type : n->type;
			if (isfp(ot)) {
				cbyte(isdouble(ot) ? BC_BOOLD : BC_BOOLF);
				return 1;
			}
			cbyte(typesize(ot) == 8 ? BC_BOOL64 : BC_BOOL);
			return 1;
		}
	case T_CAST:
		{
			unsigned lt = n->type;
			unsigned rt = n->right ? n->right->type : lt;
			unsigned ls, rs;
			/* A cast to void discards the value. The operand has
			   already been generated for its side effects and
			   there is nothing to convert it to. */
			if (lt == VOID)
				return 1;
			if (PTR(lt) || PTR(rt))
				return 1;
			/*
			 * A conversion with floating point on one side or
			 * both. The integer side is always the full 64-bit
			 * accumulator, so widen to 64 before converting and
			 * truncate after, which is one conversion per pair
			 * instead of one per width.
			 */
			if (isfp(lt) || isfp(rt)) {
				if (isfp(lt) && isfp(rt)) {
					if (isdouble(lt) && !isdouble(rt))
						cbyte(BC_F2D);
					else if (!isdouble(lt) && isdouble(rt))
						cbyte(BC_D2F);
					return 1;
				}
				if (isfp(lt)) {		/* integer -> float */
					unsigned u = (rt & UNSIGNED) || PTR(rt);
					rs = typesize(rt);
					if (rs == 1)
						cbyte(u ? BC_ZEXT8 : BC_SEXT8);
					else if (rs == 2)
						cbyte(u ? BC_ZEXT16 : BC_SEXT16);
					if (rs < 8)
						cbyte(u ? BC_ZEXT32 : BC_SEXT32);
					if (isdouble(lt))
						cbyte(u ? BC_U2D : BC_I2D);
					else
						cbyte(u ? BC_U2F : BC_I2F);
					return 1;
				}
				/* float -> integer */
				{
					unsigned u = (lt & UNSIGNED);
					if (isdouble(rt))
						cbyte(u ? BC_D2U : BC_D2I);
					else
						cbyte(u ? BC_F2U : BC_F2I);
					ls = typesize(lt);
					if (ls < 8)
						cbyte(BC_TRUNC64);
					/* And down to the target's own width, for
					   the reason the integer path below gives */
					if (ls == 1)
						cbyte(u ? BC_ZEXT8 : BC_SEXT8);
					else if (ls == 2)
						cbyte(u ? BC_ZEXT16 : BC_SEXT16);
					return 1;
				}
			}
			ls = typesize(lt);
			rs = typesize(rt);
			/* Widening to or narrowing from 64 bits. */
			if (ls == 8 && rs < 8) {
				/* bring the value up to 32 first if it came
				   from a narrower object */
				if (rs == 1)
					cbyte((rt & UNSIGNED) ? BC_ZEXT8 : BC_SEXT8);
				else if (rs == 2)
					cbyte((rt & UNSIGNED) ? BC_ZEXT16 : BC_SEXT16);
				cbyte((rt & UNSIGNED) || PTR(rt) ? BC_ZEXT32
								 : BC_SEXT32);
				return 1;
			}
			if (ls < 8 && rs == 8)
				cbyte(BC_TRUNC64);

			if (ls >= 4 && ls < 8 && rs == 1)
				cbyte((rt & UNSIGNED) ? BC_ZEXT8 : BC_SEXT8);
			else if (ls >= 4 && ls < 8 && rs == 2)
				cbyte((rt & UNSIGNED) ? BC_ZEXT16 : BC_SEXT16);
			/*
			 * Narrowing is not free. It is tempting to leave it to
			 * the store, which does truncate - but a narrowed value
			 * can be used directly, and then nothing reduces it:
			 * "(int)(signed char)200" gave 200 instead of -56.
			 * Force the value into the target's width and
			 * signedness here.
			 */
			else if (ls == 1)
				cbyte((lt & UNSIGNED) ? BC_ZEXT8 : BC_SEXT8);
			else if (ls == 2)
				cbyte((lt & UNSIGNED) ? BC_ZEXT16 : BC_SEXT16);
			return 1;
		}
	}
	if (binop(n))
		return 1;
	return fallback(n);
}
