#include <kernel.h>
#include <kdata.h>
#include <printf.h>
#include <exec.h>
#include "picosdk.h"
#include "pico_ioctl.h"
#include "config.h"
#include "psram.h"
#include <hardware/adc.h>
#ifdef CONFIG_PC3_DISPLAY
#include "display.h"
#endif
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
#ifdef CONFIG_PC3_USB_KBD
    {
        /* Thread context on a fresh kernel stack: the safe place to pump
         * the USB host stack when a spinning process has starved it */
        extern void usbkbd_task(void);
        usbkbd_task();
    }
#endif
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
#ifdef CONFIG_PC3_USB_KBD
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
#if defined(CONFIG_PC3_USB_KBD) && !defined(PC3_NO_USB_BUS_RESET)
    if (request == PICOIOC_USBRESET)
    {
        extern void usb_bus_reset(void);
        usb_bus_reset();
        return 0;
    }
#endif
#ifdef CONFIG_PC3_DISPLAY
    if (request == GFXIOC_MODE)
    {
        int m;
        if (uget(data, &m, sizeof(m)))
            return -1;
        if (display_gfx_mode(m) < 0) {
            udata.u_error = EINVAL;
            return -1;
        }
        return 0;
    }
    if (request == GFXIOC_PAL)
    {
        int v;
        if (uget(data, &v, sizeof(v)))
            return -1;
        display_gfx_pal((v >> 8) & 15, v & 15);
        return 0;
    }
    if (request == GFXIOC_PIXEL)
    {
        /* The hot path.  The coordinates and colour are packed into the
         * data argument ITSELF, so there is no uget and nothing to
         * validate - the whole call is a switch and a store.  MMBasic's
         * PIXEL statement costs 5us; this has to be well under it. */
        uint32_t v = (uint32_t)data;
        return display_gfx_pixel(v & 0x3FF, (v >> 10) & 0x1FF,
                                 (v >> 19) & 0xFF);
    }
    if (request == GFXIOC_INFO)
    {
        struct gfx_info gi;
        display_gfx_geom(&gi.width, &gi.height, &gi.stride, &gi.bpp,
                         &gi.mode);
        if (uput(&gi, data, sizeof(gi)))
            return -1;
        return 0;
    }
    if (request == GFXIOC_BLIT)
    {
        struct gfx_blit gb;
        int size = display_gfx_size();
        if (uget(data, &gb, sizeof(gb)))
            return -1;
        if (size == 0 || gb.offset >= size || gb.len > size - gb.offset) {
            udata.u_error = EINVAL;
            return -1;
        }
        if (uget(gb.buf, disp_fb + gb.offset, gb.len))
            return -1;
        return 0;
    }
#endif
    if (request == PSRAMIOC_ALLOC)
    {
        struct psram_req rq;
        uint32_t b;
        if (uget(data, &rq, sizeof(rq)))
            return -1;
        b = arena_alloc(udata.u_ptab, rq.len);
        if (!b) {
            udata.u_error = ENOMEM;
            return -1;
        }
        rq.base = b;
        if (uput(&rq, data, sizeof(rq)))
            return -1;
        return 0;
    }
    if (request == PSRAMIOC_FREE)
    {
        uint32_t b;
        if (uget(data, &b, sizeof(b)))
            return -1;
        if (arena_free(udata.u_ptab, b)) {
            udata.u_error = EINVAL;
            return -1;
        }
        return 0;
    }
    if (request == PSRAMIOC_STAT)
    {
        struct psram_stat st;
        arena_stat(&st.total, &st.free, &st.largest);
        if (uput(&st, data, sizeof(st)))
            return -1;
        return 0;
    }
    if (request == PICOIOC_ADVAL)
    {
        static uint8_t adv_ready;
        int n;

        if (uget(data, &n, sizeof(n)))
            return -1;

        if (n >= 0 && n <= 4) {
            int i;
            if (!adv_ready) {
                /* joystick switches: GP34-37, pulled up, active low */
                for (i = 34; i <= 37; i++) {
                    gpio_init(i);
                    gpio_set_dir(i, false);
                    gpio_set_input_enabled(i, true);
                    gpio_pull_up(i);
                    gpio_set_input_hysteresis_enabled(i, true);
                }
                adc_init();
                for (i = 1; i <= 4; i++)
                    adc_gpio_init(40 + i);
                adv_ready = 1;
            }
            if (n == 0) {
                /* pressed = 1: bit0 GP34, bit1 GP35, bit2 GP36, bit3 GP37 */
                uint64_t all = gpio_get_all64();
                return (int)((~(all >> 34)) & 15);
            }
            adc_select_input(n);        /* GP40+n = ADC input n */
            adc_read();                 /* discard: mux settle */
            return adc_read() << 4;     /* BBC 16-bit convention */
        }
#ifdef CONFIG_PC3_SOUND
        if (n <= -5 && n >= -8) {
            extern int sound_qfree(int);
            return sound_qfree(-5 - n);
        }
#endif
        if (n == -9) {
            /* microsecond counter, 31 bits in the return value: the
             * safe contract for callers that passed a 4-byte buffer */
            return (int)(time_us_64() & 0x7FFFFFFF);
        }
        if (n == -10) {
            /* microsecond counter, full 64 bits written back through
             * the caller's 8-byte buffer (low word held the selector).
             * A separate selector from -9: never write 8 bytes into a
             * caller that only promised 4. */
            uint64_t us = time_us_64();
            if (uput(&us, data, sizeof(us)))
                return -1;
            return 0;
        }
        return 0;
    }
#ifdef CONFIG_PC3_SOUND
    if (request == SNDIOC_SOUND)
    {
        extern int sound_cmd(uint16_t, int16_t, uint16_t, uint16_t);
        struct snd_cmd sc;
        if (uget(data, &sc, sizeof(sc)))
            return -1;
        if (sound_cmd(sc.chan, sc.amp, sc.pitch, sc.dur)) {
            udata.u_error = EAGAIN;
            return -1;
        }
        return 0;
    }
    if (request == SNDIOC_ENV)
    {
        extern void sound_envelope(const uint8_t *);
        uint8_t e[14];
        if (uget(data, e, sizeof(e)))
            return -1;
        sound_envelope(e);
        return 0;
    }
    if (request == SNDIOC_QUIET)
    {
        extern void sound_quiet(void);
        sound_quiet();
        return 0;
    }
#endif
    return -1;
}

uaddr_t pagemap_base(void)
{
    return PROGBASE;
}

/* The one interpreter "#!" execs (CONFIG_SCRIPT_INTERP): cc's output
 * starts with this line so compiled programs run as ./prog. */
const uint8_t script_interp_path[] = "/usr/bin/bcrun";

const uint8_t *plt_script_interp(void)
{
    return script_interp_path;
}

/* exec is committed to the new image: nothing out-of-process from the
 * old one survives (CONFIG_PLT_EXEC_CLEANUP) */
void plt_exec_cleanup(void)
{
    arena_release(udata.u_ptab);
}

usize_t valaddr(const uint8_t *base, usize_t size, uint_fast8_t is_write)
{
        if (base + size < base)
                size = MAXUSIZE - (usize_t)base + 1;
        if (!base || base < (const uint8_t *)PROGBASE) {
                /* Not the image - but a buffer inside a PSRAM arena
                 * this process owns is equally legitimate (arena.c) */
                uint32_t n = arena_valaddr((uint32_t)(size_t)base, size);
                if (n)
                        return n;
                /* Exec's "#!" support hands n_open the interpreter's
                 * path, a kernel string: accept reads of exactly that
                 * one object and nothing else. */
                if (!is_write &&
                    base >= script_interp_path &&
                    base < script_interp_path + sizeof("/usr/bin/bcrun"))
                        return size;
                size = 0;
        }
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


