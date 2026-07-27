# Pico Computer 3 Fuzix — development notes

Working notes for the PC3 port (branch `pc3` of UKTailwind/FUZIX). The
original phased plan lives in the MicroPython repo at
`boards/PICO_COMPUTER_3/fuzix/PLAN.md`; these notes supersede it as the
live document.

## Workflow

- Build (WSL): `PICO_SDK_PATH=$HOME/src/micropython/lib/pico-sdk make
  TARGET=rpipico SUBTARGET=pico2 diskimage -j8` from `~/src/FUZIX`.
  Outputs: `Kernel/platform/platform-rpipico/build/fuzix.uf2` (kernel,
  BOOTSEL-flash), `Images/rpipico/pc3-sd.img` needs refreshing after
  filesystem changes: `dd if=Images/rpipico/filesys.img
  of=Images/rpipico/pc3-sd.img bs=512 seek=133120 conv=notrunc`.
- SD card layout: p1 = 64M FAT (unformatted placeholder), p2 = 32M Fuzix
  root (boot `hdb2`), p3 = 4M type 0x7F. Users write pc3-sd.img whole.
- The user flashes hardware; never assume you can.
- WSL DNS via the NAT proxy is broken: for git push / clone, pin
  `20.26.156.215 github.com` in /etc/hosts (as root), work, remove. apt
  needs archive.ubuntu.com pinned similarly.
- The kernel is PICO_COPY_TO_RAM; `build/fuzix.elf` and the userland
  `.debug` files match what runs — `arm-none-eabi-addr2line -f -e` on
  panic addresses works and has solved every crash so far. User-space
  addresses: subtract PROGLOAD (printed in panics) and look up in e.g.
  `Applications/V7/cmd/sh/sh.debug`.

## Architecture (as built)

- 315 MHz, clk_peri = clk_sys (PC3 convention); flash QMI capped 63 MHz;
  8 MiB PSRAM at 0x11000000 (psram.c) = block device hdc = swap
  (rc runs `swapon /dev/hdc 16384`; 32 slots of 256K).
- Processes: PROGSIZE 256K ceiling, resident packed at actual size in 4K
  chunks, swap I/O covers only up to u_break. PTABSIZE 30, OFTSIZE 48,
  ITABSIZE 40.
- Display: display.c = 640x480x1bpp + RGB332 per 8x12 cell, HSTX
  scanout owns core1 (nothing else may run there). console.c = ANSI
  terminal engine (NOT kernel vt.c, which is VT52 and stays out of the
  build); termcap entry "pc3" (80x40) describes exactly what it
  implements; inittab sets TERM via `getty /dev/tty1 115200 pc3 80 40`.
  Console output mirrors uart+screen; input merges USB keyboard + uart
  into the one session.
- USB keyboard: usbkbd.c, TinyUSB host mode (CONFIG_PC3_USB_KBD).
  Slot/poll model vendored from MicroPython/MMBasic with its enumeration
  rules (no set_protocol at mount, no report request from mount, polled
  reports, staggered start). Layouts US/UK/DE/FR/ES/BE; `picoctl keymap
  xx` (rc default uk), `kbd=xx` bootdev param.
- Pre-emption: timer tick pends PendSV (need_resched | pending signal |
  usb pump starved); isr_pendsv redirects the stacked PC through the
  preempt_user trampoline ONLY when the PC is in user space; trampoline
  saves everything incl. flags, runs switchout/chksigs/deliver_signals
  on the kernel stack, resumes. See tricks.S comments.
- RTC: ds3231.c on I2C0 GP20/21; /dev/rtc wire format is the USERLAND
  struct layout (16 bytes, data at offset 8) — kernel and userland
  disagree about struct cmos_rtc on aligned targets.

## Landmines (all cost real debugging — do not relearn)

1. tricks.S is DIVIDED-syntax Thumb16: `add r2, #1` not `adds`,
   `orr/bic r0, r1` not `orrs/bics`. New asm labels called from C need
   `.thumb_func`.
2. A stacked exception-return PC must have bit 0 CLEAR (thumb_func
   literals have it set — bic before storing into a frame).
3. NEVER redirect a frame whose stacked xPSR has ICI/IT bits
   (0x0600FC00): the M33 has already written back the LDM/STM base
   register; a forced restart corrupts sp. Skip and let the tick retry.
4. plt_switchout/dofork/switchin share a frame layout INCLUDING r4;
   there are TWO pushers and TWO poppers — change all four together.
5. TinyUSB runs ONLY in thread context and ONLY on the dedicated
   2.5K usb_stack (usb_pump_stacked): its depth on the ~1.2K kernel
   stack (which sits directly above udata) corrupts udata. osal_pico:
   queues are the only ISR-safe primitive.
6. u_insys must be CLEARED after syscalls (was a latent `= 1` typo).
7. Kernel+tusb includes: `#define ssize_t __ssize_t` dance (see
   core1.c/usbkbd.c). config.h must define the board block BEFORE
   including tusb_config.h.
8. kprintf %x is 16-bit; use %p for addresses.
9. The platform Makefile needed a FORCE prerequisite for build/fuzix.elf
   (source edits otherwise don't rebuild the kernel).
10. Packaging: per-app .pkg files gate on `if-file <name>` — armm0
   builds produce different filenames than 8-bit ones (see levee).
   NAND (ucp-tmp.txt) and SD (pkg-based) images pack DIFFERENT lists;
   fforth is /usr/bin not /bin.
11. Shell quoting for commit messages: apostrophes break the wsl bash -c
   chains — write /tmp/cmsg with a heredoc and `git commit -F`.
   And don't chain `&& git commit` after a build that can fail.
12. Shell $variables in one-line wsl bash -c commands get eaten by the
   quoting layers: a for-loop clean with $d silently cleaned NOTHING,
   the next build relinked stale Thumb-1 objects, and a whole
   hardware round-trip was wasted re-diagnosing a fixed bug.  Put any
   command with shell variables in a script FILE and run that.
13. GCC + Thumb-1 mishandles BBC BASIC r10/r11 global register
   variables: functions that do not reference them treat them as
   scratch (save at entry, restore stale at exit - getput wiped the
   esi advance getvar made and INPUT looped forever).  Thumb-2
   (cortex-m33) codegen is correct - verify with objdump on getput
   after any toolchain change.  The whole userland is m33 now; no
   RP2040 compatibility.
14. Kernel sleeps are DECISECONDS: the timer wheel (p_timeout, _pause,
   the monotonic counter behind CLOCK_MONOTONIC) runs at 10Hz on every
   Fuzix platform, whatever TICKSPERSEC is.  libc usleep() used to
   round sub-100ms periods to _pause(0) = sleep FOREVER (bbcbasic hung
   at startup on its first cursor query; invaders/2048 were broken the
   same way), and clock_gettime(MONOTONIC) had tv_nsec off by 1000x
   (fixed).  There is NO sub-decisecond sleep: for responsive input,
   block in read() with VMIN=0/VTIME=1 (the tty wakes you the moment a
   byte arrives) - see kbwait1() in bbccon.c.

## Session status (2026-07-26 evening)

BBC BASIC RUNS on hardware: banner, immediate mode, PRINT 1/3 =
0.333333333, *dir, ESCape handling all confirmed.  Debug trail that
got there: usleep-forever libc bug (landmine 12), duplicate DSR
replies (holdback added), int8 cursor wrap (console.c commit
8d1c37c9f - CSI 999 H turned cx negative and poisoned every reply).
The good kernel is build/fuzix.uf2 @8d1c37c9f (was handed over as
fuzix-D.uf2 during the bisect; the A-D test kernels are deleted).

Cleanup done (2026-07-27): startup markers stripped, clean app on the
refreshed pc3-sd.img.  Next: resume the test list below (TIME$, file
I/O, editor keys, assembler, recursion guard, multi-process), then
MODE/PLOT/GCOL -> Phase-5 framebuffer design.

File interchange: /usr/bin/fat (Applications/util/fat.c) reads the SD
FAT partition from Fuzix - format hdb1 in Windows (FAT16 or FAT32,
NOT exFAT), drop files on it, then `fat ls` / `fat get NAME [dest]` /
`fat info` on the PC3.  Long filenames and subdirectories work; read
only (write support = future task; dosread in util is the old Minix
FAT12/16 tool, not useful here).  Host-testable: fat.c compiles
native and takes -d <image>; validated against mkfs.fat -F16/-F32
images with mtools-written LFN files before ever touching hardware.

## BBC BASIC (built; awaiting hardware test)

`Applications/bbcbasic/` = vendored BBCSDL console edition (zlib
licence, upstream 22fb22f) + Fuzix OS layer.  Binary 139K
(/usr/bin/bbcbasic on the SD image; NAND is full - SD only).
Library dir: /usr/lib/bbc (@lib$).

How it is put together (differs from the original plan in useful ways):

- bbccon.c was PATCHED (ifdef FUZIX), not replaced: single-threaded,
  keyboard via non-blocking read (raw termios VMIN=0/VTIME=0 - the
  Fuzix tty honours these), no reader thread.  The 250ms event timer is
  polled (polltimer) from trap()/the wait loops; trap() itself gates
  its poll to 1-in-64 statements so tight loops don't pay a syscall
  per statement - the wait loops (oskey/osrdch/oswait) poll every
  iteration so typing stays snappy.
- Workspace: sbrk PROBES the 256K ceiling 4K at a time, then gives
  back 12K slack for libc malloc (stdio/file channels/history).  The
  ELF layout makes this safe: u_break starts ABOVE the args+stack, so
  heap growth never touches the stack.  Nets ~105-110K of workspace.
- The C stack is the fixed USERSTACK window between BSS and heap - now
  8K (config.h).  bbexec has a FUZIX recursion guard (stklim, set in
  main) giving 'Recursion too deep!' instead of silent corruption.
- bbcstdio.c: own sprintf (%.*E/%.*f/%.*G, %lld, %llX) and sscanf
  (%n, %i, %hu) - the Fuzix libc printf has NO float/long-long and its
  scanf lacks %n.  Digit extraction is 64-bit-integer based; last
  digit of 17-sig-fig output can be 1 ulp off glibc.  Also llabs and
  strtoull.
- bbccon.h ACCSLEN branch is 1024 for FUZIX (PICO-sized).
- Libc/libm fixes this needed (all upstreamable): trunc/cos/tan/
  __rem_pio2 added to Makefile.armm0 SRC_LM (trig was never linkable
  before), tan.c vendored (was missing entirely), STRICT_ASSIGN in
  libm.h.  Note libm is a SEPARATE archive: link -lmarmm0 before
  LINKER_OPT.
- Kernel: pagemap_realloc now refuses growth past PROGSIZE (it would
  have let brk overflow the fixed swap slot and corrupt the
  neighbour); brk_extend fails quietly (the probe loop relies on it);
  console gained DSR 6n/5n replies (injected via kbd_push so POS/VPOS
  work standalone; a serial terminal answering too is benign) and SGR
  100-107 bright backgrounds.
- Everything -mcpu=Cortex-M0plus: ld REFUSES to merge v8-M objects
  with the v6-M libc, so no -mcpu=cortex-m33 in apps.  bbexec/bbeval
  at -O2 (hot loops), rest -Os.
- BBC.h uses GLOBAL REGISTER VARS r10/r11 (esi/esp) on __arm__: fine
  under AAPCS + our context switch, but don't link objects built
  without BBC.h into the interpreter's call graph.

Test list for the first hardware session: banner + immediate mode,
number formatting (PRINT 1/3, STR$, @%), keyboard ESCape of a running
loop, *DIR/*TYPE/OSCLI, file LOAD/SAVE/OPENIN/PRINT#, TIME/TIME$,
POS/VPOS standalone (DSR), osline editing/history, the inline v6-M
assembler (CALL), 'Recursion too deep!' on a runaway recursion, and
free/swap while several BASICs run.  THEN: MODE/PLOT/GCOL drive the
Phase-5 framebuffer interface design.

## After that

- Thumb C compiler backend (Fuzix-Compiler-Kit `backend-thumb.c`; no
  ARM backend exists anywhere upstream). Passes already build for the
  armm0 host and fit: cc1 = 49.7K (Applications/CC/Makefile.armm0,
  currently z80 backend). Then thumb as/ld in Fuzix Bintools emitting
  the kernel's ELF-PIE. Develop host-side against the FCC test suite.
- Upstreaming: ~15 generic fixes on this branch (toolchain, linker
  rules, RTC ABI, sh /etc/profile, init baud table, levee packaging,
  the whole pre-emption suite + landmine list). Then the platform-pc3
  split.
- Stretch: PSRAM-resident processes, virtual consoles, 378 MHz.
