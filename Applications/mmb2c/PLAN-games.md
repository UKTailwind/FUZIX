# PLAN: BLIT, SPRITE, PLAY SOUND/TONE, PLAY MODFILE/MODSAMPLE

The four capabilities that let existing MMBasic games run under Fuzix.
Reference implementation: PicoMite V6.03.00 (`D:\Dropbox\PicoMite\PicoMiteV6.03.00`),
replicated exactly per the standing rule — a silent divergence outranks an
honest gap. Audio synthesis reference: the MicroPython PC3 port
(`ports/rp2/audio.c`, `sound_tables.h`, `hxcmod.c`), which is itself
transcribed from the same PicoMite sources.

Everything possible lives in userland. Kernel changes: **one new ioctl**
(GFXIOC_SCROLL2), plus one *contingent* ioctl (rectangle blit) that is only
built if a Phase 0 measurement says per-row ioctls are too slow.

---

## 1. Ground truth (what the surveys established)

### Kernel facilities already present (`pico_ioctl.h`)

| facility | what it gives us |
|---|---|
| `GFXIOC_BLIT 0x05` / `BLITRD 0x32` | linear byte-range write/read of the caller's draw target (`struct gfx_blit {uint16 offset; uint16 len; void *buf;}`). A rectangle = one call per row. Unused by mmb2c until now. |
| `GFXIOC_FBSEL/FBOPEN/FBCOPY2/MERGE` | the F and L PSRAM buffers, per-process draw target, whole-buffer copy, L-over-F composite |
| `GFXIOC_SCROLL 0x1B` | vertical-only memmove scroll with fill colour — insufficient for SPRITE SCROLL (no horizontal, no wrap) |
| `SNDIOC_PCMOPEN/WRITE/STAT/CLOSE/OWNER` | 16-bit PCM, 8000–48000 Hz, mono/stereo, 256 KB PSRAM ring, one-owner lock. `PCMOWNER` names the owning pid — PLAY STOP already keys off it. |
| `PSRAMIOC_ALLOC/FREE/...` | the arena: raw-addressed PSRAM, per-process, freed on exit/exec. `valaddr` already accepts owned arena addresses, so arena pointers can be passed to gfx ioctls. |
| FIFOs | `mknod(path, F_PIPE)` needs no superuser; `open()` rendezvous and `select()` on pipes work. This is how a running player accepts commands. |

Costs: ioctl ≈ 1.5 µs. Mode geometry: MODE 1 = kernel 0xFF, 640×480×1bpp,
stride 80, MSB = left pixel. MODE 2 = kernel 7, 320×240×4bpp, stride 160,
**high nibble = left pixel** — the *opposite* of PicoMite's RGB121 packing
(low nibble = left, `RGB121.c:79`). Every transcribed blit routine must
mirror its nibble arithmetic; the golden-image tests exist to catch exactly
this.

Kernel SRAM is full (~8.6 KB headroom; two flag bytes once overflowed the
link). New kernel code is flash-resident and may use at most a
stride-sized (160 B) static temp.

### The QMI cache rule (applies to every phase)

All kernel code runs XIP from flash and all large data lives in PSRAM,
both behind the QMI cache. **Structure every loop so the fewest distinct
PSRAM/flash regions are touched in quick succession** — sequential
single-stream access keeps the cache working for us (MMBasic experience:
cache hits without fetches can beat plain RAM), while interleaving two
streams thrashes it (measured: the kernel MERGE was written two-pass for
this reason; playmp3's working set in the arena was 2× slower *and* put
flecks on the display). Concretely:

- Hot working state (mixer voices, sprite table, modcontext) lives in
  process SRAM. Processes run in the SRAM PROGPOOL, so ordinary statics
  and heap are already on the right side.
- Large read-mostly data (flash-slot images, MOD sample banks, big blit
  buffers) lives in the arena and is read in **sequential runs**, one
  stream at a time, staged through a small SRAM buffer (row at a time).
- Never ping-pong between two arena regions in an inner loop; split into
  passes as the kernel MERGE does.

### The runtime pattern rules (from the pixel-batch work)

- **Every** new graphics entry point calls `mm_pix_drain()` before its
  first ioctl. No exemptions.
- Per-program state goes in header statics (`mmb_gfx_fill.h` model): a
  program that names no sprite pays nothing. There is one header per
  independently usable primitive because cc1's dead-code rule counts
  *names*, not reachability.
- A MODE change invalidates geometry-dependent state: hook sprite/blit
  teardown in next to `mm_fb_forget()` in `mm_mode`.
- Every statement is implemented **twice** — `mmb2c.py` and
  `mmbc/mmbc_stmt.c` — with byte-identical output as the gate, plus the
  `uses_*` flag and the include-emission line (order is load-bearing).

---

## 2. Design decisions

### D1. Blit/sprite pixel work is userland, per-row ioctls first

A 32×32 sprite in MODE 2 is 16 bytes/row; SHOW SAFE ≈ 4 ioctl
passes/row ≈ 190 µs/sprite; 8 sprites ≈ 1.5 ms/frame ≈ 9% of a 60 Hz
frame. Acceptable on paper — **Phase 0 measures it on the board** before
committing. If it disappoints, the fallback is one contingent kernel
ioctl (`GFXIOC_RECTBLIT`: rect read/write with stride), not a redesign.

### D2. Buffers are native-packed, not RGB888

PicoMite blit buffers are 3 bytes/pixel RGB888 regardless of mode
(`Blit.c:48`, `GetMemory(w*h*3)`). We store native packed bits/nibbles
(+ w,h header). For a 16-colour palette the round-trip is lossless, so
no BASIC-visible difference except memory (6×–24× smaller — decisive on
a 320 K process pool). "Don't copy black" (WRITE mode bit 2) becomes
"index 0" (MODE 2) / "bit 0" (MODE 1), which is what the RGB888 test
reduces to in these modes anyway.

### D3. Flash slots are arena-backed pseudo-slots  *(user decision)*

No flash slots exist on Fuzix, but the commands must work unmodified.
A slot table in the runtime maps slot 1..`MM_FLASHSLOTS` (= 3, matching
`MAXFLASHSLOTS`, `configuration.h:318`) to a lazily arena-allocated
block, `0xFF`-filled at allocation (erased-flash semantics, which
`BLIT FLASH`'s validity check and "Already programmed" both rely on).
The allocation happens only when a program first references a slot.
Slot size: 128 K default (`MAX_PROG_SIZE` is 120 K on the reference
rp2040 build), one `#define`.

Surface to mirror (verified in reference):
- `FLASH DISK LOAD n, file$ [,O[VERWRITE]]` (`FileIO.c:1232`) — load a
  file into slot n; error "Already programmed" unless O.
- `FLASH ERASE n` — refill with 0xFF (keep the arena block).
- `MM.INFO(FLASH ADDRESS n)` (`MM_Misc.c:8095`) — the slot base
  address; with lazy allocation on first reference.
- `BLIT FLASH n, dst, x1,y1,x2,y2,w,h [,transparent]` (`Blit.c:678`) —
  slot layout is `uint32 w, uint32 h`, then packed 4bpp.
- The program-management FLASH subcommands (SAVE/LOAD/RUN/CHAIN/LIST)
  are out of scope — they manage BASIC programs, not game data.

Documented divergence: slots live for the process (arena freed on
exit), so a program loads its own slots at startup; since slots always
start erased, the reference's guarded `FLASH DISK LOAD` pattern works
unchanged.

### D4. Sprites are 4bpp internally, exactly as the reference

Even in MODE 1 (`Sprite.c` stores nibbles; `DrawBuffer2Fast` /
`ReadBuffer2Fast` convert: bit set ⇔ nibble non-zero). Sprite images and
background stores are one heap allocation (`2*((w*h+1)>>1)`) — small and
hot, so process SRAM, not arena. The engine (LIFO layer stacks, SAFE
show/hide walk, AABB collisions, edge codes 0xF1/2/4/8, static objects,
`next_x/next_y`, master/copy sharing) is transcribed from `Sprite.c`
with the fb access routed through per-row BLITRD/BLIT + a 160 B SRAM
row staging buffer. Transparent draws are read-modify-write on the
destination rows.

### D5. PLAY SOUND/TONE = a userland synth daemon on the PCM stream

The kernel's BBC synth (`SNDIOC_SOUND`) is square-wave-only with an
envelope model — wrong timbre, wrong volume model. Rejected
("different is worse than missing"). Instead `playsnd`, a daemon on the
playmp3 model, holds the PCM stream at 44100 Hz stereo and runs the
MicroPython synth core verbatim (`snd_sample()`, the 4×2-voice mixer,
`SineTable`/`triangletable`/`mapping` — all portable C, float phase
accumulators, so built `-mfpu` like playmp3; the audio lock doubles as
the FPU lock, which the daemon legitimately holds).

Commands reach it over a well-known FIFO (§4). Latency budget: select
timeout 10 ms + queue target 2048 frames ≈ 46 ms — the queue target must
stay ≥ 2× the worst-case swap-out (~25 ms) and is the tunable if the
board says otherwise. The daemon exits after ~5 s of silence, releasing
the PCM stream (and FPU) for MP3/MOD. First PLAY SOUND in a session pays
a spawn (~100 ms) — documented divergence.

TONE duration uses the reference's whole-cycle rounding, counted in
samples by the daemon. The **tone-done interrupt needs no IPC**: the
issuing program knows the duration, so the runtime records a deadline
and the interrupt scan fires on TIMER — sample-accurate enough and free.

### D6. PLAY MODFILE = `playmod` daemon; MODSAMPLE = a FIFO message

`playmod file.mod [volume] [noloop]` on the same skeleton: hxcmod
(vendored from the MicroPython port, integer-only — **no** `-mfpu`) at
22050 Hz stereo, `hxcmod_setcfg(ctx, 22050, 1, 1)`, chunks of 1024
frames, queue target ≈ 2048 frames (~93 ms — the reference MicroPython
latency is ~140 ms, so we are at parity or better). The .mod file is
loaded whole into the **arena** (samples are read in sequential runs by
the render loop — the QMI-friendly placement; the MicroPython port and
rp2350 PicoMite both play samples from PSRAM); `modcontext` (the
working set) stays in process SRAM.

`PLAY MODSAMPLE s, ch [,vol]` spawns nothing: it is one FIFO record to
the **already-running** playmod, which calls
`hxcmod_playsoundeffect(ctx, s-1, ch-1, vol-1, 3579545/16000)` —
sample rate hard-coded 16000 as the reference does (`Audio.c:2968`).
The sample is one already inside the loaded .mod
(`modctx->sampledata[s-1]`); hxcmod's 4 effect slots mix it into the
ongoing music in the same render loop (`hxcmod.c:1519-1546`), so the
music never pauses and nothing is loaded or allocated at trigger time.
Guard as the reference does: MODSAMPLE errors "Samples play over MOD
file" unless a MOD is playing. Owner-pid alone cannot tell playmod
from playsnd, so the runtime keeps a per-program `mm_play_kind` static
(set by PLAY MODFILE/MP3/SOUND/TONE, invalidated when
`mm_play_owner()` returns 0) and MODSAMPLE requires kind == MOD with a
live owner — the same semantics as the reference's
`CurrentlyPlaying == P_MOD` check.

End-of-mod (only when the BASIC program supplied an interrupt →
`noloop`): `hxcmod_fillbuffer` returns done → daemon drains the ring
(PCMSTAT queued = 0) and exits. The BASIC side detects completion as
`mm_play_owner() == 0` from the interrupt scan — exactly the
"PLAY-done via SNDIOC_PCMOWNER" item already queued in NEXT.md
(Interrupts phase 2), now with its consumer.

`PLAY STOP` works for both daemons **unchanged**: the existing
owner-lookup + SIGINT + SIGKILL path in bcrun; the daemons catch SIGINT
and close cleanly, as playmp3 does. `PLAY VOLUME` while playing becomes
a FIFO record (playmp3 can adopt the same listener later; today it
takes volume only at spawn, unchanged).

---

## 3. Kernel change: GFXIOC_SCROLL2

The one planned kernel change. SPRITE SCROLL needs horizontal scroll and
wrap-around, which `GFXIOC_SCROLL` cannot do, and doing it from userland
means shifting every packed byte of the fb through ioctls — the wrong
side of the boundary.

```c
struct gfx_scroll2 { int16_t dx, dy; int32_t fill; };
/* fill: RGB888 fill colour, or -1 = leave vacated area, -2 = wrap */
#define GFXIOC_SCROLL2 0x0035          /* next free; confirm with ioctlcheck.sh */
```

Operates on the caller's draw target (like everything else). Vertical
component: rotate-rows-by-reversal, one 160 B row temp, three passes —
O(3h) row copies, sequential, single-stream (QMI-friendly). Horizontal:
per-row byte rotation + bit/nibble shift by `dx` within the row temp.
Fill mode paints the vacated band via the existing fill helpers.
Flash-resident code, one 160 B static — inside the SRAM budget.
Run `ioctlcheck.sh`; QMI/clock rules don't apply (no timing change).

The old `GFXIOC_SCROLL` stays (console uses it).

*(Free by-product: MMBasic's plain `SCROLL x,y[,colour]`, previously
noted in COVERAGE as unimplementable, becomes a one-line statement.)*

---

## 4. The player control FIFO

One protocol, one header (`mmb_playctl.h`), used by `mmb_play.h`
(client) and both daemons. **The header exists in both trees (mmb2c and
FUZIX utils) — same sync discipline as `mmb_runtime.c`, and a
`MM_PLAYCTL_VER` byte in every record so a stale copy is an error, not
a silent skew** (the stale-header lesson, twice learned).

```c
#define MM_PLAYCTL_FIFO "/tmp/.playctl"
#define MM_PLAYCTL_VER  1
struct mm_playmsg {                    /* 16 bytes, fixed, atomic on a FIFO */
    uint8_t ver, op, a, b;
    int32_t p1, p2, p3;
};
/* ops: 1 SOUND   a=voice 1-4, b=side bits (L=1,R=2), p1=type,
        p2=freq mHz, p3=vol 0-25
        2 TONE    p1=left mHz, p2=right mHz, p3=duration ms (-1 forever)
        3 MODSAMPLE a=sample 1-32, b=channel 1-4, p1=vol 1-64
        4 VOLUME  p1=left 0-100, p2=right 0-100                       */
```

**Kernel FIFO semantics differ from POSIX and were pinned on the board
(fifotest, Phase 0)**: the opener counts itself a writer *before* the
pipe checks, so an `O_NDELAY` write-open always succeeds — a failed
open can never mean "daemon not running"; any FIFO open *without*
`O_NDELAY` psleeps once regardless of direction, so every open in the
protocol carries `O_NDELAY`; an empty-pipe `O_NDELAY` read returns
**0** (not −1/EAGAIN) and is "quiet", never EOF; and a small write to
a reader-less FIFO buffers silently (EPIPE only past 4K), so write
failure can't signal a dead daemon either.

Therefore: **discovery is `SNDIOC_PCMOWNER`**, which the daemon holds
by construction. Client logic (`mm_play_msg()` in `mmb_play.h`): if
`mm_play_owner()` is live and `mm_play_kind` matches, open the FIFO
`O_WRONLY|O_NDELAY` and write the record; if owner is 0, spawn the
right daemon via the existing `mm_run_*` machinery, poll PCMOWNER
(20 ms × ~100) until it goes live, then write; if owner is live but
the kind is wrong, error "sound output in use" (reference behaviour).
Daemon startup order is load-bearing: unlink stale FIFO (discards any
stale records left buffered on the orphaned inode), mkfifo, open
`O_RDWR|O_NDELAY` (self-as-writer keeps the read end permanently
valid), *then* PCMOPEN (a lost spawn-race gets EBUSY and exits; the
client's poll finds the winner), then the loop: drain FIFO records,
top up the PCM queue to target (PCMSTAT), `usleep(20000)` — playmp3's
proven pacing; no select() needed. Idle-exit policy per §D5/D6.

---

## 5. Phases

Each phase lands alone, is measured on the board, and is committed
before the next starts (one change at a time). "Both translators" is
implied in every phase; so is the byte-identical gate and a tests/*.bas
addition.

### Phase 0 — spikes  ✅ DONE 2026-08-13, all gates passed

Board numbers (blitbench, mode 7): BLITRD/BLIT of a 16 B sprite row
≈ 2.0 µs, of a 160 B full row ≈ 2.25 µs — the syscall dominates and
the payload is nearly free. Projected 8-sprite 32×32 SHOW SAFE frame
= **2.02 ms against the 2.5 ms gate → per-row ioctls stand,
GFXIOC_RECTBLIT is closed, not needed.** GFXIOC_SCROLL (memmove + fill)
= 126 µs — ample SCROLL2 budget. Arena pointer as a BLIT source:
accepted and byte-verified, same cost as an SRAM source. MODE 1
attribute question answered from source: PC3 1bpp drawing is
bits-only by construction (`display_gfx_map` maps non-black → ink,
attributes belong to the text console), so BLIT/SCROLL2 move bits
only — consistent with every existing MODE 1 primitive, no new
divergence. FIFO semantics pinned (see §4). 0x35 confirmed free
(ioctlcheck: 51 codes, no duplicates). Spike tools `blitbench.c` and
`fifotest.c` live in the platform `utils/` as regression
documentation.

Two design refinements out of Phase 0: **playsnd is integer-only**
(20.12 fixed-point phase accumulators; the utils Makefile permits
exactly two FPU programs and the pitch error vs float is ≤ 0.003 Hz —
inaudible; TONE's whole-cycle duration rounding is computed
client-side in the BASIC runtime, which has full doubles, and sent as
a sample count), and the daemons pace with `usleep(20000)` +
PCMSTAT like playmp3 rather than select().

*(original spike list, for the record)*

1. **Per-row blit microbench**: 1000× BLITRD+BLIT of 16-byte rows from
   a C program; derive µs/row. Gate: 8-sprite SHOW SAFE frame ≤ 2.5 ms
   projected. Decides whether `GFXIOC_RECTBLIT` is needed *before* any
   engine code exists.
2. **MODE 1 attribute check**: does kernel mode 0xFF keep a per-tile
   colour plane (PicoMite mode 1 copies `tilefcols/tilebcols` on
   aligned plain BLITs)? Read `display.c`/`console.c`, then a board
   test: coloured text + BLIT a region. Determines whether plain BLIT
   in MODE 1 needs an attribute story or PC3 1bpp is single-colour.
3. **FIFO smoke test**: mknod/select/write/read between two processes
   on the board (the syscalls exist; prove them under load).
4. **Arena → BLIT smoke test**: pass an arena pointer to GFXIOC_BLIT
   (valaddr accepts owned arenas — prove it for the gfx path).
5. `ioctlcheck.sh` — confirm 0x35 free.

### Phase 1 — BLIT core (`mmb_blit.h`)

`BLIT READ/WRITE/CLOSE`, plain `BLIT x1,y1,x2,y2,w,h`,
`BLIT COMPRESSED`, `BLIT MEMORY`. The engine is a userland transcription
of `blit121`/`blit121_self` (`RGB121.c:33-200`) with the nibble order
mirrored, plus the 1bpp bit-granularity equivalent; per-row ioctls with
a 160 B SRAM staging row; overlap-safe direction choice for the
self-blit. 64 buffers, native-packed + w,h, heap-first with arena
fallback above 16 K. WRITE modes 0–7 (mirror ×2, don't-copy-black)
transcribed from `Blit.c:1568-1722` including the mode-0 fast path.
`BLIT MEMORY`'s compressed-vs-raw top-bit sniff (`Blit.c:290`) comes
along for free with COMPRESSED's RLE decoder.

### Phase 2 — flash slots (`mmb_flash.h`) + `BLIT FRAMEBUFFER`  ✅ DONE 2026-08-13

Board-verified (flashpix: slot file written from BASIC, loaded, blitted,
pixel-exact, and N→F→N round trip exact). Slot capacity is 48K, not the
reference's 120K — the host gates' whole VM is 128K, and a full 320×240
sheet is 38.4K; larger files raise "File too big for a flash slot"
(divergence ledger). `MM.INFO(FLASH ADDRESS n)` allocates lazily, so
only slot-using programs pay, as specified. mm_fb_cur() joined the
runtime so the target-switching forms can restore the program's choice.

*(original scope, for the record)*

D3 as specified: slot table, `FLASH DISK LOAD/ERASE`,
`MM.INFO(FLASH ADDRESS n)`, `BLIT FLASH` (arena row → SRAM staging →
BLIT; one stream at a time). `BLIT FRAMEBUFFER src,dst,...` via
FBSEL-switch + row loop, MODE 2 only ("Not available in mode 1",
matching `Blit.c:799`).

### Phase 3 — SPRITE engine (`mmb_sprite.h`), sans SCROLL

The big one. `SPRITE LOAD` (.spr text format, `Sprite.c:887` —
space/hex chars, comment lines, `sprite_color_mode0/1[16]` palettes),
`LOADARRAY`, `SHOW`, `SHOW SAFE`, `HIDE`, `HIDE SAFE`, `HIDE ALL`,
`RESTORE`, `WRITE` (note its *inverted* transparency flag sense),
`SWAP`, `READ`, `COPY`, `CLOSE [ALL]`, `NEXT`, `MOVE`,
`SET TRANSPARENT`, `INTERRUPT/NOINTERRUPT`, `STATIC` objects +
`STINTERRUPT`, and the full `SPRITE(...)` function
(W/H/X/Y/L/C/T/E/V/D/A/N/S/B/ST selectors). Engine internals ported
from `Sprite.c`: `BlitShowBuffer` semantics (mode bits 1/2/4/8), LIFO +
zeroLIFO ordering, `ProcessCollisions` AABB + edge codes +
`lastcollisions` edge-triggering, `SPRITE_POS_INACTIVE 10000`.
Collision interrupts raise a flag the interrupt scan polls (MMBasic
does the same); if interrupt machinery phase 2 isn't landed yet, the
`SPRITE(...)` polling functions ship first and the interrupt wiring is
a follow-on within the phase.

### Phase 4 — kernel SCROLL2 + `SPRITE SCROLL` + `SPRITE MOVE` polish

§3 kernel work, then `SPRITE SCROLL x,y[,col]` transcribed from
`Sprite.c:2081`: bounds ±half-screen, wrap via `fill=-2`, layer-0
sprites and statics move with the background (centre-point wrap, **Y
inverted relative to X** — reference quirk, keep it), layer ≥1 hidden →
scroll → reshow → `ProcessCollisions(0)`.

### Phase 5 — PLAY SOUND / PLAY TONE (`mmb_play.h` + `utils/playsnd.c`)

D5 + §4. Translator: full argument surface (SOUND voice, position
L/R/B/M, types O/S/Q/T/W/P/N, freq, vol 0–25; TONE l,r[,dur[,int]]).
Vendor `sound_tables.h` unchanged. Tone-done deadline interrupt.
Type U (user wavetable) deferred — honest error.

### Phase 6 — PLAY MODFILE / MODSAMPLE (`utils/playmod.c`)

D6. Vendor `hxcmod.c/.h` unchanged from the MicroPython port (it
already carries the seffect extension). MOD-done interrupt via
owner-poll. Board gate includes an effect-trigger latency measurement
and `snd_stat.underruns == 0` over a 60 s play.

### Deferred (recorded, not planned)

`BLIT RESIZE`, `TILE/TILEMAP` (leans on slots — natural sequel to
Phase 2), PLAY SOUND type U, live volume for playmp3,
`GFXIOC_RECTBLIT` (only on Phase 0 evidence).

`SPRITE LOADPNG`, `SPRITE LOADBMP` and `BLIT LOAD`/`LOADBMP` are all
done. The BMP forms decode in `loadimage -s` and come back down a pipe,
so there is no BMP reader in the runtime and none in every generated
program — the same arrangement `SPRITE LOADPNG` uses. `bmptest.sh`
gates the conversion on the host.

---

## 6. Verification

- **Host gates**: grow the host-stub branch of the gfx section into a
  fake framebuffer (malloc'd 38400 B + geometry; BLIT/BLITRD/SCROLL2
  emulated). The entire blit/sprite engine then runs pixel-exact on the
  host; tests compare golden fb dumps. This is what catches the nibble
  -order mirroring.
- **Synth golden files**: the synth core is deterministic for
  sine/tri/square/saw; render N seconds to a file on the host and
  byte-compare against a capture from the MicroPython reference code
  compiled host-side.
- **Board**: every phase gate above, plus **side-by-side with a real
  PicoMite** before any phase is called done — three past divergences
  were only ever caught that way. Acceptance: two or three published
  PicoMite VGA games (sprite-based) run to completion, plus an
  audio-using game or demo for Phases 5–6.
- Docs pass at the end: COVERAGE.md rows (also fix the two stale notes
  found in this survey: "BLIT needs a block pixel-read ioctl" — BLITRD
  shipped; "a synthesiser the kernel does not have" — it has one, we
  chose not to use it), NEXT.md, the two PC3 manuals.

## 7. Divergence ledger (all deliberate, all documented)

| area | divergence |
|---|---|
| blit buffers | native-packed, not RGB888 (lossless for 16 colours; visible only via SPRITE(A)-style address peeking) |
| don't-copy-black | tests native index 0 / bit 0 instead of RGB≠0 — identical result in these modes |
| flash slots | per-process lifetime, arena-backed; load-your-own-slots at startup |
| PLAY SOUND/TONE | first use pays a daemon spawn (~100 ms); effect latency ~50 ms vs near-zero in-interpreter |
| MODSAMPLE | trigger latency ~90 ms (reference MicroPython: ~140 ms; PicoMite: near-zero) |
| BLIT FLASH source | arena, not XIP flash — same bytes, same commands |
| TILEMAP DRAW | the destination is composed a whole row at a time through the blit window, not blit121'd a tile at a time — same pixels, same overwrite order; a tile blit here would be a syscall pair per tile row |
| TILEMAP tileset reads | bounded by the slot: a tile index past the image reads the slot's erased 0xFF (what the reference's flash gives) but never past the allocation |
| TILEMAP mode check | "Requires RGB121 mode" is raised only when there is a screen to be in the wrong mode on; headless the statements run and the drawing is silent |
| FLASH LOAD IMAGE | decoded by loadimage in another process, packed on the way in; a file whose rows do not fit the slot is refused whole where the reference writes on into the next slot |
| deferred list above | honest errors, never silent wrong behaviour |

## 8. Open questions

1. Phase 0.2 — MODE 1 tile-colour attributes: does the PC3 1bpp mode
   have them at all? Shapes plain-BLIT and SCROLL2 fidelity in MODE 1.
2. Sprite/blit heap pressure in big games (64 sprites + buffers + the
   translated program in one 320 K pool): if a real game hits the wall,
   the arena fallback threshold (16 K) drops.
3. Effect latency: if 50–90 ms is audibly wrong in a real game, the
   options are (in order) smaller queue target, smaller select timeout,
   and only then anything kernel-side.
