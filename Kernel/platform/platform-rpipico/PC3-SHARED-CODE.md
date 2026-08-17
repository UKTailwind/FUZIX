# What could live in kernel flash, and what robots' 300 K is made of

Written 2026-08-17, after PETSCII Robots at 75 blocks of an 84-block
pool could not coexist with a 10-block MOD player.

The mechanism already exists and is proven: `libm_table.c` exports the
maths library from kernel flash and `PICOIOC_LIBM` hands a program its
address, which the program then **calls directly** — no MMU, no MPU,
`PROGBASE` is an array inside the kernel's own address space, so an
ordinary `bl` reaches it and the 1.3 us syscall crossing does not apply.
`GFXIOC_FONTADDR` does the same for the nine built-in fonts. What
follows is what else is worth putting there, judged against the one
constraint that matters here.

---

## 1. The constraint: flash code contends with PSRAM data

Flash and PSRAM are reached through the **same QMI** and share the XIP
cache. `default_text_excludes.incl` already records what that costs, in
the note explaining why `FRAMEBUFFER MERGE` is deliberately *not* moved
to flash:

> both its sources are in PSRAM ... running the loop from flash would
> put its instruction fetches in direct contention with the 76,800 bytes
> of data it is streaming, and each would evict the other. It belongs in
> RAM precisely BECAUSE its data is external.

So the rule for a shared-flash candidate is:

* **Good** — works on registers, the stack, or `mem[]` (which is process
  SRAM): pure arithmetic, formatting, parsing, thin ioctl wrappers.
* **Bad** — streams PSRAM: the framebuffer, and **BASIC arrays and
  strings**, which live in the PSRAM heap (`struct mm_vars H`). Anything
  taking a BASIC array or a long string may be walking PSRAM.
* **Bad** — the hottest inner loops, whatever they touch: XIP is slower
  than SRAM even when it hits.

---

## 2. Where robots' memory actually goes

`robots.bc`: code 143,578, data 5,956, bss 8,916 (file 184,839).

| component | bytes | note |
|---|---|---|
| bcrun text + data + bss | 121,525 | **a copy in every BASIC process** |
| the program's own code | 143,578 | 103 functions |
| VM `mem[]` (data + bss + pools + stack) | ~32,000 | demand-sized from the header |
| **total** | **~297 K = 75 blocks** | pool is 84 |

Inside the program's own code, the runtime headers are a small part:

| | bytes | functions |
|---|---|---|
| the program's translated functions | 125,728 | 103 |
| `mm*` / `mmb*` from the headers | 17,850 | 36 |

and it is concentrated: `f_ai_units` 15,692, `main` 10,330,
`f_writesprites_l` 7,580, `__mm_calld_1` 5,874 (the CALL-by-name
dispatcher), `f_show_intro` 5,518. Also **118 library imports**, which
are resolved to bcrun's own entry points and cost the program nothing.

**So the header runtime is not the lever — bcrun is.** 121 K of every
BASIC process is the same bytes as every other BASIC process.

## 3. What bcrun's text is

76,101 bytes of it are sized symbols:

| group | bytes | share |
|---|---|---|
| VM interpreter + loader | 30,675 | 40.3% |
| `mm_*` runtime (BASIC support) | 26,478 | 34.8% |
| float / libgcc | 11,030 | 14.5% |
| libc | 7,918 | 10.4% |

Largest single items: `bc_exec` 4,524, `main` 2,488, `mmwtab` 2,136,
`do_format` 1,508, `libcall` 1,464, `acos` 1,192, `_vfnprintf` 1,168,
`helper_op` 1,144, `asin` 1,008.

---

## 4. Candidates, ranked

### 4.1 float / libgcc — 11,030 bytes (2.7 blocks). Do this first.

Pure arithmetic on values in registers. **Touches no data at all**, so
the QMI objection cannot apply. The table already exists and already
exports `sin cos tan asin acos atan sinh cosh tanh sqrt exp log log10
floor ceil fabs` plus the two-argument ones.

**It is already built and switched off.** `bcrun.c` has
`BCRUN_SHAREDM`, default off, because "a tight sin/cos loop measured
2.7x slower than a program's own RAM copy". Two things have changed
since that decision:

* the DCP work made the kernel's copy **faster** than bcrun's soft
  float, not slower, so the speed objection may now point the other way;
* the switch dispatches the *same binary* to the kernel's copy, so
  `acos`, `asin`, `log` and `sqrt` are **still linked in** — turning it
  on as it stands costs the speed and saves no memory. The local
  references have to go so the linker can drop them.

Worth measuring both ways with `bench.bas` before committing.

### 4.2 libc — 7,918 bytes (1.9 blocks). Safe.

`do_format` + `_vfnprintf` alone are 2.7 K, and they format into
caller-supplied buffers that live in `mem[]`. `strcmp`, `memcpy` and
friends take whatever the caller passes — mostly `mem[]`, but a BASIC
string can be in the PSRAM heap, so the byte-loop routines are the ones
to think twice about. The formatting machinery is unambiguous.

### 4.3 `mm_*` runtime — 26,478 bytes (6.5 blocks). The biggest prize, case by case.

Most of it is thin ioctl wrappers and string/number work, and the actual
PSRAM streaming happens **kernel-side behind the ioctl** — so the
userland half is SRAM-only and shares well. Good: `mm_float_to_str`
(880), `mm_format` (672), `mm_break_epoch` (512), `mm_inkey` (476),
`mm_epoch_str` (472), `mm_gtext` (408).

**Keep local**: `mm_sort_i` / `mm_sort_f` / `mm_sort_s` (1,230
together). They walk a BASIC array, and BASIC arrays are in PSRAM —
these are the `MERGE` case exactly.

### 4.4 VM interpreter — 30,675 bytes. Mostly no; the cold parts yes.

`bc_exec` (4,524) is the hottest code on the machine and walks bytecode
in SRAM: moving it to flash is the one change that would clearly hurt,
and it is why `display.c` stays in RAM too. But `main` (2,488),
`lib_resolve` (368) and the rest of the loader run **once, at startup**,
before any drawing — 4–6 K of genuinely cold code that would never be
fetched again.

---

## 5. What this adds up to

| step | blocks freed | risk |
|---|---|---|
| float/libgcc, local copies dropped | 2.7 | low, measure speed |
| libc formatting | ~1 | low |
| `mm_*` minus the array walkers | ~6 | medium, case by case |
| cold loader | ~1.3 | low |

**Two blocks is all robots needs** to run with music (75 + 10 against
84). The first item alone covers it, and it comes off *every* BASIC
program on the machine, not just this one.

A caveat worth stating plainly: none of this shrinks the program's own
125 K of translated code, which is the larger half of the problem for a
big game. Shared flash lowers the floor for everybody; it does not
change the slope.

---

## 6. How to add to the table

`libm_table.c` is the pattern and its header comment is the contract:
the order **is** the ABI, append only, bump `PC3_LIBM_VERSION` if any
existing slot changes, and check magic and version before use so an old
binary fails loudly rather than calling the wrong slot. Two rules from
it carry to anything new:

* **No float arguments.** The kernel is built `-mfloat-abi=softfp` and
  userland soft; those agree for doubles in core registers, but a
  float-taking function would not be safe.
* **No `errno`.** A kernel-resident function would set the *kernel's*
  errno, not the caller's. Callers check their own arguments.

Add `*<file>.o(.text*)` to `default_text_excludes.incl` to place the new
table's code in flash, and keep anything that streams PSRAM out of it.
