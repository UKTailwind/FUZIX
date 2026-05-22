#include <kernel.h>
#include <kdata.h>
#include <printf.h>
#include <stdbool.h>
#include <devtty.h>
#include <keycode.h>
#include <vt.h>
#include <tty.h>
#include <graphics.h>
#include <input.h>
#include <devinput.h>


#define VT_CON	4

uint8_t screenbases[VT_CON] = {0x00, 0x40, 0x80, 0x40};
uint8_t screenpages[VT_CON] = {0x00, 0x10, 0x20, 0x30};
uint16_t CRTC_offsets[VT_CON] = {0, 0, 0, 0};
uint8_t vtborders[VT_CON] = {1, 9, 7, 7};
static struct vt_switch ttysave[VT_CON];

uint8_t outputtty = 1;
uint8_t inputtty = 1;

static uint8_t syscon = 1;	/* system console output */

static char tbuf1[TTYSIZ];
static char tbuf2[TTYSIZ];
static char tbuf3[TTYSIZ];
static char tbuf4[TTYSIZ];
#ifdef CONFIG_USIFAC_SERIAL
 static char tbuf5[TTYSIZ];
#endif

uint8_t vtattr_cap = VTA_UNDERLINE;
extern uint8_t curattr;

tcflag_t termios_mask[NUM_DEV_TTY + 1] = {
	0,
	_CSYS,
	_CSYS,
#ifdef CONFIG_USIFAC_SERIAL
	_CSYS | CBAUD	
#endif
};


struct s_queue ttyinq[NUM_DEV_TTY + 1] = {	/* ttyinq[0] is never used */
	{NULL, NULL, NULL, 0, 0, 0},
	{tbuf1, tbuf1, tbuf1, TTYSIZ, 0, TTYSIZ / 2},
	{tbuf2, tbuf2, tbuf2, TTYSIZ, 0, TTYSIZ / 2},
	{tbuf3, tbuf3, tbuf3, TTYSIZ, 0, TTYSIZ / 2},
	{tbuf4, tbuf4, tbuf4, TTYSIZ, 0, TTYSIZ / 2},	
#ifdef CONFIG_USIFAC_SERIAL
	{tbuf5, tbuf5, tbuf5, TTYSIZ, 0, TTYSIZ / 2},
#endif
};
#ifdef CONFIG_USIFAC_SERIAL
static const uint8_t baudtable[] = { /*Usifac baudrate commands*/

	0xff,			/* 50 */
	0xff,			/* 75 */
	0xff,			/* 110 */
	0xff,			/* 134.5 */
	0xff,			/* 150 */
	10,			/* 300 */
	0xff,			/* 600 */
	0xff,			/* 1200 */
	11,			/* 2400 */
	0xff,			/* 4800 */
	12,			/* 9600 */
	13,			/* 19200 */
	14,			/* 38400 */
	15,			/* 57600 */
	16,			/* 115200 */
};
#endif


static uint8_t igrbcpc[16] = {
	0x54,	/* 0000 to Black 0 */
	0x44,	/* 000B to blue 1 */
	0x5c,	/* 00R0 to red 2 */
	0x58,	/* 00RB to magenta 3 */
	0x56,	/* 0G00 to green 4 */
	0x46,	/* 0G0B to cyan 5 */
	0x5e,	/* 0GR0 to yellow 6 */
	0x40,	/* 0GRB to white 7 */
	0x40,	/* I000 to white 8 */
	0x55,	/* I00B to bright blue 9 */
	0x4c,	/* 10R0 to bright red 10 */
	0x4d,	/* 10RB to bright magenta 11 */
	0x52,	/* 1G00 to bright green 12 */
	0x53,	/* 1G0B to bright cyan 13 */
	0x4a,	/* 1GR0 to bright yellow 14 */
	0x4b	/* 1GRB to bright white 15 */
};

void devtty_init(void){
	ttysave[0].paper = 1;
	ttysave[1].paper = 1;
	ttysave[2].paper = 0;
	ttysave[3].paper = 15;
	ttysave[0].ink = 14;
	ttysave[1].ink = 15;
	ttysave[2].ink = 12;
	ttysave[3].ink = 1;
	cpctty_set_color(1, VTPAPER, ttysave[0].paper);
	cpctty_set_color(1, VTINK, ttysave[0].ink);
	cpctty_set_color(1, VTBORDER, vtborders[0]);
}

void cpckbd_conswitch(uint8_t console)
{
	if (console > VT_CON || console == inputtty){
		return;
	}
    vt_cursor_off();
	switch_outputtty(console);
	inputtty = console;
	ga_set_visible_vt();
	cpctty_set_color(console, VTPAPER, ttysave[console -1].paper);
	cpctty_set_color(console, VTINK, ttysave[console -1].ink);
	cpctty_set_color(console, VTBORDER, vtborders[console -1]);
    vt_cursor_on();
}

static void ga_putc(uint_fast8_t minor, uint_fast8_t c)
{
	irqflags_t irq = di();

	if (outputtty != minor)
		switch_outputtty(minor);
	vtoutput(&c, 1);
	irqrestore(irq);
}

static void switch_outputtty(uint8_t tty)
{
    vt_save(&ttysave[outputtty - 1]);
    CRTC_offsets[outputtty - 1] = CRTC_offset;
    outputtty = tty;
    vt_load(&ttysave[outputtty - 1]);
    screenbase  = screenbases[outputtty - 1];
    screenpage  = screenpages[outputtty - 1];
    CRTC_offset = CRTC_offsets[outputtty - 1];
}
/* Output for the system console (kprintf etc) */
void kputchar(char c)
{
	if (c == '\n'){
		tty_putc(syscon, '\r');
#ifndef CONFIG_USIFAC_SLIP
 #ifdef CONFIG_USIFAC_SERIAL
		tty_putc(5, '\r');
 #endif
#endif 
	}
	tty_putc(syscon, c);
#ifndef CONFIG_USIFAC_SLIP
 #ifdef CONFIG_USIFAC_SERIAL
	tty_putc(5, c);
 #endif
#endif 	
}

/* Both console and debug port are always ready */
ttyready_t tty_writeready(uint8_t minor)
{
	minor;
	return TTY_READY_NOW;
}

void tty_putc(uint8_t minor, unsigned char c)
{
	switch (minor){
		case 1:
		case 2:
		case 3:
		case 4:		
			ga_putc(minor, c);
			break;
#ifdef CONFIG_USIFAC_SERIAL			
		case 5:
			usifdata = c;
			break;
#endif
	}
}

int tty_carrier(uint8_t minor)
{
	minor;
	return 1;
}

void tty_setup(uint8_t minor, uint8_t flags)
{
#ifdef CONFIG_USIFAC_SERIAL
	struct termios *t = &ttydata[minor].termios;
	uint16_t cflag = t->c_cflag;
	uint8_t baud;
	used(flags);

	if (minor == 5){
		baud = cflag & CBAUD;
		if (baudtable[baud] == 0xFF){
			usifctrl = USIFAC_SET_115200B_COMMAND; /*Default 115200 for not supported baudrate request*/
			cflag &= ~CBAUD;
			cflag |= B115200;
			t->c_cflag = cflag;
		}
		else usifctrl = baudtable[baud];
	}
#endif
}
#ifdef CONFIG_USIFAC_SERIAL
void tty_pollirq_usifac(void)
{		
	while (usifctrl == 0xff)
		tty_inproc(5, usifdata);
	tty_outproc(5);
}
#endif
void tty_sleeping(uint8_t minor)
{
	minor;
}

void tty_data_consumed(uint8_t minor)
{
}


/* This is used by the vt asm code, but needs to live in the kernel */
uint16_t cursorpos;

void vtattr_notify(void)
{
/*For now we are only in mode 2, so two colours per tty, not much to do here*/

}


void cpctty_set_color(uint8_t tty, uint8_t cmd, uint8_t col)
{
	uint8_t c = igrbcpc[col & 0xf];
	switch(cmd){
		case VTBORDER:
			vtborders[tty-1] = col;
			vtborder = c;
			gatearray = PENR_BORDER_SELECT;
			gatearray = c;
			break;
		case VTINK:
			ttysave[tty-1].ink = col;
			if (outputtty == tty)
				vtink = col;
			gatearray = PENR_INK_SELECT;
			gatearray = c;
			break;
		case VTPAPER:
			ttysave[tty-1].paper = col;
			if (outputtty == tty)
				vtpaper = col;
			gatearray = PENR_PAPER_SELECT;
			gatearray = c;
			break;
	}
}

int cpctty_ioctl(uint8_t minor, uarg_t arg, char *ptr)
{
	switch (minor){
		case 1:
		case 2:
		case 3:
		case 4:		
			switch(arg){
				case VTBORDER:
				case VTINK:
				case VTPAPER:
					cpctty_set_color(udata.u_ptab->p_tty, arg, ugetc(ptr));
                    return 0;
				default:
					return vt_ioctl(minor, arg, ptr);
			}
			break;
#ifdef CONFIG_USIFAC_SERIAL
		default:
			return tty_ioctl(minor, arg, ptr);
#endif
		}
			
}
