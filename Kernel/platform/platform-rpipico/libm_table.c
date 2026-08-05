/*
 * The maths library, once, in flash - for every program on the machine.
 *
 * WHY THIS EXISTS.  bcrun links its own copy of libm, and bcrun is
 * loaded for every translated BASIC program: about 13K of pow, the
 * range reducers, the inverse trig and the rest, in each running
 * process, referenced through a table of function pointers so the
 * linker cannot drop any of it even for a program that never calls a
 * maths function.  A running bcrun was 154,469 bytes and this was the
 * second largest thing in it.
 *
 * HOW IT CAN BE SHARED.  There is no MMU and no MPU on this board, and
 * PROGBASE is an array inside the kernel's own address space, so user
 * code and kernel code live in one flat address space.  A program can
 * therefore CALL these directly - an ordinary bl, not a syscall - and
 * the 1.3us crossing that rules out putting sin() behind an ioctl does
 * not apply.  The kernel's copy lives in flash, so it costs no RAM at
 * all, and there is one of it however many programs are running.
 *
 * This is not a new idea here: MMBasic exports its firmware entry
 * points to CSUBs through a fixed offset table in exactly this way
 * (see armcfgen.md - DrawBitmap at 0x3C and the rest).
 *
 * THE ABI.  The kernel is built -mfloat-abi=softfp and userland soft;
 * those agree at function boundaries, arguments passing in the core
 * registers either way, and the M33's FPU is single precision so every
 * double here is software arithmetic in both.  A float-taking function
 * would NOT be safe to share and none is exported.
 *
 * ERRNO.  These do not report domain errors.  A kernel-resident
 * function that set errno would set the KERNEL's, not the calling
 * process's, which would be worse than not setting it at all - so the
 * contract is that callers check their own arguments.  MMBasic does
 * not consult errno after a maths call and neither does the runtime.
 * That is the one behavioural difference from calling libm directly,
 * and it is why the table is versioned: if it ever has to change, an
 * old binary should fail loudly rather than call the wrong slot.
 */

#include <kernel.h>
#include <math.h>
#include "pico_ioctl.h"

/*
 * The order is the ABI.  Append only, and bump the version if anything
 * about an existing slot changes - userland indexes this by position,
 * through the names in pico_ioctl.h.
 */
static const struct pc3_libm libm_table = {
    PC3_LIBM_MAGIC,
    PC3_LIBM_VERSION,
    PC3_LIBM_NFN,
    {
        (void *)sin,   (void *)cos,   (void *)tan,
        (void *)asin,  (void *)acos,  (void *)atan,
        (void *)sinh,  (void *)cosh,  (void *)tanh,
        (void *)sqrt,  (void *)exp,   (void *)log,
        (void *)log10, (void *)floor, (void *)ceil,
        (void *)fabs,
        /* the two-argument ones last, so the one-argument block above
           can be walked without knowing where it ends */
        (void *)pow,   (void *)atan2, (void *)fmod
    }
};

/* The address, for PICOIOC_LIBM.  A pointer rather than a fixed
 * address: the linker decides where this lands and a number written
 * down in two places would eventually be written down wrongly. */
const struct pc3_libm *plt_libm(void)
{
    return &libm_table;
}
