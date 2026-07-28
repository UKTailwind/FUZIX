/* A tiny RPN calculator: switch, char pointers, a stack in an array,
   string walking. Evaluates a couple of fixed expressions. */
int printf();

int stack[16];
int sp;

void push(int v)
{
	stack[sp] = v;
	sp++;
}

int pop(void)
{
	sp--;
	return stack[sp];
}

int eval(char *p)
{
	int a, b;

	sp = 0;
	while (*p) {
		char c = *p;
		if (c >= '0' && c <= '9') {
			push(c - '0');
		} else {
			switch (c) {
			case '+': b = pop(); a = pop(); push(a + b); break;
			case '-': b = pop(); a = pop(); push(a - b); break;
			case '*': b = pop(); a = pop(); push(a * b); break;
			case '/': b = pop(); a = pop(); push(b ? a / b : 0); break;
			case ' ': break;
			default:  printf("bad op %c\n", c); break;
			}
		}
		p++;
	}
	return pop();
}

int main(void)
{
	printf("3 4 +      = %d\n", eval("34+"));
	printf("9 5 - 2 *  = %d\n", eval("95-2*"));
	printf("8 2 / 3 +  = %d\n", eval("82/3+"));
	printf("7 7 * 9 -  = %d\n", eval("77*9-"));
	return eval("34+");
}
