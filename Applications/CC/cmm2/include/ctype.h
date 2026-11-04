#ifndef _CTYPE_H
#define _CTYPE_H

/*
 * bcrun has no ctype functions, so they are here, compiled into the
 * program: functions rather than macros, so that isdigit(*p++) moves p
 * once.  ASCII only, as the C locale is.
 */

static int isdigit(int c) { return c >= '0' && c <= '9'; }
static int isupper(int c) { return c >= 'A' && c <= 'Z'; }
static int islower(int c) { return c >= 'a' && c <= 'z'; }
static int isalpha(int c) { return isupper(c) || islower(c); }
static int isalnum(int c) { return isalpha(c) || isdigit(c); }
static int isxdigit(int c) { return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
static int isspace(int c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
static int iscntrl(int c) { return (c >= 0 && c < 32) || c == 127; }
static int isprint(int c) { return c >= 32 && c < 127; }
static int isgraph(int c) { return c > 32 && c < 127; }
static int ispunct(int c) { return isgraph(c) && !isalnum(c); }
static int toupper(int c) { return islower(c) ? c - 32 : c; }
static int tolower(int c) { return isupper(c) ? c + 32 : c; }

#endif
