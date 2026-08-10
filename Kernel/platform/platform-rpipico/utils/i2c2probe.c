/*
 *	Why did I2C2 OPEN fail?  BASIC gets one word back; this gets the
 *	errno, which is the difference between "wrong pins", "already
 *	open" and "the kernel has never heard of this ioctl".
 *
 *	    i2c2probe [sda scl]      default 38 39
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include "../pico_ioctl.h"

/* From Kernel/include/i2c.h - userland has no copy, as i2ctest.c also
   finds. */
struct i2c_msg {
	unsigned char bus;
	unsigned char addr;		/* 7-bit address << 1 | read */
	unsigned char len;
	unsigned char *data;
};
#define I2C_MSG 0x0540

int main(int argc, char *argv[])
{
	struct i2c_open op;
	int fd, sda = 38, scl = 39;

	if (argc >= 3) {
		sda = atoi(argv[1]);
		scl = atoi(argv[2]);
	}
	fd = open("/dev/sys", O_RDWR);
	if (fd < 0) {
		perror("/dev/sys");
		return 1;
	}
	op.bus = 1;
	op.sda = (unsigned char)sda;
	op.scl = (unsigned char)scl;
	op.pad = 0;
	op.khz = 400;
	printf("sizeof(struct i2c_open) = %d\n", (int)sizeof(op));
	errno = 0;
	if (ioctl(fd, PICOIOC_I2COPEN, &op) < 0) {
		printf("I2COPEN sda=%d scl=%d -> FAILED, errno %d\n",
		       sda, scl, errno);
		printf("  (EINVAL is bad pins or speed, EBUSY already open,\n"
		       "   ENOTTY the kernel does not know this ioctl)\n");
		return 1;
	}
	printf("I2COPEN sda=%d scl=%d -> ok\n", sda, scl);

	/* And a transfer, so the whole path is proved, not just the open. */
	{
		struct i2c_msg m;
		unsigned char b = 0xD0;

		m.bus = 1;
		m.addr = (0x76 << 1);           /* write */
		m.len = 1;
		m.data = &b;
		errno = 0;
		if (ioctl(open("/dev/i2c", O_RDWR), I2C_MSG, &m) < 0)
			printf("write to 0x76 -> failed, errno %d\n", errno);
		else
			printf("write to 0x76 -> ack\n");
	}
	return 0;
}
