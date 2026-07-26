#include <kernel.h>
#include <kdata.h>
#include <printf.h>
#include <exec.h>
#include "picosdk.h"
#include "pico_ioctl.h"
#include <pico/multicore.h>
#include <pico/bootrom.h>
#include <hardware/watchdog.h>
#include <hardware/exception.h>

uint8_t sys_cpu = A_ARM;
uint8_t sys_cpu_feat = AF_CORTEX_M0;
uint8_t need_resched;
uaddr_t ramtop = (uaddr_t) PROGTOP;
uint8_t sys_stubs[sizeof(struct exec)];
uint16_t swap_dev = 0xffff;

/* Unused on this port */

void set_cpu_type(void) {}
void map_init(void) {}
void plt_discard(void) {}
void program_vectors(uint16_t* pageptr) {}

void plt_reboot(void)
{
    multicore_reset_core1();
    watchdog_reboot(0, 0, 0);
}

void plt_monitor(void)
{
    sleep_ms(1); // wait to print any remaining messages
    multicore_reset_core1();
    for(;;) { sleep_until(at_the_end_of_time); }
}

/* Pre-emption support (see tricks.S): user-space PC bounds for the
 * PendSV redirect check. */
uint32_t preempt_lo, preempt_hi;

void preempt_init(void)
{
    preempt_lo = (uint32_t)PROGBASE;
    preempt_hi = (uint32_t)PROGBASE + USERMEM;
    /* PendSV must be the lowest priority exception so it runs only when
     * all other interrupt work is done */
    exception_set_priority(PENDSV_EXCEPTION, PICO_LOWEST_IRQ_PRIORITY);
}

/* Called from the PendSV trampoline (tricks.S) in thread mode on the
 * kernel stack: the pre-emption twin of unix_syscall's tail. The
 * trampoline only fires for user-mode PCs, so we cannot be here inside
 * a syscall or kernel code. */
void preempt_handler(void)
{
    udata.u_insys = 1;
    di();
    need_resched = 0;
    if (nready > 1 && runticks >= udata.u_ptab->p_priority) {
        udata.u_ptab->p_status = P_READY;
        plt_switchout();
    }
    ei();
    chksigs();
    udata.u_insys = 0;
}

int plt_dev_ioctl(uarg_t request, char *data)
{
    used(data);
    if (request == PICOIOC_FLASH)
    {
        reset_usb_boot(0, 0);
        return 0;
    }
#ifdef CONFIG_PC3_DISPLAY
    if (request == PICOIOC_KBDMAP)
    {
        extern int kbd_set_layout(const char *name);
        char name[3];
        if (uget(data, name, 2))
            return -1;
        name[2] = 0;
        if (kbd_set_layout(name)) {
            udata.u_error = EINVAL;
            return -1;
        }
        return 0;
    }
#endif
    return -1;
}

uaddr_t pagemap_base(void)
{
    return PROGBASE;
}

usize_t valaddr(const uint8_t *base, usize_t size, uint_fast8_t is_write)
{
        if (base + size < base)
                size = MAXUSIZE - (usize_t)base + 1;
        if (!base || base < (const uint8_t *)PROGBASE)
                size = 0;
        else if (base + size > (const uint8_t *)(size_t)udata.u_ptab->p_top)
                size = (uint8_t *)(size_t)udata.u_ptab->p_top - base;
        if (size == 0)
                udata.u_error = EFAULT;
        return size;
}

usize_t valaddr_r(const uint8_t *pp, usize_t l)
{
	return valaddr(pp, l, 0);
}

usize_t valaddr_w(const uint8_t *pp, usize_t l)
{
	return valaddr(pp, l, 1);
}

/* vim: sw=4 ts=4 et: */


