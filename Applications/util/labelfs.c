/*
 *	labelfs: not supported on FS32.
 *
 *	The classic format kept a label and geometry record in the
 *	bytes after the kernel superblock; FS32 declares those bytes
 *	reserved-must-be-zero (FS32-FORMAT.md), so writing a label
 *	would corrupt the filesystem's reserved region.  If labels are
 *	ever wanted they get a field in a future FS32 version, not a
 *	scribble in the reserve.
 */

#include <stdio.h>

int main(int argc, char *argv[])
{
	fprintf(stderr,
		"labelfs: filesystem labels are not supported on FS32\n");
	return 1;
}
