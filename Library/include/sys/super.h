#ifndef _SYS_SUPER_H
#define _SYS_SUPER_H

#include <sys/types.h>

typedef uint32_t blkno_t;	/* FS32: 32-bit block numbers */

/*
 * FS32 superblock structure, on-disk region of block 1 (offsets fixed
 * by Kernel/platform/platform-rpipico/FS32-FORMAT.md).  Bytes 332..511
 * on disk are reserved, written as zero.  The classic filesys_user
 * tail - label, geometry - is gone: FS32 declares those bytes
 * reserved and nothing on this port used them.
 */
#define FILESYS_TABSIZE 50
struct fuzix_filesys_kernel {
    uint16_t      s_mounted;	/* magic 0xFB32 */
    uint16_t      s_version;	/* FS32_VERSION */
    uint32_t      s_isize;	/* first data block */
    uint32_t      s_fsize;	/* total blocks */
    blkno_t       s_tfree;	/* total free blocks */
    int16_t       s_nfree;	/* valid entries in s_free */
    uint16_t      s_tinode;	/* total free inodes */
    blkno_t       s_free[FILESYS_TABSIZE];
    int16_t       s_ninode;
    uint16_t      s_inode[FILESYS_TABSIZE];
    uint8_t       s_fmod;
#define FMOD_DIRTY	1	/* Mounted or uncleanly unmounted from r/w */
#define FMOD_CLEAN	2	/* Clean. Used internally to mean don't
				   update the super block */
    uint8_t       s_timeh;	/* bits 32-40: FIXME - wire up */
    uint32_t      s_time;
    uint8_t	  s_shift;	/* must be 0 in FS32 v1 */
    uint8_t	  s_pad0[3];
};

#define ROOTINODE 1
#define SMOUNTED 0xFB32   /* Magic number to specify mounted filesystem */
#define SMOUNTED_WRONGENDIAN 0x32FB   /* byteflipped */
#define SMOUNTED_CLASSIC 12742	/* the pre-FS32 format, refused by name */
#define FS32_VERSION 1

/* FS32 pointer geometry, shared by the fs tools */
#define FS32_IPERBLK	2	/* 256-byte inodes */
#define FS32_DINODE_SIZE 256
#define FS32_DIRECT_BLOCKS 40
#define FS32_IND_PER_BLOCK 128

#endif
