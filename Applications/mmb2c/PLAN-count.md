# PLAN-count: SETPIN FIN / CIN — the counting inputs

2026-08-22.  The §12.3 residual from PLAN-web.md: the one real hardware
gap retic degrades around.  This plan is written from the reference
source, not from the manual — every mechanism below carries the
PicoMite file:line it was read from (tree `d:\Dropbox\PicoMite\PicoMite`,
the current one with `misc/External.c`).

**Scope**: `SETPIN pin, FIN [, gate]`, `SETPIN pin, CIN [, option]`,
`SETPIN pin, PIN [, cycles]` (period), `PIN()` on all three,
`PIN(pin) = n` on CIN, `SETPIN pin, OFF`, and cleanup on process
termination.  Count pins are FIXED at **GP4–GP7** (they are MMBasic's
INT1–INT4 in that order).  `OPTION COUNT` stays refused by name —
accepted-and-ignored would be silent divergence.  FFIN (the RP2350
PWM-slice fast counter) is out of scope, noted at the end.

Test rig: COM14 has GP2 wired to GP4, so the board's own PWM output is
the signal source.


## 1. What MMBasic actually does (measured, not remembered)

### 1.1 The two counters

Per designated pin there are three cells: a live count, a latched
value, and a gate countdown (`INTxCount` int64, `INTxValue`,
`INTxTimer`/`INTxInitTimer` int — External.c:129,133).

**CIN** — the GPIO edge IRQ increments the count; that is the whole
handler (`TM_EXTI_Handler_1`, External.c:6023-6041).  `PIN()` returns
the LIVE count (External.c:1569 via fun_pin 2121).  `PIN(pin) = n`
stores n into the live count — any n, not just 0 (External.c:603-613).

**FIN** — same edge increment.  A free-running **1 ms** repeating timer
(`add_repeating_timer_us(-1000, timer_callback, ...)`, PicoMite.c:903)
decrements the gate countdown; at zero it latches `value = count`,
zeroes the count, reloads the gate (`UPDATE_FREQ_INPUT`,
PicoMite.c:2220-2226, applied at 2532-2535).  `PIN()` returns
`value * 1000.0 / gate` as a float (External.c:2151-2159).  So the
reading updates once per gate, is 0 until the first gate completes,
and the gate phase is wherever SETPIN left it.

**PER** — the roles invert.  The 1 ms tick INCREMENTS the count (a
millisecond accumulator — `UPDATE_PER_INPUT`, PicoMite.c:2228-2230);
each rising EDGE decrements the cycle gate, and at zero latches
`value = count` (elapsed ms), zeroes it, reloads
(`TM_EXTI_Handler_1`'s PER branch, External.c:6025-6033).  `PIN()`
returns `value / cycles` — the average period in MILLISECONDS as a
float (External.c:2140-2148).  Third argument = cycles to average,
1..10000, default 1 (1966-1970).

### 1.2 Configuration (External.c ExtCfg, 1000-1091)

* Only the four `Option.INTxpin` pins accept these modes; anything else
  is an error (1090).
* Third argument: FIN gate 1..100000 ms, default 1000 (1960-1965).
  PER cycles 1..10000, default 1 (1966-1970).  CIN option 1..10,
  default 1 (1972-1977).
* Edge select: rising, except CIN option 2 = falling, CIN option >= 3 =
  both (1003-1007).
* Pulls: option 1 or 4 = pull-down, 2 or 5 = pull-up (1008-1011).
  NOTE this code is shared by all three modes, so a FIN **gate** of
  1, 2, 4 or 5 ms also sets a pull — and PER's DEFAULT option of 1
  gets a pull-down every time.  For FIN it is plainly accidental and
  behaviourally irrelevant (a driven pin does not care); for PER it is
  effectively documented behaviour by now.  We replicate the shared
  logic as-is rather than special-case any of it.
* `irq_set_priority(IO_IRQ_BANK0, 0)` — highest (1012).
* Input hysteresis on (1030).
* Count, value zeroed and gate loaded at config (1026-1027).
* Registration is the SDK callback model:
  `gpio_set_irq_enabled_with_callback(gp, edge, true, &gpio_callback)`
  first time, `gpio_set_irq_enabled` after (1016-1025).

### 1.3 Deconfiguration

`ExtCfg` on one of the four pins starts by unconditionally disabling
its edge IRQ (both edges, 790-821) and removing pulls (831) —
so `SETPIN pin, OFF` *and* any reconfiguration kill the counting.

### 1.4 The part the user flagged: everything on the edge path is in RAM

* PicoMite's callback and the four per-pin handlers are
  `__not_in_flash_func` (External.c:6193, 6023...).
* The 1 ms `timer_callback` is `__not_in_flash_func` (PicoMite.c:2244).
* The SDK's own dispatcher `gpio_default_irq_handler` — the function
  the IO_IRQ_BANK0 vector actually reaches — is relocated to RAM by
  `cmake/relocate_gpio_irq_to_ram.cmake`: an objcopy section rename of
  `.text.gpio_default_irq_handler` to `.time_critical.*` at pre-link,
  because PicoMite cannot edit the SDK's linker script.

### 1.5 A latent bug we will NOT copy

`fun_pin` reads the volatile int64 count in thread context while the
IRQ increments it; on a 32-bit core that is two loads and a torn read
is possible each time the count crosses a 2^32 boundary.  Astronomically
rare there; free to fix here, because our read already sits inside an
ioctl — we wrap the 8-byte copy in `save_and_disable_interrupts()`.
That is a bug fix, invisible in behaviour, not a divergence.

### 1.6 Evaluation: change anything else?  No.

The gate mechanism (quantized, free-running, first reading 0) is
observable behaviour programs may rely on, and the rule stands:
replicate MMBasic exactly.  Timestamp-based measurement would be
"better" and different; rejected.  Priority 0 for the bank IRQ is
correct for us too: the handler is ~30 instructions, and counting must
preempt the audio synth's DMA-IRQ work, not queue behind it (SDK
default priority is 0x80, so GPIO at 0 becomes the one priority-0 IRQ
— worth stating in the code).


## 2. Why RAM placement is essential on the PC3 (grounding the requirement)

The PC3 kernel executes from flash (copy-to-RAM off) and every
instruction fetch that misses XIP goes through the QMI — the same QMI
core1's scanout streams PSRAM through.  A priority-0 IRQ firing up to
~100 kHz with its handler in flash would (a) jitter on QMI contention
and (b) ADD contention to the scanout path that has already produced
visible flecking once.  In SRAM the edge path touches no QMI at all.

Note what is NOT the reason any more: the on-board flash filesystem is
gone, nothing programs flash at run time, so there is no
XIP-unavailable window to survive (default_text_excludes.incl records
this, 2026-08-20, and keeps the rule for the day a flash writer
returns).  RAM placement here is latency + bus traffic + that
future-proofing.

**Our mechanism is better than PicoMite's and already built.**  The
port's `linker_overrides/default_text_excludes.incl` names what may
execute from FLASH; anything unnamed falls to RAM.  Two consequences:

1. The new kernel file (countpin.c) is RAM-resident by simply not
   listing it.  No attributes, no cmake step.
2. The SDK dispatcher is currently in flash via line 247:
   `*hardware_gpio/gpio.c.o(.text*)`.  Fix: split that entry so
   `gpio_default_irq_handler` (and `gpio_default_irq_handler`'s
   helpers if -ffunction-sections splits any out) is not named and
   falls to RAM.  Simplest first cut: drop the whole gpio.c object
   from the flash list and measure the RAM cost with p2geom.sh —
   expected ~1-1.5K; if that offends the process pool, enumerate
   gpio.c's cold functions back into flash by name and leave only the
   handler out.  Either way it is the port's own machinery doing
   natively what PicoMite needs objcopy for.

The 1 ms gate timer runs through the SDK alarm-pool IRQ, which already
fires 200/s for the Fuzix tick from flash today; +1000/s of a
RAM-resident ~10-instruction callback is noise and needs no excludes
change beyond countpin.c itself.


## 3. Architecture: kernel facility + ioctl, not userland

Userland cannot own an IRQ (and the kernel cannot call program code —
the blit-engine lesson).  So this inverts the usual PC3 pin split:
counting is the one pin mode where the work lives in the kernel and
userland gets an ioctl per read.  That is fine: retic samples once a
minute; even a tight BASIC loop reads at ~1.5 us/ioctl.

### 3.1 Kernel: platform-rpipico/countpin.c (new)

State, 4 entries (GP4..GP7):

    mode (off/fin/cin), int64 count, int32 value,
    int32 gate_init, int32 gate_left

* **Edge callback**: registered with
  `gpio_set_irq_enabled_with_callback` — the SDK model, per
  PC3-IRQ-REVIEW.md's conclusion, and nothing else in this kernel uses
  the bank IRQ today (verified: no gpio_set_irq / IO_IRQ_BANK0 in the
  platform).  Body mirrors TM_EXTI_Handler_x: `count++` (FIN and CIN
  both).  Runs on core0; GPIO IRQ enables are per-core and no core1
  code ever touches them.
* **Gate timer**: one `add_repeating_timer_us(-1000, ...)` started when
  the first FIN **or PER** pin configures, cancelled when the last
  leaves — interrupts exist only while a SETPIN wants them, per the
  requirement.  Callback = UPDATE_FREQ_INPUT per FIN pin,
  UPDATE_PER_INPUT (`count++`) per PER pin.  (The alarm pool is proven
  here — the 200 Hz tick uses it, devices.c:193.)
* **Priority**: `irq_set_priority(IO_IRQ_BANK0, 0)` at first enable,
  as MMBasic (External.c:1012).
* **Hysteresis + pulls + edge** per §1.2, same conditions.
* **ioctl surface** (dispatched from devgpio.c's switch; struct
  `{u8 pin; u8 op_arg…; i64 val}` with uget/uput):

  | code | does |
  |---|---|
  | `GPIOC_CNT_FIN` | pin, gate 1..100000: configure FIN |
  | `GPIOC_CNT_CIN` | pin, option 1..10: configure CIN |
  | `GPIOC_CNT_PER` | pin, cycles 1..10000: configure PER |
  | `GPIOC_CNT_READ` | CIN: live count; FIN/PER: latched value (int64 out, IRQ-safe copy per §1.5) |
  | `GPIOC_CNT_SET` | CIN only: store val into live count |
  | `GPIOC_CNT_OFF` | disable edge IRQ, clear pulls+state, stop timer if last FIN/PER |

  Pin must be 4..7 (EINVAL otherwise) and — one extra call, worth it —
  `pinlock_owner(PLK_PIN, pin)` must be the caller, so the advisory
  claim is actually load-bearing on the one mode where the kernel
  holds state.
* **Teardown on death**: pinlock.c `reset_one(PLK_PIN, idx)` gains an
  unconditional `gpio_set_irq_enabled(idx, 0xF, false)` plus a
  `countpin_reset(idx)` call for 4..7 BEFORE the existing gpio_init —
  `gpio_init()` does NOT clear IO_BANK0 interrupt enables, so without
  this a killed program leaves a ghost IRQ counting into dead state.
  pinlock_release() already runs from exit, kill and exec paths, so
  process termination is covered for free.
* **Placement**: countpin.c NOT added to the text excludes (whole file
  to RAM — it is a few hundred bytes).  devgpio.c stays in flash; only
  the callback path is hot.
* ioctlcheck.sh count 66 → +5; Library/include/sys/pc3io.h mirror AND
  the on-board cc's flat include copy (mkccimage.sh) — the two-spellings
  rule; CMakeLists.txt gains countpin.c.

### 3.2 Runtime: one NEW bcrun libcall, deliberately

`mm_pinct(op, pin, val) -> MMINTEGER` in mmb_runtime.c, a new table row
and w_ wrapper in bcrun_mm.c.  NOT new ops inside the existing
`mm_gpio` — its wire format packs pin and val into bytes (gate needs
32 bits) and, decisive: skew protection refuses by **libcall name**, so
a program using counting on an old bcrun must fail by name at load, not
misbehave through an old mm_gpio's default case.  Host build: stub
returning 0, so the gates run.

### 3.3 Program side: mmb_gpio.h

* `MMG_PIN_FIN 9`, `MMG_PIN_CIN 10`, `MMG_PIN_PER 11`.
* `mmg_setpin_fin(pin, gate)` / `mmg_setpin_cin(pin, option)` /
  `mmg_setpin_per(pin, cycles)`: validate
  range (mm_error, existing idiom "Pin cannot do that" / a fresh
  message for a bad gate), claim `MM_PLK_PIN` exactly as mmg_setpin
  does, then `mm_pinct(CFG, ...)`; record mmg_mode[].
  Remember the FIN gate in a small static (needed to scale the read).
* `mmg_pin_get`: case CIN → `(MMFLOAT)mm_pinct(READ, pin, 0)` (PIN() is
  already float-always here, the documented divergence; a double is
  exact past 2^53 edges = 285 years at 1 MHz).  Case FIN →
  `value * 1000.0 / gate`.  Case PER → `value / cycles` (ms).
* `mmg_pin_put`: case CIN → `mm_pinct(SET, pin, val)`; FIN and PER stay
  "Pin is not an output", which is MMBasic's behaviour.
* Reconfigure/OFF: mmg_setpin (all modes) and the OFF path check
  `mmg_mode[pin]` first — if it was FIN/CIN, send `CNT_OFF` before the
  new configuration.  That is ExtCfg's unconditional head (§1.3) in our
  shape, and it is what returns GP4-GP7 to ordinary DIN/DOUT/PWM duty.
* Host side of the header: counters modelled on pc3_hostlatch's
  philosophy — a static per-pin count that SET stores and READ returns,
  so the one thing the host CAN check (the program's own arithmetic
  around PIN) is on the tested path.

### 3.4 Translator: mmb2c.py + the mmbc twin

In the SETPIN mode ladder (mmb2c.py:5985-6003 and mmbc_stmt.c's twin):

    SETPIN pin, FIN [, gate]    -> mmg_setpin_fin(pin, gate|1000);
    SETPIN pin, CIN [, option]  -> mmg_setpin_cin(pin, option|1);
    SETPIN pin, PIN [, cycles]  -> mmg_setpin_per(pin, cycles|1);

  The mode word PIN is matched BEFORE the pin-pair fallthrough, exactly
  as MMBasic checkstrings "PIN" ahead of evaluating the argument
  (External.c:1604 area) — otherwise it would parse as an expression.

Optional third argument parsed as an expression (MMBasic validates the
range at run time via getint; ours validates in the runtime the same
way — SETPIN's pin and gate can both be expressions).  The refusal
message at mmb2c.py:6024 gains the two words.  cgate must stay at 0
diff lines including a source using both forms.  genkw/keywords.c
regenerated if FIN/CIN are not already in the mmedit table.

### 3.5 What is refused, and how

* FIN/CIN on any pin but GP4-GP7: runtime error (kernel EINVAL surfaced
  as the same "Pin cannot do that" every other bad SETPIN gets).
* `OPTION COUNT`: stays refused by name at translate time (it moves the
  pins; ours are fixed; silence would be divergence).
* `PIN(n) = v` on FIN or PER: "Pin is not an output" (MMBasic's path).
* Manual documents all three, plus the fixed pin set, in the SETPIN
  entry and the migration section (retic's own MIGRATION.md already
  points at this gap).


## 4. Stages and acceptance (the board proves each stage)

**Stage 1 — kernel facility.**  countpin.c, devgpio dispatch, pinlock
teardown, excludes edit, ioctlcheck/pc3io.h/CMake.  Proven from C
first: a utils/cnttest.c driving the ioctls directly, with PWM looped
GP2→GP4 on COM14 — frequency within ±1 Hz at 1 kHz over the default
gate, count arithmetic over a timed window, SIGKILL mid-count leaves
the pin claimable and silent (this is the "flag with one trigger"
shape: the death path gets its own test, not a hope).  Also: the
p2geom.sh RAM delta for the gpio.c move, recorded here.

**Stage 2 — language surface.**  mm_pinct + bcrun row, mmb_gpio.h,
both translators.  Gates: a tests/countpin.bas that exercises compile
shape + host-stub round-trip (bless raw); cgate 0 diff on it and on
retic; fcctests/qemutests/ctest green; skew: the .bc on the OLD bcrun
must refuse by name (one manual check).

**Stage 3 — board acceptance, COM14 (GP2→GP4).**
1. `SETPIN gp2, PWM : PWM ...` at 1 kHz; `SETPIN gp4, FIN`: first read
   inside gate 1 is 0 (faithful), then 1000 ±1.  Gate 100 →
   readings step in 10s.  Gate change, reading re-zeroes.
2. CIN: count over a timed Pause ≈ f×t; `PIN(gp4)=0` zeroes;
   `PIN(gp4)=12345` stores (MMBasic allows any value); option 3 (both
   edges) doubles the rate at 50% duty; option 2 (falling) matches
   rising count.
2b. PER: at 1 kHz, `SETPIN gp4, PIN` reads 1.0 (±1 ms quantization);
   `SETPIN gp4, PIN, 100` reads 1.00 with the averaging tightening it;
   at 10 Hz it reads 100.  First read before the first full cycle
   window is 0, faithfully.
3. Sweep 10 Hz → 100 kHz: FIN tracks; console stays usable at 100 kHz
   (priority-0 IRQ load is real; record the honest ceiling in the
   manual rather than guessing one).
4. `PLAY SOUND` during 100 kHz counting: no audio stutter (the synth
   runs in the DMA IRQ at 0x80; counting preempts it per edge — the
   186 ms cushion should shrug this off, but it is measured, not
   assumed).
5. Termination matrix: clean exit, `SETPIN gp4, OFF`, SIGKILL, exec —
   after each, gp4 claims fresh and works as plain DIN and DOUT
   (the "available for normal I/O when not configured" requirement).
6. Two processes on two count pins concurrently (gp4 + gp5) both
   count; a second claim of gp4 while held is EBUSY.

**Stage 4 — manual + coverage.**  SETPIN entry gains FIN/CIN with the
fixed-pin rule and the measured ceiling; the WebMite-migration section
loses its "no counting inputs" caveat; mmbasic-coverage list
regenerated.

**Stage 5 — retic flow meter (gated on the user's hardware).**
Uncomment the 'PC3: flow block with `Const Flow = gp4`, restore the
setup-page checkbox text, MIGRATION.md updated.  Side-by-side against
a real flow meter is the user's call.

## Status — stages 1-3 DONE AND BOARD-PROVEN, 2026-08-22

**Stage 3 ran on COM14 (GP2 wired to GP4), all green, 2026-08-22:**

* utils/cnttest: **all 20 checks passed on the first run.**  CIN 999/s
  measured against the microsecond clock at 1kHz, set/read 12345
  exact, both-edges 1999/s; FIN latched EXACTLY 1000 on the 1s gate
  and 100 on the 100ms gate (PWM and the gate timer are both
  crystal-derived - the plan's "a tolerance is a bug" held); PER 10ms
  at 100Hz and 500ms over 50 cycles, exact; the whole refusal matrix
  (EINVAL wrong pin/range, EPERM unclaimed and cross-process, EINVAL
  SET-on-PER and READ-after-OFF); and THE test - SIGKILL mid-count,
  reclaim, recount at 999/s.  Also the flash proof by behaviour: these
  ioctls did not exist before.
* BASIC (host-translated count.bas, card-cc-compiled, new bcrun):
  CIN 2000 in 2s, zero/set-5000 round-trip, both-edges 2000/s, FIN
  1000 at gate 1000 AND at gate 250 (the Hz scaling), PER 10 at
  100Hz/10 cycles, then OFF -> DIN reads the wire.  count2.bas swept
  50Hz (52 = +/-1 gate count at a 500ms gate) / 10kHz exact / 100kHz
  exact.
* **100kHz IRQ load is statistically invisible**: the same busy loop
  took 10.223ms under 1kHz edges and 10.252ms under 100kHz.  Priority
  0 confirmed harmless the other way too: **FIN read exactly 100000
  during PLAY SOUND** - the synth's DMA IRQ lost us nothing and we
  lost it nothing (audible stutter check still wants ears at the
  board).
* Board state: COM14 kernel flashed (this build), /usr/bin/bcrun
  84,332 (mm_pinct), /usr/lib/cc/include mmb_gpio.h+mmb_runtime.h
  updated, cnttest/count/count2 left in /root.  COM17 untouched.
* Remaining from the stage-3 list: nothing blocking.  Two-concurrent-
  counters needs a second signal source (bench has one); exec-path
  cleanup is the same pinlock sweep the SIGKILL test proved.

**Next: stage 4 (manual + coverage), then stage 5 (retic flow meter)
when the user's hardware is on the bench.**

### As planned, stages 1+2 (built and gated earlier the same day)

Everything through stage 2 is implemented and green; the board work
(stage 3) waits on a kernel flash.  What was decided during
implementation, where it refines the sections above:

* **The dispatcher relocation is the objcopy rename after all**, not
  the excludes edit: the SDK batch was moved to flash because RAM
  overflowed by 10K (the excludes file's own note), so moving the whole
  object back was the wrong trade.  relocate_gpio_irq_to_ram.cmake
  (ported from PicoMite's cmake/, adapted to .o and made VERIFYING —
  it dies if the section is missing rather than silently leaving the
  dispatcher in flash) runs PRE_LINK from CMakeLists.  Measured
  placement from the map: gpio_default_irq_handler 0x88 bytes at
  0x20001128 (RAM); cnt_gpio_cb 0x68 + cnt_tick_cb 0x7c (RAM);
  countpin_ioctl/cnt_config/countpin_reset/cnt_timer_check all in
  flash by name in the excludes.  **Total new RAM: 364 bytes.**
* **The 1ms gate timer takes the alarm pool's one spare slot**
  (PICO_TIME_DEFAULT_ALARM_POOL_MAX_TIMERS=2: the tick, plus this).
  cnt_timer_check reports failure and the config undoes itself with
  EIO rather than latching a gate that never closes; the CMakeLists
  comment now names the slot's owner.
* **ioctls 0x0537-0x053C**, defined in pico_ioctl.h (with the
  PC3_COUNT_ABI shared guard, PLKIOC's pattern — a program can include
  both pico_ioctl.h and <sys/pc3io.h>) and mirrored there.
  ioctlcheck: 72 codes, no duplicates.
* **mmg_setpin now calls mmg_cnt_leave unconditionally**, so EVERY
  freshly compiled pin program references mm_pinct and needs the
  matching bcrun.  That is the skew rule doing its job (refusal by
  name — watched happen: the stale host bcrun refused all 8 pin tests
  with `no runtime function "mm_pinct"` until rebuilt), and the port
  ships matched sets anyway.
* **The host models the counters** (mmb_runtime.c host mm_pinct: 4
  cells, configure-to-zero/store/read) rather than stubbing to 0, so
  Pin(n)=v / Pin(n) round-trips under the gates.
* Range-error texts: "Invalid gate time" / "Invalid count option" /
  "Invalid cycle count" — ours, in the port's established error idiom;
  MMBasic's getint text is format-driven and was not worth a varargs
  raise.  "Pin cannot do that" covers wrong-pin and claim refusal, as
  it does for every other SETPIN.
* Gates: make check all ok incl. tests/countpin.bas (blessed raw,
  CRLF); cgate **0 diff lines** (countpin plain+fcc both 0/0); fcc
  pipeline 75/0; qemu native sweep 76/0; ctest 165 passed with the 10
  known failures; kernel builds clean with the relocation verified;
  utils/cnttest builds warning-free; board bcrun rebuilt and carries
  mm_pinct; mmedit keywords need nothing (mode words are not in its
  table).

**Next: stage 3 on COM14** — flash build/fuzix.uf2, push bcrun +
cnttest to the board, run cnttest (T1-T10 self-checking, GP2→GP4
loop), then the BASIC acceptance list above.

## 5. Residual / explicitly out

* FFIN (PWM-slice fast counter on a fixed pin) — different hardware
  (no IRQ per edge); only if a program appears that wants >300 kHz.
* WS2812 etc. on count pins — unrelated, pins return to normal duty by
  design.
