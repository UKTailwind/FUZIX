# Playing an MP3 on the Pico Computer 3

A plan for `playmp3`, a stand-alone program that decodes an MP3 file and
plays it through the PCM5102 at a requested volume, on the model already
proven by `saveimage` and `loadimage`: a whole operation is a program,
not runtime, so a BASIC program that never plays a note pays nothing for
it.

Two thirds of this already exist. The kernel drives the I2S DAC and has
done since the BBC sound system went in; the decoder is `dr_mp3`, which
both the MicroPython firmware and MMBasic already use on this hardware.
What is missing is the join between them: a way for a user process to
hand decoded PCM to the kernel's I2S engine.

## The output stage: follow MicroPython, not MMBasic

MMBasic and MicroPython solve the same problem differently, and the
difference matters here.

MMBasic keeps two "swing" buffers of `WAV_BUFFER_SIZE` (8192 bytes) and
refills the idle one from the interpreter's main loop, in
`checkWAVinput()`, when it notices `swingbuf != nextbuf`. That works, but
it is the interpreter's own loop doing the feeding, and it costs a full
extra pass over every buffer for volume.

MicroPython's `machine_i2s.c` uses chained DMA: two channels, each
mapped to one half of a small DMA buffer, chained to each other so that
when one finishes the other starts with no software in the path at all.
The completion IRQ does one thing - `feed_dma()` copies the next block
out of a ring buffer into the half that just drained, or `memset`s
silence if the ring has run dry. No handshake, no per-sample interrupt
work, and the ring can be refilled by anyone, at any rate, from anywhere.

**This is already how `sound.c` works.** Two chained DMA channels
ping-ponging 256-sample halves of `sndbuf`, `DMA_IRQ_0` on core0, and
`snd_fill()` refilling the freed half. What MicroPython calls `feed_dma`
we call `snd_fill`; the only difference is that ours synthesises the BBC
sound channels instead of copying from a ring.

So streaming MP3 is not new machinery. It is a second mode for
`snd_fill`.

## The kernel side

Four ioctls beside the existing `SNDIOC_SOUND` / `ENV` / `QUIET`:

- `SNDIOC_PCMOPEN` - rate and channels. Takes the state machine from the
  BBC synth and recomputes the PIO clock divider for the file's rate.
- `SNDIOC_PCMWRITE` - a block of 16-bit frames, copied into the ring.
- `SNDIOC_PCMSPACE` - free room, so the player can decode ahead without
  blocking on a full ring.
- `SNDIOC_PCMCLOSE` - drain and hand the state machine back.

`snd_fill()` gains one branch. In stream mode it copies `SND_NBUF`
frames out of the ring; on underrun it emits silence **and counts it**,
because an underrun that is only audible is an underrun nobody can
measure. `SNDIOC_PCMSPACE` reports the count, so the buffering can be
tuned against a number rather than against an opinion.

Three details worth taking from `machine_i2s.c` verbatim:

**Mono is handled in the IRQ.** `feed_dma` duplicates each sample into
left and right when the format is mono. Doing it here rather than in the
decoder means a mono MP3 costs the decoder nothing and halves the ring
traffic.

**The rate comes from the file.** MicroPython computes
`clkdiv = clock_get_hz(clk_sys) / (rate * 2 * bits * 2)`. `sound.c` has
the same arithmetic already, hardcoded as `SND_RATE * 64` - two samples
per frame, sixteen bits, two PIO instructions per bit. `SNDIOC_PCMOPEN`
recomputes it. The BBC synth's note tables assume 22050, so the synth
and a stream are mutually exclusive; that is what MMBasic does too.

**Underrun emits silence, it does not stall.** A stalled DMA chain is
much harder to recover than a gap in the sound.

One thing **not** to copy: MicroPython pops its ring a byte at a time
through `ringbuf_pop`, which for our 256-sample halves would be 1024
iterations inside the IRQ. Two `memcpy`s over the contiguous spans
either side of the wrap do the same work for a fraction of the cycles.

### Where the ring lives

In PSRAM, through the arena facility (`PSRAMIOC_ALLOC`); the DMA keeps
reading the small SRAM half-buffers exactly as it does now. The IRQ copy
is 1KB per 11ms, which is nothing, and it sidesteps every question about
XIP cache coherency and QMI contention between DMA, the flash-resident
kernel and PSRAM.

Sizing. At 44100 stereo 16-bit the stream is 176.4 KB/s. MicroPython's
`pcaudio` uses an ibuf of 16384 - about 93ms - and MMBasic's two 8K
buffers come to the same figure. Neither has to survive being swapped
out. A Fuzix process does: the swap period is around 25ms, and an SD
read has to happen on top of that. **256KB, about 1.5 seconds**, out of
8MB of PSRAM. It costs nothing and it removes a whole class of
intermittent glitch from the picture.

## The decoder

`dr_mp3`, configured as MMBasic configures it - `DR_MP3_ONLY_MP3`,
`DR_MP3_NO_SIMD`, `DR_MP3_NO_STDIO`, `DRMP3_DATA_CHUNK_SIZE 32768` - and
**16-bit output**, not float: `DR_MP3_FLOAT_OUTPUT` is commented out in
MMBasic's `Audio.c` and should stay that way here.

It needs no libm. minimp3 computes its exponent scaling from a small
table (`g_expfrac`) rather than calling `expf`, so there is no
interaction with the kernel's shared libm table at all. `stdlib.h`,
`string.h`, `limits.h` and `assert.h` are all present in
`Library/include`, and `stdint.h` comes from the compiler.

Memory: the `drmp3` struct is about 16KB - `pcmFrames` alone is
`sizeof(float) * DRMP3_MAX_SAMPLES_PER_FRAME` = 9216 bytes, and
`drmp3dec` carries `mdct_overlap` and `qmf_state` - plus the 32KB input
chunk. Both go in a PSRAM arena, the third client after `cc2` and
`mmbc`.

Volume is MicroPython's, not MMBasic's. MMBasic's `i2sconvert` is
`sample * mapping[vol] / 2000`: a multiply **and a divide** per sample.
MicroPython's `audio_scale` is `(sample * gain) >> 8` on an 8.8
fixed-point gain, with the perceptual taper computed once in
`_compute_gain` (`256 * 10 ** ((v - 100) / 40)`, a roughly 50dB log
taper). Same curve, a shift instead of a divide, and this is the
per-sample path so it is the one that matters.

## The thing that decides everything: floating point

minimp3's inner loops are single-precision float throughout. Userland is
built `-mcpu=cortex-m33` with the **soft-float ABI** and no `-mfpu`
(`Target/rules.armm0`), so today every one of those operations is a
libgcc call. The kernel grants only **CP4** - the double-precision DCP -
in `main.c`; CP10/CP11 are never touched, so the M33's single-precision
FPU is switched off.

MicroPython and MMBasic both get real-time MP3 on this exact chip
because their SDK builds have the FPU enabled. We do not, yet.

So **the first thing built is not the player, it is a measurement**:
`utils/mp3bench.c`, decode-only, no audio, reporting decoded frames per
second against wall clock. 44100 stereo needs 38.3 MP3 frames per second
sustained (1152 samples each). That single number picks the route.

### Measured on the board, 2026-08-05

`whiter.mp3` - MPEG1 layer 3, 44100 Hz, stereo, 128 kbps - ten seconds
of audio, decode only, decoder state in the PSRAM arena:

| build | decode time | ratio | MP3 frames/s |
|---|---|---|---|
| soft float, `-Os` | 16.67 s | **0.59x** | 22 |
| soft float, `-O2` | 16.52 s | **0.60x** | 23 |
| **FPU**, `-Os` | **3.33 s** | **3.00x** | **115** |

**Soft float is not real time and optimisation does not rescue it.**
`-O2` bought one percent, because the time is not in code the optimiser
can improve - it is in libgcc's soft-float calls, and there are just as
many of them either way. 38.3 frames a second are needed; 23 arrive.

**With the FPU it is 5x faster and comfortable.** A second file with a
178KB ID3 tag (cover art, skipped correctly) gave 2.85x at 109 frames/s
over fifteen seconds, so the margin is not an artefact of one track.
Three times real time is the headroom the player needs, because the
same process will also be reading the SD card, copying into the ring
and competing with the display.

The kernel change that enables it is done - see `main.c`, beside the
CP4 grant - and the analysis below is what it implements.

### The bug that had to be cleared first

Before any of that could be measured, `drmp3_init` refused every file
with "not a valid MP3 file". It was not the file, the SD transfer, or
the decoder: **`Library/include/limits.h` defined `INT_MAX` as 32767 for
every target**, including 32-bit armm0. dr_mp3 rejects a stream whose
buffered size exceeds `INT_MAX`, and reads in 32768-byte chunks, so the
first chunk was one byte over a limit that should have been 2147483647
and a 4MB file was declared too big before a frame was decoded.

Fixed there, target-awarely, from gcc's own `__INT_MAX__` (`UINT_MAX`
had the same fault). **That fix reaches every userland program on this
machine**, so anything that behaved oddly around 32K boundaries is worth
re-testing.

Worth knowing for the next one of these: the sequence that found it was
to prove the layers from the bottom up on the board itself - the file's
own header bytes, a 32K read compared against the same range read 512
bytes at a time, then minimp3's `drmp3dec_decode_frame` called directly
on that buffer. All three passed while dr_mp3 above them failed, which
put the fault in the wrapper, and dumping the `drmp3` struct after the
failure (`atEnd 1, dataSize 32768, cap 32768, consumed 0`, no frame ever
decoded) matched exactly one line of dr_mp3 - the `INT_MAX` test.

### If the FPU has to be turned on

Enable CP10/CP11 and build **this one program** with
`-mfloat-abi=softfp -mfpu=fpv5-sp-d16`. `softfp` keeps the soft-float
calling convention, so the binary still links against the existing libc.

**No FP context save is needed**, and this is worth being precise about
because it looks like it should be:

- The kernel does not use the FPU. `pico_set_float_implementation(fuzix
  none)` traps every single-precision symbol; the only two mentions of
  `float` in the platform sources are comments. The proof is stronger
  than the grep: CP10/CP11 are disabled right now and the machine runs,
  so no executed kernel path issues an FPU instruction.
- `S0`-`S15` and `FPSCR` are stacked by hardware on exception entry, and
  `S16`-`S31` are callee-saved under AAPCS, so compiled code preserves
  them. With one FP process, nothing else executes an FP instruction at
  all, and its register state survives a context switch untouched.

The cost is not registers, it is **the exception frame**. The first FP
instruction sets `CONTROL.FPCA`, and from then on exception entry pushes
an extended frame - 104 bytes rather than 32, with lazy stacking
reserving the space whether or not it fills it. That lands on the user
stack at every syscall and every interrupt, and on anything in
`tricks.S` or the syscall entry that assumes a 32-byte frame. Against
`USERSTACK_MAX` and the 8K default stack, that is what needs checking.

Cleanest configuration: **clear `FPCCR.ASPEN` and `FPCCR.LSPEN`**.
Automatic FP state preservation off entirely, frame stays 32 bytes,
nothing stacked anywhere. Exactly safe under the one-FP-process
assumption, and it puts the decision in a hardware register where
someone can find it, rather than leaving it as an accident.

### What holds the assumption up

There is one I2S engine, so `SNDIOC_PCMOPEN` is exclusive whether we
like it or not. **The audio device lock is the FPU lock**, for free.

The danger is that this is *build discipline*, not a code invariant:
nothing stops a future `utils/` program being compiled with `-mfpu`. The
failure mode is silent - a second FP user corrupts the first's decoder
state and produces garbage audio, not a crash - so:

1. Exactly one Makefile rule ever adds the FP flags, and the program it
   builds is the one that holds the PCM stream open. Written here
   because that is not visible from the code.
2. Optionally, grant CP10/CP11 **per process** - one conditional `CPACR`
   store on context switch, keyed off a flag set when the process opens
   the stream. Far cheaper than saving 32 registers, and a second FP
   user then takes a UsageFault instead of quietly corrupting the first.
   Worth adding only if the switch cost measures as invisible.

## The program

	playmp3 file.mp3 [volume]

Volume 0-100, defaulting to something sensible. Built like every other
`utils/` program: cross-compiled `armm0`, stripped into `hwtest/`,
installed by `mkccimage.sh`.

Because it is a separate process, playback continues while a BASIC
program runs on - there is no idle-loop refill, no `checkWAVinput`
equivalent anywhere in the interpreter. That is what the deep ring is
really buying, and it is something MMBasic cannot do. The BASIC side
forks `playmp3` and keeps the pid; stopping it is a signal.

## What was actually built, and the two surprises

Everything below shipped, but two assumptions in this plan turned out to
be wrong on the board and are worth recording because both cost a lot of
time.

**The decoder does NOT go in a PSRAM arena.** This plan says it does, on
the grounds that ~65K would not fit a process. It fits, and the arena
was expensive twice over: the same decode runs at **2.91x real time out
of the arena and 5.57x out of process memory** - later 6.14x - because
every touch of 32K of hot state went over the QMI instead of hitting
SRAM. Worse, flash and PSRAM share that controller, so the traffic
starved anything fetching from flash and it was what put flecks on the
DISPLAY. Taking the decoder out of the arena fixed the picture outright.
The rule: bulk data touched once belongs in the arena, a working set
does not. See PC3-SCANOUT-CONTENTION notes in display.c.

**MMBasic's volume law was not copied; MicroPython's was.** MMBasic's
`i2sconvert` is `sample * mapping[vol] / 2000` - a multiply and a
DIVIDE per sample. MicroPython's is `(sample * gain) >> 8` on an 8.8
gain with the taper precomputed. Same curve, a shift instead of a
divide, on the hottest path there is.

The BASIC statements are `PLAY MP3 f$` and `PLAY VOLUME n` (0-100,
clamped, remembered between statements). `PLAY MP3` does not wait: it
calls the new `mm_run_bg()`, which forks and execs without waiting, so
the program carries on while the music plays. `PLAY VOLUME` emits a
`static int mm_play_volume` into the program's prologue, and only when
the program plays something - the mmb_gfx.h/mmb_gpio.h bargain.

Both translators agree byte for byte (`mmbc/cgate.sh`, 0 diff lines),
and the whole path was run on the board: BASIC -> mmbc -> cc -> bcrun,
music playing while the program counted.

## Order of work

1. **`mp3bench`** - decode only, no audio at all. Answers the float
   question before anything is built on top of it.
2. Whichever float route that picks: nothing, or CPACR + FPCCR + the
   exception-frame check.
3. **Kernel PCM sink** - `snd_fill`'s second mode and the four ioctls.
   Proven with a program that streams a raw PCM file, with no decoder
   anywhere near it.
4. **`playmp3`** - the decoder, on a sink that is already known good.
5. The BASIC statement in `mmbc`, and `playmp3` added to
   `mkccimage.sh` **and** `verifyimage.sh`. Both. That pair is what
   `mmb_gpio.h` taught us.

Verification is on the real speaker, and the underrun counter is the
objective check - not "it sounded fine".
