#include "kernel.h"
#include "printf.h"

#if (BLKSIZE == 512)

/*
 *	File system routines for the usual 512 byte block size
 */

/* Return the number of blocks an inode occupies assuming all blocks present */
blkno_t inode_blocks(inoptr i)
{
    return (i->c_node.i_size + BLKMASK) >> BLKSHIFT;
}

/* Read an inode */
uint_fast8_t breadi(uint16_t dev, uint16_t ino, void *ptr)
{
    struct blkbuf *buf = bread(dev, (ino >> 3) + 2, 0);
    if (buf == NULL)
        return 1;
    blktok(ptr, buf, sizeof(struct dinode) * (ino & 7), sizeof(struct dinode));
    brelse(buf);
    return 0;
}

/* Write an inode */
uint_fast8_t bwritei(inoptr ino)
{
    blkno_t blkno = (ino->c_num >> 3) + 2;
    struct blkbuf *buf = bread(ino->c_dev, blkno, 0);
    if (buf == NULL)
        return 1;
    blkfromk(&ino->c_node, buf, sizeof(struct dinode) * (ino->c_num & 0x07),
            sizeof(struct dinode));
    bfree(buf, 2);
    return 0;
}

#ifdef CONFIG_SB_TRIPWIRE
/*
 *	bmap tripwire - the last unguarded step.
 *
 *	Every read and write of a file goes through bmap, and whatever
 *	block number it returns is used with nothing checking it. For a
 *	451 block file almost all of those numbers come out of an INDIRECT
 *	block, i.e. out of ordinary file data on the disk - so a single
 *	corrupt data block turns into writes landing on arbitrary blocks,
 *	which is how a free list chain block ends up zeroed and blk_alloc
 *	reports "corrupt".
 *
 *	ino_blocks_check() covers i_addr[] but cannot see inside an
 *	indirect block. This does: it names the inode, the logical block
 *	and the physical block, at the moment before it is used.
 */
static blkno_t bmap_check(inoptr ip, blkno_t bn, blkno_t nb, const char *what)
{
    register struct mount *mnt;

    if (nb == 0 || nb == NULLBLK)
        return nb;
    mnt = fs_tab_get(ip->c_dev);
    if (mnt == NULL || mnt->m_fs.s_mounted == 0)
        return nb;
    if (nb < mnt->m_fs.s_isize || nb >= mnt->m_fs.s_fsize) {
        kprintf("\nbmap tripwire(%s): dev %u inode %u logical %u -> block %u"
                " outside %u..%u (size %u)\n",
                what, ip->c_dev, ip->c_num, (unsigned)bn, (unsigned)nb,
                (unsigned)mnt->m_fs.s_isize, (unsigned)mnt->m_fs.s_fsize,
                (unsigned)ip->c_node.i_size);
        panic("bmapblk");
    }
    return nb;
}
#else
#define bmap_check(ip, bn, nb, what) (nb)
#endif

/*
 * Bmap defines the structure of file system storage by returning
 * the physical block number on a device given the inode and the
 * logical block number in a file.  The block is zeroed if created.
 */
blkno_t bmap(inoptr ip, blkno_t bn, unsigned int rwflg)
{
    int i;
    bufptr bp;
    int j;
    blkno_t nb;
    int sh;
    uint16_t dev;
    blkno_t bn0 = bn;           /* bn is decremented below; keep the original
                                   so a tripwire can name the logical block */

    if(getmode(ip) == MODE_R(F_BDEV))
        return(bn);

    dev = ip->c_dev;

    /* blocks 0..17 are direct blocks
    */
    if(bn < 18) {
        nb = ip->c_node.i_addr[bn];
        if(nb == 0) {
            if(rwflg ||(nb = blk_alloc(dev))==0)
                return(NULLBLK);
            ip->c_node.i_addr[bn] = nb;
            ip->c_flags |= CDIRTY;
        }
        return bmap_check(ip, bn0, nb, "direct");
    }

    /* addresses 18 and 19 have single and double indirect blocks.
     * the first step is to determine how many levels of indirection.
     */
    bn -= 18;
    sh = 0;
    j = 2;
    if(bn & 0xff00){       /* bn > 255  so double indirect */
        sh = 8;
        bn -= 256;
        j = 1;
    }

    /* fetch the address from the inode
     * Create the first indirect block if needed.
     */
    if(!(nb = ip->c_node.i_addr[20-j]))
    {
        if(rwflg || !(nb = blk_alloc(dev)))
            return(NULLBLK);
        ip->c_node.i_addr[20-j] = nb;
        ip->c_flags |= CDIRTY;
    }
    bmap_check(ip, bn0, nb, "indirect root");

    /* fetch through the indirect blocks
    */
    for(; j<=2; j++) {
        bp = bread(dev, nb, 0);
        if (bp == NULL) {
            corrupt_fs(ip->c_dev);
            return 0;
        }
        i = (bn >> sh) & 0xff;
        nb = *(blkno_t *)blkptr(bp, (sizeof(blkno_t)) * i, sizeof(blkno_t));
        if (nb)
            brelse(bp);
        else
        {
            if(rwflg || !(nb = blk_alloc(dev))) {
                brelse(bp);
                return(NULLBLK);
            }
            blkfromk(&nb, bp, i * sizeof(blkno_t), sizeof(blkno_t));
            bawrite(bp);
        }
        /* nb came out of a data block on the disk: the one number in
           this whole path that nothing else validates */
        bmap_check(ip, bn0, nb, j == 1 ? "double indirect" : "indirect");
        sh -= 8;
    }
    return(nb);
}

#endif
