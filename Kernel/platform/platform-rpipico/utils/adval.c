/*
 *	The ADVAL sources - joystick switches and ADC channels - read the
 *	way everything reads pins now: claim them, then the registers.
 *
 *	    adval
 *
 *	This began as an A/B against the kernel's own copy of these
 *	readings, which is how that copy came to be retired.  On the same
 *	board in the same second, the joystick agreed exactly and a
 *	potentiometer on GP41 agreed to 24 counts of 65535 - one and a
 *	half counts of the twelve-bit converter.  (Floating header pins
 *	differed by ~500, which is what reading a high-impedance input
 *	twice does, not a difference between the two paths.)
 *
 *	What is left of that A/B is the first check below: selectors 0 and
 *	1-4 of PICOIOC_ADVAL must now answer 0.  If they ever start
 *	answering something again, two pieces of code are configuring the
 *	same eight pins and one of them will lose - which is exactly the
 *	failure this replaced, where a released pin left the kernel
 *	reading 15 for ever.
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/pc3io.h>
#include "../pico_ioctl.h"

#define JOY_FIRST	34		/* GP34-GP37, pulled up, active low */

int main(void)
{
	int fd, i, v, bad = 0;

	fd = open("/dev/sys", O_RDWR);
	if (fd < 0) {
		perror("/dev/sys");
		return 1;
	}

	for (i = 0; i <= 4; i++) {
		int sel = i;

		if (ioctl(fd, PICOIOC_ADVAL, &sel) != 0) {
			printf("kernel ADVAL(%d) still answers - not retired\n",
			       i);
			bad++;
		}
	}
	if (!bad)
		puts("kernel ADVAL(0..4) retired, as expected");

	for (i = 0; i < 4; i++) {
		int pin = JOY_FIRST + i;

		if (pc3_claim(PLK_PIN, pin)) {
			printf("cannot claim GP%d: errno %d\n", pin, errno);
			return 1;
		}
		pc3_pin_in(pin, 1);
		PC3_REG(PC3_PAD(pin) + PC3_SET) = PC3_PAD_SCHMITT;
	}
	v = 0;
	for (i = 0; i < 4; i++)
		if (!pc3_pin_get(JOY_FIRST + i))
			v |= 1 << i;		/* pressed pulls it LOW */
	printf("joystick (0)  = %2d   %s\n", v,
	       v == 15 ? "<-- all four low: check the pull-ups"
		       : (v == 0 ? "(nothing pressed)" : "(pressed)"));

	if (pc3_claim(PLK_ADC, 0)) {
		printf("cannot claim the ADC: errno %d\n", errno);
		return 1;
	}
	pc3_adc_enable();
	for (i = 1; i <= 4; i++) {
		if (pc3_claim(PLK_PIN, PC3_ADC_GPIO(i))) {
			printf("cannot claim GP%d: errno %d\n",
			       PC3_ADC_GPIO(i), errno);
			return 1;
		}
		pc3_adc_pin(i);
	}
	for (i = 1; i <= 4; i++)
		printf("adc %d (GP%d)   = %5d\n", i, PC3_ADC_GPIO(i),
		       pc3_adc_read(i) << 4);	/* BBC 16-bit convention */

	close(fd);
	return bad ? 1 : 0;
}
