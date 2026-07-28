/* Strings and structs: our own strlen/strcpy/reverse, a struct array,
   and pointer arithmetic. No library beyond printf. */
int printf();

struct rec {
	char *name;
	int   value;
};

struct rec table[4];
char buf[32];

int slen(char *s)
{
	char *p = s;
	while (*p)
		p++;
	return p - s;
}

void scopy(char *d, char *s)
{
	while (*s) {
		*d = *s;
		d++;
		s++;
	}
	*d = 0;
}

void reverse(char *s)
{
	int i = 0;
	int j = slen(s) - 1;
	while (i < j) {
		char t = s[i];
		s[i] = s[j];
		s[j] = t;
		i++;
		j--;
	}
}

int main(void)
{
	int i, total;

	table[0].name = "one";   table[0].value = 1;
	table[1].name = "three"; table[1].value = 3;
	table[2].name = "five";  table[2].value = 5;
	table[3].name = "seven"; table[3].value = 7;

	total = 0;
	for (i = 0; i < 4; i++) {
		printf("%-2d %s = %d\n", i, table[i].name, table[i].value);
		total += table[i].value;
	}
	printf("total %d\n", total);

	scopy(buf, "Pico Computer 3");
	printf("copied  : %s (len %d)\n", buf, slen(buf));
	reverse(buf);
	printf("reversed: %s\n", buf);
	return total;
}
