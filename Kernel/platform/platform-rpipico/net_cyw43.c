/*
 * CYW43 Wi-Fi for the Pico Computer 3: the radio and lwIP, with no
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
#include "lwip/tcp.h"
#include "lwip/raw.h"
#include "lwip/dns.h"

/* The socket seam.  Only stdint.h behind it, so it is safe to include
   on this side of the header line - see net_lwip.h. */
#include <net_lwip.h>

#include "pico_ioctl.h"

/* The two things this file needs from the kernel side of the line. */
extern int board_is_pc2(void);
extern void kprintf(const char *fmt, ...);

/*
 * lwIP's heap, and with MEMP_MEM_MALLOC set it is lwIP's ONLY pool:
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
 *	The pump.  Called from plt_idle and from preempt_handler - the
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
void pc3_net_poll(void)
{
    if (!net_ready || net_busy)
        return;
    net_busy = 1;
    cyw43_arch_poll();
    net_busy = 0;
}

/*
 *	The gSPI link is clocked from clk_sys and the SDK's fixed divider
 *	of 2 is tuned for ~125MHz; at 375 it runs the CYW43439 well out
 *	of spec and the symptom is "hdr mismatch" or an ioctl timeout
 *	rather than anything that looks like a clock problem.  This is
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
 *	Bring the radio up and start joining.  auth is the small index
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
        if (!err)
            cyw43_arch_enable_sta_mode();
        net_busy = 0;
        if (err) {
            kprintf("cyw43: init failed %d\n", err);
            return PC3_NET_EIO;
        }
        net_ready = 1;
    }

    net_busy = 1;
    err = cyw43_arch_wifi_connect_async(ssid, key, authmap[auth]);
    net_busy = 0;
    if (err)
        return PC3_NET_EIO;
    return 0;
}

/*
 *	Leave the network.  The driver stays initialised: deinit would
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
 *	--------------------------------------------------------------
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
 *	Four datagrams per socket.  It is a queue depth, not a buffer:
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
 * One table for both protocols.  A socket is a datagram or a stream
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
 * Give a slot back.  Both protocols land here, because the socket
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
 *	TCP.
 *
 *	Receive data is NOT copied anywhere on arrival: the pbufs are
 *	chained onto the socket and stay there, in PSRAM, until userland
 *	reads them.  tcp_recved() is called as those bytes are consumed
 *	and not before, so lwIP's receive window closes when a program
 *	stops reading and opens when it starts again.  That is the whole
 *	of the flow control and it costs no buffer of our own.
 */

static err_t tcp_rx(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
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
static err_t tcp_txroom(void *arg, struct tcp_pcb *pcb, u16_t len)
{
    (void)pcb; (void)len;
    netlw_wake((uint8_t)(uintptr_t)arg);
    return ERR_OK;
}

static err_t tcp_done(void *arg, struct tcp_pcb *pcb, err_t err)
{
    (void)pcb;
    netlw_connected((uint8_t)(uintptr_t)arg);
    (void)err;
    return ERR_OK;
}

/*
 *	The connection died.  lwIP has ALREADY freed the pcb before
 *	calling this, so the pointer must be forgotten and never closed
 *	or aborted - doing either is a double free.
 */
static void tcp_died(void *arg, err_t err)
{
    unsigned slot = (unsigned)(uintptr_t)arg;

    lwsock[slot].pcb = NULL;
    netlw_reset((uint8_t)slot, err == ERR_RST ? NETLW_RESET : NETLW_DOWN);
}

static void tcp_arm(unsigned slot, struct tcp_pcb *pcb)
{
    tcp_arg(pcb, (void *)(uintptr_t)slot);
    tcp_recv(pcb, tcp_rx);
    tcp_sent(pcb, tcp_txroom);
    tcp_err(pcb, tcp_died);
}

static err_t tcp_acc(void *arg, struct tcp_pcb *newpcb, err_t err)
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
    tcp_arm((unsigned)slot, newpcb);
    netlw_wake((uint8_t)listener);
    return ERR_OK;
}


/*
 *	Raw sockets.  The queue is the datagram one - a raw socket is a
 *	datagram socket with the protocol number in place of a port - so
 *	the only new code is the callback and the two ends of it.
 */
/*
 *	A RAW SOCKET TAKES A COPY AND RETURNS 0, ALWAYS.
 *
 *	Returning 1 tells lwIP the packet is consumed, and then it goes
 *	no further: not to any other raw pcb, and not to the ICMP layer.
 *	That was the first version, and the board showed both halves of
 *	what it costs.  Two ping processes, and only the first ever saw a
 *	reply - it was eating the second's.  And while either was
 *	running the machine stopped answering pings from anywhere else,
 *	because the echo REQUESTS were being swallowed before lwIP could
 *	reply to them; ping printed them as "Bad id 1", which is the
 *	clue that took a while to read the right way round.
 *
 *	BSD semantics are that a raw socket gets a copy and the stack
 *	carries on, so that is what this does.  The copy costs a pbuf per
 *	ICMP packet, which is what a correct raw socket costs.
 */
static u8_t raw_rx(void *arg, struct raw_pcb *pcb, struct pbuf *p,
                   const ip_addr_t *addr)
{
    unsigned slot = (unsigned)(uintptr_t)arg;
    struct lwsock *l = lwsock + slot;
    uint8_t next = (l->u.head + 1) % UDPQ;
    struct pbuf *c;

    (void)pcb;
    if (next == l->u.tail)
        return 0;               /* our queue is full; not lwIP's problem */
    c = pbuf_clone(PBUF_RAW, PBUF_RAM, p);
    if (c == NULL)
        return 0;
    l->u.q[l->u.head].p = c;    /* the whole IP packet, header and all */
    l->u.q[l->u.head].src = ip4_addr_get_u32(ip_2_ip4(addr));
    l->u.q[l->u.head].port = 0;
    l->u.head = next;
    netlw_wake((uint8_t)slot);
    return 0;                   /* lwIP still owns p and still handles it */
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

int netlw_tcp_new(uint8_t slot)
{
    struct lwsock *l = lwsock + slot;
    struct tcp_pcb *pcb;

    if (l->pcb)
        return NETLW_INUSE;
    pcb = tcp_new();
    if (pcb == NULL)
        return NETLW_NOMEM;
    l->kind = LW_TCP;
    l->pcb = pcb;
    l->t.rx = NULL;
    l->t.off = 0;
    /* Every TCP socket, because no program can ask for it: Fuzix has
       no setsockopt.  Without it a server cannot rebind its port until
       the last connection leaves TIME_WAIT, two minutes later.  See
       lwipopts.h. */
    ip_set_option(pcb, SOF_REUSEADDR);
    tcp_arm(slot, pcb);
    return NETLW_OK;
}

int netlw_tcp_bind(uint8_t slot, uint32_t ip, uint16_t *port)
{
    struct lwsock *l = lwsock + slot;
    ip_addr_t a;

    if (l->pcb == NULL)
        return NETLW_NOMEM;
    ip_addr_set_ip4_u32(&a, ip);
    if (tcp_bind(l->pcb, &a, lwip_ntohs(*port)) != ERR_OK)
        return NETLW_INUSE;
    *port = lwip_htons(((struct tcp_pcb *)l->pcb)->local_port);
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
    if (tcp_connect(l->pcb, &a, lwip_ntohs(port), tcp_done) != ERR_OK)
        return NETLW_NOMEM;
    return NETLW_OK;
}

/*
 *	tcp_listen_with_backlog REPLACES the pcb with a smaller one and
 *	frees the original, so the returned pointer has to be stored -
 *	keeping the old one is a use after free.
 */
int netlw_tcp_listen(uint8_t slot)
{
    struct lwsock *l = lwsock + slot;
    struct tcp_pcb *lpcb;

    if (l->pcb == NULL)
        return NETLW_NOMEM;
    lpcb = tcp_listen_with_backlog((struct tcp_pcb *)l->pcb, 2);
    if (lpcb == NULL)
        return NETLW_NOMEM;
    l->pcb = lpcb;
    tcp_arg(lpcb, (void *)(uintptr_t)slot);
    tcp_accept(lpcb, tcp_acc);
    return NETLW_OK;
}

int netlw_tcp_send(uint8_t slot, const void *buf, uint16_t len)
{
    struct lwsock *l = lwsock + slot;
    struct tcp_pcb *pcb = l->pcb;
    uint16_t room;
    err_t e;

    if (pcb == NULL)
        return NETLW_RESET;
    room = tcp_sndbuf(pcb);
    if (room == 0)
        return 0;               /* window full: the caller sleeps */
    if (len > room)
        len = room;
    /* COPY: buf is the caller's, and on this platform that means a
       user address which may be swapped out before the segment is
       acknowledged. */
    e = tcp_write(pcb, buf, len, TCP_WRITE_FLAG_COPY);
    if (e == ERR_MEM)
        return 0;
    if (e != ERR_OK)
        return NETLW_RESET;
    /* Push it now rather than waiting for the next segment to fill:
       an interactive session is mostly single keystrokes. */
    tcp_output(pcb);
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
            /* pbuf_dechain would leave the rest unreferenced; take the
               head off the chain and drop our reference to it. */
            l->t.rx = p->next;
            if (l->t.rx)
                pbuf_ref(l->t.rx);
            pbuf_free(p);
            l->t.off = 0;
        }
    }
    /* Only now does the window open again. */
    if (done && l->pcb)
        tcp_recved(l->pcb, done);
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
 *	for recv callback" as the only clue.  A listening pcb has only
 *	the accept callback to clear, and tcp_close is what frees it -
 *	tcp_abort would be wrong, there is no connection to reset.
 */
void netlw_tcp_close(uint8_t slot)
{
    struct lwsock *l = lwsock + slot;
    struct tcp_pcb *pcb = l->pcb;

    if (pcb == NULL)
        return;
    l->pcb = NULL;              /* before anything can call back into us */
    tcp_arg(pcb, NULL);         /* the one setter with no state assert */
    if (pcb->state == LISTEN) {
        tcp_accept(pcb, NULL);
        tcp_close(pcb);
        return;
    }
    tcp_recv(pcb, NULL);
    tcp_sent(pcb, NULL);
    tcp_err(pcb, NULL);
    if (tcp_close(pcb) != ERR_OK)
        tcp_abort(pcb);         /* no memory to send FIN: hang up */
}

void netlw_peer(uint8_t slot, uint32_t *ip, uint16_t *port)
{
    struct lwsock *l = lwsock + slot;
    struct tcp_pcb *pcb = l->pcb;

    *ip = 0;
    *port = 0;
    if (l->kind == LW_TCP && pcb) {
        *ip = ip4_addr_get_u32(ip_2_ip4(&pcb->remote_ip));
        *port = lwip_htons(pcb->remote_port);
    }
}
#endif /* CONFIG_PC3_NET */
