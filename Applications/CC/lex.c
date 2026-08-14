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
 *	REACHABILITY, on top of the count above.
 *
 *	Counting names keeps anything a DEAD function mentions: the
 *	callees of something nothing calls stay, and so does everything
 *	they in turn name. That was affordable while a header held one
 *	primitive and the rule's own comment said so - "inside a header
 *	everything is reachable from its entry point". It stopped being
 *	true when the sprite and blit engines arrived: 53 functions
 *	behind fifteen entry points, so a program doing one SPRITE READ
 *	was handed the collision detector, the show/hide stacks and the
 *	compressed-blit decoders. Measured on that program, 94% of the
 *	generated code was unreachable; across a whole game, 29%.
 *
 *	So the file scope statics become a graph and it is walked from
 *	the roots. A static is kept if something outside every static
 *	body names it - main, an extern function, an initialiser - or if
 *	a kept static names it.
 *
 *	The bounds are deliberate and every one of them fails SAFE, by
 *	keeping code: more statics than FN_MAX, or names past
 *	NAMES_TRACKED, and the answer falls back to the count.
 */
#define FN_MAX		192		/* static functions tracked */
#define FN_WORDS	((FN_MAX + 31) / 32)

static unsigned short fn_name[FN_MAX];	/* index -> token id */
static unsigned fn_n;
static unsigned fn_over;		/* too many: fall back */
static unsigned fn_edge[FN_MAX][FN_WORDS];
static unsigned fn_root[FN_WORDS];
static unsigned fn_keep[FN_WORDS];
static unsigned fn_graph_done;

static void bit_set(unsigned *w, unsigned i)
{
	w[i >> 5] |= 1UL << (i & 31);
}

static unsigned bit_get(const unsigned *w, unsigned i)
{
	return (w[i >> 5] >> (i & 31)) & 1;
}

/* Index of a name in the static-function table, or -1. */
static int fn_find(unsigned id)
{
	unsigned i;

	for (i = 0; i < fn_n; i++)
		if (fn_name[i] == id)
			return (int)i;
	return -1;
}

static void fn_add(unsigned id)
{
	if (fn_find(id) >= 0)
		return;
	if (fn_n == FN_MAX) {
		fn_over = 1;
		return;
	}
	fn_name[fn_n++] = (unsigned short)id;
}

/* Mark from the roots until nothing new appears. */
static void fn_close(void)
{
	unsigned again = 1, i, j;

	for (i = 0; i < FN_WORDS; i++)
		fn_keep[i] = fn_root[i];
	while (again) {
		again = 0;
		for (i = 0; i < fn_n; i++) {
			if (!bit_get(fn_keep, i))
				continue;
			for (j = 0; j < fn_n; j++) {
				if (bit_get(fn_edge[i], j) &&
				    !bit_get(fn_keep, j)) {
					bit_set(fn_keep, j);
					again = 1;
				}
			}
		}
	}
}

/*
 *	Is this file scope static unreachable?  Asked once per function
 *	definition, by body.c.
 */
unsigned name_unreachable(unsigned id)
{
	int i;

	if (!fn_graph_done || fn_over)
		return 0;
	i = fn_find(id);
	if (i < 0)
		return 0;
	return !bit_get(fn_keep, (unsigned)i);
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
/*
 *	One walk of the token stream.  pass 0 counts names and finds the
 *	file scope static function DEFINITIONS; pass 1 fills in the graph
 *	now that the set of them is known.
 *
 *	A definition is `static ... name ( ... ) {' - the '(' before the
 *	'{' is what separates it from `static int tab[] = {...}', whose
 *	braces are an initialiser and whose contents belong to nobody.
 *	Getting that wrong would be the one dangerous mistake here: the
 *	names in a function-pointer table would be attributed to a
 *	variable rather than to the roots, and dropped.
 */
static void prescan_pass(int pass)
{
	int c;
	unsigned t, i;
	unsigned depth = 0;	/* brace depth */
	unsigned armed = 0;	/* `static' seen at file scope */
	unsigned saweq = 0;	/* ...and an '=' after it: an initialiser */
	unsigned pend = 0;	/* the name that might be being defined */
	unsigned frozen = 0;	/* pend is settled: the '(' has been seen */
	unsigned prev = 0;	/* previous token, for the '(' before '{' */
	int cur = -1;		/* static function being walked, or root */

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
			if (pass == 0)
				name_bump(t);
			if (depth == 0) {
				/* A name at file scope is a USE only if it
				   comes after an '=' - otherwise it is the
				   thing being declared, or a parameter, and
				   its own definition must not root it. */
				if (pass == 1 && (!armed || saweq)) {
					int k = fn_find(t);
					if (k >= 0)
						bit_set(fn_root, (unsigned)k);
				}
				/* The name being defined is the one just
				   before the argument list - NOT whatever
				   comes after it, or a parameter would be
				   taken for the function. */
				if (armed && !frozen)
					pend = t;
			} else if (pass == 1) {
				int k = fn_find(t);
				if (k >= 0) {
					if (cur >= 0)
						bit_set(fn_edge[cur],
							(unsigned)k);
					else
						bit_set(fn_root, (unsigned)k);
				}
			}
			prev = t;
			continue;
		}
		switch (t) {
		case T_STATIC:
			if (depth == 0) {
				armed = 1;
				saweq = 0;
				pend = 0;
				frozen = 0;
			}
			break;
		case T_LPAREN:
			/* the argument list: pend is the name */
			if (depth == 0 && armed)
				frozen = 1;
			break;
		case T_EQ:
			if (depth == 0)
				saweq = 1;
			break;
		case T_SEMICOLON:
			if (depth == 0) {
				armed = 0;
				saweq = 0;
				pend = 0;
				frozen = 0;
			}
			break;
		case T_LCURLY:
			/* a body, not an initialiser, iff `) {' */
			if (depth == 0 && armed && !saweq && pend &&
			    prev == T_RPAREN) {
				if (pass == 0)
					fn_add(pend);
				else
					cur = fn_find(pend);
			}
			depth++;
			break;
		case T_RCURLY:
			if (depth)
				depth--;
			if (depth == 0) {
				armed = 0;
				saweq = 0;
				pend = 0;
				frozen = 0;
				cur = -1;
			}
			break;
		}
		/* NOT T_LINE: a line marker sits between the ')' and the
		   '{' of every definition whose brace is on its own line,
		   which is most of them, and it must not be mistaken for
		   the token before the body. */
		if (t != T_LINE)
			prev = t;
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
}

static void prescan_rewind(void)
{
	if (lseek(0, 0, SEEK_SET) < 0)
		fatal("seek error");
	inlen = 0;
}

void prescan_names(void)
{
	if (lseek(0, 0, SEEK_CUR) < 0)
		return;

	prescan_pass(0);		/* names, and which statics exist */
	prescan_rewind();
	/* A way back to the name count alone, for bisecting a program
	   that the graph and the old rule disagree about. */
	if (getenv("CC1_NO_DCE"))
		fn_over = 1;
	if (fn_n && !fn_over) {
		prescan_pass(1);	/* who names whom */
		prescan_rewind();
		fn_close();
		fn_graph_done = 1;
	}
	if (getenv("CC1_DCE_DEBUG")) {
		unsigned i, k = 0;
		for (i = 0; i < fn_n; i++)
			if (bit_get(fn_keep, i))
				k++;
		fprintf(stderr, "\ndce: %u statics, %u kept, %u dropped, "
			"over=%u done=%u\n", fn_n, k, fn_n - k, fn_over,
			fn_graph_done);
	}
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
