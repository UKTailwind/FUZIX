/*
 * How much room is there, and how much of it does bcrun cost?
 *
 *   forkroom <kb> [prog]
 *
 * Claims <kb> of heap, touches every block of it so it is really ours,
 * then forks and execs [prog] (default /bin/true) and reports whether
 * the fork succeeded.  Run it up a ramp of sizes and the last success
 * is the practical ceiling.
 *
 * Two things make that ceiling interesting rather than obvious:
 * fork() copies the parent, and swapneeded() may swap out OTHER
 * processes but never the one forking - so a process of size S needs S
 * more available at that instant, out of a 316K pool.
 *
 * Build it BOTH ways to separate the two costs:
 *   cc forkroom.c          -> forkroom.bc, runs under bcrun (~90K + the
 *                             loader mallocs), which is what every
 *                             on-board C program and every translated
 *                             BASIC program pays
 *   cross-compiled ELF     -> runs on its own, the bare ceiling
 * The difference between the two ramps is bcrun overhead, measured
 * rather than guessed.
 *
 * Pass "ps" as the second argument to see the parent listed at the
 * moment it forks - that is bcrun resident, not the file size.
 *
 * fflush is explicit throughout: this libc does not flush stdio at
 * exit, so a redirected printf is otherwise lost.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

int main(int argc, char **argv)
{
	long kb = (argc > 1) ? atol(argv[1]) : 0;
	const char *prog = (argc > 2) ? argv[2] : "/bin/true";
	char *p = NULL;
	long i;
	pid_t pid;
	int status = 0;

	if (kb > 0) {
		p = malloc((size_t)kb * 1024);
		if (p == NULL) {
			printf("%ld KB: malloc FAILED\n", kb);
			fflush(stdout);
			return 1;
		}
		for (i = 0; i < kb * 1024; i += 512)
			p[i] = (char)i;
	}
	fflush(stdout);

	pid = fork();
	if (pid < 0) {
		printf("%ld KB: fork FAILED\n", kb);
		fflush(stdout);
		return 2;
	}
	if (pid == 0) {
		execl(prog, prog, (char *)0);
		_exit(127);
	}
	while (waitpid(pid, &status, 0) < 0)
		;
	printf("%ld KB: fork ok\n", kb);
	fflush(stdout);
	return 0;
}
