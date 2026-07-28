/* Reaching the platform from bytecode: no header needed beyond a
   declaration - an undefined name becomes a runtime call. time_us()
   is the SDK's 64-bit microsecond counter, via the kernel's ADVAL
   ioctl, truncated to 31 bits. */
int printf();
int time_us();
int adval();

char flags[2000];

int sieve(void)
{
	int i, j, count = 0;

	for (i = 0; i < 2000; i++)
		flags[i] = 1;
	flags[0] = 0;
	flags[1] = 0;
	for (i = 2; i < 2000; i++)
		if (flags[i])
			for (j = i + i; j < 2000; j += i)
				flags[j] = 0;
	for (i = 0; i < 2000; i++)
		if (flags[i])
			count++;
	return count;
}

int main(void)
{
	int t0, t1, n, reps;

	printf("time_us reads %d\n", time_us());

	t0 = time_us();
	reps = 0;
	while (reps < 5) {
		n = sieve();
		reps++;
	}
	t1 = time_us();

	printf("%d primes below 2000\n", n);
	printf("5 sieves in %d us (%d us each)\n", t1 - t0, (t1 - t0) / 5);
	printf("joystick adval(0) = %d\n", adval(0));
	return n;
}
