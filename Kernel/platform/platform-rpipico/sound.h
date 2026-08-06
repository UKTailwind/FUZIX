#ifndef PC3_SOUND_H
#define PC3_SOUND_H

#include <stdint.h>

void sound_init(void);

/* BBC SOUND chan,amp,pitch,dur (raw statement parameters; the channel
 * word carries the flush/sync bits).  Returns 0, or -1 with EAGAIN
 * semantics when the channel queue is full. */
int sound_cmd(uint16_t chan, int16_t amp, uint16_t pitch, uint16_t dur);

/* BBC ENVELOPE: e[0] = number 1-16, e[1..13] = T,PI1-3,PN1-3,AA,AD,AS,AR,ALA,ALD */
void sound_envelope(const uint8_t *e);

void sound_quiet(void);

/* PCM streaming: hand the I2S engine decoded samples instead of the
 * BBC synth.  Mutually exclusive with SOUND/ENVELOPE - open takes the
 * state machine and silences the synth.  16-bit signed, interleaved if
 * stereo; mono is duplicated to both channels by the driver.
 *
 * There is one I2S engine, so the stream belongs to ONE process at a
 * time: open, write and close all carry the caller's pid, and a second
 * process asking for it is refused rather than allowed to interleave
 * its samples with the first one's.
 *
 * open returns 0, -1 (bad arguments, or no PSRAM for the ring), or -2
 * when another process holds the stream.
 * write returns the bytes ACCEPTED, which may be less than asked for
 * when the ring is full, or -1 if the caller does not hold the stream.
 * close by anyone else is ignored.
 * stat reports the ring in bytes, and the underrun count since open;
 * anyone may ask.
 * owner returns the pid holding the stream, or 0 if it is free. */
int sound_pcm_open(uint32_t rate, int channels, uint16_t owner);
int sound_pcm_write(const uint8_t *ubuf, uint32_t len, uint16_t owner);
void sound_pcm_stat(uint32_t *space, uint32_t *queued, uint32_t *under);
void sound_pcm_close(uint16_t owner);
uint16_t sound_pcm_owner(void);

#endif
