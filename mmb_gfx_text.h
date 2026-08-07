#ifndef MMB_GFX_TEXT_H
#define MMB_GFX_TEXT_H
/*
 *	TEXT x, y, string$ [, alignment$] [, font] [, scale] [, fg] [, bg]
 *
 *	See mmb_gfx_pts.h for why the primitives live in headers, one per
 *	primitive.  TEXT batches nothing, so it does not include the
 *	batch helpers.
 *
 *	MMBasic's cmd_text and GUIPrintString (Draw.c).  The kernel draws
 *	a run of glyphs in a font, at a size, in two colours; deciding
 *	WHERE that run starts is all of the rest, and it is arithmetic on
 *	the font's cell, so it belongs here.
 *
 *	The cell comes from the kernel, per call, because the fonts are
 *	the kernel's - MMBasic's nine, in flash.  A copy of "8 by 12"
 *	here would centre font 3 as if it were font 1.
 */

#include "mmb_runtime.h"

/* MMBasic's own numbering (Draw.h), so the parse below can stay its. */
#define MMG_JUST_LEFT	0
#define MMG_JUST_CENTRE	1
#define MMG_JUST_RIGHT	2
#define MMG_JUST_TOP	0
#define MMG_JUST_MIDDLE	1
#define MMG_JUST_BOTTOM	2
#define MMG_ORIENT_N	0		/* normal                       */
#define MMG_ORIENT_V	1		/* one glyph per line, downwards */
#define MMG_ORIENT_I	2		/* inverted                     */
#define MMG_ORIENT_U	3		/* rotated CCW 90               */
#define MMG_ORIENT_D	4		/* rotated CW 90                */

/*
 *	The justify$ of TEXT and GUI CAPTION: up to three letters, in the
 *	order horizontal, vertical, orientation, any of them absent.
 *
 *	MMBasic's GetJustification, transliterated including its
 *	forgiveness - a letter that is not of the set this switch is
 *	looking for is pushed back and offered to the next one, which is
 *	what lets "B" mean bottom and "R" mean right with neither needing
 *	a place-holder.  Returns 0 if the string is not a justification at
 *	all, and MMBasic treats that as an error.
 */
static int mmg_upper(int c)
{
	return (c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c;
}

static int mmg_just(const char *p, int *jh, int *jv, int *jo)
{
	int c;

	c = mmg_upper(*p++);
	if (c == 'L')      *jh = MMG_JUST_LEFT;
	else if (c == 'C') *jh = MMG_JUST_CENTRE;
	else if (c == 'R') *jh = MMG_JUST_RIGHT;
	else if (c == 0)   return 1;
	else               p--;
	while (*p == ' ')
		p++;

	c = mmg_upper(*p++);
	if (c == 'T')      *jv = MMG_JUST_TOP;
	else if (c == 'M') *jv = MMG_JUST_MIDDLE;
	else if (c == 'B') *jv = MMG_JUST_BOTTOM;
	else if (c == 0)   return 1;
	else               p--;
	while (*p == ' ')
		p++;

	c = mmg_upper(*p++);
	if (c == 'N')      *jo = MMG_ORIENT_N;
	else if (c == 'V') *jo = MMG_ORIENT_V;
	else if (c == 'I') *jo = MMG_ORIENT_I;
	else if (c == 'U') *jo = MMG_ORIENT_U;
	else if (c == 'D') *jo = MMG_ORIENT_D;
	else if (c == 0)   return 1;
	else               return 0;
	return *p == 0;
}

/*
 *	TEXT itself.
 *
 *	The justification arithmetic is GUIPrintString's, unchanged, with
 *	its three cases: normal text moves by the whole string's width and
 *	one cell's height, vertical text by one cell's width and the whole
 *	string's height, and the rotated ones by the two swapped.
 *
 *	Only N and V are drawn.  I, U and D rotate the GLYPH, which needs
 *	the font's bits on this side of the syscall; until that is done
 *	they are drawn normally rather than not at all, so a program that
 *	asks for them shows something wrong rather than nothing.
 *
 *	s and just are MMBasic strings - length byte first - not C ones,
 *	which is why the length is taken and not counted: a string here
 *	may contain a NUL and still be worth drawing.
 */
static void mmg_text(int x, int y, const char *s, const char *just,
		     int font, int scale, MMINTEGER fc, MMINTEGER bc)
{
	int jh = MMG_JUST_LEFT, jv = MMG_JUST_TOP, jo = MMG_ORIENT_N;
	MMINTEGER cw = 0, ch = 0;
	const char *text = mm_cstr(s);
	int len = mm_slen(s);
	int w, h, i;

	if (font < 1)
		font = 1;
	if (scale < 1)
		scale = 1;
	if (scale > 15)
		scale = 15;
	if (just && mm_slen(just) &&
	    !mmg_just(mm_cstr(just), &jh, &jv, &jo))
		MM_RAISE("Justification");
	/* Negative is "there is no display", which is the host build: draw
	   nothing and say nothing, as every other primitive does there.
	   Zero is a font the kernel really does not have, and MMBasic
	   makes that an error. */
	if (mm_fontinfo(font, &cw, &ch) < 0)
		return;
	if (!cw)
		MM_RAISE("Invalid font");

	w = (int)cw * scale;		/* one cell, scaled - GetFontWidth */
	h = (int)ch * scale;

	if (jo == MMG_ORIENT_V) {
		if (jh == MMG_JUST_CENTRE)
			x -= w / 2;
		else if (jh == MMG_JUST_RIGHT)
			x -= w;
		if (jv == MMG_JUST_MIDDLE)
			y -= (len * h) / 2;
		else if (jv == MMG_JUST_BOTTOM)
			y -= len * h;
		/* One call per glyph: the run the kernel draws is a row,
		   and this is a column. */
		for (i = 0; i < len; i++)
			mm_gtext(x, y + i * h, font, scale, fc, bc,
				 text + i, 1);
		return;
	}

	if (jh == MMG_JUST_CENTRE)
		x -= (len * w) / 2;
	else if (jh == MMG_JUST_RIGHT)
		x -= len * w;
	if (jv == MMG_JUST_MIDDLE)
		y -= h / 2;
	else if (jv == MMG_JUST_BOTTOM)
		y -= h;
	/* text, not s: s points at the length byte, and drawing that as a
	   character puts a blank cell in front of every string. */
	mm_gtext(x, y, font, scale, fc, bc, text, len);
}

#endif /* MMB_GFX_TEXT_H */
