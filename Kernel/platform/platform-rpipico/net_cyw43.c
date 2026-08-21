/*
 * sockets on top of them yet.  PC3-NET-PLAN.md step 1.
 *
 * What this provides is three ioctls - NETIOC_UP, NETIOC_STATUS,
 * NETIOC_DOWN, dispatched from misc.c - and a poll hook the kernel's
 * existing pump calls.  A userland program (utils/wifi.c) joins a
 * network and reads back the DHCP lease.  Nothing here knows what a
 * socket is; that is step 4 and it goes underneath Fuzix's own socket
 * layer rather than beside it.
 *
 * NO FUZIX KERNEL HEADERS IN THIS FILE, AND THAT IS DELIBERATE.
 *
 * lwIP and the pico-sdk drag in newlib's <stdio.h> and <sys/time.h>,
 * whose ssize_t and time_t are not the kernel's - including both sets
 * in one translation unit is a wall of conflicting-types errors, which
 * is exactly what happened when this was tried.  So the SDK world
 * lives here behind plain int returns, and misc.c does the uget/uput
 * and turns those into errno values.  The only things reached across
 * the line are two functions declared by hand below.
 *
 * THE RADIO IS NOT TOUCHED UNTIL SOMEBODY ASKS.
 *
 * It is on GP23 (WL_REG_ON), GP24 (data and host wake), GP25 (CS) and
 * GP29 (clock).  On a Pico Computer 2 - the same kernel image, see
 * board.c - GP29 is the SD card's chip select and GP25 is the LED, so
 * bringing this up there would clock the card's chip select at MHz
 * while the filesystem is mounted.  Hence board_is_pc2(), checked at
 * NETIOC_UP rather than at boot: device_init() runs board_detect()
 * long after plt_init, so there is no honest answer to give at boot.
 *
 * WHERE lwIP's MEMORY LIVES: PSRAM, see lwipopts.h.  That is what
 * makes this cost the process pool one 4K block instead of five.
 *
 * WHAT BLOCKS: NETIOC_UP does the one-off cyw43_arch_init(), which
 * uploads about 230K of firmware to the chip over the PIO SPI link and
 * takes a few hundred milliseconds.  A Fuzix syscall is not preempted,
 * so the machine stops for that - including the USB keyboard pump.
 * Association itself does NOT block: it is started asynchronously and
 * userland polls NETIOC_STATUS, which is why joining a network does
 * not freeze the console for the length of a DHCP negotiation.
 */

#ifdef CONFIG_PC3_NET

#include <string.h>
#include "pico/cyw43_arch.h"
#include "pico/cyw43_driver.h"
#include <hardware/clocks.h>
#include <pico/platform/sections.h>
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include "lwip/udp.h"
#include "lwip/pbuf.h"
#include "lwip/altcp.h"
#include "lwip/altcp_tcp.h"
#include "lwip/altcp_tls.h"
#include "lwip/tcp.h"
#include "mbedtls/ssl.h"
#include "lwip/raw.h"
#include "lwip/dns.h"
#include "mbedtls/platform_time.h"

/* The socket seam.  Only stdint.h behind it, so it is safe to include
   on this side of the header line - see net_lwip.h. */
#include <net_lwip.h>

#include "pico_ioctl.h"

/* The two things this file needs from the kernel side of the line. */
extern int board_is_pc2(void);
extern void kprintf(const char *fmt, ...);
/* Declared here rather than included: net_lwip.h is a kernel header and
   this file cannot have one (the SDK's ssize_t/time_t collide with
   Fuzix's).  These two are the seam for the stack diagnostic below. */
extern uint32_t *netlw_kstack_bottom(void);
extern uint32_t *netlw_kstack_top(void);

/*
 * pbufs, PCBs and TCP segments all come out of here.  It is placed in
 * the PSRAM window by the linker, the same mechanism display.c uses
 * for the spare framebuffers, and psram.c's psram_static_len() moves
 * the PSRAM block device and the arena allocator up above it without
 * being told.
 *
 * The extra bytes are lwIP's own heap boundary records plus slack for
 * the alignment mem_init() applies to the pointer it is given.
 * Undersizing this would not fail the build - lwIP would simply run
 * its heap off the end of the array and into the swap disc.
 */
unsigned char pc3_lwip_heap[MEM_SIZE + 64] __uninitialized_psram("lwip");

static uint8_t net_ready;       /* cyw43_arch_init has succeeded */
static uint8_t net_busy;        /* kernel-side call in progress */

/*
 *	two thread-context sites that already pump TinyUSB, for exactly
 *	the same reason: a spinning process must not be able to starve
 *	it, and it must never run in interrupt context.
 *
 *	cyw43_arch_poll() also runs lwIP's timers, because the poll
 *	async_context drives lwip_nosys.c's timeout worker; there is no
 *	second sys_check_timeouts() to make here.
 *
 *	net_busy is belt and braces.  The PendSV trampoline only fires
 *	for user-mode PCs so it cannot land inside a syscall, and
 *	plt_idle only runs with the kernel idle, so re-entering lwIP is
 *	not currently possible.  Step 4 puts lwIP calls on the syscall
 *	path, where it will be.
 */
/*
 * The kernel stack, and the canary that watches it.
 *
 * The pump runs on the CURRENT PROCESS'S kernel stack - the space
 * between struct u_data and PROGLOAD - and is reached from plt_idle, so
 * it is nested on top of whatever syscall just went to sleep.  With the
 * old 1.5K a TLS handshake ran straight out of the bottom of progbase
 * and into the process table; UDATA_BLKS in config.h now carries the
 * measured 2,460 bytes a handshake needs, with margin.
 *
 * The margin is not certain, though.  Certificate VERIFICATION has
 * never been measured - the handshake that produced 2,460 had no CA
 * bundle - and walking a chain is the deeper path.  So this leaves a
 * canary in the words immediately above udata: cheap enough to run on
 * every poll (128 stores, no interrupts held off - it sits kilobytes
 * below sp, so nothing can be using it), and it turns a silent
 * corruption into a line of text.
 *
 * That matters because the silent version is expensive.  It presented
 * as "pid = -1", a process table with a hole punched in it, and cost
 * two wrong guesses at a stack size before it was understood.
 *
 * PC3_NET_STACK_PROFILE turns this into the full profiler that produced
 * the figure: paint everything below sp and report each new high-water
 * mark.  It costs ~12us per poll with interrupts off, which delays the
 * tick and the audio and scanout IRQs, so it is not for normal use.
 * Note when reading it that paint must be laid down on EVERY poll: the
 * stack is at a fixed address but not with fixed contents, because this
 * port swaps the udata block in and out with the process image.
 * Painting once reads a context switch as a 17K call depth.
 */
#define PUMP_PAINT      0xA5A55A5Au
#define PUMP_MARGIN     64	/* bytes below sp left unpainted */
#define PUMP_CANARY     128	/* words of canary immediately above udata */

#ifdef PC3_NET_STACK_PROFILE
static uint32_t pump_deepest;
#endif
static uint8_t pump_hit;

void pc3_net_poll_c(void)
{
    uint32_t *lo, *hi, *p;
    uint32_t sp;
#ifdef PC3_NET_STACK_PROFILE
    uint32_t used, primask;
#endif

    if (!net_ready || net_busy)
        return;

    lo = netlw_kstack_bottom();
    __asm volatile ("mov %0, sp" : "=r" (sp));
    hi = (uint32_t *)(sp - PUMP_MARGIN);
#ifndef PC3_NET_STACK_PROFILE
    if (hi > lo + PUMP_CANARY)
        hi = lo + PUMP_CANARY;
#else
    __asm volatile ("mrs %0, primask" : "=r" (primask));
    __asm volatile ("cpsid i");
#endif

    for (p = lo; p < hi; p++)
        *p = PUMP_PAINT;
#ifdef PC3_NET_STACK_PROFILE
    if (!primask)
        __asm volatile ("cpsie i");
#endif

    net_busy = 1;
    cyw43_arch_poll();
    /* Loopback delivery.  LWIP_NETIF_LOOPBACK queues a packet sent to
       one of our own addresses and, under NO_SYS, nothing hands it on
       until this is called. */
    netif_poll_all();
    net_busy = 0;

    for (p = lo; p < hi && *p == PUMP_PAINT; p++)
        ;
#ifdef PC3_NET_STACK_PROFILE
    used = (uint32_t)netlw_kstack_top() - (uint32_t)p;
    if (used > pump_deepest) {
        pump_deepest = used;
        kprintf("kstack %u of %u\n", (unsigned)used,
                (unsigned)((uint32_t)netlw_kstack_top() - (uint32_t)lo));
    }
#else
    /* Once only: if the stack is this deep the machine is already in
       trouble and a message per poll would make it worse. */
    if (p != hi && !pump_hit) {
        pump_hit = 1;
        kprintf("net: kernel stack within %u bytes of udata - raise "
                "UDATA_BLKS\n",
                (unsigned)((uint32_t)p - (uint32_t)lo));
    }
#endif
}

/*
 *	The PIO clock divider for the CYW43439's SPI bus.
 *
 *	This has to be computed from clk_sys, not left alone: the SDK's
 *	default of 2 is tuned for ~125MHz; at 375 it runs the CYW43439
 *	well out of spec, and the symptom is "hdr mismatch" or an ioctl
 *	timeout rather than anything that looks like a clock problem.  This is
 *	MMBasic's algorithm, which the PC3's MicroPython also carries:
 *	aim for at most 100MHz, minimum divider 2.
 *
 *	It must be set BEFORE cyw43_arch_init, which is where
 *	cyw43_spi_init reads it.  This port never changes clk_sys after
 *	boot, so there is no live retune to do afterwards - if that ever
 *	changes, the running PIO state machines need retuning too, not
 *	just this stored value.
 */
static void net_set_pio_clkdiv(void)
{
    uint32_t khz = clock_get_hz(clk_sys) / 1000;
    uint32_t div = (khz + 99999) / 100000;

    if (div < 2)
        div = 2;
    cyw43_set_pio_clkdiv_int_frac8(div, 0);
}

/*
 *	from struct net_join, mapped here so that misc.c - which is on
 *	the kernel side of the header line - never has to see an SDK
 *	constant.
 *
 *	Returns 0, or one of the PC3_NET_E* codes.
 */
int pc3_net_up(const char *ssid, const char *key, unsigned auth)
{
    static const uint32_t authmap[4] = {
        CYW43_AUTH_OPEN,
        CYW43_AUTH_WPA_TKIP_PSK,
        CYW43_AUTH_WPA2_AES_PSK,
        CYW43_AUTH_WPA2_MIXED_PSK,
    };
    int err;

    if (auth > 3)
        return PC3_NET_EINVAL;
    if (board_is_pc2()) {
        /* Not "no radio fitted" pedantry: those pins are the SD card
           on that machine.  See the header comment. */
        return PC3_NET_ENODEV;
    }

    if (!net_ready) {
        net_set_pio_clkdiv();
        net_busy = 1;
        err = cyw43_arch_init();
        if (!err) {
            cyw43_arch_enable_sta_mode();
            /* NO powersave, as the WebMite sets it (WiFi.c:258): the
               default lets the radio doze between beacons, and the AP
               drops unicast sent to a dozing client - measured here as
               2 of 5 UDP datagrams lost board-to-board and 3-300 ms
               ping jitter, both gone with the doze off.  TCP had been
               hiding it behind retransmits. */
            cyw43_wifi_pm(&cyw43_state, CYW43_NO_POWERSAVE_MODE);
        }
        net_busy = 0;
        if (err) {
            kprintf("cyw43: init failed %d\n", err);
            return PC3_NET_EIO;
        }
        net_ready = 1;
    }

    net_busy = 1;
    err = cyw43_arch_wifi_connect_async(ssid, key, authmap[auth]);
    if (!err)
        cyw43_wifi_pm(&cyw43_state, CYW43_NO_POWERSAVE_MODE);
    net_busy = 0;
    if (err)
        return PC3_NET_EIO;
    return 0;
}

/*
 *	give back the PIO state machine and the DMA channels, and taking
 *	those twice in a session is a risk with nothing to gain while
 *	rejoining is the common case.
 */
void pc3_net_down(void)
{
    if (!net_ready)
        return;
    net_busy = 1;
    cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
    net_busy = 0;
}

int pc3_net_status(struct net_status *st)
{
    struct netif *nif;

    memset(st, 0, sizeof(*st));
    st->present = board_is_pc2() ? 0 : 1;
    st->ready = net_ready;
    if (!net_ready)
        return 0;

    net_busy = 1;
    st->wifi = cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA);
    st->link = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
    cyw43_wifi_get_mac(&cyw43_state, CYW43_ITF_STA, st->mac);
    if (cyw43_wifi_get_rssi(&cyw43_state, &st->rssi))
        st->rssi = 0;
    nif = netif_default;
    if (nif) {
        /* ip4_addr_get_u32 is the address as lwIP stores it, which is
           NETWORK order - first octet in the low byte on this machine.
           struct net_status says host order, so swap here rather than
           leave every reader to know.  The board printed a netmask of
           0.255.255.255 before this was here.
           Note that a mask and gateway appear before any association:
           cyw43_lwip.c always netif_adds with its CYW43_DEFAULT_IP_*
           placeholders and DHCP overwrites them on the lease. */
        st->ip = lwip_ntohl(ip4_addr_get_u32(netif_ip4_addr(nif)));
        st->mask = lwip_ntohl(ip4_addr_get_u32(netif_ip4_netmask(nif)));
        st->gw = lwip_ntohl(ip4_addr_get_u32(netif_ip4_gw(nif)));
        {
            /* Whatever the lease carried.  lwIP keeps these only
               because LWIP_DNS is compiled in - see lwipopts.h. */
            const ip_addr_t *d;
            int i;
            for (i = 0; i < 2; i++) {
                d = dns_getserver(i);
                if (d)
                    st->dns[i] =
                        lwip_ntohl(ip4_addr_get_u32(ip_2_ip4(d)));
            }
        }
    }
    net_busy = 0;
    return 0;
}


/*
 *	The lwIP side of Fuzix's sockets.  net_lwip.h has the contract;
 *	Kernel/dev/net/net_lwip.c is the other side of it and never sees
 *	a pbuf.  Datagrams only so far.
 *
 *	No locking anywhere below, and that is a property of the machine
 *	rather than an oversight: the receive callback runs in the pump,
 *	which is plt_idle and the PendSV trampoline, and neither can
 *	land inside a syscall.  So a queue manipulated by both cannot be
 *	manipulated by both at once.  If lwIP is ever driven from a real
 *	interrupt this stops being true.
 *	--------------------------------------------------------------
 */

/*
 *	the data stays in the pbufs, which are in PSRAM.  Four is what a
 *	resolver or an NTP client needs (one reply, plus room for a
 *	duplicate and a stray), and a fifth arriving before userland
 *	reads is dropped - which is what UDP is allowed to do and what
 *	an ethernet card would do to us anyway.
 */
#define UDPQ	4

struct udpq {
    struct pbuf *p;
    uint32_t src;               /* network order, as lwIP holds it */
    uint16_t port;              /* network order */
};

/*
 * and never both, so the per-protocol state shares a union: the
 * datagram queue above, or a chain of receive pbufs and how far into
 * the head of it userland has read.
 */
#define LW_FREE 0
#define LW_UDP  1
#define LW_TCP  2
#define LW_RAW  3

static struct lwsock {
    void *pcb;                  /* struct udp_pcb * or struct tcp_pcb * */
    uint8_t kind;
    uint8_t tls;                /* an altcp_tls connection */
    uint8_t listener;           /* altcp has no state field to ask */
    union {
        struct {
            struct udpq q[UDPQ];
            uint8_t head, tail;
        } u;
        struct {
            struct pbuf *rx;    /* received, not yet read */
            uint16_t off;       /* consumed from the head pbuf */
        } t;
    };
} lwsock[NETLW_NSOCKET];

/*
 * layer frees a socket without caring which it was.
 */
void netlw_tcp_close(uint8_t slot);

void netlw_free(uint8_t slot)
{
    struct lwsock *l = lwsock + slot;

    if (l->kind == LW_UDP || l->kind == LW_RAW) {
        /* Both queue whole datagrams the same way; only the pcb they
           came from differs. */
        while (l->u.tail != l->u.head) {
            pbuf_free(l->u.q[l->u.tail].p);
            l->u.tail = (l->u.tail + 1) % UDPQ;
        }
        if (l->pcb) {
            if (l->kind == LW_RAW)
                raw_remove(l->pcb);
            else
                udp_remove(l->pcb);
        }
    } else if (l->kind == LW_TCP) {
        if (l->t.rx) {
            pbuf_free(l->t.rx);
            l->t.rx = NULL;
        }
        /* netproto_close has already closed the pcb; if the socket is
           being torn down some other way, do not leak it. */
        if (l->pcb)
            netlw_tcp_close(slot);
    }
    l->pcb = NULL;
    l->kind = LW_FREE;
}

int netlw_isup(void)
{
    return net_ready &&
        cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) == CYW43_LINK_UP;
}

uint32_t netlw_myip(void)
{
    struct netif *nif = netif_default;

    if (!net_ready || nif == NULL)
        return 0;
    return ip4_addr_get_u32(netif_ip4_addr(nif));
}

static void udp_rx(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                   const ip_addr_t *addr, u16_t port)
{
    unsigned slot = (unsigned)(uintptr_t)arg;
    struct lwsock *l = lwsock + slot;
    uint8_t next = (l->u.head + 1) % UDPQ;

    (void)pcb;
    if (p == NULL)
        return;
    if (next == l->u.tail) {
        pbuf_free(p);           /* queue full: drop, as UDP may */
        return;
    }
    l->u.q[l->u.head].p = p;
    l->u.q[l->u.head].src = ip4_addr_get_u32(ip_2_ip4(addr));
    l->u.q[l->u.head].port = lwip_htons(port);
    l->u.head = next;
    netlw_wake((uint8_t)slot);
}

int netlw_udp_new(uint8_t slot)
{
    struct lwsock *l = lwsock + slot;

    if (l->pcb)
        return NETLW_INUSE;
    l->pcb = udp_new();
    if (l->pcb == NULL)
        return NETLW_NOMEM;
    l->kind = LW_UDP;
    l->u.head = l->u.tail = 0;
    udp_recv(l->pcb, udp_rx, (void *)(uintptr_t)slot);
    return NETLW_OK;
}

int netlw_udp_bind(uint8_t slot, uint32_t ip, uint16_t *port)
{
    struct lwsock *l = lwsock + slot;
    ip_addr_t a;

    if (l->pcb == NULL)
        return NETLW_NOMEM;
    ip_addr_set_ip4_u32(&a, ip);
    if (udp_bind(l->pcb, &a, lwip_ntohs(*port)) != ERR_OK)
        return NETLW_INUSE;
    *port = lwip_htons(((struct udp_pcb *)l->pcb)->local_port);
    return NETLW_OK;
}

int netlw_udp_send(uint8_t slot, const void *buf, uint16_t len,
                   uint32_t ip, uint16_t port)
{
    struct lwsock *l = lwsock + slot;
    struct pbuf *p;
    ip_addr_t a;
    err_t e;

    if (l->pcb == NULL)
        return NETLW_NOMEM;
    if (!netlw_isup())
        return NETLW_DOWN;
    p = pbuf_alloc(PBUF_TRANSPORT, len, PBUF_RAM);
    if (p == NULL)
        return NETLW_NOMEM;
    /* buf may be a user address: this platform is
       CONFIG_USERMEM_DIRECT and valaddr() has already checked it. */
    if (len)
        pbuf_take(p, buf, len);
    ip_addr_set_ip4_u32(&a, ip);
    e = udp_sendto(l->pcb, p, &a, lwip_ntohs(port));
    pbuf_free(p);
    if (e == ERR_MEM || e == ERR_BUF)
        return NETLW_NOMEM;
    if (e != ERR_OK)
        return NETLW_DOWN;
    return NETLW_OK;
}

int netlw_udp_recv(uint8_t slot, void *buf, uint16_t max,
                   uint32_t *ip, uint16_t *port)
{
    struct lwsock *l = lwsock + slot;
    struct udpq *q;
    uint16_t n;

    if (l->u.tail == l->u.head)
        return NETLW_EMPTY;
    q = l->u.q + l->u.tail;
    n = q->p->tot_len;
    if (n > max)
        n = max;                /* recvfrom truncates and drops the rest */
    if (n)
        pbuf_copy_partial(q->p, buf, n, 0);
    *ip = q->src;
    *port = q->port;
    pbuf_free(q->p);
    q->p = NULL;
    l->u.tail = (l->u.tail + 1) % UDPQ;
    return n;
}

/*
 *
 *	Receive data is NOT copied anywhere on arrival: the pbufs are
 *	chained onto the socket and stay there, in PSRAM, until userland
 *	reads them.  tcp_recved() is called as those bytes are consumed
 *	and not before, so lwIP's receive window closes when a program

/*
 *	Raw sockets.  The queue is the datagram one - a raw socket is a
 *	datagram socket with the protocol number in place of a port - so
 *	the only new code is the callback and the two ends of it.
 */
static u8_t raw_rx(void *arg, struct raw_pcb *pcb, struct pbuf *p,
                   const ip_addr_t *addr)
{
    unsigned slot = (unsigned)(uintptr_t)arg;
    struct lwsock *l = lwsock + slot;
    uint8_t next = (l->u.head + 1) % UDPQ;

    (void)pcb;
    if (next == l->u.tail)
        return 0;               /* queue full: leave it for lwIP to drop */
    l->u.q[l->u.head].p = p;    /* the whole IP packet, header and all */
    l->u.q[l->u.head].src = ip4_addr_get_u32(ip_2_ip4(addr));
    l->u.q[l->u.head].port = 0;
    l->u.head = next;
    netlw_wake((uint8_t)slot);
    return 1;                   /* consumed: we own the pbuf now */
}

int netlw_raw_new(uint8_t slot, uint8_t proto)
{
    struct lwsock *l = lwsock + slot;

    if (l->pcb)
        return NETLW_INUSE;
    l->pcb = raw_new(proto);
    if (l->pcb == NULL)
        return NETLW_NOMEM;
    l->kind = LW_RAW;
    l->u.head = l->u.tail = 0;
    raw_bind(l->pcb, IP_ANY_TYPE);
    raw_recv(l->pcb, raw_rx, (void *)(uintptr_t)slot);
    return NETLW_OK;
}

int netlw_raw_send(uint8_t slot, const void *buf, uint16_t len, uint32_t ip)
{
    struct lwsock *l = lwsock + slot;
    struct pbuf *p;
    ip_addr_t a;
    err_t e;

    if (l->pcb == NULL)
        return NETLW_NOMEM;
    if (!netlw_isup())
        return NETLW_DOWN;
    /* PBUF_IP, not PBUF_TRANSPORT: leave lwIP room to put the IP
       header in front of what the caller gave us. */
    p = pbuf_alloc(PBUF_IP, len, PBUF_RAM);
    if (p == NULL)
        return NETLW_NOMEM;
    if (len)
        pbuf_take(p, buf, len);
    ip_addr_set_ip4_u32(&a, ip);
    e = raw_sendto(l->pcb, p, &a);
    pbuf_free(p);
    if (e == ERR_MEM || e == ERR_BUF)
        return NETLW_NOMEM;
    if (e != ERR_OK)
        return NETLW_DOWN;
    return NETLW_OK;
}

int netlw_raw_recv(uint8_t slot, void *buf, uint16_t max, uint32_t *ip)
{
    struct lwsock *l = lwsock + slot;
    struct udpq *q;
    uint16_t n;

    if (l->u.tail == l->u.head)
        return NETLW_EMPTY;
    q = l->u.q + l->u.tail;
    n = q->p->tot_len;
    if (n > max)
        n = max;
    if (n)
        pbuf_copy_partial(q->p, buf, n, 0);
    *ip = q->src;
    pbuf_free(q->p);
    q->p = NULL;
    l->u.tail = (l->u.tail + 1) % UDPQ;
    return n;
}
/*
 *	TCP, over altcp.
 *
 *	altcp is lwIP's abstraction that lets one piece of code drive
 *	either a plain connection or a TLS one; the call shapes are
 *	identical and only the constructor differs.  MMBasic's WEB builds
 *	turn it on unconditionally for the same reason, and it is why
 *	adding TLS below costs almost nothing here: everything from
 *	connect() to close() is written once.
 *
 *	Receive data is NOT copied anywhere on arrival: the pbufs are
 *	chained onto the socket and stay there, in PSRAM, until userland
 *	reads them.  altcp_recved() is called as those bytes are consumed
 *	and not before, so lwIP's receive window closes when a program
 *	stops reading and opens when it starts again.  That is the whole
 *	of the flow control and it costs no buffer of our own.  Under TLS
 *	the pbufs are the DECRYPTED stream, which is the layer's whole
 *	point.
 */

static err_t tcp_rx(void *arg, struct altcp_pcb *pcb, struct pbuf *p, err_t err)
{
    unsigned slot = (unsigned)(uintptr_t)arg;
    struct lwsock *l = lwsock + slot;

    (void)pcb;
    if (p == NULL) {            /* the peer sent FIN */
        netlw_closed((uint8_t)slot);
        return ERR_OK;
    }
    if (err != ERR_OK) {
        pbuf_free(p);
        return err;
    }
    if (l->t.rx)
        pbuf_cat(l->t.rx, p);   /* takes the reference with it */
    else
        l->t.rx = p;
    netlw_wake((uint8_t)slot);
    return ERR_OK;
}

/* Room in the send window: somebody blocked in write() can go again. */
static err_t tcp_txroom(void *arg, struct altcp_pcb *pcb, u16_t len)
{
    (void)pcb; (void)len;
    netlw_wake((uint8_t)(uintptr_t)arg);
    return ERR_OK;
}

/*
 *	Connected.  For TLS this fires only when the HANDSHAKE has
 *	finished, not when the TCP connection is up, which is exactly the
 *	semantic a caller wants: connect() returns when the session is
 *	usable.
 */
static err_t tcp_done(void *arg, struct altcp_pcb *pcb, err_t err)
{
    (void)pcb; (void)err;
    netlw_connected((uint8_t)(uintptr_t)arg);
    return ERR_OK;
}

/*
 *	The connection died - refused, reset, timed out, or a TLS
 *	handshake that failed.  lwIP has ALREADY freed the pcb before
 *	calling this, so the pointer must be forgotten and never closed
 *	or aborted: doing either is a double free.
 */
static void tcp_died(void *arg, err_t err)
{
    unsigned slot = (unsigned)(uintptr_t)arg;

    lwsock[slot].pcb = NULL;
    netlw_reset((uint8_t)slot, err == ERR_RST ? NETLW_RESET : NETLW_DOWN);
}

static void tcp_arm(unsigned slot, struct altcp_pcb *pcb)
{
    altcp_arg(pcb, (void *)(uintptr_t)slot);
    altcp_recv(pcb, tcp_rx);
    altcp_sent(pcb, tcp_txroom);
    altcp_err(pcb, tcp_died);
}

static err_t tcp_acc(void *arg, struct altcp_pcb *newpcb, err_t err)
{
    unsigned listener = (unsigned)(uintptr_t)arg;
    int slot;

    if (err != ERR_OK || newpcb == NULL)
        return ERR_VAL;
    slot = netlw_accept_slot((uint8_t)listener);
    if (slot < 0)
        return ERR_MEM;         /* no socket free: lwIP refuses it */
    lwsock[slot].kind = LW_TCP;
    lwsock[slot].pcb = newpcb;
    lwsock[slot].t.rx = NULL;
    lwsock[slot].t.off = 0;
    lwsock[slot].listener = 0;
    tcp_arm((unsigned)slot, newpcb);
    netlw_wake((uint8_t)listener);
    return ERR_OK;
}

/*
 *	SO_REUSEADDR has to be set on the tcp pcb itself, and altcp has no
 *	accessor for the one underneath it.  struct altcp_pcb is public
 *	and its `state' is the inner tcp_pcb for an altcp_tcp connection -
 *	lwIP's own code does exactly this - so reaching in is safe as long
 *	as we only do it for a connection we know we built with
 *	altcp_tcp_new.  It is never done for a TLS one, where `state' is
 *	the mbedtls session instead.
 */
static void tcp_set_reuse(struct altcp_pcb *conn)
{
    struct tcp_pcb *inner = (struct tcp_pcb *)conn->state;

    if (inner)
        ip_set_option(inner, SOF_REUSEADDR);
}

int netlw_tcp_new(uint8_t slot)
{
    struct lwsock *l = lwsock + slot;
    struct altcp_pcb *pcb;

    if (l->pcb)
        return NETLW_INUSE;
    pcb = altcp_tcp_new_ip_type(IPADDR_TYPE_V4);
    if (pcb == NULL)
        return NETLW_NOMEM;
    l->kind = LW_TCP;
    l->pcb = pcb;
    l->t.rx = NULL;
    l->t.off = 0;
    l->tls = 0;
    l->listener = 0;
    /* Every TCP socket, because no program can ask for it: Fuzix has
       no setsockopt.  Without it a server cannot rebind its port until
       the last connection leaves TIME_WAIT, two minutes later.  See
       lwipopts.h. */
    tcp_set_reuse(pcb);
    tcp_arm(slot, pcb);
    return NETLW_OK;
}

/*
 *	A TLS client socket.
 *
 *	The configuration is shared and made once: it holds the CA store
 *	and the entropy/DRBG context, and building one per connection
 *	would re-seed the DRBG and re-parse the certificates every time.
 *	With no CA loaded the session is ENCRYPTED BUT NOT
 *	AUTHENTICATED - anyone in the path can present their own
 *	certificate - which is the same state MMBasic starts in before
 *	WEB TLS CA runs, and netlw_tls_ca() below is how it stops being
 *	true.
 */
static struct altcp_tls_config *tls_config;
static uint8_t tls_verify;

int netlw_tls_new(uint8_t slot)
{
    struct lwsock *l = lwsock + slot;
    struct altcp_pcb *pcb;

    if (l->pcb)
        return NETLW_INUSE;
    if (tls_config == NULL) {
        tls_config = altcp_tls_create_config_client(NULL, 0);
        if (tls_config == NULL)
            return NETLW_NOMEM;
    }
    pcb = altcp_tls_new(tls_config, IPADDR_TYPE_V4);
    if (pcb == NULL)
        return NETLW_NOMEM;
    l->kind = LW_TCP;
    l->pcb = pcb;
    l->t.rx = NULL;
    l->t.off = 0;
    l->tls = 1;
    l->listener = 0;
    tcp_arm(slot, pcb);
    return NETLW_OK;
}

/*
 *	The name we are connecting to, for SNI and for checking the
 *	certificate against.  connect() carries an address and nothing
 *	else, so this is the one thing TLS needs that BSD sockets have
 *	nowhere to put - hence an ioctl, before the connect.
 */
int netlw_tls_host(uint8_t slot, const char *name)
{
    struct lwsock *l = lwsock + slot;
    mbedtls_ssl_context *ssl;

    if (l->pcb == NULL || !l->tls)
        return NETLW_INUSE;
    ssl = altcp_tls_context(l->pcb);
    if (ssl == NULL)
        return NETLW_NOMEM;
    if (mbedtls_ssl_set_hostname(ssl, name))
        return NETLW_NOMEM;
    return NETLW_OK;
}

/*
 *	Load a CA bundle, in PEM (with a NUL on the end) or DER.  The
 *	kernel cannot read files, so userland hands over the bytes.
 *	Replaces the shared config, so it takes effect on the next
 *	connection rather than on any that is already open.
 */
int netlw_tls_ca(const void *ca, unsigned len)
{
    struct altcp_tls_config *cfg;

    /* len 0: forget the bundle and go back to encrypted-but-not
       authenticated, which is MMBasic's WEB TLS NOVERIFY. */
    if (ca == NULL || len == 0) {
        if (tls_config)
            altcp_tls_free_config(tls_config);
        tls_config = NULL;
        tls_verify = 0;
        return NETLW_OK;
    }

    cfg = altcp_tls_create_config_client(ca, len);
    if (cfg == NULL)
        return NETLW_NOMEM;

    /*
     *	FORCE VERIFICATION, and this line is the whole point.
     *
     *	lwIP's ALTCP_MBEDTLS_AUTHMODE defaults to
     *	MBEDTLS_SSL_VERIFY_OPTIONAL (altcp_tls_mbedtls_opts.h), and
     *	OPTIONAL means mbedtls runs the check, records the answer in
     *	mbedtls_ssl_get_verify_result(), and then COMPLETES THE
     *	HANDSHAKE ANYWAY.  Nothing here reads that result, so without
     *	this call loading a CA bundle would parse every certificate in
     *	it and change nothing: the failure mode is a machine that looks
     *	like it authenticates and does not.
     *
     *	MMBasic carries the same line, and its comment records the same
     *	reason - see picomite_tls_set_ca() in its WiFi.c.
     *
     *	The cast is the only door.  struct altcp_tls_config is private
     *	to altcp_tls_mbedtls.c and there is no accessor, but its first
     *	member is the mbedtls_ssl_config and lwIP's own code relies on
     *	that.  If the SDK ever reorders it this goes quietly wrong,
     *	which is why tlsca reports what it did and why the CA test
     *	below is worth running after an SDK bump.
     */
    mbedtls_ssl_conf_authmode((mbedtls_ssl_config *)cfg,
                              MBEDTLS_SSL_VERIFY_REQUIRED);

    if (tls_config)
        altcp_tls_free_config(tls_config);
    tls_config = cfg;
    tls_verify = 1;
    return NETLW_OK;
}

/*	Does this machine check certificates?  For tlsca to report, so a
 *	user is never left guessing whether a session is authenticated. */
int netlw_tls_verifying(void)
{
    return tls_verify;
}

int netlw_tcp_bind(uint8_t slot, uint32_t ip, uint16_t *port)
{
    struct lwsock *l = lwsock + slot;
    ip_addr_t a;

    if (l->pcb == NULL)
        return NETLW_NOMEM;
    ip_addr_set_ip4_u32(&a, ip);
    if (altcp_bind(l->pcb, &a, lwip_ntohs(*port)) != ERR_OK)
        return NETLW_INUSE;
    *port = lwip_htons(altcp_get_port(l->pcb, 1));
    return NETLW_OK;
}

int netlw_tcp_connect(uint8_t slot, uint32_t ip, uint16_t port)
{
    struct lwsock *l = lwsock + slot;
    ip_addr_t a;

    if (l->pcb == NULL)
        return NETLW_NOMEM;
    if (!netlw_isup())
        return NETLW_DOWN;
    ip_addr_set_ip4_u32(&a, ip);
    if (altcp_connect(l->pcb, &a, lwip_ntohs(port), tcp_done) != ERR_OK)
        return NETLW_NOMEM;
    return NETLW_OK;
}

/*
 *	altcp_listen_with_backlog REPLACES the pcb with a listening one
 *	and frees the original, so the returned pointer has to be stored -
 *	keeping the old one is a use after free.
 *
 *	Listeners are plain TCP only.  A TLS server needs a certificate
 *	and a private key of its own, which is a different piece of work
 *	from being a client, so netproto_listen refuses a TLS socket
 *	rather than half-supporting one.
 */
int netlw_tcp_listen(uint8_t slot)
{
    struct lwsock *l = lwsock + slot;
    struct altcp_pcb *lpcb;

    if (l->pcb == NULL || l->tls)
        return NETLW_NOMEM;
    lpcb = altcp_listen_with_backlog(l->pcb, 2);
    if (lpcb == NULL)
        return NETLW_NOMEM;
    l->pcb = lpcb;
    l->listener = 1;
    altcp_arg(lpcb, (void *)(uintptr_t)slot);
    altcp_accept(lpcb, tcp_acc);
    return NETLW_OK;
}

int netlw_tcp_send(uint8_t slot, const void *buf, uint16_t len)
{
    struct lwsock *l = lwsock + slot;
    struct altcp_pcb *pcb = l->pcb;
    uint16_t room;
    err_t e;

    if (pcb == NULL)
        return NETLW_RESET;
    room = altcp_sndbuf(pcb);
    if (room == 0)
        return 0;               /* window full: the caller sleeps */
    if (len > room)
        len = room;
    /* COPY: buf is the caller's, and on this platform that means a
       user address which may be swapped out before the segment is
       acknowledged.  Under TLS it must be copied anyway - the bytes
       on the wire are not these bytes. */
    e = altcp_write(pcb, buf, len, TCP_WRITE_FLAG_COPY);
    if (e == ERR_MEM)
        return 0;
    if (e != ERR_OK)
        return NETLW_RESET;
    /* Push it now rather than waiting for the next segment to fill:
       an interactive session is mostly single keystrokes. */
    altcp_output(pcb);
    return len;
}

int netlw_tcp_recv(uint8_t slot, void *buf, uint16_t max)
{
    struct lwsock *l = lwsock + slot;
    uint8_t *out = buf;
    uint16_t done = 0;

    if (l->t.rx == NULL)
        return l->pcb ? NETLW_EMPTY : NETLW_EOF;

    while (done < max && l->t.rx) {
        struct pbuf *p = l->t.rx;
        uint16_t n = p->len - l->t.off;

        if (n > (uint16_t)(max - done))
            n = max - done;
        memcpy(out + done, (uint8_t *)p->payload + l->t.off, n);
        done += n;
        l->t.off += n;
        if (l->t.off == p->len) {
            /* Take the head off the chain and drop our reference to
               it, keeping the rest. */
            l->t.rx = p->next;
            if (l->t.rx)
                pbuf_ref(l->t.rx);
            pbuf_free(p);
            l->t.off = 0;
        }
    }
    /* Only now does the window open again. */
    if (done && l->pcb)
        altcp_recved(l->pcb, done);
    return done;
}

/*
 *	Closing.
 *
 *	A LISTENER IS NOT A CONNECTION, and lwIP will not let you pretend
 *	otherwise: tcp_recv(), tcp_sent() and tcp_err() each assert that
 *	the pcb is not in LISTEN, and an assert here is panic() - a BKPT,
 *	which on a board with no debugger is a HardFault and a dead
 *	machine.  Clearing all five callbacks unconditionally killed the
 *	kernel the first time a server exited, with "invalid socket state
 *	for recv callback" as the only clue.  altcp passes those calls
 *	straight through, so the rule survives the move to it - only now
 *	the flag says which kind we have, because an altcp_pcb has no
 *	state field to ask.
 */
void netlw_tcp_close(uint8_t slot)
{
    struct lwsock *l = lwsock + slot;
    struct altcp_pcb *pcb = l->pcb;

    if (pcb == NULL)
        return;
    l->pcb = NULL;              /* before anything can call back into us */
    altcp_arg(pcb, NULL);       /* the one setter with no state assert */
    if (l->listener) {
        altcp_accept(pcb, NULL);
        altcp_close(pcb);
        return;
    }
    altcp_recv(pcb, NULL);
    altcp_sent(pcb, NULL);
    altcp_err(pcb, NULL);
    if (altcp_close(pcb) != ERR_OK)
        altcp_abort(pcb);       /* no memory to send FIN: hang up */
}

void netlw_peer(uint8_t slot, uint32_t *ip, uint16_t *port)
{
    struct lwsock *l = lwsock + slot;

    *ip = 0;
    *port = 0;
    if (l->kind == LW_TCP && l->pcb) {
        ip_addr_t *a = altcp_get_ip(l->pcb, 0);

        if (a)
            *ip = ip4_addr_get_u32(ip_2_ip4(a));
        *port = lwip_htons(altcp_get_port(l->pcb, 0));
    }
}

/*
 *	The two clocks mbedtls asks for.
 *
 *	pc3_mbedtls_time is WALL CLOCK, and it is what decides whether a
 *	certificate has expired.  It comes from the kernel's own clock,
 *	which this machine loads from the DS3231 at boot and can correct
 *	with ntpdate.  A board with a flat backup battery and no NTP will
 *	reject every certificate as not yet valid, and that is the right
 *	answer rather than a bug.
 *
 *	mbedtls_ms_time is MONOTONIC and is used for timeouts and session
 *	freshness, so it is uptime and deliberately not the wall clock -
 *	setting the date backwards mid-session should not confuse it.
 */
long long pc3_mbedtls_time(long long *tp)
{
    long long t = (long long)netlw_now();

    if (tp)
        *tp = t;
    return t;
}

mbedtls_ms_time_t mbedtls_ms_time(void)
{
    return (mbedtls_ms_time_t)(time_us_64() / 1000);
}
#endif /* CONFIG_PC3_NET */
