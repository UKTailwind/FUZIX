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
#elif defined(ARENA_TABLES)
/* 48K of native span covers everything but f_moon-class functions,
   which the size policy excludes on the board anyway */
#define TMAX	49152
#define TPOOLMAX 2048
#else
#define TMAX	8192
#define TPOOLMAX 128
#endif

ATAB(static unsigned char tbuf[TMAX],
     static unsigned char *tbuf);
static unsigned tlen;
/* bc offset in span -> native; 16 bits suffices whenever TMAX does
   not exceed 64K, which is every build except host BIG_TABLES */
#ifdef BIG_TABLES
typedef unsigned tmap_t;
#else
typedef unsigned short tmap_t;
#endif
ATAB(static tmap_t tmap[TMAX],
     static tmap_t *tmap);

/* Loader-patched sites: each is a movw/movt pair (flag-2 fixup) */
struct tpool {
	unsigned sym;
	unsigned site;			/* toff of the movw first halfword */
};
ATAB(static struct tpool tpooltab[TPOOLMAX],
     static struct tpool *tpooltab);
static unsigned ntpool;

/* Baked libcall indices in this function's native code, for the
   compact_syms() rewrite: tbuf offset of the immediate instruction
   plus the form it took (1 = movs imm8, 2 = movw) */
struct tref {
	unsigned sym;
	unsigned site;
	unsigned char form;
};
ATAB(static struct tref treftab[TPOOLMAX],
     static struct tref *treftab);
static unsigned ntref;

/*
 *	Stage 10 peephole state.  t_targets: one bit per span byte, set
 *	during the dry pass for every in-span branch/case target - all
 *	tracking dies at a marked op, because control can arrive there
 *	with any register contents.  t_track: what the wet pass knows
 *	about the machine beyond the bytecode contract - currently,
 *	whether A still mirrors a known mem[] address (set by a store
 *	through a constant address, used to elide the reload pair).
 */
ATAB(static unsigned char t_targets[TMAX / 8],
     static unsigned char *t_targets);

/* An address the pass can reason about: a plain constant (kind 1), a
   symbol plus addend (kind 2, the BC_ADDR form globals use - the
   loader relocates both mentions identically, so symbol+addend
   equality means address equality), or a stack local (kind 3: the
   LOCAL offset normalised by the running push-depth, valid only
   within one t_epoch - locals are r4-relative, so the same offset
   names different addresses at different depths, and any op with an
   unknown stack effect starts a new epoch).  kind 0 = unknown. */
struct t_addr {
	unsigned char kind;
	unsigned sym;		/* kind 2: symbol; kind 3: epoch */
	unsigned long k;
};

static long t_seamn;		/* local-form seams fired (see THUMB_SEAMMAX) */
static long t_depth;		/* cumulative pushed bytes this epoch */
static unsigned t_epoch;
static int t_keep;	/* set by ops that maintain t_track facts */

static struct {
	int a_valid;		/* A == mem[a_key], width a_width */
	struct t_addr a_key;
	unsigned char a_width;	/* 4 or 8 */
	struct t_addr a_is;	/* what value A itself holds */
	struct t_addr slot;	/* a pushed address awaiting its store */
	long slot_depth;	/* bytes of stack above that slot */
} t_track;

/* Net stack-bytes each op pushes (positive) or pops, set by the case
   bodies; T_DUNK (the loop-top default) means "unknown effect" and
   kills slot tracking - jumps, calls, anything variable. */
#define T_DUNK 999
static long t_d;

static int t_addr_eq(const struct t_addr *x, const struct t_addr *y)
{
	return x->kind && x->kind == y->kind && x->k == y->k
	    && (x->kind == 1 || x->sym == y->sym);
}

static void t_invalidate(void)
{
	t_track.a_valid = 0;
	t_track.a_is.kind = 0;
	t_track.slot.kind = 0;
	t_epoch++;
	t_depth = 0;
}

static unsigned long fn_start;
static unsigned fn_sym;
static unsigned fn_patch_lo;
static unsigned fn_fix_lo;
static int fn_is_main;
static int t_dry;
/* the first of the three walks: builds the target bitmap, so every
   rewrite that would skip ops stands down while it runs */
static int t_collect;
static unsigned long t_base;	/* prospective code offset of this
				   function's push{lr} preamble; tbuf[0]
				   lands at t_base + 2 */
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
 *	THUMB_RECLAIM=1: a committed function's dead bytecode is not
 *	kept - the native replacement is emitted where the bytecode
 *	was, its code fixups and jump patches are dropped, and the
 *	marker carries no alias.  Mixed objects shrink to what actually
 *	runs, which is what lets a whole program fit a 256K process.
 *	The price is the fallback: no BCRUN_BYTECODE A/B and no
 *	interpreting host can run the object, so it is opt-in, meant
 *	for board builds after the gates have passed on an aliased
 *	build of the same source.  main keeps its bytecode alias
 *	regardless: h_entry is entered through the interpreter.
 */
static int thumb_reclaim(void)
{
	static int cached = -1;
	if (cached < 0)
		cached = getenv("THUMB_RECLAIM") ? 1 : 0;
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

/*
 *	THUMB_BUDGET caps the TOTAL native bytes added to the object
 *	(first-come): a mixed object must fit the target process ceiling
 *	alongside its retained bytecode.  The full-native eclipse is
 *	172K loaded against ~124K free on the board - reclaiming a
 *	committed function's dead bytecode is the stage-9 fix; until
 *	then a board build passes a budget and ships as much native as
 *	fits.  0 or unset = uncapped.
 */
static unsigned long t_spent;

static unsigned long t_budget(void)
{
	static long cached = -1;
	if (cached < 0) {
		const char *e = getenv("THUMB_BUDGET");
		cached = e ? atol(e) : 0;
	}
	return (unsigned long)cached;
}

/* THUMB_NOFUSE=1 turns push/op fusion off, for A/B without a rebuild */
static int t_nofuse(void)
{
	static int cached = -1;
	if (cached < 0)
		cached = getenv("THUMB_NOFUSE") ? 1 : 0;
	return cached;
}

/*
 *	THUMB_SKIP=name[,name...] keeps the named functions in bytecode.
 *	A debugging knob: when a whole-program build misbehaves and the
 *	budget can only bisect in commit order, this takes one function
 *	out of the middle.
 */
static int t_skipped(const char *name)
{
	const char *e = getenv("THUMB_SKIP");
	size_t n;
	if (e == NULL)
		return 0;
	n = strlen(name);
	while (*e) {
		if (strncmp(e, name, n) == 0 && (e[n] == 0 || e[n] == ','))
			return 1;
		while (*e && *e != ',')
			e++;
		if (*e)
			e++;
	}
	return 0;
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

/* Could a jump, case or call land at this bytecode offset?  Fusing a
   compare with the conditional jump that follows skips the jump's own
   tmap entry, which is only safe if nothing targets it. */
static int t_landing(unsigned long o2)
{
	unsigned i;
	for (i = 0; i < nlab; i++)
		if (labtab[i].defined && labtab[i].addr == o2)
			return 1;
	for (i = 0; i < nsym; i++)
		if (symtab[i].s_type == BC_SYM_CODE && symtab[i].s_value == o2)
			return 1;
	return 0;
}

/*
 *	Stage-8 peephole: the flags are set and `cond` is the condition
 *	for "true".  If the next bytecode op is JFALSE/JTRUE (and nothing
 *	can land on it), branch on the flags directly - no 0/1
 *	materialisation, no second compare.  Returns the offset after
 *	everything consumed; o is the compare's own offset (a 1-byte op).
 *
 *	The one place this is WRONG is the short-circuit family: && and
 *	|| (and ?:) jump to their join label with A carrying the left
 *	side's boolean - `x = (a<b) && c` reads A at the label when the
 *	branch is taken, so the 0/1 must exist.  Those - and only those -
 *	are emitted against labels with tail "L" (codegen_lr), while
 *	statement conditions use "_b"/"_t"/"_e"; an "L" target
 *	materialises as before.
 */
static void t_bcw(unsigned cond, long off);

/*
 *	Record an in-span landing site during a dry pass.  EVERY path
 *	that resolves a branch target must call this, not just the plain
 *	jump cases: a peephole that skips ops asks the bitmap whether it
 *	is allowed to, and an unmarked target is one it will swallow.
 *	t_boolbranch resolving its own fused target without marking it
 *	is what let the statement seam eat a loop back-edge - the loop
 *	then re-entered past the address push and leaked four bytes of
 *	VM stack per iteration until it ran off the top of memory.
 */
static void t_mark_target(unsigned long addr)
{
	if (t_dry && addr >= fn_start && addr - fn_start < TMAX)
		t_targets[(addr - fn_start) >> 3] |=
		    1 << ((addr - fn_start) & 7);
}

/*
 *	Push/op fusion.
 *
 *	A stack machine wraps every binary operator in a round trip
 *	through memory: PUSH stores the left operand and the operator
 *	loads it straight back, a few instructions later.  When nothing
 *	in between can disturb r2 (r2:r3 for the wide forms), the value
 *	can just stay in the register:
 *
 *	    push ; <builders> ; op   ->   subs r4,#4 ; mov r2,r0
 *	                                  <builders>
 *	                                  adds r4,#4 ; op r0,r2,r0
 *
 *	The r4 pair stays.  It costs two instructions but keeps the VM
 *	stack pointer honest, which LOCAL addressing and anything that
 *	reads the stack depend on; what actually goes is the store and
 *	the load - two memory accesses per binary operator.
 *
 *	Unlike the statement seam this skips no ops: every op is still
 *	translated, so tmap and the target bitmap stay complete and a
 *	jump into the window is the only hazard.  That one is real - the
 *	landing site would find r2 holding nothing - so the whole window
 *	is checked against t_targets, and calls are excluded because a
 *	BL clobbers r2/r3 and the callee reads the stack.
 */
static unsigned long t_fuse_at;		/* offset of the fused operator */
static unsigned char t_fuse_width;

/* A builder leaves the right operand in A and touches neither r2/r3
   nor the VM stack; the length is the op's encoded size.  Anything not
   listed here ends the window - conversions included, since in a
   version-3 object they are calls through the helper vector. */
static unsigned t_builder_len(unsigned op)
{
	switch (op) {
	case BC_NOP:
	case BC_LOAD8S: case BC_LOAD8U:
	case BC_LOAD16S: case BC_LOAD16U:
	case BC_LOAD32: case BC_LOAD64:
	case BC_SEXT8: case BC_SEXT16: case BC_SEXT32:
	case BC_ZEXT8: case BC_ZEXT16: case BC_ZEXT32:
	case BC_TRUNC64:
	case BC_NEG: case BC_NOT:
	case BC_NEG64: case BC_NOT64:
	case BC_NEGD:
		return 1;
	case BC_CONST8:
	case BC_LOCAL8:
		return 2;
	case BC_CONST16:
	case BC_LOCAL16:
		return 3;
	case BC_CONST32:
	case BC_ADDR:
		return 5;
	case BC_CONST64:
		return 9;
	}
	return 0;
}

/* Operators that can take their left operand from the parked register
   pair instead of the stack.  The 32- and 64-bit forms read it into
   r2 (r2:r3) anyway.  The double forms call the aeabi routine with the
   left in r0:r1 and the right in r2:r3 - the mirror image of the
   parked layout - so only the commutative ones qualify: calling with
   the operands the other way round gives the same answer. */
static int t_fusable_op(unsigned op, unsigned width)
{
	if (width == 4) {
		switch (op) {
		case BC_ADD: case BC_SUB: case BC_MUL:
		case BC_DIVS: case BC_DIVU: case BC_REMS: case BC_REMU:
		case BC_AND: case BC_OR: case BC_XOR:
		case BC_SHL: case BC_SHRS: case BC_SHRU:
		case BC_EQ: case BC_NE:
		case BC_LTS: case BC_LTU: case BC_GTS: case BC_GTU:
		case BC_LES: case BC_LEU: case BC_GES: case BC_GEU:
			return 1;
		}
		return 0;
	}
	switch (op) {
	case BC_ADD64: case BC_SUB64:
	case BC_AND64: case BC_OR64: case BC_XOR64:
	case BC_EQ64: case BC_NE64:
	case BC_LTS64: case BC_LTU64: case BC_GTS64: case BC_GTU64:
	case BC_LES64: case BC_LEU64: case BC_GES64: case BC_GEU64:
		return 1;
	case BC_ADDD: case BC_MULD:	/* commutative: see above */
	case BC_EQD: case BC_NED:
		return 1;
	}
	return 0;
}

/*
 *	Does the push at o begin a fusable window?  Records where the
 *	operator is, for the operator case to find.
 */
static int t_fuse_scan(unsigned long o, unsigned width,
		       unsigned long start, unsigned long end)
{
	unsigned long p = o + 1;
	unsigned n = 0;
	unsigned len;

	if (t_nofuse())
		return 0;
	while (p < end && n <= 6) {
		unsigned op = codebuf[p];
		/* a landing site anywhere in the window, operator
		   included, and the parked register is a lie */
		if (t_targets[(p - start) >> 3] & (1 << ((p - start) & 7)))
			return 0;
		if (t_fusable_op(op, width)) {
			t_fuse_at = p;
			t_fuse_width = (unsigned char)width;
			return 1;
		}
		len = t_builder_len(op);
		if (!len)
			return 0;
		p += len;
		n++;
	}
	return 0;
}

/* True when the operator at o is the one a fusable push parked for. */
static int t_fused(unsigned long o, unsigned width)
{
	if (t_fuse_at != o || t_fuse_width != width)
		return 0;
	t_fuse_at = 0;
	return 1;
}

static unsigned long t_boolbranch(unsigned long o, unsigned cond,
				  unsigned long start, unsigned long end)
{
	unsigned nxt = (o + 1 < end) ? codebuf[o + 1] : BC_NOP;

	if ((nxt == BC_JFALSE || nxt == BC_JTRUE) && !t_landing(o + 1)) {
		unsigned i;
		for (i = fn_patch_lo; i < npatch; i++) {
			if (patchtab[i].at != o + 2)
				continue;
			{
				struct label *l = &labtab[patchtab[i].lab];
				if (l->defined && strcmp(l->tail, "L") != 0 &&
				    l->addr >= start && l->addr < end) {
					unsigned c = (nxt == BC_JFALSE) ?
						(cond ^ 1) : cond;
					t_mark_target(l->addr);
					if (t_dry)
						t32(0, 0);
					else
						t_bcw(c,
						  (long)tmap[l->addr - start] -
						  (long)(tlen + 4));
					return o + 4;
				}
			}
			break;
		}
	}
	t_flagval(cond);
	return o + 1;
}

/*
 *	r0 = a library symbol index, recorded for compact_syms(): the
 *	index is baked into the instruction, not carried by a fixup.
 */
static void t_libimm(unsigned s)
{
	if (!t_dry) {
		treftab[ntref].sym = s;
		treftab[ntref].site = tlen;
		treftab[ntref].form = (s < 256) ? 1 : 2;
	}
	ntref++;
	if (s < 256)
		t16(0x2000 | s);	/* movs r0, #s */
	else
		t_mov16(0, 0, s);	/* movw r0, #s */
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
static void t_helperop(unsigned long op, unsigned pops)
{
	t16(0x4602);		/* mov  r2, r0  - A low   */
	t16(0x460B);		/* mov  r3, r1  - A high  */
	t_constr(0, op);	/* r0 = op (COPY/PUSHN
				   carry a length in the
				   high half)             */
	t16(0x4621);		/* mov  r1, r4  - vsp     */
	t32(0xF8D5, 0xC008);	/* ldr.w r12, [r5, #8]    */
	t16(0x47E0);		/* blx  r12               */
	if (pops)
		t16(0x3400 | pops);	/* adds r4, #pops */
	t_d = -(long)pops;	/* fixed stack effect (PUSHN overrides) */
}

/*
 *	Version-3 fast path: double arithmetic, compares and int64
 *	converts BL the aeabi routine (the DCP engine, on the board)
 *	through helper-vector slots 4+ instead of the helper_op round
 *	trip, whose marshalling and switch cost more than the
 *	arithmetic itself in a tight loop.  Slot order is bcrun.c's
 *	native_helpers[] - keep in step.
 */
#define NHS_DADD	4
#define NHS_DSUB	5
#define NHS_DMUL	6
#define NHS_DDIV	7
#define NHS_DCMPEQ	8
#define NHS_DCMPLT	9
#define NHS_DCMPLE	10
#define NHS_DCMPGE	11
#define NHS_DCMPGT	12
#define NHS_L2D		13
#define NHS_UL2D	14
#define NHS_D2LZ	15
#define NHS_D2ULZ	16

static unsigned t_addrsym(unsigned long at, unsigned *symp);
static unsigned t_rd16(unsigned long off);
static unsigned long t_rd32c(unsigned long off);
static void t_addsubw(unsigned sub, unsigned rd, unsigned rn,
		      unsigned imm12);

/*
 *	The commonest statement seam in compiled code:
 *	    STOREx ; <addr of next target> ; PUSH ; <addr of key> ; LOADx
 *	with key = the address the store just wrote.  A still holds the
 *	stored value, so the next target's address is pushed through r2
 *	instead of A and the whole reload disappears.  Both address
 *	forms: ADDR (globals, via the pool) and LOCALn (depth-normalised
 *	keys).  Called by the store cases AFTER emitting the store, with
 *	the slot fact still live; returns the bytecode offset to resume
 *	at (having set every tracker and t_d/t_keep), or 0 to decline.
 */
static unsigned long t_seam(unsigned long o, unsigned long start,
			    unsigned long end, int width)
{
	static int off = -1;
	unsigned wantload = (width == 8) ? BC_LOAD64 : BC_LOAD32;
	unsigned long p2, p3, p4, i2;

	if (off < 0)
		off = getenv("THUMB_NOSEAM") ? 1 : 0;
	if (off || t_collect)
		return 0;
	struct t_addr k1, k2;
	unsigned s1 = 0, s2;
	unsigned long ad1 = 0;
	unsigned v1 = 0;

	if (!t_track.slot.kind || t_track.slot_depth != 0)
		return 0;
	/* first mention: the NEXT statement's target address */
	if (o + 1 >= end)
		return 0;
	if (codebuf[o + 1] == BC_ADDR && o + 6 < end) {
		if (!t_addrsym(o + 2, &s1))
			return 0;
		ad1 = t_rd32c(o + 2);
		k1.kind = 2;
		k1.sym = s1;
		k1.k = ad1;
		p2 = o + 6;
	} else if ((codebuf[o + 1] == BC_LOCAL8 ||
		    codebuf[o + 1] == BC_LOCAL16)) {
		unsigned long l1 = (codebuf[o + 1] == BC_LOCAL8) ? 2 : 3;
		if (o + 1 + l1 >= end)
			return 0;
		v1 = (codebuf[o + 1] == BC_LOCAL8) ? codebuf[o + 2]
						   : t_rd16(o + 2);
		if (v1 > 4095)
			return 0;
		k1.kind = 3;
		k1.sym = t_epoch;
		/* computed after the store's pop: depth is 4 less */
		k1.k = v1 - (unsigned long)(t_depth - 4);
		p2 = o + 1 + l1;
	} else
		return 0;
	if (p2 >= end || codebuf[p2] != BC_PUSH)
		return 0;
	p3 = p2 + 1;
	/* second mention: must be the address the store just wrote */
	if (p3 >= end)
		return 0;
	if (codebuf[p3] == BC_ADDR && p3 + 5 < end) {
		if (!t_addrsym(p3 + 1, &s2))
			return 0;
		k2.kind = 2;
		k2.sym = s2;
		k2.k = t_rd32c(p3 + 1);
		p4 = p3 + 5;
	} else if ((codebuf[p3] == BC_LOCAL8 ||
		    codebuf[p3] == BC_LOCAL16)) {
		unsigned long l2 = (codebuf[p3] == BC_LOCAL8) ? 2 : 3;
		unsigned v2;
		if (p3 + l2 >= end)
			return 0;
		v2 = (codebuf[p3] == BC_LOCAL8) ? codebuf[p3 + 1]
						: t_rd16(p3 + 1);
		k2.kind = 3;
		k2.sym = t_epoch;
		/* store popped 4, push put 4 back: depth as at entry */
		k2.k = v2 - (unsigned long)t_depth;
		p4 = p3 + l2;
	} else
		return 0;
	if (p4 >= end || codebuf[p4] != wantload)
		return 0;
	if (!t_addr_eq(&k2, &t_track.slot))
		return 0;
	for (i2 = o + 1; i2 <= p4; i2++)
		if (t_targets[(i2 - start) >> 3] & (1 << ((i2 - start) & 7)))
			return 0;
	if (k1.kind == 3) {
		static long smax = -2;
		if (smax == -2) {
			char *e = getenv("THUMB_SEAMMAX");
			smax = e ? atol(e) : -1;
		}
		if (smax >= 0) {
			if (t_seamn >= smax)
				return 0;
			t_seamn++;
		}
		if (!t_dry && getenv("THUMB_SEAMDBG")) {
			unsigned long q;
			fprintf(stderr, "seam @%lu k1=(%u,%lu) "
				"k2=(%u,%lu) slot=(%u,%lu) dep=%ld w=%d:",
				o, k1.kind, (unsigned long)k1.k,
				k2.kind, (unsigned long)k2.k,
				t_track.slot.kind,
				(unsigned long)t_track.slot.k,
				t_depth, width);
			for (q = o; q <= p4 && q < o + 16; q++)
				fprintf(stderr, " %02x", codebuf[q]);
			fputc('\n', stderr);
		}
	}
	if (!t_dry && getenv("THUMB_SEAMDBG")) {
		unsigned long q;
		fprintf(stderr, "seam %s @%lu tl=%u k1k=%u k2k=%u w=%d:",
			bc_symname[fn_sym], o, tlen, k1.kind, k2.kind, width);
		for (q = o; q <= p4 && q < o + 16; q++)
			fprintf(stderr, " %02x", codebuf[q]);
		fputc(0x0a, stderr);
	}
	/* commit: push the next target's address through r2 */
	if (k1.kind == 2) {
		if (ntpool >= TPOOLMAX)
			return 0;
		if (!t_dry) {
			tpooltab[ntpool].sym = s1;
			tpooltab[ntpool].site = tlen;
		}
		ntpool++;
		t_mov16(0, 2, ad1 & 0xFFFF);
		t_mov16(1, 2, (ad1 >> 16) & 0xFFFF);
	} else {
		t16(0x1BA2);		/* subs r2, r4, r6 */
		if (v1) {
			if (v1 < 256)
				t16(0x3200 | v1);	/* adds r2, #v1 */
			else
				t_addsubw(0, 2, 2, v1);
		}
	}
	t16(0x3C04);			/* subs r4, #4   */
	t16(0x6022);			/* str  r2, [r4] */
	for (i2 = o + 1; i2 <= p4; i2++)
		tmap[i2 - start] = tlen;
	/* A: still the stored value, and a mirror of key; the fresh
	   slot is the next target awaiting its own store */
	t_track.a_valid = 1;
	t_track.a_key = t_track.slot;
	t_track.a_width = (unsigned char)width;
	t_track.a_is.kind = 0;
	t_track.slot = k1;
	t_track.slot_depth = 0;
	t_d = 0;
	t_keep = 1;
	return p4 + 1;
}

/* stacked OP A: stacked operand -> left (r0/r1), A -> right (r2/r3),
   pop 8.  Result (value or 0/1 flag) comes back in r0/r1; compares
   leave r1 as garbage, which the low-32 contract permits. */
/*
 *	fused = the left operand is already parked in r2:r3 and A holds
 *	the right, which is the aeabi argument order reversed.  Only
 *	commutative operators are ever fused (t_fusable_op), so the call
 *	goes out as it stands and four instructions disappear.
 */
static void t_dcpbin(unsigned slot, int fused)
{
	if (!fused) {
		t16(0x4602);	/* mov  r2, r0  - A low  -> right */
		t16(0x460B);	/* mov  r3, r1  - A high          */
		t16(0x6820);	/* ldr  r0, [r4, #0] - left low   */
		t16(0x6861);	/* ldr  r1, [r4, #4] - left high  */
	}
	t16(0x3408);		/* adds r4, #8  - pop the operand */
	t32(0xF8D5, 0xC000 | (slot * 4));	/* ldr.w r12, [r5, #] */
	t16(0x47E0);		/* blx  r12                       */
	t_d = -8;
}

/* conversion: A is already the argument in r0/r1 */
static void t_dcpconv(unsigned slot)
{
	t32(0xF8D5, 0xC000 | (slot * 4));	/* ldr.w r12, [r5, #] */
	t16(0x47E0);		/* blx  r12                       */
	t_d = 0;
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

/* bl, same encoding with the link bit */
static void t_blw(long off)
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
	    0xD000 | (j1 << 13) | (j2 << 11) | imm11);
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

static unsigned long t_lit32(unsigned long o)
{
	return litbuf[o] | ((unsigned long)litbuf[o + 1] << 8) |
	    ((unsigned long)litbuf[o + 2] << 16) |
	    ((unsigned long)litbuf[o + 3] << 24);
}

/*
 *	A switch-table entry's target: the fixup at that literal offset
 *	names a case symbol whose value is a bytecode offset in the
 *	current span.  ~0 = not found / not usable.
 */
static unsigned long t_case_target(unsigned long off)
{
	unsigned i;
	for (i = fn_fix_lo; i < nfix; i++) {
		if (fixtab[i].f_seg == BC_SEG_DATA && fix_in_lit[i] &&
		    fixtab[i].f_offset == off) {
			unsigned s = fixtab[i].f_sym;
			unsigned long tgt;
			if (symtab[s].s_type != BC_SYM_CODE)
				return ~0UL;
			tgt = symtab[s].s_value + t_lit32(off);
			/* case labels are landing sites too */
			t_mark_target(tgt);
			return tgt;
		}
	}
	return ~0UL;
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
			/* dry pass: remember every in-span landing site
			   for the peephole pass (see t_targets) */
			t_mark_target(l->addr);
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
	t_invalidate();
	t_depth = 0;
	t_epoch = 0;	/* every walk must evolve identically */
	t_seamn = 0;
	t_fuse_at = 0;	/* no push is parked across a walk */
	while (o < end) {
		unsigned op = codebuf[o];

		if (o - start >= TMAX || tlen > TMAX - 64)
			return 0;
		tmap[o - start] = tlen;

		/* control can land here with anything in the registers:
		   forget everything.  The bitmap is complete before any
		   sizing pass runs (the collect walk), so every pass
		   takes identical decisions and sizes agree. */
		if (t_targets[(o - start) >> 3] & (1 << ((o - start) & 7)))
			t_invalidate();
		t_d = T_DUNK;

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
		case BC_CONST8: {
			unsigned long k =
			    (unsigned long)(long)(signed char)codebuf[o + 1]
			    & 0xFFFFFFFFUL;
			t_const(k);
			t_track.a_is.kind = 1;
			t_track.a_is.k = k;
			t_track.a_valid = 0;
			t_keep = 1;
			t_d = 0;
			o += 2;
			break;
		}
		case BC_CONST16: {
			unsigned long k =
			    (unsigned long)(long)(short)t_rd16(o + 1)
			    & 0xFFFFFFFFUL;
			t_const(k);
			t_track.a_is.kind = 1;
			t_track.a_is.k = k;
			t_track.a_valid = 0;
			t_keep = 1;
			t_d = 0;
			o += 3;
			break;
		}
		case BC_CONST32: {
			unsigned long k = t_rd32c(o + 1);
			struct t_addr me;
			me.kind = 1;
			me.sym = 0;
			me.k = k;
			/* Elide "CONST32 addr; LOADxx" when A already
			   mirrors mem[addr]: the store that set a_valid
			   left the value in A.  Only when no branch can
			   land on the load (bitmap), and the width the
			   store established matches the load. */
			if (t_track.a_valid && o + 5 < end
			    && t_addr_eq(&me, &t_track.a_key)
			    && !(t_targets[(o + 5 - start) >> 3] &
				 (1 << ((o + 5 - start) & 7)))
			    && ((t_track.a_width == 4 &&
				 codebuf[o + 5] == BC_LOAD32) ||
				(t_track.a_width == 8 &&
				 codebuf[o + 5] == BC_LOAD64))) {
				tmap[o + 5 - start] = tlen;
				t_track.a_is.kind = 0;
				t_keep = 1;	/* a_valid survives */
				t_d = 0;
				o += 6;
				break;
			}
			t_const(k);
			t_track.a_is = me;
			t_track.a_valid = 0;
			t_keep = 1;
			t_d = 0;
			o += 5;
			break;
		}
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
			if (t_fuse_scan(o, 4, start, end)) {
				t16(0x3C04);	/* subs r4, #4      */
				t16(0x4602);	/* mov  r2, r0      */
				t_d = 4;
				t_keep = 1;
				t_track.slot.kind = 0;
				o++;
				break;
			}
			t16(0x3C04);		/* subs r4, #4      */
			t16(0x6020);		/* str  r0, [r4]    */
			/* A untouched: its facts survive.  Whatever A is
			   known to hold is now also the newest stack
			   slot - begin tracking it toward its store
			   (depth pre-compensated for this op's +4). */
			t_track.slot = t_track.a_is;
			t_track.slot_depth = -4;
			t_d = 4;
			t_keep = 1;
			o++;
			break;
		case BC_POP:
			t16(0x6820);		/* ldr  r0, [r4]    */
			t16(0x3404);		/* adds r4, #4      */
			t_d = -4;
			o++;
			break;
		case BC_DUP:
			t16(0x6822);		/* ldr  r2, [r4]    */
			t16(0x3C04);		/* subs r4, #4      */
			t16(0x6022);		/* str  r2, [r4]    */
			t_d = 4;
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
			t_d = -4;
			o++;
			break;
		case BC_LOCAL8:
		case BC_LOCAL16: {
			unsigned v = (op == BC_LOCAL8) ? codebuf[o + 1]
						       : t_rd16(o + 1);
			unsigned long sz = (op == BC_LOCAL8) ? 2 : 3;
			struct t_addr me;
			me.kind = 3;
			me.sym = t_epoch;
			me.k = v - (unsigned long)t_depth;
			/* duplicate of what A already holds? */
			if (t_addr_eq(&me, &t_track.a_is)) {
				t_keep = 1;
				t_d = 0;
				o += sz;
				break;
			}
			/* the local form of the reload elision */
			if (t_track.a_valid && o + sz < end
			    && t_addr_eq(&me, &t_track.a_key)
			    && !(t_targets[(o + sz - start) >> 3] &
				 (1 << ((o + sz - start) & 7)))
			    && ((t_track.a_width == 4 &&
				 codebuf[o + sz] == BC_LOAD32) ||
				(t_track.a_width == 8 &&
				 codebuf[o + sz] == BC_LOAD64))) {
				tmap[o + sz - start] = tlen;
				t_track.a_is.kind = 0;
				t_keep = 1;
				t_d = 0;
				o += sz + 1;
				break;
			}
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
			t_track.a_is = me;
			t_track.a_valid = 0;
			t_keep = 1;
			t_d = 0;
			o += sz;
			break;
		}
		case BC_LOAD8S:
			t16(0x5630);		/* ldrsb r0, [r6, r0] */
			t_d = 0;
			o++;
			break;
		case BC_LOAD8U:
			t16(0x5C30);		/* ldrb  r0, [r6, r0] */
			t_d = 0;
			o++;
			break;
		case BC_LOAD16S:
			t16(0x5E30);		/* ldrsh r0, [r6, r0] */
			t_d = 0;
			o++;
			break;
		case BC_LOAD16U:
			t16(0x5A30);		/* ldrh  r0, [r6, r0] */
			t_d = 0;
			o++;
			break;
		case BC_LOAD32:
			t16(0x5830);		/* ldr   r0, [r6, r0] */
			t_d = 0;
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
			if (op == BC_STORE32) {
				unsigned long nl = t_seam(o, start, end, 4);
				if (nl) {
					o = nl;
					break;
				}
			}
			/* A untouched.  A 32-bit store through a known
			   constant address makes A a mirror of that
			   memory - the seed for the reload elision.
			   Narrow stores truncate (no seed, and they may
			   break an existing mirror); a 32-bit store to
			   an UNKNOWN address keeps a 4-byte mirror (if
			   it hit a_addr it wrote A there) but can break
			   the high half of an 8-byte one. */
			if (op == BC_STORE32 && t_track.slot.kind
			    && t_track.slot_depth == 0) {
				t_track.a_valid = 1;
				t_track.a_key = t_track.slot;
				t_track.a_width = 4;
			} else if (op != BC_STORE32 ||
				   t_track.a_width == 8) {
				t_track.a_valid = 0;
			}
			t_d = -4;	/* consumes the address slot */
			t_keep = 1;
			o++;
			break;
		case BC_ADDR: {
			unsigned s;
			unsigned long ad = t_rd32c(o + 1);
			struct t_addr me;
			if (!t_addrsym(o + 1, &s))
				return 0;
			me.kind = 2;
			me.sym = s;
			me.k = ad;
			/* A already holds exactly this address: the movw/
			   movt pair (and its fixup) is a repeat.  Frequent:
			   consecutive statements about the same variable. */
			if (t_addr_eq(&me, &t_track.a_is)) {
				t_keep = 1;
				t_d = 0;
				o += 5;
				break;
			}
			/* the global-variable form of the reload elision:
			   ADDR sym; LOADxx straight after a store through
			   the same sym+addend re-reads what A still holds */
			if (t_track.a_valid && o + 5 < end
			    && t_addr_eq(&me, &t_track.a_key)
			    && !(t_targets[(o + 5 - start) >> 3] &
				 (1 << ((o + 5 - start) & 7)))
			    && ((t_track.a_width == 4 &&
				 codebuf[o + 5] == BC_LOAD32) ||
				(t_track.a_width == 8 &&
				 codebuf[o + 5] == BC_LOAD64))) {
				tmap[o + 5 - start] = tlen;
				t_track.a_is.kind = 0;
				t_keep = 1;	/* a_valid survives */
				t_d = 0;
				o += 6;
				break;
			}
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
			t_track.a_is = me;
			t_track.a_valid = 0;
			t_keep = 1;
			t_d = 0;
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
			/* left operand: parked in r2 by a fused push, or
			   still on the stack */
			if (!t_fused(o, 4))
				t16(0x6822);	/* ldr  r2, [r4] */
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
			t_d = -4;
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
			if (ntref >= TPOOLMAX)
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
				t_libimm(s);
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
			t_libimm(s);
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
			unsigned long tent = ~0UL;
			if (!t_addrsym(o + 1, &s))
				return 0;
			/*
			 *	Stage-8 peephole: a callee already known to
			 *	be native - itself, or any function this
			 *	module committed earlier - is a direct BL.
			 *	The parity slot is dead for a native callee
			 *	(only a bytecode BC_RET reads it), so the
			 *	site is subs/bl/adds: no trampoline, no
			 *	register-file reload, no global-sp traffic.
			 *	The callee returns r4 balanced (ENTER/LEAVE
			 *	and pushes are symmetric) and preserves
			 *	r4-r6 by construction.  Forced-bytecode and
			 *	x86 hosts never execute this span, so the
			 *	alias path is untouched.
			 */
			if (ad == 0) {
				if (s == fn_sym)
					tent = t_base;	/* self-recursion:
							   own preamble */
				else if (symtab[s].s_type == BC_SYM_CODE &&
					 symtab[s].s_value < codelen &&
					 codebuf[symtab[s].s_value] == BC_NATIVE)
					tent = BC_NATIVE_ENTRY(symtab[s].s_value);
			}
			if (tent != ~0UL) {
				t16(0x3C04);	/* subs r4, #4 - slot */
				if (t_dry)
					t32(0, 0);
				else
					t_blw((long)tent -
					      (long)(t_base + 2 + tlen + 4));
				t16(0x3404);	/* adds r4, #4        */
				o += 5;
				break;
			}
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
			if (!t_fused(o, 4))
				t16(0x6822);	/* ldr  r2, [r4]   */
			t16(0x3404);		/* adds r4, #4     */
			t16(0x4282);		/* cmp  r2, r0     */
			t_d = -4;	/* fall-through keeps facts; the taken
					   path lands on a marked target */
			o = t_boolbranch(o, cc[op - BC_EQ], start, end);
			break;
		}
		case BC_BOOL:
		case BC_LNOT:
			t16(0x2800);		/* cmp r0, #0      */
			t_d = 0;
			o = t_boolbranch(o, op == BC_BOOL ? 1 : 0,
					 start, end);
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
			t_d = 0;
			o += 9;
			break;
		case BC_LOAD64:
			/* Two plain ldr, never ldrd: rd64 assembles bytes,
			   so any alignment the interpreter accepts must
			   work here too, and plain loads take unaligned.
			   The address lives in r0 and the high word is
			   loaded first, so this needs no scratch register -
			   it used r2, and r2 is where push/op fusion parks
			   the left operand. */
			t16(0x1830);		/* adds r0, r6, r0   */
			t16(0x6841);		/* ldr  r1, [r0, #4] */
			t16(0x6800);		/* ldr  r0, [r0]     */
			t_d = 0;
			o++;
			break;
		case BC_STORE64:
			t16(0x6822);		/* ldr  r2, [r4]     */
			t16(0x3404);		/* adds r4, #4       */
			t16(0x1992);		/* adds r2, r2, r6   */
			t16(0x6010);		/* str  r0, [r2]     */
			t16(0x6051);		/* str  r1, [r2, #4] */
			{
				unsigned long nl = t_seam(o, start, end, 8);
				if (nl) {
					o = nl;
					break;
				}
			}
			if (t_track.slot.kind && t_track.slot_depth == 0) {
				t_track.a_valid = 1;
				t_track.a_key = t_track.slot;
				t_track.a_width = 8;
			}
			t_d = -4;	/* consumes the address slot */
			t_keep = 1;
			o++;
			break;
		case BC_PUSH64:
			if (t_fuse_scan(o, 8, start, end)) {
				t16(0x3C08);	/* subs r4, #8       */
				t16(0x4602);	/* mov  r2, r0       */
				t16(0x460B);	/* mov  r3, r1       */
				t_d = 8;
				t_keep = 1;
				t_track.slot.kind = 0;
				o++;
				break;
			}
			/* low word at the lower address, as push64 does */
			t16(0x3C08);		/* subs r4, #8       */
			t16(0x6020);		/* str  r0, [r4]     */
			t16(0x6061);		/* str  r1, [r4, #4] */
			t_d = 8;
			o++;
			break;
		case BC_POP64:
			t16(0x6820);		/* ldr  r0, [r4]     */
			t16(0x6861);		/* ldr  r1, [r4, #4] */
			t16(0x3408);		/* adds r4, #8       */
			t_d = -8;
			o++;
			break;
		case BC_SEXT32:
			t16(0x17C1);		/* asrs r1, r0, #31  */
			t_d = 0;
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
			if (!t_fused(o, 8)) {
				t16(0x6822);	/* ldr  r2, [r4]     */
				t16(0x6863);	/* ldr  r3, [r4, #4] */
			}
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
			o = t_boolbranch(o, op == BC_BOOL64 ? 1 : 0,
					 start, end);
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
			if (!t_fused(o, 8)) {
				t16(0x6822);	/* ldr  r2, [r4]     */
				t16(0x6863);	/* ldr  r3, [r4, #4] */
			}
			t16(0x3408);		/* adds r4, #8       */
			t16(0x4042);		/* eors r2, r0       */
			t16(0x404B);		/* eors r3, r1       */
			t16(0x431A);		/* orrs r2, r3       */
			o = t_boolbranch(o, op == BC_EQ64 ? 0 : 1,
					 start, end);
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
			if (!t_fused(o, 8)) {
				t16(0x6822);	/* ldr  r2, [r4]     */
				t16(0x6863);	/* ldr  r3, [r4, #4] */
			}
			t16(0x3408);		/* adds r4, #8       */
			if (swap) {
				t16(0x1A80);	/* subs r0, r0, r2   */
				t16(0x4199);	/* sbcs r1, r3       */
			} else {
				t16(0x1A12);	/* subs r2, r2, r0   */
				t16(0x418B);	/* sbcs r3, r1       */
			}
			/* lt=11 ge=10 lo=3 hs=2 */
			o = t_boolbranch(o, uns ? (ge ? 2 : 3)
					       : (ge ? 10 : 11),
					 start, end);
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
			o = t_boolbranch(o, op == BC_BOOLD ? 1 : 0,
					 start, end);
			break;
		case BC_BOOLF:
		case BC_LNOTF:
			t16(0x0042);		/* lsls r2, r0, #1   */
			o = t_boolbranch(o, op == BC_BOOLF ? 1 : 0,
					 start, end);
			break;

		/* Everything else fp/wide goes through helper_op, which
		   runs the interpreter's own case body - on the board
		   that is the DCP aeabi engine, inherited for free */
		case BC_MUL64: case BC_DIVS64: case BC_DIVU64:
		case BC_REMS64: case BC_REMU64:
		case BC_SHL64: case BC_SHRS64: case BC_SHRU64:
			t_helperop(op, 8);
			o++;
			break;

		/* ---- direct DCP arithmetic (version 3) -------------- */
		case BC_ADDD:	t_dcpbin(NHS_DADD, t_fused(o, 8));   o++; break;
		case BC_SUBD:	t_dcpbin(NHS_DSUB, 0);   o++; break;
		case BC_MULD:	t_dcpbin(NHS_DMUL, t_fused(o, 8));   o++; break;
		case BC_DIVD:	t_dcpbin(NHS_DDIV, 0);   o++; break;
		case BC_EQD:	t_dcpbin(NHS_DCMPEQ, t_fused(o, 8)); o++; break;
		case BC_NED:
			t_dcpbin(NHS_DCMPEQ, t_fused(o, 8));
			t16(0x2201);	/* movs r2, #1        */
			t16(0x4050);	/* eors r0, r2 - != is
					   !(==), NaN included */
			o++;
			break;
		case BC_LTD:	t_dcpbin(NHS_DCMPLT, 0); o++; break;
		case BC_GTD:	t_dcpbin(NHS_DCMPGT, 0); o++; break;
		case BC_LED:	t_dcpbin(NHS_DCMPLE, 0); o++; break;
		case BC_GED:	t_dcpbin(NHS_DCMPGE, 0); o++; break;
		case BC_ADDF: case BC_SUBF: case BC_MULF: case BC_DIVF:
		case BC_EQF: case BC_NEF: case BC_LTF: case BC_GTF:
		case BC_LEF: case BC_GEF:
			t_helperop(op, 4);
			o++;
			break;
		case BC_I2D:	t_dcpconv(NHS_L2D);   o++; break;
		case BC_U2D:	t_dcpconv(NHS_UL2D);  o++; break;
		case BC_D2I:	t_dcpconv(NHS_D2LZ);  o++; break;
		case BC_D2U:	t_dcpconv(NHS_D2ULZ); o++; break;
		case BC_I2F: case BC_U2F: case BC_F2I: case BC_F2U:
		case BC_F2D: case BC_D2F:
			t_helperop(op, 0);
			o++;
			break;

		/* ---- CP-H (stage 7): aggregates and switch ---------- */
		case BC_COPY:
			/* length rides in the op word's high half */
			t_helperop(op | ((unsigned long)t_rd16(o + 1) << 16), 4);
			o += 3;
			break;
		case BC_PUSHN: {
			unsigned len = t_rd16(o + 1);
			unsigned n = (len < 4) ? 4 : ((len + 3) & ~3U);
			if (n > 4095)
				return 0;
			t_helperop(op | ((unsigned long)len << 16), 0);
			/* the helper wrote below vsp; take the slots */
			if (n < 256)
				t16(0x3C00 | n);	/* subs r4, #n */
			else
				t_addsubw(1, 4, 4, n);	/* sub.w r4, #n */
			t_d = T_DUNK;	/* variable */
			o += 3;
			break;
		}
		case BC_SWITCH: {
			/*
			 *	The whole table is known at translate time:
			 *	the operand's fixup names the table symbol
			 *	(in the literal segment), each entry's target
			 *	fixup names a case symbol whose value is a
			 *	bytecode offset in this very span.  So the
			 *	native form is a compare chain - no table,
			 *	no fixups, no runtime walk.  The dead
			 *	bytecode keeps the operand fixup for the
			 *	alias path.
			 */
			unsigned s;
			unsigned long tab, cnt, i, tgt;
			if (!t_addrsym(o + 1, &s))
				return 0;
			if (symtab[s].s_type != BC_SYM_DATA || !sym_in_lit[s])
				return 0;
			tab = symtab[s].s_value + t_rd32c(o + 1);
			if (tab + 4 > litlen)
				return 0;
			cnt = t_lit32(tab);
			if (tab + 8 + 8 * cnt > litlen)
				return 0;
			for (i = 0; i < cnt; i++) {
				tgt = t_case_target(tab + 8 + 8 * i);
				if (tgt == ~0UL || tgt < start || tgt >= end)
					return 0;
				t_constr(2, t_lit32(tab + 4 + 8 * i));
				t16(0x4290);	/* cmp r0, r2 */
				if (t_dry)
					t32(0, 0);
				else
					t_bcw(0, (long)tmap[tgt - start] -
						 (long)(tlen + 4));
			}
			tgt = t_case_target(tab + 4 + 8 * cnt);
			if (tgt == ~0UL || tgt < start || tgt >= end)
				return 0;
			if (t_dry)
				t32(0, 0);
			else
				t_bw((long)tmap[tgt - start] -
				     (long)(tlen + 4));
			o += 5;
			break;
		}

		default:
			/* not covered yet: stay bytecode */
			t_bail = "opcode";
			t_bailop = op;
			return 0;
		}
		/* The slot survives any run of ops with a declared stack
		   effect; an op of unknown effect, or one that pops
		   through it, kills it.  A-register facts are stricter:
		   only ops that explicitly maintain them keep them.
		   Sound by default either way - a new op cannot
		   silently inherit stale facts. */
		if (t_d == T_DUNK) {
			/* unknown stack effect: local keys from before
			   this point must never compare equal again */
			t_epoch++;
			t_depth = 0;
			t_track.slot.kind = 0;
		} else {
			t_depth += t_d;
			if (t_track.slot.kind) {
				t_track.slot_depth += t_d;
				if (t_track.slot_depth < 0)
					t_track.slot.kind = 0;
			}
		}
		if (!t_keep) {
			t_track.a_valid = 0;
			t_track.a_is.kind = 0;
		}
		t_keep = 0;
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
	fn_is_main = (strcmp(name, "main") == 0);
}

static void thumb_commit(void)
{
	unsigned long end = codelen;
	unsigned long marker, entry, base;
	unsigned i;
	int reclaim;

	if (!thumb_enabled())
		return;
	t_bail = NULL;
	t_bailop = 0;
	if (end - fn_start > TMAX) {
		t_bail = "span";
		goto bailed;
	}
	if (t_skipped(bc_symname[fn_sym])) {
		t_bail = "skip";
		goto bailed;
	}
	reclaim = thumb_reclaim() && !fn_is_main;
	/* Where this function will land if it commits - reclaiming, on
	   top of its own bytecode; otherwise appended.  codelen is not
	   moved by translation, so both passes see the truth.  Direct
	   BLs (self and known-native callees) are encoded against it. */
	t_base = BC_NATIVE_ENTRY(reclaim ? fn_start : end);

	/* Collect walk: the target bitmap must be COMPLETE before any
	   pass whose size matters, or a backward jump's landing site is
	   unmarked during the sizing pass and marked during emission -
	   the passes would then disagree about elisions and every
	   branch offset after the divergence is garbage.

	   It runs with the skip-ahead rewrites switched off (t_collect),
	   so it visits every op in the span exactly once.  Otherwise the
	   walk that BUILDS the bitmap is itself deciding what to skip
	   from an empty bitmap, and anything a skipped op would have
	   recorded is lost. */
	memset(t_targets, 0, TMAX / 8);
	tlen = 0;
	ntpool = 0;
	ntref = 0;
	t_dry = 1;
	t_collect = 1;
	if (!t_span(fn_start, end)) {
		t_collect = 0;
		goto bailed;
	}
	t_collect = 0;

	tlen = 0;
	ntpool = 0;
	ntref = 0;
	t_dry = 1;
	if (!t_span(fn_start, end))
		goto bailed;
	if (tlen > t_maxfn()) {
		t_bail = "size policy";
		goto bailed;
	}
	if (t_budget() && t_spent + tlen + 8 > t_budget()) {
		t_bail = "budget";
		goto bailed;
	}

	{
		unsigned dry_tlen = tlen;
	tlen = 0;
	ntpool = 0;
	ntref = 0;
	t_dry = 0;
	t_span(fn_start, end);		/* same input: cannot fail now */
		if (tlen != dry_tlen)
			fprintf(stderr, "SIZE DIVERGE %s dry=%u wet=%u\n",
				bc_symname[fn_sym], dry_tlen, tlen);
	}

	if (reclaim) {
		/*
		 *	The span is dead before it is even kept: drop the
		 *	bytecode's own code fixups, jump patches and baked
		 *	libcall records, rewind, and emit the native
		 *	replacement in its place.  The wet pass above is
		 *	complete, so nothing reads the span or its fixups
		 *	after this.  Data fixups made during the function
		 *	(switch tables, initialised data) survive - only
		 *	references INTO the dead code go.
		 */
		unsigned o2;
		for (i = fn_fix_lo, o2 = fn_fix_lo; i < nfix; i++) {
			if (fixtab[i].f_seg == BC_SEG_CODE &&
			    fixtab[i].f_offset >= fn_start &&
			    fixtab[i].f_offset < end)
				continue;
			fixtab[o2] = fixtab[i];
			fix_in_lit[o2] = fix_in_lit[i];
			o2++;
		}
		nfix = o2;
		npatch = fn_patch_lo;
		for (i = o2 = 0; i < nlibref; i++) {
			if (libreftab[i].at >= fn_start &&
			    libreftab[i].at < end)
				continue;
			libreftab[o2++] = libreftab[i];
		}
		nlibref = o2;
		codelen = fn_start;
	}

	marker = codelen;
	entry = BC_NATIVE_ENTRY(marker);
	cbyte(BC_NATIVE);
	/* the bytecode alias: hosts that cannot execute Thumb interpret
	   the original span instead; a reclaimed function has none */
	if (reclaim) {
		cbyte(0xFF);
		cbyte(0xFF);
		cbyte(0xFF);
		cbyte(0xFF);
	} else {
		cbyte(fn_start & 0xFF);
		cbyte((fn_start >> 8) & 0xFF);
		cbyte((fn_start >> 16) & 0xFF);
		cbyte((fn_start >> 24) & 0xFF);
	}
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
		if (getenv("THUMB_POOLDBG"))
			fprintf(stderr, "pool %s[%u] marker %lu base %lu "
				"site %u -> @%lu sym %s "
				"bytes %02x%02x %02x%02x\n",
				bc_symname[fn_sym], i, marker, base,
				tpooltab[i].site,
				(unsigned long)fixtab[nfix - 1].f_offset,
				bc_symname[tpooltab[i].sym],
				tbuf[tpooltab[i].site],
				tbuf[tpooltab[i].site + 1],
				tbuf[tpooltab[i].site + 2],
				tbuf[tpooltab[i].site + 3]);
	}
	/* ... and this function's baked libcall indices, now absolute */
	for (i = 0; i < ntref; i++)
		librec(base + treftab[i].site, treftab[i].sym,
		       treftab[i].form);

	symtab[fn_sym].s_value = marker;
	have_native = 1;
	t_spent += tlen + 8;
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

/*
 *	Link-time half of the direct-BL peephole.  Translation can only
 *	turn a call into a direct BL when the callee committed EARLIER;
 *	most real calls are forward references, so at gen_end - every
 *	function placed - each remaining trampoline call site whose
 *	callee did go native is rewritten in place.  A call site is a
 *	pair-fixup at a movw/movt r0 followed by the exact trampoline
 *	tail (mov r1,r4 / ldr r3,[r5] / blx r3), 14 bytes; it becomes
 *	subs r4,#4 / bl callee / adds r4,#4 / 3x nop - same length, so
 *	no relayout - and the fixup is dropped so the loader cannot
 *	patch what is no longer an address pair.  (&func constants are
 *	movw/movt too, but never carry the call tail; forced-bytecode
 *	and x86 hosts never execute native spans, so the alias path is
 *	untouched either way.)
 */
static void thumb_link_calls(void)
{
	unsigned i, o2;
	unsigned linked = 0;

	if (!have_native)
		return;
	for (i = 0; i < nfix; i++) {
		unsigned long o = fixtab[i].f_offset;
		unsigned s = fixtab[i].f_sym;
		unsigned long tent, site_pc;
		long disp;
		unsigned long u;
		unsigned imm11, imm10, i1, i2, sn, j1, j2;

		if (fixtab[i].f_pad != 2 || fixtab[i].f_seg != BC_SEG_CODE)
			continue;
		if (symtab[s].s_type != BC_SYM_CODE)
			continue;
		if (symtab[s].s_value >= codelen ||
		    codebuf[symtab[s].s_value] != BC_NATIVE)
			continue;
		if (o + 14 > codelen)
			continue;
		/* movw r0 / movt r0 with any imm, then the call tail */
		if ((codebuf[o] & 0xF0) != 0x40 ||
		    (codebuf[o + 1] & 0xFB) != 0xF2 ||
		    (codebuf[o + 3] & 0x8F) != 0 ||
		    (codebuf[o + 4] & 0xF0) != 0xC0 ||
		    (codebuf[o + 5] & 0xFB) != 0xF2 ||
		    (codebuf[o + 7] & 0x8F) != 0)
			continue;
		if (codebuf[o + 8] != 0x21 || codebuf[o + 9] != 0x46 ||
		    codebuf[o + 10] != 0x2B || codebuf[o + 11] != 0x68 ||
		    codebuf[o + 12] != 0x98 || codebuf[o + 13] != 0x47)
			continue;

		tent = BC_NATIVE_ENTRY(symtab[s].s_value);
		site_pc = o + 2 + 4;	/* BL follows the subs */
		disp = (long)tent - (long)site_pc;

		codebuf[o] = 0x04;	/* subs r4, #4 */
		codebuf[o + 1] = 0x3C;
		u = ((unsigned long)disp >> 1) & 0xFFFFFF;
		imm11 = u & 0x7FF;
		imm10 = (u >> 11) & 0x3FF;
		i2 = (u >> 21) & 1;
		i1 = (u >> 22) & 1;
		sn = (disp < 0);
		j1 = (!(i1 ^ sn)) & 1;
		j2 = (!(i2 ^ sn)) & 1;
		codebuf[o + 2] = (0xF000 | (sn << 10) | imm10) & 0xFF;
		codebuf[o + 3] = (0xF000 | (sn << 10) | imm10) >> 8;
		codebuf[o + 4] = (0xD000 | (j1 << 13) | (j2 << 11) | imm11) & 0xFF;
		codebuf[o + 5] = (0xD000 | (j1 << 13) | (j2 << 11) | imm11) >> 8;
		codebuf[o + 6] = 0x04;	/* adds r4, #4 */
		codebuf[o + 7] = 0x34;
		codebuf[o + 8] = 0x00;	/* nop x3 */
		codebuf[o + 9] = 0xBF;
		codebuf[o + 10] = 0x00;
		codebuf[o + 11] = 0xBF;
		codebuf[o + 12] = 0x00;
		codebuf[o + 13] = 0xBF;

		fixtab[i].f_sym = 0xFFFF;	/* drop below */
		linked++;
	}
	if (linked) {
		for (i = o2 = 0; i < nfix; i++)
			if (!(fixtab[i].f_pad == 2 && fixtab[i].f_sym == 0xFFFF)) {
				fixtab[o2] = fixtab[i];
				fix_in_lit[o2] = fix_in_lit[i];
				o2++;
			}
		nfix = o2;
		if (getenv("THUMB_VERBOSE"))
			fprintf(stderr, "linked: %u call sites -> direct BL\n",
				linked);
	}
}

/*
 *	ARENA_TABLES: carve every large table from one PSRAM arena
 *	allocation (PC3-PSRAM-ARENA.md stage 3 - the compiler is the
 *	facility's first client).  Called from cc2's main before
 *	anything touches a table.  There is no static fallback - this
 *	table set stopped fitting a 256K process when the Thumb
 *	translator's buffers joined it - so a missing facility is a
 *	message, not a crash.
 */
#ifdef ARENA_TABLES

#include <fcntl.h>
#include <sys/ioctl.h>

#define PSRAMIOC_ALLOC	0x000A
struct psram_req {
	unsigned long len;
	unsigned long base;
};

static unsigned char *bc_arena_cursor;

/*
 *	The carve list is walked TWICE: once to measure, once to place.
 *	The size asked of the kernel is therefore the size the carves
 *	actually use, by construction.  It used to be a hand-written sum
 *	of the same terms, kept in step by hand - and it had already
 *	drifted, missing the target bitmap the peephole pass added, so
 *	the last tables carved sat outside the region the kernel had
 *	granted.
 */
static unsigned long bc_arena_need;
static int bc_arena_measuring;

void *bc_arena_carve(unsigned long n)
{
	n = (n + 7) & ~7UL;
#ifdef ARENA_MALLOC
	/* Development build: board-sized tables, but each one its own
	   allocation, so a sanitiser reports the exact table that
	   overflowed instead of the next table quietly absorbing it. */
	if (bc_arena_measuring) {
		bc_arena_need += n;
		return NULL;
	}
	return calloc(1, n ? n : 1);
#else
	if (bc_arena_measuring) {
		bc_arena_need += n;
		return NULL;
	}
	{
		void *p = bc_arena_cursor;
		bc_arena_cursor += n;
		return p;
	}
#endif
}

/* The one list of what cc2 needs; walked to measure and to place. */
static void bc_arena_carve_all(void)
{
	codebuf = bc_arena_carve(CODEMAX);
	databuf = bc_arena_carve(DATAMAX);
	litbuf = bc_arena_carve(DATAMAX);
	strtab = bc_arena_carve(STRMAX);
	symtab = bc_arena_carve(MAXSYM * sizeof(struct bc_sym));
	bc_symname = bc_arena_carve(MAXSYM * sizeof(char *));
	sym_in_lit = bc_arena_carve(MAXSYM);
	fixtab = bc_arena_carve(MAXFIX * sizeof(struct bc_fixup));
	fix_in_lit = bc_arena_carve(MAXFIX);
	labtab = bc_arena_carve(MAXLAB * sizeof(struct label));
	patchtab = bc_arena_carve(MAXFIX * sizeof(struct patch));
	libreftab = bc_arena_carve(MAXFIX * sizeof(struct libref));
	tbuf = bc_arena_carve(TMAX);
	tmap = bc_arena_carve(TMAX * sizeof(tmap_t));
	t_targets = bc_arena_carve(TMAX / 8);
	tpooltab = bc_arena_carve(TPOOLMAX * sizeof(struct tpool));
	treftab = bc_arena_carve(TPOOLMAX * sizeof(struct tref));
	/* The node pool (backend.c) carves AFTER init, out of this
	   reserve.  Counted while measuring, NOT carved while placing -
	   consuming it here left the later carve past the end of the
	   granted region, where valaddr rightly refuses I/O and cc2's
	   first read into a node came back EFAULT. */
	if (bc_arena_measuring)
		bc_arena_need += 32768;
}

void bc_arena_init(void)
{
	bc_arena_measuring = 1;
	bc_arena_need = 0;
	bc_arena_carve_all();
	bc_arena_measuring = 0;
	if (getenv("CC2_ARENA"))
		fprintf(stderr, "cc2: arena wants %lu bytes\n",
			(unsigned long)bc_arena_need);

#ifdef ARENA_MALLOC
	bc_arena_carve_all();
#else
	{
		struct psram_req rq;
		int fd = open("/dev/sys", O_RDWR);

		rq.len = bc_arena_need;
		rq.base = 0;
		if (fd < 0 || ioctl(fd, PSRAMIOC_ALLOC, &rq) < 0 || !rq.base) {
			fprintf(stderr,
				"cc2: no PSRAM arena for %lu bytes\n",
				(unsigned long)bc_arena_need);
			exit(1);
		}
		close(fd);
		bc_arena_cursor = (unsigned char *)rq.base;
		bc_arena_carve_all();
	}
#endif
}

#else

void bc_arena_init(void)
{
}

#endif
