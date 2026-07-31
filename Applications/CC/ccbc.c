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
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define LIBPATH		"/usr/lib/cc/"
#define CMD_CPP		"/usr/bin/cpp"
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

static void cleanup(void)
{
	if (!keep) {
		if (*ppfile && !pp_is_src)
			unlink(ppfile);
		if (*tokfile)
			unlink(tokfile);
		if (*irfile)
			unlink(irfile);
	}
	if (*symfile)
		unlink(symfile);
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
		fprintf(stderr, " >%s\n", out);
	}

	if (in) {
		fdin = open(in, O_RDONLY);
		if (fdin == -1) {
			perror(in);
			fatal();
		}
	}
	fdout = open(out, O_RDWR | O_CREAT | O_TRUNC, 0666);
	if (fdout == -1) {
		perror(out);
		fatal();
	}
	if (put_shebang) {
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
		dup2(fdout, 1);
		close(fdout);
		execv(argv[0], argv);
		perror(argv[0]);
		_exit(255);
	}
	if (fdin != -1)
		close(fdin);
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
			"usage: cc [-o out] [-v] [-k] [-Ldir] file.c\n");
		return 1;
	}

	if (*out == 0)
		basename_to(out, src, ".bc");
	basename_to(ppfile, src, ".pp");
	basename_to(tokfile, src, ".tok");
	basename_to(irfile, src, ".ir");
	strcpy(symfile, ".symtmp");

	signal(SIGINT, onsig);
	signal(SIGHUP, onsig);
	signal(SIGTERM, onsig);

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
		av[3] = src;
		av[4] = NULL;
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

	av[0] = pass("cc1");
	av[1] = NULL;
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
