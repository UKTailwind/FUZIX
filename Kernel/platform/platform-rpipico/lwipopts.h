/*
 * lwIP configuration for the Pico Computer 3 Fuzix port.
 *
 * WHERE lwIP's MEMORY LIVES IS THE WHOLE DESIGN HERE.
 *
 * Step 0 measured the stack with everything static, in SRAM, at the
 * smallest configuration that could still work: 15,360 bytes of .bss,
 * on a kernel whose RAM region has 1,952 bytes spare.  Two thirds of
 * that was the pbuf pool and lwIP's own heap.
 *
 * So both now come out of PSRAM instead:
 *
 *   MEMP_MEM_MALLOC     every memp pool - pbufs, PCBs, TCP segments -
 *                       is allocated from lwIP's heap rather than
 *                       being its own static array.
 *   LWIP_RAM_HEAP_POINTER
 *                       that heap is pc3_lwip_heap, which net_cyw43.c
 *                       places in the PSRAM window with the SDK's
 *                       __uninitialized_psram, exactly as display.c
 *                       places the spare framebuffers.  psram.c's
 *                       psram_static_len() then keeps the PSRAM block
 *                       device and the arena allocator above it, so
 *                       nothing has to be told about it twice.
 *
 * This is safe because NO DMA EVER TOUCHES A PBUF: cyw43_lwip.c fills
 * a receive pbuf with pbuf_take (a memcpy) and transmits by copying
 * out through the driver's own SRAM buffer.  The only thing that must
 * stay in SRAM is cyw43_state, which holds those bus buffers, and it
 * is the driver's own static.
 *
 * MMBasic did NOT do this, and the reason does not transfer: its lwIP
 * heap has to stay on the C heap because the MMBasic heap is wiped by
 * InitHeap(true) on every program RUN, while lwIP holds state (the
 * netif's DHCP struct) that outlives a RUN.  A kernel arena has no
 * such event.  What MMBasic does prove is that network-path data works
 * from PSRAM at speed.
 *
 * NO_SYS: there is no RTOS under this.  lwIP runs from the kernel's
 * existing pump (plt_idle, and PendSV when a spinning process starves
 * it) exactly as tuh_task does, so the raw API is the only API and
 * sockets/netconn stay off - Fuzix has its own socket layer.
 */
#ifndef _LWIPOPTS_H
#define _LWIPOPTS_H

#define NO_SYS                      1
#define LWIP_SOCKET                 0
#define LWIP_NETCONN                0
#define SYS_LIGHTWEIGHT_PROT        0

#define MEM_ALIGNMENT               4
#define MEM_LIBC_MALLOC             0

/* The two lines that move the stack into PSRAM.  pc3_lwip_heap is
   defined in net_cyw43.c; the array is MEM_SIZE plus lwIP's own two
   boundary records and alignment slack. */
#define MEMP_MEM_MALLOC             1
extern unsigned char pc3_lwip_heap[];
#define LWIP_RAM_HEAP_POINTER       pc3_lwip_heap

/* 64K, where the SRAM version could afford 4K.  With MEMP_MEM_MALLOC
   this one heap now serves pbufs, PCBs and TCP segments, so it has to
   be big enough for all of them at once - and out here it is 0.8% of
   the PSRAM, against 19% of the process pool if it were in SRAM.
   The MEMP_NUM_* limits below stop applying once the pools come from
   the heap; they are kept as documentation of the intended shape and
   as the numbers to restore if this ever has to go back to SRAM. */
#define MEM_SIZE                    (128 * 1024)

/*
 * TLS.
 *
 * altcp is the abstraction that lets one piece of code drive either a
 * plain TCP connection or a TLS one, and MMBasic's WEB builds turn it
 * on unconditionally for exactly that reason: the client path is the
 * same whether or not the session is encrypted.  net_lwip.c does the
 * same, so there is one implementation of connect/read/write/close.
 *
 * MEM_SIZE is now the TLS budget as well as the packet budget, because
 * altcp_tls installs its own mbedtls allocator over lwIP's heap.  One
 * session with a 16K record buffer peaks around 40K; 128K leaves room
 * for a certificate chain being parsed at the same time, and it is in
 * PSRAM, where 128K is 1.6% of what is there.
 */
#define LWIP_ALTCP                  1
#define LWIP_ALTCP_TLS              1
#define LWIP_ALTCP_TLS_MBEDTLS      1

/* Protocols.  DNS is off: Fuzix's libc has its own resolver
   (Library/libs/resolv.c) and the netd applications use it, so a
   second resolver in the kernel would be duplicate code AND duplicate
   memory.  RAW is on because ping(1) opens SOCK_RAW. */
#define LWIP_IPV4                   1
#define LWIP_IPV6                   0
#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_ICMP                   1
#define LWIP_RAW                    1
#define LWIP_UDP                    1
#define LWIP_TCP                    1
#define LWIP_DHCP                   1
/*
 * LWIP_DNS is on for STORAGE ONLY, and that is the whole of it.
 *
 * The resolver lives in Fuzix's libc (Library/libs/resolv.c), which
 * every netd application already uses, and a second one in the kernel
 * would be duplicate code and duplicate memory.  What lwIP is for here
 * is the DHCP half: dhcp.c only asks the server for a DNS list, and
 * only keeps what comes back, when LWIP_DNS is compiled in.  Without
 * it there is nothing to write into /etc/resolv.conf and a user has to
 * know their own nameserver by heart.
 *
 * So: one table entry, because nothing in this kernel ever does a
 * lookup, and two servers, because that is what a lease carries.
 */
#define LWIP_DNS                    1
#define DNS_TABLE_SIZE              1
#define DNS_MAX_SERVERS             2
#define LWIP_IGMP                   0
#define LWIP_NETIF_HOSTNAME         1
#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LINK_CALLBACK    1
#define LWIP_DHCP_DOES_ACD_CHECK    0

/*
 * SO_REUSE, and net_cyw43.c sets SOF_REUSEADDR on every TCP socket.
 *
 * Not a preference: Fuzix has no setsockopt, so a program CANNOT ask
 * for it, and without it lwIP's tcp_bind refuses any port that still
 * has a connection in TIME_WAIT - it walks tcp_tw_pcbs unless the flag
 * is set.  The board proved the consequence: a server that accepts one
 * connection and exits cannot be restarted for two minutes, reporting
 * "bind: Address already in use" with no way to override it.
 *
 * The thing SO_REUSEADDR guards against is a stray segment from the
 * old connection landing in a new one on the same four-tuple.  Against
 * a server that cannot restart, that is the better risk to take, and
 * it is the default every Unix server sets by hand anyway.
 *
 * TCP only.  UDP pcbs do not get the flag, so two sockets still cannot
 * share a datagram port.
 */
#define SO_REUSE                    1

#define PBUF_POOL_SIZE              16
#define TCP_MSS                     1460
#define TCP_WND                     (4 * TCP_MSS)
#define TCP_SND_BUF                 (4 * TCP_MSS)
#define TCP_SND_QUEUELEN            ((4 * TCP_SND_BUF + TCP_MSS - 1) / TCP_MSS)
#define MEMP_NUM_TCP_SEG            32

/* NSOCKET in Fuzix is 8, so there is no point having room for more
   than that plus the listeners they came from. */
#define MEMP_NUM_TCP_PCB            8
#define MEMP_NUM_TCP_PCB_LISTEN     2
#define MEMP_NUM_UDP_PCB            4
#define MEMP_NUM_RAW_PCB            2

#define LWIP_STATS                  0
#define LWIP_DEBUG                  0

#define LWIP_RAND() ((u32_t)rand())

#endif /* _LWIPOPTS_H */
