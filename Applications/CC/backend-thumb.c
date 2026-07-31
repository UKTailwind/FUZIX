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
 *	16-bit forms are used freely.  Loader-patched addresses are
 *	movw/movt pairs under a dedicated pair fixup (flag 2) - the
 *	plain add-32-bit-symbol fixup cannot patch the scattered imm16
 *	fields.  (Stage 5 used a pc-relative literal pool instead; its
 *	4KB LDR reach turned out to be the binding limit on real
 *	functions - the eclipse's - so the loader learned the pair kind.)
 *
 *	Coverage grows checkpoint by checkpoint, each verified on the
 *	hardware (see PLAN-arm-backend.md):
 *	  CP-A  frame, constants, unconditional jumps, return
 *	  CP-B  stack ops, locals/args, loads and stores
 *	  CP-C  32-bit ALU, inline compound-assign helpers
 *	  CP-D  compares, conditional jumps, casts
 *	  CP-E  calls, libcalls (stage 5)
 *	  CP-F  64-bit values: moves and the cheap ALU pairs  (stage 6)
 *	  CP-G  floating point and helper-routed wide ops     (stage 6)
 *
 *	Stage 6 note: a native function returning a 32-bit value leaves
 *	r1 - A's high half - undefined; the interpreter's 32-bit ops read
 *	only the low 32 bits of A by contract, so this is fine, and
 *	64-bit consumers only ever see A after CONST64/LOAD64/POP64/
 *	SEXT32/ZEXT32 established the high word.
 */

#ifdef BIG_TABLES
#define TMAX	262144
#define TPOOLMAX 8192
#else
#define TMAX	8192
#define TPOOLMAX 128
#endif

static unsigned char tbuf[TMAX];
static unsigned tlen;
static unsigned tmap[TMAX];		/* bc offset in span -> native */

/* Loader-patched sites: each is a movw/movt pair (flag-2 fixup) */
static struct tpool {
	unsigned sym;
	unsigned site;			/* toff of the movw first halfword */
} tpooltab[TPOOLMAX];
static unsigned ntpool;

static unsigned long fn_start;
static unsigned fn_sym;
static unsigned fn_patch_lo;
static unsigned fn_fix_lo;
static int t_dry;
static const char *t_bail;	/* why the last t_span gave up */
static unsigned t_bailop;

static int thumb_enabled(void)
{
	static int cached = -1;
	if (cached < 0)
		cached = getenv("BCODE_ONLY") ? 0 : 1;
	return cached;
}

/*
 *	Size policy.  A committed function keeps its dead bytecode, so
 *	code roughly triples for covered functions - fine, until one
 *	function is the eclipse's f_moon: 31K of straight-line lunar
 *	series that translates to 117K and pushes the object past what a
 *	256K Fuzix process can load.  Functions whose native span would
 *	exceed the cap stay bytecode; THUMB_MAXFN overrides (0 = no cap)
 *	for qemu experiments where the process ceiling does not apply.
 */
static unsigned long t_maxfn(void)
{
	static long cached = -1;
	if (cached < 0) {
		const char *e = getenv("THUMB_MAXFN");
		cached = e ? atol(e) : 40000;
		if (!cached)
			cached = TMAX;
	}
	return (unsigned long)cached;
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

/* rd = constant, shortest form (rd must be a low register) */
static void t_constr(unsigned rd, unsigned long v)
{
	if (v < 256) {
		t16(0x2000 | (rd << 8) | (v & 0xFF));	/* movs rd, #v */
		return;
	}
	t_mov16(0, rd, v & 0xFFFF);
	if ((v >> 16) & 0xFFFF)
		t_mov16(1, rd, (v >> 16) & 0xFFFF);
}

/* A = constant */
static void t_const(unsigned long v)
{
	t_constr(0, v);
}

/*
 *	Materialise the flags as 0/1 in r0: branch-taken lands on
 *	movs r0, #1.  The flag-setting instruction comes first; no IT
 *	blocks to get wrong.
 */
static void t_flagval(unsigned cond)
{
	t16(0xD001 | (cond << 8));	/* b<cond> +1 insn */
	t16(0x2000);			/* movs r0, #0     */
	t16(0xE000);			/* b    +0 insn    */
	t16(0x2001);			/* movs r0, #1     */
}

/*
 *	One bytecode op through the C side: bcrun's helper_op executes
 *	the fp and wide-integer arithmetic the emitter does not inline,
 *	against the same engine the interpreter uses (the DCP aeabi
 *	doubles, on the board).  A travels in r2/r3 per the AAPCS - op
 *	is r0, vsp r1 - and comes back in r0/r1 as ever.  The helper is
 *	pure: it reads the stacked operand through vsp and never moves
 *	the global sp, so the op's fixed pop count is applied to r4
 *	here, in native code.
 */
static void t_helperop(unsigned op, unsigned pops)
{
	t16(0x4602);		/* mov  r2, r0  - A low   */
	t16(0x460B);		/* mov  r3, r1  - A high  */
	t16(0x2000 | op);	/* movs r0, #op           */
	t16(0x4621);		/* mov  r1, r4  - vsp     */
	t32(0xF8D5, 0xC008);	/* ldr.w r12, [r5, #8]    */
	t16(0x47E0);		/* blx  r12               */
	if (pops)
		t16(0x3400 | pops);	/* adds r4, #pops */
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
 *	The symbol a BC_ADDR operand's fixup refers to, so the address
 *	can move to the literal pool with an identical fixup.
 */
static unsigned t_addrsym(unsigned long at, unsigned *symp)
{
	unsigned i;
	for (i = fn_fix_lo; i < nfix; i++) {
		if (fixtab[i].f_seg == BC_SEG_CODE && fixtab[i].f_offset == at) {
			*symp = fixtab[i].f_sym;
			return 1;
		}
	}
	return 0;
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
 *	The integer compound-assign helpers ("pluseq4s", "postinc1u"...)
 *	arrive as BC_LIBCALL by name; inline them - they are what every
 *	i++ in a loop compiles to.  Address on the stack, amount in A,
 *	old value loaded at the object's width, result stored back at it,
 *	A gets the new value (old for postinc/postdec) untruncated,
 *	exactly as bcrun's lib_eqop.  Returns 0 if the name is not an
 *	integer eqop of width 1, 2 or 4.
 */
static const struct teqop {
	const char *base;
	unsigned char kind;	/* 0 +, 1 -, 2 *, 3 /, 4 %, 5 &, 6 |,
				   7 ^, 8 <<, 9 >>, 10 post+, 11 post- */
} teqops[] = {
	{ "pluseq",   0 }, { "minuseq", 1 }, { "muleq",  2 },
	{ "diveq",    3 }, { "remeq",   4 }, { "andeq",  5 },
	{ "oreq",     6 }, { "xoreq",   7 }, { "shleq",  8 },
	{ "shreq",    9 }, { "postinc", 10 }, { "postdec", 11 },
	{ NULL, 0 }
};

/*
 *	Is this exactly a compound-assign helper name - base + width and
 *	signedness ("pluseq8s") or base + 'd'/'f' for the floating forms?
 *	Strict, because a library call that happens to share the prefix
 *	must not be mistaken for the one libcall family that pops a slot.
 */
static int t_eqop_name(const char *name)
{
	const struct teqop *e;
	unsigned blen;
	const char *t;

	for (e = teqops; e->base; e++) {
		blen = strlen(e->base);
		if (strncmp(name, e->base, blen) == 0) {
			t = name + blen;
			if ((t[0] == 'd' || t[0] == 'f') && !t[1])
				return 1;
			if (t[0] >= '1' && t[0] <= '8' &&
			    (t[1] == 's' || t[1] == 'u') && !t[2])
				return 1;
		}
	}
	return 0;
}

static int t_eqop(const char *name)
{
	const struct teqop *e;
	unsigned blen, sz, uns, kind, post;

	for (e = teqops; e->base; e++) {
		blen = strlen(e->base);
		if (strncmp(name, e->base, blen) == 0)
			break;
	}
	if (!e->base)
		return 0;
	if (name[blen] < '1' || name[blen] > '4' || name[blen] == '3')
		return 0;
	sz = name[blen] - '0';
	if (name[blen + 1] != 's' && name[blen + 1] != 'u')
		return 0;
	if (name[blen + 2])
		return 0;
	uns = (name[blen + 1] == 'u');
	kind = e->kind;
	post = (kind >= 10);

	t16(0x6822);			/* ldr  r2, [r4]  - the address */
	t16(0x3404);			/* adds r4, #4                  */
	if (sz == 1)
		t16(uns ? 0x5CB3 : 0x56B3);	/* ldr(s)b r3, [r6, r2] */
	else if (sz == 2)
		t16(uns ? 0x5AB3 : 0x5EB3);	/* ldr(s)h r3, [r6, r2] */
	else
		t16(0x58B3);			/* ldr     r3, [r6, r2] */

	switch (kind) {
	case 0: case 10:
		t16(0x1819);		/* adds r1, r3, r0 */
		break;
	case 1: case 11:
		t16(0x1A19);		/* subs r1, r3, r0 */
		break;
	case 2:
		t16(0x4619);		/* mov  r1, r3     */
		t16(0x4341);		/* muls r1, r0     */
		break;
	case 3:
		t32(uns ? 0xFBB3 : 0xFB93, 0xF1F0);	/* s/udiv r1,r3,r0 */
		break;
	case 4:
		t32(uns ? 0xFBB3 : 0xFB93, 0xF1F0);
		t32(0xFB01, 0x3110);	/* mls r1, r1, r0, r3 */
		break;
	case 5:
		t16(0x4619);
		t16(0x4001);		/* ands r1, r0 */
		break;
	case 6:
		t16(0x4619);
		t16(0x4301);		/* orrs r1, r0 */
		break;
	case 7:
		t16(0x4619);
		t16(0x4041);		/* eors r1, r0 */
		break;
	case 8:
		t32(0xFA03, 0xF100);	/* lsl.w r1, r3, r0 */
		break;
	case 9:
		t32(uns ? 0xFA23 : 0xFA43, 0xF100);	/* lsr/asr.w */
		break;
	}

	if (sz == 1)
		t16(0x54B1);		/* strb r1, [r6, r2] */
	else if (sz == 2)
		t16(0x52B1);		/* strh r1, [r6, r2] */
	else
		t16(0x50B1);		/* str  r1, [r6, r2] */
	t16(post ? 0x4618 : 0x4608);	/* mov r0, r3 / r1   */
	return 1;
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

	t_bail = "limits";	/* any non-opcode return 0 below */
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
			/* the preamble pushed lr on the real machine
			   stack, so calls can clobber it freely */
			t16(0xBD00);		/* pop {pc} */
			o++;
			break;
		case BC_ARGS: {
			unsigned n = codebuf[o + 1];
			if (n)
				t16(0x3400 | n);	/* adds r4, #n */
			o += 2;
			break;
		}
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

		/* ---- CP-B: stack, locals, loads, stores, addresses -- */
		case BC_PUSH:
			t16(0x3C04);		/* subs r4, #4      */
			t16(0x6020);		/* str  r0, [r4]    */
			o++;
			break;
		case BC_POP:
			t16(0x6820);		/* ldr  r0, [r4]    */
			t16(0x3404);		/* adds r4, #4      */
			o++;
			break;
		case BC_DUP:
			t16(0x6822);		/* ldr  r2, [r4]    */
			t16(0x3C04);		/* subs r4, #4      */
			t16(0x6022);		/* str  r2, [r4]    */
			o++;
			break;
		case BC_SWAP:
			t16(0x6822);		/* ldr  r2, [r4]    */
			t16(0x6020);		/* str  r0, [r4]    */
			t16(0x4610);		/* mov  r0, r2      */
			o++;
			break;
		case BC_DROP:
			t16(0x3404);		/* adds r4, #4      */
			o++;
			break;
		case BC_LOCAL8:
		case BC_LOCAL16: {
			unsigned v = (op == BC_LOCAL8) ? codebuf[o + 1]
						       : t_rd16(o + 1);
			/* A = the VM offset of sp+v: the native sp is a
			   pointer, so subtract the base back out */
			t16(0x1BA0);		/* subs r0, r4, r6  */
			if (v) {
				if (v < 256)
					t16(0x3000 | v);	/* adds r0, #v */
				else if (v <= 4095)
					t_addsubw(0, 0, 0, v);
				else
					return 0;
			}
			o += (op == BC_LOCAL8) ? 2 : 3;
			break;
		}
		case BC_LOAD8S:
			t16(0x5630);		/* ldrsb r0, [r6, r0] */
			o++;
			break;
		case BC_LOAD8U:
			t16(0x5C30);		/* ldrb  r0, [r6, r0] */
			o++;
			break;
		case BC_LOAD16S:
			t16(0x5E30);		/* ldrsh r0, [r6, r0] */
			o++;
			break;
		case BC_LOAD16U:
			t16(0x5A30);		/* ldrh  r0, [r6, r0] */
			o++;
			break;
		case BC_LOAD32:
			t16(0x5830);		/* ldr   r0, [r6, r0] */
			o++;
			break;
		case BC_STORE8:
		case BC_STORE16:
		case BC_STORE32:
			t16(0x6822);		/* ldr  r2, [r4]    */
			t16(0x3404);		/* adds r4, #4      */
			if (op == BC_STORE8)
				t16(0x54B0);	/* strb r0, [r6, r2] */
			else if (op == BC_STORE16)
				t16(0x52B0);	/* strh r0, [r6, r2] */
			else
				t16(0x50B0);	/* str  r0, [r6, r2] */
			o++;
			break;
		case BC_ADDR: {
			unsigned s;
			unsigned long ad = t_rd32c(o + 1);
			if (!t_addrsym(o + 1, &s))
				return 0;
			if (ntpool >= TPOOLMAX)
				return 0;
			if (!t_dry) {
				tpooltab[ntpool].sym = s;
				tpooltab[ntpool].site = tlen;
			}
			ntpool++;
			/* movw/movt pair carrying the addend, patched by
			   the loader's pair fixup - always both halves,
			   whatever the addend, so the site is uniform */
			t_mov16(0, 0, ad & 0xFFFF);
			t_mov16(1, 0, (ad >> 16) & 0xFFFF);
			o += 5;
			break;
		}

		/* ---- CP-C: 32-bit arithmetic ------------------------ */
		/* Binary: left operand from the stack, right in A, the
		   result in A - "A = pop() OP A". */
		case BC_ADD:
		case BC_SUB:
		case BC_MUL:
		case BC_DIVS:
		case BC_DIVU:
		case BC_REMS:
		case BC_REMU:
		case BC_AND:
		case BC_OR:
		case BC_XOR:
		case BC_SHL:
		case BC_SHRS:
		case BC_SHRU:
			t16(0x6822);		/* ldr  r2, [r4] */
			t16(0x3404);		/* adds r4, #4   */
			switch (op) {
			case BC_ADD:  t16(0x1810); break;  /* adds r0,r2,r0 */
			case BC_SUB:  t16(0x1A10); break;  /* subs r0,r2,r0 */
			case BC_MUL:  t16(0x4350); break;  /* muls r0,r2    */
			case BC_DIVS: t32(0xFB92, 0xF0F0); break;
			case BC_DIVU: t32(0xFBB2, 0xF0F0); break;
			case BC_REMS:
				t32(0xFB92, 0xF3F0);	/* sdiv r3,r2,r0 */
				t32(0xFB03, 0x2010);	/* mls r0,r3,r0,r2 */
				break;
			case BC_REMU:
				t32(0xFBB2, 0xF3F0);
				t32(0xFB03, 0x2010);
				break;
			case BC_AND:  t16(0x4010); break;  /* ands r0,r2 */
			case BC_OR:   t16(0x4310); break;  /* orrs r0,r2 */
			case BC_XOR:  t16(0x4050); break;  /* eors r0,r2 */
			case BC_SHL:  t32(0xFA02, 0xF000); break;
			case BC_SHRS: t32(0xFA42, 0xF000); break;
			case BC_SHRU: t32(0xFA22, 0xF000); break;
			}
			o++;
			break;
		case BC_NEG:
			t16(0x4240);		/* rsbs r0, r0, #0 */
			o++;
			break;
		case BC_NOT:
			t16(0x43C0);		/* mvns r0, r0     */
			o++;
			break;
		case BC_SEXT8:
			t16(0xB240);		/* sxtb r0, r0     */
			o++;
			break;
		case BC_SEXT16:
			t16(0xB200);		/* sxth r0, r0     */
			o++;
			break;
		case BC_ZEXT8:
			t16(0xB2C0);		/* uxtb r0, r0     */
			o++;
			break;
		case BC_ZEXT16:
			t16(0xB280);		/* uxth r0, r0     */
			o++;
			break;
		case BC_LIBCALL: {
			unsigned s = t_rd16(o + 1);
			if (s >= nsym)
				return 0;
			if (t_eqop(bc_symname[s])) {
				o += 3;
				break;
			}
			/*
			 * An eqop that did not inline above - the 64-bit
			 * and floating forms - is the one libcall family
			 * that takes an input in A, so it goes through
			 * helper_eqop, which carries A across in r2/r3.
			 * lib_eqop also pops its address slot; that fixed
			 * one-slot pop resyncs r4 here rather than bailing
			 * the whole function as stage 5 did.
			 */
			if (t_eqop_name(bc_symname[s])) {
				t16(0x4602);	/* mov  r2, r0 - A low  */
				t16(0x460B);	/* mov  r3, r1 - A high */
				if (s < 256)
					t16(0x2000 | s);
				else
					t_mov16(0, 0, s);
				t16(0x4621);		/* mov  r1, r4   */
				t32(0xF8D5, 0xC00C);	/* ldr.w r12, [r5, #12] */
				t16(0x47E0);		/* blx  r12      */
				t16(0x3404);		/* adds r4, #4   */
				o += 3;
				break;
			}
			/* Anything else - printf, the mm runtime, libm -
			   goes through the C side, arguments all on the
			   stack.  sp syncs from r4 via the second
			   argument. */
			if (s < 256)
				t16(0x2000 | s);	/* movs r0, #s   */
			else
				t_mov16(0, 0, s);	/* movw r0, #s   */
			t16(0x4621);			/* mov  r1, r4   */
			t16(0x686B);			/* ldr  r3, [r5, #4] */
			t16(0x4798);			/* blx  r3       */
			o += 3;
			break;
		}
		/* ---- CP-E (stage 5): calls -------------------------- */
		case BC_CALL: {
			unsigned s;
			unsigned long ad = t_rd32c(o + 1);
			if (!t_addrsym(o + 1, &s))
				return 0;
			if (ntpool >= TPOOLMAX)
				return 0;
			if (!t_dry) {
				tpooltab[ntpool].sym = s;
				tpooltab[ntpool].site = tlen;
			}
			ntpool++;
			t_mov16(0, 0, ad & 0xFFFF);
			t_mov16(1, 0, (ad >> 16) & 0xFFFF);
			t16(0x4621);		/* mov  r1, r4       */
			t16(0x682B);		/* ldr  r3, [r5, #0] */
			t16(0x4798);		/* blx  r3           */
			o += 5;
			break;
		}
		case BC_CALLA:
			/* the target is already in A = r0 */
			t16(0x4621);		/* mov  r1, r4       */
			t16(0x682B);		/* ldr  r3, [r5, #0] */
			t16(0x4798);		/* blx  r3           */
			o++;
			break;

		/* ---- CP-D: compares and conditional jumps ----------- */
		case BC_EQ:  case BC_NE:
		case BC_LTS: case BC_LTU:
		case BC_GTS: case BC_GTU:
		case BC_LES: case BC_LEU:
		case BC_GES: case BC_GEU: {
			static const unsigned char cc[] = {
				0, 1, 11, 3, 12, 8, 13, 9, 10, 2
			};	/* eq ne lt lo gt hi le ls ge hs */
			t16(0x6822);		/* ldr  r2, [r4]   */
			t16(0x3404);		/* adds r4, #4     */
			t16(0x4282);		/* cmp  r2, r0     */
			t_flagval(cc[op - BC_EQ]);
			o++;
			break;
		}
		case BC_BOOL:
		case BC_LNOT:
			t16(0x2800);		/* cmp r0, #0      */
			t_flagval(op == BC_BOOL ? 1 : 0);
			o++;
			break;
		case BC_JFALSE:
		case BC_JTRUE: {
			unsigned long tgt = t_target(o + 1);
			if (tgt == ~0UL || tgt < start || tgt >= end)
				return 0;
			t16(0x2800);		/* cmp r0, #0 - A survives */
			if (t_dry)
				t32(0, 0);
			else
				t_bcw(op == BC_JFALSE ? 0 : 1,
				      (long)tmap[tgt - start] - (long)(tlen + 4));
			o += 3;
			break;
		}

		/* ---- CP-F: 64-bit values (stage 6) ------------------ */
		case BC_CONST64:
			t_constr(0, t_rd32c(o + 1));
			t_constr(1, t_rd32c(o + 5));
			o += 9;
			break;
		case BC_LOAD64:
			/* Two plain ldr, never ldrd: rd64 assembles bytes,
			   so any alignment the interpreter accepts must
			   work here too, and plain loads take unaligned */
			t16(0x1832);		/* adds r2, r6, r0   */
			t16(0x6810);		/* ldr  r0, [r2]     */
			t16(0x6851);		/* ldr  r1, [r2, #4] */
			o++;
			break;
		case BC_STORE64:
			t16(0x6822);		/* ldr  r2, [r4]     */
			t16(0x3404);		/* adds r4, #4       */
			t16(0x1992);		/* adds r2, r2, r6   */
			t16(0x6010);		/* str  r0, [r2]     */
			t16(0x6051);		/* str  r1, [r2, #4] */
			o++;
			break;
		case BC_PUSH64:
			/* low word at the lower address, as push64 does */
			t16(0x3C08);		/* subs r4, #8       */
			t16(0x6020);		/* str  r0, [r4]     */
			t16(0x6061);		/* str  r1, [r4, #4] */
			o++;
			break;
		case BC_POP64:
			t16(0x6820);		/* ldr  r0, [r4]     */
			t16(0x6861);		/* ldr  r1, [r4, #4] */
			t16(0x3408);		/* adds r4, #8       */
			o++;
			break;
		case BC_SEXT32:
			t16(0x17C1);		/* asrs r1, r0, #31  */
			o++;
			break;
		case BC_ZEXT32:
			t16(0x2100);		/* movs r1, #0       */
			o++;
			break;
		case BC_TRUNC64:
			/* nothing: 32-bit consumers read only r0 (the
			   low-32 contract) and re-widening always goes
			   through SEXT32/ZEXT32 */
			o++;
			break;

		/* 64-bit ALU with a cheap instruction-pair form; the rest
		   of the family goes through helper_op below */
		case BC_ADD64:
		case BC_SUB64:
		case BC_AND64:
		case BC_OR64:
		case BC_XOR64:
			t16(0x6822);		/* ldr  r2, [r4]     */
			t16(0x6863);		/* ldr  r3, [r4, #4] */
			t16(0x3408);		/* adds r4, #8       */
			switch (op) {
			case BC_ADD64:
				t16(0x1810);	/* adds r0, r2, r0 */
				t16(0x4159);	/* adcs r1, r3     */
				break;
			case BC_SUB64:
				t16(0x1A10);	/* subs r0, r2, r0 */
				t16(0x418B);	/* sbcs r3, r1     */
				t16(0x4619);	/* mov  r1, r3     */
				break;
			case BC_AND64:
				t16(0x4010);	/* ands r0, r2     */
				t16(0x4019);	/* ands r1, r3     */
				break;
			case BC_OR64:
				t16(0x4310);	/* orrs r0, r2     */
				t16(0x4319);	/* orrs r1, r3     */
				break;
			case BC_XOR64:
				t16(0x4050);	/* eors r0, r2     */
				t16(0x4059);	/* eors r1, r3     */
				break;
			}
			o++;
			break;
		case BC_NEG64:
			t16(0x4240);		/* rsbs r0, r0, #0   */
			t16(0x2200);		/* movs r2, #0 - flags
						   set N/Z, carry kept */
			t16(0x418A);		/* sbcs r2, r1       */
			t16(0x4611);		/* mov  r1, r2       */
			o++;
			break;
		case BC_NOT64:
			t16(0x43C0);		/* mvns r0, r0       */
			t16(0x43C9);		/* mvns r1, r1       */
			o++;
			break;
		case BC_BOOL64:
		case BC_LNOT64:
			t16(0x4602);		/* mov  r2, r0       */
			t16(0x430A);		/* orrs r2, r1       */
			t_flagval(op == BC_BOOL64 ? 1 : 0);
			o++;
			break;

		/*
		 *	64-bit compares.  subs/sbcs leaves signed lt/ge and
		 *	unsigned lo/hs true across the whole pair; gt/le
		 *	come from subtracting the other way round.  Z after
		 *	sbcs is only the high word's, so eq/ne use the
		 *	xor-orr form instead.
		 */
		case BC_EQ64:
		case BC_NE64:
			t16(0x6822);		/* ldr  r2, [r4]     */
			t16(0x6863);		/* ldr  r3, [r4, #4] */
			t16(0x3408);		/* adds r4, #8       */
			t16(0x4042);		/* eors r2, r0       */
			t16(0x404B);		/* eors r3, r1       */
			t16(0x431A);		/* orrs r2, r3       */
			t_flagval(op == BC_EQ64 ? 0 : 1);
			o++;
			break;
		case BC_LTS64: case BC_LTU64:
		case BC_GES64: case BC_GEU64:
		case BC_GTS64: case BC_GTU64:
		case BC_LES64: case BC_LEU64: {
			/* b OP A: b-A serves lt/ge/lo/hs directly; the
			   swapped A-b turns gt into lt and le into ge */
			unsigned swap = (op == BC_GTS64 || op == BC_GTU64 ||
					 op == BC_LES64 || op == BC_LEU64);
			unsigned uns = (op == BC_LTU64 || op == BC_GEU64 ||
					op == BC_GTU64 || op == BC_LEU64);
			unsigned ge = (op == BC_GES64 || op == BC_GEU64 ||
				       op == BC_LES64 || op == BC_LEU64);
			t16(0x6822);		/* ldr  r2, [r4]     */
			t16(0x6863);		/* ldr  r3, [r4, #4] */
			t16(0x3408);		/* adds r4, #8       */
			if (swap) {
				t16(0x1A80);	/* subs r0, r0, r2   */
				t16(0x4199);	/* sbcs r1, r3       */
			} else {
				t16(0x1A12);	/* subs r2, r2, r0   */
				t16(0x418B);	/* sbcs r3, r1       */
			}
			/* lt=11 ge=10 lo=3 hs=2 */
			t_flagval(uns ? (ge ? 2 : 3) : (ge ? 10 : 11));
			o++;
			break;
		}

		/* ---- CP-G: floating point ---------------------------- */
		case BC_NEGD:
			t16(0x2201);		/* movs r2, #1       */
			t16(0x07D2);		/* lsls r2, r2, #31  */
			t16(0x4051);		/* eors r1, r2       */
			o++;
			break;
		case BC_NEGF:
			t16(0x2201);		/* movs r2, #1       */
			t16(0x07D2);		/* lsls r2, r2, #31  */
			t16(0x4050);		/* eors r0, r2       */
			o++;
			break;
		/* Truthiness is a BIT TEST, never a real compare: the
		   DCP flushes denormals to zero, and the day that rule
		   was learned is documented in PLAN-arm-backend.md under
		   "Floating point engines".  (A<<1)==0 catches +-0.0
		   exactly, NaN included, on every engine. */
		case BC_BOOLD:
		case BC_LNOTD:
			t16(0x004A);		/* lsls r2, r1, #1   */
			t16(0x4302);		/* orrs r2, r0       */
			t_flagval(op == BC_BOOLD ? 1 : 0);
			o++;
			break;
		case BC_BOOLF:
		case BC_LNOTF:
			t16(0x0042);		/* lsls r2, r0, #1   */
			t_flagval(op == BC_BOOLF ? 1 : 0);
			o++;
			break;

		/* Everything else fp/wide goes through helper_op, which
		   runs the interpreter's own case body - on the board
		   that is the DCP aeabi engine, inherited for free */
		case BC_MUL64: case BC_DIVS64: case BC_DIVU64:
		case BC_REMS64: case BC_REMU64:
		case BC_SHL64: case BC_SHRS64: case BC_SHRU64:
		case BC_ADDD: case BC_SUBD: case BC_MULD: case BC_DIVD:
		case BC_EQD: case BC_NED: case BC_LTD: case BC_GTD:
		case BC_LED: case BC_GED:
			t_helperop(op, 8);
			o++;
			break;
		case BC_ADDF: case BC_SUBF: case BC_MULF: case BC_DIVF:
		case BC_EQF: case BC_NEF: case BC_LTF: case BC_GTF:
		case BC_LEF: case BC_GEF:
			t_helperop(op, 4);
			o++;
			break;
		case BC_I2D: case BC_U2D: case BC_D2I: case BC_D2U:
		case BC_I2F: case BC_U2F: case BC_F2I: case BC_F2U:
		case BC_F2D: case BC_D2F:
			t_helperop(op, 0);
			o++;
			break;

		default:
			/* not covered yet: stay bytecode */
			t_bail = "opcode";
			t_bailop = op;
			return 0;
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
	fn_fix_lo = nfix;
}

static void thumb_commit(void)
{
	unsigned long end = codelen;
	unsigned long marker, entry, base;
	unsigned i;

	if (!thumb_enabled())
		return;
	t_bail = NULL;
	t_bailop = 0;
	if (end - fn_start > TMAX) {
		t_bail = "span";
		goto bailed;
	}

	tlen = 0;
	ntpool = 0;
	t_dry = 1;
	if (!t_span(fn_start, end))
		goto bailed;
	if (tlen > t_maxfn()) {
		t_bail = "size policy";
		goto bailed;
	}

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
	/* Preamble: save lr on the real machine stack, so call sites
	   can clobber it; BC_RET translates to pop {pc}.  Before tbuf
	   rather than in it, so tmap and branch offsets are unaffected. */
	cbyte(0x00);
	cbyte(0xB5);			/* push {lr} */
	base = codelen;
	for (i = 0; i < tlen; i++)
		cbyte(tbuf[i]);
	if (codelen >= CODEMAX)
		return;			/* cbyte already said "overflow" */

	/* Address sites are movw/movt pairs carrying their addend in
	   the instruction bits; the loader's flag-2 fixup decodes, adds
	   the symbol and re-encodes.  No literal pool, no reach limit. */
	for (i = 0; i < ntpool; i++) {
		fixup(BC_SEG_CODE, base + tpooltab[i].site, tpooltab[i].sym);
		fixtab[nfix - 1].f_pad = 2;
	}

	symtab[fn_sym].s_value = marker;
	have_native = 1;
	if (getenv("THUMB_VERBOSE"))
		fprintf(stderr, "native: %s (%lu bc -> %u bytes)\n",
			bc_symname[fn_sym], end - fn_start, tlen);
	return;

bailed:
	if (getenv("THUMB_VERBOSE")) {
		fprintf(stderr, "bytecode: %s (%lu bc): %s",
			bc_symname[fn_sym], end - fn_start,
			t_bail ? t_bail : "?");
		if (t_bailop)
			fprintf(stderr, " %02x", t_bailop);
		fputc('\n', stderr);
	}
}
