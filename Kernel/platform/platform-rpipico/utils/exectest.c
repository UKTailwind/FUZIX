#include <stdio.h>
#include <unistd.h>
#include <errno.h>

int main(int argc, char *argv[])
{
	char *av[3];
	if (argc < 2) {
		printf("usage: exectest prog\n");
		return 1;
	}
	av[0] = argv[1];
	av[1] = NULL;
	execv(argv[1], av);
	printf("execv failed errno %d\n", errno);
	return 1;
}
