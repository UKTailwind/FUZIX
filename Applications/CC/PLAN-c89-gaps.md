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

## 3. Struct passing and returning

**What happens.** `type_iterator.c:250` refuses a struct parameter with
"cannot pass objects", and returning one fails the same way. Everything
downstream then cascades.

**Fix.** This one needs an ABI decision before any code:

* **Return** via a hidden first argument — the caller allocates space
  and passes its address, the callee copies into it. This is what the
  gcc-built userland does, and matching it matters if compiled code
  ever has to call gcc-built code.
* **Pass** by copying onto the argument stack. Arguments are already
  word-aligned and `target_argsize()` already reports the real size, so
  the frame arithmetic is in place.

Then: drop the refusal in the frontend, make struct assignment emit a
block copy, and give the backend a way to move n bytes. The interpreter
already has `vcopy()`, so this can start as a libcall and become an
opcode later if it matters.

Check first whether plain struct *assignment* (`b = a;`) already works
— the probe cascaded from the earlier errors, so that is currently
unknown, and it may be a smaller job than it looks.

Medium sized, and the ABI choice is the part to get right.

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

1. **Identifier length and the truncation warning**, with block scope.
   An hour, and it removes the only silent-wrong-code case.
2. **Block scope.** Small, contained, and the probe already exists.
3. **Struct passing and returning.** Establish the ABI first.
4. **Bitfields.** Schedule against wanting to compile Fuzix sources,
   not before.

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
