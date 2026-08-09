/*
 *	Compiler pass main loop
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "compiler.h"

FILE *debug;

unsigned deffunctype;		/* The type of an undeclared function */
unsigned funcbody;		/* Parser global for function body */
unsigned voltrack;		/* Track possible volatiles */
unsigned in_sizeof;		/* Set if we are in sizeof() */
/* Set once we have written a progress dot, so we know to end the line */
static unsigned progress;
/* Set by -q.  The dots go to stderr because stdout carries the IR, so a
   caller cannot redirect them away on their own - only suppress them,
   and only the driver knows whether they are wanted.  cc passes -q when
   its own stdout is not a terminal. */
static unsigned quiet;

/*
 *	A C program consists of a series of declarations that by default
 *	are external definitions.
 */
static void toplevel(void)
{
	if (token == T_TYPEDEF) {
		next_token();
		dotypedef();
	} else {
		funcbody = 0;
		voltrack = 0;
		target_reginit();
		declaration(S_EXTDEF);
		if (!quiet) {
			write(2, ".", 1);
			progress = 1;
		}
	}
}

/* A function defined by use is taken to be int f(); */
static unsigned functype[2] = {
	1, ELLIPSIS
};

int main(int argc, char *argv[])
{
	int i;

	for (i = 1; i < argc; i++)
		if (argv[i][0] == '-' && argv[i][1] == 'q' && argv[i][2] == 0)
			quiet = 1;

	/* Counts the names before parsing, so that a file scope static
	   nothing mentions twice can be parsed and not generated. */
	prescan_names();
	next_token();
	init_nodes();
	/* A function with no type info returning INT */
	deffunctype = make_function(CINT, functype);
#ifdef DEBUG
	/* The first non-option argument, so -q does not become a file
	   called "-q" in a debug build. */
	for (i = 1; i < argc; i++) {
		if (argv[i][0] == '-')
			continue;
		debug = fopen(argv[i], "w");
		if (debug == NULL) {
			perror(argv[i]);
			return 255;
		}
		break;
	}
#endif
	while (token != T_EOF)
		toplevel();
	/* No write out any uninitialized variables */
	write_bss();
	out_write();
	/* End the line of progress dots, or the shell prompt lands on the
	   end of them */
	if (progress)
		write(2, "\n", 1);
	return errors;
}
