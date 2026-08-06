#define __UZIFS_DOT_H__

#include <stdint.h>
#include <stddef.h>

#ifndef BLKSIZE
#define BLKSIZE 512	/* FS32 default; the 400-block tools define theirs */
#endif

#define FILENAME_LEN 30
#define ROOTDEV 0
#define ROOTINODE 1

#if (BLKSIZE == 400)
#define SMOUNTED  12743   /* Magic number to specify mounted filesystem */
#define SMOUNTED_WRONGENDIAN 50993   /* byteflipped */
#define DIR_LEN	40
#define DIR_PAD 8
#define IPERBLK	6
#elif (BLKSIZE == 512)
/* FS32 (see Kernel/platform/platform-rpipico/FS32-FORMAT.md): 512-byte
 * blocks, 32-bit block numbers, 256-byte inodes.  The classic 64-byte
 * inode format has magic 12742; an FS32 implementation must refuse it
 * with a message naming the classic format, not a bare error. */
#define SMOUNTED 0xFB32   /* Magic number to specify mounted filesystem */
#define SMOUNTED_WRONGENDIAN 0x32FB   /* byteflipped */
#define SMOUNTED_CLASSIC 12742	/* the pre-FS32 format, refused by name */
#define FS32_VERSION 1
#define DIR_LEN	32
#define IPERBLK 2
/* Pointer geometry.  All implementations use these names; the format
 * document is the authority on the values. */
#define DIRECT_BLOCKS	40
#define IND_PER_BLOCK	128	/* BLKSIZE / sizeof(uint32_t) */
#define ONE_IND_END	(DIRECT_BLOCKS + IND_PER_BLOCK)		   /* 168 */
#define TWO_IND_END	(ONE_IND_END + IND_PER_BLOCK*IND_PER_BLOCK) /* 16552 */
#define THREE_IND_END	(TWO_IND_END + 128UL*128*128)	 /* 2113704 blocks */
#endif
#define CMAGIC   24721
#define UFTSIZE 10
#define NSIGS 16
#define NDEVS 1
#define NBUFS 10
#define OFTSIZE 15
#define ITABSIZE 20
#define _NSIG NSIGS
#define NULLINODE ((inoptr)NULL)
#define NULLBLK ((blkno_t)-1)
#define NULLINOPTR ((inoptr*)NULL)

/* Flags for setftime() */
#define A_TIME 1
#define M_TIME 2
#define C_TIME 4


#define FO_RDONLY        0
#define FO_WRONLY        1
#define FO_RDWR          2

#define ifnot(x) if(!(x))

extern int dev_fd;
extern int dev_offset;
void panic(char *s);

typedef struct s_queue {
    char *q_base;    /* Pointer to data */
    char *q_head;    /* Pointer to addr of next char to read. */
    char *q_tail;    /* Pointer to where next char to insert goes. */
    int   q_size;    /* Max size of queue */
    int   q_count;   /* How many characters presently in queue */
    int   q_wakeup;  /* Threshold for waking up processes waiting on queue */
} queue_t;

struct  uzi_stat    /* Really only used by users */
{
    int16_t   st_dev;
    uint16_t  st_ino;
    uint16_t  st_mode;
    uint16_t  st_nlink;
    uint16_t  st_uid;
    uint16_t  st_gid;
    uint16_t  st_rdev;
    uint32_t  st_size;
    uint32_t  fst_atime;
    uint32_t  fst_mtime;
    uint32_t  fst_ctime;
};

typedef struct direct {
        uint16_t   d_ino;
        char     d_name[30];
#ifdef DIR_PAD
        uint8_t	d_pad[DIR_PAD];
#endif
} direct;

#if (BLKSIZE == 400)
typedef uint16_t blkno_t;    /* classic 400-block format: 16-bit */
#else
typedef uint32_t blkno_t;    /* FS32: 32-bit block numbers, fs to 2TB */
#endif

typedef struct blkbuf {
    uint8_t     bf_data[512];    /* This MUST be first ! */
    char        bf_dev;
    blkno_t     bf_blk;
    char        bf_dirty;
    char        bf_busy;
    uint32_t    bf_time;         /* LRU time stamp; 32-bit, see bufclock */
} blkbuf;

typedef blkbuf *bufptr;

#if (BLKSIZE == 400)
/* Classic 64-byte inode, kept for the 400-block tools */
typedef struct dinode {
    uint16_t i_mode;
    uint16_t i_nlink;
    uint16_t i_uid;
    uint16_t i_gid;
    uint32_t i_size;
    uint32_t i_atime;
    uint32_t i_mtime;
    uint32_t i_ctime;
    blkno_t  i_addr[20];
} dinode;               /* Exactly 64 bytes long! */
#else
/* FS32 on-disk inode: exactly 256 bytes, two per block, never
 * straddling a block.  i_addr is 40 direct + single + double + triple
 * indirect (DIRECT_BLOCKS et al above).  Every field is naturally
 * aligned so the on-disk bytes are the struct bytes on x86-64 and ARM
 * EABI alike. */
typedef struct dinode {
    uint16_t i_mode;
    uint16_t i_nlink;
    uint16_t i_uid;
    uint16_t i_gid;
    uint32_t i_size;
    uint32_t i_atime;
    uint32_t i_mtime;
    uint32_t i_ctime;
    uint8_t  i_timeh[3];	/* bits 32-39 of a/m/ctime; 0 in v1 */
    uint8_t  i_pad;		/* must be 0 */
    blkno_t  i_addr[DIRECT_BLOCKS + 3];
    uint8_t  i_reserved[56];	/* written 0, ignored on read */
} dinode;               /* Exactly 256 bytes long! */
#endif

/* Bit masks for i_mode and st_mode */

#define OTH_EX  0001
#define OTH_WR  0002
#define OTH_RD  0004
#define GRP_EX  0010
#define GRP_WR  0020
#define GRP_RD  0040
#define OWN_EX  0100
#define OWN_WR  0200
#define OWN_RD  0400

#define SAV_TXT 01000
#define SET_GID 02000
#define SET_UID 04000

#define MODE_MASK 07777

#define F_REG   0100000
#define F_DIR   040000
#define F_PIPE  010000
#define F_BDEV  060000
#define F_CDEV  020000

#define F_MASK  0170000

typedef struct cinode {
    int        c_magic;           /* Used to check for corruption. */
    int        c_dev;             /* Inode's device */
    unsigned   c_num;             /* Inode # */
    dinode     c_node;
    char       c_refs;            /* In-core reference count */
    char       c_dirty;           /* Modified flag. */
} cinode, *inoptr;

#if (BLKSIZE == 400)
/* Classic superblock, kept for the 400-block tools */
typedef struct filesys {
    uint16_t    s_mounted;
    uint16_t    s_isize;
    uint16_t    s_fsize;
    int16_t     s_nfree;
    blkno_t     s_free[50];
    int16_t     s_ninode;
    uint16_t    s_inode[50];
    uint8_t     s_fmod;
    uint8_t	s_timeh;	/* top bits of time */
    uint32_t    s_time;
    blkno_t     s_tfree;
    uint16_t    s_tinode;
    uint8_t	s_shift;
    inoptr      s_mntpt;
} filesys, *fsptr;
#else
/* FS32 superblock: the first 332 bytes of block 1, all fields
 * naturally aligned, offsets fixed by FS32-FORMAT.md.  s_mntpt is
 * in-core only and sits beyond the on-disk region. */
typedef struct filesys {
    uint16_t    s_mounted;	/* 0: magic 0xFB32 */
    uint16_t    s_version;	/* 2: FS32_VERSION */
    uint32_t    s_isize;	/* 4: first data block */
    uint32_t    s_fsize;	/* 8: total blocks */
    blkno_t     s_tfree;	/* 12: total free blocks */
    int16_t     s_nfree;	/* 16: valid entries in s_free */
    uint16_t    s_tinode;	/* 18: total free inodes */
    blkno_t     s_free[50];	/* 20 */
    int16_t     s_ninode;	/* 220 */
    uint16_t    s_inode[50];	/* 222 */
    uint8_t     s_fmod;		/* 322 */
    uint8_t	s_timeh;	/* 323: top bits of time */
    uint32_t    s_time;		/* 324 */
    uint8_t	s_shift;	/* 328: must be 0 */
    uint8_t	s_pad0[3];	/* 329 */
    uint8_t	s_reserved[180];/* 332: written 0, ignored on read */
    inoptr      s_mntpt;	/* in-core only, never on disk */
} filesys, *fsptr;

#define FS32_SUPER_DISKSIZE 512	/* bytes of the struct that go to disk */

/* Free-list chain block: written as this explicit struct, never as the
 * classic memory-overlay from &s_nfree (FS32-FORMAT.md, "Free list"). */
typedef struct fblk {
    int16_t     f_nfree;	/* 1..50 */
    uint16_t    f_pad;		/* 0 */
    blkno_t     f_free[50];
} fblk;

#ifndef FS32_NO_ASSERTS
_Static_assert(sizeof(dinode) == 256, "FS32 dinode must be 256 bytes");
_Static_assert(sizeof(fblk) == 204, "FS32 free chain block layout");
_Static_assert(offsetof(struct filesys, s_reserved) == 332,
	       "FS32 superblock layout");
#endif
#endif /* BLKSIZE != 400 */

#define EPERM           1               /* Not owner */
#define ENOENT          2               /* No such file or directory */
#define ESRCH           3               /* No such process */
#define EINTR           4               /* Interrupted System Call */
#define EIO             5               /* I/O Error */
#define ENXIO           6               /* No such device or address */
#define E2BIG           7               /* Arg list too long */
#define ENOEXEC         8               /* Exec format error */
#define EBADF           9               /* Bad file number */
#define ECHILD          10              /* No children */
#define EAGAIN          11              /* No more processes */
#define ENOMEM          12              /* Not enough core */
#define EACCES          13              /* Permission denied */
#define EFAULT          14              /* Bad address */
#define ENOTBLK         15              /* Block device required */
#define EBUSY           16              /* Mount device busy */
#define EEXIST          17              /* File exists */
#define EXDEV           18              /* Cross-device link */
#define ENODEV          19              /* No such device */
#define ENOTDIR         20              /* Not a directory */
#define EISDIR          21              /* Is a directory */
#define EINVAL          22              /* Invalid argument */
#define ENFILE          23              /* File table overflow */
#define EMFILE          24              /* Too many open files */
#define ENOTTY          25              /* Not a typewriter */
#define ETXTBSY         26              /* Text file busy */
#define EFBIG           27              /* File too large */
#define ENOSPC          28              /* No space left on device */
#define ESPIPE          29              /* Illegal seek */
#define EROFS           30              /* Read-only file system */
#define EMLINK          31              /* Too many links */
#define EPIPE           32              /* Broken pipe */


#ifdef UCP
typedef struct u_data {
    struct p_tab *u_ptab;       /* Process table pointer */
    char        u_insys;        /* True if in kernel */
    char        u_callno;       /* sys call being executed. */
    char        *u_retloc;      /* Return location from sys call */
    int         u_retval;       /* Return value from sys call */
    int         u_error;                /* Last error number */
    char        *u_sp;          /* Used when a process is swapped. */
    char        *u_bc;          /* Place to save user's frame pointer */
    int         u_cursig;       /* Signal currently being caught */
    int         u_argn;         /* Last system call arg */
    int         u_argn1;        /* This way because args on stack backwards */
    int         u_argn2;
    int         u_argn3;        /* args n-3, n-2, n-1, and n */

    char *      u_base;         /* Source or dest for I/O */
    unsigned    u_count;        /* Amount for I/O */
    uint32_t       u_offset;       /* Place in file for I/O */
    struct blkbuf *u_buf;
    char        u_sysio;        /* True if I/O is to system data space   280*/

    int         u_gid;
    int         u_euid;
    int         u_egid;
    int         u_mask;         /* umask: file creation mode mask */
    uint32_t      u_time;         /* Start time */
    char        u_files[UFTSIZE];       /* Process file table:
                                           contains indexes into open file table. */
    inoptr      u_cwd;          /* Index into inode table of cwd. */
    unsigned    u_break;        /* Top of data space */
    inoptr      u_ino;          /* Used during execve() */
    char        *u_isp;         /* Value of initial sp (argv) */

    int         (*u_sigvec[NSIGS])();   /* Array of signal vectors */
    char        u_name[8];      /* Name invoked with */
    uint32_t      u_utime;        /* Elapsed ticks in user mode */
    uint32_t      u_stime;        /* Ticks in system mode */
    uint32_t      u_cutime;       /* Total childrens ticks */
    uint32_t      u_cstime;
} u_data;

typedef struct oft {
    uint32_t     o_ptr;      /* File position point16_ter */
    inoptr    o_inode;    /* Pointer into in-core inode table */
    char      o_access;   /* O_RDONLY, O_WRONLY, or O_RDWR */
    char      o_refs;     /* Reference count: depends on # of active children*/
} oft;
/* static struct cinode i_tab[ITABSIZE];    * In-core inode table */
static struct oft of_tab[OFTSIZE]; /* Open File Table */

static void xfs_init(int);

static inoptr root;
static struct cinode i_tab[ITABSIZE];
static struct filesys fs_tab[1];
static struct blkbuf bufpool[NBUFS];
static struct u_data udata;
static void bufsync (void);
static uint8_t *zerobuf (void);
static void brelse(bufptr bp);
static void bawrite(bufptr bp);
static int bfree(bufptr bp, int dirty);
static void bufinit(void);
static bufptr bfind(int dev, blkno_t blk);
static bufptr freebuf(void);
static void magic(inoptr ino);
static uint8_t *bread(int dev, blkno_t blk, int rewrite);
static int fmount(int dev, inoptr ino);
static void i_ref(inoptr ino);
static void xfs_end(void);
static int doclose(int16_t uindex);
static int fuzix_open(char *name, int16_t flag);
static int fuzix_creat(char *name, int16_t mode);
static int fuzix_close(int16_t uindex);
static int fuzix_link( char *name1, char *name2);
static int fuzix_unlink(char *path);
static uint16_t fuzix_read( int16_t d, char *buf, uint16_t nbytes);
static uint16_t fuzix_write( int16_t d, char *buf, uint16_t nbytes);
static int fuzix_mknod( char *name, int16_t mode, int16_t dev);
static int fuzix_mkdir(char *name, int mode);
static blkno_t bmap(inoptr ip, blkno_t bn, int rwflg);
static inoptr rwsetup( int rwflag, int d, char *buf, int nbytes);
static void oft_deref(int of);
static int uf_alloc(void);
static void i_deref(inoptr ino);
static void f_trunc(inoptr ino);
static void freeblk(int dev, blkno_t blk, int level);
static void setftime(inoptr ino, int flag);
static void wr_inode(inoptr ino);
static int isdevice(inoptr ino);
static uint16_t readi( inoptr ino );
static uint16_t writei( inoptr ino);
static void updoff(int d);
static void validblk(int dev, blkno_t num);
static inoptr getinode(int uindex);
static inoptr n_open( register char *name, register inoptr *parent );
static inoptr srch_dir(inoptr wd, register char *compname);
static inoptr srch_mt( inoptr ino);
static inoptr i_open( register int dev, register unsigned ino);
static int ch_link( inoptr wd, char *oldname, char *newname, inoptr nindex);
static char * filename( char *path);
static void filename_2( char *path, char *name);
static int namecomp( char *n1, char *n2);
static inoptr newfile( inoptr pino, char *name);
static fsptr getdev( int devno);
static unsigned i_alloc(int devno);
static void i_free( int devno, unsigned ino);
static blkno_t blk_alloc( int devno );
static void blk_free( int devno, blkno_t blk);
static int oft_alloc(void);
static void fuzix_sync(void);
static int fuzix_chdir(char *dir);
static int min(int a, int b);
static int fuzix_chmod( char *path, int16_t mode);
static int fuzix_stat( char *path, struct uzi_stat *buf);
static void stcpy( inoptr ino, struct uzi_stat *buf);
static int fuzix_getfsys(int dev,char * buf);
static int fuzix_getmode(inoptr ino);
static void bawrite(bufptr bp);
static int bfree(bufptr bp, int dirty);

#endif
