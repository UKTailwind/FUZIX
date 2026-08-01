#include <stdlib.h>
#include <kernel.h>
#include <kdata.h>
#include "devtty.h"
#include "picosdk.h"
#include "core1.h"
#include "rawuart.h"
#include "kernel-armm0.def"
#include "globals.h"
#include "printf.h"
#include "psram.h"

uint32_t psram_size;

//the led that indicates power
//The on board one is pin 25
const uint POWER_LED = 25;

uint_fast8_t plt_param(char* p)
{
    char *s;
    uint8_t drv;
#ifdef CONFIG_PC3_USB_KBD
    /* kbd=us|uk|de|fr|es|be : keyboard layout (type at the bootdev
     * prompt, e.g. "hdb2 kbd=de") */
    if (strncmp(p, "kbd=", sizeof("kbd=")-1) == 0)
    {
        extern int kbd_set_layout(const char *name);
        return kbd_set_layout(p + sizeof("kbd=")-1) == 0;
    }
#endif
    /* psram=<n>[K|M] : resize the userland arena (0 disables it and
     * returns the space to the disc).  Type at the bootdev prompt,
     * e.g. "hdb2 psram=2M"; rc's swapon size must agree with the disc
     * that results - an overrunning swap gets EIO, not corruption. */
    if (strncmp(p, "psram=", sizeof("psram=")-1) == 0)
    {
        extern void psram_disc_resize(void);
        uint32_t v = 0;
        s = p + sizeof("psram=")-1;
        while (*s >= '0' && *s <= '9')
            v = v * 10 + (*s++ - '0');
        if (*s == 'M' || *s == 'm')
            v <<= 20;
        else if (*s == 'K' || *s == 'k')
            v <<= 10;
        arena_len = v & ~4095u;
        psram_disc_resize();
        return 1;
    }
    if (strncmp(p, "tty=", sizeof("tty=")-1) == 0)
    {
        ttymap_count = 0;
        p += sizeof("tty=")-1;
        while(*p)
        {
            s = p;
            while(*p && *p != ',' && *p != '\n')
                p++;
            if(*p)
                *p++=0;
            drv = 0xff;
            if (strncmp(s, "usb", 3) == 0)
            {
                s += 3;
                drv = TTYDRV_USB;
            }
            else if(strncmp(s, "uart", 4) == 0)
            {
                s += 4;
                drv = TTYDRV_UART;
            }

            if (drv == 0xff || !(*s))
            {
                kprintf("invalid param %s\n", s);
                panic("tty=");
            }
            ttymap[ttymap_count+1].tty = atoi(s);
            ttymap[ttymap_count+1].drv = drv;
            ttymap_count++;
            if (ttymap_count >= NUM_DEV_TTY)
            {
                panic("ttycount");
            }
        }
        devtty_init();
        return 1;
    }
    return 0;
}

void fatal_exception_handler(struct extended_exception_frame* eh)
{
    kprintf("FLAGRANT SYSTEM ERROR! EXCEPTION %d\n", eh->cause);
    /*
     * Order matters more than tidiness here: the console TX can die
     * partway through a dump in fault context, so everything needed to
     * IDENTIFY the fault goes out first - what faulted, where it
     * faulted, and where the program was loaded, so a PC can be turned
     * back into a source line.  The general registers are context and
     * come after; they were what survived when the interesting lines
     * did not.
     */
    kprintf("CFSR=%p HFSR=%p MMFAR=%p BFAR=%p\n",
        *(volatile uint32_t *)0xE000ED28, *(volatile uint32_t *)0xE000ED2C,
        *(volatile uint32_t *)0xE000ED34, *(volatile uint32_t *)0xE000ED38);
    kprintf("pc=%p lr=%p sp=%p PROGLOAD=%p\n",
        eh->pc, eh->lr, eh->sp, PROGLOAD);
    kprintf("user mode relative: pc=%p lr=%p pid=%d\n",
        eh->pc-PROGLOAD, eh->lr-PROGLOAD,
        udata.u_ptab ? udata.u_ptab->p_pid : -1);
    kprintf(" r0=%p r1=%p  r2=%p  r3=%p\n", eh->r0, eh->r1, eh->r2, eh->r3);
    kprintf(" r4=%p r5=%p  r6=%p  r7=%p\n", eh->r4, eh->r5, eh->r6, eh->r7);
    kprintf(" r8=%p r9=%p r10=%p r11=%p\n", eh->r8, eh->r9, eh->r10, eh->r11);
    kprintf("r12=%p sp=%p  lr=%p  pc=%p\n", eh->r12, eh->sp, eh->lr, eh->pc);
    kprintf("PROGBASE=%p PROGLOAD=%p PROGTOP=%p\n", PROGBASE, PROGLOAD, PROGTOP);
    kprintf("UDATA=%p KSTACK=%p-%p\n", &udata, &udata+1, ((uint32_t)&udata) + UDATA_SIZE);
    kprintf("isp=%p brk=%p\n", udata.u_isp, udata.u_break);
    /* Why: CFSR decodes usage/bus/mem faults, HFSR says if escalated,
     * MMFAR/BFAR give the faulting address when valid */
    kprintf("CFSR=%p HFSR=%p MMFAR=%p BFAR=%p\n",
        *(volatile uint32_t *)0xE000ED28, *(volatile uint32_t *)0xE000ED2C,
        *(volatile uint32_t *)0xE000ED34, *(volatile uint32_t *)0xE000ED38);
    {
        extern uint32_t dbg_redirs, dbg_entries, dbg_exits;
        extern uint32_t dbg_redir_sp, dbg_redir_pc, dbg_redir_xpsr;
        kprintf("preempt: redirs=%p entries=%p exits=%p\n",
            dbg_redirs, dbg_entries, dbg_exits);
        kprintf("last redirect: sp=%p pc=%p xpsr=%p\n",
            dbg_redir_sp, dbg_redir_pc, dbg_redir_xpsr);
    }
    panic("fatal exception");
}

void syscall_handler(struct svc_frame* eh)
{
    udata.u_callno = *(uint8_t*)(eh->pc - 2);
    udata.u_argn = eh->r0;
    udata.u_argn1 = eh->r1;
    udata.u_argn2 = eh->r2;
    udata.u_argn3 = eh->r3;
    udata.u_insys = 1;

    unix_syscall();

    /* Was "= 1" - a typo, never cleared. Harmless while nothing consumed
     * u_insys asynchronously, fatal to the PendSV preempt check. */
    udata.u_insys = 0;
    eh->r0 = udata.u_retval;
    eh->r1 = udata.u_error;
}

int main(void)
{
    /* Grant coprocessor 4 - the RP2350's double-precision DCP - to
     * everything.  Userland's libc __aeabi_d* routines run on it; they
     * are the SDK's self-saving wrappers, which save and restore an
     * interrupted context's 24 bytes of DCP state on their own stack,
     * so no context-switch support is needed here - only access.
     * (CPACR field n is bits [2n+1:2n]; 0b11 = full access.) */
    *(volatile uint32_t *)0xE000ED88 |= 3u << (2 * 4);
    __asm volatile("dsb; isb");
    /* The engaged flag is stale out of reset; the wrappers' PCMP test
     * would forever take the save/restore path against garbage state.
     * Reading via RCMP clears it - the SDK's runtime init does the
     * same thing for the same reason. */
    __asm volatile("mrc p4, #0, r0, c0, c0, #1" : : : "r0");

#ifdef PC3_SYS_CLOCK_KHZ
    /* Pico Computer 3: raise clk_sys before anything derives a divisor
     * from it (uart, spi). clk_peri follows clk_sys, as in the PC3's
     * MicroPython/MMBasic firmwares. The flash QMI divisor must be
     * re-capped immediately after; PSRAM timing derives from the final
     * clk_sys inside psram_init. */
    set_sys_clock_khz(PC3_SYS_CLOCK_KHZ, true);
    qmi_flash_timing(PC3_FLASH_MAX_HZ);
    psram_size = psram_init(PC3_PSRAM_CS_PIN);
#endif

    // early init to handle boot kernel messages
    devtty_early_init();

#ifdef PC3_SYS_CLOCK_KHZ
    /* build stamp: makes the running kernel identify itself, so a
     * stale flash can never be mistaken for the current one */
    kprintf("kernel build: %s %s\n", __DATE__, __TIME__);
    kprintf("clk_sys %dMHz; PSRAM ", (int)(clock_get_hz(clk_sys) / 1000000));
    if (psram_size) {
        /* quick confidence check through the XIP window */
        volatile uint32_t *p = (volatile uint32_t *)PSRAM_BASE;
        p[0] = 0x600DF00D;
        p[1] = ~0x600DF00D;
        if (p[0] == 0x600DF00D && p[1] == ~0x600DF00Du)
            kprintf("%dKiB at 0x11000000\n", (int)(psram_size >> 10));
        else
            kprintf("FAILED r/w test\n");
    } else {
        kprintf("not found\n");
    }
#endif

    if ((U_DATA__U_SP_OFFSET != offsetof(struct u_data, u_sp)) ||
        (U_DATA__U_PTAB_OFFSET != offsetof(struct u_data, u_ptab)) ||
        (P_TAB__P_PID_OFFSET != offsetof(struct p_tab, p_pid)) ||
        (P_TAB__P_STATUS_OFFSET != offsetof(struct p_tab, p_status)) ||
        (UDATA_SIZE_ASM != UDATA_SIZE))
    {
        kprintf("U_DATA__U_SP = %d\n", offsetof(struct u_data, u_sp));
        kprintf("U_DATA__U_PTAB = %d\n", offsetof(struct u_data, u_ptab));
        kprintf("P_TAB__P_PID_OFFSET = %d\n", offsetof(struct p_tab, p_pid));
        kprintf("P_TAB__P_STATUS_OFFSET = %d\n", offsetof(struct p_tab, p_status));
        panic("bad offsets");
    }
    ramsize = (SRAM_END - SRAM_BASE) / 1024;
    procmem = USERMEM / 1024;
    //turn on power led
    gpio_init(POWER_LED);
    gpio_set_dir(POWER_LED, GPIO_OUT);
    gpio_put(POWER_LED, 1);

    /*
     * The boot udata lives at progbase, plain SRAM outside the
     * kernel's bss, and SRAM survives a warm reset.  create_init()
     * wipes only the file table and trusts everything else to be
     * zero - true on ports where udata sits in bss, false here: after
     * a reset it still held the PREVIOUS run's udata, and i_ref() of
     * a stale u_cwd inode pointer bus-faulted the boot.  A flash
     * cycle papered over it because the BOOTSEL ROM session happens
     * to trample this SRAM.  Zero what the core assumes is zeroed.
     */
    memset(&udata, 0, sizeof(struct u_data));

    di();
    fuzix_main();
}

/* vim: sw=4 ts=4 et: */


