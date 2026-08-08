# The next two emission changes — main-line native, and honest float literals

2026-08-08.  Follow-on from the codegen review and the bare-comparison
work (`38467a4`).  Both issues below came out of measuring that change
on the board: bench gained 0.3% where the host interpreter said 15%,
and the reason reranked the whole queue.

**Outcome, written after the work: issue 2 shipped (`8b55ccc`); issue 1
was void — the premise was wrong, and the section now records why.**

## Issue 1 — the main line can never go native — WRONG, see below

The claim was that bcrun's Thumb translator refuses `main` by design
(`fn_is_main`, backend-thumb.c), so the main program of every
translated BASIC program runs permanently interpreted, and a
`__mmb_main()` wrapper would let it go native.

### What is actually true

`fn_is_main` does one thing only:

    reclaim = thumb_reclaim() && !fn_is_main;

It suppresses *reclaim* — main keeps its bytecode instead of having the
native code written over it — because the loader may still need that
bytecode as an alias.  It does not suppress translation, and never did.

Worse for the premise, bcrun has entered main natively since
`962483175` ("CC: enter main native", 2026-07-31, in v0.9 and every
release after).  Its entry path says so in as many words:

    /* Dispatch the entry exactly like a call site: h_entry points
       at main's BC_NATIVE marker when it was translated, and a
       BASIC program lives in its main line - entering through the
       bytecode quietly interpreted the whole program while every
       benchmark with the work in called functions stayed fast. */

Measured, not read: `THUMB_VERBOSE=1` over the whole test suite reports
`native: main` for 43 of the 44 programs — bench 1166 bc -> 2468 bytes,
the eclipse 1407 -> 2916.  The single exception is `structtest`, whose
main line is 32,556 bytecodes and bails with `size policy`: the
THUMB_MAXFN ceiling, which a wrapper does not move — the same span
would sit in the wrapper.

So there is nothing to gain here and the wrapper is dropped.

### What this means for the bare-comparison measurement

bench's 0.3% was never main-vs-SUB.  Its main line was already native,
so the comparison diamonds were already cheap native branch pairs; the
15% was the host *interpreter*, where each diamond is dispatched.  The
board reading was the honest one all along.

### What is left of the idea

Only the cliff: a main line (or a SUB) over the translated-size ceiling
falls back to the interpreter wholesale, and structtest is proof that
BASIC can write one.  That is the queue's "function splitting for
over-cliff routines" item, which needs to split spans, not rename them.

## Issue 2 — `(MMFLOAT)(3600LL)` defeats the literal-divisor test — SHIPPED

`as_flt()` on an integer value wrapped it in a cast, so BASIC `x/3600`
emitted `mm_fdiv(x, (MMFLOAT)(3600LL))`: `nonzero_literal()` saw a cast,
not a number, and the provably-safe division still went through the
checked call.  Twelve sites in the eclipse alone.

Since the checks became ON ERROR-only, untrapped programs no longer pay
this (they emit bare `/` everywhere), so the issue was confined to
**programs that use ON ERROR** — narrower, but exactly the programs
that pay for every check, and the fix also shrinks the emitted text.

### What shipped (`8b55ccc`)

`float_form_of_int_literal()` beside `nonzero_literal()` in both
translators: a plain decimal integer literal is emitted as the float it
becomes — `3600LL` -> `3600.0`, the same double either way, since up to
fifteen digits every integer is exact.  Refused, keeping the cast: more
than fifteen digits (strtod rounding is then the compiler's business,
not ours), hex from `&H` (`0x10.0` is not a number), and a leading zero
(C reads that as octal).

Measured: the eclipse and bench compile to **byte-identical bytecode** —
cc1 folded the cast anyway, so the only change there is emitted text —
while a trapping program with literal divisors loses its `mm_fdiv`
calls and its .bc shrinks 2% (3169 -> 3103 bytes), digits unchanged.
Gates: cgate 0 both modes, make check 22 ok, fcctests 22/22, qemutests
23/23.  No board run: identical bytecode cannot run differently.

## The board recipe, for whatever comes next

1. `fcc/sync-mmbc.sh`, build mmbc via
   `make -f Makefile.armm0 FUZIX_ROOT=... USERCPU=armm0 mmbc`, strip,
   uusend, install over `/usr/bin/mmbc` keeping `mmbc.prev`.
2. Retranslate and recompile bench + solar_eclipse ON the board;
   digits byte-identical; numbers fresh-boot, screen output.
   References: v0.9 = 49499 grains / 2.311 s; current = 51579 / 2.069.
3. Commits: mmb2c first, then the FUZIX sync commit with the board
   numbers in the message.  The ARM mmbc binary is committed in the
   FUZIX tree as installed.

The serial console moves; FZPORT names the port (COM14 today).  One
fzsh command per long compile — typeahead races the dots otherwise.
