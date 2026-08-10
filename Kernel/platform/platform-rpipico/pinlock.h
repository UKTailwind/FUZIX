#ifndef PINLOCK_H
#define PINLOCK_H

#include <stdint.h>

struct p_tab;

/* 0, or a negative errno.  -EBUSY someone else has it, -EINVAL not a
   claimable resource, -ENOMEM the table is full.  Claiming something
   this process already holds succeeds. */
int pinlock_claim(struct p_tab *who, uint8_t cls, uint8_t idx);

/* 0, or -EINVAL if the caller does not hold it.  Resets the resource. */
int pinlock_free(struct p_tab *who, uint8_t cls, uint8_t idx);

/* The pid holding it, or 0 if it is free. */
int pinlock_owner(uint8_t cls, uint8_t idx);

/* Everything this process holds comes back, reset.  Called from the
   exit path (swapper.c: pagemap_free) and from exec (misc.c:
   plt_exec_cleanup), which is every way a process can stop owning
   things - including being killed and faulting. */
void pinlock_release(struct p_tab *who);

#endif
