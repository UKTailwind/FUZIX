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
#include <hardware/structs/usb.h>
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
static void usb_pump(void);

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

void kbd_backend_set_leds(int slot, uint8_t leds)
{
    if (slot < 0) {
        return;
    }
    hid_slots[slot].sendlights = leds;
    kbd_leds_mark(slot);
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

/* --- task pump ----------------------------------------------------------- */

/* The pump body: only ever entered via usb_pump_stacked (tricks.S),
 * which switches to the dedicated USB stack first - tuh_task's depth
 * must never land on the kernel stack (it overflows into udata). */
void usb_pump_c(void)
{
    /* tuh_task is not reentrant; guard against nested pumping */
    static volatile bool in_task;
    if (usbh_inited && !in_task) {
        in_task = true;
        tuh_task();
        hid_poll();
        kbd_repeat_check();
        last_pump_ms = (uint32_t)(time_us_64() / 1000);
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
 * kernel idles, then nap until the next interrupt. */
void plt_idle(void)
{
    usbkbd_task();
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
    (void)desc_report;
    (void)desc_len;
    uint8_t proto = tuh_hid_interface_protocol(dev_addr, instance);

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
    hid_slots[0].type = HID_KBD;
    hid_slots[0].report_requested = false;
    hid_slots[0].report_rate = 20; /* ms, as MMBasic */
    /* Staggered startup delay (MMBasic): first report request this many
     * ms after mount. */
    hid_slots[0].report_timer = -(10 + 2 * 500);
    hid_slots[0].notfirsttime = false;
    hid_slots[0].sendlights = kbd_led_bitmap();
    kputs("USB keyboard attached\n");
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
