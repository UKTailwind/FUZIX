# Getting the kernel out of RAM

RAM is the scarce resource on this board and flash is nearly empty.
The kernel was built `PICO_COPY_TO_RAM`, so all 90,676 bytes of `.text`
were copied into RAM at boot; flash sits at 22% of the space before
`FLASH_OFFSET`. Every byte recovered goes straight back to the process
pool, which had to give up 4K to fit the text primitives.

## The mechanism, and why it is not `__not_in_flash_func`

The obvious approach — mark every function `__not_in_flash_func()` and
then remove the macro from the ones that can move — does not work here.
**35 of the kernel's 68 sources are shared Fuzix core files**
(`../../process.c`, `../../syscall_*.c`, `../../tty.c` …) which every
other Fuzix port compiles. They cannot carry Pico SDK macros.

`-ffunction-sections` is already on, so the linker sees one
`.text.<name>` section per function, and placement can be done **by
name, in the linker script, with no source changes**. The SDK's own
`.data` section ends with

    *(.text*)
    *(.rodata*)
    /* stuff we exclude in ..._excludes files because we want it in RAM */

so whatever `default_text_excludes.incl` does not claim for flash falls
through to RAM automatically. Two override files therefore control
everything:

    linker_overrides/default_text_excludes.incl     what may EXECUTE from flash
    linker_overrides/default_rodata_excludes.incl   what may be READ from flash

A name listed there is in flash. A name left out is in RAM. Flash
sections come first in the script and the first match wins.

## Stage 1 — done, and deliberately does nothing

`PICO_COPY_TO_RAM` is off and both lists are **empty**, so everything is
still in RAM and the build behaves exactly as before:

    .text in flash    472  ->    524     (boot stub only)
    RAM .data      24,736  -> 115,352    (now holds the old .text)
    .bss           89,776  ->  89,776
    RAM ends   0x20032298  -> 0x20032258 (64 bytes smaller: alignment)

The mechanism is in place and behaviour has not changed. That makes
stage 2 a matter of adding one name at a time and measuring, instead of
one change that moves everything and has to be debugged as a whole.

**Measured, because the QMI worry deserved a number rather than a
guess.** Same `fbtext.bc` — 600 frames of drawing into the PSRAM
framebuffer and copying it out — on kernels differing only in
`PICO_COPY_TO_RAM`:

    copy_to_ram 1   4,079 ms
    copy_to_ram 0   4,072 ms

Within noise. Leaving XIP enabled costs nothing while the flash list is
empty, which is the result that makes stage 2 worth doing at all. It
does NOT yet answer what happens when kernel code is actually being
fetched from flash during PSRAM work - that is the thing to watch as
names are added.

(That run also caught something unrelated: `fbtext.bas` was scrolling
the whole 38,400-byte buffer every frame, because its bottom line ended
with a newline that overflowed the screen. Correct behaviour, 2 ms a
frame, and the sample now carries a note. 4.64 ms a frame without it.)

## The rule

A function may run from flash if a cache miss on it cannot hurt.

**It must stay in RAM if it is:**

* on the **scanout path** — core1 builds every scanline against a
  deadline and a miss is a visible glitch;
* an **interrupt handler**, or called from one;
* reached while **flash is busy** — flash and PSRAM share the QMI, so
  XIP fetches contend with the framebuffer layer and `bcrun`'s heap;
* part of the **flash driver itself** — code that erases or programs
  flash cannot be executing from it;
* **hot enough to measure** — the ioctl path is 1.3 µs per call and
  that is a number we have defended before.

## Must stay in RAM

| what | why |
|---|---|
| `disp_dma_irq`, the line expanders, `core1_*`, `display_in_blanking` | scanout, per-scanline deadline |
| `alarm_pool_irq_handler`, `irq_*`, every `*_irq_handler` | interrupt context |
| `plt_dev_ioctl` (1,796 bytes) and everything it dispatches to — `display_gfx_pixel/rect/pixels/rects/bitmap/text/scroll`, the `fb_*` calls | the drawing hot path; PIXEL is 1.3 µs and batching exists to defend it |
| `swapper.c` — `contextswitch`, `swapin`, `swapout`, `pagemap_*` | runs on every context switch, memcpy-heavy |
| `tricks.S` | the context switch itself |
| `rawflash.c`, `devflash.c`, `lib/dhara/*` | cannot execute from the thing being erased |
| `usbkbd.c` / `usbh.c` interrupt-adjacent paths | enumeration is already known to be fragile here |
| `libgcc`, `libc`, `lib*_a-mem*`, `libm` | the SDK deliberately keeps these in RAM; `memcpy`/`memset` are everywhere |
| `arena.c`, `mm/*` | allocation under the drawing and swap paths |

## Candidates for flash, biggest first

Read-only data is the cheapest and safest win — it is read, never
written, and mostly at human speed.

| bytes | symbol | object | note |
|---|---|---|---|
| 4,848 | `USkeyValue` `UKkeyValue` `DEkeyValue` `FRkeyValue` `ESkeyValue` `BEkeyValue` | `usbkbd.c` | 808 each, read on a keypress |
| 2,692 | `font1` | `console.c` | read per glyph; the expanders read `disp_fb`, not the font |
| 7,188 | `flash_dev_init.str1.4` | `devflash.c` | **the biggest single item, but check it** — strings belonging to the flash driver |
| ~1,800 | `__func__` / `str1.4` literals | `i2c.c`, `pio.c`, `clocks.c`, `flash.c` | SDK assertion strings |

Code, in rough order of payoff and safety:

| bytes | function | object | note |
|---|---|---|---|
| 1,700 | `_vsnprintf` | `printf.c` | panics and boot messages only |
| 1,208 + 520 + 1,020 | `do_csi`, `do_sgr`, `charout` | `console.c` | escape parsing at terminal speed; `charout` is per character so measure it |
| 1,216 | `_execve` | `syscall_execelf32.c` | once per program start |
| 688 | `sound_init` | `sound.c` | init |
| 484 | `fuzix_main` | `start.c` | boot |
| 408 | `display_gfx_mode` | `display.c` | stops and restarts the scanout, but is not itself on it |
| 560 + 560 | `kbd_key`, `lineedit_input` | | human speed |
| ~4,300 | most of `filesys.c` — `n_open`, `i_open`, `ch_link`, `i_alloc` | | SD-backed, not flash-backed |
| ~2,000 | `syscall_other.c`, `syscall_fs3.c` | | ordinary system calls |

Whole objects worth examining as units:

    devflash.c   7,578    but see the caveat above
    console.c    7,357    minus charout if it measures badly
    usbh.c       6,865    enumeration is fragile - be careful
    usbkbd.c     6,098    mostly the six keymaps
    filesys.c    4,318    good candidate
    journal.c    2,656    dhara - MUST STAY
    map.c        1,900    dhara - MUST STAY

## Stage 2

Add names a few at a time, rebuild, and check: the display does not
glitch, the console still scrolls, a compile still runs, and the PIXEL
and framebuffer timings (`utils/linebench`, `samples/fbtext.bas` at
4.6 ms a frame) have not moved. Recover the 4K taken from the pool
first, then keep going.

The number to beat: **90,676 bytes**, of which perhaps 30-50K is
genuinely cold.
