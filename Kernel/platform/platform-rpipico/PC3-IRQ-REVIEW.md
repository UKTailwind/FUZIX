# Interrupt vectors on the PC3 port: what is actually happening

Written 2026-08-01, after an afternoon spent on a wrong premise. The
question asked was whether this port's hand-hooked vectors are a legacy
of the Z80-era Fuzix design and should migrate to the SDK's mechanisms.
This is what measurement says.

## The measurement

A probe compiled into `usbkbd_init()`, printed by the running board:

```
usb: vtor 20000000  ram_vt 20000000  slot14 200195DD  nvic 00004000
```

* `vtor` is `0x20000000` and `ram_vector_table` is `0x20000000`. **The
  SDK's RAM vector table is installed and in use.** The pre-init
  `runtime_init_install_ram_vector_table()` runs, copies the flash
  table, and points VTOR at RAM - `PICO_COPY_TO_RAM` does not disable
  it (only `PICO_NO_FLASH` or `PICO_NO_RAM_VECTOR_TABLE` would).
* Slot 14 (USBCTRL) holds `0x200195DD`, an address inside the SDK's
  `irq_handler_chain` slots. **TinyUSB's `irq_add_shared_handler()`
  worked**: its handler is installed and reachable.
* NVIC ISER bit 14 is set. **The interrupt is enabled.**

So on this port, today, the SDK's interrupt mechanisms function
normally. Handler installation at runtime works.

## Which means the comment in rawuart.c is wrong

`rawuart.c` says `irq_set_exclusive_handler()` "only does anything when
the SDK has built a writable RAM vector table. This kernel is
PICO_COPY_TO_RAM, and the call left the default handler in place."
That is not the case now, and the evidence above says it cannot have
been the mechanism at the time either - whatever the uart symptom was,
this explanation does not hold. `display.c` and `sound.c` both install
their DMA handlers with `irq_set_exclusive_handler()` and both work,
which should have been the clue.

The comment cost real time: it is why a missing USB interrupt looked
like the obvious explanation for the keyboard fault, and why a static
`isr_irq14` looked like the obvious fix.

## Why the static hook made things worse

Defining `isr_irq14` strongly puts a non-default handler in the slot
before TinyUSB gets there. `irq_add_shared_handler()` then has to build
a chain over an existing handler it did not install, and the SDK's
response to that is `panic()` - which on hardware with no debugger is a
`BKPT`, a HardFault with `HFSR.DEBUGEVT`, and a PC inside the SDK's
`vfctprintf` printing the message. That is exactly the boot crash
observed (`pc=2000f8ec`, `CFSR=0`, `HFSR=80000000`).

Two mechanisms for one slot is the bug, not either mechanism.

## What the port should do

The static hooking is a Fuzix convention - a Z80-era kernel owns its
vector table - and on this platform it is unnecessary for ordinary
peripherals. The SDK's model works and is what every other consumer of
the chip expects, TinyUSB included.

1. **Peripheral interrupts: use the SDK.** `irq_set_exclusive_handler()`
   as `display.c` and `sound.c` already do. Never define `isr_irqN` for
   a peripheral the SDK or a library also claims.
2. **Move the uarts across** and delete the misleading comment. They are
   the only statically hooked peripheral vectors left. This wants
   testing on hardware, not just a build.
3. **The exceptions stay hand-hooked.** `isr_svcall` and `isr_pendsv`
   are the kernel's own context-switch and pre-emption paths
   (`tricks.S`); they are not SDK-owned and there is nothing to gain
   from routing them through `irq_set_exclusive_handler`, which handles
   IRQs rather than system exceptions.
4. **Do not chase this into the SDK's runtime init.** The RAM vector
   table, the pre-init chain and the handler-chain slots all work as
   shipped.

## What this does NOT explain

The USB keyboard fault. The interrupt is delivered, so the cause is
inside enumeration, not below it. What is known:

* With the switch in the hub position and a keyboard attached after
  boot, `SIE_STATUS` reads `0x00000205` - VBUS, line state, and
  SPEED=2 (full speed). The controller sees the device.
* No `tuh_mount_cb` follows. Enumeration does not complete.
* `SIE_STATUS` later reads `0x47800205`: CRC, bit-stuff and
  RX-overflow errors latched.
* The idle pump rate rises from ~290/s to ~72,000/s, meaning `__wfi()`
  stops sleeping - an enabled interrupt is pending and re-asserting.

The PC3's USB switch makes this reproducible on demand: moving it from
the programming port to the hub is a clean port-connect event with
nothing else happening. That is the rig for the next session.

## Addendum, 2026-09-01: the third hcd panic - "Invalid speed"

A field report (v0.25, PC2 and PC3): booting with a USB keyboard
attached occasionally ends in `panic: Invalid speed` on the console, a
blank HDMI, and an fsck after the reset; attaching the keyboard after
login never does it. Reviewed rather than reproduced; the path is
unambiguous.

* The text is TinyUSB's, not the kernel's: `hcd_rp2040.c:456`,
  `hcd_port_speed_get()` panics when `SIE_STATUS.SPEED` reads 0 -
  which is what the field says while the root port is DISCONNECTED
  (it is a live RO field; the datasheet's 00).
* It is not just an enumeration query. `need_pre()` (line 84) reads it
  to decide the PREAMBLE bit at **every transfer start**: every SETUP
  (line 632), every control data stage (581), every interrupt endpoint
  opened (361). Between a physical disconnect and `tuh_task` processing
  the CONN_DIS event - and inside the enumeration's blocking delays,
  150 ms debounce and the reset waits, during which events queue - any
  transfer issued to the hub or to a keyboard behind it panics.
* What the panic becomes here: the SDK's stock `panic()` (no
  `PICO_PANIC_FUNCTION`) prints to a stdio nobody initialised, then
  `_exit(1)` -> `while(1) __breakpoint()` -> HardFault ->
  `fatal_exception_handler` dump -> Fuzix `panic("fatal exception")`
  -> `plt_monitor()` -> `multicore_reset_core1()`. That last call is
  the blank HDMI: the display core is stopped on purpose. core0 then
  sleeps until the user resets, with the root still mounted rw.
* Why boot with a keyboard: hub and keyboard power up together, the
  kernel then drives MMBasic's 20 ms SE0 bus reset on the root port
  and waits 50 ms, and the hub re-enumerates while its downstream
  ports (re)detect the keyboard. TinyUSB's hub driver switches PORT_POWER
  on port after port straight after reading the hub descriptor
  (`hub.c` `config_set_port_power`, no `bPwrOn2PwrGood` wait between
  ports), so a keyboard's inrush lands in the middle of the hub's own
  control transfers: a supply sag that drops the hub's upstream link for
  even a few microseconds is a root-port disconnect, and the next SETUP
  is the panic. After login the upstream link is stable and a
  downstream attach is handled through hub port status; the root port's
  speed never changes.

What was tried first, and why it was abandoned the same day: a
build-time patch of the 0.20 hcd returning `TUSB_SPEED_INVALID` instead
of panicking. On a PC2 whose keyboard is fine under v0.25 (3/3 boots)
that kernel lost the keyboard on every boot - mount at 1.4 s, root port
to SE0 at ~3.3 s, `detached`, dead - and the bisect was instructive:
the same arrangement with a comment-only patch (identical machine code
to v0.25 bar `__LINE__` constants, RAM layout shifted 56 bytes) booted
clean, an instrumented build of the full change kept its keyboard alive
past 80 s, and the two images differ in code only inside the speed
function's return path. So the 0.20 outcome is timing-dependent per
build, not caused by any one line - which is exactly what a driver that
retries a NAKing device every 16 µs with no way to yield EPX would
produce when a keyboard is slow to answer the LED report, and exactly
the shape of the field report (occasional, boot-time, keyboard
attached). The register evidence was: `SIE_STATUS` SPEED=0, LINE_STATE=
SE0, `INT_EP_CTRL`=0, `SOF_RD` frozen - the PHY seeing no pull-up, the
stack having closed everything.

Decision (2026-09-01): move the kernel to **TinyUSB 0.21**, whose
RP2040/RP2350 hcd was rewritten in Mar-May 2026 - EPX switching on the
RP2350's hardware `STOP_EPX_ON_NAK`, a 300 µs NAK poll while a transfer
is pending, `TUSB_SPEED_INVALID` instead of the panic, per-speed
SOF/keep-alive - and whose host stack no longer blocks inside
`tuh_task()`. `usbcheck.sh` asserts 0.21 now; the kernel and MMBasic
(still 0.20) no longer share a host-stack version. Still true in 0.21
and still masked here: the hcd `panic()`s on `ERROR_DATA_SEQ`, and
`HOST_RESUME` is enabled and never handled.

0.21 as shipped did not boot here: `panic: buf_ctrl @ 0x%lX already
available` the moment the host came up. TinyUSB's `panic()` is Fuzix's
through mangle.h and drops its arguments, so a hook (`tusb_config.h`
-> `pc3_usb_panic` in usbkbd.c) now prints the argument, its value, the
controller status and the caller before panicking. That gave:
`arg=50100080 *arg=00007400 SIE=40800205 BUF_STATUS=0 INT_EP_CTRL=0`
- EPX's buffer control holding a zero-length DATA1 IN with `AVAIL` still
set (a control transfer's status stage, never completed), no buffer
completion pending, RP2350's `ENDPOINT_ERROR` flagged, and the hub not
yet configured. The path: the 0.21 hcd's RX-timeout handler does
STOP_TRANS and completes the transfer as FAILED but clears
`epx_buf_ctrl` only in its EPX-switch path, never in the failure path;
usbh retries the request; the retry's first `bufctrl_write32` refuses
the stale `AVAIL`. 0.20 merely cleared the timeout bit and let the SIE
keep retrying, which is why it never met this. Fixed by
`usb/hcd_rp2040.patch` (build-time, see BUILDING-PC3.md): clear the
buffer when failing the stopped transfer. Also set: RP2350's
`LINESTATE_TUNING.MULTI_HUB_FIX`, the hardware's own answer to
turnaround timeouts through a hub. Masking the USB interrupt around
the kernel's SE0 bus reset at init (so the reset does not queue a
remove/attach pair of its own) was tried first and kept, but was not
the cause.

Result on the PC2: keyboard attached and working under 0.21. On a
power-on reset the console shows `attached / detached / attached`
before the root mounts - the keyboard or hub genuinely drops and
re-appears while enumerating, and the stack now re-enumerates it; on a
warm `reboot` the devices are stable and it attaches once. That drop is
the field report's trigger, and it is benign now.

## Addendum, 2026-09-03: the host hardening from the MicroPython port

The PC3 MicroPython port carried the same TinyUSB 0.21 further, applying
the hardening set the PicoMite validated on this hardware (its writeup is
`PicoMite/docs/usb-host-hardening.html`; the MicroPython story is
`ports/rp2/boards/PICO_COMPUTER_3/DEVELOPMENT_NOTES.md` §75). The
root cause it names sits under the panics above: the RP2 SIE keeps **one**
handshake-result latch shared between the control endpoint and the
interrupt-endpoint poller (TinyUSB #3533), so an interrupt poll can
overwrite a control transfer's ACK before the IRQ handler reads it — the
transfer is misread as `RX_TIMEOUT`. The 100 ms recovery delay was partly
outrunning that race, not just device wake-up.

Three portable pieces are now in this kernel's build-time patches:

- `usb/hcd_rp2040.patch` — an **EP0 RX-timeout grace window** (1 s, closed
  by any EP0 completion). A timeout on endpoint 0 leaves the transfer
  armed and outlasts the clobbered latch; expiry takes the original fail
  path (with the buffer clear from the first addendum). Bulk and interrupt
  keep the fast-fail — their flow control is NAK, never `RX_TIMEOUT`.
- `usb/usbh.patch` — **enumeration-exclusive control dispatch**: while a
  device enumerates, only address 0, the enumerating address and the hub
  in use may claim the control slot; other control traffic waits in the
  pending FIFO, with blocked entries rotated to the tail (gating the head
  deadlocks — the enumeration's own transfers can queue behind a blocked
  one). And a **failed enumeration disables its hub port**
  (`CLEAR_FEATURE(PORT_ENABLE)`, async non-NULL callback), so an abandoned
  device at address 0 cannot answer in parallel with the next bring-up.
- `tusb_config.h` — `CFG_TUH_CONTROL_PENDING_QUEUE_SZ` 4 → 8.

The MicroPython "keep the mount callbacks light" fix (a deferred-print
ring) is **not** ported: it addressed `mp_printf` re-entering the VM
through dupterm, which a kernel has no equivalent of, and this kernel's
callbacks are already light — `tuh_mount_cb`/`tuh_umount_cb` are no-ops,
only one keyboard is ever claimed, and the single `kputs` was proven on
the v0.26 cold boot. Not adopted, by the doc's own measurement: the #3533
reference driver rewrite (it enumerated *fewer* devices on the marginal
rig).

Board-proven on the PC3 (COM11, 2026-09-03): a clean boot with keyboard,
stick and touch on the hub (no panic), the console fully responsive under
load — the MicroPython port's first cut of this set wedged the console
here, so a live shell is the datapoint that matters — and the keyboard
typing. `SIE_STATUS=0x50800205`, `INT_EP_CTRL=0x06` (the one Corsair
keyboard's two HID interfaces; Fuzix claims a single keyboard and ignores
the rest by design, so that is a complete enumeration).
