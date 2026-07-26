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

## NEXT TASK: BBC BASIC port (the flagship application)

Feasibility measured and green: rtrussell/BBCSDL (zlib licence),
console-mode core compiles for cortex-m0plus at ~50K text
(bbmain 10.2K + bbexec 18.8K + bbeval 14.7K + bbasmb_arm_v6m 6.1K).
Estimated binary 90-100K + ~150K BASIC workspace inside the 256K
ceiling.

Plan:
1. Clone https://github.com/rtrussell/BBCSDL (shallow). Vendor the
   needed files into `Applications/bbcbasic/`: bbmain.c bbexec.c
   bbeval.c bbasmb_arm_v6m.c bbdata_arm_32.s (check it assembles for
   thumb; it is mostly data), BBC.h bbccon.h version.h.
2. Write a Fuzix OS layer replacing bbccon.c/bbccos.c: stdin/stdout raw
   tty I/O (our console is ANSI — exactly what the console edition
   emits, so VDU/COLOUR/CLS largely work already), file I/O to Unix
   calls, TIME via time()/uptime, no threads (the Linux build uses a
   pthread timer — replace with polling in the interpreter trap check).
   Reference: console/raspi/makefile shows the per-file CFLAGS and the
   seven objects.
3. Makefile.armm0 following the fforth/util pattern (rules.armm0,
   LINKER_TAIL); soft-double via libgcc is automatic. Workspace: static
   or sbrk ~150K.
4. fuzix-bbcbasic.pkg -> /usr/bin/bbcbasic (+ maybe /usr/lib/bbc
   examples); rebuild diskimage, refresh pc3-sd.img.
5. Test: tokenise/run classics, the inline v6-M assembler, file
   save/load. THEN: graphics statements (MODE/PLOT/GCOL) drive the
   design of the framebuffer interface (deliberately deferred until
   this consumer exists).

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
