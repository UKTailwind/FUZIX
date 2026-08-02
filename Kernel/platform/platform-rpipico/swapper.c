#include <kernel.h>
#include <kdata.h>
#include <printf.h>
#include <exec.h>
#include "config.h"
#include "globals.h"
#include "psram.h"

#undef DEBUG

/* A special swap and pagemap implementation for the Raspberry Pi Pico. Available
 * memory is divided into 4kB chunks; processes can occupy any number of these (up
 * to 64kB). When a process is switched in, memory is rearranged by swapping chunks
 * until the running process is at the bottom of memory in the right order.
 *
 * p->p_page is 0 if swapped out, 1 is swapped in.
 */

#define BLOCKSIZE 4096
#define NUM_ALLOCATION_BLOCKS (USERMEM / BLOCKSIZE)

/* Pinned to its own linker region (see linker_overrides/) rather than left
 * to float in BSS. As plain BSS the pool moved every time the kernel's size
 * changed, which made unrelated edits shift every process image and turned
 * layout mistakes into bugs that looked like application faults. Now the
 * address is fixed and the kernel outgrowing its half is a link error. */
uint8_t progbase[USERMEM] __attribute__((section(".progbase")));

struct mapentry
{
    uint8_t slot;
    uint8_t block;
};
static struct mapentry allocation_map[NUM_ALLOCATION_BLOCKS];

static uint8_t get_slot(ptptr p)
{
	uint8_t slot = p - ptab;
    if (slot >= PTABSIZE)
        panic("bad ptab");
    return slot;
}

static uaddr_t get_proc_size(ptptr p)
{
    if (!p)
        return 0;
    /* init is initially created with a p_top of 0, but it actually needs 512 bytes. */
    if (!p->p_top)
        udata.u_top = p->p_top = PROGLOAD + 512;
    return p->p_top - PROGBASE;
}

static int get_proc_size_blocks(ptptr p)
{
    return (uaddr_t)alignup(get_proc_size(p), BLOCKSIZE) / BLOCKSIZE;
}

static struct mapentry* find_block(uint8_t slot, uint8_t block)
{
    for (int i=0; i<NUM_ALLOCATION_BLOCKS; i++)
    {
        struct mapentry* b = &allocation_map[i];
        if ((b->slot == slot) && (b->block == block))
            return b;
    }

    return NULL;
}

static struct mapentry* find_free_block(ptptr p)
{
    for (;;)
    {
        struct mapentry* b = find_block(0xff, 0xff); /* find a free block */
        if (b)
            return b;

        #ifdef DEBUG
            kprintf("alloc failed, finding a process to swap out");
        #endif
        if (!swapneeded(p, true))
        {
            kprintf("warning: out of memory\n");
            return NULL;
        }
    }
}

#ifdef DEBUG
    static void debug_blocks(void)
    {
        kprintf("current process size %p bytes %d blocks; isp %d rel\n",
            get_proc_size(udata.u_ptab), get_proc_size_blocks(udata.u_ptab),
            udata.u_isp - PROGBASE);
        for (int i=0; i<NUM_ALLOCATION_BLOCKS; i++)
        {
            const struct mapentry* b = &allocation_map[i];
            void* p = (void*)PROGBASE + i*BLOCKSIZE;
            if (b->block != 0xff)
                kprintf("#%d: slot %d block %d/%d %p\n",
                        i, b->slot,
                        b->block,
                        get_proc_size_blocks(&ptab[b->slot]),
                        p);
        }
    }
#endif

void pagemap_free(ptptr p)
{
    #ifdef DEBUG
        kprintf("free %d\n", get_slot(p));
    #endif
    /* the process is going away: its PSRAM arenas go with it, or a
       megabyte leaks with no OOM killer to recover it */
    arena_release(p);
    int slot = get_slot(p);
    for (int i=0; i<NUM_ALLOCATION_BLOCKS; i++)
    {
        struct mapentry* b = &allocation_map[i];
        if (b->slot == slot)
        {
            #ifdef DEBUG
                kprintf("free slot #%d\n", i);
            #endif
            b->slot = b->block = 0xff;
        }
    }

	p->p_page = 0;
}

int pagemap_alloc(ptptr p)
{
	if (p == udata.u_ptab)
		return 0;

    int blocks = get_proc_size_blocks(p);
    int slot = get_slot(p);
    #ifdef DEBUG
        kprintf("alloc %d, %d blocks\n", get_slot(p), blocks);
        debug_blocks();
    #endif

    for (int i=0; i<blocks; i++)
    {
        struct mapentry* b = find_free_block(p);
        if (!b)
            return ENOMEM;
        b->slot = slot;
        b->block = i;
    }

	p->p_page = 1;
    #ifdef DEBUG
        kprintf("done alloc\n");
        debug_blocks();
    #endif
	return 0;
}

/* size does *not* include udata */
int pagemap_realloc(struct exec *hdr, usize_t size)
{
    struct p_tab* p = udata.u_ptab;

    uaddr_t oldblocks = get_proc_size_blocks(p);
    int blocks = (int)alignup(size + UDATA_SIZE, BLOCKSIZE) / BLOCKSIZE;
    int slot = get_slot(p);

    /* The whole process must fit its fixed-size swap slot: growing past
     * PROGSIZE would make swapout overwrite the neighbouring slot. */
    if (blocks * BLOCKSIZE > PROGSIZE + UDATA_SIZE)
        return ENOMEM;

    #ifdef DEBUG
        kprintf("realloc %d from %d to %d blocks\n", get_slot(udata.u_ptab), oldblocks, blocks);
    #endif
    if (blocks < oldblocks)
    {
        #ifdef DEBUG
            kprintf("shrinking process\n");
        #endif
        for (int i=blocks; i<oldblocks; i++)
        {
            struct mapentry* b = find_block(slot, i);
            b->slot = b->block = 0xff;
        }
    }
    else if (blocks > oldblocks)
    {
        #ifdef DEBUG
            kprintf("growing process\n");
        #endif
        for (int i=oldblocks; i<blocks; i++)
        {
            struct mapentry* b = find_free_block(p);
            if (!b)
                return ENOMEM;
            b->slot = slot;
            b->block = i;
        }
    }

    udata.u_top = p->p_top = PROGBASE + blocks*BLOCKSIZE;
    p->p_size = blocks*BLOCKSIZE / 1024;
    #ifdef DEBUG
        debug_blocks();
    #endif
    contextswitch(p);
	return 0;
}

usize_t pagemap_mem_used(void)
{
    usize_t count = 0;
    for (int i=0; i<NUM_ALLOCATION_BLOCKS; i++)
    {
        struct mapentry* b = &allocation_map[i];
        if (b->slot != 0xff)
            count++;
    }
    return count * (BLOCKSIZE/1024);
}

void pagemap_init(void)
{
    #ifdef DEBUG
        kprintf("%d blocks of memory\n", NUM_ALLOCATION_BLOCKS);
    #endif
    memset(allocation_map, 0xff, sizeof(allocation_map));
	udata.u_ptab = NULL;
}

/* Corruption tripwire (debug): a dormant process's frame contents must
 * not change between the moment it stops running and the moment it
 * runs again.  Sum every resident process's frames when it becomes
 * dormant, verify before it becomes current; panic at the first
 * mismatch instead of crashing minutes later inside the victim. */
#define TRIPWIRE 0
#if TRIPWIRE
static uint32_t slot_sum[PTABSIZE];
static uint8_t slot_sum_valid[PTABSIZE];

static uint32_t sum_slot(int slot, int blocks)
{
    uint32_t sum = 0;
    for (int i = 0; i < blocks; i++) {
        struct mapentry *b = find_block(slot, i);
        uint32_t *w, *e;
        if (!b)
            panic("tripwire: lost block");
        w = (uint32_t *)((void *)PROGBASE
                         + (b - allocation_map) * BLOCKSIZE);
        e = w + BLOCKSIZE / 4;
        while (w < e)
            sum = (sum << 1 | sum >> 31) ^ *w++;
    }
    return sum;
}

static void tripwire_check(ptptr newp)
{
    for (ptptr q = ptab; q < ptab + PTABSIZE; q++) {
        int s;
        if (q->p_status == P_EMPTY || !q->p_page)
            continue;
        s = get_slot(q);
        if (!slot_sum_valid[s]) {
            /* freshly dormant (the outgoing process): record it */
            slot_sum[s] = sum_slot(s, get_proc_size_blocks(q));
            slot_sum_valid[s] = 1;
            continue;
        }
        if (q == newp)
            continue;        /* about to run: verified below by caller */
        if (slot_sum[s] != sum_slot(s, get_proc_size_blocks(q))) {
            kprintf("tripwire: slot %d pid %d corrupted while dormant\n",
                    s, q->p_pid);
            panic("tripwire");
        }
    }
}
#endif

void contextswitch(ptptr p)
{
#if TRIPWIRE
    tripwire_check(p);
    if (p->p_page) {
        int s = get_slot(p);
        if (slot_sum_valid[s]
            && slot_sum[s] != sum_slot(s, get_proc_size_blocks(p))) {
            kprintf("tripwire: incoming slot %d pid %d corrupt\n",
                    s, p->p_pid);
            panic("tripwire");
        }
        slot_sum_valid[s] = 0;   /* it is about to run and change */
    }
#endif
    #ifdef DEBUG
        kprintf("context switch from %d to %d\n", get_slot(udata.u_ptab), get_slot(p));
    #endif

    if (!p->p_page)
        swapin(p, p->p_page2);

    int slot = get_slot(p);
    int blocks = get_proc_size_blocks(p);
    for (int i=0; i<blocks; i++)
    {
        struct mapentry* b1 = &allocation_map[i];
        int i1 = b1 - allocation_map;
        void* p1 = (void*)PROGBASE + i1*BLOCKSIZE;
        if ((b1->slot != slot) || (b1->block != i))
        {
            struct mapentry* b2 = find_block(slot, i);
            if (!b2)
                panic("missing block");
            int i2 = b2 - allocation_map;
            void* p2 = (void*)PROGBASE + i2*BLOCKSIZE;
            if (b1->slot == 0xff)
            {
                #ifdef DEBUG
                    kprintf("copy #%d to #%d\n", i2, i1);
                #endif
                memcpy(p1, p2, BLOCKSIZE);
            }
            else
            {
                #ifdef DEBUG
                    kprintf("swap #%d and #%d\n", i1, i2);
                #endif
                swap_blocks(p1, p2, BLOCKSIZE);
            }

            struct mapentry t = *b1;
            *b1 = *b2;
            *b2 = t;
        }
    }

    #ifdef DEBUG
        debug_blocks();
    #endif
}

/* Copy the current process into a new child slot, and context switch so it's live. */
void clonecurrentprocess(ptptr p)
{
    #ifdef DEBUG
        kprintf("clone %d to slot %d\n", get_slot(udata.u_ptab), get_slot(p));
        if (p->p_top != udata.u_ptab->p_top)
            panic("mismatched sizes");
    #endif
    int srcslot = get_slot(udata.u_ptab);
    int destslot = get_slot(p);
    int blocks = get_proc_size_blocks(p);
    for (int i=0; i<blocks; i++)
    {
        struct mapentry* b1 = find_block(srcslot, i);
        struct mapentry* b2 = find_block(destslot, i);
        if (!b1 || !b2)
            panic("missing block");
        int i1 = b1 - allocation_map;
        int i2 = b2 - allocation_map;
        void* p1 = (void*)PROGBASE + i1*BLOCKSIZE;
        void* p2 = (void*)PROGBASE + i2*BLOCKSIZE;
        #ifdef DEBUG
            kprintf("copy #%d to #%d (%p to %p)\n", i1, i2, p1, p2);
        #endif
        memcpy(p2, p1, BLOCKSIZE);

        struct mapentry t = *b1;
        *b1 = *b2;
        *b2 = t;
    }
    #ifdef DEBUG
        kprintf("end clone\n");
    #endif
}

uint_fast8_t plt_canswapon(uint16_t devno)
{
    /* Only allow swapping to hd devices. */
    return (devno >> 8) == 0;
}

/* Swap round-trip tripwire.
 *
 * The swap device is the XIP-mapped PSRAM, cached WRITE-BACK (psram.c
 * sets XIP_CTRL_WRITABLE_M1).  A swapped-out process therefore lives in
 * dirty cache lines until something writes them through, and anything
 * that invalidates that cache without cleaning it first loses them -
 * see the QMI/XIP note in config.h.  A process swapped back in from
 * lines that were dropped comes back subtly wrong, and because its
 * udata rides along in the same image (UDATA_BLKS of it, holding
 * u_block, u_dptr, u_nblock and the open file table) the first thing a
 * damaged process does is write a perfectly valid file block to the
 * WRONG LBA.  That destroys a filesystem while leaving the in-memory
 * superblock untouched, which is exactly the corruption seen here and
 * exactly why the superblock tripwire stays silent through it.
 *
 * So checksum what goes out and check what comes back.  One sum per
 * swap slot rather than per block: 124 bytes, and the blocks are
 * written and read in the same order, so accumulating across them costs
 * one pass over memory we are already copying.
 */
#define SWAP_TRIPWIRE 1
#if SWAP_TRIPWIRE
static uint32_t swap_sum[MAX_SWAPS];

static uint32_t sum_words(uint32_t sum, const void *base, unsigned len)
{
    const uint32_t *w = base;
    const uint32_t *e = w + len / 4;
    while (w < e)
        sum = (sum << 1 | sum >> 31) ^ *w++;
    return sum;
}
#endif

int swapout(ptptr p)
{
#if SWAP_TRIPWIRE
	uint32_t sum = 0;
#endif
#ifdef DEBUG
	kprintf("swapping out %d (%d)\n", get_slot(p), p->p_pid);
#endif

	uint16_t page = p->p_page;
	if (!page)
		panic(PANIC_ALREADYSWAP);
    if (SWAPDEV == 0xffff)
        return ENOMEM;

	/* Are we out of swap ? */
	int16_t map = swapmap_alloc();
	if (map == -1)
		return ENOMEM;

	uint16_t swaparea = map * SWAP_SIZE;

    int slot = get_slot(p);
    int blocks = get_proc_size_blocks(p);
    for (int i=0; i<blocks; i++)
    {
        struct mapentry* b = find_block(slot, i);
        int blockindex;
        void* p;

        if (!b)
            panic("swapout: lost block");
        blockindex = b - allocation_map;
        p = (void*)PROGBASE + blockindex*BLOCKSIZE;

#if SWAP_TRIPWIRE
        sum = sum_words(sum, p, BLOCKSIZE);
#endif
        if (swapwrite(SWAPDEV, swaparea + (i*(BLOCKSIZE>>BLKSHIFT)),
            BLOCKSIZE, (uaddr_t)p, 1) != BLOCKSIZE)
            panic("swapout: write failed");

        b->slot = b->block = 0xff;
    }
#if SWAP_TRIPWIRE
	swap_sum[map] = sum;
#endif

	p->p_page = 0;
	p->p_page2 = map;
	return 0;
}

/*
 * Swap ourself in: must be on the swap stack when we do this
 */
void swapin(ptptr p, uint16_t map)
{
    uint16_t swaparea = map * SWAP_SIZE;
#if SWAP_TRIPWIRE
    uint32_t sum = 0;
#endif

    int slot = get_slot(p);
    int blocks = get_proc_size_blocks(p);
    for (int i=0; i<blocks; i++)
    {
        struct mapentry* b = find_free_block(p);
        int blockindex;
        void* p;

        /* find_free_block returning NULL is survivable for brk (the
         * caller reports ENOMEM) but not here: dereferencing it turns
         * into a swapread through a wild constant pointer - 4K of
         * disc into the same innocent memory every time, which is
         * exactly the class of corruption that must be a panic. */
        if (!b)
            panic("swapin: no memory");
        blockindex = b - allocation_map;
        p = (void*)PROGBASE + blockindex*BLOCKSIZE;

        if (swapread(SWAPDEV, swaparea + (i*(BLOCKSIZE>>BLKSHIFT)),
            BLOCKSIZE, (uaddr_t)p, 1) != BLOCKSIZE)
            panic("swapin: read failed");
#if SWAP_TRIPWIRE
        sum = sum_words(sum, p, BLOCKSIZE);
#endif

        b->slot = slot;
        b->block = i;
    }
#if SWAP_TRIPWIRE
    if (sum != swap_sum[map]) {
        kprintf("\nswap tripwire: pid %d slot %d area %u came back changed"
                " (%x, wrote %x)\n",
                p->p_pid, slot, swaparea, sum, swap_sum[map]);
        panic("swapsum");
    }
#endif

    p->p_page = 1;
    p->p_page2 = 0;
    /* The slot is free again.  The generic kernel does this in
     * swapper2(); this port swaps in from contextswitch() and never
     * goes through swapper2(), so without this line every swap-in
     * leaked one of the ~22 slots and a single multi-pass compile
     * exhausted the pool - after which the NULL path above corrupted
     * memory deterministically.  Found 2026-07-31 chasing a shell
     * crash at a constant PC. */
    swapmap_add(map);
}

arg_t brk_extend(uaddr_t addr)
{
    if (addr < PROGBASE)
        return EINVAL;
    if (addr >= brk_limit()) {
        /* Claim more memory for this process.  Fail quietly: ENOMEM is
         * a normal answer (BBC BASIC probes for its workspace this way)
         * and the process will report it if it matters. */
        if (pagemap_realloc(NULL, addr - PROGBASE))
            return ENOMEM;
        return 0;
    }
    return 0;
}

// vim: ts=4 sw=4 et

