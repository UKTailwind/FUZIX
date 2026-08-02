#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include "compiler.h"

/*
 *	Read and match against the token stream. Need to move from stdio
 *	eventually
 */

#define NO_TOKEN	0xFFFF		/* An unused value */


/*
 *	Simple block buffer read. We use 128 byte records so we can make
 *	this work in CP/M. For CP/M we'll also need to write some kind of
 *	'end of file' token
 */

static unsigned char inbuf[128];
static unsigned char *inptr;
static int inlen;

/* Read the next block. Hopefully this is the only routine we need to
   swap for CP/M etc */
static int in_record(void)
{
	inptr = inbuf;
	return read(0, inbuf, 128);
}

static int in_byte(void)
{
	if (inlen == 0)
		inlen = in_record();
	if (inlen--)
		return *inptr++;
	inlen = 0;
	return EOF;
}

/*
 *	Which names are used more than once.
 *
 *	A function with external linkage has to be generated whatever we
 *	think of it: something we cannot see may call it. A file scope
 *	static is different - everything that could possibly reach it is
 *	in this one translation unit - so if its name occurs exactly once
 *	in the whole token stream, that occurrence being its own
 *	definition, then nothing calls it, nothing takes its address, and
 *	the code need never be generated at all. There is no linker on
 *	this target to work that out later.
 *
 *	Counting names rather than resolving references costs one extra
 *	pass over a file we already have open, and it errs the safe way:
 *	a forward declaration, a recursive call, even an unrelated struct
 *	member of the same name all push the count up and the function is
 *	kept. It can waste code, never lose it.
 *
 *	Two saturating bits per name - none, one, more. A name past the
 *	end of the table counts as "more", so a program with more names
 *	than this loses the optimisation and nothing else.
 */
#define NAMES_TRACKED	16384
static unsigned char name_count[NAMES_TRACKED / 4];
static unsigned prescan_done;

static void name_bump(unsigned id)
{
	unsigned i = id - T_SYMBOL;
	unsigned sh;

	if (i >= NAMES_TRACKED)
		return;
	sh = (i & 3) * 2;
	if (((name_count[i >> 2] >> sh) & 3) != 3)
		name_count[i >> 2] += 1 << sh;
}

unsigned name_used_once(unsigned id)
{
	unsigned i = id - T_SYMBOL;

	if (!prescan_done || id < T_SYMBOL || i >= NAMES_TRACKED)
		return 0;
	return ((name_count[i >> 2] >> ((i & 3) * 2)) & 3) == 1;
}

/*
 *	Walk the token stream once counting names, then rewind. The shape
 *	of this loop has to match next_token() and copy_string() exactly
 *	or it falls out of step: tokens are two bytes, five of them carry
 *	a four byte value and three carry eight, T_LINE carries a line
 *	number and sometimes a file name, and T_STRING carries a byte
 *	stream in which 255 quotes the byte after it.
 *
 *	If stdin will not seek - someone has piped into cc1 rather than
 *	giving it the file the driver does - the pass is simply skipped
 *	and every static is generated as before.
 */
void prescan_names(void)
{
	int c;
	unsigned t, i;

	if (lseek(0, 0, SEEK_CUR) < 0)
		return;

	for (;;) {
		c = in_byte();
		if (c == EOF)
			break;
		t = c;
		c = in_byte();
		if (c == EOF)
			break;
		t |= c << 8;

		if (t >= T_SYMBOL) {
			name_bump(t);
			continue;
		}
		switch (t) {
		case T_LINE:
			in_byte();
			c = in_byte();
			/* bit 15 of the line number: a file name follows */
			if (c != EOF && (c & 0x80))
				for (i = 0; i < 32; i++)
					if (in_byte() <= 0)
						break;
			break;
		case T_INTVAL:
		case T_UINTVAL:
		case T_LONGVAL:
		case T_ULONGVAL:
		case T_FLOATVAL:
			for (i = 0; i < 4; i++)
				in_byte();
			break;
		case T_LONGLONGVAL:
		case T_ULONGLONGVAL:
		case T_DOUBLEVAL:
			for (i = 0; i < 8; i++)
				in_byte();
			break;
		case T_STRING:
			while ((c = in_byte()) > 0)
				if (c == 0xFF)
					in_byte();
			break;
		}
	}

	if (lseek(0, 0, SEEK_SET) < 0)
		fatal("seek error");
	inlen = 0;
	prescan_done = 1;
}

static unsigned char outbuf[128];
static unsigned char *outptr = outbuf;
static unsigned int outlen;
static unsigned int outrecord = 0;

/*
 *	Set while a function that will not be generated is parsed. It
 *	still has to be parsed - for its errors, and to consume its
 *	tokens - so the output side is turned off rather than the parse
 *	being skipped. Everything cc1 emits goes through out_byte,
 *	out_block and out_seek, so these three are the whole of it.
 */
unsigned out_off;

void out_write(void)
{
	if (lseek(1, outrecord * 128UL, SEEK_SET) < 0)
		fatal("seek error");
	if (outlen && write(1, outbuf, outlen) != outlen)
		fatal("write error");
	outlen = 0;
	outptr = outbuf;
}

/* Again try and isolate the block I/O into two tiny routines */
void out_flush(void)
{
	out_write();
	outrecord++;
}

/* Read a record. We use this in situations where we need to rewind and
   update headers */
static void out_record_read(unsigned record)
{
	if (lseek(1, record * 128UL, SEEK_SET) < 0)
		fatal("seek error");
	if (read(1, outbuf, 128) < 0)
		fatal("read error");
	outrecord = record;
}

/* Report the current record/offset */
unsigned long out_tell(void)
{
	return (((unsigned long)outrecord) << 8) | outlen;
}

/* Go to a given record/offset from before */
void out_seek(unsigned long pos)
{
	if (out_off)
		return;
	out_write();
	out_record_read(pos >> 8);
	outlen = pos & 0xFF;
	outptr = outbuf + outlen;
}

/* Add bytes at the current position */
void out_byte(unsigned char c)
{
	if (out_off)
		return;
	if (outlen == 128)
		out_flush();
	*outptr++ = c;
	outlen++;
}

void out_block(void *pv, unsigned len)
{
	unsigned char *p = pv;
	if (out_off)
		return;
	while(len) {
		unsigned n;

		/* Flush any full record */
		if (outlen == 128)
			out_flush();
		/* Fill up what we can */
		n = 128 - outlen;
		if (n > len)
			n = len;
		memcpy(outptr, p, n);
		outptr += n;
		outlen += n;
		p += n;
		len -= n;
	}
}

char filename[33];

unsigned line_num;

cval_t token_value;
unsigned token;
unsigned last_token = NO_TOKEN;

unsigned tokbyte(void)
{
	unsigned c = in_byte();
	if (c == EOF) {
		error("corrupt stream");
		exit(1);
	}
	return c;
}

void next_token(void)
{
	int c;

	/* Handle pushed back tokens */
	if (last_token != NO_TOKEN) {
		token = last_token;
		last_token = NO_TOKEN;
		return;
	}

	c = in_byte();
	if (c == EOF) {
		token = T_EOF;
//        printf("*** EOF\n");
		return;
	}
	token = c;
	c = in_byte();
	if (c == EOF) {
		token = T_EOF;
		return;
	}
	token |= (c << 8);

	if (token == T_LINE) {
		char *p = filename;

		line_num = tokbyte();
		line_num |= tokbyte() << 8;

		if (line_num & 0x8000) {
			line_num &= 0x7FFF;
			for (c = 0; c < 32; c++) {
				*p = tokbyte();
				if (*p == 0)
					break;
				p++;
			}
			*p = 0;
		}
		next_token();
		return;
	}

	if (token == T_INTVAL || token == T_LONGVAL || token == T_UINTVAL
	    || token == T_ULONGVAL || token == T_FLOATVAL) {
		token_value = tokbyte();
		token_value |= (cval_t)tokbyte() << 8;
		token_value |= (cval_t)tokbyte() << 16;
		token_value |= (cval_t)tokbyte() << 24;
	}
	/* The wide forms carry eight bytes, not four */
	else if (token == T_LONGLONGVAL || token == T_ULONGLONGVAL
		 || token == T_DOUBLEVAL) {
		unsigned i;
		token_value = 0;
		for (i = 0; i < 8; i++)
			token_value |= (cval_t)tokbyte() << (8 * i);
	}
}

/*
 * You can only push back one token and it must not have attached data. This
 * works out fine because we only ever need to push back a name when processing
 *  labels
 */
void push_token(unsigned t)
{
	last_token = token;
	token = t;
}

/*
 *	Try and move on a bit so that we don't generate a wall of errors for
 *	a single mistake
 */
void junk(void)
{
	while (token != T_EOF && token != T_SEMICOLON)
		next_token();
	next_token();
}

/*
 *	If the token is the one expected then consume it and return 1, if not
 *	do not consume it and leave 0. This lets us write things like
 *
 *	if (match(T_STAR)) { ... }
 */
unsigned match(unsigned t)
{
	if (t == token) {
		next_token();
		return 1;
	}
	return 0;
}

void need_semicolon(void)
{
	if (!match(T_SEMICOLON)) {
		error("missing semicolon");
		junk();
	}
}

/* This can only be used if the token is a single character token. That turns
   out to be sufficient for C so there is no need for anything fancy here */
void require(unsigned t)
{
	if (!match(t))
		errorc(t, "expected");
}

unsigned symname(void)
{
	unsigned t;
	if (token < T_SYMBOL)
		return 0;
	t = token;
	next_token();
	return t;
}

/*
 *	This is ugly and we need to review how we handle it
 */

static unsigned char pad_zero[2] = { 0xFF, 0xFE };

unsigned copy_string(unsigned label, unsigned maxlen, unsigned pad, unsigned lit)
{
	unsigned c;
	unsigned l = 0;

	header(H_STRING, label, lit);

	/* Copy the encoding string as is */
	while((c = tokbyte()) != 0) {
		if (l < maxlen) {
			out_byte(c);
			/* Quoted FFFF FFFE pairs count as one byte */
			if (c == 0xFF)
				out_byte(tokbyte());
			l++;
		}
	} while(c);

	/* No write any padding bytes */
	if (pad) {
		while(l++ < maxlen)
			out_block(&pad_zero, 2);
	}
	/* Write the end marker */
	out_byte(0);
	footer(H_STRING, label, l);

	next_token();
	if (token != T_STRING_END)
		error("bad token stream");
	next_token();
	return l;
}


unsigned label_tag;

unsigned quoted_string(int *len)
{
	unsigned l = 0;
	unsigned label = ++label_tag;

	if (token != T_STRING)
		return 0;

	l = copy_string(label, ~0, 0, 1);

	if (len)
		*len = l;

	return label;
}
