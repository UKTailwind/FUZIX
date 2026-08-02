#undef DEBUG
#include <kernel.h>
#include <kdata.h>
#include <printf.h>

/*
 * There are only two places in the core kernel that know about buffer
 * data and manipulate it directly. This is one of them, and  mm.c is the
 * other. Please keep it that way if at all possible because at some point
 * we will begin supporting out of memory map buffers.
 */

/* N_open is given a string containing a path name in user space,
 * and returns an inode table pointer.  If it returns NULL, the file
 * did not exist.  If the parent existed, and parent is not null,
 * parent will be filled in with the parents inoptr. Otherwise, parent
 * will be set to NULL.
 * The last node parsed is saved in lastname and is useful to some system
 * calls as they want a parent and to create the new node.
 */

uint8_t lastname[31];

static uint_fast8_t n_open_fault;
static uint_fast8_t n_fault_type;
static uint8_t *name, *nameend;

static uint8_t getcf(void)
{
    if (name == nameend) {
        udata.u_error = n_fault_type;
        n_open_fault = 1;
        return 0;
    }
    return (uint8_t)_ugetc(name);
}

inoptr n_open(uint8_t *namep, inoptr *parent)
{
    staticfast inoptr wd;     /* the directory we are currently searching. */
    staticfast inoptr ninode;
    regptr uint8_t *fp;
    regptr inoptr temp;
    uint8_t c;
    usize_t len;

    if (parent)
        *parent = NULLINODE;

    /* Check the user address and length. If it's shorter than 512 bytes this
       is fine, but set nameeend accordingly. This allows us to use _ugetc
       in the hot path which saves us a ton of cycles */
    len = valaddr_r(namep, 512);
    if (len == 0)
        return NULLINODE;

    name = namep;
    nameend = namep + len;
    n_open_fault = 0;

    /* What error do we return if we hit nameend - are we overlong, or out
       of memory space */
    if (len == 512)
        n_fault_type = ENAMETOOLONG;
    else
        n_fault_type = EACCES;

    if(getcf() == '/')
        wd = udata.u_root;
    else
        wd = udata.u_cwd;

    ninode = i_ref(wd);
    i_ref(ninode);

    for(;;)
    {
        /* ninode is the inode we are walking from at this point and wd
           the parent. They may be the same. We hold one reference to each */
        if(ninode)
            magic(ninode);

        /* cheap way to spot rename inside yourself */
        if (udata.u_rename == ninode)
            udata.u_rename = NULLINODE;

        /* See if we are at a mount point.
           If we are a mount point then we swap the parent inode for
           the child inode and swap the reference to the child
           Q: could we set an incore inode flag for mountpoint to speed
              this up ? */
        if(ninode)
            ninode = srch_mt(ninode);

        /* Skip any slashes between nodes. The standards say there can be
           multiple slashes */
        while((c = getcf()) == '/')
            ++name;
        /* It is acceptable to end a file path with / */
        if(!c || n_open_fault)           /* No more components of path? */
            break;

        /* If we failed to find our node we are done */
        if(!ninode){
            udata.u_error = ENOENT;
            goto nodir;
        }
        /* Drop the reference to the old parent */
        i_deref(wd);
        /* Make the parent our own node. We now hold a single reference to
           wd/ninode */
        wd = ninode;
        /* If we are still searching and the parent node is not a directory
           we are done and we failed */
        if(getmode(wd) != MODE_R(F_DIR)){
            udata.u_error = ENOTDIR;
            goto nodir;
        }
        /* If we are now allowed to access the directory then we are done */
        if(!(getperm(wd) & OTH_EX)){
            udata.u_error = EACCES;
            goto nodir;
        }

        /* Walk the filename until / or end. Store the filename of up to
           30 characters in lastname which is also used by our callers. It
           is permissible to give longer name that matches the first 30 */
        fp = lastname;
        while((c = getcf()) != '\0') {
            if (c == '/')
                break;
            if (fp != lastname + 30)
                *fp++ = c;
            ++name;
        }
        /* Terminate the lastname buffer with \0 */
        *fp = 0;
        /* We are going up through a mount point if
           either:
           - We are accessing the root inode
           - We are accessing the root inode number of a device
           and:
           - Our path is ..

           FIXME: re-order tests for speed */
        if((wd == udata.u_root || (wd->c_num == ROOTINODE && wd->c_dev != root_dev)) &&
                lastname[0] == '.' && lastname[1] == '.' && lastname[2] == '\0') {
            /* We are doing /../ */
            if (wd == udata.u_root) {
                i_ref(wd);
                continue;
            }
            /* Find the mount point inode, which is hidden by the mount */
            temp = fs_tab[wd->c_super].m_mntpt;
            /* Take a reference to it */
            i_ref(temp);
            /* Drop the old directory */
            i_deref(wd);
            /* Fall through so we walk the mount point directory .. entry */
            wd = temp;
        }
        /* Find the entry in the directory. ninode will be NULL if we failed or
           valid and referenced if it existed */
        ninode = srch_dir(wd, lastname);
    }
    /* If we faulted then treat it as invalid */
    if (n_open_fault) {
        udata.u_error = n_fault_type;
        goto nodir;
    }

    /* Return the parent node if requested. This is needed by callers that
       do directory manipulation */
    if(parent)
        *parent = wd;
    else
        i_deref(wd);
    /* Check if we failed */
    if(!(parent || ninode))
        udata.u_error = ENOENT;
    /* Return the target node if found. NULL with a valid parent is quite
       possible and indicates the directory exists but the filename is new */
    return ninode;

nodir:
    if(parent)
        *parent = NULLINODE;
    i_deref(wd);
    return NULLINODE;
}

/* Srch_dir is given an inode pointer of an open directory and a string
 * containing a filename, and searches the directory for the file.  If
 * it exists, it opens it and returns the inode pointer, otherwise NULL.
 * This depends on the fact that ba_read will return unallocated blocks
 * as zero-filled, and a partially allocated block will be padded with
 * zeroes.
 */

inoptr srch_dir(register inoptr wd, uint8_t *compname)
{
    register struct direct *d;
    register blkno_t curblock;
    register struct blkbuf *buf;
    uint_fast8_t curentry;
    int nblocks;
    uint16_t inum;

    i_lock(wd);

    nblocks = inode_blocks(wd);

    for(curblock=0; curblock < nblocks; ++curblock) {
        buf = bread(wd->c_dev, bmap(wd, curblock, 1), 0);
        if (buf == NULL)
            break;
        for(curentry = 0; curentry < (BLKSIZE / DIR_LEN); ++curentry) {
            d = blkptr(buf, curentry * DIR_LEN, DIR_LEN);
            if(namecomp(compname, d->d_name)) {
                inum = d->d_ino;
                brelse(buf);
                i_unlock(wd);
                return i_open(wd->c_dev, inum);
            }
        }
        brelse(buf);
    }
    i_unlock(wd);
    return NULLINODE;
}


/* Srch_mt sees if the given inode is a mount point. If so it
 * dereferences it, and references and returns a pointer to the
 * root of the mounted filesystem.
 */

inoptr srch_mt(register inoptr ino)
{
    register uint_fast8_t j;
    register struct mount *m = &fs_tab[0];

    for(j=0; j < NMOUNTS; ++j){
        if(m->m_dev != NO_DEVICE &&  m->m_mntpt == ino) {
            i_deref(ino);
            return i_open(m->m_dev, ROOTINODE);
        }
        m++;
    };
    return ino;
}


/* I_open is given an inode number and a device number,
 * and makes an entry in the inode table for them, or
 * increases it reference count if it is already there.
 * An inode # of zero means a newly allocated inode.
 *
 * Once we support sleeping on bigger boxes during I/O we will need
 * a lock (superblock lock perhaps) to cover allocation of blocks and
 * inodes.
 */

inoptr i_open(register uint16_t dev, uint16_t ino)
{
    register inoptr nindex;
    register inoptr j;
    struct mount *m;
    bool isnew = false;
    bool cached = false;

    validchk(dev, PANIC_IOPEN);

    if(!ino){        /* ino==0 means we want a new one */
        isnew = true;
        ino = i_alloc(dev);
        if(!ino) {
            udata.u_error = ENOSPC;
            return NULLINODE;
        }
    }

    m = fs_tab_get(dev);

    /* Maybe make this DEBUG only eventually - the fs_tab_get cost
       is higher than ideal */
    if(ino < ROOTINODE || ino >= (m->m_fs.s_isize - 2) * INO_PER_BLOCK) {
        kputs("i_open: bad inode number\n");
        return NULLINODE;
    }

    nindex = NULLINODE;
    for(j = i_tab; j<i_tab + ITABSIZE; j++){
        if(!j->c_refs) // free slot?
            nindex = j;

        if(j->c_dev == dev && j->c_num == ino) {
            nindex = j;
            cached = true;
            goto found;
        }
    }
    /* Not already in the table. */

    if(!nindex){      /* No unrefed slots in inode table */
        udata.u_error = ENFILE;
        goto lost;
    }

    if (breadi(dev, ino, &nindex->c_node))
        goto lost;
#ifdef CONFIG_FS_TRIPWIRE_DEEP
    ino_blocks_check(dev, ino, &nindex->c_node, "read");
#endif

    nindex->c_dev = dev;
    nindex->c_num = ino;
    nindex->c_super = m - fs_tab;
    nindex->c_magic = CMAGIC;
    nindex->c_flags = (m->m_flags & MS_RDONLY) ? CRDONLY : 0;
found:
    if(isnew) {
        /*
         * A freshly allocated inode has to be checked against the
         * disk, not against whatever this table entry still holds
         * from the last file that used this inode number.
         *
         * The loop above takes a matching entry whatever its
         * reference count, and that path does not read the inode - so
         * a newly allocated number that happened to be cached was
         * validated against a stale copy. The in-core inode and the
         * disk then disagree, and the next i_deref frees an inode
         * that is really in use, putting a live file on the free
         * list.
         */
        if (cached) {
            /*
             * ...unless the entry is in use. If i_alloc has handed out
             * an inode that is open, the in-core copy belongs to the
             * file using it and holds block pointers and a size that
             * are not on disk yet. Overwriting it here loses all of
             * that, and the stale copy is then written back at close -
             * a 24K file was destroyed exactly this way. Refuse the
             * allocation instead and leave the open file alone.
             */
            if (nindex->c_refs)
                goto badino;
            if (breadi(dev, ino, &nindex->c_node))
                return NULLINODE;
        }
        if(nindex->c_node.i_nlink || nindex->c_node.i_mode & F_MASK)
            goto badino;
    } else {
        if(!(nindex->c_node.i_nlink && nindex->c_node.i_mode & F_MASK))
            goto badino;
    }
    nindex->c_refs++;
    return nindex;

badino:
    /*
     * Say which inode and what was wrong with it. The bare message
     * this replaces gave no way to tell an allocator handing out a
     * live inode from a directory entry pointing at a dead one, and
     * the caller turns this into ENFILE - "File table overflow" -
     * which sends you looking at the wrong thing entirely.
     */
    kprintf("i_open: bad inode %u %s mode %x nlink %u\n",
            (uint16_t)ino, isnew ? "new" : "old",
            (uint16_t)nindex->c_node.i_mode,
            (uint16_t)nindex->c_node.i_nlink);
    return NULLINODE;

lost:
    /*
     * We allocated an inode and are now failing for an unrelated
     * reason. Give it back, or it stays marked in use with nothing
     * pointing at it and s_tinode drifts down by one every time. That
     * is the "free inode count in superblock is N, should be N+31"
     * fsck reports after a heavy run.
     *
     * Only on these paths. A badino failure with isnew means the inode
     * we were handed is a live file, and freeing it again is precisely
     * the thing being fixed.
     */
    if (isnew)
        i_free(dev, ino);
    return NULLINODE;
}

bool emptydir(register inoptr wd)
{
    struct direct curentry;

    i_islocked(wd);

    udata.u_offset =  2 * DIR_LEN;	/* . .. ignored */

    do
    {
        udata.u_count = DIR_LEN;
        udata.u_base  = (uint8_t *)&curentry;
        udata.u_sysio = true;
        readi(wd, 0);

        /* Read until EOF or name is found.  readi() advances udata.u_offset */
        if (*curentry.d_name)
            return false;
    } while(udata.u_done == DIR_LEN);

    return true;
}


/* Ch_link modifies or makes a new entry in the directory for the name
 * and inode pointer given. The directory is searched for oldname.  When
 * found, it is changed to newname, and it inode # is that of *nindex.
 * A oldname of "" matches a unused slot, and a nindex of NULLINODE
 * means an inode # of 0.  A return status of 0 means there was no
 * space left in the filesystem, or a non-empty oldname was not found,
 * or the user did not have write permission.
 */

bool ch_link(register inoptr wd, uint8_t *oldname, uint8_t *newname, inoptr nindex)
{
    struct direct curentry;
    register int i;

    i_islocked(wd);

    if (wd->c_flags & CRDONLY) {
        udata.u_error = EROFS;
        return false;
    }
    /* FIXME: for modern style permissions we should also check whether
       wd has the sticky bit set and if so require ownership or root */
    if(!(getperm(wd) & OTH_WR))
    {
        udata.u_error = EACCES;
        return false;
    }
    /* Inserting a new blank entry ? */
    if (!*newname && nindex != NULLINODE) {
        udata.u_error = EEXIST;
        return false;
    }

    /* Search the directory for the desired slot. */

    udata.u_offset = 0;

    for(;;)
    {
        udata.u_count = DIR_LEN;
        udata.u_base  =(uint8_t *)&curentry;
        udata.u_sysio = true;
        readi(wd, 0);

        /* Read until EOF or name is found.  readi() advances udata.u_offset */
        if(udata.u_done == 0 || namecomp(oldname, curentry.d_name))
            break;
    }

    if(udata.u_done == 0 && *oldname) {
        udata.u_error = ENOENT;
        return false;                  /* Entry not found */
    }

    memcpy(curentry.d_name, newname, FILENAME_LEN);
    /* FIXME: add strncpy and use for this */
    for(i = 0; i < FILENAME_LEN; ++i)
        if(curentry.d_name[i] == '\0')
            break;
    for(; i < FILENAME_LEN; ++i)
        curentry.d_name[i] = '\0';

    if(nindex)
        curentry.d_ino = nindex->c_num;
    else
        curentry.d_ino = 0;

    /* If an existing slot is being used, we must back up the file offset */
    if(udata.u_done){
        udata.u_offset -= DIR_LEN;
    }

    udata.u_count = DIR_LEN;
    udata.u_base  = (unsigned char*)&curentry;
    udata.u_sysio = true;
    writei(wd, 0);

    if(udata.u_error)
        return false;

    setftime(wd, A_TIME|M_TIME|C_TIME);     /* Sets CDIRTY */

    /* Update file length to next block */
    if(BLKOFF(wd->c_node.i_size))
        wd->c_node.i_size += BLKSIZE - BLKOFF(wd->c_node.i_size);

    return true; // success
}

/* Namecomp compares two strings to see if they are the same file name.
 * It stops at FILENAME_LEN chars or a null or a slash. It returns 0 for difference.
 *
 * TODO: This generates crap code on most compilers so we probably ought to
 * turn it into platform asm code.
 */
bool namecomp(uint8_t *n1, uint8_t *n2) // return true if n1 == n2
{
    uint_fast8_t n; // do we have enough variables called n?

    n = FILENAME_LEN;
    while(*n1 && *n1 != '/')
    {
        if(*n1++ != *n2++)
            return false; // mismatch
        n--;
        if(n==0)
            return true; // match
    }

    return (*n2 == '\0' || *n2 == '/');
}


/* Newfile is given a pointer to a directory and a name, and creates
 * an entry in the directory for the name, dereferences the parent,
 * and returns a pointer to the new inode.  It allocates an inode
 * number, and creates a new entry in the inode table for the new
 * file, and initializes the inode table entry for the new file.
 * The new file will have one reference, and 0 links to it.
 * Better make sure there isn't already an entry with the same name.
 *
 * Returns the new inode locked so nobody can access it before ready. We need
 * to think hard about newfile taking a callback to fix up the ino struct so
 * it's cleaner ???
 */

inoptr newfile(register inoptr pino, uint8_t *name)
{
    register inoptr nindex;
    register uint_fast8_t j;

    /* No parent? */
    if (!pino) {
        udata.u_error = ENXIO;
        goto nogood;
    }

    /* We check getperm before CRDONLY because if you reverse these two
       it breaks gcc 68hc11 3.4 */
    if (!(getperm(pino) & OTH_WR)) {
        udata.u_error = EPERM;
        goto nogood;
    }

    /* First see if parent is writeable */
    if (pino->c_flags & CRDONLY) {
        udata.u_error = EROFS;
        goto nogood;
    }

    if (!(nindex = i_open(pino->c_dev, 0))) {
        udata.u_error = ENFILE;
        goto nogood;
    }

    i_lock(pino);	/* Lock in tree order */
    i_lock(ino);
    /* This does not implement BSD style "sticky" groups */
    nindex->c_node.i_uid = udata.u_euid;
    nindex->c_node.i_gid = udata.u_egid;

    nindex->c_node.i_mode = F_REG;   /* For the time being */
    nindex->c_node.i_nlink = 1;
    nindex->c_node.i_size = 0;
    for (j = 0; j < 20; j++) {
        nindex->c_node.i_addr[j] = 0;
    }
    wr_inode(nindex);
    if (!ch_link(pino, (uint8_t *)"", name, nindex)) {
        i_deref(nindex);
	/* ch_link sets udata.u_error */
        goto nogood;
    }
    i_unlock_deref(pino);
    return nindex;

nogood:
    i_unlock_deref(pino);
    return NULLINODE;
}


/* Check the given device number, and return its address in the mount
 * table.  Also time-stamp the superblock of dev, and mark it modified.
 * Used when freeing and allocating blocks and inodes.
 */

#ifdef CONFIG_FS_TRIPWIRE_DEEP
/*
 *	Superblock tripwire.
 *
 *	Filesystem corruption on this port has twice been discovered long
 *	after the fact, by blk_alloc or i_alloc finding nonsense and by
 *	fsck afterwards - by which time the damage is done and there is
 *	nothing left to say who did it.  This checks the superblock's
 *	invariants at every filesystem operation instead, so the first
 *	operation after the damage stops the machine with the field named
 *	rather than the hundredth one destroying a file.
 *
 *	It checks invariants rather than comparing against a saved copy:
 *	half of a superblock changes legitimately on every allocation, so
 *	a byte compare would cry wolf constantly, and it would cost 900
 *	bytes of kernel memory this platform does not have spare.
 *
 *	Everything below must hold for any superblock the kernel is
 *	willing to allocate from.  If one does not, the filesystem is
 *	already damaged and going further writes the damage to the card.
 */
static void sb_validate(struct mount *mnt, const char *where)
{
    fsptr fs = &mnt->m_fs;
    const char *why = NULL;
    uint16_t bad = 0;
    int i;

    if (fs->s_mounted != SMOUNTED) {
        why = "magic"; bad = fs->s_mounted;
    } else if (fs->s_isize < 2 || fs->s_isize >= fs->s_fsize) {
        why = "isize"; bad = fs->s_isize;
    } else if (fs->s_nfree > FILESYS_TABSIZE) {
        why = "nfree"; bad = fs->s_nfree;
    } else if (fs->s_ninode < 0 || fs->s_ninode > FILESYS_TABSIZE) {
        why = "ninode"; bad = (uint16_t)fs->s_ninode;
    } else if (fs->s_tfree > fs->s_fsize) {
        why = "tfree"; bad = fs->s_tfree;
    } else {
        /* A free block must be zero (the end of the list) or lie in
           the data area - never in the superblock or the inodes. */
        for (i = 0; i < fs->s_nfree; i++) {
            blkno_t b = fs->s_free[i];
            if (b && (b < fs->s_isize || b >= fs->s_fsize)) {
                why = "free"; bad = b;
                break;
            }
        }
        if (!why) {
            for (i = 0; i < fs->s_ninode; i++) {
                uint16_t n = fs->s_inode[i];
                if (n < 2 || n >= (uint16_t)((fs->s_isize - 2) * 8)) {
                    why = "inode"; bad = n;
                    break;
                }
            }
        }
    }

    if (why) {
        kprintf("\nsuperblock tripwire: dev %u bad %s = %u at %s\n",
                mnt->m_dev, why, bad, where);
        kprintf("  mounted %u isize %u fsize %u nfree %u ninode %d\n",
                mnt->m_fs.s_mounted, mnt->m_fs.s_isize, mnt->m_fs.s_fsize,
                mnt->m_fs.s_nfree, mnt->m_fs.s_ninode);
        kprintf("  tfree %u tinode %u fmod %u\n",
                mnt->m_fs.s_tfree, mnt->m_fs.s_tinode, mnt->m_fs.s_fmod);
        panic("superblock");
    }
}
#else
#define sb_validate(mnt, where) do { } while (0)
#endif

fsptr getdev(uint16_t dev)
{
    register struct mount *mnt;
    time_t t;

    mnt = fs_tab_get(dev);

    if (!mnt || mnt->m_fs.s_mounted == 0) {
        panic(PANIC_GD_BAD);
        /* Return needed to persuade SDCC all is ok */
        return NULL;
    }
    /* Every filesystem operation comes through here, so this is the
       one place that sees them all. */
    sb_validate(mnt, "getdev");
    if (!(mnt->m_flags & MS_RDONLY)) {
        rdtime(&t);
        mnt->m_fs.s_time = t.low;
        mnt->m_fs.s_timeh = t.high;
        mnt->m_fs.s_fmod = FMOD_DIRTY;
    }
    return &mnt->m_fs;
}


/* Returns true if the magic number of a superblock is corrupt. */

bool inline baddev(fsptr dev)
{
    return(dev->s_mounted != SMOUNTED);
}


/* I_alloc finds an unused inode number, and returns it, or 0
 * if there are no more inodes available.
 *
 * This will need to happen under the superblock lock once we do sleeping
 */

/*
 *	DEBUG: which code path put each free list entry there - 'F' for
 *	i_free, 'S' for the disk scan. When i_alloc finds a live inode on
 *	the list this says where it came from, which is the one thing the
 *	message on its own could never tell us. Indexed the same as
 *	dev->s_inode[]; single device is fine for the purpose.
 */

uint16_t i_alloc(uint16_t devno)
{
    staticfast fsptr dev;
    staticfast blkno_t blk;
    register struct dinode *di;
    staticfast uint16_t j;
    register struct blkbuf *buf;
    staticfast struct mount *mnt;
    uint16_t k;
    unsigned ino;
    uint16_t ret;

    if(baddev(dev = getdev(devno))) {
        /* Before the lock is held, so report and leave directly */
        kputs("i_alloc: corrupt superblock\n");
        dev->s_mounted = 1;
        udata.u_error = ENOSPC;
        return 0;
    }

    /*
     * V7's s_ilock, which this function's own comment asked for. The
     * rebuild below refills s_inode[] from index 0 and only assigns
     * s_ninode when it has finished, and it sleeps in block I/O to get
     * there. Two processes creating files at once therefore ran two
     * rebuilds into one array: the second overwrote entries the first
     * had already handed out, and those inodes came back round as free
     * while they were live files.
     */
    mnt = fs_tab_get(devno);
    while (mnt->m_ilock)
        psleep(&mnt->m_ilock);
    mnt->m_ilock = 1;

tryagain:
    while(dev->s_ninode) {
        struct dinode check;
        if(!(dev->s_tinode))
            goto corrupt;
        ino = dev->s_inode[--dev->s_ninode];
        if(ino < 2 || ino >=(dev->s_isize-2)*8)
            goto corrupt;
        /*
         * The free list is a cache written by whoever last had the
         * filesystem, and it can be wrong: an entry may name an inode
         * that is now a live file. Handing that out corrupts the file
         * that owns it, so check the inode on disk before believing
         * the list. The block is almost always in the buffer cache,
         * and being wrong here is expensive enough to be worth a read.
         */
        if (breadi(devno, ino, &check) == 0 &&
            (check.i_mode || check.i_nlink)) {
            kprintf("i_alloc: %u in free list but in use (mode %x nlink %u)\n",
                    (uint16_t)ino,
                    (uint16_t)check.i_mode, (uint16_t)check.i_nlink);
            /* It was never free, so the count that said it was is one
               too high. Repair it rather than carrying the error. */
            if (dev->s_tinode)
                --dev->s_tinode;
            continue;		/* try the next entry */
        }
        --dev->s_tinode;
        ret = ino;
        goto done_unlock;
    }
    /* We must scan the inodes, and fill up the table */

    sync();           /* Make on-disk inodes consistent */
    k = 0;
    for(blk = 2; blk < dev->s_isize; blk++) {
        buf = bread(devno, blk, 0);
        if (buf == NULL)
            goto corrupt;
        for(j=0; j < INO_PER_BLOCK; j++) {
            /* Optimisation: add offsetof and use that to reduce blkptr range */
            di = blkptr(buf, sizeof(struct dinode) * j, sizeof(struct dinode));
            if(!(di->i_mode || di->i_nlink)) {
                register inoptr itp;
                ino = INO_PER_BLOCK * (blk - 2) + j;
                /*
                 * An inode that is in core is not free, whatever the
                 * disk says. The in-core copy is the newer one and the
                 * disk has simply not caught up: _pipe() sets i_mode
                 * to F_PIPE in core and never writes it, newfile() has
                 * a window between i_open() and wr_inode(), and sync()
                 * above only flushes entries with c_refs > 0.
                 *
                 * Without this the scan puts live objects on the free
                 * list and i_alloc hands them out a second time. That
                 * is the inode double free: the pipes a shell creates
                 * for every command are the common case, which is why
                 * it took a compiler driver to provoke it.
                 *
                 * V7's ialloc() has exactly this check and it is the
                 * only thing protecting it, since V7 does not write a
                 * newly allocated inode to disk either.
                 */
                for (itp = i_tab; itp < i_tab + ITABSIZE; itp++)
                    if (itp->c_dev == devno && itp->c_num == ino)
                        goto skip;
                dev->s_inode[k++] = ino;
            }
skip:
            if(k == FILESYS_TABSIZE) {
                brelse(buf);
                goto done;
            }
        }
        brelse(buf);
    }

done:
    /* Each (block, slot) is visited exactly once, so the scan cannot
       produce an internal duplicate; the in-core check above is what
       stops it duplicating something that is already live. */
    if(!k) {
        if(dev->s_tinode)
            goto corrupt;
        udata.u_error = ENOSPC;
        ret = 0;
        goto done_unlock;
    }
    dev->s_ninode = k;
    goto tryagain;

corrupt:
    kputs("i_alloc: corrupt superblock\n");
    dev->s_mounted = 1;
    udata.u_error = ENOSPC;
    ret = 0;

done_unlock:
    mnt->m_ilock = 0;
    wakeup(&mnt->m_ilock);
    return ret;
}


/* I_free is given a device and inode number, and frees the inode.
 * It is assumed that there are no references to the inode in the
 * inode table or in the filesystem.
 *
 * This will need to happen under the superblock lock once we do sleeping
 */

void i_free(uint16_t devno, uint16_t ino)
{
    register fsptr dev;
    struct mount *mnt;
    uint16_t i;		/* DEBUG */

    if(baddev(dev = getdev(devno)))
        return;

    if(ino < 2 || ino >=(dev->s_isize-2)*8)
        panic(PANIC_IFREE_BADI);

    /*
     * If a rebuild is in progress, keep away from the list entirely -
     * this is what V7's ifree() does. The rebuild refills s_inode[]
     * from index 0 and assigns s_ninode at the end, so anything pushed
     * while it runs is simply overwritten, and the inode is lost from
     * the cache while still counted as allocated. The inode is already
     * clear on disk, so the next scan will find it; the cache is only
     * a cache.
     */
    mnt = fs_tab_get(devno);
    if (mnt && mnt->m_ilock) {
        ++dev->s_tinode;
        return;
    }

    /*
     * An inode may appear on the free list at most once. Nothing here
     * ever checked, and i_deref does reach its freeing branch twice
     * for the same inode - so the list ended up holding it twice, and
     * the second allocation handed out a live file. That is the fault
     * behind "i_open: bad disk inode", which the create path then
     * reports as the entirely misleading ENFILE.
     *
     * Enforce the invariant where it is broken. Freeing something
     * already free is a no-op, not a reason to add it again. The list
     * is at most FILESYS_TABSIZE entries, so the scan is cheap next to
     * the block I/O around it.
     *
     * This is a guard, not a cure: the double deref that provokes it
     * is still there and still worth finding. It is safe to fix here
     * because the invariant is the thing that matters - an allocator
     * must never hand out the same object twice.
     */
    for (i = 0; i < dev->s_ninode; i++) {
        if (dev->s_inode[i] == ino) {
            kprintf("i_free: %u freed twice\n", (uint16_t)ino);
            return;
        }
    }

    ++dev->s_tinode;
    if(dev->s_ninode < FILESYS_TABSIZE) {
        dev->s_inode[dev->s_ninode++] = ino;
    }
}


/* Blk_alloc is given a device number, and allocates an unused block
 * from it. A returned block number of zero means no more blocks.
 *
 * This will need to happen under the superblock lock once we do sleeping
 */

blkno_t blk_alloc(uint16_t devno)
{
    register fsptr dev;
    register struct blkbuf *buf;
    blkno_t newno;

    if(baddev(dev = getdev(devno)))
        goto corrupt2;

    if(dev->s_nfree <= 0 || dev->s_nfree > FILESYS_TABSIZE)
        goto corrupt;

    newno = dev->s_free[--dev->s_nfree];
    if(!newno)
    {
        if(dev->s_tfree != 0)
            goto corrupt;
        udata.u_error = ENOSPC;
        ++dev->s_nfree;
        return(0);
    }

    /* See if we must refill the s_free array */

    if(!dev->s_nfree)
    {
        buf = bread(devno, newno, 0);
        if (buf == NULL)
            goto corrupt;
        /*
         * sizeof(dev->s_nfree), NOT sizeof(int).
         *
         * s_nfree is a uint16_t. On the 8 and 16 bit targets Fuzix
         * grew up on, int is two bytes and the two agree; here int is
         * four, so this copied two bytes too many - and what sits
         * immediately after s_free[] in struct filesys is s_ninode.
         *
         * blk_free() wrote the same over-long field, so the two spare
         * bytes on disk hold whatever s_ninode was when that free list
         * block was written. Refilling the block list therefore
         * restored a stale inode free list count, and every already
         * popped slot in s_inode[] above the real count came back to
         * life - naming inodes that were by then live files. That is
         * the inode double free, and it is why it only appeared under
         * heavy block allocation and never showed an i_free() call.
         *
         * Offsets 0..101 are unchanged, so existing filesystems and the
         * host side tools still interoperate; only the two bytes past
         * the block list are no longer touched.
         */
        blktok(&dev->s_nfree, buf, 0,
            sizeof(dev->s_nfree) + FILESYS_TABSIZE * sizeof(blkno_t));
        /* This assumes no padding: this is an UZI era assumption */
        brelse(buf);
    }

    validblk(devno, newno);

    if(!dev->s_tfree)
        goto corrupt;
    --dev->s_tfree;

   /*
    * FIXME: When we implement the rest of the bigger block size fs support
    * this routine is responsible for zeroing the entire extent not just the
    * BLKSIZE byte block
    */
    /* Zero out the new block */
    buf = bread(devno, newno, 2);
    if (buf == NULL)
        goto corrupt;
    blkzero(buf);
    bawrite(buf);
    return newno;

corrupt:
    kputs("blk_alloc: corrupt\n");
    dev->s_mounted = 1;
corrupt2:
    udata.u_error = ENOSPC;
    return 0;
}


/* Blk_free is given a device number and a block number,
 * and frees the block.
 *
 * This will need to happen under the superblock lock once we do sleeping
 */

void blk_free(uint16_t devno, blkno_t blk)
{
    fsptr dev;
    struct blkbuf *buf;

    if(!blk)
        return;

    if(baddev(dev = getdev(devno)))
        return;

    validblk(devno, blk);

    if(dev->s_nfree == FILESYS_TABSIZE) {
        buf = bread(devno, blk, 1);
        if (buf) {
            /* nfree must directly preceed the blocks and without padding. That's
               the assumption UZI always had.
               sizeof(dev->s_nfree), NOT sizeof(int): s_nfree is a
               uint16_t and int is four bytes on a 32bit target, so
               "sizeof(int)" copied two bytes too many. See the matching
               read in blk_alloc() for what that cost. */
            blkfromk(&dev->s_nfree, buf, 0,
                sizeof(dev->s_nfree) + FILESYS_TABSIZE * sizeof(blkno_t));
            bawrite(buf);
            dev->s_nfree = 0;
        } else
            dev->s_mounted = 1;
    }

    ++dev->s_tfree;
    dev->s_free[(dev->s_nfree)++] = blk;
}


/* Oft_alloc and oft_deref allocate and dereference(and possibly free)
 * entries in the open file table.
 */

int_fast8_t oft_alloc(void)
{
    register uint_fast8_t j;

    for(j=0; j < OFTSIZE ; ++j) {
        if(of_tab[j].o_refs == 0) {
            of_tab[j].o_refs = 1;
            of_tab[j].o_inode = NULLINODE;
            return j;
        }
    }
    udata.u_error = ENFILE;
    return -1;
}

/*
 *	To minimise storage we don't track exclusive locks explicitly. We know
 *	that if we are dropping an exclusive lock then we must be the owner,
 *	and if we are dropping a lock that is not exclusive we must own one of
 *	the non exclusive locks.
 */
void deflock(register struct oft *ofptr)
{
    register inoptr i = ofptr->o_inode;
    register uint_fast8_t c = i->c_flags & CFLOCK;

    if (ofptr->o_access & O_FLOCK) {
        if (c == CFLEX)
            c = 0;
        else
            c--;
        i->c_flags = (i->c_flags & ~CFLOCK) | c;
        wakeup(&i->c_flags);
    }
}

/*
 *	Drop a reference in the open file table. If this is the last reference
 *	from a user file table then drop any file locks, dereference the inode
 *	and mark empty
 */
void oft_deref(uint_fast8_t of)
{
    register struct oft *ofptr;

    ofptr = of_tab + of;
    if(!(--ofptr->o_refs) && ofptr->o_inode) {
        deflock(ofptr);
        i_deref(ofptr->o_inode);
        ofptr->o_inode = NULLINODE;
    }
}


/* Uf_alloc finds an unused slot in the user file table.*/

int_fast8_t uf_alloc_n(uint_fast8_t base)
{
    register uint_fast8_t j;

    for(j=base; j < UFTSIZE ; ++j) {
        if(udata.u_files[j] == NO_FILE) {
            return j;
        }
    }
    udata.u_error = EMFILE;
    return -1;
}


int_fast8_t uf_alloc(void)
{
    return uf_alloc_n(0);
}


/* I_deref decreases the reference count of an inode, and frees it from
 * the table if there are no more references to it.  If it also has no
 * links, the inode itself and its blocks(if not a device) is freed.
 */

/*
 *	Compare this with V7's iput(), which is where it comes from. Four
 *	things had drifted, and together they are the inode double free:
 *
 *	  * V7 does all of the destruction with i_count still 1 and only
 *	    decrements at the very end. This decremented first, so from
 *	    f_trunc() - which does block I/O - until the end of the
 *	    function the table entry read as unreferenced and i_open()
 *	    would hand it out as a free slot to whoever ran next.
 *	  * V7 frees on i_nlink <= 0 alone. This also required CDIRTY, so
 *	    an inode whose last link and last reference went away without
 *	    anything having dirtied it was never returned to the free list
 *	    and never had its mode cleared on disk - it stayed allocated
 *	    with nothing pointing at it.
 *	  * V7 finishes with "ip->i_flag = 0; ip->i_number = 0;" - the
 *	    cache entry is dead and can never be found again. This left
 *	    c_num set, so a later i_open could still match the entry by
 *	    number and validate against a copy of an inode that no longer
 *	    described anything. That is where the in-core inode and the
 *	    disk came to disagree.
 *	  * V7 holds ILOCK across itrunc. i_lock() is a no-op in this
 *	    configuration (kernel.h), so there is no lock to hold; keeping
 *	    the reference is what stands in for it.
 */

void i_deref(register inoptr ino)
{
    uint_fast8_t mode = getmode(ino);

    magic(ino);

    if(!ino->c_refs)
        panic(PANIC_INODE_FREED);

    if (mode == MODE_R(F_PIPE))
        wakeup((uint8_t *)ino);

    if (ino->c_refs == 1) {
        /* Last reference. Do not drop it until the object is gone. */
        if (!ino->c_node.i_nlink && !(ino->c_flags & CRDONLY)) {
            /* No links and no other users: the file ceases to exist */
            if (mode == MODE_R(F_REG) || mode == MODE_R(F_DIR) ||
                mode == MODE_R(F_PIPE))
                f_trunc(ino);
            ino->c_node.i_mode = 0;
            /* Zeroing the mode has to reach the disk, or i_alloc's
               scan will not see the inode as free either */
            ino->c_flags |= CDIRTY;
            wr_inode(ino);
            /*
             * Only now is it safe to list it. Freeing before the
             * cleared inode reached the disk left a window in which
             * the list said "free" and the disk still described a live
             * file, which is what i_alloc's validation reads.
             */
            i_free(ino->c_dev, ino->c_num);
        }

        /* If the inode was modified, we must write it to disk. */
        if (ino->c_flags & CDIRTY)
            wr_inode(ino);

        if (!ino->c_node.i_nlink) {
            /* Dead. Make the entry unfindable, as V7 does. */
            ino->c_num = 0;
            ino->c_flags = 0;
        }
    }
    ino->c_refs--;
}

void corrupt_fs(uint16_t devno)
{
    struct mount *mnt = fs_tab_get(devno);
    mnt->m_fs.s_mounted = 1;
    kputs("filesystem corrupt.\n");
}
/* Wr_inode writes out the given inode in the inode table out to disk,
 * and resets its dirty bit.
 */

void wr_inode(register inoptr ino)
{
/*    struct blkbuf *buf;
    blkno_t blkno;
*/
    magic(ino);
#ifdef CONFIG_FS_TRIPWIRE_DEEP
    ino_blocks_check(ino->c_dev, ino->c_num, &ino->c_node, "write");
#endif

    if (bwritei(ino))
        corrupt_fs(ino->c_dev);
    else
        ino->c_flags &= ~CDIRTY;
}


/* isdevice(ino) returns true if ino points to a device */
bool isdevice(inoptr ino)
{
    return !!(ino->c_node.i_mode & F_CDEV);
}


/* This returns the device number of an inode representing a device */
uint16_t devnum(inoptr ino)
{
    return (uint16_t)ino->c_node.i_addr[0];
}


/*
 *	f_trunc_blocks frees all the blocks associated with the file, if it
 *	is a disk file. The blocks are freed in reverse order. This is
 *	very important so that they end up on the freelist in the
 *	order we want to allocate them.
 */
int f_trunc_blocks(register inoptr ino, uint16_t nblock)
{
    register uint16_t dev;
    register int_fast8_t j;
    uint16_t map1 = 0;
    uint16_t map2 = 0;

    if (ino->c_flags & CRDONLY) {
        udata.u_error = EROFS;
        return -1;
    }

    /* Block offsets are
        0-17 direct
        18 256 blocks (18-273)
        19 256 * 256 blocks (274-65810)

        (We only allow 65535 block offset in order to keep a lot of stuff
         uint16_t - FIXME to fix u writei())

        We don't support triple indirect blocks.

        When we are called nblock is the number of blocks that will
        remain in the file when we truncate it

        We set map1 to the number of blocks we must purge for single
        indirect. We set map2 for the number of blocks we must purge
        of double indirect.

        freeblk frees full subblocks above the block passed, and then frees
        blocks >> 8 on the last iteration to partially clear the last set
    */

    if (nblock > 17 && nblock < 274)
        map1 = (nblock - 18) << 8;
    else if (nblock > 273)
        map2 = nblock - 273;
    dev = ino->c_dev;

    /* FIXME: ideally zero the indirect pointers before we write the
       free lists */

    /*
     * First deallocate the double indirect blocks.
     *
     * freeblk() frees the block it is given, root included, so when
     * nothing is retained (map2 == 0, which is EVERY f_trunc() - it
     * always passes nblock 0) i_addr[19] must be cleared. The test used
     * to be "if (map2)", the exact opposite, so a full truncation left
     * i_addr[19] pointing at a block it had just put on the free list.
     *
     * That is the filesystem corruption chased on 2026-08-02. Rewriting
     * the file - SAVE IMAGE opening with O_TRUNC and writing 451 blocks
     * straight back - reaches logical block 274, bmap finds i_addr[19]
     * still set and reuses the freed block as the double indirect root,
     * by then reallocated as somebody's data or as a free list chain
     * block. Hence block numbers like 50 and 65442 appearing in a file's
     * block list, blk_free() panicking on them, and a chain block
     * coming back zeroed because blk_alloc had handed it out twice.
     *
     * It only bites files longer than 273 blocks, which is why it
     * survived on machines with small discs.
     *
     * Note the single indirect line below already had the right sense -
     * the two tests were opposites, and its "???? should this just be if
     * map1" says the doubt was known.
     */
    freeblk(dev, ino->c_node.i_addr[19], 2, map2);
    if (map2 == 0)
        ino->c_node.i_addr[19] = 0;

    /* Also deallocate the indirect blocks. With nblock > 273 the whole
       single indirect range is retained, so it must not be touched at
       all - freeblk(.., map1 == 0) would free the lot. */
    if (nblock < 274) {
        freeblk(dev, ino->c_node.i_addr[18], 1, map1);
        if (map1 == 0)
            ino->c_node.i_addr[18] = 0;
    }

    /* Finally, free the direct blocks */
    /* FIXME: use pointers for efficiency ? */
    /* At this point nblock is definitely < 0x8000 so forcing a signed
       compare does what we want */
    for(j = 17; j >= (int)nblock; --j) {
        freeblk(dev, ino->c_node.i_addr[j], 0, 0);
        ino->c_node.i_addr[j] = 0;
    }

    ino->c_flags |= CDIRTY;
    return 0;
}


/* Truncate a file back to nothing using f_trunc_blocks and then write
   the inode size as 0 */
int f_trunc(regptr inoptr ino)
{
    /* Is it worth checking size already 0 ? */
    if (f_trunc_blocks(ino, 0))
        return -1;
     ino->c_node.i_size = 0;
     return 0;
}

/* Companion function to f_trunc().

   This is the one case where we can't hide the difference between an internal
   and external buffer cache cleanly. The external one has a somewhat higher
   overhead (we could mitigate it by batching perhaps) and also size.

   This is annoying and it would be nice one day to find a clean solution */

#ifdef CONFIG_BLKBUF_EXTERNAL
void freeblk(uint16_t dev, blkno_t blk, uint_fast8_t level, uint16_t nblock)
{
    struct blkbuf *buf;
    regptr blkno_t *bn;
    int16_t j;
    int_fast8_t nblock1 = nblock >> 8;

    if(!blk)
        return;

    if(level){
        buf = bread(dev, blk, 0);
        if (buf == NULL) {
            corrupt_fs(dev);
            return;
        }
        for(j = BLKSIZE / 2 - 1; j >= nblock1; --j) {
            uint8_t b = 0;
            if (j == nblock1)
                b = nblock & 0xFF;
            blktok(&bn, buf, j * sizeof(blkno_t), sizeof(blkno_t));
            freeblk(dev, bn[j], level - 1, b);
        }
        brelse(buf);
    }
#ifdef CONFIG_TRIM
    d_ioctl(dev, HDIO_TRIM, (void*)&blk);
#endif
    blk_free(dev, blk);
}

#else

void freeblk(uint16_t dev, blkno_t blk, uint_fast8_t level, uint16_t nblock)
{
    struct blkbuf *buf;
    regptr blkno_t *bn;
    int16_t j;
    int_fast8_t nblock1 = nblock >> 8;

    if(!blk)
        return;

    if(level){
        buf = bread(dev, blk, 0);
        if (buf == NULL) {
            corrupt_fs(dev);
            return;
        }
        bn = blkptr(buf, 0, BLKSIZE);
        for(j = BLKSIZE / 2 - 1; j >= 0; --j) {
            /* When we hit nblock1 we are doing the final partial clear, so
               only tell the child freeblk to do a partial clear */
            uint_fast8_t b = 0;
            if (j == nblock1)
                b = nblock & 0xFF;
            freeblk(dev, bn[j], level-1, b);
        }
        brelse(buf);
    }
#ifdef CONFIG_TRIM
    d_ioctl(dev, HDIO_TRIM, (void*)&blk);
#endif
    blk_free(dev, blk);
}
#endif

/* Validblk panics if the given block number is not a valid
 *  data block for the given device.
 */
#ifdef CONFIG_FS_TRIPWIRE_DEEP
/*
 *	Inode block-list tripwire.
 *
 *	validblk(blk_free) caught the corruption naming block 50 - a block
 *	inside the INODE area, reached from a file's block list while
 *	truncating it. That says an inode (or one of its indirect blocks)
 *	holds a bad pointer, but not WHERE it went wrong, and those are two
 *	entirely different bugs:
 *
 *	  - bad as it comes off the disk  -> an earlier WRITE put garbage
 *	    there, so the fault is in the write path;
 *	  - good on disk, bad later	  -> something scribbled on the
 *	    in-core inode table, so the fault is memory corruption.
 *
 *	So check the list at both ends: as i_open reads it, and as wr_inode
 *	is about to write it back. Whichever fires first decides which of
 *	those two searches to start. Only regular files and directories -
 *	a device inode keeps its device number in i_addr[0].
 */
void ino_blocks_check(uint16_t dev, uint16_t inum, const dinode *d,
                      const char *where)
{
    register struct mount *mnt;
    uint16_t mode = d->i_mode & F_MASK;
    int i;

    if (mode != F_REG && mode != F_DIR)
        return;
    mnt = fs_tab_get(dev);
    if (mnt == NULL || mnt->m_fs.s_mounted == 0)
        return;

    for (i = 0; i < 20; i++) {
        blkno_t b = d->i_addr[i];
        if (b == 0)
            continue;
        if (b < mnt->m_fs.s_isize || b >= mnt->m_fs.s_fsize) {
            kprintf("\ninode tripwire(%s): dev %u inode %u i_addr[%d] = %u"
                    " outside %u..%u (mode %x size %u)\n",
                    where, dev, inum, i, (unsigned)b,
                    (unsigned)mnt->m_fs.s_isize,
                    (unsigned)mnt->m_fs.s_fsize,
                    (unsigned)d->i_mode, (unsigned)d->i_size);
            panic("inoblk");
        }
    }
}

void validblk_at(uint16_t dev, register blkno_t num, const char *who)
#else
void validblk(uint16_t dev, register blkno_t num)
#endif
{
    register struct mount *mnt;

    mnt = fs_tab_get(dev);

    if(mnt == NULL || mnt->m_fs.s_mounted == 0) {
        panic(PANIC_VALIDBLK_NM);
        return;
    }

    if(num < mnt->m_fs.s_isize || num >= mnt->m_fs.s_fsize) {
#ifdef CONFIG_FS_TRIPWIRE_DEEP
        /* Which caller, and what the number was, separates the two
           ways this happens: blk_alloc means the free list handed out
           a bad block - and since the superblock's copy is checked at
           every operation, a bad one there means the refill read from
           disk brought in garbage.  blk_free means an INODE holds a
           bad block pointer, which is a different fault entirely. */
        kprintf("\nvalidblk(%s): dev %u blk %u outside %u..%u"
                " (nfree %u, tfree %u)\n",
                who, mnt->m_dev, (unsigned)num,
                (unsigned)mnt->m_fs.s_isize, (unsigned)mnt->m_fs.s_fsize,
                (unsigned)mnt->m_fs.s_nfree, (unsigned)mnt->m_fs.s_tfree);
#endif
        panic(PANIC_VALIDBLK_INV);
    }
}


/* This returns the inode pointer associated with a user's file
 * descriptor, checking for valid data structures.
 */
inoptr getinode(uint_fast8_t uindex)
{
    register uint_fast8_t oftindex;
    register inoptr inoindex;

    if(uindex >= UFTSIZE || udata.u_files[uindex] == NO_FILE) {
        udata.u_error = EBADF;
        return NULLINODE;
    }

    oftindex = udata.u_files[uindex];

    if(oftindex >= OFTSIZE || oftindex == NO_FILE)
        panic(PANIC_GETINO_BADT);

    if((inoindex = of_tab[oftindex].o_inode) < i_tab || inoindex >= i_tab+ITABSIZE)
        panic(PANIC_GETINO_OFT);

    magic(inoindex);
    return(inoindex);
}


/* Super returns true if we are the superuser */
bool super(void)
{
    return(udata.u_euid == 0);
}

/* Similar but this helper sets the error code */
bool esuper(void)
{
    if (udata.u_euid) {
        udata.u_error = EPERM;
        return -1;
    }
    return 0;
}

/* Getperm looks at the given inode and the effective user/group ids,
 * and returns the effective permissions in the low-order 3 bits.
 */
uint8_t getperm(register inoptr ino)
{
    int mode;

    if(super())
        return(07);

    mode = ino->c_node.i_mode;
    if(ino->c_node.i_uid == udata.u_euid)
        mode >>= 6;
    else if(ino->c_node.i_gid == udata.u_egid)
        mode >>= 3;
#ifdef CONFIG_LEVEL_2
    /* BSD process groups */
    else if (in_group(ino->c_node.i_gid))
        mode >>= 3;
#endif

    return(mode & 07);
}


/* This sets the times of the given inode, according to the flags. */
void setftime(register inoptr ino, register uint_fast8_t flag)
{
    if (ino->c_flags & CRDONLY)
        return;

    /* If only ATIME is due an update then skip it for a noatime fs */
    if (flag == A_TIME && fs_tab[ino->c_super].m_flags & MS_NOATIME)
        return;

    ino->c_flags |= CDIRTY;

    if(flag & A_TIME)
        rdtime32(&(ino->c_node.i_atime));
    if(flag & M_TIME)
        rdtime32(&(ino->c_node.i_mtime));
    if(flag & C_TIME)
        rdtime32(&(ino->c_node.i_ctime));
}

uint8_t getmode(inoptr ino)
{
    /* Shifting by 9 (past permissions) might be more logical but
       8 happens to be cheap */
    return (ino->c_node.i_mode & F_MASK) >> 8;
}

static struct mount *newfstab(void)
{
    register struct mount *m = fs_tab;
    register uint_fast8_t i;
    for (i = 0; i < NMOUNTS; i++) {
        if (m->m_dev == NO_DEVICE)
            return m;
        m++;
    }
    return NULL;
}

struct mount *fs_tab_get(uint16_t dev)
{
    register struct mount *m = fs_tab;
    register uint_fast8_t i;
    for (i = 0; i < NMOUNTS; i++) {
        if (m->m_dev == dev)
            return m;
        m++;
    }
    return NULL;
}

/* Fmount places the given device in the mount table with mount point info. */
struct mount *fmount(uint16_t dev, register inoptr ino, uint16_t flags)
{
    register struct mount *m;
    register struct filesys *fp;
    register bufptr buf;

    if(d_open(dev, 0) != 0)
        return NULL;    /* Bad device */

    m = newfstab();
    if (m == NULL) {
        udata.u_error = EMFILE;
        return NULL;	/* Table is full */
    }

    fp = &m->m_fs;

    /* Get the buffer with the superblock (block 1) */
    buf = bread(dev, 1, 0);
    if (buf == NULL)
        return NULL;
    blktok(fp, buf, 0, sizeof(struct filesys));
    brelse(buf);

#ifdef DEBUG
    kprintf("fp->s_mounted=0x%x, fp->s_isize=0x%x, fp->s_fsize=0x%x\n",
    fp->s_mounted, fp->s_isize, fp->s_fsize);
#endif

    /* See if there really is a filesystem on the device */
    if(fp->s_mounted != SMOUNTED  ||  fp->s_isize >= fp->s_fsize ||
        fp->s_shift > FS_MAX_SHIFT) {
        udata.u_error = EINVAL;
        return NULL;
    }

    /*
     * The free inode list in the superblock is only a cache of inodes
     * that were free when it was written. Do not trust it: discard it
     * and let i_alloc rebuild it from the disk on first use.
     *
     * Anything that writes the filesystem while we are not looking
     * invalidates it, and two things routinely do. ucp builds the
     * image on the host, and fsck runs from rc against the root we
     * have *already* mounted - it sets s_ninode to 0 on disk for
     * exactly this reason, but we read the superblock before it ran
     * and would otherwise keep using the stale copy, and write it back
     * over fsck's correction on the next sync.
     *
     * The symptom when this goes wrong is i_alloc handing out an inode
     * that is a live file, which surfaces as "i_open: bad disk inode"
     * and then, misleadingly, ENFILE - "File table overflow" - from
     * the create path, on a filesystem fsck calls clean.
     *
     * The cost is one inode scan after each mount.
     */
    fp->s_ninode = 0;

    if (fp->s_fmod == FMOD_DIRTY) {
        kputs("warning: mounting dirty file system, forcing r/o.\n");
        flags |= MS_RDONLY;
    }
    if (!(flags & MS_RDONLY))
        /* Dirty - and will write dirty mark back to media */
        fp->s_fmod = FMOD_DIRTY;
    else	/* Clean in memory, don't write it back to media */
        fp->s_fmod = FMOD_CLEAN;
    m->m_mntpt = ino;
    if(ino)
        ++ino->c_refs;
    m->m_flags = flags;
    /* Makes our entry findable */
    m->m_dev = dev;

    /* Mark the filesystem dirty on disk */
    sync();

    return m;
}


void magic(inoptr ino)
{
    if(ino->c_magic != CMAGIC)
        panic(PANIC_CORRUPTI);
}

/* This is a helper function used by _unlink and _rename; it doesn't really
 * belong here, but needs to be in common code as it's used from two different
 * syscall banks.
 *
 * FIXME: this could be more efficient if we remembered which directory offset
 * we found the node at lookup time
 */
arg_t unlinki(inoptr ino, inoptr pino, uint8_t *fname)
{
	if (getmode(ino) == MODE_R(F_DIR)) {
		udata.u_error = EISDIR;
		return -1;
	}

	/* Remove the directory entry (ch_link checks perms) */
	if (!ch_link(pino, fname, (uint8_t *)"", NULLINODE))
		return -1;

	/* Decrease the link count of the inode */
	if (!(ino->c_node.i_nlink--)) {
		ino->c_node.i_nlink += 2;
		kprintf("_unlink: bad nlink\n");
	}
	setftime(ino, C_TIME);
	return (0);
}

