# Process memory layout, and the stack alignment bug

Status 2026-07-28: **solved.** This replaces HANDOVER-progbase-bug.md,
which described the symptom before the cause was known and blamed the
wrong thing.

## The bug

BBC BASIC returned garbage for every numeric expression -- `PRINT 6*7`
gave `0`, `PRINT TIME` gave denormals like `1.92685602E-322` -- while
small programs such as `expr`, `date` and `uptime` were perfect.
Whether it happened depended on the address of `progbase`, so any edit
that changed the kernel's size could turn it on or off. That is why it
looked like "adding anything to the kernel corrupts user memory", and
why the console line editor was wrongly removed for causing it.

## The cause

`wargs()` (`syscall_exec.c`) builds the argument and environment block
by subtracting the *exact* byte length of the strings -- `ALIGNUP` is
the identity macro here (`kernel.h:41`) -- and `_execve` then did:

    udata.u_isp = udata.u_sp = nenvp - 2;

with no alignment step. AAPCS requires an 8-byte aligned stack on entry
to a function. ARM code handed an odd multiple of 4 miscompiles its own
64-bit accesses, which is why a double-heavy interpreter died and
integer-only utilities did not.

The offset from `PROGBASE` to the stack is odd for the BBC BASIC
binary, so it *inverts*: an 8-byte aligned pool produced a 4 mod 8
stack. Since `progbase` was plain BSS it moved on every relink, and the
alignment flipped roughly half the time.

Measured on hardware, identical kernel, pool moved by four bytes:

| progbase   | initial sp | `PRINT 6*7` |
|------------|------------|-------------|
| 0x2002f004 | 0x200580f8 | 42          |
| 0x2002f000 | 0x200580fc | 0           |

The fix lays the block out again four bytes lower when it lands wrong.
`sp` addresses `[argc][argv][envp...]` as one unit, so the whole block
moves together -- lowering `sp` alone would leave `envp` at the wrong
offset.

## What was eliminated on the way

Worth recording, because several of these look plausible and cost time:

* `memcpy` and `memset` -- both branch only on 4-byte alignment and use
  plain `ldr`/`str` (memset's fast path uses `strd`, but only after a
  4-byte check). Identical behaviour at both alignments.
* `swap_blocks` (`cpu-armm0/lowlevel-armm0.c`) -- plain 32-bit word
  loop, alignment-agnostic.
* The block shuffling in `contextswitch`. Proved by instrumenting the
  swapper to checksum each process image when it is switched out and
  verify it on the way back in. **The canary never fired**, which is
  what showed the corruption was present from load time rather than
  inflicted later.
* The upstream libc import, the line editor's own code, BBC BASIC
  itself, and zeroing of the pool -- all excluded by experiment earlier.

## Layout now

The kernel runs from RAM, not XIP, so text, data, bss and the pool all
share 512 KB. The pool is pinned to its own linker region
(`linker_overrides/`) instead of floating in BSS:

    RAM      0x20000000  0x30000   kernel text + data + bss
    PROGPOOL 0x20030000  320 KB    process pool, runs to top of SRAM
    SCRATCH_X 0x20080000 4 KB      disp_core1_stack 512 B, SDK core1 stack
    SCRATCH_Y 0x20081000 4 KB      core0 kernel stack

At the time of writing `__bss_end__` is 0x2002dd60, leaving about
8.8 KB of kernel growth before the link fails with "region RAM
overflowed" -- which is the point: growth is now a loud error instead of
a silent relocation of userland.

Gotchas: each SDK `section_*.incl` opens its **own** `SECTIONS { }`
block, and cmake does not track the `.incl` files as dependencies, so
`rm build/fuzix.elf` to force a relink after editing them.

## Test rig

Scripts in `devtools/`; PC3 console is COM11 at 115200, needs `pyserial`
on the Windows host.

* `python devtools/fz2.py 25 "PRINT 6*7" ...` -- runs BBC BASIC lines
* `python devtools/fzsh.py 25 "cmd" ...` -- runs shell commands
* **Send character-by-character with ~25 ms delay.** Bulk writes overflow
  the 132-byte tty queue and input is silently mangled.
* Reflash without touching the board:
  `sync; remount -n / ro; sync` then `picoctl flash`, board appears as
  drive **F: (RP2350)**, copy `fuzix.uf2` there.
* **Always remount read-only first** or the card needs an fsck on every
  boot.
* The build needs `PICO_SDK_PATH=~/src/micropython/lib/pico-sdk` and
  `-DPICO_BOARD=pico2 -DTOTALMEM=312`; it is *not* set in the
  environment, it lives in `build/CMakeCache.txt`. Do not delete that
  cache without network, or cmake will try to re-fetch the SDK.
