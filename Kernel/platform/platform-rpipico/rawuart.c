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

	/*
	 * Transmit, exactly as MMBasic does it: ONE byte per interrupt, and
	 * when the ring runs dry turn the transmit interrupt off so it
	 * stops asking.  This is the whole of it - see rawuart_putc for the
	 * producer, which is the half that has to be right.
	 */
	if (uart_is_writable(uart)) {
		if (!txring_empty(which))
			txring_pump(uart, which);
		else
			uart_set_irq_enables(uart, true, false);
	}
}

/*
 *	Installed the SDK's way, with irq_set_exclusive_handler().
 *
 *	These used to be strong definitions of isr_irq33/isr_irq34 hooked
 *	straight into the vector table, on the belief that the SDK call
 *	does nothing under PICO_COPY_TO_RAM.  That belief was wrong and is
 *	recorded as wrong in PC3-IRQ-REVIEW.md: measured on the running
 *	board, VTOR points at ram_vector_table and runtime installation
 *	works.  Worse, a strong handler in a slot the SDK also manages is
 *	what makes irq_add_shared_handler chain over a handler it did not
 *	install, and the SDK's answer to that is panic().  Two mechanisms
 *	for one vector is a bug waiting to happen; display.c and sound.c
 *	already do it this way.
 */
#define LOGICAL_OF_HW0	(DEV_UART_0_INSTANCE == 0 ? 0 : 1)
#define LOGICAL_OF_HW1	(DEV_UART_0_INSTANCE == 1 ? 0 : 1)

static void rawuart_irq_hw0(void) { rawuart_rx_irq(LOGICAL_OF_HW0); }
static void rawuart_irq_hw1(void) { rawuart_rx_irq(LOGICAL_OF_HW1); }

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

	irq_set_exclusive_handler(irqn, (uart == uart0) ? rawuart_irq_hw0
							: rawuart_irq_hw1);
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

/*
 *	Called from the timer tick.  This is the safety net, and the reason
 *	it has to exist is worth stating plainly.
 *
 *	The PL011 transmit interrupt is a watermark CROSSING, not a level,
 *	and the SDK sets the threshold to 1/8 - four bytes of a 32 byte
 *	FIFO.  A FIFO that never rises above four therefore never crosses
 *	the watermark on the way down and never interrupts again.  In
 *	steady state that is exactly what happens, so console output is not
 *	really interrupt driven at all: it is driven by the manual kick in
 *	putc, and the hardware edge almost never fires.
 *
 *	That makes a lost kick fatal rather than merely late, and kicks are
 *	easy to lose.  putc is reached from tty_interrupt(), which runs
 *	inside timer_tick_cb with PRIMASK set (devices.c does di() on
 *	entry), and the NVIC pending bit is ONE bit: a whole tick's worth
 *	of characters coalesces into a single interrupt.  Whatever that one
 *	interrupt does not send is then stranded with no pending bit and no
 *	edge coming.  The console stops for ever, in both directions,
 *	because receive shares the handler - the machine is alive and looks
 *	dead.
 *
 *	Rather than try to prove that can never happen, make it
 *	recoverable: if the ring has anything in it, ask again.  One
 *	compare per tick, and a permanent hang becomes a 5ms hiccup.
 */
static void hex8(char *p, uint32_t v)
{
	static const char d[] = "0123456789ABCDEF";
	int i;

	for (i = 7; i >= 0; i--) {
		p[i] = d[v & 15];
		v >>= 4;
	}
}

/* Counters the report reads.  ticks proves whether the timer interrupt
 * is still running, which is the first thing to know when the machine
 * has gone quiet: everything else is downstream of that. */
static volatile unsigned rawuart_ticks;
static unsigned rawuart_notready[2];
static uint8_t rawuart_reported[2];

/*
 * Dump the uart's own account of itself TO THE SCREEN.  console_putc
 * mirrors every byte to the uart as well as the display, so kprintf
 * dies along with the uart and is no use here; core1 paints the screen
 * from its own framebuffer and does not care.
 */
static void rawuart_report(unsigned w, const char *why)
{
	extern void console_screen_puts(const char *s);
	/* Must fit in 80 columns or the interesting end wraps away. */
	char msg[] = "\r\n[u0 tk=........ en=. pd=. ac=. pm=. bp=.. "
		     "ri=.... h=... t=...]\r\n";
	uart_hw_t *hw = uart_get_hw(rawuart_instance(w));
	unsigned irqn = (rawuart_instance(w) == uart0) ? UART0_IRQ : UART1_IRQ;
	volatile uint32_t *iser = (volatile uint32_t *)0xE000E100;
	volatile uint32_t *ispr = (volatile uint32_t *)0xE000E200;
	volatile uint32_t *iabr = (volatile uint32_t *)0xE000E300;
	uint32_t primask, basepri;
	static const char d[] = "0123456789ABCDEF";

	(void)why;
	if (rawuart_reported[w])
		return;
	rawuart_reported[w] = 1;

	__asm__ volatile ("mrs %0, primask" : "=r" (primask));
	__asm__ volatile ("mrs %0, basepri" : "=r" (basepri));

	msg[4] = '0' + w;
	hex8(&msg[9], rawuart_ticks);
	/* en: enabled in the NVIC.  pd: pending.  ac: ACTIVE - the handler
	 * was entered and never completed, which is the one state that
	 * stops delivery for ever while the hardware keeps asking. */
	msg[21] = '0' + ((iser[irqn >> 5] >> (irqn & 31)) & 1);
	msg[26] = '0' + ((ispr[irqn >> 5] >> (irqn & 31)) & 1);
	msg[31] = '0' + ((iabr[irqn >> 5] >> (irqn & 31)) & 1);
	msg[36] = '0' + (primask & 1);
	msg[41] = d[(basepri >> 4) & 15];
	msg[42] = d[basepri & 15];
	msg[47] = d[(hw->ris >> 12) & 15];
	msg[48] = d[(hw->ris >> 8) & 15];
	msg[49] = d[(hw->ris >> 4) & 15];
	msg[50] = d[hw->ris & 15];
	msg[54] = d[(txring[w].head >> 8) & 15];
	msg[55] = d[(txring[w].head >> 4) & 15];
	msg[56] = d[txring[w].head & 15];
	msg[60] = d[(txring[w].tail >> 8) & 15];
	msg[61] = d[(txring[w].tail >> 4) & 15];
	msg[62] = d[txring[w].tail & 15];
	console_screen_puts(msg);
}

/*
 *	The stall watchdog.
 *
 *	If the ring has not moved for a second while holding characters,
 *	the console is wedged.  Say so ON THE SCREEN - console_putc mirrors
 *	every byte to the uart as well as the display, so kprintf dies with
 *	the uart and is no use here, but core1 keeps painting the screen
 *	out of its own framebuffer regardless.
 *
 *	This prints once and then keeps the port alive by polling: the
 *	point is to get the machine's own account of what the uart was
 *	doing, not to leave it dead.
 */
void rawuart_tx_poll(void)
{
	unsigned w;

	rawuart_ticks++;

	for (w = 0; w < 2; w++) {
		uart_inst_t *uart;

		if (!rawuart_irq_installed[w] || txring_empty(w))
			continue;
		uart = rawuart_instance(w);
		uart_set_irq_enables(uart, true, true);
		irq_set_pending((uart == uart0) ? UART0_IRQ : UART1_IRQ);
	}
}

/* True when interrupts are enabled here, so the transmit interrupt can
 * be relied on to drain the ring.  It cannot when putc is reached from
 * tty_interrupt(), because timer_tick_cb holds di() across the tick. */
static inline int irq_enabled(void)
{
    uint32_t primask;

    __asm__ volatile ("mrs %0, primask" : "=r" (primask));
    return (primask & 1u) == 0;
}

void rawuart_putc(uint8_t devn, uint8_t c)
{
    uint_fast8_t w = devn - 1;
    uart_inst_t *uart = rawuart_instance(w);
    int empty;

    /* Before the interrupt is running (boot messages) there is no ring
     * to queue into, so write straight through. */
    if (!rawuart_irq_installed[w])
    {
        while (!uart_is_writable(uart))
            tight_loop_contents();
        uart_get_hw(uart)->dr = c;
        return;
    }

    /* Sampled BEFORE queueing - see the note below. */
    empty = uart_is_writable(uart);

    /*
     * MMBasic's SerialConsolePutC, followed exactly (PicoMite.c ~1210):
     *
     *     int empty = uart_is_writable(uart);
     *     while (buffer full) ;
     *     buf[head] = c; head = (head + 1) % size;
     *     if (empty) { while (irqs) {} ; enable tx irq; set pending; }
     *
     * Three things in that are load bearing, and we had all three
     * wrong:
     *
     *  1. `empty` is sampled BEFORE queueing.  It records whether the
     *     transmit engine might be idle, which is the only case where a
     *     kick is needed at all.  If the FIFO was full, a watermark
     *     crossing is already guaranteed.
     *  2. the wait for a full ring does NOT touch the ring.  The
     *     interrupt is the only consumer.  Ours pumped from here as
     *     well, and two consumers advancing `tail` - one of them
     *     interruptible between reading buf[tail] and storing the new
     *     tail - is how a ring gets left in a state where it is neither
     *     empty nor has room, which wedges output permanently.
     *  3. `while (irqs) {}` waits until interrupts are ON before
     *     enabling and kicking, so the kick cannot be swallowed.
     *
     * We cannot do (3) literally: tty_interrupt() echoes from inside
     * timer_tick_cb, which holds di() across the whole tick, so waiting
     * there would deadlock.  In that one case the handler's own body is
     * run inline - which keeps the single-consumer invariant, because
     * interrupts are already masked and nothing else can be in it.
     */
    while (txring_full(w))
    {
        if (irq_enabled())
            continue;           /* the interrupt will drain it: wait */
        rawuart_rx_irq(w);      /* masked: be the interrupt ourselves */
    }

    /*
     * The queue itself still has to be atomic against the tick, which
     * preempts ordinary context and echoes.  Two calls interleaving
     * between the store to buf[head] and the update of head corrupt the
     * ring, and if that leaves head equal to tail with the transmit
     * interrupt already off, output stops for good.
     */
    {
        irqflags_t irq = di();
        txring[w].buf[txring[w].head] = c;
        txring[w].head = (txring[w].head + 1) & (TXRING - 1);
        irqrestore(irq);
    }

    if (empty)
    {
        uart_set_irq_enables(uart, true, true);
        irq_set_pending((uart == uart0) ? UART0_IRQ : UART1_IRQ);
    }
}

void rawuart_sleeping(uint8_t devn) {}

ttyready_t rawuart_ready(uint8_t devn)
{
    uint_fast8_t w = devn - 1;

    if (!rawuart_irq_installed[w])
        return uart_is_writable(rawuart_instance(w)) ? TTY_READY_NOW
                                                     : TTY_READY_SOON;
    /* Room in the ring is what matters now, not the hardware FIFO. */
    if (!txring_full(w)) {
        rawuart_notready[w] = 0;
        return TTY_READY_NOW;
    }
    /*
     * The ring is full, which is the NORMAL steady state when a process
     * outruns 115200 - the tty layer spins here calling us until the
     * interrupt frees a slot.  So this is the exact spot the machine
     * sits in when output wedges, and the one place an instrument can
     * run with interrupts still enabled.  Report once, to the screen,
     * because the uart is the thing that cannot be trusted to report on
     * itself - and count ticks so the dump says whether the timer is
     * still alive.
     */
    if (++rawuart_notready[w] == 2000000u)
        rawuart_report(w, "tty wait");
    return TTY_READY_SOON;
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
