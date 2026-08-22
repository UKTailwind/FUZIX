#include <kernel.h>
#include <version.h>
#include <kdata.h>
#include <devsys.h>
#include <blkdev.h>
#include <tty.h>
#include <devtty.h>
#include <dev/devsd.h>
#include <printf.h>
#include "globals.h"
#include "picosdk.h"
#include <hardware/irq.h>
#include <hardware/structs/scb.h>
#include "core1.h"
#include "rawuart.h"

struct devsw dev_tab[] =  /* The device driver switch table */
{
// minor    open         close        read      write           ioctl
// ---------------------------------------------------------------------
  /* 0: /dev/hd - block device interface */
  {  blkdev_open,   no_close,   blkdev_read,    blkdev_write,	blkdev_ioctl},
  /* 1: /dev/fd - Floppy disk block devices */
  {  no_open,	    no_close,	no_rdwr,	no_rdwr,	no_ioctl},
  /* 2: /dev/tty	TTY devices */
  {  tty_open,     tty_close,   tty_read,  tty_write,  pc3_tty_ioctl },
  /* 3: /dev/lpr	Printer devices */
  {  no_open,     no_close,   no_rdwr,   no_rdwr,  no_ioctl  },
  /* 4: /dev/mem etc	System devices (one offs) */
  {  no_open,      sys_close,    sys_read, sys_write, sys_ioctl  },
  /* Pack to 7 with nxio if adding private devices and start at 8 */
};

static repeating_timer_t tick_timer;

bool validdev(uint16_t dev)
{
    /* This is a bit uglier than needed but the right hand side is
       a constant this way */
    if(dev > ((sizeof(dev_tab)/sizeof(struct devsw)) << 8) - 1)
	return false;
    else
        return true;
}

/*
 *	The system tick, driven by the SDK's repeating timer.
 *
 *	It used to re-arm its own hardware alarm, and that is what froze the
 *	machine: hardware_alarm_set_target returns true when the target has
 *	already passed, and in that case the alarm is NOT armed.  Nothing
 *	else re-armed it, so ONE missed deadline stopped the timer for good -
 *	no scheduling, no tty_interrupt, no signal delivery, no pre-emption.
 *	The board stayed powered and completely unresponsive, core1 kept
 *	painting a perfect picture, and nothing faulted so there was no crash
 *	dump to say why.
 *
 *	The pool cannot do that.  The entry stays in its ordered list and the
 *	pool re-programs the hardware from the head of that list, so there is
 *	no state in which the tick is simply gone; a target already in the
 *	past makes the callback fire again immediately instead.
 *
 *	The delay is NEGATIVE deliberately, as MMBasic's is
 *	(PicoMite.c:903, add_repeating_timer_us(-1000)): the sign selects the
 *	phase reference.  Negative measures the next fire from the SCHEDULED
 *	target, so the rate stays exactly one period however long this
 *	handler takes; positive would measure from now and let the tick drift
 *	by the handler's own duration.
 */
/*
 *	Read by the core1 stall watchdog (display.c).  The tick holds di()
 *	across its whole body, so if it hangs anywhere in here interrupts
 *	stay off for ever and nothing on core0 is left to say where.  One
 *	store per step costs nothing and turns "the tick stopped" into the
 *	name of the call that stopped it.
 */
volatile uint32_t pc3_tickbeat;
volatile uint8_t pc3_tickphase;

#define TICK_PHASE(n)	(pc3_tickphase = (n))

static bool timer_tick_cb(repeating_timer_t *rt)
{
    irqflags_t irq = di();
    udata.u_ininterrupt = 1;
    pc3_tickbeat++;

    (void)rt;

#ifdef CONFIG_PC3_USB_KBD
    {
        extern void usbkbd_tick(void);
        TICK_PHASE(1);
        usbkbd_tick();
    }
#endif
    TICK_PHASE(2);
    tty_interrupt();
    /* After the echo, not before: tty_interrupt() is what queues it, and
     * this is the kick that makes sure a tick's worth of queued output
     * actually leaves.  See rawuart_tx_poll. */
    TICK_PHASE(3);
    rawuart_tx_poll();
    TICK_PHASE(4);
    timer_interrupt();
    /* A player waiting for room in the PCM ring is woken HERE and
       not from the DMA IRQ: this is the kernel-s own interrupt
       path, where touching the process table is safe.  5ms of
       granularity, against the 100ms floor usleep gives userland -
       which is what lets an audio queue be short. */
    {
        extern void sound_pcm_tick(void);
        sound_pcm_tick();
    }
    TICK_PHASE(5);

    /* Pre-empt / signal a running user process: pend PendSV, whose
       handler redirects user-mode PCs through the preempt trampoline
       (tricks.S). Without this a spinning process could neither be
       rescheduled nor killed from the keyboard. The trampoline also
       pumps the USB host stack when it has been starved by a spinning
       process (usbkbd_starved) - never from interrupt context. */
    {
#ifdef CONFIG_PC3_USB_KBD
        extern int usbkbd_starved(void);
        int starved = usbkbd_starved();
#else
        int starved = 0;
#endif
        TICK_PHASE(6);
        if (!udata.u_insys && udata.u_ptab &&
            (starved || need_resched ||
             (udata.u_ptab->p_sig[0].s_pending & ~udata.u_ptab->p_sig[0].s_held) ||
             (udata.u_ptab->p_sig[1].s_pending & ~udata.u_ptab->p_sig[1].s_held))) {
            scb_hw->icsr = 1u << 28; /* PENDSVSET */
        }
    }

    TICK_PHASE(7);
    udata.u_ininterrupt = 0;
    irqrestore(irq);
    /*	0 = left the tick cleanly.  If the watchdog reports a stall at
     *	phase 0 then the body is not the problem at all: the tick
     *	finished and was simply never called again, which puts the fault
     *	in the alarm pool or in what PendSV went off and did. */
    TICK_PHASE(0);
    return true;			/* keep repeating */
}

#ifdef CONFIG_NET
extern void netdev_init(void);
#endif

void device_init(void)
{
    extern void ds3231_init(void);
    extern void psram_disc_init(void);
    extern void preempt_init(void);
    extern void rawuart_rx_irq_start(void);

    preempt_init();

    /* Take the console uart under interrupt here and nowhere earlier.
     * start.c calls device_init() after pagemap_init() and
     * create_init(), so udata, the process table and the per-process
     * kernel stack all exist by now.
     *
     * devtty_early_init() and devtty_init() are both too early -
     * devtty_init is called *from* devtty_early_init, which is why
     * moving it there changed nothing. A character arriving before
     * create_init() has finished took a hard fault inside makeproc
     * with a stack pointer outside KSTACK entirely. */
    rawuart_rx_irq_start();

    /* Timer interrupt must be initialized before block devices.
       set_boot_line uses the pause syscall which will not be operational
       otherwise.

       The pool claims the hardware alarm itself (alarm 0, pinned in
       CMakeLists.txt, which is the one this tick has always used), so
       there is no hardware_alarm_claim here and no callback to install.

       alarm_pool_init_default() is called explicitly rather than left to
       the SDK's runtime-init hook: the hook is conditional on macros this
       kernel does not control, and an uninitialised default pool would
       leave the machine with no tick at all - the exact failure being
       fixed here.  It checks whether the pool is already up, so calling
       it when the hook did run is a no-op.
       The first tick lands one period out rather than immediately, which
       the old hardware_alarm_force_irq forced; 5ms before the first tick
       is not a constraint anything here has. */
    alarm_pool_init_default();
    if (!add_repeating_timer_us(-(1000000 / TICKSPERSEC), timer_tick_cb,
			        NULL, &tick_timer))
	panic("tick");

    /* No flash_dev_init() here any more, and config.h has the whole
     * argument: the on-board flash filesystem was never a release
     * asset and it pinned 7,912 bytes of SRAM in place, because code
     * cannot execute from a device it is erasing.  The SD card is hda
     * now and the PSRAM swap disc is hdb. */
#ifdef CONFIG_NET
	netdev_init();
#endif
    ds3231_init();

#ifdef CONFIG_PICO_COMPUTER_3
    {
        /* Pico Computer 2 or 3? (board.c) - must precede the SD card,
         * whose wiring differs between the two. */
        extern void board_detect(void);
        board_detect();
    }
#endif

#ifdef CONFIG_NET
    {
        /* Just clears the socket table.  Nothing touches the radio
         * here - that waits for NETIOC_UP, see net_cyw43.c. */
        extern void netdev_init(void);
        netdev_init();
    }
#endif

    sd_rawinit();
    devsd_init();

    /* No PSRAM disc any more.  It existed to be a swap device, and
     * swap is now a per-process allocation out of the PSRAM heap
     * (swapout in swapper.c) - a memcpy into memory that is already
     * mapped, with no block layer, no swapon, and no LBA arithmetic
     * between the kernel and the bytes.  Removing it also hands the
     * whole window to the heap instead of 1 MiB of it. */
    {
        extern void swap_report_size(void);
        swap_report_size();     /* what `free` calls Swap: the heap */
    }

    {
        /* console command history: lives in PSRAM reserved above */
        extern void lineedit_init(void);
        lineedit_init();
    }

#ifdef CONFIG_PC3_SOUND
    {
        extern void sound_init(void);
        sound_init();
    }
#endif

    {
        /* The fixed PIO output programs (WS2812/BITSTREAM), loaded
           beside the I2S program - after sound_init so PIO1's layout
           is settled, though pioout.c claims by explicit number and
           would survive either order. */
        extern void pioout_init(void);
        pioout_init();
    }

#ifdef CONFIG_PC3_USB_KBD
    {
        extern void usbkbd_init(void);
        usbkbd_init();
    }
#endif

    /* Answer the bootdev: prompt ourselves.  Since the flash disc went
     * there is exactly one root this machine can have - partition 2 of
     * the SD card - and a question with one answer is not a question.
     *
     * The escape hatches are already in the mechanism, not added here:
     * set_boot_line waits 10 ticks and seeds NOTHING if a key is
     * pending, so holding a key through boot still gets the prompt
     * (for tty=... options, or another partition); and the seeded line
     * is consumed on its first use, so if hda2 will not mount - no
     * card, wrong card - the mount loop in start.c comes back around
     * to the prompt instead of asking hda2 for ever. */
    set_boot_line("hda2");
}

/* vim: sw=4 ts=4 et: */

