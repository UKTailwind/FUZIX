# PLAN-pio: user PIO on PIO0 — MMBasic's runtime surface, an offline assembler

Status: **DESIGN, for review.  No code has been changed.**

2026-08-23.  This executes the decision recorded in COVERAGE-STATUS
NOTES (catmap.py, 2026-08-22): the PIO assembly language gets a
SEPARATE assembler built from MMBasic's own assembler code, whose
output a BASIC program imports; mmbc owes only the runtime surface
(`PIO`, `Pio(` — category 2).  The 22 assembly names stay category 4
by design.  NEXT.md §6's four-piece costing is the starting point and
survives contact with the sources; this plan fills in the design it
asked for, above all the question of where `PIO CONFIGURE` executes.

Reference lines cited below are `d:\Dropbox\PicoMite\PicoMite\misc\
Custom.c` (V6.03.01, the current tree), read this session, except
where another file is named.  Kernel lines are this tree.

The block map, unchanged and load-bearing (pico_ioctl.h:1099-1103):
PIO0 is the user-PIO block — nothing in the kernel touches it today
(verified again this session: grep hits only comments, pinlock's
refusal, and locktest's assertion of that refusal); PIO1 is I2S +
pioout; PIO2 is the radio's bus.  Everything below is PIO0 only.


## 1. What the reference actually does (measured, not remembered)

The whole implementation is one file, `Custom.c` (~2900 lines):
`cmd_pio` at 663-2564 with the assembler inline at 1292-2211,
`fun_pio` at 2565-2841, the inline-assembler statement stubs at
2847-2978.  The facts that shape this design:

**(a) There are TWO configuration commands, and both end in the same
register stores.**  `PIO INIT MACHINE pio, sm, clock [,pinctrl]
[,execctrl] [,shiftctrl] [,start] [,sideout] [,setout] [,outout]`
(2361-2400) takes the three RAW REGISTER WORDS, composed by the user
(usually with the `Pio(PINCTRL...)`, `Pio(EXECCTRL...)`,
`Pio(SHIFTCTRL...)` helper functions, which are pure bit arithmetic —
2568-2707), and `pio_init()` (477-520) assigns them straight into the
`pio_sm_config` struct.  `PIO CONFIGURE pio, sm, clock, start,
<23-25 named fields>` (2430-2548) instead composes those same three
words through the SDK's `sm_config_set_*` calls in `configurePIO()`
(384-476).  Either way the terminal operations are identical:
`pio_sm_set_config` + `pio_sm_init` (= write CLKDIV, EXECCTRL,
SHIFTCTRL, PINCTRL, restart the SM, clear the FIFOs, jump to start)
plus `pio_sm_set_consecutive_pindirs` for the out/set/side ranges.

**The decisive observation: every one of those SDK functions is a
STATIC INLINE in the SDK's own headers.**  `sm_config_set_wrap`,
`set_out_pins`, `set_sideset`, `set_clkdiv`, `pio_sm_set_config`,
`pio_sm_exec` — they compile to stores against the PIO block; there
is no SDK .c code behind them to "call into".  "Executing the SDK
calls" and "writing the four per-SM registers" are the same machine
instructions.  That fact drives §2.

**(b) The assembler's output path is direct instruction-memory
writes.**  `.end program` resolves labels and then does
`pio->instr_mem[i] = instructions[i]` at absolute slots (2159-2165);
`.line n` sets the origin, `nextline[]` tracks packing, and there is
no `pio_add_program`, no relocation.  `PIO PROGRAM pio, a%()`
(2264-2296) is the whole-image form: exactly 8 int64 = 32
instructions, all four SMs disabled, memory cleared, image loaded at
origin 0.  `PIO PROGRAM LINE pio, slot, instr` (1268-1290) pokes one
slot.  So MMBasic already contains the import surface an offline
assembler needs — the assembler and the loader are separable in the
reference itself.

**(c) DMA is five fixed channels and a chain trick.**  TX 4/5/6, RX
8/9 (configuration.h:562-567).  Ring and continuous modes chain the
primary channel to a second, which rewrites the primary's
`al2_write_addr_trig`/`al3_read_addr_trig` from a saved pointer and
re-triggers it forever (754-862, 901-1143).  `PIO MAKE RING BUFFER
var, size` (2240-2259) retro-fits a scalar global integer into a
power-of-two-ALIGNED array (`GetAlignedMemory`) because
`channel_config_set_ring` demands aligned addresses.  Size is in
bytes, 256..32768, power of two.

**(d) Nothing is interrupt-driven.**  `PIO INTERRUPT` handlers and
the DMA completion vectors are polled in MMBasic's command loop
(MM_Misc.c:9925-9981): RX fires on "FIFO not empty", TX on the edge
"was full, now isn't", DMA on `!dma_channel_is_busy` (one-shot, and
it stops the SM).  No `PIO0_IRQ_n` handler exists anywhere in the
reference for user PIO.  This is excellent news for us: the reference
semantics are ALREADY polling semantics, so replicating them needs no
kernel interrupt work at all.

**(e) Reset is looser than ours.**  `ClearExternalIO`
(External.c:5987-6012) clears the interrupt vectors and aborts the
DMA channels but leaves the state machines RUNNING across program
end, RUN and CTRL-C.  `PIO CLEAR pio` (2212-2237) is the explicit
teardown, and it too is direct register writes (CTRL clear-bits per
SM, reset values into the three config registers, FIFOs, `irq=255`).

**(f) Reference bugs found while reading (the MATH CRC precedent:
replicate semantics, not defects — each recorded in §7.4):**
`PIO SYNC`'s pio==1 branch writes pio0's CTRL (374-377); the DMA
completion poll maps any nonzero PIO to pio1, so PIO2 completions
disable the wrong SM (MM_Misc.c:9961/9972); the TX-interrupt
"was full" depth reads SM0's SHIFTCTRL whatever `sm` is
(MM_Misc.c:9944); `configurePIO`'s rp2350 put/get-join branch tests
`joinrxfifoput` in all three conditions so GET-alone is never applied
(460-465); the RX DMA DREQ selection at 801-804 is dead code,
overwritten at 805.

**(g) MMBasic has no SM-level ownership at all.**  Availability is
three block-level booleans; within an available block a user can
clobber anything, and per-pin checking exists only on the
`PIO CONFIGURE` path (`getGPpin`, 627-641).  We are not replicating
that looseness — pinlock is the port's law — but it means no MMBasic
program depends on finer-grained arbitration than "the block".


## 2. Where does PIO CONFIGURE execute?  The options, and the answer

The question this plan exists to answer.  Four architectures were on
the table; the numbers and the platform's own precedents decide it.

**Option A — an ioctl per SDK function.**  A marshalled RPC:
userland sends (function-id, args), the kernel executes the SDK call.
Cost is real but NOT the discriminator: a full CONFIGURE is ~15
calls × 1.488us ≈ 22us, once per program start — noise.  What kills
it: per §1(a) there is no SDK code to execute — the kernel would be
running the same stores userland can run, after a 1.5us crossing,
through a marshalling layer that is a third copy of the ABI
(mmb_runtime.c:5150-5155 names that exact trap).  And it protects
nothing: no MMU means a wild userland store reaches PIO0 regardless
(pinlock.c:23-26).  Rejected.

**Option B — one batch ioctl (`PIO0_APPLY` carrying a config
struct).**  The GFXIOC_PIXELS shape.  Same analysis as A with fewer
crossings; still kernel flash, still a third ABI copy, still no
protection.  Batching earned its place in graphics because the
per-datum path crossed the kernel; PIO's per-datum path (§below)
never does.  Rejected — but recorded as the fallback shape if PIO0
ever had to be shared per-SM between processes, which §4 rejects for
its own reason.

**Option C — kernel-exported function table (the libm_table shape).**
Direct calls into kernel flash, no crossing.  Dies on the same fact
as A: the SDK functions are header inlines, so the kernel has no
function bodies to export.  There is nothing behind the door.

**Option D — userland drives the registers under a whole-block
claim.  CHOSEN.**  This is the port's standing shape (pc3io.h:4-19:
claim once, then 10ns stores vs 1.488us crossings), it is what
pioout already proved for PIO1's harder case (shared CTRL), and for
PIO0 it is STRICTLY SIMPLER: the claimant owns the entire block, so
every PIO0 register — CTRL included — is process-exclusive and plain
read-modify-write is legal.  The ONE HARD RULE (atomic aliases only)
remains scoped to PIO1, where it belongs.

So: **`PIO CONFIGURE` and `PIO INIT MACHINE` become ~60 lines of
register arithmetic in a program-side header (`mmb_pio.h`),
replicating `configurePIO()`/`pio_init()` line for line** — we hold
the exact decode from this session's reading (§1(a), §7).  The
kernel's part is what a program cannot do for itself, exactly as
pioout.c:5-11 states it: PLACEMENT and OWNERSHIP — the block claim,
the DMA channel claims, and the death sweep.  **No new ioctl is
needed anywhere in this design** — the claim path is the existing
PLKIOC, the DMA buffers come from the existing PSRAMIOC_ALLOC
(§5), and the counting-inputs/pioout precedent says ioctlcheck
staying untouched is a feature, not an omission.

The condition that would reverse this: if two processes could each
own one PIO0 state machine, CTRL and the instruction memory would be
cross-process shared and the kernel would have to mediate.  §4
refuses per-SM sharing for a prior reason — instruction memory is a
whole-block resource (`PIO PROGRAM` loads all 32 slots; NEXT.md:177)
— so the case never arises.


## 3. The hardware budget

| resource | disposition |
|---|---|
| PIO0, all of it | the user block: 4 SMs, 32 instr slots, GPIOBASE, IRQ flags — one claimant at a time |
| PIO0 DMA | four channels, boot-claimed by explicit number (§4): TX + TX-retrigger + RX + RX-retrigger |
| PIO0_IRQ_0/1 | untouched — nothing in this design uses interrupts, per §1(d) |
| alarm pool | untouched (stays 2/2 — the pioout discipline holds) |
| kernel RAM | zero bytes of buffers — DMA memory is the caller's arena (§5) |
| pinlock table | block + 4 DMA + up to ~13 pins ≈ 18 of PLK_SLOTS 24 — fits, with room |

**DMA channel numbers.**  Only pioout's 11 is source-guaranteed
today; display takes the first two unused (0,1 in practice), sound
the next two, and the radio claims its own lazily inside
`cyw43_arch_init()` at NETIOC_UP.  Following pioout.c:123-126
("explicit numbers, so nothing may depend on init order"), boot
claims by number:

| symbol | ch | role |
|---|---|---|
| `PIO0_DMA_TX` | 7 | TX primary |
| `PIO0_DMA_TX2` | 8 | TX retrigger (ring/continuous) |
| `PIO0_DMA_RX` | 9 | RX primary |
| `PIO0_DMA_RX2` | 10 | RX retrigger (free-running ring) |

claimed with `dma_channel_claim()` in a new kernel `pio0.c` init
(beside pioout's, after sound), leaving 4-6 and 12-15 for the SDK's
lazy claimers.  RP2350 has 16 channels; nothing tightens.

**The pin window.**  PIO0's GPIOBASE is NOT pinned (NEXT.md:172-176)
— this block gets the escape hatch PLAN-pioout.md:77-84 promised.
`PIO SET BASE 0, 0|16` is a userland GPIOBASE write under the block
claim, and the reachable claimable pins are the window intersected
with pinlock's header set:

| base | claimable header pins in reach |
|---|---|
| 0 | GP0-GP7, GP26 |
| 16 | GP26, GP32 (PC3 only — the DS3231 pin on a PC2), GP34-GP46 |

Thirteen-plus header pins no PIO on this machine could drive until
now.  The reference's own base rules replicate as-is: only 0 and 16,
`PIO CONFIGURE` reads the base back from hardware and defaults its
pin bases to it (2457-2467), `Pio(PINCTRL...)`'s GP numbers are used
verbatim as 5-bit window-relative fields (2576-2628).


## 4. Ownership and lifecycle

**The claim is the BLOCK.**  `PLK_PIO` idx follows the established
pio*4+sm numbering (pioout is idx 5 = pio1 sm1), so **idx 0 is
defined as "the PIO0 block"**: all four SMs, the instruction memory,
GPIOBASE, the IRQ flag register, and the four DMA channels' purpose.
`claimable()` (pinlock.c:126-131) learns `idx == 0 ||
idx == PIOOUT_PLK_IDX`; indices 1-3 stay refused forever — they can
never be claimed separately because the instruction memory cannot be
(NEXT.md:177).  `PLK_DMA` learns 7-10 beside 11.  A second process
asking gets EBUSY, honestly; "already ours" succeeds, as ever
(pinlock.c:228-233).

**The latent bug gets fixed here, as NEXT.md:178-180 ordered.**
`reset_one()` currently calls `pioout_sm_reset()` for EVERY PLK_PIO
index and `pioout_dma_reset()` for every PLK_DMA index
(pinlock.c:202-210).  It learns to dispatch: PLK_PIO idx 5 →
pioout's reset; **idx 0 → `pio0_block_reset()`** in the new kernel
pio0.c: disable all four SMs, clear FIFOs, restart SMs and clkdivs,
`pio0->irq = 255`, GPIOBASE back to 0 — the same registers `PIO
CLEAR` writes (§1(e)), because that is the proven teardown.  No
instruction-memory writes (it is write-only and inert once the SMs
are off — the trap pioout paid for stays paid).  PLK_DMA idx n →
`dma_channel_abort(n)` generically, which also retires the pioout
special case.

**Claims ride on `SETPIN GPn, PIO0`.**  In the reference, routing a
pin to PIO is `SETPIN`'s job (`gpio_set_function(GPIO_FUNC_PIOn)` +
input enable, External.c:1506-1522), and `PIO CONFIGURE` merely
validates ownership on its named pins while `PIO INIT MACHINE`
checks nothing.  We keep that shape exactly: `SETPIN GPn, PIO0`
claims PLK_PIN and writes funcsel 6 + pad connect (IE on, ISO clear
— the pc3_pioout_pin recipe at pc3io.h:727-732, funcsel adjusted);
the CONFIGURE path maps a claim failure onto MMBasic's "Pin in use".
The first PIO statement in a program claims the block + the four DMA
channels through the existing `mm_gpio(MM_GPIO_CLAIM,...)` path,
cached ours-already thereafter — the mmg_pioout_claim() pattern
(mmb_pioout.h:78-87), one ioctl set on the first call.

**The A9-errata walk is scoped, as NEXT.md:181-185 required.**
`PIO CONFIGURE` in the reference enables the input buffer on EVERY
pin 1..43 (2538-2547), which on this machine reaches the SD card,
the console UART, the RTC and the display.  Ours touches only pins
this process has claimed for PIO0 — where SETPIN already set IE, so
the walk is a no-op in practice.  Divergence recorded (§7.3).

**Exit stops the machines.**  The reference leaves SMs running after
END (§1(e)); under Fuzix the death sweep resets what you owned —
platform law, and the correct trade (a killed program must not leave
a wedged wire).  A program that wants "leave it running" keeps a
process alive to own it.  Divergence recorded (§7.3).


## 5. DMA under the platform law: the arena, not a kernel buffer

THE DMA MUST NEVER TOUCH PROCESS MEMORY (pico_ioctl.h:1124-1141 —
the swapper shuffles the pool in 4K chunks on every context switch).
pioout solved this with a fixed boot-reserved kernel buffer because
its transactions are bounded and serial.  User PIO wants
arbitrary-sized, possibly multiple, possibly long-lived buffers —
and the kernel ALREADY has the right object: **`PSRAMIOC_ALLOC`
hands a process a never-moved, never-swapped, exit-released PSRAM
block, and `valaddr` already blesses owned arenas**
(misc.c:1176-1181).  NEXT.md §6.4 called this: what is missing is
only that a translated program cannot reach the arena — **a new
libcall name, not a new mechanism**.

**`mm_parena(bytes)`** → arena address, 0 on failure.  Its own
libcall name for the usual skew reason (mmb_runtime.h:687-692): a
program using PIO DMA on an old bcrun is refused at load instead of
turning -1 into a pointer.  On the host build it is a real buffer
`mmap`'d LOW with MAP_32BIT — the PIE-static trap that cost a gate
run is already paid for and documented at mmb_runtime.c:5261-5288;
the model copies that code.  Board side is one PSRAMIOC_ALLOC ioctl;
release is automatic at exit (`arena_release` in `pagemap_free`).

**`PIO MAKE RING BUFFER a%, size`** replicates to the letter on top
of it: size in bytes, 256..32768, power of two, else "Not power of
2".  mmbc translates the named variable — which the reference
requires to be a not-yet-dimensioned global scalar integer, and so
can we — into a pointer-backed int64 array of size/8 elements whose
storage is `mm_parena(2*size)` aligned up to `size` (over-allocate
to guarantee `channel_config_set_ring`'s alignment; the reference
gets alignment from `GetAlignedMemory`).  The array then works in
every BASIC context — it is an ordinary array whose bytes happen to
live at a stable absolute address the DMA may legally read, and
`Pio(DMA RX POINTER)` (the raw `write_addr`, 2825-2831) is
meaningful against it exactly as on a PicoMite.

**Which arrays may feed which DMA forms:**

| form | ordinary array | ring-buffer array |
|---|---|---|
| `PIO DMA TX` one-shot (nbr>0, size 0) | YES — bounced: copied into an arena staging block at the statement, DMA reads the copy | YES — in place |
| `PIO DMA TX` continuous (size=nbr) and ring (nbr=0) | refused: "needs a ring buffer array" | YES — in place |
| `PIO DMA RX` all forms | refused: same error | YES — in place, progressive visibility preserved |
| `PIO DMA TX TABLE` | refused by name (§7.3) | — |

The bounce keeps the common one-shot-TX-from-an-ordinary-array
programs working unchanged; its one observable divergence (a mutation
of the source array DURING the transfer is not seen — the reference's
DMA would see it) is recorded in §7.3.  RX cannot bounce without
losing the progressive visibility MMBasic programs poll for, so it
asks for the ring array honestly — the error message says what to
type.  Staging block: one `mm_parena(40000)` on first bounced TX
(BITSTREAM's own cap, a size with a track record), reused.

`PIO DMA RX/TX OFF` and the pre-setup abort replicate as userland
writes of our four bits into `dma_hw->abort` (write-1, self-
clearing, no read-modify-write hazard) then per-channel BUSY waits —
the reference's own sequence (737-753).

**QMI note:** DMA-from-PSRAM is board-proven at pioout's rates; PIO0
adds DMA-INTO-PSRAM for RX.  Same bus, same absorption argument
(PLAN-pioout.md §4), and the scanout shares the QMI — a pathological
all-out-full-speed SM is possible in a way pioout could not express,
so acceptance (§9) includes a scanout-under-max-rate check.  Judged
on behaviour, not feared in advance.


## 6. The offline assembler: mmpioasm

Ported from the reference's own code, as decided.  The assembler
core is `Custom.c:1292-2211` (~920 lines) whose only dependencies
are `getint`, `GetMemory`, `error` and one hardware store, plus the
~130-line dialect front end (2847-2978).  NEXT.md's estimate stands:
~1300 lines standalone, a host program first (it is pure text → 32
words), cross-built for the board like any tool.

**Input: the MMBasic dialect, exactly.**  A text file whose lines
are what a PicoMite program would contain between `.program` and
`.end program`: the six directives (accepting both the `.side set`
and `.side_set` spellings the tokeniser folds, MMBasic.c:1285-1294),
`label:` lines, and the instruction set including the RP2350 rows
(`IRQ PREV/NEXT`, `WAIT ... JMPPIN`, `MOV RXFIFO[..]` and its
PUSH/PULL re-encodings, 1825-1948).  Published PicoMite PIO examples
must assemble byte-for-byte — that is the point of porting rather
than adopting pioasm, and it is what the gate checks.

**Output: a `.bas` fragment the program `#Include`s.**  For
`mmpioasm blink.pio` → `blink.pio.bas`:

    ' mmpioasm 1.0 from blink.pio -- do not edit
    DIM PIO_IMG%(7) = (&H...,&H...,&H...,&H...,&H...,&H...,&H...,&H...)
    CONST BLINK_START = 0          ' from .line / packing
    CONST BLINK_WRAP_TARGET = 0    ' feed to Pio(EXECCTRL...) / PIO CONFIGURE
    CONST BLINK_WRAP = 4
    CONST BLINK_SIDE_COUNT = 1    ' etc: what .side_set declared
    CONST PIO_NEXT_LINE = 5        ' first free slot after all programs

One image, all `.program` blocks in the file packed by the same
`.line`/`nextline` rules the reference uses (2093-2105, 2166-2168),
gaps filled with NOP; per-program constants carry what the
reference's `Pio(.WRAP)`, `Pio(.WRAP TARGET)` and `Pio(NEXT LINE)`
read back from live assembler state — offline, they are constants,
and those three function forms are refused by name with a message
naming the constant (§7.3).  The program then runs the reference's
own import surface, unchanged:

    #Include "blink.pio.bas"
    PIO PROGRAM 0, PIO_IMG%()
    PIO INIT MACHINE 0, 0, 2e6, Pio(PINCTRL 1,,,,GP0),
        Pio(EXECCTRL GP0, BLINK_WRAP_TARGET, BLINK_WRAP)
    PIO START 0, 0

A `-c` flag emits the same data as a C header for native cc
programs; same words, trivial second printer.

**`#Include` is new in mmbc** — one lexer-level file push, flattening
exactly as MMEdit does for PicoMite users (the convention already in
the ecosystem), in both translators, cgate holding the 0 diff.  It
is deliberately general (any .bas fragment), but this plan only
requires it for assembler output.

**The gate that proves the port:** assemble the reference's shipped
examples and diff all 32 words against a real PicoMite's `PIO
ASSEMBLE ... LIST` output for the same source ([[pc3-side-by-side]]
— the authority is the running original, not the reading of it).
That is a host gate once the LIST outputs are captured on the bench.


## 7. The BASIC surface

### 7.1 What ships, and where it executes

Everything below is program-side `mmb_pio.h` (file-scope statics,
dead code stripped by cc1 — the mmb_gpio.h argument at
mmb_gpio.h:6-12) driving pc3io.h's new PIO0 section.  bcrun grows
ONE name: `mm_parena`.

| statement / function | execution |
|---|---|
| `PIO PROGRAM pio, a%()` | 4× CTRL enable-clear, 32 instr_mem stores (array must be 8 int64, else "Array size" — 2264-2296) |
| `PIO PROGRAM LINE pio, slot, instr` | one instr_mem store (1289) |
| `PIO INIT MACHINE ...` | the pio_init() decode of §1(a): clkdiv from `375000*1000.0/clock` in 16.8, the three words stored raw, restart+FIFO-clear+jump via SM_INSTR, pindirs from the decoded pinctrl fields via SM exec (the pc3_pioout_setup recipe, pc3io.h:746-776) |
| `PIO CONFIGURE ...` (27/29 args) | the configurePIO() composition of §1(a) as arithmetic, then the same terminal stores; pin args translated and claim-checked (getGPpin semantics, 627-641) |
| `PIO SET BASE pio, 0\|16` | one GPIOBASE store (2402-2427; base 16 valid — PC3 is an RP2350B) |
| `PIO START pio, sm` | the six-step startPIO() sequence (352-360) as stores |
| `PIO STOP pio, sm` | CTRL enable-clear (2358) |
| `PIO CLEAR pio` | the register teardown of §1(e) (2228-2237) |
| `PIO EXECUTE pio, sm, ins...` | SM_INSTR stores (695-698) |
| `PIO WRITE pio, sm, count, d...` | blocking TXF stores watching FSTAT (724-730) |
| `PIO READ pio, sm, count, var` | the underflow-aware read: FDEBUG W1C, RXF load, -1 on empty (1218-1245) — exact |
| `PIO WRITEFIFO` / `Pio(READFIFO)` | rxf_putget stores/loads (1263, 2744) |
| `PIO DMA RX / TX / OFF`, `PIO MAKE RING BUFFER` | §5 |
| `PIO SYNC pio, mymask` | CTRL CLKDIV_RESTART set-bits (361-383) — PIO0 only; see §7.3 for prev/next |
| `Pio(PINCTRL/EXECCTRL/SHIFTCTRL ...)` | pure bit arithmetic, layouts captured in §1's reading (2576-2707) — translator parses the GP-literal and dotted argument syntax |
| `Pio(FSTAT/FDEBUG/FLEVEL ...)` | register loads (2709-2824), including the per-SM FLEVEL nibbles |
| `Pio(DMA RX POINTER)` / `(DMA TX POINTER)` | DMA write_addr/read_addr loads (2825-2839) |
| `SETPIN GPn, PIO0` | claim + funcsel 6 + pad connect (§4) |

Error strings, ranges and defaults replicate to the letter from the
cited lines — including `PIO INIT MACHINE`'s defaults (execctrl
0x1F000, shiftctrl 0xC0000, 2383-2387), CLKMIN/CLKMAX from the fixed
375MHz (92-93), and "PIO n not available" for pio 1 and 2, which is
literally true here for a better reason than on a PicoMite.

### 7.2 PIO INTERRUPT and the DMA interrupt arguments: phase 2

The reference's semantics are statement-boundary polling (§1(d)), so
the faithful implementation is the ON ERROR recipe: checks emitted
ONLY for programs that arm them ([[pc3-onerror-checks-gating]] — the
+11.9% lesson), replicating MM_Misc.c:9925-9981 including the TX
was-full edge detector.  It is honest, bounded work — but it is the
one piece with a bcrun-overhead price tag attached, so it ships as
its own phase AFTER the surface is board-proven, and phase 1 refuses
the `intr` arguments and `PIO INTERRUPT` by name ("PIO interrupts
not supported yet — poll Pio(DMA RX POINTER)"), which is also what a
PicoMite program does in continuous modes, where the reference
itself refuses interrupts (781, 1010).

### 7.3 Divergences, disclosed (the honest-gap list)

1. **Assembly is offline** (the decided split).  The 22 in-language
   names error as unknown statements; `PIO ASSEMBLE` is refused by
   name pointing at mmpioasm.  `Pio(.WRAP)`, `Pio(.WRAP TARGET)`,
   `Pio(NEXT LINE)` are refused naming the emitted constants.
2. **One owner per block**: a second process's first PIO statement
   gets EBUSY ("PIO 0 in use").  No reference analogue — one
   program per machine there.
3. **Exit resets**: SMs stop and pins release when the owner exits;
   the reference leaves them running (§4).
4. **Errata IE walk scoped** to this process's PIO0 pins (§4).
5. **DMA arrays**: RX and continuous/ring TX require a
   `PIO MAKE RING BUFFER` array; one-shot TX from an ordinary array
   is bounced, so mid-transfer source mutation is not observed (§5).
6. **`PIO DMA TX TABLE` refused by name**: it takes raw addresses
   into a hardware-IRQ scanline walker holding DMA_IRQ_0 at priority
   0 (212-252) — a display-driver mechanism, against both the DMA
   law and the no-interrupts shape.  A future video use case goes
   through the kernel display path, not here.
7. **`PIO SYNC` prev/next masks refused by name**: they reach other
   blocks' CTRL (the audio's, the radio's) — and the reference's own
   pio==1 branch writes the wrong block anyway (§1(f)).  `mymask`
   within PIO0 ships.
8. **`MEMORY SHARE HOST/CLIENT`** stay out (category 5 with the rest
   of Device/Memory-share; a different machine's feature).

### 7.4 Reference defects deliberately not copied

Per the MATH CRC precedent (three reference bugs not copied, gates
diff against corrected truth): the SYNC wrong-block write, the
completion-poll pio2→pio1 mapping, the TX-full depth from SM0's
SHIFTCTRL, and the joinrxfifoget dead branch (§1(f)) are implemented
as intended, not as written, each with a comment citing the
reference line.  None is reachable-correct code a program could
have depended on: the first three are wrong-target writes on
hardware we don't share, the fourth makes GET-alone silently
identical to no-join.


## 8. Kernel and header changes, exactly

Kernel (`platform-rpipico`), all small:
* new `pio0.c`: boot init (claim DMA 7-10 by number, assert
  GPIOBASE 0 at boot the way pioout does), `pio0_block_reset()`.
  No programs to load — the block boots empty, which is the point.
* `pinlock.c`: `claimable()` learns PLK_PIO idx 0 and PLK_DMA 7-10;
  `reset_one()` dispatches per idx (fixes the recorded latent bug);
  the stale "refused for now" comments in pico_ioctl.h:970-989 and
  pc3io.h:82-83 corrected while there.
* `pico_ioctl.h`: a `PC3_PIO0_ABI` block (the channel numbers, the
  PLK index, the base rule) beside PC3_PIOOUT_ABI.  **No new ioctl;
  ioctlcheck output is unchanged.**
* `utils/locktest.c:139-140` updated: PIO0 claim now SUCCEEDS, and
  the test grows the block-reset-on-death case (locktest-style kill
  check for SMs + 4 channels).

Library:
* `pc3io.h`: a PIO0 section parameterized by sm — block base
  0x50200000, CTRL/FSTAT/FDEBUG/FLEVEL/IRQ, TXF(sm)/RXF(sm),
  RXF_PUTGET(sm,i), INSTR_MEM(i), SM regs(sm), GPIOBASE — plus DMA
  accessors parameterized by channel with the al2/al3 trigger
  offsets and the abort register.  Plain accessors are legal here
  (whole-block owner); the section comment says exactly why PIO1's
  atomic-only rule does not carry over, so nobody "fixes" it.

mmb2c:
* `mmb_pio.h` (program-side, both translators emit it gated on use),
  `mmb_runtime.h` grows `mm_parena` + its MM_ ioctl-number copy
  (the keep-in-step comment, as ever), `bcrun_mm.c` one wrapper +
  one `mmwtab` line, `mmb_runtime.c` board impl (PSRAMIOC_ALLOC) and
  host model (MAP_32BIT mmap).
* mmbc + mmb2c.py: `#Include`, the `PIO`/`Pio(` dispatches, `SETPIN
  ... PIO0`, `PIO MAKE RING BUFFER`'s array transform, cgate 0 diff.
* Host gates: mmb_pio.h's shadow-register model (the mmg hostlatch
  pattern) so the composers' words and the packing arithmetic run
  under fcc/qemu; plus one host gate compiling the composer
  arithmetic beside the real pico-sdk headers and diffing produced
  config words over a matrix of argument sets — the SDK itself as
  the oracle for §1(a)'s claim.


## 9. Acceptance (the counting inputs are the instrument, again)

On COM14, GP2→GP4 loop unless said otherwise:

1. **mmpioasm gate**: reference examples' 32 words diff clean
   against a bench PicoMite's `PIO ... LIST` (§6).
2. **Square wave truth**: a two-instruction side-set blinker on GP2
   at a computed frequency → `SETPIN GP4, FIN` reads it exactly;
   repeat at base 16 on GP34→ (jumper) to prove the window.
3. **CONFIGURE ≡ INIT MACHINE**: the same program brought up both
   ways produces the same FIN reading and the same four SM register
   words (Pio(FDEBUG)/register readback via Pio( where readable).
4. **FIFO truth**: `PIO WRITE`/`PIO READ` loopback through an
   `out`/`in` program; READ's -1-on-empty against a stopped SM.
5. **DMA TX**: a counted stream → CIN counts exactly n; ring TX
   sustained while the owner sleeps (the swapper shape that killed
   the first pioout design — now it MUST pass, the array is arena).
6. **DMA RX**: pioout's WS2812 zero-frame on GP2 captured by a PIO0
   RX program on GP4 — the two PIO systems cross-check; ring RX
   pointer advances observed from BASIC.
7. **Kill mid-everything**: SIGKILL with SMs running + both DMAs
   active → pins, block, channels claimable; next run exact; audio
   playing throughout, unaffected; scanout clean during a maximal-
   rate TX (the §5 QMI check).
8. **Side-by-side**: one canonical program (blinker + FIFO echo) run
   on the bench PicoMite and the PC3, outputs compared — the
   authority rule before the manual chapter is written.

Gates as ever: make check with tests/pio*.bas, cgate 0 diff, fcc,
qemu, ioctlcheck (unchanged), locktest (updated), usbcheck.


## 10. Order of work

1. **mmpioasm** (host): port, capture PicoMite LIST outputs, gate
   green.  Proves the instruction words before any kernel work.
2. **Kernel pio0.c + pinlock** (+ locktest): provable from C
   (utils/pio0test.c) against the counting inputs before BASIC
   exists — blink, claim, kill, reclaim.
3. **pc3io.h PIO0 section**; pio0test moves onto it.
4. **mmb_pio.h + translators, no DMA**: PROGRAM, INIT MACHINE,
   CONFIGURE, SET BASE, START/STOP/CLEAR, EXECUTE, WRITE/READ,
   WRITEFIFO/READFIFO, SYNC(mymask), the Pio( reads and composers,
   SETPIN PIO0, `#Include`.  Acceptance items 2-4, 8.
5. **DMA**: mm_parena, MAKE RING BUFFER, the four forms + OFF,
   POINTER functions.  Acceptance 5-7.
6. **Manual chapter + COVERAGE-STATUS** (PIO and Pio( move to
   translated when the dispatches learn them), mancheck, the v-next
   release recipe as ever.

Phase 2, separately reviewed: PIO INTERRUPT + DMA intr arguments via
gated statement-boundary checks (§7.2).

Not in scope: PIO1/PIO2 (owned), PIO DMA TX TABLE, MEMORY SHARE,
PIO SYNC prev/next, the in-language assembler (decided), and any
alarm-pool or interrupt consumption.
