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

### Where the tree is left

`PC3_NET=0` is the shipped kernel, unchanged. `PC3_NET=1` compiles and
links everything and then **fails the link by 15,544 bytes**, on
purpose: that failure is the current, honest state of the question. To
build a measurement image, widen the split by 20K - `memory_ram.incl`
to `RAM 0x31000` / `PROGPOOL 316k`, and `-DTOTALMEM=316` - which is how
the numbers above were taken.

## Step 1 - the radio, no stack

`cyw43_arch_init()`, join a network, print the MAC and the association
result. Proves the gSPI PIO divider at 375MHz (the SDK's fixed divider
of 2 is tuned for 125MHz and the symptom of getting this wrong is "hdr
mismatch" / ioctl timeouts - `CYW43_PIO_CLOCK_DIV_DYNAMIC` is already
set in the CMakeLists for it), that the PIO SM and 2 DMA channels do
not collide with sound and the scanout, that the pump keeps the driver
serviced, and that the PSU holds up under TX.

**Gate everything on `board_is_pc2()` first.** The CYW43 is on
GP23/24/25/29. On a Pico Computer 2 - same kernel image, see `board.c` -
GP29 is the SD card's chip select and GP25 is the LED. Bringing the
radio up there clocks the SD chip select at MHz. `board.c` currently
says "the CYW43 (PC3 only) is unused by Fuzix, so the SD wiring is the
only consequence"; that comment stops being true at step 1.

Credentials come from a file on the card, never from the repo.

## Step 2 - DHCP, then one packet each way

Lease printed, then an NTP query from a debug ioctl. NTP is the
cheapest end-to-end proof there is: one packet out, one back, ARP and
routing exercised, and the answer is independently checkable against
the DS3231.

Measure here, on the board: RAM, blocks lost, and the graphics stress
with the radio associated - both idle-polling and mid-transfer - while
looking at the screen. Scanout contention is this port's known sore
point and the packet path now shares the QMI with it.

## Step 3 - the PSRAM A/B

`LWIP_RAM_HEAP_POINTER` at an `arena.c` block plus `MEMP_MEM_MALLOC=1`,
then re-run step 2's measurements unchanged. Worth ~11K of SRAM.

Two things are already known about this:

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
