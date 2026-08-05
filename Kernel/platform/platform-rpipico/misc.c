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
#include "rawuart.h"

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
    rawuart_flush_polled();     /* say it before the watchdog bites */
    multicore_reset_core1();
    watchdog_reboot(0, 0, 0);
}

void plt_monitor(void)
{
    /* This is where panic() ends up, so getting the message out is the
     * entire job.  It used to be sleep_ms(1) "wait to print any
     * remaining messages" - about eleven characters at 115200, against
     * panic lines of well over a hundred, and nothing at all if the
     * transmit interrupt cannot run.  Poll the ring out instead. */
    rawuart_flush_polled();
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
    /* Everything below draws through the CALLER's own write target, so
     * point the primitives at it before any of them runs.  Unconditional
     * rather than picked out per request: it is a compare and a store,
     * and the one thing that must never happen is a drawing ioctl that
     * was missed off the list landing in another process's layer. */
    display_fb_enter(udata.u_ptab);
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
        /* The hot path.  The coordinates are packed into the data
         * argument ITSELF, so there is no uget and nothing to validate
         * - the whole call is a switch and a store.  MMBasic's PIXEL
         * statement costs 5us; this measured 1.30us. */
        uint32_t v = (uint32_t)data;
        return display_gfx_pixel(v & 0x3FF, (v >> 10) & 0x1FF,
                                 display_gfx_curcol());
    }
    if (request == GFXIOC_COLOUR)
    {
        /* data IS the RGB888 value - 24 bits fit in the argument. */
        display_gfx_colour((uint32_t)data);
        return 0;
    }
    if (request == GFXIOC_GETPIXEL)
    {
        uint32_t v = (uint32_t)data;
        return display_gfx_getpixel(v & 0x3FF, (v >> 10) & 0x1FF);
    }
    if (request == GFXIOC_RECT)
    {
        struct gfx_rect gr;
        if (uget(data, &gr, sizeof(gr)))
            return -1;
        return display_gfx_rect(gr.x1, gr.y1, gr.x2, gr.y2,
                                display_gfx_curcol());
    }
    if (request == GFXIOC_PIXELS || request == GFXIOC_RECTS)
    {
        /* One shape, one crossing.  The arrays are read where they lie,
         * blessed once by valaddr_r - the same trick GFXIOC_BITMAP uses,
         * and the reason a 640 point line costs 2.5K of transfer rather
         * than a copy into a kernel buffer there is no room for. */
        struct gfx_batch gb;
        int isz, bytes;

        if (uget(data, &gb, sizeof(gb)))
            return -1;
        if (gb.count == 0)
            return 0;
        if (gb.count > GFX_BATCH_MAX || gb.flags) {
            udata.u_error = EINVAL;
            return -1;
        }
        isz = (request == GFXIOC_PIXELS) ? (int)sizeof(struct gfx_pt)
                                         : (int)sizeof(struct gfx_rc);
        bytes = (int)gb.count * isz;
        if (valaddr_r(gb.items, bytes) != (usize_t)bytes) {
            udata.u_error = EFAULT;
            return -1;
        }
        if (gb.colours) {
            int cb = (int)gb.count * 4;
            if (valaddr_r(gb.colours, cb) != (usize_t)cb) {
                udata.u_error = EFAULT;
                return -1;
            }
        }
        if (request == GFXIOC_PIXELS)
            return display_gfx_pixels(gb.items, gb.count, gb.colours);
        return display_gfx_rects(gb.items, gb.count, gb.colours);
    }
    if (request == GFXIOC_BITMAP)
    {
        /* The bits are read where they lie.  Copying them in would want
         * a buffer the kernel has no room for - RAM here is within a
         * few hundred bytes of full - and valaddr_r blesses the whole
         * span once, which is the same guarantee for a lot less. */
        struct gfx_bitmap gb;
        int nbytes;
        if (uget(data, &gb, sizeof(gb)))
            return -1;
        if (gb.width == 0 || gb.height == 0 || gb.scale == 0) {
            udata.u_error = EINVAL;
            return -1;
        }
        nbytes = ((int)gb.width * gb.height + 7) / 8;
        if (valaddr_r(gb.bits, nbytes) != (usize_t)nbytes) {
            udata.u_error = EFAULT;
            return -1;
        }
        return display_gfx_bitmap(gb.x, gb.y, gb.width, gb.height,
                                  gb.scale,
                                  display_gfx_map((uint32_t)gb.fg),
                                  gb.bg < 0 ? -1
                                            : display_gfx_map((uint32_t)gb.bg),
                                  gb.bits);
    }
    if (request == GFXIOC_FBOPEN)
    {
        /* data is the value itself, not a pointer: one int in, like
           GFXIOC_MODE's neighbours. */
        int r = display_fb_open(udata.u_ptab, (int)(intptr_t)data);
        if (r) {
            /* EBUSY and EINVAL say different things, and a program that
               cannot have the layer deserves to know which. */
            udata.u_error = (r == -2) ? EBUSY : EINVAL;
            return -1;
        }
        return 0;
    }
    if (request == GFXIOC_FBSEL)
    {
        if (display_fb_select(udata.u_ptab, (int)(intptr_t)data)) {
            udata.u_error = EINVAL;
            return -1;
        }
        return 0;
    }
    if (request == GFXIOC_FBCOPY)
    {
        if (display_fb_copy(udata.u_ptab, (int)(intptr_t)data)) {
            udata.u_error = EINVAL;
            return -1;
        }
        return 0;
    }
    if (request == GFXIOC_VSYNC)
    {
        display_wait_vblank();
        return 0;
    }
    if (request == GFXIOC_SCROLL)
    {
        /* rows in the top byte, signed; the colour to fill with in the
         * low 24 as RGB888, like every other call here - the caller
         * should not have to know the mode's own colour numbers. */
        uint32_t v = (uint32_t)data;
        int rows = (int)(int8_t)(v >> 24);

        if (display_gfx_scroll(rows, display_gfx_map(v & 0xFFFFFF))) {
            udata.u_error = EINVAL;
            return -1;
        }
        return 0;
    }
    if (request == GFXIOC_TEXT)
    {
        /* The string is read where it lies, blessed once by valaddr_r -
         * the same trick GFXIOC_BITMAP and the batch calls use, and the
         * reason a line of text costs one crossing and no kernel
         * buffer. */
        struct gfx_text gt;

        if (uget(data, &gt, sizeof(gt)))
            return -1;
        if (gt.len == 0)
            return 0;
        if (gt.len > GFX_TEXT_MAX || gt.scale == 0) {
            udata.u_error = EINVAL;
            return -1;
        }
        if (valaddr_r(gt.str, gt.len) != (usize_t)gt.len) {
            udata.u_error = EFAULT;
            return -1;
        }
        /* 0 means font 1, so a caller built before there were fonts to
         * choose from still gets the console's.  Still returns the x the
         * text ended at, which is what lets a caller lay a line out
         * piece by piece; a font that does not exist is the one error. */
        if (!display_font(gt.font ? gt.font : 1, 0, 0, 0, 0)) {
            udata.u_error = EINVAL;
            return -1;
        }
        return display_gfx_text(gt.x, gt.y, gt.font ? gt.font : 1, gt.scale,
                                display_gfx_map((uint32_t)gt.fg),
                                gt.bg < 0 ? -1
                                          : display_gfx_map((uint32_t)gt.bg),
                                gt.str, (int)gt.len);
    }
    if (request == PICOIOC_LIBM)
    {
        /* The address of the table, for a program to call through.
         * Nothing is copied and nothing is validated beyond the write:
         * what comes back is a kernel flash address, and the caller
         * checks the magic and version before trusting it. */
        extern const struct pc3_libm *plt_libm(void);
        const void *p = (const void *)plt_libm();

        if (uput(&p, data, sizeof(p)))
            return -1;
        return 0;
    }
    if (request == GFXIOC_MAP)
    {
        /* index and colour both fit the argument: 24 bits of RGB888 and
         * the entry number in the top byte, so no uget. */
        uint32_t v = (uint32_t)data;

        if (display_gfx_remap((int)(v >> 24) & 0xFF, v & 0xFFFFFF)) {
            udata.u_error = EINVAL;
            return -1;
        }
        return 0;
    }
    if (request == GFXIOC_MAPCTL)
    {
        int r = ((int)(intptr_t)data == GFX_MAP_RESET)
                ? display_gfx_remap_reset()
                : display_gfx_remap_apply();
        if (r) {
            udata.u_error = EINVAL;
            return -1;
        }
        return 0;
    }
    if (request == GFXIOC_FONTINFO)
    {
        struct gfx_fontinfo gf;
        int w = 0, h = 0, first = 0, count = 0;

        if (uget(data, &gf, sizeof(gf)))
            return -1;
        display_font(gf.font, &w, &h, &first, &count);
        gf.width = (uint8_t)w;
        gf.height = (uint8_t)h;
        gf.first = (uint8_t)first;
        gf.count = (uint16_t)count;
        gf.nfonts = (uint16_t)display_font_count();
        if (uput(&gf, data, sizeof(gf)))
            return -1;
        return 0;
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
        int size = display_gfx_fbsize();
        if (uget(data, &gb, sizeof(gb)))
            return -1;
        if (size == 0 || gb.offset >= size || gb.len > size - gb.offset) {
            udata.u_error = EINVAL;
            return -1;
        }
        /* Into the caller's write target, not disp_fb: BLIT is a drawing
         * operation like the rest, so a shadow-buffer program keeps
         * working when it is pointed at the layer. */
        if (uget(gb.buf, display_fb_target() + gb.offset, gb.len))
            return -1;
        return 0;
    }
#endif
    if (request == PSRAMIOC_REALLOC)
    {
        /* Grow or shrink an allocation. rq.base in, the NEW base out -
         * which may differ, because newlib moves the block when it
         * cannot extend in place. A caller holding interior pointers
         * into the old block has to rebuild them; that is why this is
         * a separate call and not something alloc does quietly. */
        struct psram_req rq;
        uint32_t b;
        if (uget(data, &rq, sizeof(rq)))
            return -1;
        b = arena_realloc(udata.u_ptab, rq.base, rq.len);
        if (!b) {
            udata.u_error = ENOMEM;
            return -1;
        }
        rq.base = b;
        if (uput(&rq, data, sizeof(rq)))
            return -1;
        return 0;
    }
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
#ifdef CONFIG_PC3_DISPLAY
    /* The new image did not create it and has no idea it is selected;
     * leaving the claim would have it drawing into a layer it cannot
     * show. */
    display_fb_release(udata.u_ptab);
#endif
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


