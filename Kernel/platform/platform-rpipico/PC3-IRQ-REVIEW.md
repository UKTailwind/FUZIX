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
