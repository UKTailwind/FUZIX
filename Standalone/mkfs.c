
/**************************************************
UZI (Unix Z80 Implementation) Utilities:  mkfs.c

FS32 version: 32-bit block numbers, 256-byte inodes.  The format is
Kernel/platform/platform-rpipico/FS32-FORMAT.md; this file follows it,
not the other way round.

Usage changed from classic: the second argument is an INODE COUNT, not
a block count of inodes.  "mkfs img 2048 65536" makes a 32MB filesystem
with 2048 inodes whatever the inode size is - scripts stop encoding the
64-byte-inode assumption.  The -b block-size option is gone: it wrote
s_shift layouts nothing could mount.
***************************************************/

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include <fcntl.h>
#define BLKSIZE 512
#include "fuzix_fs.h"
#include "util.h"

char zero512[512];

direct dirbuf[16] = {
        { ROOTINODE, "." },
        { ROOTINODE, ".."}
};

struct dinode inode[IPERBLK];

void dwrite(uint32_t blk, char *addr);

union disk {
	struct filesys fs;
	uint8_t zero[512];
} fs_super;

static void usage(void)
{
	printf("Usage: mkfs [-X] device inodes fsize\n");
	exit(1);
}

int main(int argc, char **argv)
{
	uint32_t fsize, isize, inodes;
	uint32_t j;
	int opt;
	time_t t = time(NULL);

	while((opt = getopt(argc, argv, "X")) != -1) {
		switch(opt) {
			case 'X':
				swizzling = 1;
				break;
			default:
				usage();
		}
	}
	if (argc - optind != 3)
		usage();

	inodes = (uint32_t) strtoul(argv[optind + 1], NULL, 0);
	fsize = (uint32_t) strtoul(argv[optind + 2], NULL, 0);

	/* d_ino is 16-bit: 65535 inodes is the format's cap. */
	if (inodes < 2 || inodes > 65535) {
		printf("Bad inode count (2..65535)\n");
		return -1;
	}
	isize = 2 + ((inodes + 1) >> 1);	/* first data block */

	if (fsize < isize + 2) {
		printf("Bad parameter values\n");
		return -1;
	}

	memset(zero512, 0, 512);

	printf("Making FS32 filesystem with %s byte order on device %s: fsize = %u blocks, %u inodes (isize = %u).\n",
	       swizzling==0 ? "normal" : "reversed", argv[optind],
	       (unsigned) fsize, (unsigned) inodes, (unsigned) isize);

	if (fd_open(argv[optind], O_CREAT)) {
		printf("Can't open device");
		return -1;
	}

	/* Zero out the blocks.  For a big filesystem this is the bulk of
	   the run; hosts are fast and a fully-zeroed image compresses to
	   nothing, so keep it simple. */
	for (j = 0; j < fsize; ++j)
		dwrite(j, zero512);

	/* Initialize the super-block */

	fs_super.fs.s_mounted = swizzle16(SMOUNTED);	/* Magic number */
	fs_super.fs.s_version = swizzle16(FS32_VERSION);
	fs_super.fs.s_isize = swizzle32(isize);
	fs_super.fs.s_fsize = swizzle32(fsize);
	fs_super.fs.s_nfree = swizzle16(1);
	fs_super.fs.s_free[0] = 0;
	fs_super.fs.s_tfree = 0;
	fs_super.fs.s_ninode = 0;
	fs_super.fs.s_tinode = swizzle16((uint16_t)(inodes - 2));
	fs_super.fs.s_shift = 0;
	fs_super.fs.s_time = swizzle32((uint32_t) t);
	fs_super.fs.s_timeh = (uint8_t) (t >> 32);

	/* Build the free list.  Block isize holds the root directory;
	   everything above it is free.  Chain blocks are written as the
	   explicit fblk struct, never the classic &s_nfree overlay
	   (FS32-FORMAT.md, "Free list"). */
	for (j = fsize - 1; j > isize; --j) {
		int n;
		if (swizzle16(fs_super.fs.s_nfree) == 50) {
			fblk f;
			memset(&f, 0, sizeof(f));
			f.f_nfree = fs_super.fs.s_nfree;
			memcpy(f.f_free, fs_super.fs.s_free,
			       sizeof(f.f_free));
			memcpy(zero512, &f, sizeof(f));
			dwrite(j, zero512);
			memset(zero512, 0, 512);
			fs_super.fs.s_nfree = 0;
		}

		fs_super.fs.s_tfree =
		    swizzle32(swizzle32(fs_super.fs.s_tfree) + 1);
		n = swizzle16(fs_super.fs.s_nfree);
		fs_super.fs.s_free[n++] = swizzle32(j);
		fs_super.fs.s_nfree = swizzle16(n);
	}

	/* The inodes are already zeroed out */
	/* create the root dir */
	inode[ROOTINODE].i_mode = swizzle16(F_DIR | (0777 & MODE_MASK));
	inode[ROOTINODE].i_nlink = swizzle16(3);
	inode[ROOTINODE].i_size = swizzle32(64);
	inode[ROOTINODE].i_addr[0] = swizzle32(isize);

	/* Reserve reserved inode */
	inode[0].i_nlink = swizzle16(1);
	inode[0].i_mode = ~0;

	dwrite(2, (char *) inode);

	dirbuf[0].d_ino = swizzle16(dirbuf[0].d_ino);
	dirbuf[1].d_ino = swizzle16(dirbuf[1].d_ino);
	dwrite(isize, (char *) dirbuf);

	/* Write out super block */
	dwrite(1, (char *) &fs_super);
	return 0;
}

void dwrite(uint32_t blk, char *addr)
{
	lseek(dev_fd, (off_t) blk * 512, SEEK_SET);
	if (write(dev_fd, addr, 512) != 512) {
		perror("write");
		exit(1);
	}
}
