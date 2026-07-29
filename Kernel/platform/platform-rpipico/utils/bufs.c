/*
 *	bufs - report the kernel block buffer cache.
 *
 *	Written to hunt the "panic: no free buffers" in
 *	../NOTES-buffer-panic.md. That panic has fired with the machine
 *	idle, which rules out buffer pressure and means something is
 *	finishing without releasing. So the interesting output is not the
 *	count but the identity: which blocks are still pinned, and which
 *	process and syscall pinned them.
 *
 *	Sample it between the steps of a workload. On an idle machine
 *	every buffer should read busy 0; a buffer that is busy with no
 *	process running is the leak, and be_pid/be_call name the culprit.
 *
 *	    bufs		summary line, plus any pinned buffers
 *	    bufs -v		the whole pool
 *	    bufs -q		summary line only, for scripting
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "../../../include/bufstat.h"
#include "../../../include/syscall_name.h"

#define NO_DEVICE 0xFFFFU

static const char *callname(unsigned n)
{
	if (n < NR_SYSCALL && syscall_name[n])
		return syscall_name[n];
	return "?";
}

static void header(void)
{
	printf("  # dev   blk  busy dirty   age   pid syscall\n");
}

static void show(int i, struct bufent *be, unsigned clock)
{
	printf("%3d ", i);
	if (be->be_dev == NO_DEVICE)
		printf("  -     -");
	else
		printf("%3u %5u", be->be_dev, be->be_blk);
	printf("  %4u %5u %5u %5u %s\n",
	       be->be_busy, be->be_dirty,
	       (unsigned)((clock - be->be_time) & 0xFFFF),
	       be->be_pid, callname(be->be_call));
}

int main(int argc, char *argv[])
{
	struct bufstat bs;
	int fd, i, verbose = 0, quiet = 0;
	unsigned busy = 0, dirty = 0, cached = 0;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-v") == 0)
			verbose = 1;
		else if (strcmp(argv[i], "-q") == 0)
			quiet = 1;
		else {
			fputs("usage: bufs [-v|-q]\n", stderr);
			exit(1);
		}
	}

	fd = open("/dev/proc", O_RDONLY, 0);
	if (fd == -1) {
		perror("/dev/proc");
		exit(1);
	}
	if (ioctl(fd, PIOC_BUFSTAT, &bs) != 0) {
		perror("PIOC_BUFSTAT");
		close(fd);
		exit(1);
	}
	close(fd);

	if (bs.bs_nbufs > BUFSTAT_MAX)		/* kernel clamps, belt and braces */
		bs.bs_nbufs = BUFSTAT_MAX;

	for (i = 0; i < bs.bs_nbufs; i++) {
		if (bs.bs_buf[i].be_busy)
			busy++;
		if (bs.bs_buf[i].be_dirty)
			dirty++;
		if (bs.bs_buf[i].be_dev != NO_DEVICE)
			cached++;
	}

	/* Fixed format: bufwatch.py parses this line. */
	printf("bufs %u busy %u dirty %u cached %u clock %u\n",
	       bs.bs_nbufs, busy, dirty, cached, bs.bs_clock);
	if (quiet)
		return 0;

	if (verbose) {
		header();
		for (i = 0; i < bs.bs_nbufs; i++)
			show(i, &bs.bs_buf[i], bs.bs_clock);
	} else if (busy) {
		/* Unasked for, because a pinned buffer on an idle machine is
		   the whole point of this program. */
		header();
		for (i = 0; i < bs.bs_nbufs; i++)
			if (bs.bs_buf[i].be_busy)
				show(i, &bs.bs_buf[i], bs.bs_clock);
	}
	return 0;
}
