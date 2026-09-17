/*
   inoscan - find inodes the disk says are free but that still carry a
   block pointer.

	inoscan [-f] [device]		default /dev/hda2

   THE INVARIANT IT CHECKS.  A free inode's block list is all zero.
   Nothing states that anywhere, but everything depends on it: i_open()
   accepts an inode as fresh on `i_mode == 0 && i_nlink == 0' alone and
   hands the caller whatever i_addr[] held, and f_trunc_blocks() is what
   normally guarantees it - it zeroes each pointer as it frees the block
   ("ino->c_node.i_addr[j] = 0" in the loop).

   So an inode that is free on disk with a non-zero i_addr[] is one that
   was released WITHOUT being truncated.  The next file or pipe to be
   allocated that inode number inherits the stale pointer as one of its
   own data blocks; when that file is deleted or truncated, the pointer
   is handed to blk_free(), and if it is not a legal data block the
   kernel stops with "validblk: invalid blk".  The poison is on the
   card, so it survives a reboot and fires at an unrelated moment.

   Sockets did this every time one was closed: make_socket() kept the
   socket number in i_addr[0], and i_deref() skips f_trunc() for
   F_SOCK.  blk_free() ignores block 0, so slot 0 was invisible and it
   took a resident server - something holding slot 0 while another
   program opened a socket - to make it reachable.

   -f REPAIRS: it zeroes the block list of every free inode that has
   one.  A free inode is referenced by nothing, so this is safe, but do
   it on a quiet machine and reboot afterwards - and run a kernel with
   the fix, or the next socket you close puts another one there.

   Reads the raw partition, so run it after a sync.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#define BLKSIZE		512
#define DINODE_SIZE	256
#define INO_PER_BLOCK	(BLKSIZE / DINODE_SIZE)
#define NADDR		43		/* 40 direct + 3 indirect roots */
#define I_ADDR_OFF	28
#define FS32_MAGIC	0xFB32

static unsigned char sb[BLKSIZE];
static unsigned char blk[BLKSIZE];

static unsigned long g32(const unsigned char *p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
	   ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

static unsigned g16(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

int main(int argc, char *argv[])
{
    const char *dev = "/dev/hda2";
    unsigned long isize, fsize, b, ino, free_inodes = 0;
    int fd, i, j, k, bad = 0, fixed = 0, fix = 0, dirty, thisone;

    for (k = 1; k < argc; k++) {
	if (argv[k][0] == '-' && argv[k][1] == 'f')
	    fix = 1;
	else
	    dev = argv[k];
    }

    fd = open(dev, fix ? O_RDWR : O_RDONLY);
    if (fd < 0) {
	perror(dev);
	return 2;
    }
    if (lseek(fd, (long)BLKSIZE, SEEK_SET) < 0 ||
	read(fd, sb, BLKSIZE) != BLKSIZE) {
	perror("superblock");
	return 2;
    }
    if (g16(sb) != FS32_MAGIC) {
	printf("%s: not FS32 (magic %04x)\n", dev, g16(sb));
	return 2;
    }
    isize = g32(sb + 4);
    fsize = g32(sb + 8);
    printf("%s: isize %lu, fsize %lu, data blocks %lu..%lu\n",
	   dev, isize, fsize, isize, fsize - 1);

    for (b = 2; b < isize; b++) {
	if (lseek(fd, (long)(b * BLKSIZE), SEEK_SET) < 0)
	    break;
	if (read(fd, blk, BLKSIZE) != BLKSIZE)
	    break;
	dirty = 0;
	for (i = 0; i < INO_PER_BLOCK; i++) {
	    unsigned char *d = blk + i * DINODE_SIZE;

	    ino = (b - 2) * INO_PER_BLOCK + i;
	    if (g16(d) || g16(d + 2))		/* mode or nlink: in use */
		continue;
	    free_inodes++;
	    thisone = 0;
	    for (j = 0; j < NADDR; j++) {
		unsigned long a = g32(d + I_ADDR_OFF + 4 * j);

		if (!a)
		    continue;
		printf("inode %lu: FREE but i_addr[%d] = %lu%s\n",
		       ino, j, a,
		       (a < isize || a >= fsize) ? "  <- not a data block"
						 : "");
		bad++;
		thisone++;
	    }
	    if (fix && thisone) {
		memset(d + I_ADDR_OFF, 0, 4 * NADDR);
		dirty = 1;
	    }
	}
	if (dirty) {
	    if (lseek(fd, (long)(b * BLKSIZE), SEEK_SET) < 0 ||
		write(fd, blk, BLKSIZE) != BLKSIZE) {
		perror("write");
		close(fd);
		return 2;
	    }
	    fixed++;
	}
    }

    printf("%lu free inodes, %d stale block pointer(s)%s\n",
	   free_inodes, bad, (fix && bad) ? " - CLEARED" : "");
    if (fix && fixed)
	printf("%d inode block(s) rewritten - sync and reboot\n", fixed);
    close(fd);
    return bad ? 1 : 0;
}
