#ifndef PIOOUT_H
#define PIOOUT_H

/* Load the five fixed output programs into PIO1 and reserve the
   output SM and DMA channel (constants in pico_ioctl.h).  Panics if
   the load does not land where the ABI says - a build error, not a
   runtime condition. */
void pioout_init(void);

/* The death-sweep halves, for pinlock.c reset_one(): stop and clean
   the reserved SM / abort the reserved channel. */
void pioout_sm_reset(void);
void pioout_dma_reset(void);

/* GPIOC_PIOOUT_BUF lands here (devgpio.c routes it). */
int pioout_ioctl(uarg_t request, char *data);

#endif
