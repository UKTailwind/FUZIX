/*
 * HID keyboard decoder, vendored from the Pico Computer 3 MicroPython
 * port (ports/rp2/kbd_decode.c, MIT licence), itself a faithful port of
 * MMBasic's keyboard handling: layout mapping (keyboard_maps.h), AltGr
 * specials, lock keys + LEDs, auto-repeat, the held-key table.
 *
 * Fuzix adaptations: translated bytes go to kbd_push() (usbkbd.c, into
 * the console tty input ring) instead of MicroPython's stdin ringbuf;
 * ticks come from the SDK timer; the Python on_key callback is gone.
 * Ctrl-C needs no special case - the tty layer's line discipline handles
 * INTR like any real terminal would.
 */

#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "pico/time.h"

#include "kbd_decode.h"

/* usbkbd.c: push one translated byte to the console input */
extern void kbd_push(uint8_t c);

static uint32_t ticks_ms(void)
{
    return (uint32_t)(time_us_64() / 1000);
}

// HID modifier byte (report[0]) masks.
#define KBD_SHIFT (0x22) // left|right shift
#define KBD_CTRL  (0x11) // left|right ctrl

// Auto-repeat (typematic): HID keyboards only report on state change, so we
// synthesise repeats from the held key.
// Configurable through the standard Fuzix KBRATE ioctl (/bin/kbdrate),
// which works in tenths of a second; kept in ms here.  Defaults are
// MMBasic's shape - a long wait before the first repeat so a deliberate
// keypress never doubles, then a comfortable typematic rate.
#define KBD_REPEAT_FIRST_DEFAULT (600)
#define KBD_REPEAT_NEXT_DEFAULT  (250)
uint16_t kbd_repeat_first = KBD_REPEAT_FIRST_DEFAULT;
uint16_t kbd_repeat_next = KBD_REPEAT_NEXT_DEFAULT;
// A poll gap longer than this means a release could have been missed,
// so the held key is no longer trustworthy for repeat purposes.
#define KBD_REPEAT_STALE_MS (150)

static uint8_t kbd_prev[6];       // keycodes from the previous report
static bool kbd_caps;             // caps-lock state
static bool kbd_num = true;       // num-lock state (on at boot, as MMBasic)
static bool kbd_scroll;           // scroll-lock state
static uint8_t kbd_held;          // usage currently repeating (0 = none)
static uint8_t kbd_held_mods;
static uint32_t kbd_next_repeat;  // ticks_ms() of the next repeat
static uint32_t kbd_last_report;  // ticks_ms() of the last HID report

// Currently-held keys, MMBasic KeyDown[]: [0..5] = mapped key codes of the held
// keys, most recent first; [6] = modifier bitmap.
static volatile int kbd_keydown[7];

// AltGr (right-Alt) layout specials: {usage, code} pairs, terminated by usage 0.
// Verbatim from MMBasic APP_MapKeyToUsage (a code of 0 means "dead", as MMBasic).
static const uint8_t kbd_altgr_de[] = {0x24, 0x7b, 0x25, 0x5b, 0x26, 0x5d, 0x27, 0x7d,
                                       0x2d, 0x5c, 0x14, 0x40, 0x64, 124, 0x30, 126, 0};
static const uint8_t kbd_altgr_fr[] = {0x1f, 126, 0x20, 35, 0x21, 123, 0x22, 91,
                                       0x23, 124, 0x24, 96, 0x25, 92, 0x26, 94,
                                       0x27, 64, 0x2d, 93, 0x2e, 125, 0};
static const uint8_t kbd_altgr_es[] = {0x35, 92, 0x1e, 124, 0x08, 0, 0x1f, 64,
                                       0x20, 35, 0x21, 0, 0x2f, 91, 0x30, 93,
                                       0x31, 125, 0x34, 123, 0};
static const uint8_t kbd_altgr_be[] = {0x64, 92, 0x20, 35, 0x2f, 91, 0x30, 93,
                                       0x31, 96, 0x34, 39, 0x38, 126, 0};

// Map a HID usage + modifier byte to a key code: the layout-table value with
// AltGr, Ctrl, num-lock and caps-lock applied. Faithful port of MMBasic
// APP_MapKeyToUsage (KeyboardMap.c). Returns 0 for "no code".
static int kbd_map_code(uint8_t usage, uint8_t mods)
{
    if (usage == 0x39 || usage == 0x47 || usage == 0x53) {
        return 0; // caps/scroll/num lock
    }
    if (usage < 0x04 || usage > 0x64) {
        return 0;
    }
    if (mods & 0x40) { // right Alt (AltGr): layout-specific specials
        const uint8_t *t = NULL;
        if (kbd_layout_name[0] == 'D') {
            t = kbd_altgr_de;
        } else if (kbd_layout_name[0] == 'F') {
            t = kbd_altgr_fr;
        } else if (kbd_layout_name[0] == 'E') {
            t = kbd_altgr_es;
        } else if (kbd_layout_name[0] == 'B') {
            t = kbd_altgr_be;
        }
        for (; t && t[0]; t += 2) {
            if (t[0] == usage) {
                return t[1];
            }
        }
    }
    bool shift = mods & KBD_SHIFT;
    if (usage <= 0x1d && (mods & KBD_CTRL)) {
        return kbd_layout[usage * 2] - 96; // Ctrl-<letter> -> 1..26
    }
    if (usage >= 0x54 && usage <= 0x63) { // numeric keypad: num-lock selects
        return kbd_layout[usage * 2 + (kbd_num ? 0 : 1)];
    }
    if (usage <= 0x1d) { // letters: caps-lock XOR shift
        return kbd_layout[usage * 2 + ((kbd_caps ^ shift) ? 1 : 0)];
    }
    return kbd_layout[usage * 2 + (shift ? 1 : 0)];
}

// Translate one key (HID usage + modifiers) to bytes and push them. `slot`
// is the keyboard's HID slot (for driving its lock-key LEDs), or -1.
static void kbd_key(uint8_t usage, uint8_t mods, int slot)
{
    bool shift = mods & KBD_SHIFT;
    bool ctrl = mods & KBD_CTRL;
    // Lock keys: toggle the state, update the LED bitmap, and push it to the
    // keyboard's LEDs (exactly as MMBasic).
    if (usage == 0x39 || usage == 0x53 || usage == 0x47) {
        bool *state = (usage == 0x39) ? &kbd_caps : (usage == 0x53) ? &kbd_num : &kbd_scroll;
        *state = !*state;
        kbd_backend_set_leds(slot, kbd_led_bitmap());
        return;
    }
    // Numeric keypad: with num-lock off the digit/period keys act as the
    // navigation cluster (MMBasic behaviour). Keypad-5 keeps typing '5'.
    if (!kbd_num && usage >= 0x59 && usage <= 0x63) {
        //                          kp1   kp2   kp3   kp4  kp5  kp6   kp7   kp8   kp9   kp0   kp.
        static const uint8_t nav[] = {0x4d, 0x51, 0x4e, 0x50, 0, 0x4f, 0x4a, 0x52, 0x4b, 0x49, 0x4c};
        if (nav[usage - 0x59]) {
            usage = nav[usage - 0x59];
        }
    }
    // Function keys F1-F12 (HID 0x3a..0x45) as the xterm sequences a
    // terminal sends. The layout tables carry MMBasic's pseudo-ASCII codes
    // (0x91-0x9c, shifted 0xb1-0xbc) because they were imported wholesale
    // from MMBasic - but every other special key here is already a VT100
    // sequence (the arrows emit CSI A..D), and one termcap entry has to
    // describe both this keyboard and a serial terminal on the same tty.
    // So emit what a terminal emits; a program wanting MMBasic's codes
    // maps them back itself.
    if (usage >= 0x3a && usage <= 0x45) {
        static const char ss3[4] = {'P', 'Q', 'R', 'S'};              // F1-F4
        static const uint8_t num[8] = {15, 17, 18, 19, 20, 21, 23, 24}; // F5-F12
        uint8_t buf[8];
        int n = 0, i;
        buf[n++] = 0x1b;
        if (usage <= 0x3d) {
            if (shift) { // xterm modifier form: CSI 1 ; 2 <letter>
                buf[n++] = '[';
                buf[n++] = '1';
                buf[n++] = ';';
                buf[n++] = '2';
            } else {
                buf[n++] = 'O';
            }
            buf[n++] = ss3[usage - 0x3a];
        } else {
            uint8_t v = num[usage - 0x3e]; // 16 and 22 are skipped, as on a VT220
            buf[n++] = '[';
            buf[n++] = '0' + v / 10;
            buf[n++] = '0' + v % 10;
            if (shift) {
                buf[n++] = ';';
                buf[n++] = '2';
            }
            buf[n++] = '~';
        }
        for (i = 0; i < n; i++) {
            kbd_push(buf[i]);
        }
        return;
    }
    // The table covers printing keys. Skip the caps-lock and editing/navigation
    // cluster (0x39, 0x49..0x52) so they fall to the escape-sequence switch
    // below - e.g. Delete emits "\x1b[3~" (forward delete), not 0x7f.
    if (usage <= 0x64 && usage != 0x39 && !(usage >= 0x49 && usage <= 0x52)) {
        bool letter = (usage >= 0x04 && usage <= 0x1d);
        bool eff_shift = (letter && kbd_caps) ? !shift : shift;
        int v = kbd_layout[usage * 2 + (eff_shift ? 1 : 0)];
        if (v != 0) {
            if (ctrl && ((v | 0x20) >= 'a' && (v | 0x20) <= 'z')) {
                v &= 0x1f; // Ctrl-<letter> -> 0x01..0x1a
            }
            kbd_push((uint8_t)v);
            return;
        }
    }
    // Keys with no printing value -> control codes / VT100 sequences.
    const char *seq = NULL;
    switch (usage) {
        case 0x28: seq = "\r"; break;      // enter
        case 0x29: seq = "\x1b"; break;    // escape
        case 0x2a: seq = "\x08"; break;    // backspace
        case 0x2b: seq = "\t"; break;      // tab
        case 0x4f: seq = "\x1b[C"; break;  // right
        case 0x50: seq = "\x1b[D"; break;  // left
        case 0x51: seq = "\x1b[B"; break;  // down
        case 0x52: seq = "\x1b[A"; break;  // up
        case 0x4a: seq = "\x1b[H"; break;  // home
        case 0x4d: seq = "\x1b[F"; break;  // end
        case 0x49: seq = "\x1b[2~"; break; // insert
        case 0x4c: seq = "\x1b[3~"; break; // delete
        case 0x4b: seq = "\x1b[5~"; break; // page up
        case 0x4e: seq = "\x1b[6~"; break; // page down
        default: return;
    }
    for (const char *p = seq; *p; p++) {
        kbd_push((uint8_t)*p);
    }
}

void kbd_process_report(const uint8_t *r, uint16_t len, int slot)
{
    if (len < 8) {
        return; // 8-byte boot report: [mods][reserved][keycode x6]
    }
    kbd_last_report = ticks_ms();       // the held-key state is now fresh
    uint8_t mods = r[0];
    const uint8_t *keys = r + 2;
    for (int i = 0; i < 6; i++) {
        uint8_t k = keys[i];
        if (k <= 1) {
            continue; // 0 = none, 1 = roll-over error
        }
        bool was_down = false;
        for (int j = 0; j < 6; j++) {
            if (kbd_prev[j] == k) {
                was_down = true;
                break;
            }
        }
        if (!was_down) { // newly pressed
            kbd_key(k, mods, slot);
            if (k != 0x39 && k != 0x53 && k != 0x47) { // no repeat for lock keys
                kbd_held = k; // last new key wins the repeat
                kbd_held_mods = mods;
                kbd_next_repeat = ticks_ms() + kbd_repeat_first;
            }
        }
    }
    if (kbd_held) { // stop repeating once the held key is released
        bool still = false;
        for (int i = 0; i < 6; i++) {
            if (keys[i] == kbd_held) {
                still = true;
                break;
            }
        }
        if (!still) {
            kbd_held = 0;
        }
    }
    memcpy(kbd_prev, keys, 6);

    // Update the held-key table (MMBasic KeyDown[]). Keycodes 1..3 are HID
    // error indicators - keep the previous state on such a report.
    int total = 0;
    for (int i = 0; i < 6; i++) {
        if (keys[i] > 0 && keys[i] < 4) {
            return;
        }
        if (keys[i]) {
            total++;
        }
    }
    for (int i = 0; i < 6; i++) {
        // Reverse report order, so keydown(1) is the most recent key.
        uint8_t k = (i < total) ? keys[total - i - 1] : 0;
        kbd_keydown[i] = k ? kbd_map_code(k, mods) : 0;
    }
    kbd_keydown[6] = ((mods & 0x04) ? 1 : 0)     // left Alt
        | ((mods & 0x01) ? 2 : 0)                // left Ctrl
        | ((mods & 0x08) ? 4 : 0)                // left GUI
        | ((mods & 0x02) ? 8 : 0)                // left Shift
        | ((mods & 0x40) ? 16 : 0)               // right Alt
        | ((mods & 0x10) ? 32 : 0)               // right Ctrl
        | ((mods & 0x80) ? 64 : 0)               // right GUI
        | ((mods & 0x20) ? 128 : 0);             // right Shift
}

// MMBasic fun_keydown semantics; kept for a future ioctl.
int usb_kbd_keydown(int n)
{
    if (n == 8) {
        return (kbd_caps ? 1 : 0) | (kbd_num ? 2 : 0) | (kbd_scroll ? 4 : 0);
    }
    if (n >= 1) {
        return kbd_keydown[n - 1];
    }
    int count = 0;
    for (int i = 0; i < 6; i++) {
        if (kbd_keydown[i]) {
            count++;
        }
    }
    return count;
}

// Called from the usb task (thread context) to synthesise auto-repeat.
//
// Only repeat while the held-key state is FRESH.  If reports have not
// been arriving, the release of the held key may have happened without
// being seen, and repeating then repeats a key that is no longer down:
// it showed up as one extra Return after "hdb2" at the boot prompt,
// where the pump is starved through mount and init and the stale
// repeat arrived just in time for login to read an empty line.
//
// Freshness is measured from the last REPORT, not from the last call
// to this function.  Timing the calls was wrong: the console mirrors
// every byte to the 115200 uart, so a full-screen repaint is ~3KB and
// takes ~260ms, during which nothing pumps - and that starved every
// repaint past the threshold, re-armed the delay each time, and made
// auto-repeat look broken.  Reports are polled every 20ms, so a stale
// window of 150ms means "several polls missed", which is what the
// boot-time case actually looks like.
void kbd_repeat_check(void)
{
    uint32_t now = ticks_ms();

    if (!kbd_held) {
        return;
    }
    if (now - kbd_last_report > KBD_REPEAT_STALE_MS) {
        kbd_next_repeat = now + kbd_repeat_first;
        return;
    }
    if ((int32_t)(now - kbd_next_repeat) >= 0) {
        kbd_key(kbd_held, kbd_held_mods, -1);
        kbd_next_repeat = now + kbd_repeat_next;
    }
}

// KBRATE (sys/kd.h): the standard Fuzix keyboard-rate ioctl, in tenths
// of a second.  Clamped so a bad value cannot wedge the keyboard into
// never repeating or repeating continuously.
void kbd_set_repeat(unsigned first_tenths, unsigned next_tenths)
{
    unsigned f = first_tenths * 100;
    unsigned n = next_tenths * 100;

    if (f < 100) f = 100;
    if (f > 2000) f = 2000;
    if (n < 25) n = 25;
    if (n > 2000) n = 2000;
    kbd_repeat_first = (uint16_t)f;
    kbd_repeat_next = (uint16_t)n;
}

// Lock-state bitmap in LED order (0x01 num, 0x02 caps, 0x04 scroll).
uint8_t kbd_led_bitmap(void)
{
    return (uint8_t)((kbd_num ? 0x01 : 0) | (kbd_caps ? 0x02 : 0) | (kbd_scroll ? 0x04 : 0));
}

// A keyboard went away (MMBasic clearrepeat): stop the auto-repeat...
void kbd_stop_repeat(void)
{
    kbd_held = 0;
}

// ...and no keys can still be held.
void kbd_clear_state(void)
{
    memset((void *)kbd_keydown, 0, sizeof(kbd_keydown));
    memset(kbd_prev, 0, sizeof(kbd_prev));
}
