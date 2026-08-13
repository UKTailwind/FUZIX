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
 *	kernel's SCROLL2); SPRITE(B...) bounds analysis, LOADPNG and
 *	LOADBMP are deferred - honest errors, never wrong pictures.
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
};

/* index 0 is the reference's spritebuff[0]: the background pseudo
 * sprite whose collisions[] lists what collided when everything moved */
static struct mmb_sprite mms[MMS_MAX + 1];
static unsigned char mms_lifo[MMS_MAX], mms_zlifo[MMS_MAX];
static unsigned char mms_lp, mms_zp, mms_inuse, mms_hideall;
static unsigned char mms_layer_n[MMS_MAXLAYER + 1];
static unsigned char mms_transparent;	/* SPRITE SET TRANSPARENT, 0-15 */
static unsigned char mms_any;		/* any sprite ever defined */

/* the interrupt-scan flags, MMBasic's CollisionFound machinery */
static unsigned char mms_coll_found, mms_st_found;
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
MMG_FN void mms_hide_px(struct mmb_sprite *sb)
{
	int stride = 0, bpp = 0, hres, vres, i, j;

	sb->active = 0;
	if (mmb_geom(&stride, &bpp, &hres, &vres))
		return;
	for (i = 0; i < sb->h; i++) {
		int y = sb->y + i, x0 = sb->x, w = sb->w, s0 = 0;

		if (y < 0 || y >= vres)
			continue;
		if (x0 < 0) { s0 = -x0; w += x0; x0 = 0; }
		if (x0 + w > hres) w = hres - x0;
		if (w < 1)
			continue;
		for (j = 0; j < w; j++)
			mmb_rowpx[j] = sb->store[(size_t)i * sb->w + s0 + j];
		mmb_row_put(y, x0, w, stride, bpp, mmb_rowpx);
	}
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
	if (sb->img == NULL)
		return;
	if (!(mode == 0 || (mode & 4)) && sb->active && !headless)
		mms_hide_px(sb);
	sb->x = (short)x1;
	sb->y = (short)y1;
	if (headless) {
		if (!(mode & 4))
			sb->active = 1;
		return;
	}
	if (mode != 2) {
		for (i = 0; i < h; i++) {
			int y = y1 + i, x0 = x1, ww = w, s0 = 0;

			if (y < 0 || y >= vres)
				continue;
			if (x0 < 0) { s0 = -x0; ww += x0; x0 = 0; }
			if (x0 + ww > hres) ww = hres - x0;
			if (ww < 1)
				continue;
			if (mmb_row_get(y, x0, ww, stride, bpp, mmb_rowpx) == 0)
				for (j = 0; j < ww; j++)
					sb->store[(size_t)i * w + s0 + j] =
					    mmb_rowpx[j];
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
		if (fullmode & 8) {
			for (j = 0; j < ww; j++) {
				int sc = s0 + j;

				if (sb->rot & 1)
					sc = w - 1 - sc;
				mmb_rowpx[j] = sb->img[(size_t)sr * w + sc];
			}
		} else {
			if (mmb_row_get(y, x0, ww, stride, bpp, mmb_rowpx) < 0)
				continue;
			for (j = 0; j < ww; j++) {
				int sc = s0 + j;
				unsigned char c;

				if (sb->rot & 1)
					sc = w - 1 - sc;
				c = sb->img[(size_t)sr * w + sc];
				if (c != mms_transparent)
					mmb_rowpx[j] = c;
			}
		}
		mmb_row_put(y, x0, ww, stride, bpp, mmb_rowpx);
	}
	if (!(mode & 4))
		sb->active = 1;
}

/* ---- collisions, verbatim ------------------------------------------- */

MMG_FN void mms_checklimits(int bnbr, int *n)
{
	struct mmb_sprite *sb = &mms[bnbr];
	int hres = (int)mm_hres(), vres = (int)mm_vres();

	sb->coll[*n] = 0;
	if (sb->x < 0) {
		if (!(sb->edges & 1)) {
			sb->edges |= 1;
			sb->coll[*n] = 0xF1;
			(*n)++;
		}
	} else
		sb->edges &= (unsigned char)~1;
	if (sb->y < 0) {
		if (!(sb->edges & 2)) {
			sb->edges |= 2;
			if (sb->coll[*n] & 0xF0)
				sb->coll[*n] |= 0xF2;
			else {
				sb->coll[*n] = 0xF2;
				(*n)++;
			}
		}
	} else
		sb->edges &= (unsigned char)~2;
	if (sb->x + sb->w > hres) {
		if (!(sb->edges & 4)) {
			sb->edges |= 4;
			if (sb->coll[*n] & 0xF0)
				sb->coll[*n] |= 0xF4;
			else {
				sb->coll[*n] = 0xF4;
				(*n)++;
			}
		}
	} else
		sb->edges &= (unsigned char)~4;
	if (sb->y + sb->h > vres) {
		if (!(sb->edges & 8)) {
			sb->edges |= 8;
			if (sb->coll[*n] & 0xF0)
				sb->coll[*n] |= 0xF8;
			else {
				sb->coll[*n] = 0xF8;
				(*n)++;
			}
		}
	} else
		sb->edges &= (unsigned char)~8;
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

/* One overlap test and its edge-triggered bookkeeping, factored out
 * because the reference writes it inline three times and the Fuzix
 * cc2 has a per-function node pool the unfactored transcription
 * overflowed ("Too many nodes").  `both` says whether the partner's
 * mask moves too - the background sweep does not touch it. */
MMG_FN void mms_pair(int b, int k, int *n, int both)
{
	struct mmb_sprite *sb = &mms[b];
	struct mmb_sprite *sk = &mms[k];
	MMINTEGER mask = (MMINTEGER)1 << (k - 1);
	MMINTEGER mymask = (MMINTEGER)1 << (b - 1);

	if (!(sk->x + sk->w < sb->x || sk->x > sb->x + sb->w ||
	      sk->y + sk->h < sb->y || sk->y > sb->y + sb->h)) {
		if (*n < MMS_MAXCOLL && !(sb->lastcoll & mask))
			sb->coll[(*n)++] = (unsigned char)k;
		sb->lastcoll |= mask;
		if (both)
			sk->lastcoll |= mymask;
	} else {
		sb->lastcoll &= ~mask;
		if (both)
			sk->lastcoll &= ~mymask;
	}
}

/* One sprite's share of the background sweep; returns its collision
 * count.  Its own function to keep each expression tree inside cc2's
 * per-function node pool. */
MMG_FN int mms_bg_one(int k)
{
	struct mmb_sprite *sk = &mms[k];
	int kk, jj = 1, n = 1, peers;

	memset(sk->coll, 0, MMS_MAXCOLL);
	peers = mms_layer_n[(int)sk->layer] + mms_layer_n[0];
	if (peers > 1)
		for (kk = 1; kk <= MMS_MAX; kk++) {
			struct mmb_sprite *skk = &mms[kk];

			if (!skk->alloc || kk == k)
				continue;
			if (jj == peers)
				break;
			if (skk->layer == sk->layer || skk->layer == 0) {
				jj++;
				mms_pair(k, kk, &n, 0);
			}
		}
	mms_checklimits(k, &n);
	mms_check_st(k, &n);
	return n;
}

/* the background sweep: every active sprite re-checked after the whole
 * scene moved (SPRITE MOVE / RESTORE / SCROLL) */
MMG_FN void mms_coll_bg(void)
{
	int k, j = 0, bcol = 1, n;

	for (k = 1; k <= MMS_MAX; k++) {
		struct mmb_sprite *sk = &mms[k];

		if (!sk->alloc)
			continue;
		if (j == mms_inuse)
			break;
		if (!sk->active)
			continue;
		j++;
		n = mms_bg_one(k);
		if (n > 1 && n < MMS_MAXCOLL && bcol < MMS_MAXCOLL) {
			mms[0].coll[bcol] = (unsigned char)k;
			bcol++;
			sk->coll[0] = (unsigned char)(n - 1);
		}
	}
	if (bcol > 1) {
		mms_coll_found = 1;
		mms_which = 0;
		mms[0].coll[0] = (unsigned char)(bcol - 1);
	}
}

MMG_FN void mms_collisions(int bnbr)
{
	int k, j = 1, n = 1;
	MMINTEGER mask;
	struct mmb_sprite *sb = &mms[bnbr];

	mms_coll_found = 0;
	mms_which = -1;
	memset(mms[0].coll, 0, MMS_MAXCOLL);
	if (bnbr == 0) {
		mms_coll_bg();
		return;
	}
	memset(sb->coll, 0, MMS_MAXCOLL);
	if (sb->layer != 0) {
		if (mms_layer_n[(int)sb->layer] + mms_layer_n[0] > 1)
			for (k = 1; k <= MMS_MAX; k++) {
				struct mmb_sprite *sk = &mms[k];

				if (!sk->alloc)
					continue;
				mask = (MMINTEGER)1 << (k - 1);
				if (!sk->active) {
					sb->lastcoll &= ~mask;
					continue;
				}
				if (k == bnbr)
					continue;
				if (j == mms_layer_n[(int)sb->layer] +
					 mms_layer_n[0])
					break;
				if (sk->layer == sb->layer ||
				    sk->layer == 0) {
					j++;
					mms_pair(bnbr, k, &n, 1);
				}
			}
	} else {
		for (k = 1; k <= MMS_MAX; k++) {
			struct mmb_sprite *sk = &mms[k];

			if (!sk->alloc)
				continue;
			if (j == mms_inuse)
				break;
			if (k == bnbr)
				continue;
			mask = (MMINTEGER)1 << (k - 1);
			if (!sk->active) {
				sb->lastcoll &= ~mask;
				continue;
			} else
				j++;
			mms_pair(bnbr, k, &n, 1);
		}
	}
	mms_checklimits(bnbr, &n);
	mms_check_st(bnbr, &n);
	if (n > 1) {
		mms_coll_found = 1;
		mms_which = (short)bnbr;
		sb->coll[0] = (unsigned char)(n - 1);
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
	sb->rot = (unsigned char)flags;
	if (sb->rot > 3) {
		mode |= 8;
		sb->rot &= 3;
	}
	if (safe) {
		if (sb->active) {
			if (ontop) {
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
	sb->rot = (unsigned char)flags;
	if ((sb->rot & 4) == 0)
		mode |= 8;
	sb->rot &= 3;
	mms_show_px((int)bn, (int)xi, (int)yi, mode);
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
	if (count < 1 || count > MMS_MAX - 1)
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
		sb->master |= (MMINTEGER)1 << c1;
		sb->lastcoll = 0;
	}
	mms_any = 1;
	mms[0].alloc = 1;
}

MMG_FN void mms_swap(MMINTEGER bn, MMINTEGER rn, MMINTEGER flags)
{
	struct mmb_sprite *sb = mms_get(bn);
	struct mmb_sprite *rb;
	MMINTEGER master;
	signed char mymaster;
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
	master = rb->master;
	mymaster = rb->mymaster;
	rb->master = sb->master;
	rb->mymaster = sb->mymaster;
	/* The new sprite inherits the old one's saved background.  The
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
	sb->master = master;
	sb->mymaster = mymaster;
	sb->x = MMS_INACTIVE;
	sb->y = MMS_INACTIVE;
	sb->layer = -1;
	sb->nx = MMS_INACTIVE;
	sb->ny = MMS_INACTIVE;
	sb->active = 0;
	sb->lastcoll = 0;
	rb->rot = (unsigned char)flags;
	if (rb->rot > 3) {
		mode |= 8;
		rb->rot &= 3;
	}
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
	if (sb->mymaster == -1)
		free(sb->img);	/* img and store are one block */
	else {
		mms[(int)sb->mymaster].master &= ~((MMINTEGER)1 << (int)bn);
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
	if (sscanf(line, "%d , %d , %d", &width, &number, &height) < 2 ||
	    width < 1 || number < 1) {
		fclose(f);
		MM_RAISE("Invalid sprite file");
	}
	if (height == 0)
		height = width;
	if (number + start > MMS_MAX) {
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
		} else if (!(sb->w == width && sb->h == height)) {
			fclose(f);
			MM_RAISE("Existing buffer is incorrect size");
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
