#ifndef MMB_JSON_H
#define MMB_JSON_H
/*
 *	JSON$(a%(), path$) - fun_json's observable surface (PicoMite
 *	misc/Custom.c:3043-3199) as a STREAMING path-walker: no tree,
 *	one structural pass to validate and one walk per call, against
 *	the ~40 K of cJSON the reference builds and frees per query
 *	(PLAN-web.md §7, the flagged implementation divergence).
 *
 *	Replicated to the letter, each item read out of the reference:
 *
 *	- the document must PARSE ("Invalid JSON data") - proven here by
 *	  a strict recursive scan of the root value before walking;
 *	  trailing text after the root is ignored, as cJSON_Parse
 *	  ignores it;
 *	- INTERMEDIATE fields are case-sensitive, the FINAL field is
 *	  case-insensitive (GetObjectItemCaseSensitive vs
 *	  GetObjectItem, and the walk ALWAYS ends with a field lookup -
 *	  so a path ending in [n] looks up an empty field and yields
 *	  "", which the WebMite's own jsontest.bas documents);
 *	- an [n] step takes the n'th CHILD, so it indexes an object's
 *	  members as happily as an array - cJSON_GetArrayItem's own
 *	  behaviour;
 *	- field names cap at 31 characters (field[32] there, minus the
 *	  overrun), indexes at 5 digits;
 *	- number leaf: integral -> IntToStr, else FloatToStr(0, AUTO,
 *	  ' '); booleans are the words; a string leaf arrives with its
 *	  escapes DECODED (\uXXXX to UTF-8 included), capped at the
 *	  255 a BASIC string holds; null, an array leaf, or a missing
 *	  path yield "" - only an OBJECT leaf raises "Not an item".
 */

#include "mmb_runtime.h"

#ifndef MMG_FN
#if defined(MM_FCC) || defined(MM_PC3)
#define MMG_FN static
#else
#define MMG_FN static __inline__ __attribute__((unused))
#endif
#endif

#define MMJ_MAXDEPTH 64

static const char *mmj_t;
static long mmj_n;

MMG_FN long mmj_ws(long i)
{
	while (i < mmj_n && (mmj_t[i] == ' ' || mmj_t[i] == '\t' ||
			     mmj_t[i] == '\r' || mmj_t[i] == '\n'))
		i++;
	return i;
}

/*	past the closing quote, or -1 */
MMG_FN long mmj_strend(long i)
{
	i++;				/* the opening quote */
	while (i < mmj_n) {
		if (mmj_t[i] == '\\')
			i += 2;
		else if (mmj_t[i] == '"')
			return i + 1;
		else
			i++;
	}
	return -1;
}

/*	validate and skip one value; past it, or -1 */
MMG_FN long mmj_value(long i, int depth)
{
	char c;
	long j;

	if (depth > MMJ_MAXDEPTH)
		return -1;
	i = mmj_ws(i);
	if (i >= mmj_n)
		return -1;
	c = mmj_t[i];
	if (c == '"')
		return mmj_strend(i);
	if (c == '{' || c == '[') {
		char close = c == '{' ? '}' : ']';
		int first = 1;

		i++;
		for (;;) {
			i = mmj_ws(i);
			if (i >= mmj_n)
				return -1;
			if (mmj_t[i] == close)
				return i + 1;
			if (!first) {
				if (mmj_t[i] != ',')
					return -1;
				i = mmj_ws(i + 1);
			}
			first = 0;
			if (close == '}') {
				if (i >= mmj_n || mmj_t[i] != '"')
					return -1;
				i = mmj_strend(i);
				if (i < 0)
					return -1;
				i = mmj_ws(i);
				if (i >= mmj_n || mmj_t[i] != ':')
					return -1;
				i++;
			}
			i = mmj_value(i, depth + 1);
			if (i < 0)
				return -1;
		}
	}
	if (c == 't' || c == 'f' || c == 'n') {
		const char *w = c == 't' ? "true" :
				c == 'f' ? "false" : "null";
		for (j = 0; w[j]; j++)
			if (i + j >= mmj_n || mmj_t[i + j] != w[j])
				return -1;
		return i + j;
	}
	if (c == '-' || (c >= '0' && c <= '9')) {
		j = i;
		while (j < mmj_n &&
		       ((mmj_t[j] >= '0' && mmj_t[j] <= '9') ||
			mmj_t[j] == '-' || mmj_t[j] == '+' ||
			mmj_t[j] == '.' || mmj_t[j] == 'e' ||
			mmj_t[j] == 'E'))
			j++;
		return j > i ? j : -1;
	}
	return -1;
}

/*	decode a JSON string (cursor at the opening quote) into out,
 *	max-1 bytes, the way cJSON decodes it: the escapes, and \uXXXX
 *	to UTF-8 with surrogate pairs combined */
MMG_FN int mmj_unstr(long i, char *out, int max)
{
	int o = 0;
	unsigned long u, lo;
	char c;

	i++;
	while (i < mmj_n && mmj_t[i] != '"' && o < max - 1) {
		c = mmj_t[i++];
		if (c != '\\') {
			out[o++] = c;
			continue;
		}
		if (i >= mmj_n)
			break;
		c = mmj_t[i++];
		switch (c) {
		case 'b': out[o++] = '\b'; break;
		case 'f': out[o++] = '\f'; break;
		case 'n': out[o++] = '\n'; break;
		case 'r': out[o++] = '\r'; break;
		case 't': out[o++] = '\t'; break;
		case 'u': {
			int k;

			u = 0;
			for (k = 0; k < 4 && i < mmj_n; k++, i++) {
				char h = mmj_t[i];
				u <<= 4;
				if (h >= '0' && h <= '9')
					u |= (unsigned long)(h - '0');
				else if (h >= 'a' && h <= 'f')
					u |= (unsigned long)(h - 'a' + 10);
				else if (h >= 'A' && h <= 'F')
					u |= (unsigned long)(h - 'A' + 10);
			}
			if (u >= 0xD800 && u <= 0xDBFF &&
			    i + 5 < mmj_n && mmj_t[i] == '\\' &&
			    mmj_t[i + 1] == 'u') {
				lo = 0;
				for (k = 0; k < 4; k++) {
					char h = mmj_t[i + 2 + k];
					lo <<= 4;
					if (h >= '0' && h <= '9')
						lo |= (unsigned long)(h - '0');
					else if (h >= 'a' && h <= 'f')
						lo |= (unsigned long)(h - 'a' + 10);
					else if (h >= 'A' && h <= 'F')
						lo |= (unsigned long)(h - 'A' + 10);
				}
				i += 6;
				u = 0x10000 +
				    ((u - 0xD800) << 10) + (lo - 0xDC00);
			}
			if (u < 0x80) {
				out[o++] = (char)u;
			} else if (u < 0x800) {
				if (o < max - 2) {
					out[o++] = (char)(0xC0 | (u >> 6));
					out[o++] = (char)(0x80 | (u & 0x3F));
				}
			} else if (u < 0x10000) {
				if (o < max - 3) {
					out[o++] = (char)(0xE0 | (u >> 12));
					out[o++] = (char)(0x80 | ((u >> 6) & 0x3F));
					out[o++] = (char)(0x80 | (u & 0x3F));
				}
			} else if (o < max - 4) {
				out[o++] = (char)(0xF0 | (u >> 18));
				out[o++] = (char)(0x80 | ((u >> 12) & 0x3F));
				out[o++] = (char)(0x80 | ((u >> 6) & 0x3F));
				out[o++] = (char)(0x80 | (u & 0x3F));
			}
			break;
		}
		default:
			out[o++] = c;	/* covers \" \\ \/ */
		}
	}
	out[o] = 0;
	return o;
}

/*	the value of the member named key inside the object at i, or -1;
 *	cs = case-sensitive, the intermediate/final split */
MMG_FN long mmj_objfind(long i, const char *key, int cs)
{
	char kb[64];
	int first = 1, k, same;

	i = mmj_ws(i);
	if (i >= mmj_n || mmj_t[i] != '{')
		return -1;
	i++;
	for (;;) {
		i = mmj_ws(i);
		if (i >= mmj_n || mmj_t[i] == '}')
			return -1;
		if (!first)
			i = mmj_ws(i + 1);	/* the comma */
		first = 0;
		if (i >= mmj_n || mmj_t[i] != '"')
			return -1;
		mmj_unstr(i, kb, (int)sizeof(kb));
		i = mmj_strend(i);
		i = mmj_ws(i) + 1;		/* the colon */
		i = mmj_ws(i);
		if (cs)
			same = strcmp(kb, key) == 0;
		else {
			for (k = 0; kb[k] || key[k]; k++) {
				char x = kb[k], y = key[k];
				if (x >= 'a' && x <= 'z')
					x = (char)(x - 'a' + 'A');
				if (y >= 'a' && y <= 'z')
					y = (char)(y - 'a' + 'A');
				if (x != y)
					break;
			}
			same = kb[k] == 0 && key[k] == 0;
		}
		if (same)
			return i;
		i = mmj_value(i, 0);
	}
}

/*	the n'th CHILD of the object or array at i - GetArrayItem's own
 *	object-tolerant behaviour - or -1 */
MMG_FN long mmj_child(long i, int idx)
{
	char open;
	int first = 1;

	i = mmj_ws(i);
	if (i >= mmj_n)
		return -1;
	open = mmj_t[i];
	if (open != '{' && open != '[')
		return -1;
	i++;
	for (;;) {
		i = mmj_ws(i);
		if (i >= mmj_n || mmj_t[i] == (open == '{' ? '}' : ']'))
			return -1;
		if (!first)
			i = mmj_ws(i + 1);
		first = 0;
		if (open == '{') {
			i = mmj_strend(i);
			i = mmj_ws(i) + 1;
			i = mmj_ws(i);
		}
		if (idx == 0)
			return i;
		idx--;
		i = mmj_value(i, 0);
	}
}

MMG_FN char *mm_json(const MMINTEGER *a, int cells, const char *path)
{
	char field[32], num[6], vb[40];
	char *out = mm_tmp();
	long cur, e;
	long slen = a[0];
	long cap = (long)(cells - 1) * 8;
	int alive = 1, mode = 0, fp = 0, np = 0, pi, pl, k;
	char c;
	double d;

	mmj_t = (const char *)&a[1];
	if (slen < 0)
		slen = 0;
	if (slen > cap)
		slen = cap;
	mmj_n = slen;

	cur = mmj_ws(0);
	e = mmj_value(cur, 0);
	if (e < 0) {
		mm_error("Invalid JSON data");
		return out;
	}

	/*	the reference's state machine, verbatim
	 *	(Custom.c:3098-3133): fields resolve case-sensitively as
	 *	the path is read, indexes through the n'th child, and the
	 *	loop ALWAYS finishes with one case-insensitive lookup of
	 *	whatever is left in field - the empty string included. */
	pl = mm_slen(path);
	field[0] = 0;
	for (pi = 1; pi <= pl; pi++) {
		c = path[pi];
		if (c == '[') {
			mode = 1;
			field[fp] = 0;
			if (alive) {
				e = mmj_objfind(cur, field, 1);
				if (e < 0)
					alive = 0;
				else
					cur = e;
			}
			fp = 0;
			continue;
		}
		if (c == ']') {
			num[np] = 0;
			k = 0;
			for (np = 0; num[np]; np++)
				k = k * 10 + (num[np] - '0');
			np = 0;
			if (alive) {
				e = mmj_child(cur, k);
				if (e < 0)
					alive = 0;
				else
					cur = e;
			}
			continue;
		}
		if (c == '.') {
			if (mode == 0) {
				field[fp] = 0;
				if (alive) {
					e = mmj_objfind(cur, field, 1);
					if (e < 0)
						alive = 0;
					else
						cur = e;
				}
				fp = 0;
			} else
				mode = 0;
			continue;
		}
		if (mode == 0) {
			if (fp < (int)sizeof(field) - 1)
				field[fp++] = c;
		} else if (np < (int)sizeof(num) - 1 &&
			   c >= '0' && c <= '9')
			num[np++] = c;
	}
	field[fp] = 0;
	if (alive) {
		e = mmj_objfind(cur, field, 0);
		if (e < 0)
			alive = 0;
		else
			cur = e;
	}

	if (!alive) {
		mm_ssetc(out, "");
		return out;
	}
	cur = mmj_ws(cur);
	c = cur < mmj_n ? mmj_t[cur] : 0;
	if (c == '{') {
		mm_error("Not an item");
		return out;
	}
	if (c == '"') {
		char sb[256];
		int n = mmj_unstr(cur, sb, (int)sizeof(sb));

		mm_ssetn(out, sb, n);
		return out;
	}
	if (c == 't') {
		mm_ssetc(out, "true");
		return out;
	}
	if (c == 'f') {
		mm_ssetc(out, "false");
		return out;
	}
	if (c == '-' || (c >= '0' && c <= '9')) {
		char nb[48];	/* an M-string for mm_atof, VAL's parser */
		long j = mmj_value(cur, 0);
		int n = (int)(j - cur);

		if (n > (int)sizeof(nb) - 2)
			n = (int)sizeof(nb) - 2;
		nb[0] = (char)n;
		memcpy(nb + 1, mmj_t + cur, (size_t)n);
		/* the NUL matters: the board's mm_atof goes through a
		   C-string conversion that reads to a terminator, and a
		   stale one let every number leaf inherit the previous
		   leaf's tail digits - 16.0 parsed as 16.01147 */
		nb[1 + n] = 0;
		d = (double)mm_atof(nb);
		if ((double)(long long)d == d)
			mm_int_to_str(vb, (long long)d, 10);
		else
			mm_float_to_str(vb, (MMFLOAT)d, 0,
					MM_AUTO_PRECISION, ' ');
		mm_ssetc(out, vb);
		return out;
	}
	/* null, an array leaf, or anything else: the reference's
	   fall-through - the empty string */
	mm_ssetc(out, "");
	return out;
}

#endif /* MMB_JSON_H */
