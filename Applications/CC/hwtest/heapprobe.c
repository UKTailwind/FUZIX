/* What does the kernel's PSRAM arena actually hand back, and can the
   bytecode VM read and write through it?  Deliberately does not use
   mmb_runtime: the point is to separate the kernel's arena and bcrun's
   pointer handling from the mm_* runtime on top of them. */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define PSRAMIOC_ALLOC 0x000A

struct req {
	unsigned long len;
	unsigned long base;
};

int main(void)
{
	struct req rq;
	int fd;
	unsigned char *p;
	int i, bad;

	fd = open("/dev/sys", O_RDWR);
	if (fd < 0) {
		printf("open /dev/sys failed\n");
		return 1;
	}
	rq.len = 4096;
	rq.base = 0;
	if (ioctl(fd, PSRAMIOC_ALLOC, &rq) != 0) {
		printf("ioctl failed\n");
		return 1;
	}
	printf("arena gave base %lx len %lu\n", rq.base, rq.len);
	if (!rq.base)
		return 1;

	p = (unsigned char *)rq.base;
	printf("about to write\n");
	for (i = 0; i < 4096; i++)
		p[i] = (unsigned char)(i * 7 + 3);
	printf("write ok, reading back\n");
	bad = 0;
	for (i = 0; i < 4096; i++)
		if (p[i] != (unsigned char)(i * 7 + 3))
			bad++;
	printf("readback mismatches %d\n", bad);
	return 0;
}
