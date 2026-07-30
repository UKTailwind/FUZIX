/*
 *	backend-thumb.c - the Thumb-2 side of mixed mode.
 *
 *	Included from the end of backend-bcode.c: one translation unit,
 *	because everything here works on the bytecode emitter's own
 *	tables - codebuf, the label and patch tables, symbols, fixups.
 *
 *	Strategy: the bytecode emitter runs unchanged for every function.
 *	At gen_epilogue the function's just-emitted bytecode span is
 *	translated opcode by opcode into Thumb-2; if every opcode in the
 *	span is covered, the native code is appended to the code segment
 *	behind a BC_NATIVE marker and the function's symbol is moved onto
 *	it, so every caller finds it through the ordinary fixup path.
 *	Anything uncovered just bails and the function stays bytecode -
 *	its span is already there.  The bytecode is fully explicit
 *	(widths, offsets, label references still symbolic in patchtab),
 *	which makes it a better translation source than a second tree
 *	walker that would have to agree with the first about every detail
 *	of stack tracking.
 *
 *	The dead bytecode of a committed function stays in the object:
 *	space, not correctness - nothing refers to it once the symbol
 *	moves.  h_entry never moves, so main always runs as bytecode;
 *	that costs nothing until calls go native in stage 5.  Reclaim is
 *	a later stage.
 *
 *	Register file (bytecode.h): r0/r1 = A, r4 = VM stack pointer as
 *	a native pointer, r5 = helper vector, r6 = mem[] base; r2/r3
 *	scratch.  Flags are dead between bytecode ops, so flag-setting
 *	16-bit forms are used freely.  Loader-patched addresses live in a
 *	per-function literal pool read with LDR pc-relative - the
 *	object's add-32-bit-symbol fixups cannot patch a movw/movt pair,
 *	and this way the loader needs no new fixup kind.
 *
 *	Coverage grows checkpoint by checkpoint, each verified on the
 *	hardware (see PLAN-arm-backend.md):
 *	  CP-A  frame, constants, unconditional jumps, return
 *	  CP-B  stack ops, locals/args, loads and stores      (pending)
 *	  CP-C  32-bit ALU, inline compound-assign helpers    (pending)
 *	  CP-D  compares, conditional jumps, casts            (pending)
 */

#ifdef BIG_TABLES
#define TMAX	65536
#else
#define TMAX	8192
#endif
#define TPOOLMAX 128

static unsigned char tbuf[TMAX];
static unsigned tlen;
static unsigned short tmap[TMAX];	/* bc offset in span -> native */

static struct tpool {
	unsigned sym;
	unsigned long addend;
	unsigned site;			/* toff of the LDR.W first halfword */
} tpooltab[TPOOLMAX];
static unsigned ntpool;

static unsigned long fn_start;
static unsigned fn_sym;
static unsigned fn_patch_lo;
static int t_dry;

static int thumb_enabled(void)
{
	static int cached = -1;
	if (cached < 0)
		cached = getenv("BCODE_ONLY") ? 0 : 1;
	return cached;
}

/* ---- raw emission --------------------------------------------------- */

static void t16(unsigned v)
{
	if (!t_dry && tlen + 2 <= TMAX) {
		tbuf[tlen] = v & 0xFF;
		tbuf[tlen + 1] = (v >> 8) & 0xFF;
	}
	tlen += 2;
}

static void t32(unsigned hw1, unsigned hw2)
{
	t16(hw1);
	t16(hw2);
}

/* ---- instruction builders ------------------------------------------- */

/* movw/movt rd, #imm16 */
static void t_mov16(unsigned top, unsigned rd, unsigned imm16)
{
	unsigned i = (imm16 >> 11) & 1;
	unsigned imm4 = (imm16 >> 12) & 0xF;
	unsigned imm3 = (imm16 >> 8) & 7;
	unsigned imm8 = imm16 & 0xFF;
	t32((top ? 0xF2C0 : 0xF240) | (i << 10) | imm4,
	    (imm3 << 12) | (rd << 8) | imm8);
}

/* A = constant, shortest form */
static void t_const(unsigned long v)
{
	if (v < 256) {
		t16(0x2000 | (v & 0xFF));	/* movs r0, #v */
		return;
	}
	t_mov16(0, 0, v & 0xFFFF);
	if ((v >> 16) & 0xFFFF)
		t_mov16(1, 0, (v >> 16) & 0xFFFF);
}

/* add/sub rd, rn, #imm12 (T4 ADDW/SUBW, no flags) */
static void t_addsubw(unsigned sub, unsigned rd, unsigned rn, unsigned imm12)
{
	unsigned i = (imm12 >> 11) & 1;
	unsigned imm3 = (imm12 >> 8) & 7;
	unsigned imm8 = imm12 & 0xFF;
	t32((sub ? 0xF2A0 : 0xF200) | (i << 10) | rn,
	    (imm3 << 12) | (rd << 8) | imm8);
}

/* b.w, offset from after the 4-byte instruction (T4, +-16MB) */
static void t_bw(long off)
{
	unsigned long u = ((unsigned long)off >> 1) & 0xFFFFFF;
	unsigned imm11 = u & 0x7FF;
	unsigned imm10 = (u >> 11) & 0x3FF;
	unsigned i2 = (u >> 21) & 1;
	unsigned i1 = (u >> 22) & 1;
	unsigned s = (off < 0);
	unsigned j1 = (!(i1 ^ s)) & 1;
	unsigned j2 = (!(i2 ^ s)) & 1;
	t32(0xF000 | (s << 10) | imm10,
	    0x9000 | (j1 << 13) | (j2 << 11) | imm11);
}

/* b<cond>.w, offset as above (T3, +-1MB) */
static void t_bcw(unsigned cond, long off)
{
	unsigned long u = ((unsigned long)off >> 1) & 0xFFFFF;
	unsigned imm11 = u & 0x7FF;
	unsigned imm6 = (u >> 11) & 0x3F;
	unsigned j1 = (u >> 17) & 1;
	unsigned j2 = (u >> 18) & 1;
	unsigned s = (off < 0);
	t32(0xF000 | (s << 10) | (cond << 6) | imm6,
	    0x8000 | (j1 << 13) | (j2 << 11) | imm11);
}

/* ---- span decoding -------------------------------------------------- */

static unsigned t_rd16(unsigned long o)
{
	return codebuf[o] | (codebuf[o + 1] << 8);
}

static unsigned long t_rd32c(unsigned long o)
{
	return codebuf[o] | ((unsigned long)codebuf[o + 1] << 8) |
	    ((unsigned long)codebuf[o + 2] << 16) |
	    ((unsigned long)codebuf[o + 3] << 24);
}

/*
 *	A jump's target, via the pending patch entry: the displacement
 *	bytes in codebuf are still zero at gen_epilogue time - they are
 *	resolved at gen_end - but the label a jump refers to is defined
 *	by the time the function's RET is emitted.
 */
static unsigned long t_target(unsigned long at)
{
	unsigned i;
	for (i = fn_patch_lo; i < npatch; i++) {
		if (patchtab[i].at == at) {
			struct label *l = &labtab[patchtab[i].lab];
			if (!l->defined)
				return ~0UL;
			return l->addr;
		}
	}
	return ~0UL;
}

/*
 *	One pass over the function's bytecode.  Dry: compute sizes and
 *	fill tmap.  Wet: emit, using the completed tmap for branch
 *	displacements.  Both passes run the same code so the sizes cannot
 *	disagree.  Returns 0 to bail - the function stays bytecode.
 */
static int t_span(unsigned long start, unsigned long end)
{
	unsigned long o = start;

	while (o < end) {
		unsigned op = codebuf[o];

		if (o - start >= TMAX || tlen > TMAX - 64)
			return 0;
		tmap[o - start] = tlen;

		switch (op) {
		case BC_NOP:
			o++;
			break;

		/* ---- CP-A: frame, constants, jumps, return ---------- */
		case BC_ENTER:
		case BC_LEAVE: {
			unsigned n = t_rd16(o + 1);
			if (n) {
				if (n > 4095)
					return 0;
				t_addsubw(op == BC_ENTER, 4, 4, n);
			}
			o += 3;
			break;
		}
		case BC_RET:
			t16(0x4770);		/* bx lr */
			o++;
			break;
		case BC_CONST8:
			t_const((unsigned long)(long)(signed char)codebuf[o + 1]
				& 0xFFFFFFFFUL);
			o += 2;
			break;
		case BC_CONST16:
			t_const((unsigned long)(long)(short)t_rd16(o + 1)
				& 0xFFFFFFFFUL);
			o += 3;
			break;
		case BC_CONST32:
			t_const(t_rd32c(o + 1));
			o += 5;
			break;
		case BC_JUMP: {
			unsigned long tgt = t_target(o + 1);
			if (tgt == ~0UL || tgt < start || tgt >= end)
				return 0;
			if (t_dry)
				t32(0, 0);
			else
				t_bw((long)tmap[tgt - start] - (long)(tlen + 4));
			o += 3;
			break;
		}

		default:
			return 0;	/* not covered yet: stay bytecode */
		}
	}
	return 1;
}

/* ---- per-function driver -------------------------------------------- */

static void thumb_fn_begin(const char *name)
{
	fn_sym = symref(name);
	fn_start = codelen;
	fn_patch_lo = npatch;
}

static void thumb_commit(void)
{
	unsigned long end = codelen;
	unsigned long marker, entry, base, pbase;
	unsigned i;

	if (!thumb_enabled())
		return;
	if (end - fn_start > TMAX)
		return;

	tlen = 0;
	ntpool = 0;
	t_dry = 1;
	if (!t_span(fn_start, end))
		return;
	/* An LDR literal reaches 4095 bytes; keep pool users well clear */
	if (ntpool && tlen + 4 * ntpool > 4000)
		return;
	if (ntpool > TPOOLMAX)
		return;

	tlen = 0;
	ntpool = 0;
	t_dry = 0;
	t_span(fn_start, end);		/* same input: cannot fail now */

	marker = codelen;
	entry = BC_NATIVE_ENTRY(marker);
	cbyte(BC_NATIVE);
	/* the bytecode alias: hosts that cannot execute Thumb interpret
	   the original span instead */
	cbyte(fn_start & 0xFF);
	cbyte((fn_start >> 8) & 0xFF);
	cbyte((fn_start >> 16) & 0xFF);
	cbyte((fn_start >> 24) & 0xFF);
	while (codelen < entry)
		cbyte(BC_NOP);
	base = codelen;
	for (i = 0; i < tlen; i++)
		cbyte(tbuf[i]);
	while (codelen & 3)
		cbyte(BC_NOP);
	pbase = codelen;
	for (i = 0; i < ntpool; i++) {
		fixup(BC_SEG_CODE, codelen, tpooltab[i].sym);
		clong(tpooltab[i].addend);
	}
	if (codelen >= CODEMAX)
		return;			/* cbyte already said "overflow" */

	/* Patch the LDR literal sites now the layout is absolute.  The
	   pc base of an LDR literal is Align4(site + 4); the code buffer
	   is loaded 8-aligned, so offsets here are the runtime truth. */
	for (i = 0; i < ntpool; i++) {
		unsigned long site = base + tpooltab[i].site;
		unsigned long imm = (pbase + 4UL * i) - ((site + 4) & ~3UL);
		codebuf[site + 2] = imm & 0xFF;
		codebuf[site + 3] = (imm >> 8) & 0xF;	/* rt = r0 */
	}

	symtab[fn_sym].s_value = marker;
	have_native = 1;
	if (getenv("THUMB_VERBOSE"))
		fprintf(stderr, "native: %s (%lu bc -> %u bytes)\n",
			bc_symname[fn_sym], end - fn_start, tlen);
}
