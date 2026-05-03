
#define uputp  uputw			/* Copy user pointer type */
#define ugetp  ugetw			/* between user and kernel */
#define uputi  uputw			/* Copy user int type */
#define ugeti  ugetw			/* between user and kernel */

extern void *memcpy(void *, void *, size_t);
extern void *memset(void *, int, size_t);
extern size_t strlen(const char *);

/* High byte is saved, low byte is a mystery so take worst case. Also allow
   a bit less as C stack is not return stack */
#define brk_limit() ((((uint16_t)udata.u_syscall_sp) | 0xFF) - 384)

#define staticfast	static

/* User's structure for times() system call */
typedef unsigned long clock_t;

typedef union {            /* this structure is endian dependent */
    clock_t  full;         /* 32-bit count of ticks since boot */
    struct {
      uint16_t low;         /* 16-bit count of ticks since boot */
      uint16_t high;
    } h;
} ticks_t;

/* No useful behaviour for unused parameters */
#define used(x)

#define cpu_to_le16(x)	(x)
#define le16_to_cpu(x)	(x)
#define cpu_to_le32(x)	(x)
#define le32_to_cpu(x)	(x)

/* No support for inline */
#define inline

#define ntohs(x)	((((x) & 0xFF) << 8) | (((x) & 0xFF00) >> 8))

/* We need to look at this once the compiler is a bit more mature and see
   if adding register pointer vars is a win - probably it will be */
#define regptr register

/* fcc is bright enough to partly optimise this but not fully so do it
   by hand */
#define HIBYTE32(x)	(((uint8_t *)&(x))[3])

#define __packed
#define barrier()

#define __fastcall
