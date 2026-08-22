/*
 *	Ownership of the PC3 I/O header - PC3-IO-PLAN.md.
 *
 *	This is the kernel half of moving pin work OUT of the kernel.  A
 *	program that has claimed GP4 drives GP4 itself, by storing to SIO;
 *	there is no MMU on this board, so it always could, and an ioctl per
 *	edge costs 1.488us against about ten nanoseconds for the store it
 *	wraps.  Bit-banged I2C and SPI are simply not possible at the first
 *	figure and trivial at the second.
 *
 *	So what is left here is the part userland cannot do for itself:
 *
 *	  - WHO OWNS WHAT, so two programs do not drive one pin.  The
 *	    /dev/gpio driver's own comment already names this gap: "two
 *	    driving ONE pin get last-writer-wins - nonsense, but not
 *	    damage".  This is what closes it.
 *	  - PUTTING IT BACK.  A program killed while driving a relay cannot
 *	    release it.  pinlock_release() runs from the exit path and from
 *	    exec, and RESETS the resource - a freed table entry on its own
 *	    would change nothing physical, which is the whole point of
 *	    having the kernel hold the register in the first place.
 *
 *	ADVISORY, and the header says so.  No MMU, no MPU: a wild pointer
 *	can still write IO_BANK0 and nothing on this board can stop it.
 *	What this stops is cooperating programs colliding, which is the
 *	failure that actually happens here.
 *
 *	No locking of its own, for the reason /dev/i2c gives: the kernel is
 *	non-preemptive, so an ioctl that does not sleep is atomic against
 *	every other process, and nothing below sleeps.  The table is only
 *	ever touched from a syscall or from the exit path.
 *
 *	The ownership shape is arena.c's and display.c's, deliberately - a
 *	struct p_tab * and a sweep on death.  Three owned resources with
 *	three different idioms would be three things to get wrong.
 */

#include <kernel.h>
#include <kdata.h>
#include "picosdk.h"
#include "config.h"
#include "pico_ioctl.h"
#include "pinlock.h"
#include "countpin.h"
#include "pioout.h"
#include <hardware/gpio.h>
#include <hardware/i2c.h>
#include <hardware/spi.h>
#include <hardware/pwm.h>

#ifdef CONFIG_PC3_PINLOCK

#ifdef CONFIG_DEV_I2C
extern void plt_i2c_close(uint8_t bus);
#endif
#ifdef CONFIG_PC3_SPI0
extern void plt_spi_close(uint8_t bus);
#endif

/*
 *	Twenty-four is the whole header (22 pins) plus room for a couple of
 *	blocks, which is one program using everything at once.  A held
 *	entry is 8 bytes and only HELD ones cost anything, so a slot per
 *	possible resource would be three times the size to say "free" more
 *	elaborately.  Full is -ENOMEM rather than a silent drop.
 */
#define PLK_SLOTS	24

static struct {
	struct p_tab *owner;		/* NULL = the slot is free */
	uint8_t cls;
	uint8_t idx;
} locks[PLK_SLOTS];

/*
 *	The I/O header: GP0-GP7, GP26, GP34-GP46.  Everything else on this
 *	part is the board's - the display, the SD card, the QMI and PSRAM,
 *	the I2S DAC, the console UART, the DS3231 - and a BASIC program
 *	that could claim one of those could hang the machine.
 */
#define HDR_PIN_MASK	(0xFFULL | (1ULL << 26) | (0x1FFFULL << 34))

/*
 *	GP32 is not on the header and is claimable anyway - it is the
 *	DS3231's ALARM output, and an alarm nothing can read is not an
 *	alarm.  With it, waking on the RTC is one line of BASIC:
 *	SETPIN 32, INTL, WakeUp (the line is open-drain and active low).
 *
 *	ONLY ON A PC3, and that is not caution for its own sake: this same
 *	kernel runs the Pico Computer 2, where GP32 is the SD card's MISO
 *	(devsdspi.c, PC2_SD_RX).  Handing it to a BASIC program there
 *	would let it take the card's data line, and releasing it would
 *	reset the pin under a mounted filesystem.  board.c already knows
 *	which machine this is.
 *
 *	The kernel reads the DS3231 over I2C and never touches this pin,
 *	so there is nothing to arbitrate against - unlike I2C0 itself.
 */
#define RTC_ALARM_PIN	32

static int claimable(uint8_t cls, uint8_t idx)
{
	extern int board_is_pc2(void);

	switch (cls) {
	case PLK_PIN:
		if (idx >= 48)
			return 0;
		if ((HDR_PIN_MASK >> idx) & 1)
			return 1;
		if (idx == RTC_ALARM_PIN)
			return !board_is_pc2();
		return 0;
	case PLK_I2C:
		/* I2C0 is GP20/21: the QWIIC socket and the DS3231 together.
		   It stays behind /dev/i2c, which arbitrates it against the
		   RTC poll in interrupt context; handing the raw controller
		   to a program would defeat that. */
		return idx == 1;
	case PLK_SPI:
		return idx == 0;	/* SPI1 is the SD card */
	case PLK_PWM:
		return idx < 12;	/* the kernel uses no slices */
	case PLK_ADC:
		return idx == 0;
	case PLK_PIO:
		/* Exactly the output SM pioout.c reserved at boot (idx is
		   pio*4+sm).  Sound's I2S machine and the whole of PIO0
		   (the user-PIO runtime's block) and PIO2 (the CYW43 bus)
		   stay refused. */
		return idx == PIOOUT_PLK_IDX;
	case PLK_DMA:
		/* Likewise the one channel reserved for it. */
		return idx == PIOOUT_DMA_CH;
	default:
		/* Anything else in PIO or DMA space: sound holds a state
		   machine in pio1 and the display holds DMA channels.
		   Which others are free is a survey, not a guess, and an
		   honest EINVAL beats a claim that looks like it worked. */
		return 0;
	}
}

static int find(uint8_t cls, uint8_t idx)
{
	unsigned i;

	for (i = 0; i < PLK_SLOTS; i++)
		if (locks[i].owner && locks[i].cls == cls && locks[i].idx == idx)
			return (int)i;
	return -1;
}

/*
 *	Put it back.  For a pin that means the state the board powers up
 *	in: function SIO, direction in, output low, no pulls, input buffer
 *	off - so a released pin neither drives anything nor loads it.
 */
static void reset_one(uint8_t cls, uint8_t idx)
{
	switch (cls) {
	case PLK_PIN:
		/* Counting first, and it cannot be skipped: gpio_init()
		   does NOT clear IO_BANK0's interrupt enables, so a
		   program killed while SETPIN FIN/CIN/PER was live would
		   otherwise leave a priority-0 IRQ counting into dead
		   state forever.  A no-op for every pin but GP4-GP7. */
		countpin_reset(idx);
		gpio_init(idx);		/* SIO, dir in, out low */
		gpio_set_dir(idx, GPIO_IN);
		gpio_disable_pulls(idx);
		gpio_set_input_enabled(idx, false);
		break;
	case PLK_I2C:
#ifdef CONFIG_DEV_I2C
		/* NOT i2c_deinit: i2cuser.c has its own "bus 1 is open"
		   state - which pins, and whether a transfer is allowed at
		   all - and deinitialising the block behind its back leaves
		   that saying yes to a controller that is off.  That is
		   exactly the bug this replaced: the flag survived the
		   process that set it and no open ever worked again. */
		plt_i2c_close(idx);
#else
		i2c_deinit(idx ? i2c1 : i2c0);
#endif
		break;
	case PLK_SPI:
#ifdef CONFIG_PC3_SPI0
		/* NOT spi_deinit, for the reason the I2C case above gives:
		   spiuser.c keeps its own "bus 0 is open" state - which
		   pins, how wide the word is, whether a transfer is allowed
		   at all - and deinitialising the block behind its back
		   leaves that saying yes to a controller that is off. */
		plt_spi_close(idx);
#else
		spi_deinit(idx ? spi1 : spi0);
#endif
		break;
	case PLK_PWM:
		pwm_set_enabled(idx, false);
		break;
	case PLK_PIO:
		/* Only the pioout SM is claimable, so this is it: stop and
		   clean, so a program killed mid-frame leaves a half-lit
		   strip and a working machine. */
		pioout_sm_reset();
		break;
	case PLK_DMA:
		pioout_dma_reset();
		break;
	case PLK_ADC:
		/* Nothing to undo: the converter holds no state that
		   outlives the process in a way the next user would not
		   set for itself. */
		break;
	}
}

int pinlock_claim(struct p_tab *who, uint8_t cls, uint8_t idx)
{
	int i;
	unsigned n;

	if (!claimable(cls, idx))
		return -EINVAL;

	i = find(cls, idx);
	if (i >= 0) {
		/* Already ours is success, not an error: a runtime that
		   claims on every SETPIN must not have to remember. */
		if (locks[i].owner == who)
			return 0;
		return -EBUSY;
	}

	for (n = 0; n < PLK_SLOTS; n++) {
		if (!locks[n].owner) {
			locks[n].owner = who;
			locks[n].cls = cls;
			locks[n].idx = idx;
			return 0;
		}
	}
	return -ENOMEM;
}

int pinlock_free(struct p_tab *who, uint8_t cls, uint8_t idx)
{
	int i = find(cls, idx);

	if (i < 0 || locks[i].owner != who)
		return -EINVAL;
	reset_one(cls, idx);
	locks[i].owner = NULL;
	return 0;
}

int pinlock_owner(uint8_t cls, uint8_t idx)
{
	int i = find(cls, idx);

	return i < 0 ? 0 : locks[i].owner->p_pid;
}

/*
 *	Everything a dying (or exec-ing) process owns comes back.
 *
 *	Blocks before pins, in two passes rather than in claim order: a
 *	process holding both GP4 and I2C1 has GP4 muxed to the controller,
 *	and deinitialising a block can move the pin it is driving.  Pins
 *	last means the pin ends in the known safe state whatever the block
 *	did on the way out, and it costs one extra walk of 24 entries on a
 *	path that is already freeing memory.
 */
void pinlock_release(struct p_tab *who)
{
	unsigned i;

	for (i = 0; i < PLK_SLOTS; i++)
		if (locks[i].owner == who && locks[i].cls != PLK_PIN)
			reset_one(locks[i].cls, locks[i].idx);

	for (i = 0; i < PLK_SLOTS; i++) {
		if (locks[i].owner == who) {
			if (locks[i].cls == PLK_PIN)
				reset_one(PLK_PIN, locks[i].idx);
			locks[i].owner = NULL;
		}
	}
}

#endif
