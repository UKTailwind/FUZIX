#include <kernel.h>
#include <kdata.h>
#include <printf.h>
#include <exec.h>
#include "config.h"
#include "globals.h"
#include "psram.h"
#ifdef CONFIG_PC3_DISPLAY
#include "display.h"
#endif
#ifdef CONFIG_PC3_PINLOCK
#include "pinlock.h"
#endif

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

/*
 * The same measurement for a process that is NOT the running one.
 *
 * Sizing another process must not MODIFY, and get_proc_size() does:
 * it carries init's fixup, "if (!p->p_top) udata.u_top = p->p_top =
 * PROGLOAD + 512", and udata there is the CURRENT process.  Measuring
 * a dormant process through it would write the running process's
 * u_top - a store the kernel makes nowhere else.  One that has not
 * been sized yet occupies nothing.
 */
static int other_proc_blocks(ptptr p)
{
    if (!p->p_top)
        return 0;
    return (int)((uaddr_t)alignup(p->p_top - PROGBASE, BLOCKSIZE)
                 / BLOCKSIZE);
}

/*
 * The largest process other than this one, in blocks.
 *
 * This is the room that has to be left: swapin() has nowhere to put an
 * incoming image except resident blocks, and it cannot evict the
 * process that is running to get them - so if one process takes the
 * whole pool, nothing else can ever come back and the next context
 * switch to it panics with "swapin: no memory".
 *
 * v0.5 was protected from that by accident.  PROGSIZE was a fixed 256K
 * and pagemap_realloc refused to grow a process past it, which left
 * 56K of the 312K pool for everyone else.  v0.6 raised PROGSIZE to the
 * whole of USERMEM - deliberately, so that ONE process could use all of
 * memory - and took the only reserve with it.
 *
 * bbcbasic is what found it: its startup probes with sbrk(4096) in a
 * loop until the kernel refuses (bbccon.c, "capped: probe, not
 * landgrab"), so it takes exactly as much as it is allowed to and the
 * refusal is the whole mechanism.  Under v0.6 the refusal came only
 * when the pool was empty.
 *
 * So the ceiling is now the thing it should always have been: not an
 * arbitrary constant, but "leave room for the biggest neighbour to be
 * resident alongside me".
 */
static int largest_neighbour(void)
{
    ptptr q;
    int most = 0;

    for (q = ptab; q < ptab + PTABSIZE; q++) {
        int n;
        /* A zombie has already had its blocks handed back by
           pagemap_free and will never be swapped in again, so it needs
           no room kept for it. */
        if (q->p_status == P_EMPTY || q->p_status == P_ZOMBIE
            || q == udata.u_ptab)
            continue;
        n = other_proc_blocks(q);
        if (n > most)
            most = n;
    }
    return most;
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
        /* Silent: running out here is not necessarily a failure any
           more - pagemap_alloc has a second way to place a forked
           child - so the callers say so when it really is one. */
        if (!swapneeded(p, true))
            return NULL;
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
#ifdef CONFIG_PC3_DISPLAY
    /* and so does the framebuffer layer, or the next program to ask for
       one is told it is busy by a process that no longer exists */
    display_fb_release(p);
#endif
#ifdef CONFIG_PC3_PINLOCK
    /* and the I/O header, with the PINS THEMSELVES put back to inputs:
       freeing the claim alone would leave a program that died driving a
       relay still driving it */
    pinlock_release(p);
#endif
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

/*
 *	All or nothing.  The blocks taken before we ran out are given back:
 *	newproc() abandons the slot on ENOMEM without calling pagemap_free,
 *	and since the slot stays P_EMPTY nothing else ever will either, so
 *	whatever we kept would be lost for the rest of the boot.  One
 *	failed fork of bcrun used to cost 132K of a 312K machine.
 */
static int pagemap_alloc_resident(ptptr p)
{
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
        {
            for (int j=0; j<i; j++)
            {
                struct mapentry* c = find_block(slot, j);
                if (c)
                    c->slot = c->block = 0xff;
            }
            return ENOMEM;
        }
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

/*
 *	Set by pagemap_alloc when a fork found no room for a second
 *	resident copy, consumed by clonecurrentprocess.  Interrupts are off
 *	from newproc() through dofork(), so one word is enough.
 */
static uint32_t fork_stage;

/*
 *	Where a swapped-out process's image lives, indexed by process slot.
 *	Kept here rather than in p_page2 because that is a uint16_t and
 *	cannot hold a pointer; p_page2 keeps its "is it swapped" role for
 *	the generic kernel, and where the bytes are is our business.
 *	Declared this early because clonecurrentprocess writes it too.
 */
static uint32_t swapaddr[PTABSIZE];

int pagemap_alloc(ptptr p)
{
	if (p == udata.u_ptab)
		return 0;

	fork_stage = 0;

	if (pagemap_alloc_resident(p) == 0)
		return 0;

	/* A process coming back IN from PSRAM has to have real memory:
	   its image is already in the arena and there is nowhere else for
	   it to go.  p_page2 marks it; newproc() memsets a new p_tab, so
	   for a fresh child it is 0. */
	if (p->p_page2 || !udata.u_ptab)
	{
		kprintf("warning: out of memory\n");
		return ENOMEM;
	}

	/*
	 *	A fork, with room for only one of the two images.  Parent and
	 *	child are identical at this instant, so which of them is
	 *	called the copy is free: stage the PARENT into PSRAM and let
	 *	the child keep the resident blocks it is already executing in
	 *	(clonecurrentprocess does the relabelling).
	 *
	 *	Without this, fork needs 2x the process resident and nothing
	 *	larger than half of USERMEM can fork at all - bcrun with a
	 *	program loaded is ~172K of a 312K machine, which is why every
	 *	SAVE IMAGE ended in "cannot start a program".
	 */
	fork_stage = arena_alloc_raw(udata.u_ptab,
				     (unsigned)get_proc_size_blocks(p) * BLOCKSIZE);
	if (!fork_stage)
	{
		kprintf("warning: out of memory\n");
		return ENOMEM;
	}
	p->p_page = 1;
	return 0;
}

/* size does *not* include udata */
int pagemap_realloc(struct exec *hdr, usize_t size)
{
    struct p_tab* p = udata.u_ptab;

    uaddr_t oldblocks = get_proc_size_blocks(p);
    int blocks = (int)alignup(size + UDATA_SIZE, BLOCKSIZE) / BLOCKSIZE;
    int slot = get_slot(p);

    /* Swap is an allocation the size of the process now, not a fixed
     * slot, so the address-space ceiling is all of it (see PROGSIZE in
     * config.h). */
    if (blocks * BLOCKSIZE > PROGSIZE + UDATA_SIZE)
        return ENOMEM;

    /* But a GROW must leave room for the biggest other process to be
     * resident, or it can never be swapped back in - see
     * largest_neighbour().  Only growing is checked: shrinking, and a
     * process that is already over the line because its neighbours
     * appeared after it, must both still be able to proceed. */
    if (blocks > oldblocks) {
        int room = NUM_ALLOCATION_BLOCKS - largest_neighbour();
        if (room < 1)
            room = 1;
        if (blocks > room)
            return ENOMEM;
    }

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
            {
                /* Unwind: p_top still says oldblocks, so anything we
                   kept would be invisible to contextswitch and to the
                   next grow, which would then hand out a second block
                   with the same index. */
                for (int j=oldblocks; j<i; j++)
                {
                    struct mapentry* c = find_block(slot, j);
                    if (c)
                        c->slot = c->block = 0xff;
                }
                kprintf("warning: out of memory\n");
                return ENOMEM;
            }
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
/* OFF, and not worth turning on again without redesigning it.  Enabled
 * 2026-08-02 to chase the filesystem corruption, it produced two false
 * alarms (see get_proc_size's write in tripwire_blocks(), and udata
 * living inside block 0) and then hung the machine solid - no panic, no
 * reboot, no response to ^C - in the one workload we needed it for,
 * while the compile it does not care about ran fine.  It cost three
 * re-image-and-flash cycles and yielded nothing about the actual bug. */
#define TRIPWIRE 0
#if TRIPWIRE
static uint32_t slot_sum[PTABSIZE];
static uint8_t slot_sum_valid[PTABSIZE];

/* Measuring must not MODIFY.  get_proc_size() carries init's fixup -
 * "if (!p->p_top) udata.u_top = p->p_top = PROGLOAD + 512" - and udata
 * here is *(struct u_data *)progbase, i.e. the CURRENT process.  So
 * sizing some OTHER process from the tripwire would write the running
 * process's u_top, a store the unmodified kernel never makes.  A
 * process that has not been sized yet is simply not checked. */
static int tripwire_blocks(ptptr p)
{
    if (!p->p_top)
        return 0;
    return (int)((uaddr_t)alignup(p->p_top - PROGBASE, BLOCKSIZE) / BLOCKSIZE);
}

/* Block 0 starts with udata (`#define udata (*(struct u_data*)progbase)`
 * - it lives INSIDE the process image, not beside it), and the kernel is
 * entitled to write a dormant process's udata: fork copies the parent's
 * blocks, swaps the map entries and then fills in the child's u_ptab and
 * its zero return value while the child is not running.  Summing it
 * therefore reports every fork as corruption - which is what "incoming
 * slot 1 pid 2" was, a freshly forked child, on a perfectly clean disk.
 *
 * So skip the udata bytes and start block 0 at PROGLOAD.  Everything the
 * process itself owns is still covered; the one region whose changes are
 * legitimate is not. */
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
        if (i == 0)
            w += UDATA_SIZE / 4;
        while (w < e)
            sum = (sum << 1 | sum >> 31) ^ *w++;
    }
    return sum;
}

static void tripwire_check(ptptr newp)
{
    for (ptptr q = ptab; q < ptab + PTABSIZE; q++) {
        int s, nb;
        if (q->p_status == P_EMPTY || !q->p_page)
            continue;
        nb = tripwire_blocks(q);
        if (!nb)
            continue;        /* not sized yet - see tripwire_blocks() */
        s = get_slot(q);
        if (!slot_sum_valid[s]) {
            /* freshly dormant (the outgoing process): record it */
            slot_sum[s] = sum_slot(s, nb);
            slot_sum_valid[s] = 1;
            continue;
        }
        if (q == newp)
            continue;        /* about to run: verified below by caller */
        if (slot_sum[s] != sum_slot(s, nb)) {
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
    if (p->p_page && tripwire_blocks(p)) {
        int s = get_slot(p);
        if (slot_sum_valid[s]
            && slot_sum[s] != sum_slot(s, tripwire_blocks(p))) {
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

    /*
     *	Only one of the two images fits in RAM (see pagemap_alloc).  The
     *	copy goes to PSRAM and is called the PARENT, because the blocks
     *	we are executing in right now are the ones dofork returns into
     *	as the child.  Nothing moves and nothing is freed - the resident
     *	blocks just change their label.
     *
     *	The parent's udata rides along in block 0, and dofork saved its
     *	stack pointer into it before calling us, so the image is a
     *	complete swapped-out process: the ordinary swapin path brings it
     *	back when it is next scheduled, by which time the child has run
     *	and the room exists.
     */
    if (fork_stage)
    {
        ptptr parent = udata.u_ptab;
        for (int i=0; i<blocks; i++)
        {
            struct mapentry* b = find_block(srcslot, i);
            if (!b)
                panic("missing block");
            memcpy((void *)(fork_stage + (uint32_t)i * BLOCKSIZE),
                   (void*)PROGBASE + (b - allocation_map)*BLOCKSIZE,
                   BLOCKSIZE);
            b->slot = destslot;
        }
        swapaddr[srcslot] = fork_stage;
        sysinfo.swapusedk += ((unsigned)blocks * BLOCKSIZE) >> 10;
        parent->p_page = 0;
        parent->p_page2 = 1;    /* "swapped" - the address is ours */
        fork_stage = 0;
        return;
    }

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
/* OFF since the corruption was found. It did its job: it never fired,
 * which is what ruled out the swap device and the XIP write-back cache
 * and sent the search to the filesystem, where the fault turned out to
 * be f_trunc leaving i_addr[19] pointing at a freed block. Costs one
 * sum per swap slot plus a pass over each 4K block; turn it back on if
 * swap integrity is ever in question again. */
#define SWAP_TRIPWIRE 0
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

/*
 *	Swap is a PSRAM ALLOCATION THE SIZE OF THE PROCESS.
 *
 *	It used to be a fixed slot on a block device:
 *
 *	    #define SWAP_SIZE ((PROGSIZE >> BLKSHIFT) + UDATA_BLKS)
 *
 *	256K per process whatever the process was, so a 12K shell
 *	consumed 256K and 6912K of PSRAM bought 27 of them.  Worse, it is
 *	why PROGSIZE exists at all: pagemap_realloc refuses to grow a
 *	process past it because "swapout would overwrite the neighbouring
 *	slot".  That 256K ceiling was the slot size, not an address-space
 *	limit - an 8-bit machine's decision inherited by a 32-bit one.
 *
 *	Now each swapped process gets exactly the bytes it occupies, out
 *	of the same heap as everything else, and the transfer is a memcpy
 *	because the region is already mapped.  No block device, no
 *	swapon, no LBA arithmetic, no bounds check to get wrong.
 *
 *	The address is kept here, indexed by process slot, because
 *	p_page2 is a uint16_t and cannot hold a pointer.  p_page2 keeps
 *	its "is it swapped" role for the generic kernel; where the bytes
 *	are is the platform's business.  (swapaddr[] itself is declared up
 *	beside pagemap_alloc, which now also has to set it.)
 */

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

    int slot = get_slot(p);
    int blocks = get_proc_size_blocks(p);
    uint32_t region;
    unsigned bytes = (unsigned)blocks * BLOCKSIZE;

    if (!bytes)
        return ENOMEM;
    /* NOT zeroed: every byte is overwritten by the copy below, and a
       200K memset through the QMI at 12MB/s would add 16ms to every
       swapout for nothing. */
    region = arena_alloc_raw(p, bytes);
    if (!region)
        return ENOMEM;

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
        memcpy((void *)(region + (uint32_t)i * BLOCKSIZE), p, BLOCKSIZE);

        b->slot = b->block = 0xff;
    }
    swapaddr[slot] = region;
    /* swapmap_init used to keep these, in units of half a fixed slot.
     * There are no slots now, so account the real bytes: what `free`
     * shows is then the truth rather than a slot count. */
    sysinfo.swapusedk += bytes >> 10;
#if SWAP_TRIPWIRE
	swap_sum[slot] = sum;
#endif

	p->p_page = 0;
	p->p_page2 = 1;			/* "swapped" - the address is ours */
	return 0;
}

/*
 * Swap ourself in: must be on the swap stack when we do this
 */
void swapin(ptptr p, uint16_t map)
{
    int slot = get_slot(p);
    uint32_t region = swapaddr[slot];
#if SWAP_TRIPWIRE
    uint32_t sum = 0;
#endif

    used(map);				/* the address is ours, not a slot */
    if (!region)
        panic("swapin: no region");

    int blocks = get_proc_size_blocks(p);
    for (int i=0; i<blocks; i++)
    {
        struct mapentry* b = find_free_block(p);
        int blockindex;
        void* p;

        /* find_free_block returning NULL is survivable for brk (the
         * caller reports ENOMEM) but not here: dereferencing it turns
         * into a copy through a wild constant pointer - 4K into the
         * same innocent memory every time, which is exactly the class
         * of corruption that must be a panic. */
        if (!b)
            panic("swapin: no memory");
        blockindex = b - allocation_map;
        p = (void*)PROGBASE + blockindex*BLOCKSIZE;

        memcpy(p, (void *)(region + (uint32_t)i * BLOCKSIZE), BLOCKSIZE);
#if SWAP_TRIPWIRE
        sum = sum_words(sum, p, BLOCKSIZE);
#endif

        b->slot = slot;
        b->block = i;
    }
#if SWAP_TRIPWIRE
    if (sum != swap_sum[slot]) {
        kprintf("\nswap tripwire: pid %d slot %d came back changed"
                " (%x, wrote %x)\n",
                p->p_pid, slot, sum, swap_sum[slot]);
        panic("swapsum");
    }
#endif

    p->p_page = 1;
    p->p_page2 = 0;
    /* Give the region back.  The old code returned a slot to swapmap
     * here for the same reason, and forgetting it leaked one per
     * swap-in until the pool ran dry - found 2026-07-31 chasing a shell
     * crash at a constant PC.  A leak is now a leak of real memory, so
     * it matters at least as much. */
    arena_free(p, region);
    swapaddr[slot] = 0;
    sysinfo.swapusedk -= (uint16_t)(((uint32_t)blocks * BLOCKSIZE) >> 10);
}

/*
 *	Total "swap" for sysinfo: there is no swap device, so the honest
 *	number is the heap it comes out of.  Called once the PSRAM size
 *	is known.
 */
void swap_report_size(void)
{
	uint32_t span = arena_pool_top() - arena_pool_base();

	sysinfo.swapk = (uint16_t)(span >> 10);
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

