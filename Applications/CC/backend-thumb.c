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
static unsigned fn_fix_lo;
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
			if (!t_addrsym(o + 1, &s))
				return 0;
			if (ntpool >= TPOOLMAX)
				return 0;
			if (!t_dry) {
				tpooltab[ntpool].sym = s;
				tpooltab[ntpool].addend = t_rd32c(o + 1);
				tpooltab[ntpool].site = tlen;
			}
			ntpool++;
			t32(0xF8DF, 0x0000);	/* ldr.w r0, [pc, #..] */
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
			/* An eqop name that did NOT inline (64-bit or
			   floating forms) pops its address slot inside the
			   C helper, which would desync this function's r4:
			   the whole function stays bytecode. */
			{
				const struct teqop *e;
				for (e = teqops; e->base; e++)
					if (strncmp(bc_symname[s], e->base,
						    strlen(e->base)) == 0)
						return 0;
			}
			/* Anything else - printf, the mm runtime, libm -
			   goes through the C side.  sp syncs from r4 via
			   the second argument. */
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
			if (!t_addrsym(o + 1, &s))
				return 0;
			if (ntpool >= TPOOLMAX)
				return 0;
			if (!t_dry) {
				tpooltab[ntpool].sym = s;
				tpooltab[ntpool].addend = t_rd32c(o + 1);
				tpooltab[ntpool].site = tlen;
			}
			ntpool++;
			t32(0xF8DF, 0x0000);	/* ldr.w r0, [pc, #..] */
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
		/* cmp, then a five-halfword materialise: branch-taken
		   lands on movs r0,#1.  No IT blocks to get wrong. */
		case BC_EQ:  case BC_NE:
		case BC_LTS: case BC_LTU:
		case BC_GTS: case BC_GTU:
		case BC_LES: case BC_LEU:
		case BC_GES: case BC_GEU: {
			static const unsigned char cc[] = {
				0, 1, 11, 3, 12, 8, 13, 9, 10, 2
			};	/* eq ne lt lo gt hi le ls ge hs */
			unsigned c = cc[op - BC_EQ];
			t16(0x6822);		/* ldr  r2, [r4]   */
			t16(0x3404);		/* adds r4, #4     */
			t16(0x4282);		/* cmp  r2, r0     */
			t16(0xD001 | (c << 8));	/* b<c> +1 insn    */
			t16(0x2000);		/* movs r0, #0     */
			t16(0xE000);		/* b    +0 insn    */
			t16(0x2001);		/* movs r0, #1     */
			o++;
			break;
		}
		case BC_BOOL:
		case BC_LNOT:
			t16(0x2800);		/* cmp r0, #0      */
			t16(0xD001 | ((op == BC_BOOL ? 1 : 0) << 8));
			t16(0x2000);
			t16(0xE000);
			t16(0x2001);
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
	fn_fix_lo = nfix;
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
	/* Preamble: save lr on the real machine stack, so call sites
	   can clobber it; BC_RET translates to pop {pc}.  Before tbuf
	   rather than in it, so tmap and branch offsets are unaffected. */
	cbyte(0x00);
	cbyte(0xB5);			/* push {lr} */
	base = codelen;
	for (i = 0; i < tlen; i++)
		cbyte(tbuf[i]);
	while (codelen & 3)
		cbyte(BC_NOP);
	pbase = codelen;
	for (i = 0; i < ntpool; i++) {
		fixup(BC_SEG_CODE, codelen, tpooltab[i].sym);
		/* flag 1: a value slot, not a BC_CALL operand - see the
		   loader */
		fixtab[nfix - 1].f_pad = 1;
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
