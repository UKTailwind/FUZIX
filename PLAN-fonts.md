# PLAN: user-defined fonts (`DefineFont`)

Written 2026-08-13 from the picofrog survey.  **EXECUTED 2026-08-14 and
board-verified** - mmb2c 8502c02, FUZIX 1f4e8e475.  Every phase below
is done; the notes in each say what actually happened.

Board evidence, in order:

* the spike, on the real machine: `defined font 10`, `metrics: width 8`,
  **`pixels: 0 bad`**, `define font 9: refused`, `short extent:
  refused`, `foreign address: refused`, and `child correctly saw no
  font 10` - the ownership hazard closed by a real forked process
  rather than by argument;
* `tests/fontdef.bas` through the whole chain (translate, on-board
  `cc`, `bcrun`, kernel) printing `start` / `done`;
* **picofrog's own 764-byte Konami font** as font 10: the `!` glyph
  read back with exactly the twelve lit pixels its bytes predict, and
  a line of it drawn beside the built-in font for the eye.

The one thing that went wrong is worth more than the plan: the first
board run said `Error: Invalid font`, and the cause was the BOARD's
`/usr/lib/cc/include/mmb_runtime.h` being stale.  Without the
prototype the call compiled anyway and every argument after the first
shifted - the kernel was handed `addr 0 bytes 0`.  Deploying a new
runtime function means deploying THREE things (bcrun, the header, and
whatever calls it), and the header is the one with no symptom of its
own.  See [[board-missing-prototype-slots]].

`picofrog6_b9.bas` is the driver: it carries its own 8x8 "Konami style"
font as a `DefineFont` block and selects it with `Font 9`.  Nothing
else in the game needs anything the port does not already have, so
this and the string-argument work (`PLAY SOUND 1,"B","Q"`,
`FRAMEBUFFER WRITE lc$` - see NEXT.md) are what stand between us and
running it.

---

## 1. Ground truth

### What the BASIC side looks like

```basic
Font 9                                  ' selection - ALREADY TRANSLATED
...
DefineFont 9
  5F200808
  00000000 00000000 18181818 00180018 ...
End DefineFont
```

Lines of space-separated 8-hex-digit groups between `DefineFont n` and
`End DefineFont`.  picofrog's block is at line 1324 - **after** every
statement that uses it, which pins down decision D3 below.

### The data format - and the piece of luck

Each group is a 32-bit LITTLE-ENDIAN word.  Byte-swapping `5F200808`
gives `08 08 20 5F`, which is *exactly* the layout `pico_ioctl.h`
already documents for our own fonts (GFXIOC_FONTADDR):

	byte 0	width in pixels		byte 2	first character
	byte 1	height in pixels	byte 3	how many characters
	byte 4+	glyphs, width*height bits each, MSB first, no padding

So picofrog's font is 8x8, 95 characters from 0x20, and
`4 + 95 * 8 = 764` bytes - which is what the block's own comment
says ("Memory usage : 764 Bytes").  The glyph bits are already in our
bit order.  **Nothing in the glyph renderer changes; the only
transformation anywhere is a byte swap per word, done at translation
time.**

Test vectors, hand-checked against the file (keep these - they are the
cheapest possible regression test for the swap):

| word pair | bytes after swap | is |
|---|---|---|
| `00000000 00000000` | 8 x `00` | space (0x20) |
| `18181818 00180018` | `18 18 18 18 18 00 18 00` | `!` |
| `006C6C6C 00000000` | `6C 6C 6C 00 00 00 00 00` | `"` |

`0x18` = `00011000`, a two-pixel bar in the middle of the cell: an
exclamation mark is five of those, a gap, a dot, a gap.  Correct.

### What MMBasic does (Draw.c, MMBasic.c)

- `FontTable[FONT_TABLE_SIZE]`, `FONT_TABLE_SIZE 16`,
  `FONT_BUILTIN_NBR 9` (Draw.h:149).  Index = font number - 1.
- `initFonts()` fills indices 0-8 with the nine built-ins and NULLs
  9-15.  User fonts are therefore numbers **10-16**.
- Fonts are bound at PROGRAM LOAD, not when the block is reached:
  `MMBasic.c:1167-1176` walks the CFunction area in flash and does
  `FontTable[*cfp & 15] = (unsigned char *)(cfp + 2)` for every entry
  whose bit 31 is set.  The data lives in flash with the program and
  the table holds a POINTER to it - which is the model this plan
  copies, with a Fuzix process image standing in for the flash.

### What our kernel has (fonts.c, display.c)

- `font_table[9]`, MMBasic's nine, `const` so they are in XIP flash.
- `display_font(font, &w, &h, &first, &count)` returns NULL outside
  `1..NFONTS` and reads every metric out of the font's own header -
  so it already handles any width/height/range a user font can have.
- `display_gfx_text()` (display.c:1752) is the only glyph renderer,
  used by GFXIOC_TEXT for both `TEXT` and graphics `PRINT`.
- `GFXIOC_FONTINFO 0x001D` and `GFXIOC_FONTADDR 0x0031` report
  metrics and the glyph address.
- `struct gfx_text.font` is a `uint8_t` - 10..16 fits, no ABI change.

### What mmb2c has

- `FONT [#]n [, scale]` -> `mm_font(n, scale)` (mmb2c.py:4444).
- `TEXT x,y,s$[,just][,font][,scale][,fg][,bg]` -> `mmg_text(...)`.
- `MM.INFO(FONT ADDRESS n)` -> `mm_fontaddr(n)`.
- `mmb_runtime.c` caches `mm_gfont/mm_gcw/mm_gch` so graphics `PRINT`
  knows the cell without a syscall per character; the cache is
  refreshed from `MM_GFX_FONTINFO` when `FONT` runs.

So **selection already works end to end**.  Only definition is
missing.

### Next free ioctl

`0x0036`.  Verified with `ioctlcheck.sh`: 52 codes, no duplicates,
highest in use `0x0035` (GFXIOC_SCROLL2).  The numbers are one flat
space shared by every prefix - check against ALL of them, which is
what that script is for.

---

## 2. Design decisions

### D1. The kernel keeps POINTERS, not copies  *(user decision)*

Eight slots, fonts 9-16 (only 10-16 usable, see D2), each holding an
address and an owner.  Copying a font into kernel RAM is not an
option: picofrog's is 764 bytes and kernel RAM is the scarce thing
here.  The data stays in the program's own image, where it costs the
program and nothing else, and the kernel reads it directly - there is
no MMU, so a program address is a machine address, exactly as
GFXIOC_FONTADDR already relies on in the other direction.

**This is the mirror of a call we already have.**  FONTADDR hands a
program the address of a kernel font; FONTDEF hands the kernel the
address of a program's font.

### D2. User fonts are 10-16; defining 1-9 is a clean error  *(user decision)*

MMBasic lets a `DefineFont` land on any index, so picofrog's
`DefineFont 9` replaces the built-in 8x10.  We do not need that:
"there is nothing special about 9", so **picofrog's block gets edited
to 10** and our built-ins stay immutable.

`DefineFont 1..9` must then be a translation-time ERROR naming the
range, never a silent no-op - a program whose font quietly did not
take would draw in the wrong glyphs and look like a rendering bug.
(The triage rule: a silent divergence outranks an honest gap.)

Eight slots rather than seven: the table is indexed `font - 9`, slot 0
is reserved and unused, and the arithmetic stays a mask.

### D3. Registration happens in the PROLOGUE, not where the block sits

picofrog calls `Font 9` at line 95 and defines the font at line 1324.
MMBasic binds at program load, so this works there and must work here.
`DefineFont` is therefore a DECLARATION: the translator emits the data
at file scope and the registration call at the top of `main()`,
immediately after `mm_mark()` (mmb2c.py:6826), never as an in-line
statement.

### D4. The byte swap is done at TRANSLATION time

The emitted C carries the kernel's byte layout directly:

```c
static const unsigned char mmfont_10[] = {
    0x08,0x08,0x20,0x5f, 0x00,0x00,0x00,0x00, ...
};
```

No runtime swap, no endianness assumption in the header, and the host
gates compile the identical bytes the board does.

### D5. Ownership is `struct p_tab *`, and it is released on death

**This is the one real hazard in the whole feature.**  Every process
on this machine loads at the same PROGLOAD, so a font address
registered by program A is a *plausible-looking* address inside
program B, and a stale slot would silently draw glyphs out of whatever
is running now - garbage on screen, no fault, nothing to trace.

Two mechanisms, both already precedented in this tree:

1. Each slot stores `struct p_tab *owner` (as `pinlock.c` does, and
   NOT a pid - pids get reused, p_tab pointers within a lifetime do
   not).  `display_font()` ignores a slot whose owner is not the
   current process.  Two programs can each hold their own font 10.
2. `display_font_release(p)` clears that process's slots, called from
   the two places that already call `display_fb_release(p)` and
   `pinlock_release(p)`: `swapper.c:190-196` (pagemap_free - the exit
   path, including kill and fault) and `misc.c` `plt_exec_cleanup`
   (exec).

The owner check alone is sufficient for correctness; the release hook
keeps the table honest and is four lines.

### D6. Swap is not a problem, and here is why

Fuzix swaps whole processes to PSRAM, but a font is only ever read
during a GFXIOC_TEXT call made BY THE OWNING PROCESS, which is
resident by definition while its own syscall runs.  Nothing else can
reach the pointer, because of D5.

---

## 3. Kernel change

```c
/* Where a PROGRAM's own font lives.  The mirror of GFXIOC_FONTADDR:
 * no MMU, so the caller's address is one the kernel can read, and the
 * font stays in the program's image where it costs nothing else.
 * Fonts 10-16; 1-9 are the built-ins and are refused.  The slot is
 * dropped when the process exits or execs, and is ignored for any
 * other process - every process loads at the same address, so a stale
 * pointer would otherwise be a plausible one. */
struct gfx_fontdef {
	uint8_t font;		/* 10..16 */
	uint8_t pad[3];
	uint32_t addr;		/* the glyph data, header first */
	uint32_t bytes;		/* 4 + count * (width * height / 8) */
};
#define GFXIOC_FONTDEF 0x0036		/* confirmed free by ioctlcheck.sh */
```

Kernel work, all small:

1. `fonts.c`: the 8-slot table, `display_font_set(font, addr, bytes,
   owner)`, `display_font_release(owner)`, and a user-table lookup at
   the top of `display_font()` before the built-in table.
2. `misc.c`: the `GFXIOC_FONTDEF` case - range-check the number,
   `valaddr` the address, sanity-check `bytes` against the header
   (`4 + count * (w * h / 8)`) and refuse a header whose `w * h` is not
   a multiple of 8, which the packing assumes.
3. `swapper.c` + `misc.c`: the two release calls.
4. `FONTINFO`/`FONTADDR`: answer for user fonts too, by going through
   `display_font()` - they then need no special-casing at all.

Cost: ~64 bytes of bss (8 slots x address + owner) and roughly 60
lines.  RAM is affordable *now* - moving `doexit` to flash for the
FIFO fix freed ~312 bytes and spent 8 - but check the link, because
this file's own rule is that anything new overflows it.

`fonts.c` is already in `default_text_excludes.incl` (flash), and the
new accessors belong there with it: once per string, never on the
scanout path.

---

## 4. Phases

### Phase 0 - spike (de-risk before any translator work)

A `utils/fontdef.c` that carries a hand-made 8x8 font (one glyph a
solid block, one a checkerboard, one the `!` vector above), registers
it with GFXIOC_FONTDEF, draws with GFXIOC_TEXT, and reads pixels back.
Proves the kernel half - the pointer lifetime, the metrics, the
renderer needing no change - before mmb2c parses a single hex word.
This is the pattern that paid in PLAN-games (sc2test before SPRITE
SCROLL, fifotest before the play daemons).

Also prove the negative: a second process asking for font 10 must get
nothing, and the slot must be gone after the first exits.

### Phase 1 - kernel

Section 3.  Gate: the spike passes, `sc2test`/`gfxtest` still pass,
and the link does not overflow.

### Phase 2 - runtime + bcrun

`mm_fontdef(n, addr, bytes)` in `mmb_runtime.c` (host stub returns
-1), plus **the `bcrun_mm.c` wrapper and its name-table entry** -
without both, the loader says `no runtime function "mm_fontdef"`.
`mm_font()` also stops rejecting 10-16 if it currently clamps.

### Phase 3 - both translators

Parse `DefineFont n` ... `End DefineFont` in `mmb2c.py` and `mmbc`,
byte-identically:

- accumulate hex words until `End DefineFont` (accept `EndDefineFont`
  and a `#` on the number, as MMBasic tolerates both);
- error on: a number outside 10-16 (D2), a group that is not 8 hex
  digits, a total length that disagrees with the header, `w * h` not a
  multiple of 8, or a duplicate definition of the same font;
- emit `static const unsigned char mmfont_N[]` at file scope (with
  the other statics, ~mmb2c.py:6796) and
  `mm_fontdef(N, (long)mmfont_N, sizeof mmfont_N);` as the first thing
  in `main()` after `mm_mark()`;
- set a `uses_fontdef` flag; no new header is needed - this rides in
  `mmb_runtime.c`, like `mm_play_send`.

Gate: `mmbc/cgate.sh` zero diff, `make check`, `fcctests.sh`,
`qemutests.sh`.

### Phase 4 - board

`tests/fontdef.bas`: define a font of known bit patterns, draw it,
`PIXEL()` the result and compare against the patterns - comparing
only against painted pixels, never `MAP()`/`RGB()` (the kernel's
RGB332 expansion differs from RGB121; this cost a debug cycle in the
sprite work).  Then the real thing: picofrog's own block, edited to
font 10, drawn and looked at.

---

## 5. Verification ledger

| claim | how it is proved |
|---|---|
| word swap is right | the three test vectors above, checked in the parser's unit test AND by eye on screen |
| header agrees with data | translator arithmetic; a truncated paste is an error, not a wrong font |
| renderer needs no change | Phase 0 spike draws a user font with the untouched `display_gfx_text` |
| pointer lifetime is safe | Phase 0 negative test: second process, and after-exit |
| metrics reach the client | graphics `PRINT` with a user font wraps and scrolls at the right cell |
| both translators agree | cgate zero diff |

---

## 6. Deferred (recorded, not planned)

- **Replacing built-ins 1-9.**  MMBasic allows it; we refuse it (D2).
  If some other program needs it, the table becomes 16 slots and the
  lookup order is unchanged - about 64 more bytes of bss.
- **`LOAD FONT` from a file at runtime.**  The same ioctl serves it;
  the data would sit in the program's heap instead of its image, and
  the owner check covers that identically.
- **Fonts in the PSRAM arena.**  Only interesting if a font ever needs
  to outlive its program, which nothing has asked for.  Note the MP3
  measurement first: hot data in the arena ran at half the speed of
  process memory, and glyphs are read per character.

---

## 7. Open questions

1. Does anything else in picofrog use a font-dependent metric we do
   not report?  (The full feature scan is still to do; only `Font`,
   `DefineFont`, sprites, blits and PLAY have been surveyed.)
2. Should `FONT` with an undefined user number be an error or fall
   back to font 1?  MMBasic draws nothing useful from a NULL entry;
   an honest error is probably right, but check the reference first.
