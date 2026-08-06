/**************************************************
UZI (Unix Z80 Implementation) Utilities:  mkfs.c

FS32 version.  The format is defined by
Kernel/platform/platform-rpipico/FS32-FORMAT.md.

Usage changed from classic: the second argument is an INODE COUNT, not
a block count of inodes - "mkfs /dev/hdb3 2048 65536".  The struct
definitions come from <sys/super.h> so this cannot drift from the
libc's view of the superblock.
***************************************************/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/super.h>
#include <time.h>


struct dinode {
    uint16_t i_mode;
    uint16_t i_nlink;
    uint16_t i_uid;
    uint16_t i_gid;
    uint32_t i_size;
    uint32_t   i_atime;		/* Breaks in 2038 */
    uint32_t   i_mtime;		/* Need to hide some extra bits ? */
    uint32_t   i_ctime;		/* 24 bytes */
    uint8_t  i_timeh[3];
    uint8_t  i_pad;
    blkno_t  i_addr[FS32_DIRECT_BLOCKS + 3];
    uint8_t  i_reserved[56];
};               /* Exactly 256 bytes long! */

struct fblk {
    int16_t     f_nfree;
    uint16_t    f_pad;
    blkno_t     f_free[50];
};

_Static_assert(sizeof(struct dinode) == FS32_DINODE_SIZE,
	       "FS32 dinode must be 256 bytes");

#define FILENAME_LEN	30
#define DIR_LEN		32
typedef struct direct {
    uint16_t   d_ino;
    char     d_name[FILENAME_LEN];
} direct;


uint8_t fast=0;     /* flag for fast formatting option */

int dev;

direct dirbuf[16] = { {ROOTINODE, "."}, {ROOTINODE, ".."} };
struct dinode inode[FS32_IPERBLK];
struct fuzix_filesys_kernel fs_tab;

void dwrite(blkno_t blk, char *addr)
{
    if (lseek(dev, (off_t) blk * 512, 0) == -1) {
        perror("lseek");
        exit(1);
    }
    if (write(dev, addr, 512) != 512) {
        perror("write");
        exit(1);
    }
}

char *zerobuf(void)
{
    static char buf[512];

    memset(buf, 0, 512);
    return buf;
}


int yes(void)
{
    char line[20];

    fflush(stdout);

    if (!fgets(line, sizeof(line), stdin) || (*line != 'y' && *line != 'Y'))
	return (0);

    return (1);
}

void mkfs(uint32_t fsize, uint32_t isize, uint16_t inodes)
{
    uint32_t j;
    char *zeros;
    time_t t = time(NULL);

    /* Zero out the blocks */
    printf("Clearing blocks ");
    zeros = zerobuf();		/* Get a zero filled buffer */

    if (!fast) {
	    for (j = 0; j < fsize; ++j) {
	            if ((j & 255) == 0) {
	                putchar('.');
	                fflush(stdout);
	            }
		    dwrite(j, zeros);
            }
    } else {
	    for (j = 0; j < isize; ++j) {
	            putchar('.');
	            fflush(stdout);
		    dwrite(j, zeros);
            }
    }

    /* Initialize the super-block */
    fs_tab.s_mounted = SMOUNTED;	/* Magic number */
    fs_tab.s_version = FS32_VERSION;
    fs_tab.s_isize = isize;
    fs_tab.s_fsize = fsize;
    fs_tab.s_nfree = 1;
    fs_tab.s_free[0] = 0;
    fs_tab.s_tfree = 0;
    fs_tab.s_ninode = 0;
    fs_tab.s_tinode = inodes - 2;
    fs_tab.s_shift = 0;
    fs_tab.s_time = t;
    fs_tab.s_timeh = (t >> 31) >> 1;	/* Mutter .. C standards .. mutter */

    /* Free each block, building the free list.  Chain blocks are the
       explicit fblk struct (FS32-FORMAT.md), never a memory overlay. */

    printf("\nBuilding free list...\n");
    for (j = fsize - 1; j > isize; --j) {
	if (fs_tab.s_nfree == 50) {
	    struct fblk f;
	    static char fbuf[512];
	    memset(fbuf, 0, sizeof(fbuf));
	    memset(&f, 0, sizeof(f));
	    f.f_nfree = fs_tab.s_nfree;
	    memcpy(f.f_free, fs_tab.s_free, sizeof(f.f_free));
	    memcpy(fbuf, &f, sizeof(f));
	    dwrite(j, fbuf);
	    fs_tab.s_nfree = 0;
	}
	++fs_tab.s_tfree;
	fs_tab.s_free[(fs_tab.s_nfree)++] = j;
    }

    /* The inodes are already zeroed out */
    /* create the root dir */

    inode[ROOTINODE].i_mode = S_IFDIR | 0777;
    inode[ROOTINODE].i_nlink = 3;
    inode[ROOTINODE].i_size = 64;
    inode[ROOTINODE].i_addr[0] = isize;

    /* Reserve reserved inode */
    inode[0].i_nlink = 1;
    inode[0].i_mode = ~0;

    printf("Writing initial inode and superblock...\n");

    sync();
    dwrite(2, (char *) inode);
    dwrite(isize, (char *) dirbuf);

    sync();
    /* Write out super block: the in-core kernel struct is 332 bytes;
       pad the block with zeros as the format requires. */
    {
        static char sbuf[512];
        memset(sbuf, 0, sizeof(sbuf));
        memcpy(sbuf, &fs_tab, sizeof(fs_tab));
        dwrite(1, sbuf);
    }

    sync();
    printf("Done.\n");
}

void printopts(void)
{
	fprintf( stderr, "usage: mkfs [options] device inodes fsize\n");
	exit(-1);
}


int main(int argc, char *argv[])
{
    uint32_t fsize, isize, inodes;
    struct stat statbuf;
    int option;

    while((option = getopt(argc, argv, "f")) != -1) {
	    switch( option ){
	    case 'f':
		    fast=1;
		    break;
	    case '?':
	    default:
		    printopts();
	    }
    }

    if (argc-optind < 3)
        printopts();


    if (stat(argv[optind], &statbuf) != 0) {
        fprintf(stderr, "mkfs: can't stat %s\n", argv[optind]);
        exit(-1);
    }

    if (!S_ISBLK(statbuf.st_mode)) {
        fprintf(stderr, "mkfs: %s is not a block device\n", argv[optind]);
        exit(-1);
    }

    inodes = (uint32_t) atol(argv[optind+1]);
    fsize = (uint32_t) atol(argv[optind+2]);

    if (inodes < 2 || inodes > 65535) {
	fprintf(stderr, "mkfs: bad inode count (2..65535)\n");
	exit(-1);
    }
    isize = 2 + ((inodes + 1) >> 1);	/* first data block */

    if (fsize < isize + 2) {
	fprintf(stderr, "mkfs: bad parameter values\n");
	exit(-1);
    }

    printf("Making FS32 filesystem on device %s with %lu inodes fsize %lu. Confirm? ",
	   argv[optind], (unsigned long) inodes, (unsigned long) fsize);
    if (!yes())
	exit(-1);

    dev = open(argv[optind], O_RDWR|O_SYNC);
    if (dev < 0) {
        fprintf(stderr, "mkfs: can't open device %s\n", argv[optind]);
        exit(-1);
    }

    mkfs(fsize, isize, (uint16_t) inodes);

    exit(0);
}
