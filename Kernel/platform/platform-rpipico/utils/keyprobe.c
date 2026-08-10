/*
 *	Why does INKEY$ never see a keystroke at this console?
 *
 *	    keyprobe
 *
 *	INKEY$ (mmb_runtime.c, mm_inkey) is four steps: isatty, tcgetattr,
 *	tcsetattr to raw with VMIN=0/VTIME=0, then a one-byte read.  Any
 *	of them failing gives the same silent answer - an empty string -
 *	which is exactly what a BASIC program sees and exactly why the
 *	fault is invisible from up there.  This does the same four steps
 *	and says what each one returned.
 *
 *	Typed characters ARE reaching the tty: they echo.  So the question
 *	is which of these four throws them away.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <termios.h>

int main(void)
{
	struct termios cooked, raw, back;
	int i, n, got = 0;
	unsigned char c;

	printf("isatty(0)      = %d\n", isatty(0));

	errno = 0;
	if (tcgetattr(0, &cooked) != 0) {
		printf("tcgetattr      = FAILED, errno %d\n", errno);
		return 1;
	}
	printf("tcgetattr      = ok\n");
	printf("  c_lflag      = 0x%lx  (ICANON %s, ECHO %s)\n",
	       (unsigned long)cooked.c_lflag,
	       (cooked.c_lflag & ICANON) ? "on" : "off",
	       (cooked.c_lflag & ECHO) ? "on" : "off");
	printf("  VMIN         = %d\n", (int)cooked.c_cc[VMIN]);
	printf("  VTIME        = %d\n", (int)cooked.c_cc[VTIME]);

	raw = cooked;
	raw.c_lflag &= ~(unsigned)(ICANON | ECHO);
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 0;
	errno = 0;
	if (tcsetattr(0, TCSANOW, &raw) != 0) {
		printf("tcsetattr(raw) = FAILED, errno %d\n", errno);
		return 1;
	}
	printf("tcsetattr(raw) = ok\n");

	/* Did it actually TAKE?  A driver may accept the call and ignore
	   the bits - which looks like success and behaves like failure. */
	if (tcgetattr(0, &back) == 0)
		printf("  read back    = c_lflag 0x%lx (ICANON %s), VMIN %d\n",
		       (unsigned long)back.c_lflag,
		       (back.c_lflag & ICANON) ? "STILL ON" : "off",
		       (int)back.c_cc[VMIN]);

	printf("\nnow type - 100 reads, 100ms apart\n");
	for (i = 0; i < 100; i++) {
		errno = 0;
		n = (int)read(0, &c, 1);
		if (n == 1) {
			got++;
			printf("  read -> %d (0x%02x)\n", (int)c, (int)c);
		} else if (n < 0 && errno != EAGAIN) {
			printf("  read -> %d, errno %d\n", n, errno);
		}
		usleep(100000);
	}
	printf("\ngot %d characters\n", got);

	tcsetattr(0, TCSANOW, &cooked);
	return 0;
}
