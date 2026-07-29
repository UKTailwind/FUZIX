# Closing the C89 dialect gaps — plan

2026-07-29. Four known departures from C89: block scope, 14-character
identifiers, struct passing and returning, bitfields.

Each was probed on the host chain first, so what follows is measured
behaviour rather than a reading of the parser. That changed the
priorities: the cheapest fix turns out to have the worst failure mode.

---

## Measured state

| Gap | Behaviour today | Diagnostic? |
|---|---|---|
| Identifier length | two names differing after char 15 become **one variable** | **none** |
| Block scope | inner declaration is dropped, both names alias | error, then wrong code |
| Struct pass/return | refused outright | loud |
| Bitfields | declarator unparsed, fields become full words | loud |

The probe for identifiers:

    int abcdefghijklmnopqrstuvwxyz_one;
    int abcdefghijklmnopqrstuvwxyz_two;

    gcc:  id 1 2
    ours: id 2 2

Two distinct variables silently become one, with nothing printed. That
is the only one of the four that can corrupt a working program without
saying anything, which is why it is first below despite being the
smallest.

`hosttest/samples/scope.c` is the block scope probe and is in the tree.
`sibling` and `nested` already pass — blocks *do* mark and pop symbols.
Only shadowing fails.

---

## 1. Identifier length — smallest fix, worst failure mode

**What happens.** `symtab.h` sets `NAMELEN 16`, so names are
significant to 15 characters and anything longer collides. C89 requires
31 significant characters for internal identifiers.

**Fix.** Raise `NAMELEN` to 32. The name table is `MAXNAME` (1024)
entries, so the cost is 16 KB more BSS in cc0 — nothing on a 256 KB
process, but real on a Z80, so make it conditional on the target rather
than raising it for everyone.

**And warn on truncation regardless.** Even at 31 characters a
collision is possible, and silence is the actual defect here. cc0
should say so when it truncates a name. That is worth doing even if the
length never changes.

**Trap.** `symtab.h` is shared by cc0 and cc2 and describes an on-disk
table. Both must be rebuilt together, exactly like `bytecode.h` — add
it to the Makefile dependencies at the same time or the next stale
build will look like a corrupt symbol table.

Small and self-contained. Do it with the scope work.

---

## 2. Block scope — the pressing one

**What happens.** `statement_block()` already calls
`mark_local_symbols()` at `{` and `pop_local_symbols()` at `}`, and
`find_symbol()` already walks backwards so the innermost match wins.
The machinery is there. What is missing is that
`update_symbol_by_name()` rejects *any* duplicate local name without
knowing which block it was declared in:

    /* Local symbols don't duplicate. TODO awareness of block level */
    if (sym && !global)
        error("duplicate name");

So an inner `int x;` that shadows an outer one is refused, the
declaration is dropped, and both names then refer to the same storage.
Shadowing a parameter additionally trips "storage class mismatch",
because the parameter symbol is passed to `update_symbol()` which
compares storage classes.

**Fix.** Track where the current block's symbols start and scope the
duplicate check to it:

* keep a `block_base` alongside the existing mark — `statement_block()`
  already has it in `ltop`, it just needs saving and restoring around
  the recursive call so it names the *current* block;
* in `update_symbol_by_name()`, only report a duplicate when the symbol
  found is above `block_base`, i.e. declared in this same block;
* otherwise pass `NULL` into `update_symbol()` so a new symbol is
  created that shadows the outer one. That also disposes of the
  parameter case, since parameters sit below the function's outermost
  block base and are never "in this block".

Nothing else needs to change: the backward search already resolves to
the innermost symbol, and `pop_local_symbols()` already unwinds at `}`.

**Deliberately not doing** stack slot reuse. Shadowed locals will each
get their own frame offset, so a function with many blocks uses a
larger frame than it needs. That is wasteful, not wrong, and reusing
offsets means tracking a high-water mark per block — a separate change
worth making only if frames turn out to be a problem.

**Verify with** `hosttest/samples/scope.c`, which should print
`shadow 11`, `sibling 7`, `nested 9`, `param 5`. Add it to `all.sh`
once it passes.

---

## 3. Struct passing and returning — RESUME HERE

**ABI decided:** hidden pointer for *all* struct returns, no size split.
Passing is by copying onto the argument stack.

### Done: assignment, commit `74afa0885`

`b = a` works, including globals both ways, nested structs, arrays of
structs and `g = b = a`. `samples/struct2.c` covers it and is in the
suite.

Two facts from that work shape everything below:

* **A struct valued expression is represented by its address.** There
  is no loading an aggregate into the accumulator, so `hier1` keeps
  both sides as addresses rather than calling `make_rval`, which would
  insert a dereference.
* **cc2 cannot size a struct.** Its `typesize()` returns 4 for anything
  it does not recognise, because struct sizes live in cc1's symbol
  table (`type_sizeof`, which reads `s->data.idx[1]`). **Any aggregate
  operation must carry its length from the front end.** `BC_COPY` takes
  it as a u16 immediate from the node's `value`.

### Attempted and reverted: passing

The design is sound and should be rebuilt as-is. What was written:

* `type_iterator.c` — drop the `cannot pass objects` refusal for
  structs, keep it for functions;
* `target-thumb.c` — `target_argsize()` uses `type_sizeof` for
  aggregates (`target_sizeof` errors on them) and rounds up to a whole
  number of words;
* `token.h` — `T_ARGSTRUCT` (0x1211), wrapping a struct argument, child
  evaluates to the address, `value` holds the length;
* `expression.c` `call_args()` — wrap a struct argument in it before
  `*argsize += target_argsize(...)`;
* `bytecode.h` — `BC_PUSHN` u16, copy that many bytes from the address
  in A onto the stack, rounded up to words;
* `backend-bcode.c` — `gen_push()` emits `BC_PUSHN` when the node is
  `T_ARGSTRUCT`; `gen_node` does nothing for it, the address is already
  in A;
* `bcrun.c` — `BC_PUSHN`: `sp -= roundup(len); vcopy(sp, A, len);`

**It fails with cc2 reporting `sp`** — the stack depth counter is not
back to zero at the epilogue, so the push accounting and what
`T_CLEANUP` subtracts do not agree.

### What to do first, before writing any code

Work out the invariant `sp` in `backend-bcode.c` is meant to satisfy,
and exactly which node gets pushed for an argument. Specifically:

* `codegen_lr()` in `backend.c` does `codegen_lr(n->left)`, then
  `gen_direct(n)`, then `gen_push(n->left)` — that is the *generic*
  binary-node push, not an argument-specific one;
* arguments are a `T_ARGCOMMA` chain built right to left in
  `call_args()`, so `left` is the remaining arguments and `right` is
  this one;
* **a single argument is not wrapped in `T_ARGCOMMA` at all** - it is
  the direct left child of `T_FUNCCALL`. That asymmetry is the most
  likely place the accounting went wrong;
* `T_CLEANUP` subtracts `n->right->value`, the `argsize` accumulated by
  `call_args()`, and is handled in `gen_direct` not `gen_node`.

Do not guess at the arithmetic. A code generator that balances on the
samples and quietly miscompiles something else is worse than one that
refuses to build.

### Then: returning, via the hidden pointer

Reuses the same machinery. The pieces:

* a function returning a struct gains a hidden first parameter holding
  the address of caller-allocated space, which shifts every real
  parameter's offset;
* at the call site the front end reserves a temporary in the caller's
  frame and passes its address as that first argument;
* `return expr;` in such a function becomes a `BC_COPY` to the hidden
  pointer, then returns the pointer in A;
* the call's value is then an address, which is what the rest of the
  struct handling already expects - so `f().x` and `a = f()` fall out.

---

## 4. Bitfields — the big one

**What happens.** `struct.c` parses a member as type-plus-declarator
and has no notion of `: width`. The colon is rejected with "expected
;", the field is then built as a full-width member, and the struct is
the wrong size — 16 bytes against gcc's 8 in the probe. Values happened
to print correctly because each field had a whole word to itself.

**Fix.** Three pieces, none of them optional:

* **Parser.** Accept `: constant-expression` after the declarator in
  `struct_declaration()`, including the anonymous `int : 3;` padding
  form and `int : 0;` for alignment.
* **Layout.** `struct_add_field()` stores three words per field — name,
  type, offset. A bitfield also needs a bit offset and a width, so the
  record grows to four words or the offset word gets packed. That
  changes the struct index format, which `idx_copy()` and every member
  lookup read, so it touches more than it first appears.
* **Access.** A member read becomes load, shift, mask, and sign-extend
  if signed; a write becomes read-modify-write. Both need to happen in
  the frontend's member handling, since the backend sees only loads and
  stores.

This is the largest of the four by a wide margin, and it is the one
Alan Cox left out deliberately. It errors loudly, so nothing silently
misbehaves while it is missing.

Worth noting what it would buy: bitfields are what stand between this
compiler and real Fuzix sources — `console.c`, `util/fat.c`,
`util/stty.c` and `resolv.h` all use them. That is a much larger goal
than single-file programs, so bitfields should be scheduled against
that ambition rather than on their own.

---

## Recommended order

1. ~~**Identifier length and the truncation warning.**~~ **DONE**
   2026-07-29, commit `1b97bbd62`. 31 significant characters on this
   target, the limit taken from `NAMELEN` in both places that had their
   own number for it, and a warning when characters are dropped.
2. ~~**Block scope.**~~ **DONE** in the same commit. `block_base`, and
   the duplicate check scoped to it. Verified in both directions -
   four kinds of genuine duplicate still rejected, shadowing of an
   outer local and of a parameter accepted. `samples/scope.c` passes on
   the host and on the PC3.
3. **Struct passing and returning.** Next. Establish the ABI first.
4. **Bitfields.** Schedule against wanting to compile Fuzix sources,
   not before.

### Left behind by 1 and 2

* Shadowed locals each take their own frame slot instead of reusing the
  space, so a function with many blocks has a larger frame than it
  needs. Wasteful, not wrong; fixing it means a high-water mark per
  block.
* `NAMELEN` lives in `symtab.h`, which cc0 writes and cc2 reads. A
  rebuilt cc0 against an old cc2 produces an object that builds, runs,
  and prints nothing. `Makefile.host` now depends on it for both, as it
  already did for `bytecode.h` - the cross Makefile always did.

## One thing to fix alongside

`hosttest/optest.sh` prints cc1's errors and then carries on regardless.
That is how `optest.c`'s `type mismatch` at line 136 went unnoticed for
a whole day — the suite passed while cc1 was rejecting the input, and
it only surfaced when the on-target driver, which stops on error,
refused to build it. The harness should fail on a cc1 error unless the
test explicitly expects one.

That matters more than usual for this work: all four gaps above are
things cc1 complains about, and a harness that ignores its complaints
will not tell you when they are fixed.
