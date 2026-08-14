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

// The HID keyboard decoder - ONE core, vendored BYTE-IDENTICAL in two trees:
//
//   micropython/ports/rp2/kbd_decode.{c,h}      board USB host (mp_usbh.c)
//                                               and the PC emulator (kbd_sdl.c)
//   FUZIX .../platform-rpipico/kbd_decode.{c,h} the PC3 Fuzix kernel (usbkbd.c)
//
// It is a faithful port of MMBasic's keyboard handling: layout mapping
// (keyboard_maps.h, also vendored identically), the AltGr specials, lock
// keys + LEDs, auto-repeat, and the held-key table behind KEYDOWN().
// Everything platform-specific sits behind the kbd_push/kbd_backend_*
// seam below - kbd_backend.c on MicroPython (the emulator shares it),
// usbkbd.c on Fuzix.  Do not edit one copy without the other: kbdsync.sh
// in the Fuzix platform compares the hashes.
#ifndef MICROPY_INCLUDED_RP2_KBD_DECODE_H
#define MICROPY_INCLUDED_RP2_KBD_DECODE_H

#include <stdint.h>

// Active layout table + name, owned by the platform (usb_keyboard.c on
// MicroPython, usbkbd.c on Fuzix).
extern const int *kbd_layout;
extern const char *kbd_layout_name;

// Auto-repeat timing in ms: MMBasic's defaults (OPTION KEYBOARD REPEAT
// 600,150).  Runtime-adjustable; Fuzix drives this from the KBRATE ioctl
// via kbd_set_repeat.
extern uint16_t kbd_repeat_first;
extern uint16_t kbd_repeat_next;

// Feed one 8-byte HID boot report ([mods][res][key x6]). slot is the HID
// slot for lock-LED updates, or -1.
void kbd_process_report(const uint8_t *r, uint16_t len, int slot);
// Synthesise auto-repeat for the held key; call every few ms.
void kbd_repeat_check(void);
// KEYDOWN(n) backend (MMBasic fun_keydown semantics).
int usb_kbd_keydown(int n);
// Lock-state bitmap in LED order: 0x01 num, 0x02 caps, 0x04 scroll.
uint8_t kbd_led_bitmap(void);
// A keyboard went away: stop auto-repeat / clear the held-key state.
void kbd_stop_repeat(void);
void kbd_clear_state(void);
// Set the repeat timing in tenths of a second (the KBRATE convention),
// clamped so a bad value cannot wedge the keyboard.
void kbd_set_repeat(unsigned first_tenths, unsigned next_tenths);

// --- The backend seam: provided by the platform. ---
// Push one translated byte to the console/stdin input.
void kbd_push(uint8_t c);
// Push the LED bitmap to the keyboard at `slot` (TinyUSB on hardware;
// no-op on the emulator).
void kbd_backend_set_leds(int slot, uint8_t leds);
// A millisecond tick for the repeat engine.
uint32_t kbd_ticks_ms(void);
// A key event with its mapped code (each press and each synthesised
// repeat): MicroPython schedules keyboard.on_key; Fuzix ignores it.
void kbd_backend_on_key(int code);
// One short diagnostic line (the orphaned-repeat guard says when it
// fires): kputs on Fuzix, mp_printf on MicroPython.
void kbd_backend_msg(const char *s);

#endif // MICROPY_INCLUDED_RP2_KBD_DECODE_H
