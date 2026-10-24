#ifndef MMB_DRAW3D_H
#define MMB_DRAW3D_H
/*
 *	DRAW3D - the PicoMite's 3D engine, graphics/Draw3D.c, whole: the
 *	objects, quaternion rotation, the camera's projection, back-face
 *	culling, the depth sort, the lighting and the hidden-line z-buffer.
 *
 *	    DRAW3D CREATE n, nv, nf, camera, v(), fc(), f(), col(), [edge()] [, fill()]
 *	    DRAW3D CAMERA n, viewplane [, x [, y [, panx [, pany]]]]
 *	    DRAW3D ROTATE q(), n [, n ...]
 *	    DRAW3D SHOW | WRITE n, x, y, z [, nonormals [, depthmode]]
 *	    DRAW3D HIDE | RESTORE | RESET | CLOSE n [, n ...], HIDE ALL, CLOSE ALL
 *	    DRAW3D LIGHT n, x, y, z, ambient     DRAW3D SET FLAGS n, flag, face, count ...
 *	    DRAW3D(XMIN | XMAX | YMIN | YMAX | X | Y | Z | DISTANCE n)
 *
 *	The arithmetic is the firmware's line for line, in doubles rather
 *	than its floats: FLOAT3D is float there only for the
 *	microcontroller's speed (the user, 2026-10-24).  The drawing goes
 *	through the primitives TRIANGLE, POLYGON and LINE use, which are
 *	the firmware's DrawTriangle, polygon fill and DrawLine, and a
 *	hidden face's box is cleared with BOX's rectangle.
 *
 *	The objects are malloc'd, so under bcrun they come out of the heap
 *	MM.INFO(HEAP) counts: Elite sizes its object pool by the heap a
 *	CREATE takes.
 *
 *	Every failure leaves through MM_RAISE.  Under ON ERROR SKIP the
 *	raise returns, and Elite probes how many objects there may be by
 *	creating them until one fails, so a failed CREATE must leave
 *	nothing behind.  The commands that take a list of objects return
 *	1 to end the statement there, as an error or HIDE's early return
 *	ends cmd_3D's loop.
 *
 *	DIAGNOSE is not here: it prints in the firmware's PInt and PFlt
 *	formats, which are not PRINT's.  Hidden-line mode needs a
 *	framebuffer on an LCD PicoMite; an HDMI one, which the PC3 is,
 *	draws it anywhere, and so does this.
 */

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "mmb_gfx_triangle.h"
#include "mmb_gfx_polygon.h"

#define MM3D_MAX	32		/* MAX3D */
#define MM3D_MAXCAM	3		/* MAXCAM */
#define MM3D_MAXV	20		/* MAX_3D_POLYGON_VERTICES */
#define MM3D_NOFILL	0xFFFFFFFFU	/* a face drawn as its outline */
#define MM3D_FAR	32767		/* xmin while nothing is drawn */

struct mm3d_vec {
	MMFLOAT x, y, z;
};

/* w x y z m, the order T_Mult indexes them in */
struct mm3d_q {
	MMFLOAT w, x, y, z, m;
};

struct mm3d_cam {
	MMFLOAT x, y, z, viewplane, panx, pany;
};

struct mm3d_obj {
	struct mm3d_q *q_vertices;	/* the vertices as created */
	struct mm3d_q *r_vertices;	/* ... and as last rotated */
	struct mm3d_q *q_centroids;
	struct mm3d_q *r_centroids;
	struct mm3d_vec *normals;
	unsigned short *facecount;	/* vertices in each face */
	unsigned short *facestart;	/* each face's start in face_x_vert */
	unsigned short *face_x_vert;	/* every face's vertex numbers */
	unsigned int *fill;		/* RGB888, or MM3D_NOFILL */
	unsigned int *line;
	unsigned int *colours;
	unsigned char *flags;
	MMFLOAT *dots;
	MMFLOAT *depth;
	int *depthindex;
	MMFLOAT distance, ambient;
	struct mm3d_vec light, current;
	int tot_face_x_vert;
	int xmin, xmax, ymin, ymax;
	int nv, nf, vmax, camera, nonormals, depthmode;
};

static struct mm3d_obj *mm3d_tab[MM3D_MAX + 1];
static struct mm3d_cam mm3d_cams[MM3D_MAXCAM + 1];
static int mm3d_ready;
static MMFLOAT *mm3d_zbuf;		/* hiddenline_zbuf_cache */
static int mm3d_zcap;
static char mm3d_msg[48];

/* Program start: no camera has a position (closeall3d's -32767). */
static void mm3d_init(void)
{
	int i;

	if (mm3d_ready)
		return;
	mm3d_ready = 1;
	for (i = 1; i <= MM3D_MAXCAM; i++)
		mm3d_cams[i].viewplane = -32767;
}

/* error()'s % with one number in it: "Object % does not exist" */
static const char *mm3d_fmt(const char *pre, MMINTEGER v, const char *post)
{
	char d[24];
	int k = 0, neg = v < 0, i = 0;
	unsigned long long u = neg ? (unsigned long long)-v
				   : (unsigned long long)v;

	do {
		d[k++] = (char)('0' + (int)(u % 10));
		u /= 10;
	} while (u);
	while (*pre && i < (int)sizeof(mm3d_msg) - 26)
		mm3d_msg[i++] = *pre++;
	if (neg)
		mm3d_msg[i++] = '-';
	while (k)
		mm3d_msg[i++] = d[--k];
	while (*post && i < (int)sizeof(mm3d_msg) - 1)
		mm3d_msg[i++] = *post++;
	mm3d_msg[i] = 0;
	return mm3d_msg;
}

static MMFLOAT mm3d_round(MMFLOAT x)		/* roundf */
{
	return x >= 0.0 ? floor(x + 0.5) : ceil(x - 0.5);
}

/* The object, raising "Object % does not exist" (StandardErrorParam 7)
 * or getint's range error; NULL after either. */
static struct mm3d_obj *mm3d_get(MMINTEGER n)
{
	mm3d_init();
	if (mm_range(n, 1, MM3D_MAX))
		return NULL;
	if (mm3d_tab[n] == NULL) {
		mm_error(mm3d_fmt("Object ", n, " does not exist"));
		return NULL;
	}
	return mm3d_tab[n];
}

/* Free3DMemory */
static void mm3d_free(int n)
{
	struct mm3d_obj *o = mm3d_tab[n];

	if (o == NULL)
		return;
	free(o->q_vertices);
	free(o->r_vertices);
	free(o->q_centroids);
	free(o->r_centroids);
	free(o->facecount);
	free(o->facestart);
	free(o->fill);
	free(o->line);
	free(o->colours);
	free(o->face_x_vert);
	free(o->dots);
	free(o->depth);
	free(o->depthindex);
	free(o->normals);
	free(o->flags);
	free(o);
	mm3d_tab[n] = NULL;
}

/* GetMemory: zeroed */
static void *mm3d_alloc(unsigned long n)
{
	void *p = malloc(n ? n : 1);

	if (p != NULL)
		memset(p, 0, n ? n : 1);
	return p;
}

/* T_Mult, on w x y z m */
static void mm3d_qmul(const struct mm3d_q *q1, const struct mm3d_q *q2,
		      struct mm3d_q *n)
{
	MMFLOAT a1 = q1->w, a2 = q2->w, b1 = q1->x, b2 = q2->x;
	MMFLOAT c1 = q1->y, c2 = q2->y, d1 = q1->z, d2 = q2->z;

	n->w = a1 * a2 - b1 * b2 - c1 * c2 - d1 * d2;
	n->x = a1 * b2 + b1 * a2 + c1 * d2 - d1 * c2;
	n->y = a1 * c2 - b1 * d2 + c1 * a2 + d1 * b2;
	n->z = a1 * d2 + b1 * c2 - c1 * b2 + d1 * a2;
	n->m = q1->m * q2->m;
}

/* q_rotate: rotate * in * rotate' */
static void mm3d_qrot(const struct mm3d_q *in, const struct mm3d_q *rot,
		      struct mm3d_q *out)
{
	struct mm3d_q temp, inv;

	mm3d_qmul(rot, in, &temp);
	inv.w = rot->w;
	inv.x = -rot->x;
	inv.y = -rot->y;
	inv.z = -rot->z;
	inv.m = rot->m;
	mm3d_qmul(&temp, &inv, out);
}

static void mm3d_normalise(struct mm3d_vec *v)
{
	MMFLOAT n = sqrt(v->x * v->x + v->y * v->y + v->z * v->z);

	v->x /= n;
	v->y /= n;
	v->z /= n;
}

/* depthsort: the furthest first, the index following */
static void mm3d_sort(MMFLOAT *farray, int n, int *index)
{
	int i, j = n, s = 1, t;
	MMFLOAT f;

	while (s) {
		s = 0;
		for (i = 1; i < j; i++) {
			if (farray[i] > farray[i - 1]) {
				f = farray[i];
				farray[i] = farray[i - 1];
				farray[i - 1] = f;
				s = 1;
				t = index[i - 1];
				index[i - 1] = index[i];
				index[i] = t;
			}
		}
		j--;
	}
}

/* DrawPolygon: an outline, a triangle, a quad as two, or the fill */
static void mm3d_face(struct mm3d_obj *o, short *xc, short *yc, int face)
{
	int i, cnt = o->facecount[face];
	MMINTEGER c = (MMINTEGER)o->line[face];
	MMINTEGER f = (MMINTEGER)o->fill[face];
	MMINTEGER px[MM3D_MAXV], py[MM3D_MAXV];

	if (o->fill[face] == MM3D_NOFILL) {
		for (i = 0; i < cnt; i++) {
			if (i < cnt - 1)
				mm_line(xc[i], yc[i], xc[i + 1], yc[i + 1], c);
			else
				mm_line(xc[i], yc[i], xc[0], yc[0], c);
		}
	} else if (cnt == 3) {
		mmg_triangle(xc[0], yc[0], xc[1], yc[1], xc[2], yc[2], c, f);
	} else if (cnt == 4) {
		mmg_triangle(xc[0], yc[0], xc[1], yc[1], xc[2], yc[2], f, f);
		mmg_triangle(xc[0], yc[0], xc[2], yc[2], xc[3], yc[3], f, f);
		if (f != c) {
			mm_line(xc[0], yc[0], xc[1], yc[1], c);
			mm_line(xc[1], yc[1], xc[2], yc[2], c);
			mm_line(xc[2], yc[2], xc[3], yc[3], c);
			mm_line(xc[0], yc[0], xc[3], yc[3], c);
		}
	} else {
		for (i = 0; i < cnt; i++) {
			px[i] = xc[i];
			py[i] = yc[i];
		}
		mmg_polygon(NULL, px, NULL, py, cnt, cnt, c, f);
	}
}

/* hiddenline_raster_triangle: a face's 1/z into the z-buffer */
static void mm3d_zfill(int x0, int y0, MMFLOAT iz0, int x1, int y1,
		       MMFLOAT iz1, int x2, int y2, MMFLOAT iz2, int minx,
		       int miny, int maxx, int maxy, MMFLOAT *zbuf, int bw)
{
	int tminx, tmaxx, tminy, tmaxy, bminx, bmaxx, bminy, bmaxy;
	int den, a0, b0, cc0, a1, b1, cc1, a2, b2, cc2, sign, x, y;
	long e0, e1, e2;
	MMFLOAT inv, c0, c1, c2, iz;

	tminx = x0 < x1 ? x0 : x1;
	tminx = tminx < x2 ? tminx : x2;
	tmaxx = x0 > x1 ? x0 : x1;
	tmaxx = tmaxx > x2 ? tmaxx : x2;
	tminy = y0 < y1 ? y0 : y1;
	tminy = tminy < y2 ? tminy : y2;
	tmaxy = y0 > y1 ? y0 : y1;
	tmaxy = tmaxy > y2 ? tmaxy : y2;
	bminx = tminx > minx ? tminx : minx;
	bmaxx = tmaxx < maxx ? tmaxx : maxx;
	bminy = tminy > miny ? tminy : miny;
	bmaxy = tmaxy < maxy ? tmaxy : maxy;

	den = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2);
	if (den == 0)
		return;
	inv = 1.0 / (MMFLOAT)den;
	c0 = iz0 * inv;
	c1 = iz1 * inv;
	c2 = iz2 * inv;
	/* edge functions: e(x, y) = A*x + B*y + C */
	a0 = y1 - y2; b0 = x2 - x1; cc0 = x1 * y2 - x2 * y1;
	a1 = y2 - y0; b1 = x0 - x2; cc1 = x2 * y0 - x0 * y2;
	a2 = y0 - y1; b2 = x1 - x0; cc2 = x0 * y1 - x1 * y0;
	sign = den > 0 ? 1 : -1;

	for (y = bminy; y <= bmaxy; y++) {
		e0 = (long)a0 * bminx + (long)b0 * y + cc0;
		e1 = (long)a1 * bminx + (long)b1 * y + cc1;
		e2 = (long)a2 * bminx + (long)b2 * y + cc2;
		for (x = bminx; x <= bmaxx; x++) {
			if (e0 * sign >= 0 && e1 * sign >= 0
			    && e2 * sign >= 0) {
				iz = (MMFLOAT)e0 * c0 + (MMFLOAT)e1 * c1
				     + (MMFLOAT)e2 * c2;
				if (iz > zbuf[(y - miny) * bw + (x - minx)])
					zbuf[(y - miny) * bw + (x - minx)] = iz;
			}
			e0 += a0;
			e1 += a1;
			e2 += a2;
		}
	}
}

/* hiddenline_draw_edge: the pixels of an edge nothing nearer covers */
static void mm3d_zedge(int x0, int y0, MMFLOAT iz0, int x1, int y1,
		       MMFLOAT iz1, MMINTEGER c, int minx, int miny,
		       int maxx, int maxy, MMFLOAT *zbuf, int bw)
{
	int dx = x1 > x0 ? x1 - x0 : x0 - x1;
	int dy = y1 > y0 ? y1 - y0 : y0 - y1;
	int steps = dx > dy ? dx : dy;
	int sx, sy, err, x, y, i, e2;
	MMFLOAT izstep, izf;

	if (steps == 0) {
		if (x0 >= minx && x0 <= maxx && y0 >= miny && y0 <= maxy
		    && iz0 >= zbuf[(y0 - miny) * bw + (x0 - minx)] - 0.0005)
			mm_pixel(x0, y0, c);
		return;
	}
	izstep = (iz1 - iz0) / (MMFLOAT)steps;
	sx = x0 < x1 ? 1 : -1;
	sy = y0 < y1 ? 1 : -1;
	err = dx - dy;
	x = x0;
	y = y0;
	izf = iz0;
	for (i = 0; i <= steps; i++) {
		if (x >= minx && x <= maxx && y >= miny && y <= maxy
		    && izf >= zbuf[(y - miny) * bw + (x - minx)] - 0.0005)
			mm_pixel(x, y, c);
		e2 = err << 1;
		if (e2 > -dy) {
			err -= dy;
			x += sx;
		}
		if (e2 < dx) {
			err += dx;
			y += sy;
		}
		izf += izstep;
	}
}

/* hiddenline_get_zbuf: kept and grown, freed by CLOSE ALL */
static MMFLOAT *mm3d_getz(int count)
{
	if (count <= 0)
		return NULL;
	if (count > mm3d_zcap) {
		free(mm3d_zbuf);
		mm3d_zbuf = (MMFLOAT *)mm3d_alloc((unsigned long)count
						  * sizeof(MMFLOAT));
		mm3d_zcap = mm3d_zbuf != NULL ? count : 0;
	}
	return mm3d_zbuf;
}

/* display3d.  A vertex in real space is its rotated unit vector times
 * its length, at the object's position.  clear is SHOW's: wipe the box the object last drew in
 * and keep the new one; WRITE leaves both alone. */
static void mm3d_display(int n, MMFLOAT x, MMFLOAT y, MMFLOAT z, int clear,
			 int nonormals, int depthmode)
{
	struct mm3d_obj *o = mm3d_tab[n];
	struct mm3d_cam *cam = &mm3d_cams[o->camera];
	struct mm3d_vec ray, lighting, p1, p2, p3, u, v;
	MMFLOAT x1, y1, z1, tmp, at, bt, ct, t, cc = 1, d = -cam->viewplane;
	MMFLOAT invz[MM3D_MAXV], maxd, vd, ratio;
	int maxh = (int)mm_vres(), maxw = (int)mm_hres();
	int vp, vi, f, si, k, vc, vn, red, green, blue;
	unsigned int csave = 0, fsave = 0;
	short xc[MM3D_MAXV], yc[MM3D_MAXV];
	int hidden = depthmode == 2;
	MMFLOAT *zb = NULL;
	int hminx = maxw, hmaxx = -1, hminy = maxh, hmaxy = -1, hbw = 0;
	short *hpx = NULL, *hpy = NULL;
	MMFLOAT *hpz = NULL;
	unsigned char *hvis = NULL;

	lighting.x = lighting.y = lighting.z = 0;
	if (o->xmin != MM3D_FAR && clear)
		mmg_rect1(0, o->xmin, o->ymin, o->xmax, o->ymax);
	o->xmin = MM3D_FAR;
	o->ymin = MM3D_FAR;
	o->xmax = -MM3D_FAR;
	o->ymax = -MM3D_FAR;
	o->distance = 0.0;
	for (f = 0; f < o->nf; f++) {
		/* each face's normal, from its first three vertices */
		vp = o->facestart[f];
		vi = o->face_x_vert[vp + 1];
		p1.x = o->r_vertices[vi].x * o->r_vertices[vi].m + x;
		p1.y = o->r_vertices[vi].y * o->r_vertices[vi].m + y;
		p1.z = o->r_vertices[vi].z * o->r_vertices[vi].m + z;
		vi = o->face_x_vert[vp + 2];
		p2.x = o->r_vertices[vi].x * o->r_vertices[vi].m + x;
		p2.y = o->r_vertices[vi].y * o->r_vertices[vi].m + y;
		p2.z = o->r_vertices[vi].z * o->r_vertices[vi].m + z;
		vi = o->face_x_vert[vp];
		p3.x = o->r_vertices[vi].x * o->r_vertices[vi].m + x;
		p3.y = o->r_vertices[vi].y * o->r_vertices[vi].m + y;
		p3.z = o->r_vertices[vi].z * o->r_vertices[vi].m + z;
		u.x = p2.x - p1.x;
		u.y = p2.y - p1.y;
		u.z = p2.z - p1.z;
		v.x = p3.x - p1.x;
		v.y = p3.y - p1.y;
		v.z = p3.z - p1.z;
		o->normals[f].x = u.y * v.z - u.z * v.y;
		o->normals[f].y = u.z * v.x - u.x * v.z;
		o->normals[f].z = u.x * v.y - u.y * v.x;
		mm3d_normalise(&o->normals[f]);
		ray.x = p1.x - cam->x;
		ray.y = p1.y - cam->y;
		ray.z = p1.z - cam->z;
		mm3d_normalise(&ray);
		lighting.x = p1.x - o->light.x;
		lighting.y = p1.y - o->light.y;
		lighting.z = p1.z - o->light.z;
		mm3d_normalise(&lighting);
		o->dots[f] = ray.x * o->normals[f].x + ray.y * o->normals[f].y
			     + ray.z * o->normals[f].z;
		if (depthmode == 0) {
			/* the centroid's distance, squared */
			tmp = o->r_centroids[f].m;
			o->depth[f] =
			    (o->r_centroids[f].z * tmp + z - cam->z)
				* (o->r_centroids[f].z * tmp + z - cam->z)
			    + (o->r_centroids[f].y * tmp + y - cam->y)
				* (o->r_centroids[f].y * tmp + y - cam->y)
			    + (o->r_centroids[f].x * tmp + x - cam->x)
				* (o->r_centroids[f].x * tmp + x - cam->x);
		} else {
			/* the furthest vertex's, squared */
			maxd = -32767.0;
			for (k = 0; k < o->facecount[f]; k++) {
				vi = o->face_x_vert[vp + k];
				tmp = o->r_vertices[vi].m;
				vd = (o->r_vertices[vi].z * tmp + z - cam->z)
					 * (o->r_vertices[vi].z * tmp + z
					    - cam->z)
				     + (o->r_vertices[vi].y * tmp + y - cam->y)
					 * (o->r_vertices[vi].y * tmp + y
					    - cam->y)
				     + (o->r_vertices[vi].x * tmp + x - cam->x)
					 * (o->r_vertices[vi].x * tmp + x
					    - cam->x);
				if (vd > maxd)
					maxd = vd;
			}
			o->depth[f] = maxd;
		}
		o->depthindex[f] = f;
		o->distance += sqrt(o->depth[f]);
	}
	o->distance /= f;
	mm3d_sort(o->depth, o->nf, o->depthindex);

	if (hidden) {
		hpx = (short *)mm3d_alloc((unsigned long)o->tot_face_x_vert
					  * sizeof(short));
		hpy = (short *)mm3d_alloc((unsigned long)o->tot_face_x_vert
					  * sizeof(short));
		hpz = (MMFLOAT *)mm3d_alloc((unsigned long)o->tot_face_x_vert
					    * sizeof(MMFLOAT));
		hvis = (unsigned char *)mm3d_alloc((unsigned long)o->nf);
		if (hpx == NULL || hpy == NULL || hpz == NULL || hvis == NULL)
			goto nomem;
		/* project the faces that show, once, for the depth fill
		   and the edges */
		for (f = 0; f < o->nf; f++) {
			MMFLOAT facedot;

			si = o->depthindex[f];
			facedot = o->dots[si];
			if (o->flags[si] & 4)
				facedot = -facedot;
			if ((o->flags[si] & 1) || !(nonormals || facedot < 0))
				continue;
			hvis[si] = 1;
			vp = o->facestart[si];
			for (k = 0; k < o->facecount[si]; k++) {
				int pi = vp + k;

				vi = o->face_x_vert[vp + k];
				x1 = o->r_vertices[vi].x * o->r_vertices[vi].m + x;
				y1 = o->r_vertices[vi].y * o->r_vertices[vi].m + y;
				z1 = o->r_vertices[vi].z * o->r_vertices[vi].m + z;
				at = x1 - cam->x;
				bt = y1 - cam->y;
				ct = z1 - cam->z;
				if (ct > -0.0005 && ct < 0.0005)
					ct = ct < 0.0 ? -0.0005 : 0.0005;
				t = -(cc * z1 + d) / (cc * ct);
				hpx[pi] = (short)(x1 + mm3d_round(at * t)
						  + (maxw >> 1) - cam->x
						  - cam->panx);
				hpy[pi] = (short)(maxh - mm3d_round(y1 + bt * t)
						  - 1);
				hpy[pi] = (short)(hpy[pi] - ((maxh >> 1) - cam->y
							      - cam->pany));
				if (ct < 0.0005)
					ct = 0.0005;
				hpz[pi] = 1.0 / ct;
				if (hpx[pi] < hminx)
					hminx = hpx[pi];
				if (hpx[pi] > hmaxx)
					hmaxx = hpx[pi];
				if (hpy[pi] < hminy)
					hminy = hpy[pi];
				if (hpy[pi] > hmaxy)
					hmaxy = hpy[pi];
				if (clear) {
					if (hpx[pi] > o->xmax)
						o->xmax = hpx[pi];
					if (hpx[pi] < o->xmin)
						o->xmin = hpx[pi];
					if (hpy[pi] > o->ymax)
						o->ymax = hpy[pi];
					if (hpy[pi] < o->ymin)
						o->ymin = hpy[pi];
				}
			}
		}
		if (hminx < 0)
			hminx = 0;
		if (hminy < 0)
			hminy = 0;
		if (hmaxx >= maxw)
			hmaxx = maxw - 1;
		if (hmaxy >= maxh)
			hmaxy = maxh - 1;
		if (hmaxx >= hminx && hmaxy >= hminy) {
			hbw = hmaxx - hminx + 1;
			zb = mm3d_getz(hbw * (hmaxy - hminy + 1));
			if (zb == NULL)
				goto nomem;
			memset(zb, 0, (size_t)hbw * (hmaxy - hminy + 1)
				       * sizeof(MMFLOAT));
			/* the depth of everything that shows */
			for (f = 0; f < o->nf; f++) {
				si = o->depthindex[f];
				if (!hvis[si])
					continue;
				vp = o->facestart[si];
				for (k = 1; k < o->facecount[si] - 1; k++)
					mm3d_zfill(hpx[vp], hpy[vp], hpz[vp],
						   hpx[vp + k], hpy[vp + k],
						   hpz[vp + k], hpx[vp + k + 1],
						   hpy[vp + k + 1],
						   hpz[vp + k + 1], hminx,
						   hminy, hmaxx, hmaxy, zb,
						   hbw);
			}
		}
	}

	if (hidden && zb != NULL) {
		for (f = 0; f < o->nf; f++) {
			si = o->depthindex[f];
			if (!hvis[si])
				continue;
			vp = o->facestart[si];
			if (o->fill[si] == MM3D_NOFILL) {
				vc = o->facecount[si];
				for (k = 0; k < vc; k++) {
					vn = k + 1 == vc ? 0 : k + 1;
					mm3d_zedge(hpx[vp + k], hpy[vp + k],
						   hpz[vp + k], hpx[vp + vn],
						   hpy[vp + vn], hpz[vp + vn],
						   (MMINTEGER)o->line[si], hminx,
						   hminy, hmaxx, hmaxy, zb, hbw);
				}
			} else {
				for (k = 0; k < o->facecount[si]; k++) {
					xc[k] = hpx[vp + k];
					yc[k] = hpy[vp + k];
				}
				mm3d_face(o, xc, yc, si);
			}
		}
	} else {
		/* the faces that point at the camera, furthest first */
		for (f = 0; f < o->nf; f++) {
			si = o->depthindex[f];
			vp = o->facestart[si];
			if (o->flags[si] & 4)
				o->dots[si] = -o->dots[si];
			if (!(nonormals || o->dots[si] < 0))
				continue;
			for (k = 0; k < o->facecount[si]; k++) {
				vi = o->face_x_vert[vp + k];
				x1 = o->r_vertices[vi].x * o->r_vertices[vi].m + x;
				y1 = o->r_vertices[vi].y * o->r_vertices[vi].m + y;
				z1 = o->r_vertices[vi].z * o->r_vertices[vi].m + z;
				/* real space, now projected */
				at = x1 - cam->x;
				bt = y1 - cam->y;
				ct = z1 - cam->z;
				if (ct > -0.0005 && ct < 0.0005)
					ct = ct < 0.0 ? -0.0005 : 0.0005;
				t = -(cc * z1 + d) / (cc * ct);
				xc[k] = (short)(x1 + mm3d_round(at * t)
						+ (maxw >> 1) - cam->x
						- cam->panx);
				yc[k] = (short)(maxh - mm3d_round(y1 + bt * t)
						- 1);
				yc[k] = (short)(yc[k] - ((maxh >> 1) - cam->y
							 - cam->pany));
				if (ct < 0.0005)
					ct = 0.0005;
				invz[k] = 1.0 / ct;
				if (clear) {
					if (xc[k] > o->xmax)
						o->xmax = xc[k];
					if (xc[k] < o->xmin)
						o->xmin = xc[k];
					if (yc[k] > o->ymax)
						o->ymax = yc[k];
					if (yc[k] < o->ymin)
						o->ymin = yc[k];
				}
			}
			if (o->flags[si] & 1)
				continue;
			if (o->flags[si] & 10) {
				fsave = o->fill[si];
				csave = o->line[si];
				if (o->flags[si] & 2)
					o->fill[si] = 0xFF0000;
				if (o->flags[si] & 8) {
					/* lit by the light's direction to the
					   last face's first vertex, as the
					   firmware's loop leaves it */
					ratio = fabs(lighting.x * o->normals[si].x
						     + lighting.y * o->normals[si].y
						     + lighting.z * o->normals[si].z);
					ratio = ratio * o->ambient + o->ambient;
					red = (int)((o->fill[si] & 0xFF0000) >> 16);
					green = (int)((o->fill[si] & 0xFF00) >> 8);
					blue = (int)(o->fill[si] & 0xFF);
					red = (int)mm3d_round(red * ratio);
					green = (int)mm3d_round(green * ratio);
					blue = (int)mm3d_round(blue * ratio);
					o->fill[si] = ((unsigned int)red << 16)
						      | ((unsigned int)green << 8)
						      | (unsigned int)blue;
					red = (int)((o->line[si] & 0xFF0000) >> 16);
					green = (int)((o->line[si] & 0xFF00) >> 8);
					blue = (int)(o->line[si] & 0xFF);
					red = (int)mm3d_round(red * ratio);
					green = (int)mm3d_round(green * ratio);
					blue = (int)mm3d_round(blue * ratio);
					o->line[si] = ((unsigned int)red << 16)
						      | ((unsigned int)green << 8)
						      | (unsigned int)blue;
				}
			}
			if (hidden && zb != NULL && o->fill[si] == MM3D_NOFILL) {
				vc = o->facecount[si];
				for (k = 0; k < vc; k++) {
					vn = k + 1 == vc ? 0 : k + 1;
					mm3d_zedge(xc[k], yc[k], invz[k], xc[vn],
						   yc[vn], invz[vn],
						   (MMINTEGER)o->line[si], hminx,
						   hminy, hmaxx, hmaxy, zb, hbw);
				}
			} else {
				mm3d_face(o, xc, yc, si);
			}
			if (o->flags[si] & 10) {
				o->fill[si] = fsave;
				o->line[si] = csave;
			}
		}
	}
	/* how it was shown, for DRAW3D() and RESTORE */
	o->current.x = x;
	o->current.y = y;
	o->current.z = z;
	o->nonormals = nonormals;
	o->depthmode = depthmode;
	free(hpx);
	free(hpy);
	free(hpz);
	free(hvis);
	return;
nomem:
	free(hpx);
	free(hpy);
	free(hpz);
	free(hvis);
	mm_error("Not enough memory");
}

/* CREATE n, nv, nf, camera, vertices(), counts(), faces(), colours(),
 * [edges()] [, fills()].  Each array arrives as its first element and
 * its element count; an absent one is NULL.  base is OPTION BASE, which
 * the colour indices count from. */
static MMG_UNUSED void mmg3d_create(MMINTEGER n, MMINTEGER nv, MMINTEGER nf,
				    MMINTEGER cam, const MMFLOAT *vertex,
				    MMINTEGER nvert,
				    const MMINTEGER *facecount, MMINTEGER nfc,
				    const MMINTEGER *faces, MMINTEGER nfaces,
				    const MMINTEGER *colours, MMINTEGER ncol,
				    const MMINTEGER *linecolour, MMINTEGER nline,
				    const MMINTEGER *fillcolour, MMINTEGER nfill,
				    int base)
{
	struct mm3d_obj *o;
	MMFLOAT m, cx, cy, cz, scale, tmp;
	MMINTEGER fc = 0, index;
	int f, v, k, vp;

	mm3d_init();
	if (mm_range(n, 1, MM3D_MAX))
		return;
	if (mm3d_tab[n] != NULL)
		MM_RAISE("Object already exists");
	if (nv < 3)
		MM_RAISE("3D object must have a minimum of 3 vertices");
	if (nf < 1)
		MM_RAISE("3D object must have a minimum of 1 face");
	if (mm_range(cam, 1, MM3D_MAXCAM))
		return;
	if (nvert < nv * 3)
		MM_RAISE("Vertex array must be (2, n) with n at least the "
			 "vertex count");
	if (nfc < nf)
		MM_RAISE("Vertex count array too small");
	for (f = 0; f < nf; f++)
		fc += facecount[f];
	if (nfaces < fc)
		MM_RAISE("Face/vertex array too small");
	if (linecolour != NULL && nline < nf)
		MM_RAISE("Line colour  array too small");
	if (fillcolour != NULL && nfill < nf)
		MM_RAISE("Fill colour array too small");

	/* the data look valid, so make the object */
	o = (struct mm3d_obj *)mm3d_alloc(sizeof(struct mm3d_obj));
	if (o == NULL)
		MM_RAISE("Not enough memory");
	mm3d_tab[n] = o;
	o->nf = (int)nf;
	o->nv = (int)nv;
	o->current.x = o->current.y = o->current.z = -32767;
	o->xmin = o->ymin = MM3D_FAR;
	o->xmax = o->ymax = -MM3D_FAR;
	o->camera = (int)cam;
	o->q_vertices = (struct mm3d_q *)mm3d_alloc(nv * sizeof(struct mm3d_q));
	o->r_vertices = (struct mm3d_q *)mm3d_alloc(nv * sizeof(struct mm3d_q));
	o->facecount = (unsigned short *)mm3d_alloc(nf * sizeof(unsigned short));
	o->facestart = (unsigned short *)mm3d_alloc(nf * sizeof(unsigned short));
	o->fill = (unsigned int *)mm3d_alloc(nf * sizeof(unsigned int));
	o->line = (unsigned int *)mm3d_alloc(nf * sizeof(unsigned int));
	o->r_centroids = (struct mm3d_q *)mm3d_alloc(nf * sizeof(struct mm3d_q));
	o->q_centroids = (struct mm3d_q *)mm3d_alloc(nf * sizeof(struct mm3d_q));
	o->dots = (MMFLOAT *)mm3d_alloc(nf * sizeof(MMFLOAT));
	o->depth = (MMFLOAT *)mm3d_alloc(nf * sizeof(MMFLOAT));
	o->flags = (unsigned char *)mm3d_alloc((unsigned long)nf);
	o->depthindex = (int *)mm3d_alloc(nf * sizeof(int));
	o->normals = (struct mm3d_vec *)mm3d_alloc(nf * sizeof(struct mm3d_vec));
	o->face_x_vert = (unsigned short *)mm3d_alloc(fc * sizeof(unsigned short));
	o->colours = (unsigned int *)mm3d_alloc(ncol * sizeof(unsigned int));
	if (o->q_vertices == NULL || o->r_vertices == NULL
	    || o->facecount == NULL || o->facestart == NULL || o->fill == NULL
	    || o->line == NULL || o->r_centroids == NULL
	    || o->q_centroids == NULL || o->dots == NULL || o->depth == NULL
	    || o->flags == NULL || o->depthindex == NULL || o->normals == NULL
	    || o->face_x_vert == NULL || o->colours == NULL) {
		mm3d_free((int)n);
		MM_RAISE("Not enough memory");
	}
	/* each vertex as a unit vector and its length */
	for (v = 0; v < o->nv; v++) {
		m = 0.0;
		o->q_vertices[v].x = *vertex++;
		m += o->q_vertices[v].x * o->q_vertices[v].x;
		o->q_vertices[v].y = *vertex++;
		m += o->q_vertices[v].y * o->q_vertices[v].y;
		o->q_vertices[v].z = *vertex++;
		m += o->q_vertices[v].z * o->q_vertices[v].z;
		if (m != 0.0) {
			m = sqrt(m);
			o->q_vertices[v].x /= m;
			o->q_vertices[v].y /= m;
			o->q_vertices[v].z /= m;
			o->q_vertices[v].w = 0.0;
			o->q_vertices[v].m = m;
		} else {
			o->q_vertices[v].x = 0;
			o->q_vertices[v].y = 0;
			o->q_vertices[v].z = 0;
			o->q_vertices[v].w = 0.0;
			o->q_vertices[v].m = 1.0;
		}
		o->r_vertices[v] = o->q_vertices[v];
	}
	o->tot_face_x_vert = 0;
	o->vmax = 0;
	for (f = 0; f < o->nf; f++) {
		/* More than MAX_3D_POLYGON_VERTICES overruns the firmware's
		   projection arrays; here it is refused. */
		if (facecount[f] < 3 || facecount[f] > MM3D_MAXV) {
			mm3d_free((int)n);
			MM_RAISE(mm3d_fmt(facecount[f] < 3
					  ? "Vertex count less than 3 for face "
					  : "Too many vertices for face ",
					  f + base, ""));
		}
		o->facecount[f] = (unsigned short)facecount[f];
		if (o->facecount[f] > o->vmax)
			o->vmax = o->facecount[f];
		o->facestart[f] = (unsigned short)o->tot_face_x_vert;
		o->tot_face_x_vert += o->facecount[f];
	}
	for (k = 0; k < ncol; k++)
		o->colours[k] = (unsigned int)colours[k];
	for (f = 0; f < o->tot_face_x_vert; f++) {
		/* a vertex the object does not have would be read from
		   outside it; the firmware does not look */
		if (faces[f] < 0 || faces[f] >= nv) {
			mm3d_free((int)n);
			MM_RAISE(mm3d_fmt("Vertex ", faces[f],
					  " does not exist"));
		}
		o->face_x_vert[f] = (unsigned short)faces[f];
	}
	for (f = 0; f < o->nf; f++) {
		if (linecolour != NULL) {
			index = linecolour[f] - base;
			if (index >= ncol || index < 0) {
				mm3d_free((int)n);
				MM_RAISE(mm3d_fmt("Edge colour Index ", index, ""));
			}
			o->line[f] = o->colours[index];
		} else
			o->line[f] = (unsigned int)mm_fg();
		if (fillcolour != NULL) {
			index = fillcolour[f] - base;
			if (index >= ncol || index < 0) {
				mm3d_free((int)n);
				MM_RAISE(mm3d_fmt("Fill colour Index ", index, ""));
			}
			o->fill[f] = o->colours[index];
		} else
			o->fill[f] = MM3D_NOFILL;
		/* each face's centroid, as a unit vector and its length */
		cx = cy = cz = 0;
		vp = o->facestart[f];
		for (v = 0; v < o->facecount[f]; v++) {
			k = o->face_x_vert[vp + v];
			tmp = o->q_vertices[k].m;
			cx += o->q_vertices[k].x * tmp;
			cy += o->q_vertices[k].y * tmp;
			cz += o->q_vertices[k].z * tmp;
		}
		cx /= (MMFLOAT)o->facecount[f];
		cy /= (MMFLOAT)o->facecount[f];
		cz /= (MMFLOAT)o->facecount[f];
		scale = sqrt(cx * cx + cy * cy + cz * cz);
		o->q_centroids[f].x = cx / scale;
		o->q_centroids[f].y = cy / scale;
		o->q_centroids[f].z = cz / scale;
		o->q_centroids[f].m = scale;
		o->q_centroids[f].w = 0;
		o->r_centroids[f] = o->q_centroids[f];
	}
}

/* SHOW (clear 1) and WRITE (clear 0) */
static MMG_UNUSED void mmg3d_show(MMINTEGER n, MMINTEGER x, MMINTEGER y,
				  MMINTEGER z, MMINTEGER nonormals,
				  MMINTEGER depthmode, int clear)
{
	struct mm3d_obj *o = mm3d_get(n);

	if (o == NULL)
		return;
	if (mm3d_cams[o->camera].viewplane == -32767)
		MM_RAISE("Camera position not defined");
	mm3d_display((int)n, (MMFLOAT)x, (MMFLOAT)y, (MMFLOAT)z, clear,
		     (int)nonormals, (int)depthmode);
}

/* ROTATE q(), n: one object; 1 ends the statement */
static MMG_UNUSED int mmg3d_rotate(const MMFLOAT *q, MMINTEGER nq, MMINTEGER n)
{
	struct mm3d_obj *o;
	struct mm3d_q q1;
	int v, f;

	if (nq != 5)
		MM_RAISEV("Argument 1 must be a 5 element floating point array",
			  1);
	o = mm3d_get(n);
	if (o == NULL)
		return 1;
	q1.w = q[0];
	q1.x = q[1];
	q1.y = q[2];
	q1.z = q[3];
	q1.m = q[4];
	for (v = 0; v < o->nv; v++)
		mm3d_qrot(&o->q_vertices[v], &q1, &o->r_vertices[v]);
	for (f = 0; f < o->nf; f++)
		mm3d_qrot(&o->q_centroids[f], &q1, &o->r_centroids[f]);
	return 0;
}

/* HIDE n: 1 ends the statement - an error, or an object already
 * hidden, where cmd_3D returns from the whole list */
static MMG_UNUSED int mmg3d_hide(MMINTEGER n)
{
	struct mm3d_obj *o = mm3d_get(n);

	if (o == NULL)
		return 1;
	if (o->xmin == MM3D_FAR)
		return 1;
	mmg_rect1(0, o->xmin, o->ymin, o->xmax, o->ymax);
	o->xmin = o->ymin = MM3D_FAR;
	o->xmax = o->ymax = -MM3D_FAR;
	return 0;
}

static MMG_UNUSED void mmg3d_hideall(void)
{
	int i;

	mm3d_init();
	for (i = 1; i <= MM3D_MAX; i++) {
		if (mm3d_tab[i] != NULL && mm3d_tab[i]->xmin != MM3D_FAR) {
			mmg_rect1(0, mm3d_tab[i]->xmin, mm3d_tab[i]->ymin,
				  mm3d_tab[i]->xmax, mm3d_tab[i]->ymax);
			mm3d_tab[i]->xmin = mm3d_tab[i]->ymin = MM3D_FAR;
			mm3d_tab[i]->xmax = mm3d_tab[i]->ymax = -MM3D_FAR;
		}
	}
}

/* RESET n: the rotated shape becomes the shape */
static MMG_UNUSED int mmg3d_reset(MMINTEGER n)
{
	struct mm3d_obj *o = mm3d_get(n);
	int v, f;

	if (o == NULL)
		return 1;
	for (v = 0; v < o->nv; v++)
		o->q_vertices[v] = o->r_vertices[v];
	for (f = 0; f < o->nf; f++)
		o->q_centroids[f] = o->r_centroids[f];
	return 0;
}

/* RESTORE n: show a hidden object where it was */
static MMG_UNUSED int mmg3d_restore(MMINTEGER n)
{
	struct mm3d_obj *o = mm3d_get(n);

	if (o == NULL)
		return 1;
	if (o->xmin != MM3D_FAR)
		MM_RAISEV(mm3d_fmt("Object ", n, " is not hidden"), 1);
	mm3d_display((int)n, o->current.x, o->current.y, o->current.z, 1,
		     o->nonormals, o->depthmode);
	return 0;
}

/* CLOSE n: wipe what it drew, and free it */
static MMG_UNUSED int mmg3d_close(MMINTEGER n)
{
	struct mm3d_obj *o = mm3d_get(n);

	if (o == NULL)
		return 1;
	if (o->xmin != MM3D_FAR)
		mmg_rect1(0, o->xmin, o->ymin, o->xmax, o->ymax);
	mm3d_free((int)n);
	return 0;
}

/* CLOSE ALL: closeall3d, which wipes nothing */
static MMG_UNUSED void mmg3d_closeall(void)
{
	int i;

	mm3d_init();
	for (i = 1; i <= MM3D_MAX; i++)
		mm3d_free(i);
	for (i = 1; i <= MM3D_MAXCAM; i++)
		mm3d_cams[i].viewplane = -32767;
	free(mm3d_zbuf);
	mm3d_zbuf = NULL;
	mm3d_zcap = 0;
}

static MMG_UNUSED void mmg3d_light(MMINTEGER n, MMINTEGER x, MMINTEGER y,
				   MMINTEGER z, MMINTEGER ambient)
{
	struct mm3d_obj *o = mm3d_get(n);

	if (o == NULL)
		return;
	o->light.x = (MMFLOAT)x;
	o->light.y = (MMFLOAT)y;
	o->light.z = (MMFLOAT)z;
	o->ambient = (MMFLOAT)ambient / 100.0;
}

/* SET FLAGS n, flag, face, count: one face, count long; 1 ends the
 * statement */
static MMG_UNUSED int mmg3d_flags(MMINTEGER n, MMINTEGER flag,
				  MMINTEGER face, MMINTEGER nbr)
{
	struct mm3d_obj *o = mm3d_get(n);

	if (o == NULL)
		return 1;
	if (mm_range(flag, 0, 255))
		return 1;
	if (face < 0 || face >= o->nf || nbr <= 0 || nbr > o->nf - face)
		MM_RAISEV("Invalid syntax", 1);
	while (--nbr >= 0)
		o->flags[face + nbr] = (unsigned char)flag;
	return 0;
}

/* CAMERA n, viewplane [, x [, y [, panx [, pany]]]] - the absent ones
 * arrive as 0, as the firmware defaults them */
static MMG_UNUSED void mmg3d_camera(MMINTEGER n, MMFLOAT viewplane,
				    MMFLOAT x, MMFLOAT y, MMINTEGER panx,
				    MMINTEGER pany)
{
	mm3d_init();
	if (mm_range(n, 1, MM3D_MAXCAM))
		return;
	mm3d_cams[n].viewplane = viewplane;
	mm3d_cams[n].x = x;
	mm3d_cams[n].y = y;
	mm3d_cams[n].panx = (MMFLOAT)panx;
	mm3d_cams[n].pany = (MMFLOAT)pany;
	mm3d_cams[n].z = 0.0;
}

/* DRAW3D(XMIN | XMAX | YMIN | YMAX | X | Y | DISTANCE | Z n): 1-8 */
static MMG_UNUSED MMFLOAT mmg3d_info(int what, MMINTEGER n)
{
	struct mm3d_obj *o = mm3d_get(n);

	if (o == NULL)
		return 0;
	switch (what) {
	case 1: return o->xmin;
	case 2: return o->xmax;
	case 3: return o->ymin;
	case 4: return o->ymax;
	case 5: return o->current.x;
	case 6: return o->current.y;
	case 7: return o->distance;
	default: return o->current.z;
	}
}

#endif /* MMB_DRAW3D_H */
