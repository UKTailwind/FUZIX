# TYPE / STRUCT — the firmware semantics mmb2c implements

Distilled from PicoMite V6.03.01 source (file:line references are into
that tree, d:\Dropbox\PicoMite\PicoMite).  This is the contract for
Section 3 of REVIEW-COVERAGE-2026-08-07.md; where the firmware and its
manual disagree, the firmware wins (the manual errata were fixed
2026-08-07).

## Definition

```
TYPE typename
    member [ (d1[,d2...]) ] AS  INTEGER|INT|FLOAT|STRING [LENGTH n]|<earlier typename>
    ...
END TYPE
```

* Registered in a pre-run scan (PrepareProgramExt, MMBasic.c:942-1141):
  a TYPE block may appear anywhere, and DIM v AS T may textually
  precede TYPE T — but a NESTED type reference must be textually
  earlier (registration is in textual order, Commands.c:10027-10048).
* Inside the block only members, blank statements and comments are
  legal.  `:` separates members on one line.
* No suffix forms (x%), no initialisers, no nested TYPE blocks.
* Member array dims are literal decimal integers only; dim must be
  > OPTION BASE.  Element count per dim = dim + 1 - base.
* Limits: 32 types, 16 members, 8 nesting levels, STRING LENGTH 1..255
  (default 255), name length 32.
* The firmware misses a duplicate-member check (first wins, second is
  dead space) — mmb2c refuses duplicates instead.  Member names with
  '.' parse in the firmware but are unreachable — refused here.

## Layout (Commands.c:10056-10089, MMBasic.c:581-608, 1116-1120)

* Members in declaration order.  offset starts at current total.
* INTEGER/FLOAT: size 8, start aligned to 8.
* STRING [LENGTH n]: size n+1 (length byte first, NOT NUL-terminated),
  no alignment.
* Nested struct member: size = inner total_size, start ALWAYS aligned
  to 8 (even when the inner type is all-string with alignment 1).
* Array member: padding applied once before the whole array; size =
  elemsize * product(counts).
* Trailing padding: to a multiple of 8 IF the struct contains any
  numeric anywhere (GetStructAlignment > 1); an all-string struct gets
  none.
* Conformance check: TYPE Line (STRING LENGTH 20, then 4 INTEGERs) is
  56 bytes: name at 0 (21), pad to 24, then 24/32/40/48.

## Declarations

* DIM/LOCAL/STATIC v AS T; DIM a(10) AS T (arrays up to MAXDIM).
* GetMemory zeroes: all struct variables start cleared.
* DIM v AS T = (v1, v2, ...): values flattened in member order, member
  arrays expanded, struct arrays element after element.  Nested-struct
  members in an initialiser are REJECTED by the firmware.  The firmware
  does not length-check string values in INITIALISERS (overrun) —
  mmb2c errors instead.
* Assigning an over-length string to a LENGTH-n member raises
  "String too long" — PROVEN on a real PicoMite 2026-08-07 (an early
  draft truncated; the board said otherwise).  mm_ssetm errors.
* CONST of a struct: no.

## Member access (MMBasic.c:4556-4614, ResolveStructMember :3899-4194)

Dots are ordinary name characters, so `p.x` is one identifier.  The
rule: split at the FIRST dot; if the prefix names an existing variable
(local scope first, then global) of struct type, the whole thing is a
member access; otherwise it is a plain dotted variable name.  When a
struct variable p and a plain variable p.x both exist the plain one is
unreachable — mmb2c refuses the collision.

Forms: v.m · v.n.m (nested) · v.a(i) (member array, index mandatory
for non-struct array members) · a(i).m (struct array element) ·
data(2).items(1).values(4).  A type suffix after a member name parses
and is ignored by the firmware — mmb2c requires it to agree or errors.
Multi-dim member indices linearise exactly like ordinary arrays.

## Whole-struct operations

* v1 = v2 where both sides are whole struct variables or whole struct
  array elements of the SAME type: memcpy(total_size).  RHS may also
  be a struct-returning function (deferred in mmb2c slice A).
* FIRMWARE DEFECTS REFUSED, not reproduced: assigning into or out of a
  NESTED struct member copies the OUTER type's size (overrun /
  over-read, Commands.c:1692 vs :1655).  mmb2c refuses nested-member
  whole-struct assignment in both directions.
* Whole structs are illegal in every expression context (PRINT v
  silently prints nothing in the firmware — refused here).

## Parameters and returns (MMBasic.c:3790-3870)

* `SUB s(p AS T)` / with `()` for whole arrays: always BY REFERENCE;
  BYVAL is ignored for structs; type indices must match.
* FUNCTION f(...) AS T returns a struct via a caller-level temp —
  deferred in mmb2c slice A (refused with a message).

## STRUCT verbs (cmd_struct, Commands.c:8808-9881) — slice A scope

| verb | semantics | mmb2c slice A |
|---|---|---|
| COPY src TO dst (and src() TO dst()) | memcpy, same type, no members; array form needs () on both; dst array >= src | implement |
| CLEAR v / arr() | memset 0 | implement |
| SWAP v1, v2 | 3-way copy, same type; arr(i) ok, arr() not | implement |
| SORT arr().m [,flags] | shell sort whole structs by member; flags bit0 reverse, bit1 nocase, bit2 empties last | refuse (later slice) |
| SAVE/LOAD #n, v|arr()|arr(i) | raw memory image incl. padding | refuse (needs a raw-file runtime entry) |
| PRINT | debug dump | refuse |
| EXTRACT/INSERT | member <-> flat array | refuse (stride-aware later) |

## STRUCT( function (Commands.c:10169-10519) — always integer

* STRUCT(SIZEOF t$) / STRUCT(OFFSET t$, m$) / STRUCT(TYPE t$, m$)
  with LITERAL string arguments: compile-time constants in mmb2c
  (TYPE returns 1 FLOAT / 2 STRING / 4 INTEGER, 0 for nested).
  Non-literal arguments refused.
* STRUCT(FIND ...) — refuse in slice A.

## Misc

* READ into a struct member works; DATA of structs does not exist.
* OPTION BASE 1 + member arrays: firmware sizing/indexing disagree
  (prepare-time vs run-time base) — refuse the combination.
* STRUCT member arrays passed whole to commands: firmware is
  stride-aware in some commands, error 47 elsewhere — out of slice A.
