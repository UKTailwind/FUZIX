/*
 *	Defines for the FS32 512 byte file system layout
 *	(Kernel/platform/platform-rpipico/FS32-FORMAT.md is the authority)
 */

#define BLKSIZE		512
#define INO_PER_BLOCK	2	/* 256-byte inodes */
#define BLKSHIFT	9
#define BLOCK(x)	((x) >> BLKSHIFT)
#define BLKMASK		511

/* Help the 8bit compilers out by preventing any 32bit promotions */
#define BLKOFF(x)	(((uint16_t)(x)) & BLKMASK)

#define BLK_TO_OFFSET(x)	((x) << BLKSHIFT)

#define SMOUNTED  0xFB32  /* Magic number to specify mounted filesystem */
#define SMOUNTED_CLASSIC 12742	/* pre-FS32 format: refused by name */
#define FS32_VERSION 1

/* FS32 pointer geometry: 40 direct + single + double + triple
   indirect, 128 32-bit pointers per indirect block */
#define DIRECT_BLOCKS	40
#define IND_PER_BLOCK	128	/* BLKSIZE / sizeof(blkno_t) */
#define ONE_IND_END	(DIRECT_BLOCKS + IND_PER_BLOCK)		     /* 168 */
#define TWO_IND_END	(ONE_IND_END + IND_PER_BLOCK*IND_PER_BLOCK) /* 16552 */
#define THREE_IND_END	(TWO_IND_END + 128UL*128*128)	  /* 2113704 blocks */

/* Size of a directory. They can contain padding internally but a disk block
   must be divisible exactly into directory entries */

#define FILENAME_LEN	30
#define DIR_LEN		32
