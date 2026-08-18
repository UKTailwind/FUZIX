/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Pico Computer 3
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND...
 */

// The HID keyboard decoder core.  Vendored BYTE-IDENTICAL in the
// MicroPython rp2 port and the PC3 Fuzix kernel - see kbd_decode.h for
// the map of trees, backends and the sync check.  This file is plain C
// with no MicroPython, pico-sdk or Fuzix includes; anything a platform
// does differently comes in through the kbd_push/kbd_backend_* seam.
//
// Faithful port of MMBasic's keyboard handling (KeyboardMap.c):
// APP_MapKeyToUsage's layout mapping and AltGr specials, the lock keys
// and their LEDs, synthesised auto-repeat, and the KeyDown[] table.

#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "kbd_decode.h"

// HID modifier byte (report[0]) masks.
#define KBD_SHIFT (0x22) // left|right shift
#define KBD_CTRL  (0x11) // left|right ctrl

// Auto-repeat (typematic): HID keyboards only report on state change, so
// we synthesise repeats from the held key.
//
// These were MMBasic's (OPTION KEYBOARD REPEAT 600,150), and 150ms - under
// seven characters a second - is a typewriter's rate, not a machine's.  It
// is fine for typing and wrong for anything HELD: PETSCII Robots played
// from the keyboard crawled beside the same game played from the Game*Mite
// switches, which are read fresh every pass of the game loop with no
// typematic delay at all.  The player waited 600ms for the first step and
// then got one every 150ms.
//
// So: the Linux console's delay, and the fast repeat rate a PC keyboard
// controller offers (20 a second).  250ms is still far longer than a
// deliberate tap, so a keypress does not double.
//
// Runtime-adjustable through kbd_set_repeat (Fuzix: the KBRATE ioctl,
// /bin/kbdrate, in tenths of a second; kept in ms here) - note that the
// standard interface's TENTHS cannot express 50ms, which is why this is a
// default and not a program's job.
#define KBD_REPEAT_FIRST_DEFAULT (250)
#define KBD_REPEAT_NEXT_DEFAULT  (50)
uint16_t kbd_repeat_first = KBD_REPEAT_FIRST_DEFAULT;
uint16_t kbd_repeat_next = KBD_REPEAT_NEXT_DEFAULT;
// A poll gap longer than this means a release could have been missed,
// so the held key is no longer trustworthy for repeat purposes.
#define KBD_REPEAT_STALE_MS (150)
// Give up on a synthesised repeat when no HID report of ANY kind has
// arrived for this long (see the orphan guard in kbd_repeat_check).
#define KBD_REPEAT_ORPHAN_MS (30000)

static uint8_t kbd_prev[6];       // keycodes from the previous report
static bool kbd_caps;             // caps-lock state
static bool kbd_num = true;       // num-lock state (on at boot, as MMBasic)
static bool kbd_scroll;           // scroll-lock state
static uint8_t kbd_held;          // usage currently repeating (0 = none)
static uint8_t kbd_held_mods;
static uint32_t kbd_next_repeat;  // kbd_ticks_ms() of the next repeat
static uint32_t kbd_last_report_ms; // kbd_ticks_ms() of the last HID report

// Currently-held keys, MMBasic KeyDown[]: [0..5] = mapped key codes of the held
// keys, most recent first; [6] = modifier bitmap (1 L-Alt, 2 L-Ctrl, 4 L-GUI,
// 8 L-Shift, 16 R-Alt, 32 R-Ctrl, 64 R-GUI, 128 R-Shift).  Written by the
// report path, read through usb_kbd_keydown().
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
// AltGr, Ctrl, num-lock and caps-lock applied.  Faithful port of MMBasic
// APP_MapKeyToUsage (KeyboardMap.c).  Returns 0 for "no code" (lock keys,
// out-of-table usages, dead AltGr combinations).  This ONE function is the
// authority for both the typed-byte path (kbd_key) and the KeyDown[] table.
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
    // terminal sends.  The layout tables carry MMBasic's pseudo-ASCII codes
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
    // Printing keys: ONE translation - kbd_map_code, the faithful port of
    // MMBasic's APP_MapKeyToUsage - so AltGr, Ctrl, num-lock and caps-lock
    // all behave exactly as MMBasic.  (This path used to re-derive the
    // character from the bare layout table and skipped the AltGr specials:
    // on a Spanish layout AltGr+2 typed '2' instead of '@', and the
    // bracket/brace combinations - plain column 0 in the table - typed
    // nothing at all.  The specials sat unused in this same file.)
    // Skip caps-lock and the editing/navigation cluster (0x39, 0x49..0x52)
    // so they fall to the escape-sequence switch below - e.g. Delete emits
    // "\x1b[3~" (forward delete), not 0x7f.
    if (usage <= 0x64 && usage != 0x39 && !(usage >= 0x49 && usage <= 0x52)) {
        int v = kbd_map_code(usage, mods);
        if (v > 0) {
            kbd_push((uint8_t)v);
            return;
        }
        // v == 0: no printing value, or a dead AltGr combination (MMBasic
        // types nothing for those).  Fall through: the switch below has no
        // case for printing usages, so dead stays dead.
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
    kbd_last_report_ms = kbd_ticks_ms(); // the pipeline is provably alive
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
            int code = kbd_map_code(k, mods);
            if (code) {
                kbd_backend_on_key(code); // keyboard.on_key on MicroPython
            }
            if (k != 0x39 && k != 0x53 && k != 0x47) { // no repeat for lock keys
                kbd_held = k; // last new key wins the repeat
                kbd_held_mods = mods;
                kbd_next_repeat = kbd_ticks_ms() + kbd_repeat_first;
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

// MMBasic fun_keydown semantics: n=0 -> number of keys currently held,
// n=1..6 -> code of the nth held key (1 = most recent), n=7 -> modifier
// bitmap, n=8 -> lock bitmap (1 caps, 2 num, 4 scroll).
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

// Called from the platform's USB pump (thread context) to synthesise
// auto-repeat.
//
// A gap since the last call means the pump was starved, so the release
// of the held key may have happened without being seen; repeating then
// repeats a key that is no longer down.  That showed up as one extra
// Return after "hdb2" at the boot prompt, where the pump is starved
// through mount and init.
//
// Two things this must NOT do, both learned the hard way:
//
//  - It must not re-arm the whole first-repeat delay after a gap.  The
//    Fuzix console mirrors every byte to the 115200 uart, so a
//    full-screen repaint is ~3KB and takes ~260ms with nothing pumping;
//    re-arming on that cancelled auto-repeat on every repaint.
//  - It must not measure repeat FRESHNESS from the last report.  A HID
//    keyboard only reports on state change, so while a key is held
//    down no reports arrive at all - which is the whole reason repeats
//    are synthesised here.  That measure is stale precisely when a
//    repeat is due, and stops it dead.  (The 30s orphan guard below is
//    the opposite tool: it uses that same silence, at a timescale no
//    repaint or starvation reaches, to catch a DEAD report pipeline.)
//
// So: after a gap, hold off just long enough for the next poll (20ms)
// to deliver any pending release.  If the key really was let go,
// kbd_held clears before we fire; if it is still down, repeating
// resumes after one short hiccup instead of starting over.
#define KBD_REPEAT_SETTLE_MS (50)

void kbd_repeat_check(void)
{
    static uint32_t last_check;
    uint32_t now = kbd_ticks_ms();
    uint32_t gap = now - last_check;

    last_check = now;
    if (!kbd_held) {
        return;
    }
    /* Orphan guard.  A repeat is synthesised on trust that the key's
     * release will be seen.  A held key sends no reports - but its
     * release is one, and so is any other key.  If NOTHING has arrived
     * for 30 seconds while we repeat, either one key has been held for
     * half a minute or the report pipeline is dead - and the dead
     * pipeline used to type the last character forever, until the
     * power went.  Stop; a genuinely held key just needs pressing
     * again. */
    if ((uint32_t)(now - kbd_last_report_ms) > KBD_REPEAT_ORPHAN_MS) {
        kbd_held = 0;
        kbd_backend_msg("USB: keyboard silent while repeating - repeat stopped\n");
        return;
    }
    if (gap > KBD_REPEAT_STALE_MS) {
        /* Push the deadline out, never pull it in. */
        if ((int32_t)(kbd_next_repeat - (now + KBD_REPEAT_SETTLE_MS)) < 0) {
            kbd_next_repeat = now + KBD_REPEAT_SETTLE_MS;
        }
        return;
    }
    if ((int32_t)(now - kbd_next_repeat) >= 0) {
        kbd_key(kbd_held, kbd_held_mods, -1);
        int code = kbd_map_code(kbd_held, kbd_held_mods);
        if (code) {
            kbd_backend_on_key(code); // on_key sees repeats, like console input
        }
        kbd_next_repeat = now + kbd_repeat_next;
    }
}

// Repeat timing in tenths of a second (the Fuzix KBRATE / kbdrate
// convention).  Clamped so a bad value cannot wedge the keyboard into
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

// Seed num-lock (see kbd_decode.h): the platform's saved setting for the
// keyboard that just arrived.  LEDs are pushed by the caller.
void kbd_set_numlock(int on)
{
    kbd_num = on ? true : false;
}

// Does this keyboard declare a Num Lock LED?  A keyboard with no Num
// Lock LIGHT is very likely a keyboard with no numeric keypad, and both
// keyboards dumped so far declare exactly the lights they have
// (19 01 29 03 = Num/Caps/Scroll) rather than the HID spec's boilerplate
// 29 05 - so the LED block carries information the key block does not.
//
// This is the CONVERSE of what those dumps disprove, and only the
// converse holds: a declared Num Lock LED means nothing (the Raspberry
// Pi keyboard declares one and has no keypad), but an absent one is good
// evidence.  So it supplies the DEFAULT for a keyboard the platform has
// no setting for, never an override - and one Num Lock press corrects
// it either way, which is what makes guessing safe here at all.
//
// Walks the descriptor's short items tracking the current usage page and
// the pending local usages, and asks at each Output main item whether
// Num Lock (LED page 0x08, usage 0x01) is among the usages declared for
// it.  Returns 1 for "declares one", and for "cannot tell" - an absent
// descriptor must not be read as an absent keypad.
int kbd_has_numlock_led(const uint8_t *d, uint16_t len)
{
    if (d == NULL || len == 0) {
        return 1; // nothing to go on - MMBasic's default
    }
    uint16_t page = 0;      // current Usage Page (global)
    bool loc_num = false;   // Num Lock is among the pending local usages
    uint16_t umin = 0;      // pending Usage Minimum...
    uint16_t umin_page = 0; // ...and the page it was declared on
    bool umin_set = false;
    uint16_t i = 0;
    while (i < len) {
        uint8_t b = d[i];
        if (b == 0xfe) { // long item: [0xfe][data size][tag][data...]
            if (i + 1 >= len) {
                break;
            }
            i += 3 + d[i + 1];
            continue;
        }
        uint8_t size = b & 0x03;
        if (size == 3) {
            size = 4; // 0,1,2,3 encodes 0,1,2,4 data bytes
        }
        uint8_t type = (b >> 2) & 0x03; // 0 main, 1 global, 2 local
        uint8_t tag = (b >> 4) & 0x0f;
        if ((uint32_t)i + 1 + size > len) {
            break; // truncated item - stop rather than read past the end
        }
        uint32_t v = 0;
        for (uint8_t k = 0; k < size; k++) {
            v |= (uint32_t)d[i + 1 + k] << (8 * k);
        }
        if (type == 1 && tag == 0x0) { // Global: Usage Page
            page = (uint16_t)v;
        } else if (type == 2 && tag <= 0x2) { // Local: Usage / Minimum / Maximum
            // A 4-byte usage carries its own page in the top 16 bits.
            uint16_t upage = (size == 4) ? (uint16_t)(v >> 16) : page;
            uint16_t u = (uint16_t)v;
            if (tag == 0x0) {
                if (upage == 0x08 && u == 0x01) {
                    loc_num = true;
                }
            } else if (tag == 0x1) {
                umin = u;
                umin_page = upage;
                umin_set = true;
            } else { // Usage Maximum closes the range
                if (umin_set && umin_page == 0x08 && upage == 0x08
                    && umin <= 0x01 && u >= 0x01) {
                    loc_num = true;
                }
                umin_set = false;
            }
        } else if (type == 0) { // any Main item consumes the local state
            if (tag == 0x9 && loc_num) { // Output
                return 1;
            }
            loc_num = false;
            umin_set = false;
        }
        i += 1 + size;
    }
    return 0;
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
