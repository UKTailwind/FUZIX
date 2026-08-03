/*
 *	The PSRAM arena: memory outside the process image, handed to
 *	userland on request.  See PC3-PSRAM-ARENA.md for the original
 *	design; the short form is that the 256K process ceiling is a
 *	swap-slot size, not an address-space limit, and bulk data that
 *	lives out here is never context-switch copied, never swapped and
 *	never forked.
 *
 *	THE ALLOCATOR IS NEWLIB'S.  This file used to carry a first-fit
 *	over eight {owner, base, len} slots.  malloc/free/realloc over the
 *	PSRAM window replaces it, because:
 *
 *	  - realloc is the point.  Every client here guesses a maximum
 *	    once and lives with it: mmbc reserves 768K of a 1M pool
 *	    whatever the program needs, cc2 takes a fixed carve, bcrun
 *	    gives a translated BASIC program 48K of address space in
 *	    total.  Growable allocation is what removes those ceilings,
 *	    and growable allocation is exactly what should not be
 *	    hand-written.
 *	  - it already exists and is tested.  The SDK's _sbrk is __weak,
 *	    so pointing the C library's heap at PSRAM is an override, not
 *	    a port.  The kernel linked no heap of its own before this
 *	    (nm showed no malloc, free, realloc or _sbrk), so nothing
 *	    competes for it.
 *
 *	It costs 2432 bytes of SRAM - the kernel is PICO_COPY_TO_RAM, so
 *	library code is RAM-resident - which is why TOTALMEM went from
 *	316K to 312K.  The pool loses one 4K chunk; PROGSIZE caps a
 *	process at 256K so the top of it was unreachable anyway.
 *
 *	What newlib cannot do is OWNERSHIP: it has no idea which process
 *	asked, so it cannot give the memory back when that process exits.
 *	That is the whole of what remains here - a table of (owner, base,
 *	len) consulted on release and by valaddr.
 *
 *	Contracts, unchanged and stated where they can be read:
 *	  - Not protected.  No MMU, no MPU region in use: a wild pointer
 *	    into the window corrupts a neighbour silently.  Note newlib
 *	    keeps its headers IN BAND, beside the data, so an overrun can
 *	    now damage the allocator itself and not merely the next
 *	    block's contents.  That is the price of the standard tool
 *	    over a table of extents.
 *	  - fork: the arena stays with the parent.  The child's image
 *	    still holds the raw pointer and nothing can fault its
 *	    dereference - allocate after exec, or don't fork holding one.
 *	  - exec and exit release everything the process owns.
 *	  - Zeroed on allocation.  PSRAM survives a warm reset and the
 *	    previous run's contents are nobody's business; the boot-udata
 *	    bug taught that.
 *	  - realloc MAY MOVE the block.  A client that has handed out
 *	    interior pointers cannot survive that - mmbc's bump allocator
 *	    is exactly such a client - so growing is opt-in per caller,
 *	    never done behind anyone's back.
 */

#include <kernel.h>
#include <kdata.h>
#include <printf.h>
#include <stdlib.h>
#include "config.h"
#include "psram.h"

/*
 * The kernel mangles _sbrk to f_sbrk (mangle.h) so its own syscall
 * entry points do not collide with the C library's names.  We want the
 * REAL one: this is the C library's heap hook, overriding the SDK's
 * __weak version, not a Fuzix syscall.
 */
#define MANGLED 0
#include "mangle.h"

uint32_t arena_len = PSRAM_ARENA_DEFAULT;

/*
 *	The heap's span, and it must stay ABOVE the disc.
 *
 *	Not psram_static_len(): that is the bottom of the window, which
 *	is where the SWAP DISC starts.  Basing the heap there would have
 *	malloc handing out swap blocks - the same class of fault as the
 *	f_trunc bug, and just as silent.  The disc keeps
 *	[static end, this), lineedit keeps the top PSRAM_RESERVE, and the
 *	heap is what the arena always was.
 *
 *	Collapsing these three into one allocator is the next step, and
 *	it needs the disc to stop being a fixed extent first.
 */
uint32_t arena_pool_base(void)
{
	return PSRAM_BASE + psram_size - PSRAM_RESERVE - arena_len;
}

uint32_t arena_pool_top(void)
{
	return PSRAM_BASE + psram_size - PSRAM_RESERVE;
}

/*
 *	_sbrk: the C library's heap, pointed at PSRAM.
 *
 *	Overrides the SDK's __weak version in newlib_interface.c, which
 *	hands out SRAM between `end` and __StackLimit.  The kernel has no
 *	SRAM to give, and the whole purpose is to allocate from the 8 MiB
 *	that is not SRAM.
 *
 *	Bounded by the existing split, so this changes the allocator
 *	without moving any boundary: the disc keeps its blocks and
 *	lineedit keeps its 64K at the top.
 */
void *_sbrk(int incr)
{
	static uint32_t brk;
	uint32_t prev;

	if (!psram_size)
		return (void *)-1;
	if (!brk)
		brk = arena_pool_base();
	prev = brk;
	if (incr < 0) {
		if ((uint32_t)(-incr) > brk - arena_pool_base())
			return (void *)-1;
	} else if (arena_pool_top() - brk < (uint32_t)incr)
		return (void *)-1;
	brk += incr;
	return (void *)prev;
}

/*
 *	Ownership.  One entry per live allocation; the table is itself
 *	the first thing allocated, so it lives in PSRAM and costs no
 *	SRAM.  Sixty-four where eight extents were not enough: an entry
 *	is a record, not a region, so a process may hold many.
 */
#define ARENA_OWN 64

struct arena_own {
	struct p_tab *owner;		/* NULL = free entry */
	uint32_t base;
	uint32_t len;
};

static struct arena_own *own;

static void arena_own_init(void)
{
	if (own || !psram_size)
		return;
	own = malloc(ARENA_OWN * sizeof(struct arena_own));
	if (own)
		memset(own, 0, ARENA_OWN * sizeof(struct arena_own));
}

static struct arena_own *arena_find(struct p_tab *o, uint32_t base)
{
	unsigned i;

	if (!own)
		return NULL;
	for (i = 0; i < ARENA_OWN; i++)
		if (own[i].owner == o && own[i].base == base)
			return &own[i];
	return NULL;
}

uint32_t arena_alloc(struct p_tab *owner, uint32_t len)
{
	unsigned i;
	void *p;

	if (!psram_size || len == 0)
		return 0;
	arena_own_init();
	if (!own)
		return 0;
	for (i = 0; i < ARENA_OWN; i++)
		if (!own[i].owner)
			break;
	if (i == ARENA_OWN)
		return 0;		/* out of records, not out of memory */

	p = malloc(len);
	if (!p)
		return 0;
	memset(p, 0, len);
	own[i].owner = owner;
	own[i].base = (uint32_t)p;
	own[i].len = len;
	return (uint32_t)p;
}

/*
 *	Grow (or shrink) an allocation the caller already owns.  Returns
 *	the new base, which MAY DIFFER: newlib moves the block when it
 *	cannot extend in place.  0 leaves the original untouched.
 *
 *	This is the call that lets a client stop guessing its maximum.
 */
uint32_t arena_realloc(struct p_tab *owner, uint32_t base, uint32_t len)
{
	struct arena_own *e = arena_find(owner, base);
	uint32_t was;
	void *p;

	if (!e || len == 0)
		return 0;
	was = e->len;
	p = realloc((void *)base, len);
	if (!p)
		return 0;
	if (len > was)
		memset((uint8_t *)p + was, 0, len - was);
	e->base = (uint32_t)p;
	e->len = len;
	return (uint32_t)p;
}

int arena_free(struct p_tab *owner, uint32_t base)
{
	struct arena_own *e = arena_find(owner, base);

	if (!e)
		return -1;
	free((void *)base);
	e->owner = NULL;
	return 0;
}

/* Everything a dying (or exec-ing) process owns comes back. */
void arena_release(struct p_tab *owner)
{
	unsigned i;

	if (!own)
		return;
	for (i = 0; i < ARENA_OWN; i++) {
		if (own[i].owner == owner) {
			free((void *)own[i].base);
			own[i].owner = NULL;
		}
	}
}

/*
 *	For the platform's valaddr (misc.c): is [base, base+size) inside
 *	an allocation the CURRENT process owns?  Returns the usable
 *	length from base (clamped to its end), 0 if not owned.  Without
 *	this, any syscall handed an arena buffer - a compiler reading
 *	source into its tables, an editor writing its PSRAM text buffer
 *	out - dies with EFAULT, and the facility cannot do I/O.
 */
uint32_t arena_valaddr(uint32_t b, uint32_t size)
{
	unsigned i;

	if (!own)
		return 0;
	for (i = 0; i < ARENA_OWN; i++) {
		uint32_t top;
		if (own[i].owner != udata.u_ptab)
			continue;
		top = own[i].base + own[i].len;
		if (b < own[i].base || b >= top)
			continue;
		if (b + size > top)
			size = top - b;
		return size;
	}
	return 0;
}

/*
 *	Reported by PSRAMIOC_STAT.  "free" is the span less what this
 *	file has handed out; newlib's own free lists are not walked, so a
 *	pool with holes reports optimistically.  "largest" is the honest
 *	answer to "will my next request fit": what remains above the
 *	break, which any fresh allocation can certainly use.
 */
void arena_stat(uint32_t *total, uint32_t *freeb, uint32_t *largest)
{
	unsigned i;
	uint32_t used = 0;
	uint32_t span = arena_pool_top() - arena_pool_base();
	void *p;

	*total = span;
	if (own)
		for (i = 0; i < ARENA_OWN; i++)
			if (own[i].owner)
				used += own[i].len;
	*freeb = span - used;
	p = _sbrk(0);
	*largest = (p == (void *)-1) ? 0 : arena_pool_top() - (uint32_t)p;
}
