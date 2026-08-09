/*
 *	Exercise /dev/i2c.
 *
 *	    i2ctest              scan the bus
 *	    i2ctest <addr> <reg> read one register (both hex, no 0x)
 *
 *	The BME680 on the PC3's QWIIC socket answers at 0x76 or 0x77 and
 *	its chip-id register is 0xD0, which reads 0x61 - so
 *	"i2ctest 76 d0" is the end-to-end test of the whole path.
 *
 *	Note the two-transaction read: write the register number with a
 *	STOP, then read.  The kernel interface is one transaction per
 *	ioctl and has no repeated START, which nearly every register-file
 *	device tolerates because it keeps its address pointer across the
 *	STOP.  A device that does not will need the interface extended.
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>

/* From Kernel/include/i2c.h - userland has no copy of it. */
struct i2c_msg {
	unsigned char bus;
	unsigned char addr;		/* 7-bit address << 1 | read */
	unsigned char len;
	unsigned char *data;
};
#define I2C_MSG 0x0540

static int fd;

static int xfer(unsigned addr, int read, unsigned char *buf, unsigned len)
{
	struct i2c_msg m;

	m.bus = 0;
	m.addr = (unsigned char)((addr << 1) | (read ? 1 : 0));
	m.len = (unsigned char)len;
	m.data = buf;
	return ioctl(fd, I2C_MSG, &m);
}

int main(int argc, char *argv[])
{
	unsigned addr, reg;
	unsigned char b;

	fd = open("/dev/i2c", O_RDWR);
	if (fd < 0) {
		perror("/dev/i2c");
		return 1;
	}

	if (argc == 1) {
		printf("scanning bus 0:\n");
		for (addr = 0x08; addr < 0x78; addr++) {
			if (xfer(addr, 1, &b, 1) == 0)
				printf("  %02x answers\n", addr);
		}
		printf("done\n");
		return 0;
	}

	if (argc != 3) {
		fputs("usage: i2ctest [<addr> <reg>]   (hex, no 0x)\n", stderr);
		return 1;
	}
	addr = strtoul(argv[1], NULL, 16);
	reg = strtoul(argv[2], NULL, 16);

	b = (unsigned char)reg;
	if (xfer(addr, 0, &b, 1)) {
		fprintf(stderr, "write reg: %s\n", strerror(errno));
		return 1;
	}
	if (xfer(addr, 1, &b, 1)) {
		fprintf(stderr, "read: %s\n", strerror(errno));
		return 1;
	}
	printf("%02x reg %02x = %02x\n", addr, reg, b);
	return 0;
}
