/*
 *	cc on the Colour Maximite 2: the driver's side (ccbc_cmm2.h).
 *
 *	The passes keep their own code.  The build renames each one's main
 *	(cpp_main, cc0_main, cc1_main, cc2_main) and its calls of exit
 *	(cmm2_pass_exit), and makes every other name in it local, so the
 *	four live in one program without meeting.  Each runs once per
 *	compile, so its static data starts as it would in a new process.
 */
#include <errno.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "ccbc_cmm2.h"

int cpp_main(int, char **);
int cc0_main(int, char **);
int cc1_main(int, char **);
int cc2_main(int, char **);
int mmbc_main(int, char **);

static const struct {
	const char *name;
	int (*main)(int, char **);
} passes[] = {
	{ "cpp", cpp_main },
	{ "cc0", cc0_main },
	{ "cc1", cc1_main },
	{ "cc2", cc2_main },
	{ "mmbc", mmbc_main },		/* MMBasic to C, for a .bas */
	{ NULL, NULL }
};

static jmp_buf pass_end;

/* A pass's exit: the end of the pass, its code to the driver. */
void cmm2_pass_exit(int code)
{
	longjmp(pass_end, 0x100 | (code & 0xFF));
}

const char *cmm2_libpath(const char *argv0)
{
	static char dir[128];
	const char *slash = strrchr(argv0, '/');

	if (slash == NULL || (size_t)(slash - argv0) >= sizeof(dir) - 1)
		return "";
	memcpy(dir, argv0, slash + 1 - argv0);
	dir[slash + 1 - argv0] = 0;
	return dir;
}

static int (*find(const char *path))(int, char **)
{
	const char *n = strrchr(path, '/');
	unsigned i;

	n = n ? n + 1 : path;
	for (i = 0; passes[i].name; i++)
		if (strcmp(passes[i].name, n) == 0)
			return passes[i].main;
	return NULL;
}

int cmm2_has_pass(const char *path)
{
	return find(path) != NULL;
}

int cmm2_run_pass(char **argv, int fdin, int fdout)
{
	int (*fn)(int, char **) = find(argv[0]);
	int argc, r;

	if (fn == NULL) {
		errno = ENOENT;
		return -1;
	}
	for (argc = 0; argv[argc]; argc++)
		;
	if (fdin != -1) {
		dup2(fdin, 0);
		close(fdin);
	}
	if (fdout != -1) {
		dup2(fdout, 1);
		close(fdout);
	}
	r = setjmp(pass_end);
	if (r == 0)
		r = 0x100 | (fn(argc, argv) & 0xFF);
	fflush(stdout);		/* what the pass wrote through stdio */
	fflush(stderr);
	close(0);		/* the console again */
	close(1);
	return (r & 0xFF) << 8;
}

/* Fuzix's C library has these, and cc0 and cpp use them (newlib has
   neither): a number in decimal, in a buffer of the function's own. */
char *_itoa(int v)
{
	static char buf[12];

	snprintf(buf, sizeof(buf), "%d", v);
	return buf;
}

const char *_ltoa(long v)
{
	static char buf[12];

	snprintf(buf, sizeof(buf), "%ld", v);
	return buf;
}
