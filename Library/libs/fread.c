/* stdio.c
 * Copyright (C) 1996 Robert de Bath <rdebath@cix.compulink.co.uk>
 * This file is part of the Linux-8086 C library and is distributed
 * under the GNU Library General Public License.
 */

/* This is an implementation of the C standard IO package. */

#include "stdio-l.h"

/*
 * fread will often be used to read in large chunks of data calling read()
 * directly can be a big win in this case. Beware also fgetc calls this
 * function to fill the buffer.
 *
 * This ignores __MODE__IOTRAN; probably exactly what you want.
 * (It _is_ what fgetc wants)
 */
int fread(void *buf, size_t size, size_t nelm, FILE * fp)
{
	register int len, v;
	unsigned bytes, got = 0;

	if (!buf || !size || !nelm || !fp)
		return 0;
	v = fp->mode;
	/* Want to do this to bring the file pointer up to date */
	if (v & __MODE_WRITING)
		fflush(fp);
	/* Can't read or there's been an EOF or error then return zero */
	if ((v & (__MODE_READ | __MODE_EOF | __MODE_ERR)) != __MODE_READ)
		return 0;
	/* This could be long, doesn't seem much point tho */
	bytes = size * nelm;
	len = fp->bufread - fp->bufpos;
	if (len >= bytes) {	/* Enough buffered */
		memcpy(buf, fp->bufpos, bytes);
		fp->bufpos += bytes;
		return nelm;
	} else if (len > 0) {	/* Some buffered */
		memcpy(buf, fp->bufpos, len);
		got = len;
		fp->bufpos += len;
	}
	/* Need more; do it with a direct read */
	len = read(fp->fd, (char *) buf + got, bytes - got);
	/*
	 * The read moved the DESCRIPTOR without moving bufread, and
	 * fseek's fast path assumes the two agree - it works out where
	 * the buffer starts in the file as fpos + (bufstart - bufread).
	 * Leave the buffer non-empty here and that sum is wrong by
	 * however far this read went, so a later seek back into the
	 * apparent buffer window lands at the wrong file offset and
	 * quietly returns the wrong bytes.  It cost a day: bcrun read a
	 * 305 byte object's header, code and data out of the buffer,
	 * direct-read the symbols past the end of it, then seeked back
	 * to the fixups and got data from 49 bytes too early - a garbage
	 * symbol index and a wild load.  Large files never showed it,
	 * because their reads leave the buffer empty and the fast path
	 * declines.
	 *
	 * So say what is true: the buffer holds nothing now.
	 */
	fp->bufpos = fp->bufread = fp->bufstart;
	/* Possibly for now _or_ later */
	if (len < 0) {
		fp->mode |= __MODE_ERR;
		len = 0;
	} else if (len == 0)
		fp->mode |= __MODE_EOF;
	return (got + len) / size;
}
