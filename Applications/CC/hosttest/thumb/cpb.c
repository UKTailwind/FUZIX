#include <stdio.h>

int gv;
short gs;
char gc;
unsigned char guc;
unsigned short gus;

int id(int x) { return x; }
int second(int a, int b) { return b; }
int setg(int v) { gv = v; return gv; }
int lv(void) { int x; x = 7; return x; }
char cget(char *p) { return *p; }
unsigned char ucget(unsigned char *p) { return *p; }
short sget(short *p) { return *p; }
unsigned short usget(unsigned short *p) { return *p; }
void cput(char *p, char v) { *p = v; }
void sput(short *p, short v) { *p = v; }

int main(void)
{
	printf("%d %d %d %d\n", id(123), second(9, 44), setg(-99), lv());
	gc = 'A' + 2;
	gs = -321;
	guc = 200;
	gus = 60000;
	printf("%d %d %d %d\n", cget(&gc), sget(&gs), ucget(&guc), usget(&gus));
	cput(&gc, 'Z');
	sput(&gs, 1234);
	printf("%d %d %d\n", cget(&gc), sget(&gs), gv);
	return 0;
}
