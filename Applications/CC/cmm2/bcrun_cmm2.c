/*
 *	bcrun on the Colour Maximite 2: the machine's side (bcrun_cmm2.h).
 *
 *	Built with the CMM2 firmware's tools/sdk, whose cmm2.h gives the
 *	services table and the loadable program's start block.
 */
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include "cmm2.h"
#include "bcrun_cmm2.h"

/* Left to the C library after the program's heap is taken: stdio's
   buffers and the few blocks bcrun allocates once it is running. */
#define CMM2_KEEP	(64UL << 10)

unsigned long cmm2_vm_heap(unsigned long *len)
{
	char *top = (char *)cmm2_start->heap + cmm2_start->heap_size;
	char *brk = sbrk(0);
	unsigned long n;
	void *p;

	if (brk == (char *)-1 || top - brk < (long)(CMM2_KEEP + 65536))
		return 0;
	n = ((unsigned long)(top - brk) - CMM2_KEEP) & ~7UL;
	p = cmm2_heap_take(n);
	if (p == NULL)
		return 0;
	*len = n;
	return (unsigned long)(uintptr_t)p;
}

long long cmm2_us64(void)
{
	return (long long)cmm2->micros();
}

void cmm2_check(void)
{
	static uint64_t next;
	uint64_t t = cmm2->micros();

	if (t < next)
		return;
	next = t + 10000;
	if (cmm2->poll())
		exit(0);	/* MMBasic says Ctrl-C was pressed */
}

void cmm2_code_ready(void *code, unsigned long len)
{
	uintptr_t a = (uintptr_t)code & ~31UL;
	uintptr_t e = (uintptr_t)code + len;

	__asm volatile("dsb" ::: "memory");
	for (; a < e; a += 32)
		*(volatile uint32_t *)0xE000EF68 = a;	/* DCCMVAC */
	__asm volatile("dsb" ::: "memory");
	*(volatile uint32_t *)0xE000EF50 = 0;		/* ICIALLU */
	__asm volatile("dsb\n\tisb" ::: "memory");
}

CMM2_AAPCS double cmm2_dadd(double a, double b) { return a + b; }
CMM2_AAPCS double cmm2_dsub(double a, double b) { return a - b; }
CMM2_AAPCS double cmm2_dmul(double a, double b) { return a * b; }
CMM2_AAPCS double cmm2_ddiv(double a, double b) { return a / b; }
CMM2_AAPCS int cmm2_dcmpeq(double a, double b) { return a == b; }
CMM2_AAPCS int cmm2_dcmplt(double a, double b) { return a < b; }
CMM2_AAPCS int cmm2_dcmple(double a, double b) { return a <= b; }
CMM2_AAPCS int cmm2_dcmpge(double a, double b) { return a >= b; }
CMM2_AAPCS int cmm2_dcmpgt(double a, double b) { return a > b; }

/*
 *	No network and no device ioctls on the CMM2: the libcalls bcrun
 *	offers a program still bind, and fail as a machine without them
 *	would have them fail.
 */
int socket(int d, int t, int p) { (void)d; (void)t; (void)p; errno = ENOSYS; return -1; }
int connect(int s, const struct sockaddr *a, socklen_t l) { (void)s; (void)a; (void)l; errno = ENOSYS; return -1; }
int bind(int s, const struct sockaddr *a, socklen_t l) { (void)s; (void)a; (void)l; errno = ENOSYS; return -1; }
int listen(int s, int n) { (void)s; (void)n; errno = ENOSYS; return -1; }
int accept(int s, struct sockaddr *a, socklen_t *l) { (void)s; (void)a; (void)l; errno = ENOSYS; return -1; }

ssize_t sendto(int s, const void *b, size_t n, int f, const struct sockaddr *a, socklen_t l)
{
	(void)s; (void)b; (void)n; (void)f; (void)a; (void)l;
	errno = ENOSYS;
	return -1;
}

ssize_t recvfrom(int s, void *b, size_t n, int f, struct sockaddr *a, socklen_t *l)
{
	(void)s; (void)b; (void)n; (void)f; (void)a; (void)l;
	errno = ENOSYS;
	return -1;
}

int ioctl(int fd, int rq, ...) { (void)fd; (void)rq; errno = ENOTTY; return -1; }
int fcntl(int fd, int c, ...) { (void)fd; (void)c; errno = ENOSYS; return -1; }
