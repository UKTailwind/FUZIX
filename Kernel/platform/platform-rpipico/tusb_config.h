#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H
#include "stdint.h"
#include "config.h"

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
#define CFG_TUSB_DEBUG              1
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
