#ifndef MMB_SPRITE_H
#define MMB_SPRITE_H
/*
 *	The SPRITE family (graphics/Sprite.c), on the same row workhorses
 *	as BLIT - mmb_blit.h must be included first and the translator
 *	guarantees it.
 *
 *	What is transcribed, exactly: the two LIFO stacks (layer 0 draws
 *	under layers 1-4, the most recently shown sprite is topmost), the
 *	SAFE show/hide walk that unstacks overlapping sprites and restacks
 *	them in order, background save/restore, AABB collisions with the
 *	edge codes 0xF1/2/4/8 and static objects as 0x80|n, the
 *	edge-triggered lastcollisions masks, next_x/next_y, master/copy
 *	sharing, and every error string and argument range the reference
 *	checks.  SPRITE SCROLL is Phase 4 of PLAN-games.md (it needs the
 *	kernel's SCROLL2).  SPRITE(B), LOADPNG and LOADBMP are in.
 *
 *	Divergences, recorded in PLAN-games.md: a sprite image is one
 *	native index per byte, not packed nibbles (double the reference's
 *	RAM, in PSRAM under bcrun, and the same lossless argument as blit
 *	buffers - visible only through SPRITE(A)); and in MODE 1 a
 *	non-zero index is ink, which is what the reference's
 *	DrawBuffer2Fast conversion does too.
 *
 *	SPRITE LOAD colours are the reference's own tables reduced to
 *	RGB121 indices at build time - pure bit extraction, no kernel
 *	call, so loading works identically headless.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef MMB_BLIT_H
#error "mmb_sprite.h needs mmb_blit.h first (the translator emits both)"
#endif

#define MMS_MAX 64		/* MAXBLITBUF - sprites share the number space */
#define MMS_MAXCOLL 4		/* MAXCOLLISIONS */
#define MMS_MAXLAYER 4		/* MAXLAYER */
#define MMS_MAXST 64		/* MAXSTOBJECTS */
#define MMS_INACTIVE 10000	/* SPRITE_POS_INACTIVE */

struct mmb_sprite {
	unsigned char *img;	/* w*h indices; copies share the master's */
	unsigned char *store;	/* w*h saved background */
	MMINTEGER master;	/* bitmask of my copies; -1 = I am a copy */
	MMINTEGER lastcoll;
	short w, h, x, y, nx, ny;
	signed char layer;	/* -1 = not shown */
	signed char mymaster;	/* -1 = master, else the master's number */
	unsigned char rot, active, edges, alloc;
	unsigned char coll[MMS_MAXCOLL + 1];
	/* SPRITE(B): the image's opaque extent - left and right of each
	 * row, top and bottom of each column, one block, made the first
	 * time it is asked for - and the last answer's eight sides */
	short *bnd;
	short bgc[8];
};

/* index 0 is the reference's spritebuff[0]: the background pseudo
 * sprite whose collisions[] lists what collided when everything moved */
static struct mmb_sprite mms[MMS_MAX + 1];
static unsigned char mms_lifo[MMS_MAX], mms_zlifo[MMS_MAX];
static unsigned char mms_lp, mms_zp, mms_inuse, mms_hideall;
static unsigned char mms_layer_n[MMS_MAXLAYER + 1];
static unsigned char mms_transparent;	/* SPRITE SET TRANSPARENT, 0-15 */
static unsigned char mms_any;		/* any sprite ever defined */

/* the interrupt-scan flags, MMBasic's CollisionFound machinery;
 * mms_coll_int is COLLISIONInterrupt != NULL, kept by mmb_int.h's
 * mmi_sprite_int and mmi_sprite_noint (which come after this file) */
static unsigned char mms_coll_found, mms_st_found, mms_coll_int;
static short mms_which = -1;		/* sprite_which_collided */
static short mms_hit_st = -1, mms_st_which = -1;

struct mmb_stobj { short x, y, w, h; unsigned char active; };
static struct mmb_stobj mms_st[MMS_MAXST + 1];

/* ---- LIFO helpers, verbatim ----------------------------------------- */

MMG_FN void mms_lifo_add(unsigned char *st, unsigned char *pp, int n)
{
	int i, j = 0;

	for (i = 0; i < *pp; i++)
		if (st[i] != n)
			st[j++] = st[i];
	st[j] = (unsigned char)n;
	*pp = (unsigned char)(j + 1);
}

MMG_FN void mms_lifo_remove(unsigned char *st, unsigned char *pp, int n)
{
	int i, j = 0;

	for (i = 0; i < *pp; i++)
		if (st[i] != n)
			st[j++] = st[i];
	*pp = (unsigned char)j;
}

MMG_FN void mms_lifo_swap(unsigned char *st, int pp, int n, int m)
{
	int i;

	for (i = 0; i < pp; i++)
		if (st[i] == n)
			st[i] = (unsigned char)m;
}

/* ---- pixel work on the blit row workhorses -------------------------- */

/* restore the saved background - the reference's blithide */
/*	The rectangle a sprite at (x,y) actually occupies on screen, as the
 *	row loops below clip it: the x span is the same for every row, so
 *	it is worth computing once - and a window over it turns the loop's
 *	crossing-per-row into one for the batch (mmb_win_open). */
MMG_FN int mms_clip(int x, int y, int w, int h, int hres, int vres,
		    int *cx, int *cw, int *cy, int *ch)
{
	int y0 = y, y1 = y + h;

	if (x < 0) { *cw = w + x; *cx = 0; } else { *cw = w; *cx = x; }
	if (*cx + *cw > hres)
		*cw = hres - *cx;
	if (y0 < 0)
		y0 = 0;
	if (y1 > vres)
		y1 = vres;
	*cy = y0;
	*ch = y1 - y0;
	return (*cw >= 1 && *ch >= 1);
}

MMG_FN void mms_hide_px(struct mmb_sprite *sb)
{
	int stride = 0, bpp = 0, hres, vres, i, j;
	int cx, cw, cy, ch;

	sb->active = 0;
	if (mmb_geom(&stride, &bpp, &hres, &vres))
		return;
	if (mms_clip(sb->x, sb->y, sb->w, sb->h, hres, vres,
		     &cx, &cw, &cy, &ch))
		mmb_win_open(cy, ch, cx, cw, stride, bpp);
	for (i = 0; i < sb->h; i++) {
		int y = sb->y + i, x0 = sb->x, w = sb->w, s0 = 0;

		if (y < 0 || y >= vres)
			continue;
		if (x0 < 0) { s0 = -x0; w += x0; x0 = 0; }
		if (x0 + w > hres) w = hres - x0;
		if (w < 1)
			continue;
		/*	The row base once, not once per pixel.  "store[i * sb->w
		 *	+ s0 + j]" inside the j loop is a multiply per PIXEL:
		 *	the board profiler counted 48 of them for one 8x8
		 *	SPRITE SHOW, each a crossing out of native code into
		 *	the interpreter's helper, and 336us for the show. */
		{
			const unsigned char *srow = sb->store
					+ (size_t)i * sb->w + s0;

			for (j = 0; j < w; j++)
				mmb_rowpx[j] = srow[j];
		}
		mmb_row_put(y, x0, w, stride, bpp, mmb_rowpx);
	}
	mmb_win_close();
}

/* the reference's BlitShowBuffer: restore old spot unless told not to,
 * remember the new spot, save what is under it, draw with rotation and
 * (unless opaque) the transparent colour */
MMG_FN void mms_show_px(int bnbr, int x1, int y1, int mode)
{
	struct mmb_sprite *sb = &mms[bnbr];
	int fullmode = mode, stride = 0, bpp = 0, hres, vres, i, j;
	int headless = mmb_geom(&stride, &bpp, &hres, &vres);
	int w = sb->w, h = sb->h;

	mode &= 7;
	/* orientations 4-7 draw every pixel, the transparent colour too,
	 * and the flag stays with the sprite (rot bit 2), so every redraw
	 * honours it (PicoMite #90) */
	if (sb->rot & 4)
		fullmode |= 8;
	if (sb->img == NULL)
		return;
	if (!(mode == 0 || (mode & 4)) && sb->active && !headless)
		mms_hide_px(sb);
	/* WRITE (mode 4) draws a copy: a sprite showing keeps its place and
	 * its saved background (#90) */
	if (!(mode & 4)) {
		sb->x = (short)x1;
		sb->y = (short)y1;
	}
	if (headless) {
		if (!(mode & 4))
			sb->active = 1;
		return;
	}
	/*	ONE window across both loops below.  The save reads the
	 *	rectangle and the draw modifies the same bytes, so with the
	 *	batch resident the whole SPRITE SHOW is one crossing in and
	 *	one out - where it was a read per row for the save and a
	 *	read and a write per row for the draw.  Nothing between them
	 *	touches the framebuffer, which is what makes it safe to hold
	 *	the rows across the pair. */
	{
		int cx, cw, cy, ch;

		if (mms_clip(x1, y1, w, h, hres, vres, &cx, &cw, &cy, &ch))
			mmb_win_open(cy, ch, cx, cw, stride, bpp);
	}
	if (mode != 2 && !(mode & 4)) {
		for (i = 0; i < h; i++) {
			int y = y1 + i, x0 = x1, ww = w, s0 = 0;

			if (y < 0 || y >= vres)
				continue;
			if (x0 < 0) { s0 = -x0; ww += x0; x0 = 0; }
			if (x0 + ww > hres) ww = hres - x0;
			if (ww < 1)
				continue;
			if (mmb_row_get(y, x0, ww, stride, bpp, mmb_rowpx) == 0) {
				unsigned char *srow = sb->store
						+ (size_t)i * w + s0;

				for (j = 0; j < ww; j++)
					srow[j] = mmb_rowpx[j];
			}
		}
	}
	for (i = 0; i < h; i++) {
		int y = y1 + i, x0 = x1, ww = w, s0 = 0, sr;

		if (y < 0 || y >= vres)
			continue;
		if (x0 < 0) { s0 = -x0; ww += x0; x0 = 0; }
		if (x0 + ww > hres) ww = hres - x0;
		if (ww < 1)
			continue;
		sr = (sb->rot & 2) ? h - 1 - i : i;
		/*	As in the save above: the source row base is the same
		 *	for every pixel of the row, and computing it inside
		 *	the j loop made "sr * w" a multiply per pixel. */
		{
			const unsigned char *irow = sb->img + (size_t)sr * w;

			if (fullmode & 8) {
				for (j = 0; j < ww; j++) {
					int sc = s0 + j;

					if (sb->rot & 1)
						sc = w - 1 - sc;
					mmb_rowpx[j] = irow[sc];
				}
			} else {
				/* SWAP (mode 2) draws over the background
				 * stored under the old image, so none of the
				 * old one stays where the new is transparent
				 * (#89); everything else over the screen */
				if (mode == 2)
					memcpy(mmb_rowpx, sb->store +
					       (size_t)i * w + s0, (size_t)ww);
				else if (mmb_row_get(y, x0, ww, stride, bpp,
						     mmb_rowpx) < 0)
					continue;
				for (j = 0; j < ww; j++) {
					int sc = s0 + j;
					unsigned char c;

					if (sb->rot & 1)
						sc = w - 1 - sc;
					c = irow[sc];
					if (c != mms_transparent)
						mmb_rowpx[j] = c;
				}
			}
		}
		mmb_row_put(y, x0, ww, stride, bpp, mmb_rowpx);
	}
	mmb_win_close();
	if (!(mode & 4))
		sb->active = 1;
}

/* ---- collisions, verbatim ------------------------------------------- */

/* an edge of the screen as a collision - 0xF1 left, 0xF2 top, 0xF4
 * right, 0xF8 bottom, each an entry of its own - while there is room:
 * an edge left out is reported by a later move, as its bit in edges
 * stays clear (PicoMite #91's edgecollision) */
MMG_FN void mms_edge(struct mmb_sprite *sb, int *n, int out, int bit)
{
	if (!out)
		sb->edges &= (unsigned char)~bit;
	else if (!(sb->edges & bit) && *n < MMS_MAXCOLL) {
		sb->edges |= (unsigned char)bit;
		sb->coll[(*n)++] = (unsigned char)(0xF0 | bit);
	}
}

MMG_FN void mms_checklimits(int bnbr, int *n)
{
	struct mmb_sprite *sb = &mms[bnbr];
	int hres = (int)mm_hres(), vres = (int)mm_vres();

	if (*n <= MMS_MAXCOLL)
		sb->coll[*n] = 0;
	mms_edge(sb, n, sb->x < 0, 1);
	mms_edge(sb, n, sb->y < 0, 2);
	mms_edge(sb, n, sb->x + sb->w > hres, 4);
	mms_edge(sb, n, sb->y + sb->h > vres, 8);
}

MMG_FN void mms_check_st(int bnbr, int *n)
{
	struct mmb_sprite *sb = &mms[bnbr];
	int i;

	for (i = 1; i <= MMS_MAXST; i++) {
		if (!mms_st[i].active)
			continue;
		if (!(sb->x + sb->w <= mms_st[i].x ||
		      sb->x >= mms_st[i].x + mms_st[i].w ||
		      sb->y + sb->h <= mms_st[i].y ||
		      sb->y >= mms_st[i].y + mms_st[i].h)) {
			if (*n < MMS_MAXCOLL)
				sb->coll[(*n)++] = (unsigned char)(0x80 | i);
			mms_st_found = 1;
			mms_hit_st = (short)bnbr;
			mms_st_which = (short)i;
		}
	}
}

/* may sprites k and kk collide: both showing, and on the same layer or
 * either on layer 0, which meets every layer (#88's SpritesMeet); and do
 * their rectangles overlap, touching counting (SpritesOverlap).  Small
 * functions, for the Fuzix cc2's per-function node pool. */
MMG_FN int mms_meet(int k, int kk)
{
	struct mmb_sprite *a = &mms[k], *b = &mms[kk];

	return kk != k && b->alloc && b->active &&
	       (a->layer == 0 || b->layer == 0 || a->layer == b->layer);
}

MMG_FN int mms_overlap(int k, int kk)
{
	struct mmb_sprite *a = &mms[k], *b = &mms[kk];

	return !(b->x + b->w < a->x || b->x > a->x + a->w ||
		 b->y + b->h < a->y || b->y > a->y + a->h);
}

/* SpriteCollisions: sprite k's collisions that have begun into its list
 * from coll[1] - its lastcoll bit kk - 1 is kept while one lasts, so
 * that it is reported once - then the screen edges and static objects
 * it has reached.  Returns the number of entries + 1.  both: k's bit in
 * each other sprite's mask too (a sprite that moved alone: the meeting
 * is not news to the other either). */
MMG_FN int mms_sprite_coll(int k, int both)
{
	struct mmb_sprite *sb = &mms[k];
	MMINTEGER mine = (MMINTEGER)1 << (k - 1);
	int kk, n = 1;

	memset(sb->coll, 0, sizeof(sb->coll));
	for (kk = 1; kk <= MMS_MAX; kk++) {
		struct mmb_sprite *sk = &mms[kk];
		MMINTEGER bit = (MMINTEGER)1 << (kk - 1);

		if (kk == k || !sk->alloc)
			continue;
		if (mms_meet(k, kk) && mms_overlap(k, kk)) {
			if (n < MMS_MAXCOLL && !(sb->lastcoll & bit))
				sb->coll[n++] = (unsigned char)kk;
			sb->lastcoll |= bit;
			if (both)
				sk->lastcoll |= mine;
		} else {
			sb->lastcoll &= ~bit;
			if (both)
				sk->lastcoll &= ~mine;
		}
	}
	mms_checklimits(k, &n);
	mms_check_st(k, &n);
	if (n > 1)
		sb->coll[0] = (unsigned char)(n - 1);
	return n;
}

/* ProcessCollisions (PicoMite #88): after sprite bnbr has been shown,
 * or (bnbr 0) after MOVE, SCROLL or RESTORE has moved everything.  After
 * a whole-scene check the sprites that collided are mms[0]'s list.  The
 * found flag stays set until its interrupt has run (mmb_int.h clears
 * it), and is cleared here only when no SPRITE INTERRUPT is set. */
MMG_FN void mms_collisions(int bnbr)
{
	int k, bcol = 1;

	if (!mms_coll_int)
		mms_coll_found = 0;
	mms_which = -1;
	memset(mms[0].coll, 0, sizeof(mms[0].coll));
	if (bnbr != 0) {
		if (mms_sprite_coll(bnbr, 1) > 1) {
			mms_coll_found = 1;
			mms_which = (short)bnbr;
		}
		return;
	}
	for (k = 1; k <= MMS_MAX; k++) {
		if (!mms[k].alloc || !mms[k].active)
			continue;
		if (mms_sprite_coll(k, 0) > 1 && bcol < MMS_MAXCOLL)
			mms[0].coll[bcol++] = (unsigned char)k;
	}
	if (bcol > 1) {
		mms_coll_found = 1;
		mms_which = 0;
		mms[0].coll[0] = (unsigned char)(bcol - 1);
	}
}

/* ---- shared helpers -------------------------------------------------- */

MMG_FN struct mmb_sprite *mms_get(MMINTEGER bn)
{
	if (bn < 1 || bn > MMS_MAX)
		MM_RAISEV("Invalid sprite number", (struct mmb_sprite *)0);
	return &mms[bn];
}

MMG_FN void mms_alloc(int bnbr, int w, int h)
{
	struct mmb_sprite *sb = &mms[bnbr];

	sb->img = (unsigned char *)malloc((size_t)w * h * 2);
	if (sb->img == NULL)
		MM_RAISE("Not enough memory");
	sb->bnd = NULL;
	memset(sb->bgc, 0, sizeof(sb->bgc));
	sb->store = sb->img + (size_t)w * h;
	memset(sb->img, 0, (size_t)w * h * 2);
	sb->w = (short)w;
	sb->h = (short)h;
	sb->master = 0;
	sb->mymaster = -1;
	sb->x = MMS_INACTIVE;
	sb->y = MMS_INACTIVE;
	sb->layer = -1;
	sb->nx = MMS_INACTIVE;
	sb->ny = MMS_INACTIVE;
	sb->active = 0;
	sb->lastcoll = 0;
	sb->edges = 0;
	sb->rot = 0;
	sb->alloc = 1;
	mms[0].alloc = 1;
	mms_any = 1;
}

/* the buffers of a sprite whose image did not arrive, given back: a
 * failed decode leaves no buffer in use (PicoMite #93, 4d91e6c) */
MMG_FN void mms_unalloc(struct mmb_sprite *sb)
{
	free(sb->bnd);
	sb->bnd = NULL;
	free(sb->img);
	sb->img = NULL;
	sb->store = NULL;
	sb->w = 0;
	sb->h = 0;
}

MMG_FN int mms_sumlayer(void)
{
	int i, j = 0;

	for (i = 0; i <= MMS_MAXLAYER; i++)
		j += mms_layer_n[i];
	return j;
}

MMG_FN void mms_sanity(void)
{
	if (mms_inuse != mms_lp + mms_zp || mms_inuse != mms_sumlayer())
		MM_RAISE("sprite internal error");
}

/* hide without bookkeeping - the reference's blithide call sites keep
 * their own counters */
MMG_FN void mms_unshow(int bnbr)
{
	struct mmb_sprite *sb = &mms[bnbr];

	sb->active = 0;
	{
		int stride = 0, bpp = 0, hres, vres;

		if (!mmb_geom(&stride, &bpp, &hres, &vres))
			mms_hide_px(sb);
	}
}

MMG_FN void mms_hidesafe(int bnbr)
{
	struct mmb_sprite *sb = &mms[bnbr];
	int found = 0x7FFF, zerolifo = 0, i;

	for (i = mms_lp - 1; i >= 0; i--) {
		if (mms_lifo[i] == bnbr) {
			mms_unshow(mms_lifo[i]);
			found = i;
			break;
		}
		mms_unshow(mms_lifo[i]);
	}
	if (found == 0x7FFF)
		for (i = mms_zp - 1; i >= 0; i--) {
			if (mms_zlifo[i] == bnbr) {
				mms_unshow(mms_zlifo[i]);
				found = -i;
				zerolifo = 1;
				break;
			}
			mms_unshow(mms_zlifo[i]);
		}
	if (found != 0x7FFF) {
		mms_inuse--;
		mms_layer_n[(int)sb->layer]--;
		sb->x = MMS_INACTIVE;
		sb->y = MMS_INACTIVE;
		if (sb->layer == 0)
			mms_lifo_remove(mms_zlifo, &mms_zp, bnbr);
		else
			mms_lifo_remove(mms_lifo, &mms_lp, bnbr);
		sb->layer = -1;
		sb->nx = MMS_INACTIVE;
		sb->ny = MMS_INACTIVE;
		sb->lastcoll = 0;
		sb->edges = 0;
		if (zerolifo) {
			found = -found;
			for (i = found; i < mms_zp; i++)
				mms_show_px(mms_zlifo[i], mms[mms_zlifo[i]].x,
					    mms[mms_zlifo[i]].y, 0);
			for (i = 0; i < mms_lp; i++)
				mms_show_px(mms_lifo[i], mms[mms_lifo[i]].x,
					    mms[mms_lifo[i]].y, 0);
		} else
			for (i = found; i < mms_lp; i++)
				mms_show_px(mms_lifo[i], mms[mms_lifo[i]].x,
					    mms[mms_lifo[i]].y, 0);
	}
}

MMG_FN void mms_showsafe(int bnbr, int x, int y)
{
	int found = 0x7FFF, zerolifo = 0, i;

	for (i = mms_lp - 1; i >= 0; i--) {
		if (mms_lifo[i] == bnbr) {
			mms_unshow(mms_lifo[i]);
			found = i;
			break;
		}
		mms_unshow(mms_lifo[i]);
	}
	if (found == 0x7FFF)
		for (i = mms_zp - 1; i >= 0; i--) {
			if (mms_zlifo[i] == bnbr) {
				mms_unshow(mms_zlifo[i]);
				zerolifo = 1;
				found = -i;
				break;
			}
			mms_unshow(mms_zlifo[i]);
		}
	mms_show_px(bnbr, x, y, 1);
	if (zerolifo) {
		found = -found;
		for (i = found + 1; i < mms_zp; i++)
			mms_show_px(mms_zlifo[i], mms[mms_zlifo[i]].x,
				    mms[mms_zlifo[i]].y, 0);
		for (i = 0; i < mms_lp; i++)
			mms_show_px(mms_lifo[i], mms[mms_lifo[i]].x,
				    mms[mms_lifo[i]].y, 0);
	} else if (found != 0x7FFF)
		for (i = found + 1; i < mms_lp; i++)
			mms_show_px(mms_lifo[i], mms[mms_lifo[i]].x,
				    mms[mms_lifo[i]].y, 0);
}

/* ---- the statements -------------------------------------------------- */

/* SPRITE SHOW [SAFE] - safe=0 plain, safe=1 SAFE; the reference's two
 * branches differ exactly as transcribed here.  ontop is SHOW SAFE's
 * optional fifth argument. */
MMG_FN void mms_show(MMINTEGER bn, MMINTEGER xi, MMINTEGER yi,
		     MMINTEGER layer, MMINTEGER flags, MMINTEGER safe,
		     MMINTEGER ontop)
{
	struct mmb_sprite *sb = mms_get(bn);
	int mode = 1, x1, y1, hres, vres;

	if (sb == NULL)
		return;
	if (mms_hideall)
		MM_RAISE("Sprites are hidden");
	if (!sb->alloc || sb->img == NULL)
		MM_RAISE("Buffer not in use");
	hres = (int)mm_hres();
	vres = (int)mm_vres();
	if (xi < -sb->w + 1 || xi > hres - 1 || yi < -sb->h + 1 ||
	    yi > vres - 1)
		MM_RAISE("Invalid sprite coordinates");
	if (layer < 0 || layer > MMS_MAXLAYER)
		MM_RAISE("Invalid sprite layer");
	if (flags < 0 || flags > 7)
		MM_RAISE("Invalid sprite flags");
	if (ontop < 0 || ontop > 1)
		MM_RAISE("Invalid syntax");
	x1 = (int)xi;
	y1 = (int)yi;
	sb->rot = (unsigned char)flags;	/* 4-7 opaque: mms_show_px */
	if (safe) {
		if (sb->active) {
			/* a new layer: within layers 1 to 4, one list, the
			 * sprite keeps its place; to or from layer 0, the
			 * other list, it goes on top, as with ontop (#90) */
			int old = sb->layer;

			if (!ontop && layer != old && old != 0 && layer != 0) {
				mms_layer_n[old]--;
				mms_layer_n[layer]++;
				sb->layer = (signed char)layer;
			}
			if (ontop || (layer != old &&
				      (old == 0 || layer == 0))) {
				mms_hidesafe((int)bn);
				sb->layer = (signed char)layer;
				mms_layer_n[layer]++;
				if (layer == 0)
					mms_lifo_add(mms_zlifo, &mms_zp, (int)bn);
				else
					mms_lifo_add(mms_lifo, &mms_lp, (int)bn);
				mms_inuse++;
				mms_show_px((int)bn, x1, y1, mode);
			} else
				mms_showsafe((int)bn, x1, y1);
		} else {
			sb->layer = (signed char)layer;
			mms_layer_n[layer]++;
			if (layer == 0)
				mms_lifo_add(mms_zlifo, &mms_zp, (int)bn);
			else
				mms_lifo_add(mms_lifo, &mms_lp, (int)bn);
			mms_inuse++;
			mms_show_px((int)bn, x1, y1, mode);
		}
	} else {
		if (sb->active) {
			mms_layer_n[(int)sb->layer]--;
			if (sb->layer == 0)
				mms_lifo_remove(mms_zlifo, &mms_zp, (int)bn);
			else
				mms_lifo_remove(mms_lifo, &mms_lp, (int)bn);
			mms_inuse--;
		}
		sb->layer = (signed char)layer;
		mms_layer_n[layer]++;
		if (layer == 0)
			mms_lifo_add(mms_zlifo, &mms_zp, (int)bn);
		else
			mms_lifo_add(mms_lifo, &mms_lp, (int)bn);
		mms_inuse++;
		mms_show_px((int)bn, x1, y1, mode);
	}
	mms_collisions((int)bn);
	mms_sanity();
}

MMG_FN void mms_hide(MMINTEGER bn, MMINTEGER safe)
{
	struct mmb_sprite *sb = mms_get(bn);

	if (sb == NULL)
		return;
	if (mms_hideall)
		MM_RAISE("Sprites are hidden");
	if (!sb->alloc || sb->img == NULL)
		MM_RAISE("Buffer not in use");
	if (!sb->active)
		MM_RAISE("Not Showing");
	if (safe) {
		mms_hidesafe((int)bn);
		mms_sanity();
		return;
	}
	mms_inuse--;
	mms_unshow((int)bn);
	mms_layer_n[(int)sb->layer]--;
	sb->x = MMS_INACTIVE;
	sb->y = MMS_INACTIVE;
	if (sb->layer == 0)
		mms_lifo_remove(mms_zlifo, &mms_zp, (int)bn);
	else
		mms_lifo_remove(mms_lifo, &mms_lp, (int)bn);
	sb->layer = -1;
	sb->nx = MMS_INACTIVE;
	sb->ny = MMS_INACTIVE;
	sb->lastcoll = 0;
	sb->edges = 0;
	mms_sanity();
}

MMG_FN void mms_hide_all(void)
{
	int i;

	if (mms_hideall)
		MM_RAISE("Sprites are hidden");
	for (i = mms_lp - 1; i >= 0; i--)
		mms_unshow(mms_lifo[i]);
	for (i = mms_zp - 1; i >= 0; i--)
		mms_unshow(mms_zlifo[i]);
	mms_hideall = 1;
}

MMG_FN void mms_restore(void)
{
	int i;

	if (!mms_hideall)
		MM_RAISE("Sprites are not hidden");
	for (i = 0; i < mms_zp; i++)
		mms_show_px(mms_zlifo[i], mms[mms_zlifo[i]].x,
			    mms[mms_zlifo[i]].y, 0);
	for (i = 0; i < mms_lp; i++) {
		struct mmb_sprite *sb = &mms[mms_lifo[i]];

		if (sb->nx != MMS_INACTIVE) {
			sb->x = sb->nx;
			sb->nx = MMS_INACTIVE;
		}
		if (sb->ny != MMS_INACTIVE) {
			sb->y = sb->ny;
			sb->ny = MMS_INACTIVE;
		}
		mms_show_px(mms_lifo[i], sb->x, sb->y, 0);
	}
	mms_hideall = 0;
	mms_collisions(0);
}

MMG_FN void mms_move(void)
{
	int i;

	if (mms_hideall)
		MM_RAISE("Sprites are hidden");
	for (i = mms_lp - 1; i >= 0; i--)
		mms_unshow(mms_lifo[i]);
	for (i = mms_zp - 1; i >= 0; i--)
		mms_unshow(mms_zlifo[i]);
	for (i = 0; i < mms_zp; i++) {
		struct mmb_sprite *sb = &mms[mms_zlifo[i]];

		if (sb->nx != MMS_INACTIVE) {
			sb->x = sb->nx;
			sb->nx = MMS_INACTIVE;
		}
		if (sb->ny != MMS_INACTIVE) {
			sb->y = sb->ny;
			sb->ny = MMS_INACTIVE;
		}
		mms_show_px(mms_zlifo[i], sb->x, sb->y, 0);
	}
	for (i = 0; i < mms_lp; i++) {
		struct mmb_sprite *sb = &mms[mms_lifo[i]];

		if (sb->nx != MMS_INACTIVE) {
			sb->x = sb->nx;
			sb->nx = MMS_INACTIVE;
		}
		if (sb->ny != MMS_INACTIVE) {
			sb->y = sb->ny;
			sb->ny = MMS_INACTIVE;
		}
		mms_show_px(mms_lifo[i], sb->x, sb->y, 0);
	}
	mms_collisions(0);
}

MMG_FN void mms_next(MMINTEGER bn, MMINTEGER x, MMINTEGER y)
{
	struct mmb_sprite *sb = mms_get(bn);
	int hres, vres;

	if (sb == NULL)
		return;
	if (!sb->alloc)
		MM_RAISE("Buffer not in use");
	hres = (int)mm_hres();
	vres = (int)mm_vres();
	if (x < -sb->w + 1 || x > hres - 1 || y < -sb->h + 1 || y > vres - 1)
		MM_RAISE("Invalid sprite coordinates");
	sb->nx = (short)x;
	sb->ny = (short)y;
}

MMG_FN void mms_write(MMINTEGER bn, MMINTEGER xi, MMINTEGER yi,
		      MMINTEGER flags)
{
	struct mmb_sprite *sb = mms_get(bn);
	int mode = 4, hres, vres;

	if (sb == NULL)
		return;
	if (!sb->alloc || sb->img == NULL)
		MM_RAISE("Buffer not in use");
	hres = (int)mm_hres();
	vres = (int)mm_vres();
	if (xi < -sb->w + 1 || xi > hres || yi < -sb->h + 1 || yi > vres)
		MM_RAISE("Invalid sprite coordinates");
	if (flags < 0 || flags > 7)
		MM_RAISE("Invalid sprite flags");
	/* WRITE's orientations 0-3 draw every pixel and 4-7 (the default)
	 * leave the transparent ones, the other way round from SHOW's; a
	 * sprite showing keeps its own orientation (#90) */
	{
		unsigned char keep = sb->rot;

		sb->rot = (unsigned char)((flags & 3) | ((flags & 4) ? 0 : 4));
		mms_show_px((int)bn, (int)xi, (int)yi, mode);
		sb->rot = keep;
	}
}

MMG_FN void mms_read(MMINTEGER bn, MMINTEGER xi, MMINTEGER yi,
		     MMINTEGER wi, MMINTEGER hi)
{
	struct mmb_sprite *sb = mms_get(bn);
	int stride = 0, bpp = 0, hres, vres, headless, i, j;
	int x = (int)xi, y = (int)yi, w = (int)wi, h = (int)hi;

	if (sb == NULL)
		return;
	if (w < 1 || h < 1)
		return;
	if (sb->img == NULL) {
		mms_alloc((int)bn, w, h);
		if (sb->img == NULL)
			return;
	} else {
		if (sb->mymaster != -1)
			MM_RAISE("Can't read into a copy");
		if (sb->master > 0)
			MM_RAISE("Copies exist");
		if (!(sb->w == w && sb->h == h))
			MM_RAISE("Existing buffer is incorrect size");
	}
	headless = mmb_geom(&stride, &bpp, &hres, &vres);
	if (headless)
		return;
	for (i = 0; i < h; i++) {
		int yy = y + i, x0 = x, ww = w, s0 = 0;

		if (yy < 0 || yy >= vres)
			continue;
		if (x0 < 0) { s0 = -x0; ww += x0; x0 = 0; }
		if (x0 + ww > hres) ww = hres - x0;
		if (ww < 1)
			continue;
		if (mmb_row_get(yy, x0, ww, stride, bpp, mmb_rowpx) == 0)
			for (j = 0; j < ww; j++)
				sb->img[(size_t)i * w + s0 + j] = mmb_rowpx[j];
	}
}

MMG_FN void mms_copy(MMINTEGER bn, MMINTEGER first, MMINTEGER count)
{
	struct mmb_sprite *sb = mms_get(bn);
	int c1, n1;

	if (sb == NULL)
		return;
	if (!sb->alloc || sb->img == NULL)
		MM_RAISE("Buffer not in use");
	if (first < 1 || first > MMS_MAX)
		MM_RAISE("Invalid sprite number");
	if (count < 1 || count > MMS_MAX + 1 - first)	/* the last is 64 */
		MM_RAISE("Invalid copy count");
	for (c1 = (int)first, n1 = (int)count; n1; n1--, c1++) {
		if (c1 > MMS_MAX)
			MM_RAISE("Invalid sprite number");
		if (mms[c1].alloc && mms[c1].img != NULL)
			MM_RAISE("Buffer already in use");
		if (sb->master == -1)
			MM_RAISE("Can't copy a copy");
	}
	for (c1 = (int)first, n1 = (int)count; n1; n1--, c1++) {
		struct mmb_sprite *cp = &mms[c1];

		cp->img = sb->img;
		cp->w = sb->w;
		cp->h = sb->h;
		cp->bnd = NULL;
		memset(cp->bgc, 0, sizeof(cp->bgc));
		cp->store = (unsigned char *)malloc((size_t)sb->w * sb->h);
		if (cp->store == NULL)
			MM_RAISE("Not enough memory");
		cp->x = MMS_INACTIVE;
		cp->y = MMS_INACTIVE;
		cp->nx = MMS_INACTIVE;
		cp->ny = MMS_INACTIVE;
		cp->layer = -1;
		cp->mymaster = (signed char)bn;
		cp->master = -1;
		cp->edges = 0;
		cp->rot = 0;
		cp->active = 0;
		cp->alloc = 1;
		sb->master++;	/* its copies, counted (#91) */
		sb->lastcoll = 0;
	}
	mms_any = 1;
	mms[0].alloc = 1;
}

MMG_FN void mms_swap(MMINTEGER bn, MMINTEGER rn, MMINTEGER flags)
{
	struct mmb_sprite *sb = mms_get(bn);
	struct mmb_sprite *rb;
	int mode = 2;

	if (sb == NULL)
		return;
	rb = mms_get(rn);
	if (rb == NULL)
		return;
	if (mms_hideall)
		MM_RAISE("Sprites are hidden");
	if (!sb->alloc || sb->img == NULL || !sb->active)
		MM_RAISE("Original buffer not displayed");
	if (!rb->alloc || rb->img == NULL)
		MM_RAISE("New buffer not defined");
	if (rb->active)
		MM_RAISE("New buffer already displayed");
	if (!(rb->w == sb->w && rb->h == sb->h))
		MM_RAISE("Size mismatch");
	if (flags < 0 || flags > 7)
		MM_RAISE("Invalid sprite flags");
	/* master and mymaster describe each sprite's own image, which SWAP
	 * does not exchange (#89).  The new sprite inherits the old one's saved background.  The
	 * reference aliases the store POINTER; copying the bytes gives
	 * the same picture without two sprites owning one block (the
	 * master's img and store are one allocation here). */
	memcpy(rb->store, sb->store, (size_t)sb->w * sb->h);
	rb->x = sb->x;
	rb->y = sb->y;
	rb->layer = sb->layer;
	rb->lastcoll = sb->lastcoll;
	if (rb->layer == 0)
		mms_lifo_swap(mms_zlifo, mms_zp, (int)bn, (int)rn);
	else
		mms_lifo_swap(mms_lifo, mms_lp, (int)bn, (int)rn);
	sb->x = MMS_INACTIVE;
	sb->y = MMS_INACTIVE;
	sb->layer = -1;
	sb->nx = MMS_INACTIVE;
	sb->ny = MMS_INACTIVE;
	sb->active = 0;
	sb->lastcoll = 0;
	rb->rot = (unsigned char)flags;	/* 4-7 opaque: mms_show_px */
	mms_show_px((int)rn, rb->x, rb->y, mode);
	mms_sanity();
}

MMG_FN void mms_close(MMINTEGER bn)
{
	struct mmb_sprite *sb = mms_get(bn);

	if (sb == NULL)
		return;
	if (mms_hideall)
		MM_RAISE("Sprites are hidden");
	if (!sb->alloc || sb->img == NULL)
		MM_RAISE("Buffer not in use");
	if (sb->master > 0)
		MM_RAISE("Copies still open");
	if (sb->active) {
		mms_unshow((int)bn);
		if (sb->layer == 0)
			mms_lifo_remove(mms_zlifo, &mms_zp, (int)bn);
		else
			mms_lifo_remove(mms_lifo, &mms_lp, (int)bn);
		mms_layer_n[(int)sb->layer]--;
		mms_inuse--;
	}
	free(sb->bnd);
	sb->bnd = NULL;
	if (sb->mymaster == -1)
		free(sb->img);	/* img and store are one block */
	else {
		if (mms[(int)sb->mymaster].master > 0)
			mms[(int)sb->mymaster].master--;	/* one copy fewer */
		free(sb->store);	/* a copy owns only its store */
	}
	sb->img = NULL;
	sb->store = NULL;
	sb->master = -1;
	sb->mymaster = -1;
	sb->x = MMS_INACTIVE;
	sb->y = MMS_INACTIVE;
	sb->w = 0;
	sb->h = 0;
	sb->nx = MMS_INACTIVE;
	sb->ny = MMS_INACTIVE;
	sb->layer = -1;
	sb->active = 0;
	sb->edges = 0;
	mms_sanity();
}

MMG_FN void mms_close_all(void)
{
	int i;

	for (i = 1; i <= MMS_MAX; i++) {
		struct mmb_sprite *sb = &mms[i];

		if (!sb->alloc || sb->img == NULL)
			continue;
		if (sb->active)
			mms_unshow(i);
		free(sb->bnd);
		sb->bnd = NULL;
		if (sb->mymaster == -1)
			free(sb->img);
		else
			free(sb->store);
		sb->img = NULL;
		sb->store = NULL;
		sb->master = -1;
		sb->mymaster = -1;
		sb->active = 0;
		sb->layer = -1;
		sb->alloc = 0;
	}
	mms_lp = mms_zp = mms_inuse = mms_hideall = 0;
	memset(mms_layer_n, 0, sizeof(mms_layer_n));
	memset(mms_st, 0, sizeof(mms_st));
	mms_st_found = 0;
	mms_hit_st = -1;
	mms_st_which = -1;
	mms_coll_found = 0;
	mms_which = -1;
}

/* centre-point wrap for SCROLL: the reference's own arithmetic, with
 * Y INVERTED relative to X (Sprite.c:2110-2121) - a quirk, and kept. */
MMG_FN int mms_wrapc(int centre, int delta, int limit)
{
	centre += delta;
	if (centre >= limit)
		centre -= limit;
	if (centre < 0)
		centre += limit;
	return centre;
}

/* SPRITE SCROLL x, y [, colour] - the whole scene moves: layer-0
 * sprites and statics wrap with the background, layers 1-4 are hidden,
 * the frame scrolls in the kernel (GFXIOC_SCROLL2: wrap by default,
 * -1 leaves the band, a colour fills it), pending NEXT positions are
 * applied, everything reshows, and the background collision sweep
 * runs - Sprite.c:2081-2208 in order. */
MMG_FN void mms_scroll(MMINTEGER xi, MMINTEGER yi, MMINTEGER blank)
{
	int hres = (int)mm_hres(), vres = (int)mm_vres(), i;
	int x = (int)xi, y = (int)yi;

	if (mms_hideall)
		MM_RAISE("Sprites are hidden");
	if (x < -hres / 2 - 1 || x > hres || y < -vres / 2 - 1 || y > vres)
		MM_RAISE("Invalid sprite coordinates");
	if (blank < -2 || blank > 0xFFFFFF)
		MM_RAISE("Invalid colour");
	if (x == 0 && y == 0)
		return;
	for (i = mms_lp - 1; i >= 0; i--)
		mms_unshow(mms_lifo[i]);
	for (i = mms_zp - 1; i >= 0; i--) {
		struct mmb_sprite *sb = &mms[mms_zlifo[i]];
		int xs = sb->x + (sb->w >> 1);
		int ys = sb->y + (sb->h >> 1);

		mms_unshow(mms_zlifo[i]);
		sb->x = (short)(mms_wrapc(xs, x, hres) - (sb->w >> 1));
		sb->y = (short)(mms_wrapc(ys, -y, vres) - (sb->h >> 1));
	}
	for (i = 1; i <= MMS_MAXST; i++) {
		struct mmb_stobj *st = &mms_st[i];
		int bx, by;

		if (!st->active)
			continue;
		bx = st->x + (st->w >> 1);
		by = st->y + (st->h >> 1);
		st->x = (short)(mms_wrapc(bx, x, hres) - (st->w >> 1));
		st->y = (short)(mms_wrapc(by, -y, vres) - (st->h >> 1));
	}
	mm_fb_scroll2(x, y, blank);
	for (i = 0; i < mms_zp; i++)
		mms_show_px(mms_zlifo[i], mms[mms_zlifo[i]].x,
			    mms[mms_zlifo[i]].y, 0);
	for (i = 0; i < mms_lp; i++) {
		struct mmb_sprite *sb = &mms[mms_lifo[i]];

		if (sb->nx != MMS_INACTIVE) {
			sb->x = sb->nx;
			sb->nx = MMS_INACTIVE;
		}
		if (sb->ny != MMS_INACTIVE) {
			sb->y = sb->ny;
			sb->ny = MMS_INACTIVE;
		}
		mms_show_px(mms_lifo[i], sb->x, sb->y, 0);
	}
	mms_collisions(0);
}

MMG_FN void mms_static(MMINTEGER n, MMINTEGER x, MMINTEGER y,
		       MMINTEGER w, MMINTEGER h, MMINTEGER off)
{
	if (n < 1 || n > MMS_MAXST)
		MM_RAISE("Invalid static object");
	if (off) {
		mms_st[n].active = 0;
		mms_st[n].x = mms_st[n].y = mms_st[n].w = mms_st[n].h = 0;
		return;
	}
	if (w < 1 || w > 32767 || h < 1 || h > 32767)
		MM_RAISE("Invalid static object size");
	mms_st[n].x = (short)x;
	mms_st[n].y = (short)y;
	mms_st[n].w = (short)w;
	mms_st[n].h = (short)h;
	mms_st[n].active = 1;
}

MMG_FN void mms_static_clear(void)
{
	memset(mms_st, 0, sizeof(mms_st));
	mms_st_found = 0;
	mms_hit_st = -1;
	mms_st_which = -1;
}

MMG_FN void mms_set_transparent(MMINTEGER c)
{
	if (c < 0 || c > 15)
		MM_RAISE("Invalid transparent colour");
	mms_transparent = (unsigned char)c;
}

/* ---- SPRITE LOAD ------------------------------------------------------
 * The .spr text format: comment lines start with a quote, the first
 * data line is "width, count [, height]", then count blocks of height
 * rows of width characters.  The reference's colour tables reduced to
 * RGB121 indices - pure bit extraction, so this works headless too. */

static const unsigned char mms_pal0[16] = {
	0, 1, 6, 7, 8, 9, 14, 15, 2, 3, 4, 5, 10, 11, 12, 13
};
static const unsigned char mms_pal1[16] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
};

MMG_FN int mms_getline(FILE *f, char *buf, int n)
{
	int i = 0, c;

	for (;;) {
		c = fgetc(f);
		if (c == EOF)
			return i ? i : -1;
		if (c == '\n')
			return i;
		if (c == '\r')
			continue;
		if (i < n - 1)
			buf[i++] = (char)c;
		buf[i] = 0;
	}
}

/*
 * The numbers on the header line: "width, count [, height]".
 *
 * BY HAND, AND NOT WITH sscanf.  bcrun resolves a NAMED SUBSET of the C
 * library at load time (lib_fast[] in bcrun.c) and sscanf is not in it,
 * so a program with SPRITE LOAD anywhere in it died with
 *
 *	bcrun: no runtime function "sscanf"
 *
 * before a line of it ran - reported from the board, and invisible to
 * every host gate, because the gates link glibc and find sscanf there.
 * A header compiled into every generated program may only call what
 * bcrun provides.
 *
 * Separators are comma AND space, which is what the reference's
 * getargs(&z, 5, ", ") accepts, so "8,4", "8, 4, 8" and "8 4 8" all
 * read the same way.  Returns how many numbers were found.
 */
MMG_FN int mms_nums(const char *s, int *v, int max)
{
	int n = 0, val, neg, any;

	while (n < max) {
		while (*s == ' ' || *s == '\t' || *s == ',')
			s++;
		neg = 0;
		if (*s == '-' || *s == '+') {
			neg = (*s == '-');
			s++;
		}
		val = 0;
		any = 0;
		while (*s >= '0' && *s <= '9') {
			val = val * 10 + (*s++ - '0');
			any = 1;
		}
		if (!any)
			break;
		v[n++] = neg ? -val : val;
	}
	return n;
}

MMG_FN void mms_load(const char *file, MMINTEGER start, MMINTEGER mode)
{
	char line[520], name[300];
	FILE *f;
	const unsigned char *pal = mode ? mms_pal1 : mms_pal0;
	int width, number, height = 0, bnbr, lc, i, n;

	if (start < 1 || start > MMS_MAX)
		MM_RAISE("Invalid sprite number");
	if (mode < 0 || mode > 1)
		MM_RAISE("Invalid mode");
	/* AppendDefaultExtension(".spr") */
	strncpy(name, mm_cstr(file), sizeof(name) - 8);
	name[sizeof(name) - 8] = 0;
	if (strchr(name, '.') == NULL)
		strcat(name, ".spr");
	f = fopen(name, "rb");
	if (f == NULL)
		MM_RAISE("File not found");
	do {
		n = mms_getline(f, line, sizeof(line));
	} while (n >= 0 && line[0] == 39);
	if (n < 0) {
		fclose(f);
		MM_RAISE("Invalid sprite file");
	}
	{
		int v[3], got = mms_nums(line, v, 3);

		if (got < 2) {
			fclose(f);
			MM_RAISE("Invalid sprite file");
		}
		width = v[0];
		number = v[1];
		/* The third is optional, and the reference's own default
		   for a missing or zero one is a SQUARE sprite. */
		height = (got >= 3) ? v[2] : 0;
	}
	if (height == 0)
		height = width;
	/* a line of the file is at most 255 characters (#91) */
	if (width < 1 || width > 255 || height < 1 || number < 1) {
		fclose(f);
		MM_RAISE("Invalid sprite file");
	}
	if (number + start - 1 > MMS_MAX) {	/* the last is 64 (#64) */
		fclose(f);
		MM_RAISE("Maximum of 64 sprites");
	}
	for (bnbr = (int)start; bnbr < number + (int)start; bnbr++) {
		struct mmb_sprite *sb = &mms[bnbr];

		if (sb->img == NULL) {
			mms_alloc(bnbr, width, height);
			if (sb->img == NULL) {
				fclose(f);
				return;
			}
		} else {
			/* an existing sprite, as SPRITE READ (#64) */
			if (sb->mymaster != -1) {
				fclose(f);
				MM_RAISE("Can't read into a copy");
			}
			if (sb->master > 0) {
				fclose(f);
				MM_RAISE("Copies exist");
			}
			if (!(sb->w == width && sb->h == height)) {
				fclose(f);
				MM_RAISE("Existing buffer is incorrect size");
			}
		}
		for (lc = 0; lc < height; lc++) {
			do {
				n = mms_getline(f, line, sizeof(line));
			} while (n >= 0 && line[0] == 39);
			if (n < 0)
				n = 0;
			line[sizeof(line) - 1] = 0;
			for (i = (int)strlen(line); i < width; i++)
				line[i] = ' ';
			for (i = 0; i < width; i++) {
				int ci = -1;
				char ch = line[i];

				if (ch >= '0' && ch <= '9')
					ci = ch - '0';
				else if (ch >= 'A' && ch <= 'F')
					ci = ch - 'A' + 10;
				else if (ch >= 'a' && ch <= 'f')
					ci = ch - 'a' + 10;
				sb->img[(size_t)lc * width + i] =
				    (ci >= 0) ? pal[ci] : 0;
			}
		}
	}
	fclose(f);
}

/* SPRITE LOADARRAY [#]n, w, h, array() - the array holds RGB888
 * colours; RGB121 is pure bit extraction, as the reference does. */
/* ---- SPRITE LOADPNG #n, f$ [, transparent [, cutoff [, x, y, w, h]]] -
 *
 *	The decoding happens in ANOTHER PROCESS - /usr/bin/loadpng with
 *	its -s flag - and the sprite comes back down a pipe.  upng has to
 *	inflate a whole image before a pixel of it exists (see loadpng.c),
 *	which needs the PSRAM arena and far more code than belongs in
 *	every BASIC program; as a program it costs a resident one nothing.
 *
 *	What arrives is exactly this buffer's shape: two 16-bit sizes,
 *	then one COLOUR INDEX PER BYTE, row major.  One byte per pixel and
 *	not the reference's packed 4bpp because that is what mms_alloc
 *	gives us - the same divergence the blit buffers make.  So the read
 *	goes straight into sb->img with no unpacking at all.
 *
 *	`transparent` carries MMBasic's sign trick, and it is passed
 *	through untouched: 0-15 is the index given to pixels below the
 *	cutoff, and -n means "substitute index n for opaque black".  The
 *	loader applies it; nothing here needs to know.  xo, yo, wi and hi
 *	are the part of the image (PicoMite 4d91e6c), as mms_loadbmp's:
 *	-1 for wi or hi is the rest of it, and the loader checks it.
 */
MMG_FN void mms_loadpng(MMINTEGER bn, const char *file, MMINTEGER targ,
			MMINTEGER cutoff, MMINTEGER xo, MMINTEGER yo,
			MMINTEGER wi, MMINTEGER hi)
{
	struct mmb_sprite *sb = mms_get(bn);
	unsigned char hdr[4];
	int fd, w, h, got, n;

	if (sb == NULL)
		return;
	if (sb->img != NULL)
		MM_RAISE("Buffer already in use");
	if (targ < -15 || targ > 15)
		MM_RAISE("Invalid transparent colour");
	if (cutoff < 1 || cutoff > 254)
		MM_RAISE("Invalid cutoff");
	if (xo < 0 || yo < 0)
		MM_RAISE("Coordinates");

	mm_run_begin();
	mm_run_arg("\007loadpng");
	mm_run_arg("\002-s");
	mm_run_arg(file);
	mm_run_arg_i(targ);
	mm_run_arg_i(cutoff);
	mm_run_arg_i(xo);
	mm_run_arg_i(yo);
	mm_run_arg_i(wi);
	mm_run_arg_i(hi);
	fd = mm_run_pipe();
	if (fd < 0)
		return;			/* mm_run_pipe raised it */

	/*	The header first, because the size decides the buffer -
	 *	and a short read here means the decoder died before it
	 *	wrote anything, which its own message on stderr will have
	 *	explained.  Reap it either way. */
	got = 0;
	while (got < 4) {
		n = (int)mm_run_pipe_read(fd, hdr + got, 4 - got);
		if (n <= 0)
			break;
		got += n;
	}
	if (got < 4) {
		/*	Close FIRST: if the decoder exited non-zero it has
		 *	already said why, on stderr and as a BASIC error,
		 *	and that message is the useful one. */
		if (mm_run_pipe_close(fd) < 0)
			return;
		MM_RAISE("The PNG could not be decoded");
	}
	w = hdr[0] | (hdr[1] << 8);
	h = hdr[2] | (hdr[3] << 8);
	if (w < 1 || h < 1 || w > (int)mm_hres() || h > (int)mm_vres()) {
		mm_run_pipe_close(fd);
		MM_RAISE("Invalid sprite size");
	}
	mms_alloc((int)bn, w, h);
	if (sb->img == NULL) {
		mm_run_pipe_close(fd);
		return;
	}
	got = 0;
	while (got < w * h) {
		n = (int)mm_run_pipe_read(fd, sb->img + got,
					  w * h - got);
		if (n <= 0)
			break;
		got += n;
	}
	if (mm_run_pipe_close(fd) < 0) {
		mms_unalloc(sb);
		return;			/* a failed decoder is an error */
	}
	if (got < w * h) {
		mms_unalloc(sb);
		MM_RAISE("The PNG could not be decoded");
	}
}

/* ---- SPRITE LOADBMP #n, f$ [, x [, y [, w [, h]]]] ------------------
 *
 *	The same shape as LOADPNG above, and for a different reason: the
 *	BMP decoder is not big, but it is /usr/bin/loadimage's already -
 *	six bit depths, BI_BITFIELDS, RLE4 and RLE8, bottom-up and
 *	top-down - and a second copy compiled into every BASIC program
 *	that loads a sprite would be a second thing to keep right.
 *	`loadimage -s` writes the sprite down a pipe instead.
 *
 *	The four optional arguments are the reference's window into the
 *	image, and the loader applies them: a window that runs off the
 *	picture is an error there, not a clipped sprite.
 *
 *	A name with no extension gets ".bmp", as the reference gives it:
 *	loadimage applies the rule, as it does for LOAD IMAGE, and
 *	saveimage does the same for SAVE IMAGE.
 */
MMG_FN void mms_loadbmp(MMINTEGER bn, const char *file, MMINTEGER xo,
			MMINTEGER yo, MMINTEGER wi, MMINTEGER hi)
{
	struct mmb_sprite *sb = mms_get(bn);
	unsigned char hdr[4];
	int fd, w, h, got, n;

	if (sb == NULL)
		return;
	if (sb->img != NULL)
		MM_RAISE("Buffer already in use");
	if (xo < 0 || yo < 0)
		MM_RAISE("Coordinates");

	mm_run_begin();
	mm_run_arg("\011loadimage");
	mm_run_arg("\002-s");
	mm_run_arg(file);
	mm_run_arg_i(xo);
	mm_run_arg_i(yo);
	mm_run_arg_i(wi);
	mm_run_arg_i(hi);
	fd = mm_run_pipe();
	if (fd < 0)
		return;			/* mm_run_pipe raised it */

	got = 0;
	while (got < 4) {
		n = (int)mm_run_pipe_read(fd, hdr + got, 4 - got);
		if (n <= 0)
			break;
		got += n;
	}
	if (got < 4) {
		/*	Close FIRST, as LOADPNG does: a decoder that
		 *	exited non-zero has already said why, and that
		 *	message is the useful one. */
		if (mm_run_pipe_close(fd) < 0)
			return;
		MM_RAISE("The BMP could not be decoded");
	}
	w = hdr[0] | (hdr[1] << 8);
	h = hdr[2] | (hdr[3] << 8);
	if (w < 1 || h < 1 || w > (int)mm_hres() || h > (int)mm_vres()) {
		mm_run_pipe_close(fd);
		MM_RAISE("Invalid sprite size");
	}
	mms_alloc((int)bn, w, h);
	if (sb->img == NULL) {
		mm_run_pipe_close(fd);
		return;
	}
	got = 0;
	while (got < w * h) {
		n = (int)mm_run_pipe_read(fd, sb->img + got, w * h - got);
		if (n <= 0)
			break;
		got += n;
	}
	if (mm_run_pipe_close(fd) < 0) {
		mms_unalloc(sb);
		return;			/* a failed decoder is an error */
	}
	if (got < w * h) {
		mms_unalloc(sb);
		MM_RAISE("The BMP could not be decoded");
	}
}

MMG_FN void mms_loadarray(MMINTEGER bn, MMINTEGER wi, MMINTEGER hi,
			  const MMINTEGER *a, MMINTEGER count)
{
	struct mmb_sprite *sb = mms_get(bn);
	int w = (int)wi, h = (int)hi, i;

	if (sb == NULL)
		return;
	if (w < 1 || w > (int)mm_hres() || h < 1 || h > (int)mm_vres())
		MM_RAISE("Invalid sprite size");
	if (sb->img != NULL)
		MM_RAISE("Buffer already in use");
	if (count < (MMINTEGER)w * h)
		MM_RAISE("Array Dimensions");
	mms_alloc((int)bn, w, h);
	if (sb->img == NULL)
		return;
	for (i = 0; i < w * h; i++) {
		MMINTEGER c = a[i];

		sb->img[i] = (unsigned char)(((c & 0x800000) >> 20) |
					     ((c & 0x00C000) >> 13) |
					     ((c & 0x000080) >> 7));
	}
}

/* ---- the SPRITE(...) function ----------------------------------------
 * Integer selectors through one entry point; V and D, which return
 * radians and pixels, through the float one.  The letters become the
 * reference's t codes in the translator. */

/* getspritebounds: for each row the first and last opaque column, for
 * each column the first and last opaque row, from the image as it is
 * and the transparent colour as it is when first asked for (the
 * reference keeps them from then on, and so does this) */
MMG_FN short *mms_bounds(struct mmb_sprite *sb)
{
	int w = sb->w, h = sb->h, x, y;
	short *b;

	if (sb->bnd != NULL)
		return sb->bnd;
	b = (short *)malloc(sizeof(short) * (size_t)(h + h + w + w));
	if (b == NULL)
		MM_RAISEV("Not enough memory", (short *)0);
	for (x = 0; x < w; x++) {
		b[h + h + x] = 0x7FFF;		/* top */
		b[h + h + w + x] = -1;		/* bottom */
	}
	for (y = 0; y < h; y++) {
		b[y] = (short)w;		/* left */
		b[h + y] = -1;			/* right */
		for (x = 0; x < w; x++) {
			if (sb->img[(size_t)y * w + x] == mms_transparent)
				continue;
			if (b[y] == w)
				b[y] = (short)x;
			b[h + y] = (short)x;
			b[h + h + w + x] = (short)y;
			if (b[h + h + x] == 0x7FFF)
				b[h + h + x] = (short)y;
		}
	}
	sb->bnd = b;
	return b;
}

/* one opaque background pixel at px, py against the bounds: the
 * sides' penetration, and 2 when it is inside the sprite's shape */
MMG_FN int mms_bpixel(struct mmb_sprite *sb, const short *b, int px, int py)
{
	int w = sb->w, h = sb->h, ret = 1;
	int bx = (sb->rot & 1) ? w - 1 - px : px;
	int by = (sb->rot & 2) ? h - 1 - py : py;
	short *g = sb->bgc;

	if (px > g[0]) g[0] = (short)px;
	if (px < g[1]) g[1] = (short)px;
	if (py > g[2]) g[2] = (short)py;
	if (py < g[3]) g[3] = (short)py;
	if (bx >= b[by] && bx <= b[h + by]) {
		if (bx - b[by] > g[4]) { g[4] = (short)(bx - b[by]); ret = 2; }
		if (b[h + by] - bx > g[5]) { g[5] = (short)(b[h + by] - bx); ret = 2; }
	}
	if (by >= b[h + h + bx] && by <= b[h + h + w + bx]) {
		if (by - b[h + h + bx] > g[6]) { g[6] = (short)(by - b[h + h + bx]); ret = 2; }
		if (b[h + h + w + bx] - by > g[7]) { g[7] = (short)(b[h + h + w + bx] - by); ret = 2; }
	}
	return ret;
}

/* SPRITE(B, n) - fun_sprite's t 14: the sprite's opaque pixels against
 * the background stored under it.  0 none of the background is opaque,
 * 1 some is, 2 some is inside the sprite's shape; the eight sides are
 * SPRITE(B, n, side).  A sprite not showing answers 0 and zeroes them
 * (PicoMite #92). */
MMG_FN MMINTEGER mms_spriteb(int bnbr)
{
	struct mmb_sprite *sb = &mms[bnbr];
	short *g = sb->bgc, *b;
	int sw = sb->w, sh = sb->h, px, py, r, iret = 0;

	if (!(sb->alloc && sb->active)) {
		memset(g, 0, sizeof(sb->bgc));
		return 0;
	}
	b = mms_bounds(sb);
	if (b == NULL)
		return 0;
	g[0] = -1; g[1] = 0x7FFF; g[2] = -1; g[3] = 0x7FFF;
	g[4] = g[5] = g[6] = g[7] = -1;
	for (py = 0; py < sh; py++)
		for (px = 0; px < sw; px++) {
			if (sb->store[(size_t)py * sw + px] == mms_transparent)
				continue;
			r = mms_bpixel(sb, b, px, py);
			if (r > iret)
				iret = r;
		}
	g[0] = (short)(g[0] == -1 ? 0 : g[0] + 1);
	g[1] = (short)(g[1] == 0x7FFF ? 0 : sw - g[1]);
	g[2] = (short)(g[2] == -1 ? 0 : g[2] + 1);
	g[3] = (short)(g[3] == 0x7FFF ? 0 : sh - g[3]);
	for (r = 4; r < 8; r++)
		g[r] = (short)(g[r] == -1 ? 0 : g[r] + 1);
	if (g[0] == sw) g[0] = 0;
	if (g[1] == sw) g[1] = 0;
	if (g[2] == sh) g[2] = 0;
	if (g[3] == sh) g[3] = 0;
	if (g[4] != 0 && g[5] != 0) {
		if (g[4] < g[5]) g[5] = 0;
		else if (g[4] > g[5]) g[4] = 0;
		else g[4] = g[5] = 0;	/* both: a top or bottom hit */
	}
	if (g[6] != 0 && g[7] != 0) {
		if (g[6] < g[7]) g[7] = 0;
		else if (g[6] > g[7]) g[6] = 0;
		else g[6] = g[7] = 0;	/* both: a left or right hit */
	}
	return iret;
}

MMG_FN MMINTEGER mms_fun(int t, MMINTEGER bni, MMINTEGER third, int nargs)
{
	int bnbr = (int)bni, w = -1, h = -1;
	int x = MMS_INACTIVE, y = MMS_INACTIVE, l = 0, c = 0;
	struct mmb_sprite *sb;

	if (t == 12) {		/* N [, layer] */
		if (nargs >= 2) {
			if (bni < 0 || bni > MMS_MAXLAYER)
				MM_RAISEV("Invalid sprite layer", 0);
			return mms_layer_n[bni];
		}
		return mms_inuse;
	}
	if (t == 13)		/* S */
		return mms_which;
	if (t == 14) {		/* B, n [, side] */
		if (bni < 1 || bni > MMS_MAX)
			MM_RAISEV("Invalid sprite number", 0);
		if (nargs < 2)
			return mms_spriteb((int)bni);
		if (third < 0 || third > 7)
			MM_RAISEV("Invalid side", 0);
		return mms[bni].bgc[third];
	}
	if (bnbr < 0 || bnbr > MMS_MAX)
		MM_RAISEV("Invalid sprite number", 0);
	sb = &mms[bnbr];
	if (bnbr == 0) {
		if (!mms_any)
			MM_RAISEV("No sprites defined", 0);
		if (nargs >= 2) {
			if (third < 1 || third > sb->coll[0])
				MM_RAISEV("Invalid collision index", 0);
			c = sb->coll[third];
		} else
			c = sb->coll[0];
	}
	if (sb->alloc && sb->img != NULL) {
		w = sb->w;
		h = sb->h;
	}
	if (sb->alloc && sb->active) {
		x = sb->x;
		y = sb->y;
		l = sb->layer;
		if (bnbr != 0) {
			if (nargs >= 2) {
				if (third < 1 || third > sb->coll[0])
					MM_RAISEV("Invalid collision index", 0);
				c = sb->coll[third];
			} else
				c = sb->coll[0];
		}
	}
	switch (t) {
	case 1: return w;
	case 2: return h;
	case 3: return (sb->alloc && sb->active) ? x : MMS_INACTIVE;
	case 4: return (sb->alloc && sb->active) ? y : MMS_INACTIVE;
	case 5: return (sb->alloc && sb->active) ? l : -1;
	case 6: return (sb->alloc && sb->coll[0]) ? c : -1;
	case 8: return (sb->alloc && sb->active) ? sb->lastcoll : 0;
	case 9: return (sb->alloc && sb->active) ? sb->edges : 0;
	case 11: return sb->alloc ? (MMINTEGER)(long)sb->img : 0;
	}
	MM_RAISEV("Invalid sprite selector", 0);
}

MMG_FN MMFLOAT mms_fun_f(int t, MMINTEGER bni, MMINTEGER rni)
{
	int bnbr = (int)bni, rbnbr = (int)rni;
	int x = 0, y = 0, w = 0, h = 0, x1 = 0, y1 = 0, w1 = 0, h1 = 0;
	struct mmb_sprite *sb, *rb;

	if (bnbr < 0 || bnbr > MMS_MAX || rbnbr < 1 || rbnbr > MMS_MAX)
		MM_RAISEV("Invalid sprite number", -1.0);
	sb = &mms[bnbr];
	rb = &mms[rbnbr];
	if (sb->alloc && sb->img != NULL) { w = sb->w; h = sb->h; }
	if (sb->alloc && sb->active) { x = sb->x; y = sb->y; }
	if (rb->alloc && rb->img != NULL) { w1 = rb->w; h1 = rb->h; }
	if (rb->alloc && rb->active) { x1 = rb->x; y1 = rb->y; }
	if (!(sb->alloc && sb->active && rb->alloc && rb->active))
		return -1.0;
	x += w / 2;
	y += h / 2;
	x1 += w1 / 2;
	y1 += h1 / 2;
	if (t == 10)
		return sqrt((MMFLOAT)((x1 - x) * (x1 - x) +
				      (y1 - y) * (y1 - y)));
	{
		MMFLOAT v = atan2((MMFLOAT)(y1 - y), (MMFLOAT)(x1 - x));

		v += 1.5707963267948966;
		if (v < 0)
			v += 6.283185307179586;
		return v;
	}
}

MMG_FN MMINTEGER mms_fun_st(int kind, MMINTEGER n, int prop)
{
	if (kind == 1)		/* ST, COLLISION */
		return mms_hit_st;
	if (kind == 2)		/* ST, OBJECT */
		return mms_st_which;
	if (n < 1 || n > MMS_MAXST)
		MM_RAISEV("Invalid static object", 0);
	switch (prop) {
	case 1: return mms_st[n].active ? mms_st[n].x : -1;
	case 2: return mms_st[n].active ? mms_st[n].y : -1;
	case 3: return mms_st[n].active ? mms_st[n].w : -1;
	case 4: return mms_st[n].active ? mms_st[n].h : -1;
	case 5: return mms_st[n].active;
	}
	MM_RAISEV("Invalid sprite selector", 0);
}

#endif /* MMB_SPRITE_H */
