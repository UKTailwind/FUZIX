# Pico Computer 3 Fuzix — development notes

Working notes for the PC3 port (branch `pc3` of UKTailwind/FUZIX). The
original phased plan lives in the MicroPython repo at
`boards/PICO_COMPUTER_3/fuzix/PLAN.md`; these notes supersede it as the
live document.

## Num lock per keyboard (2026-08-14) — ported from the MicroPython PC3

The embedded-keypad problem `kbd_leds_mark`'s comment already named (a Pi
keyboard typing 6 for O) now has an answer. It is the KEYBOARD'S firmware
overlaying a keypad onto `7890/uiop/jkl;/m`, triggered only by the num
lock bit in the LED report we send — and which kind of keyboard it is
cannot be discovered by asking one.

Proven on hardware from the MicroPython side: a Raspberry Pi keyboard
(`04d9:0006`, no keypad) and a full-size Lenovo (`04b3:3025`) return
**byte-identical 65-byte report descriptors**, both declaring the whole
key usage page (`19 00 2a ff 00` — an Array item declares the range of
values an element may carry, not which keys exist) and both declaring a
num lock LED. A VID:PID quirk table is no better: `04d9` is Holtek, a
generic controller vendor, so quirking it would break a full-size Holtek
keyboard.

So: guess from the one readable signal, remember what you are told.

- `kbd_has_numlock_led()` lives in **kbd_decode.c** — the file vendored
  byte-identical with the MicroPython port — so both trees share one copy
  and `kbdsync.sh` guards it. A keyboard declaring no num lock LED is
  taken to have no keypad. Only that direction holds: a declared LED means
  nothing (the Pi keyboard declares one), an absent one is evidence. It is
  credible here because both dumped keyboards declare `19 01 29 03`,
  exactly the lights they have, not the spec's boilerplate `29 05`.
  Host-tested under ASan/UBSan against the function extracted from the
  shared file by `sed`, 12 cases including truncated and garbage input.
- `kbd_numlock_pref[4]` in usbkbd.c remembers by VID:PID, consulted at
  mount **before** `sendlights` is seeded so the first LED report already
  carries it. Static and allocation-free — the mount callback cannot
  allocate or wait. `tuh_vid_pid_get` is cached by TinyUSB, so calling it
  there breaks none of the enumeration rules at the top of the file.
- `kbd_backend_set_leds` updates the table when the num bit changes, so a
  Num Lock press is remembered for the session.
- `PICOIOC_NUMLOCK` (0x0037; `ioctlcheck.sh` clean) is query-and-set in one
  struct. On a set, vid/pid 0 means the mounted keyboard; naming one
  records a preference for a keyboard that is not attached.
- `picoctl numlock [on|off [vvvv:pppp]]`; bare `picoctl numlock` reports
  the state, the keyboard's VID:PID and whether it declared the LED.

### Persistence — the half that first shipped missing

The first cut left the table RAM-only and put a *commented* rc line in as
the answer. Tested on hardware: the Pi keyboard came up with num lock on
every boot, exactly as that design says it must. Two reasons, and only the
second is fixable here — the Pi keyboard **declares** a num lock LED, so
rule 1 never fires for it, and nothing replayed rule 2 across a reboot.
MicroPython works because it *persists*, not because it detects.

So the persistence is now ported properly, through userland, where a Unix
keeps this:

- `/etc/numlock` — a line per keyboard, `04d9:0006 off`.
- `picoctl numlock off` applies it AND records it there; `--once` doesn't.
  A bare `picoctl numlock` reports state, VID:PID and whether the LED was
  declared.
- `picoctl numlock --load` in `/etc/rc` replays the lot at boot.
- `usb_kbd_numlock_pref_apply()` in usbkbd.c — recording a preference for
  the keyboard that IS mounted also applies it, otherwise `--load` would
  file the setting and leave the keyboard being typed on unchanged, which
  is the whole case it exists for.
- Host-tested under ASan/UBSan against `nl_read`/`nl_save`/`nl_load`
  extracted from picoctl.c by `awk`: create, append, update-in-place with
  no duplicate, comments/blanks/rubbish skipped, missing file quiet,
  round trip. It found a real bug first — a hand-edited file with no
  trailing newline had the new entry jammed onto its last line.

rc still runs after the keyboard's first LED report, so an affected
keyboard has the overlay on for the first second or so of boot, before
login. Nothing to do about that short of the kernel writing files.

### Two traps found on the way

**`Kernel/platform/platform-rpipico/rc` is NOT the rc that ships.**
`fuzix-basefs.pkg` installs `/etc/rc` from
`Standalone/filesystem-src/etc-files/rc`, and the two have drifted
independently. The first commented line went into the platform copy and
would never have reached a card. The platform copy now says so at the top.

**`make diskimage` was already broken.** ucp died with `error 28` partway
through and then `panic: inode freed` — which reads like corruption, not a
full disk. It is inodes: the root is 4.1M of content in a 32M image, and
the 32M and 8M images fail at exactly the same file. FS32 is the cause,
having taken `DINODE_SIZE` from 64 bytes to 256, so upstream's `isize=256`
buys a quarter as many as it used to. Nothing to do with the num lock work
— that adds no files. Fixed by measurement rather than arithmetic, because
the arithmetic does not line up: 256 dies at `stty`, 512 at `utsname.h`,
1024 is clean, though 512 blocks at 2 inodes each ought to have covered the
591 the root needs. Something else is eating them, and
`NOTES-inode-freelist.md` describes a two-byte overrun onto `s_ninode`
fixed in the kernel that may still be live in these host tools — **not
proven, worth chasing**. `isize` is now 2048 in the top-level Makefile.

Kernel, picoctl and a full card image all build clean; `pc3-sd.img`
refreshed and verified to contain the new rc line and picoctl. **Not yet
run on hardware.**

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
- WSL DNS is fixed (2026-07-28) — no more /etc/hosts pinning. The cause
  was systemd-resolved having no upstream at all (`resolvectl status` ->
  `Current Scopes: none`) because `/etc/wsl.conf` sets
  `generateResolvConf = false`, so nothing ever supplied nameservers
  while raw IP worked fine. Fixed by
  `/etc/systemd/resolved.conf.d/wsl-dns.conf` with `DNS=<gateway>
  1.1.1.1 8.8.8.8` plus `FallbackDNS`, so it survives the NAT gateway
  address changing. Root without a sudo password:
  `wsl.exe -d Ubuntu -u root -- bash -c '...'`.
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

## Status (2026-08-01): pc3-v0.4 shipped — MMBasic self-hosts

Release: https://github.com/UKTailwind/FUZIX/releases/tag/pc3-v0.4
(kernel uf2 + pc3-sd-cc.img.gz + manual). BOTH the kernel and the card
are needed: the card carries the compiler, the kernel carries the
loader that honours its stack request.

`mmbc prog.bas; cc prog.c; ./prog.bc` all run on the machine. The
3200-line solar eclipse translates, compiles and runs here in 3.24 s
(MMBasic 12.5, MicroPython 8.8); Dhrystone compiled on the board is
90,021/s, a quarter of gcc -O2 cross-compiled for the same chip.

Fixed this session, each of which had been taking the machine down:

- **8K USERSTACK was not enough for the compiler passes.** Recursive
  descent over a large program overflows it silently into BSS. Binaries
  now ASK: the ELF loader honours `PT_GNU_STACK` p_memsz (capped by
  `USERSTACK_MAX`), cc1/cc2 request 32K and mmbc 16K. The linker script
  has an explicit PHDRS list, so `ld -z stack-size` is silently ignored
  — the request is a NOLOAD section placed LAST (placing it first
  shifts .text up by the stack size) sized by `--defsym __stack_size=N`.
  **Test this class of bug off-hardware: `ulimit -s 8` on the host
  build reproduces it exactly.**
- **cc1 `sym_find_idx` compared index lists without checking length** —
  it read past shorter dimension lists and could match on the rubbish
  beyond, returning another type's dimensions.
- **cc2 arena**: the request is measured by walking the carve list
  twice. The 32K node-pool reserve must be COUNTED while measuring and
  NOT carved while placing — carving it put backend.c's later node-pool
  carve outside the granted region, where valaddr refuses I/O, and cc2
  died on its first read (EFAULT dressed up as "short read").
- **Board cc2 defaults to THUMB_RECLAIM**: the board bcrun always runs
  native, and an eclipse that keeps its dead bytecode does not fit a
  256K process.
- **USB keyboard**: the LED report is a control transfer on EP0 and was
  issued inline from wherever a lock key was decoded. That wedged EP0 —
  the keyboard worked until the next lock key, and thereafter a device
  could attach but never enumerate. It now goes out from `hid_poll`
  only, one transfer at a time, left dirty and retried if refused. The
  mount callback now has three rules, not two: no set_protocol, no
  receive_report, no set_report.
- **Stray Return after the boot prompt**: auto-repeat on a key whose
  release was never fetched (the pump is starved through mount and
  init). `kbd_repeat_check` re-arms after a poll gap instead of firing.

**Interrupts: read PC3-IRQ-REVIEW.md before touching a vector.** The
SDK's mechanisms work on this port — VTOR points at the SDK's
`ram_vector_table` and runtime handler installation works. The comment
in rawuart.c saying otherwise is wrong, and it cost a day. Defining
`isr_irqN` for a slot the SDK or a library also claims makes
`irq_add_shared_handler` chain over a handler it did not install, and
the SDK's answer to that is `panic()` — a BKPT, which without a
debugger is a HardFault with HFSR.DEBUGEVT and a PC inside the SDK's
printf.

Debug builds: `PC3_USB_TRACE=1` gives TinyUSB's own trace through the
small printf in usbtrace.c (level 2 overflows kernel RAM, level 1
fits); `PC3_NO_KBD_LEDS=1` builds the LED report out. Flashing without
touching the buttons: `picoctl flash` from a booted shell (`sync` and
`remount / ro` first), then copy the uf2 to the RP2350 drive — but only
with the board's DPDT USB switch in the PROGRAMMING position. That
switch is also the rig for USB work: flipping it to the hub is a clean,
repeatable port-connect event with nothing else going on.

## NEXT: graphics for MMBasic — a 320x240 4bpp mode

The agreed next task. Add a 320x240 4-bit (16 colour) driver to BBC
BASIC as **MODE 7**, because that is the geometry MMBasic needs. Then
expose the kernel's mode-switch calls through a header so C — and
therefore translated MMBasic — can select the mode and draw in it.

That header is the first of the peripheral surfaces `mmbc` will grow.
The rest (GPIO, I2C/SPI/one-wire, sound) should follow the same shape:
a documented C interface in `/usr/lib/cc/include`, a runtime inside
bcrun, and translator support in mmb2c + the mmbc mirror, with
Appendix C of the manual regenerated from the tables
(`mmb2c fcc/coverage.py`) as coverage grows.

Open question to settle when starting: what a translated program should
do when it selects a graphics mode with no HDMI attached — fail, or run
blind.

## Status (2026-07-27 evening): the full machine

ALL CONFIRMED ON HARDWARE: BBC BASIC runs with GRAPHICS (Phase 5,
PC3-GFX-DESIGN.md - MODE 0-5 on 1024x768@70, MMBasic clocking at
375 MHz, MODE 0 full-width 5:8 coverage upscale, PLOT/GCOL/palette)
and SOUND (Phase 6, sound.c - the 4-channel BBC synth with queues,
sync and ENVELOPE on the PCM5102 I2S DAC, SNDIOC on /dev/sys).
Remaining graphics niceties queued: PLOT circles (144+) and
rectangle fill (96+), GCOL modes 1-4, POINT( readback, VDU 5,
ADVAL(-6..-9) sound queue readback, reclaiming the 40K shadow
framebuffer when no mode is active.  MODE >= 6 returns to the
console; MODE 7 teletext is a future treat.

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
