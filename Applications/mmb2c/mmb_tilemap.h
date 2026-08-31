#ifndef MMB_TILEMAP_H
#define MMB_TILEMAP_H
/*
 *	TILEMAP CREATE label, id, slot, tw, th, tpr, cols, rows
 *	TILEMAP ATTR label, id, n
 *	TILEMAP DESTROY id
 *	TILEMAP SET id, col, row, tile
 *	TILEMAP DRAW id, dest, vx, vy, sx, sy, vw, vh [, transparent]
 *	TILEMAP SCROLL id, dx, dy
 *	TILEMAP VIEW id, x, y
 *	TILEMAP CLOSE
 *	TILEMAP SPRITE CREATE id, map, tile, x, y
 *	TILEMAP SPRITE MOVE id, x, y
 *	TILEMAP SPRITE SET id, tile
 *	TILEMAP SPRITE DRAW dest, transparent
 *	TILEMAP SPRITE DESTROY id
 *	TILEMAP SPRITE CLOSE
 *	TILEMAP(TILE id, x, y)   TILEMAP(COLLISION id, x, y, w, h [, mask])
 *	TILEMAP(ATTR id, tile)   TILEMAP(VIEWX id) (VIEWY id) (COLS id) (ROWS id)
 *	TILEMAP(SPRITE X id) (SPRITE Y id) (SPRITE TILE id) (SPRITE W id)
 *	TILEMAP(SPRITE H id) (SPRITE HIT a, b)
 *
 *	MMBasic's graphics/TileMap.c: a tile engine over a tileset that
 *	lives in a flash image slot (FLASH LOAD IMAGE / FLASH DISK LOAD,
 *	mmb_flash.h) and a map read out of DATA statements, with its own
 *	lightweight sprites that borrow the tileset.  On the same row
 *	workhorses as BLIT and SPRITE - mmb_blit.h must be included first,
 *	and mmb_data.h too, and the translator guarantees both.
 *
 *	What is transcribed, exactly: the four maps and sixty-four
 *	sprites, the map and attribute tables read from a label with the
 *	program's own READ position left where it was, the visible tile
 *	range and sub-tile offset (C division, so a negative viewport
 *	behaves as the reference's does), tile 0 as empty, the clip to the
 *	SCREEN rather than to the viewport rectangle (a viewport of 100
 *	pixels over 16-pixel tiles draws seven whole tiles, as blit121
 *	does), SCROLL's clamp against the screen size, every query, every
 *	argument range and every error string.
 *
 *	Where it differs, and why:
 *
 *	- The reference blits a tile at a time into memory it owns.  Here
 *	  a tile blit would be a system call per tile row, so the
 *	  destination is composed a whole ROW at a time - every tile that
 *	  crosses the row is merged into it - and the rows go through the
 *	  window mmb_blit.h opens: one crossing per batch of rows instead
 *	  of two per tile row.  Same pixels, same order of overwrite.
 *	- blit121's aligned memcpy becomes a nibble swap: the tileset is
 *	  PicoMite-packed (LOW nibble the left pixel) and the framebuffer
 *	  is this machine's (HIGH nibble the left pixel), so an aligned
 *	  source byte is the destination byte with its nibbles exchanged.
 *	- A tile index past the end of the tileset reads on into the slot,
 *	  which is the erased 0xFF the reference's flash would give - but
 *	  the read stops at the slot's end instead of running off the
 *	  allocation.  The reference has real flash behind it there.
 *	- "Requires RGB121 mode": the reference's cmd_tilemap refuses any
 *	  TILEMAP statement outside an RGB121 mode.  So does this, when
 *	  there is a screen to be in the wrong mode on; headless (the
 *	  gates) the statements run and the drawing is silent, the same
 *	  silence every other primitive keeps.
 *	- The destination letters are N, F and L.  The reference's T (a
 *	  second layer on an RP2350 VGA) has no buffer here and is a
 *	  translation error in fb_buf, not a run-time one.
 *
 *	Reference: graphics/TileMap.c (MAX_TILEMAPS :51, MAX_SPRITES :80,
 *	tilemap_read_data :212, cmd_tilemap :560, fun_tilemap :651) and
 *	graphics/RGB121.c blit121 :33 for the clip and the two paths.
 */

#include <stdlib.h>
#include <string.h>

#ifndef MMB_BLIT_H
#error "mmb_tilemap.h needs mmb_blit.h first (the translator emits both)"
#endif
#ifndef MMB_FLASH_H
#error "mmb_tilemap.h needs mmb_flash.h first (the translator emits both)"
#endif
#ifndef MMB_DATA_H
#error "mmb_tilemap.h needs mmb_data.h first (the translator emits both)"
#endif

#define MMT_MAX		4	/* MAX_TILEMAPS */
#define MMT_SPRITES	64	/* MAX_SPRITES */

/* Named rather than anonymous: the Fuzix cc1 cannot declare an array
 * of an anonymous struct type. */
struct mmb_tilemap {
	const unsigned char *px;	/* the slot's pixels, past the header */
	unsigned short *map;		/* cols * rows tile indices, row-major */
	unsigned short *attrs;		/* per tile TYPE: tile t -> attrs[t-1] */
	unsigned long lim;		/* bytes of slot behind px */
	int nattrs;
	int fw, fh;			/* tileset image, pixels */
	int tw, th;			/* one tile, pixels */
	int tpr;			/* tiles across the tileset */
	int cols, rows;
	int vx, vy;			/* the viewport, world pixels */
	unsigned char active;
};
static struct mmb_tilemap mmt[MMT_MAX];

struct mmb_tsprite {
	short x, y;
	unsigned short tile;		/* 1-based; 0 draws nothing */
	unsigned char ref;		/* which tilemap, 0-based */
	unsigned char active;
};
static struct mmb_tsprite mmts[MMT_SPRITES];

/* ---- the reference's messages, with their numbers spliced in ---------- */

MMG_FN char *mmt_cat(char *d, const char *s)
{
	while (*s)
		*d++ = *s++;
	*d = 0;
	return d;
}

MMG_FN char *mmt_num(char *d, MMINTEGER n)
{
	mm_int_to_str(d, (long long)n, 10);
	while (*d)
		d++;
	return d;
}

/* error("... % ...", n) */
MMG_FN void mmt_err_n(const char *pre, MMINTEGER n, const char *post)
{
	char m[96];

	mmt_cat(mmt_num(mmt_cat(m, pre), n), post);
	mm_error(m);
}

/* error("... % ... % ...", a, b) */
MMG_FN void mmt_err_nn(const char *pre, MMINTEGER a, const char *mid,
		       MMINTEGER b, const char *post)
{
	char m[96];

	mmt_cat(mmt_num(mmt_cat(mmt_num(mmt_cat(m, pre), a), mid), b), post);
	mm_error(m);
}

/*	"N is invalid (valid is LO to HI)" - getint()'s wording, the helper
 *	mmb_math.h also carries, under this header's own name because a
 *	program need not include that one.  Returns 1 when it raised: an
 *	MM_RAISE returns from the function it is written in, and this is
 *	not that function. */
MMG_FN int mmt_range(MMINTEGER v, MMINTEGER lo, MMINTEGER hi)
{
	char m[96];
	char *p;

	if (v >= lo && v <= hi)
		return 0;
	p = mmt_num(m, v);
	p = mmt_cat(p, " is invalid (valid is ");
	p = mmt_num(p, lo);
	p = mmt_cat(p, " to ");
	p = mmt_num(p, hi);
	mmt_cat(p, ")");
	mm_error(m);
	return 1;
}

/*	cmd_tilemap's preamble on a VGA PicoMite: every TILEMAP statement
 *	wants an RGB121 screen, whatever it is about to do.  With no
 *	screen (the gates) there is no mode to be wrong, and it runs. */
MMG_FN int mmt_mode(void)
{
	int stride = 0, bpp = 0, hres, vres;

	if (mmb_geom(&stride, &bpp, &hres, &vres) == 0 && bpp != 4) {
		mm_error("Requires RGB121 mode (MODE 2/3)");
		return 1;
	}
	return 0;
}

/* The map a statement names, or NULL after the reference's error. */
MMG_FN struct mmb_tilemap *mmt_get(MMINTEGER id)
{
	if (mmt_range(id, 1, MMT_MAX))
		return 0;
	if (!mmt[id - 1].active) {
		mmt_err_n("Tilemap ", id, " not created");
		return 0;
	}
	return &mmt[id - 1];
}

MMG_FN struct mmb_tsprite *mmts_get(MMINTEGER id)
{
	if (mmt_range(id, 1, MMT_SPRITES))
		return 0;
	if (!mmts[id - 1].active) {
		mmt_err_n("Sprite ", id, " not created");
		return 0;
	}
	return &mmts[id - 1];
}

/* ---- the tables, out of DATA ------------------------------------------ */

/*	tilemap_read_data: `count' integers from the label's item on, into
 *	a fresh array, with the program's own READ position untouched.
 *
 *	The label became an index at translation - the RESTORE mechanism -
 *	so what the reference does by scanning the program text for DATA
 *	lines is a bound check here.  The items after a label run on into
 *	whatever labels follow; so does the reference's scan, which stops
 *	at the end of the program and nowhere else. */
MMG_FN unsigned short *mmt_read_data(int at, int count)
{
	int save = mm_dptr, i;
	unsigned short *buf;

	if (at + count > mm_dn) {
		mmt_err_nn("Not enough DATA for tilemap (need ", count,
			   ", found ", mm_dn - at, ")");
		return 0;
	}
	buf = (unsigned short *)malloc((size_t)count * sizeof(unsigned short));
	if (buf == 0) {
		mm_error("Not enough memory");	/* GetMemory's own error */
		return 0;
	}
	mm_restore(at);
	for (i = 0; i < count; i++)
		buf[i] = (unsigned short)mm_read_i();
	mm_dptr = save;
	return buf;
}

MMG_FN void mmt_free(struct mmb_tilemap *tm)
{
	if (tm->map)
		free(tm->map);
	if (tm->attrs)
		free(tm->attrs);
	memset(tm, 0, sizeof(*tm));
}

/* ---- TILEMAP CREATE label, id, slot, tw, th, tpr, cols, rows ---------- */

MMG_FN void mmt_create(int at, MMINTEGER id, MMINTEGER slot, MMINTEGER tw,
		       MMINTEGER th, MMINTEGER tpr, MMINTEGER cols,
		       MMINTEGER rows)
{
	struct mmb_tilemap *tm;
	unsigned char *s;
	unsigned long fw, fh;

	if (mmt_mode())
		return;
	if (mmt_range(id, 1, MMT_MAX) || mmt_range(slot, 1, MMF_SLOTS)
	    || mmt_range(tw, 1, 256) || mmt_range(th, 1, 256)
	    || mmt_range(tpr, 1, 1024) || mmt_range(cols, 1, 10000)
	    || mmt_range(rows, 1, 10000))
		return;
	s = mmf_addr(slot);
	if (s == 0)
		return;
	/* the slot's header: width and height, two little-endian uint32 */
	fw = (unsigned long)s[0] | ((unsigned long)s[1] << 8) |
	     ((unsigned long)s[2] << 16) | ((unsigned long)s[3] << 24);
	fh = (unsigned long)s[4] | ((unsigned long)s[5] << 8) |
	     ((unsigned long)s[6] << 16) | ((unsigned long)s[7] << 24);
	if (fw < 1 || fw > 3840 || fh < 1 || fh > 2160) {
		mmt_err_n("Invalid flash image in slot ", slot, "");
		return;
	}
	tm = &mmt[id - 1];
	mmt_free(tm);			/* re-creating frees the old one */
	tm->map = mmt_read_data(at, (int)(cols * rows));
	if (tm->map == 0)
		return;
	tm->px = s + 8;
	tm->lim = (unsigned long)mmf_size[slot - 1] - 8;
	tm->fw = (int)fw;
	tm->fh = (int)fh;
	tm->tw = (int)tw;
	tm->th = (int)th;
	tm->tpr = (int)tpr;
	tm->cols = (int)cols;
	tm->rows = (int)rows;
	tm->vx = 0;
	tm->vy = 0;
	tm->active = 1;
}

/* ---- TILEMAP ATTR label, id, n ---------------------------------------- */

MMG_FN void mmt_attr(int at, MMINTEGER id, MMINTEGER num)
{
	struct mmb_tilemap *tm;

	if (mmt_mode())
		return;
	tm = mmt_get(id);
	if (tm == 0)
		return;
	if (mmt_range(num, 1, 65535))
		return;
	if (tm->attrs)
		free(tm->attrs);
	tm->attrs = 0;
	tm->nattrs = 0;
	tm->attrs = mmt_read_data(at, (int)num);
	if (tm->attrs)
		tm->nattrs = (int)num;
}

/* ---- TILEMAP DESTROY id / TILEMAP CLOSE ------------------------------- */

MMG_FN void mmt_destroy(MMINTEGER id)
{
	if (mmt_mode())
		return;
	if (mmt_range(id, 1, MMT_MAX))
		return;
	mmt_free(&mmt[id - 1]);
}

MMG_FN void mmts_close(void)
{
	if (mmt_mode())
		return;
	memset(mmts, 0, sizeof(mmts));
}

MMG_FN void mmt_close(void)
{
	int i;

	if (mmt_mode())
		return;
	for (i = 0; i < MMT_MAX; i++)
		mmt_free(&mmt[i]);
	memset(mmts, 0, sizeof(mmts));
}

/* ---- TILEMAP SET id, col, row, tile ----------------------------------- */

MMG_FN void mmt_set(MMINTEGER id, MMINTEGER col, MMINTEGER row, MMINTEGER tile)
{
	struct mmb_tilemap *tm;

	if (mmt_mode())
		return;
	tm = mmt_get(id);
	if (tm == 0)
		return;
	if (mmt_range(col, 0, tm->cols - 1) || mmt_range(row, 0, tm->rows - 1)
	    || mmt_range(tile, 0, 65535))
		return;
	tm->map[(int)col + (int)row * tm->cols] = (unsigned short)tile;
}

/* ---- TILEMAP SCROLL id, dx, dy / TILEMAP VIEW id, x, y ---------------- */

MMG_FN void mmt_scroll(MMINTEGER id, MMINTEGER dx, MMINTEGER dy)
{
	struct mmb_tilemap *tm;
	int max_x, max_y;

	if (mmt_mode())
		return;
	tm = mmt_get(id);
	if (tm == 0)
		return;
	tm->vx += (int)dx;
	tm->vy += (int)dy;
	/* clamp to the world, against the SCREEN size as the reference does */
	max_x = tm->cols * tm->tw - (int)mm_hres();
	max_y = tm->rows * tm->th - (int)mm_vres();
	if (tm->vx < 0)
		tm->vx = 0;
	if (tm->vy < 0)
		tm->vy = 0;
	if (max_x > 0 && tm->vx > max_x)
		tm->vx = max_x;
	if (max_y > 0 && tm->vy > max_y)
		tm->vy = max_y;
}

MMG_FN void mmt_view(MMINTEGER id, MMINTEGER x, MMINTEGER y)
{
	struct mmb_tilemap *tm;

	if (mmt_mode())
		return;
	tm = mmt_get(id);
	if (tm == 0)
		return;
	tm->vx = (int)x;
	tm->vy = (int)y;
}

/* ---- pixel work --------------------------------------------------------- */

/*	One tile row's worth of pixels: n from tileset row `srow' at pixel
 *	sx (LOW nibble the left pixel) into the packed destination row
 *	`row', whose first byte holds pixel 2*b0, at pixel dx, kept inside
 *	[cx0, cx1).  blit121's two paths: the aligned opaque one, a memcpy
 *	there, is a nibble swap here because the two packings are mirrors;
 *	everything else goes a pixel at a time with the transparent index
 *	skipped. */
MMG_FN void mmt_seg(unsigned char *row, int b0, const unsigned char *srow,
		    int sx, int dx, int n, int cx0, int cx1, int blank)
{
	int k;

	if (dx < cx0) {
		k = cx0 - dx;
		sx += k;
		dx += k;
		n -= k;
	}
	if (dx + n > cx1)
		n = cx1 - dx;
	if (n < 1)
		return;
	if (blank < 0 && !(sx & 1) && !(dx & 1)) {
		const unsigned char *sp = srow + (sx >> 1);
		unsigned char *dp = row + (dx >> 1) - b0;
		int pairs = n >> 1;

		for (k = 0; k < pairs; k++) {
			unsigned char s = sp[k];

			dp[k] = (unsigned char)((s << 4) | (s >> 4));
		}
		if (n & 1)
			dp[pairs] = (unsigned char)((dp[pairs] & 0x0F)
						    | ((sp[pairs] & 15) << 4));
		return;
	}
	for (k = 0; k < n; k++) {
		int s = sx + k, d = dx + k;
		unsigned char c = (s & 1) ? (unsigned char)(srow[s >> 1] >> 4)
					  : (unsigned char)(srow[s >> 1] & 15);
		unsigned char *p;

		if (blank >= 0 && c == (unsigned char)blank)
			continue;
		p = row + (d >> 1) - b0;
		if (d & 1)
			*p = (unsigned char)((*p & 0xF0) | c);
		else
			*p = (unsigned char)((*p & 0x0F) | (c << 4));
	}
}

/*	Tileset row `sy', pixels [sx, sx+n) wanted: the reference reads
 *	flash and gets whatever lies past the image; this reads the slot,
 *	which is the same erased 0xFF past the file, and stops at the
 *	slot's end rather than run off the allocation. */
MMG_FN const unsigned char *mmt_srow(const struct mmb_tilemap *tm, int sx,
				    int sy, int n)
{
	unsigned long sstride = ((unsigned long)tm->fw + 1) >> 1;
	unsigned long off = (unsigned long)sy * sstride;

	if (off + (unsigned long)((sx + n + 1) >> 1) > tm->lim)
		return 0;
	return tm->px + off;
}

/*	The packed destination row: out of the open window when the row is
 *	in it, else fetched into mmb_rowb - and mmt_row_done puts it back
 *	the way it came. */
MMG_FN unsigned char *mmt_row(int y, int b0, int nb, int stride, int *win)
{
	unsigned char *row = mmb_win_bytes(y, b0, nb);

	*win = 1;
	if (row == 0) {
		if (mm_fb_read((MMINTEGER)y * stride + b0, nb, mmb_rowb) < 0)
			return 0;
		row = mmb_rowb;
		*win = 0;
	}
	return row;
}

MMG_FN void mmt_row_done(int y, int b0, int nb, int stride, int win)
{
	if (win)
		mmb_win.dirty = 1;
	else
		mm_fb_put((MMINTEGER)y * stride + b0, nb, mmb_rowb);
}

/* ---- TILEMAP DRAW id, dest, vx, vy, sx, sy, vw, vh [, t] -------------- */

/*	Screen rows [cy0, cy1) of the tiles in columns c0..c1 whose row r0
 *	lands at by: one row of the destination composed from every tile
 *	that crosses it.  Its own function so the expression trees stay
 *	inside cc2's per-function node pool. */
MMG_FN void mmt_rows(struct mmb_tilemap *tm, int c0, int c1, int r0,
		     int bx, int by, int cx0, int cx1, int cy0, int cy1,
		     int stride, int blank)
{
	int tw = tm->tw, th = tm->th;
	int b0 = cx0 >> 1, nb = ((cx1 - 1) >> 1) - b0 + 1;
	int y;

	for (y = cy0; y < cy1; y++) {
		int r = r0 + (y - by) / th, ty = (y - by) % th, c, win;
		unsigned char *row;

		if (r < 0 || r >= tm->rows)
			continue;		/* off the map: skipped */
		row = mmt_row(y, b0, nb, stride, &win);
		if (row == 0)
			return;
		for (c = c0; c <= c1; c++) {
			int tile, scol, srw;
			const unsigned char *sp;

			if (c < 0 || c >= tm->cols)
				continue;
			tile = tm->map[c + r * tm->cols];
			if (tile == 0)
				continue;	/* tile 0 = empty */
			scol = (tile - 1) % tm->tpr;
			srw = (tile - 1) / tm->tpr;
			sp = mmt_srow(tm, scol * tw, srw * th + ty, tw);
			if (sp == 0)
				continue;
			mmt_seg(row, b0, sp, scol * tw, bx + (c - c0) * tw,
				tw, cx0, cx1, blank);
		}
		mmt_row_done(y, b0, nb, stride, win);
	}
}

MMG_FN void mmt_draw(MMINTEGER id, MMINTEGER dst, MMINTEGER vxi,
		     MMINTEGER vyi, MMINTEGER sxi, MMINTEGER syi,
		     MMINTEGER vwi, MMINTEGER vhi, MMINTEGER blank)
{
	struct mmb_tilemap *tm;
	int stride = 0, bpp = 0, hres, vres, keep;
	int vx = (int)vxi, vy = (int)vyi, sx = (int)sxi, sy = (int)syi;
	int tw, th, c0, c1, r0, r1, bx, by, cx0, cx1, cy0, cy1;

	if (mmt_mode())
		return;
	tm = mmt_get(id);
	if (tm == 0)
		return;
	if (mmt_range(vwi, 1, 3840) || mmt_range(vhi, 1, 2160)
	    || mmt_range(blank, -1, 15))
		return;
	/* the viewport is remembered whether or not anything is drawn */
	tm->vx = vx;
	tm->vy = vy;
	if (mmb_geom(&stride, &bpp, &hres, &vres))
		return;			/* headless: the gates */
	tw = tm->tw;
	th = tm->th;
	/* the visible tile range and the sub-tile offset, as the reference
	 * computes them - C division, negative viewports included */
	c0 = vx / tw;
	r0 = vy / th;
	c1 = (vx + (int)vwi - 1) / tw;
	r1 = (vy + (int)vhi - 1) / th;
	/* where tile (c0, r0) lands, and the rectangle all of them cover,
	 * clipped to the screen - blit121 clips to the screen and to
	 * nothing else */
	bx = sx - vx % tw;
	by = sy - vy % th;
	cx0 = bx < 0 ? 0 : bx;
	cx1 = bx + (c1 - c0 + 1) * tw;
	if (cx1 > hres)
		cx1 = hres;
	cy0 = by < 0 ? 0 : by;
	cy1 = by + (r1 - r0 + 1) * th;
	if (cy1 > vres)
		cy1 = vres;
	if (cx0 >= cx1 || cy0 >= cy1)
		return;
	keep = (int)mm_fb_cur();
	mm_fb_write(dst);
	mmb_win_open(cy0, cy1 - cy0, cx0, cx1 - cx0, stride, bpp);
	mmt_rows(tm, c0, c1, r0, bx, by, cx0, cx1, cy0, cy1, stride,
		 (int)blank);
	mmb_win_close();
	mm_fb_write(keep);
}

/* ---- TILEMAP SPRITE ... ------------------------------------------------ */

MMG_FN void mmts_create(MMINTEGER id, MMINTEGER ref, MMINTEGER tile,
			MMINTEGER x, MMINTEGER y)
{
	struct mmb_tsprite *sp;

	if (mmt_mode())
		return;
	if (mmt_range(id, 1, MMT_SPRITES) || mmt_range(ref, 1, MMT_MAX)
	    || mmt_range(tile, 1, 65535))
		return;
	if (!mmt[ref - 1].active) {
		mmt_err_n("Tilemap ", ref, " not created");
		return;
	}
	sp = &mmts[id - 1];
	sp->x = (short)x;
	sp->y = (short)y;
	sp->tile = (unsigned short)tile;
	sp->ref = (unsigned char)(ref - 1);
	sp->active = 1;
}

MMG_FN void mmts_move(MMINTEGER id, MMINTEGER x, MMINTEGER y)
{
	struct mmb_tsprite *sp;

	if (mmt_mode())
		return;
	sp = mmts_get(id);
	if (sp == 0)
		return;
	sp->x = (short)x;
	sp->y = (short)y;
}

MMG_FN void mmts_set(MMINTEGER id, MMINTEGER tile)
{
	struct mmb_tsprite *sp;

	if (mmt_mode())
		return;
	sp = mmts_get(id);
	if (sp == 0)
		return;
	if (mmt_range(tile, 1, 65535))
		return;
	sp->tile = (unsigned short)tile;
}

MMG_FN void mmts_destroy(MMINTEGER id)
{
	if (mmt_mode())
		return;
	if (mmt_range(id, 1, MMT_SPRITES))
		return;
	memset(&mmts[id - 1], 0, sizeof(mmts[id - 1]));
}

/* One sprite: its tile at (x, y), clipped to the screen, one window. */
MMG_FN void mmts_draw1(const struct mmb_tsprite *sp, int stride, int bpp,
		       int hres, int vres, int blank)
{
	const struct mmb_tilemap *tm = &mmt[sp->ref];
	int tw = tm->tw, th = tm->th;
	int scol = (sp->tile - 1) % tm->tpr, srw = (sp->tile - 1) / tm->tpr;
	int cx0, cx1, cy0, cy1, b0, nb, y;

	cx0 = sp->x < 0 ? 0 : sp->x;
	cx1 = sp->x + tw;
	if (cx1 > hres)
		cx1 = hres;
	cy0 = sp->y < 0 ? 0 : sp->y;
	cy1 = sp->y + th;
	if (cy1 > vres)
		cy1 = vres;
	if (cx0 >= cx1 || cy0 >= cy1)
		return;
	b0 = cx0 >> 1;
	nb = ((cx1 - 1) >> 1) - b0 + 1;
	mmb_win_open(cy0, cy1 - cy0, cx0, cx1 - cx0, stride, bpp);
	for (y = cy0; y < cy1; y++) {
		int win;
		unsigned char *row = mmt_row(y, b0, nb, stride, &win);
		const unsigned char *s;

		if (row == 0)
			break;
		s = mmt_srow(tm, scol * tw, srw * th + (y - sp->y), tw);
		if (s)
			mmt_seg(row, b0, s, scol * tw, sp->x, tw, cx0, cx1,
				blank);
		mmt_row_done(y, b0, nb, stride, win);
	}
	mmb_win_close();
}

/* TILEMAP SPRITE DRAW dest, transparent: every active sprite, in slot
 * order, so a higher number lands on top. */
MMG_FN void mmts_draw(MMINTEGER dst, MMINTEGER blank)
{
	int stride = 0, bpp = 0, hres, vres, keep, i;

	if (mmt_mode())
		return;
	if (mmt_range(blank, -1, 15))
		return;
	if (mmb_geom(&stride, &bpp, &hres, &vres))
		return;
	keep = (int)mm_fb_cur();
	mm_fb_write(dst);
	for (i = 0; i < MMT_SPRITES; i++) {
		if (!mmts[i].active || mmts[i].tile == 0)
			continue;
		if (!mmt[mmts[i].ref].active)
			continue;
		mmts_draw1(&mmts[i], stride, bpp, hres, vres, (int)blank);
	}
	mm_fb_write(keep);
}

/* ---- the function ------------------------------------------------------ */

/* TILEMAP(TILE id, x, y): the tile under a world pixel, 0 off the map */
MMG_FN MMINTEGER mmt_fn_tile(MMINTEGER id, MMINTEGER pxi, MMINTEGER pyi)
{
	struct mmb_tilemap *tm = mmt_get(id);
	int px = (int)pxi, py = (int)pyi, col, row;

	if (tm == 0)
		return 0;
	col = px / tm->tw;
	row = py / tm->th;
	if (col < 0 || col >= tm->cols || row < 0 || row >= tm->rows)
		return 0;
	return tm->map[col + row * tm->cols];
}

/* TILEMAP(COLLISION id, x, y, w, h [, mask]): the first non-empty tile
 * under a rectangle - any tile with mask 0, else one whose attribute
 * bits meet the mask */
MMG_FN MMINTEGER mmt_fn_coll(MMINTEGER id, MMINTEGER bxi, MMINTEGER byi,
			     MMINTEGER bwi, MMINTEGER bhi, MMINTEGER maski)
{
	struct mmb_tilemap *tm = mmt_get(id);
	int bx = (int)bxi, by = (int)byi, bw = (int)bwi, bh = (int)bhi;
	int mask = (int)maski, c0, c1, r0, r1, r, c, hit = 0;

	if (tm == 0)
		return 0;
	c0 = bx / tm->tw;
	r0 = by / tm->th;
	c1 = (bx + bw - 1) / tm->tw;
	r1 = (by + bh - 1) / tm->th;
	for (r = r0; r <= r1 && hit == 0; r++)
		for (c = c0; c <= c1 && hit == 0; c++) {
			int tile;

			if (c < 0 || c >= tm->cols || r < 0 || r >= tm->rows)
				continue;
			tile = tm->map[c + r * tm->cols];
			if (tile == 0)
				continue;
			if (mask == 0)
				hit = tile;
			else if (tm->attrs && tile <= tm->nattrs
				 && (tm->attrs[tile - 1] & mask))
				hit = tile;
		}
	return hit;
}

/* TILEMAP(ATTR id, tile) */
MMG_FN MMINTEGER mmt_fn_attr(MMINTEGER id, MMINTEGER tile)
{
	struct mmb_tilemap *tm = mmt_get(id);

	if (tm == 0)
		return 0;
	if (mmt_range(tile, 1, 65535))
		return 0;
	if (tm->attrs == 0 || tile > tm->nattrs)
		return 0;
	return tm->attrs[tile - 1];
}

/* TILEMAP(VIEWX id) 1, (VIEWY id) 2, (COLS id) 3, (ROWS id) 4 */
MMG_FN MMINTEGER mmt_fn(int sel, MMINTEGER id)
{
	struct mmb_tilemap *tm = mmt_get(id);

	if (tm == 0)
		return 0;
	switch (sel) {
	case 1:
		return tm->vx;
	case 2:
		return tm->vy;
	case 3:
		return tm->cols;
	}
	return tm->rows;
}

/* TILEMAP(SPRITE X id) 1, (Y) 2, (TILE) 3, (W) 4, (H) 5 */
MMG_FN MMINTEGER mmt_fn_sprite(int sel, MMINTEGER id)
{
	struct mmb_tsprite *sp = mmts_get(id);

	if (sp == 0)
		return 0;
	switch (sel) {
	case 1:
		return sp->x;
	case 2:
		return sp->y;
	case 3:
		return sp->tile;
	case 4:
		return mmt[sp->ref].tw;
	}
	return mmt[sp->ref].th;
}

/* TILEMAP(SPRITE HIT a, b): the two boxes overlap */
MMG_FN MMINTEGER mmt_fn_hit(MMINTEGER a, MMINTEGER b)
{
	struct mmb_tsprite *sa, *sb;
	int w1, h1, w2, h2;

	if (mmt_range(a, 1, MMT_SPRITES) || mmt_range(b, 1, MMT_SPRITES))
		return 0;
	sa = &mmts[a - 1];
	sb = &mmts[b - 1];
	if (!sa->active) {
		mmt_err_n("Sprite ", a, " not created");
		return 0;
	}
	if (!sb->active) {
		mmt_err_n("Sprite ", b, " not created");
		return 0;
	}
	w1 = mmt[sa->ref].tw;
	h1 = mmt[sa->ref].th;
	w2 = mmt[sb->ref].tw;
	h2 = mmt[sb->ref].th;
	return (sa->x < sb->x + w2 && sa->x + w1 > sb->x &&
		sa->y < sb->y + h2 && sa->y + h1 > sb->y) ? 1 : 0;
}

#endif /* MMB_TILEMAP_H */
