# FRAMEBUFFER LAYER and FRAMEBUFFER MERGE

A decision and its reasoning, not a plan of work. Nothing here is
built yet.

## What MMBasic does, twice

MMBasic implements transparent overlays **two different ways**, in two
different builds, and the difference is where the compositing happens.

### LAYER — in the display driver (VGA and HDMI builds)

`graphics/HDMI.c`, inside the scanline builder. There are three
buffers, tested per pixel in order:

```c
l = LayerBuf[pp + i];
d = DisplayBuf[pp + i];
s = SecondLayer[pp + i];
if ((s & 0xf) != transparents)      *p++ = map16quads[s & 0xf];
else if ((l & 0xf) != transparent)  *p++ = map16quads[l & 0xf];
else                                *p++ = map16quads[d & 0xf];
```

`SecondLayer` wins, else `LayerBuf`, else `DisplayBuf`; each overlay
has its own transparent index (`transparents`, `transparent`). Nothing
is ever copied — the composite is recomputed for every pixel of every
frame, continuously, for free as far as the program is concerned.

`FRAMEBUFFER LAYER` (`FrameBuffer.c`) just allocates it:

```c
LayerBuf = GetMemory(HRes * VRes / 2);
```

The cost is that **every buffer must be readable at scanout rate**.

### MERGE — in the application (TFT builds: ILI9341 and friends)

`FrameBuffer.c`, `merge()` and `merge_scanline()`. Two off-screen
buffers are combined into the physical display when the command is
issued:

```c
uint8_t top = src_pixel & 0xF0, bottom = src_pixel & 0x0F;
if (top == highcolour && bottom == colour) continue;   /* whole byte clear */
uint8_t new_top    = (top    != highcolour) ? top    : (dst[x] & 0xF0);
uint8_t new_bottom = (bottom != colour)     ? bottom : (dst[x] & 0x0F);
dst[x] = new_top | new_bottom;
```

Keyed per **nibble** on packed 4bpp bytes, with an early-out when a
whole byte is transparent. Eight scanlines are accumulated in a stack
buffer and pushed to the panel in one call. `FRAMEBUFFER MERGE
[colour]` takes the transparent index, 0 to 15, defaulting to 0.

Because it runs once on demand, the source buffers can live anywhere.

## Why this port cannot simply take the driver version

Three reasons, in order of weight.

**1. There is no SRAM for it.** `DISP_FB_POOL` is 40,960 bytes. A
driver-side layer has to be in SRAM, because core1 DMAs the scanout
from it line by line. The kernel has ~8.6K of RAM free, so the layer
would come off the 340K process pool, leaving about 300K — **12% of
every program's memory, permanently, for a feature most programs never
use.** It cannot be made conditional either: the pool is carved at link
time by `linker_overrides/memory_ram.incl` and `TOTALMEM`, long before
any program says `FRAMEBUFFER LAYER`.

**2. It would slow the one path that must never be late.** The
`EXP_4BPP_X2` expander's inner loop is a single 32-bit load from
`gfx_lut[byte]` and a single store, covering two pixels at once:

```c
*(uint32_t *)p = *(const uint32_t *)gfx_lut[s[t]];
```

Transparent keying is per nibble, so that byte-wide lookup stops
working in the general case. MMBasic's own early-out — one compare for
"both nibbles transparent" — would keep an *empty* layer nearly free,
and is the right mitigation if this is ever built. But it is still
extra work per byte on core1's deadline, and a miss there is a visible
glitch, not a slow frame.

**3. The port already made this decision once.** From the comment on
`disp_fb2` in `display.c`:

> Drawing into PSRAM is a QMI transaction through a write-back XIP
> cache, so it is slower than SRAM - which is exactly why MMBasic's
> model is draw-then-COPY rather than scanning out from the layer.
> Scanout must stay on disp_fb: core1 DMAs from it line by line.

The `F` buffer is in PSRAM precisely because core1 cannot read it. A
layer is the same object with the same constraint.

## The decision

**Implement the MERGE model. Use `FRAMEBUFFER LAYER` as the command
that creates the second off-screen buffer, and `FRAMEBUFFER MERGE` as
the composite.**

This is not a compromise invented for this port — **it is the other
MMBasic build**. A program written for a PicoMite driving an ILI9341
runs unchanged. Only the *moment* of compositing differs, and MMBasic
itself defines both moments.

It also fits what is already here: `COPY F,N` is a 38,400-byte
PSRAM->SRAM copy measured at **0.726 ms** (53 MB/s bulk, not the 12
MB/s that scattered access suggests). A merge reads two PSRAM buffers
instead of one and composites on the way, so 2-3 ms against a 16.7 ms
frame is the expectation — **to be measured, not assumed.** The last
prediction about PSRAM here was out by 4.4x, in our favour, which is
the reason for saying so.

## Sketch, if it is built

* a second PSRAM buffer beside `disp_fb2` — another 40K of PSRAM and
  of swap, irrelevant against 8 MB
* a merge routine in `display.c`, taking the transparent index;
  MMBasic's per-nibble rule and its whole-byte early-out, copied
* `GFXIOC_MERGE` carrying the transparent colour
* `display_fb_select` learning a third target (N, F, L)
* `FRAMEBUFFER LAYER` / `WRITE L` / `MERGE [c]` / `CLOSE L` through the
  runtime, `mmb2c.py` and the `mmbc` mirror, byte-identical

Comparable in size to the original FRAMEBUFFER work.

## What would justify revisiting the driver version

**A mouse pointer.** `graphics/Pointer.c` is a caller of this
machinery in MMBasic, and a cursor over graphics is the one case where
compositing genuinely has to be continuous: you cannot merge the whole
screen on every mouse movement. If pointer support is ever wanted,
that is the moment to spend the 40K deliberately and to measure the
expander with the whole-byte early-out in place - rather than now, on
speculation.

Note also that MMBasic's HDMI build supports *two* overlays. Nothing
here needs a second one, and it would double the cost.
