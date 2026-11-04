/*
 * blink.c: an I/O pin from C on the Colour Maximite 2.
 *
 *	RUN "blink.c", 3
 *
 * GP3 (or the GP given) toggled ten times a second for five seconds,
 * then GP26 read as an analog input.  The pins are numbered as the
 * PicoMite's GPs, as the services number them; MMBasic puts them back
 * when the program ends.
 */
#include <stdio.h>
#include <stdlib.h>
#include <cmm2.h>

int main(int argc, char **argv)
{
	int gp = argc > 1 ? atoi(argv[1]) : 3;
	int i;
	unsigned long uv;

	if (pin_setup(gp, CMM2_PIN_OUT, CMM2_PULL_NONE) != 0) {
		printf("GP%d: %s\n", gp, io_error());
		return 1;
	}
	for (i = 0; i < 50; i++) {
		pin_write(gp, i & 1);
		delay_us(100000);
	}
	if (pin_setup(26, CMM2_PIN_ANALOG, CMM2_PULL_NONE) == 0)
		printf("GP26: %d, %lu uV\n", adc_read(26, &uv), uv);
	return 0;
}
