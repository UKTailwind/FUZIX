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

# Stage 3: what is still in RAM, measured (2026-08-20)

Reviewed because networking needed a 4K block of the process pool
(PC3-NET-PLAN.md) and this file's own rule says the valve is to move
more code to flash rather than take memory from the pool.

**66,595 bytes of code and read-only data still execute from RAM.**
Every figure below is a measured `.data` delta from a real link, not an
estimate: the item was added to the excludes, the kernel relinked, and
the section size read from the map.

## Two mechanical findings first

**The .incl files are not link dependencies.** `make` does not relink
when an excludes file changes, because
`pico_add_linker_script_override_path` is called without `FILES` or
`GLOB_FILES`. Editing placement and rebuilding measures NOTHING and
looks like the change had no effect. `rm build/fuzix.elf` first. This
cost a wrong measurement here before it was noticed.

**Read-only data is almost entirely unclaimed.** Only libm, lwIP and
the cyw43 driver name `.rodata` for flash. Everything else's constants
sit in RAM, and the largest single item in the whole RAM image is the
8,430-byte merged string pool - every kprintf format string in the
kernel, which the map attributes to `devices.c.o` because ld merges
`.rodata.*.str1.4` across objects and lists the first contributor.

An earlier note here worried that the pool cannot move because
`rawflash.c` and dhara run while the flash is busy and a `kprintf` on
that path would then read its format string from XIP. That objection
does not survive: **printf.c is already in flash.** If such a call were
reachable it would already hang, whatever the strings do.

## The list, in priority order

### 1. Cold code and the string pool - measured -5,848

    main.c text                boot, plt_param, fatal_exception_handler
    pico_sync/mutex.c          SDK, cold
    hardware_claim/claim.c     SDK, cold
    pico_runtime_init          SDK, boot only
    pico_multicore             core1 launch, once
    pico_stdio, pico_stdio_uart
    rodata for: main.c start.c filesys.c swapper.c usbkbd.c
                ds3231.c inode.c devio.c process.c kdata.c

Nothing here is on a timed path. This alone is more than the 4K block
networking took, so it can be paid back and the pool returned to 336K.

Test: boot, `ls -lR /`, a compile, and write to the flash disk while
something is printing.

### 2. usbh.c - measured -6,584

TinyUSB's host state machine. Runs from `tuh_task()` in thread
context, which is this port's pump.

**One function in it is called from the ISR**: `hcd_event_handler`,
which `hcd_rp2040.c` calls to queue an event. Under this file's rule
that function stays in RAM, and since a claim is per input section the
honest form is to name the sections wanted in flash rather than the
whole object - `process_enumeration` alone is 1,764 bytes of the
6,584 and runs once per plug-in.

Test: unplug and replug the keyboard, through the hub, several times.
Enumeration is the fragile part of this machine's USB.

### 3. The SDK double-precision maths - measured -5,984

    double_sci_m33.S  4,562
    double_aeabi_dcp.S  894
    double_conv_m33.S   524

sin/cos/exp/log and the conversions, reached from BBC BASIC and from
translated BASIC through libm_table.c. `libm.a` and `double_math.c`
are already in flash, so this is finishing a job that was started.

The risk is real but bounded: these are leaf routines, each well under
the XIP cache, and a float-heavy loop calls the same one repeatedly.

Test: `utils/libmbench` before and after. This one has a number, so
there is no reason to guess.

### 4. display.c graphics - measured -4,992

The `display_gfx_*` family: rect, bitmap, scroll2, pixels, merge. Not
the scanout - that is core1 and the DMA - but it is drawing code, and
drawing runs *while* the screen is being scanned out of PSRAM. Fetching
these instructions over the QMI competes with exactly that.

This is the port's known sore point, so it is last despite being easy:
console.c went to flash and measured clean, which is the precedent
that says it is worth trying, not proof that it works.

Test: `utils/ripple`, `utils/linebench`, `utils/blitbench`, and a
person looking at the screen for flecking.

### Total if all four land: -23,408 bytes, or nearly six 4K blocks.

## What must NOT move, and why

    journal.c 2,656  } dhara, the flash FTL: cannot execute from the
    map.c     1,636  } device it is erasing
    flash.c     846  } hardware_flash, same
    rawflash.c       } already excluded for the same reason

    rawuart.c 2,188  UART interrupt
    hcd_rp2040 2,026 USB interrupt
    rp2040_usb  772  USB interrupt helpers
    irq.c     1,738  SDK interrupt dispatch
    time.c    2,272  alarm pool - the system tick lives here

    psram.c     772  retimes the QMI; must not be fetched through it
    clocks.c    534  runs across the clock change, same argument
    core1.c     400  the scanout loop itself

    sound.c   1,664  already claimed for flash: what is left is
                     __not_in_flash_func by source, the synth in the
                     DMA interrupt.  Not reachable from these files.

## Awkward, left for later

`tty.c` (2,258) and `process.c` (2,114) are each partly reachable from
interrupt context - `tty_inproc` from the UART ISR, the scheduler from
the tick - so they need a function-level list rather than an object
one. Together they are another 4K for somebody with the patience.

## A warning about benchmarking any of this

Removing the flash filesystem shrank the image by about 6.5K and moved
every flash address after it. `bench` jumped by much more than the
change could account for, and the explanation is cache set assignment:
two hot flash-resident routines - `log` and `sin` in that case - either
share an XIP cache set or they do not, entirely according to where the
addresses fall. Nothing about the code changed.

So:

* A step change in a benchmark after a placement change is **expected**,
  in either direction, and it is not evidence that the moved code was
  hot. Every item on the list above shifts the addresses of everything
  after it.
* A gain won this way can be lost by the next unrelated commit that
  changes the image size, with nothing in the diff to suggest why.
* Judge a placement change on the RAM it recovers, which is
  deterministic, and treat the timing as weather. If a timing result
  matters, it needs the interleaved A/B in PC3-DEVNOTES.md, and even
  then it is measuring this build, not this decision.
