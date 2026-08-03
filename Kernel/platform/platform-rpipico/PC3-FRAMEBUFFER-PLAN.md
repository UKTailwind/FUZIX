# FRAMEBUFFER: the plan

Written 2026-08-03, straight after v0.6, as the starting point for the
next piece of work. Nothing here is built yet beyond what section 1
describes.

## 1. What already exists

The kernel half is done and on the card:

* `disp_fb[DISP_FB_POOL]` — the live screen. core1 DMAs scanlines out
  of it, so **scanout is not switchable**; it always reads `disp_fb`.
* `disp_fb2[DISP_FB_POOL] __uninitialized_psram("fb2")` in `display.c`
  — one off-screen layer, placed in the PSRAM window by the linker.
* `gfx_draw`, a pointer the drawing primitives write through
  (`display.c` ~920–1180). `display_fb_select(0|1)` points it at the
  screen or the layer; `display_fb_selected()` reports it.
* `display_fb_copy()` — one `memcpy` of `display_gfx_fbsize()` bytes,
  layer to screen.
* `GFXIOC_FBSEL` (0x0016) and `GFXIOC_FBCOPY` (0x0017) in
  `pico_ioctl.h` expose those two.
* `DISP_FB_POOL` is 40960 — 320×256 at 4bpp, the largest mode the pool
  serves. MODE 2 (320×240 at 4bpp) is 38400.

What does **not** exist: any BASIC-level `FRAMEBUFFER` command in
`mmb2c`/`mmbc`, a second off-screen buffer, `COPY` with a choice of
source and destination, freeing, or `MERGE`.

## 2. What MMBasic actually does — the reference

From `Draw.c:cmd_framebuffer` in PicoMiteV6.00.02. Trust this over
anything invented here.

    FRAMEBUFFER CREATE          FrameBuf = GetMemory(HRes*VRes/2)
    FRAMEBUFFER LAYER           LayerBuf = GetMemory(HRes*VRes/2)
    FRAMEBUFFER WRITE N|L|F     point WriteBuf at screen / layer / frame
    FRAMEBUFFER COPY s, d [,B]  s and d each one of N, L, F
    FRAMEBUFFER CLOSE F|L       FreeMemory, and fall back to the screen
                                if the closed one was being written to
    FRAMEBUFFER MERGE [c[,...]] layer onto the display, colour c
                                treated as transparent
    FRAMEBUFFER SYNC / WAIT     (PicoMite display-panel specific)

Points worth keeping:

* **Two buffers, not one.** F and L are separate and both optional.
  Ours has a single hard-wired layer.
* **They are ordinary heap allocations** — `GetMemory`/`FreeMemory` —
  created on demand and freed on `CLOSE`. A program that never says
  `FRAMEBUFFER CREATE` pays nothing.
* `CREATE` on an existing buffer is an error ("Framebuffer already
  exists"), not a silent no-op.
* `WRITE F` with no `FrameBuf` is an error, so the failure is named
  rather than drawing into nowhere.
* `CLOSE` restores the write target if it was pointing at what is
  being closed.

`MERGE`'s transparent-colour blit is the one genuinely new operation;
everything else is selection and copying.

## 3. The design question this raises

`disp_fb2` is placed by the linker, which was right when it was one
fixed layer but is wrong for MMBasic's model:

* it costs 40 KB of PSRAM whether or not any program wants it (and
  `psram_static_len()` makes the heap start above it);
* there is exactly one, so `LAYER` and `CREATE` cannot both exist;
* it cannot be freed.

Since v0.6 the kernel has a real PSRAM heap (`arena.c`, newlib
`malloc`/`free` over an `_sbrk` that walks the PSRAM window), and
processes already own arena allocations that are released on exit
(`pagemap_free` → `arena_release`). **So the layer should be an
ordinary arena allocation owned by the calling process**: created on
demand, freed on `CLOSE` or on exit, and two of them possible.

That retires `__uninitialized_psram("fb2")` and the `psram_static_len()`
reservation with it.

## 4. Measure before building

The XIP cache is 16 KB and a MODE 2 buffer is 38,400 bytes, so a
full-screen draw-then-copy **will not** behave like the eclipse's 3 KB
of arrays, which fit the cache and cost nothing measurable.

Before committing to a design, measure on the board:

1. a full-screen plot into `disp_fb` (SRAM) against the same into
   `disp_fb2` (PSRAM) — `utils/linebench.c` and `utils/gfxtest.c` are
   the closest existing harnesses;
2. `display_fb_copy()`, i.e. a 38,400-byte PSRAM→SRAM `memcpy`, which
   at the measured 12 MB/s is about 3.2 ms — a plausible frame cost,
   but confirm it.

If drawing into PSRAM turns out to be the dominant cost, the
alternative worth weighing is keeping the off-screen buffer in SRAM
and spending the 40 KB there instead: TOTALMEM is 312 KB and a process
may now use all of it, so that trade is no longer free the way it was
when `PROGSIZE` capped a process at 256 KB.

## 5. Then the BASIC side

`mmb2c.py` first (it is the reference), then mirror into `mmbc` and
check with `mmbc/cgate.sh` — byte-identical output over the suite is
the definition of correct. The runtime calls go in `mmb_runtime.c` with
wrappers in `bcrun_mm.c`, exactly as the existing graphics primitives
do, and the masters live in the mmb2c repo (`fcc/sync-runtime.sh`,
`fcc/sync-mmbc.sh`).

A gated test belongs in `mmb2c/tests/` with a `.expected`, like
`localheap.bas`. It cannot check pixels, so it should check what is
checkable: that `CREATE` twice is an error, that `WRITE F` without a
buffer is an error, that `CLOSE` releases (allocate/free in a loop and
survive), and that `COPY` moves what was drawn.
