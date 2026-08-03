# libc: fread's direct read desynchronises fseek's buffer fast path

Found 2026-08-03 on the rpipico port (armm0), but the code is shared:
`Library/libs/fread.c` and `Library/libs/rewind.c` are the Linux-8086
stdio and every Fuzix target uses them.

## The bug

`fread` has two paths.  For a request that fits the buffer it memcpys
and advances `bufpos`.  For a larger one it copies whatever is buffered
and then reads the remainder **straight into the caller's buffer**:

    len = read(fp->fd, (char *) buf + got, bytes - got);

That advances the file DESCRIPTOR without touching `bufread`.  From
then on the descriptor position and `bufread` disagree, by however many
bytes that read moved.

`fseek`, in rewind.c, has a fast path that seeks inside the buffer
without touching the descriptor.  It works out where the buffer starts
in the file as

    fpos + (fp->bufstart - fp->bufread)      /* fpos = lseek(fd, 0, SEEK_CUR) */

which is only true while the descriptor position corresponds to
`bufread`.  After a direct read it does not, so the computed window is
wrong by the drift, and a seek to an offset that falls in the WRONG
window silently sets `bufpos` to the wrong place.  The caller then
reads real, well-formed, wrong data - no error, no short count.

## Reproducing

Any program that reads a file with `fread` only (never `fgetc`, which is
the only function that sets `__MODE_READING`), where:

1. some read is large enough to take the direct path, and
2. a later `fseek(..., SEEK_SET)` goes back to an offset that is still
   inside the buffer's apparent - but now wrong - window.

Concretely, what found it: bcrun loading a 305-byte object with
BUFSIZ 256.  Header, code and data came out of the buffer, leaving
`bufread` at file offset 256.  The symbol table and string table
overran it, so those took the direct path and left the descriptor at
305.  `fseek(f, 187, SEEK_SET)` then computed the window as
305 - 256 = 49, decided 49 <= 187 < 305 was inside the buffer, and set
`bufpos` to buffer offset 138.  The buffer held file bytes 0..255, so
the next read returned file offset 138 - 49 bytes early, in the middle
of the code section.  The bytes there were parsed as a relocation
record, giving a 16-bit symbol index of about 65,000, and the loader
indexed its symbol table with it and died on a wild address.

It is size dependent, which is why it hid for so long: a large file's
reads leave the buffer empty, so the fast path correctly declines and
the slow path (fflush + lseek) runs.  Every 100K object this port loads
was fine; the first 305-byte one was not.

## Fix

Say what is true - after a direct read the buffer holds nothing:

    fp->bufpos = fp->bufread = fp->bufstart;

With the three equal, the computed window is empty and the fast path
can never wrongly claim an offset.

An alternative is to have `fread` set `__MODE_READING` the way `fgetc`
does, so `fflush`'s compensating `lseek` runs; that is a larger change
in behaviour and this one is sufficient.

## Verified

rpipico/armm0, PC2 hardware: all 29 C samples in
`Applications/CC/hosttest/samples` compiled by the board's own cc and
run under bcrun now produce output byte-identical to the gcc
references.  Before the fix, `sw2` (a three-case switch, 305-byte
object) hard-faulted the machine.

Host gates unaffected: they run on glibc, which does not have this bug.
