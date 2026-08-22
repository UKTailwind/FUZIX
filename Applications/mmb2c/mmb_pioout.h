#ifndef MMB_PIOOUT_H
#define MMB_PIOOUT_H
/*
 *	WS2812 and BITSTREAM - MMBasic's two interrupts-off commands,
 *	delivered with interrupts ON: the kernel keeps five fixed PIO
 *	programs resident in PIO1 (PLAN-pioout.md; kernel pioout.c), a
 *	program claims the reserved state machine and DMA channel, packs
 *	its words into the kernel's PSRAM buffer, and the wire is driven
 *	by hardware while the machine keeps running.  Only the calling
 *	PROGRAM waits, through the serviced wait, exactly as PAUSE does.
 *
 *	THE BUFFER IS NOT OURS AND THAT IS THE POINT.  The DMA must
 *	never read process memory on this machine - the swapper moves
 *	it in 4K chunks on every context switch - so the words go into
 *	the kernel's never-moving PSRAM buffer, found once through
 *	mm_pobuf().
 *
 *	Semantics are the reference's, cited by line from PicoMite
 *	External.c: the type letters and their timing sets (4448-4473),
 *	GRB(W) packing from an RGB(+W<<24) colour integer (4496-4523),
 *	the reset gap enforced from COMMAND START, covering the packing
 *	time (4494/4528); BITSTREAM's ranges and errors (4870-4905),
 *	toggling from the pin's current OUTPUT latch (bitstream() is a
 *	gpio_xor loop), and the open-collector mode's rules.  What is
 *	knowingly different: the emission engine (PIO+DMA instead of
 *	masked bit-banging), low tails within one 50ns cycle of the
 *	reference's, and the pin family - GP0-GP7 and GP26 only, PIO1's
 *	reachable window.
 *
 *	Pins here are GPIO numbers, as everywhere in this port.
 */

#include "mmb_gpio.h"

#define MMG_WS_O 0
#define MMG_WS_B 1
#define MMG_WS_S 2
#define MMG_WS_W 3

#if !(defined(MM_PC3) || defined(__FUZIX__))
/*	Host: the transaction is modelled - claims succeed, the packing
 *	arithmetic runs into the real (model) buffer, nothing is driven,
 *	the wait returns at once.  The constants mirror pc3io.h's ABI
 *	block so the shared bodies below compile unchanged. */
#define PIOOUT_ORG_BS	0
#define PIOOUT_ORG_BSOC	3
#define PIOOUT_ORG_WSO	6
#define PIOOUT_ORG_WSB	10
#define PIOOUT_ORG_WSS	14
#define PIOOUT_PLK_IDX	5
#define PIOOUT_DMA_CH	11
#define PIOOUT_BUF_WORDS 10000
static void pc3_pioout_pin(int p) { (void)p; }
static void pc3_pioout_setup(int o, int l, int p, int w, int lv)
{ (void)o; (void)l; (void)p; (void)w; (void)lv; }
static void pc3_pioout_dir(int d) { (void)d; }
static void pc3_pioout_start(const unsigned long *w, unsigned long n)
{ (void)w; (void)n; }
static int pc3_pioout_busy(void) { return 0; }
#endif

/*	The open-collector pull-up, PAD ONLY - pc3_pin_in would re-mux
 *	the pin away from PIO1, which is exactly what must not happen
 *	while the machine drives it. */
#if defined(MM_PC3) || defined(__FUZIX__)
MMG_FN void mmg_pioout_pull(int pin)
{
	PC3_REG(PC3_PAD(pin) + PC3_SET) = PC3_PAD_PUE;
}
#else
static void mmg_pioout_pull(int pin) { (void)pin; }
#endif

/*	The claims and the buffer, once.  Ours-already succeeds, so a
 *	program looping frames pays one ioctl set on the first. */
static unsigned long *mmg_pob;

MMG_FN int mmg_pioout_claim(MMINTEGER pin)
{
	if (mmg_claim(pin, MM_PLK_PIN)
	    || mmg_claim(PIOOUT_PLK_IDX, MM_PLK_PIO)
	    || mmg_claim(PIOOUT_DMA_CH, MM_PLK_DMA))
		return -1;
	if (!mmg_pob)
		mmg_pob = (unsigned long *)(unsigned long)mm_pobuf();
	return mmg_pob ? 0 : -1;
}

/*	The blocking wait, MMBasic's observable semantics: the statement
 *	returns when the wire is done.  A frame is milliseconds, so it
 *	spins; a long BITSTREAM sleeps in PAUSE-sized pieces so the rest
 *	of the machine gets the processor - the serviced wait keeps the
 *	pumps alive, as PAUSE's does. */
MMG_FN void mmg_pioout_wait(void)
{
	MMINTEGER t0 = mm_us();

	while (pc3_pioout_busy())
		if (mm_us() - t0 > 20000)
			mm_pause(5);
}

/*
 *	WS2812 type, pin, nbr, colours%()   (scalar colour when nbr = 1)
 *
 *	Timing sets O / B / S, W = S with four colour bytes; high times
 *	are the reference's exactly.  Colour integers are MMBasic's:
 *	red<<16 | green<<8 | blue, white<<24 for the W type; the wire
 *	order is green, red, blue(, white), MSB first (External.c:
 *	4496-4523).  The reset gap is enforced from command start, which
 *	covers the packing time - the reference's own scheme.
 */
MMG_FN void mmg_ws2812(MMINTEGER type, MMINTEGER pin, MMINTEGER nbr,
		       MMINTEGER *c, MMINTEGER cnt)
{
	static const unsigned char worg[4] = {
		PIOOUT_ORG_WSO, PIOOUT_ORG_WSB, PIOOUT_ORG_WSS,
		PIOOUT_ORG_WSS
	};
	static const unsigned short wrst[4] = { 50, 280, 80, 80 };
	MMINTEGER endreset;
	unsigned long w;
	int i, bits;

	endreset = mm_us() + wrst[type & 3];
	if (pin < 0 || pin >= MM_GPIO_NPINS)
		MM_RAISE("Invalid pin");
	if (nbr < 1 || nbr > 256)
		MM_RAISE("Invalid LED count");
	if (cnt < nbr)
		MM_RAISE("Array too small");
	if (mmg_mode[pin] != MMG_PIN_OFF && mmg_mode[pin] != MMG_PIN_DOUT)
		MM_RAISE("Pin is in use");
	if (mmg_pioout_claim(pin))
		MM_RAISE("Pin cannot do that");
	bits = (type == MMG_WS_W) ? 32 : 24;
	for (i = 0; i < (int)nbr; i++) {
		w = ((unsigned long)((c[i] >> 8) & 0xFF) << 24)
		    | ((unsigned long)((c[i] >> 16) & 0xFF) << 16)
		    | ((unsigned long)(c[i] & 0xFF) << 8);
		if (bits == 32)
			w |= (unsigned long)((c[i] >> 24) & 0xFF);
		mmg_pob[i] = w;
	}
	pc3_pioout_pin((int)pin);
	pc3_pioout_setup(worg[type & 3], 4, (int)pin, bits, 0);
	while (mm_us() < endreset)
		;
	pc3_pioout_start(mmg_pob, (unsigned long)nbr);
	mmg_pioout_wait();
	/*	The pin stays a driven-low digital output, which is what
	 *	the reference leaves behind - and handing it back to SIO
	 *	means PIN(n) works afterwards. */
	pc3_pin_put((int)pin, 0);
	pc3_pin_out((int)pin);
	mmg_mode[pin] = MMG_PIN_DOUT;
}

MMG_FN void mmg_ws2812_one(MMINTEGER type, MMINTEGER pin, MMINTEGER nbr,
			   MMINTEGER colour)
{
	if (nbr != 1)
		MM_RAISE("Array expected");
	mmg_ws2812(type, pin, 1, &colour, 1);
}

/*
 *	BITSTREAM pin, n, array() [, mode] - n timed transitions, each
 *	array element the microseconds to hold after it (0..67108,
 *	"Number range", 4911).  mode 0 drives the pin, toggling from its
 *	current output latch; mode 1 is open-collector - the pin starts
 *	RELEASED under a pull-up, each transition toggles drive-low
 *	versus released, and an odd count is refused with the
 *	reference's own words (4888-4891).
 */
MMG_FN void mmg_bs_core(MMINTEGER pin, MMINTEGER n, MMFLOAT *f,
			MMINTEGER *ii, MMINTEGER cnt, MMINTEGER mode)
{
	unsigned long cyc, w;
	MMINTEGER us;
	int i, lvl0, last;

	if (pin < 0 || pin >= MM_GPIO_NPINS)
		MM_RAISE("Invalid pin");
	if (n < 1 || n > PIOOUT_BUF_WORDS)
		MM_RAISE("Invalid count");
	if (cnt < n)
		MM_RAISE("Array too small");
	if (mode && (n & 1))
		MM_RAISE("Open-collector mode requires even number of "
			 "transitions");
	if (mmg_mode[pin] != MMG_PIN_OFF && mmg_mode[pin] != MMG_PIN_DOUT)
		MM_RAISE("Pin is in use");
	if (mmg_pioout_claim(pin))
		MM_RAISE("Pin cannot do that");

	/*	mode 0 toggles the LEVEL from the pin's current output
	 *	latch, which is what the reference's gpio_xor loop does;
	 *	mode 1 toggles the DIRECTION from released. */
	lvl0 = mode ? 0 : (int)((pc3_pins_out() >> pin) & 1);
	for (i = 0; i < (int)n; i++) {
		us = f ? (MMINTEGER)(f[i] + 0.5) : ii[i];
		if (us < 0 || us > 67108)
			MM_RAISE("Number range");
		cyc = (unsigned long)us * 20;
		if (cyc < 3)
			cyc = 3;
		w = ((cyc - 3) << 1)
		    | (unsigned long)((lvl0 ^ ((i + 1) & 1)) & 1);
		mmg_pob[i] = w;
	}

	pc3_pioout_pin((int)pin);
	if (mode) {
		pc3_pioout_setup(PIOOUT_ORG_BSOC, 3, (int)pin, 0, 0);
		mmg_pioout_pull((int)pin);
		pc3_pioout_dir(0);	/* start released */
	} else {
		pc3_pioout_setup(PIOOUT_ORG_BS, 3, (int)pin, 0, lvl0);
	}
	pc3_pioout_start(mmg_pob, (unsigned long)n);
	mmg_pioout_wait();

	last = lvl0 ^ ((int)n & 1);
	if (mode) {
		/*	Released under its pull-up, as the reference
		 *	leaves it. */
		pc3_pin_in((int)pin, 1);
	} else {
		pc3_pin_put((int)pin, last);
		pc3_pin_out((int)pin);
	}
	mmg_mode[pin] = MMG_PIN_DOUT;
}

MMG_FN void mmg_bitstream_i(MMINTEGER pin, MMINTEGER n, MMINTEGER *a,
			    MMINTEGER cnt, MMINTEGER mode)
{
	mmg_bs_core(pin, n, (MMFLOAT *)0, a, cnt, mode);
}

MMG_FN void mmg_bitstream_f(MMINTEGER pin, MMINTEGER n, MMFLOAT *a,
			    MMINTEGER cnt, MMINTEGER mode)
{
	mmg_bs_core(pin, n, a, (MMINTEGER *)0, cnt, mode);
}

#endif /* MMB_PIOOUT_H */
