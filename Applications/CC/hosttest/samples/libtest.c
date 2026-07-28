/* Exercise the new runtime: malloc/free, the string and memory
   functions, and file I/O. */
int printf();
char *malloc();
char *strcpy();
char *strcat();
int strlen();
int strcmp();
int memcmp();
char *memset();
char *memcpy();
char *strchr();
int atoi();
int open();
int creat();
int write();
int read();
int close();
int unlink();
int free();

char scratch[64];

int main(void)
{
	char *a, *b, *big;
	int fd, n;

	/* --- strings ------------------------------------------------- */
	strcpy(scratch, "Pico");
	strcat(scratch, " Computer");
	strcat(scratch, " 3");
	printf("str : '%s' len %d\n", scratch, strlen(scratch));
	printf("cmp : %d %d\n", strcmp(scratch, "Pico Computer 3"),
	       strcmp(scratch, "zzz"));
	printf("chr : %s\n", strchr(scratch, 'C'));
	printf("atoi: %d\n", atoi("1234"));

	/* --- memory -------------------------------------------------- */
	memset(scratch, '=', 8);
	scratch[8] = 0;
	printf("set : %s\n", scratch);

	/* --- heap ---------------------------------------------------- */
	a = malloc(100);
	b = malloc(200);
	if (!a || !b) {
		printf("malloc failed\n");
		return 1;
	}
	strcpy(a, "first block");
	strcpy(b, "second block");
	printf("heap: %s / %s\n", a, b);
	free(a);
	big = malloc(4000);
	if (!big) {
		printf("big malloc failed\n");
		return 1;
	}
	memset(big, 'x', 4000);
	big[3999] = 0;
	printf("big : %d bytes, last-1 '%c'\n", strlen(big), big[3998]);
	free(big);
	free(b);

	/* --- files --------------------------------------------------- */
	fd = creat("bctest.txt");
	if (fd < 0) {
		printf("creat failed\n");
		return 1;
	}
	write(fd, "hello file\n", 11);
	close(fd);

	fd = open("bctest.txt", 0);
	memset(scratch, 0, 64);
	n = read(fd, scratch, 63);
	close(fd);
	printf("file: read %d bytes: %s", n, scratch);
	unlink("bctest.txt");

	return 0;
}
