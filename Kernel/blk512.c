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

/* Read an inode: FS32 packs two 256-byte inodes per block.  The
   in-core dinode omits the trailing reserved bytes, so the slot
   arithmetic uses DINODE_SIZE and the copies use sizeof. */
uint_fast8_t breadi(uint16_t dev, uint16_t ino, void *ptr)
{
    struct blkbuf *buf = bread(dev, (ino >> 1) + 2, 0);
    if (buf == NULL)
        return 1;
    blktok(ptr, buf, DINODE_SIZE * (ino & 1), sizeof(struct dinode));
    brelse(buf);
    return 0;
}

/* Write an inode, keeping the on-disk reserved tail zero as the
   format requires */
uint_fast8_t bwritei(inoptr ino)
{
    static const uint8_t dino_zero[DINODE_SIZE - sizeof(struct dinode)];
    blkno_t blkno = (ino->c_num >> 1) + 2;
    struct blkbuf *buf = bread(ino->c_dev, blkno, 0);
    if (buf == NULL)
        return 1;
    blkfromk(&ino->c_node, buf, DINODE_SIZE * (ino->c_num & 1),
            sizeof(struct dinode));
    blkfromk((void *)dino_zero, buf,
            DINODE_SIZE * (ino->c_num & 1) + sizeof(struct dinode),
            sizeof(dino_zero));
    bfree(buf, 2);
    return 0;
}

#ifdef CONFIG_FS_TRIPWIRE_DEEP
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

    /* blocks 0..DIRECT_BLOCKS-1 are direct blocks
    */
    if(bn < DIRECT_BLOCKS) {
        nb = ip->c_node.i_addr[bn];
        if(nb == 0) {
            if(rwflg ||(nb = blk_alloc(dev))==0)
                return(NULLBLK);
            ip->c_node.i_addr[bn] = nb;
            ip->c_flags |= CDIRTY;
        }
        return bmap_check(ip, bn0, nb, "direct");
    }

    /* i_addr[DIRECT_BLOCKS..+2] are the single, double and triple
     * indirect roots.  An indirect block holds IND_PER_BLOCK = 128
     * 32-bit pointers, so each level consumes 7 bits of bn.
     * Determine the level, the root slot, and the shift that extracts
     * the top index.
     */
    bn -= DIRECT_BLOCKS;
    if(bn < IND_PER_BLOCK) {
        j = 1;                  /* levels of indirection to walk */
        sh = 0;
        i = DIRECT_BLOCKS;
    } else if(bn < IND_PER_BLOCK + (blkno_t)IND_PER_BLOCK * IND_PER_BLOCK) {
        bn -= IND_PER_BLOCK;
        j = 2;
        sh = 7;
        i = DIRECT_BLOCKS + 1;
    } else {
        bn -= IND_PER_BLOCK + (blkno_t)IND_PER_BLOCK * IND_PER_BLOCK;
        if (bn >= (blkno_t)IND_PER_BLOCK * IND_PER_BLOCK * IND_PER_BLOCK)
            return NULLBLK;     /* beyond THREE_IND_END */
        j = 3;
        sh = 14;
        i = DIRECT_BLOCKS + 2;
    }

    /* fetch the root from the inode
     * Create the first indirect block if needed.
     */
    if(!(nb = ip->c_node.i_addr[i]))
    {
        if(rwflg || !(nb = blk_alloc(dev)))
            return(NULLBLK);
        ip->c_node.i_addr[i] = nb;
        ip->c_flags |= CDIRTY;
    }
    bmap_check(ip, bn0, nb, "indirect root");

    /* fetch through the indirect blocks
    */
    for(; j > 0; j--) {
        bp = bread(dev, nb, 0);
        if (bp == NULL) {
            corrupt_fs(ip->c_dev);
            return 0;
        }
        i = (bn >> sh) & (IND_PER_BLOCK - 1);
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
        bmap_check(ip, bn0, nb, j > 1 ? "mid indirect" : "indirect");
        sh -= 7;
    }
    return(nb);
}

#endif
