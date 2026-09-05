/*
 * Pico Computer 3 display for Fuzix - the PORTABLE half.
 *
 * Everything in this file works on plain byte buffers: which buffer the
 * drawing primitives write to and who owns it, the mode and palette
 * state, the RGB888 colour model, and every primitive from a pixel to a
 * run of text.  Nothing here touches the HSTX, the DMA, core1 or the
 * memory map, so the same file compiles into the kernel and into a host
 * program that stands in for it (the PC3 device server).
 *
 * What the hardware has to do for us is named in display_priv.h and
 * implemented in display_hstx.c: choose and restart the raster, hand a
 * new mode to the scanout, rebuild the expansion table when the palette
 * changes, wait for blanking, and say where the framebuffers live.
 * display.h is unchanged - the rest of the kernel does not know the
 * file was split.
 *
 * The split is the same one ports/rp2/hdmi.c and hdmi_rp2.c made in the
 * MicroPython port, for the same reason: one master copy of the drawing
 * code, run by two machines.
 */

#include <stdint.h>
#include <string.h>
#include "display.h"
#include "display_priv.h"
/* struct gfx_pt / gfx_rc: the batched drawing items are part of the
 * userland interface, so their definition lives with the ioctls. */
#include "pico_ioctl.h"

/*
 * Where the DRAWING primitives write.  Scanout is not switchable and
 * never looks at this: core1 always DMAs out of disp_fb, so pointing
 * this at the layer is exactly MMBasic's "FRAMEBUFFER WRITE F" - the
 * picture is built off-screen and appears on the COPY.
 *
 * It is DERIVED state, recomputed by display_fb_enter() at every
 * graphics ioctl from who is calling.  The truth is the two variables
 * below: which process holds the layer, and whether it is currently
 * drawing into it.  Anyone else - another program, the console's own
 * repaint - gets the screen, whatever the holder last asked for.
 */
/*
 * ONE BYTE holds which buffers exist and which is selected, and that is
 * not premature tidiness: adding two plain flags here overflowed the
 * kernel's RAM region BY EIGHT BYTES and would not link.  There is that
 * little SRAM left, which is the same fact - seen from the other side -
 * that made a scanout-time layer impossible.
 *
 *   bit 0  F exists      bits 4-5  the selected target, DISP_FB_N/F/L
 *   bit 1  layer exists
 */
#define FBS_HAVE_F	0x01
#define FBS_HAVE_L	0x02
#define FBS_SEL_SHIFT	4
#define FBS_SEL_MASK	0x30

static struct p_tab *fb_owner;          /* NULL = both buffers are free */
static uint8_t fb_state;
static uint8_t *gfx_draw = disp_fb;

static int fb_have(int which)
{
    return fb_state & (which == DISP_FB_L ? FBS_HAVE_L : FBS_HAVE_F);
}

static int fb_selected(void)
{
    return (fb_state & FBS_SEL_MASK) >> FBS_SEL_SHIFT;
}

static void fb_set_sel(int which)
{
    fb_state = (uint8_t)((fb_state & ~FBS_SEL_MASK) |
                         ((which << FBS_SEL_SHIFT) & FBS_SEL_MASK));
}

/* The buffer a target names, or NULL if the owner has not created it. */
static uint8_t *fb_buf(int which)
{
    if (which == DISP_FB_F)
        return fb_have(DISP_FB_F) ? disp_fb2 : NULL;
    if (which == DISP_FB_L)
        return fb_have(DISP_FB_L) ? disp_fb3 : NULL;
    return disp_fb;
}

/*
 * A CHILD of the owner draws where the owner draws.
 *
 * On a PicoMite LOAD IMAGE is interpreter code and lands in whatever
 * FRAMEBUFFER WRITE selected.  Here it is a separate binary, and so are
 * SAVE IMAGE and the LOAD JPG/LOAD PNG to come, because the decoders are
 * far too big to carry in every compiled program.  Without this the
 * target does not cross the fork: a program drawing into F loads its
 * picture onto the SCREEN instead, where the next MERGE promptly wipes
 * it - which is exactly what PETSCII Robots' title screen did.
 *
 * The owner is blocked in wait() for that child, so there are never two
 * writers; and the exclusion the target exists for still holds, because
 * an UNRELATED program - or the console repainting - is not a child of
 * the owner and still gets the screen.
 */
void display_fb_enter(struct p_tab *who)
{
    uint8_t *t = NULL;

    if (fb_owner && fb_selected() != DISP_FB_N &&
        (fb_owner == who || (who && disp_who_parent(who) == fb_owner)))
        t = fb_buf(fb_selected());
    gfx_draw = t ? t : disp_fb;
}

uint8_t *display_fb_target(void)
{
    return gfx_draw;
}

/*
 * Claim or release the layer - MMBasic's FRAMEBUFFER CREATE and CLOSE F.
 * There is one layer, so a second claimant is told so (-2) rather than
 * quietly sharing a buffer with another program.  -1 means this board
 * has no PSRAM to put one in.
 */
int display_fb_open(struct p_tab *who, int claim, int which)
{
    if (which != DISP_FB_F && which != DISP_FB_L)
        return -1;
    if (!claim) {
        /* Closing one buffer leaves the other; the process only stops
         * being the owner when it has neither.  MMBasic's CLOSE F and
         * CLOSE L are separate for the same reason. */
        if (fb_owner != who)
            return 0;
        fb_state &= (uint8_t)~(which == DISP_FB_L ? FBS_HAVE_L : FBS_HAVE_F);
        if (fb_selected() == which)
            fb_set_sel(DISP_FB_N);
        if (!(fb_state & (FBS_HAVE_F | FBS_HAVE_L)))
            fb_owner = NULL;
        display_fb_enter(who);
        return 0;
    }
    if (which == DISP_FB_F ? !display_fb2_ok() : !display_fb3_ok())
        return -1;
    if (fb_owner && fb_owner != who)
        return -2;
    fb_owner = who;
    fb_state |= (uint8_t)(which == DISP_FB_L ? FBS_HAVE_L : FBS_HAVE_F);
    return 0;
}

void display_fb_release(struct p_tab *who)
{
    if (fb_owner == who) {
        fb_owner = NULL;
        fb_state = 0;
        gfx_draw = disp_fb;
    }
}

/* DISP_FB_N/F/L.  Only the holder may select an off-screen buffer, and
 * only one it has created; anyone else gets -1 rather than silently
 * drawing nowhere. */
int display_fb_select(struct p_tab *who, int which)
{
    if (which == DISP_FB_N) {
        if (fb_owner == who)
            fb_set_sel(DISP_FB_N);
        gfx_draw = disp_fb;
        return 0;
    }
    if (fb_owner != who || fb_buf(which) == NULL)
        return -1;
    fb_set_sel(which);
    gfx_draw = fb_buf(which);
    return 0;
}

/* Blit between the layer and the screen - the whole live framebuffer,
 * which is stride*rows for a graphics mode and the console's own size
 * otherwise.  One memcpy: the layer holds the mode's own layout, so
 * there is no conversion.  MMBasic's FRAMEBUFFER COPY, whose source and
 * destination we can offer both ways round for the same memcpy. */
int display_fb_copy(struct p_tab *who, int src, int dst)
{
    int n = display_gfx_fbsize();
    uint8_t *s, *d;

    if (src == dst)
        return 0;
    /* The screen needs no claim - anyone may copy to or from what is on
     * it - but an off-screen buffer must be one this caller created. */
    if ((src != DISP_FB_N || dst != DISP_FB_N) && fb_owner != who)
        return -1;
    s = fb_buf(src);
    d = fb_buf(dst);
    if (s == NULL || d == NULL)
        return -1;
    if (n <= 0 || n > DISP_FB_POOL)
        n = DISP_FB_POOL;
    memcpy(d, s, (unsigned)n);
    return 0;
}

/*
 * MERGE - the layer over F, onto the screen.
 *
 * MMBasic's merge_scanline (FrameBuffer.c:823), transcribed.  The
 * buffers are packed 4bpp, two pixels to a byte, on both machines, so
 * the keying is PER NIBBLE and the transparent index has to be tested
 * in each half separately:
 *
 *     top    = src & 0xF0     transparent when it equals colour << 4
 *     bottom = src & 0x0F     transparent when it equals colour
 *
 * MMBasic's whole-byte early-out is kept and matters more here than
 * there: an empty layer is 38,400 bytes of "both nibbles transparent",
 * and skipping those is the difference between a merge costing what a
 * copy costs and costing several times more.
 *
 * NEITHER SOURCE IS MODIFIED, which is MMBasic's behaviour: it copies
 * F's line into a batch buffer, merges the layer over that, and pushes
 * the batch to the panel.  Here the screen IS the panel, so the result
 * lands in disp_fb directly and the two off-screen buffers are left
 * alone - a program can merge repeatedly with a moving layer over a
 * fixed background without redrawing the background.
 *
 * IT WAITS FOR VERTICAL BLANKING FIRST, always, because the thing it
 * writes into is the buffer core1 is DMAing to the display.  Writing it
 * while the scanout is part way down means the top of the screen shows
 * the new picture and the bottom the old one - tearing, once per merge.
 * Starting at the top of blanking gives the whole blanking interval as
 * a head start, and the merge is a couple of milliseconds against a
 * 16.7 ms frame, so the scanout never catches it.
 *
 * Unconditional rather than an option: a merge is by definition going
 * to the visible screen, so there is no case where tearing is what the
 * program wanted.  FRAMEBUFFER COPY keeps its explicit B for the same
 * job, because a copy might be going anywhere.
 */
/*	The live depth, 1 or 4.  A one-line accessor because gfx_bpp and
 *	gfx_exp are defined further down the file, with the expanders they
 *	belong to, and the framebuffer machinery is up here. */
static int gfx_cur_bpp(void);

int display_fb_merge(struct p_tab *who, int colour)
{
    int n = display_gfx_fbsize();
    int bpp = gfx_cur_bpp();
    uint8_t hi, lo, clear;
    const uint8_t *l;
    uint8_t *o = disp_fb;
    int i, w;

    /*	THE DEPTH DECIDES HOW A PIXEL IS KEYED, and there are two.
     *	Modes 0 and 3 are 1bpp - eight pixels to a byte, 640 across -
     *	and the console is 1bpp as well; modes 1, 2, 4, 5 and 7 are 4bpp,
     *	two pixels to a byte.  Keying nibbles in a 1bpp mode would treat
     *	eight pixels as two four-bit numbers and produce nonsense, which
     *	is what the first version of this did. */
    if (colour < 0 || colour > (bpp == 1 ? 1 : 15))
        return -1;
    if (fb_owner != who || !fb_have(DISP_FB_F) || !fb_have(DISP_FB_L))
        return -1;
    l = disp_fb3;
    if (n <= 0 || n > DISP_FB_POOL)
        n = DISP_FB_POOL;
    hi = (uint8_t)(colour << 4);
    lo = (uint8_t)colour;
    clear = (uint8_t)(hi | lo);

    /*
     * TWO PASSES, ONE PSRAM STREAM EACH, and that is the whole design.
     *
     * The obvious loop reads a byte of F and a byte of L for every byte
     * of output.  Both are in PSRAM, reached through the QMI and its
     * XIP cache, so that is TWO interleaved streams 38,400 bytes apart
     * competing for one small cache - each evicting the other's lines,
     * every byte.
     *
     * So F goes down first as a straight copy, which is one linear read
     * and what the hardware is good at (0.726 ms measured for this size,
     * 53 MB/s).  Then the layer is streamed once, also linearly, and
     * only the bytes that are not transparent write over what is now in
     * SRAM.  The destination read in the second pass is disp_fb - SRAM,
     * 3.7x faster than PSRAM and not in the cache's way at all.
     *
     * The whole-byte test is a single compare against (colour<<4)|colour
     * and skips the read-modify-write entirely, so an empty layer costs
     * one PSRAM stream and no writes.
     */
    /*
     * NO WAIT HERE ANY MORE.  This used to spin for the top of blanking
     * before compositing, which is up to a whole 16.7ms frame inside a
     * syscall - and this kernel does not preempt inside one, so it
     * stopped every other process for that long.  With a MOD player
     * running, that was audible: its queue drained between merges.
     *
     * The waiting now belongs to the caller, in slices, through
     * GFXIOC_VSYNCTRY - which does the identical spin but bounded, so
     * userland comes back to preemptible ground between tries.  The
     * runtime does it before calling this (mm_fb_merge_hw), so the
     * picture still lands at the top of blanking; what changed is who
     * is holding the CPU while it waits.
     */
    memcpy(o, disp_fb2, (unsigned)n);

    w = n & ~3;                 /* the word-aligned part; both are 4-aligned */

    if (bpp == 1) {
        /*
         * ONE BIT A PIXEL: a bit is transparent or it is not, so the
         * whole byte is a single boolean operation and there is nothing
         * to test per pixel.
         *
         *   transparent 0 - an opaque pixel is a 1 bit, so OR them in
         *   transparent 1 - an opaque pixel is a 0 bit, so AND them
         *
         * 32 pixels a word, which makes this the cheapest case by far.
         */
        const uint32_t *ls = (const uint32_t *)(const void *)l;
        uint32_t *os = (uint32_t *)(void *)o;

        if (colour) {
            for (i = 0; i < w / 4; i++)
                os[i] &= ls[i];
            for (i = w; i < n; i++)
                o[i] &= l[i];
        } else {
            for (i = 0; i < w / 4; i++)
                os[i] |= ls[i];
            for (i = w; i < n; i++)
                o[i] |= l[i];
        }
        return 0;
    }

    /*
     * FOUR BITS A PIXEL: MMBasic's per-nibble rule, but tested a WORD
     * at a time first.  Its own early-out is per byte; four bytes is
     * eight pixels, and a layer is mostly transparent almost by
     * definition - that is what a layer is for - so most words are the
     * transparent pattern repeated and skip in one compare and one
     * branch rather than four.  A mixed word falls through to the byte
     * loop, which is MMBasic's code.
     */
    {
        const uint32_t *ls = (const uint32_t *)(const void *)l;
        uint32_t clearw = (uint32_t)clear * 0x01010101u;

        for (i = 0; i < w; i += 4) {
            int k;

            if (ls[i >> 2] == clearw)
                continue;       /* eight transparent pixels: F stands */
            for (k = i; k < i + 4; k++) {
                uint8_t s = l[k], top, bot, d;

                if (s == clear)
                    continue;
                top = s & 0xF0;
                bot = s & 0x0F;
                d = o[k];
                o[k] = (uint8_t)(((top != hi) ? top : (d & 0xF0)) |
                                 ((bot != lo) ? bot : (d & 0x0F)));
            }
        }
        for (i = w; i < n; i++) {
            uint8_t s = l[i], top, bot, d;

            if (s == clear)
                continue;
            top = s & 0xF0;
            bot = s & 0x0F;
            d = o[i];
            o[i] = (uint8_t)(((top != hi) ? top : (d & 0xF0)) |
                             ((bot != lo) ? bot : (d & 0x0F)));
        }
    }
    return 0;
}
uint8_t disp_tile_fg[DISP_ROWS * DISP_COLS];
uint8_t disp_tile_bg[DISP_ROWS * DISP_COLS];


/* --- BBC graphics state -------------------------------------------------- */
volatile enum gexp gfx_exp = EXP_CONSOLE;
uint8_t gfx_pal[16];
/* MMBasic's remap332: MAP(n)=c collects here and MAP SET moves the lot
 * across in one go, during blanking.  The split is the whole point -
 * writing the live table entry by entry recolours the picture in
 * instalments, and a fade done that way is visibly wrong. */
static uint8_t gfx_pal_pending[16];
static uint16_t gfx_stride;                 /* source bytes per mode line */
static uint16_t gfx_rows;                   /* source lines: 256, or 240 */
static uint8_t gfx_mode_now = 0xFF;         /* the mode number as asked for */

/* Physical colours in RGB332.  0-7 are the authentic BBC set, and are
 * all that modes 0-5 can reach (the real 8-15 flash, which we do not
 * do, so those map to their steady counterparts).  MODE 7 is our own
 * mode, not teletext, and uses all 16: 8-15 are a darker companion set
 * so its 16 logical colours are 16 DISTINCT colours by default. */
static uint8_t bbc_rgb332[16] = {
    0x00, 0xE0, 0x1C, 0xFC, 0x03, 0xE3, 0x1F, 0xFF,
    0x6D, 0x60, 0x0C, 0x6C, 0x01, 0x61, 0x0D, 0x24
};

/*
 * MODE 7 is what a translated MMBasic program draws in, and MMBasic's
 * own 4bpp screen is RGB121: one bit of red, two of green, one of
 * blue, so its sixteen colours are the corners of a regular cube.
 *
 * These are MMBasic's HDMI defaults exactly - RGB332(MAP16DEF[i]) from
 * HDMI.c, which is what mapreset() loads.  That is the right reference
 * and its VGA table is not: there the sixteen values only pick a slot,
 * because the colour itself is fixed by the output resistors.  Here, as
 * on MMBasic's HDMI, the byte IS the colour that goes on the wire.
 *
 * The four with mid green - 4, 5, 12 and 13 - are the ones that make
 * the difference: MAP16DEF uses 0x55 and 0xAA for the two middle green
 * levels, which truncate to 2 and 5, where a 0/64/128/255 ramp gives 2
 * and 4.  Those entries used to be a shade dark against the
 * interpreter on the same chip.
 */
static uint8_t rgb121_rgb332[16] = {
    0x00, 0x03, 0x08, 0x0B, 0x14, 0x17, 0x1C, 0x1F,
    0xE0, 0xE3, 0xE8, 0xEB, 0xF4, 0xF7, 0xFC, 0xFF
};

/* Mask applied to a physical colour number in this expander: MODE 7
 * reaches all 16, every BBC mode only the authentic 8. */
static uint8_t gfx_physmask(enum gexp ex)
{
    return (ex == EXP_4BPP_X2) ? 15 : 7;
}

static int gfx_bpp(enum gexp ex)
{
    return (ex == EXP_CONSOLE || ex == EXP_1BPP_5TO8) ? 1 : 4;
}

static int gfx_cur_bpp(void)
{
    return gfx_bpp(gfx_exp);
}

/* Enter a graphics mode - BBC 0-5, or MODE 7 (320x240, 16 colours) -
 * or 0xFF back to the text console.  Returns the framebuffer size, or
 * -1 for a mode we do not have.
 *
 * The scanout is only torn down and rebuilt when the underlying RASTER
 * changes, because that is the only thing the monitor can see: HSTX,
 * clk_hstx, the sync command lists and the DMA chain all belong to the
 * raster, not to the mode.  Modes 0-5 share 1024x768 and the console
 * and MODE 7 share 640x480, so every switch WITHIN either group is
 * done live during vertical blanking with core1 still running - the
 * monitor keeps lock and only the picture changes.  This is exactly
 * how MMBasic splits setmode() from restartHDMI(). */
int display_gfx_mode(int mode)
{
    extern void console_gfx(int active);    /* console.c */
    /* Default logical -> physical palettes, indexed by the pal column
     * below.  The BBC modes reach physical 0-7; MODE 7 reaches 16. */
    static const uint8_t defpal[4][16] = {
        /* 0: modes 0/3 - black, white */
        { 0, 7, 0, 7, 0, 7, 0, 7, 0, 7, 0, 7, 0, 7, 0, 7 },
        /* 1: modes 1/4 - black, red, yellow, white */
        { 0, 1, 3, 7, 0, 1, 3, 7, 0, 1, 3, 7, 0, 1, 3, 7 },
        /* 2: modes 2/5 - the full BBC set, twice */
        { 0, 1, 2, 3, 4, 5, 6, 7, 0, 1, 2, 3, 4, 5, 6, 7 },
        /* 3: mode 7 - 16 distinct colours, BBC-authentic in the low 8 */
        { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
    };
    int raster;
    enum gexp exp;
    uint16_t stride, rows;
    int pal, i;
    int rebuild;

    display_stack_check();

    switch (mode) {
    case 0: case 3:
        exp = EXP_1BPP_5TO8; stride = 80;  rows = 256;
        raster = DISP_RASTER_XGA; pal = 0;
        break;
    case 1: case 4:
        exp = EXP_4BPP_X3;   stride = 160; rows = 256;
        raster = DISP_RASTER_XGA; pal = 1;
        break;
    case 2: case 5:
        exp = EXP_4BPP_X6;   stride = 80;  rows = 256;
        raster = DISP_RASTER_XGA; pal = 2;
        break;
    case 7:
        exp = EXP_4BPP_X2;   stride = 160; rows = 240;
        raster = DISP_RASTER_VGA; pal = 3;
        break;
    case 0xFF:
        exp = EXP_CONSOLE;   stride = 0;   rows = 0;
        raster = DISP_RASTER_VGA; pal = -1;
        break;
    default:
        return -1;
    }

    /* The layer holds a picture in the OLD mode's geometry, and nothing
     * converts it - so it does not survive the change.  MMBasic's
     * setmode() opens with closeframebuffer('A') for the same reason;
     * this is that, and it is why a program creates its framebuffer
     * after choosing its mode, never before. */
    fb_owner = NULL;
    fb_state = 0;               /* both buffers and the selection */
    gfx_draw = disp_fb;

    rebuild = disp_hw_mode_prepare(raster);

    gfx_stride = stride;
    gfx_rows = rows;
    gfx_mode_now = (uint8_t)mode;

    if (exp == EXP_CONSOLE) {
        /* Blank the whole pool first: on a live switch core1 is still
         * scanning it out, and the old graphics picture would show for
         * a frame under the console expander before the repaint. */
        memset(disp_fb, 0, DISP_FB_POOL);
        gfx_exp = exp;
        disp_hw_mode_handover(exp, raster);
        console_gfx(0);         /* clears and repaints the console */
    } else {
        for (i = 0; i < 16; i++)
            gfx_pal[i] = gfx_pal_pending[i] = (exp == EXP_4BPP_X2)
                ? rgb121_rgb332[defpal[pal][i] & 15]
                : bbc_rgb332[defpal[pal][i] & gfx_physmask(exp)];
        /* Palette, table and framebuffer all ready BEFORE core1 is
         * told to use them - gfx_exp is the handover, and nothing the
         * expanders read may still be stale when it changes. */
        disp_hw_mode_tables(exp);
        memset(disp_fb, 0, (int)stride * rows);
        gfx_exp = exp;
        disp_hw_mode_handover(exp, raster);
        /* AFTER the handover, deliberately: the console now renders
         * into the graphics framebuffer rather than falling silent, so
         * it has to read the geometry of the mode being entered - and
         * display_gfx_geom() answers for gfx_exp. Called before this,
         * it sized the terminal from the mode we were leaving. */
        console_gfx(1);
    }

    disp_hw_mode_finish(rebuild);
    return (int)stride * rows;
}

/* Set logical colour -> physical colour.  Modes 0-5 take the authentic
 * BBC 0-7 (8-15 flash on real hardware; here they map steady); MODE 7
 * takes all 16, from the RGB121 set it defaults to - so that physical
 * colour n means the same thing before and after a change. */
void display_gfx_pal(uint8_t logical, uint8_t physical)
{
    enum gexp ex = gfx_exp;

    gfx_pal[logical & 15] = gfx_pal_pending[logical & 15] =
        (ex == EXP_4BPP_X2)
        ? rgb121_rgb332[physical & 15]
        : bbc_rgb332[physical & gfx_physmask(ex)];
    disp_hw_palette_changed(ex);
}

/*
 * MMBasic's MAP - an arbitrary colour per palette entry, rather than
 * VDU19's choice from a fixed set.
 *
 * MAP(n) = colour collects into gfx_pal_pending and changes nothing;
 * MAP SET moves the whole palette across during blanking.  MMBasic
 * splits it the same way (remap332 -> map16quads on SET, after
 * `while (v_scanline != 0)`), and for the same reason: a fade or a
 * cycle applied entry by entry to the live table shows the picture
 * half recoloured.
 *
 * The stored value is RGB332 because that is what the scanout emits -
 * gfx_pal IS the byte core1 puts on the wire - so this is MMBasic's own
 * RGB332(): the top three bits of red and green and the top two of
 * blue, truncated, not rounded.
 *
 * 16-colour modes only, as MMBasic allows it only in SCREENMODE2/3/5.
 * The 1bpp modes have no palette to speak of and the console's tiles
 * are a different mechanism entirely.
 */
int display_gfx_remap(int index, uint32_t rgb888)
{
    if (gfx_bpp(gfx_exp) != 4 || index < 0 || index > 15)
        return -1;
    gfx_pal_pending[index] = (uint8_t)((rgb888 >> 16 & 0xE0) |
                                       (rgb888 >> 11 & 0x1C) |
                                       (rgb888 >> 6 & 0x03));
    return 0;
}

int display_gfx_remap_apply(void)
{
    int i;

    if (gfx_bpp(gfx_exp) != 4)
        return -1;
    /* Blanking first: the LUT rebuild writes the table core1 is reading
     * a scanline at a time, and doing that mid-frame tears the colours
     * across the picture. */
    display_wait_vblank();
    for (i = 0; i < 16; i++)
        gfx_pal[i] = gfx_pal_pending[i];
    disp_hw_palette_changed(gfx_exp);
    return 0;
}

/* MAP RESET - back to the mode's own defaults, live and pending alike,
 * which is what MMBasic's mapreset() does to map and remap together. */
int display_gfx_remap_reset(void)
{
    enum gexp ex = gfx_exp;
    int i;

    if (gfx_bpp(ex) != 4)
        return -1;
    display_wait_vblank();
    for (i = 0; i < 16; i++)
        gfx_pal[i] = gfx_pal_pending[i] = (ex == EXP_4BPP_X2)
            ? rgb121_rgb332[i]
            : bbc_rgb332[i & gfx_physmask(ex)];
    disp_hw_palette_changed(ex);
    return 0;
}

/* Current graphics framebuffer size (0 = console mode: the geometry is
 * zeroed on the way back, so this needs no special case). */
int display_gfx_size(void)
{
    return (int)gfx_stride * gfx_rows;
}

/* Size of the DRAWABLE framebuffer, which is not the same thing: the
 * console is drawable (640x480 1bpp = 38400 bytes) but its graphics
 * geometry is zeroed, so display_gfx_size() reports 0 for it.  BLIT
 * must use this, or it rejects every write to the text console. */
int display_gfx_fbsize(void)
{
    if (gfx_exp == EXP_CONSOLE)
        return DISP_STRIDE * DISP_HEIGHT;
    return (int)gfx_stride * gfx_rows;
}

/* --- drawing primitives -------------------------------------------------- */
/* Width and depth of the live mode.  The console is drawable too: it is
 * a 640x480 1bpp bitmap, with colour coming from the per-cell tiles. */
static int gfx_width(enum gexp ex)
{
    switch (ex) {
    case EXP_CONSOLE:    return DISP_WIDTH;     /* 640, 1bpp */
    case EXP_4BPP_X2:    return 320;
    case EXP_4BPP_X3:    return 320;
    case EXP_4BPP_X6:    return 160;
    case EXP_1BPP_5TO8:  return 640;
    }
    return 0;
}

void display_gfx_geom(uint16_t *w, uint16_t *h, uint16_t *stride,
                      uint8_t *bpp, uint8_t *mode)
{
    enum gexp ex = gfx_exp;

    *w = (uint16_t)gfx_width(ex);
    *bpp = (uint8_t)gfx_bpp(ex);
    if (ex == EXP_CONSOLE) {
        *h = DISP_HEIGHT;
        *stride = DISP_STRIDE;
        *mode = 0xFF;
    } else {
        *h = gfx_rows;
        *stride = gfx_stride;
        *mode = gfx_mode_now;
    }
}

/* The current drawing colour, already reduced to the mode's own
 * representation.  MMBasic's contract is that callers always speak
 * RGB888 and the primitive converts; doing it here, once per colour
 * change, keeps the per-pixel path down to a store. */
static uint8_t gfx_curcol = 1;

/* RGB888 -> whatever the live mode uses.  Split out from the current
 * colour because DrawBitmap takes an explicit foreground and background
 * and must convert both without disturbing it. */
uint8_t display_gfx_map(uint32_t rgb888)
{
    enum gexp ex = gfx_exp;
    uint8_t want, best = 0, i;
    int bestd = 0x7FFF;
    uint8_t r, g, b;

    if (gfx_bpp(ex) == 1) {
        /* Two colours: anything that is not black is ink.  This is what
         * MMBasic's DrawPixel2 does with its `if (c)`. */
        return rgb888 ? 1 : 0;
    }

    /*
     * MODE 7 is MMBasic's MODE 2, and there the index for a colour is
     * FIXED: RGB121() in the interpreter is pure bit extraction, taking
     * no notice of the palette.  That is what makes MAP work - remap an
     * entry and everything already drawn in it changes colour, while a
     * program carries on naming colours the same way.
     *
     * Nearest-match cannot do that.  Once MAP has moved an entry, the
     * nearest entry to red is no longer the one called red, so new
     * drawing lands somewhere else and a palette cycle takes the
     * picture apart.  The two agree exactly while the palette is the
     * default RGB121 cube, so this costs nothing until MAP is used -
     * and then it is the difference between working and not.
     */
    if (ex == EXP_4BPP_X2)
        return (uint8_t)(((rgb888 & 0x800000) >> 20) |
                         ((rgb888 & 0x00C000) >> 13) |
                         ((rgb888 & 0x000080) >> 7));

    /* The BBC modes keep nearest-match: their palette is a choice from
     * a fixed set of physical colours (VDU19), there is no encoding to
     * extract, and the set is not a regular cube. */
    r = (rgb888 >> 16) & 0xFF;
    g = (rgb888 >> 8) & 0xFF;
    b = rgb888 & 0xFF;
    want = ((r >> 5) << 5) | ((g >> 5) << 2) | (b >> 6);
    for (i = 0; i < 16; i++) {
        int dr = ((gfx_pal[i] >> 5) & 7) - ((want >> 5) & 7);
        int dg = ((gfx_pal[i] >> 2) & 7) - ((want >> 2) & 7);
        int db = (gfx_pal[i] & 3) - (want & 3);
        int d = dr * dr + dg * dg + db * db * 4;  /* blue has 2 bits */
        if (d < bestd) {
            bestd = d;
            best = i;
        }
    }
    return best;
}

void display_gfx_colour(uint32_t rgb888)
{
    gfx_curcol = display_gfx_map(rgb888);
}

int display_gfx_curcol(void)
{
    return gfx_curcol;
}

/* RGB332 -> RGB888, so callers only ever see MMBasic's colour space. */
static uint32_t rgb332_to_888(uint8_t c)
{
    uint32_t r = ((c >> 5) & 7) * 255u / 7u;
    uint32_t g = ((c >> 2) & 7) * 255u / 7u;
    uint32_t b = (c & 3) * 255u / 3u;

    return (r << 16) | (g << 8) | b;
}

/* Read one pixel back AS RGB888 - MMBasic's PIXEL() function returns a
 * colour, not an index, and the conversion belongs in the primitive
 * for the same reason the forward one does.  -1 off-screen. */
int display_gfx_getpixel(int x, int y)
{
    enum gexp ex = gfx_exp;
    int w = gfx_width(ex);
    int h = (ex == EXP_CONSOLE) ? DISP_HEIGHT : gfx_rows;
    int stride = (ex == EXP_CONSOLE) ? DISP_STRIDE : gfx_stride;
    uint8_t v;

    if (!w || x < 0 || y < 0 || x >= w || y >= h)
        return -1;
    if (gfx_bpp(ex) == 4) {
        v = gfx_draw[y * stride + (x >> 1)];
        return (int)rgb332_to_888(gfx_pal[(x & 1) ? (v & 15) : (v >> 4)]);
    }
    /* 1bpp: the bit chooses ink or paper, and the actual colour lives
     * in the cell's tile attributes - so read those rather than
     * pretending the console is black and white. */
    v = gfx_draw[y * stride + (x >> 3)];
    if (ex == EXP_CONSOLE) {
        int cell = (y / DISP_CELL_H) * DISP_COLS + (x / DISP_CELL_W);
        uint8_t c = ((v >> (7 - (x & 7))) & 1) ? disp_tile_fg[cell]
                                               : disp_tile_bg[cell];
        return (int)rgb332_to_888(c);
    }
    return ((v >> (7 - (x & 7))) & 1) ? 0xFFFFFF : 0x000000;
}

/* One pixel.  Kept tight - no swapping, no clipping loop - because this
 * is the hot path: MMBasic's PIXEL statement costs 5us and a compiled
 * one has to beat it.  4bpp layout is high nibble = LEFT pixel, which
 * is framebuf's GS4_HMSB and the opposite of MMBasic's RGB121. */
int display_gfx_pixel(int x, int y, int c)
{
    enum gexp ex = gfx_exp;
    int w = gfx_width(ex);
    int h = (ex == EXP_CONSOLE) ? DISP_HEIGHT : gfx_rows;
    int stride = (ex == EXP_CONSOLE) ? DISP_STRIDE : gfx_stride;
    uint8_t *p;

    if (!w)
        return -1;
    if (x < 0 || y < 0 || x >= w || y >= h)
        return 0;               /* off-screen is not an error */

    if (gfx_bpp(ex) == 4) {
        p = &gfx_draw[y * stride + (x >> 1)];
        if (x & 1)
            *p = (*p & 0xF0) | (c & 15);        /* odd  -> low nibble */
        else
            *p = (*p & 0x0F) | ((c & 15) << 4); /* even -> high nibble */
    } else {
        p = &gfx_draw[y * stride + (x >> 3)];
        if (c)
            *p |= 0x80 >> (x & 7);              /* MSB = leftmost */
        else
            *p &= ~(0x80 >> (x & 7));
    }
    return 0;
}

/* A filled rectangle, which is also how lines arrive (x1==x2 or
 * y1==y2).  Ordering and clipping are done once, then the span loop is
 * flat - the point of having this as a primitive rather than making
 * userland call the pixel one in a loop. */
int display_gfx_rect(int x1, int y1, int x2, int y2, int c)
{
    enum gexp ex = gfx_exp;
    int w = gfx_width(ex);
    int h = (ex == EXP_CONSOLE) ? DISP_HEIGHT : gfx_rows;
    int stride = (ex == EXP_CONSOLE) ? DISP_STRIDE : gfx_stride;
    int x, y, t, xe;

    if (!w)
        return -1;
    if (x2 < x1) { t = x1; x1 = x2; x2 = t; }
    if (y2 < y1) { t = y1; y1 = y2; y2 = t; }
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= w) x2 = w - 1;
    if (y2 >= h) y2 = h - 1;
    if (x1 > x2 || y1 > y2)
        return 0;               /* entirely off-screen */

    if (gfx_bpp(ex) == 4) {
        uint8_t both = ((c & 15) << 4) | (c & 15);
        for (y = y1; y <= y2; y++) {
            uint8_t *row = &gfx_draw[y * stride];
            x = x1;
            if (x & 1) {        /* odd left edge: low nibble only */
                row[x >> 1] = (row[x >> 1] & 0xF0) | (c & 15);
                x++;
            }
            xe = x2;
            if ((x2 & 1) == 0 && x <= x2) {
                /* even right edge: high nibble only */
                row[x2 >> 1] = (row[x2 >> 1] & 0x0F) | ((c & 15) << 4);
                xe = x2 - 1;
            }
            /* What is left starts on a byte and ends on one, so it is
             * a memset - which is the point.  A span was being filled
             * a byte at a time, half the speed of the interpreter's
             * DrawRectangle16 doing exactly this, and filling is most
             * of the work in anything that draws solid shapes. */
            if (x <= xe)
                memset(&row[x >> 1], both, (unsigned)(((xe - x) >> 1) + 1));
        }
    } else {
        for (y = y1; y <= y2; y++) {
            uint8_t *row = &gfx_draw[y * stride];
            for (x = x1; x <= x2; x++) {
                if (c)
                    row[x >> 3] |= 0x80 >> (x & 7);
                else
                    row[x >> 3] &= ~(0x80 >> (x & 7));
            }
        }
    }
    return 0;
}

/* A run of points in one call.  This is the batched form the geometry
 * in userland draws through, so the setup - which mode, how wide, how
 * deep - is hoisted out of the loop and paid once for the whole shape,
 * where display_gfx_pixel pays it per call.
 *
 * col may be NULL for "all in the current colour".  When it is not,
 * the map from RGB888 to the mode's own colour is cached against the
 * last value: a constant-colour run maps once, and 4bpp mapping is a
 * sixteen-way nearest-match that would otherwise dominate.
 */
int display_gfx_pixels(const struct gfx_pt *pt, int n, const uint32_t *col)
{
    enum gexp ex = gfx_exp;
    int w = gfx_width(ex);
    int h = (ex == EXP_CONSOLE) ? DISP_HEIGHT : gfx_rows;
    int stride = (ex == EXP_CONSOLE) ? DISP_STRIDE : gfx_stride;
    int four = (gfx_bpp(ex) == 4);
    uint32_t last = 0;
    int c = gfx_curcol;
    int i, x, y;

    if (!w)
        return -1;
    if (col && n > 0) {
        last = col[0];
        c = display_gfx_map(last);
    }

    for (i = 0; i < n; i++) {
        if (col && col[i] != last) {
            last = col[i];
            c = display_gfx_map(last);
        }
        x = pt[i].x;
        y = pt[i].y;
        if (x < 0 || y < 0 || x >= w || y >= h)
            continue;               /* off-screen is dropped, not an error */
        if (four) {
            uint8_t *p = &gfx_draw[y * stride + (x >> 1)];
            if (x & 1)
                *p = (*p & 0xF0) | (c & 15);
            else
                *p = (*p & 0x0F) | ((c & 15) << 4);
        } else {
            uint8_t *p = &gfx_draw[y * stride + (x >> 3)];
            if (c)
                *p |= 0x80 >> (x & 7);
            else
                *p &= ~(0x80 >> (x & 7));
        }
    }
    return 0;
}

/* A run of rectangles - the other half of the batched pair, and what a
 * filled circle or polygon turns into: one span per scan line.  The
 * span loop dominates, so this reuses display_gfx_rect rather than
 * hoisting anything, which keeps it to a few dozen bytes of kernel. */
int display_gfx_rects(const struct gfx_rc *rc, int n, const uint32_t *col)
{
    uint32_t last = 0;
    int c = gfx_curcol;
    int i, r;

    if (!gfx_width(gfx_exp))
        return -1;
    if (col && n > 0) {
        last = col[0];
        c = display_gfx_map(last);
    }
    for (i = 0; i < n; i++) {
        if (col && col[i] != last) {
            last = col[i];
            c = display_gfx_map(last);
        }
        r = display_gfx_rect(rc[i].x1, rc[i].y1, rc[i].x2, rc[i].y2, c);
        if (r < 0)
            return r;
    }
    return 0;
}

/* A scaled 1-bit source bitmap - MMBasic's DrawBitmap2 and DrawBitmap16
 * folded into one, because at this level they differ only in how a pixel
 * is stored.  Every character on the screen goes through this, which is
 * why the editor needs it before anything else.
 *
 * fc and bc are already reduced to the mode's own colours; bc < 0 leaves
 * the background alone, which is MMBasic's `bc == -1` transparency.
 *
 * TWO conventions differ from MMBasic and both are deliberate:
 *
 *   - the SOURCE bit order is MMBasic's, verbatim, so its fonts and
 *     BLIT data can be used unchanged.  It looks strange - the shift is
 *     taken from the END of the bitmap - but for any bitmap whose total
 *     bit count is a multiple of 8 (every font) it is plain MSB-first.
 *   - the DESTINATION is ours: 1bpp MSB = leftmost pixel and 4bpp high
 *     nibble = left pixel, where MMBasic uses `1 << (x % 8)` and the
 *     low nibble.  See PC3-GFX-DESIGN.md - the port is unified with
 *     MicroPython's framebuf so assets interchange between the two PC3
 *     environments, and the scanout expander is indifferent.
 */
/* The blit above with the glyph turned - see GORIENT_* in pico_ioctl.h
 * for why turning it is the kernel's job.
 *
 * Every orientation reads the SAME source bits; only the order changes,
 * and for all four that order is linear in the destination pixel.  So
 * each destination row needs one base and one step, worked out before
 * the row starts, and the innermost loop stays the add it always was -
 * no branch per pixel, no rotated copy, no scratch buffer, and a user
 * font of any cell size works because nothing here is sized.
 *
 * The mapping is MMBasic's GUIPrintChar read backwards.  It builds the
 * rotated glyph by walking the SOURCE and computing where each bit
 * lands; this walks the DESTINATION and computes where each pixel came
 * from, which is the same permutation inverted:
 *
 *   I   dest(k,i) <- source(width-1-k, height-1-i)
 *   U   dest(k,i) <- source(width-1-i, k)        cell turns: h wide, w tall
 *   D   dest(k,i) <- source(i, height-1-k)       cell turns: h wide, w tall
 */
static int gfx_blit(int x1, int y1, int width, int height, int scale,
                    int fc, int bc, const uint8_t *bitmap, int orient)
{
    enum gexp ex = gfx_exp;
    int w = gfx_width(ex);
    int h = (ex == EXP_CONSOLE) ? DISP_HEIGHT : gfx_rows;
    int stride = (ex == EXP_CONSOLE) ? DISP_STRIDE : gfx_stride;
    int bpp = gfx_bpp(ex);
    int nbits = width * height;
    /* The cell AS DRAWN.  A quarter turn swaps it; a half turn does not. */
    int dw = (orient >= GORIENT_U) ? height : width;
    int dh = (orient >= GORIENT_U) ? width : height;
    int i, j, k, m, x, y, c;

    if (!w)
        return -1;
    if (width <= 0 || height <= 0 || scale <= 0)
        return -1;
    /* wholly off-screen: MMBasic's own early out, before any work */
    if (x1 >= w || y1 >= h ||
        x1 + dw * scale < 0 || y1 + dh * scale < 0)
        return 0;

    for (i = 0; i < dh; i++) {                  /* destination row */
        int base, step;

        switch (orient) {
        case GORIENT_I:
            base = (height - 1 - i) * width + width - 1;
            step = -1;
            break;
        case GORIENT_U:
            base = width - 1 - i;
            step = width;
            break;
        case GORIENT_D:
            base = (height - 1) * width + i;
            step = -width;
            break;
        default:                                /* N and V draw upright */
            base = i * width;
            step = 1;
            break;
        }
        for (j = 0; j < scale; j++) {           /* repeated to scale */
            int n = base;

            y = y1 + i * scale + j;
            if (y < 0 || y >= h) {
                continue;
            }
            for (k = 0; k < dw; k++, n += step) {
                int set = (bitmap[n >> 3] >> ((nbits - n - 1) & 7)) & 1;

                c = set ? fc : bc;
                if (c < 0)
                    continue;                   /* transparent paper */
                for (m = 0; m < scale; m++) {
                    uint8_t *p;

                    x = x1 + k * scale + m;
                    if (x < 0 || x >= w)
                        continue;
                    if (bpp == 4) {
                        p = &gfx_draw[y * stride + (x >> 1)];
                        if (x & 1)
                            *p = (*p & 0xF0) | (c & 15);
                        else
                            *p = (*p & 0x0F) | ((c & 15) << 4);
                    } else {
                        p = &gfx_draw[y * stride + (x >> 3)];
                        if (c)
                            *p |= 0x80 >> (x & 7);
                        else
                            *p &= ~(0x80 >> (x & 7));
                    }
                }
            }
        }
    }
    return 0;
}

/* The upright blit, which is every caller but text: GUI BITMAP, the
 * sprites, and every character the console draws. */
int display_gfx_bitmap(int x1, int y1, int width, int height, int scale,
                       int fc, int bc, const uint8_t *bitmap)
{
    return gfx_blit(x1, y1, width, height, scale, fc, bc, bitmap,
                    GORIENT_N);
}

/*
 * A run of text at a PIXEL position - MMBasic's GUIPrintChar, which is
 * how PRINT reaches the screen in a graphics mode.
 *
 * Any of the built-in fonts (fonts.c), font 1 being the console's own -
 * MMBasic's font1 - so a program's text matches the shell's.  The
 * layout is MMBasic's throughout: header [width][height][first][count]
 * then the glyphs, each width*height bits packed continuously.
 *
 * It goes through display_gfx_bitmap, so it writes to gfx_draw like
 * every other primitive - which is the whole point.  A program that has
 * selected the off-screen buffer gets its text there, instead of the
 * console scribbling on the screen underneath the picture.
 *
 * One call for the whole string rather than one per character: a
 * counter redrawn every frame is the case this exists for.
 */
int display_gfx_text(int x, int y, int font, int scale, int fc, int bc,
                     const uint8_t *s, int len, int orient)
{
    int i, w, h, first, count, glyph;
    const uint8_t *fp = display_font(font, &w, &h, &first, &count);
    int cw, ch, modx = 0, mody = 0;

    if (!fp)
        return -1;
    if (scale <= 0)
        scale = 1;
    if (orient < GORIENT_N || orient > GORIENT_D)
        orient = GORIENT_N;
    /* Bytes per glyph.  NOT h: that only holds for a font 8 pixels
     * wide, and of the nine only two are. */
    glyph = (w * h) / 8;
    cw = (orient >= GORIENT_U) ? h : w;         /* the cell as drawn */
    ch = (orient >= GORIENT_U) ? w : h;
    /*
     * Where the turned glyph sits relative to the anchor - GUIPrintChar's
     * modx/mody, transliterated including the -1 that makes the anchor
     * the pixel the character rotated ABOUT rather than the corner one
     * past it.  U is the reference's own odd one out: it alone has no
     * -1, so its anchor lands one pixel outside the cell where N, I and
     * D all land inside.  Copied as it stands.  A PicoMite and a PC3
     * side by side must put the same pixel in the same place, and a
     * quiet one-pixel improvement here is a divergence a program can
     * see and nobody can explain - see mmb_gfx_text.h, which carries
     * the same note for the justification half.
     */
    if (orient == GORIENT_I) {
        modx = -(w * scale - 1);
        mody = -(h * scale - 1);
    } else if (orient == GORIENT_U) {
        mody = -(w * scale);
    } else if (orient == GORIENT_D) {
        modx = -(h * scale - 1);
    }

    for (i = 0; i < len; i++) {
        int c = s[i];

        if (c < first || c >= first + count) {
            /* Not in the font.  MMBasic fills the cell with the paper
             * colour and moves on - which for font 6, whose 11 glyphs
             * are the digits, is every other character.  Substituting a
             * space would index off the end of that font. */
            if (bc >= 0)
                display_gfx_rect(x + modx, y + mody,
                                 x + modx + cw * scale - 1,
                                 y + mody + ch * scale - 1, bc);
        } else {
            gfx_blit(x + modx, y + mody, w, h, scale, fc, bc,
                     &fp[4 + (c - first) * glyph], orient);
        }
        /* The pen turns with the glyph - GUIPrintChar's tail.  Note U
         * and D step by the glyph's WIDTH, not its height: the cell
         * turned, so the advance along the line is the side that used
         * to be across it. */
        switch (orient) {
        case GORIENT_V:
            y += h * scale;
            break;
        case GORIENT_I:
            x -= w * scale;
            break;
        case GORIENT_U:
            y -= w * scale;
            break;
        case GORIENT_D:
            y += w * scale;
            break;
        default:
            x += w * scale;
            break;
        }
    }
    return x;
}

/*
 * Scroll the drawing target - the ONE implementation.
 *
 * rows > 0 moves the picture up, rows < 0 down, and the vacated band is
 * filled with fillc.  The console calls this for its graphics modes and
 * userland reaches it through GFXIOC_SCROLL, so a PRINT running off the
 * bottom does the same thing whoever issued it.
 *
 * It moves gfx_draw, not disp_fb, which is the point: con_gfx_scroll
 * used to memmove the SCREEN while con_gfx_plot and con_gfx_clear drew
 * through the write target, so a console that scrolled while a program
 * was drawing off-screen moved the wrong picture.  Nothing had noticed
 * because nothing had yet printed with a framebuffer selected.
 */
/*
 * The two-axis scroll with wrap - GFXIOC_SCROLL2, SPRITE SCROLL's
 * engine.  dx > 0 picture right, dy > 0 picture up, matching the
 * reference's ScrollBufferH/V senses.  fillc is a NATIVE index (the
 * dispatcher reduces RGB888), or -1 leave, -2 wrap.
 *
 * One packed row is the only working storage.  Horizontal movement is
 * per pixel through it - at 4bpp an odd dx crosses nibbles and at 1bpp
 * any dx crosses bits, and one honest path beats four clever ones.
 * Vertical wrap is the three-reversal rotation, swapping rows through
 * the same buffer, so no band-sized allocation exists anywhere.
 * A frame-sized scroll in mode 7 costs a few milliseconds of RAM work,
 * once per game frame.
 */
static uint8_t sc2_row[DISP_STRIDE > 160 ? DISP_STRIDE : 164];

static int sc2_get(const uint8_t *r, int x, int bpp)
{
    if (bpp == 4)
        return (x & 1) ? (r[x >> 1] & 15) : (r[x >> 1] >> 4);
    return (r[x >> 3] >> (7 - (x & 7))) & 1;
}

static void sc2_set(uint8_t *r, int x, int bpp, int v)
{
    if (bpp == 4) {
        if (x & 1)
            r[x >> 1] = (uint8_t)((r[x >> 1] & 0xF0) | (v & 15));
        else
            r[x >> 1] = (uint8_t)((r[x >> 1] & 0x0F) | ((v & 15) << 4));
    } else {
        uint8_t m = (uint8_t)(0x80 >> (x & 7));

        if (v)
            r[x >> 3] |= m;
        else
            r[x >> 3] &= (uint8_t)~m;
    }
}

static void sc2_revrows(uint8_t *base, int stride, int a, int b)
{
    while (a < b - 1) {
        b--;
        memcpy(sc2_row, base + a * stride, (unsigned)stride);
        memcpy(base + a * stride, base + b * stride, (unsigned)stride);
        memcpy(base + b * stride, sc2_row, (unsigned)stride);
        a++;
    }
}

int display_gfx_scroll2(int dx, int dy, int fillc)
{
    enum gexp ex = gfx_exp;
    int h = (ex == EXP_CONSOLE) ? DISP_HEIGHT : gfx_rows;
    int stride = (ex == EXP_CONSOLE) ? DISP_STRIDE : gfx_stride;
    int w = gfx_width(ex);
    int bpp = gfx_bpp(ex);
    int x, y;
    uint8_t fill8;

    if (!stride || !w || (bpp != 1 && bpp != 4))
        return -1;
    fill8 = (bpp == 4) ? (uint8_t)((fillc & 15) | ((fillc & 15) << 4))
                       : (uint8_t)(fillc ? 0xFF : 0);

    if (dx) {
        for (y = 0; y < h; y++) {
            uint8_t *row = gfx_draw + y * stride;

            memcpy(sc2_row, row, (unsigned)stride);
            /* new[x] takes old[x - dx] when that lies on the row;
             * otherwise wrap takes it modulo, leave keeps old[x], and
             * a colour fills. */
            for (x = 0; x < w; x++) {
                int sx = x - dx;
                int v;

                if (sx >= 0 && sx < w)
                    v = sc2_get(sc2_row, sx, bpp);
                else if (fillc == -2) {
                    sx %= w;
                    if (sx < 0)
                        sx += w;
                    v = sc2_get(sc2_row, sx, bpp);
                } else if (fillc == -1)
                    v = sc2_get(sc2_row, x, bpp);
                else
                    v = (bpp == 4) ? (fillc & 15) : (fillc ? 1 : 0);
                sc2_set(row, x, bpp, v);
            }
        }
    }

    if (dy) {
        int n = dy < 0 ? -dy : dy;

        if (fillc == -2) {
            n %= h;
            if (n) {
                int k = dy > 0 ? n : h - n;

                /* rotate rows left by k: picture up by dy>0 */
                sc2_revrows(gfx_draw, stride, 0, k);
                sc2_revrows(gfx_draw, stride, k, h);
                sc2_revrows(gfx_draw, stride, 0, h);
            }
        } else if (n >= h) {
            if (fillc != -1)
                memset(gfx_draw, fill8, (unsigned)(stride * h));
        } else {
            int keep = (h - n) * stride;

            if (dy > 0) {
                memmove(gfx_draw, gfx_draw + n * stride, (unsigned)keep);
                if (fillc != -1)
                    memset(gfx_draw + keep, fill8,
                           (unsigned)(n * stride));
            } else {
                memmove(gfx_draw + n * stride, gfx_draw, (unsigned)keep);
                if (fillc != -1)
                    memset(gfx_draw, fill8, (unsigned)(n * stride));
            }
        }
    }
    return 0;
}

int display_gfx_scroll(int rows, int fillc)
{
    enum gexp ex = gfx_exp;
    int h = (ex == EXP_CONSOLE) ? DISP_HEIGHT : gfx_rows;
    int stride = (ex == EXP_CONSOLE) ? DISP_STRIDE : gfx_stride;
    int n = rows < 0 ? -rows : rows;
    int keep;
    uint8_t fill;

    if (!stride || !rows)
        return -1;
    /* both nibbles, or all eight bits, of the incoming band */
    fill = (gfx_bpp(ex) == 4) ? (uint8_t)((fillc & 15) | ((fillc & 15) << 4))
                              : (uint8_t)(fillc ? 0xFF : 0);
    if (n >= h) {
        memset(gfx_draw, fill, (unsigned)(stride * h));
        return 0;
    }
    keep = (h - n) * stride;
    if (rows > 0) {
        memmove(gfx_draw, gfx_draw + n * stride, (unsigned)keep);
        memset(gfx_draw + keep, fill, (unsigned)(n * stride));
    } else {
        memmove(gfx_draw + n * stride, gfx_draw, (unsigned)keep);
        memset(gfx_draw, fill, (unsigned)(n * stride));
    }
    return 0;
}

