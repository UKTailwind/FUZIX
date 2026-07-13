# Amstrad CPC with Standard Memory Expansions

Currently, standard RAM expansions up to 1024 KiB are supported using the thunked memory model. Complete 64K blocks are alternated in the RAM space visible to the CPU. **This provides a total of 1088 KiB of usable RAM on all Amstrad CPC models.**

The memory model from the Cromemco port has been adapted to the Amstrad CPC hardware, and the drivers from the CPC 6128 port have been modified to work with this memory model.

The installed RAM size is detected dynamically during system initialization. The system should run with as little as 192 KiB of installed RAM, although only configurations with 256 KiB or more have been tested.

The first 64K block, which is the only one accessible by the video hardware, is reserved for VRAM. The video driver from the CPC 6128 port has been extended to handle **four simultaneous terminals** (accessible with CTRL+SHIFT+F1 to CTRL+SHIFT+F4).

The kernel reserves the second 64K block, leaving the remaining 64K blocks available for user processes. The upper 4K of each block are used as common memory, so 60 KiB are available to each resident process.

Disk swap is enabled and working. By combining banked RAM with swap, the system supports up to 16 simultaneous processes independently of the amount of resident RAM available, within the limits of the configured hardware and storage setup.

## Memory Model

```text
Bank 0      Video RAM
            0000-3FFF tty1
            4000-7FFF tty2
            8000-BFFF tty3
            C000-FFFF tty4

Bank 1      Kernel
            0000-00FF shared vectors / stubs
            0100-EFFF kernel
            F000-F1FF udata copy
            F200-FFFF common copy

Bank 2-16   User
            0000-00FF shared vectors / stubs
            0100-EFFF application
            F000-F1FF udata copy for this application
            F200-FFFF common copy
```

## Status

Video mode 2 is used. The video driver configures the CRTC for an 80x25 character display. Hardware scrolling is used. Ioctl calls for managing colours (paper, ink, and border) are implemented, so the `border`, `ink`, and `paper` commands work. Attribute rendering for underline and reverse is also implemented.

The CPC palette contains 27 colours, but FUZIX currently uses only a subset of 16:

| FUZIX colour | FUZIX name | CPC firmware colour | CPC colour name | CPC hardware value |
| ------------ | ---------- | ------------------- | --------------- | ------------------ |
| 0 | Black | 0 | Black | `&54` |
| 1 | Blue | 1 | Blue | `&44` |
| 2 | Red | 3 | Red | `&5C` |
| 3 | Magenta | 4 | Magenta | `&58` |
| 4 | Green | 9 | Green | `&56` |
| 5 | Cyan | 10 | Cyan | `&46` |
| 6 | Yellow | 12 | Yellow | `&5E` |
| 7 | White | 13 | White | `&40` |
| 8 | White | 13 | White | `&40` |
| 9 | Bright Blue | 2 | Bright Blue | `&55` |
| 10 | Bright Red | 6 | Bright Red | `&4C` |
| 11 | Bright Magenta | 8 | Bright Magenta | `&4D` |
| 12 | Bright Green | 18 | Bright Green | `&52` |
| 13 | Bright Cyan | 20 | Bright Cyan | `&53` |
| 14 | Bright Yellow | 24 | Bright Yellow | `&4A` |
| 15 | Bright White | 26 | Bright White | `&4B` |

There is a dedicated CPC terminal definition in `/etc/termcap` that exposes the CPC graphic glyphs for use as ACS characters. This helps curses applications and other terminal programs that use ACS or read terminal capabilities from termcap.

A `.profile` file is installed in `/root`. It sets up the included CPC terminal definition from `termcap` and configures the CET time zone with daylight saving time changes.

The floppy works. `/dev/fd0` is drive A and `/dev/fd1` is drive B. `fd0` is hardcoded for single-sided operation and `fd1` for double-sided operation. A minimal system root disk image is generated to boot from `fd1`. The format is 9 sectors per track with sector IDs from 1 to 9.

The IDE driver works and supports X-MASS, Symbiface IDE, and Cyboard IDE.

USB mass storage using the Albireo and the USIfAC2/ULIfAC with the CH376 module driver works. To support this, the CH375/CH376 driver used on other platforms has been modified to support several devices simultaneously over serial or parallel connections.

The M4 Board is partially supported. The SD card can be used as a storage device, and the NTP client can be used as an RTC source.

The Symbiface RTC module is supported.

An image file named `FUZIX.IMG`, placed in the root directory of the FAT filesystem on an Albireo/USIfACII/ULIfAC USB device or an M4 Board SD card, is supported as a block device.

The Makefile generates a DSK image (`fuzix.dsk`) that can be loaded from BASIC with the `RUN"FUZIX"` command. A snapshot is also generated as an alternative loading method.

To test it, write `disk.img` to your mass storage device. Then load and run the snapshot, or use the `fuzix.dsk` image to start it from BASIC.

Support has been added for the USIFAC serial port. If `CONFIG_USIFAC_SERIAL` is defined in `config.h`, the `tty5` device is added. To use the console on this device, modify the following line in `/etc/inittab`:

```text
05:3:off:getty /dev/tty5
```

to:

```text
05:3:respawn:getty /dev/tty5
```

When using the USIFAC serial console on `tty5` with PuTTY or another ANSI-capable terminal emulator, it is recommended to run:

```sh
TERM=xterm
export TERM
```

Using IBM code page 437 or 850 is also recommended for better rendering of line-drawing and character-based interface elements.

This has been tested with PuTTY, connecting the USIFAC to a Linux system using a USB-to-serial adapter, and it works very well. By default, the USIFAC is configured for 115200 baud with no flow control.

## TODO

Configurable screen support for other `LINES x COLS` combinations, video modes 1 and 0, and routines to support fonts with other resolutions.

Resolve the remaining network support issues. At the moment, neither the NET4CPC driver nor the SLIP driver using the serial port on the USIfAC/ULIfAC works correctly.

Add support for the M4 Board network hardware (WIP).

Fix lots of bugs.

Look for speed optimization opportunities.

## Build & Run

Install cpctools: [https://github.com/cpcsdk/cpctools](https://github.com/cpcsdk/cpctools)  
Install hex2bin: [https://github.com/algodesigner/hex2bin](https://github.com/algodesigner/hex2bin)  
Install iDSK: [https://github.com/cpcsdk/idsk](https://github.com/cpcsdk/idsk)  
Install `flip`.

```bash
make TARGET=cpcsme diskimage
```

The `.sna` snapshot, `.dsk` boot disk image, and mass storage filesystem images are generated in the `Images` folder.

The mass storage filesystem image `disk.img` can be transferred to a real device using `dd` or a similar utility, or copied as `FUZIX.IMG` to the root of the FAT filesystem managed by the M4 Board or by the CH376 on Albireo/USIfAC/ULIfAC devices.

To boot from floppy, or from the DSK image on M4/Albireo/USIfACII/ULIfAC, execute `RUN"FUZIX"` at the BASIC prompt.

To run in an emulator, use ACE-DL or the 1984 emulator and configure `disk.img` as the emulated mass storage image.

The 1984 repository also includes useful documentation for building FUZIX for the CPC: [https://github.com/salvogendut/1984/blob/main/docs/FUZIX_BUILD.md](https://github.com/salvogendut/1984/blob/main/docs/FUZIX_BUILD.md)