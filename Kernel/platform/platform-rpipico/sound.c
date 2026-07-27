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
 */

#include <kernel.h>
#include <kdata.h>
#include <printf.h>
#include "picosdk.h"
#include "config.h"
#include "sound.h"

#include <hardware/pio.h>
#include <hardware/dma.h>
#include <hardware/clocks.h>

#define SND_BCLK  10            /* LRCLK is BCLK+1 = GP11 */
#define SND_DATA  22
#define SND_RATE  22050
#define SND_NBUF  256           /* samples per half-buffer */
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
                c->phase += c->inc;
                mix += (c->phase & 0x80000000u) ? c->level : -c->level;
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

/* --- DMA plumbing --------------------------------------------------------- */

static void __not_in_flash_func(snd_dma_irq)(void)
{
    if (dma_hw->ints0 & (1u << dmach_a)) {
        dma_hw->ints0 = 1u << dmach_a;
        dma_channel_set_read_addr(dmach_a, sndbuf[0], false);
        dma_channel_set_trans_count(dmach_a, SND_NBUF, false);
        snd_fill(sndbuf[0]);
    }
    if (dma_hw->ints0 & (1u << dmach_b)) {
        dma_hw->ints0 = 1u << dmach_b;
        dma_channel_set_read_addr(dmach_b, sndbuf[1], false);
        dma_channel_set_trans_count(dmach_b, SND_NBUF, false);
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
