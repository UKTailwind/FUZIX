/* a is 255, so a += 1 must wrap a to 0 and leave b alone.
   Correct answer: a=0, b=2, so 0*100 + 2 = 2.
   A 32-bit read-modify-write carries out of the char and hits b. */
char a = 255;
char b = 2;

int main(void)
{
	a += 1;
	return a * 100 + b;
}
