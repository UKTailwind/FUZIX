# The next two emission changes — main-line native, and honest float literals

2026-08-08.  Follow-on from the codegen review and the bare-comparison
work (`38467a4`).  Both issues below came out of measuring that change
on the board: bench gained 0.3% where the host interpreter said 15%,
and the reason reranked the whole queue.

## Issue 1 — the main line can never go native

bcrun's Thumb translator refuses `main` by design (`fn_is_main`,
backend-thumb.c: the entry point is reached through the interpreter),
and mmbc puts the whole BASIC main line inside C `main()`.  So the main
program of every translated BASIC program is **permanently
interpreted**, at ~15-30 cycles of dispatch per bytecode op, while any
SUB that fits under the ~40K translated-size ceiling runs native.

bench.bas is one loop in the main line: every one of its iterations
pays interpreter dispatch on every op.  That is why removing the
comparison diamonds moved the host interpreter 15% but the board only
0.3% — the board's SUB-heavy programs were already mostly native, and
the main-line ones were mostly libcalls.  The interpreted main line is
where the remaining dispatch overhead lives.

### Solution

Emit the main-line statements into their own function and have `main`
call it:

    static void __mmb_main(void);
    int main(void) {
        /* heap init, DATA init, __mm_e binding - as today */
        __mmb_main();
        /* channel close, flush - as today */
        return 0;
    }
    static void __mmb_main(void) {
        unsigned __mark = mm_mark(); (void)__mark;
        /* the translated main line, verbatim */
    }

`__mmb_main` is then an ordinary function: translated to Thumb at load
if it fits, interpreted if it does not — today's behaviour, so a giant
main line loses nothing.  `main` itself stays interpreted and shrinks
to a handful of calls that run once.

Details that need care:

* `__mark` moves into the wrapper (statement `mm_release(__mark)`
  references it lexically).
* The main line's variables are BASIC globals already — nothing moves.
  `H` stays file-scope; `mm_heap` setup stays in `main`.
* GOSUB labels and the return switch are statements — they travel into
  the wrapper together.  The refusal of cross-routine GOSUB already
  guarantees no label escapes.
* `END` emits the runtime exit and works from any depth; the implicit
  end-of-program becomes plain `return` (the wrapper is void — check
  write()'s tail emission and the `return 0` it prints today).
* ON ERROR: `mm_err_bind` stays in `main` before the call; the
  wrapper's guarded statements reference `__mm_e` (file-scope pointer)
  exactly as SUB bodies do today.
* mmbc's report/global_decls ordering must not change — the wrapper is
  emitted where the main body is emitted today, plus a forward
  declaration beside the other prototypes.

Acceptance: all gates (cgate 0 both modes, fcctests, qemutests), then
the board per the benchmark method note — fresh boot, screen output,
on-board mmbc+cc.  bench grains is the number to watch: its ~4K main
span should translate (expansion ~2.4-3.8x, well under 40K), and the
interpreter-to-native jump on its non-libcall ops is the prize.  The
eclipse should not move (its main is 85 lines of setup; the work is in
SUBs) — digits byte-identical as always.  `THUMB_VERBOSE=1` names any
bail and its reason if grains does not move.

## Issue 2 — `(MMFLOAT)(3600LL)` defeats the literal-divisor test

`as_flt()` on an integer value wraps it in a cast, so BASIC `x/3600`
emits `mm_fdiv(x, (MMFLOAT)(3600LL))`: `nonzero_literal()` sees a cast,
not a number, and the provably-safe division still goes through the
checked call.  Twelve sites in the eclipse alone.

Since the checks became ON ERROR-only, untrapped programs no longer pay
this (they emit bare `/` everywhere), so the issue is now confined to
**programs that use ON ERROR** — narrower, but exactly the programs
that pay for every check, and the fix also shrinks the emitted text.

### Solution

In `as_flt()` (both translators), when the value's code is a plain
decimal integer literal — digits with an optional `LL` suffix, the same
shape `is_literal_number()` recognises — emit it as a float literal
instead of a cast: `3600LL` becomes `3600.0`.  Bit-identical value
(both are the double 3600.0), and `nonzero_literal()` then reads it
directly.  Hex (`0x...LL`, from `&H`) keeps the cast — appending `.0`
to hex is not a number.  cc1 folds the cast today anyway, so the .bc
for non-divisor uses is unchanged; only the ON ERROR divisor sites gain
the inline `/`.

## Plan

Order: issue 2 first (small, self-contained, re-cuts the emitted-text
baseline once), then issue 1 (structural).  For each:

1. mmb2c.py change, then the mirror in mmbc/ (`mmbc_expr.c` for
   as_flt; `mmbc_out.c`/`mmbc_stmt.c` for the wrapper emission —
   find write()'s main head/tail and the prototype block).
2. Gates: mmbc host build, cgate.sh **0** over the suite in both
   modes, fcctests 22/22, qemutests 23/23.  make-run's SYSTEM-spawn
   failures are pre-existing; diff the failure pattern against a
   stashed baseline, nothing new.
3. Board: `fcc/sync-mmbc.sh`, build mmbc via
   `make -f Makefile.armm0 FUZIX_ROOT=... USERCPU=armm0 mmbc`, strip,
   uusend, install over `/usr/bin/mmbc` keeping `mmbc.prev`.
   Retranslate and recompile bench + solar_eclipse ON the board;
   digits byte-identical; numbers fresh-boot, screen output.
   References: v0.9 = 49499 grains / 2.311 s; current = 51579 / 2.069.
4. Commits: mmb2c first, then the FUZIX sync commit with the board
   numbers in the message.  ARM mmbc binary is committed in the FUZIX
   tree as installed.

The serial console moves; FZPORT names the port (COM14 today).  One
fzsh command per long compile — typeahead races the dots otherwise.
