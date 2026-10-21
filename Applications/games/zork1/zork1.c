/*
 *	zork1 - play Zork I with z3.
 *
 *	A program rather than a shell script, for two reasons found on the
 *	board: exec hands anything that is not an ELF binary to bcrun, which
 *	answers "not a bytecode object", so a script cannot be run by name;
 *	and the V7 sh turns an empty "$@" into one empty argument, which z3
 *	takes for the story file.
 */
#include <stdio.h>
#include <unistd.h>

#ifndef Z3
#define Z3	"/usr/games/z3"
#endif
#ifndef STORY
#define STORY	"/usr/games/lib/zork1.z3"
#endif

int main(int argc, char *argv[])
{
	char *nargv[8];
	int i, n = 0;

	/* z3's options pass through; it takes three */
	if (argc > 6) {
		fputs("usage: zork1 [-t] [-p] [-q]\n", stderr);
		return 1;
	}
	nargv[n++] = "z3";
	for (i = 1; i < argc; i++)
		nargv[n++] = argv[i];
	nargv[n++] = STORY;
	nargv[n] = NULL;
	execv(Z3, nargv);
	perror(Z3);
	return 1;
}
