#include <stdio.h>
#include <strings.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "util.h"

/* FS32 fsck.  The on-disk format is defined by
 * Kernel/platform/platform-rpipico/FS32-FORMAT.md.  This file keeps its
 * own copies of the structures ON PURPOSE: it is one of the independent
 * implementations that are cross-checked against each other, so it must
 * not share code with ucp/the kernel - only the format document.
 *
 * Ported from the classic fsck; three latent classic bugs fixed on the
 * way (each wrote a repair to the wrong block): the pass-1 double
 * indirect zap wrote to the entry INDEX, setblkno's double-indirect
 * path stored `num` instead of `dnum` and wrote the wrong block back,
 * and pass 4's inode bound was `8 * isize - 2` for `8 * (isize - 2)`.
 */

typedef uint32_t	blkno_t;

struct filesys {
    uint16_t    s_mounted;
    uint16_t    s_version;
    uint32_t    s_isize;
    uint32_t    s_fsize;
    blkno_t     s_tfree;
    int16_t     s_nfree;
    uint16_t    s_tinode;
    blkno_t     s_free[50];
    int16_t     s_ninode;
    uint16_t    s_inode[50];
    uint8_t     s_fmod;
#define FMOD_DIRTY	1
#define FMOD_CLEAN	2
    uint8_t	s_timeh;	/* top bits of time */
    uint32_t    s_time;
    uint8_t	s_shift;
    uint8_t	s_pad0[3];
    uint8_t	s_reserved[180];
};

struct fblk {
    int16_t     f_nfree;
    uint16_t    f_pad;
    blkno_t     f_free[50];
};

#define ROOTINODE 1
#define SMOUNTED 0xFB32   /* Magic number to specify mounted filesystem */
#define SMOUNTED_WRONGENDIAN 0x32FB   /* byteflipped */
#define SMOUNTED_CLASSIC 12742	/* the pre-FS32 format */
#define FS32_VERSION 1

#define IPERBLK 2
#define DIRECT_BLOCKS	40
#define IND_PER_BLOCK	128
#define ONE_IND_END	(DIRECT_BLOCKS + IND_PER_BLOCK)
#define TWO_IND_END	(ONE_IND_END + IND_PER_BLOCK*IND_PER_BLOCK)

struct dinode {
    uint16_t i_mode;
    uint16_t i_nlink;
    uint16_t i_uid;
    uint16_t i_gid;
    uint32_t i_size;
    uint32_t i_atime;
    uint32_t i_mtime;
    uint32_t i_ctime;
    uint8_t  i_timeh[3];
    uint8_t  i_pad;
    blkno_t  i_addr[DIRECT_BLOCKS + 3];
    uint8_t  i_reserved[56];
};               /* Exactly 256 bytes long! */

_Static_assert(sizeof(struct filesys) == 512, "FS32 superblock layout");
_Static_assert(sizeof(struct dinode) == 256, "FS32 dinode must be 256 bytes");
_Static_assert(sizeof(struct fblk) == 204, "FS32 free chain block layout");

#define F_REG   0100000
#define F_DIR   040000
#define F_PIPE  010000
#define F_BDEV  060000
#define F_CDEV  020000

#define F_MASK  0170000

struct direct {
        uint16_t   d_ino;
        char     d_name[30];
};

_Static_assert(sizeof(struct direct) == 32, "dirent stays 32 bytes");

#define MAXDEPTH 20	/* Maximum depth of directory tree to search */

/* This checks a filesystem */

static int dev = 0;
static struct filesys superblock;
static int error;
static int aflag;
static int yflag;

static unsigned char *bitmap;
static int16_t *linkmap;
static uint8_t *daread(blkno_t blk);
static void dwrite(blkno_t blk, uint8_t *addr);
static void iread(uint16_t ino, struct dinode *buf);
static void iwrite(uint16_t ino, struct dinode *buf);
static void setblkno(struct dinode *ino, blkno_t num, blkno_t dnum);
static void ckdir(uint16_t inum, uint16_t pnum, char *name);
static void dirread(struct dinode *ino, uint16_t j, struct direct *dentry);
static void dirwrite(struct dinode *ino, uint16_t j, struct direct *dentry);
static void mkentry(uint16_t inum);
static blkno_t blk_alloc0(struct filesys *filesys);
static blkno_t getblkno(struct dinode *ino, blkno_t num);

static void pass1(void);
static void pass2(void);
static void pass3(void);
static void pass4(void);
static void pass5(void);

/* Total inode slots on the filesystem (including reserved inode 0) */
static unsigned ninodes(void)
{
    return IPERBLK * (swizzle32(superblock.s_isize) - 2);
}

static int yes_noerror(void)
{
    static char buf[16];
    if (yflag) {
        puts("y");
        return 1;
    }
    do {
        if (fgets(buf, 15, stdin) == NULL)
            exit(1);
        if (isupper(*buf))
            *buf = tolower(*buf);
    } while(*buf != 'n' && *buf != 'y');
    return  (*buf == 'y') ? 1 : 0;
}

static int yes(void) {
    int ret = yes_noerror();
    if (ret)
        error |= 1;
    else
        error |= 4;
    return ret;
}

static void bitset(blkno_t b)
{
    bitmap[b >> 3] |= (1 << (b & 7));
}

static void bitclear(blkno_t b)
{
    bitmap[b >> 3] &= ~(1 << (b & 7));
}

static int bittest(blkno_t b)
{
    return (bitmap[b >> 3] & (1 << (b & 7))) ? 1 : 0;
}

static void panic(char *s)
{
	fprintf(stderr, "panic: %s\n", s);
	exit(error | 8);
}

int main(int argc, char **argv)
{
    uint8_t *buf;

    while (argc > 1 && *argv[1] == '-') {
        if (strcmp(argv[1], "-a") == 0) {
            aflag = 1;
        } else if (strcmp(argv[1], "-y") == 0) {
            yflag = 1;
        } else {
            fprintf(stderr, "Bad option: %s\n", argv[1]);
            return 16;
        }
        argc--;
        argv++;
    }

    if(argc != 2){
        fprintf(stderr, "syntax: fsck [-a] [-y] [devfile][:offset]\n");
        return 16;
    }

    if(fd_open(argv[1], 0) < 0) {
        printf("Cannot open file\n");
        return 16;
    }

    buf = daread(1);
    bcopy(buf, (char *) &superblock, sizeof(struct filesys));

    if (superblock.s_fmod == FMOD_DIRTY) {
        printf("Filesystem was not cleanly unmounted.\n");
        error |= 1;
    }
    else if (aflag)
        return 0;

    /* Verify the fsize and isize parameters */
    if (superblock.s_mounted == SMOUNTED_WRONGENDIAN) {
        swizzling = 1;
        printf("Checking file system with reversed byte order.\n");
    }

    if (swizzle16(superblock.s_mounted) == SMOUNTED_CLASSIC) {
        printf("Device %u holds a classic (pre-FS32) filesystem - reformat needed.\n", dev);
        exit(error | 32);
    }

    if (swizzle16(superblock.s_mounted) != SMOUNTED) {
        printf("Device %u has invalid magic number %u. Fix? ", dev, superblock.s_mounted);
        if (!yes())
            exit(error|32);
        superblock.s_mounted = swizzle16(SMOUNTED);
        superblock.s_version = swizzle16(FS32_VERSION);
        dwrite((blkno_t) 1, (uint8_t *) &superblock);
    }

    if (swizzle16(superblock.s_version) != FS32_VERSION ||
        superblock.s_shift != 0) {
        printf("Device %u has FS32 version %u shift %u - this fsck only handles version %u shift 0.\n",
                dev, swizzle16(superblock.s_version), superblock.s_shift,
                FS32_VERSION);
        exit(error | 32);
    }

    printf("Device %u has fsize = %u and isize = %u (%u inodes). Continue? ",
            dev, (unsigned) swizzle32(superblock.s_fsize),
            (unsigned) swizzle32(superblock.s_isize), ninodes());
    if (!yes_noerror())
        exit(error | 32);

    bitmap = calloc((swizzle32(superblock.s_fsize) + 7UL) / 8, sizeof(char));
    linkmap = (int16_t *) calloc(ninodes(), sizeof(int16_t));

    printf("Memory pool %lu bytes\n",
        (unsigned long) (2UL * ninodes() +
        (swizzle32(superblock.s_fsize) + 7UL) / 8));
    if (!bitmap || !linkmap) {
        fprintf(stderr, "Not enough memory.\n");
        exit(error | 8);
    }

    printf("Pass 1: Checking inodes...\n");
    pass1();

    printf("Pass 2: Rebuilding free list...\n");
    pass2();

    printf("Pass 3: Checking block allocation...\n");
    pass3();

    printf("Pass 4: Checking directory entries...\n");
    pass4();

    printf("Pass 5: Checking link counts...\n");
    pass5();

    /* If we fixed things, and no errors were left unconnected */
    if ((error & 5) == 1) {
        superblock.s_fmod = FMOD_CLEAN;
        dwrite((blkno_t) 1, (uint8_t *) &superblock);
    }

    bdclose();

    printf("Done.\n");

    exit(error);
}

/* Range check for a block a pointer may legitimately hold */
static int blk_in_range(blkno_t b)
{
    return b >= swizzle32(superblock.s_isize) &&
           b < swizzle32(superblock.s_fsize);
}

/* Mark the INDIRECT blocks (not data blocks) of the tree below the
 * given root in the allocation bitmap, zapping out-of-range entries.
 * depth is how many levels below the root are still indirect blocks:
 * a double root passes 1, a triple root passes 2.  Data blocks are
 * covered separately by the getblkno() walk.  Works on a private copy
 * of each block because daread() keeps only one block cached. */
static void mark_middles(unsigned n, blkno_t root, int depth)
{
    blkno_t local[IND_PER_BLOCK];
    unsigned b;

    if (root == 0 || depth == 0)
        return;
    memcpy(local, daread(root), 512);
    for (b = 0; b < IND_PER_BLOCK; ++b) {
        blkno_t e = swizzle32(local[b]);
        if (e == 0)
            continue;
        if (!blk_in_range(e)) {
            printf("Inode %u indirect chain entry %u out of range, val = %u. Zap? ",
                    n, b, (unsigned) e);
            if (yes()) {
                uint8_t *buf = daread(root);
                ((blkno_t *) buf)[b] = 0;
                dwrite(root, buf);
                local[b] = 0;
            }
            /* Whether zapped or kept, never follow or map a wild
               pointer - the classic code bitset() it anyway and
               overran the allocation bitmap. */
            continue;
        }
        bitset(e);
        mark_middles(n, e, depth - 1);
    }
}

/*
 *  Pass 1 checks each inode independently for validity, zaps bad block
 *  numbers in the inodes, and builds the block allocation map.
 */

static void pass1(void)
{
    unsigned n;
    struct dinode ino;
    uint16_t mode;
    unsigned b;
    blkno_t bno;
    blkno_t db;
    unsigned icount;
    /* The lowest file size (bytes) at which each indirect root may
       legitimately be nonzero: a root below its threshold is left over
       from a lost truncate. */
    static const uint32_t root_threshold[3] = {
        DIRECT_BLOCKS * 512UL,
        ONE_IND_END * 512UL,
        TWO_IND_END * 512UL
    };

    icount = 0;

    for (n = ROOTINODE; n < ninodes(); ++n) {
        iread(n, &ino);
        linkmap[n] = -1;
        if (ino.i_mode == 0)
            continue;

        mode = swizzle16(ino.i_mode) & F_MASK;
        /* FIXME: named pipes.. */

        /* Check mode */
        if (mode != F_REG && mode != F_DIR && mode != F_BDEV && mode != F_CDEV) {
            printf("Inode %u with mode 0%o is not of correct type. Zap? ",
                    n, swizzle16(ino.i_mode));
            if (yes()) {
                ino.i_mode = 0;
                ino.i_nlink = 0;
                iwrite(n, &ino);
                continue;
            }
        }
        linkmap[n] = 0;
        ++icount;
        /* Check size: off_t is int32 on the target */

        if (swizzle32(ino.i_size) > 0x7FFFFFFFUL) {
            printf("Inode %u size exceeds off_t with value of %lu. Fix? ",
                    n, (unsigned long) swizzle32(ino.i_size));
            if (yes()) {
                ino.i_size = 0;
                iwrite(n, &ino);
            }
        }
        /* Check blocks and build free block map */
        if (mode == F_REG || mode == F_DIR) {
            /* Check the three indirect roots */

            for (b = DIRECT_BLOCKS; b < DIRECT_BLOCKS + 3; ++b) {
                if (ino.i_addr[b] != 0 &&
                    !blk_in_range(swizzle32(ino.i_addr[b]))) {
                    printf("Inode %u indirect root %u out of range, val = %u. Zap? ",
                            n, b, (unsigned) swizzle32(ino.i_addr[b]));
                    if (yes()) {
                        ino.i_addr[b] = 0;
                        iwrite(n, &ino);
                    }
                }
                if (ino.i_addr[b] != 0 &&
                    swizzle32(ino.i_size) < root_threshold[b - DIRECT_BLOCKS]) {
                    printf("Inode %u indirect root %u past end of file, val = %u. Zap? ",
                            n, b, (unsigned) swizzle32(ino.i_addr[b]));
                    if (yes()) {
                        ino.i_addr[b] = 0;
                        iwrite(n, &ino);
                    }
                }
                if (ino.i_addr[b] != 0 &&
                    blk_in_range(swizzle32(ino.i_addr[b])))
                    bitset(swizzle32(ino.i_addr[b]));
            }

            /* Mark the middle blocks of the double and triple trees.
               (The single root's children are data blocks - the
               getblkno walk below covers them.) */
            mark_middles(n, swizzle32(ino.i_addr[DIRECT_BLOCKS + 1]), 1);
            mark_middles(n, swizzle32(ino.i_addr[DIRECT_BLOCKS + 2]), 2);

            /* FIXME: if we have a giant sparse file we need to look up
               the indirect blocks and skip on when they are zero not
               blindly view them all */
            /* Check the data blocks */
            for (bno = 0; bno <= swizzle32(ino.i_size)/512; ++bno) {
                db = getblkno(&ino, bno);

                if (db != 0 && !blk_in_range(db)) {
                    printf("Inode %u block %u out of range, val = %u. Zap? ",
                            n, (unsigned) bno, (unsigned) db);
                    if (yes()) {
                        setblkno(&ino, bno, 0);
                        iwrite(n, &ino);
                    }
                    db = 0;	/* never map a wild pointer */
                }
                if (db != 0)
                    bitset(db);
            }
        }
    }
    /* Fix free inode count in superblock block */
    if (swizzle16(superblock.s_tinode) != ninodes() - ROOTINODE - icount) {
        printf("Free inode count in superblock block is %u, should be %u. Fix? ",
                swizzle16(superblock.s_tinode),
                ninodes() - ROOTINODE - icount);

        if (yes()) {
            superblock.s_tinode =
                swizzle16((uint16_t)(ninodes() - ROOTINODE - icount));
            dwrite((blkno_t) 1, (uint8_t *) &superblock);
        }
    }
}


/* Clear inode free list, rebuild block free list using bit map. */
static void pass2(void)
{
    blkno_t j;
    blkno_t oldtfree;
    int s;

    printf("Rebuild free list? ");
    if (!yes_noerror())
        return;

    error |= 1;
    oldtfree = swizzle32(superblock.s_tfree);

    /* Initialize the superblock-block */

    superblock.s_ninode = 0;
    superblock.s_nfree = swizzle16(1);
    superblock.s_free[0] = 0;
    superblock.s_tfree = 0;

    /* Free each block, building the free list.  Chain blocks are the
       explicit fblk struct (FS32-FORMAT.md), never a memory overlay. */

    for (j = swizzle32(superblock.s_fsize) - 1; j >= swizzle32(superblock.s_isize); --j) {
        if (bittest(j) == 0) {
            if (swizzle16(superblock.s_nfree) == 50) {
                struct fblk f;
                uint8_t fbuf[512];
                memset(fbuf, 0, sizeof(fbuf));
                memset(&f, 0, sizeof(f));
                f.f_nfree = superblock.s_nfree;
                memcpy(f.f_free, superblock.s_free, sizeof(f.f_free));
                memcpy(fbuf, &f, sizeof(f));
                dwrite(j, fbuf);
                superblock.s_nfree = 0;
            }
            superblock.s_tfree = swizzle32(swizzle32(superblock.s_tfree)+1);
            s = swizzle16(superblock.s_nfree);
            superblock.s_free[s++] = swizzle32(j);
            superblock.s_nfree = swizzle16(s);
        }
    }

    dwrite((blkno_t) 1, (uint8_t *) &superblock);

    if (oldtfree != swizzle32(superblock.s_tfree))
        printf("During free list regeneration s_tfree was changed to %u from %u.\n",
                (unsigned) swizzle32(superblock.s_tfree), (unsigned) oldtfree);

}

/* Pass 3 finds and fixes multiply allocated blocks.  As in classic
 * fsck this covers the indirect roots and the data blocks; middle
 * indirect blocks are only duplicate-checked against data blocks via
 * the bitmap pass 1 built, not repaired. */
static void pass3(void)
{
    unsigned n;
    struct dinode ino;
    uint16_t mode;
    unsigned b;
    blkno_t bno;
    blkno_t db;
    blkno_t newno;
    /*--- was blk_alloc ---*/

    for (db = swizzle32(superblock.s_isize); db < swizzle32(superblock.s_fsize); ++db)
        bitclear(db);

    for (n = ROOTINODE; n < ninodes(); ++n) {
        iread(n, &ino);

        mode = swizzle16(ino.i_mode) & F_MASK;
        if (mode != F_REG && mode != F_DIR)
            continue;

        /* Check the indirect roots */

        for (b = DIRECT_BLOCKS; b < DIRECT_BLOCKS + 3; ++b) {
            if (ino.i_addr[b] != 0 &&
                blk_in_range(swizzle32(ino.i_addr[b]))) {
                if (bittest(swizzle32(ino.i_addr[b])) != 0) {
                    printf("Indirect root %u in inode %u value %u multiply allocated. Fix? ",
                            b, n, (unsigned) swizzle32(ino.i_addr[b]));
                    if (yes()) {
                        newno = blk_alloc0(&superblock);
                        if (newno == 0) {
                            printf("Sorry... No more free blocks.\n");
                            error |= 4;
                        } else {
                            dwrite(newno, daread(swizzle32(ino.i_addr[b])));
                            ino.i_addr[b] = swizzle32(newno);
                            iwrite(n, &ino);
                        }
                    }
                } else
                    bitset(swizzle32(ino.i_addr[b]));
            }
        }

        /* Check the rest */
        for (bno = 0; bno <= swizzle32(ino.i_size)/512; ++bno) {
            db = getblkno(&ino, bno);

            if (db != 0 && blk_in_range(db)) {
                if (bittest(db)) {
                    printf("Block %u in inode %u value %u multiply allocated. Fix? ",
                            (unsigned) bno, n, (unsigned) db);
                    if (yes()) {
                        newno = blk_alloc0(&superblock);
                        if (newno == 0) {
                            printf("Sorry... No more free blocks.\n");
                            error |= 4;
                        } else {
                            dwrite(newno, daread(db));
                            setblkno(&ino, bno, newno);
                            iwrite(n, &ino);
                        }
                    }
                } else
                    bitset(db);
            }
        }

    }

}

static int depth;

/*
 *  Pass 4 traverses the directory tree, fixing bad directory entries
 *  and finding the actual number of references to each inode.
 */

static void pass4(void)
{
    depth = 0;
    linkmap[ROOTINODE] = 1;
    ckdir(ROOTINODE, ROOTINODE, "/");
    if (depth != 0)
        panic("Inconsistent depth");
}


/* This recursively checks the directories */

static void ckdir(uint16_t inum, uint16_t pnum, char *name)
{
    struct dinode ino;
    struct direct dentry;
    uint16_t j;
    int c;
    uint8_t i;
    int nentries;
    char *ename;

    iread(inum, &ino);
    if ((swizzle16(ino.i_mode) & F_MASK) != F_DIR)
        return;
    ++depth;

    if (swizzle32(ino.i_size) % 32 != 0) {
        printf("Directory inode %u has improper length. Fix? ", inum);
        if (yes()) {
            ino.i_size = swizzle32(swizzle32(ino.i_size) & ~0x1f);
            iwrite(inum, &ino);
        }
    }
    nentries = swizzle32(ino.i_size)/32;

    for (j = 0; j < nentries; ++j) {
        dirread(&ino, j, &dentry);

        for (i = 0; i < 30; ++i) if (dentry.d_name[i] == '\0') break;
        for (     ; i < 30; ++i) dentry.d_name[i] = '\0';
        dirwrite(&ino, j, &dentry);

        if (dentry.d_ino == 0)
            continue;

        if (swizzle16(dentry.d_ino) < ROOTINODE ||
                swizzle16(dentry.d_ino) >= ninodes()) {
            printf("Directory entry %s%-1.30s has out-of-range inode %u. Zap? ",
                    name, dentry.d_name, swizzle16(dentry.d_ino));
            if (yes()) {
                dentry.d_ino = 0;
                dentry.d_name[0] = '\0';
                dirwrite(&ino, j, &dentry);
                continue;
            }
        }
        if (dentry.d_ino && linkmap[swizzle16(dentry.d_ino)] == -1) {
            printf("Directory entry %s%-1.30s points to bogus inode %u. Zap? ",
                    name, dentry.d_name, swizzle16(dentry.d_ino));
            if (yes()) {
                dentry.d_ino = 0;
                dentry.d_name[0] = '\0';
                dirwrite(&ino, j, &dentry);
                continue;
            }
        }
        ++linkmap[swizzle16(dentry.d_ino)];

        for (c = 0; c < 30 && dentry.d_name[c]; ++c) {
            if (dentry.d_name[c] == '/') {
                printf("Directory entry %s%-1.30s contains slash. Fix? ",
                        name, dentry.d_name);
                if (yes()) {
                    dentry.d_name[c] = 'X';
                    dirwrite(&ino, j, &dentry);
                }
            }
        }

        if (strncmp(dentry.d_name, ".", 30) == 0 && swizzle16(dentry.d_ino) != inum) {
            printf("Dot entry %s%-1.30s points to wrong place. Fix? ",
                    name, dentry.d_name);
            if (yes()) {
                dentry.d_ino = swizzle16(inum);
                dirwrite(&ino, j, &dentry);
            }
        }
        if (strncmp(dentry.d_name, "..", 30) == 0 && swizzle16(dentry.d_ino) != pnum) {
            printf("DotDot entry %s%-1.30s points to wrong place. Fix? ",
                    name, dentry.d_name);
            if (yes()) {
                dentry.d_ino = swizzle16(pnum);
                dirwrite(&ino, j, &dentry);
            }
        }
        if (swizzle16(dentry.d_ino) != pnum &&
                swizzle16(dentry.d_ino) != inum && depth < MAXDEPTH) {
            ename = malloc(strlen(name) + strlen(dentry.d_name) + 2);
            if (ename == NULL) {
                fprintf(stderr, "Not enough memory.\n");
                exit(error |= 8);
            }
            strcpy(ename, name);
            strcat(ename, dentry.d_name);
            strcat(ename, "/");
            ckdir(swizzle16(dentry.d_ino), inum, ename);
            free(ename);
        }
    }
    --depth;
}


/* Pass 5 compares the link counts found in pass 4 with the inodes. */
static void pass5(void)
{
    unsigned n;
    struct dinode ino;

    for (n = ROOTINODE; n < ninodes(); ++n) {
        iread(n, &ino);

        if (ino.i_mode == 0) {
            if (linkmap[n] != -1)
                panic("Inconsistent linkmap");
            continue;
        }

        if (linkmap[n] == -1 && ino.i_mode != 0)
            panic("Inconsistent linkmap");

        if (linkmap[n] > 0 && swizzle16(ino.i_nlink) != linkmap[n]) {
            printf("Inode %u has link count %u should be %u. Fix? ",
                    n, swizzle16(ino.i_nlink), linkmap[n]);
            if (yes()) {
                ino.i_nlink = swizzle16(linkmap[n]);
                iwrite(n, &ino);
            }
        }

        if (linkmap[n] == 0) {
            if ((swizzle16(ino.i_mode) & F_MASK) == F_BDEV ||
                    (swizzle16(ino.i_mode) & F_MASK) == F_CDEV ||
                    (ino.i_size == 0)) {
                printf("Useless inode %u with mode 0%o has become detached. Link count is %u. Zap? ",
                        n, swizzle16(ino.i_mode), swizzle16(ino.i_nlink));
                if (yes()) {
                    ino.i_nlink = 0;
                    ino.i_mode = 0;
                    iwrite(n, &ino);
                    superblock.s_tinode =
                                swizzle16(swizzle16(superblock.s_tinode) + 1);
                    dwrite((blkno_t) 1, (uint8_t *) &superblock);
                }
            } else {
                printf("Inode %u has become detached. Link count is %u. ",
                        n, swizzle16(ino.i_nlink));
                if (ino.i_nlink == 0)
                    printf("Zap? ");
                else
                    printf("Fix? ");
                if (yes()) {
                    if (ino.i_nlink == 0) {
                        ino.i_nlink = 0;
                        ino.i_mode = 0;
                        iwrite(n, &ino);
                        superblock.s_tinode =
                                swizzle16(swizzle16(superblock.s_tinode) + 1);
                        dwrite((blkno_t) 1, (uint8_t *) &superblock);
                    } else {
                        ino.i_nlink = swizzle16(1);
                        iwrite(n, &ino);
                        mkentry(n);
                    }
                }
            }
        }

    }
}


/* This makes an entry in "lost+found" for inode n */
static void mkentry(uint16_t inum)
{
    struct dinode rootino;
    struct direct dentry;
    uint16_t d;

    iread(ROOTINODE, &rootino);
    for (d = 0; d < swizzle32(rootino.i_size)/32; ++d) {
        dirread(&rootino, d, &dentry);
        if (dentry.d_ino == 0 && dentry.d_name[0] == '\0') {
            dentry.d_ino = swizzle16(inum);
            sprintf(dentry.d_name, "l+f%u", inum);
            dirwrite(&rootino, d, &dentry);
            return;
        }
    }
    printf("Sorry... No empty slots in root directory.\n");
    error |= 4;
}

/*
 *  Getblkno gets a pointer index, and a number of a block in the file.
 *  It returns the number of the block on the disk.  A value of zero
 *  means an unallocated block.
 */

static blkno_t getblkno(struct dinode *ino, blkno_t num)
{
    blkno_t indb;
    blkno_t dindb;
    blkno_t *buf;

    if (num < DIRECT_BLOCKS) {		/* Direct block */
        return swizzle32(ino->i_addr[num]);
    }
    num -= DIRECT_BLOCKS;
    if (num < IND_PER_BLOCK) {		/* Single indirect */
        indb = swizzle32(ino->i_addr[DIRECT_BLOCKS]);
        if (indb == 0)
            return (0);
        buf = (blkno_t *) daread(indb);
        return swizzle32(buf[num]);
    }
    num -= IND_PER_BLOCK;
    if (num < (blkno_t) IND_PER_BLOCK * IND_PER_BLOCK) {  /* Double */
        indb = swizzle32(ino->i_addr[DIRECT_BLOCKS + 1]);
        if (indb == 0)
            return (0);
        buf = (blkno_t *) daread(indb);
        dindb = swizzle32(buf[num >> 7]);
        if (dindb == 0)
            return 0;
        buf = (blkno_t *) daread(dindb);
        return swizzle32(buf[num & (IND_PER_BLOCK - 1)]);
    }
    /* Triple indirect */
    num -= (blkno_t) IND_PER_BLOCK * IND_PER_BLOCK;
    indb = swizzle32(ino->i_addr[DIRECT_BLOCKS + 2]);
    if (indb == 0)
        return (0);
    buf = (blkno_t *) daread(indb);
    dindb = swizzle32(buf[num >> 14]);
    if (dindb == 0)
        return 0;
    buf = (blkno_t *) daread(dindb);
    dindb = swizzle32(buf[(num >> 7) & (IND_PER_BLOCK - 1)]);
    if (dindb == 0)
        return 0;
    buf = (blkno_t *) daread(dindb);
    return swizzle32(buf[num & (IND_PER_BLOCK - 1)]);
}


/*
 *  Setblkno sets the given block number of the given file to the given
 *  disk block number in an EXISTING chain.  fsck only ever rewrites a
 *  pointer whose chain it has already walked, so a missing indirect
 *  block is a panic, never a creation.
 */
static void setblkno(struct dinode *ino, blkno_t num, blkno_t dnum)
{
    blkno_t indb;
    blkno_t *buf;

    if (num < DIRECT_BLOCKS) {			/* Direct block */
        ino->i_addr[num] = swizzle32(dnum);
        return;
    }
    num -= DIRECT_BLOCKS;
    if (num < IND_PER_BLOCK) {			/* Single indirect */
        indb = swizzle32(ino->i_addr[DIRECT_BLOCKS]);
        if (indb == 0)
            panic("Missing indirect block");
        buf = (blkno_t *) daread(indb);
        buf[num] = swizzle32(dnum);
        dwrite(indb, (uint8_t *) buf);
        return;
    }
    num -= IND_PER_BLOCK;
    if (num < (blkno_t) IND_PER_BLOCK * IND_PER_BLOCK) {  /* Double */
        indb = swizzle32(ino->i_addr[DIRECT_BLOCKS + 1]);
        if (indb == 0)
            panic("Missing indirect block");
        buf = (blkno_t *) daread(indb);
        indb = swizzle32(buf[num >> 7]);
        if (indb == 0)
            panic("Missing indirect block");
        buf = (blkno_t *) daread(indb);
        buf[num & (IND_PER_BLOCK - 1)] = swizzle32(dnum);
        dwrite(indb, (uint8_t *) buf);
        return;
    }
    /* Triple indirect */
    num -= (blkno_t) IND_PER_BLOCK * IND_PER_BLOCK;
    indb = swizzle32(ino->i_addr[DIRECT_BLOCKS + 2]);
    if (indb == 0)
        panic("Missing indirect block");
    buf = (blkno_t *) daread(indb);
    indb = swizzle32(buf[num >> 14]);
    if (indb == 0)
        panic("Missing indirect block");
    buf = (blkno_t *) daread(indb);
    indb = swizzle32(buf[(num >> 7) & (IND_PER_BLOCK - 1)]);
    if (indb == 0)
        panic("Missing indirect block");
    buf = (blkno_t *) daread(indb);
    buf[num & (IND_PER_BLOCK - 1)] = swizzle32(dnum);
    dwrite(indb, (uint8_t *) buf);
}


/*
 *  blk_alloc0 allocates an unused block.
 *  A returned block number of zero means no more blocks.
 */

/*--- was blk_alloc ---*/
static blkno_t blk_alloc0(struct filesys *filesys)
{
    blkno_t newno;
    struct fblk *f;

    filesys->s_nfree = swizzle16(swizzle16(filesys->s_nfree) - 1);
    newno = swizzle32(filesys->s_free[swizzle16(filesys->s_nfree)]);
    if (!newno) {
        filesys->s_nfree = swizzle16(swizzle16(filesys->s_nfree) + 1);
        return (0);
    }

    /* See if we must refill the s_free array */

    if (!filesys->s_nfree) {
        f = (struct fblk *) daread(newno);
        filesys->s_nfree = f->f_nfree;
        memcpy(filesys->s_free, f->f_free, sizeof(filesys->s_free));
    }

    filesys->s_tfree = swizzle32(swizzle32(filesys->s_tfree) - 1);

    if (newno < swizzle32(filesys->s_isize) || newno >= swizzle32(filesys->s_fsize)) {
        printf("Free list is corrupt.  Did you rebuild it?\n");
        return (0);
    }
    dwrite((blkno_t) 1, (uint8_t *) filesys);
    return (newno);
}

static blkno_t lblk;

static uint8_t *daread(blkno_t blk)
{
    static uint8_t da_buf[512];

    if (blk == lblk)
        return da_buf;

    if (bdread(blk, da_buf) < 0)
        exit(1);
    lblk = blk;
    return da_buf;
}

static void dwrite(blkno_t blk, uint8_t *addr)
{
    if (bdwrite(blk, addr)) {
        exit(1);
    }
    lblk = 0;
}

static void iread(uint16_t ino, struct dinode *buf)
{
    struct dinode *addr;

    addr = (struct dinode *) daread((ino >> 1) + 2);
    bcopy((char *) &addr[ino & 1], (char *) buf, sizeof(struct dinode));
}

static void iwrite(uint16_t ino, struct dinode *buf)
{
    struct dinode *addr;

    addr = (struct dinode *) daread((ino >> 1) + 2);
    bcopy((char *) buf, (char *) &addr[ino & 1], sizeof(struct dinode));
    dwrite((ino >> 1) + 2, (uint8_t *) addr);
}

static void dirread(struct dinode *ino, uint16_t j, struct direct *dentry)
{
    blkno_t blkno;
    uint8_t *buf;

    blkno = getblkno(ino, (blkno_t) j / 16);
    if (blkno == 0)
        panic("Missing block in directory");
    buf = daread(blkno);
    bcopy(buf + 32 * (j % 16), (char *) dentry, 32);
}

static void dirwrite(struct dinode *ino, uint16_t j, struct direct *dentry)
{
    blkno_t blkno;
    uint8_t *buf;

    blkno = getblkno(ino, (blkno_t) j / 16);
    if (blkno == 0)
        panic("Missing block in directory");
    buf = daread(blkno);
    bcopy((char *) dentry, buf + 32 * (j % 16), 32);
    dwrite(blkno, buf);
}
