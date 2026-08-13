/*
 * Pico Computer 3 sound for Fuzix: the BBC Micro sound system on the
 * PCM5102 I2S DAC (BCLK GP10, LRCLK GP11, DATA GP22).
 *
 * The classic model, kernel-side: channels 1-3 are square-wave tones,
 * channel 0 is an LFSR noise source; each channel has a note queue and
 * the SOUND channel-word flush/sync bits work; ENVELOPE 1-16 provides
 * the authentic three-section pitch envelope plus ADSR amplitude,
 * stepped at 100 Hz.  Pitch is the BBC scale: 4 units per semitone,
 * 89 = A4 = 440 Hz.
 *
 * Output: PIO1 runs the standard 16-bit stereo I2S program (2 clocks
 * per bit; SM clock = 64 x sample rate); two chained DMA channels
 * ping-pong 256-sample buffers and the completion IRQ (DMA_IRQ_0,
 * core0) remixes the freed half.  Mixing costs well under 0.1% of a
 * core.  Everything lives in RAM.
 *
 * The same DMA chain has a second mode - see "PCM streaming" below -
 * where the IRQ copies a process's decoded PCM out of a PSRAM ring
 * instead of synthesising.  That is what plays MP3s and WAVs, and it
 * is the reason the output stage was worth building this way rather
 * than MMBasic's: the DMA does not care which of the two filled the
 * buffer, so nothing in the chain changes.  See PC3-MP3-PLAN.md.
 */

#include <kernel.h>
#include <kdata.h>
#include <printf.h>
#include <stdlib.h>
#include "picosdk.h"
#include "config.h"
#include "sound.h"

#include <hardware/pio.h>
#include <hardware/dma.h>
#include <hardware/clocks.h>

#define SND_BCLK  10            /* LRCLK is BCLK+1 = GP11 */
#define SND_DATA  22
#define SND_RATE  22050
/*
 * Frames per half-buffer, and it is a BUS decision as much as a memory
 * one.  This was 256 - a 1K half - which is fine for the synth, because
 * that computes its samples one at a time with phase and envelope
 * arithmetic in between and its writes trickle out over the whole
 * interval.  PCM streaming does the identical byte count as a flat-out
 * memcpy, and a saturated 1K burst every 5.8 ms was enough to put
 * flecks on the display: core1 builds every scanline in software and
 * DMAs it out of disp_fb continuously, so RAM bandwidth is contended
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
#define SND_QLEN  8             /* notes per channel queue */

/* MicroPython machine_i2s 16-bit stereo write program (2 side-set
 * bits: bit0 BCLK, bit1 LRCLK; MSB first, autopull 32). */
static uint16_t i2s_prog[8] = {
    59438, 24577, 2113, 28673, 63534, 28673, 6213, 24577
};

/* BBC pitch: phase increments for pitches 240-287 at 22050 Hz;
 * inc(p) = table[p % 48] >> (5 - p / 48). */
static uint32_t pinc48[48] = {
    0x2D36D1A9, 0x2DDF2DB9, 0x2E89FCB1, 0x2F3747AE, 0x2FE717F2, 0x309976DF,
    0x314E6DFB, 0x320606EE, 0x32C04B87, 0x337D45B6, 0x343CFF93, 0x34FF8359,
    0x35C4DB6A, 0x368D1251, 0x375832BD, 0x38264786, 0x38F75BAC, 0x39CB7A59,
    0x3AA2AEE0, 0x3B7D04BD, 0x3C5A879A, 0x3D3B4348, 0x3E1F43C8, 0x3F069545,
    0x3FF14418, 0x40DF5CC9, 0x41D0EC0F, 0x42C5FECD, 0x43BEA219, 0x44BAE33A,
    0x45BACFA7, 0x46BE7509, 0x47C5E13D, 0x48D12253, 0x49E0468E, 0x4AF35C6A,
    0x4C0A7295, 0x4D2597F5, 0x4E44DBA8, 0x4F684D05, 0x508FFB98, 0x51BBF72D,
    0x52EC4FC6, 0x542115A3, 0x555A5940, 0x56982B55, 0x57DA9CDB, 0x5921BF08,
};

struct note {
    uint8_t amp;                /* 0-15 volume, or 0x80 | envelope# */
    uint8_t pitch;
    uint8_t dur;                /* 20ths of a second; 255 = forever */
    uint8_t sync;               /* 0-3 */
};

struct schan {
    /* queue */
    struct note q[SND_QLEN];
    volatile uint8_t qr, qw;
    /* playing note */
    uint8_t active;
    uint8_t env;                /* envelope number or 0 */
    uint8_t pitch;              /* base pitch of the note */
    int16_t dur_cs;             /* remaining, -1 = forever */
    int16_t level;              /* current amplitude 0..126 */
    uint32_t phase, inc;
    /* envelope runtime */
    uint8_t esec;               /* pitch section 0-2, 3 = done */
    uint8_t ecount;             /* steps left in section */
    int16_t poff;               /* accumulated pitch offset */
    uint8_t ephase;             /* 0 attack 1 decay 2 sustain 3 release */
    uint8_t tctr;               /* envelope step countdown (cs) */
};

static struct schan ch[4];
static uint8_t envs[17][13];    /* T,PI1-3,PN1-3,AA,AD,AS,AR,ALA,ALD */
static uint32_t noise_lfsr = 0x1FFFF;
static uint8_t noise_ctr;

static int16_t sndbuf[2][SND_NBUF * 2];
static int dmach_a = -1, dmach_b = -1;
static PIO snd_pio;
static uint snd_sm;
static uint16_t cs_acc;         /* 100 Hz tick accumulator */

/* --- note/envelope engine (IRQ context) ---------------------------------- */

static void set_inc(struct schan *c)
{
    int p = c->pitch + (c->env ? c->poff : 0);
    if (p < 0) p = 0;
    if (p > 255) p = 255;
    c->inc = pinc48[p % 48] >> (5 - p / 48);
}

static void start_note(struct schan *c, struct note *n)
{
    c->pitch = n->pitch;
    c->dur_cs = (n->dur == 255) ? -1 : (n->dur ? n->dur * 5 : 1);
    c->poff = 0;
    c->esec = 0;
    c->ecount = 0;
    c->ephase = 0;
    c->tctr = 0;
    if (n->amp & 0x80) {
        c->env = n->amp & 0x7F;
        if (c->env > 16) c->env = 16;
        c->level = 0;
        c->ecount = envs[c->env][4];    /* PN1 */
    } else {
        c->env = 0;
        c->level = (n->amp > 15 ? 15 : n->amp) * 8;   /* 0..120 */
    }
    c->active = 1;
    set_inc(c);
}

static void env_step(struct schan *c)
{
    uint8_t *e = envs[c->env];
    /* pitch envelope: sections of PN1-3 steps of PI1-3 each */
    if (c->esec < 3) {
        while (c->esec < 3 && c->ecount == 0) {
            c->esec++;
            if (c->esec < 3)
                c->ecount = e[4 + c->esec];
            else if (!(e[0] & 0x80)) {
                /* auto-repeat the pitch envelope */
                c->esec = 0;
                c->ecount = e[4];
                c->poff = 0;
            }
        }
        if (c->esec < 3 && c->ecount) {
            c->poff += (int8_t)e[1 + c->esec];
            c->ecount--;
            set_inc(c);
        }
    }
    /* amplitude ADSR: AA until ALA, AD until ALD, AS, then release */
    {
        int16_t lvl = c->level;
        int8_t ala = e[11] & 0x7F, ald = e[12] & 0x7F;
        switch (c->ephase) {
        case 0:
            lvl += (int8_t)e[7];
            if (lvl >= ala) { lvl = ala; c->ephase = 1; }
            break;
        case 1:
            lvl += (int8_t)e[8];
            if (lvl <= ald) { lvl = ald; c->ephase = 2; }
            break;
        case 2:
            lvl += (int8_t)e[9];        /* AS: 0 or negative */
            break;
        case 3:
            lvl += (int8_t)e[10];       /* AR: negative */
            break;
        }
        if (lvl < 0) lvl = 0;
        if (lvl > 126) lvl = 126;
        c->level = lvl;
        if (c->ephase == 3 && lvl == 0)
            c->active = 0;
    }
}

static void try_dequeue(void)
{
    int i, j, n;
    for (i = 0; i < 4; i++) {
        struct schan *c = &ch[i];
        if (c->active || c->qr == c->qw)
            continue;
        struct note *hd = &c->q[c->qr % SND_QLEN];
        if (hd->sync) {
            /* count idle channels whose head carries the same sync */
            n = 0;
            for (j = 0; j < 4; j++) {
                struct schan *o = &ch[j];
                if (!o->active && o->qr != o->qw &&
                    o->q[o->qr % SND_QLEN].sync == hd->sync)
                    n++;
            }
            if (n < hd->sync + 1)
                continue;
            /* release the whole group */
            for (j = 0; j < 4; j++) {
                struct schan *o = &ch[j];
                if (!o->active && o->qr != o->qw &&
                    o->q[o->qr % SND_QLEN].sync == hd->sync) {
                    start_note(o, &o->q[o->qr % SND_QLEN]);
                    o->qr++;
                }
            }
        } else {
            start_note(c, hd);
            c->qr++;
        }
    }
}

static void tick_100hz(void)
{
    int i;
    for (i = 0; i < 4; i++) {
        struct schan *c = &ch[i];
        if (!c->active)
            continue;
        if (c->env) {
            uint8_t t = envs[c->env][0] & 0x7F;
            if (t == 0) t = 1;
            if (++c->tctr >= t) {
                c->tctr = 0;
                env_step(c);
            }
        }
        if (c->dur_cs > 0 && --c->dur_cs == 0) {
            if (c->env && c->ephase < 3)
                c->ephase = 3;          /* enter release */
            else
                c->active = 0;
        }
    }
    try_dequeue();
}

/* --- mixer ---------------------------------------------------------------- */

static void __not_in_flash_func(snd_fill)(int16_t *buf)
{
    int s, i;
    for (s = 0; s < SND_NBUF; s++) {
        int32_t mix = 0;

        cs_acc += 100;
        if (cs_acc >= SND_RATE) {
            cs_acc -= SND_RATE;
            tick_100hz();
        }

        for (i = 1; i < 4; i++) {
            struct schan *c = &ch[i];
            if (c->active && c->level) {
                uint32_t ph, nc, u, t;
                int32_t v;

                c->phase += c->inc;
                ph = c->phase;
                nc = c->inc;
                v = (ph & 0x80000000u) ? c->level : -c->level;

                /* polyBLEP.  A square that can only flip on sample
                 * boundaries carries alias images that beat against
                 * the true harmonics - a pitch-dependent shimmer on
                 * sustained notes, worst at the top of the range.
                 * The band-limited step differs from the naive one
                 * only within a sample of each edge, and there the
                 * residual is (1-tau)^2 of the step toward the
                 * transition midpoint - which for a square centred
                 * on zero is just a scale-down of the sample's own
                 * value.  tau in Q8; the divide is a single UDIV on
                 * this core, and only edge-adjacent samples (a few
                 * hundred per second per voice) reach it.  This
                 * runs in the DMA IRQ: everything stays inline and
                 * integer.  Edges: wrap = fall, half = rise. */
                if (ph < nc)
                    u = ph;                     /* just after fall */
                else if (ph > (uint32_t)-nc)
                    u = (uint32_t)-ph;          /* just before fall */
                else if ((ph - 0x80000000u) < nc)
                    u = ph - 0x80000000u;       /* just after rise */
                else if ((0x80000000u - ph) < nc)
                    u = 0x80000000u - ph;       /* just before rise */
                else
                    u = ~0u;
                if (u != ~0u && (t = u / (nc >> 8)) < 256) {
                    t = 256 - t;
                    v -= (int32_t)(v * (int32_t)(t * t)) >> 16;
                }
                mix += v;
            }
        }
        /* channel 0: noise, LFSR clocked from its pitch */
        if (ch[0].active && ch[0].level) {
            if (++noise_ctr >= (2 << (ch[0].pitch & 3))) {
                noise_ctr = 0;
                /* 15-bit LFSR, taps 0 and 1: white-ish */
                uint32_t b = ((noise_lfsr) ^ (noise_lfsr >> 1)) & 1;
                noise_lfsr = (noise_lfsr >> 1) | (b << 14);
            }
            mix += (noise_lfsr & 1) ? ch[0].level : -ch[0].level;
        }

        mix *= 64;                       /* 4 x 126 x 64 = 32256 max */
        if (mix > 32767) mix = 32767;
        if (mix < -32768) mix = -32768;
        buf[s * 2] = mix;
        buf[s * 2 + 1] = mix;
    }
}

/* --- PCM streaming -------------------------------------------------------
 *
 * The second mode for the buffer filler: instead of synthesising the BBC
 * channels, copy PCM a process has already decoded.  This is what
 * MicroPython's machine_i2s.c does - the DMA chain is untouched and the
 * completion IRQ's whole job is feed_dma(), one block out of a ring, or
 * silence when the ring is dry.
 *
 * The ring is in PSRAM (the kernel's heap IS the PSRAM window, see
 * arena.c) because there is nowhere else: kernel SRAM has single-digit
 * kilobytes spare, and the ring wants a quarter of a megabyte.  At
 * 44100 stereo the stream is 176.4 KB/s, so 256K is about 1.5 seconds -
 * chosen against the ~25 ms a process can be swapped out for, plus SD
 * latency, rather than against the ~93 ms MMBasic and MicroPython use.
 * Neither of those has to survive being swapped out; a Fuzix process
 * does.
 *
 * Allocated on the first open and never freed.  A machine that never
 * plays anything pays nothing.
 *
 * The RING is shared but the STREAM is not: it belongs to one pid at a
 * time.  The first version left it open to anyone, and two players
 * really did interleave their samples into it - it sounded like it, and
 * whichever finished first closed the stream under the other, which
 * then got EINVAL on every write.  So open refuses a second process
 * (EBUSY) and only the owner may write or close.  A player that dies
 * without closing is not a deadlock: pcm_reap notices the pid is gone
 * and hands the state machine back.
 *
 * head and tail are free-running byte counters; used = head - tail in
 * unsigned arithmetic, which is exact across wrap and needs no spare
 * slot to tell full from empty.  Only the IRQ moves tail and only the
 * ioctl moves head, so neither needs a lock.
 */

#define PCM_RING_BYTES (256u * 1024u)

static uint8_t *pcm_ring;
static volatile uint32_t pcm_head, pcm_tail;
static volatile uint32_t pcm_underruns;
static volatile uint8_t pcm_active, pcm_started;
static uint8_t pcm_channels = 2;
static uint16_t pcm_owner;              /* the pid holding the stream */

/* Set the state machine clock for a sample rate.  Two samples a frame,
 * sixteen bits each, two PIO instructions a bit = 64 clocks a frame.
 * Integer only: the kernel is built with float trapped
 * (pico_set_float_implementation none), so the SDK's float clkdiv call
 * would not link, let alone run. */
static void pcm_set_rate(uint32_t rate)
{
    uint32_t sys = clock_get_hz(clk_sys);
    uint32_t div256 = (uint32_t)(((uint64_t)sys * 256) / (rate * 64));

    pio_sm_set_clkdiv_int_frac(snd_pio, snd_sm, div256 >> 8, div256 & 0xFF);
    pio_sm_clkdiv_restart(snd_pio, snd_sm);
}

/* n bytes out of the ring, in at most two spans. */
static void __not_in_flash_func(pcm_take)(void *dst, uint32_t n)
{
    uint32_t off = pcm_tail % PCM_RING_BYTES;
    uint32_t first = PCM_RING_BYTES - off;

    if (first > n)
        first = n;
    memcpy(dst, pcm_ring + off, first);
    if (n > first)
        memcpy((uint8_t *)dst + first, pcm_ring, n - first);
    pcm_tail += n;
}

static void __not_in_flash_func(pcm_fill)(int16_t *buf)
{
    uint32_t used = pcm_head - pcm_tail;
    uint32_t framesz = (pcm_channels == 2) ? 4 : 2;
    uint32_t need = SND_NBUF * framesz;
    uint32_t got = (used < need) ? used : need;
    int i, frames;

    /*
     * A SHORT buffer must still be consumed, and the first version of
     * this did not do that: it filled with silence and left the data
     * where it was, so the last few milliseconds of every stream - less
     * than one 1K half-buffer - could never drain.  A player waiting
     * for the queue to empty before closing then waited forever, which
     * is exactly what pcmtest did.
     *
     * So take whatever is there, pad the rest with silence, and only
     * call it an underrun when there was nothing at all to play: that
     * is when the hardware actually emitted a gap.  A run that is
     * merely short in its final block is the normal end of a stream.
     */
    got -= got % framesz;               /* never split a frame */
    if (got < need)
        memset(buf, 0, SND_NBUF * 2 * sizeof(int16_t));
    if (got == 0) {
        /* Not an underrun before the stream has started.  The DMA is
         * consuming from the moment OPEN returns, while the player is
         * still generating or decoding its first block, so an empty
         * ring at that point means "not begun" and not "starved" -
         * counting it reported six every run and made the number
         * useless for the thing it exists to measure. */
        if (pcm_started)
            pcm_underruns++;
        return;
    }
    pcm_started = 1;
    frames = (int)(got / framesz);

    if (pcm_channels == 2) {
        pcm_take(buf, got);
        return;
    }

    /* Mono: duplicate into both channels here rather than in the
     * player, so a mono file costs the decoder nothing and halves the
     * ring traffic (machine_i2s.c does the same).  Expanded in place,
     * so it needs no second buffer - an IRQ has no stack to spare for
     * one.
     *
     * The samples MUST land in the LOWER half and be expanded
     * backwards.  Reading buf[i] and writing buf[2i] and buf[2i+1] is
     * safe because every write from a later iteration is at 2i+2 or
     * above, which is past i for any i.  The first version of this put
     * them in the TOP half and read buf[SND_NBUF + i], where that
     * inequality does not hold: i = 255 writes buf[510] and buf[511],
     * which is precisely what i = 254 then reads.  Every sample but
     * the first was garbage, and it sounded like it. */
    pcm_take(buf, got);
    for (i = frames - 1; i >= 0; i--) {
        int16_t v = buf[i];
        buf[i * 2] = v;
        buf[i * 2 + 1] = v;
    }
}

/* Hand the state machine back to the synth. */
static void pcm_release(void)
{
    pcm_active = 0;
    pcm_owner = 0;
    pcm_head = pcm_tail = 0;
    pcm_set_rate(SND_RATE);
}

/* Release a stream whose owner is gone.  A ZOMBIE counts as gone: it
 * has exited, it is only waiting to be reaped, and it will not be
 * writing any more samples.  Without this a player killed with SIGKILL
 * - or one that faulted - would lock the audio device out for everyone
 * until the machine was rebooted, and lock out SOUND with it. */
static void pcm_reap(void)
{
    ptptr p;

    if (!pcm_active)
        return;
    for (p = ptab; p < ptab_end; ++p)
        if (p->p_pid == pcm_owner && p->p_status != P_EMPTY &&
            p->p_status != P_ZOMBIE)
            return;
    pcm_release();
}

/* Take the state machine for a stream at this rate.  The BBC synth's
 * note tables assume SND_RATE, so the two are mutually exclusive -
 * which is what MMBasic does too.
 *
 * Returns -2 when another process is playing.  Reopening one's own
 * stream is allowed and restarts it, which is what a player that
 * changes sample rate mid-file would need. */
int sound_pcm_open(uint32_t rate, int channels, uint16_t owner)
{
    if (rate < 8000 || rate > 48000 || (channels != 1 && channels != 2))
        return -1;
    pcm_reap();
    if (pcm_active && pcm_owner != owner)
        return -2;
    if (pcm_ring == NULL) {
        pcm_ring = malloc(PCM_RING_BYTES);
        if (pcm_ring == NULL)
            return -1;
    }
    sound_quiet();
    pcm_head = pcm_tail = 0;
    pcm_underruns = 0;
    pcm_started = 0;
    pcm_channels = (uint8_t)channels;
    pcm_set_rate(rate);
    pcm_owner = owner;
    pcm_active = 1;             /* last: the IRQ reads this */
    return 0;
}

/* Who is playing, or 0.  The one thing a program outside the player can
 * usefully ask: BASIC's PLAY STOP signals this pid, and PLAY MP3
 * refuses to start when it is not zero. */
uint16_t sound_pcm_owner(void)
{
    pcm_reap();
    return pcm_active ? pcm_owner : 0;
}

/* Copy from the caller into the ring, as much as fits.  Returns the
 * number of bytes taken, which the caller must honour - a short write
 * means the ring is full and the player should come back later, not
 * that anything is wrong. */
int sound_pcm_write(const uint8_t *ubuf, uint32_t len, uint16_t owner)
{
    uint32_t used, space, off, first;

    if (!pcm_active || pcm_owner != owner)
        return -1;
    used = pcm_head - pcm_tail;
    space = PCM_RING_BYTES - used;
    if (len > space)
        len = space;
    /* Never accept a partial frame.  Everything downstream assumes
     * head - tail is a whole number of frames, and a single odd byte
     * would swap the channels for the rest of the stream. */
    len -= len % (uint32_t)((pcm_channels == 2) ? 4 : 2);
    if (len == 0)
        return 0;

    off = pcm_head % PCM_RING_BYTES;
    first = PCM_RING_BYTES - off;
    if (first > len)
        first = len;
    if (uget((void *)ubuf, pcm_ring + off, first))
        return -1;
    if (len > first && uget((void *)(ubuf + first), pcm_ring, len - first))
        return -1;
    pcm_head += len;
    return (int)len;
}

void sound_pcm_stat(uint32_t *space, uint32_t *queued, uint32_t *under)
{
    uint32_t used = pcm_head - pcm_tail;

    *space = pcm_active ? PCM_RING_BYTES - used : 0;
    *queued = used;
    *under = pcm_underruns;
}

/* Stops at once and drops whatever is still queued.  A player that
 * wants the tail played out polls sound_pcm_stat until nothing is
 * queued and closes then.
 *
 * Ignored from anyone but the owner, which is the other half of the
 * lock: a stale close - a second player exiting after being refused -
 * must not silence the process that legitimately holds the stream. */
void sound_pcm_close(uint16_t owner)
{
    if (!pcm_active || pcm_owner != owner)
        return;
    pcm_release();
}

/* --- DMA plumbing --------------------------------------------------------- */

static void __not_in_flash_func(snd_dma_irq)(void)
{
    if (dma_hw->ints0 & (1u << dmach_a)) {
        dma_hw->ints0 = 1u << dmach_a;
        dma_channel_set_read_addr(dmach_a, sndbuf[0], false);
        dma_channel_set_trans_count(dmach_a, SND_NBUF, false);
        if (pcm_active)
            pcm_fill(sndbuf[0]);
        else
            snd_fill(sndbuf[0]);
    }
    if (dma_hw->ints0 & (1u << dmach_b)) {
        dma_hw->ints0 = 1u << dmach_b;
        dma_channel_set_read_addr(dmach_b, sndbuf[1], false);
        dma_channel_set_trans_count(dmach_b, SND_NBUF, false);
        if (pcm_active)
            pcm_fill(sndbuf[1]);
        else
            snd_fill(sndbuf[1]);
    }
}

/* --- public API ----------------------------------------------------------- */

int sound_cmd(uint16_t chan, int16_t amp, uint16_t pitch, uint16_t dur)
{
    int cn = chan & 3;
    struct schan *c = &ch[cn];
    struct note n;
    irqflags_t irq;

    /* A player that died without closing would otherwise leave the
     * synth muted for good - the IRQ would go on filling from an empty
     * PCM ring.  Nothing else here cares about the stream: while a live
     * player holds it, SOUND queues notes that are heard when it ends,
     * which is what this did before there was a lock at all. */
    pcm_reap();

    if (chan & 0x10) {                  /* flush */
        irq = di();
        c->qr = c->qw;
        c->active = 0;
        irqrestore(irq);
    }

    if (amp > 0)
        n.amp = 0x80 | (amp > 16 ? 16 : amp);
    else
        n.amp = (-amp) > 15 ? 15 : -amp;
    n.pitch = pitch & 0xFF;
    n.dur = dur > 255 ? 255 : dur;
    n.sync = (chan >> 8) & 3;

    irq = di();
    if ((uint8_t)(c->qw - c->qr) >= SND_QLEN) {
        irqrestore(irq);
        return -1;                      /* queue full: EAGAIN */
    }
    c->q[c->qw % SND_QLEN] = n;
    c->qw++;
    try_dequeue();
    irqrestore(irq);
    return 0;
}

void sound_envelope(const uint8_t *e)
{
    int n = e[0];
    if (n < 1 || n > 16)
        return;
    memcpy(envs[n], e + 1, 13);
}

int sound_qfree(int cn)
{
    struct schan *c = &ch[cn & 3];
    return SND_QLEN - (uint8_t)(c->qw - c->qr);
}

void sound_quiet(void)
{
    int i;
    irqflags_t irq = di();
    for (i = 0; i < 4; i++) {
        ch[i].qr = ch[i].qw;
        ch[i].active = 0;
        ch[i].level = 0;
    }
    irqrestore(irq);
}

void sound_init(void)
{
    pio_program_t prog;
    uint off;

    memset(&prog, 0, sizeof(prog));
    prog.instructions = i2s_prog;
    prog.length = 8;
    prog.origin = -1;

    snd_pio = pio1;
    snd_sm = pio_claim_unused_sm(snd_pio, true);
    off = pio_add_program(snd_pio, &prog);

    pio_gpio_init(snd_pio, SND_BCLK);
    pio_gpio_init(snd_pio, SND_BCLK + 1);
    pio_gpio_init(snd_pio, SND_DATA);

    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_out_pins(&c, SND_DATA, 1);
    sm_config_set_sideset(&c, 2, false, false);
    sm_config_set_sideset_pins(&c, SND_BCLK);
    sm_config_set_out_shift(&c, false, true, 32);   /* MSB first, autopull */
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    sm_config_set_wrap(&c, off, off + 7);
    {
        /* SM clock = 64 x sample rate */
        uint32_t sys = clock_get_hz(clk_sys);
        uint32_t target = SND_RATE * 64;
        uint32_t div256 = ((uint64_t)sys * 256) / target;
        sm_config_set_clkdiv_int_frac(&c, div256 >> 8, div256 & 0xFF);
    }
    pio_sm_set_consecutive_pindirs(snd_pio, snd_sm, SND_BCLK, 2, true);
    pio_sm_set_consecutive_pindirs(snd_pio, snd_sm, SND_DATA, 1, true);
    pio_sm_init(snd_pio, snd_sm, off, &c);

    /* silence in both buffers to start */
    memset(sndbuf, 0, sizeof(sndbuf));

    dmach_a = dma_claim_unused_channel(true);
    dmach_b = dma_claim_unused_channel(true);

    dma_channel_config dc = dma_channel_get_default_config(dmach_a);
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
    channel_config_set_dreq(&dc, pio_get_dreq(snd_pio, snd_sm, true));
    channel_config_set_chain_to(&dc, dmach_b);
    dma_channel_configure(dmach_a, &dc, &snd_pio->txf[snd_sm],
        sndbuf[0], SND_NBUF, false);

    dc = dma_channel_get_default_config(dmach_b);
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
    channel_config_set_dreq(&dc, pio_get_dreq(snd_pio, snd_sm, true));
    channel_config_set_chain_to(&dc, dmach_a);
    dma_channel_configure(dmach_b, &dc, &snd_pio->txf[snd_sm],
        sndbuf[1], SND_NBUF, false);

    dma_hw->ints0 = (1u << dmach_a) | (1u << dmach_b);
    dma_hw->inte0 |= (1u << dmach_a) | (1u << dmach_b);
    irq_set_exclusive_handler(DMA_IRQ_0, snd_dma_irq);
    irq_set_enabled(DMA_IRQ_0, true);   /* core0 */

    dma_channel_start(dmach_a);
    pio_sm_set_enabled(snd_pio, snd_sm, true);

    kputs("sound: BBC 4-channel synth on I2S (PCM5102)\n");
}
