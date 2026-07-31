/*
 *	PSRAM arena exerciser (PC3-PSRAM-ARENA.md stage 2).
 *
 *	alloc / fill / verify / double-alloc / free / stat / fork (child
 *	must own nothing and the parent's data must survive) / exit.
 *	Run it twice: the second run proves release-on-exit returned
 *	everything.
 */
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include "../pico_ioctl.h"

static int fd;

static void stat_line(const char *tag)
{
	struct psram_stat st;
	if (ioctl(fd, PSRAMIOC_STAT, &st) < 0) {
		printf("%s: STAT failed\n", tag);
		return;
	}
	printf("%s: total %u free %u largest %u\n",
	       tag, (unsigned)st.total, (unsigned)st.free,
	       (unsigned)st.largest);
}

int main(void)
{
	struct psram_req rq;
	volatile unsigned char *p;
	unsigned long i, bad = 0;
	unsigned base1, base2;
	int pid, status;

	fd = open("/dev/sys", O_RDWR);
	if (fd < 0) {
		printf("no /dev/sys\n");
		return 1;
	}
	stat_line("start");

	rq.len = 64 * 1024;
	if (ioctl(fd, PSRAMIOC_ALLOC, &rq) < 0) {
		printf("ALLOC 64K failed\n");
		return 1;
	}
	base1 = rq.base;
	printf("alloc 64K at %x\n", base1);

	p = (volatile unsigned char *)base1;
	for (i = 0; i < 64 * 1024; i++)
		if (p[i])
			bad++;
	printf("zeroed: %s (%lu dirty)\n", bad ? "NO" : "yes", bad);

	for (i = 0; i < 64 * 1024; i++)
		p[i] = (unsigned char)(i * 7 + 3);
	for (bad = 0, i = 0; i < 64 * 1024; i++)
		if (p[i] != (unsigned char)(i * 7 + 3))
			bad++;
	printf("pattern: %s (%lu bad)\n", bad ? "FAIL" : "ok", bad);

	rq.len = 128 * 1024;
	if (ioctl(fd, PSRAMIOC_ALLOC, &rq) < 0) {
		printf("ALLOC 128K failed\n");
		return 1;
	}
	base2 = rq.base;
	printf("alloc 128K at %x\n", base2);
	stat_line("two live");

	pid = fork();
	if (pid == 0) {
		/* the child owns nothing: its FREE of the parent's
		   region must fail */
		unsigned b = base1;
		int r = ioctl(fd, PSRAMIOC_FREE, &b);
		printf("child: free(parent's) %s\n",
		       r < 0 ? "refused - good" : "SUCCEEDED - BAD");
		_exit(0);
	}
	wait(&status);

	/* parent's data must have survived the fork and child exit */
	for (bad = 0, i = 0; i < 64 * 1024; i++)
		if (p[i] != (unsigned char)(i * 7 + 3))
			bad++;
	printf("after fork: %s (%lu bad)\n", bad ? "FAIL" : "ok", bad);
	stat_line("after fork");

	{
		unsigned b = base1;
		if (ioctl(fd, PSRAMIOC_FREE, &b) < 0)
			printf("FREE failed\n");
	}
	stat_line("freed 64K");

	/* base2 leaks deliberately: exit must reclaim it - run the
	   program again and "start" shows the full pool if it did */
	printf("exiting with 128K held: rerun to confirm reclaim\n");
	return 0;
}
