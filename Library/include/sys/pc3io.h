#ifndef _SYS_PC3IO_H
#define _SYS_PC3IO_H
/*
 *	Direct I/O for the Pico Computer 3's header pins.
 *
 *	A program drives the pins ITSELF, with loads and stores.  There is
 *	no MMU on this board and the kernel never drops privilege, so the
 *	registers were always reachable; what changed is that there is now
 *	somewhere to say who owns them.  The kernel keeps the arbitration
 *	and the cleanup (pinlock.c) and nothing else:
 *
 *		pc3_claim(PLK_PIN, 4);		one syscall, once
 *		pc3_pin_out(4);
 *		pc3_pin_high(4);		one store, ~10ns
 *
 *	An ioctl costs 1.488us on this machine, so the old one-per-edge
 *	interface was not a tax on pin work, it WAS the pin work.  Bit
 *	banging is possible at ten nanoseconds an edge and is not possible
 *	at one and a half microseconds.
 *
 *	THE RULE, and it is not optional: never read-modify-write SIO's
 *	GPIO_OUT or GPIO_OE.  They are single registers shared with the
 *	kernel and with core1's display, so a read-modify-write here
 *	clobbers whatever they changed in between.  Use the SET/CLR/XOR
 *	registers, which is what everything below does - that is the whole
 *	reason these are functions rather than a comment telling you to be
 *	careful.  Same for the APB blocks, which have SET/CLR/XOR aliases
 *	at +0x2000/+0x3000/+0x1000.
 *
 *	Pins 32-47 are a SECOND BANK with its own registers, and on the
 *	RP2350 the two banks INTERLEAVE (GPIO_OE is 0x030 here, not the
 *	RP2040's 0x020).  Everything below takes a plain GPIO number and
 *	sorts that out; the PC3's header is mostly GP34-GP46, so a version
 *	of this that quietly ignored the high bank would appear to work
 *	and drive nothing.
 *
 *	Kept plain C89 with no ioctl in the inner path so the on-board cc
 *	can compile it too - the same discipline as mmb2c's mmb_gpio.h.
 */

/*	PC3IO_NO_SYSCALLS: the registers WITHOUT the claim wrappers, for a
 *	caller that has no open() or ioctl() to make them with.  That is
 *	the on-board cc, whose generated code reaches the kernel only
 *	through bcrun's natives - mmb2c's mmb_gpio.h defines this and
 *	does its claiming through the runtime instead.  The registers are
 *	shared either way, because two copies of the ISO trap below is
 *	how two copies come to disagree. */
#ifndef PC3IO_NO_SYSCALLS
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#endif

/*	gcc must not warn about the ones a program does not use; the
 *	on-board cc has no attributes and discards unused statics anyway. */
#ifdef __GNUC__
#define PC3_FN static __inline__ __attribute__((unused))
#else
#define PC3_FN static
#endif

/* ---- the ownership ioctls.  The authority for these is the kernel's
 *	pico_ioctl.h; this is userland's copy of the same ABI, as
 *	i2ctest.c keeps its own copy of struct i2c_msg.  Keep them in
 *	step. ---- */

#ifndef PC3_PINLOCK_ABI
#define PC3_PINLOCK_ABI

struct pinlock_req {
	unsigned char cls;
	unsigned char idx;
	unsigned char flags;
	unsigned char pad;
};

#define PLK_PIN		0
#define PLK_I2C		1
#define PLK_SPI		2
#define PLK_PWM		3
#define PLK_ADC		4
#define PLK_PIO		5
#define PLK_DMA		6

#define PLKIOC_CLAIM	0x0026
#define PLKIOC_RELEASE	0x0027
#define PLKIOC_OWNER	0x0028

#endif	/* PC3_PINLOCK_ABI */

/* ---- registers (RP2350: addressmap.h, sio.h, pads_bank0.h, adc.h) ---- */

#define PC3_REG(a)	(*(volatile unsigned long *)(a))

#define PC3_SIO		0xd0000000UL
#define PC3_SIO_IN	(PC3_SIO + 0x004)
#define PC3_SIO_HI_IN	(PC3_SIO + 0x008)
#define PC3_SIO_OUT_SET	(PC3_SIO + 0x018)
#define PC3_SIO_HI_OUT_SET (PC3_SIO + 0x01c)
#define PC3_SIO_OUT_CLR	(PC3_SIO + 0x020)
#define PC3_SIO_HI_OUT_CLR (PC3_SIO + 0x024)
#define PC3_SIO_OUT_XOR	(PC3_SIO + 0x028)
#define PC3_SIO_HI_OUT_XOR (PC3_SIO + 0x02c)
#define PC3_SIO_OE	(PC3_SIO + 0x030)
#define PC3_SIO_OE_SET	(PC3_SIO + 0x038)
#define PC3_SIO_HI_OE_SET (PC3_SIO + 0x03c)
#define PC3_SIO_OE_CLR	(PC3_SIO + 0x040)
#define PC3_SIO_HI_OE_CLR (PC3_SIO + 0x044)

/* IO_BANK0: two registers per pin, CTRL is the second.  FUNCSEL 5 = SIO. */
#define PC3_IOBANK0	0x40028000UL
#define PC3_GPIO_CTRL(n) (PC3_IOBANK0 + 8UL * (n) + 4)
#define PC3_FUNC_SIO	5
#define PC3_FUNC_NULL	31

/* PADS_BANK0: one per pin from +4.  OD is output DISABLE, IE input enable. */
#define PC3_PADS	0x40038000UL
#define PC3_PAD(n)	(PC3_PADS + 4UL + 4UL * (n))
#define PC3_PAD_SCHMITT	0x02UL
#define PC3_PAD_PDE	0x04UL
#define PC3_PAD_PUE	0x08UL
#define PC3_PAD_IE	0x40UL
#define PC3_PAD_OD	0x80UL
/*	ISOLATION, and it RESETS TO 1.  This is new on the RP2350 and it
 *	is the single easiest way to write pin code that does nothing at
 *	all: until this bit is cleared the pad is disconnected from the
 *	chip - it will not drive, will not read, and a pull-up does
 *	nothing.  Every setup function below clears it LAST, which is the
 *	order the datasheet asks for: configure the pad while it is
 *	isolated, then connect it. */
#define PC3_PAD_ISO	0x100UL

/* Atomic aliases, for the APB blocks only - SIO has its own SET/CLR
   registers instead and does NOT respond to these. */
#define PC3_XOR		0x1000UL
#define PC3_SET		0x2000UL
#define PC3_CLR		0x3000UL

#define PC3_ADC		0x400a0000UL
#define PC3_ADC_CS	(PC3_ADC + 0x00)
#define PC3_ADC_RESULT	(PC3_ADC + 0x04)
#define PC3_ADC_CS_EN	0x00000001UL
#define PC3_ADC_CS_START_ONCE 0x00000004UL
#define PC3_ADC_CS_READY 0x00000100UL
#define PC3_ADC_CS_AINSEL_LSB 12

#define PC3_RESETS	0x40020000UL
#define PC3_RESETS_DONE	(PC3_RESETS + 0x08)
#define PC3_RESET_ADC	0x00000001UL

/* ---- claiming ---- */

#ifndef PC3IO_NO_SYSCALLS

/*	/dev/sys, opened once and kept.  A program that never claims
 *	anything never opens it. */
PC3_FN int pc3_sysfd(void)
{
	static int fd = -1;

	if (fd < 0)
		fd = open("/dev/sys", O_RDWR);
	return fd;
}

PC3_FN int pc3_lockop(int op, int cls, int idx)
{
	struct pinlock_req rq;
	int fd = pc3_sysfd();

	if (fd < 0)
		return -1;
	rq.cls = (unsigned char)cls;
	rq.idx = (unsigned char)idx;
	rq.flags = 0;
	rq.pad = 0;
	return ioctl(fd, op, &rq);
}

/*	0, or -1 with errno EBUSY (someone else has it), EINVAL (not a
 *	claimable resource on this board) or ENOMEM (table full).
 *	Claiming what you already hold succeeds. */
PC3_FN int pc3_claim(int cls, int idx)
{
	return pc3_lockop(PLKIOC_CLAIM, cls, idx);
}

/*	Optional: exiting releases everything, and resets it.  Call this
 *	only to give something up while still running. */
PC3_FN int pc3_release(int cls, int idx)
{
	return pc3_lockop(PLKIOC_RELEASE, cls, idx);
}

/*	The pid holding it, or 0 if free. */
PC3_FN int pc3_owner(int cls, int idx)
{
	return pc3_lockop(PLKIOC_OWNER, cls, idx);
}

#endif	/* PC3IO_NO_SYSCALLS */

/* ---- pins ---- */

/*	Point the pin at SIO so the registers below control it, connect
 *	the pad, and enable its input buffer.
 *
 *	This is the SDK's gpio_set_function, register for register, and
 *	all three parts matter.  IE is the one that looks optional and is
 *	not: the input buffer feeds GPIO_IN, so without it a pin reads a
 *	steady 0 whatever is on the wire - including a pin this program
 *	is itself driving high, which is how it was found.  OD is cleared
 *	for the mirror-image reason; a pin left output-disabled by a
 *	previous owner drives nothing and looks like a dead output.
 *
 *	Called LAST by pc3_pin_out and pc3_pin_in - see PC3_PAD_ISO - and
 *	separately by a pin coming back from a peripheral. */
PC3_FN void pc3_pin_sio(int pin)
{
	PC3_REG(PC3_GPIO_CTRL(pin)) = PC3_FUNC_SIO;
	PC3_REG(PC3_PAD(pin) + PC3_SET) = PC3_PAD_IE;
	PC3_REG(PC3_PAD(pin) + PC3_CLR) = PC3_PAD_OD | PC3_PAD_ISO;
}

PC3_FN void pc3_pin_high(int pin)
{
	if (pin < 32)
		PC3_REG(PC3_SIO_OUT_SET) = 1UL << pin;
	else
		PC3_REG(PC3_SIO_HI_OUT_SET) = 1UL << (pin - 32);
}

PC3_FN void pc3_pin_low(int pin)
{
	if (pin < 32)
		PC3_REG(PC3_SIO_OUT_CLR) = 1UL << pin;
	else
		PC3_REG(PC3_SIO_HI_OUT_CLR) = 1UL << (pin - 32);
}

PC3_FN void pc3_pin_put(int pin, int v)
{
	if (v)
		pc3_pin_high(pin);
	else
		pc3_pin_low(pin);
}

PC3_FN void pc3_pin_toggle(int pin)
{
	if (pin < 32)
		PC3_REG(PC3_SIO_OUT_XOR) = 1UL << pin;
	else
		PC3_REG(PC3_SIO_HI_OUT_XOR) = 1UL << (pin - 32);
}

PC3_FN int pc3_pin_get(int pin)
{
	if (pin < 32)
		return (int)((PC3_REG(PC3_SIO_IN) >> pin) & 1);
	return (int)((PC3_REG(PC3_SIO_HI_IN) >> (pin - 32)) & 1);
}

/*	An output.  The pad's output-disable has to be cleared as well as
 *	SIO's OE - a pin left OD from a previous owner drives nothing and
 *	looks exactly like a dead output. */
PC3_FN void pc3_pin_out(int pin)
{
	PC3_REG(PC3_PAD(pin) + PC3_CLR) = PC3_PAD_OD;
	if (pin < 32)
		PC3_REG(PC3_SIO_OE_SET) = 1UL << pin;
	else
		PC3_REG(PC3_SIO_HI_OE_SET) = 1UL << (pin - 32);
	pc3_pin_sio(pin);		/* last: connects the pad */
}

/*	An input.  pull: 1 up, -1 down, 0 floating.
 *
 *	The input BUFFER is a separate pad bit and is off after a reset -
 *	without setting it the pin reads a steady 0 whatever is on the
 *	wire, which is the single most misleading failure on this part
 *	(it cost a GP33-to-GP35 loopback session in devgpio.c). */
PC3_FN void pc3_pin_in(int pin, int pull)
{
	if (pin < 32)
		PC3_REG(PC3_SIO_OE_CLR) = 1UL << pin;
	else
		PC3_REG(PC3_SIO_HI_OE_CLR) = 1UL << (pin - 32);
	PC3_REG(PC3_PAD(pin) + PC3_CLR) = PC3_PAD_OD | PC3_PAD_PUE | PC3_PAD_PDE;
	/*	Input buffer on, and HYSTERESIS with it.  MMBasic turns the
	 *	Schmitt trigger on for every digital input it configures
	 *	(External.c:841 and each of the interrupt modes), and this
	 *	is a board with header pins and flying leads on them, where
	 *	a slow or noisy edge is the normal case rather than the
	 *	exceptional one.  The pad powers up with it set, but a
	 *	previous owner may have cleared it, so say so explicitly
	 *	rather than inheriting whatever was left. */
	PC3_REG(PC3_PAD(pin) + PC3_SET) = PC3_PAD_IE | PC3_PAD_SCHMITT;
	if (pull > 0)
		PC3_REG(PC3_PAD(pin) + PC3_SET) = PC3_PAD_PUE;
	else if (pull < 0)
		PC3_REG(PC3_PAD(pin) + PC3_SET) = PC3_PAD_PDE;
	pc3_pin_sio(pin);		/* last: connects the pad */
}

/* ---- ADC ----
 *
 *	Eight channels on GP40-GP47; channel n is GP40+n.  One converter
 *	shared between them, so claim PLK_ADC as well as the pin.
 */

#define PC3_ADC_GPIO(chan)	(40 + (chan))

/*	Bring the converter out of reset and enable it.  Idempotent, and
 *	cheap enough to call before every read if that is simpler.
 *
 *	RESETS is written through its CLR alias, one bit, so this cannot
 *	disturb any other block coming out of reset. */
PC3_FN void pc3_adc_enable(void)
{
	PC3_REG(PC3_RESETS + PC3_CLR) = PC3_RESET_ADC;
	while (!(PC3_REG(PC3_RESETS_DONE) & PC3_RESET_ADC))
		;
	PC3_REG(PC3_ADC_CS) = PC3_ADC_CS_EN;
	while (!(PC3_REG(PC3_ADC_CS) & PC3_ADC_CS_READY))
		;
}

/*	An analogue input.  This is adc_gpio_init() from the SDK, register
 *	for register: function NULL so the digital driver goes hi-Z, no
 *	pulls, and the digital input buffer off.
 *
 *	Note what is NOT here: the pad's OD bit is left alone.  Setting it
 *	looks like the tidy thing to do and measurably shifts the reading
 *	- an early version of this read about 1500 counts (of 65535) high
 *	on all four channels against the kernel's path. */
PC3_FN void pc3_adc_pin(int chan)
{
	int pin = PC3_ADC_GPIO(chan);

	PC3_REG(PC3_GPIO_CTRL(pin)) = PC3_FUNC_NULL;
	PC3_REG(PC3_PAD(pin) + PC3_CLR) =
		PC3_PAD_IE | PC3_PAD_PUE | PC3_PAD_PDE | PC3_PAD_ISO;
}

/*	One conversion, 12 bits.
 *
 *	TWO conversions, in fact: changing AINSEL switches an analogue mux
 *	and the first result after it moves is taken before the input has
 *	settled.  The kernel's ADVAL has always thrown that one away and
 *	so does this - it is not a refinement, it is the difference
 *	between agreeing with the old path and not. */
/*	Point the converter at a channel.  Kept SEPARATE from starting a
 *	conversion because that is the shape of the SDK's
 *	adc_select_input/adc_read, and this whole path exists to agree
 *	with what the kernel used to do through them.
 *
 *	Measured, so that the next person does not re-derive it: against
 *	the kernel's readings on the same board, a potentiometer on GP41
 *	agrees to 24 counts of 65535 - one and a half counts of the
 *	twelve-bit converter.  Floating header pins differ by about 500,
 *	and that is a property of reading a high-impedance input twice,
 *	not of the two code paths: separating these two writes was tried
 *	as a fix for it and changed nothing.  Judge this path on GP41. */
PC3_FN void pc3_adc_select(int chan)
{
	PC3_REG(PC3_ADC_CS) = PC3_ADC_CS_EN |
		((unsigned long)chan << PC3_ADC_CS_AINSEL_LSB);
}

/*	One conversion on whatever channel is selected.  START_ONCE goes
 *	through the SET alias so the rest of CS - the channel above all -
 *	is untouched. */
PC3_FN int pc3_adc_conv(void)
{
	PC3_REG(PC3_ADC_CS + PC3_SET) = PC3_ADC_CS_START_ONCE;
	while (!(PC3_REG(PC3_ADC_CS) & PC3_ADC_CS_READY))
		;
	return (int)(PC3_REG(PC3_ADC_RESULT) & 0xFFF);
}

PC3_FN int pc3_adc_read(int chan)
{
	pc3_adc_select(chan);
	pc3_adc_conv();			/* discard: mux settle */
	return pc3_adc_conv();
}

#endif
