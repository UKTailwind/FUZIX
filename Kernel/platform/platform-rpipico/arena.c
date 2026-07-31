/*
 *	The PSRAM arena: a region of PSRAM outside the process image,
 *	handed to userland on request.  See PC3-PSRAM-ARENA.md for the
 *	full design; the short form is that the 256K process ceiling is
 *	a swap-slot size, not an address-space limit, and bulk data that
 *	lives out here is never context-switch copied, never swapped and
 *	never forked.
 *
 *	The pool sits between the PSRAM disc and the kernel's own
 *	reserve at the top:
 *
 *	    [ 0            .. disc end   )   PSRAM disc / swap
 *	    [ disc end     .. top - 64K  )   arena pool (this file)
 *	    [ top - 64K    .. top        )   kernel (lineedit)
 *
 *	Contracts, stated where they can be read:
 *	  - Not protected.  No MMU, no MPU region: a wild pointer into
 *	    the window corrupts a neighbour or the swap device silently.
 *	  - fork: the arena stays with the parent.  The child's image
 *	    still contains the raw pointer and nothing can fault its
 *	    dereference - allocate after exec, or don't fork while
 *	    holding one.
 *	  - exec and exit release everything the process owns.
 *	  - Regions are 4K-granular and zeroed on allocation (PSRAM
 *	    survives a warm reset; the previous run's contents are
 *	    nobody's business - the boot-udata bug taught that).
 */

#include <kernel.h>
#include <kdata.h>
#include <printf.h>
#include "config.h"
#include "psram.h"

#define ARENA_SLOTS	8
#define ARENA_ALIGN	4096u

static struct arena_slot {
	struct p_tab *owner;			/* NULL = free */
	uint32_t base;
	uint32_t len;
} slots[ARENA_SLOTS];

uint32_t arena_len = PSRAM_ARENA_DEFAULT;

uint32_t arena_pool_base(void)
{
	return PSRAM_BASE + psram_size - PSRAM_RESERVE - arena_len;
}

static uint32_t arena_pool_top(void)
{
	return PSRAM_BASE + psram_size - PSRAM_RESERVE;
}

/*
 *	First fit over the gaps between allocations.  Eight slots do
 *	not deserve cleverness: gather, sort by base, walk the holes.
 */
uint32_t arena_alloc(struct p_tab *owner, uint32_t len)
{
	struct arena_slot *order[ARENA_SLOTS];
	struct arena_slot *fs = NULL;
	unsigned n = 0, i, j;
	uint32_t at, top = arena_pool_top();

	if (!psram_size || !arena_len || len == 0)
		return 0;
	len = (len + ARENA_ALIGN - 1) & ~(ARENA_ALIGN - 1);

	for (i = 0; i < ARENA_SLOTS; i++) {
		if (!slots[i].owner) {
			fs = &slots[i];
			continue;
		}
		for (j = n; j > 0 && order[j-1]->base > slots[i].base; j--)
			order[j] = order[j-1];
		order[j] = &slots[i];
		n++;
	}
	if (!fs)
		return 0;

	at = arena_pool_base();
	for (i = 0; i < n; i++) {
		if (order[i]->base - at >= len)
			break;
		at = order[i]->base + order[i]->len;
	}
	if (top - at < len)
		return 0;

	fs->owner = owner;
	fs->base = at;
	fs->len = len;
	memset((void *)at, 0, len);
	return at;
}

int arena_free(struct p_tab *owner, uint32_t base)
{
	unsigned i;

	for (i = 0; i < ARENA_SLOTS; i++) {
		if (slots[i].owner == owner && slots[i].base == base) {
			slots[i].owner = NULL;
			return 0;
		}
	}
	return -1;
}

/* Everything a dying (or exec-ing) process owns comes back. */
void arena_release(struct p_tab *owner)
{
	unsigned i;

	for (i = 0; i < ARENA_SLOTS; i++)
		if (slots[i].owner == owner)
			slots[i].owner = NULL;
}

/*
 *	For the platform's valaddr (misc.c): is [base, base+size) inside
 *	an arena the CURRENT process owns?  Returns the usable length
 *	from base (clamped to the arena's end), 0 if not owned.  Without
 *	this, any syscall handed an arena buffer - a compiler reading
 *	source into its tables, an editor writing its PSRAM text buffer
 *	out - dies with EFAULT, and the facility cannot do I/O.
 */
uint32_t arena_valaddr(uint32_t b, uint32_t size)
{
	unsigned i;

	for (i = 0; i < ARENA_SLOTS; i++) {
		uint32_t top;
		if (slots[i].owner != udata.u_ptab)
			continue;
		top = slots[i].base + slots[i].len;
		if (b < slots[i].base || b >= top)
			continue;
		if (b + size > top)
			size = top - b;
		return size;
	}
	return 0;
}

void arena_stat(uint32_t *total, uint32_t *freeb, uint32_t *largest)
{
	unsigned i;
	uint32_t used = 0;

	*total = arena_len;
	for (i = 0; i < ARENA_SLOTS; i++)
		if (slots[i].owner)
			used += slots[i].len;
	*freeb = arena_len - used;
	/* Largest single hole, by probing: cheap at eight slots. */
	{
		struct arena_slot *order[ARENA_SLOTS];
		unsigned n = 0, j;
		uint32_t at = arena_pool_base(), big = 0;

		for (i = 0; i < ARENA_SLOTS; i++) {
			if (!slots[i].owner)
				continue;
			for (j = n; j > 0 && order[j-1]->base > slots[i].base; j--)
				order[j] = order[j-1];
			order[j] = &slots[i];
			n++;
		}
		for (i = 0; i < n; i++) {
			if (order[i]->base - at > big)
				big = order[i]->base - at;
			at = order[i]->base + order[i]->len;
		}
		if (arena_pool_top() - at > big)
			big = arena_pool_top() - at;
		*largest = big;
	}
}
