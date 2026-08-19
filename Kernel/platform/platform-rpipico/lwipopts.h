/*
 * lwIP configuration for the Pico Computer 3 Fuzix port.
 *
 * This is deliberately the FLOOR, not a comfortable configuration: the
 * first question this port has to answer about networking is what it
 * costs in SRAM, and the honest way to ask it is to measure the
 * smallest stack that could still do the job and then decide what to
 * buy back.  See PC3-NET-PLAN.md.
 *
 * The numbers that matter:
 *
 *   PBUF_POOL_SIZE * PBUF_POOL_BUFSIZE   the receive pool, static .bss
 *   MEM_SIZE                             lwIP's own heap, static .bss
 *   the MEMP_NUM_* pools                 one static array each
 *
 * All three are static arrays here (MEM_LIBC_MALLOC is 0 and
 * MEMP_MEM_MALLOC is 0), which is what makes them measurable in the
 * map before a single packet moves.  Moving them into PSRAM is a
 * later, separate experiment - LWIP_RAM_HEAP_POINTER plus
 * MEMP_MEM_MALLOC - and it is deliberately NOT done here, so that the
 * baseline number is a baseline.
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
#define MEMP_MEM_MALLOC             0

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
#define LWIP_DNS                    0
#define LWIP_IGMP                   0
#define LWIP_NETIF_HOSTNAME         1
#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LINK_CALLBACK    1
#define LWIP_DHCP_DOES_ACD_CHECK    0

/* The floor.  A working configuration will want more of both - each
   pbuf is about 1.5K and MEM_SIZE bounds how much TCP can have queued
   - but every byte added here is a byte off the process pool until
   the PSRAM experiment says otherwise. */
#define MEM_SIZE                    4000
#define PBUF_POOL_SIZE              4
#define TCP_MSS                     1460
#define TCP_WND                     (2 * TCP_MSS)
#define TCP_SND_BUF                 (2 * TCP_MSS)
#define TCP_SND_QUEUELEN            ((4 * TCP_SND_BUF + TCP_MSS - 1) / TCP_MSS)
#define MEMP_NUM_TCP_SEG            16

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
