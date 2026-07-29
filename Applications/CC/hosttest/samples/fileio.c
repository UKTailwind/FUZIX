/*
 * stdio: fopen and friends.
 *
 * c-testsuite 00187. bcrun already had the raw syscalls - open, read,
 * write, close, lseek - onto real Fuzix descriptors; this is the stdio
 * layer over them.
 *
 * A FILE * is the descriptor plus one, so stdin, stdout and stderr are
 * the constants 1, 2 and 3. They have to be constants: the bytecode
 * object format can import a *function* from the runtime but not a
 * variable, so they could not be real objects. The same limit is why
 * "&fprintf" is refused outright at load time - a library symbol has an
 * index, not an address, and storing one where a code address belongs
 * would produce an indirect call into nowhere.
 *
 * The file is created in the working directory and removed at the end,
 * so this can be run repeatedly and leaves nothing behind.
 */

#include <stdio.h>

int main(void)
{
	FILE *f;
	char buf[32];
	int c, n;
	long pos;

	/* write, then read the whole thing back */
	f = fopen("fio.tmp", "w");
	if (f == NULL) {
		printf("open for write failed\n");
		return 1;
	}
	n = fwrite("hello\nworld\n", 1, 12, f);
	printf("wrote %d\n", n);
	fclose(f);

	f = fopen("fio.tmp", "r");
	if (f == NULL) {
		printf("open for read failed\n");
		return 1;
	}
	n = fread(buf, 1, 5, f);
	buf[5] = 0;
	printf("read %d [%s]\n", n, buf);
	fclose(f);

	/* character at a time, to end of file */
	f = fopen("fio.tmp", "r");
	printf("chars");
	while ((c = fgetc(f)) != EOF)
		printf(" %d", c < ' ' ? -1 : c);
	printf("\n");
	printf("eof %d\n", feof(f) ? 1 : 0);
	fclose(f);

	/* getc is the same thing */
	f = fopen("fio.tmp", "r");
	c = getc(f);
	printf("getc %d\n", c);
	fclose(f);

	/* line at a time */
	f = fopen("fio.tmp", "r");
	while (fgets(buf, sizeof(buf), f) != NULL)
		printf("line [%s]", buf);
	fclose(f);

	/* a short buffer, so a line comes back in pieces */
	f = fopen("fio.tmp", "r");
	while (fgets(buf, 4, f) != NULL)
		printf("piece [%s]\n", buf);
	fclose(f);

	/* seek and tell */
	f = fopen("fio.tmp", "r");
	fseek(f, 6, SEEK_SET);
	pos = ftell(f);
	n = fread(buf, 1, 5, f);
	buf[5] = 0;
	printf("seek %d %d [%s]\n", (int) pos, n, buf);
	rewind(f);
	printf("rewound %d\n", (int) ftell(f));
	fclose(f);

	/* append */
	f = fopen("fio.tmp", "a");
	fputs("again\n", f);
	fclose(f);
	f = fopen("fio.tmp", "r");
	n = 0;
	while (fgetc(f) != EOF)
		n++;
	printf("after append %d\n", n);
	fclose(f);

	/* fprintf to a file, read back */
	f = fopen("fio.tmp", "w");
	fprintf(f, "%d %s %f\n", 42, "xy", 1.5);
	fclose(f);
	f = fopen("fio.tmp", "r");
	fgets(buf, sizeof(buf), f);
	printf("fprintf [%s]", buf);
	fclose(f);

	/* fputc, and fprintf to stdout, which must stay in step with
	   printf's own output */
	f = fopen("fio.tmp", "w");
	fputc('a', f);
	fputc('b', f);
	fclose(f);
	f = fopen("fio.tmp", "r");
	fgets(buf, sizeof(buf), f);
	printf("fputc [%s]\n", buf);
	fclose(f);

	printf("before ");
	fprintf(stdout, "middle ");
	printf("after\n");

	/* opening something that is not there */
	f = fopen("fio.nosuch", "r");
	printf("missing %d\n", f == NULL ? 1 : 0);

	remove("fio.tmp");
	f = fopen("fio.tmp", "r");
	printf("removed %d\n", f == NULL ? 1 : 0);

	return 0;
}
