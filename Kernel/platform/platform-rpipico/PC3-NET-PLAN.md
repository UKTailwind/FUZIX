# Networking on the Pico Computer 3

The board has a CYW43439 that Fuzix has never used. MicroPython and
MMBasic both drive it through the pico-sdk, so the hardware and the
driver are proven on this machine; what is not proven is that a
384K-of-SRAM Unix can afford a TCP/IP stack.

This file is the running record. It is written in the order the
questions have to be answered, and every number in it was measured on
this tree rather than estimated.

## The shape being proposed

Fuzix already has a socket layer. `Kernel/syscall_net.c`, `network.c`
and `include/netdev.h` are compiled by this port today and disabled by
one line in `config.h` (`#undef CONFIG_NET`). Sockets are inodes, so
`read`/`write`/`close` work on them already; the backend contract is 17
`netproto_*` hooks. `Library/libs` has `socket.c`, `resolv.c` and the
`inet_*` family, and `Applications/netd` has ping, telnet, httpd, dig,
ntpdate, htget and ifconfig - all of them level-1 code that does not
need `select`, which matters because `CONFIG_LEVEL_2` is off here.

So the work is a backend and a driver, not a subsystem:

    Fuzix socket layer (unchanged)
        netproto_* hooks
            net_lwip.c            <- to be written
                lwIP raw API, NO_SYS=1
                    cyw43-driver, pico_cyw43_arch_lwip_poll

This is the same relationship `dev/net/net_w5x00.c` has with a W5500:
the socket layer keeps its state machine, the backend owns the buffers.
lwIP is an offload engine that happens to run on our own CPU.

The poll flavour of the arch layer is deliberate. It has no
async_context worker threads and takes no alarm-pool entries (this port
allows itself two, one of which is the system tick). lwIP gets driven
from the pump this kernel already has - `plt_idle`, and PendSV when a
spinning process starves it, which is exactly where `tuh_task()` is
called from. MMBasic's WEB builds make the same choice.

## Step 0 - what it costs. DONE.

Measured, not guessed. `cmake -DPC3_NET=1` links the CYW43 driver and
lwIP with the smallest configuration that could still do the job
(`lwipopts.h`: 4 pbufs, a 4000-byte heap, 8 TCP PCBs, no IPv6, no DNS,
no TLS). `net_cyw43.c` calls into it from `main.c` but returns
immediately, so nothing runs and nothing touches a pin - the point is
to make the linker commit the memory so the map can be read.

Control is the same tree with `PC3_NET=0`, which is byte-identical to
the shipped v0.17 kernel apart from the `__TIME__` stamp (5 bytes).

| | control | +network | delta |
|---|---|---|---|
| flash image | 188,048 | 475,372 | **+287,324** (45% of the 1M before FLASH_OFFSET) |
| RAM `.data` (code+rodata resident) | 73,096 | 74,720 | +1,624 |
| RAM `.bss` (static buffers) | 104,904 | 120,264 | **+15,360** |
| RAM free in the region | 1,952 | *(overflows)* | **-15,544** |

**The first link fails, and that is the first result.** Without
placement the SDK's RAM `.data` section claims every `.text` and
`.rodata` not named in `linker_overrides/`, including the 230K firmware
blob: *"region RAM overflowed by 300312 bytes"*. With the network
objects named for flash (both `_excludes.incl` files now carry the
patterns) the overflow drops to 15,544 bytes, all of it static buffers.

Total additional SRAM demand: **16,984 bytes**, or five 4K blocks off
the 84-block process pool.

### What those 15,360 bytes actually are

    6,131  memp_memory_PBUF_POOL_base     4 receive pbufs
    4,019  ram_heap                       lwIP's own heap (MEM_SIZE)
    2,448  cyw43_state                    driver state and bus buffers
    1,251  memp_memory_TCP_PCB_base       8 TCP PCBs
    1,592  the other memp pools, dhcp, async_context, netif

Two thirds of it is the pbuf pool and lwIP's heap, and almost all the
rest is the memp pools. Those are exactly the things
`LWIP_RAM_HEAP_POINTER` + `MEMP_MEM_MALLOC=1` can move somewhere else.
What genuinely cannot leave SRAM is `cyw43_state`, because it holds the
buffers the PIO/DMA bus transfers use - about 2.5K.

So the PSRAM experiment (step 3) is not shaving a few per cent off a
17K bill; it is the difference between five blocks and one.

### Three ways to pay, in the order the port's own rules prefer

`linker_overrides/memory_ram.incl` says it outright: *"The valve when
that happens is to move more code to flash rather than to take memory
back from the pool."*

1. **More kernel code to flash.** 74,720 bytes of kernel text still run
   from RAM. Finding 17K of it that a cache miss cannot hurt would pay
   the whole bill with nothing taken from anybody. Note that 1,624 of
   the delta above is itself new RAM-resident code - gpio, sem,
   unique_id and the pico_lwip glue, none of which is named for flash
   yet - so part of this is just finishing the job started here.
2. **PSRAM for lwIP's pools.** Worth ~11K of the 15,360 (step 3).
3. **The process pool.** 336K -> 316K is five blocks. robots.bas needs
   73 of the 84, so five blocks is affordable but not free, and it is
   the option to reach for last.

### Where the 1,624 bytes of new RAM-resident code came from

Worth knowing, because it is not what it looks like. The largest single
item is 606 bytes of **merged string pool** - GCC emits string literals
into `.rodata.<func>.str1.4` sections which ld merges into one output
chunk, and the map attributes the whole chunk to its first contributor
(`devices.c.o`). So lwIP's and the cyw43 driver's assertion and warning
strings land in the kernel's RAM-resident string pool.

That pool is 8,330 bytes and it is *all* in RAM, which looks like two
free blocks waiting to be claimed. It is not: `rawflash.c` and dhara
are deliberately RAM-resident because they run while the flash is
erasing, and a `kprintf` on that path reading its format string from
XIP would hang the machine. Moving the pool wholesale is therefore not
a free win, and that is presumably why it was never done. If those 606
bytes ever matter on their own, `LWIP_NOASSERT` drops most of them.

The rest is ordinary: sem 246, unique_id 132, gpio 108, dma 108,
claim 79, irq 32, pio 19 - SDK helpers that were never referenced
before and are not named for flash.

## Step 1 - the radio, no stack. ON THE BOARD, 2026-08-20.

Everything below is verified on hardware except joining a real network,
which needs credentials. What the board says:

    # free
             total         used         free
    Mem:       332           76          256
    Swap:     7980            0         7980
    # wifi pc3-net-probe wrongkey 2
    joining
    no such network
    # wifi
    no such network
    mac 28:cd:c1:19:4e:c2
    ip      0.0.0.0
    netmask 255.255.255.0
    gateway 192.168.0.1

That is a lot of the design confirmed in three lines. The firmware
uploaded over the gSPI link at 375MHz, so `net_set_pio_clkdiv` is
right and there is no "hdr mismatch". lwIP came up with its heap in
PSRAM. The PIO state machine and the DMA channels were claimed without
disturbing sound or the scanout. The chip answered with its own MAC.
And the association state advanced from "joining" to a verdict while
`wifi` was asleep in `sleep(1)` - which can only happen if
`pc3_net_poll()` is being called from `plt_idle`, so the pump works.

`free` is the flash proof, not the banner: 336 -> 332 is the block the
process pool gave up, and 8048 -> 7980 is the PSRAM the lwIP heap took
off the top of the swap disc. Before flashing, the same `wifi` binary
on the old kernel said "NETIOC_STATUS: Not a typewriter", which is the
right answer from a kernel that has never heard of the ioctl.

**One bug, found by running it.** The first run printed `netmask
0.255.255.255` and `gateway 1.0.168.192`: `ip4_addr_get_u32` returns
the address in network order and `struct net_status` says host order.
Fixed with `lwip_ntohl` in the kernel rather than by teaching every
reader the convention. The placeholder mask and gateway that show
before any association are not a second bug - `cyw43_lwip.c` always
`netif_add`s with its `CYW43_DEFAULT_IP_*` values and DHCP overwrites
them on the lease.

Still unverified: an actual association and lease, whether the USB
keyboard survives the firmware upload (the console did), and what the
display does with the radio associated.

### How it was written

Three ioctls on `/dev/sys` (`net_cyw43.c`, dispatched from `misc.c`)
and a userland tool:

    NETIOC_UP      0x0040   struct net_join    power up, start joining
    NETIOC_STATUS  0x0041   struct net_status  link state, MAC, lease, RSSI
    NETIOC_DOWN    0x0042                      leave

    utils/wifi.c:  wifi                     status
                   wifi <ssid> <key> [auth] join and wait for an address
                   wifi -f [file]           join from /etc/wifi.conf (0600)
                   wifi down

**Joining is asynchronous.** `NETIOC_UP` starts the association and
returns; the waiting is done by `wifi`, polling `NETIOC_STATUS` once a
second. That is not tidiness - a Fuzix syscall is not preempted, so a
blocking join would stop the whole machine, keyboard included, for the
length of a DHCP negotiation. The one thing that does block is the
first `NETIOC_UP` of a boot, which uploads ~230K of firmware to the
chip: a few hundred milliseconds, once.

The pump is wired into both thread-context sites that already pump
TinyUSB - `plt_idle` (usbkbd.c) and `preempt_handler` (misc.c). The
second matters as much as the first: without it a process that never
idles and never sleeps on the tty would starve lwIP's timers, and TCP
would stall for the length of a compile. `cyw43_arch_poll()` runs
lwIP's timers too, via the poll async_context, so there is no separate
`sys_check_timeouts()` to make.

**Gated on `board_is_pc2()`**, checked at `NETIOC_UP` rather than at
boot - `board_detect()` runs from `device_init()`, long after
`plt_init`, so there is no honest answer to give earlier. The comments
in `board.c` and `main.c` that said the radio was unused have been
corrected rather than left to rot.

### The trap this hit: two sets of headers

`net_cyw43.c` includes **no Fuzix kernel headers at all**, and that is
load-bearing. lwIP and the pico-sdk pull in newlib's `<stdio.h>` and
`<sys/time.h>`, whose `ssize_t` and `time_t` are not the kernel's, and
one translation unit containing both is a wall of conflicting-types
errors. So the SDK world lives in that file behind plain `int` returns
(`PC3_NET_E*` in `pico_ioctl.h`), and `misc.c` does the `uget`/`uput`
and the errno translation. The auth constants are mapped on the SDK
side for the same reason.

It cost nothing in RAM: `.data` and `.bss` are byte-identical to the
step 3 measurement, because all of it is named for flash.

### What running it should show

    # wifi <ssid> <key>
    joining
    joined, no address
    up
    mac 28:cd:c1:xx:xx:xx
    ip      192.168.1.42
    netmask 255.255.255.0
    gateway 192.168.1.1

and `wifi` alone reports the same afterwards. Failures to expect first:
"hdr mismatch" or an ioctl timeout means the gSPI PIO divider is wrong
for 375MHz (`net_set_pio_clkdiv`, MMBasic's algorithm); a hang at
`NETIOC_UP` with no message means the firmware upload is not getting
through at all.

Watch three things beyond whether it joins: whether the keyboard
survives the firmware upload, whether the display flecks while the
radio is associated and idle-polling, and what the PSU does under TX.

## Step 2 - DHCP, then one packet each way

Lease printed, then an NTP query from a debug ioctl. NTP is the
cheapest end-to-end proof there is: one packet out, one back, ARP and
routing exercised, and the answer is independently checkable against
the DS3231.

Measure here, on the board: RAM, blocks lost, and the graphics stress
with the radio associated - both idle-polling and mid-transfer - while
looking at the screen. Scanout contention is this port's known sore
point and the packet path now shares the QMI with it.

## Step 3 - lwIP's memory into PSRAM. DONE, and taken out of order.

Done before step 1 deliberately: it decides whether this feature costs
the process pool one block or five, it is pure build-side work with no
hardware risk, and step 1 needs a board in front of you.

`MEMP_MEM_MALLOC=1` makes every lwIP pool - pbufs, PCBs, TCP segments -
come from lwIP's heap instead of being its own static array, and
`LWIP_RAM_HEAP_POINTER` points that heap at `pc3_lwip_heap`, a 64K
array `net_cyw43.c` places in the PSRAM window with the SDK's
`__uninitialized_psram`. That is the same mechanism `display.c` uses
for the spare framebuffers, and `psram.c`'s `psram_static_len()` then
moves the PSRAM block device and the arena allocator up above it
without being told anything.

| | SRAM-only | lwIP in PSRAM |
|---|---|---|
| RAM `.bss` delta | +15,360 | **+2,984** |
| RAM `.data` delta | +1,624 | +1,624 |
| total extra SRAM | 16,984 | **4,608** |
| process pool | 336K -> 316K (**5 blocks**) | 336K -> 332K (**1 block**) |
| PSRAM used | 0 | 65,600 (0.8%, off the top of the swap disc) |

What is left in SRAM is `cyw43_state` (2,448 - driver state and the
buffers the PIO and DMA reach, which cannot move), the 1,624 of code
above, and about 500 bytes of small statics. The RAM region ends with
1,440 bytes free, which is the budget steps 1-4 have to fit in.

`PC3_NET=1` now builds with one flag and no manual edits:
`linker_overrides_net/memory_ram.incl` carves the 332K split and is
added ahead of the base override directory so it shadows that one file,
and the CMakeLists sets `TOTALMEM 332` to match. A `PC3_NET=0` build
never sees either and is byte-identical to the shipped kernel apart
from the `__TIME__` stamp - verified from a clean build directory.

Two things were known before starting, and both held:

* **No DMA ever touches a pbuf.** `cyw43_lwip.c` fills receive pbufs
  with `pbuf_take` (a memcpy) and transmits by copying out through the
  driver's own buffer, so PSRAM placement raises no coherence question.
  Only `cyw43_state` has to stay in SRAM.
* **MMBasic did not do this, and its reason does not apply to us.** Its
  lwIP heap stays on the C heap because the MMBasic heap is wiped by
  `InitHeap(true)` on every program RUN, and lwIP holds persistent
  state (the netif's DHCP struct) across runs; a second reason is that
  its heap hands out 256-byte pages, which suits lwIP's many small
  objects badly. Neither is true of a kernel arena handed to lwIP's own
  `mem.c`. What MMBasic *does* prove is that network-path data works
  from PSRAM at speed - its TLS record buffers and cert parsing live
  there.

**Still to verify on the board.** This is a link-time result. Nothing
has moved a packet, so the cost of putting the packet path on the QMI
next to the scanout is still unmeasured, and that is step 2's job: the
graphics stress with the radio associated, idle-polling and
mid-transfer, watching the screen. If PSRAM pbufs fleck the display,
the fallback is not "back to SRAM" - it is the five-block version, and
that is a decision with a number attached rather than a guess.

## Step 4 - the first socket

`CONFIG_NET` on, `net_lwip.c` with the UDP path only (bind/sendto/
recvfrom), and run the *existing* `ntpdate` and `dig` from userland.
Then TCP client (htget, telnet), then listen/accept (httpd).

Association has no place in Fuzix's ioctl set: put `PIOC_WIFI_JOIN` /
`_STATUS` / `_SCAN` on the platform `/dev/sys` ioctl (`pico_ioctl.h`,
covered by `ioctlcheck.sh`), the way sound and i2c already do it, and
keep `net_lwip.c` itself platform-neutral so it could serve other
Fuzix ports.

## Not now

TLS (mbedtls alone wants 40K of lwIP heap for one handshake), IPv6, AP
mode, Bluetooth, and a BASIC-facing command set.
