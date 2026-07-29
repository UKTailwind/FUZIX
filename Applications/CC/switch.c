#include "compiler.h"

/* Will need typing for the largest integral type TODO */
cval_t switch_table[NUM_SWITCH];
cval_t *switch_next = switch_table;

/*
 *	When we finish a switch block off we write the table out. We could
 *	do things here like enble binary search but for now we don't and
 *	for some types it's tricky
 *	TODO;
 */
void switch_done(unsigned tag, cval_t *oldptr, unsigned type)
{
    unsigned count = 0;
    cval_t *p = oldptr;

    header(H_SWITCHTAB, tag, switch_next - oldptr);
    /* Table */
    while(p < switch_next) {
        put_typed_constant(type, *p++);
        put_typed_case(tag, ++count);
    }
    /* Default */
    put_typed_case(tag, 0);
    footer(H_SWITCHTAB, tag, 0);
    switch_next = oldptr;
}

cval_t *switch_alloc(void)
{
    return switch_next;
}

void switch_add_node(cval_t value)
{
    if (switch_next == &switch_table[NUM_SWITCH])
        fatal("switch table full");
    *switch_next++ = value;
}
