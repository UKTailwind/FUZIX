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

#endif
