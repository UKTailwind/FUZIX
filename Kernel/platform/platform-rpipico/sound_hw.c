/*
 * sound_hw.c - the Pico Computer 3's audio output stage.
 *
 * PIO1 runs the standard 16-bit stereo I2S program (2 clocks per bit;
 * SM clock = 64 x sample rate) to the PCM5102 DAC (BCLK GP10, LRCLK
 * GP11, DATA GP22); two chained DMA channels ping-pong 64-frame buffers
 * and the completion IRQ (DMA_IRQ_0, core0) asks sound.c to refill the
 * half just freed.  Mixing costs well under 0.1% of a core.  The DMA
 * does not care which of sound.c's three sources filled the buffer -
 * the BBC synth, the PCM ring or the MMBasic synth - which is what made
 * the output stage worth building this way rather than MMBasic's, and
 * what lets a PC play the same sound.c through a sound card.
 *
 * Also here, because they are the kernel's and not the synth's: the
 * copy from a process's memory into the ring, the process table scan
 * that hands a dead owner's stream back, and the sleep behind
 * SNDIOC_PCMWAIT with its scheduler poke.  sound_priv.h is the contract.
 */

#include <kernel.h>
#include <kdata.h>
#include <printf.h>
#include "picosdk.h"
#include "config.h"
#include "sound.h"
#include "sound_priv.h"

#include <hardware/pio.h>
#include <hardware/dma.h>
#include <hardware/clocks.h>

#define SND_BCLK  10            /* LRCLK is BCLK+1 = GP11 */
#define SND_DATA  22

/* MicroPython machine_i2s 16-bit stereo write program (2 side-set
 * bits: bit0 BCLK, bit1 LRCLK; MSB first, autopull 32). */
static uint16_t i2s_prog[8] = {
    59438, 24577, 2113, 28673, 63534, 28673, 6213, 24577
};

static int16_t sndbuf[2][SND_NBUF * 2];
static int dmach_a = -1, dmach_b = -1;
static PIO snd_pio;
static uint snd_sm;

/* Set the state machine clock for a sample rate.  Two samples a frame,
 * sixteen bits each, two PIO instructions a bit = 64 clocks a frame.
 * Integer only: the kernel is built with float trapped
 * (pico_set_float_implementation none), so the SDK's float clkdiv call
 * would not link, let alone run. */
void snd_hw_rate(uint32_t rate)
{
    uint32_t sys = clock_get_hz(clk_sys);
    uint32_t div256 = (uint32_t)(((uint64_t)sys * 256) / (rate * 64));

    pio_sm_set_clkdiv_int_frac(snd_pio, snd_sm, div256 >> 8, div256 & 0xFF);
    pio_sm_clkdiv_restart(snd_pio, snd_sm);
}

/* sound_pcm_write copies from the caller's buffer in place, so the
 * buffer is validated here and not copied twice. */
int snd_hw_copyin(void *dst, const void *src, uint32_t n)
{
    return uget((void *)src, dst, n);
}

/* A ZOMBIE counts as gone: it has exited, it is only waiting to be
 * reaped, and it will not be writing any more samples.  Without this a
 * player killed with SIGKILL - or one that faulted - would lock the
 * audio device out for everyone until the machine was rebooted, and
 * lock out SOUND with it. */
int snd_hw_pid_alive(uint16_t pid)
{
    ptptr p;

    for (p = ptab; p < ptab_end; ++p)
        if (p->p_pid == pid && p->p_status != P_EMPTY &&
            p->p_status != P_ZOMBIE)
            return 1;
    return 0;
}

/*
 * Waiting for room, without a player having to guess how long.
 *
 * The DMA IRQ is where the ring actually drains, but it is a raw SDK
 * handler and not inside the kernel's interrupt discipline - waking
 * the scheduler from there could catch the process table mid-update.
 * The TICK is the proper context and is 5ms (TICKSPERSEC 200), which
 * is twenty times finer than the decisecond floor usleep() imposes on
 * userland, and that is the whole point: the queue no longer has to be
 * deep enough to cover a 100ms sleep, so it stops being latency.
 *
 * One waiter, because there is one PCM stream and one owner.
 */
static volatile uint32_t pcm_waitmark;
static volatile uint8_t pcm_waiting;

void sound_pcm_tick(void)
{
    uint32_t queued;

    if (!pcm_waiting)
        return;
    if (sound_pcm_queued(0, &queued) < 0 || queued <= pcm_waitmark) {
        pcm_waiting = 0;
        wakeup((char *)&pcm_waiting);
        /*
         * AND LET IT RUN.  Waking it is not enough: MAXTICKS is
         * TICKSPERSEC/2, so a timeslice here is 100 ticks - HALF A
         * SECOND - and a compute-bound program holds the processor for
         * all of it.  The player would be ready and not running while
         * its queue emptied, which is why 557ms of audio papered over
         * this and 186ms did not: 557 outlasts a 500ms timeslice.
         *
         * Winding runticks up to the current process's own limit makes
         * the next preempt check reschedule, so the player runs within
         * a tick or two of the ring needing it.  Only an audio wakeup
         * does this - it is the one thing here with a deadline - and
         * it costs the interrupted program one early context switch.
         */
        if (udata.u_ptab != NULL && runticks < udata.u_ptab->p_priority)
            runticks = udata.u_ptab->p_priority;
    }
}

int sound_pcm_wait(uint32_t mark, uint16_t owner)
{
    uint32_t queued;

    if (sound_pcm_queued(owner, &queued) < 0)
        return -1;
    if (queued <= mark)
        return 0;               /* already room: do not sleep at all */
    pcm_waitmark = mark;
    pcm_waiting = 1;
    /* psleep, not psleep_nosig: PLAY STOP is a SIGINT and must not be
       held off until the ring happens to drain. */
    psleep((char *)&pcm_waiting);
    pcm_waiting = 0;
    return 0;
}

/* --- DMA plumbing --------------------------------------------------------- */

static void __not_in_flash_func(snd_dma_irq)(void)
{
    if (dma_hw->ints0 & (1u << dmach_a)) {
        dma_hw->ints0 = 1u << dmach_a;
        dma_channel_set_read_addr(dmach_a, sndbuf[0], false);
        dma_channel_set_trans_count(dmach_a, SND_NBUF, false);
        sound_fill_block(sndbuf[0]);
    }
    if (dma_hw->ints0 & (1u << dmach_b)) {
        dma_hw->ints0 = 1u << dmach_b;
        dma_channel_set_read_addr(dmach_b, sndbuf[1], false);
        dma_channel_set_trans_count(dmach_b, SND_NBUF, false);
        sound_fill_block(sndbuf[1]);
    }
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
