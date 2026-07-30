#include <stdio.h>

int c42(void) { return 42; }
int cneg(void) { return -1234; }
int cbig(void) { return 305419896; }

int main(void)
{
	printf("%d %d %d\n", c42(), cneg(), cbig());
	return 0;
}
