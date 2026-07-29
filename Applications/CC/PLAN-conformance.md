# c-testsuite conformance — analysis and plan

2026-07-29. Standing at **165 of 175** on the C89 subset of the
`single-exec` set (https://github.com/c-testsuite/c-testsuite).

**Bitfields are the only genuine C89 gap left.** Of the other nine
failures, eight are constructs that are not C89 at all and are
correctly refused, and one is an object format limitation. No test
answers *wrongly*: everything left either refuses to compile or, in
00189's case, refuses to load.

    bash hosttest/ctest.sh        # run it
    bash hosttest/ctestwhy.sh     # what cc1 objected to, counted
    bash hosttest/ctestshow.sh    # each failure with its offending line
    bash hosttest/ctestc89.sh     # is it our gap, or not C89 at all?

---

## The 10 that remain

### A. Genuine C89 gap — 1

| test | what |
|---|---|
| 00218 | **bitfields.** See section 6, and `PLAN-c89-gaps.md` section 4. `unsigned x : 1;` is C89; the enum-typed bitfield in that test is not, so gcc refuses the file too. Deliberately not attempted: see the note below. |

### B. Our own limit, not dialect — 1

| test | what |
|---|---|
| 00189 | `&fprintf`. A runtime library symbol is resolved by *index* and has no address, so the object format cannot express its address at all. bcrun refuses it by name at load time rather than storing an index where a code address belongs. Fixing it means giving library functions real addresses - a thunk per imported function, or a relocation kind that resolves to one - which is an object format change, not a compiler gap. |

### C. Not C89, correctly refused — 8

Recorded so nobody re-investigates. gcc `-std=c89 -pedantic-errors`
rejects all of these too.

00170 and 00209 incomplete or forward-declared enums · 00207 variable
length arrays · 00216 compound literals · 00213 and 00214 statement
expressions (`({ ... })`) · 00210 `__attribute__` · 00219 `_Generic`

---

## Done 2026-07-29: 155 -> 165

Commit `258c89528`. Each has a sample in `hosttest/samples/` so it
cannot regress quietly, and all four new samples were also compiled
**on the PC3** and diffed against the gcc reference - identical.

| test | gap | sample |
|---|---|---|
| 00202 | unary plus, not implemented at all | `unaryplus.c` |
| 00198 | `typedef` inside a block | `blocktypedef.c` |
| 00129 | a typedef name shadowed by an ordinary identifier | `namespace.c` |
| 00078 | block-scope function declarations | `namespace.c` |
| 00114 | prototype then K&R definition | `namespace.c` |
| 00124 | parameter names repeated across declarator levels | `namespace.c` |
| 00144 | null pointer constant in `?:` | `namespace.c` |
| 00098 | wide character constants | — |
| 00205 | elided braces in aggregate initialisers | `braceelide.c` |
| 00200 | cc2 `MAXSYM` 512, inherited from the 8-bit targets | — |

Three of these are worth remembering for the shape rather than the fix:

* **The label namespace was never broken.** 00129 was filed as a label
  problem and labels turned out to be fine. The real blocker was that
  `is_typedef()` still answered yes after an ordinary identifier had
  shadowed the typedef, so `myt = 5;` was parsed as a *declaration*.
  Reducing the test rather than reading its title is what found it.
* **Brace elision is half a comma problem.** The visible failure was
  "expected {". The half that would have bitten later is that an elided
  group must leave the separating comma for its parent - eating it makes
  the parent think its own list ended early, which produces no
  diagnostic at all, just an object of the right shape with the wrong
  contents. `braceelide.c` therefore checks every field of every group.
* **Wide strings are refused, not accepted.** `L'x'` is taken as a plain
  character constant, which is honest here - wchar_t is an integer type
  and the character set is ASCII. `L"..."` would have to come back as a
  narrow array, right in bytes and wrong in element size, so it gets a
  diagnostic instead. Refusing beats miscompiling.

### Why bitfields were not attempted

The parser and layout halves are tractable. The access half is not, at
least not safely in one pass: a bitfield *write* is a read-modify-write
that has to evaluate the container's address exactly once, and the tree
has no temporary mechanism for holding it. Re-evaluating is fine for
`s.f = x` and wrong the moment the address expression has side effects,
which is a silent wrong answer rather than a diagnostic.

That is the one failure mode this plan has consistently said is worse
than the gap - so bitfields keep erroring loudly until they can be done
whole. `PLAN-c89-gaps.md` section 4 already schedules them against
wanting to compile real Fuzix sources, which is the right trigger:
`console.c`, `util/fat.c`, `util/stty.c` and `resolv.h` all use them.

### Also landed alongside

**`#include <stdio.h>` works on the board for the first time.** cpp has
no built-in include path - `include_paths[]` is only ever filled from
`-I` - so every on-target program had to declare its own
`int printf();`. `ccbc.c` now passes `-I<libpath>include` and the
`ctest-include` headers, which describe what bcrun actually provides,
are installed at `/usr/lib/cc/include`. `/usr/include` is the wrong
answer: those headers are the Fuzix libc's, for native binaries.

**bcrun printf length modifiers.** `%ld` had no handling at all, fell
through to the default and printed itself literally - and consumed no
argument, so every conversion after it took the wrong one. `%i`, `%o`
and `%X` were missing too.

---

## Appendix: the original 38, as first classified

The suite's own tags are **not reliable** for deciding what we should
support: several tests tagged `c89` use VLAs, statement expressions,
`__attribute__` or `_Generic`. `ctestc89.sh` asks gcc instead, with
`-std=c89 -pedantic-errors`. If gcc refuses it too, refusing it is
correct behaviour and not a gap.

| | count |
|---|---|
| genuine gaps in our C89 | **18** |
| not C89 at all — gcc refuses them too | **20** |

Of the 20, three hide a genuine C89 gap behind a non-C89 construct and
are listed under the gaps anyway (00212, 00218, and the `%f` in 00195).

---

## 1. Silent miscompiles — DONE, commit `f5a1864ef`

**A compiler that refuses a program is a nuisance. One that accepts it
and produces the wrong answer is a liability.** These six all compiled
without a diagnostic and then misbehaved, so nothing warned you.

All six are fixed. Regression samples: `ptrarray.c`, `escapes.c`,
`statics.c`, and `goto.c` from the label fix before them. Kept below
because the *reasons* are worth having when the same shapes recur -
two of the six were the type encoding not distinguishing a pointer from
an array, and that will come up again.

### 1.1 Pointer to array indexes with the wrong scale — 00130

    char arr[2][4], (*p)[4];
    arr[1][3] = 2;
    p = arr;
    p[1][3]          /* gives 0, should be 2 */

`p[1]` must advance by `sizeof(char[4])` = 4. It does not. Reduced case
in `/tmp/p130.c`; `arr[1][3]`, `*q` and `*v` are all correct, so it is
specifically the pointer-to-array scale, i.e. `type_ptrscale` on a type
whose target is an array. This is the worst of the six: 2D arrays are
ordinary code and the answer is silently wrong.

### 1.2 `sizeof expr` without parentheses grabs too much — 00038

    if (sizeof 0 < 2) return 1;      /* we take it */

Must parse as `(sizeof 0) < 2` — sizeof binds a *unary-expression*. We
parse `sizeof (0 < 2)`, whose value is 4, so the condition is true.
`get_sizeof()` in expression.c needs `hier10()` for the unparenthesised
form, not a full expression.

### 1.3 Hex escapes are mis-lexed — 00177

    printf("%d\n", '\x01');   /* prints 224 */
    printf("test \x40\n");    /* prints "test " */

Octal escapes (`'\1'`, `'\10'`, `'\100'`) are correct; `\x` is not, in
both character constants and string literals. cc0's escape handling.

### 1.4 String literal compared against a null pointer — 00112

    return "abc" == (void *)0;   /* returns 1, must be 0 */

### 1.5 `switch` whose body is not a compound statement — 00051

    switch (x)
        case 0:
            ;

Legal, if strange. Also exercises `goto` out of a nested switch and a
label inside a switch block. Returns 1 where it should return 0.

### 1.6 A static local aliased the string literals — 00182

`bcrun: bad address` on a real program, and much the most interesting
of the six.

`gen_data_label()` checks the segment and emits a BSS symbol when it is
in bss. `gen_literal()`, which is what a *numbered* label uses, always
said `BC_SYM_DATA`. A static local is written out as a numbered label,
so `static int d[4]` inside a function got a data address and aliased
the string literal area: assigning to `d[3]` rewrote the fourth word of
the literals, and a `printf` format string later became whatever had
been stored there.

Worth remembering as a shape: the symptom was *other functions*
printing rubbish, nowhere near the code at fault.

### 1.7 Left behind

`char (*cp)[4] = carr;` is still refused with `type mismatch`. The
auto-initialiser half is fixed - a pointer to an array is a scalar and
initialises like one, which is now `type_is_pointer_object()` - but a
decayed `char[2][4]` and a declared `char (*)[4]` get **different type
codes** even though they are the same type, because the array symbol
differs. Assignment accepts it and initialisation does not, which is
itself inconsistent.

Not a miscompile, so it is not group 1, but it belongs with the type
work in section 5.

---

## 2. A trivial C89 gap worth fixing immediately

### 2.1 Cast to void — 00212

    (void)printf("Ok\n");

We say `invalid type conversion`. This is C89, it is idiomatic, and
`typeconv()` simply has no case for a cast to `void`. Small fix, and it
is the sort of thing that appears everywhere in real source.

*(00212 is also tagged non-C89 by gcc because the file uses `long long`
elsewhere - which we support. The `(void)` gap is real regardless.)*

---

## 3. Initialisers for automatic aggregates — 00117, 00118, 00185

    int x[] = { 1, 0 };
    struct { int x; } s = { 0 };

`not a valid auto initializer`. Statics and globals initialise fine; a
local aggregate does not. Three tests, but far more important than that
count suggests - this is everyday C, and its absence will be felt
immediately in any real program.

Needs the initialiser walker to emit stores into the frame rather than
data, which is why it was left out.

---

## 3a. DONE: implicit "return 0" for main

Falling off the end of `main` returned whatever was in the accumulator,
so a program ending in a `printf` exited with the character count. That
is what stopped `printf` returning a count, which it is supposed to do:
making printf correct turned two passing tests into exit 3 and exit 12.
The fault was in main, not printf, and both are now right.

cc1 could not tell it was compiling `main` - names reach it as ids and
cc0 owns the string table. Solved by interning `"main"` as the very
first user symbol in cc0, so it always carries `T_MAIN` (= `T_SYMBOL`);
cc0 checks that promise at startup. Costs one table entry per object.

**Note what this actually is.** gcc with `-std=gnu89` does *not* zero
main's return either - it leaves the same garbage we used to. Zeroing
is a C99 rule. We apply it deliberately, because it is what every
modern compiler does and what the conformance suite assumes, but that
means gcc-in-C89-mode cannot be the oracle for it. `samples/mainret.c`
therefore ends with an explicit `return 0` and says so; the coverage
for the implicit case is c-testsuite 00206 and 00212.

### Found alongside: the harness never compared exit status

`optest.sh` threw gcc's exit status away and only printed ours. The
whole point of a differential test is that the oracle decides, and the
program's status is part of its answer - it is exactly where this bug
lived. Now compared, which also puts real checks on the samples that
return meaningful values (min1 111, sw2 70, sieve 46, strs 16, rpn 7,
width3 2).

## 4. Runtime library

| test | needs |
|---|---|
| 00195 | `printf` `%f` — the long-standing known gap |
| 00186 | `sprintf` |
| 00187 | `FILE`, `fopen`, `fclose`, `fgetc`, `fputc` |
| 00189 | `fprintf`, `stdout`, and a prototype with `...` |

`%f` is already recorded in STATE.md as the next thing worth doing;
this run puts a number on it. A minimal `FILE` layer over the `open`/
`read`/`write`/`close` that bcrun already provides would take 00187 and
00189 together, and would matter far beyond these two tests.

---

## 5. Smaller parser gaps

### 5.1 Struct tag scope — DONE

Tags are now scoped like any other name. Three things were needed and
the third is the one worth remembering:

* `find_struct` searched the table **forwards** and took the first
  match, so the outermost tag always won. Ordinary identifiers have
  always searched backwards for exactly this reason.
* tags were never discarded at the end of a block, because
  `pop_local_symbols` keeps anything whose storage class is at or above
  `S_STATIC`, and `S_STRUCT` is well above it.
* **position in the table cannot decide which tags to discard.** A file
  scope tag sits above `symtab` while `block_base` may still *be*
  `symtab`, so "above the mark" does not mean "declared in this block".
  Tags therefore carry `S_TAGLOCAL`, set when the tag is declared
  inside a function body, and are allocated as local so `local_top`
  moves past them - without that an inner block's pop reaches back and
  frees a tag belonging to the block outside it.

`in_funcbody` was added for this. Note that the existing `funcbody`
looks like it says the same thing and does not: it is set *after*
`function_body()` returns, meaning "one has just been parsed".

`samples/tagscope.c` covers it, including that the file scope tag is
visible again once an inner block has ended, that unions behave the
same, and that a tag first seen inside one function does not leak into
the next.

### 5.2 Prototype then K&R definition — 00114

    int main(void);
    int main() { return 0; }

`type mismatch`. `f(void)` and `f()` are compatible in C89 and this
pattern is everywhere in period source.

### 5.3 Block-scope function declarations — 00078

    int main() { int f1(char *); ... }

`can't size type` — we try to allocate a frame slot for it. A
declaration with no storage should not reach `assign_storage`.

### 5.4 Nested declarator parameter names — 00124

A function returning a function pointer, with parameter names repeated
between the two parameter lists. We report `duplicate name`; the two
lists are different scopes.

### 5.5 Large constant initialiser lists — 00205

`expected "{"` on a long list of parenthesised constant expressions.
Not yet diagnosed.

### 5.7 Tags, members and ordinary identifiers share a namespace — 00129, 00198

    typedef struct s s;
    struct s { ... } s2;
    ...
    struct s s;        /* a variable also called s */

C keeps struct/union/enum *tags*, struct *members*, and ordinary
identifiers in separate namespaces; we appear to conflate them, and
report `invalid value` on the declaration. Two tests, and the pattern
is common in real headers.

### 5.6 Wide character constants — 00098

`L'\0'`. C89, but genuinely niche for this target. Lowest priority of
the group.

---

## 6. Bitfields — 00218

    enum tree_code code : 8;
    unsigned side_effects_flag : 1;

The enum-typed bitfield is not C89 (C89 allows only `int`, `unsigned
int`, `signed int`), so gcc refuses this test too - but we support no
bitfields at all, which *is* a C89 gap. Already planned in
`PLAN-c89-gaps.md` section 4 and still the largest single item.

---

## 7. Not C89 — deliberately out of scope

Recorded so nobody re-investigates them. gcc `-std=c89
-pedantic-errors` refuses all of these:

* **Mixed declarations and code** (C99) — 00129, 00154, 00173, 00175,
  00185, 00187, 00198, 00210, 00214. **Nine tests, the largest single
  group of failures.** See the decision below.
* **Variable length arrays** — 00207
* **Compound literals** — 00216
* **Statement expressions `({ ... })`** (GNU) — 00213, 00214
* **`__attribute__`** (GNU) — 00210
* **`_Generic`** (C11) — 00219
* **Incomplete/forward-declared enums** — 00170, 00209
* **cc2 symbol table full** — 00200 (`too many symbols`, MAXSYM 768).
  A limit rather than a bug, but worth raising for this target since
  memory is not the constraint it is on a Z80.

### DONE: mixed declarations and code

Taken deliberately. It is C99 rather than C89, so the compiler is now
**C89 plus declaration-after-statement**, on the grounds that everyone
writes it and refusing it is a nuisance out of all proportion to the
standard it comes from. A pure relaxation: nothing legal before changes
meaning.

Done in `statement_block()`, where a declaration really is allowed -
*not* in `statement()`, where a disabled `declaration_block()` call had
been sitting marked "C99 for later if we want it". That call would also
have accepted `if (x) int y;`, which is legal in no dialect. The
comment there now says so.

It unblocked all nine tests but only gained four, because five had a
second unrelated gap behind it: 00129 and 00198 the tag/typedef
namespace question (5.7 below), 00187 needs `FILE`/`fopen`, 00210 uses
`__attribute__` and 00214 statement expressions. Worth remembering when
estimating from failure counts - a rejection hides whatever comes after
it.

`samples/mixdecl.c` covers it, including that scoping, shadowing and
re-initialisation behave the same wherever the declaration sits.

---

## Recommended order

1. ~~**The six miscompiles** (section 1).~~ **DONE** `f5a1864ef`.
   137 -> 143, and nothing left answers wrongly.
2. **Cast to void** (2.1). Minutes, and C89. **Next.**
3. **Automatic aggregate initialisers** (section 3). Everyday C.
4. **Declaration after statement** (section 7 decision). Nine tests,
   small change, if you want it.
5. **`%f`, then `sprintf`, then a minimal `FILE`** (section 4). These
   are now two of the four remaining non-rejection failures.
6. **The smaller parser gaps** (section 5), tag scope first, and 1.7
   with them.
7. **Bitfields** (section 6), scheduled against compiling real Fuzix
   sources as `PLAN-c89-gaps.md` already says.

Expected position after 2–5: **around 160 of 175**, with the remainder
being genuinely-not-C89 constructs and bitfields.

## Do not lose

Everything in section 1 should get a sample in `hosttest/samples/` as
it is fixed, the way `goto.c` was added for the label collision. The
conformance suite is a moving external dependency; the samples are
ours, they run in `all.sh`, and they are what stops a fix regressing.
