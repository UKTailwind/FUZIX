/*
 * printf formatting, and %f in particular.
 *
 * c-testsuite 00195. %f was the last documented hole in the runtime:
 * the compiler could compute in floating point but not print the
 * result, so samples had to scale and cast to an integer to show
 * anything (fp.c still does).
 *
 * It could not be handed to the host's sprintf the way %d is, because
 * bcrun runs on the PC3 against Fuzix libc, whose printf has no
 * floating point at all. So it is converted by hand in bcrun and this
 * is what checks the conversion against gcc.
 */

int printf();

struct point {
	double x;
	double y;
};

struct point pa[100];
double gd = 2.718281828;

int main(void)
{
	double d = 12.34;
	float f = 1.5;
	int i;

	/* the basic case, and two in one call */
	printf("a %f\n", 12.34);
	printf("b %f, %f\n", 12.34, 56.78);
	printf("c %f\n", d);
	printf("d %f\n", gd);

	/* a float argument is promoted to double by the front end */
	printf("e %f\n", (double) f);

	/* zero, negative zero, and the plain integers */
	printf("f %f %f\n", 0.0, -0.0);
	printf("g %f %f\n", 1.0, -1.0);

	/* values that need every digit */
	printf("h %f\n", 0.5);
	printf("i %f\n", 1.0 / 3.0);
	printf("j %f\n", 2.0 / 3.0);
	printf("k %f\n", 123456789.125);
	printf("l %f %f\n", -273.15, 1e-3);

	/* explicit precision, including none */
	printf("m %.2f %.0f %.9f\n", 3.14159265, 3.7, 1.0 / 7.0);
	printf("n %.1f %.3f\n", 0.05, 0.0005);

	/* precision taken from an argument */
	printf("o %.*f\n", 3, 1.0 / 3.0);

	/* width, zero fill and left justification */
	printf("p %10.2f|%-10.2f|%010.2f|\n", 3.5, 3.5, 3.5);

	/* rounding at the last digit. 0.125 is deliberately absent: it is
	   an exact binary half, where bcrun rounds up and a full C library
	   rounds to even, so it is the one shape gcc cannot be used as an
	   oracle for. See the note in bcrun's fmt_double. */
	printf("q %.2f %.2f\n", 1.005, 2.675);
	printf("q2 %.1f %.3f %.0f\n", 9.99, 9.9999, 9.7);

	/* mixed with the integer and string conversions, which also
	   checks that a double advances the argument list by two slots */
	printf("r %d %f %s %f %c\n", 42, 2.5, "mix", 3.5, 'z');
	printf("s %f %d\n", 1.25, 7);

	/* through an array of structs, which is where 00195 found it */
	pa[10].x = 12.34;
	pa[10].y = 56.78;
	printf("t %f, %f\n", pa[10].x, pa[10].y);

	/* computed in a loop */
	for (i = 0; i < 4; i++)
		printf("u %f\n", i * 1.25);

	/* the other conversions, unchanged but worth holding still */
	printf("v %d %u %x %c %s\n", -5, 5u, 255u, 'A', "str");
	printf("w %5d|%-5d|%05d|\n", 42, 42, 42);
	printf("x 100%%\n");

	return 0;
}
