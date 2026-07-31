# mmbc — the translator in C (mmb2c phase 2)

Goal: `mmbc prog.bas` as a native ARM Fuzix binary on the PC3, so the
whole chain — BASIC → C → native ARM — runs on the machine itself.
mmb2c.py stays as the reference implementation; the acceptance rule for
every stage is **byte-identical output to the Python** over the whole
test suite (t1–t8 + solar_eclipse, both plain and --fcc modes).

## Why this is tractable

The Python was audited (2026-07-31) before a line of C was written:

- **No AST.** The translator is multi-pass recursive descent over
  re-tokenized lines; the only IR is strings.  Every expression routine
  returns a `(type, C-text)` pair → C `struct val { int ty; const char
  *txt; }`.
- **No float formatting.**  Number literals pass through as source
  text; nothing ever formats a double.  The scariest byte-identity
  hazard does not exist.
- **No dict-order dependence.**  Everywhere output depends on iterating
  a table, the Python sorts names first (`write`, `global_decls`,
  `report`, bounds tables, `__mmb_clear`).  The unsorted iterations
  (`implied`, `skipped`, `data`) are lists = append order.  C arrays +
  qsort/strcmp reproduce everything.
- **No reclamation.**  The Python never frees; texts live as long as
  the run.  A bump allocator is the honest translation.

## Design

- **Mirror the Python 1:1.**  Same function names, same order, same
  logic, one C file per region:

      mmbc.h            types, caps, tables, prototypes
      mmbc_lex.c        tokenize + string helpers (cvar/clabel/c_string_literal)
      mmbc_sym.c        Sym/Routine tables, lookup/declare/reference
      mmbc_expr.c       e_* grammar, e_name, index/arrayref, calls
      mmbc_stmt.c       walk/statement/statement_inner + do_* handlers
      mmbc_builtin.c    call_builtin/emit_builtin/builtin_raw tables
      mmbc_out.c        emit/raw, global_decls, report, write
      mmbc_main.c       driver (convert, const fixup, argv)

  When output differs from the Python, the mirrored structure makes the
  divergence a two-window diff, not an archaeology dig.

- **Two string pools.**  *Persistent* (symbol access texts, output
  lines, bounds tables, DATA items — lives for the run) and *scratch*
  (expression fragments — reset at each statement boundary).  Eclipse
  numbers: output C is 137K, source 90K; persistent ≈ 250K, scratch
  peak ≈ a few K.  Host build: pools are malloc'd once.  Board build:
  carved from the PSRAM arena (PSRAMIOC_ALLOC, second client after
  cc2), so the 256K process holds only code + fixed tables.

- **Tables as fixed-cap insertion-ordered arrays** with linear lookup
  (symbol counts are hundreds, passes are few — speed is not the
  problem; determinism is).

- **Dialect:** plain C compiled by gcc (host) and arm-none-eabi-gcc +
  Fuzix libc (board), like bcrun/cc2.  Compiling mmbc with our own cc
  is a purity rung for later, not a constraint now.

## Stages (harness-first, per the house rule)

0. **Harness.**  `mmbc/Makefile` host build; `mmbc/mmbctests.sh` runs
   every tests/*.bas through mmb2c.py and mmbc (plain and --fcc) and
   byte-diffs the generated C.  Red until stage 6 closes it; each stage
   has its own earlier gate.
1. **Tokenizer.**  Line reader, tokenize(), string helpers.  Gate: a
   `--tokens` debug dump added to *both* implementations, diffed over
   every .bas in the repo.
2. **Symbols + declaration passes.**  Sym/Routine, pass_routine_names,
   pass_declarations, collect_data, OPTION handling.  Gate: `--symbols`
   dump in both, diffed.
3. **Expressions.**  e_* chain, e_name, arrayref/index, call_args /
   emit_call / pass_arg, const folding (as_int/as_flt).  Gate: t1's
   generated C byte-identical (t1 is expression-heavy, statement-light).
4. **Statements.**  walk, statement_inner, all do_* handlers, block
   stack, GOSUB machinery.  Gate: t1–t5 byte-identical.
5. **Builtins.**  call_builtin/emit_builtin/builtin_raw tables.  Gate:
   t1–t8 byte-identical.
6. **Driver + write().**  report, global_decls, --fcc specials
   (bnd_tables, mm_byref), warnings/skipped plumbing, exit codes.
   Gate: **all 9 tests byte-identical in both modes** + fcctests.sh run
   with mmbc substituted for mmb2c.py = 9/9.
7. **Board.**  Sync into FUZIX Applications/CC, Makefile.armm0 target,
   arena carve, uusend to the PC3.  Acceptance: **solar_eclipse.bas
   translated ON the board**, output byte-identical to the host run,
   then `cc` + `./se.bc` — the machine compiles BASIC to native ARM
   with no other computer involved.

## Byte-identity traps found in the audit (check before debugging)

- Python `%-20s`/`%-5d` field padding — printf-compatible, but mind
  `text[:60]` truncation points in report().
- `list.sort()` on (name, body) tuples for bnd_tables — sort by name
  then body.
- `warnings` dedupe is order-preserving first-occurrence.
- tokenize keeps the *source spelling* of numbers and the canonical
  (upper-cased) spelling of identifiers separately — both matter.
