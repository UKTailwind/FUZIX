/*
 * USB host keyboard for the Pico Computer 3: TinyUSB host mode on the
 * RP2350 controller (which faces the on-board 4-port hub), HID keyboard
 * decode via kbd_decode.c (MMBasic's model, all six layouts).
 *
 * The slot/polling architecture is vendored from the MicroPython PC3
 * port (mp_usbh.c), which ported it verbatim from MMBasic - including
 * the three hard-won enumeration rules:
 *   - do NOT call tuh_hid_set_protocol at mount (wedges EP0, blocks
 *     devices enumerating behind the keyboard),
 *   - do NOT request a report from the mount callback,
 *   - reports are POLLED on a timer, never re-armed from the receive
 *     callback (re-arming broke multi-device use).
 *
 * tuh_task() is pumped from plt_idle (which overrides the weak asm
 * version in tricks.S) and from the console tty's pre-sleep hook; the
 * per-slot report timers advance from the 200 Hz kernel tick.
 * Translated bytes land in a small lock-free ring that console_getc
 * (polled by tty_interrupt) drains into the console tty - so the
 * keyboard and the serial port feed the same session.
 */

#include <kernel.h>
#include <kdata.h>
#include <printf.h>
#include "picosdk.h"
#include "config.h"

#ifdef CONFIG_PC3_USB_KBD

#define ssize_t __ssize_t
#define time_t __time_t
#include <tusb.h>
#undef ssize_t
#undef time_t
#include "pico/time.h"
#include "hardware/timer.h"
#include <hardware/structs/usb.h>
#include <hardware/structs/nvic.h>
#include <hardware/irq.h>
#include "kbd_decode.h"
#include "keyboard_maps.h"
#include "rawuart.h"

/* --- keyboard layouts (MMBasic set) -------------------------------------- */

const int *kbd_layout = UKkeyValue;
const char *kbd_layout_name = "UK";

static const struct {
    const char *name;
    const int *table;
} kbd_layouts[] = {
    { "US", USkeyValue }, { "UK", UKkeyValue }, { "DE", DEkeyValue },
    { "FR", FRkeyValue }, { "ES", ESkeyValue }, { "BE", BEkeyValue },
};

int kbd_set_layout(const char *name)
{
    unsigned int i;
    for (i = 0; i < sizeof(kbd_layouts) / sizeof(kbd_layouts[0]); i++) {
        /* case-insensitive 2-letter match */
        if (((kbd_layouts[i].name[0] | 0x20) == (name[0] | 0x20)) &&
            ((kbd_layouts[i].name[1] | 0x20) == (name[1] | 0x20))) {
            kbd_layout = kbd_layouts[i].table;
            kbd_layout_name = kbd_layouts[i].name;
            return 0;
        }
    }
    return -1;
}

/* --- input ring: decoder (thread) -> console_getc (IRQ poll) ------------- */

#define KBD_RING 64
static volatile uint8_t kring[KBD_RING];
static volatile uint8_t khead, ktail; /* head = write, tail = read */

void kbd_push(uint8_t c)
{
    uint8_t next = (khead + 1) & (KBD_RING - 1);
    if (next != ktail) {
        kring[khead] = c;
        khead = next;
    }
}

/* Console tty input: keyboard first, then the serial port - both feed
 * the same session (devn is the uart device number, passed through). */
int console_getc(uint8_t devn)
{
    /* the keyboard ring feeds the console session only */
    if (devn == 1 && khead != ktail) {
        uint8_t c = kring[ktail];
        ktail = (ktail + 1) & (KBD_RING - 1);
        return c;
    }
    return rawuart_getc(devn);
}

/* --- HID device slots (MMBasic model, keyboard-only) --------------------- */

#define HID_NSLOTS 4
enum { HID_NONE = 0, HID_KBD = 1 };

typedef struct {
    volatile bool active;
    uint8_t addr;
    uint8_t inst;
    uint16_t vid, pid;              /* identifies the device across unplugs */
    uint8_t type;
    volatile bool report_requested; /* a tuh_hid_receive_report in flight */
    volatile int report_timer;      /* ms since last report (advanced by tick) */
    int report_rate;                /* ms between report requests */
    bool notfirsttime;              /* LED report has been sent once */
    volatile bool leds_dirty;       /* LEDs changed; hid_poll will send them */
    uint8_t sendlights;             /* LED bitmap: 0x01 num 0x02 caps 0x04 scroll */
} hid_slot_t;

static hid_slot_t hid_slots[HID_NSLOTS];
static bool usbh_inited;

static volatile uint32_t last_pump_ms;
static volatile uint32_t last_pump_us;
static void usb_pump(void);

/* The 32-bit raw microsecond counter, read straight out of the timer.
 * time_us_64() is a syscall-free SDK call but still a 64-bit assembly
 * of two registers plus a divide at the call site; this is one load,
 * which is what makes it affordable per character.  Wraps every ~71
 * minutes, and every use below is an unsigned DIFFERENCE, which is
 * correct across the wrap. */
static inline uint32_t timer_raw_us(void)
{
    return timer_hw->timerawl;
}

/* Advance the report timers: called from the 200 Hz kernel tick (IRQ
 * context - plain counters only, as MMBasic's 1 ms timer does). The
 * pump itself NEVER runs in interrupt context: tuh_task's stack depth
 * on top of a deep syscall overflows the ~1.2K kernel stack straight
 * into udata. When the thread-context pump is starved (a spinning
 * process: no idle, no tty sleep), the tick pends PendSV instead
 * (devices.c) and the preempt trampoline - thread mode, empty kernel
 * stack - pumps from preempt_handler. */
void usbkbd_tick(void)
{
    for (int i = 0; i < HID_NSLOTS; i++) {
        if (hid_slots[i].type != HID_NONE && hid_slots[i].report_timer < 10000) {
            hid_slots[i].report_timer += 1000 / TICKSPERSEC;
        }
    }
}

/* Has the thread-context pump been starved? (tick-side PendSV trigger) */
int usbkbd_starved(void)
{
    return usbh_inited &&
        (uint32_t)(time_us_64() / 1000) - last_pump_ms > 5;
}

/* Push the LED bitmap to a keyboard's physical LEDs: 1-byte OUTPUT
 * report, exactly as MMBasic. */
/*
 *	The LED report is a control transfer on EP0 - the endpoint
 *	enumeration also needs.  Issued from wherever a lock key happened
 *	to be decoded, it wedged EP0: the keyboard kept working on its
 *	interrupt endpoint until the next lock key, then stopped, and
 *	from then on an attached device was seen but could never
 *	enumerate.
 *
 *	So nothing sends it inline.  A lock key marks the LEDs dirty, and
 *	hid_poll - which already owns the one-transfer-at-a-time rhythm
 *	for this keyboard - sends it when no report request is in flight,
 *	keeps it dirty if the submission is refused, and tries again on
 *	the next pass.  It matters on this hardware: a Pi keyboard has an
 *	embedded keypad, so a keyboard left in num lock types 6 for O
 *	until the host tells it otherwise.
 */
static void kbd_leds_mark(int slot)
{
    if (slot >= 0) {
        hid_slots[slot].leds_dirty = true;
    }
}

/* Only ever called from hid_poll, with EP0 idle. */
static bool kbd_leds_send(int slot)
{
    return tuh_hid_set_report(hid_slots[slot].addr, hid_slots[slot].inst, 0,
        HID_REPORT_TYPE_OUTPUT, &hid_slots[slot].sendlights, 1);
}

/* --- num lock is a property of the KEYBOARD, not of us ------------------- */
/*
 * The embedded keypad described above (a Pi keyboard typing 6 for O) is
 * the KEYBOARD'S OWN doing, and the only thing that triggers it is the
 * num lock bit in the LED report we send.  So the right value is a
 * property of the keyboard - and it cannot be discovered by asking one.
 *
 * A Raspberry Pi keyboard (04d9:0006, no keypad) and a full-size Lenovo
 * (04b3:3025) return BYTE-IDENTICAL 65-byte report descriptors: both
 * declare the whole key usage page (19 00 2a ff 00 - an Array item
 * declares the range of values an element may carry, not which keys
 * exist), and both declare a num lock LED.  A VID:PID quirk table is no
 * better: 04d9 is Holtek, a generic controller vendor shared across
 * unrelated OEM designs, so quirking it would break a full-size Holtek
 * keyboard.
 *
 * Two rules, in this order:
 *
 *   1. A keyboard that declares NO num lock LED is taken to have no
 *      keypad - kbd_has_numlock_led in the shared decoder, and see the
 *      note there for why only that direction holds.
 *   2. Whatever was last chosen for THIS keyboard beats rule 1,
 *      remembered by VID:PID so swapping between a compact keyboard and
 *      a full-size one does the right thing each time.
 *
 * The MicroPython PC3 port has the same two rules and writes (2) to
 * /settings.json, so it survives a reboot.  A kernel does not write
 * files: here (2) lives in RAM for the session, and `picoctl numlock`
 * in /etc/rc is what makes a choice permanent.  Small and static - this
 * is read from the mount callback, which cannot allocate or wait.
 */
#define KBD_NUMLOCK_PREFS 4
static struct {
    uint16_t vid, pid;
    uint8_t on;
} kbd_numlock_pref[KBD_NUMLOCK_PREFS];
static uint8_t kbd_numlock_pref_n;

/* Remember `on' for this keyboard, replacing any existing entry.  A full
   table drops its oldest; the cost is one more Num Lock press on the
   keyboard that got evicted. */
void usb_kbd_numlock_pref(uint16_t vid, uint16_t pid, int on)
{
    for (int i = 0; i < kbd_numlock_pref_n; i++) {
        if (kbd_numlock_pref[i].vid == vid && kbd_numlock_pref[i].pid == pid) {
            kbd_numlock_pref[i].on = on ? 1 : 0;
            return;
        }
    }
    if (kbd_numlock_pref_n == KBD_NUMLOCK_PREFS) {
        memmove(&kbd_numlock_pref[0], &kbd_numlock_pref[1],
            sizeof(kbd_numlock_pref) - sizeof(kbd_numlock_pref[0]));
        kbd_numlock_pref_n--;
    }
    kbd_numlock_pref[kbd_numlock_pref_n].vid = vid;
    kbd_numlock_pref[kbd_numlock_pref_n].pid = pid;
    kbd_numlock_pref[kbd_numlock_pref_n].on = on ? 1 : 0;
    kbd_numlock_pref_n++;
}

/* The remembered setting for this keyboard, or `dflt' - what the
   descriptor's LED block suggests - for one never seen. */
static int kbd_numlock_for(uint16_t vid, uint16_t pid, int dflt)
{
    for (int i = 0; i < kbd_numlock_pref_n; i++) {
        if (kbd_numlock_pref[i].vid == vid && kbd_numlock_pref[i].pid == pid) {
            return kbd_numlock_pref[i].on;
        }
    }
    return dflt;
}

/* Whether the mounted keyboard declared a num lock LED, for the ioctl to
   report - the machine's own answer beats guessing from the model name. */
static bool kbd_numlock_led_seen = true;

/* Reached only from a Caps/Num/Scroll keypress, so a change in the num
   bit here IS the user saying what this keyboard wants: remember it, so
   a keyboard unplugged and plugged back in this session comes back the
   way they left it. */
void kbd_backend_set_leds(int slot, uint8_t leds)
{
    if (slot < 0) {
        return;
    }
    if ((leds ^ hid_slots[slot].sendlights) & 0x01) {
        usb_kbd_numlock_pref(hid_slots[slot].vid, hid_slots[slot].pid,
            leds & 0x01);
    }
    hid_slots[slot].sendlights = leds;
    kbd_leds_mark(slot);
}

/* --- what the PICOIOC_NUMLOCK ioctl calls (misc.c) ----------------------- */

int usb_kbd_numlock_get(void)
{
    return kbd_led_bitmap() & 0x01;
}

/* Whether the mounted keyboard declares a num lock LED. */
int usb_kbd_numlock_led(void)
{
    return kbd_numlock_led_seen ? 1 : 0;
}

/* The mounted keyboard as (vid << 16) | pid, or 0 if there is none. */
uint32_t usb_kbd_id(void)
{
    for (int i = 0; i < HID_NSLOTS; i++) {
        if (hid_slots[i].active && hid_slots[i].type == HID_KBD) {
            return ((uint32_t)hid_slots[i].vid << 16) | hid_slots[i].pid;
        }
    }
    return 0;
}

/* Apply num lock now, remember it for the mounted keyboard, and mark the
   LEDs so hid_poll pushes them - which is what makes an overlay keyboard
   drop its embedded keypad immediately rather than at the next mount. */
void usb_kbd_numlock_set(int on)
{
    kbd_set_numlock(on);
    for (int i = 0; i < HID_NSLOTS; i++) {
        if (hid_slots[i].active && hid_slots[i].type == HID_KBD) {
            usb_kbd_numlock_pref(hid_slots[i].vid, hid_slots[i].pid, on);
            hid_slots[i].sendlights = kbd_led_bitmap();
            kbd_leds_mark(i);
        }
    }
}

/* Record a preference for a NAMED keyboard, and if that happens to be
   the keyboard in use, apply it now instead of at its next mount.
   Without the second half, `picoctl numlock --load' from /etc/rc would
   file the setting correctly and leave the keyboard being typed on
   exactly as it was - which is the whole case it exists for, since the
   keyboard has already mounted and had its first LED report by the time
   rc runs. */
void usb_kbd_numlock_pref_apply(uint16_t vid, uint16_t pid, int on)
{
    usb_kbd_numlock_pref(vid, pid, on);
    if (usb_kbd_id() == (((uint32_t)vid << 16) | pid)) {
        usb_kbd_numlock_set(on);
    }
}

/* --- the rest of the decoder core's backend seam (kbd_decode.h) ---------- */

/* Millisecond tick for the repeat engine. */
uint32_t kbd_ticks_ms(void)
{
    return (uint32_t)(time_us_64() / 1000);
}

/* MicroPython schedules its keyboard.on_key callback here; the kernel
 * has no equivalent (held-key state is read via usb_kbd_keydown, kept
 * for a future ioctl). */
void kbd_backend_on_key(int code)
{
    (void)code;
}

/* The core's diagnostic line (the orphaned-repeat guard). */
void kbd_backend_msg(const char *s)
{
    kputs(s);
}

static int hid_slot_find(uint8_t addr, uint8_t inst)
{
    for (int i = 0; i < HID_NSLOTS; i++) {
        if (hid_slots[i].active && hid_slots[i].addr == addr && hid_slots[i].inst == inst) {
            return i;
        }
    }
    return -1;
}

/* MMBasic hid_app_task's report-request loop: for each active slot with
 * no request in flight whose timer reached its rate, issue exactly one
 * tuh_hid_receive_report. */
static void hid_poll(void)
{
    for (int i = 0; i < HID_NSLOTS; i++) {
        if (!hid_slots[i].active || hid_slots[i].report_requested) {
            continue;
        }
        if (hid_slots[i].report_timer >= hid_slots[i].report_rate) {
            /* First poll of a keyboard: push the initial LED state once
               - which is what turns num lock OFF on a keyboard that
               powered up with it on. */
            if (hid_slots[i].type == HID_KBD && !hid_slots[i].notfirsttime) {
                hid_slots[i].notfirsttime = true;
                hid_slots[i].leds_dirty = true;
            }
            /* One transfer at a time, and EP0 first: the LED report
               goes out on its own pass, and the input report is
               requested on the next.  A refused submission stays
               dirty and is retried rather than lost. */
            if (hid_slots[i].leds_dirty) {
                if (kbd_leds_send(i)) {
                    hid_slots[i].leds_dirty = false;
                    hid_slots[i].report_timer = 0;
                }
                continue;
            }
            hid_slots[i].report_requested = true;
            if (!tuh_hid_receive_report(hid_slots[i].addr, hid_slots[i].inst)) {
                hid_slots[i].report_requested = false;
                hid_slots[i].report_timer = 0;
            }
        }
    }
}

/* --- polled rescue ------------------------------------------------------- */

/* rawuart.c's lesson, applied to USB: interrupt delivery on this
 * platform can strand - enabled, pending, never taken (mechanism still
 * unexplained; NOTES-console-wedge.md).  The uart survives it by being
 * drained from the tick; USB survives it here, from the pump.  A
 * stranded USB interrupt otherwise kills input silently - and if a key
 * was down at the time, the synthesised auto-repeat types that
 * character forever, which was exactly the field report. */

/* hcd.h, which tusb.h does not export to the application. */
extern void hcd_int_handler(uint8_t rhport, bool in_isr);

/* SIE error latches nothing else clears: CRC, bit-stuff and RX-overflow
 * have no interrupt enabled and the hcd's ISR never touches them (the
 * documented dead-keyboard state had all three latched - the 0x47800205
 * in PC3-IRQ-REVIEW.md); DATA_SEQ and RESUME are ours to clear now that
 * their interrupts are masked at init (see usbkbd_init). */
#define USB_SIE_ERROR_LATCHES ( \
    USB_SIE_STATUS_CRC_ERROR_BITS | USB_SIE_STATUS_BIT_STUFF_ERROR_BITS | \
    USB_SIE_STATUS_RX_OVERFLOW_BITS | USB_SIE_STATUS_DATA_SEQ_ERROR_BITS | \
    USB_SIE_STATUS_RESUME_BITS)

static void usb_rescue(void)
{
    static bool pending_seen;
    static bool said;
    uint32_t errs = usb_hw->sie_status & USB_SIE_ERROR_LATCHES;
    uint32_t primask;

    if (errs) {
        hw_clear_bits(&usb_hw->sie_status, errs);
    }

    /* This runs in thread context.  With PRIMASK clear, an enabled
     * interrupt cannot sit pending - the CPU would have taken it before
     * the load below completed.  Seeing it pending anyway means
     * delivery is stranded; requiring it on two consecutive passes
     * rules out sampling the one cycle of a delivery in flight. */
    __asm volatile ("mrs %0, primask" : "=r" (primask));
    if (!primask && irq_is_enabled(USBCTRL_IRQ) &&
        (nvic_hw->ispr[USBCTRL_IRQ / 32] & (1u << (USBCTRL_IRQ % 32)))) {
        if (pending_seen) {
            irq_set_enabled(USBCTRL_IRQ, false);
            hcd_int_handler(0, false); /* run the stranded ISR by hand */
            irq_set_enabled(USBCTRL_IRQ, true);
            kbd_stop_repeat(); /* input gapped; the held key is not trustworthy */
            pending_seen = false;
            if (!said) { /* said once, like rawuart's stall report */
                said = true;
                kputs("USB: stranded interrupt, rescued by polling\n");
            }
        } else {
            pending_seen = true;
        }
    } else {
        pending_seen = false;
    }
}

/* --- task pump ----------------------------------------------------------- */

/* The pump body: only ever entered via usb_pump_stacked (tricks.S),
 * which switches to the dedicated USB stack first - tuh_task's depth
 * must never land on the kernel stack (it overflows into udata). */
/* tuh_task is not reentrant; guard against nested pumping.  File scope
 * rather than function-static because usbkbd_pump_if_starved has to see
 * it: that one is called from the tty output path, which a HID callback
 * can re-enter through kprintf. */
static volatile bool in_task;

void usb_pump_c(void)
{
    if (usbh_inited && !in_task) {
        in_task = true;
        usb_rescue();
        tuh_task();
        hid_poll();
        kbd_repeat_check();
        last_pump_ms = (uint32_t)(time_us_64() / 1000);
        last_pump_us = timer_raw_us();
        in_task = false;
    }
}

extern void usb_pump_stacked(void);

void usbkbd_task(void)
{
    if (!udata.u_ininterrupt) {
        usb_pump_stacked();
    }
}

/* Override the weak asm plt_idle (tricks.S): pump USB whenever the
 * kernel idles, then nap until the next interrupt.
 *
 * The network pump lives here too, and for the same reason it was put
 * here for USB: thread context, empty kernel stack, and it runs
 * whenever nothing else wants the CPU.  It is a no-op until somebody
 * has brought the radio up.  __wfi() is still correct with it - the
 * CYW43's host-wake interrupt is one of the things that will end the
 * nap.
 */
void plt_idle(void)
{
    usbkbd_task();
#ifdef CONFIG_PC3_NET
    {
        extern void pc3_net_poll_c(void);
        pc3_net_poll_c();
    }
#endif
    __wfi();
}

/* Console tty pre-sleep hook: pump once more before the reader blocks,
 * so a keypress already in the controller is delivered without waiting
 * for the next idle pass. */
void console_sleeping(uint8_t devn)
{
    (void)devn;
    usbkbd_task();
}

/*
 * Console tty OUTPUT hook: pump if a long stream has starved the pump.
 *
 * The three thread-context pump sites all assume the process eventually
 * stops running: plt_idle wants an idle kernel, console_sleeping wants a
 * READER about to block, and the preempt trampoline (devices.c) is gated
 * on !udata.u_insys.  A process that only WRITES reaches none of them -
 * it is always runnable, it never reads the tty, and it is inside write()
 * for almost the whole of a line, since every character is rendered to
 * the display on the way past.
 *
 * WHAT THAT LOOKED LIKE, and it is worth writing down because the shape
 * is so specific: a program streaming PRINT output could not be stopped
 * with ^C from the USB keyboard, while ^C from the serial console
 * worked.  Typing during the stream produced EXACTLY ONE character, the
 * first key pressed, and it appeared only once the program had ended.
 *
 * That is the signature of a starved pump rather than a lost signal.
 * hid_poll leaves exactly ONE report request in flight per keyboard, and
 * tuh_hid_report_received_cb - which decodes the report and is the only
 * thing that lets hid_poll re-arm ("deliberately NO re-arm") - runs only
 * from tuh_task.  So the first keypress completed its transfer in
 * hardware and sat there undelivered; every later keypress had no
 * outstanding request to complete and was lost on the wire.  The serial
 * side needs none of this: rawuart_getc is drained from the tick.
 *
 * Called per character, so the test has to be cheap: one 32-bit timer
 * load and a compare, with a real pump at most every 5ms.  The keyboard
 * is polled at 20ms (report_rate), so 5ms costs nothing in latency and
 * matches usbkbd_starved's own threshold.
 */
void usbkbd_pump_if_starved(void)
{
    /* in_task: a HID callback that prints would arrive back here on the
     * dedicated USB stack, and usb_pump_stacked would switch to the
     * stack it is already running on. */
    if (!usbh_inited || in_task || udata.u_ininterrupt) {
        return;
    }
    if ((uint32_t)(timer_raw_us() - last_pump_us) < 5000u) {
        return;
    }
    usbkbd_task();
}

/*
 * Force a real SE0 bus reset on the root port.  Verbatim from MMBasic
 * (USBKeyboard.c USB_bus_reset), including the timings.
 *
 * hcd_port_reset() in the pico-sdk TinyUSB driver is a no-op stub, so
 * driving the PHY directly is the only way to assert a USB reset on
 * RP2040/RP2350.  It matters here because the PC3's CH334 hub is
 * EXTERNALLY POWERED: it never loses VBUS when the board warm-reboots,
 * so it keeps the USB address and configured state it held in the
 * previous session and ignores re-enumeration from address 0 - the
 * whole device tree simply vanishes.  An SE0 reset forces a directly
 * attached hub back to Default state; TinyUSB then cascades the reset
 * to the devices behind it as it re-enumerates each downstream port.
 *
 * The controller must ALREADY be initialised when this is called, so
 * that the PHY is powered and muxed.
 */
#define USB_BUS_RESET_PHY_OVERRIDE_EN ( \
    USB_USBPHY_DIRECT_OVERRIDE_TX_DM_OE_OVERRIDE_EN_BITS | \
    USB_USBPHY_DIRECT_OVERRIDE_TX_DM_OVERRIDE_EN_BITS | \
    USB_USBPHY_DIRECT_OVERRIDE_TX_DP_OE_OVERRIDE_EN_BITS | \
    USB_USBPHY_DIRECT_OVERRIDE_TX_DP_OVERRIDE_EN_BITS)

void usb_bus_reset(void)
{
    /* Take manual control of the DP/DM output drivers. */
    hw_set_bits(&usb_hw->phy_direct_override, USB_BUS_RESET_PHY_OVERRIDE_EN);
    /* Enable the drivers, then pull both lines low = SE0 = bus reset. */
    hw_set_bits(&usb_hw->phy_direct,
        USB_USBPHY_DIRECT_TX_DM_OE_BITS | USB_USBPHY_DIRECT_TX_DP_OE_BITS);
    hw_clear_bits(&usb_hw->phy_direct,
        USB_USBPHY_DIRECT_TX_DM_BITS | USB_USBPHY_DIRECT_TX_DP_BITS);

    /* Hold it.  The spec minimum is 10ms; MMBasic drives ~20ms so that
     * slow and cheap hubs sample it reliably. */
    busy_wait_us(20000);

    /* Release: hand DP/DM back to the SIE.  ALL four override-enable
     * bits must be cleared - clearing only the DM pair leaves DP under
     * manual control on return, which is what made this unreliable in
     * MMBasic before it was fixed there. */
    hw_clear_bits(&usb_hw->phy_direct,
        USB_USBPHY_DIRECT_TX_DM_OE_BITS | USB_USBPHY_DIRECT_TX_DP_OE_BITS);
    hw_clear_bits(&usb_hw->phy_direct_override, USB_BUS_RESET_PHY_OVERRIDE_EN);
}

void usbkbd_init(void)
{
    int i;

    /* MMBasic's startup order exactly (PicoMite.c): clear the slot
     * state, bring the host controller up so the PHY is powered, THEN
     * drive the bus reset, then allow recovery time.  Enumeration is
     * deferred to tuh_task() - pumped from plt_idle here - which runs
     * after all of this, so it starts from a clean bus. */
    for (i = 0; i < HID_NSLOTS; i++)
        memset((void *)&hid_slots[i], 0, sizeof(hid_slots[i]));

    tuh_init(0); /* native controller, root-hub port 0 */

    /* A kernel must not HardFault on bus noise.  TinyUSB's rp2040 hcd
     * panic()s - a BKPT, i.e. a hard lockup - on a data-toggle mismatch
     * (ERROR_DATA_SEQ), and on any enabled-but-unhandled interrupt, of
     * which HOST_RESUME is one it enables and never handles.  Both
     * arrive from ordinary electrical noise on this board (378 MHz,
     * PSRAM and HSTX beside the connector, every device behind a hub):
     * the field signature was constant lockups with the CRC/bit-stuff
     * latches set (PC3-IRQ-REVIEW.md).  MMBasic ships the same hcd
     * unpatched, but a BASIC interpreter rebooting is an annoyance
     * where a kernel rebooting is a disk check.  Mask both at the
     * controller: the hardware poll of the endpoint simply continues,
     * and usb_rescue() clears the latches they leave behind. */
    hw_clear_bits(&usb_hw->inte,
        USB_INTE_ERROR_DATA_SEQ_BITS | USB_INTE_HOST_RESUME_BITS);

#ifndef PC3_NO_USB_BUS_RESET
    usb_bus_reset();    /* force any attached hub back to Default state */
    busy_wait_us(50000); /* let the hub re-detect its downstream ports */
#endif

    usbh_inited = true;
    kputs("USB host: keyboard on the hub, layout ");
    kputs(kbd_layout_name);
    kputs("\n");
}

/* --- TinyUSB host callbacks ---------------------------------------------- */

void tuh_mount_cb(uint8_t dev_addr)
{
    (void)dev_addr;
}

void tuh_umount_cb(uint8_t dev_addr)
{
    /* Quiet: the keyboard says when it goes (tuh_hid_umount_cb), and
       the hub behind it coming and going is not news. */
    (void)dev_addr;
}

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance,
    uint8_t const *desc_report, uint16_t desc_len)
{
    uint8_t proto = tuh_hid_interface_protocol(dev_addr, instance);
    uint16_t vid = 0, pid = 0;
    /* Cached by TinyUSB - no bus traffic, so it is safe here (the rule
       above about control transfers from this callback). */
    tuh_vid_pid_get(dev_addr, &vid, &pid);

    /* Keyboard-only for now: mice/gamepads/touch are left unclaimed
     * (unclaimed interfaces are harmless, as MMBasic's filter shows).
     * NOTE: no tuh_hid_set_protocol here - TinyUSB already activates
     * boot protocol on boot-capable interfaces; a control transfer from
     * this callback wedges EP0 and blocks devices behind the keyboard. */
    if (proto != HID_ITF_PROTOCOL_KEYBOARD || hid_slots[0].active) {
        return;
    }

    hid_slots[0].active = true;
    hid_slots[0].addr = dev_addr;
    hid_slots[0].inst = instance;
    hid_slots[0].vid = vid;
    hid_slots[0].pid = pid;
    hid_slots[0].type = HID_KBD;
    hid_slots[0].report_requested = false;
    hid_slots[0].report_rate = 20; /* ms, as MMBasic */
    /* Staggered startup delay (MMBasic): first report request this many
     * ms after mount. */
    hid_slots[0].report_timer = -(10 + 2 * 500);
    hid_slots[0].notfirsttime = false;
    /* Settle num lock BEFORE the LED bitmap is seeded, so the very first
       LED report carries it and a keyboard that overlays a keypad onto
       its letter keys never gets the chance to turn the overlay on. */
    kbd_numlock_led_seen = kbd_has_numlock_led(desc_report, desc_len) ? true : false;
    kbd_set_numlock(kbd_numlock_for(vid, pid, kbd_numlock_led_seen ? 1 : 0));
    hid_slots[0].sendlights = kbd_led_bitmap();
    kputs("USB keyboard attached\n");
    if (!kbd_numlock_led_seen) {
        kputs("USB keyboard: no num lock LED, assuming no numeric keypad\n");
    }
    /* NOTE: no tuh_hid_receive_report here - hid_poll() issues it. */
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance)
{
    kbd_stop_repeat();
    int slot = hid_slot_find(dev_addr, instance);
    if (slot >= 0) {
        if (hid_slots[slot].type == HID_KBD) {
            kbd_clear_state();
            /* Said out loud: a keyboard that goes away without being
               unplugged is the fault being chased, and silence made it
               indistinguishable from one that simply stopped
               reporting. */
            kputs("USB keyboard detached\n");
        }
        memset(&hid_slots[slot], 0, sizeof(hid_slots[slot]));
        hid_slots[slot].report_requested = true; /* don't poll an empty slot */
    }
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance,
    uint8_t const *report, uint16_t len)
{
    int slot = hid_slot_find(dev_addr, instance);
    if (slot < 0) {
        return;
    }
    if (hid_slots[slot].type == HID_KBD) {
        kbd_process_report(report, len, slot);
    }
    /* Clear the in-flight flag and reset the timer; hid_poll re-requests
     * when the timer next reaches report_rate. Deliberately NO re-arm. */
    hid_slots[slot].report_requested = false;
    hid_slots[slot].report_timer = 0;
}

#endif /* CONFIG_PC3_DISPLAY */
