#ifndef _CTYPE_H
#define _CTYPE_H

/* Functions rather than macros so an argument with side effects is
 * evaluated once, as the standard requires.  static is fine: bytecode
 * programs are a single translation unit. */

static int isdigit(int c)  { return c >= '0' && c <= '9'; }
static int isupper(int c)  { return c >= 'A' && c <= 'Z'; }
static int islower(int c)  { return c >= 'a' && c <= 'z'; }
static int isalpha(int c)  { return isupper(c) || islower(c); }
static int isalnum(int c)  { return isalpha(c) || isdigit(c); }
static int isspace(int c)
{
    return c == ' ' || c == '\t' || c == '\n' ||
           c == '\r' || c == '\f' || c == '\v';
}
static int isxdigit(int c)
{
    return isdigit(c) || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}
static int toupper(int c)  { return islower(c) ? c - 'a' + 'A' : c; }
static int tolower(int c)  { return isupper(c) ? c - 'A' + 'a' : c; }

#endif
