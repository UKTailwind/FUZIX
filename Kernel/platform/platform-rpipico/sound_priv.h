#ifndef PC3_SOUND_PRIV_H
#define PC3_SOUND_PRIV_H
/*
 * The seam between sound.c and sound_hw.c.
 *
 * sound.c is the portable half: the BBC synth, the PCM ring, the
 * MMBasic synth and the ownership rules, all of it over plain blocks of
 * 16-bit stereo frames.  sound_hw.c is the PC3's output stage: the PIO
 * I2S program, the chained DMA and its completion interrupt, the clock
 * divider, and the three things the portable half needs from the kernel
 * proper - a copy from a process's memory, the process table, and the
 * sleep behind SNDIOC_PCMWAIT.  As display_priv.h is to the display,
 * this header is everything the two halves say to each other, and it is
 * deliberately small: the PC3 device server on a PC compiles sound.c
 * against its own implementation of what is declared here (build it with
 * -DPC3_HOST), so anything added below is a promise to two machines.
 *
 * Nothing outside the two files includes this.  sound.h remains the
 * public interface.
 */

#include <stdint.h>
#include "sound.h"

/*
 * Frames per block, and it is a BUS decision as much as a memory one.
 * This was 256 - a 1K half - which is fine for the synth, because that
 * computes its samples one at a time with phase and envelope arithmetic
 * in between and its writes trickle out over the whole interval.  PCM
 * streaming does the identical byte count as a flat-out memcpy, and a
 * saturated 1K burst every 5.8 ms was enough to put flecks on the
 * display: core1 builds every scanline in software and DMAs it out of
 * disp_fb continuously, so RAM bandwidth is contended
 * (default_text_excludes.incl says so in as many words).
 *
 * MicroPython's machine_i2s.c runs a 256 byte DMA buffer - 128 bytes a
 * half - and says the size was "empirically determined... a tradeoff
 * between memory use and interrupt frequency".  64 frames is 256 bytes
 * a half: a quarter of the old burst, while keeping 1.45 ms of slack at
 * 44100 against interrupt latency, where MicroPython's 128 bytes would
 * leave only 0.73 ms and this kernel does disable interrupts in places.
 */
#define SND_NBUF  64            /* stereo frames per half-buffer */
#define SND_RATE  22050         /* the BBC synth's rate */
#define MMS_RATE  44100         /* the MMBasic synth's rate */

/* ---- what the core asks of the hardware ---------------------------------- */

/* The output stage's sample rate.  Process context only - open, close,
 * PLAY STOP - never from the fill, which runs in the interrupt: on the
 * board this reaches the SDK's PIO code, which is in flash. */
void snd_hw_rate(uint32_t rate);

/* n bytes from a caller's buffer into the ring: 0, or -1 if the source
 * cannot be read.  The kernel's uget; a memcpy on the host, where the
 * bytes arrived in the request. */
int snd_hw_copyin(void *dst, const void *src, uint32_t n);

/* Is the process that owns a stream still there?  The kernel scans its
 * process table; the server asks its connection table. */
int snd_hw_pid_alive(uint16_t pid);

/*
 * The lock between the note queues and the fill.  On the board the fill
 * runs in the DMA interrupt, so the lock is di(); on a PC the fill runs
 * on the audio thread, the lock is a mutex, and the server holds the
 * same mutex around the fill itself.  SND_FAST marks the fill and what
 * it calls: RAM-resident on the board (the linker's
 * default_text_excludes.incl leaves .time_critical sections out of
 * flash), nothing on the host.
 */
#ifdef PC3_HOST
typedef int snd_lock_t;
snd_lock_t snd_hw_lock(void);
void snd_hw_unlock(snd_lock_t l);
#define SND_FAST(fn) fn
#else
#include <kernel.h>
#include "picosdk.h"
typedef irqflags_t snd_lock_t;
#define snd_hw_lock()     di()
#define snd_hw_unlock(l)  irqrestore(l)
#define SND_FAST(fn) __not_in_flash_func(fn)
#endif

/* ---- what the core offers the hardware ----------------------------------- */

/* Fill one block of SND_NBUF stereo frames from whichever of the three
 * sources holds the output: the PCM ring, the MMBasic synth, else the
 * BBC synth.  Interrupt context on the board; the audio thread on a PC. */
void sound_fill_block(int16_t *buf);

/* The PCM stream's level, for the wait: 0 and the bytes queued if
 * `owner' holds the stream (0 = whoever does), -1 if no stream is open
 * or another pid holds it. */
int sound_pcm_queued(uint16_t owner, uint32_t *queued);

#endif /* PC3_SOUND_PRIV_H */
