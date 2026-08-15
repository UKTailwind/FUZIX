/*
 *	cc for the PC3 bytecode target.
 *
 *	    cc prog.c            -> prog.bc, runnable with bcrun
 *	    cc -o name prog.c
 *	    cc -v prog.c         show each pass as it runs
 *	    cc -k prog.c         keep the .tok and .ir intermediates
 *
 *	ccfuzix.c is the driver for the ordinary FCC pipeline, which ends
 *	in an assembler and a linker. This target has neither: cc2 writes
 *	a loadable object directly, so there is nothing to link and one
 *	source file is one program. That, plus the fact that ccfuzix.c
 *	under CPU_armm0 is still configured to emit Z80, is why this is a
 *	separate program rather than another #ifdef in there.
 *
 *	The passes cannot be driven from the shell, which is the other
 *	reason this exists. cc1 and cc2 read back from their own standard
 *	output, so it has to be opened O_RDWR; the Fuzix shell has no
 *	"1<>" and quietly leaves them reading the console instead.
 *
 *	cpp runs first if it is installed. It is not optional in practice
 *	even for source with no directives in it: cc0 does not know what a
 *	comment is - stripping them has always been cpp's job in FCC - so
 *	without it the opening comment marker of the file becomes a divide
 *	followed by a multiply.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define LIBPATH		"/usr/lib/cc/"
#define CMD_CPP		"/usr/bin/cpp"
#define CMD_MMBC	"/usr/bin/mmbc"
/*
 * Headers describing what bcrun actually provides, which is not what
 * /usr/include describes - those are the Fuzix libc's, for native
 * binaries. cpp has no built-in search path at all (include_paths[] is
 * only ever filled from -I), so without this "#include <stdio.h>"
 * simply failed on the board and every program had to declare its own
 * "int printf();".
 */
#define INCDIR		"include"

static const char *libpath = LIBPATH;
static const char *cppcmd = CMD_CPP;
static const char *mmbccmd = CMD_MMBC;
static char incpath[80];
static int verbose;
static int keep;
static int pp_is_src;		/* no cpp: cc0 reads the source itself */

/* The intermediates, so the signal handler and the exit path can find
   them without threading them through every call */
static char ppfile[64];
static char tokfile[64];
static char irfile[64];
static char symfile[64];
/* The C that mmbc generated from a .bas source.  A distinct name -
   prog.mb.c, not prog.c - so compiling prog.bas can never overwrite a
   real prog.c sitting beside it. */
static char basfile[64];

/*
 *	Only one cc at a time.
 *
 *	The passes share a fixed scratch file, .symtmp, in the working
 *	directory: two compiles running together delete it under one
 *	another and one of them fails with "No such file or directory".
 *	Naming it per-process would fix that particular collision, but not
 *	the reason it is a bad idea here - a compile is most of the 340K
 *	process pool, and two at once thrash it - so the lock is the
 *	honest answer and it is machine-wide rather than per directory.
 *
 *	It has to survive a crash.  This machine does crash, and a lock
 *	left behind by a dead compile would leave the board unable to
 *	build anything at all, with no clue why.  So the file carries the
 *	holder's pid and a lock whose holder is gone is taken over.
 */
#define LOCKFILE	"/tmp/cc.lock"

static int held;		/* we own LOCKFILE and must remove it */

static int lock_is_stale(void)
{
	char buf[16];
	int fd = open(LOCKFILE, O_RDONLY);
	int n;
	pid_t owner;

	if (fd < 0)			/* vanished under us: it is ours */
		return 1;
	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n <= 0)			/* truncated or empty - not a claim */
		return 1;
	buf[n] = 0;
	owner = atoi(buf);
	if (owner <= 0)
		return 1;
	/* Signal 0 checks for existence without sending anything. */
	if (kill(owner, 0) == 0)
		return 0;
	return errno == ESRCH;
}

static void take_lock(void)
{
	char buf[16];
	int fd, tries;

	for (tries = 0; tries < 2; tries++) {
		fd = open(LOCKFILE, O_WRONLY | O_CREAT | O_EXCL, 0600);
		if (fd >= 0) {
			sprintf(buf, "%u\n", (unsigned) getpid());
			write(fd, buf, strlen(buf));
			close(fd);
			held = 1;
			return;
		}
		if (errno != EEXIST)	/* no /tmp, read-only root: carry on
					   rather than refuse to compile */
			return;
		if (!lock_is_stale())
			break;
		unlink(LOCKFILE);	/* the holder is gone; try again */
	}
	fprintf(stderr, "cc: another compile is running.\n");
	exit(1);
}

static void cleanup(void)
{
	if (!keep) {
		if (*ppfile && !pp_is_src)
			unlink(ppfile);
		if (*tokfile)
			unlink(tokfile);
		if (*irfile)
			unlink(irfile);
		if (*basfile)
			unlink(basfile);
	}
	if (*symfile)
		unlink(symfile);
	if (held) {
		unlink(LOCKFILE);
		held = 0;
	}
}

static void fatal(void)
{
	cleanup();
	exit(1);
}

static void onsig(int sig)
{
	cleanup();
	_exit(1);
}

/*
 *	Run one pass. in may be NULL, for a pass that takes its input as an
 *	argument. out is opened read/write - cc1 and cc2 seek and read back
 *	what they have written, so plain O_WRONLY gives an object that
 *	looks corrupt for no visible reason.
 */
/* Written at the front of the final object before cc2 runs, so the
   kernel's #! support execs the result directly: cc prog.c; ./prog.bc */
static const char shebang[] = "#!/usr/bin/bcrun\n";
static int put_shebang;

static void run(char **argv, const char *in, const char *out)
{
	pid_t pid, p;
	int status;
	int fdin = -1, fdout = -1;

	if (verbose) {
		char **q = argv;
		fprintf(stderr, "+");
		while (*q)
			fprintf(stderr, " %s", *q++);
		if (in)
			fprintf(stderr, " <%s", in);
		if (out)
			fprintf(stderr, " >%s", out);
		fprintf(stderr, "\n");
	}

	if (in) {
		fdin = open(in, O_RDONLY);
		if (fdin == -1) {
			perror(in);
			fatal();
		}
	}
	/* out may be NULL for a pass that writes its own file - mmbc
	   takes -o and its stdout is only chatter */
	if (out) {
		fdout = open(out, O_RDWR | O_CREAT | O_TRUNC, 0666);
		if (fdout == -1) {
			perror(out);
			fatal();
		}
	}
	if (put_shebang && fdout != -1) {
		/* the child inherits the shared offset, so its output
		   lands after the line; bcrun's loader skips it */
		write(fdout, shebang, sizeof(shebang) - 1);
		put_shebang = 0;
	}

	pid = fork();
	if (pid == -1) {
		perror("fork");
		fatal();
	}
	if (pid == 0) {
		if (fdin != -1) {
			dup2(fdin, 0);
			close(fdin);
		}
		if (fdout != -1) {
			dup2(fdout, 1);
			close(fdout);
		}
		execv(argv[0], argv);
		perror(argv[0]);
		_exit(255);
	}
	if (fdin != -1)
		close(fdin);
	if (fdout != -1)
		close(fdout);

	while ((p = waitpid(pid, &status, 0)) != pid) {
		if (p == -1) {
			perror("waitpid");
			fatal();
		}
	}
	if (WIFSIGNALED(status)) {
		fprintf(stderr, "cc: %s died on signal %d\n", argv[0],
			WTERMSIG(status));
		fatal();
	}
	/* The pass has already said what was wrong, so just stop */
	if (WEXITSTATUS(status))
		fatal();
}

static char *pass(const char *name)
{
	static char buf[80];
	strcpy(buf, libpath);
	strcat(buf, name);
	return buf;
}

/* prog.c -> prog.bc, and anything else -> name.bc */
static void basename_to(char *dst, const char *src, const char *ext)
{
	const char *slash = strrchr(src, '/');
	const char *dot;

	if (slash)
		src = slash + 1;
	strcpy(dst, src);
	dot = strrchr(dst, '.');
	if (dot)
		dst[dot - dst] = 0;
	strcat(dst, ext);
}

int main(int argc, char *argv[])
{
	char *src = NULL;
	char out[64];
	char *av[6];
	int i;

	*out = 0;

	for (i = 1; i < argc; i++) {
		if (argv[i][0] == '-' && argv[i][1]) {
			switch (argv[i][1]) {
			case 'o':
				if (++i == argc) {
					fprintf(stderr, "cc: -o needs a name\n");
					return 1;
				}
				strcpy(out, argv[i]);
				break;
			case 'v':
				verbose = 1;
				break;
			case 'k':
				keep = 1;
				break;
			case 'L':
				libpath = argv[i] + 2;
				break;
			default:
				fprintf(stderr, "cc: unknown option %s\n",
					argv[i]);
				return 1;
			}
		} else if (src) {
			fprintf(stderr, "cc: one source file at a time - "
				"there is no linker\n");
			return 1;
		} else
			src = argv[i];
	}

	if (src == NULL) {
		fprintf(stderr,
			"usage: cc [-o out] [-v] [-k] [-Ldir] file.c|file.bas\n");
		return 1;
	}

	if (*out == 0)
		basename_to(out, src, ".bc");
	basename_to(ppfile, src, ".pp");
	basename_to(tokfile, src, ".tok");
	basename_to(irfile, src, ".ir");
	strcpy(symfile, ".symtmp");

	/* Before the handlers, so an interrupt during the wait cannot make
	   cleanup() remove a lock we never took. */
	take_lock();

	signal(SIGINT, onsig);
	signal(SIGHUP, onsig);
	signal(SIGTERM, onsig);

	/*
	 * BASIC first, through mmbc: cc prog.bas is the whole build, the
	 * way the manual has always written the two commands.  The
	 * generated C gets a name of its own (prog.mb.c) and is removed
	 * with the other intermediates, so a real prog.c next to the
	 * BASIC is never touched.  After this the pipeline neither knows
	 * nor cares where the C came from.
	 */
	{
		const char *dot = strrchr(src, '.');

		if (dot && (strcmp(dot, ".bas") == 0 ||
			    strcmp(dot, ".BAS") == 0)) {
			if (access(mmbccmd, X_OK) != 0) {
				fprintf(stderr, "cc: no %s - BASIC needs "
					"the translator\n", mmbccmd);
				fatal();
			}
			basename_to(basfile, src, ".mb.c");
			av[0] = (char *) mmbccmd;
			av[1] = src;
			av[2] = "-o";
			av[3] = basfile;
			av[4] = NULL;
			run(av, NULL, NULL);
			src = basfile;
		}
	}

	/*
	 * cpp takes its input as an argument and writes to stdout. If it
	 * is not installed, carry on without it and say so - the compiler
	 * still works, but only on source with no directives and no
	 * comments, which is a trap worth naming out loud.
	 */
	if (access(cppcmd, X_OK) == 0) {
		/* -I<libpath>include, so that -L moves the headers with the
		   passes rather than leaving them behind. */
		strcpy(incpath, "-I");
		strcat(incpath, libpath);
		strcat(incpath, INCDIR);
		av[0] = (char *) cppcmd;
		av[1] = "-E";
		av[2] = incpath;
		/*
		 * This compiler only ever targets the machine it is running
		 * on, so it says so.  Headers that have to choose between
		 * real hardware and a host stub - mmb_gpio.h picking the pin
		 * REGISTERS over a do-nothing stub - otherwise have nothing
		 * to test: the cross builds define MM_PC3 for bcrun and
		 * MM_FCC for the host gates, and until now the on-board cc
		 * defined neither.  A generated program then compiled, ran,
		 * and drove no pins at all, which is the worst way for this
		 * to be wrong.
		 */
		av[3] = "-DMM_PC3";
		av[4] = src;
		av[5] = NULL;
		run(av, NULL, ppfile);
	} else {
		fprintf(stderr, "cc: no %s - compiling without the "
			"preprocessor, so no directives and no comments\n",
			cppcmd);
		strcpy(ppfile, src);	/* feed cc0 the source directly */
		pp_is_src = 1;		/* ... and do not delete it */
	}

	av[0] = pass("cc0");
	av[1] = NULL;
	run(av, ppfile, tokfile);

	/*
	 * cc1 prints a dot per function.  They go to ITS stderr, and have
	 * to: its stdout is the IR, so a dot written there would corrupt
	 * the object.  That means a caller cannot redirect them away, and
	 * "cc prog.c > /dev/null &" still scribbled over the screen.
	 * Only the driver knows whether progress is wanted, so it decides
	 * the usual way - if our own output is not going to a terminal,
	 * nobody is watching it, and cc1 is told to keep quiet.
	 */
	av[0] = pass("cc1");
	av[1] = isatty(1) ? NULL : "-q";
	av[2] = NULL;
	run(av, tokfile, irfile);

	/* cc2 takes the symbol scratch file, the cpu, and a debug flag */
	av[0] = pass("cc2");
	av[1] = symfile;
	av[2] = "armm0";
	av[3] = "0";
	av[4] = NULL;
	put_shebang = 1;
	run(av, irfile, out);
	chmod(out, 0755);

	cleanup();
	return 0;
}
