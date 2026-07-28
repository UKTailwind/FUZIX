#include <kernel.h>
#include <printf.h>
#include "picosdk.h"
#include "config.h"
#include "core1.h"

#include <pico/critical_section.h>
#include <pico/multicore.h>
#include <hardware/uart.h>
#include <hardware/irq.h>
#include "rawuart.h"

#if NUM_DEV_TTY_UART > 2
#error "Only two UARTs are supported"
#endif

#ifndef DEV_UART_0_CTS_PIN
#define DEV_UART_0_CTS_PIN 0
#endif
#ifndef DEV_UART_0_RTS_PIN
#define DEV_UART_0_RTS_PIN 0
#endif

#ifndef DEV_UART_1_CTS_PIN
#define DEV_UART_1_CTS_PIN 0
#endif
#ifndef DEV_UART_1_RTS_PIN
#define DEV_UART_1_RTS_PIN 0
#endif

/* Which hardware uart instance each tty uart uses. The pins given above
 * must be routable to that instance. */
#ifndef DEV_UART_0_INSTANCE
#define DEV_UART_0_INSTANCE 0
#endif
#ifndef DEV_UART_1_INSTANCE
#define DEV_UART_1_INSTANCE 1
#endif

static uint clocks[] = {
    0,      /* B0 */
    50,     /* B50 */
    75,     /* B75 */
    110,    /* B110 */
    134,    /* B134 */
    150,    /* B150 */
    300,    /* B300 */
    600,    /* B600 */
    1200,   /* B1200 */
    2400,   /* B2400 */
    4800,   /* B4800 */
    9600,   /* B9600 */
    19200,  /* B19200 */
    38400,  /* B38400 */
    57600,  /* B57600 */
    115200, /* B115200 */
};

/*
 *	Interrupt driven receive.
 *
 *	The tty layer drains the uart from tty_interrupt(), which runs on
 *	the 200Hz timer tick - once every 5ms. The PL011 receive FIFO is
 *	32 bytes deep and 115200 8N1 delivers about 58 characters in 5ms,
 *	so a third of every burst was being thrown away: fine for typing,
 *	fatal for anything that streams. It capped reliable input at
 *	roughly 6400 characters a second against a line offering 11520,
 *	which is why file transfer never worked and why pasting into the
 *	console corrupted text.
 *
 *	So take the characters under interrupt into a ring, exactly as
 *	MMBasic and MicroPython do on this hardware, and let the tick
 *	drain the ring instead of the FIFO. The ring holds far more than
 *	a tick's worth, so bursts survive scheduling latency.
 */
static inline uart_inst_t *rawuart_instance(uint_fast8_t num);

#define RXRING	512			/* power of two */

struct rxring {
	volatile unsigned head;		/* written by the interrupt */
	volatile unsigned tail;		/* written by the reader */
	volatile unsigned char buf[RXRING];
	volatile unsigned lost;		/* overruns, for diagnosis */
	volatile unsigned got;		/* characters taken by the interrupt */
};

static struct rxring rxring[2];

/*
 *	Transmit ring.
 *
 *	Output has to be interrupt driven too, and not because blocking is
 *	slow. tty_interrupt() echoes input from inside the timer tick, and
 *	timer_tick_cb() holds di() - PRIMASK - across the whole handler.
 *	Blocking there on the transmit FIFO therefore stalls with every
 *	interrupt masked, so the receive interrupt cannot run whatever its
 *	priority, and the receive FIFO overflows. That is why a burst got
 *	only about one tick's worth of characters through.
 *
 *	With a ring, putc queues and returns, the tick stays short, and
 *	receive keeps up. This is MMBasic's arrangement (io/Serial.c):
 *	blocking happens only when the ring itself is full.
 */
#define TXRING	256			/* power of two */

struct txring {
	volatile unsigned head;		/* written by the writer */
	volatile unsigned tail;		/* written by the interrupt */
	volatile unsigned char buf[TXRING];
};

static struct txring txring[2];

static int txring_empty(unsigned w)
{
	return txring[w].head == txring[w].tail;
}

static int txring_full(unsigned w)
{
	return ((txring[w].head + 1) & (TXRING - 1)) == txring[w].tail;
}

/* Move one byte from the ring to the hardware. Caller checks writable. */
static void txring_pump(uart_inst_t *uart, unsigned w)
{
	struct txring *t = &txring[w];
	if (t->head == t->tail)
		return;
	uart_get_hw(uart)->dr = t->buf[t->tail];
	t->tail = (t->tail + 1) & (TXRING - 1);
}

/*
 *	Handler copied from MMBasic (PicoMite io/Serial.c on_uart_irq0):
 *	take one character per interrupt into the ring, and on overflow
 *	drop the oldest. No ICR or IFLS handling - reading the data
 *	register is what clears the receive and receive-timeout sources,
 *	and the SDK's defaults for the FIFO trigger level are fine. This
 *	is deliberately the shape of code that is already proven on this
 *	silicon rather than anything cleverer.
 */
static void rawuart_rx_irq(unsigned which)
{
	uart_inst_t *uart = uart_get_instance(which == 0 ?
					      DEV_UART_0_INSTANCE :
					      DEV_UART_1_INSTANCE);
	struct rxring *r = &rxring[which];

	if (uart_is_readable(uart)) {
		unsigned char c = (unsigned char)uart_getc(uart);
		r->got++;
		r->buf[r->head] = c;
		r->head = (r->head + 1) & (RXRING - 1);
		if (r->head == r->tail) {
			/* overflowed: discard the oldest, as MMBasic does */
			r->tail = (r->tail + 1) & (RXRING - 1);
			r->lost++;
		}
	}

	/* Transmit, as MMBasic does: send the next byte, and when the ring
	 * runs dry turn the transmit interrupt off again so it stops
	 * asking. */
	if (uart_is_writable(uart)) {
		if (!txring_empty(which))
			txring_pump(uart, which);
		else
			uart_set_irq_enables(uart, true, false);
	}
}

/*
 *	Hook the vector statically, the way tricks.S hooks isr_svcall and
 *	isr_pendsv, rather than calling irq_set_exclusive_handler().
 *
 *	That call installs into the table VTOR points at, and only does
 *	anything when the SDK has built a writable RAM vector table. This
 *	kernel is PICO_COPY_TO_RAM, and the call left the default handler
 *	in place: the uart interrupt was enabled in the NVIC but vectored
 *	to __unhandled_user_irq, so no character ever reached the ring
 *	(the console simply did not respond) and a byte arriving after a
 *	reset took a hard fault with a garbage stack pointer.
 *
 *	crt0.S declares isr_irq0..isr_irq79 weak, so a strong definition
 *	here wins at link time and needs no runtime installation at all.
 *	The names must be literal, hence the static assertions.
 */
_Static_assert(UART0_IRQ == 33, "UART0_IRQ moved: rename isr_irq33");
_Static_assert(UART1_IRQ == 34, "UART1_IRQ moved: rename isr_irq34");

/* Which logical uart each hardware instance belongs to. On the PC3 the
 * console is GP8/GP9, which is hardware uart1. */
#define LOGICAL_OF_HW0	(DEV_UART_0_INSTANCE == 0 ? 0 : 1)
#define LOGICAL_OF_HW1	(DEV_UART_0_INSTANCE == 1 ? 0 : 1)

void isr_irq33(void) { rawuart_rx_irq(LOGICAL_OF_HW0); }	/* uart0 */
void isr_irq34(void) { rawuart_rx_irq(LOGICAL_OF_HW1); }	/* uart1 */

/* Take one character from the ring, or -1 if it is empty. */
static int rawuart_ring_get(unsigned which)
{
	struct rxring *r = &rxring[which];
	unsigned char c;

	if (r->head == r->tail)
		return -1;
	c = r->buf[r->tail];
	r->tail = (r->tail + 1) & (RXRING - 1);
	return c;
}

/* The handler is already in the vector table (isr_irq33/34 above), so
 * there is nothing to install: just enable it in the NVIC and turn on
 * receive in the uart. Transmit stays polled - blocking on a full
 * transmit FIFO cannot lose characters, which is what matters. */
static void rawuart_rx_irq_enable(uart_inst_t *uart, unsigned which)
{
	unsigned irqn = (uart == uart0) ? UART0_IRQ : UART1_IRQ;

	(void)which;

	/*
	 * Start from a clean slate. A warm reset leaves the uart running:
	 * there can be characters sitting in the receive FIFO and an
	 * interrupt already latched in the NVIC, which then fires the
	 * instant it is unmasked - before anything here is ready. That is
	 * why a reset crashed while a fresh flash did not.
	 */
	uart_set_irq_enables(uart, false, false);
	while (uart_is_readable(uart))
		(void)uart_get_hw(uart)->dr;
	uart_get_hw(uart)->icr = 0x7FF;		/* every source */
	irq_clear(irqn);

	/*
	 * Receive must outrank the timer tick.
	 *
	 * tty_interrupt() runs inside the timer interrupt and echoes as it
	 * goes, and echo blocks on the transmit FIFO. At equal priority
	 * the receive interrupt cannot preempt that, so while the tick
	 * handler is busy echoing a line the receive FIFO overflows and we
	 * lose characters exactly as before - a burst got about one tick's
	 * worth through (~55 characters at 115200) and then died. Lower
	 * number is higher priority; the SDK default is 0x80.
	 */
	irq_set_priority(irqn, 0x40);

	irq_set_enabled(irqn, true);
	uart_set_irq_enables(uart, true, false);
}

static uart_inst_t * rawuart_init_one(int num, int tx, int rx, int cts, int rts)
{
    uart_inst_t *uart = uart_get_instance(num);
    /* If the port is already running (tty re-init after early boot
     * messages), let queued output drain before resetting it */
    if (uart_get_hw(uart)->cr & UART_UARTCR_UARTEN_BITS)
        uart_tx_wait_blocking(uart);
    uart_init(uart, PICO_DEFAULT_UART_BAUD_RATE);
    gpio_set_function(tx, GPIO_FUNC_UART);
    gpio_set_function(rx, GPIO_FUNC_UART);
    uart_set_translate_crlf(uart, false);
    uart_set_fifo_enabled(uart, true);
    if (cts > 0)
    {
        gpio_set_function(cts, GPIO_FUNC_UART);
    }
    if (rts > 0)
    {
        gpio_set_function(rts, GPIO_FUNC_UART);
    }
    uart_set_hw_flow(uart, cts > 0, rts > 0);
    return uart;
}

static uint8_t rawuart_irq_installed[2];
static uint8_t rawuart_inited[2];
static uint8_t rawuart_irq_started;

static uart_inst_t * rawuart_init(uint_fast8_t uart)
{
    uart_inst_t *u;

    if (uart == 0)
    {
        u = rawuart_init_one(DEV_UART_0_INSTANCE,
                             DEV_UART_0_TX_PIN,
                             DEV_UART_0_RX_PIN,
                             DEV_UART_0_CTS_PIN,
                             DEV_UART_0_RTS_PIN);
    }
    else
    {
        u = rawuart_init_one(DEV_UART_1_INSTANCE,
                             DEV_UART_1_TX_PIN,
                             DEV_UART_1_RX_PIN,
                             DEV_UART_1_CTS_PIN,
                             DEV_UART_1_RTS_PIN);
    }
    rxring[uart].head = rxring[uart].tail = 0;
    rawuart_inited[uart] = 1;
    /* If the kernel is already up (this is the second port being opened
     * rather than the console at boot) take it under interrupt now. */
    if (rawuart_irq_started && !rawuart_irq_installed[uart])
    {
        rawuart_irq_installed[uart] = 1;
        rawuart_rx_irq_enable(u, uart);
    }
    return u;
}

/*
 *	Start interrupt driven receive.
 *
 *	Called from devtty_init(), NOT from rawuart_early_init(). MMBasic
 *	turns the receive interrupt on as soon as the port exists and can
 *	do so safely because it has no process state to build. Fuzix does:
 *	early init runs before fuzix_main() has established udata and the
 *	per-process kernel stack, and a character arriving in that window
 *	took a hard fault inside makeproc with a corrupted stack pointer
 *	(sp outside KSTACK entirely). Intermittent, because it depends on
 *	exactly when a byte lands.
 *
 *	Installing an exclusive handler twice panics the SDK, so this is
 *	guarded - devtty_init runs after the port has already been set up
 *	once for the boot messages.
 */
void rawuart_rx_irq_start(void)
{
    uint_fast8_t i;
    rawuart_irq_started = 1;
    for (i = 0; i < NUM_DEV_TTY_UART; i++)
    {
        /* Only ports that have actually been brought up. Enabling the
         * interrupt on a uart with no clocks or pins configured gives
         * a spurious source that nothing can clear. The second port is
         * initialised later, when /dev/tty2 is opened - rawuart_init
         * enables its interrupt at that point instead. */
        if (!rawuart_inited[i] || rawuart_irq_installed[i])
            continue;
        rawuart_irq_installed[i] = 1;
        rxring[i].head = rxring[i].tail = 0;
        rawuart_rx_irq_enable(rawuart_instance(i), i);
    }
}

static void rawuart_deinit(uart_inst_t * uart){
    uart_deinit(uart);
}

void rawuart_early_init(void)
{
    // init first uart for kprint
    rawuart_init(0);
}

/* devn is 1-based; map the logical uart to its hardware instance */
static inline uart_inst_t *rawuart_instance(uint_fast8_t num)
{
    return uart_get_instance(num == 0 ? DEV_UART_0_INSTANCE : DEV_UART_1_INSTANCE);
}

void rawuart_putc(uint8_t devn, uint8_t c)
{
    uint_fast8_t w = devn - 1;
    uart_inst_t *uart = rawuart_instance(w);

    /* Before the interrupt is running (boot messages) there is no ring
     * to queue into, so write straight through. */
    if (!rawuart_irq_installed[w])
    {
        while (!uart_is_writable(uart))
            tight_loop_contents();
        uart_get_hw(uart)->dr = c;
        return;
    }

    /*
     * A full ring cannot simply wait for the transmit interrupt to
     * drain it: putc is reached from tty_interrupt(), which runs with
     * PRIMASK set, so that interrupt could never run and we would hang
     * forever. Drain by polling instead, which works regardless.
     */
    while (txring_full(w))
    {
        if (uart_is_writable(uart))
            txring_pump(uart, w);
    }

    txring[w].buf[txring[w].head] = c;
    txring[w].head = (txring[w].head + 1) & (TXRING - 1);

    /* Ask for the transmit interrupt and kick it, as MMBasic does -
     * the level interrupt will not fire on its own if the FIFO is
     * already below the threshold. */
    uart_set_irq_enables(uart, true, true);
    irq_set_pending((uart == uart0) ? UART0_IRQ : UART1_IRQ);
}

void rawuart_sleeping(uint8_t devn) {}

ttyready_t rawuart_ready(uint8_t devn)
{
    uint_fast8_t w = devn - 1;

    if (!rawuart_irq_installed[w])
        return uart_is_writable(rawuart_instance(w)) ? TTY_READY_NOW
                                                     : TTY_READY_SOON;
    /* Room in the ring is what matters now, not the hardware FIFO. */
    return txring_full(w) ? TTY_READY_SOON : TTY_READY_NOW;
}

int rawuart_getc(uint8_t devn)
{
    uint_fast8_t which = devn - 1;
    uart_inst_t *uart;
    int c = rawuart_ring_get(which);

    if (c >= 0)
        return c;

    /*
     * Fall back to reading the FIFO directly.
     *
     * Safe whether or not the interrupt is running: when it is, it has
     * already moved everything into the ring and this finds nothing;
     * when it is not, this is exactly the old polled path. The console
     * therefore cannot be left mute by the interrupt failing to come
     * up, which is what kept happening.
     */
    uart = rawuart_instance(which);
    if (uart_is_readable(uart))
        return (int)uart_get_hw(uart)->dr;
    return -1;
}

/* How many characters the interrupt has taken. If this stays at zero
 * the handler is not running at all. */
unsigned rawuart_rx_irqs(uint8_t devn)
{
    return rxring[devn - 1].got;
}

/* Characters dropped because the ring filled. Zero unless the machine
 * is badly overloaded; if this is ever non-zero the tty layer is not
 * draining often enough, not the uart. */
unsigned rawuart_rx_lost(uint8_t devn)
{
    return rxring[devn - 1].lost;
}

static inline bool uart_tx_is_empty(uart_inst_t *uart)
{
    return (uart_get_hw(uart)->fr & UART_UARTFR_TXFE_BITS) != 0;
}

void rawuart_setup(uint_fast8_t minor, uint_fast8_t devn, uint_fast8_t flags)
{
    struct termios *t = &ttydata[minor].termios;
    static uint32_t last_cflag[2] = { 0xFFFFFFFF, 0xFFFFFFFF };

    /* Only the c_cflag bits (baud, size, stop, parity) reach the
     * hardware.  Re-initialising the uart resets the FIFOs and eats
     * bytes in flight, so a tcsetattr that only changes VMIN/VTIME or
     * local flags - as polling loops do continually - must be a
     * hardware no-op. */
    if (t->c_cflag == last_cflag[devn - 1])
        return;
    last_cflag[devn - 1] = t->c_cflag;

    uart_inst_t *uart = rawuart_init(devn - 1);

    /* Wait for output to finish */
    if (flags)
    {
        while (!uart_tx_is_empty(uart))
            _sched_yield();
    }
    uint baud_rate;
    uint data_bits;
    uint stop_bits = 1;
    uart_parity_t parity = UART_PARITY_NONE;

    speed_t speed = t->c_cflag & CBAUD;
    baud_rate = clocks[speed];
    if (baud_rate == 0) // Hangup if speed is B0 as per stty.1
    {
        rawuart_deinit(uart);
        last_cflag[devn - 1] = 0xFFFFFFFF; // port is down: next setup must init
        return;
    }

    if (t->c_cflag & CSTOPB)
    {
        stop_bits = 2;
    }

    data_bits = ((t->c_cflag & CSIZE) >> 4) + 5;
    if (t->c_cflag & PARENB)
    {
        parity = UART_PARITY_EVEN;
    }
    if (t->c_cflag & PARODD)
    {
        parity = UART_PARITY_ODD;
    }
    uart_set_baudrate(uart, baud_rate);
    uart_set_format(uart, data_bits, stop_bits, parity);
}
