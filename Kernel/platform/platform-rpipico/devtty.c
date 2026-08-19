#include <kernel.h>
#include <kdata.h>
#include <printf.h>
#include <stdbool.h>
#include <vt.h>
#include <tty.h>
#include "rawuart.h"
#include "picosdk.h"
#include <pico/multicore.h>
#include "core1.h"
#include "devtty.h"

uint8_t ttybuf[TTYSIZ * NUM_DEV_TTY];

int ttymap_count;
struct ttymap ttymap[NUM_DEV_TTY + 1];
struct s_queue ttyinq[NUM_DEV_TTY + 1];
tcflag_t termios_mask[NUM_DEV_TTY + 1];

void no_setup(uint_fast8_t minor, uint_fast8_t devn, uint_fast8_t flags)
{
    used(minor);
    used(devn);
    used(flags);
}

#ifdef CONFIG_PC3_DISPLAY
/* Output mirrors to the uart and the video display (console.c) */
extern void console_putc(uint8_t devn, uint8_t c);
#define UART_PUTC console_putc
#else
#define UART_PUTC rawuart_putc
#endif
#ifdef CONFIG_PC3_USB_KBD
/* Input merges the USB keyboard and the uart (usbkbd.c), which also
 * pumps the host stack before the tty sleeps. */
extern int console_getc(uint8_t devn);
extern void console_sleeping(uint8_t devn);
#define UART_GETC console_getc
#define UART_SLEEPING console_sleeping
#else
#define UART_GETC rawuart_getc
#define UART_SLEEPING rawuart_sleeping
#endif

struct ttydriver ttydrivers[2] =
    {
        {UART_PUTC, rawuart_ready, UART_SLEEPING, UART_GETC, rawuart_setup},
        {usbconsole_putc, usbconsole_ready, usbconsole_sleeping, usbconsole_getc, no_setup},
};

static void devtty_defconfig(uint8_t drv, int count, int minor)
{
    int devnum = 1;
    while (devnum <= count)
    {
        ttymap[minor].tty = devnum++;
        ttymap[minor].drv = drv;
        if (drv == TTYDRV_USB)
        {
            termios_mask[minor] = _CSYS;
        }
        else
        {
            termios_mask[minor] = CSIZE | CBAUD | PARENB | PARODD | _CSYS;
        }
        minor++;
    }
}

/* To be called right after startup to be able to print boot messages */
void devtty_early_init(void)
{
    rawuart_early_init();
    core1_init();
#ifdef CONFIG_PC3_DISPLAY
    {
        extern void console_init(void);
        console_init();
    }
#endif
    devtty_init();
}

void devtty_init(void)
{
    int defconfig = 0;
    if (ttymap_count == 0)
    {
        defconfig = 1;
    }
    for (int i = 1; i <= NUM_DEV_TTY; i++)
    {
        ttyinq[i].q_base = ttyinq[i].q_head = ttyinq[i].q_tail = &ttybuf[TTYSIZ * (i - 1)];
        ttyinq[i].q_size = TTYSIZ;
        ttyinq[i].q_count = 0;
        ttyinq[i].q_wakeup = TTYSIZ / 2;
    }

    if (defconfig)
    {
        absolute_time_t until = delayed_by_ms(get_absolute_time(), DEV_USB_DETECT_TIMEOUT);

        int usb_detected = 0;
        while (absolute_time_diff_us(get_absolute_time(), until) > 0)
        {
            if (usbconsole_is_available(1))
            {
                usb_detected = 1;
                break;
            }
        }

        if (usb_detected)
        {
            devtty_defconfig(TTYDRV_USB, NUM_DEV_TTY_USB, 1);
            devtty_defconfig(TTYDRV_UART, NUM_DEV_TTY_UART, 1 + NUM_DEV_TTY_USB);
            until = delayed_by_ms(get_absolute_time(), DEV_USB_INIT_TIMEOUT);
            while (absolute_time_diff_us(get_absolute_time(), until) > 0)
            {
                tight_loop_contents();
            }
            kprintf("devtty: %s as default tty\n", "usb");
        }
        else
        {
            devtty_defconfig(TTYDRV_UART, NUM_DEV_TTY_UART, 1);
            devtty_defconfig(TTYDRV_USB, NUM_DEV_TTY_USB, 1 + NUM_DEV_TTY_UART);
            kprintf("devtty: %s as default tty\n", "uart");
        }
        ttymap_count = NUM_DEV_TTY;
    }
}

/* Output for the system console (kprintf etc) */
void kputchar(uint_fast8_t c)
{
    /* If tty's were not properly initialized */
    if (ttymap_count == 0)
    {
        if (c == '\n')
            rawuart_putc(1, '\r');
        rawuart_putc(1, c);
    }
    else
    {
        if (c == '\n')
        {
            while (tty_writeready(1) != TTY_READY_NOW)
            {
                tight_loop_contents();
            }
            tty_putc(1, '\r');
        }
        while (tty_writeready(1) != TTY_READY_NOW)
        {
            tight_loop_contents();
        }
        tty_putc(1, c);
    }
}

void tty_putc(uint_fast8_t minor, uint_fast8_t c)
{
    struct ttymap *map = &ttymap[minor];
    if (map->tty == 0)
        return;
    struct ttydriver *drv = &ttydrivers[map->drv];
    drv->putc(map->tty, c);
#ifdef CONFIG_PC3_USB_KBD
    /* A process that only WRITES starves the USB host pump, and then
     * the keyboard stops existing - see usbkbd_pump_if_starved for the
     * whole story.  Here rather than in console_putc because this is
     * one path with one exit, where console_putc has four and holds
     * escape-sequence state across them. */
    {
        extern void usbkbd_pump_if_starved(void);
        usbkbd_pump_if_starved();
    }
#endif
}

ttyready_t tty_writeready(uint_fast8_t minor)
{
    struct ttymap *map = &ttymap[minor];
    if (map->tty == 0)
        return TTY_READY_LATER;
    struct ttydriver *drv = &ttydrivers[map->drv];
    return drv->ready(map->tty);
}

/* For the moment */
int tty_carrier(uint_fast8_t minor)
{
    return 1;
}

void tty_sleeping(uint_fast8_t minor)
{
    struct ttymap *map = &ttymap[minor];
    if (map->tty == 0)
        return;
    struct ttydriver *drv = &ttydrivers[map->drv];
    drv->sleeping(map->tty);
}

void tty_data_consumed(uint_fast8_t minor) {}

/*
 *	This function is called whenever the terminal interface is opened
 *	or the settings changed. It is responsible for making the requested
 *	changes to the port if possible. Strictly speaking it should write
 *	back anything that cannot be implemented to the state it selected.
 */
void tty_setup(uint_fast8_t minor, uint_fast8_t flags)
{
    struct ttymap *map = &ttymap[minor];
    if (map->tty == 0)
        return;
    struct ttydriver *drv = &ttydrivers[map->drv];
    drv->setup(minor, map->tty, flags);
}

void tty_interrupt(void)
{
    int c;
    for (int minor = 1; minor <= ttymap_count; minor++)
    {
        struct ttymap *map = &ttymap[minor];
        if (map->tty == 0)
            continue;
        struct ttydriver *drv = &ttydrivers[map->drv];
        while ((c = drv->getc(map->tty)) >= 0)
        {
            /* Emergency break: ^\ always raises SIGQUIT, even when the
             * foreground program holds the tty raw with ISIG off.  The
             * only session lives on this console - without this a
             * wedged raw-mode program forces a reset (and an fsck).
             * Restore sane line discipline so the shell that inherits
             * the tty is usable. */
            if (c == 0x1C)
            {
                struct tty *t = &ttydata[minor];
                t->termios.c_lflag |= (ICANON | ECHO | ECHOE | ISIG);
                t->termios.c_oflag |= (OPOST | ONLCR);
                t->termios.c_iflag |= ICRNL;
                if (t->pgrp)
                    sgrpsig(t->pgrp, SIGQUIT);
                continue;
            }
            {
                /* canonical-mode line editing + history (lineedit.c);
                 * raw-mode programs are untouched */
                extern int lineedit_input(uint_fast8_t, uint_fast8_t);
                if (lineedit_input(minor, c))
                    continue;
            }
            if (tty_inproc(minor, c) == 0)
            {
                break;
            }
        }
    }
}

/* Platform tty ioctl: KBRATE for the USB keyboard's auto-repeat, then
 * the generic handler.  KBRATE is the standard Fuzix interface (see
 * sys/kd.h and /bin/kbdrate) - a two-byte { first, continual } in
 * TENTHS of a second, so `kbdrate 2 6` gives a 600ms wait before the
 * first repeat and 200ms between the rest.  Tenths is coarse, but it
 * is what the existing tool speaks, and inventing a private ioctl for
 * a knob the system already has would be worse. */
int pc3_tty_ioctl(uint_fast8_t minor, uarg_t request, char *data)
{
#ifdef CONFIG_PC3_USB_KBD
    if (request == KBRATE)
    {
        extern void kbd_set_repeat(unsigned first_tenths, unsigned next_tenths);
        uint8_t kr[2];          /* { first, continual } */
        if (uget(data, kr, sizeof(kr)))
            return -1;
        kbd_set_repeat(kr[0], kr[1]);
        return 0;
    }
#endif
    return tty_ioctl(minor, request, data);
}
/* vim: sw=4 ts=4 et: */
