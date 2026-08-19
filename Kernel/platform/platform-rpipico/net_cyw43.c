/*
 * CYW43 / lwIP glue for the Pico Computer 3.
 *
 * STEP 0 - THIS FILE DOES NOTHING AT RUNTIME, ON PURPOSE.
 *
 * The first question about networking on this machine is not "does it
 * work" but "what does it cost", because the kernel's RAM region has
 * under 2K of headroom and everything else has to come out of the 84
 * blocks of process pool.  So this file exists to make the linker pull
 * in the code and the static buffers that a real implementation would
 * use, and nothing else: pc3_net_init() is called from main.c but
 * returns immediately, because pc3_net_enable is never set.
 *
 * The guard is volatile so the compiler cannot prove the body is dead
 * and delete it - which would measure zero and prove nothing.  The
 * calls below are unreachable but not removable, so the map reports
 * the true size of the stack we would be committing to.
 *
 * Bringing the radio up is step 1 and it is a separate change.  Note
 * before that happens: the CYW43 lives on GP23/24/25/29, and on a Pico
 * Computer 2 (same kernel image, see board.c) GP29 is the SD card's
 * chip select and GP25 is the LED.  Nothing here may touch those pins
 * without board_is_pc2() saying no first.
 */

#ifdef CONFIG_PC3_NET

#include "pico/cyw43_arch.h"
#include "lwip/netif.h"
#include "lwip/dhcp.h"
#include "lwip/udp.h"
#include "lwip/tcp.h"
#include "lwip/raw.h"
#include "lwip/timeouts.h"

/* Never set.  See the header comment. */
volatile uint8_t pc3_net_enable;

/* The pump this would join is plt_idle/PendSV, next to tuh_task.  Not
   wired up yet - referenced here only so the poll path is linked and
   measured. */
void pc3_net_poll(void)
{
    if (!pc3_net_enable)
        return;
    cyw43_arch_poll();
    sys_check_timeouts();
}

void pc3_net_init(void)
{
    struct netif *nif;

    if (!pc3_net_enable)
        return;

    /* Association and the link. */
    if (cyw43_arch_init())
        return;
    cyw43_arch_enable_sta_mode();
    cyw43_arch_wifi_connect_timeout_ms("", "", CYW43_AUTH_WPA2_AES_PSK, 30000);

    /* The address: DHCP, as every other firmware on this board does. */
    nif = netif_default;
    if (nif)
        dhcp_start(nif);

    /* The three protocol paths Fuzix's socket layer will need: UDP for
       recvfrom/sendto, TCP for the stream sockets, RAW for ping(1). */
    udp_new();
    tcp_new();
    raw_new(IP_PROTO_ICMP);

    pc3_net_poll();
}

#endif /* CONFIG_PC3_NET */
