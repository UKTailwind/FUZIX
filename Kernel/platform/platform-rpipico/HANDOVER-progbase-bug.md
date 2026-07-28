# Open bug: address-dependent corruption of user memory

Status 2026-07-28. **The machine works right now** — do not "fix" anything
before reading this. The fault below is latent, not currently triggered.

## Symptom

BBC BASIC returns garbage for *every* numeric expression — `PRINT 6*7`,
`PRINT TIME`, `PRINT ADVAL(-9)` — printing denormal doubles like
`2.27270197E-322`. Those decode as small integers (44, 46, 51) with the
variant's type tag non-zero, i.e. the whole 16-byte VAR is corrupt, so
BBC BASIC formats the value slot as a `double`. Strings and error
messages still work. Other programs (`expr`, `date`, `uptime`) are fine,
which makes it look BASIC-specific. It is not.

## What is actually going on

`progbase` (`swapper.c:21`) is a 320 KB BSS array holding the entire
process pool. Being ordinary BSS, the linker places it after whatever
the kernel contains, so **it moves whenever the kernel's size changes**.
Its address determines whether processes are corrupted.

Measured, on hardware:

| progbase   | result |
|------------|--------|
| 0x2002afb4 | works  |
| 0x2002b00c | works  |
| 0x2002bfb4 | works  |
| 0x2002c690 | BROKEN |
| 0x20030000 | BROKEN |

That is the whole bug. Adding ~3.5 KB of kernel code (the console line
editor) moved `progbase` and broke every process; removing it restored
the exact previous size (197,120-byte uf2) and fixed it.

## Excluded by experiment — do not re-investigate

* **The upstream libc import.** Interpreter machine code is byte-identical
  between working and broken builds apart from relocated addresses; the
  import only pulled in `isxdigit`, growing the binary 112 bytes. Backed
  out anyway (preserved on branch `cb-libc-fixes`).
* **The line editor's code.** Disabled at runtime → still broken. Its
  PSRAM reservation reverted → still broken. Only *removing the source
  file*, restoring the old kernel size, fixed it.
* **BBC BASIC itself.** The same binary works on the old kernel and fails
  on the new one. Proven by swapping kernels with one SD card.
* **Zeroing.** With the pool pinned and `NOLOAD`, `memset(progbase, 0,
  USERMEM)` at boot made no difference.

## Work in progress: pinning progbase

Peter's technique, and it works mechanically: split the RAM MEMORY
region and give the pool its own psect in the second region. Files are
saved at `~/fuzix-wip/linker_overrides/` — `memory_ram.incl` (splits RAM
192K + PROGPOOL 320K) and `section_extra_post_data.incl` (places
`.progbase` in PROGPOOL).

To reinstate:

1. copy `linker_overrides/` into `Kernel/platform/platform-rpipico/`
2. `swapper.c:21` → `uint8_t progbase[USERMEM] __attribute__((section(".progbase")));`
3. `CMakeLists.txt` → `pico_add_linker_script_override_path(fuzix ${CMAKE_CURRENT_LIST_DIR}/linker_overrides)`

Gotcha: every SDK `section_*.incl` opens its **own** `SECTIONS { }`
block — a bare section definition is a syntax error. Hooks live in
`pico-sdk/src/rp2_common/pico_standard_link/script_include/`.

Pinning at 0x20030000 links correctly (`progbase` 0x20030000,
`__progbase_end` 0x20080000) but lands on a **broken** address, so the
system fails to run. That is expected, not a linker problem.

## Next experiment

With the pool pinned the address is finally a controlled variable.
Walk the PROGPOOL origin between a known-good and known-bad value —
0x2002C000, 0x2002E000, 0x20030000 — and find the exact boundary. The
boundary names the mechanism.

Leading hypothesis: at 0x20030000 the pool ends flush against
0x20080000 (the scratch banks), whereas working layouts left ~10 KB of
BSS above it. Something is very likely writing past `PROGTOP` and was
previously landing in harmless padding. Second suspect: the per-process
kernel syscall stack, which lives *inside* `udata` at the base of each
process (1,536 bytes total, `tricks.S` sets `sp = udata + UDATA_SIZE`)
with no guard at all.

## Test rig (no hardware handling needed)

Scripts in the session scratchpad; PC3 console is COM11 at 115200.

* `python fz2.py 25 "PRINT 6*7" ...` — runs BBC BASIC lines
* `python fzsh.py 25 "cmd" ...` — runs shell commands
* **Send character-by-character with ~25 ms delay.** Bulk writes overflow
  the 132-byte tty queue and input is silently mangled — this cost hours
  of chasing phantom bugs.
* Reflash without touching the board:
  `sync; remount -n / ro; sync` → `picoctl flash` → board appears as
  drive **F: (RP2350)** → copy `fuzix.uf2` there.
* **Always remount read-only first.** Flashing a mounted filesystem
  corrupts the card and forces an fsck on every boot.

## Removed features awaiting the fix

The console line editor (up/down history, in-line editing) was removed
solely because its size triggered this bug. The code is in the git
history and should return once the kernel can grow safely.
