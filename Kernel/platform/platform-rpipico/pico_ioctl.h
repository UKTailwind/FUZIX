#ifndef PICO_IOCTL_H
#define PICO_IOCTL_H

#include <stdint.h>

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
#define GFXIOC_FBOPEN 0x0018		/* int: 1 claim the layer, 0 release */
#define GFXIOC_FBSEL  0x0016		/* int: 0 screen, 1 layer */
#define GFXIOC_FBCOPY 0x0017		/* int: 0 layer->screen, 1 screen->layer */

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

/* The shared maths library.  data -> a void * that receives the
 * address of the table below; from there a program CALLS the entries
 * directly, because there is no MMU here and kernel flash is in the
 * same address space.  See libm_table.c for why, and for the errno
 * contract - these do not report domain errors.
 *
 * Check the magic and the version before using it.  An old binary on a
 * new kernel must fail rather than call the wrong slot. */
#define PICOIOC_LIBM  0x0020

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

/* Block until the top of vertical blanking - MMBasic's FRAMEBUFFER WAIT,
 * and what FRAMEBUFFER COPY ...,B does first.  No argument.  Bounded: if
 * the scanout has stopped this returns rather than hanging the caller. */
#define GFXIOC_VSYNC  0x0019
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

/* BBC ADVAL (PC3): data -> int selector, returns the reading.
 *   0      joystick switches GP34-37 (pulled up, active low),
 *          pressed = 1: bit0 GP34, bit1 GP35, bit2 GP36, bit3 GP37
 *   1-4    ADC GP41-GP44, 16-bit (12-bit ADC << 4)
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

#endif
