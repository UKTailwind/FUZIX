#ifndef PICO_IOCTL_H
#define PICO_IOCTL_H

#include <stdint.h>

/*
 * ONE FLAT NUMBER SPACE.  Read that before adding anything below.
 *
 * plt_dev_ioctl (misc.c) dispatches every code in this file from a
 * single if-chain, so GFXIOC_, PICOIOC_, SNDIOC_, PSRAMIOC_ and PLKIOC_
 * are naming, not namespacing: two names sharing a number means the
 * test that comes FIRST in that chain wins and the other is dead code
 * that silently does the wrong thing.
 *
 * PICOIOC_BOARD was given 0x0021, which SNDIOC_PCMOPEN already had.
 * PICOIOC_BOARD is tested first, so every PCM open wrote a 2 or a 3
 * over the caller's sample rate and returned success - PLAY MP3 played
 * silence and reported nothing wrong.  The prefixes are exactly why it
 * looked free.
 *
 * The allocation table is not maintained by hand here, because a hand
 * copy is one more thing to fall out of date.  Ask:
 *
 *	sh ioctlcheck.sh
 *
 * which prints every code in numeric order and FAILS on a duplicate.
 * Run it after adding one.
 */

/* Reboot PI Pico into flash mode */
#define PICOIOC_FLASH 0x0001

/* Set the USB keyboard layout: data -> a 2-letter layout name
 * (US/UK/DE/FR/ES/BE, case-insensitive) */
#define PICOIOC_KBDMAP 0x0002

/* Re-drive an SE0 bus reset on the root USB port, forcing any attached
 * hub back to Default state so it re-enumerates.  The kernel does this
 * once at boot; this is for when a hub appears AFTER boot already
 * configured - on the PC3 that means flipping the DPDT switch from the
 * programming port to the hub, which presents a hub that has never
 * lost VBUS and so ignores enumeration from address 0.  Needed because
 * hcd_port_reset() is a no-op stub in the pico-sdk TinyUSB driver, so
 * enumeration never resets the port by itself.  No argument. */
/* 0x001C, not 0x000D: it collided with PSRAMIOC_REALLOC, and only
 * harmlessly because PC3_NO_USB_BUS_RESET compiles this handler out.
 * It is tested BEFORE the PSRAM calls, so re-enabling the bus reset
 * would have silently shadowed realloc. */
#define PICOIOC_USBRESET 0x001C

/*
 * Num lock, which is a property of the KEYBOARD rather than of the
 * machine - see the long note in usbkbd.c.  A keyboard with no numeric
 * keypad overlays one onto 7890/uiop/jkl;/m while num lock is on, its
 * own firmware doing that in response to the LED report the kernel
 * sends, and nothing in the USB descriptors identifies such a keyboard:
 * a Raspberry Pi keyboard and a full-size Lenovo send byte-identical
 * report descriptors.  So the kernel guesses from the one readable
 * signal (a keyboard that declares no num lock LED is taken to have no
 * keypad) and remembers what it is told, by VID:PID.
 *
 * One call does both directions.  `set' 0 fills the struct in and
 * changes nothing; 1 applies `on'.  On a set, vid/pid 0 means the
 * keyboard that is plugged in - naming one explicitly records the choice
 * for a keyboard that is NOT attached, which is how /etc/rc can carry a
 * setting across a reboot for each keyboard the machine owns.
 *
 * On return, vid/pid are always the MOUNTED keyboard (0 if none) and
 * `led' says whether it declared a num lock LED, so `picoctl numlock'
 * can report what the guess was based on.
 */
struct kbd_numlock {
	uint8_t on;		/* in on a set; out always: current state */
	uint8_t set;		/* in: 0 query, 1 apply */
	uint8_t led;		/* out: keyboard declares a num lock LED */
	uint8_t pad;
	uint16_t vid;		/* in on a set (0 = the mounted one); out: mounted */
	uint16_t pid;
};
/* The same bytes as GFXIOC_BLIT and GFXIOC_BLITRD, but a RECTANGLE:
 * `rows` spans of `len` bytes, `stride` apart in the framebuffer and
 * contiguous in the caller's buffer.  Same target, same layout, same
 * bounds rule - only the row loop moves.
 *
 * Why: every rectangle mover in the runtime was a loop of one-row
 * calls, and each of those is a system call - two for a write, which
 * read-modify-writes for the boundary bytes.  A 9x9 sprite cost about
 * fifty crossings to show, which the board measured as 0.54ms per
 * SPRITE SHOW and 91% of a 38ms frame.  The pixels moved are identical;
 * what changes is how many times the kernel is entered to move them.
 *
 * The caller still owns the packing rules and the pixel logic, exactly
 * as it does for the single-row pair - this is a transfer, not a
 * drawing operation.
 */
struct gfx_blitr {
	uint32_t offset;	/* first row: byte offset into the target */
	uint16_t len;		/* bytes per row                          */
	uint16_t rows;
	uint16_t stride;	/* the target's bytes per row             */
	uint16_t pad;
	void *buf;		/* rows * len bytes, contiguous           */
};
#define GFXIOC_BLITR   0x0039	/* rows INTO the target   */
#define GFXIOC_BLITRDR 0x003A	/* rows OUT of the target */

/*
 * MMBasic PLAY SOUND / PLAY TONE / PLAY VOLUME, synthesised IN THE
 * KERNEL - the audio IRQ reads voice parameters this ioctl pokes, so
 * a parameter change is audible within one 64-frame buffer (~1.5 ms)
 * rather than behind a daemon's 186 ms PCM cushion.  MMBasic's own
 * arrangement, and the reason picofrog can slide a pitch every 20 ms.
 *
 * The op and the values are PINNED to mmb_playctl.h's MM_PLAY_* and
 * MM_SND_* (SOUND=1 TONE=2 VOLUME=4; OFF=0 SINE=1 SQUARE=2 TRI=3
 * SAW=4 PNOISE=5 WNOISE=6): the client built these records for the
 * daemon's FIFO for a whole release and every shipped .bc still
 * carries them.
 *
 * SOUND: a=voice 1-4, b=side bits (L=1,R=2), p1=type, p2=freq mHz,
 *        p3=vol 0-25.  TONE: p1=left mHz, p2=right mHz, p3=duration
 *        in samples at 44100 (-1 forever).  VOLUME: p1=left 0-100,
 *        p2=right 0-100.
 *
 * The first command claims the output for the calling pid (EBUSY if
 * an MP3/MOD player holds it); five seconds of silence, process
 * death, or SNDIOC_MMSTOP release it.  SNDIOC_PCMOWNER reports the
 * claim, which is what makes PLAY MP3's refusal and PLAY STOP's
 * discovery work unchanged.
 */
struct snd_mmcmd {
	uint8_t op, a, b, pad;
	int32_t p1, p2, p3;
};
#define SNDIOC_MMCMD	0x003B	/* struct snd_mmcmd */
#define SNDIOC_MMSTOP	0x003C	/* silence and release, data unused */

/* Does console output reach the DISPLAY as well as the uart?
 *
 * data is the value itself: 1 mirrors (the default and what this
 * machine is built around - the same byte to the screen and the serial
 * line, so it can be driven from either), 0 sends it to the uart alone.
 *
 * MMBasic has two independent devices, and OPTION CONSOLE SERIAL means
 * the uart by itself.  Here there is ONE device that is both, so the
 * screen half has to be turned off explicitly - and in a graphics mode
 * it must be, because the console renders as pixels there and a PRINT
 * meant for a terminal is drawn over the program's own picture.
 *
 * Given up automatically when the process ends, like the layer and the
 * user fonts: a program that dies holding this would otherwise leave
 * the machine with a display that shows nothing anyone types. */
#define PICOIOC_CONMIRROR 0x0038

#define PICOIOC_NUMLOCK 0x0037

/* MMBasic's KEYDOWN(): which keys are HELD, rather than which key was
 * typed.  data -> a struct kbd_down that receives the lot.
 *
 * A game needs this and INKEY$ structurally cannot give it: a keyboard
 * sends one character at a time, so "up and fire together" is not a
 * question the character stream can answer.  The HID boot report is six
 * concurrent key codes, which is exactly MMBasic's six, so this is the
 * report itself rather than anything reconstructed - usb_kbd_keydown()
 * already kept it, and until now nothing outside the kernel could ask.
 *
 * ONE call returns everything: MMBasic's function takes an index and a
 * game reads several in a row, and six separate crossings could each
 * see a different instant.  A single snapshot cannot disagree with
 * itself, and costs one syscall instead of six.
 */
struct kbd_down {
	uint8_t count;		/* how many of key[] are non-zero */
	uint8_t mods;		/* the modifier bitmap - KEYDOWN(7) */
	uint8_t locks;		/* 1 caps, 2 num, 4 scroll - KEYDOWN(8) */
	uint8_t pad;
	uint8_t key[6];		/* the held codes, most recent first */
	uint8_t pad2[2];
};
#define PICOIOC_KEYDOWN 0x003D

/* Graphics (PC3: see PC3-GFX-DESIGN.md).  All on /dev/sys. */

/* data -> int: BBC mode 0-5, mode 7 (320x240 16 colours, NOT teletext),
 * or 0xFF back to the text console.  Modes 0-5 scan out at 1024x768
 * and the console and mode 7 at 640x480; switching within one of those
 * groups holds the monitor's lock, crossing between them does not. */
#define GFXIOC_MODE   0x0003

/* data -> int: (logical colour << 8) | physical colour.  Modes 0-5
 * take the authentic BBC 0-7; mode 7 takes all 16. */
#define GFXIOC_PAL    0x0004

/* data -> struct gfx_blit: copy bytes into the mode framebuffer.
 * Layout (PC3 modes): 4bpp high nibble = left pixel, 1bpp MSB = left;
 * line stride 160 bytes (modes 1/4/7) or 80 (modes 0/2/3/5), and 256
 * lines - except mode 7, which has 240. */
struct gfx_blit {
    uint16_t offset;            /* byte offset into the framebuffer */
    uint16_t len;
    void *buf;
};
#define GFXIOC_BLIT   0x0005

/* The same thing the other way: copy bytes OUT of the framebuffer into
 * the caller's buffer.  Same struct, same offset and length, same
 * target - display_fb_target(), so a program drawing into the layer
 * reads the layer back.
 *
 * NATIVE FORMAT, deliberately, exactly as GFXIOC_BLIT writes it: 4bpp
 * high nibble = left pixel, 1bpp MSB = left.  This is MMBasic's
 * ReadBufferFast; GFXIOC_GETPIXEL is its ReadBuffer, which converts
 * through the palette and hands back RGB888 a pixel at a time.  The
 * two are for different jobs and both are wanted:
 *
 *   GETPIXEL   one pixel, RGB888, ~2.5us - a program asking what
 *              colour something is
 *   BLITRD     a run of bytes, raw, one crossing - a program that
 *              wants to LOOK AT a lot of pixels
 *
 * The second is what makes a flood fill possible at a sensible speed.
 * Reading a 320-pixel row through GETPIXEL is 320 system calls and
 * 800us; the same row here is one call and 160 bytes.  Without it the
 * fill has to be written the way nothing else on this machine is, and
 * MMBasic's own floodfill reads whole scanlines for the same reason.
 *
 * Reading is bounds-checked identically to writing, against
 * display_gfx_fbsize(): a short read is refused rather than served
 * with whatever follows the framebuffer. */
#define GFXIOC_BLITRD 0x0032

/* Current mode geometry, so a program can size a shadow buffer and
 * clip without hardcoding what the kernel already knows. */
struct gfx_info {
	uint16_t width;		/* pixels */
	uint16_t height;
	uint16_t stride;	/* bytes per line */
	uint8_t  bpp;		/* 1 or 4 */
	uint8_t  mode;		/* 0-5, 7, or 0xFF for the text console */
};
#define GFXIOC_INFO   0x000E

/* Set the current drawing colour.  data IS the RGB888 value (24 bits
 * fit in the argument, so no uget).  MMBasic's contract: callers always
 * speak RGB888 and the primitive converts to whatever the current mode
 * uses - 0/1 for the 1bpp modes, or the nearest of the 16 logical
 * colours for the 4bpp ones.  Converted once here, never per pixel. */
#define GFXIOC_COLOUR 0x0010

/* Set one pixel in the current colour.  The hot path: MMBasic's PIXEL
 * statement costs 5us, so this must stay cheap - the coordinates are
 * packed into the data argument ITSELF rather than pointed to, which
 * skips the uget and its validation entirely.  Measured at 1.30us
 * against 1.488us for an ordinary ioctl.
 *
 *   data = x | (y << 10)        x 0-1023, y 0-511
 *
 * Out-of-range pixels are dropped, not an error, as MMBasic does.
 * 4bpp layout is high nibble = LEFT pixel (framebuf GS4_HMSB), the
 * opposite of MMBasic's RGB121 - see PC3-GFX-DESIGN.md. */
#define GFXIOC_PIXEL  0x000F
#define GFX_PIXEL_PACK(x, y) (((x) & 0x3FF) | (((y) & 0x1FF) << 10))

/* Read one pixel back: data = packed x,y as above; returns the mode's
 * colour index, or -1 off-screen.  Used by MMBasic's PIXEL() function,
 * which returns RGB888 - the caller maps the index back. */
#define GFXIOC_GETPIXEL 0x0011

/* A filled rectangle in the current colour, which is also how lines
 * arrive (x1 == x2 or y1 == y2).  One call for the whole span, so the
 * syscall is paid once instead of per pixel - the reason the drawing
 * primitives are in the kernel at all. */
struct gfx_rect {
	int16_t x1, y1, x2, y2;
};
#define GFXIOC_RECT   0x0012

/* A whole shape in ONE ioctl: a run of points, or a run of rectangles.
 *
 * The crossing costs 1.3us and a pixel store costs 15ns, so anything
 * with more than a handful of points is dominated by the syscall, not
 * the drawing.  Batching moves the geometry - lines, circles, polygons,
 * arcs - out into userland, where it costs no kernel memory and can be
 * written once in a shared runtime rather than per program.  What is
 * left in the kernel is two primitives that never have to grow.
 *
 * It is also MMBasic's own array API rather than an invention:
 * PIXEL x%(), y%(), c%() maps onto GFXIOC_PIXELS directly, colour array
 * and all.  A filled circle becomes one run of spans plus one run of
 * points, which is how MMBasic draws it too.
 *
 * Coordinates are int16 rather than the packed form GFXIOC_PIXEL uses:
 * that packing exists only because it has to fit in an ioctl ARGUMENT,
 * and its nine bits of y stop at 511 - short of the 768 the BBC modes
 * scan out.  In an array there is no such pressure, and signed values
 * let a caller hand over off-screen points and let the kernel clip.
 *
 * colours may be NULL, meaning "all in the current colour"; otherwise
 * it is one RGB888 per item, as MMBasic's colour array is.  The mapping
 * to the mode's own colours caches the last value, so a constant-colour
 * run pays for it once.
 */
struct gfx_pt {
	int16_t x, y;
};
struct gfx_rc {
	int16_t x1, y1, x2, y2;
};
struct gfx_batch {
	uint16_t count;
	uint16_t flags;		/* reserved, must be zero */
	void *items;		/* struct gfx_pt[] or gfx_rc[] */
	void *colours;		/* uint32 RGB888 per item, or NULL */
};
#define GFXIOC_PIXELS 0x0014
#define GFXIOC_RECTS  0x0015

/* The off-screen layer - MMBasic's FRAMEBUFFER, in PSRAM.
 *
 * Scanout is unaffected and always shows disp_fb, so a picture built in
 * the layer appears only on FBCOPY.  That is MMBasic's
 * draw-off-screen-then-show shape, and the reason the layer can live in
 * PSRAM at all: it is written and blitted, never scanned out.
 *
 * There is one layer and it is OWNED.  FBOPEN 1 claims it - EBUSY if
 * another process already has it, EINVAL on a board with no PSRAM - and
 * FBOPEN 0 gives it back; so does exiting, exec'ing, or changing mode,
 * because none of those leave a picture worth keeping.  MMBasic's
 * CREATE and CLOSE F, which are a GetMemory/FreeMemory pair there and a
 * claim on the one static layer here.
 *
 * The claim exists because the write target is PER PROCESS.  FBSEL 1
 * points the drawing primitives at the layer for the CALLER only:
 * anything else that draws - another program, the console repainting -
 * still lands on the screen.  A single global target would mean a
 * program that blocked with the layer selected had its picture written
 * over by whatever ran next, and one that exited without deselecting
 * left the whole machine drawing off-screen. */
#define GFXIOC_FBOPEN 0x0018		/* int: (which << 8) | claim */
#define GFXIOC_FBSEL  0x0016		/* int: 0 screen, 1 F, 2 layer */

/*
 * FRAMEBUFFER LAYER and MERGE.
 *
 * The layer is a THIRD buffer and nothing more; what makes it a layer
 * is MERGE, which composites it over F onto the screen skipping a
 * nominated transparent index.  MMBasic's TFT model - see display.h
 * and PC3-LAYER-MERGE.md for why not its scanout-time one.
 *
 * FBCOPY CHANGED SHAPE when the layer arrived and its NUMBER changed
 * with it, deliberately.  It used to be one int meaning "which
 * direction", which cannot express three buffers; it is now
 * (src << 4) | dst over 0 screen, 1 F, 2 layer.  A retired number
 * means a mismatched runtime and kernel get EINVAL and say so, where
 * reusing 0x0017 would have had an old binary silently copying the
 * wrong way round.  0x0017 is not to be used again.
 */
#define GFXIOC_FBCOPY2 0x0033		/* int: (src << 4) | dst */
#define GFXIOC_MERGE   0x0034		/* int: transparent index 0-15 */

/* A run of text at a PIXEL position - MMBasic's PRINT in a graphics
 * mode, which draws glyphs rather than sending characters to a console.
 *
 * This is what makes PRINT work with the off-screen buffer: it goes
 * through the caller's write target like every other drawing call, so
 * text lands wherever the drawing is going.  Sending it to the console
 * instead - which is what happened before - put the shell's cursor on
 * the picture and scrolled the whole display when the line wrapped.
 *
 * The font is the console's own 8x12, so program text and shell text
 * match.  bg = -1 leaves the paper alone, which is MMBasic's
 * transparent PRINT (its PrintPixelMode 1).  Returns the x coordinate
 * the text ended at, so a caller can lay out a line piece by piece.
 */
struct gfx_text {
	int16_t x, y;		/* top-left, in pixels */
	uint8_t scale;		/* 1 = one cell of the font */
	uint8_t font;		/* 1..9, MMBasic's numbering; 0 means 1 */
	int32_t fg;		/* RGB888 */
	int32_t bg;		/* RGB888, or -1 for transparent */
	uint16_t len;
	void *str;
};
#define GFXIOC_TEXT   0x001A

/* The metrics of one of the built-in fonts.  A caller laying text out -
 * justifying it, centring it, deciding where the next line goes - needs
 * the cell size, and it must not carry its own copy of that: the fonts
 * are the kernel's, MMBasic's nine, and asking is one syscall against
 * a wrong answer that shows up as text in the wrong place.
 *
 * width and height come back 0 for a font that does not exist.  first
 * and count are the character range the font actually covers - font 6
 * is the eleven digits and nothing else. */
struct gfx_fontinfo {
	uint8_t font;		/* in: 1..9 */
	uint8_t width;		/* out: the cell, in pixels, at scale 1 */
	uint8_t height;
	uint8_t first;		/* out: first character in the font */
	uint16_t count;		/* out: how many */
	uint16_t nfonts;	/* out: how many fonts there are */
};
#define GFXIOC_FONTINFO 0x001D

/* Where a built-in font's glyph data actually IS.
 *
 * The same trick as PICOIOC_LIBM below: there is no MMU here, so an
 * address in kernel flash is an address the caller can read, and the
 * fonts are const so they are in XIP flash rather than in anyone's RAM.
 * One syscall at the start and a program can then draw its own glyphs -
 * onto an SPI panel the kernel knows nothing about, rotated, scaled,
 * into a shadow buffer - without a second copy of MMBasic's nine fonts
 * in its own image, and without a syscall per character.
 *
 * The layout at `addr' is MMBasic's, unchanged (see fonts.c):
 *
 *	byte 0	width in pixels		byte 2	first character
 *	byte 1	height in pixels	byte 3	how many characters
 *	byte 4  onwards: the glyphs, width*height bits each, packed
 *		continuously, MSB first, no padding between them
 *
 * so a caller that has the address needs nothing else - the metrics are
 * in the first four bytes.  GFXIOC_FONTINFO stays because laying text
 * out is the common case and it should not need PEEKs.
 *
 * The glyph for character c starts at
 *	addr + 4 + (c - first) * (width * height / 8)
 * and width*height is a multiple of 8 for all nine fonts, which is what
 * makes that plain MSB-first byte packing rather than a bit stream.
 *
 * addr and bytes come back 0 for a font that does not exist.  `bytes' is
 * header plus glyphs: a program reading flash has no other way to know
 * where the font ends, and reading past it is silent nonsense rather
 * than a fault. */
struct gfx_fontaddr {
	uint8_t font;		/* in: 1..16 */
	uint8_t pad[3];
	uint32_t addr;		/* out: machine address of the font data */
	uint32_t bytes;		/* out: 4 + count * (width * height / 8) */
};
#define GFXIOC_FONTADDR 0x0031

/* A font of the CALLER'S OWN - MMBasic's DefineFont, and the exact
 * mirror of GFXIOC_FONTADDR above: that hands a program the address of
 * a kernel font, this takes the address of a program's.
 *
 * Nothing is copied.  The glyphs stay in the caller's image - where
 * they cost the program rather than the kernel's last few hundred
 * bytes of RAM - and are read where they lie, which works for the
 * reason FONTADDR works in the other direction: no MMU, so an address
 * is an address.  MMBasic does the same, its FontTable holding
 * pointers to font data sitting in flash beside the program.
 *
 * `addr' points at the four-byte header documented above, `bytes' at
 * the whole extent including it; both are checked against the calling
 * process, and the header must agree with `bytes' (and width*height
 * must be a multiple of 8, which the glyph packing assumes).
 *
 * Numbers 10-16 ONLY: the built-in nine are shared with the console
 * and every other program, so they cannot be replaced.  Nine plus
 * seven is MMBasic's sixteen.
 *
 * The registration belongs to the PROCESS and disappears when it exits
 * or execs.  It has to: every process here loads at the same address,
 * so a slot left behind would be a plausible pointer into whatever
 * runs next, drawing silent garbage.  Another process asking for the
 * same number gets nothing, not this one's glyphs.
 */
struct gfx_fontdef {
	uint8_t font;		/* in: 10..16 */
	uint8_t pad[3];
	uint32_t addr;		/* in: the glyph data, header first */
	uint32_t bytes;		/* in: 4 + count * (width * height / 8) */
};
#define GFXIOC_FONTDEF 0x0036

/* The shared maths library.  data -> a void * that receives the
 * address of the table below; from there a program CALLS the entries
 * directly, because there is no MMU here and kernel flash is in the
 * same address space.  See libm_table.c for why, and for the errno
 * contract - these do not report domain errors.
 *
 * Check the magic and the version before using it.  An old binary on a
 * new kernel must fail rather than call the wrong slot. */
#define PICOIOC_LIBM  0x0020

/* Which machine this is.  data -> an int that receives 2 or 3, the same
 * answer the boot banner prints, from the same detection (the DS3231's
 * 32 kHz on GP27).  A program that wants to name itself - MMBasic's
 * MM.DEVICE$ does - has no other way to ask: board_name() lived in the
 * kernel and only the banner ever called it. */
/*
 * 0x002C, NOT 0x0021.  It was 0x0021, which is SNDIOC_PCMOPEN - and
 * plt_dev_ioctl tests this one first, so every PCM open landed here
 * instead: it uput a 2 or a 3 over the first four bytes of the caller's
 * struct snd_pcm (the sample rate) and returned success.  PLAY MP3 then
 * opened a stream that was never configured and played nothing, with no
 * error anywhere.  The numbers in this file are one flat space shared by
 * every prefix; a new one has to be checked against ALL of them, not
 * just against its own group.
 */
#define PICOIOC_BOARD 0x002C

#define PC3_LIBM_MAGIC   0x50433350UL   /* "PC3P" */
#define PC3_LIBM_VERSION 1
#define PC3_LIBM_NFN     19

/* The order IS the ABI - append only. */
enum {
	PC3_LIBM_SIN = 0, PC3_LIBM_COS,   PC3_LIBM_TAN,
	PC3_LIBM_ASIN,    PC3_LIBM_ACOS,  PC3_LIBM_ATAN,
	PC3_LIBM_SINH,    PC3_LIBM_COSH,  PC3_LIBM_TANH,
	PC3_LIBM_SQRT,    PC3_LIBM_EXP,   PC3_LIBM_LOG,
	PC3_LIBM_LOG10,   PC3_LIBM_FLOOR, PC3_LIBM_CEIL,
	PC3_LIBM_FABS,
	/* two-argument from here */
	PC3_LIBM_POW,     PC3_LIBM_ATAN2, PC3_LIBM_FMOD
};

struct pc3_libm {
	uint32_t magic;
	uint16_t version;
	uint16_t count;
	void *fn[PC3_LIBM_NFN];
};

/* MMBasic's MAP - an arbitrary colour per palette entry, where
 * GFXIOC_PAL only picks from a fixed set of physical colours.
 *
 * data = (index << 24) | rgb888, index 0-15.  Nothing changes on
 * screen: the value is held until GFXIOC_MAPCTL applies it, so a whole
 * new palette arrives at once rather than the picture recolouring in
 * instalments.  16-colour modes only. */
#define GFXIOC_MAP    0x001E

/* data: 0 = SET, apply the collected palette during blanking; 1 =
 * RESET, back to the mode's own defaults, live and pending together. */
#define GFXIOC_MAPCTL 0x001F
#define GFX_MAP_SET   0
#define GFX_MAP_RESET 1
/* Bounds one call's work, as GFX_BATCH_MAX does for the shape calls. */
#define GFX_TEXT_MAX  256
/* Scroll the write target.  data = (rows << 24) | RGB888: rows signed
 * in the top byte, positive up and negative down, and the colour the
 * vacated band is filled with in the low 24 - RGB888 like every other
 * call here, so a caller never needs to know the mode's own colour
 * numbers.
 *
 * The SAME call the console makes for its own scrolling (con_gfx_scroll
 * goes through display_gfx_scroll), so a program printing off the
 * bottom of the screen and the shell doing it are one implementation.
 * And it moves the WRITE TARGET, so a program drawing off-screen
 * scrolls its own buffer rather than the screen. */
#define GFXIOC_SCROLL 0x001B

/* Scroll the write target in BOTH axes, with wrap-around - what
 * MMBasic's SPRITE SCROLL needs and GFXIOC_SCROLL cannot do (no
 * horizontal, no wrap).  dx > 0 moves the picture right, dy > 0 moves
 * it up - the reference's own senses.  fill says what the vacated band
 * becomes: an RGB888 colour, or -1 to leave the vacated pixels holding
 * what they held (the reference's memmove residue), or -2 to wrap the
 * departing band round to the other edge.  Horizontal work is done at
 * pixel granularity whatever the packing, one row staged at a time, so
 * the cost is flash code and one row buffer, not SRAM.
 * GFXIOC_SCROLL stays: the console's own scrolling uses it. */
struct gfx_scroll2 {
	int16_t dx;
	int16_t dy;
	int32_t fill;		/* RGB888, or -1 leave, or -2 wrap */
};
#define GFXIOC_SCROLL2 0x0035

/* Block until the top of vertical blanking - MMBasic's FRAMEBUFFER WAIT,
 * and what FRAMEBUFFER COPY ...,B does first.  No argument.  Bounded: if
 * the scanout has stopped this returns rather than hanging the caller. */
#define GFXIOC_VSYNC  0x0019

/*
 * The same wait, BOUNDED, so the caller can do the waiting.
 *
 * GFXIOC_VSYNC and the wait MERGE used to do are a spin inside the
 * kernel, and this kernel does not preempt inside a syscall
 * (preempt_handler only fires for user-mode PCs).  A frame is 16.7ms;
 * holding the CPU for that long stops EVERYTHING, and what it stopped
 * was the MOD player, whose audio then ran dry between merges.
 *
 * data is the microseconds to spin for.  The return is 1 if the top of
 * blanking was reached and 0 if the budget ran out, so a caller loops:
 *
 *	while (!ioctl(fd, GFXIOC_VSYNCTRY, (void *)2000)) ;
 *
 * and between those calls it is in USER mode, where the tick can take
 * the CPU away and give it to somebody who needs it.  The kernel still
 * does the fine-grained watching - it is the only thing that can see
 * v_scanline - but it does it in slices.
 *
 * Capped at 20000us a call so this cannot become the thing it fixes.
 */
#define GFXIOC_VSYNCTRY 0x003E
/* Bounds the kernel's work per call.  A caller with more chunks; the
 * runtime does that invisibly, and it keeps one ioctl from holding the
 * cpu for an unbounded time. */
#define GFX_BATCH_MAX 1024

/* Draw a 1-bit source bitmap, scaled, with its own foreground and
 * background - MMBasic's DrawBitmap, and how text reaches the screen.
 * Both colours are RGB888; bg = -1 leaves the paper alone, which is
 * MMBasic's transparency.  The source bits are MSB-first, row-major,
 * exactly as MMBasic's fonts are packed, so they interchange. */
struct gfx_bitmap {
	int16_t x, y;
	uint8_t width, height;	/* of the SOURCE, in pixels */
	uint8_t scale;
	uint8_t pad;
	int32_t fg;		/* RGB888 */
	int32_t bg;		/* RGB888, or -1 for transparent */
	void *bits;
};
#define GFXIOC_BITMAP 0x0013
/* Bits are copied in before drawing, so the source has a ceiling: an
 * 8x12 glyph is 12 bytes and 32x32 is the largest square that fits. */
#define GFX_BITMAP_MAX 256

/* BBC sound (PC3): the SOUND statement's raw parameters; the channel
 * word carries the &1x flush and &Sxx sync bits.  Returns -1/EAGAIN
 * when that channel's note queue is full. */
struct snd_cmd {
    int16_t chan;
    int16_t amp;                /* -15..0 volume, 1-16 = ENVELOPE n */
    int16_t pitch;
    int16_t dur;                /* 20ths of a second, 255 = forever */
};
#define SNDIOC_SOUND  0x0006

/* data -> 14 bytes: N,T,PI1,PI2,PI3,PN1,PN2,PN3,AA,AD,AS,AR,ALA,ALD */
#define SNDIOC_ENV    0x0007

/* silence everything, flush all queues */
#define SNDIOC_QUIET  0x0008

/*
 * PCM streaming: play samples a process has already decoded, instead
 * of the BBC synth.  The I2S engine is one piece of hardware, so a
 * stream and SOUND/ENVELOPE are mutually exclusive - OPEN takes the
 * state machine and silences the synth, CLOSE hands it back.  MMBasic
 * behaves the same way.
 *
 * Samples are 16-bit signed, interleaved left/right if stereo; mono is
 * duplicated to both channels by the driver, so a mono file costs the
 * player nothing.  See PC3-MP3-PLAN.md.
 *
 * The usual loop is: OPEN, then WRITE whatever STAT says there is room
 * for, and at the end poll STAT until "queued" reaches zero before
 * CLOSE - CLOSE itself is immediate and drops the tail.
 */
struct snd_pcm {
	uint32_t rate;			/* 8000..48000 */
	uint16_t channels;		/* 1 or 2 */
	uint16_t bits;			/* 16; anything else is refused */
};
#define SNDIOC_PCMOPEN 0x0021	/* struct snd_pcm */

/* data -> struct snd_buf.  Returns the bytes ACCEPTED, which may be
 * fewer than asked for: the ring was full, and the player should come
 * back rather than treat it as an error. */
struct snd_buf {
	void *base;
	uint32_t len;			/* bytes, not samples */
};
#define SNDIOC_PCMWRITE 0x0022

/* data -> struct snd_stat, filled in.  underruns counts buffers the
 * IRQ had to fill with silence since OPEN; it is the objective test of
 * whether the buffering is deep enough, and the reason to prefer it to
 * "it sounded all right". */
struct snd_stat {
	uint32_t space;			/* bytes free in the ring */
	uint32_t queued;		/* bytes waiting to play */
	uint32_t underruns;
};
#define SNDIOC_PCMSTAT 0x0023

#define SNDIOC_PCMCLOSE 0x0024

/* Returns the pid playing, or 0 if the sound output is free.  There is
 * one I2S engine, so the stream belongs to one process at a time: OPEN
 * fails with EBUSY for anyone else, and WRITE and CLOSE from anyone
 * else are refused - two players sharing the ring interleaved their
 * samples and it sounded exactly as bad as that suggests.
 *
 * The pid is what makes the rule usable from outside: BASIC's PLAY STOP
 * signals it, and PLAY MP3 refuses to start when it is not zero.  A
 * player that died without closing is not counted - the kernel hands
 * the stream back when its owner is gone. */
#define SNDIOC_PCMOWNER 0x0025

/*
 * Sleep until the queue has drained to `data' bytes, so a player does
 * not have to GUESS how long to wait.
 *
 * usleep() cannot help: it rounds up to deciseconds because the timer
 * wheel does (Library/libs/usleep.c), so the shortest sleep available
 * to userland is 100ms.  A player that sleeps 100ms must therefore
 * keep more than 100ms queued - plus whatever a busy machine adds
 * before it is scheduled and has rendered - and everything queued is
 * LATENCY on the next sound effect.  playmod needed 557ms of queue to
 * stop stuttering under load, which is 557ms before a door thud is
 * heard.  The number was always a guess at a scheduling delay.
 *
 * This removes the guess.  The kernel knows exactly when the ring
 * drains, so it does the waiting and wakes the player on the TICK -
 * 5ms here (TICKSPERSEC 200), twenty times finer than a decisecond.
 * The queue can then be short, because it only has to cover 5ms of
 * granularity rather than 100ms of sleep plus scheduling.
 *
 * Woken by a signal as well, which is what PLAY STOP needs.
 * Returns 0 when there is room, -1 if the caller does not hold the
 * stream.
 *
 * 0x003F, NOT 0x0026 - which is PLKIOC_CLAIM, and the pin lock is
 * tested first, so every wait went there instead and came back as a
 * failure.  playmod then fell back to usleep and pulsed exactly as
 * before, with the queue now SHORTENED on the strength of a wait that
 * was never happening.  That is the second time a number in this file
 * has been quietly taken (see PICOIOC_BOARD above); the numbers are a
 * single flat space shared by every subsystem here, and grep before
 * choosing is the whole of the discipline.
 */
#define SNDIOC_PCMWAIT 0x003F

/* BBC ADVAL (PC3): data -> int selector, returns the reading.
 *
 *   Selectors 0 (joystick, GP34-GP37) and 1-4 (ADC, GP41-GP44) are
 *   GONE.  They were pin work on I/O header pins, and userland now
 *   claims those pins and reads them itself - <sys/pc3io.h>, with
 *   ownership through PLKIOC_CLAIM below.  They return 0 here.
 *
 *   -5..-8 sound channel 0-3 queue free slots
 *   -9     hardware microsecond counter, 31 bits in the return value
 *   -10    hardware microsecond counter, 64 bits: pass an 8-byte
 *          buffer whose low word holds the selector; the kernel
 *          writes the full value back into it and returns 0 */
#define PICOIOC_ADVAL 0x0009

/* The PSRAM arena (PC3-PSRAM-ARENA.md): a region of PSRAM outside the
 * process image.  Not context-switch copied, not swapped, not forked -
 * and not protected: the base address is raw, because with no MMU
 * there is nothing else it could be.  Released on exec and exit; a
 * fork leaves the arena with the parent. */
struct psram_req {
	uint32_t len;			/* in: bytes (4K granular) */
	uint32_t base;			/* out: address, raw */
};
struct psram_stat {
	uint32_t total;
	uint32_t free;
	uint32_t largest;
};
#define PSRAMIOC_ALLOC 0x000A		/* struct psram_req */
/* Grow or shrink: base in, the NEW base out. It may MOVE - the
 * allocator is newlib's and it copies when it cannot extend in place -
 * so a caller holding interior pointers must rebuild them. This is what
 * lets a client start small instead of guessing its maximum. */
#define PSRAMIOC_REALLOC 0x000D		/* struct psram_req */
#define PSRAMIOC_FREE  0x000B		/* uint32_t base */
#define PSRAMIOC_STAT  0x000C		/* struct psram_stat */

/*
 * Ownership of the I/O header - see pinlock.c and PC3-IO-PLAN.md.
 *
 * The point of this is what it does NOT do: it does not carry the
 * traffic.  A program that has claimed GP4 drives GP4 itself, by
 * storing to SIO, because there is no MMU on this board and never was -
 * the same reason PICOIOC_LIBM hands out kernel function pointers and
 * PSRAMIOC_ALLOC hands out a raw address.  An ordinary ioctl costs
 * 1.488us here (see GFXIOC_PIXEL above) against about ten nanoseconds
 * for the store it would be wrapping, so a syscall per pin edge is not
 * a small tax, it is the entire cost.
 *
 * What the kernel keeps is the two things userland cannot do for
 * itself: say who owns a pin, and put the pin BACK when that owner
 * dies.  A program killed halfway through driving a relay cannot
 * release it; the kernel can, and does, on exit and on exec.
 *
 * ADVISORY, like flock().  With no MMU and no MPU there is nothing to
 * enforce with - a wild pointer can still write IO_BANK0 - so this
 * stops cooperating programs colliding, which is the failure that
 * actually happens on a single-user machine, and stops nothing else.
 * Say so rather than letting the word "lock" imply protection.
 */
/* Userland has the same block in <sys/pc3io.h>, which programs include
   for the register access that goes with it; the guard lets a file
   include both in either order. */
/* One DS3231 register - MMBasic's RTC GETREG / RTC SETREG.
 *
 * This is how an ALARM is armed, because MMBasic has no alarm command:
 * write registers 0x07-0x0A with the match time, then set INTCN and
 * A1IE in the control register 0x0E, and the chip pulls its INT line
 * (GP32 on this board) low when the time comes.  SETPIN 32, INTL,
 * handler then catches it.
 *
 * A kernel call rather than /dev/i2c, which refuses 0x68 outright: this
 * chip is the system clock, so the access shares the kernel's own
 * retry-and-unwedge path and its busy flag.  One refusal is kept - a
 * write cannot set EOSC, because stopping a battery-backed oscillator
 * outlives the power cycle and the machine comes back not knowing the
 * time.  Everything else is the program's, as it is on a PicoMite. */
/* Open the SECOND I2C controller on a pair of header pins - MMBasic's
 * I2C2, which is why it needs opening at all: the fixed bus (I2C0,
 * GP20/21, the QWIIC socket) is always there and needs no OPEN, while
 * this one has no pins until a program says which.
 *
 * The pins must be an SDA/SCL pair the RP2350 can actually mux to I2C1:
 * SDA where (pin & 3) == 2 and SCL where (pin & 3) == 3.  On the PC3's
 * header that is GP38/GP39 and GP42/GP43.
 *
 * Transfers then go through the ordinary I2C_MSG ioctl on /dev/i2c with
 * bus = 1.  THAT stays a syscall on purpose: a transaction is ~300us of
 * bus time, so the 1.5us crossing is half a percent - the argument that
 * moved pin work to userland does not apply here, and the SDK's
 * controller code is proven where a hand-written one would not be.
 *
 * The pins are claimed through the pin lock, so they come back on exit
 * like any other, and the controller is released with them. */
struct i2c_open {
	uint8_t bus;			/* 1; bus 0 is the fixed one */
	uint8_t sda;
	uint8_t scl;
	uint8_t pad;
	uint32_t khz;			/* 100, 400 or 1000 */
	/*
	 * MMBasic's second OPEN argument, and it belongs HERE rather than
	 * on each transfer because that is where MMBasic keeps it:
	 * i2cEnable stores I2C_Timeout once and every transfer passes
	 * I2C_Timeout*1000 microseconds (I2C.c).  It takes 0, or 100 and
	 * up - under 100 is "Number out of bounds" there, so it is here.
	 *
	 * 0 means "no timeout" on a PicoMite.  It CANNOT mean that here.
	 * This kernel is non-preemptive, so a transfer that waits forever
	 * does not hang the program that asked for it - it hangs the
	 * machine, console and display included, with no way back but the
	 * reset button.  0 is therefore a long cap (I2C_MAX_TIMEOUT),
	 * which is the same answer for every device that is merely slow
	 * and a different one only for a bus that is already broken.
	 */
	uint16_t timeout_ms;		/* 0 = the cap, else >= 100 */
	uint16_t pad2;
};
#define PICOIOC_I2COPEN	0x002A
#define PICOIOC_I2CCLOSE 0x002B		/* uint8_t bus */

/*
 * One transfer, with MMBasic's option bits - the PC3's own, alongside
 * upstream's I2C_MSG on /dev/i2c rather than instead of it.
 *
 * Upstream's struct i2c_msg (Kernel/include/i2c.h, driven by
 * Kernel/dev/devi2c.c) is shared with every other Fuzix platform and
 * has nowhere to put a flag.  Widening it would put a PC3 idea into
 * code that goes back upstream; a portable program still uses I2C_MSG
 * and gets exactly what it always did, and only the BASIC path needs
 * this.
 *
 * I2CF_HOLD is MMBasic's option bit 0: end the transfer WITHOUT a
 * STOP, so the next one is a repeated START on the same device.  It is
 * the SDK's nostop argument, which is precisely what MMBasic passes -
 * (I2C_Status == I2C_Status_BusHold ? true : false).
 *
 * A held bus is a transaction the caller has promised to finish.  If it
 * does not - an error, or the process dies - the kernel finishes it:
 * see the recovery in i2cuser.c, which is what stops one program's
 * abandoned hold locking the bus for everyone after it.
 */
struct i2c_xfer {
	uint8_t bus;
	uint8_t addr;			/* 7-bit address << 1 | read */
	uint8_t len;
	uint8_t flags;
	uint8_t *data;
};
#define I2CF_HOLD	0x01
#define PICOIOC_I2CXFER	0x002D

/*
 * SPI0 - MMBasic's SPI, on header pins.  SPI1 is the SD card and stays
 * the kernel's, which is why only one controller is offered here.
 *
 * The RP2350 fixes which GPIO can carry which SPI signal, and the rule
 * is modular like I2C's: the INSTANCE is (pin >> 3) & 1 - 0 for SPI0,
 * 1 for SPI1 - and the ROLE is pin & 3: 0 RX, 1 SS, 2 SCLK, 3 TX.  On
 * the PC3's header that leaves SPI0 on GP0-GP7 and GP34-GP39, and every
 * SPI1 pin there (GP26, GP40, GP42-GP46) belongs to the card.  Refusing
 * the wrong pin here beats a bus that clocks nothing.
 *
 * speed, mode and bits are MMBasic's three OPEN arguments.  mode is
 * (CPOL << 1) | CPHA, which is the ordinary SPI mode number and exactly
 * what MMBasic decodes - (mode & 2) for polarity, (mode & 1) for phase.
 * bits is 4 to 16, MMBasic's own bounds, 8 unless asked.
 *
 * CHIP SELECT IS NOT HERE, deliberately: MMBasic does not drive it
 * either.  A display needs CS held across a whole command-and-data
 * sequence, not per transfer, so only the program knows when to move it
 * - and now that pins are userland it is a register write, not a call.
 */
struct spi_open {
	uint8_t bus;			/* 0; SPI1 is the SD card */
	uint8_t sck;
	uint8_t tx;			/* MOSI */
	uint8_t rx;			/* MISO */
	uint32_t hz;
	uint8_t mode;			/* 0-3 */
	uint8_t bits;			/* 4-16 */
	uint16_t pad;
};
#define PICOIOC_SPIOPEN		0x002E
#define PICOIOC_SPICLOSE	0x002F	/* uint8_t bus */

/*
 * One transfer.  tx or rx may be NULL: tx alone writes, rx alone reads
 * (clocking zeros out, as MMBasic's SPI READ does), both together is
 * the write-and-read the SPI() function needs.  len counts UNITS, not
 * bytes - a unit is 16 bits when bits > 8, which is the width
 * spi_set_format was given.
 *
 * NO BOUNCE BUFFER, unlike the I2C path.  That one copies through 64
 * bytes because it is upstream's shared driver; here a transfer is a
 * whole display row or a whole frame - 153,600 bytes for 240x320 at 16
 * bits - and copying it through the kernel would need memory this
 * machine does not have.  There is no MMU, so after valaddr says the
 * range is the caller's, the controller reads it where it lies.  The
 * kernel is non-preemptive, so nothing can move it meanwhile.
 */
struct spi_xfer {
	uint8_t bus;
	uint8_t pad[3];
	uint32_t len;			/* units */
	uint8_t *tx;			/* NULL to read only */
	uint8_t *rx;			/* NULL to write only */
};
#define PICOIOC_SPIXFER		0x0030

struct rtc_reg {
	uint8_t reg;			/* 0-255 */
	uint8_t val;			/* in for a write, out for a read */
	uint8_t write;			/* 0 = read, 1 = write */
	uint8_t pad;
};
#define PICOIOC_RTCREG	0x0029

#ifndef PC3_PINLOCK_ABI
#define PC3_PINLOCK_ABI

struct pinlock_req {
	uint8_t cls;			/* PLK_* below */
	uint8_t idx;			/* pin, controller, or slice number */
	uint8_t flags;			/* reserved, must be zero */
	uint8_t pad;
};

/* The class is part of the name because a pin is not the only thing two
 * programs can collide over.  Twelve PWM slices cover forty-eight pins,
 * so two processes on DIFFERENT header pins can land on the same slice
 * and fight over its wrap and divider; there is one ADC for all the
 * channels; and a controller can be muxed to more than one pin pair.
 * Locking pins alone would look right and still let all three through. */
#define PLK_PIN		0		/* idx = GPIO number */
#define PLK_I2C		1		/* idx = controller: 1 only, see below */
#define PLK_SPI		2		/* idx = controller: 0 only, see below */
#define PLK_PWM		3		/* idx = slice 0-11 */
#define PLK_ADC		4		/* idx = 0, the one converter */
#define PLK_PIO		5		/* refused for now */
#define PLK_DMA		6		/* refused for now */

/* What may be claimed, and why the rest may not:
 *
 *   pins    GP0-GP7, GP26, GP34-GP46 - the I/O header.  Everything else
 *           belongs to the board (display, SD, PSRAM/QMI, I2S, UART,
 *           the DS3231) and handing it out would be a way to hang the
 *           machine from a BASIC program.
 *   I2C     controller 1 only.  I2C0 is GP20/21 - the QWIIC socket AND
 *           the DS3231 - and stays behind /dev/i2c, which arbitrates it
 *           against the RTC poll in interrupt context.
 *   SPI     controller 0 only.  SPI1 is the SD card.
 *   PWM     all twelve slices; the kernel uses none.
 *   ADC     the converter.  BBC BASIC's ADVAL shares it and will be
 *           brought under this scheme rather than left to race.
 *   PIO/DMA refused: the display and the sound engine hold state
 *           machines and channels, and which ones has to be surveyed
 *           before userland can be told any are free.  An honest EINVAL
 *           beats a claim that appears to work.
 */
#define PLKIOC_CLAIM	0x0026		/* struct pinlock_req */
#define PLKIOC_RELEASE	0x0027		/* struct pinlock_req */
/* Returns the pid holding it, or 0 if free - SNDIOC_PCMOWNER's shape,
 * and for the same reason: "who has GP4" is answerable from a shell. */
#define PLKIOC_OWNER	0x0028		/* struct pinlock_req */

#endif	/* PC3_PINLOCK_ABI */

/*
 * Counting inputs - SETPIN FIN / CIN / PER (mmb2c's PLAN-count.md,
 * countpin.c here).
 *
 * These are /dev/gpio codes, NOT /dev/sys ones: they extend the GPIOC_
 * block from Kernel/include/gpio.h (0x0530-0x0536 upstream) and are
 * dispatched by devgpio.c to countpin.c.  They live here rather than in
 * the shared kernel header because the facility is this platform's, and
 * because ioctlcheck.sh reads this file - the flat space it polices is
 * /dev/sys's, but a number unique across BOTH devices is cheap and one
 * table beats two.
 *
 * The count pins are FIXED: GP4-GP7, MMBasic's INT1-INT4 in order.
 * arg is the FIN gate in ms (1..100000), the PER cycles to average
 * (1..10000), or the CIN option (1..10 - MMBasic's edge/pull table:
 * 2 falling, >=3 both edges; 1/4 pull-down, 2/5 pull-up).  READ fills
 * val with the LIVE count (CIN) or the last completed gate's latched
 * value (FIN/PER); SET stores val into the live count, CIN only, any
 * value - both exactly as MMBasic's Pin() behaves.
 *
 * The caller must hold the pin's PLK_PIN claim.  Counting is the one
 * pin mode where the kernel holds state and an interrupt, so here the
 * advisory lock is enforced, and pinlock's death-sweep is what
 * guarantees a killed program's count IRQ dies with it.
 *
 * Guarded like PC3_PINLOCK_ABI above and for the same reason: a
 * userland program includes <sys/pc3io.h> AND this file, and both
 * carry the ABI.
 */
#ifndef PC3_COUNT_ABI
#define PC3_COUNT_ABI

struct cntreq {
	uint8_t pin;		/* GPIO number, 4..7 */
	uint8_t pad1;
	uint16_t pad2;
	int32_t arg;
	int64_t val;
};

#define GPIOC_CNT_FIN	0x0537		/* struct cntreq: frequency input */
#define GPIOC_CNT_CIN	0x0538		/* struct cntreq: counting input */
#define GPIOC_CNT_PER	0x0539		/* struct cntreq: period input */
#define GPIOC_CNT_READ	0x053A		/* struct cntreq: val out */
#define GPIOC_CNT_SET	0x053B		/* struct cntreq: val in, CIN only */
#define GPIOC_CNT_OFF	0x053C		/* struct cntreq: stop counting */

#endif	/* PC3_COUNT_ABI */

/*
 * CYW43 Wi-Fi (PC3_NET builds).  PC3-NET-PLAN.md.
 *
 * Association has no place in Fuzix's own network ioctls - those are
 * the BSD SIOCxIF* set in netdev.h and they describe an interface that
 * is already up - so joining a network lives here with the rest of the
 * platform's private calls, the way sound and i2c do.
 *
 * NETIOC_UP is asynchronous.  It powers the radio and starts the join;
 * it does NOT wait for association or for DHCP, because a Fuzix
 * syscall is not preempted and a blocking join would stop the machine
 * for the length of a DHCP negotiation.  Userland polls NETIOC_STATUS.
 *
 * The one thing NETIOC_UP does block for is the first call's firmware
 * upload - about 230K over the PIO SPI link, a few hundred ms, once
 * per boot.
 */
struct net_join {
	char ssid[33];		/* NUL terminated */
	char key[65];		/* NUL terminated; empty for an open net */
	uint8_t auth;		/* 0 open, 1 WPA-TKIP, 2 WPA2-AES, 3 mixed */
	uint8_t pad[3];
};

struct net_status {
	uint8_t present;	/* radio fitted (0 on a Pico Computer 2) */
	uint8_t ready;		/* driver initialised */
	uint8_t link;		/* cyw43_tcpip_link_status: 3 = has an IP */
	int8_t wifi;		/* cyw43_wifi_link_status; negative is a fault */
	uint8_t mac[6];
	uint8_t pad[2];
	uint32_t ip;		/* host order */
	uint32_t mask;
	uint32_t gw;
	uint32_t dns[2];	/* from the DHCP lease; 0 if none */
	int32_t rssi;		/* dBm, 0 if unknown */
};

#define NETIOC_UP	0x0040		/* struct net_join */
#define NETIOC_STATUS	0x0041		/* struct net_status */
#define NETIOC_DOWN	0x0042		/* no argument */

/*
 * Load the machine's CA bundle, PEM (NUL terminated) or DER.  The
 * kernel cannot read files, so userland reads it and passes the bytes;
 * on this platform user memory is directly addressable, so what goes
 * across is a pointer valaddr has checked, not a copy.
 *
 * Until this has been done, a TLS session is ENCRYPTED BUT NOT
 * AUTHENTICATED - the peer can present any certificate it likes.  That
 * is the same state MMBasic starts in before WEB TLS CA runs, and it
 * is worth saying out loud rather than implying otherwise.
 */
struct net_ca {
	void *buf;
	uint32_t len;
};

#define NETIOC_TLSCA	0x0043		/* struct net_ca */

/* net_cyw43.c returns these rather than setting udata.u_error itself:
 * it cannot include the kernel headers (see the file comment), so
 * misc.c does the translation.  Kernel side only. */
#define PC3_NET_ENODEV	(-1)		/* no radio - a Pico Computer 2 */
#define PC3_NET_EIO	(-2)		/* the driver said no */
#define PC3_NET_EINVAL	(-3)


#endif
