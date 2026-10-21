/* pngtest - decode a PNG with loadpng's upng.c and say what came out.
 *
 *   pngtest file.png
 *
 * Prints the error, the size and an FNV hash of the decoded buffer, and
 * exits 0 on a decode, 1 on a refusal.  pngtest.sh builds it with
 * AddressSanitizer, so a decoder that reads or writes outside a buffer
 * fails loudly instead of handing back a plausible picture.
 *
 * The four things upng asks of its host are upng_pc3.c's, with real
 * allocations so the sanitizer guards each buffer: GetMemory zero-fills
 * (MMBasic's does), FreeMemorySafe only clears the caller's pointer (the
 * PC3's arena never frees).
 */
#include <stdio.h>
#include <stdlib.h>
#include "upng.h"

void *GetMemory(unsigned long n) { return calloc(1, n ? n : 1); }
void FreeMemorySafe(void *pp) { *(void **)pp = NULL; }
void routinechecks(void) { }
void error(const char *msg) { printf("error(): %s\n", msg); exit(3); }

int main(int argc, char **argv)
{
	FILE *f;
	unsigned char *buf;
	long n;
	upng_t *u;
	const unsigned char *p;
	unsigned i, size, h = 2166136261u;

	if (argc != 2 || !(f = fopen(argv[1], "rb")))
		return 2;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	rewind(f);
	buf = malloc(n);
	if (fread(buf, 1, n, f) != (size_t)n)
		return 2;
	fclose(f);

	u = upng_new_from_bytes(buf, n);
	upng_decode(u);
	p = upng_get_buffer(u);
	size = p ? upng_get_size(u) : 0;
	for (i = 0; i < size; i++)
		h = (h ^ p[i]) * 16777619u;
	printf("error %d  %ux%u  %u bytes  fnv %08x\n", (int)upng_get_error(u),
	       upng_get_width(u), upng_get_height(u), size, h);
	return upng_get_error(u) == UPNG_EOK ? 0 : 1;
}
