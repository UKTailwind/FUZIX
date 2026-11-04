/*
 * shapes.c: drawing from C on the Colour Maximite 2.
 *
 *	RUN "shapes.c"
 *
 * The drawing is MMBasic's (BOX, CIRCLE, TEXT...), through the services
 * <cmm2.h> declares; the framebuffer video_mode gives is an ordinary
 * pointer.  Any key ends it.
 */
#include <stdio.h>
#include <cmm2.h>

int main(void)
{
	cmm2_video v;
	int i, x, y;

	if (video_mode(1, 8, &v) != 0) {
		printf("no MODE 1\n");
		return 1;
	}
	con_screen(0);			/* printf to the serial port only */
	gfx_cls(BLACK);
	gfx_box(20, 20, 160, 100, 3, WHITE, RED);
	gfx_rbox(200, 20, 160, 100, 20, YELLOW, BLUE);
	gfx_circle(470, 70, 50, 2, 1.0, GREEN, NOFILL);
	gfx_triangle(560, 120, 650, 20, 740, 120, MAGENTA, ORANGE);
	for (i = 0; i < 32; i++)
		gfx_line(400, 330, 400 + (i - 16) * 24, 180, 1, RGB(i * 8, 255 - i * 8, 160));
	gfx_text(400, 380, "Hello from C", "CM", 1, 3, WHITE, NOFILL);
	for (y = 440; y < 600; y++)	/* a pattern straight into the framebuffer */
		for (x = 0; x < 800; x++)
			v.fb[y * v.pitch + x] = (x ^ y) & 0xFF;
	while (con_getc() < 0)
		if (poll())
			break;
	return 0;
}
