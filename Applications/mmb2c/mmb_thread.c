/* ------------------------------------------------------------------
 * mmb_thread.c  -  MMBasic V8's threads, for translated programs.
 *
 * Included at the end of mmb_runtime.c (one translation unit: it swaps
 * the runtime's own statics).  PLAN-threads.md is the design; the
 * Threads User Manual (CMM2 and PicoMite, November 2026) is what it
 * does.
 *
 * One thread runs at a time, and the switch is made only between
 * statements - at the poll every statement already ends in, every few
 * statements (mmb_int.h, MMX_EVERY) - or inside a wait.  Each thread
 * has a C stack of its own, so a wait in the runtime (PAUSE, and the
 * THREAD waits) can let the others run from the middle of its own loop
 * and carry on when its turn comes back.  Under bcrun each also has a
 * VM stack of its own, where its BASIC locals live, and the VM's
 * registers are swapped with the rest.
 *
 * What a thread has of its own (the manual's chapter 4) and where:
 *   - LOCALs, parameters, loops, SUB and FUNCTION calls: C locals and
 *     C control flow, on its own stack - nothing to do here;
 *   - the string and by-reference temporaries (mm_pool, mm_byref_pool):
 *     stacks wound back by mm_mark/mm_release, so shared they would let
 *     one thread release another's - a pool each;
 *   - ON ERROR, MM.ERRNO, MM.ERRMSG$ (mm_est, mm_errno_v, mm_errmsg_v);
 *   - what the program's own headers bind (mm_thr_bind): the DATA
 *     pointer, GOSUB's returns.
 *
 * The context switch: ucontext on Linux, Fibers on Windows.  The board
 * has neither and has no threads yet: THREAD START says so.
 * ------------------------------------------------------------------ */

#if defined(_WIN32)
#define MMX_FIBER 1
#elif defined(__linux__) && !defined(__FUZIX__)     && (!defined(MM_FCC) || defined(MM_HOSTED))
#define MMX_UCTX 1          /* <ucontext.h>: at the top of mmb_runtime.c */
#endif

#if defined(MMX_FIBER) || defined(MMX_UCTX)
#define MMX_ON 1
#include <poll.h>           /* the Windows shim's, there */
#endif

#ifdef MMX_ON

#if defined(MMX_FIBER) && !defined(_WINDOWS_)
/* Declared rather than taken from windows.h, whose macros (RGB, ERROR
 * and the rest) this file's neighbours would collide with - unless
 * something before it has brought windows.h in already, as pc3host's
 * client header does in bcrun. */
__declspec(dllimport) int __stdcall QueryPerformanceCounter(long long *c);
__declspec(dllimport) int __stdcall QueryPerformanceFrequency(long long *f);
__declspec(dllimport) void *__stdcall ConvertThreadToFiber(void *p);
__declspec(dllimport) void *__stdcall CreateFiber(size_t size,
                                                  void (__stdcall *fn)(void *),
                                                  void *p);
__declspec(dllimport) void __stdcall SwitchToFiber(void *f);
__declspec(dllimport) void __stdcall DeleteFiber(void *f);
#endif

#define MMX_N       MM_THR_MAX      /* threads 1 to 16; 0 is the main program */
#define MMX_MAXARG  16
#define MMX_LOCKS   16
#define MMX_FREE    0
#define MMX_READY   1               /* running, or waiting for its turn */
#define MMX_WAIT    2               /* in mm_thr_block */
#define MMX_ZOMBIE  3               /* ended: its stacks go when the next runs */

/* A thread's string temporaries: as many as the main program's.  Its
 * VM stack (under bcrun), where its BASIC locals are: 64K on a PC, as
 * a PC has room; 8K elsewhere, the main program's STACKROOM.  Its C
 * stack holds only the interpreter and the runtime under bcrun, and the
 * whole program in a native build. */
#define MMX_TMPN    MM_TMPN
#ifdef PC_HOST
#define MMX_VSTACK  (64L * 1024)
#else
#define MMX_VSTACK  (8L * 1024)
#endif
#ifdef MMX_HOST_VM
#define MMX_CSTACK  (256L * 1024)
#else
#define MMX_CSTACK  (1024L * 1024)
#endif
#define MMX_PAINT   0xA5            /* a VM stack's unused bytes, for THREAD LIST */
#define MMX_NEVER   ((MMINTEGER)0x7FFFFFFFFFFFFFFFLL)

/* What a thread has of its own on the screen (the manual's chapter 4):
 * its colours, font, the cursor PRINT and TEXT leave (CurrentX and Y,
 * MM.INFO(HPOS) and VPOS), PRINT @'s mode, and where its drawing goes
 * (FRAMEBUFFER WRITE).  A new thread starts with its starter's, and
 * MODE gives every thread the new mode's.  The console's own cursor
 * is the terminal's and stays one: two threads PRINTing to it share
 * it, as two threads share a PicoMite's serial console. */
struct mmx_disp {
    MMINTEGER fg, bg;
    int gx, gy, fbw;
#if defined(MM_FCC) || defined(__FUZIX__) || defined(MM_PC3)
    int font, cw, ch, scale, pmode;
#else
    int font, scale, cw, ch;
#endif
};

struct mmx {
    int state, paused, prio;
    int wkind, warg, woke;          /* what it waits for, and whether it came */
    int intpend;                    /* an interrupt of its own is waiting for it */
    MMINTEGER until;                /* the time limit of its wait, us; 0 none */
    void (*entry)(void);
#ifdef MMX_UCTX
    ucontext_t uc;
#endif
#ifdef MMX_FIBER
    void *fiber;
#endif
    void *cstack;
    /* the runtime's state while another thread runs */
    char (*pool)[MM_STRSZ];
    unsigned pool_top;
    union mm_byref_u *byref;
    unsigned byref_top;
    int errno_v;
    char errmsg[MM_ERRMSG + 2];
    int est[4];
    unsigned char *bound;           /* the program's bound state */
    struct mmx_disp disp;
    char *vblock;                   /* by-ref slots, pool, arguments, VM stack */
#ifdef MMX_HOST_VM
    struct mmx_vmregs vm;
    char *vstack;
    unsigned long vsize;
#endif
    void **lf;                      /* its LOCAL frames, newest last */
    int nlf, alf;
    int nargs;
    MMINTEGER ai[MMX_MAXARG];
    MMFLOAT af[MMX_MAXARG];
    char *as[MMX_MAXARG];
};

static struct mmx mmx_tab[MMX_N + 1];
static int mmx_booted;
static int mmx_live;                /* threads 1 to 16 alive */
static int mmx_zombie = -1;
static int mmx_hold;                /* THREAD HOLD's depth (the holder runs) */
static int mmx_inint;               /* an interrupt routine is running */
static int mmx_lock[MMX_LOCKS + 1]; /* lock k's thread + 1; 0 free */
static MMINTEGER mmx_slice_us = 1000;
static MMINTEGER mmx_turn_end;
static MMINTEGER mmx_next_wake;     /* the soonest time limit of a waiting thread */
static int mmx_rr;                  /* the last thread the rotation chose */
static int *mmx_idp;                /* the program's copy of mmx_cur (mmb_int.h) */
static int mmx_def_kb;              /* THREAD SIZE's, 0 for the default */
static int mmx_main_prio = 1;
static MMINTEGER *mmx_flags;        /* FLAG()'s word (mmb_misc.h, through mmb_thread.h) */

/* the program's per-thread state, bound by mmb_thread.h at the start */
#define MMX_NBIND   8
static unsigned char *mmx_bind_p[MMX_NBIND];
static int mmx_bind_n[MMX_NBIND];
static int mmx_nbind, mmx_bindsz;
static unsigned char *mmx_bind_init;

/* THREAD START as it is built: mm_thr_new, the arguments, mm_thr_go */
static int mmx_new_n, mmx_new_kb, mmx_new_prio, mmx_new_nargs, mmx_new_nstr;
static MMINTEGER mmx_new_ai[MMX_MAXARG];
static MMFLOAT mmx_new_af[MMX_MAXARG];
static char mmx_new_as[MMX_MAXARG][MM_STRSZ];
static char mmx_new_sx[MMX_MAXARG];    /* argument k is a string */

/* ---- the clock ---------------------------------------------------- */

/* The scheduler's own: a turn is a millisecond, so it is read every few
 * statements, and the runtime's mm_us_now is the PC3's clock - under
 * bcrun on a PC a round trip to the device server.  Monotonic, and only
 * ever compared with itself: a wait is handed over as a length. */
static MMINTEGER mmx_now(void)
{
#ifdef MMX_FIBER
    static long long f;
    long long c;
#ifdef _WINDOWS_
    LARGE_INTEGER x;

    if (f == 0) {
        QueryPerformanceFrequency(&x);
        f = x.QuadPart;
    }
    QueryPerformanceCounter(&x);
    c = x.QuadPart;
#else
    if (f == 0)
        QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
#endif
    return (MMINTEGER)(c / f * 1000000 + c % f * 1000000 / f);
#else
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (MMINTEGER)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
#endif
}

/* ---- memory ------------------------------------------------------- */

/* A program reaches a thread's pool, by-ref slots and arguments, and
 * under bcrun its VM stack: they come from the VM's heap there
 * (MMX_HOST_VM, bcrun_mm.c), and from malloc in a native build. */
static void *mmx_valloc(unsigned long n)
{
#ifdef MMX_HOST_VM
    return mmx_vm_alloc(n);
#else
    return malloc((size_t)n);
#endif
}

static void mmx_vfree(void *p)
{
    if (p == NULL)
        return;
#ifdef MMX_HOST_VM
    mmx_vm_free(p);
#else
    free(p);
#endif
}

/* ---- the screen ---------------------------------------------------- */

static void mmx_disp_get(struct mmx_disp *d)
{
    d->fg = mm_gfx_fg;
    d->bg = mm_gfx_bg;
    d->gx = mm_gx;
    d->gy = mm_gy;
    d->fbw = mm_fb_wr;
#if defined(MM_FCC) || defined(__FUZIX__) || defined(MM_PC3)
    d->font = mm_gfont;
    d->cw = mm_gcw;
    d->ch = mm_gch;
    d->scale = mm_gscale;
    d->pmode = mm_gpmode;
#else
    d->font = mm_hfont;
    d->scale = mm_hscale;
    d->cw = mm_hcw;
    d->ch = mm_hch;
#endif
}

static void mmx_disp_put(const struct mmx_disp *d)
{
    mm_gfx_fg = d->fg;
    mm_gfx_bg = d->bg;
    mm_gx = d->gx;
    mm_gy = d->gy;
#if defined(MM_FCC) || defined(__FUZIX__) || defined(MM_PC3)
    mm_gfont = d->font;
    mm_gcw = d->cw;
    mm_gch = d->ch;
    mm_gscale = d->scale;
    mm_gpmode = d->pmode;
#else
    mm_hfont = d->font;
    mm_hscale = d->scale;
    mm_hcw = d->cw;
    mm_hch = d->ch;
#endif
    /* the device holds the write target too: told only when it moves */
    if (d->fbw != mm_fb_wr) {
        mm_fb_sel_hw(d->fbw);
        mm_fb_wr = d->fbw;
    }
}

/* MODE has just set the running thread's: every other thread has them
 * too. */
static void mmx_mode_all(void)
{
    struct mmx_disp d;
    int i;

    if (!mmx_live)
        return;
    mmx_disp_get(&d);
    for (i = 0; i <= MMX_N; i++)
        if (i != mmx_cur && (mmx_tab[i].state == MMX_READY
                             || mmx_tab[i].state == MMX_WAIT))
            mmx_tab[i].disp = d;
}

/* ---- the switch --------------------------------------------------- */

static void mmx_save(struct mmx *t)
{
    int i, o = 0;

    /* what is queued for the screen was drawn as this thread */
    mm_gflush();
    mmx_disp_get(&t->disp);

    t->pool = mm_pool;
    t->pool_top = mm_pool_top;
    t->byref = mm_byref_pool;
    t->byref_top = mm_byref_top;
    t->errno_v = mm_errno_v;
    memcpy(t->errmsg, mm_errmsg_v, sizeof t->errmsg);
    if (mm_est)
        memcpy(t->est, mm_est, sizeof t->est);
    for (i = 0; i < mmx_nbind; i++) {
        memcpy(t->bound + o, mmx_bind_p[i], (size_t)mmx_bind_n[i]);
        o += mmx_bind_n[i];
    }
#ifdef MMX_HOST_VM
    mmx_vm_save(&t->vm);
#endif
}

static void mmx_load(struct mmx *t)
{
    int i, o = 0;

    if (mmx_idp)
        *mmx_idp = mmx_cur;
    mmx_disp_put(&t->disp);

    mm_pool = t->pool;
    mm_pool_top = t->pool_top;
    mm_byref_pool = t->byref;
    mm_byref_top = t->byref_top;
    mm_errno_v = t->errno_v;
    memcpy(mm_errmsg_v, t->errmsg, sizeof t->errmsg);
    if (mm_est)
        memcpy(mm_est, t->est, sizeof t->est);
    for (i = 0; i < mmx_nbind; i++) {
        memcpy(mmx_bind_p[i], t->bound + o, (size_t)mmx_bind_n[i]);
        o += mmx_bind_n[i];
    }
#ifdef MMX_HOST_VM
    mmx_vm_load(&t->vm);
#endif
}

/* An ended thread's blocks, freed by the thread that runs after it -
 * never from its own stack. */
static void mmx_lf_free(void *p);

static void mmx_free(struct mmx *t)
{
    while (t->nlf > 0)
        mmx_lf_free(t->lf[--t->nlf]);
    free(t->lf);
    t->lf = NULL;
    t->alf = 0;
#ifdef MMX_UCTX
    free(t->cstack);
#endif
#ifdef MMX_FIBER
    if (t->fiber)
        DeleteFiber(t->fiber);
    t->fiber = NULL;
#endif
    t->cstack = NULL;
    mmx_vfree(t->vblock);
    t->vblock = NULL;
    free(t->bound);
    t->bound = NULL;
    t->state = MMX_FREE;
}

static void mmx_reap(void)
{
    if (mmx_zombie >= 0) {
        mmx_free(&mmx_tab[mmx_zombie]);
        mmx_zombie = -1;
    }
}

static int mmx_turn(int i);

static void mmx_enter_turn(int to)
{
    mmx_turn_end = mmx_now() + mmx_slice_us * mmx_turn(to);
}

/* Run thread `to` instead of this one; returns when this one is chosen
 * again.  The chooser has loaded this thread's state by then. */
static void mmx_switch(int to)
{
    int from = mmx_cur;

    if (to == from)
        return;
    mmx_save(&mmx_tab[from]);
    mmx_cur = to;
    mmx_load(&mmx_tab[to]);
    mmx_enter_turn(to);
#ifdef MMX_UCTX
    swapcontext(&mmx_tab[from].uc, &mmx_tab[to].uc);
#endif
#ifdef MMX_FIBER
    SwitchToFiber(mmx_tab[to].fiber);
#endif
    mmx_reap();
}

/* ---- choosing ----------------------------------------------------- */

static int mmx_alive(int i)
{
    return mmx_tab[i].state == MMX_READY || mmx_tab[i].state == MMX_WAIT;
}

/* A thread's turn, in slices: its priority, or that of a thread
 * waiting for a lock it has, if that is higher - a lock lends its
 * waiter's priority to its holder. */
static int mmx_turn(int i)
{
    int p = mmx_tab[i].prio, j;

    for (j = 0; j <= MMX_N; j++)
        if (mmx_tab[j].state == MMX_WAIT && mmx_tab[j].wkind == MM_THR_LOCK
            && mmx_lock[mmx_tab[j].warg] == i + 1 && mmx_tab[j].prio > p)
            p = mmx_tab[j].prio;
    return p;
}

/* Whether thread i can run now.  A waiting thread whose lock is free
 * takes it here, so nothing can come between the test and the taking. */
static int mmx_runnable(int i, MMINTEGER now)
{
    struct mmx *t = &mmx_tab[i];

    if (t->paused)
        return 0;
    if (t->state == MMX_READY)
        return 1;
    if (t->state != MMX_WAIT)
        return 0;
    switch (t->wkind) {
    case MM_THR_LOCK:
        if (mmx_lock[t->warg] == 0) {
            mmx_lock[t->warg] = i + 1;
            t->woke = 1;
            return 1;
        }
        break;
    case MM_THR_THREAD:
        if (!mmx_alive(t->warg)) {
            t->woke = 1;
            return 1;
        }
        break;
    case MM_THR_FLAG:
        if (mmx_flags != NULL && ((*mmx_flags >> t->warg) & 1)) {
            t->woke = 1;
            return 1;
        }
        break;
    }
    if (t->until && now >= t->until) {
        t->woke = 0;
        return 1;
    }
    return 0;
}

/* The next thread to run: a thread whose PAUSE has ended first (the one
 * whose time came soonest), then the rotation: in number order after
 * the last thread it chose.  A sleeper that goes first does not move
 * the rotation on - were it to start again from the sleeper, a program
 * that PAUSEs in a loop beside a busy thread would hand the turn to that
 * same thread every time and starve the ones after it.  -1 if none can. */
static int mmx_pick(void)
{
    MMINTEGER now = mmx_now(), soon = 0;
    int i, k, best = -1;

    for (i = 0; i <= MMX_N; i++) {
        struct mmx *t = &mmx_tab[i];

        if (t->state != MMX_WAIT || t->paused || !t->until)
            continue;
        if (t->wkind == MM_THR_SLEEP && t->until <= now
            && (best < 0 || t->until < mmx_tab[best].until))
            best = i;
        if (!soon || t->until < soon)
            soon = t->until;
    }
    mmx_next_wake = soon ? soon : MMX_NEVER;
    /* a thread with an interrupt of its own waiting goes first of all,
     * out of a wait that takes interrupts (its loop takes it, and waits
     * on); a paused one keeps it until it is resumed */
    for (i = 0; i <= MMX_N; i++) {
        struct mmx *t = &mmx_tab[i];

        if (!t->intpend || t->paused || !mmx_alive(i)
            || (t->state == MMX_WAIT && t->wkind == MM_THR_IO))
            continue;
        t->intpend = 0;
        if (t->state == MMX_WAIT)
            t->woke = 0;
        return i;
    }
    if (best >= 0) {
        mmx_tab[best].woke = 0;
        return best;
    }
    for (k = 1; k <= MMX_N + 1; k++) {
        i = (mmx_rr + k) % (MMX_N + 1);
        if (mmx_runnable(i, now)) {
            mmx_rr = i;
            return i;
        }
    }
    return -1;
}

static void mmx_sleep_us(MMINTEGER us)
{
    if (us <= 0)
        return;
#if defined(_WIN32)
#ifdef _WINDOWS_
    /* Sleep and usleep go by the system tick, 15.6 ms unless something
     * has asked for finer, and a thread's PAUSE is meant to end within
     * a millisecond or two: a high-resolution waitable timer (Windows
     * 10 1803 on) does that without asking the whole system to tick
     * faster.  CREATE_WAITABLE_TIMER_HIGH_RESOLUTION is 2, spelt so for
     * headers that predate it. */
    {
        static HANDLE tm;
        static int tried;
        LARGE_INTEGER due;

        if (!tried) {
            tried = 1;
            tm = CreateWaitableTimerExW(NULL, NULL, 2, TIMER_ALL_ACCESS);
        }
        if (tm != NULL) {
            due.QuadPart = -(LONGLONG)us * 10;
            if (SetWaitableTimer(tm, &due, 0, NULL, NULL, FALSE)) {
                WaitForSingleObject(tm, INFINITE);
                return;
            }
        }
    }
#endif
    usleep((unsigned)us);
#else
    {
        struct timespec ts;

        ts.tv_sec = (time_t)(us / 1000000);
        ts.tv_nsec = (long)(us % 1000000) * 1000L;
        nanosleep(&ts, NULL);
    }
#endif
}

/* Nothing can run: the process sleeps until the soonest time limit (a
 * PAUSE's slice, a timed wait), 10 ms at most. */
static void mmx_idle(void)
{
    MMINTEGER now = mmx_now(), until = now + 10000;
    int i;

    for (i = 0; i <= MMX_N; i++)
        if (mmx_tab[i].state == MMX_WAIT && !mmx_tab[i].paused
            && mmx_tab[i].until && mmx_tab[i].until < until)
            until = mmx_tab[i].until;
    mmx_sleep_us(until - now);
}

/* This thread cannot go on (it waits, or is paused): run the others
 * until it can. */
static void mmx_schedule(void)
{
    int i;

    for (;;) {
        i = mmx_pick();
        if (i >= 0) {
            mmx_switch(i);
            return;
        }
        mmx_idle();
    }
}

static void mmx_unlock_all(int i)
{
    int k;

    for (k = 1; k <= MMX_LOCKS; k++)
        if (mmx_lock[k] == i + 1)
            mmx_lock[k] = 0;
}

/* The running thread ends.  Never returns. */
static void mmx_finish(void)
{
    int me = mmx_cur, to;

    mmx_unlock_all(me);
    mmx_hold = 0;
    mmx_tab[me].state = MMX_ZOMBIE;
    mmx_live--;
    mmx_zombie = me;
    for (;;) {
        to = mmx_pick();
        if (to >= 0)
            break;
        mmx_idle();
    }
    mmx_cur = to;
    mmx_load(&mmx_tab[to]);
    mmx_enter_turn(to);
#ifdef MMX_UCTX
    setcontext(&mmx_tab[to].uc);
#endif
#ifdef MMX_FIBER
    SwitchToFiber(mmx_tab[to].fiber);
#endif
    for (;;) { }        /* not reached */
}

/* A new thread's first run, on its own stack. */
#ifdef MMX_UCTX
static void mmx_tramp(void)
#endif
#ifdef MMX_FIBER
static void __stdcall mmx_tramp(void *unused)
#endif
{
#ifdef MMX_FIBER
    (void)unused;
#endif
    mmx_reap();
#ifdef MMX_HOST_VM
    mmx_vm_enter(mmx_tab[mmx_cur].entry, mmx_tab[mmx_cur].vstack,
                 mmx_tab[mmx_cur].vsize);
#else
    mmx_tab[mmx_cur].entry();
#endif
    mmx_finish();
}

/* The main program becomes thread 0, the first time it is needed. */
static void mmx_boot(void)
{
    struct mmx *t = &mmx_tab[0];

    if (mmx_booted)
        return;
    mmx_booted = 1;
    t->state = MMX_READY;
    t->prio = mmx_main_prio;
    mmx_next_wake = MMX_NEVER;
    if (mmx_bindsz && t->bound == NULL)
        t->bound = (unsigned char *)malloc((size_t)mmx_bindsz);
#ifdef MMX_FIBER
    t->fiber = ConvertThreadToFiber(NULL);
#ifdef _WINDOWS_
    if (t->fiber == NULL)               /* it is one already */
        t->fiber = GetCurrentFiber();
#endif
#endif
    mmx_enter_turn(0);
}

/* ---- the program's per-thread state -------------------------------- */

void mm_thr_bind(void *p, MMINTEGER n)
{
    unsigned char *q;

    if (mmx_nbind >= MMX_NBIND || n <= 0 || mmx_live)
        return;
    q = (unsigned char *)realloc(mmx_bind_init, (size_t)(mmx_bindsz + n));
    if (q == NULL)
        return;
    mmx_bind_init = q;
    memcpy(q + mmx_bindsz, p, (size_t)n);
    mmx_bind_p[mmx_nbind] = (unsigned char *)p;
    mmx_bind_n[mmx_nbind++] = (int)n;
    mmx_bindsz += (int)n;
    free(mmx_tab[0].bound);
    mmx_tab[0].bound = (unsigned char *)malloc((size_t)mmx_bindsz);
}

void mm_thr_flagword(MMINTEGER *p)
{
    mmx_flags = p;
}

void mm_thr_idword(int *p)
{
    mmx_idp = p;
    if (p)
        *p = mmx_cur;
}

/* An interrupt of thread n's is waiting for it (mmb_int.h): it runs
 * next, and the running thread gives way at its next poll. */
void mm_thr_wake(MMINTEGER n)
{
    if (n < 0 || n > MMX_N || !mmx_alive((int)n) || (int)n == mmx_cur)
        return;
    mmx_tab[n].intpend = 1;
    mmx_next_wake = 0;
}

/* mmb_int.h's mm_int_fire, around a routine: no switch while it runs */
void mm_thr_inint(MMINTEGER on)
{
    mmx_inint += on ? 1 : -1;
    if (mmx_inint < 0)
        mmx_inint = 0;
}

/* ---- THREAD START -------------------------------------------------- */

void mm_thr_new(MMINTEGER n, MMINTEGER kb, MMINTEGER prio)
{
    mmx_new_n = 0;
    if (mm_range(n, 1, MMX_N))
        return;
    if (kb >= 0 && mm_range(kb, 6, 512))
        return;
    if (prio >= 0 && mm_range(prio, 1, 10))
        return;
    if (mmx_alive((int)n)) {
        char m[40];

        sprintf(m, "Thread %d is running", (int)n);
        mm_error(m);
        return;
    }
    mmx_new_n = (int)n;
    mmx_new_kb = kb >= 0 ? (int)kb : mmx_def_kb;
    mmx_new_prio = prio >= 0 ? (int)prio : 1;
    mmx_new_nargs = 0;
    mmx_new_nstr = 0;
}

static int mmx_new_arg(void)
{
    if (mmx_new_nargs >= MMX_MAXARG) {
        mm_error("Too many arguments for a thread");
        return -1;
    }
    return mmx_new_nargs++;
}

void mm_thr_argi(MMINTEGER v)
{
    int k = mmx_new_arg();

    if (k < 0)
        return;
    mmx_new_ai[k] = v;
    mmx_new_sx[k] = 0;
}

void mm_thr_argf(MMFLOAT v)
{
    int k = mmx_new_arg();

    if (k < 0)
        return;
    mmx_new_af[k] = v;
    mmx_new_sx[k] = 0;
}

void mm_thr_args(const char *s)
{
    int k = mmx_new_arg();

    if (k < 0)
        return;
    memcpy(mmx_new_as[k], s, (size_t)mm_slen(s) + 1);
    mmx_new_as[k][mm_slen(s) + 1] = 0;
    mmx_new_sx[k] = 1;
    mmx_new_nstr++;
}

void mm_thr_go(void (*entry)(void))
{
    int n = mmx_new_n, k, s;
    struct mmx *t;
    unsigned long by, pl, ar, vs = 0, total;
    char *b;

    if (n == 0 || mm_poisoned())
        return;
    mmx_new_n = 0;
    mmx_boot();
    t = &mmx_tab[n];
    by = (unsigned long)MM_BYREFN * sizeof(union mm_byref_u);
    pl = (unsigned long)MMX_TMPN * MM_STRSZ;
    ar = (unsigned long)mmx_new_nstr * MM_STRSZ;
#ifdef MMX_HOST_VM
    vs = MMX_VSTACK;
    if ((unsigned long)mmx_new_kb * 1024 > vs)
        vs = (unsigned long)mmx_new_kb * 1024;
#endif
    total = by + pl + ar + vs + 16;
    memset(t, 0, sizeof *t);
    t->vblock = (char *)mmx_valloc(total);
    if (mmx_bindsz)
        t->bound = (unsigned char *)malloc((size_t)mmx_bindsz);
#ifdef MMX_UCTX
    {
        unsigned long cs = MMX_CSTACK;

#ifndef MMX_HOST_VM
        /* native: the program's own locals are on this stack */
        cs += (unsigned long)mmx_new_kb * 1024;
#endif
        t->cstack = malloc((size_t)cs);
        if (t->cstack != NULL && getcontext(&t->uc) == 0) {
            t->uc.uc_stack.ss_sp = t->cstack;
            t->uc.uc_stack.ss_size = (size_t)cs;
            t->uc.uc_link = NULL;
            makecontext(&t->uc, mmx_tramp, 0);
        } else {
            free(t->cstack);
            t->cstack = NULL;
        }
    }
#endif
#ifdef MMX_FIBER
    t->fiber = CreateFiber((size_t)MMX_CSTACK, mmx_tramp, NULL);
    t->cstack = t->fiber;
#endif
    if (t->vblock == NULL || t->cstack == NULL
        || (mmx_bindsz && t->bound == NULL)) {
        mmx_free(t);
        mm_error("Not enough memory for a thread");
        return;
    }
    b = (char *)(((uintptr_t)t->vblock + 7) & ~(uintptr_t)7);
    t->byref = (union mm_byref_u *)(void *)b;
    b += by;
    t->pool = (char (*)[MM_STRSZ])(void *)b;
    b += pl;
    for (k = 0, s = 0; k < mmx_new_nargs; k++) {
        t->ai[k] = mmx_new_ai[k];
        t->af[k] = mmx_new_af[k];
        if (mmx_new_sx[k]) {
            t->as[k] = b + (unsigned long)s * MM_STRSZ;
            memcpy(t->as[k], mmx_new_as[k], MM_STRSZ);
            s++;
        }
    }
    b += ar;
#ifdef MMX_HOST_VM
    t->vstack = b;
    t->vsize = vs;
    memset(b, MMX_PAINT, (size_t)vs);
    mmx_vm_init(&t->vm, b, vs);
#endif
    t->nargs = mmx_new_nargs;
    mm_gflush();
    mmx_disp_get(&t->disp);            /* its starter's */
    if (mmx_bindsz)
        memcpy(t->bound, mmx_bind_init, (size_t)mmx_bindsz);
    t->entry = entry;
    t->prio = mmx_new_prio;
    t->state = MMX_READY;
    mmx_live++;
}

/* The thread's own SUB, called by the entry the translator writes:
 * argument k as THREAD START gave it. */
MMINTEGER mm_thr_geti(MMINTEGER k)
{
    return mmx_tab[mmx_cur].ai[k];
}

MMFLOAT mm_thr_getf(MMINTEGER k)
{
    return mmx_tab[mmx_cur].af[k];
}

char *mm_thr_gets(MMINTEGER k)
{
    return mmx_tab[mmx_cur].as[k];
}

/* ---- turns -------------------------------------------------------- */

/* Every MMX_EVERY statements while the program has threads (mmb_int.h):
 * the next thread runs when this one's turn is over, or as soon as a
 * thread's PAUSE has ended. */
void mm_thr_poll(void)
{
    MMINTEGER now;
    int i;

    if (!mmx_live || mmx_inint || mmx_hold)
        return;
    now = mmx_now();
    if (now < mmx_turn_end && now < mmx_next_wake)
        return;
    i = mmx_pick();
    if (i >= 0 && i != mmx_cur)
        mmx_switch(i);
    else
        mmx_enter_turn(mmx_cur);
}

void mm_thr_yield(void)
{
    int i;

    if (mmx_inint) {
        mm_error("Invalid in an interrupt routine");
        return;
    }
    if (!mmx_live || mmx_hold)
        return;
    i = mmx_pick();
    if (i >= 0 && i != mmx_cur)
        mmx_switch(i);
    else
        mmx_enter_turn(mmx_cur);
}

static void mmx_not_running(int n)
{
    char m[40];

    sprintf(m, "Thread %d is not running", n);
    mm_error(m);
}

void mm_thr_exit(void)
{
    if (mmx_cur == 0) {
        mm_error("Not in a thread");
        return;
    }
    if (mmx_inint) {                    /* the routine could never end */
        mm_error("Invalid in an interrupt routine");
        return;
    }
    mmx_finish();
}

void mm_thr_stop(MMINTEGER n)
{
    if (mm_range(n, 1, MMX_N))
        return;
    if (!mmx_alive((int)n))
        return;
    if ((int)n == mmx_cur) {
        if (mmx_inint) {
            mm_error("Invalid in an interrupt routine");
            return;
        }
        mmx_finish();
        return;
    }
    /* Stopped where it stands: it waits at a poll or in a wait, and is
     * never run again, so its stacks go now. */
    mmx_unlock_all((int)n);
    mmx_live--;
    mmx_free(&mmx_tab[n]);
}

void mm_thr_pause(MMINTEGER n)
{
    if (mm_range(n, 1, MMX_N))
        return;
    if (!mmx_alive((int)n)) {
        mmx_not_running((int)n);
        return;
    }
    mmx_tab[n].paused = 1;
    if ((int)n == mmx_cur && !mmx_inint && !mmx_hold)
        mmx_schedule();
}

void mm_thr_resume(MMINTEGER n)
{
    if (mm_range(n, 1, MMX_N))
        return;
    if (!mmx_alive((int)n)) {
        mmx_not_running((int)n);
        return;
    }
    mmx_tab[n].paused = 0;
}

void mm_thr_slice(MMINTEGER ms)
{
    if (mm_range(ms, 1, 100))
        return;
    mmx_slice_us = ms * 1000;
    if (mmx_booted)
        mmx_enter_turn(mmx_cur);
}

void mm_thr_priority(MMINTEGER n, MMINTEGER p)
{
    if (mm_range(n, 0, MMX_N) || mm_range(p, 1, 10))
        return;
    if (n == 0) {
        mmx_main_prio = (int)p;
        mmx_tab[0].prio = (int)p;
        return;
    }
    if (!mmx_alive((int)n)) {
        mmx_not_running((int)n);
        return;
    }
    mmx_tab[n].prio = (int)p;
}

/* THREAD SIZE kb [, locals [, nest]]: a blank one is -1.  LOCALS and
 * NEST size nothing here (PLAN-threads.md, D3), but keep their ranges. */
void mm_thr_size(MMINTEGER kb, MMINTEGER locals, MMINTEGER nest)
{
    if (kb >= 0 && mm_range(kb, 6, 512))
        return;
    if (locals >= 0 && mm_range(locals, 8, 256))
        return;
    if (nest >= 0 && mm_range(nest, 4, 100))
        return;
    if (kb >= 0)
        mmx_def_kb = (int)kb;
}

/* ---- locks, holds and waits ---------------------------------------- */

void mm_thr_unlock(MMINTEGER k)
{
    char m[48];

    if (mm_range(k, 1, MMX_LOCKS))
        return;
    if (mmx_lock[k] != mmx_cur + 1) {
        sprintf(m, "Lock %d is not held by this thread", (int)k);
        mm_error(m);
        return;
    }
    mmx_lock[k] = 0;
}

void mm_thr_lock_timeout(MMINTEGER k)
{
    char m[32];

    sprintf(m, "Lock %d timed out", (int)k);
    mm_error(m);
}

void mm_thr_hold(void)
{
    mmx_hold++;
}

void mm_thr_release(void)
{
    if (mmx_hold == 0) {
        mm_error("The threads are not held");
        return;
    }
    mmx_hold--;
}

/* Whether what a wait is for is so already (taking a free lock). */
static int mmx_over(int kind, int arg)
{
    switch (kind) {
    case MM_THR_LOCK:
        if (mmx_lock[arg] == 0) {
            mmx_lock[arg] = mmx_cur + 1;
            return 1;
        }
        return 0;
    case MM_THR_THREAD:
        return !mmx_alive(arg);
    case MM_THR_FLAG:
        return mmx_flags != NULL && ((*mmx_flags >> arg) & 1);
    }
    return 0;
}

/*
 * The one wait: until what `kind` names has come, or for `us`
 * microseconds (-1 no limit), letting the other threads run meanwhile.  1 when it
 * came, 0 at the time, -1 at an error.  mmb_thread.h calls it a slice
 * at a time, with this thread's interrupts taken between, as PAUSE is.
 *
 *   MM_THR_SLEEP           nothing but the time (PAUSE)
 *   MM_THR_LOCK k          lock k, which it then has
 *   MM_THR_THREAD n        thread n to end
 *   MM_THR_FLAG f          FLAG(f) to be 1
 */
MMINTEGER mm_thr_block(MMINTEGER kind, MMINTEGER arg, MMINTEGER us)
{
    struct mmx *t;
    int held = 0;
    char m[48];
    MMINTEGER until = us >= 0 ? mmx_now() + us : 0;

    mmx_boot();
    t = &mmx_tab[mmx_cur];
    switch ((int)kind) {
    case MM_THR_LOCK:
        if (mm_range(arg, 1, MMX_LOCKS))
            return -1;
        if (mmx_lock[arg] == mmx_cur + 1) {
            sprintf(m, "Lock %d is already held by this thread", (int)arg);
            mm_error(m);
            return -1;
        }
        break;
    case MM_THR_THREAD:
        if (mm_range(arg, 1, MMX_N))
            return -1;
        if ((int)arg == mmx_cur) {
            mm_error("A thread cannot wait for itself");
            return -1;
        }
        break;
    case MM_THR_FLAG:
        if (mm_range(arg, 0, 63))
            return -1;
        break;
    }
    if (kind != MM_THR_SLEEP && mmx_over((int)kind, (int)arg))
        return 1;
    if (us == 0 && kind != MM_THR_SLEEP)
        return 0;                       /* tried: no waiting at all */
    if (mmx_inint) {
        if (kind != MM_THR_SLEEP) {
            mm_error("Cannot wait in an interrupt routine");
            return -1;
        }
        held = -1;
    }
    if (mmx_hold && held == 0) {
        /* THREAD WAIT lets the hold go while it waits, and has it again
         * after; THREAD LOCK could never be given its lock */
        if (kind == MM_THR_LOCK) {
            mm_error("Cannot wait while the threads are held");
            return -1;
        }
        if (kind == MM_THR_SLEEP)
            held = -1;
        else {
            held = mmx_hold;
            mmx_hold = 0;
        }
    }
    if (held < 0 || !mmx_live) {
        /* nothing else may run, or there is nothing else: wait here */
        for (;;) {
            MMINTEGER now = mmx_now();

            if (kind != MM_THR_SLEEP && mmx_over((int)kind, (int)arg))
                return 1;
            if (until && now >= until)
                return 0;
            if (kind == MM_THR_SLEEP && held < 0) {
                mm_pause_raw((MMFLOAT)(until - now) / 1000.0);
                return 0;
            }
            mmx_sleep_us(until && until - now < 1000 ? until - now : 1000);
        }
    }
    t->state = MMX_WAIT;
    t->wkind = (int)kind;
    t->warg = (int)arg;
    t->until = until;
    t->woke = 0;
    if (until && until < mmx_next_wake)
        mmx_next_wake = until;
    mmx_schedule();
    t = &mmx_tab[mmx_cur];
    t->state = MMX_READY;
    t->until = 0;
    if (held > 0)
        mmx_hold = held;
    return t->woke;
}

/*
 * A read that would wait for input - INPUT at the console, a serial
 * port's LINE INPUT - lets the other threads run until there is
 * something to read on fd (poll), or for `us` (-1 no limit).  1 when
 * there is, 0 at the time, -1 when there are no threads to run (or they
 * may not), and the caller waits as it always did.  Such a wait takes
 * no interrupts, as INPUT never has: they run when it ends.
 */
static int mmx_io_wait(int fd, MMINTEGER us)
{
    MMINTEGER end;
    struct pollfd p;

    if (!mmx_live || mmx_inint || mmx_hold)
        return -1;
    end = us >= 0 ? mmx_now() + us : 0;
    for (;;) {
        p.fd = fd;
        p.events = POLLIN;
        p.revents = 0;
        if (poll(&p, 1, 0) > 0)
            return 1;
        if (end && mmx_now() >= end)
            return 0;
        mm_thr_block(MM_THR_IO, 0, 2000);
    }
}

#ifdef PC_HOST
/* A wait of the runtime's own with nothing to watch (a serial port's
 * idle): the others run for `us`.  0 when there are no threads. */
static int mmx_io_pause(MMINTEGER us)
{
    if (!mmx_live || mmx_inint || mmx_hold)
        return 0;
    mm_thr_block(MM_THR_IO, 0, us);
    return 1;
}
#endif

/* PAUSE while there are threads: the others run meanwhile. */
static int mmx_pausing(MMFLOAT ms)
{
    if (!mmx_live || mmx_inint || mmx_hold)
        return 0;
    mm_thr_block(MM_THR_SLEEP, 0, (MMINTEGER)(ms * 1000.0));
    return 1;
}

/* ERASE, REDIM and CLEAR, in a program with threads: a thread waiting
 * inside a statement may hold the address of the array's data. */
void mm_thr_solo(void)
{
    if (mmx_live)
        mm_error("Not allowed while a thread is running");
}

/* RUN and CHAIN replace the program: in the main program only. */
static int mmx_in_thread(void)
{
    if (mmx_cur != 0) {
        mm_error("Not allowed in a thread");
        return 1;
    }
    return 0;
}

/* ---- LOCAL frames ------------------------------------------------- *
 *
 * A routine's LOCAL strings and arrays are a frame from mm_lheap, given
 * back by mm_lfree as it returns.  A thread's frames are listed in its
 * record as well, so that a thread stopped, or ended by THREAD EXIT,
 * in the middle of its calls gives them back - the firmware frees a
 * stopped thread's locals.  The main program's are not listed: it is
 * never stopped.  Frames nest as calls do, so the one freed is the
 * last one listed. */
static void mmx_lf_push(void *p)
{
    struct mmx *t = &mmx_tab[mmx_cur];
    void **n;

    if (mmx_cur == 0 || p == NULL)
        return;
    if (t->nlf == t->alf) {
        n = (void **)realloc(t->lf, (size_t)(t->alf + 16) * sizeof *n);
        if (n == NULL)
            return;         /* unlisted: kept if the thread is stopped */
        t->lf = n;
        t->alf += 16;
    }
    t->lf[t->nlf++] = p;
}

static void mmx_lf_pop(void *p)
{
    struct mmx *t = &mmx_tab[mmx_cur];
    int i;

    if (mmx_cur == 0)
        return;
    for (i = t->nlf - 1; i >= 0; i--)
        if (t->lf[i] == p) {
            memmove(&t->lf[i], &t->lf[i + 1],
                    (size_t)(t->nlf - 1 - i) * sizeof t->lf[0]);
            t->nlf--;
            return;
        }
}

static void mmx_lf_free(void *p)
{
#ifdef MMX_HOST_VM
    mmx_vm_free(p);
#else
    free(p);
#endif
}

/* ---- what a program can ask ---------------------------------------- */

MMINTEGER mm_thr_id(void)
{
    return mmx_cur;
}

/* MM.INFO(THREAD n): 0 not running, 1 running or ready, 2 in a PAUSE,
 * 3 paused, 4 waiting. */
MMINTEGER mm_thr_state(MMINTEGER n)
{
    struct mmx *t;

    if (mm_range(n, 0, MMX_N))
        return 0;
    if (n == 0)
        return mmx_booted && mmx_tab[0].paused ? 3 : mmx_booted
            && mmx_tab[0].state == MMX_WAIT ? (mmx_tab[0].wkind
                                               == MM_THR_SLEEP ? 2 : 4) : 1;
    t = &mmx_tab[n];
    if (!mmx_alive((int)n))
        return 0;
    if (t->paused)
        return 3;
    if (t->state == MMX_WAIT)
        return t->wkind == MM_THR_SLEEP ? 2 : 4;
    return 1;
}

/* MM.INFO(THREAD LOCK k): the thread that has lock k, -1 if none. */
MMINTEGER mm_thr_lockowner(MMINTEGER k)
{
    if (mm_range(k, 1, MMX_LOCKS))
        return -1;
    return mmx_lock[k] - 1;
}

static FILE *mmx_out;                /* NULL: the program's own output */

static void mmx_put(const char *s)
{
    if (mmx_out != NULL) {
        fputs(s, mmx_out);
        return;
    }
    while (*s)
        mm_putc((unsigned char)*s++);
}

static void mmx_nl(void)
{
    if (mmx_out != NULL)
        fputs("\r\n", mmx_out);
    else
        mm_pr_nl();
}

static void mmx_putn(const char *before, long n)
{
    char b[24];

    mmx_put(before);
    sprintf(b, "%ld", n);
    mmx_put(b);
}

/* THREAD LIST: each thread, what it is doing, a priority that is not
 * 1, and (under bcrun) the most of its VM stack it has used.  As the
 * firmware's, without the line: a translated program does not know it. */
static void mmx_list(int withstack)
{
    int i, p;

    if (!mmx_live) {
        mmx_put("No threads");
        mmx_nl();
        return;
    }
    for (i = 0; i <= MMX_N; i++) {
        struct mmx *t = &mmx_tab[i];
        int waiting = t->state == MMX_WAIT;

        if (!mmx_alive(i))
            continue;
        if (i)
            mmx_putn("Thread ", i);
        else
            mmx_put("Main program");
        mmx_put(":");
        if (t->paused)
            mmx_put(" paused");
        else if (waiting && t->wkind == MM_THR_SLEEP)
            mmx_put(" in PAUSE");
        else if (waiting && t->wkind == MM_THR_LOCK) {
            mmx_putn(" waiting for lock ", t->warg);
            if (mmx_lock[t->warg] == 1)
                mmx_put(" (the main program has it)");
            else if (mmx_lock[t->warg]) {
                mmx_putn(" (thread ", mmx_lock[t->warg] - 1);
                mmx_put(" has it)");
            }
        } else if (waiting && t->wkind == MM_THR_THREAD)
            mmx_putn(" waiting for thread ", t->warg);
        else if (waiting && t->wkind == MM_THR_IO)
            mmx_put(" waiting for input");
        else if (waiting)
            mmx_putn(" waiting for flag ", t->warg);
        else if (i == mmx_cur)
            mmx_put(" running");
        else
            mmx_put(" ready");
        p = mmx_turn(i);
        if (t->prio != 1 || p != 1) {
            mmx_putn(", priority ", t->prio);
            if (p != t->prio) {
                mmx_putn(" (", p);
                mmx_put(" while its lock is waited for)");
            }
        }
#ifdef MMX_HOST_VM
        if (withstack && i && t->vstack) {
            unsigned long u = 0;

            while (u < t->vsize && (unsigned char)t->vstack[u] == MMX_PAINT)
                u++;
            mmx_putn(", stack used at most ", (long)(t->vsize - u));
            mmx_putn(" of ", (long)t->vsize);
        }
#endif
        mmx_nl();
    }
}

void mm_thr_list(void)
{
    mmx_list(1);
}

/* An error has ended the program: where each thread was, after the
 * message, as the firmware lists them at the prompt. */
static void mmx_stopped(void)
{
    if (!mmx_live)
        return;
    mmx_out = stderr;
    mmx_list(0);
    mmx_out = NULL;
}

#else /* !MMX_ON: no threads on this machine yet */

static void mmx_none(void)
{
    mm_error("Threads are not available on this machine yet");
}

void mm_thr_bind(void *p, MMINTEGER n) { (void)p; (void)n; }
void mm_thr_flagword(MMINTEGER *p) { (void)p; }
void mm_thr_idword(int *p) { if (p) *p = 0; }
void mm_thr_wake(MMINTEGER n) { (void)n; }
void mm_thr_inint(MMINTEGER on) { (void)on; }
void mm_thr_new(MMINTEGER n, MMINTEGER kb, MMINTEGER prio)
{
    (void)n; (void)kb; (void)prio;
    mmx_none();
}
void mm_thr_argi(MMINTEGER v) { (void)v; }
void mm_thr_argf(MMFLOAT v) { (void)v; }
void mm_thr_args(const char *s) { (void)s; }
void mm_thr_go(void (*entry)(void)) { (void)entry; }
MMINTEGER mm_thr_geti(MMINTEGER k) { (void)k; return 0; }
MMFLOAT mm_thr_getf(MMINTEGER k) { (void)k; return 0; }
char *mm_thr_gets(MMINTEGER k) { (void)k; return mm_tmp(); }
void mm_thr_poll(void) { }
void mm_thr_yield(void) { }
void mm_thr_exit(void) { mm_error("Not in a thread"); }
void mm_thr_stop(MMINTEGER n) { (void)mm_range(n, 1, MM_THR_MAX); }
void mm_thr_pause(MMINTEGER n) { (void)n; mmx_none(); }
void mm_thr_resume(MMINTEGER n) { (void)n; mmx_none(); }
void mm_thr_slice(MMINTEGER ms) { (void)mm_range(ms, 1, 100); }
void mm_thr_priority(MMINTEGER n, MMINTEGER p) { (void)n; (void)p; mmx_none(); }
void mm_thr_size(MMINTEGER kb, MMINTEGER l, MMINTEGER n) { (void)kb; (void)l; (void)n; }
void mm_thr_unlock(MMINTEGER k) { (void)k; mmx_none(); }
void mm_thr_lock_timeout(MMINTEGER k) { (void)k; mmx_none(); }
void mm_thr_hold(void) { }
void mm_thr_release(void) { }
MMINTEGER mm_thr_block(MMINTEGER kind, MMINTEGER arg, MMINTEGER us)
{
    (void)kind; (void)arg; (void)us;
    mmx_none();
    return -1;
}
void mm_thr_solo(void) { }
static int mmx_pausing(MMFLOAT ms) { (void)ms; return 0; }
static int mmx_io_wait(int fd, MMINTEGER us) { (void)fd; (void)us; return -1; }
#ifdef PC_HOST
static int mmx_io_pause(MMINTEGER us) { (void)us; return 0; }
#endif
static void mmx_lf_push(void *p) { (void)p; }
static void mmx_lf_pop(void *p) { (void)p; }
static void mmx_mode_all(void) { }
static void mmx_stopped(void) { }
static int mmx_in_thread(void) { return 0; }
MMINTEGER mm_thr_id(void) { return 0; }
MMINTEGER mm_thr_state(MMINTEGER n) { (void)n; return n == 0; }
MMINTEGER mm_thr_lockowner(MMINTEGER k) { (void)k; return -1; }
void mm_thr_list(void)
{
    const char *s = "No threads";

    while (*s)
        mm_putc((unsigned char)*s++);
    mm_pr_nl();
}

#endif /* MMX_ON */
