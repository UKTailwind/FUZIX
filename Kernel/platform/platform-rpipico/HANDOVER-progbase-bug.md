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

## The RAM map (measured, good build `dcd63f4d8`)

The kernel runs **from RAM, not XIP**, so text, data, bss and the pool
all compete for the same 512 KB.

| what | address | size |
|------|---------|------|
| `.text`        | 0x20000110 |  75,552 |
| `.data`        | 0x20012730 |  22,232 |
| `.bss`         | 0x20018048 | 415,520 |
| ↳ `progbase`   | 0x2002b00c | 327,680 |
| ↳ above pool (`progptr`, `ptab`, `sndbuf`, `usb_stack`, …) | 0x2007b00c | ~9.2 K |
| `__bss_end__`  | 0x2007D628 | — |
| **free**       | 0x2007D628–0x20080000 | **10,712** |
| SCRATCH_X: `disp_core1_stack` 512 B, SDK core1 stack 2 K | 0x20080000 | 4 K |
| SCRATCH_Y: core0 kernel stack | 0x20081000 | 4 K |

Regenerate with `arm-none-eabi-nm -n build/fuzix.elf` and
`arm-none-eabi-size -A build/fuzix.elf`.

There is **no allocator in the image at all** — `nm` finds no `malloc`,
`free`, `sbrk` or `_malloc_r`. `.heap` is size 0. Nothing consumes the
free region at the top, so "the heap got starved" is not available as an
explanation.

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

## What the numbers rule out

An earlier draft of this document blamed the pool ending flush against
the scratch banks at 0x20030000, with working layouts leaving ~10 KB of
slack. **The measurements do not support that.** At the *broken*
unpinned address 0x2002c690 the pool ends at 0x2007c690 — the same ~9 KB
of BSS still sits above it and roughly 5 KB of main SRAM is still free
below 0x20080000. That layout is not qualitatively different from the
working 0x2002b00c one. Flushness explains the pinned 0x20030000 case at
best, and cannot explain 0x2002c690 at all.

What the table actually says is that the transition is **narrow**:

    0x2002bfb4  works
    0x2002c690  BROKEN     <- only 1,692 bytes apart

so the boundary lies inside a 1,692-byte window, and that window
contains 0x2002C000. A threshold that sharp is a hint: it points at
something with a fixed address or a fixed alignment requirement, not at
a gradual squeeze.

Remaining suspect worth keeping: the per-process kernel syscall stack,
which lives *inside* `udata` at the base of each process (1,536 bytes
total, `tricks.S` sets `sp = udata + UDATA_SIZE`) with no guard at all.
It would corrupt the process it belongs to, which matches the symptom,
but nothing yet explains why its address would matter.

## Next experiment

Cheapest first — **steps 1 and 3 need no hardware.**

1. **Desk diff.** Rebuild a known-broken kernel (restore `lineedit.c`
   from `30514a217^`, or just add a 4 KB dummy array to grow `.text`)
   and diff `arm-none-eabi-nm -n` good vs broken. Look for any symbol
   that changes *region* rather than merely sliding, and check
   `__bss_end__` against 0x20080000. Ten minutes, and it either names
   the collision or eliminates the whole class.

2. **Pinned walk.** With the pool pinned the address is a free
   variable. Step the PROGPOOL origin across the narrow window in ~512 B
   increments — 0x2002BC00, 0x2002C000, 0x2002C400 — rather than the
   coarse 8 KB jumps this document originally suggested. Note that
   pinning also changes where everything *else* lands, so confirm a
   pinned build at a known-good origin still works before trusting a
   pinned failure.

3. **Make it fail loudly.** Add to the linker override:
   `ASSERT(__bss_end__ <= 0x20080000, "kernel BSS ran into the scratch banks")`
   and, when pinned, `ASSERT(__progbase_end == 0x20080000, ...)`.
   Silence is what let this bug ship.

4. **Runtime canary.** Fill 64 bytes immediately above `PROGTOP` with a
   magic pattern and check it on every context switch. That converts
   "processes are corrupt" into a caught event with a `pc` in the
   backtrace, which names the writer outright.

## Known-good state

Commit `dcd63f4d8`, `build/fuzix.uf2` = 198,144 bytes, `progbase` at
0x2002b00c. Verified on hardware: `PRINT 6*7` → 42, `PRINT TIME` and
`PRINT ADVAL(-9)` both sane. Build with the usual
`make -C Kernel/platform/platform-rpipico` route; the artefacts land in
`Kernel/platform/platform-rpipico/build/`.

## Test rig (no hardware handling needed)

Scripts live in `Kernel/platform/platform-rpipico/devtools/` (they used
to be in a per-session scratchpad, which of course evaporated). PC3
console is COM11 at 115200; needs `pyserial` on the Windows host.

* `python devtools/fz2.py 25 "PRINT 6*7" ...` — runs BBC BASIC lines
* `python devtools/fzsh.py 25 "cmd" ...` — runs shell commands
* `python devtools/fz.py` — probe / shell / basic, faster but less safe
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
