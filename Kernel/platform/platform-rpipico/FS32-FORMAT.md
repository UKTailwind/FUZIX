# FS32: the PC3 large-disk filesystem format

Version 1.  This document is the format.  The kernel (`filesys.c`), the
host tools (`Standalone/`), and the on-card tools (`fsck-fuzix`, `mkfs`,
`df`) each implement it independently and are checked against each
other; when they disagree, this document decides which one is wrong.

## Why it exists

The classic Fuzix filesystem addresses blocks with `uint16_t blkno_t`:
65,535 x 512-byte blocks = 32 MB, hard.  The v0.8 SD root ships at
64,000 blocks - the format's ceiling, minus safety margin - on cards a
hundred times that size.  FS32 widens block numbers to 32 bits and the
inode to 256 bytes, and removes the ceiling for any card this machine
will ever see.

This is a flag-day format.  There is no dual-format support: an FS32
kernel refuses classic filesystems (wrong magic) and a classic kernel
refuses FS32, both cleanly at mount.  Kernel and card already ship as a
matched pair per release, so the flag day is one release boundary.

## What deliberately does not change

- **Physical block: 512 bytes.**  The buffer cache, the blkdev layer,
  raw device I/O, the SD driver and the `fat`/`fdisk` utilities are all
  untouched.  No SRAM cost.
- **Directory entry: 32 bytes** - `uint16_t d_ino` + 30-char name.
  Directory code, userspace `struct stat` (`st_ino` is 16-bit) and every
  program that reads directories are untouched.  Consequence: **at most
  65,535 inodes per filesystem** - the one 16-bit limit FS32 keeps,
  accepted to avoid breaking the userspace ABI.
- **Disk layout skeleton**: block 0 boot/reserved, block 1 superblock,
  blocks 2..s_isize-1 inodes, block s_isize the root directory, data to
  s_fsize-1.
- Free-list allocator (classic 50-entry chained cache), inode
  allocation, mount semantics, `s_fmod` dirty handling: same
  algorithms, wider fields.

## Limits

| Quantity          | Classic          | FS32                              |
|-------------------|------------------|-----------------------------------|
| Filesystem size   | 32 MB            | 2 TB (uint32 blocks x 512)        |
| Max file size     | ~32 MB           | ~1.0 GiB (pointer tree; off_t allows 2 GiB) |
| Inodes per fs     | 65,535           | 65,535 (kept: 16-bit d_ino)       |
| Inode size        | 64 bytes (8/blk) | 256 bytes (2/blk)                 |
| Block pointers    | 18+1+1 x 16-bit  | 40+1+1+1 x 32-bit (adds triple)   |
| NULLBLK sentinel  | 0xFFFF (reachable!) | 0xFFFFFFFF (unreachable)       |

The classic format needed the fs kept clear of 65,535 because NULLBLK
was a real, existing sector (see mksdimage.sh's 64000-block note).  In
FS32 the sentinel can never be a valid block, so a filesystem may fill
its partition exactly.

## Magic

    SMOUNTED_FS32             0xFB32   (64306)
    SMOUNTED_FS32_WRONGENDIAN 0x32FB   (13051)

Classic magic is 12742 (0x31C6); a mount that finds it reports "classic
filesystem - reformat needed" rather than a bare EINVAL.

## Superblock (block 1)

All fields little-endian (the native order of every host and target
involved; the tools keep the `-X` swizzle machinery regardless).  The
struct is laid out so that every field is naturally aligned - the
on-disk bytes are the C struct bytes on both x86-64 and ARM EABI, with
no implicit padding anywhere.

    offset size type      field       meaning
    0      2    uint16    s_mounted   magic, 0xFB32
    2      2    uint16    s_version   format version, = 1
    4      4    uint32    s_isize     first data block; inodes occupy 2..s_isize-1
    8      4    uint32    s_fsize     total blocks in filesystem
    12     4    uint32    s_tfree     total free blocks
    16     2    int16     s_nfree     valid entries in s_free
    18     2    uint16    s_tinode    total free inodes
    20     200  uint32[50] s_free     free-list cache (see free list below)
    220    2    int16     s_ninode    valid entries in s_inode
    222    100  uint16[50] s_inode    free-inode cache
    322    1    uint8     s_fmod      FMOD_DIRTY / FMOD_CLEAN as today
    323    1    uint8     s_timeh     time bits 32-39 (reserved, as today)
    324    4    uint32    s_time      last-update time
    328    1    uint8     s_shift     must be 0 in version 1
    329    3    uint8[3]  pad         must be 0
    332    180  uint8[180] reserved   must be 0 when written; ignored on read

    total consumed: 332 of 512
    invariants at mount: s_mounted == 0xFB32, s_version == 1,
      2 < s_isize < s_fsize, s_shift == 0,
      (s_isize - 2) * 2 <= 65535

`s_version` exists so that any future layout change is a version bump
with a defined migration, never another wholesale format.

## Inode (256 bytes, 2 per block)

Inode number i lives in block `2 + (i >> 1)` at byte offset
`(i & 1) << 8`.  Inode 0 is reserved (marked allocated, never used);
inode 1 is the root directory.  An inode never straddles a block.

    offset size type      field       meaning
    0      2    uint16    i_mode      as classic
    2      2    uint16    i_nlink
    4      2    uint16    i_uid
    6      2    uint16    i_gid
    8      4    uint32    i_size      bytes; off_t caps at 2^31-1
    12     4    uint32    i_atime
    16     4    uint32    i_mtime
    20     4    uint32    i_ctime
    24     3    uint8[3]  i_timeh     bits 32-39 of a/m/ctime; 0 in v1
    27     1    uint8     pad         must be 0
    28     172  uint32[43] i_addr     block pointers, see below
    200    56   uint8[56] reserved    must be 0 when written; ignored on read

### Pointer geometry

    i_addr[0..39]   40 direct blocks           ->  20 KB
    i_addr[40]      single indirect: 128 ptrs  -> +64 KB
    i_addr[41]      double indirect: 128^2     -> +8 MB
    i_addr[42]      triple indirect: 128^3     -> +1 GB

An indirect block is 512 bytes = 128 uint32 block numbers, zero
meaning "hole / not allocated" exactly as classic.  Named constants -
never bare numbers - in all implementations:

    DIRECT_BLOCKS   40
    IND_PER_BLOCK   128           /* BLKSIZE / sizeof(uint32_t) */
    ONE_IND_END     (DIRECT_BLOCKS + IND_PER_BLOCK)              /* 168 */
    TWO_IND_END     (ONE_IND_END + IND_PER_BLOCK*IND_PER_BLOCK)  /* 16552 */
    THREE_IND_END   (TWO_IND_END + 128UL*128*128)                /* 2113704 */

Max file = THREE_IND_END * 512 = 1,082,216,448 bytes.  The classic
code's magic numbers 18 / 256 / 273 / 274 (and the itrunc comment
"reaches logical block 274") map to DIRECT_BLOCKS / IND_PER_BLOCK /
ONE_IND_END-1 / ONE_IND_END.  **Triple indirect is new code in every
implementation** - classic Fuzix explicitly does not support it - and
is the part that earns the torture tests.

## Free list

Classic algorithm, 32-bit entries.  The superblock caches up to 50 free
block numbers in `s_free`; when a 51st block is freed, the current 50 +
count are written into the block being freed, which becomes the head of
the chain:

    Free-list chain block:
    offset size type       field
    0      2    int16      count   (1..50)
    2      2    uint16     pad     (0)
    4      200  uint32[50] free    (entries beyond count undefined)

Implementations must write this **explicit struct**, not the classic
overlay trick of dumping memory from `&s_nfree` - the overlay's shape
is an accident of struct packing and differs between the old and new
superblocks.

The in-superblock free-inode cache (`s_ninode`/`s_inode`) is advisory
exactly as today: the PC3 kernel discards it at mount and rebuilds by
scanning (see the fmount comment in filesys.c) - unchanged.

## mkfs parameters

`mkfs device inodes fsize` - **the second argument becomes an inode
count, not a block count of inodes**.  isize on disk is derived:
`s_isize = 2 + ((inodes + 1) >> 1)`.  Scripts stop encoding the
64-byte-inode assumption; "2048 inodes" stays "2048" whatever the inode
size.  fsize may equal the partition's sector count exactly.

## Retired

- `s_shift` extents: field kept (must be 0), mechanism dropped.  mkfs's
  `-b` option is removed - it wrote layouts nothing could mount.
- The 64000-block safety margin and its NULLBLK reasoning.

## Verification gates (in order, each depends on the last)

1. Host: new mkfs image -> new Standalone fsck reports clean.
2. Host: ucp populates a tree (the update-flash.sh recipe) -> fsck
   clean; files read back byte-identical; a file larger than TWO_IND_END
   blocks written and read back (exercises triple indirect).
3. Host: deliberate corruption (bad pointer, crosslinked block, bad
   free count) -> fsck detects each.
4. Kernel: boots, mounts, runs the create/write/truncate/delete/fill
   torture; fsck (host AND on-card implementations) clean after each
   phase.  Two independent implementations agreeing is the gate -
   never one implementation checked against its own list.
5. Board: full release gates + card written and booted.
