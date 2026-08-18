# Drawing on the PC3: what the kernel offers, what the runtime does with it, and where the time goes

Written 2026-08-17, from measurements on the board, after PETSCII Robots
ran correctly but far slower than the same program interpreted by
MMBasic on an RP2040.

The headline: **the kernel already provides the transfer primitive we
want, and the runtime already uses it. The cost is not the kernel
boundary — it is the per-pixel work the runtime does in userland
between two crossings.** A kernel crossing costs 2 us; our userland
decode costs 1.35 us *per pixel*.

---

## 1. What the kernel can already do

`pico_ioctl.h`, all on `/dev/sys`, all acting on the caller's current
draw target (`display_fb_target()` — screen, F, or the layer).

### Block transfer — this is the "pass a pointer" primitive

| ioctl | number | direction | shape |
|---|---|---|---|
| `GFXIOC_BLIT`    | 0x0005 | into target | one run of bytes at an offset |
| `GFXIOC_BLITRD`  | 0x0032 | out of target | one run of bytes at an offset |
| `GFXIOC_BLITR`   | 0x0039 | into target | **rectangle**: `rows` spans of `len` bytes, `stride` apart |
| `GFXIOC_BLITRDR` | 0x003A | out of target | **rectangle**, same shape |

```c
struct gfx_blitr {
	uint32_t offset;	/* first row: byte offset into the target */
	uint16_t len;		/* bytes per row                          */
	uint16_t rows;
	uint16_t stride;	/* the target's bytes per row             */
	uint16_t pad;
	void *buf;		/* rows * len bytes, contiguous           */
};
```

These are **native format and nothing else**: 4bpp with the **high
nibble the left pixel**, 1bpp MSB left. The header says so explicitly —
*"The caller still owns the packing rules and the pixel logic, exactly
as it does for the single-row pair — this is a transfer, not a drawing
operation."*

That is already the design of "hand the kernel a pointer and a
position, keep transparency in userland". It exists, it is symmetric,
and both directions are one crossing.

### Drawing primitives the kernel does own

| ioctl | number | what |
|---|---|---|
| `GFXIOC_PIXEL` / `GETPIXEL` | 0x000F / 0x0011 | one pixel, RGB888 |
| `GFXIOC_PIXELS` / `RECTS`   | 0x0014 / 0x0015 | a batch of points / rectangles, up to `GFX_BATCH_MAX` 1024 |
| `GFXIOC_RECT`               | 0x0012 | one filled rectangle |
| `GFXIOC_BITMAP`             | 0x0013 | a **1bpp** source, scaled, fg/bg, bg −1 transparent — this is how text is drawn |
| `GFXIOC_TEXT`               | 0x001A | a run of characters in a built-in font |
| `GFXIOC_MERGE`              | 0x0034 | key the layer over F by colour index |
| `GFXIOC_FBCOPY2`            | 0x0033 | whole-buffer copy between screen/F/layer |
| `GFXIOC_SCROLL` / `SCROLL2` | 0x001B / 0x0035 | hardware-assisted scrolling |
| `GFXIOC_FBOPEN` / `FBSEL`   | 0x0018 / 0x0016 | claim / select F and the layer |
| `GFXIOC_INFO`               | 0x000E | width, height, stride, bpp, mode |

**There is no 4bpp blit-with-transparency ioctl, and by the argument
above there should not be one** — transparency is pixel logic and
belongs on the caller's side of the boundary.

`GFXIOC_BITMAP` is the closest thing, and it is 1bpp only: it is a
*glyph* painter, not a sprite painter.

### RAM residency

The kernel now executes from flash by default, with
`linker_overrides/default_text_excludes.incl` naming what may go there.
**`display.c` as a whole stays in RAM** and only its genuinely cold
functions are excluded (palette, mode setup, init). The note in that
file spells out why `MERGE` is deliberately *not* excluded: both its
sources are in PSRAM, PSRAM and flash share the QMI and the XIP cache,
so running the loop from flash would put instruction fetches in direct
contention with the data being streamed.

**Anything new added to `display.c` is RAM-resident unless it is
explicitly listed.** Nothing needs doing to get that.

---

## 2. What the runtime does today

### `BLIT MEMORY addr, x, y [,t]` and `SPRITE MEMORY` (identical path)

`mmb2c` emits `mmb_blit_mem(addr, x, y, blank)`. In `mmb_blit.h`:

1. Read `uint16 w, h` from `addr`; bit 15 of either means run-length
   coded. Pixel data starts at `addr + 4`. This is MMBasic's format
   and is byte-identical to it — **low nibble = left pixel**, opposite
   to the framebuffer's packing.
2. `mmb_geom()` — one `GFXIOC_INFO` crossing per blit for stride/bpp,
   plus `mm_hres`/`mm_vres`.
3. `mmb_win_open()` over the destination rectangle *(added today)*.
4. For each row of the sprite:
   - `mmb_row_get()` — fetch the destination row and **unpack it into
     one byte per pixel** in `mmb_rowpx`. Inside an open window the
     fetch is served from a batch already in SRAM; the unpack still
     happens per pixel.
   - the decode loop — call `next(0)` **through a function pointer**
     (`mmb_unc` or `mmb_rle`) for every pixel, compare against the
     transparent index, store one byte into `mmb_rowpx`.
   - `mmb_row_put()` — **repack** `mmb_rowpx` back into nibbles, a
     read-modify-write per pixel, into the batch.
5. `mmb_win_close()` — one `GFXIOC_BLITR` for the whole rectangle.

So per pixel: an indirect call, an unpack, a compare, a repack. **Three
passes over the pixel and one function call**, all compiled by `cc2` —
our own small C compiler, not GCC.

The window (`mmb_win_open`/`mmb_win_bytes`/`mmb_win_flush`, MMB_WINB =
1024 bytes) turns the per-row crossings into one per rectangle. A 24×24
tile is 12 packed bytes a row, so a whole tile fits one batch: **2
crossings instead of 48.** `mmb_sprite.h` has used this since it was
written; `mmb_blit_decode` was given it today.

### Everything else

- `SPRITE SHOW` (`mmb_sprite.h`) saves the background with the same
  window mechanism, then draws — already batched.
- Text goes to `GFXIOC_TEXT` / `GFXIOC_BITMAP`: the kernel paints it.
- Lines, circles, polygons, arcs: geometry computed in userland, pixels
  handed over in `GFXIOC_PIXELS` / `RECTS` batches.
- `FRAMEBUFFER MERGE` is entirely kernel-side.

---

## 3. Measurements

Board, 375 MHz, BASIC `MODE 2` — which is kernel mode 7, 320×240 4bpp,
stride 160 (see §5) — drawing into F, which is in PSRAM. `Timer` is
milliseconds.

| what | total | each |
|---|---|---|
| 77 tile blits, 24×24 (44,352 px) | 64 ms | 0.83 ms |
| **5 blits, 96×96 (46,080 px)** | **62 ms** | 12.4 ms |
| 10 layer `CLS` | 23 ms | 2.3 ms |
| 28 `SPRITE MEMORY` 24×24 | 24 ms | 0.86 ms |
| 10 `FRAMEBUFFER MERGE` | 169 ms | 16.9 ms |
| 10 `MERGE ,B` | 167 ms | 16.7 ms |
| 2000 characters | 55 ms | 0.028 ms |
| 2000 `PIXEL()` reads | 5 ms | 0.0025 ms |
| 2000 empty loop iterations | 1 ms | — |

### What these say

**The crossing is cheap.** 2000 `PIXEL()` reads minus the loop is 4 ms
— **2 us per crossing**, and that is a round trip that also returns a
converted RGB888 value.

**The cost is per pixel, not per call.** 46,080 pixels in *five* blits
cost the same 62 ms as 44,352 pixels in *seventy-seven*. Fifteen times
fewer crossings, no change. Removing the 3,696 per-row crossings that
the blitter used to make was worth about 7 ms of the 64 — real, worth
keeping, and nowhere near the problem.

**Userland pixel work is ~6× more expensive than the same work in the
kernel:**

- our decode: 62 ms / 46,080 px = **1.35 us/px** (~500 cycles at 375 MHz)
- kernel `MERGE`: 16.9 ms / (38,400 bytes × 2 passes) = **0.22 us/byte**,
  and a byte is two pixels
- kernel glyph: 27.5 us per character including its crossing

A movement step in Robots is 77 tiles + a layer `CLS` + up to 28
sprites + one merge ≈ **107 ms, about 9 fps**.

---

## 4. Where the 500 cycles per pixel go

Nothing here is memory type. MMBasic on the RP2040 reads its tiles from
flash and streams its MOD from flash — the same class of external,
QMI-reached memory as our PSRAM. What differs is the work per pixel:

1. **An indirect call per pixel.** `next(0)` is a function pointer, so
   no inlining and no register allocation across it.
2. **Unpack and repack.** The destination row is expanded to one byte
   per pixel and then squeezed back, so every pixel is touched three
   times when it could be touched once — and the source and destination
   are *both* already nibble-packed, two pixels to a byte.
3. **`cc2` code quality.** These loops are compiled into the program by
   our own compiler, which does little register allocation. MMBasic's
   equivalent is GCC ‑O2 firmware.

The fix follows from (2) and (1) and needs **no kernel change**: work
nibble-to-nibble, a byte (two pixels) at a time, in loops specialised
for the uncompressed and RLE cases instead of a function pointer. The
fully opaque, byte-aligned case degenerates to `memcpy` of 12 bytes a
row. That is where the 6× lives.

---

## 5. Mode numbering, and one anomaly

### BASIC MODE 2 is kernel mode 7 — the mapping is already right

Two numbering schemes meet here and `mm_mode()` translates between
them, deliberately:

| BASIC `MODE` | kernel mode | geometry |
|---|---|---|
| 1 | 0xFF (the console) | 640×480 1bpp |
| 2 | **7** | **320×240 4bpp, stride 160** |

The kernel's own 0–5 are the BBC modes on a 1024×768 raster (mode 2
there is 160×256), and its 320×240 16-colour screen is mode 7. A BASIC
program never sees that: `MODE 2` means what it means on a PicoMite, and
`MODE 1` returns to the console where the prompt is. `robots.bas` saying
`Mode 2` is correct.

So the measurements above were taken at **320×240, stride 160, 38,400
bytes** — nothing in the test was clipped.

### MERGE measured 16.9 ms — and 16.7 ms of that was the blanking wait

**CORRECTED 2026-08-18. There is no mystery here; do not go hunting
one.** What follows is what this section used to claim, and why it was
wrong, because the wrong version is the more believable one.

The claim was: `display_fb_merge`'s own comment records 0.726 ms to copy
153,600 bytes, i.e. 53 MB/s; a merge at 320×240 moves ~76,800 bytes
across its two passes, so it should cost ~1.4 ms — yet it measured
**16.9 ms**, about 4.5 MB/s, so something in the path must not behave as
the comment describes.

**The 16.9 ms was one frame at 60 Hz (16.67 ms), not a slow copy.** When
that measurement was taken, `display_fb_merge` began by spinning for the
top of vertical blanking, *inside the syscall*. The number timed the
wait; the copy was hidden underneath it. The wait moved out to the
caller the following day — `GFXIOC_VSYNCTRY`, bounded slices, driven
from `mm_fb_merge_hw` — for an unrelated reason: a non-preempting
kernel holding the CPU for a whole frame drained the MOD player's queue
and was audible. `display.c` now carries the "NO WAIT HERE ANY MORE"
note recording it.

Two consequences:

* **The copy cost is now measurable clean, and has not been measured.**
  One board run says whether it is the ~1.4 ms the comment predicts.
  Until someone runs it, neither number should be quoted.
* **A frame still costs up to 16.7 ms of waiting** — that is a 60 Hz
  display and it is supposed to be there. What changed is that the CPU
  is yielded during it rather than held, so the wait no longer costs
  *everything else on the machine* what it used to.

The general lesson is one this project keeps re-learning: a measurement
that brackets a hardware wait times the wait. Check a suspicious number
against the frame period, the tick and the timeslice before believing it
means what you would like it to mean.

---

## 6. What to do, in order

1. **Rewrite the blit inner loop nibble-to-nibble** in `mmb_blit.h`,
   specialised per source encoding, no function pointer, no
   unpack/repack. Expected ~6× on all tile and sprite drawing. No
   kernel change; the transfer primitives are already right.
2. ~~**Investigate `MERGE`** against the 53 MB/s its own note claims.~~
   **WITHDRAWN** — the 16.9 ms was the in-kernel blanking wait, which
   has since moved to the caller. See §5. What is left is a
   confirmation, not an investigation: time a merge now the wait is out
   and check it against the predicted ~1.4 ms.
3. Re-time a movement step and compare against the RP2040.

The audio pulse is being tracked separately: it was identical in a
bytecode build 3× slower than the native one, so it does not track CPU
load, and the working hypothesis is QMI contention between the graphics
streams and playmod's samples in a PSRAM arena.
