#ifndef __BUFSTAT_DOT_H__
#define __BUFSTAT_DOT_H__

/*
 *	Buffer cache reporting: ioctl PIOC_BUFSTAT on /dev/proc.
 *
 *	This exists to hunt the "panic: no free buffers" described in
 *	platform-rpipico/NOTES-buffer-panic.md. That panic has fired with
 *	the machine idle, and an idle machine cannot be under buffer
 *	pressure, so the useful question is not "how many are pinned" but
 *	"who is still holding one". Sampling this between the steps of a
 *	workload names the step that loses a buffer.
 *
 *	So each entry carries the pid and syscall that were current when
 *	the buffer was last pinned. That is the same trick that found the
 *	inode double free: record which code path did it rather than
 *	inferring it from the wreckage.
 *
 *	Shared between the kernel and utils/bufs.c. Both sides are built
 *	by the same compiler for the same target, so the layout is simply
 *	assumed to match; struct bufent is 12 bytes with no padding.
 */

#define PIOC_BUFSTAT	3		/* data -> struct bufstat */

#define BUFSTAT_MAX	32		/* entries the caller must allow for */

struct bufent {
	uint16_t be_dev;		/* 0xFFFF (NO_DEVICE) if never used */
	uint16_t be_blk;
	uint16_t be_time;		/* LRU stamp; age = bs_clock - this */
	uint8_t  be_busy;		/* BF_FREE 0, BF_BUSY 1 */
	uint8_t  be_dirty;
	uint16_t be_pid;		/* who pinned it last */
	uint8_t  be_call;		/* and in which syscall */
	uint8_t  be_pad;
};

struct bufstat {
	uint16_t bs_nbufs;		/* entries actually written */
	uint16_t bs_clock;		/* bufclock right now */
	struct bufent bs_buf[BUFSTAT_MAX];
};

#endif
