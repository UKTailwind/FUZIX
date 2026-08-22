/*
 *	The fixed PIO output programs: WS2812 and BITSTREAM.
 *	PLAN-pioout.md (Applications/mmb2c) is the design.
 *
 *	The kernel's whole part is PLACEMENT and OWNERSHIP: five small
 *	programs loaded once into PIO1's shared instruction memory beside
 *	the I2S program, one state machine and one DMA channel reserved,
 *	and the death-sweep hooks that put both back.  Everything else -
 *	the timing precompute, the SM configuration, the DMA and the wait
 *	- is userland register work through <sys/pc3io.h>, which is the
 *	port's usual split (pinlock.c's header comment has the argument).
 *
 *	The images are hand-assembled and the origins are load-bearing:
 *	the jmp targets inside them are ABSOLUTE addresses, so the load
 *	is verified instruction by instruction and the kernel refuses to
 *	boot quietly wrong (a panic here is a build error, not a runtime
 *	condition).
 *
 *	The programs (PLAN-pioout.md section 3):
 *
 *	bitstream, driven, at 0 - one 32-bit word per edge, bit 0 the
 *	level, bits 31:1 the duration in 50ns cycles minus 3:
 *	    0: out pins, 1          6001
 *	    1: out x, 31            603F
 *	    2: jmp x--, 2           0042
 *	open-collector variant at 3 - out pindirs instead, dir 1 drives
 *	the preset low, dir 0 releases to the pull-up:
 *	    3: out pindirs, 1       6081
 *	    4: out x, 31            603F
 *	    5: jmp x--, 5           0045
 *
 *	WS2812 shifter, three timing variants (side-set 1 bit on the
 *	data pin, autopull, MSB-first).  High time for a 0-bit is T1
 *	cycles, for a 1-bit T1+T2; low tail T3 (0-bit: T2+T3):
 *	    g+0: out x, 1     side 0 [T3-1]
 *	    g+1: jmp !x, g+3  side 1 [T1-1]
 *	    g+2: jmp g        side 1 [T2-1]
 *	    g+3: nop          side 0 [T2-1]
 *	The T values are MMBasic's own (External.c:4448-4473), in its own
 *	0.05us units, with the HIGH times exact and the low tails within
 *	one 50ns cycle - the knowing, LED-invisible deviation the plan
 *	records:
 *	    O at 6:  T1=7 T2=7 T3=9   (0.35/0.70 high)
 *	    B at 10: T1=8 T2=8 T3=7   (0.40/0.80 high)
 *	    S at 14: T1=6 T2=6 T3=9   (0.30/0.60 high; W = S + 4 colours)
 */

#include <kernel.h>
#include <kdata.h>
#include <stdlib.h>
#include "config.h"
#include "picosdk.h"
#include "pico_ioctl.h"
#include "pioout.h"
#include <hardware/pio.h>
#include <hardware/dma.h>

/* out x,1 side0 [T3-1] / jmp !x,g+3 side1 [T1-1] /
   jmp g side1 [T2-1]   / nop side0 [T2-1] */
#define WS(g, T1, T2, T3) \
	0x6021 | (((T3) - 1) << 8), \
	0x0020 | ((g) + 3) | ((0x10 | ((T1) - 1)) << 8), \
	(g) | ((0x10 | ((T2) - 1)) << 8), \
	0xA042 | (((T2) - 1) << 8)

static const uint16_t pioout_img[18] = {
	/* 0-2: bitstream, driven */
	0x6001, 0x603F, 0x0042,
	/* 3-5: bitstream, open-collector */
	0x6081, 0x603F, 0x0045,
	WS(PIOOUT_ORG_WSO, 7, 7, 9),	/* 6-9   */
	WS(PIOOUT_ORG_WSB, 8, 8, 7),	/* 10-13 */
	WS(PIOOUT_ORG_WSS, 6, 6, 9),	/* 14-17 */
};

/*	The word buffer the DMA reads - PSRAM via the kernel heap
 *	(arena.c routes malloc there), which never moves, unlike process
 *	memory, which the swapper rearranges on every context switch.
 *	pico_ioctl.h tells the whole story at GPIOC_PIOOUT_BUF. */
static uint32_t *pioout_buf;

int pioout_ioctl(uarg_t request, char *data)
{
	struct pioout_buf pb;

	if (request == GPIOC_PIOOUT_BUF && pioout_buf) {
		pb.addr = (unsigned long)pioout_buf;
		pb.words = PIOOUT_BUF_WORDS;
		return uput(&pb, data, sizeof(pb));
	}
	udata.u_error = EINVAL;
	return -1;
}

void pioout_init(void)
{
	pio_program_t prog;
	int off;

	pioout_buf = malloc(PIOOUT_BUF_WORDS * 4);
	if (!pioout_buf)
		panic("pioout buf");

	memset(&prog, 0, sizeof(prog));
	prog.instructions = (const uint16_t *)pioout_img;
	prog.length = 18;
	prog.origin = 0;	/* absolute jmps: 0 or nothing */

	off = pio_add_program(pio1, &prog);
	if (off != 0)
		panic("pioout org");
	/* NO readback verify: PIO instruction memory is WRITE-ONLY, and
	   a verify loop here panicked a perfectly good load on first
	   boot.  The offset check above is the assert that means
	   something; the images themselves are proven by pioouttest
	   counting their edges on the wire. */
	/* The plan's pin-window premise: I2S pins GP10/11/22 keep
	   PIO1's GPIOBASE at 0.  If someone ever rebases it, every
	   userland pin number here goes quietly wrong - so loudly. */
	if (pio1->gpiobase != 0)
		panic("pioout gpiobase");

	/* The output SM and the DMA channel, reserved for their PLK
	   claims.  Explicit numbers, not claim-unused: userland finds
	   them as the PIOOUT_SM / PIOOUT_DMA_CH constants, so nothing
	   may depend on init order. */
	pio_sm_claim(pio1, PIOOUT_SM);
	dma_channel_claim(PIOOUT_DMA_CH);
	pio_sm_set_enabled(pio1, PIOOUT_SM, false);
}

/*	The death-sweep halves, called from pinlock.c reset_one().  Stop
 *	first, then put the machinery into the state the next claimant's
 *	setup expects: SM disabled with clean FIFOs, channel quiet.  The
 *	pin itself is PLK_PIN's reset, as for every other mode. */
void pioout_sm_reset(void)
{
	pio_sm_set_enabled(pio1, PIOOUT_SM, false);
	pio_sm_clear_fifos(pio1, PIOOUT_SM);
	pio_sm_restart(pio1, PIOOUT_SM);
}

void pioout_dma_reset(void)
{
	dma_channel_abort(PIOOUT_DMA_CH);
}
