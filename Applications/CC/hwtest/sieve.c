/* Sieve of Eratosthenes - arrays, char storage, nested loops, compound
   assignment, and the count printed at the end. Primes below 200. */
int printf();

char flags[200];

int main(void)
{
	int i, j, count;

	i = 0;
	while (i < 200) {
		flags[i] = 1;
		i++;
	}
	flags[0] = 0;
	flags[1] = 0;

	for (i = 2; i < 200; i++) {
		if (flags[i]) {
			for (j = i + i; j < 200; j += i)
				flags[j] = 0;
		}
	}

	count = 0;
	for (i = 0; i < 200; i++) {
		if (flags[i]) {
			printf("%d ", i);
			count++;
		}
	}
	printf("\n%d primes below 200\n", count);
	return count;
}
