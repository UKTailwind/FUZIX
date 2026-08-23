#ifndef COUNTPIN_H
#define COUNTPIN_H

/* /dev/gpio's GPIOC_CNT_* codes land here (devgpio.c routes them). */
int countpin_ioctl(uarg_t request, char *data);

/* ... and the two edge-capture codes, which carry a different request
   structure: GPIOC_CNT_CAP arms or disarms, GPIOC_CNT_CAPRD copies the
   ring out.  Pulsin( and Distance( are built on these - PLAN-pulsin.md. */
int countpin_cap_ioctl(uarg_t request, char *data);

/* Stop counting on one pin: edge IRQ off, state cleared, the 1ms gate
   timer cancelled if nothing needs it any more.  Safe on any GPIO
   number - only GP4-GP7 do anything.  Called from the ioctl path and
   from pinlock's death-sweep (reset_one), which is what makes a killed
   program's count IRQ die with it. */
void countpin_reset(uint_fast8_t gpio);

#endif
