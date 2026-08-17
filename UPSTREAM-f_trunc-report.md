# f_trunc_blocks leaves i_addr[19] pointing at a freed block

`Kernel/filesys.c`, `f_trunc_blocks()`. Present on current master.

## The code

```c
    /* First deallocate the double indirect blocks */
    freeblk(dev, ino->c_node.i_addr[19], 2, map2);
    if (map2)
        ino->c_node.i_addr[19] = 0;

    /* Also deallocate the indirect blocks */
    freeblk(dev, ino->c_node.i_addr[18], 1, map1);
    if (map1 == 0 && map2 == 0)	/* ???? should this just be if map1 */
        ino->c_node.i_addr[18] = 0;
```

`f_trunc()` calls `f_trunc_blocks(ino, 0)`, so with `nblock == 0` both
`map1` and `map2` are 0.

`freeblk()` frees the block it is given — the root of the subtree, not
just its children — so the double indirect root goes onto the free
list. Then `if (map2)` is false, and `i_addr[19]` is left pointing at
it.

The single indirect line immediately below has the opposite sense and
does clear `i_addr[18]` in the same situation. The two tests disagree,
and only one of them can be right.

## What it costs

The inode keeps a pointer to a block that is now free. When the file is
rewritten — `open(..., O_TRUNC)` then write — `bmap()` reaches logical
block 274, finds `i_addr[19]` non-zero, and uses the freed block as the
double indirect root. By then it has usually been reallocated, often as
a data block of the same file, so `bmap()` reads file data and treats
each 16-bit word as a block number.

Downstream that shows up as:

* `blk_free()` called on nonsense block numbers (`validblk` panics if
  the number happens to fall outside the filesystem, and silently
  corrupts if it does not);
* the same block allocated twice, so a free list chain block gets
  zeroed by `blk_alloc()`'s "zero the new block" step and the next
  refill reads `s_nfree == 0`, giving `blk_alloc: corrupt`;
* `fsck` reporting out-of-range block pointers in the file.

Observed here, on a filesystem whose data blocks are 256..64000:

```
Inode 321 block 274 out of range, val = 50.    Zap? y
Inode 321 block 277 out of range, val = 65442. Zap? y
```

Logical block 274 is the first double indirect block. The values are
not block numbers at all: the file was a 1-bit-per-pixel image saved as
a 24-bit BMP, so its data is mostly `0x00` and `0xFF` bytes, and
`0x0032` = 50 and `0xFFA2` = 65442 are pixels.

Only files longer than 273 blocks reach double indirection, which is
presumably why this has not been hit before — it needs a file over
about 140 KB that is truncated and rewritten repeatedly.

## Reproducing it

Any filesystem with room, and a file over 273 blocks (139,776 bytes):

```sh
dd if=/dev/zero of=/tmp/pattern bs=1024 count=200
cp /tmp/pattern big
for i in 1 2 3 4 5; do cp /tmp/pattern big; done   # cp opens O_TRUNC
sync
fsck /dev/...
```

`fsck` reports out-of-range blocks in `big`, starting at logical block
274. Repeating the rewrite makes it progressively worse as the freed
root gets reallocated to different things.

Here it was found with a BASIC program repeatedly saving and reloading
a screenshot — 451 blocks per file, five rounds destroyed the
filesystem — and reproduced identically on two different machines with
different SD card drivers, which is what ruled out the hardware.

## Note on the surrounding code

While reading it, two more things in the partial-truncation path look
wrong, though I have not confirmed them with a test that exercises
`ftruncate()`:

* the internal (non-`CONFIG_BLKBUF_EXTERNAL`) `freeblk()` loops
  `for (j = BLKSIZE / 2 - 1; j >= 0; --j)`, where the external variant
  loops `j >= nblock1`. The internal one therefore frees every entry
  regardless of the retention bound.
* `freeblk()` frees the block it was given unconditionally, including
  when the call is a partial clear and that block is meant to survive.
* with `nblock > 273`, `map1` is 0, so
  `freeblk(dev, i_addr[18], 1, map1)` frees the entire single indirect
  tree even though logical blocks 18..273 are all retained.

`f_trunc()` (`nblock == 0`) does not hit any of these, so they are a
separate question from the one above.
