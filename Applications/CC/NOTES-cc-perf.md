# cc-perf branch: experiment log

Branch for performance/memory experiments out of REVIEW-2026-08-06.md,
each change A/B-gated by an env knob and measured on the hardware
before it is trusted.  Protocol per change: host suite (all.sh),
thumb/gate.sh five ways, qemudiff, mmb2c qemutests, ctest 165, then
the board.  Board numbers are RP2350B @ 378 MHz, the 2026-08-05 bcrun
(the object format and runtime are untouched on this branch so far).

Reference points: gcc -O2 cross 379,022 D/s; pc3-branch compiler
103,548 D/s (2026-08-05) / 102,842 measured this session at 400,000
runs; eclipse 2.312-2.317 s.

## 2026-08-06: direct [r4,#off] frame access  (THUMB_NOR4 disables)

LOCALn;LOADx becomes one load off r4; STOREx through a tracked frame
slot pops the address unread and stores off r4.  Commit 6f652e9b4.

## 2026-08-06: constant right operands  (THUMB_NOCFOLD disables)

PUSH;CONSTk;OP32 runs entirely in registers: add/sub/cmp immediates,
shifts by imm5, and/or/xor imm8, uxtb/uxth masks, constant built in
r2 otherwise.  Commit e784166a9.

## Board results, 2026-08-06 (all outputs verified identical)

| Dhrystone 2.1, 400,000 runs | D/s | vs base |
|---|---|---|
| base = pc3 shape (both knobs set) | 102,842 | - |
| + direct [r4,#off] | 110,487 | +7.4% |
| + constant folding (branch default) | 116,776 | **+13.5%** |

Now 30.8% of gcc -O2, from 27.1%.

Eclipse (DCP-double dominated, as predicted): 2.3165 -> 2.3022 s,
-0.6%.  Object sizes: dhry 15,910 -> 14,952 (-6.0%); eclipse
141,384 -> 133,160 (-5.8%) - the size win also buys back native-cliff
headroom (a function's native span must fit THUMB_MAXFN).

qemu wall time had predicted -9%; the board gave -11.9%.  qemu
understates memory-traffic wins - expect this again.

## Next candidates (from the review, in payoff order)

1. Local right operand: PUSH;LOCALn;LOADx;OP - the other half of the
   binary-operator round trip (ldr r2,[r4,#n-4]; op r0,r0,r2), turns
   `a+b` into the 3-instruction gcc shape.  Needs the reversed-operand
   encodings cfold already introduced.
2. LOCAL;PUSH elision for stores whose slot is provably unread - needs
   DUP/SWAP/inlined-eqop consumer analysis first (they read the top
   slot); without it this is the silent-wrong-answer class.
3. String/memory fast slots (strcpy/strcmp/memcpy/strlen through
   helper-vector slots like the DCP ops) + word-wise lib_strcmp -
   ~18% of the remaining Dhrystone gap is library crossings.  Touches
   bcrun: needs a board bcrun resend, unlike everything above.
4. Memory R1-R3 from the review (ELF reloc segment freed, mm libm
   shims, lazy profiling arrays) - independent of the translator work.
