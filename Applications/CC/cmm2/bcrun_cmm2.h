/*
 *	bcrun on the Colour Maximite 2: what bcrun.c asks of the machine
 *	under -DCMM2 (bcrun_cmm2.c).
 *
 *	bcrun is a loadable program there (tools/sdk in the CMM2 firmware's
 *	repository), started with RUN "bcrun.app", prog.bc [args].  Its C
 *	library is newlib on the firmware's services: files on the SD card,
 *	the console for stdin and stdout.  There is no /dev/sys, no network
 *	and no kernel to ask for PSRAM, so the few things bcrun gets from
 *	those on the PC3 come from here instead.
 */
#ifndef BCRUN_CMM2_H
#define BCRUN_CMM2_H

/* The program's heap: what the loadable program's own heap has left
   once the program is loaded, less a little for bcrun's own C library.
   Returns its address and sets *len, or returns 0. */
unsigned long cmm2_vm_heap(unsigned long *len);

/* Microseconds since the CMM2 started. */
long long cmm2_us64(void);

/* The firmware's poll, every 10ms at most: the USB devices looked
   after, and the program ended if Ctrl-C has been pressed. */
void cmm2_check(void);

/* The native code written out of the data cache and the instruction
   cache emptied, so that what the loader relocated is what runs. */
void cmm2_code_ready(void *code, unsigned long len);

/*
 *	The double helpers native code calls through slots 4-12 of the
 *	helper vector.  Native code passes doubles as the aeabi routines
 *	take them, in r0:r1 and r2:r3, so these are built for that
 *	convention while their bodies use the M7's double-precision FPU.
 */
#define CMM2_AAPCS	__attribute__((pcs("aapcs")))
CMM2_AAPCS double cmm2_dadd(double, double);
CMM2_AAPCS double cmm2_dsub(double, double);
CMM2_AAPCS double cmm2_dmul(double, double);
CMM2_AAPCS double cmm2_ddiv(double, double);
CMM2_AAPCS int cmm2_dcmpeq(double, double);
CMM2_AAPCS int cmm2_dcmplt(double, double);
CMM2_AAPCS int cmm2_dcmple(double, double);
CMM2_AAPCS int cmm2_dcmpge(double, double);
CMM2_AAPCS int cmm2_dcmpgt(double, double);

#endif
