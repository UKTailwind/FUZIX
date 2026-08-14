/*
 * The built-in fonts - MMBasic's own nine, in flash.
 *
 * These are the interpreter's font files copied across unchanged, so a
 * character drawn here is the character MMBasic draws.  They are worth
 * having now for a reason that did not hold before v0.7: `const' data
 * lands in XIP flash rather than being copied into RAM at boot, so all
 * nine together cost about 25K of the 16M of flash and NOT ONE BYTE of
 * the 512K of RAM.  Measured, not assumed - font1 sits at 0x1000aba0.
 *
 * The layout is MMBasic's: four header bytes [width][height][first
 * char][count], then the glyphs, each one width*height bits packed
 * continuously, MSB first.  display_gfx_bitmap already decodes exactly
 * that - see the note there about keeping MMBasic's source bit order on
 * purpose - so every one of these drops in with no change to the
 * blitter.  The condition that makes it plain MSB-first is that
 * width*height is a multiple of 8, and it is for all nine.
 *
 * Two of them are declared as word arrays rather than byte arrays
 * (Inconsola as int, TinyFont as uint32_t).  Reading them back as bytes
 * is only correct on a little-endian machine, which this is; the header
 * word 0x5F202018 reads as 24, 32, 32, 95 exactly as intended.
 *
 * Font 3 is the HDMI choice.  MMBasic picks between Hom_16x24_LE and
 * arial_bold there depending on the display, and the PC3 is HDMI.
 */

#include <kernel.h>
#include <kdata.h>			/* udata: whose font a slot is */
#include <stdint.h>
#include "display.h"

/* font1 is DECLARED, not included: console_font.h defines it and the
 * console already carries that copy.  Including it here would put a
 * second 2,692 bytes in the image for nothing. */
extern const unsigned char font1[];

#include "fonts/Misc_12x20_LE.h"
#include "fonts/Hom_16x24_LE.h"
#include "fonts/Fnt_10x16.h"
#include "fonts/Inconsola.h"
#include "fonts/ArialNumFontPlus.h"
#include "fonts/Font_8x6.h"
#include "fonts/smallfont.h"
#include "fonts/font-8x10.h"

/*
 * MMBasic's FontTable, in MMBasic's order, so a program that asks for
 * font 4 gets the font the interpreter calls 4.
 *
 * Font 6 is the odd one: 32x50 digits only, first character '0' and
 * eleven of them.  Nothing needs to know that here - every user of this
 * table reads the count out of the font itself.
 */
static const unsigned char *const font_table[] = {
    font1,                              /* 1   8x12  224 chars          */
    Misc_12x20_LE,                      /* 2  12x20   95                */
    Hom_16x24_LE,                       /* 3  16x24   95                */
    Fnt_10x16,                          /* 4  10x16  224                */
    (const unsigned char *)Inconsola,   /* 5  24x32   95                */
    ArialNumFontPlus,                   /* 6  32x50   11 (digits)       */
    F_6x8_LE,                           /* 7   6x8    96                */
    (const unsigned char *)TinyFont,    /* 8   4x6    96                */
    font8x10                            /* 9   8x10   95                */
};

#define NFONTS ((int)(sizeof(font_table) / sizeof(font_table[0])))

/*
 * A PROGRAM'S OWN FONT (GFXIOC_FONTDEF), which is MMBasic's DefineFont.
 *
 * The mirror of GFXIOC_FONTADDR: that hands a program the address of a
 * kernel font, this takes the address of a program's.  Nothing is
 * copied - the glyphs stay in the caller's image, where they cost the
 * program and not the kernel's last few hundred bytes of RAM, and are
 * read where they lie.  There is no MMU, so a user address is a machine
 * address; MMBasic does the same thing with the font in flash beside
 * the program (MMBasic.c:1167, FontTable[] holding pointers).
 *
 * Numbers 10-16 only.  MMBasic's nine built-ins keep 1-9 and a
 * DefineFont onto one of them is refused rather than honoured, so a
 * program cannot change what the console and every other program draws
 * with; the translator turns that into an error the programmer sees.
 * Nine built-in plus seven of these is MMBasic's sixteen.
 *
 * OWNERSHIP IS NOT OPTIONAL.  Every process on this machine loads at
 * the same PROGLOAD, so one program's font address is a perfectly
 * plausible address inside the next one, and a slot left behind would
 * have it drawing glyphs out of whatever is running now - silently,
 * with no fault to trace.  So each slot carries the p_tab that
 * registered it (a pointer, not a pid: pids are reused), a slot is
 * invisible to any other process, and display_font_release() empties
 * it on exit and on exec.
 */
#define UFONT_FIRST  10
#define UFONT_SLOTS  7                  /* fonts 10-16 */

static struct {
    const unsigned char *addr;
    struct p_tab *owner;
} ufont[UFONT_SLOTS];

/* Remember one.  The caller (misc.c) has already checked that the whole
 * extent belongs to this process and that the header agrees with it. */
int display_font_set(int font, const unsigned char *addr, struct p_tab *owner)
{
    if (font < UFONT_FIRST || font >= UFONT_FIRST + UFONT_SLOTS)
        return -1;
    ufont[font - UFONT_FIRST].addr = addr;
    ufont[font - UFONT_FIRST].owner = owner;
    return 0;
}

/* Process gone (exit, exec) - its fonts go with it. */
void display_font_release(struct p_tab *who)
{
    unsigned i;

    for (i = 0; i < UFONT_SLOTS; i++)
        if (ufont[i].owner == who) {
            ufont[i].addr = NULL;
            ufont[i].owner = NULL;
        }
}

/*
 * The glyph data for a font, with its metrics, or NULL if there is no
 * such font.  Fonts are numbered from 1, as MMBasic numbers them.
 *
 * Every metric comes out of the font's own header rather than a table
 * here, so adding a font is one line above and nothing else - and it is
 * what lets a user font of any cell size drop in with no other change.
 */
const unsigned char *display_font(int font, int *w, int *h,
                                  int *first, int *count)
{
    const unsigned char *fp;

    if (font >= UFONT_FIRST && font < UFONT_FIRST + UFONT_SLOTS) {
        unsigned i = (unsigned)(font - UFONT_FIRST);

        /* Another process's slot is not ours to draw from */
        if (ufont[i].addr == NULL || ufont[i].owner != udata.u_ptab)
            return NULL;
        fp = ufont[i].addr;
    }
    else if (font < 1 || font > NFONTS)
        return NULL;
    else
        fp = font_table[font - 1];
    if (w)
        *w = fp[0];
    if (h)
        *h = fp[1];
    if (first)
        *first = fp[2];
    if (count)
        *count = fp[3];
    return fp;
}

int display_font_count(void)
{
    return NFONTS;
}
