/*
 *	TTY driver for nano-z80
 *
 */

#include <kernel.h>
#include <kdata.h>
#include <printf.h>
#include <tty.h>
#include <devtty.h>
#include <vt.h>


#define TTY_SERA    5

/* Video TTY ports */
#define vid_tty_data    0x76
#define vid_tty_busy    0x77
#define vid_tty_act_buf 0x30
#define vid_tty_vis_buf 0x31
#define vid_tty_cls     0x09
#define io_page_vid     0x04

/* USB keyboard ports */
#define keyb_data_avail 0x74
#define keyb_data       0x75

/* USB-C port UART ports */
#define uart_a_tx_data  0x70
#define uart_a_tx_ready 0x71
#define uart_a_rx_data  0x72
#define uart_a_rx_avail 0x73

/* 3V3 UART header ports */
#define io_page_reg     0x7f
#define io_page_uart    0x05
#define uart_b_tx_data  0x04
#define uart_b_tx_ready 0x05
#define uart_b_rx_data  0x06
#define uart_b_rx_avail 0x07

/*
 *	One buffer for each tty
 */
static uint8_t tbuf1[TTYSIZ];
static uint8_t tbuf2[TTYSIZ];
static uint8_t tbuf3[TTYSIZ];
static uint8_t tbuf4[TTYSIZ];
static uint8_t tbuf5[TTYSIZ];
static uint8_t tbuf6[TTYSIZ];

static uint8_t sleeping;

static uint8_t active_vt=0;
static uint8_t visible_vt=0;

static struct vt_switch ttysave[4];
/*
 *	TTY masks - define which bits can be changed for each port
 */

tcflag_t termios_mask[NUM_DEV_TTY + 1] = {
    0,
    _CSYS,
    _CSYS,
    _CSYS,
    _CSYS,
    _CSYS,
    _CSYS,
};


/*
 *	One entry per tty. The 0th entry is never used as tty minor 0 is
 *	special (/dev/tty) and it's cheaper to waste a few bytes that keep
 *	doing subtractions.
 */
struct s_queue ttyinq[NUM_DEV_TTY + 1] = {	/* ttyinq[0] is never used */
	{NULL, NULL, NULL, 0, 0, 0},
	{tbuf1, tbuf1, tbuf1, TTYSIZ, 0, TTYSIZ / 2},
	{tbuf2, tbuf2, tbuf2, TTYSIZ, 0, TTYSIZ / 2},
	{tbuf3, tbuf3, tbuf3, TTYSIZ, 0, TTYSIZ / 2},
	{tbuf4, tbuf4, tbuf4, TTYSIZ, 0, TTYSIZ / 2},
	{tbuf5, tbuf5, tbuf5, TTYSIZ, 0, TTYSIZ / 2},
	{tbuf6, tbuf6, tbuf6, TTYSIZ, 0, TTYSIZ / 2},
};


/* Updated early in boot to 0,2,1 if PropIO present. This table works both
   ways purely because of the possible mappings. If that changes we'll need
   a forward and backward table. Most platforms have a fixed idea of the console
   so don't need this remapping layer */
//uint8_t ttymap[NUM_DEV_TTY + 1] = {
//	0, 1, 2, 3, 4, 5, 6
//};

/* Write to system console. This is the backend to all the kernel messages,
   kprintf(), panic() etc. */

void kputchar(uint_fast8_t c)
{
	while(tty_writeready(1) != TTY_READY_NOW);
	if (c == '\n')
		tty_putc(1, '\r');
	while(tty_writeready(1) != TTY_READY_NOW);
	tty_putc(1, c);
}

/*
 *	See if the given tty is able to transmit data without blocking. This
 *	may be done by checking the hardware, or if there is a software
 *	transmit queue by checking the queue is full.
 *
 *	There are three possible returns
 *	TTY_READY_NOW means fire away
 *	TTY_READY_SOON means we will spin trying until pre-empted. As the
 *		8bit processors are slow relative to baud rates it's often
 *		more efficient to do this
 *	TTY_READY_LATER means we will give up the CPU. This is best if the
 *		baud rate is low, the link is blocked by flow control signals
 *		or the CPU is fast.
 *
 *	If TTY_READY_LATER is returned then the kernel will also call
 *	tty_sleeping(minor) before sleeping on the tty so that the driver
 *	can turn on or off tx complete interrupts.
 *
 *	A video display that never blocks will just return TTY_READY_NOW
 */
ttyready_t tty_writeready(uint_fast8_t minor)
{
	if (minor < TTY_SERA)
		return (in(vid_tty_busy) & 0x01) ? TTY_READY_SOON : TTY_READY_NOW;
    else if(minor == TTY_SERA) {
        out(io_page_reg, io_page_uart);
        return (in(uart_b_tx_ready) & 0x01) ? TTY_READY_NOW : 
                                              TTY_READY_SOON;  
    }
    else if(minor == TTY_SERA + 1) {
        return (in(uart_a_tx_ready) & 0x01) ? TTY_READY_NOW : 
                                              TTY_READY_SOON; 
    }
    //return TTY_READY_NOW;
    //return prop_tty_writeready();
    return TTY_READY_NOW;
}

/*
 *	Write a character to a tty. This is the normal user space path for
 *	each outbound byte. It gets called in the normal tty flow, but may
 *	also be called from an interrupt to echo characters even if the
 *	tty is busy. This one reason to implement a small transmit queue.
 *
 *	If the character echo doesn't fit just drop it. It should pretty much
 *	never occur and there is nothing else to do.
 */

void change_vt(uint8_t new_vt) {
    //kprintf("change_vt - new_vt: %d, active_vt: %d\n", new_vt, active_vt);
    if(new_vt != active_vt && new_vt < (TTY_SERA - 1)) {
        vt_save(&ttysave[active_vt]);
        //out(io_page_reg, io_page_vid);
        //out(vid_tty_vis_buf, new_vt);
        vt_load(&ttysave[new_vt]);
        active_vt = new_vt;
    }
}



void tty_putc(uint_fast8_t minor, uint_fast8_t c)
{
	uint8_t ch = c;
    if (minor < TTY_SERA) {
	    out(io_page_reg, io_page_vid);
        out(vid_tty_act_buf, minor-1);
        change_vt(minor - 1);
        vtoutput(&ch, 1);
    }
    else if(minor == TTY_SERA) {
        //kprintf("\nSending %c on TTY2\n",c);
        //out(io_page_reg, io_page_uart);
        out(uart_a_tx_data, c);
    }
    else if(minor == TTY_SERA + 1) {
        out(io_page_reg, io_page_uart);
        out(uart_b_tx_data, c);
    }
	//else
	//	prop_tty_write(c);
}


/*
 *	This function is called whenever the terminal interface is opened
 *	or the settings changed. It is responsible for making the requested
 *	changes to the port if possible. Strictly speaking it should write
 *	back anything that cannot be implemented to the state it selected.
 *
 *	That needs tidying up in many platforms and we also need a proper way
 *	to say 'this port is fixed config' before making it so.
 */
void tty_setup(uint_fast8_t minor, uint_fast8_t flags)
{
    // Clear screen on video terminals, except for boot TTY
    if((minor > 1) && (minor < TTY_SERA)) {
        out(io_page_reg, io_page_vid);
        out(vid_tty_act_buf, minor - 1); // Select active buffer
        while(in(vid_tty_busy));         // Wait for tty to be free
        out(vid_tty_cls, 1);                 // Hardware clear screen
    }
    return;
}

/*
 *	This function is called when the kernel is about to sleep on a tty.
 *	We don't care about this.
 */
void tty_sleeping(uint_fast8_t minor)
{
	sleeping |= (1 << minor);
}

/*
 *	Return 1 if the carrier on the terminal is raised. If the port has
 *	no carrier signal always return 1. It is used to block a port on open
 *	until carrier.
 */
int tty_carrier(uint_fast8_t minor)
{
        //if (ttymap[minor] == 1)
		//return in(uart_msr) & 0x80;
	return 1;
}

/*
 *	When the input queue is part drained this method is called from the
 *	kernel so that hardware flow control signals can be updated.
 */
void tty_data_consumed(uint_fast8_t minor)
{
	used(minor);
}

/*
 * Read keyboard data on interrupt
 */
void read_kb(void) 
{
	uint8_t minor = visible_vt+1;	/* VT minor number */
    uint8_t data = in(keyb_data);
    // Switch video terminal with F1-F4
    if (data >= 0xf0 && data < 0xf4) {
        visible_vt = data - 0xf0;
        out(io_page_reg, io_page_vid);
        out(vid_tty_vis_buf, visible_vt);
        change_vt(visible_vt);
    }
    else
        vt_inproc(minor, data);
}

/*
 * Read UART A on interrupt
 */
void read_uart_a(void) 
{
    while(in(uart_a_rx_avail)) {
        tty_inproc(TTY_SERA, in(uart_a_rx_data));
    }
}

/*
 * Read UART B on interrupt
 */
void read_uart_b(void) 
{
    out(io_page_reg, io_page_uart);
    while(in(uart_b_rx_avail)) {
        tty_inproc(TTY_SERA+1, in(uart_b_rx_data));
    }
}




/*
 *	Our platform specific code so we have a function to call to poll the
 *	serial ports for activity.
 */

//void tty_poll(void)
//{	
	/*uint8_t minor = visible_vt+1;	/* VT minor number */

	/*if (in(keyb_data_avail) & 0x01) {
        // Check for F-keys to change visible vt
        uint8_t data = in(keyb_data);
        //kprintf("Keyboard input data: 0x%x", data);
        if (data >= 0xf0) {
            visible_vt = data - 0xf0;
            out(io_page_reg, io_page_vid);
            out(vid_tty_vis_buf, visible_vt);
            change_vt(visible_vt);
        }
        else
            vt_inproc(minor, data);
    }*/

//	uint8_t minor = TTY_SERA;	/* UART minor number */

 //   out(io_page_reg, io_page_uart);
 //   if (in(uart_b_rx_avail) & 0x01) {
 //       uint8_t data = in(uart_b_rx_data);
 //       tty_inproc(minor, data);
 //   }

 //   minor = TTY_SERA + 1;
 //   if (in(uart_a_rx_avail) & 0x01) {
 //       uint8_t data = in(uart_a_rx_data);
 //       tty_inproc(minor, data);
 //   }


//}

int nz80_tty_ioctl(uint_fast8_t minor, uarg_t request, char *data)
{
    uint8_t dev = minor;

    // Use standard ioctl for serial ports
    if(minor >= TTY_SERA)
        return tty_ioctl(minor, request, data);

    // Otherwise VT - only support reporting size for now
    if(request == VTSIZE)
        return (30 << 8) | 80;

    // Use built in for other requests for now 
    return vt_ioctl(minor, request, data);
}

