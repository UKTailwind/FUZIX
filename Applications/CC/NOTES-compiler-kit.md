# The Fuzix Compiler Kit is a separate, live repo — review needed

2026-07-30. **TODO, not yet done.**

`Applications/CC` in the FUZIX tree is a **vendored snapshot**. The real
home of the compiler is

    https://codeberg.org/EtchedPixels/Fuzix-Compiler-Kit    (branch main)

and it is actively maintained — last commit 2026-07-21, nine days before
this note. Recent work there is a sweep of byte-sized `/=` and `%=`
promotion fixes across 8080/8085, ee200, z8, nova, sm83, z80, 6809,
tms7000, 6502 and 8070, plus `f4c6b83e4 primary: permit constants of the
form +11 etc`.

**This was missed when the conformance work was done.** Everything in
`PLAN-conformance.md` and `NOTES-upstream.md` compared our tree against
the *FUZIX* tree's vendored copy, which is the wrong reference for
compiler questions. Some of what looked like our discoveries had already
been fixed at source.

## Checked so far, against the kit rather than the vendored copy

| Our finding | State in Fuzix-Compiler-Kit |
|---|---|
| `trim_constant` dispatch (`t & 0xF0` vs `UCHAR`) | **already fixed** — uses `case CCHAR/CSHORT/CLONG` |
| hex escape nibble order | **already fixed** — `(unhex(c) << 4) \| unhex(c2)` |
| unary plus | **already done**, independently, in `primary.c` (`f4c6b83e4`) — a different approach to ours, which is in `hier10` |
| `sizeof` binding a unary-expression | still broken (uses `hier0`) |
| identifier tail hardcoded 14 vs `NAMELEN` 16 | still broken |
| cast to void | still missing |
| `&func` / `int (*fp)()` typeconv | present, needs a proper read |

## Consequence for PR 1261

[PR 1261](https://codeberg.org/EtchedPixels/FUZIX/pulls/1261) fixes
`trim_constant` in the FUZIX tree. That is a real bug *in that tree* —
the vendored copy is stale and the code that actually builds is wrong —
but the fix already exists at source, so the maintainer may well prefer
a resync of the vendored copy to a targeted patch. Worth saying so in
the PR rather than leaving it looking like we did not know where the
compiler lives.

The other three open PRs (1262 filesys, 1263 devio, 1264 libc) are
unaffected: none of them is compiler code.

## The review to do, both directions

**Theirs to ours.** Our vendored copy is stale by an unknown amount and
we have built a great deal on top of it. Diff properly and decide what
to take: the `/=` `%=` promotion sweep, their unary plus and
`trim_constant` and hex-escape fixes (we have our own equivalents — pick
one), and whatever else the divergence holds. Ours is heavily modified,
so this is a merge, not a pull.

**Ours to theirs.** Our C89 conformance work is target-independent and
substantial, and none of it has been offered:

* cast to void
* `sizeof` binding
* identifier length, and the truncation warning
* auto aggregate initialisers
* elided braces in aggregate initialisers
* struct and union tag scoping
* `typedef` inside a block, and typedef names shadowed by ordinary
  identifiers
* block-scope function declarations
* prototype then K&R definition
* parameter names repeated across declarator levels
* null pointer constant in `?:`
* wide character constants
* declarations after statements (a deliberate C99 borrowing — his call
  whether he wants it)

**And the thing that is probably worth more than any single fix: the
harness.** `hosttest/ctest.sh` plus the c-testsuite runner and
`optest.sh`'s differential-against-gcc, with `ctest-include`. That is
what found all of the above, it is target independent, and it took the
port from 112/175 to 165/175. Offer it first — a maintainer who can
measure conformance can then fix things himself, which is worth more
than a stack of individual patches.

## Method note

Check **content, not commit presence**, and check against the **right**
repo. Both traps have now bitten: cherry-picked commits keep showing as
missing because they get new hashes (that hid the `timer_expired` fix),
and comparing compiler code against the FUZIX tree instead of the kit
made two already-fixed bugs look like discoveries.
