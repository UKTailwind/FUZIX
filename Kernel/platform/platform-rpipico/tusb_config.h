#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H
#include "stdint.h"
#include "config.h"

/* TinyUSB's panic() is Fuzix's, through mangle.h, and Fuzix's takes one
 * string: the arguments - which buffer control register, in the hcd's
 * "already available" - are dropped.  Route it through a kernel hook
 * (usbkbd.c) that prints the first argument, what that register holds,
 * the controller status and the caller, then panics as before.
 *
 * Only when TinyUSB's own include chain (tusb_option.h) brings this file
 * in: config.h includes it too, for the CFG_ values, and there the
 * kernel's mangle.h must have the last word - without this guard every
 * kernel file warned that "panic" was redefined and the hook lost. */
#ifndef PC3_CONFIG_H_INCLUDES_TUSB
#undef panic
void pc3_usb_panic(const char *fmt, ...);
#define panic pc3_usb_panic
#endif

#ifdef CONFIG_PC3_USB_KBD

/* Pico Computer 3: the USB controller faces the on-board 4-port hub and
 * runs in HOST mode for the keyboard. Values proven in the PC3
 * MicroPython port (shared/tinyusb/tusb_config.h), matching MMBasic. */
#define CFG_TUSB_RHPORT0_MODE       (OPT_MODE_HOST)
#define CFG_TUH_ENABLED             (1)
#define CFG_TUH_HUB                 (2) /* composite devices w/ built-in hub */
#define CFG_TUH_DEVICE_MAX          (3 * CFG_TUH_HUB + 1)
#define CFG_TUH_ENUMERATION_BUFSIZE (1024) /* composite descriptors exceed 256 */
#define CFG_TUH_HID                 (4 * CFG_TUH_DEVICE_MAX)
#define CFG_TUH_HID_EPIN_BUFSIZE    (64)
#define CFG_TUH_HID_EPOUT_BUFSIZE   (64)

/* PC3_USB_TRACE: the stack's own enumeration trace, through the small
 * printf in usbtrace.c.  A hardware debugging kernel only - verbose,
 * and it slows the pump - but it is what says where enumeration stops.
 * Build with: make SUBTARGET=pico2 PC3_USB_TRACE=1 */
#ifdef PC3_USB_TRACE
#define CFG_TUSB_DEBUG              2
extern int usb_trace_printf(const char *fmt, ...);
#define CFG_TUSB_DEBUG_PRINTF       usb_trace_printf
#endif

#else

#define CFG_TUSB_RHPORT0_MODE   (OPT_MODE_DEVICE)

#define CFG_TUD_CDC             (NUM_DEV_TTY_USB)
#define CFG_TUD_CDC_RX_BUFSIZE  (64)
#define CFG_TUD_CDC_TX_BUFSIZE  (64)

#endif

#endif
