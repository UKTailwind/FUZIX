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
	long fo;		/* kind 3: FRAME offset (v - true depth),
				   the walk-global identity of the local;
				   -1 when unknown or not a local */
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
 *	interpreting host can run the object, so on the host it is
 *	opt-in, used by the gates after the aliased build of the same
 *	source has passed.  On the BOARD it is the default: the board's
 *	bcrun always executes native, and a large program that keeps
 *	its dead bytecode does not fit its own process - the eclipse
 *	compiled on the machine and then would not load.  THUMB_RECLAIM=0
 *	turns it off either side.  main keeps its bytecode alias
 *	regardless: h_entry is entered through the interpreter.
 */
static int thumb_reclaim(void)
{
	static int cached = -1;
	if (cached < 0) {
		const char *e = getenv("THUMB_RECLAIM");
#ifdef ARENA_TABLES
		cached = e ? atoi(e) : 1;
#else
		cached = e ? atoi(e) : 0;
#endif
	}
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

/* THUMB_NOR4=1 turns the direct [r4,#off] frame access off, for A/B */
static int t_nor4(void)
{
	static int cached = -1;
	if (cached < 0)
		cached = getenv("THUMB_NOR4") ? 1 : 0;
	return cached;
}

/* THUMB_NOCFOLD=1 turns constant-operand folding off, for A/B */
static int t_nocfold(void)
{
	static int cached = -1;
	if (cached < 0)
		cached = getenv("THUMB_NOCFOLD") ? 1 : 0;
	return cached;
}

/* THUMB_NORFOLD=1 turns memory right-operand folding off, for A/B */
static int t_norfold(void)
{
	static int cached = -1;
	if (cached < 0)
		cached = getenv("THUMB_NORFOLD") ? 1 : 0;
	return cached;
}

/* THUMB_NOSTRSLOT=1 keeps the string family on the dispatcher, for
   A/B - and for building objects an older (pre-version-4) bcrun can
   still load */
static int t_nostrslot(void)
{
	static int cached = -1;
	if (cached < 0)
		cached = getenv("THUMB_NOSTRSLOT") ? 1 : 0;
	return cached;
}

/* THUMB_NOICOPY=1 keeps BC_COPY on helper_op, for A/B */
static int t_noicopy(void)
{
	static int cached = -1;
	if (cached < 0)
		cached = getenv("THUMB_NOICOPY") ? 1 : 0;
	return cached;
}

/* THUMB_NORSKIP=1 keeps the r4 adjust pair in fused windows, for A/B */
static int t_norskip(void)
{
	static int cached = -1;
	if (cached < 0)
		cached = getenv("THUMB_NORSKIP") ? 1 : 0;
	return cached;
}

/* THUMB_NOREGC=1 turns register caching of hot locals off, for A/B */
static int t_noregc(void)
{
	static int cached = -1;
	if (cached < 0)
		cached = getenv("THUMB_NOREGC") ? 1 : 0;
	return cached;
}

/*
 *	THUMB_REGC8=1 enables the r8:r9 pair cache for the hottest
 *	8-byte local.  OFF by default on the evidence: the eqop
 *	inlining already banked the 64-bit counter win (the helper
 *	crossing), and what remains - mov-bridged high-register
 *	arithmetic, the wide push/pop preamble, two warm loads per
 *	call - measured board-neutral on the eclipse (2.277 -> 2.274)
 *	and slightly negative on grains (49,653 -> 49,492).  Kept
 *	correct and gated for machines or workloads where the balance
 *	differs.
 */
static int t_regc8(void)
{
	static int cached = -1;
	if (cached < 0)
		cached = getenv("THUMB_REGC8") ? 1 : 0;
	return cached;
}

/*
 *	Register caching of the hottest local (regalloc-lite).
 *
 *	One 32-bit local per function lives in r7 for the whole span:
 *	a low register with full 16-bit ALU access, untouched by the
 *	register file, and already in native_enter's clobber list, so
 *	no bcrun change and old runtimes take these objects unchanged.
 *	The function saves it (push {r7,lr} - same two bytes as the
 *	plain preamble) and every RET restores it.
 *
 *	There is no spill state: memory NEVER holds the variable after
 *	the warm load, so eligibility is everything.  The collect walk
 *	classifies every LOCAL in the span by FRAME OFFSET (v minus the
 *	true stack depth):
 *
 *	  LOCALn ; LOAD32                          a read
 *	  LOCALn ; PUSH ... STORE32 (slot intact)   a write
 *	  LOCALn ; PUSH ; <amt> ; LIBCALL eqop4     a read-modify-write
 *
 *	and ANYTHING else - narrow or 64-bit access, the address pushed
 *	as a call argument (ARGS pops through it), consumed by DUP/POP/
 *	COPY, a pending write crossing a fact-killing op or a landing
 *	site, nested pending writes (the tracker holds only the newest
 *	slot fact) - disqualifies that offset.  The chosen offset's
 *	recorded spans are re-verified against the completed target
 *	bitmap before the sizing walk, so the emitter's fast paths are
 *	guaranteed to fire; if one ever cannot, the function bails to
 *	bytecode rather than emit a stale memory access.
 *
 *	True depth: t_vdepth tracks the BYTECODE stack depth through
 *	every walk.  Where an op's t_d is physical (the elided fused
 *	windows) the two diverge transiently, but such windows cannot
 *	contain a LOCAL, so no frame offset is ever computed inside
 *	the divergence.  An op with no known delta poisons caching for
 *	the function (t_lcbad) - never correctness.
 */
#define TLC_MAX		32	/* distinct locals tracked          */
#define TLC_SPANS	256	/* recorded access spans            */
#define TLC_PEND	8	/* nested pending address pushes    */

struct tlc {
	long fo;
	unsigned short reads, eqops, writes;
	unsigned char esc;
	unsigned char w;	/* access width: 0 unset, 4 or 8; a mix
				   escapes */
};
static struct tlc t_lctab[TLC_MAX];
static unsigned t_lcn;
/* consumer annotations: the exact bytecode offsets of the STORE32s
   and eqop LIBCALLs that write a local through a pended address push.
   The emitter keys on THESE, never on tracker-slot survival - inner
   value pushes overwrite the single slot fact, but a store always
   consumes the address its own statement pushed, whatever happened
   in between. */
#define TLC_WR 64
static struct {
	unsigned long off;
	long fo;
	unsigned char eq;
} t_lcwr[TLC_WR];
static unsigned t_lcwrn;
static struct {
	unsigned long s, e;	/* [s, e) bytecode offsets          */
	long fo;
} t_lcspan[TLC_SPANS];
static unsigned t_lcspann;
static struct {
	long fo;
	long vd;		/* true depth just after the push   */
	unsigned long start;	/* offset of the LOCAL op           */
} t_lcpend[TLC_PEND];
static unsigned t_lcpendn;
/* LOCAL;PUSH adjacency: the LOCAL arms, the PUSH creates the pending
   (creating it during the LOCAL would see the by-depth sweep kill it
   before the push's delta lands) */
static long t_lc_armfo;
static unsigned long t_lc_armoff, t_lc_armstart;
static int t_lcbad;		/* this function cannot cache      */
static long t_vdepth;		/* true bytecode stack depth        */
static long t_vd;		/* this op's true delta, or T_DUNK  */
static long t_cfo = -1;		/* r7-cached 32-bit local, -1 none  */
static long t_cfo8 = -1;	/* r8:r9-cached 8-byte local        */
static unsigned t_psize = 2;	/* preamble bytes: 2, or 4 with the
				   wide push {r7, r8, r9, lr}       */

static void t_lc_setw(struct tlc *p, unsigned w)
{
	if (p->w && p->w != w)
		p->esc = 1;
	else
		p->w = (unsigned char)w;
}

static void t_lc_wr(unsigned long off, long fo, unsigned eq)
{
	if (t_lcwrn >= TLC_WR) {
		t_lcbad = 1;
		return;
	}
	t_lcwr[t_lcwrn].off = off;
	t_lcwr[t_lcwrn].fo = fo;
	t_lcwr[t_lcwrn].eq = (unsigned char)eq;
	t_lcwrn++;
}

/* the annotated consumer at this offset: its frame offset, or -1 */
static long t_lc_wr_fo(unsigned long off)
{
	unsigned i;

	for (i = 0; i < t_lcwrn; i++)
		if (t_lcwr[i].off == off)
			return t_lcwr[i].fo;
	return -1;
}

static struct tlc *t_lc_find(long fo)
{
	unsigned i;

	for (i = 0; i < t_lcn; i++)
		if (t_lctab[i].fo == fo)
			return &t_lctab[i];
	if (t_lcn >= TLC_MAX) {
		t_lcbad = 1;
		return NULL;
	}
	t_lctab[t_lcn].fo = fo;
	t_lctab[t_lcn].reads = t_lctab[t_lcn].eqops = t_lctab[t_lcn].writes = 0;
	t_lctab[t_lcn].esc = 0;
	t_lctab[t_lcn].w = 0;
	return &t_lctab[t_lcn++];
}

static void t_lc_escape(long fo)
{
	struct tlc *p = t_lc_find(fo);

	if (p)
		p->esc = 1;
}

static void t_lc_span(unsigned long s, unsigned long e, long fo)
{
	if (t_lcspann >= TLC_SPANS) {
		t_lcbad = 1;
		return;
	}
	/* s is the leading LOCAL op.  Control LANDING there is fine -
	   the rewrite starts fresh at that op (a loop back-edge onto
	   its own counter's increment is the canonical case) - so only
	   the swallowed interior is checked for landing sites. */
	t_lcspan[t_lcspann].s = s + 1;
	t_lcspan[t_lcspann].e = e;
	t_lcspan[t_lcspann].fo = fo;
	t_lcspann++;
}

/* the tracker follows only the NEWEST pushed slot, so an older
   pending write is a fact the emitter will have lost: escape it */
static void t_lc_pend_push(long fo, unsigned long start)
{
	unsigned i;

	for (i = 0; i < t_lcpendn; i++)
		t_lc_escape(t_lcpend[i].fo);
	if (t_lcpendn >= TLC_PEND) {
		t_lcbad = 1;
		t_lcpendn = 0;
	}
	t_lcpend[t_lcpendn].fo = fo;
	t_lcpend[t_lcpendn].vd = t_vdepth + 4;
	t_lcpend[t_lcpendn].start = start;
	t_lcpendn++;
}

/* the pending whose slot is on top of the stack right now, if any */
static int t_lc_pend_top(void)
{
	if (t_lcpendn && t_lcpend[t_lcpendn - 1].vd == t_vdepth)
		return (int)(t_lcpendn - 1);
	return -1;
}

static void t_lc_pend_escape_all(void)
{
	while (t_lcpendn)
		t_lc_escape(t_lcpend[--t_lcpendn].fo);
}

/* ARGS popped n bytes: any pending inside the popped range was an
   argument - its address escaped into the callee */
static void t_lc_pend_args(long n)
{
	while (t_lcpendn && t_lcpend[t_lcpendn - 1].vd > t_vdepth - n)
		t_lc_escape(t_lcpend[--t_lcpendn].fo);
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
/*
 *	P5: the r4 pair itself goes when the window contains no LOCAL -
 *	the only builder that reads r4.  Everything else in a window
 *	works purely in A, so if the push does not move r4 and the
 *	operator does not move it back, nothing in between can tell,
 *	and the virtual stack depth nets to zero across the window so
 *	every key/slot fact stays aligned with the physical r4.
 *	t_fuse_nolocal is decided by the scan (identically in every
 *	walk); t_fuse_elided carries it from the push to the operator;
 *	t_fusel is what the operator site reads after t_fused().
 */
static unsigned char t_fuse_nolocal;
static unsigned char t_fuse_elided;
static unsigned char t_fusel;

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
	case BC_NOT64:
		return 1;
	/*
	 * NOT LISTED, and they must not be: BC_NEG64 and BC_NEGD both use
	 * r2 as a scratch (movs r2,#0 / sbcs, and movs r2,#1 / lsls / eors)
	 * and r2:r3 is where push/op fusion parks the left operand.  A
	 * window containing either one hands the operator a destroyed
	 * operand.
	 *
	 * That is not theoretical.  It shipped, and it produced a compare
	 * against a unary minus that was neither equal, less than NOR
	 * greater than - the signature of garbage arriving in r2:r3 - so
	 * `If i = -p Then` silently never fired.  The hidden-line test in
	 * the ripple benchmark plotted 102 pixels instead of 19364 and the
	 * only visible symptom was a picture with most of it missing.
	 *
	 * BC_LOAD64 is in the list above because it was rewritten to load
	 * the high word first and need no scratch at all, for exactly this
	 * reason - see its comment.  Anything added here must satisfy the
	 * same rule: it may use r0 and r1, and nothing else.
	 */
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
	unsigned nolocal = 1;

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
			t_fuse_nolocal = (unsigned char)
			    (nolocal && !t_norskip());
			return 1;
		}
		if (op == BC_LOCAL8 || op == BC_LOCAL16)
			nolocal = 0;
		len = t_builder_len(op);
		if (!len)
			return 0;
		p += len;
		n++;
	}
	return 0;
}

/* True when the operator at o is the one a fusable push parked for.
   t_fusel says whether that push skipped its half of the r4 pair, so
   the operator must skip its half too. */
static int t_fused(unsigned long o, unsigned width)
{
	if (t_fuse_at != o || t_fuse_width != width) {
		t_fusel = 0;
		return 0;
	}
	t_fuse_at = 0;
	t_fusel = t_fuse_elided;
	t_fuse_elided = 0;
	return 1;
}

static unsigned long t_boolbranch(unsigned long o, unsigned cond,
				  unsigned long start, unsigned long end)
{
	unsigned long jo = o + 1;
	unsigned nxt;

	/*
	 *	The flags are set and cond is the condition for "true".
	 *	cc1 normalises truthiness with a BOOL after nearly every
	 *	comparison, so the jump this site wants sits behind one -
	 *	`x > y ; BOOL ; JFALSE` is the commonest condition shape in
	 *	compiled BASIC.  A BOOL of the 0/1 this site would
	 *	materialise is an identity, and an LNOT is a condition
	 *	flip, so chains of either are swallowed (nothing may land
	 *	inside) and the fusion looks at what follows them.  Without
	 *	this the compare paid a full 8-byte t_flagval and the BOOL
	 *	re-tested the value it had just built.
	 */
	while (jo < end && !t_landing(jo) &&
	       (codebuf[jo] == BC_BOOL || codebuf[jo] == BC_LNOT)) {
		if (codebuf[jo] == BC_LNOT)
			cond ^= 1;
		jo++;
	}
	nxt = (jo < end) ? codebuf[jo] : BC_NOP;
	if ((nxt == BC_JFALSE || nxt == BC_JTRUE) && !t_landing(jo)) {
		unsigned i;
		for (i = fn_patch_lo; i < npatch; i++) {
			if (patchtab[i].at != jo + 1)
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
					return jo + 3;
				}
			}
			break;
		}
	}
	t_flagval(cond);
	return jo;
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
/* version 4: the string family (see native_helpers in bcrun.c) */
#define NHS_STRCPY	17
#define NHS_STRCMP	18
#define NHS_STRLEN	19
#define NHS_MEMCPY	20
/* version 5: the stack guard.  21 is the floor as a value, 22 is what
   to call on hitting it. */
#define NHS_STACKFLOOR	21
#define NHS_STACKFAULT	22

/*
 *	The stack guard, emitted at the top of every translated function
 *	once its frame is taken.
 *
 *	    ldr  r2, [r5, #21*4]     the floor
 *	    cmp  r4, r2
 *	    bhs  1f
 *	    ldr  r2, [r5, #22*4]     does not return
 *	    blx  r2
 *	 1:
 *
 *	Five halfwords, and three of them on the path that is always
 *	taken.  r2 is free here: arguments arrive on the VM stack, not in
 *	registers, and r2 is caller-clobbered across every call this
 *	backend emits, so nothing can be live in it at a function's first
 *	instruction.
 *
 *	Without this, unbounded recursion in a translated function
 *	overwrites whatever is below the stack and takes the machine
 *	down.  The interpreter has had the test in BC_ENTER for a while;
 *	the trouble was that a small recursive routine is precisely what
 *	gets translated, so the guarded path was the one that could not
 *	be reached.
 */
static void t_stackguard(void)
{
	/* Encodings spelled out rather than written as final constants:
	   the first cut of this had 0x42A4 for the compare, which is
	   "cmp r4, r4" - always equal, so the branch was always taken
	   and the fault was unreachable.  It assembled, it ran, and it
	   guarded nothing. */
	t16(0x6800 | (NHS_STACKFLOOR << 6) | (5 << 3) | 2);
				/* ldr r2, [r5, #floor]  */
	t16(0x4280 | (2 << 3) | 4);
				/* cmp r4, r2            */
	t16(0xD200 | 1);	/* bhs .+4 - over the two below */
	t16(0x6800 | (NHS_STACKFAULT << 6) | (5 << 3) | 2);
				/* ldr r2, [r5, #fault]  */
	t16(0x4780 | (2 << 3));	/* blx r2 - does not return     */
}

/* The string libcalls with direct helper slots: slot and argument
   count, or -1.  Matched by name exactly like the eqop family - a
   program-defined function of the same name is a BC_CALL, never a
   BC_LIBCALL, so only true library binds arrive here. */
static int t_str_slot(const char *name, unsigned *nargs)
{
	if (!strcmp(name, "strcpy")) { *nargs = 2; return NHS_STRCPY; }
	if (!strcmp(name, "strcmp")) { *nargs = 2; return NHS_STRCMP; }
	if (!strcmp(name, "strlen")) { *nargs = 1; return NHS_STRLEN; }
	if (!strcmp(name, "memcpy")) { *nargs = 3; return NHS_MEMCPY; }
	return -1;
}

/*
 *	Emit a string-family call as a direct BL through its version-4
 *	helper slot: the arguments are machine addresses sitting on the
 *	VM stack, exactly what the C function takes, so they load
 *	straight into r0-r2.  The stack is untouched - BC_ARGS pops
 *	afterwards, as it does on the dispatcher path.  Marks the object
 *	version 4 when the function commits (wet pass), so an older
 *	bcrun refuses it at load instead of indexing past its table.
 *	Returns 0 if this symbol is not one of the family.
 */
static int t_strslot_emit(unsigned s)
{
	unsigned na;
	int slot;

	if (t_nostrslot())
		return 0;
	slot = t_str_slot(bc_symname[s], &na);
	if (slot < 0)
		return 0;
	t16(0x6820);			/* ldr r0, [r4]     */
	if (na >= 2)
		t16(0x6861);		/* ldr r1, [r4, #4] */
	if (na >= 3)
		t16(0x68A2);		/* ldr r2, [r4, #8] */
	t32(0xF8D5, 0xC000 | (slot * 4));	/* ldr.w r12, [r5, #] */
	t16(0x47E0);			/* blx r12          */
	return 1;
}

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
		k1.fo = -1;
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
		k1.fo = (long)v1 - (t_vdepth - 4);
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
		k2.fo = -1;
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
		k2.fo = (long)v2 - t_vdepth;
		p4 = p3 + l2;
	} else
		return 0;
	if (p4 >= end || codebuf[p4] != wantload)
		return 0;
	if (!t_addr_eq(&k2, &t_track.slot))
		return 0;
	/* the cached locals live in registers, not memory: the seam's
	   reload elision and its slot bookkeeping both reason about
	   memory, so it stands down and the plain cached paths handle
	   them */
	if (t_cfo >= 0 && (k1.fo == t_cfo || k2.fo == t_cfo))
		return 0;
	if (t_cfo8 >= 0 && (k1.fo == t_cfo8 || k2.fo == t_cfo8))
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
	/* a fused push that skipped its r4 half (t_fusel) never made
	   the slot this pop would take back */
	if (!(fused && t_fusel))
		t16(0x3408);	/* adds r4, #8  - pop the operand */
	t32(0xF8D5, 0xC000 | (slot * 4));	/* ldr.w r12, [r5, #] */
	t16(0x47E0);		/* blx  r12                       */
	t_d = (fused && t_fusel) ? 0 : -8;
}

/* conversion: A is already the argument in r0/r1 */
static void t_dcpconv(unsigned slot)
{
	t32(0xF8D5, 0xC000 | (slot * 4));	/* ldr.w r12, [r5, #] */
	t16(0x47E0);		/* blx  r12                       */
	t_d = 0;
}

/*
 *	rt, [r4, #off] load/store, shortest encoding.  The 16-bit forms
 *	exist only for the unsigned loads and the stores, and only for
 *	small aligned offsets; the signed loads are always .W.  The
 *	caller has already checked off <= 4095 - nothing here can fail,
 *	so the dry and wet walks cannot disagree about having emitted it.
 */
#define R4_LDR32	0
#define R4_LDRH		1
#define R4_LDRB		2
#define R4_LDRSH	3
#define R4_LDRSB	4
#define R4_STR32	5
#define R4_STRH		6
#define R4_STRB		7

static void t_r4mem(unsigned kind, unsigned rt, unsigned long off)
{
	static const unsigned short wide[] = {
		0xF8D4, 0xF8B4, 0xF894, 0xF9B4, 0xF994,
		0xF8C4, 0xF8A4, 0xF884
	};
	if (rt > 7)
		goto wide;	/* the 16-bit forms take r0-r7 only */
	switch (kind) {
	case R4_LDR32:
	case R4_STR32:
		if (off <= 124 && !(off & 3)) {
			t16((kind == R4_LDR32 ? 0x6800 : 0x6000) |
			    ((unsigned)(off >> 2) << 6) | 0x20 | rt);
			return;
		}
		break;
	case R4_LDRH:
	case R4_STRH:
		if (off <= 62 && !(off & 1)) {
			t16((kind == R4_LDRH ? 0x8800 : 0x8000) |
			    ((unsigned)(off >> 1) << 6) | 0x20 | rt);
			return;
		}
		break;
	case R4_LDRB:
	case R4_STRB:
		if (off <= 31) {
			t16((kind == R4_LDRB ? 0x7800 : 0x7000) |
			    ((unsigned)off << 6) | 0x20 | rt);
			return;
		}
		break;
	}
wide:
	t32(wide[kind], (rt << 12) | (unsigned)off);
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

	/*
	 *	The 8-byte integer forms.  MMINTEGER is 64-bit, so EVERY
	 *	MMBasic counter is one of these - a BASIC FOR loop paid a
	 *	helper_eqop crossing per iteration while C's int loops ran
	 *	inline.  The carry-pair forms only; 64-bit mul/div/rem and
	 *	the shifts stay on the helper.  Contract per exec_eqop:
	 *	old = [addr] (8 bytes), amount = A untruncated, result
	 *	stored back, A = result (old for the post forms).
	 */
	if (name[blen] == '8' &&
	    (name[blen + 1] == 's' || name[blen + 1] == 'u') &&
	    !name[blen + 2]) {
		unsigned k8 = e->kind;

		switch (k8) {
		case 0: case 1: case 5: case 6: case 7:
		case 10: case 11:
			break;
		default:
			return 0;
		}
		t16(0x6822);		/* ldr  r2, [r4] - address  */
		t16(0x3404);		/* adds r4, #4              */
		t16(0x1992);		/* adds r2, r2, r6          */
		t16(0x6813);		/* ldr  r3, [r2]   - old lo */
		t32(0xF8D2, 0xC004);	/* ldr.w r12, [r2, #4] - hi */
		switch (k8) {
		case 0: case 10:
			t16(0x1818);		/* adds r0, r3, r0    */
			t32(0xEB41, 0x010C);	/* adc.w r1, r1, r12  */
			break;
		case 1: case 11:
			t16(0x1A18);		/* subs r0, r3, r0    */
			t32(0xEB6C, 0x0101);	/* sbc.w r1, r12, r1  */
			break;
		case 5:
			t16(0x4018);		/* ands r0, r3        */
			t32(0xEA0C, 0x0101);	/* and.w r1, r12, r1  */
			break;
		case 6:
			t16(0x4318);		/* orrs r0, r3        */
			t32(0xEA4C, 0x0101);	/* orr.w r1, r12, r1  */
			break;
		case 7:
			t16(0x4058);		/* eors r0, r3        */
			t32(0xEA8C, 0x0101);	/* eor.w r1, r12, r1  */
			break;
		}
		t16(0x6010);		/* str  r0, [r2]            */
		t16(0x6051);		/* str  r1, [r2, #4]        */
		if (k8 >= 10) {
			t16(0x4618);	/* mov  r0, r3 - A = old    */
			t16(0x4661);	/* mov  r1, r12             */
		}
		return 1;
	}

	/*
	 *	The double pre-forms, through the DCP slots: a float FOR
	 *	loop's `t += step`.  The address stays ON the stack across
	 *	the call (the aeabi routine touches nothing of ours), so
	 *	no register survives it and none needs to.  diveqd stays
	 *	on the helper: exec_eqop guards /= 0.0 to 0.0 where IEEE
	 *	says infinity, and the two paths must not disagree.  The
	 *	post forms would need the old value saved across the call;
	 *	nothing emits them.
	 */
	if (name[blen] == 'd' && !name[blen + 1]) {
		unsigned slot;

		switch (e->kind) {
		case 0: slot = NHS_DADD; break;
		case 1: slot = NHS_DSUB; break;
		case 2: slot = NHS_DMUL; break;
		default:
			return 0;
		}
		t32(0xF8D4, 0xC000);	/* ldr.w r12, [r4] - peek   */
		t16(0x44B4);		/* add  r12, r6             */
		t16(0x4602);		/* mov  r2, r0 - amt right  */
		t16(0x460B);		/* mov  r3, r1              */
		t32(0xF8DC, 0x0000);	/* ldr.w r0, [r12] - old lo */
		t32(0xF8DC, 0x1004);	/* ldr.w r1, [r12, #4]      */
		t32(0xF8D5, 0xC000 | (slot * 4));
		t16(0x47E0);		/* blx  r12                 */
		t16(0x6822);		/* ldr  r2, [r4] - again    */
		t16(0x3404);		/* adds r4, #4 - pop now    */
		t16(0x1992);		/* adds r2, r2, r6          */
		t16(0x6010);		/* str  r0, [r2]            */
		t16(0x6051);		/* str  r1, [r2, #4]        */
		return 1;
	}

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
 *	The cached compound assigns: the whole read-modify-write runs
 *	on r7.  Width 4 only (a cached local is a 32-bit scalar), all
 *	twelve kinds.  The address slot is discarded unread.  Same
 *	div-by-zero shape as the width-4 memory inline: hardware sdiv
 *	yields 0 on /0 exactly as exec_eqop's guard does.
 */
static int t_eqop_r7(const char *name)
{
	const struct teqop *e;
	unsigned blen, uns, kind;

	for (e = teqops; e->base; e++) {
		blen = strlen(e->base);
		if (strncmp(name, e->base, blen) == 0)
			break;
	}
	if (!e->base)
		return 0;
	if (name[blen] != '4' ||
	    (name[blen + 1] != 's' && name[blen + 1] != 'u') ||
	    name[blen + 2])
		return 0;
	uns = (name[blen + 1] == 'u');
	kind = e->kind;

	t16(0x3404);			/* adds r4, #4 - drop the slot */
	if (kind >= 10)
		t16(0x463A);		/* mov  r2, r7 - old, for A    */
	switch (kind) {
	case 0: case 10:
		t16(0x4407);		/* add  r7, r0        */
		break;
	case 1: case 11:
		t16(0x1A3F);		/* subs r7, r7, r0    */
		break;
	case 2:
		t16(0x4347);		/* muls r7, r0        */
		break;
	case 3:
		t32(uns ? 0xFBB7 : 0xFB97, 0xF7F0);	/* s/udiv r7,r7,r0 */
		break;
	case 4:
		t32(uns ? 0xFBB7 : 0xFB97, 0xF3F0);	/* s/udiv r3,r7,r0 */
		t32(0xFB03, 0x7710);	/* mls r7, r3, r0, r7 */
		break;
	case 5:
		t16(0x4007);		/* ands r7, r0        */
		break;
	case 6:
		t16(0x4307);		/* orrs r7, r0        */
		break;
	case 7:
		t16(0x4047);		/* eors r7, r0        */
		break;
	case 8:
		t16(0x4087);		/* lsls r7, r0        */
		break;
	case 9:
		t16(uns ? 0x40C7 : 0x4107);	/* lsrs/asrs r7, r0 */
		break;
	}
	t16(kind >= 10 ? 0x4610		/* mov r0, r2 - A = old */
		       : 0x4638);	/* mov r0, r7 - A = new */
	return 1;
}

/*
 *	The pair-cached compound assigns: the 8-byte local lives in
 *	r8:r9, so the old value bridges to r2:r3 (high registers have
 *	no 16-bit ALU forms), the new value computes in r0:r1 exactly
 *	as the stacked 64-bit ALU does, and moves back.  The integer
 *	carry-pair kinds, plus the double pre-forms through the DCP
 *	slots - the same sets the memory inlines cover, and the
 *	classifier escapes everything else.
 */
static int t_eqop_r89(const char *name)
{
	const struct teqop *e;
	unsigned blen, kind;

	for (e = teqops; e->base; e++) {
		blen = strlen(e->base);
		if (strncmp(name, e->base, blen) == 0)
			break;
	}
	if (!e->base)
		return 0;
	kind = e->kind;
	if (name[blen] == '8' &&
	    (name[blen + 1] == 's' || name[blen + 1] == 'u') &&
	    !name[blen + 2]) {
		switch (kind) {
		case 0: case 1: case 5: case 6: case 7:
		case 10: case 11:
			break;
		default:
			return 0;
		}
		t16(0x3404);		/* adds r4, #4 - drop the slot */
		t16(0x4642);		/* mov  r2, r8 - old lo        */
		t16(0x464B);		/* mov  r3, r9 - old hi        */
		switch (kind) {
		case 0: case 10:
			t16(0x1810);		/* adds r0, r2, r0  */
			t16(0x4159);		/* adcs r1, r3      */
			break;
		case 1: case 11:
			t16(0x1A10);		/* subs r0, r2, r0  */
			t32(0xEB63, 0x0101);	/* sbc.w r1, r3, r1 */
			break;
		case 5:
			t16(0x4010);		/* ands r0, r2      */
			t16(0x4019);		/* ands r1, r3      */
			break;
		case 6:
			t16(0x4310);		/* orrs r0, r2      */
			t16(0x4319);		/* orrs r1, r3      */
			break;
		case 7:
			t16(0x4050);		/* eors r0, r2      */
			t16(0x4059);		/* eors r1, r3      */
			break;
		}
		t16(0x4680);		/* mov r8, r0        */
		t16(0x4689);		/* mov r9, r1        */
		if (kind >= 10) {
			t16(0x4610);	/* mov r0, r2 - A = old */
			t16(0x4619);	/* mov r1, r3           */
		}
		return 1;
	}
	if (name[blen] == 'd' && !name[blen + 1]) {
		unsigned slot;

		switch (kind) {
		case 0: slot = NHS_DADD; break;
		case 1: slot = NHS_DSUB; break;
		case 2: slot = NHS_DMUL; break;
		default:
			return 0;
		}
		t16(0x3404);		/* adds r4, #4 - drop the slot */
		t16(0x4602);		/* mov r2, r0 - amt right      */
		t16(0x460B);		/* mov r3, r1                  */
		t16(0x4640);		/* mov r0, r8 - old left       */
		t16(0x4649);		/* mov r1, r9                  */
		t32(0xF8D5, 0xC000 | (slot * 4));
		t16(0x47E0);		/* blx r12                     */
		t16(0x4680);		/* mov r8, r0 - result cached  */
		t16(0x4689);		/* mov r9, r1                  */
		return 1;
	}
	return 0;
}

/*
 *	Constant right operand: PUSH ; CONSTk ; OP32 never needs the
 *	stack at all.  The left operand is already in A, so the whole
 *	shape runs in registers - an immediate form where one exists
 *	(add/sub/cmp imm, shift by imm5, and/or/xor imm8, the uxtb/uxth
 *	masks), and otherwise the constant is built in r2 and the
 *	operator takes it from there, which still beats the fused
 *	five-instruction round trip whatever the constant is.  Operand
 *	order is preserved throughout (left in r0, k on the right), so
 *	the compare conditions are the same table the stacked form uses
 *	and t_boolbranch fuses the following jump exactly as before.
 *
 *	Declined when a branch can land on the constant or the operator
 *	(the landing site would find the push missing).  The window has
 *	no net stack effect and reads nothing through r4, so a pending
 *	slot fact survives by construction.  Like the reload elisions
 *	this runs during the collect walk too: the only swallowed op
 *	that can resolve a target is the jump t_boolbranch consumes,
 *	and t_boolbranch marks it.
 *
 *	Returns the offset to resume at (t_d and tmap maintained), or
 *	0 to decline.
 */
static unsigned long t_cfold(unsigned long o, unsigned long start,
			     unsigned long end)
{
	static const unsigned char ccm[] = {
		0, 1, 11, 3, 12, 8, 13, 9, 10, 2
	};	/* eq ne lt lo gt hi le ls ge hs */
	unsigned long p = o + 1, k, opoff, neg;
	unsigned cop, op2;

	if (t_nocfold())
		return 0;
	if (p >= end)
		return 0;
	cop = codebuf[p];
	if (cop == BC_CONST8) {
		if (p + 1 >= end)
			return 0;
		k = (unsigned long)(long)(signed char)codebuf[p + 1]
		    & 0xFFFFFFFFUL;
		opoff = p + 2;
	} else if (cop == BC_CONST16) {
		if (p + 2 >= end)
			return 0;
		k = (unsigned long)(long)(short)t_rd16(p + 1)
		    & 0xFFFFFFFFUL;
		opoff = p + 3;
	} else if (cop == BC_CONST32) {
		if (p + 4 >= end)
			return 0;
		k = t_rd32c(p + 1);
		opoff = p + 5;
	} else
		return 0;
	if (opoff >= end)
		return 0;
	if ((t_targets[(p - start) >> 3] & (1 << ((p - start) & 7))) ||
	    (t_targets[(opoff - start) >> 3] & (1 << ((opoff - start) & 7))))
		return 0;
	op2 = codebuf[opoff];
	neg = (0UL - k) & 0xFFFFFFFFUL;

	switch (op2) {
	case BC_ADD:
	case BC_SUB: {
		unsigned sub = (op2 == BC_SUB);
		unsigned long imm = k;
		if (imm > 4095 && neg <= 4095) {
			imm = neg;
			sub = !sub;
		}
		if (imm <= 255)
			t16((sub ? 0x3800 : 0x3000) | (unsigned)imm);
		else if (imm <= 4095)
			t_addsubw(sub, 0, 0, (unsigned)imm);
		else {
			t_constr(2, k);
			t16(sub ? 0x1A80 : 0x1810);
					/* subs/adds r0, r0, r2 */
		}
		break;
	}
	case BC_AND:
		if (k == 0xFFUL) {
			t16(0xB2C0);	/* uxtb r0, r0 */
			break;
		}
		if (k == 0xFFFFUL) {
			t16(0xB280);	/* uxth r0, r0 */
			break;
		}
		if (k <= 255) {
			t32(0xF010, (unsigned)k);	/* ands r0, #k */
			break;
		}
		t_constr(2, k);
		t16(0x4010);		/* ands r0, r2 */
		break;
	case BC_OR:
		if (k <= 255) {
			t32(0xF050, (unsigned)k);	/* orrs r0, #k */
			break;
		}
		t_constr(2, k);
		t16(0x4310);		/* orrs r0, r2 */
		break;
	case BC_XOR:
		if (k <= 255) {
			t32(0xF090, (unsigned)k);	/* eors r0, #k */
			break;
		}
		t_constr(2, k);
		t16(0x4050);		/* eors r0, r2 */
		break;
	case BC_SHL:
	case BC_SHRS:
	case BC_SHRU:
		if (k >= 1 && k <= 31) {
			t16((op2 == BC_SHL ? 0x0000 :
			     op2 == BC_SHRU ? 0x0800 : 0x1000) |
			    ((unsigned)k << 6));
					/* lsls/lsrs/asrs r0, r0, #k */
		} else if (k) {
			/* shift counts use the register's bottom byte,
			   exactly as the stacked form's .W shift did */
			t_constr(2, k);
			t16(op2 == BC_SHL ? 0x4090 :
			    op2 == BC_SHRU ? 0x40D0 : 0x4110);
					/* lsls/lsrs/asrs r0, r2 */
		}			/* k == 0: identity */
		break;
	case BC_MUL:
		t_constr(2, k);
		t16(0x4350);		/* muls r0, r2 */
		break;
	case BC_DIVS:
	case BC_DIVU:
		t_constr(2, k);
		t32(op2 == BC_DIVS ? 0xFB90 : 0xFBB0, 0xF0F2);
					/* s/udiv r0, r0, r2 */
		break;
	case BC_REMS:
	case BC_REMU:
		t_constr(2, k);
		t32(op2 == BC_REMS ? 0xFB90 : 0xFBB0, 0xF3F2);
					/* s/udiv r3, r0, r2 */
		t32(0xFB03, 0x0012);	/* mls r0, r3, r2, r0 */
		break;
	case BC_EQ:  case BC_NE:
	case BC_LTS: case BC_LTU:
	case BC_GTS: case BC_GTU:
	case BC_LES: case BC_LEU:
	case BC_GES: case BC_GEU:
		tmap[p - start] = tlen;
		tmap[opoff - start] = tlen;
		if (k <= 255)
			t16(0x2800 | (unsigned)k);	/* cmp r0, #k */
		else {
			t_constr(2, k);
			t16(0x4290);			/* cmp r0, r2 */
		}
		t_d = 0;
		return t_boolbranch(opoff, ccm[op2 - BC_EQ], start, end);
	default:
		return 0;
	}
	tmap[p - start] = tlen;
	tmap[opoff - start] = tlen;
	t_d = 0;
	return opoff + 1;
}

/* The reversed-operand operator forms the register folds share: left
   in r0, right in r2, result in r0.  Compares are not here - they
   need the boolbranch tail.  t_rfold_ok says whether an opcode is
   coverable without emitting anything, so a fold can check before it
   commits its first instruction. */
static int t_rfold_ok(unsigned op2)
{
	switch (op2) {
	case BC_ADD: case BC_SUB: case BC_MUL:
	case BC_DIVS: case BC_DIVU: case BC_REMS: case BC_REMU:
	case BC_AND: case BC_OR: case BC_XOR:
	case BC_SHL: case BC_SHRS: case BC_SHRU:
	case BC_EQ:  case BC_NE:
	case BC_LTS: case BC_LTU: case BC_GTS: case BC_GTU:
	case BC_LES: case BC_LEU: case BC_GES: case BC_GEU:
		return 1;
	}
	return 0;
}

static void t_op_r0r2(unsigned op2)
{
	switch (op2) {
	case BC_ADD:  t16(0x1810); break;	/* adds r0, r2, r0 */
	case BC_SUB:  t16(0x1A80); break;	/* subs r0, r0, r2 */
	case BC_MUL:  t16(0x4350); break;	/* muls r0, r2     */
	case BC_AND:  t16(0x4010); break;	/* ands r0, r2     */
	case BC_OR:   t16(0x4310); break;	/* orrs r0, r2     */
	case BC_XOR:  t16(0x4050); break;	/* eors r0, r2     */
	case BC_SHL:  t16(0x4090); break;	/* lsls r0, r2     */
	case BC_SHRS: t16(0x4110); break;	/* asrs r0, r2     */
	case BC_SHRU: t16(0x40D0); break;	/* lsrs r0, r2     */
	case BC_DIVS: t32(0xFB90, 0xF0F2); break;
	case BC_DIVU: t32(0xFBB0, 0xF0F2); break;
	case BC_REMS:
		t32(0xFB90, 0xF3F2);	/* sdiv r3, r0, r2    */
		t32(0xFB03, 0x0012);	/* mls  r0, r3, r2, r0 */
		break;
	case BC_REMU:
		t32(0xFBB0, 0xF3F2);
		t32(0xFB03, 0x0012);
		break;
	}
}

/*
 *	Memory right operand: PUSH ; LOCALn ; LOADx ; OP32, and the
 *	global form with ADDR in place of LOCALn.  The left operand
 *	stays in A and the right is loaded straight into r2, so the
 *	whole binary operator runs without touching the stack - `a+b`
 *	is ldr r2 / adds, the shape gcc emits.  The LOCAL offset is
 *	encoded relative to r4 after the push that no longer happens,
 *	so it addresses off the current r4 at v-4; v < 4 would name
 *	the pushed slot itself and declines.  Same fold set, operand
 *	order and boolbranch tail as t_cfold; same landing-site rule
 *	over every swallowed op.  r2 (r3 for the rem forms) is free by
 *	the same argument as t_cfold: a PUSH is not a builder, so this
 *	window can never sit inside a fused one.
 */
static unsigned long t_rfold(unsigned long o, unsigned long start,
			     unsigned long end)
{
	static const unsigned char ccm[] = {
		0, 1, 11, 3, 12, 8, 13, 9, 10, 2
	};	/* eq ne lt lo gt hi le ls ge hs */
	unsigned long p = o + 1, v = 0, ad = 0, loff, opoff;
	unsigned lop, op2, lkind, sym = 0;
	int global = 0;

	if (t_norfold())
		return 0;
	if (p >= end)
		return 0;
	if (codebuf[p] == BC_LOCAL8) {
		if (p + 1 >= end)
			return 0;
		v = codebuf[p + 1];
		loff = p + 2;
	} else if (codebuf[p] == BC_LOCAL16) {
		if (p + 2 >= end)
			return 0;
		v = t_rd16(p + 1);
		loff = p + 3;
	} else if (codebuf[p] == BC_ADDR) {
		if (p + 4 >= end)
			return 0;
		if (!t_addrsym(p + 1, &sym))
			return 0;
		if (ntpool >= TPOOLMAX)
			return 0;
		ad = t_rd32c(p + 1);
		global = 1;
		loff = p + 5;
	} else
		return 0;
	if (!global && (v < 4 || v - 4 > 4095))
		return 0;
	if (loff >= end)
		return 0;
	lop = codebuf[loff];
	switch (lop) {
	case BC_LOAD8S:  lkind = R4_LDRSB; break;
	case BC_LOAD8U:  lkind = R4_LDRB;  break;
	case BC_LOAD16S: lkind = R4_LDRSH; break;
	case BC_LOAD16U: lkind = R4_LDRH;  break;
	case BC_LOAD32:  lkind = R4_LDR32; break;
	default:
		return 0;
	}
	opoff = loff + 1;
	if (opoff >= end)
		return 0;
	if ((t_targets[(p - start) >> 3] & (1 << ((p - start) & 7))) ||
	    (t_targets[(loff - start) >> 3] & (1 << ((loff - start) & 7))) ||
	    (t_targets[(opoff - start) >> 3] &
	     (1 << ((opoff - start) & 7))))
		return 0;
	op2 = codebuf[opoff];
	if (!t_rfold_ok(op2))
		return 0;

	/* right operand into r2 */
	if (global) {
		if (!t_dry) {
			tpooltab[ntpool].sym = sym;
			tpooltab[ntpool].site = tlen;
		}
		ntpool++;
		t_mov16(0, 2, ad & 0xFFFF);
		t_mov16(1, 2, (ad >> 16) & 0xFFFF);
		switch (lkind) {
		case R4_LDRSB: t16(0x56B2); break;  /* ldrsb r2,[r6,r2] */
		case R4_LDRB:  t16(0x5CB2); break;  /* ldrb  r2,[r6,r2] */
		case R4_LDRSH: t16(0x5EB2); break;  /* ldrsh r2,[r6,r2] */
		case R4_LDRH:  t16(0x5AB2); break;  /* ldrh  r2,[r6,r2] */
		case R4_LDR32: t16(0x58B2); break;  /* ldr   r2,[r6,r2] */
		}
	} else if (t_cfo >= 0 && (long)v - (t_vdepth + 4) == t_cfo) {
		/* the cached local: it lives in r7, never in memory.
		   The LOCAL executes at depth+4 in the bytecode (after
		   the push this fold elides), hence the rebase.  A
		   non-32-bit load cannot be classified-cached; decline
		   and let the LOCAL case rule on it. */
		if (lkind != R4_LDR32)
			return 0;
		t16(0x463A);		/* mov r2, r7 */
	} else {
		/* the fold swallows a LOCAL;LOADx the LOCAL case never
		   sees: classify the read here or the score undercounts */
		if (t_collect && lkind == R4_LDR32) {
			long cfo = (long)v - (t_vdepth + 4);
			if (cfo >= 0 && !(cfo & 3) && cfo <= 4092) {
				struct tlc *cp = t_lc_find(cfo);
				if (cp) {
					cp->reads++;
					t_lc_span(p, loff + 1, cfo);
				}
			}
		}
		t_r4mem(lkind, 2, v - 4);
	}
	tmap[p - start] = tlen;
	tmap[loff - start] = tlen;
	tmap[opoff - start] = tlen;
	t_d = 0;
	if (op2 >= BC_EQ && op2 <= BC_GEU) {
		t16(0x4290);			/* cmp r0, r2 */
		return t_boolbranch(opoff, ccm[op2 - BC_EQ], start, end);
	}
	t_op_r0r2(op2);
	return opoff + 1;
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
	t_fuse_elided = 0;
	t_fusel = 0;
	t_vdepth = 0;	/* true depth: every walk starts at frame base */
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
		t_vd = T_DUNK;

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
			/* After the frame is taken, so what is tested is
			   where the stack actually now is.  Emitted even
			   for a frameless function: the caller has still
			   pushed arguments and a return slot, so every
			   level costs stack whether this one asks for
			   locals or not. */
			if (op == BC_ENTER && o == start)
				t_stackguard();
			/* warm the cached local from its frame slot -
			   for an argument this is the caller's value,
			   for a plain local it is garbage exactly as C
			   allows.  From here memory is never consulted
			   again. */
			if (op == BC_ENTER && o == start && t_cfo >= 0)
				t_r4mem(R4_LDR32, 7, (unsigned long)t_cfo);
			if (op == BC_ENTER && o == start && t_cfo8 >= 0) {
				t_r4mem(R4_LDR32, 8,
					(unsigned long)t_cfo8);
				t_r4mem(R4_LDR32, 9,
					(unsigned long)t_cfo8 + 4);
			}
			o += 3;
			break;
		}
		case BC_RET:
			/* the preamble pushed lr - and the cache
			   registers this function uses - on the real
			   machine stack, so calls can clobber them
			   freely */
			if (t_cfo8 >= 0)
				t32(0xE8BD, 0x8300 |
				    (t_cfo >= 0 ? 0x80 : 0));
				  /* pop.w {(r7,) r8, r9, pc} */
			else
				t16(t_cfo >= 0 ? 0xBD80 : 0xBD00);
				  /* pop {pc} / pop {r7, pc}  */
			o++;
			break;
		case BC_ARGS: {
			unsigned n = codebuf[o + 1];
			if (n)
				t16(0x3400 | n);	/* adds r4, #n */
			t_vd = -(long)n;
			if (t_collect)
				t_lc_pend_args((long)n);
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
			t_track.a_is.fo = -1;
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
			t_track.a_is.fo = -1;
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
			me.fo = -1;
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
		case BC_PUSH: {
			/* the armed LOCAL;PUSH becomes a pending write */
			if (t_collect && t_lc_armoff == o)
				t_lc_pend_push(t_lc_armfo, t_lc_armstart);
			/* constant or memory right operand: no stack
			   round trip, no parked register - see t_cfold
			   and t_rfold.  A PUSH can never sit inside a
			   fused window (it is not a builder), so r2 is
			   free for the right operand. */
			unsigned long nl = t_cfold(o, start, end);
			if (!nl)
				nl = t_rfold(o, start, end);
			if (nl) {
				o = nl;
				break;
			}
		}
			if (t_fuse_scan(o, 4, start, end)) {
				if (t_fuse_nolocal) {
					/* LOCAL-free window: r4 never
					   moves, depth nets to zero */
					t16(0x4602);	/* mov r2, r0 */
					t_fuse_elided = 1;
					t_d = 0;
				} else {
					t16(0x3C04);	/* subs r4, #4 */
					t16(0x4602);	/* mov  r2, r0 */
					t_d = 4;
				}
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
			if (t_collect) {
				int pt = t_lc_pend_top();
				if (pt >= 0) {
					t_lc_escape(t_lcpend[pt].fo);
					t_lcpendn--;
				}
			}
			t_d = -4;
			o++;
			break;
		case BC_DUP:
			t16(0x6822);		/* ldr  r2, [r4]    */
			t16(0x3C04);		/* subs r4, #4      */
			t16(0x6022);		/* str  r2, [r4]    */
			if (t_collect) {
				int pt = t_lc_pend_top();
				if (pt >= 0)
					t_lc_escape(t_lcpend[pt].fo);
			}
			t_d = 4;
			o++;
			break;
		case BC_SWAP:
			t16(0x6822);		/* ldr  r2, [r4]    */
			t16(0x6020);		/* str  r0, [r4]    */
			t16(0x4610);		/* mov  r0, r2      */
			/* reads and rewrites the top slot: any pending
			   there is out of the model */
			if (t_collect)
				while (t_lcpendn &&
				       t_lcpend[t_lcpendn - 1].vd >=
				       t_vdepth - 4)
					t_lc_escape(
					    t_lcpend[--t_lcpendn].fo);
			o++;
			break;
		case BC_DROP:
			t16(0x3404);		/* adds r4, #4      */
			if (t_collect) {
				int pt = t_lc_pend_top();
				if (pt >= 0) {
					t_lc_escape(t_lcpend[pt].fo);
					t_lcpendn--;
				}
			}
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
			me.fo = (long)v - t_vdepth;
			/* classification: what kind of use is this? */
			if (t_collect) {
				unsigned nxt = (o + sz < end) ?
				    codebuf[o + sz] : BC_NOP;
				if (me.fo < 0 || (me.fo & 3) ||
				    me.fo > 4084)
					t_lc_escape(me.fo);
				else if (nxt == BC_LOAD32 ||
					 nxt == BC_LOAD64) {
					struct tlc *p = t_lc_find(me.fo);
					if (p) {
						p->reads++;
						t_lc_setw(p,
						    nxt == BC_LOAD32 ? 4 : 8);
						t_lc_span(o, o + sz + 1,
							  me.fo);
					}
				} else if (nxt == BC_PUSH) {
					t_lc_armfo = me.fo;
					t_lc_armoff = o + sz;
					t_lc_armstart = o;
				} else
					t_lc_escape(me.fo);
			}
			/* the cached locals: a read is one mov (a pair for
			   the 8-byte one); a write or compound assign
			   starts with LOCAL;PUSH exactly as before (the
			   address may be materialised - nothing ever
			   reads or writes through it, the consumer
			   redirects to the register).  Anything else
			   here means the walks diverged: bail to
			   bytecode rather than touch stale memory. */
			if ((t_cfo >= 0 && me.fo == t_cfo) ||
			    (t_cfo8 >= 0 && me.fo == t_cfo8)) {
				int pair = (me.fo == t_cfo8);
				unsigned nxt = (o + sz < end) ?
				    codebuf[o + sz] : BC_NOP;
				if (nxt == (pair ? BC_LOAD64 : BC_LOAD32) &&
				    !(t_targets[(o + sz - start) >> 3] &
				      (1 << ((o + sz - start) & 7)))) {
					if (pair) {
						t16(0x4640); /* mov r0, r8 */
						t16(0x4649); /* mov r1, r9 */
					} else
						t16(0x4638); /* mov r0, r7 */
					tmap[o + sz - start] = tlen;
					t_track.a_is.kind = 0;
					t_d = 0;
					o += sz + 1;
					break;
				}
				if (nxt != BC_PUSH)
					return 0;
				/* fall through: the write's address push */
			}
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
			/* Direct frame access: LOCALn feeding straight
			   into a load reads the slot off r4 in one
			   instruction instead of materialising the VM
			   offset in A and re-adding the base - the
			   address is r4+v by construction, whatever r6
			   holds.  Declined if a branch can land on the
			   load (it would arrive expecting A to hold the
			   address).  For the 4- and 8-byte forms A ends
			   up mirroring mem[local] - the same fact a
			   store through the slot seeds - so consecutive
			   reads of one local collapse further. */
			if (!t_nor4() && o + sz < end && v <= 4095
			    && !(t_targets[(o + sz - start) >> 3] &
				 (1 << ((o + sz - start) & 7)))) {
				unsigned lop = codebuf[o + sz];
				unsigned fk = 0;
				int fuse = 1;
				switch (lop) {
				case BC_LOAD8S:  fk = R4_LDRSB; break;
				case BC_LOAD8U:  fk = R4_LDRB;  break;
				case BC_LOAD16S: fk = R4_LDRSH; break;
				case BC_LOAD16U: fk = R4_LDRH;  break;
				case BC_LOAD32:  fk = R4_LDR32; break;
				case BC_LOAD64:
					fk = R4_LDR32;
					if (v > 4091)
						fuse = 0;
					break;
				default:
					fuse = 0;
				}
				if (fuse) {
					if (lop == BC_LOAD64) {
						t_r4mem(R4_LDR32, 1, v + 4);
						t_r4mem(R4_LDR32, 0, v);
					} else
						t_r4mem(fk, 0, v);
					tmap[o + sz - start] = tlen;
					t_track.a_is.kind = 0;
					if (lop == BC_LOAD32 ||
					    lop == BC_LOAD64) {
						t_track.a_valid = 1;
						t_track.a_key = me;
						t_track.a_width =
						    (lop == BC_LOAD32) ? 4 : 8;
						t_keep = 1;
					}
					t_d = 0;
					o += sz + 1;
					break;
				}
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
		case BC_STORE32: {
			/* When the address on top of the stack is a
			   frame slot the tracker followed from its
			   LOCAL;PUSH (the same fact the seam trusts),
			   pop it unread and store straight off r4: the
			   slot sits at r4+k once the address is popped.
			   Same stack effect, same A, one memory access
			   fewer.  Every fact downstream - the mirror
			   seed, the seam - holds unchanged. */
			unsigned long k = 0;
			int direct = 0;
			/* classification: a store consuming a pending
			   address slot is a write; only the 32-bit form
			   is cacheable */
			if (t_collect) {
				int pt = t_lc_pend_top();
				if (pt >= 0) {
					if (op == BC_STORE32) {
						struct tlc *p =
						    t_lc_find(
							t_lcpend[pt].fo);
						if (p) {
							p->writes++;
							t_lc_wr(o,
							 t_lcpend[pt].fo,
							 0);
						}
					} else
						t_lc_escape(
						    t_lcpend[pt].fo);
					t_lcpendn--;
				}
			}
			/* an annotated store of the cached local: pop
			   the address unread, the register IS the
			   variable - no memory write, no mirror seed,
			   and the seam (which reasons about memory) is
			   skipped */
			if (op == BC_STORE32 && t_cfo >= 0 &&
			    t_lc_wr_fo(o) == t_cfo) {
				t16(0x3404);	/* adds r4, #4 */
				t16(0x4607);	/* mov  r7, r0 */
				t_d = -4;
				t_keep = 1;
				o++;
				break;
			}
			/* a store consuming either cached local's address
			   slot without an annotation is a divergence */
			if (t_track.slot.kind == 3 && t_track.slot_depth == 0
			    && ((t_cfo >= 0 && t_track.slot.fo == t_cfo) ||
				(t_cfo8 >= 0 && t_track.slot.fo == t_cfo8)))
				return 0;
			if (!t_nor4() && t_track.slot.kind == 3
			    && t_track.slot_depth == 0
			    && t_track.slot.sym == t_epoch) {
				k = t_track.slot.k +
				    (unsigned long)(t_depth - 4);
				direct = (k <= 4095);
			}
			if (direct) {
				t16(0x3404);	/* adds r4, #4      */
				t_r4mem(op == BC_STORE8 ? R4_STRB :
					op == BC_STORE16 ? R4_STRH :
					R4_STR32, 0, k);
			} else {
				t16(0x6822);	/* ldr  r2, [r4]    */
				t16(0x3404);	/* adds r4, #4      */
				if (op == BC_STORE8)
					t16(0x54B0);	/* strb r0, [r6, r2] */
				else if (op == BC_STORE16)
					t16(0x52B0);	/* strh r0, [r6, r2] */
				else
					t16(0x50B0);	/* str  r0, [r6, r2] */
			}
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
		}
		case BC_ADDR: {
			unsigned s;
			unsigned long ad = t_rd32c(o + 1);
			struct t_addr me;
			if (!t_addrsym(o + 1, &s))
				return 0;
			me.kind = 2;
			me.sym = s;
			me.k = ad;
			me.fo = -1;
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
			if (!t_fusel)
				t16(0x3404);	/* adds r4, #4   */
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
			t_d = t_fusel ? 0 : -4;
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
			/* classification: an eqop consuming a pending
			   address slot is the read-modify-write pattern.
			   Cacheable: every width-4 kind (r7), the 8-byte
			   carry-pair kinds and the double pre-forms
			   (r8:r9) - exactly the sets the emitters
			   cover; anything else escapes the offset */
			if (t_collect && t_eqop_name(bc_symname[s])) {
				int pt = t_lc_pend_top();
				if (pt >= 0) {
					const char *nm = bc_symname[s];
					const struct teqop *e;
					unsigned bl2, wch, ok = 0, w = 0;
					struct tlc *p =
					    t_lc_find(t_lcpend[pt].fo);
					for (e = teqops; e->base; e++) {
						bl2 = strlen(e->base);
						if (!strncmp(nm, e->base,
							     bl2))
							break;
					}
					wch = e->base ? nm[bl2] : 0;
					if (wch == '4') {
						ok = 1;
						w = 4;
					} else if (wch == '8') {
						switch (e->kind) {
						case 0: case 1: case 5:
						case 6: case 7:
						case 10: case 11:
							ok = 1;
							w = 8;
						}
					} else if (wch == 'd') {
						switch (e->kind) {
						case 0: case 1: case 2:
							ok = 1;
							w = 8;
						}
					}
					if (ok && p && !p->esc) {
						p->eqops++;
						t_lc_setw(p, w);
						t_lc_wr(o,
							t_lcpend[pt].fo, 1);
					} else
						t_lc_escape(t_lcpend[pt].fo);
					t_lcpendn--;
				}
			}
			/* an annotated compound assign of a cached local
			   runs entirely in registers */
			{
				long wfo = t_lc_wr_fo(o);

				if (wfo >= 0 && t_cfo >= 0 &&
				    wfo == t_cfo) {
					if (!t_eqop_r7(bc_symname[s]))
						return 0;
					t_d = T_DUNK;
					t_vd = -4;
					o += 3;
					break;
				}
				if (wfo >= 0 && t_cfo8 >= 0 &&
				    wfo == t_cfo8) {
					if (!t_eqop_r89(bc_symname[s]))
						return 0;
					t_d = T_DUNK;
					t_vd = -4;
					o += 3;
					break;
				}
			}
			/* an eqop consuming either cached local's address
			   slot without an annotation is a divergence */
			if (t_track.slot.kind == 3 && t_track.slot_depth == 0
			    && ((t_cfo >= 0 && t_track.slot.fo == t_cfo) ||
				(t_cfo8 >= 0 && t_track.slot.fo == t_cfo8))
			    && t_eqop_name(bc_symname[s]))
				return 0;
			if (t_eqop(bc_symname[s])) {
				t_vd = -4;
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
				t_vd = -4;
				o += 3;
				break;
			}
			/* string family: direct slot, no dispatcher */
			if (t_strslot_emit(s)) {
				t_d = 0;
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
			t_vd = 0;
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
					      (long)(t_base + t_psize +
						     tlen + 4));
				t16(0x3404);	/* adds r4, #4        */
				o += 5;
				break;
			}
			/* A library callee arrives as a CALL on a
			   BC_SYM_LIB symbol (helper_call's tagged-index
			   path); the string family skips all of that
			   and BLs its slot */
			if (ad == 0 && symtab[s].s_type == BC_SYM_LIB &&
			    t_strslot_emit(s)) {
				t_d = 0;
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
			if (!t_fusel)
				t16(0x3404);	/* adds r4, #4     */
			t16(0x4282);		/* cmp  r2, r0     */
			t_d = t_fusel ? 0 : -4;
					/* fall-through keeps facts; the taken
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
		case BC_STORE64: {
			/* the direct frame form, as the 32-bit stores */
			unsigned long k = 0;
			int direct = 0;
			if (t_collect) {
				int pt = t_lc_pend_top();
				if (pt >= 0) {
					struct tlc *p =
					    t_lc_find(t_lcpend[pt].fo);
					if (p) {
						p->writes++;
						t_lc_setw(p, 8);
						t_lc_wr(o,
							t_lcpend[pt].fo, 0);
					}
					t_lcpendn--;
				}
			}
			/* an annotated store of the pair-cached local */
			if (t_cfo8 >= 0 && t_lc_wr_fo(o) == t_cfo8) {
				t16(0x3404);	/* adds r4, #4 */
				t16(0x4680);	/* mov  r8, r0 */
				t16(0x4689);	/* mov  r9, r1 */
				t_d = -4;
				t_keep = 1;
				o++;
				break;
			}
			/* a store consuming either cached local's address
			   slot without an annotation is a divergence */
			if (t_track.slot.kind == 3 && t_track.slot_depth == 0
			    && ((t_cfo >= 0 && t_track.slot.fo == t_cfo) ||
				(t_cfo8 >= 0 && t_track.slot.fo == t_cfo8)))
				return 0;
			if (!t_nor4() && t_track.slot.kind == 3
			    && t_track.slot_depth == 0
			    && t_track.slot.sym == t_epoch) {
				k = t_track.slot.k +
				    (unsigned long)(t_depth - 4);
				direct = (k <= 4091);
			}
			if (direct) {
				t16(0x3404);	/* adds r4, #4       */
				t_r4mem(R4_STR32, 0, k);
				t_r4mem(R4_STR32, 1, k + 4);
			} else {
				t16(0x6822);	/* ldr  r2, [r4]     */
				t16(0x3404);	/* adds r4, #4       */
				t16(0x1992);	/* adds r2, r2, r6   */
				t16(0x6010);	/* str  r0, [r2]     */
				t16(0x6051);	/* str  r1, [r2, #4] */
			}
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
		}
		case BC_PUSH64:
			if (t_fuse_scan(o, 8, start, end)) {
				if (t_fuse_nolocal) {
					t16(0x4602);	/* mov r2, r0 */
					t16(0x460B);	/* mov r3, r1 */
					t_fuse_elided = 1;
					t_d = 0;
				} else {
					t16(0x3C08);	/* subs r4, #8 */
					t16(0x4602);	/* mov  r2, r0 */
					t16(0x460B);	/* mov  r3, r1 */
					t_d = 8;
				}
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
			if (!t_fusel)
				t16(0x3408);	/* adds r4, #8       */
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
			if (!t_fusel)
				t16(0x3408);	/* adds r4, #8       */
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
			if (!t_fusel)
				t16(0x3408);	/* adds r4, #8       */
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
		case BC_COPY: {
			/* Small constant-length copies inline: dst is the
			   stacked word, src is A, A becomes dst - exactly
			   the interpreter's case.  Plain ldr/str pairs, not
			   ldm/stm: the immediate forms take unaligned
			   addresses, and a struct copy is either disjoint
			   or exact-overlap so a forward copy is memmove
			   here.  r2/r3 are free - BC_COPY is neither a
			   builder nor fusable, so no window spans it.
			   Larger copies keep the helper_op round trip. */
			unsigned len = t_rd16(o + 1);
			if (t_collect) {
				int pt = t_lc_pend_top();
				if (pt >= 0) {
					t_lc_escape(t_lcpend[pt].fo);
					t_lcpendn--;
				}
			}
			if (!t_noicopy() && len <= 64) {
				unsigned k = 0;
				t16(0x6822);	/* ldr  r2, [r4] - dst  */
				t16(0x3404);	/* adds r4, #4          */
				for (; k + 4 <= len; k += 4) {
					t16(0x6803 | ((k >> 2) << 6));
						/* ldr  r3, [r0, #k] */
					t16(0x6013 | ((k >> 2) << 6));
						/* str  r3, [r2, #k] */
				}
				if (len & 2) {
					t16(0x8803 | ((k >> 1) << 6));
						/* ldrh r3, [r0, #k] */
					t16(0x8013 | ((k >> 1) << 6));
						/* strh r3, [r2, #k] */
					k += 2;
				}
				if (len & 1) {
					if (k <= 31) {
						t16(0x7803 | (k << 6));
						/* ldrb r3, [r0, #k] */
						t16(0x7013 | (k << 6));
						/* strb r3, [r2, #k] */
					} else {
						t32(0xF890, 0x3000 | k);
						t32(0xF882, 0x3000 | k);
					}
				}
				t16(0x4610);	/* mov r0, r2 - A = dst */
				t_d = -4;
				o += 3;
				break;
			}
			/* length rides in the op word's high half */
			t_helperop(op | ((unsigned long)len << 16), 4);
			o += 3;
			break;
		}
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
			t_vd = (long)n;	/* ...but statically known here */
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
		/*
		 *	True depth, walked identically in every pass.
		 *	Explicit t_vd wins; a real t_d is the same number
		 *	everywhere a local can be named (the elided fused
		 *	windows diverge transiently but cannot contain a
		 *	LOCAL); the ops that keep t_d at T_DUNK for fact
		 *	purposes get their known deltas here.  Anything
		 *	unaccounted for poisons CACHING for the function,
		 *	never correctness.
		 */
		{
			long dv = t_vd;

			if (dv == T_DUNK) {
				switch (op) {
				case BC_NOP: case BC_ENTER: case BC_LEAVE:
				case BC_RET:
				case BC_JUMP: case BC_JFALSE: case BC_JTRUE:
				case BC_SWITCH: case BC_SWAP:
				case BC_NEG: case BC_NOT:
				case BC_SEXT8: case BC_SEXT16:
				case BC_ZEXT8: case BC_ZEXT16:
				case BC_ZEXT32: case BC_TRUNC64:
				case BC_NEG64: case BC_NOT64:
				case BC_NEGD: case BC_NEGF:
				case BC_BOOL64: case BC_LNOT64:
				case BC_BOOLD: case BC_LNOTD:
				case BC_BOOLF: case BC_LNOTF:
				case BC_CALL: case BC_CALLA:
					dv = 0;
					break;
				case BC_ADD64: case BC_SUB64:
				case BC_AND64: case BC_OR64: case BC_XOR64:
				case BC_EQ64: case BC_NE64:
				case BC_LTS64: case BC_LTU64:
				case BC_GTS64: case BC_GTU64:
				case BC_LES64: case BC_LEU64:
				case BC_GES64: case BC_GEU64:
					dv = t_fusel ? 0 : -8;
					break;
				default:
					dv = (t_d != T_DUNK) ? t_d : T_DUNK;
				}
			}
			if (dv == T_DUNK)
				t_lcbad = 1;
			else
				t_vdepth += dv;
		}
		/* any pending whose slot an un-modelled op consumed
		   (address arithmetic, a compare, anything that popped
		   through it) is an escape - swept by depth so nothing
		   stale can ever match again.  Pendings deliberately
		   SURVIVE calls, jumps and other fact-killers: the
		   consumer is annotated by offset, and a store always
		   consumes the address its own statement pushed. */
		if (t_collect)
			while (t_lcpendn &&
			       t_lcpend[t_lcpendn - 1].vd > t_vdepth)
				t_lc_escape(t_lcpend[--t_lcpendn].fo);

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
	t_lcn = 0;
	t_lcspann = 0;
	t_lcpendn = 0;
	t_lcwrn = 0;
	t_lcbad = 0;
	t_cfo = -1;
	t_cfo8 = -1;
	t_psize = 2;
	t_lc_armoff = ~0UL;
	t_dry = 1;
	t_collect = 1;
	if (!t_span(fn_start, end)) {
		t_collect = 0;
		goto bailed;
	}
	t_collect = 0;

	/*
	 *	Pick the local to cache in r7, now that classification
	 *	and the target bitmap are both complete.  Every recorded
	 *	access span must be clean of landing sites, or the
	 *	emitter's rewrites could not be guaranteed to fire and a
	 *	stale memory access would slip out.
	 */
	t_lc_pend_escape_all();		/* patterns that never completed */
	{
		const char *dbg = getenv("THUMB_REGCDBG");
		if (dbg && atoi(dbg) >= 2) {
			unsigned i;
			fprintf(stderr, "regc? %s bad=%d n=%u spans=%u\n",
				bc_symname[fn_sym], t_lcbad, t_lcn,
				t_lcspann);
			for (i = 0; i < t_lcn; i++)
				fprintf(stderr,
					"  fo=%ld r=%u e=%u w=%u esc=%d\n",
					t_lctab[i].fo, t_lctab[i].reads,
					t_lctab[i].eqops, t_lctab[i].writes,
					t_lctab[i].esc);
		}
	}
	{
		/* THUMB_REGCFN=name: cache only in the named function -
		   the bisection knob for a suspected wrong pick */
		const char *only = getenv("THUMB_REGCFN");
		if (only && strcmp(only, bc_symname[fn_sym]))
			t_lcbad = 1;
	}
	if (!t_noregc() && !t_lcbad && end > fn_start &&
	    codebuf[fn_start] == BC_ENTER) {
		int best4 = -1, best8 = -1;
		long bests4 = 0, bests8 = 0;
		unsigned i, j;

		for (i = 0; i < t_lcn; i++) {
			struct tlc *p = &t_lctab[i];
			long score;

			if (p->esc || p->fo < 0 || (p->fo & 3) ||
			    p->fo > 4084 || !p->w)
				continue;
			if (p->reads + p->eqops == 0)
				continue;
			score = (long)p->reads + 2 * (long)p->eqops +
				(long)p->writes;
			if (score < 4)
				continue;
			for (j = 0; j < t_lcspann && score >= 0; j++) {
				unsigned long q;

				if (t_lcspan[j].fo != p->fo)
					continue;
				for (q = t_lcspan[j].s;
				     q < t_lcspan[j].e; q++)
					if (t_targets[(q - fn_start) >> 3] &
					    (1 << ((q - fn_start) & 7))) {
						score = -1;
						break;
					}
			}
			if (p->w == 4 && score > bests4) {
				bests4 = score;
				best4 = (int)i;
			} else if (p->w == 8 && t_regc8() &&
				   score > bests8) {
				bests8 = score;
				best8 = (int)i;
			}
		}
		if (best4 >= 0)
			t_cfo = t_lctab[best4].fo;
		if (best8 >= 0)
			t_cfo8 = t_lctab[best8].fo;
		if ((best4 >= 0 || best8 >= 0) &&
		    getenv("THUMB_REGCDBG"))
			fprintf(stderr,
				"regcache: %s fo=%ld score=%ld "
				"fo8=%ld score8=%ld\n",
				bc_symname[fn_sym], t_cfo, bests4,
				t_cfo8, bests8);
	}
	t_psize = (t_cfo8 >= 0) ? 4 : 2;

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
	/* Preamble: save lr - and whichever cache registers this
	   function uses - on the real machine stack, so call sites can
	   clobber them; BC_RET translates to the matching pop.  The
	   pair cache needs the wide push (r8/r9 have no 16-bit form),
	   which is why the self-BL offset math carries t_psize.  tmap
	   and in-span branch offsets are tbuf-relative and unaffected
	   either way. */
	if (t_cfo8 >= 0) {
		unsigned hw2 = 0x4300 | (t_cfo >= 0 ? 0x80 : 0);

		cbyte(0x2D);
		cbyte(0xE9);		/* push.w {(r7,) r8, r9, lr} */
		cbyte(hw2 & 0xFF);
		cbyte(hw2 >> 8);
	} else {
		cbyte(t_cfo >= 0 ? 0x80 : 0x00);
		cbyte(0xB5);		/* push {lr} / push {r7, lr} */
	}
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
