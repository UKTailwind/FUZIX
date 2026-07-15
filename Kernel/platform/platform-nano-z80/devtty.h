#ifndef _DEVTTY_H
#define _DEVTTY_H

extern void tty_poll(void);
extern uint8_t timermsr;
extern int nz80_tty_ioctl(uint_fast8_t minor, uarg_t request, char *data);

#endif
