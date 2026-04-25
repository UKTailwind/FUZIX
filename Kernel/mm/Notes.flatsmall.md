# Flat / Small Memory Manager

This memory manager is designed for systems with more limited memory and a
relatively slow storage subsystem (so simply dumping everything on and off
disk and keeping one task in memory at once makes less sense).

## How It Works

At start up time we measure the number of blocks of PAGE_SIZE that will fit
in the given address range. Each page after the base up to the last full
page that will fit then gets assigned a slot number fromn 0 .. top_bank - 1.

These are distinct from pages. Each page we can fit in the disk swap area
is assigned a number from 0 upwards and indicates a disk offset for that
block. To avoid complications there must be page space on disk.

We keep track of memory in the pagemap array which is sized by the number
of banks and the number of processes. For any process it tells us the page
numbers that are in use for this task.

A separate array mem[] keeps track of the page and age for each slot. Given
a slot it can tell you the page number present and manages the age.

A second array rmap[] tracks the location of each page so we can find out
if the page is in memory (and where) or is on disk.

The bigger the page size the smaller the tables, but the less efficient use
of memory.

Pages can be swapped (on disk), empty (blank), free (unused) or in memory
in which case the rmap will tell us their location. They can also be marked
locked and this is used to avoid pages vanishing whilst in the current
process or during things like copies.

Shared pages are also supported. This is handled by tracking the top of
code only pages in memory.

## UData handling

The udata for the running task ends up at the top of the memory in the
uppermost block. This enables it to be paged out and saves us a lot of
precious memory on some systems. There are however consequences of this
swapping

When a new task is being forked and during we boot we run with a mostly
fake udata that has a stack and a few words used by the swap and disk
routines. In addition the udata provided to make_proc is not at the normal
address but it will run at the normal address. This means the stack pointer
needed is not the one that will be computed by building a stack frame on the
child stack. Hence a routine remap_sp is provided to deal with this.
