# FRAMEBUFFER

Written 2026-08-03 as a plan; rewritten the same day as a record, once
it was built and measured on the board. Section 4 is the part worth
keeping — it settles a design question the plan got wrong.

## 1. What it is

MMBasic's `FRAMEBUFFER` (Draw.c `cmd_framebuffer`), reduced to the "F"
buffer:

    FRAMEBUFFER CREATE            make it, blank
    FRAMEBUFFER WRITE N | F       drawing goes to the screen, or to it
    FRAMEBUFFER COPY s, d [, B]   s and d each N or F; B waits for the
                                  top of the frame first
    FRAMEBUFFER CLOSE [F]         give it back
    FRAMEBUFFER WAIT              wait for the top of the frame

`LAYER`, `MERGE` and the second buffers are **not** built. There is one
off-screen buffer and no transparent blit; the translator refuses them
by name rather than turning them into something they are not.

## 2. The kernel: the write target is per PROCESS

This was the whole of the difficulty, and it is not obvious from the
BASIC side, where `CREATE` and `CLOSE` look like no-ops.

`gfx_draw` — the pointer every drawing primitive writes through — is now
**derived state**, recomputed by `display_fb_enter()` at the top of
every graphics ioctl from *who is calling*. The truth is two variables
in `display.c`:

    static struct p_tab *fb_owner;   /* NULL = the layer is free */
    static uint8_t fb_sel;           /* owner is drawing into it */

Anything else that draws — another program, the console's own repaint —
gets the screen, whatever the holder last asked for. A single global
target would mean a program that blocked with the layer selected had
its picture written over by whatever ran next, and one that exited
without deselecting left the whole machine drawing off-screen with no
way back.

So the layer is **owned**, one process at a time:

* `GFXIOC_FBOPEN` (0x0018) claims it — `EBUSY` if another process has
  it, `EINVAL` if the board has no PSRAM — and releases it.
* `GFXIOC_FBSEL` (0x0016) points the primitives at it, for the caller
  only, and only if the caller holds it.
* `GFXIOC_FBCOPY` (0x0017) copies either way: 0 layer→screen,
  1 screen→layer.
* `GFXIOC_VSYNC` (0x0019) waits for the top of the frame. Bounded, so a
  stopped scanout cannot hang the caller.

Released in three places, and all three matter:

* `pagemap_free()` — exit. Without it the next program is told the
  layer is busy by a process that no longer exists.
* `plt_exec_cleanup()` — exec. The new image did not create it.
* `display_gfx_mode()` — a mode change. What is in the buffer is in the
  geometry of the mode being left and nothing converts it. MMBasic's
  `setmode()` opens with `closeframebuffer('A')` for the same reason,
  which is why `CREATE` belongs after `MODE`.

`GFXIOC_BLIT` writes through the same target, so a shadow-buffer
program keeps working when pointed at the layer.

## 3. The BASIC side

`mmb2c.py` first, mirrored into `mmbc`, `cgate.sh` byte-identical.
Runtime calls in `mmb_runtime.c`: `mm_fb_create/close/write/copy/wait`.
The rules — what is refused, and when — are common to every target, so
a program behaves the same under the host gates as on the board; only
five hardware hooks differ, and on the host they do nothing.

**`CLS` follows `WRITE`.** It was an ANSI escape to the console, and the
console writes straight to `disp_fb` — so with `WRITE F` it cleared the
*screen* and left the buffer accumulating every frame ever drawn. It
now floods whatever is being drawn on, in the background colour, and
falls back to the console's own clear (cursor and colour tiles) when
that is the screen.

Gated: `tests/framebuf.bas` and `tests/fbwrite.bas` check the refusals.
That needed a harness addition — `mm_error` exits 1 and every gate
insisted on 0 — so `tests/<name>.rc` now says what exit status is
expected. `samples/fborbit.bas` is a real animation, and lives in
`samples/` because it loops until a key and so has no finish for a gate
to compare.

## 4. What it costs — measured, and NOT what the plan expected

`tests/fbdemo.bas` on the board, MODE 2, repeatable to the microsecond:

    full-screen fill, straight to the screen (SRAM)    1.488 ms
    the same fill into the buffer (PSRAM)              3.628 ms
    copy the 38,400-byte buffer to the screen          0.726 ms
    animation frame, drawn direct                      1.185 ms
    animation frame, buffered with ",B"               17.06  ms

**The plan predicted ~3.2 ms for the copy, from the 12 MB/s measured for
PSRAM. The real figure is 0.726 ms — 53 MB/s, 4.4× better.** That
12 MB/s was scattered access. A bulk sequential read streams through the
QMI and uses every byte of every cache line, so the 16 KB XIP cache
being far smaller than the 38,400-byte buffer costs nothing here. The
worry that shaped the whole plan does not apply to the copy at all.

Drawing into PSRAM *is* 2.4× the cost of drawing on the screen. But the
total is what matters: **3.6 ms to draw + 0.7 ms to copy = 4.4 ms**
against the 17 ms the display gives you. The 17.06 ms buffered figure is
not slowness — it is `,B` holding the loop to the refresh, one frame per
iteration with none dropped, and about three times as much drawing would
fit before one was.

**So the design question is settled: the buffer stays in PSRAM.** The
alternative the plan floated — spend 40 KB of SRAM on it instead — would
buy 2.1 ms a frame out of a 12.6 ms surplus.

## 5. What is left

* `LAYER` and `MERGE`, which need a second buffer and a
  transparent-colour blit. Only then is there a reason to make the
  buffers arena allocations owned by the process rather than the one
  static `disp_fb2` — with a single F buffer, `CREATE`/`CLOSE` really
  are just a claim on it, and `__uninitialized_psram("fb2")` earns its
  40 KB.
* `MODE 2` `PRINT` still goes to the console, and so to the screen, even
  while drawing goes to the buffer. `OPTION CONSOLE` is the fix and is
  already on the list.
