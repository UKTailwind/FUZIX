/*
 *	cc on the Colour Maximite 2: what ccbc.c asks of the machine under
 *	-DCMM2 (ccbc_cmm2.c).
 *
 *	A loadable program cannot start another, so cc.app is the driver
 *	with the passes linked in (the CMM2 firmware's apps/cc/make.py):
 *	each pass is built as it is for any machine, its main renamed and
 *	everything else in it kept to itself, and the driver calls it
 *	where Fuzix would fork and exec it.
 */
#ifndef CCBC_CMM2_H
#define CCBC_CMM2_H

/* Where the passes and headers are: the folder cc.app was run from
   (its argv[0]), with the slash, or "" for the current one. */
const char *cmm2_libpath(const char *argv0);

/* Is the pass a path names (by its last part: cpp, cc0, cc1, cc2) in
   cc.app. */
int cmm2_has_pass(const char *path);

/* The pass argv[0] names run with these arguments, fdin and fdout (or
   -1) as its standard input and output, as a child's would be.  Its
   status as wait() gives it, or -1 if it is not in cc.app. */
int cmm2_run_pass(char **argv, int fdin, int fdout);

#endif
