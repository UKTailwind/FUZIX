#include <string.h>

size_t strlcpy(char *dst, const char *src, size_t dstsize)
{
  size_t len, cp;

  /* dstsize 0 must write nothing (BSD semantics).  The old code
     computed dstsize - 1 unconditionally: with 0 that underflows to
     SIZE_MAX and memcpy ploughs through all of memory - on a machine
     with no protection that is a system-wide corruption, found via
     cpp's quoted-include path handing this a zero bound. */
  len = strlen(src);
  if (dstsize == 0)
    return len;
  cp = len >= dstsize ? dstsize - 1 : len;
  memcpy(dst, src, cp);
  dst[cp] = 0;
  return len;
}

size_t strlcat(char *dst, const char *src, size_t dstsize)
{
  size_t len = strlen(dst);
  /* No room at all (or a zero-size buffer: dstsize - 1 must not
     underflow): existing string fills the buffer */
  if (dstsize == 0 || len >= dstsize - 1)
    return len + strlen(src);
  return strlcpy(dst + len, src, dstsize - len);
}

  