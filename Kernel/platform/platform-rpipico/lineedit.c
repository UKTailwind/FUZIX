/*
 * Console line editor and command history for the Pico Computers.
 *
 * Sits between the console input poll and tty_inproc: while the
 * console tty (minor 1) is in canonical mode with echo - the shell
 * prompt, login, any cooked-mode program - input is collected and
 * edited here, and only a finished line is handed to the kernel line
 * discipline.  Raw-mode programs (BBC BASIC, editors) see every byte
 * untouched, because the gate is the termios state itself.
 *
 * Keys: left/right/home/end/backspace/delete edit within the line,
 * up/down walk the history, ^U kills the line, ^A/^E are home/end.
 * ^C and friends pass straight through to the signal machinery.
 *
 * All state - the edit buffer and a 32K history ring - lives in a
 * 64K region reserved at the top of the PSRAM (see devpsram.c), so
 * the feature costs no SRAM and no process memory.  History survives
 * warm resets (magic-checked) but not power-off, like the RAM disc.
 * Swap is unaffected in practice: slots are 256K and only 31 fit the
 * remaining 8128K anyway, one more than the process table can use.
 *
 * Echo uses only characters, backspaces and CR/LF - never absolute
 * cursor addressing - so the HDMI console and the mirrored serial
 * terminal stay in step no matter how their scroll histories differ.
 */

#include <kernel.h>
#include <kdata.h>
#include <tty.h>
#include "picosdk.h"
#include "psram.h"
#include "globals.h"

extern void tty_putc(uint_fast8_t minor, uint_fast8_t c);
extern ttyready_t tty_writeready(uint_fast8_t minor);

#define LINE_MAX   128          /* < TTYSIZ (132): the whole line plus
                                   its newline must fit the canon queue */
#define HIST_SIZE  32768u
#define LE_MAGIC   0x50433345u  /* "PC3E" */

struct le_state {
    uint32_t magic;
    uint32_t head;              /* next write offset into ring */
    uint8_t  line[LINE_MAX];    /* line being edited */
    uint8_t  stash[LINE_MAX];   /* line stashed while browsing history */
    uint16_t len, pos;          /* line length, cursor position */
    uint16_t slen;              /* stashed length */
    uint16_t browse;            /* 0 = live line, N = N entries back */
    uint8_t  esc;               /* escape parser state */
    uint8_t  parm;              /* collected CSI parameter */
    uint8_t  ring[HIST_SIZE];   /* [len][bytes][len] records, circular */
};

static struct le_state *le;     /* NULL: no PSRAM, editor disabled */

void lineedit_init(void)
{
    if (psram_size < PSRAM_RESERVE)
        return;
    le = (struct le_state *)(PSRAM_BASE + psram_size - PSRAM_RESERVE);
    if (le->magic != LE_MAGIC) {
        memset(le, 0, sizeof(struct le_state));
        le->magic = LE_MAGIC;
        le->browse = 0;
    }
    /* volatile per-boot state: never trust it across a reset */
    le->len = le->pos = le->slen = 0;
    le->browse = 0;
    le->esc = 0;
}

/* ------------------------------------------------------------------ */
/* echo helpers: everything relative, mirrored to both displays       */

static void put(uint_fast8_t c)
{
    /*
     * Every lineedit path runs inside tty_interrupt(), which timer_tick_cb
     * calls with di() held (PRIMASK set).  The old spin on tty_writeready()
     * deadlocked core0 there: once the 256-byte tx ring fills, its only
     * drainers are the tx interrupt (masked, so it can never run) and
     * rawuart_tx_poll() (a LATER step of the very tick now stuck spinning).
     * A redraw burst larger than the ring - a down-arrow off a long history
     * line emits ~300 rubout bytes in one tick - therefore hung the machine
     * for good: CORE0 STALLED phase=2, with the tx IRQ enabled+pending and
     * PRIMASK=1 in the [u0 ...] report.
     *
     * rawuart_putc already handles a full ring correctly in BOTH contexts:
     * in thread context it waits on the tx interrupt, and when masked it
     * self-pumps the ring into the FIFO itself ("be the interrupt
     * ourselves").  So drive it directly and let it do the waiting - the
     * worst case degrades to a burst clocked out at wire speed, never a
     * lockup.  This is the arrangement MMBasic uses: drop/pace, never wedge.
     */
    tty_putc(1, c);
}

static void rubout(void)
{
    put('\b');
    put(' ');
    put('\b');
}

/* reprint buf[pos..len) and return the cursor to pos */
static void tail_redraw(int extra_blank)
{
    int i;
    for (i = le->pos; i < le->len; i++)
        put(le->line[i]);
    if (extra_blank) {
        put(' ');
        put('\b');
    }
    for (i = le->pos; i < le->len; i++)
        put('\b');
}

/* wipe the displayed line entirely (cursor anywhere within it) */
static void erase_line(void)
{
    int i;
    for (i = le->pos; i < le->len; i++)
        put(le->line[i]);
    for (i = 0; i < le->len; i++)
        rubout();
    le->len = 0;
    le->pos = 0;
}

/* ------------------------------------------------------------------ */
/* history ring                                                       */

static uint8_t ring_at(uint32_t off)
{
    return le->ring[off % HIST_SIZE];
}

static void ring_put(uint32_t off, uint8_t c)
{
    le->ring[off % HIST_SIZE] = c;
}

static void hist_append(void)
{
    uint32_t off = le->head;
    int i;
    uint8_t n = le->len;

    if (n == 0)
        return;
    ring_put(off++, n);
    for (i = 0; i < n; i++)
        ring_put(off++, le->line[i]);
    ring_put(off++, n);
    le->head = off % HIST_SIZE;
    ring_put(le->head, 0);      /* sentinel: end of valid history */
}

/* Locate the entry `back` entries before head (back >= 1).  Returns
 * the offset of its length header, or NOTFOUND.  Bidirectional
 * records allow the backward walk; it ends at the sentinel or on any
 * inconsistency (old data overwritten by the circular writer). */
#define NOTFOUND 0xFFFFFFFFu

static uint32_t hist_locate(int back, uint8_t *plen)
{
    uint32_t off = le->head;
    uint32_t walked = 0;
    uint8_t n;

    while (back-- > 0) {
        n = ring_at(off ? off - 1 : HIST_SIZE - 1);  /* trailing length */
        if (n == 0 || n > LINE_MAX)
            return NOTFOUND;
        walked += n + 2u;
        if (walked > HIST_SIZE - 1)
            return NOTFOUND;    /* wrapped past our own tail */
        off = (off + HIST_SIZE - 2 - n) % HIST_SIZE;
        if (ring_at(off) != n)
            return NOTFOUND;    /* header/trailer mismatch: stale data */
        if (back == 0) {
            *plen = n;
            return off;
        }
    }
    return NOTFOUND;
}

/* load a located entry into the (already erased) edit buffer */
static void hist_load(uint32_t off, uint8_t n)
{
    int i;

    for (i = 0; i < n; i++)
        le->line[i] = ring_at(off + 1 + i);
    le->len = n;
    le->pos = n;
}

static void show_line(void)
{
    int i;

    for (i = 0; i < le->len; i++)
        put(le->line[i]);
}

/* ------------------------------------------------------------------ */
/* the editor                                                         */

static void insert_char(uint_fast8_t c)
{
    int i;

    if (le->len >= LINE_MAX)
        return;
    for (i = le->len; i > le->pos; i--)
        le->line[i] = le->line[i - 1];
    le->line[le->pos] = c;
    le->len++;
    put(c);
    le->pos++;
    tail_redraw(0);
}

static void delete_left(void)
{
    int i;

    if (le->pos == 0)
        return;
    for (i = le->pos - 1; i < le->len - 1; i++)
        le->line[i] = le->line[i + 1];
    le->len--;
    le->pos--;
    put('\b');
    tail_redraw(1);
}

static void delete_at(void)
{
    int i;

    if (le->pos >= le->len)
        return;
    for (i = le->pos; i < le->len - 1; i++)
        le->line[i] = le->line[i + 1];
    le->len--;
    tail_redraw(1);
}

static void cursor_home(void)
{
    while (le->pos > 0) {
        put('\b');
        le->pos--;
    }
}

static void cursor_end(void)
{
    while (le->pos < le->len) {
        put(le->line[le->pos]);
        le->pos++;
    }
}

/* browse: 0 = the live line, N = N entries back in the history */
static void browse_step(int dir)
{
    uint32_t off;
    uint8_t n;

    if (dir > 0) {
        off = hist_locate(le->browse + 1, &n);
        if (off == NOTFOUND)
            return;             /* no further back to go */
        if (le->browse == 0) {  /* leaving the live line: stash it */
            memcpy(le->stash, le->line, le->len);
            le->slen = le->len;
        }
        erase_line();
        le->browse++;
        hist_load(off, n);
    } else {
        if (le->browse == 0)
            return;
        le->browse--;
        erase_line();
        if (le->browse == 0) {
            memcpy(le->line, le->stash, le->slen);
            le->len = le->slen;
            le->pos = le->slen;
        } else {
            off = hist_locate(le->browse, &n);
            if (off != NOTFOUND)
                hist_load(off, n);
        }
    }
    show_line();
}

/* hand the finished line to the kernel line discipline */
static void accept_line(uint_fast8_t terminator)
{
    struct tty *t = &ttydata[1];
    tcflag_t saved = t->termios.c_lflag;
    int i;

    hist_append();
    le->browse = 0;

    /* the line is already on screen: feed it without a second echo */
    t->termios.c_lflag &= ~(ECHO | ECHOE | ECHOK);
    for (i = 0; i < le->len; i++)
        tty_inproc(1, le->line[i]);
    t->termios.c_lflag = saved;
    tty_inproc(1, terminator);  /* echoes the newline itself */

    le->len = 0;
    le->pos = 0;
}

/* returns 1 if the byte was consumed, 0 if the caller should pass it
 * to tty_inproc as usual */
int lineedit_input(uint_fast8_t minor, uint_fast8_t c)
{
    struct tty *t;

    if (minor != 1 || !le)
        return 0;
    t = &ttydata[1];
    if ((t->termios.c_lflag & (ICANON | ECHO)) != (ICANON | ECHO))
        return 0;

    /* escape sequence collection */
    if (le->esc == 1) {
        if (c == '[' || c == 'O') {
            le->esc = 2;
            le->parm = 0;
            return 1;
        }
        le->esc = 0;            /* lone ESC + something: drop both */
        return 1;
    }
    if (le->esc == 2) {
        if (c >= '0' && c <= '9') {
            le->parm = le->parm * 10 + (c - '0');
            return 1;
        }
        if (c == ';') {         /* multi-parameter: not ours, swallow */
            le->parm = 0;
            return 1;
        }
        le->esc = 0;
        switch (c) {
        case 'A': browse_step(1);  return 1;    /* up */
        case 'B': browse_step(-1); return 1;    /* down */
        case 'C': if (le->pos < le->len) { put(le->line[le->pos]); le->pos++; } return 1;
        case 'D': if (le->pos > 0) { put('\b'); le->pos--; } return 1;
        case 'H': cursor_home(); return 1;
        case 'F': cursor_end();  return 1;
        case '~':
            switch (le->parm) {
            case 1: case 7: cursor_home(); return 1;
            case 4: case 8: cursor_end();  return 1;
            case 3: delete_at(); return 1;
            }
            return 1;           /* other keypad keys: ignore */
        }
        return 1;               /* unknown final byte: swallow */
    }

    switch (c) {
    case 0x1B:
        le->esc = 1;
        return 1;
    case '\r':
    case '\n':
        accept_line(c);
        return 1;
    case 0x08:
    case 0x7F:
        delete_left();
        return 1;
    case 0x15:                  /* ^U: kill line */
        cursor_end();
        while (le->len)
            { rubout(); le->len--; }
        le->pos = 0;
        le->browse = 0;
        return 1;
    case 0x01:                  /* ^A */
        cursor_home();
        return 1;
    case 0x05:                  /* ^E */
        cursor_end();
        return 1;
    case 0x04:                  /* ^D: EOF only on an empty line */
        if (le->len == 0)
            return 0;
        delete_at();
        return 1;
    case 0x03:                  /* ^C and friends: the line dies with */
    case 0x1A:                  /* the read - reset and pass through  */
        le->len = 0;
        le->pos = 0;
        le->browse = 0;
        return 0;
    }

    if (c >= 0x20 || c == '\t') {
        insert_char(c);
        return 1;
    }
    return 0;                   /* other control bytes: default path */
}
