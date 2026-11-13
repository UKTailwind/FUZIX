/* draw3dharness - DRAW3D LIGHT's colours, checked on the host.
 *
 * The .bas gates see DRAW3D's bounding box but no pixels.  This draws a
 * lit cube through mmb_draw3d.h with mm_fill and mm_line recording the
 * colours they are asked for, and compares them with a model of the
 * reference (Draw3D.c after caee048, #133): each face is lit along the
 * line from the light to that face's own p1, the second vertex of its
 * list, whose normal is p1's.  Before #133 every face used the LAST
 * face's direction; the model works that out too, so a scene that cannot
 * tell the two apart fails here rather than passing on nothing.
 *
 * The scene: draw3d.bas's 100-unit cube at (300, 0, 1000), camera 1 at
 * the origin, viewplane 400.  From there the front face (z = 950) and
 * the left face (x = 250) point at the camera and nothing else does.
 * The light is at (1000, 500, 0), ambient 50, every face white and lit.
 *
 * Build and run:  part of make check, as every tests/...harness.c is.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "mmb_runtime.h"
#include "mmb_draw3d.h"

/* ---- the machine: no screen, only what was asked for -------------- */

#define MAXSEEN 16
static unsigned long seen[MAXSEEN];
static int nseen, errors;

static void saw(MMINTEGER rgb)
{
	int i;

	for (i = 0; i < nseen; i++)
		if (seen[i] == (unsigned long)rgb)
			return;
	if (nseen < MAXSEEN)
		seen[nseen++] = (unsigned long)rgb;
}

MMINTEGER mm_hres(void) { return 640; }
MMINTEGER mm_vres(void) { return 480; }
MMINTEGER mm_fg(void) { return 0xFFFFFF; }
void mm_pixel(MMINTEGER x, MMINTEGER y, MMINTEGER rgb) { saw(rgb); }
void mm_line(MMINTEGER x1, MMINTEGER y1, MMINTEGER x2, MMINTEGER y2,
	     MMINTEGER rgb) { saw(rgb); }
void mm_plot(const short *xy, MMINTEGER n, MMINTEGER rgb) { saw(rgb); }
void mm_fill(const short *xyxy, MMINTEGER n, MMINTEGER rgb)
{
	if (rgb != 0)		/* 0 is SHOW's clear of the old box */
		saw(rgb);
}
void mm_error(const char *msg)
{
	printf("  draw3dharness: error \"%s\"\n", msg);
	errors++;
}
int mm_range(MMINTEGER v, MMINTEGER lo, MMINTEGER hi)
{
	if (v < lo || v > hi) {
		mm_error("range");
		return 1;
	}
	return 0;
}

/* ---- the model ------------------------------------------------------ */

static const MMFLOAT vert[8][3] = {
	{-50, -50, -50}, {50, -50, -50}, {50, 50, -50}, {-50, 50, -50},
	{-50, -50, 50}, {50, -50, 50}, {50, 50, 50}, {-50, 50, 50},
};
static const MMINTEGER faces[24] = {
	0, 3, 2, 1,  4, 5, 6, 7,  1, 2, 6, 5,  0, 4, 7, 3,  3, 7, 6, 2,
	0, 1, 5, 4,
};
static const MMFLOAT pos[3] = {300, 0, 1000};
static const MMFLOAT light[3] = {1000, 500, 0};
#define AMBIENT 50

/* white lit along the light-to-vertex line, onto an axis normal: the
   reference's |L.N| * ambient + ambient, each channel rounded */
static unsigned long lit(int v, int axis)
{
	MMFLOAT l[3], m, ratio, amb = AMBIENT / 100.0;
	int k, c;

	for (k = 0; k < 3; k++)
		l[k] = vert[v][k] + pos[k] - light[k];
	m = sqrt(l[0] * l[0] + l[1] * l[1] + l[2] * l[2]);
	ratio = fabs(l[axis] / m) * amb + amb;
	c = (int)floor(255 * ratio + 0.5);
	return ((unsigned long)c << 16) | ((unsigned long)c << 8)
	       | (unsigned long)c;
}

int main(void)
{
	MMFLOAT v[24];
	MMINTEGER fc[6] = {4, 4, 4, 4, 4, 4}, col[1] = {0xFFFFFF};
	MMINTEGER idx[6] = {0, 0, 0, 0, 0, 0};
	unsigned long want[2], old[2];
	int i, k, fail = 0;

	for (i = 0; i < 8; i++)
		for (k = 0; k < 3; k++)
			v[i * 3 + k] = vert[i][k];
	/* the front face (0) is z-facing and its p1 is vertex 3; the left
	   face (3) is x-facing and its p1 is vertex 4 */
	want[0] = lit((int)faces[0 * 4 + 1], 2);
	want[1] = lit((int)faces[3 * 4 + 1], 0);
	/* before #133: both along the last face's (5) p1, vertex 1 */
	old[0] = lit((int)faces[5 * 4 + 1], 2);
	old[1] = lit((int)faces[5 * 4 + 1], 0);
	if ((want[0] == old[0] && want[1] == old[1]) || want[0] == want[1]) {
		printf("  FAIL draw3dharness: the scene cannot tell per-face "
		       "light from the old rule\n");
		return 1;
	}

	mmg3d_camera(1, 400, 0, 0, 0, 0);
	mmg3d_create(1, 8, 6, 1, v, 24, fc, 6, faces, 24, col, 1, idx, 6,
		     idx, 6, 0);
	(void)mmg3d_flags(1, 8, 0, 6);
	mmg3d_light(1, (MMINTEGER)light[0], (MMINTEGER)light[1],
		    (MMINTEGER)light[2], AMBIENT);
	mmg3d_show(1, (MMINTEGER)pos[0], (MMINTEGER)pos[1],
		   (MMINTEGER)pos[2], 0, 0, 1);

	if (errors)
		fail = 1;
	if (nseen != 2
	    || !((seen[0] == want[0] && seen[1] == want[1])
		 || (seen[0] == want[1] && seen[1] == want[0])))
		fail = 1;
	if (fail) {
		printf("  FAIL draw3dharness: want %06lX %06lX (old rule "
		       "%06lX %06lX), drew", want[0], want[1], old[0], old[1]);
		for (i = 0; i < nseen; i++)
			printf(" %06lX", seen[i]);
		printf("\n");
		return 1;
	}
	printf("  ok draw3dharness: LIGHT per face, %06lX and %06lX\n",
	       want[0], want[1]);
	return 0;
}
