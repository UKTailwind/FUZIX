/* switch inside a while loop, nothing else */
int printf();

int main(void)
{
	int i = 0;
	int n = 0;

	while (i < 3) {
		switch (i) {
		case 0: n += 1; break;
		case 1: n += 10; break;
		default: n += 100; break;
		}
		i++;
	}
	printf("n=%d\n", n);
	return n;
}
