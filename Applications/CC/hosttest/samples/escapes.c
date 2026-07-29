/*
 * Character escapes, in character constants and in string literals.
 *
 * c-testsuite 00177.
 *
 * \x read exactly two digits and then assembled them the wrong way
 * round, so '\x40' came out as 0x04 and every hex escape anywhere was
 * silently wrong. C89 also puts no limit on the number of hex digits,
 * unlike the three of an octal escape, so '\x1' is valid and was being
 * rejected.
 */

int printf();

char *hexstr = "\x41\x42\x43";
char *octstr = "\101\102\103";

int main(void)
{
	/* octal: one, two and three digits, and the maximum */
	printf("a %d %d %d %d\n", '\1', '\10', '\100', '\377');

	/* hex: one and two digits, and the ends of the range */
	printf("b %d %d %d %d\n", '\x1', '\x01', '\x0e', '\x10');
	printf("c %d %d %d\n", '\x40', '\x7f', '\xff');

	/* upper and lower case hex digits must agree */
	printf("d %d %d\n", '\xAB', '\xab');

	/* the simple escapes */
	printf("e %d %d %d %d %d %d %d\n", '\a', '\b', '\f', '\n', '\r',
	       '\t', '\v');
	printf("f %d %d %d\n", '\\', '\'', '\"');

	/* in strings, where the escape is followed by more text - the
	   digit run must stop at the first non hex digit */
	printf("g [%s]\n", "\x41\x42\x43");
	printf("h [%s]\n", hexstr);
	printf("i [%s]\n", octstr);
	printf("j [%s]\n", "A\x42" "C");
	printf("k [%s]\n", "tab\there");
	printf("l [%s]\n", "\x41 \x42 \x43");

	/* an escape immediately before a non hex letter */
	printf("m [%s]\n", "\x41z");

	return 0;
}
