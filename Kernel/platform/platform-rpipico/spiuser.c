/*
 *	SPI0 for the Pico Computer 3 - MMBasic's SPI, on header pins.
 *
 *	The shape is i2cuser.c's, and for the same reasons: the controller
 *	and its pins are claimed through the pin lock so they come back when
 *	the owner dies, the configuration lives here because the lock knows
 *	about ownership and not about clock rates, and the transfer is a
 *	syscall because what is on the other side of it is the SDK's proven
 *	driver.
 *
 *	SPI1 is not offered at all: it is the SD card's, and a program that
 *	could take it could take the filesystem out from under itself.
 *
 *	WHAT IS DIFFERENT FROM I2C, and it is the whole reason this file
 *	exists rather than an extra case in that one: an I2C transaction is
 *	a few bytes and a bounce buffer costs nothing, while an SPI
 *	transaction here is a display frame.  240x320 at 16 bits is 153,600
 *	bytes; the kernel has 336 bytes of RAM spare.  So there is no bounce
 *	buffer - valaddr says the range belongs to the caller and the
 *	controller then reads it where it lies.  That is sound on this
 *	machine specifically: no MMU, so a user address is a machine
 *	address, and a non-preemptive kernel means nothing can move it
 *	while the transfer runs.
 */

#include <kernel.h>
#include <kdata.h>
#include <printf.h>
#include "picosdk.h"
#include <hardware/spi.h>
#include <hardware/gpio.h>
#include "config.h"
#include "pico_ioctl.h"
#include "pinlock.h"

#ifdef CONFIG_PC3_SPI0

static uint8_t spi0_open;
static uint8_t spi0_sck, spi0_tx, spi0_rx;
static uint8_t spi0_bits = 8;
static uint32_t spi0_hz;		/* what spi_init could actually reach */

/*
 *	The RP2350's mux, and it is not negotiable: the instance is
 *	(pin >> 3) & 1 and the role is pin & 3 - 0 RX, 1 SS, 2 SCLK, 3 TX.
 *	Checked against the SDK's own io_bank0.h for every header pin, so
 *	GP2 is SPI0_SCLK, GP3 is SPI0_TX, GP4 is SPI0_RX, and GP40 upwards
 *	is SPI1 whatever it looks like.
 */
/*	PC3_ prefixed because the SDK's hardware/spi.h already defines
 *	SPI_INSTANCE and means something else by it. */
#define PC3_SPI_INSTANCE(p)	(((p) >> 3) & 1)
#define PC3_SPI_ROLE(p)		((p) & 3)
#define PC3_SPI_RX		0
#define PC3_SPI_SCK		2
#define PC3_SPI_TX		3

int plt_spi_open(uint8_t bus, uint8_t sck, uint8_t tx, uint8_t rx,
                 uint32_t hz, uint8_t mode, uint8_t bits)
{
	int mine, r;
	uint8_t got_ctrl = 0, got_sck = 0, got_tx = 0;

	if (bus != 0)
		return -ENODEV;
	if (PC3_SPI_INSTANCE(sck) != 0 || PC3_SPI_ROLE(sck) != PC3_SPI_SCK)
		return -EINVAL;
	if (PC3_SPI_INSTANCE(tx) != 0 || PC3_SPI_ROLE(tx) != PC3_SPI_TX)
		return -EINVAL;
	if (PC3_SPI_INSTANCE(rx) != 0 || PC3_SPI_ROLE(rx) != PC3_SPI_RX)
		return -EINVAL;
	if (mode > 3)
		return -EINVAL;
	/*	MMBasic's own bounds, to the digit: getint(argv[4], 4, 16). */
	if (bits < 4 || bits > 16)
		return -EINVAL;
	if (hz == 0)
		return -EINVAL;

	/*	Undo only what THIS call took - a re-open by the process that
		already holds the controller must not hand it back because one
		of the new pins was somebody else's. */
	mine = udata.u_ptab->p_pid;
	got_ctrl = (pinlock_owner(PLK_SPI, 0) != mine);
	got_sck = (pinlock_owner(PLK_PIN, sck) != mine);
	got_tx = (pinlock_owner(PLK_PIN, tx) != mine);

	r = pinlock_claim(udata.u_ptab, PLK_SPI, 0);
	if (r)
		return r;
	r = pinlock_claim(udata.u_ptab, PLK_PIN, sck);
	if (r)
		goto undo_ctrl;
	r = pinlock_claim(udata.u_ptab, PLK_PIN, tx);
	if (r)
		goto undo_sck;
	r = pinlock_claim(udata.u_ptab, PLK_PIN, rx);
	if (r)
		goto undo_tx;

	/*	Moved to different pins: the old ones go back, or the header
		keeps three pins muxed to a controller no longer on them. */
	if (spi0_open && (spi0_sck != sck || spi0_tx != tx || spi0_rx != rx)) {
		pinlock_free(udata.u_ptab, PLK_PIN, spi0_sck);
		pinlock_free(udata.u_ptab, PLK_PIN, spi0_tx);
		pinlock_free(udata.u_ptab, PLK_PIN, spi0_rx);
	}

	/*	spi_init returns the rate it could actually reach, which is
	 *	rarely the one asked for: the divisor is clk_peri / (CPSDVSR *
	 *	(1 + SCR)) with CPSDVSR even, so most requests land on a
	 *	neighbouring value and anything above clk_peri / 2 silently
	 *	becomes clk_peri / 2.  Handing that number back is the
	 *	difference between a program knowing its clock and a
	 *	benchmark that reports the same time for 40, 50 and 62.5 MHz
	 *	with no clue why. */
	spi0_hz = spi_init(spi0, hz);
	/*	MMBasic's decode exactly: (mode & 2) is polarity, (mode & 1)
		is phase, and it is always MSB first (SPI.c cmd_spi). */
	spi_set_format(spi0, bits, (mode & 2) ? 1 : 0, (mode & 1) ? 1 : 0,
		       SPI_MSB_FIRST);
	gpio_set_function(sck, GPIO_FUNC_SPI);
	gpio_set_function(tx, GPIO_FUNC_SPI);
	gpio_set_function(rx, GPIO_FUNC_SPI);
	spi0_sck = sck;
	spi0_tx = tx;
	spi0_rx = rx;
	spi0_bits = bits;
	spi0_open = 1;
	/* the ACHIEVED rate, not the requested one - see spi_init above */
	return (int)spi0_hz;

undo_tx:
	if (got_tx)
		pinlock_free(udata.u_ptab, PLK_PIN, tx);
undo_sck:
	if (got_sck)
		pinlock_free(udata.u_ptab, PLK_PIN, sck);
undo_ctrl:
	if (got_ctrl)
		pinlock_free(udata.u_ptab, PLK_SPI, 0);
	return r;
}

/*	Shut the controller.  The pin lock's, on the way out - it does not
 *	touch the locks itself, because pinlock_free is what calls it. */
void plt_spi_close(uint8_t bus)
{
	if (bus != 0 || !spi0_open)
		return;
	spi_deinit(spi0);
	spi0_open = 0;
}

/*	SPI CLOSE: give the whole thing back.  Freeing the controller lock
 *	is what shuts the hardware, so the pins are read out first. */
void plt_spi_release(uint8_t bus)
{
	uint8_t sck = spi0_sck, tx = spi0_tx, rx = spi0_rx;

	if (bus != 0)
		return;
	if (pinlock_free(udata.u_ptab, PLK_SPI, 0))
		return;			/* not ours - leave it alone */
	pinlock_free(udata.u_ptab, PLK_PIN, sck);
	pinlock_free(udata.u_ptab, PLK_PIN, tx);
	pinlock_free(udata.u_ptab, PLK_PIN, rx);
}

/*
 *	One transfer, straight out of the caller's memory.  Returns the
 *	number of units moved, or a negative errno.
 *
 *	The three shapes are MMBasic's three: tx only is SPI WRITE, rx only
 *	is SPI READ (which clocks zeros out to get data back), and both is
 *	the SPI() function's write-and-read.
 */
/*	How many bytes one unit occupies, for the caller that has to bound
 *	the buffer before handing it over (misc.c's valaddr).  A word wider
 *	than 8 bits is carried in 16, which is the width spi_set_format was
 *	given. */
uint8_t plt_spi_unit_bytes(void)
{
	return spi0_bits > 8 ? 2 : 1;
}

int plt_spi_xfer(uint8_t bus, uint8_t *tx, uint8_t *rx, uint32_t len)
{
	int r;

	if (bus != 0)
		return -ENODEV;
	if (!spi0_open)
		return -ENODEV;		/* SPI not OPEN */
	if (len == 0)
		return 0;
	if (tx == NULL && rx == NULL)
		return -EINVAL;

	if (spi0_bits > 8) {
		uint16_t *t = (uint16_t *)tx, *d = (uint16_t *)rx;

		if (t && d)
			r = spi_write16_read16_blocking(spi0, t, d, len);
		else if (t)
			r = spi_write16_blocking(spi0, t, len);
		else
			r = spi_read16_blocking(spi0, 0, d, len);
	} else {
		if (tx && rx)
			r = spi_write_read_blocking(spi0, tx, rx, len);
		else if (tx)
			r = spi_write_blocking(spi0, tx, len);
		else
			r = spi_read_blocking(spi0, 0, rx, len);
	}
	return (r == (int)len) ? (int)len : -EIO;
}

#endif
