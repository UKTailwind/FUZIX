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

// The HID keyboard decoder, shared between the USB host (mp_usbh.c) and the
// PC emulator's SDL keyboard: layout mapping (keyboard_maps.h), lock keys,
// auto-repeat, the held-key table behind keyboard.keydown(), the on_key
// callback, and translated bytes pushed to the stdin ring buffer. The only
// hardware touch -- driving the keyboard's lock LEDs -- is behind
// kbd_backend_set_leds().
#ifndef MICROPY_INCLUDED_RP2_KBD_DECODE_H
#define MICROPY_INCLUDED_RP2_KBD_DECODE_H

#include <stdint.h>

// Active layout table + name, owned by usb_keyboard.c (the keyboard module).
extern const int *kbd_layout;
extern const char *kbd_layout_name;

// Feed one 8-byte HID boot report ([mods][res][key x6]). slot is the HID
// slot for lock-LED updates, or -1.
void kbd_process_report(const uint8_t *r, uint16_t len, int slot);
// Synthesise auto-repeat for the held key; call every few ms.
void kbd_repeat_check(void);
// keyboard.keydown(n) backend (MMBasic KEYDOWN semantics).
int usb_kbd_keydown(int n);
// Lock-state bitmap in LED order: 0x01 num, 0x02 caps, 0x04 scroll.
uint8_t kbd_led_bitmap(void);
// A keyboard went away: stop auto-repeat / clear the held-key state.
void kbd_stop_repeat(void);
void kbd_clear_state(void);

// Backend: push the LED bitmap to the keyboard at `slot` (TinyUSB on rp2;
// no-op on the emulator).
void kbd_backend_set_leds(int slot, uint8_t leds);

#endif // MICROPY_INCLUDED_RP2_KBD_DECODE_H
