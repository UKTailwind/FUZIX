# Booking Management System for Fuzix/Linux

The Booking System is a retro 1980's style lightweight multi-user Booking Management System written in C for the Fuzix operating system but will run just as well on most Linux systems. It is developed and built on Debian and cross compiled for Fuzix.

Designed with text based screens (80x25) for low-resource hardware and UART terminal environments, this application provides a fast text-based interface for managing generic bookings, customers, and related records using an indexed flat-file database.
The project is currently being developed and tested primarily for the Raspberry Pi Pico running Fuzix.

For the full green screen effect see details below for how to install and run RPTerm on a seperate Pico for a standalone client server application.

# Downloads
* Ready to run FUZIX kernel tweaked for the Booking System Demo available here (fuzix.uf2)
This can be found in the booking source code ./bin folder.

* Ready to run FUZIX filesystem with the booking system compiled in (filesystem.img)
This can be found in the booking source code ./bin folder.

* Ready to run RPTerm image (.uf2)
This can be found in the booking source code ./bin folder.

* Modified Source code for FUZIX with the booking system included available here
<TODO> I will submit the booking source code for inclusion in the FUZIX code base.

* Modified Source code for RPTerm terminal emulator with minor changes not submitted to the main branch
You won't need this unless you want to modify settings and recompile.
<TODO>

# Original Source
You won't need this unless you want to compile FUZIX and/or RPTerm from Scratch
* Original FUZIX source code available here
https://codeberg.org/EtchedPixels/FUZIX

* Original RPTerm source code available here
https://github.com/dquadros/RPTerm

---

# Features
## Current Features
* Booking List screen 
* Booking Detail screen (Create Edit View)
* Customer List screen
* Customer Detail screen (create Edit View)
* Customer selection mode integrated with booking entry
* Staff selection screen
* Booking status selection screen

##Features
* Reusable terminal UI framework
* Keyboard abstraction layer
* Indexed flat-file database access
* Multi-user aware file handling
* UART terminal support

## Planned Booking Features
* Staff management
* Invoice management
* Additional reusable database infrastructure
* Improved reporting and search tools
* Print capabilities
---

# Design Goals
This project focuses on:

* Small memory footprint
* Simple, extendable and maintainable C code
* Fast terminal response over serial connections
* Compatibility with low-resource systems
* Avoiding unnecessary dependencies
* Reusable UI and database components
* Multi user, tested with 3 users on Raspberry Pi Pico.
* Keyboard driven interface optimised for VT100-style terminals.
---

# Project Structure

Typical source layout:

```text
booking/
├── main.c
├── booking_list.c
├── booking_detail.c
├── customer_list.c
├── customer_detail.c
├── data
    ├── booking.db
    ├── customer.db
    ├── staff.db
    ├── state.db
├── db_*.c
├── ui.c
├── debug.c
└── Makefile
```
---

# Building Fuzix Host on Debian
The application is intended to be built as part of the Fuzix Applications tree.
Follow the main README.md in the top level FUZIX folder for full build instructions.
All the files required to build the booking program are contained within the "booking" sub folder except for one.
For the build to include the 'booking' system you must edit the parent Makefile and include the application.
./FUZIX/Applications/Makefile
Change the APPS= line to include booking. I also included the game '2048' just for fun.
```text
APPS = util cmd sh games cave cpm v7games games cursesgames \
       as09 ld09 netd MWC flashrom ue cpmfs plato \
       emulators cpnet dw assembler CC cpp ar \
       2048 booking
```

If you really want to build your own here are the build commands I have used as an example:

```bash
cd FUZIX
make TARGET=rpipico clean
make TARGET=rpipico SUBTARGET=pico_w FSMEDIA=sd CPU=armm0 diskimage V=1
```
# Testing on Debian 
Alternately the system can be tested directly on Debian using the included build.sh to compile an executable built for Linux.
    Once compiled the bin folder contains the executable and the data folder contains the test data.
    Run directly from the booking folder using the command ./bin/booking to start the application.
    The data folder is expected to be a sub folder or the booking folder.

If you don't want to build your own executable I have included examples that I have tested on a Pico W however it should also run on the plan Pico. The wifi chip is not required.  Downloads are listed towards the top of this file.

# Installing Fuzix on the Pico
The above build commands will create the following output files.
 Operating System ./Kernel/platform/platform-rpipico/build/fuzix.uf2
 File system      ./Kernel/platform/platform-rpipico/filesystem.img

The fuzic.uf2 file is copied onto the pico's onboard flash storage by powering on the pico in bootsel mode and dropping the file onto the device.

Next the file system needs to be copied into it's own partition on a SD card using the dd command. The built in flash storage is too small and flaky for the file system using read write operations. 

A second partition for the sawp file is also required to allow for limited available memory.
To install you will need an SD card reader on your host build system.

The two SD partitions can be created using the fdisk utility. Change /dev/sde to suite your build environment.
```bash
sudo fdisk /dev/sde
```
Note: all existing information on the card will be erased.
Use the following commands within fdisk.
d = remove existing partition(s)
n = create new "linux" 32MB partition using +32M otherwise accept the defaults to all the questions.
n = create new "linux" 2MB partition using +2M otherwise accept the defaults to all the questions.
p = print the partition table

The partition table should look something like this once complete
Device     Boot Start   End Sectors Size Id Type
/dev/sde1        2048 67583   65536  32M 83 Linux
/dev/sde2       67584 71679    4096   2M 83 Linux

w = write out the partition.
Note: You don't need to format the partitions.


Next copy the filesystem to to the SD. 
Note: The sd cards partitions do not need to be mounted during the copy.

Change /dev/sde1 to suite your build environment and the location of your filesystem.img.
```bash
sudo dd if=Kernel/platform/platform-rpipico/filesystem.img  of=/dev/sde1 bs=4M status=progress
```
Up to this point the pico should now have the kernel in the flash memory, the filesystem on the SD's first partition and the scecond partition ready for the swap file to be created automatically.
---

# Runtime Requirements
* ANSI compatible terminal emulation
I have tested with picocom on Debian but you could use putty on Windows.
The code will also compile and execute directly on Debian in GNOME Terminal.

* Default Wiring
   Connect SD card reader to pico
   From left to right with the pins facing up.

## SD Card Adapter Wiring (Raspberry Pi Pico SPI1)
The Booking Management System stores its database files on a microSD card connected via SPI1.
### Wiring
Check your card's pin configureation.  This what my el-cheapo model uses.
From left to right with the pins facing up.
```text
| SD Adapter Pin | Pico Pin        | GPIO     |
| -------------- | --------------- | -------- |
| 3V3            | Physical Pin 25 | 3V3(OUT) |
| CLK / SCK      | GPIO13          | SPI1 SCK |
| CS             | GPIO15          | SPI1 CS  |
| MOSI / DI      | GPIO14          | SPI1 TX  |
| MISO / DO      | GPIO12          | SPI1 RX  |
| GND            | Any GND         | Ground   |
```
### Notes
* The SD card adapter used during development was an unlabeled 6-pin SPI module.
* SPI1 was selected to avoid conflicts with UART terminal connections.
* The system has been tested successfully with Fuzix on the Raspberry Pi Pico.
*  Connect USB terminal(s) to PC. Up to 4 connections are available via the standard usb cable
   
## Connect UART Terminal(s) to pico
GPIO Pin
UART 0 TX PIN 0
UART 0 RX PIN 1
UART 1 TX PIN 4
UART 1 RX PIN 5

Don't forget to cross TX and RX on the Terminal end.
Don't forget a ground!

## PC Console connection
Power your Pico from a usb connection to your pc

* Default usb Console command on Debian
You could also connect from a terminal like Putty using windows but I didn't test this.
```
    picocom -b 115200 /dev/ttyACM0
```
* Alternate usb terminal connections on Debian
```
    picocom -b 115200 /dev/ttyACM1
    picocom -b 115200 /dev/ttyACM2
    picocom -b 115200 /dev/ttyACM3
```
* Uart Terminal Connection Command
    UART terminal command on Debian
```
    picocom -b 115200 /dev/ttyUSB0  #(Default console if ACM0 not present)
    picocom -b 115200 /dev/ttyUSB2
```
This could provide a maximum of 6 simultaneous terminal connections but I only tested with 3 concurrent sessions.

# Fuzix Boot Options
## Initial Tests
Connect one end of your usb cable to your pc. (This example uses Debian)
Type this command at a terminal window ready for the following step.
```
    picocom -b 115200 /dev/ttyACM0
```
Connect the other end to power up your pico and press return on your terminal within 2 seconds.
You should receive a boot message like this.tfo
```
FUZIX version 0.5
Copyright (c) 1988-2002 by H.F.Bower, D.Braun, S.Nitschke, H.Peraza
Copyright (c) 1997-2001 by Arcady Schekochikhin, Adriano C. R. da Cunha
Copyright (c) 2013-2015 Will Sowerbutts <will@sowerbutts.com>
Copyright (c) 2014-2025 Alan Cox <alan@etchedpixels.co.uk>
Devboot
264KiB total RAM, 160KiB available to processes (15 processes max)
Enabling interrupts ... ok.
NAND flash, 1952kB physical 1277kB logical at 0x13018000: hda: 
SD drive 0: hdb: hdb1 hdb2 
bootdev: 
```
hdb is your SD device
hdb1 is your filesystem partition.
hdb2 is your swapfile partition.
At the bootdev: prompt type the name of your filesystem partition likely hdb1 
```
bootdev: hdb1
```
You have to type it correctly as backspace does not work yet.
You will see the following text or something similar next.
```
Mounting root fs (root_dev=17, ro): OK
Starting /init
init version 0.9.1
Checking root file system.
Current date is Sun 2026-05-17
Enter new date: 
```
Enter the current date in the same format as the example date.
If the date is correct just press return.

Enter the current time HH:MM you don't need the seconds.
```
Current time is 21:21:15
Enter new time: 
```
If all is well you should see this.
```
Enabling swap...
login: 
```

# Users
## Default
root    no password
## Additional test users
My pre-built system on the .uf2 has some test users already configured.  Hey it's a multi user system :)
You can log them in from different terminals.
ted     'password'
sue     'password'

---
# Booking System
## Launching the booking system
If you log in as ted or sue the booking system will automatically start.
You can choose Esc to exit the system and return to the $ prompt.
To return to the login: prompt use Cntrl D

If you log in as root use the following commands to start the booking program.
```
# cd /opt/booking
# ./bin/booking
```
## Example Screen Shot
```
 Booking Customer System                                            By D.Pollard

 ID     Customer Name             Phone 1   Phone 2
--------------------------------------------------------------------------------
 002002 Acme 1 Transport          0398765432           
 002003 Barry Unsworth            0411111111   
 002004 Julia Gillard             0422222222           
 002005 Chris Minns               0433333333           
 002006 Anthony Albanese          0444444444           
 002007 Bob Hawke                 0455555555           
 002008 Paul Keating II           0466666666           
 002009 John Howard               0477777777           
 002010 Malcolm Turnbull          0488888888           
 002011 Scott B Morrison          0499999999           
 002012 Kevin Rudd                0410101010           
 002013 Tony Abbott               0410202020           
 002014 Peter Dutton              0410303030           
 002015 Penny Wong                0410404040           
 002016 Bill Shorten              0410505050           
 002017 Mark McGowan              0410606060           
PgUp/PgDn Scroll,  S Search, V View, E Edit, C Create, Esc Exit
Command:                                                    
 Showing records 1 to 16                                                        
```

## Keyboard Controls
   Onscreen help information is available for all commands.
### Booking List

| Key           | Action          |
| ------------- | --------------- |
| Left Arrow    | Previous day    |
| Right Arrow   | Next day        |
| Shift + Left  | Previous week   |
| Shift + Right | Next week       |
| PgUp / PgDn   | Scroll bookings |
| E             | Edit booking    |
| G             | Go to date      |
| Esc           | Exit            |

### Customer List

| Key        | Action                                |
| ---------- | ------------------------------------- |
| Arrow Keys | Navigate                              |
| Enter      | Select customer (selection mode only) |
| E          | Edit customer                         |
| C          | Create customer                       |
| V          | View customer                         |
| S          | Search                                |
| Esc        | Exit                                  |

### Booking Detail
### Customer Detail

---

## Database Format

The application currently uses fixed-length flat-file database records with indexed access.
The fields have "|" delimiters for human readability only.
 
Design priorities include:
* Human-readable delimiters
* Simple recovery and debugging
* Low overhead
* Predictable record sizes
* Fast sequential and indexed access

---

## Development Notes

This project is an active learning and experimentation platform exploring:

* Fuzix application development
* Multi-user file handling
* Terminal UI design
* Embedded-friendly database techniques
* Portable C programming

The code favours clarity and maintainability over excessive abstraction.

## TODO List
* Date Field Validation
* Time Field Validation
* Invoice Module
* Staff Create/Modify/Edit  (The data structure is already done)

---
## License

Project status: experimental / hobby development.
License terms to be decided.

----
# RPTerm Standalone VT100 Terminal Emulator.
For the true green screen feel I have also tweaked RPTerm to run on a seperate Raspberry Pi pico and can be connected via a three (3) wire cross over cable on UART0 on both devices.
The prebuilt UF2 file is .bin/RPTerm.uf2.
Start your second pico in bootsel mode and drop the file on it. Nothing else to it.
I used the "Pico VGA Demo" board but needed to point the first 6 pins up the other way to use for the UART connection.
This also allowed for the ground connection and a couple of spares. You should of course come up with your own terminal emulation board with out much more than a few resistors for the VGA connector.  That's not covered here.
You will need a USB to USB Micro cable adaptor to plug in a standard keyboard.

# Other Terminal Keyboard Support.
Currently both ANSI standard and VT-52 Keyboards are supported.  Not that you can actually run on a VT-52, the code is just included as an example and proof of concept.

* Keyboard Backend Architecture

This project supports multiple keyboard types through a pluggable backend system. The core keyboard parser (ui_keyboard_parser.c) is fully generic and does not require modification when adding a new keyboard.

* Each keyboard type is implemented as a backend module that defines:
** Escape sequence handling rules
** Key sequence translation tables
** Protocol-specific behaviour

* Adding a New Keyboard
To add support for a new keyboard type, create a new backend and register it in the build and application selection logic.

1. Create a keyboard backend file

File: kb_backend_xxxx.c
Purpose: Implements all keyboard-specific behaviour.

This file defines:
Escape sequence handling (ESC / CSI / SS3 processing)
Terminator rules for multi-byte sequences
Backend configuration structure

Example contents:
const kb_backend_t kb_backend_xxxx = {
    kb_xxxx_seq_table,      /* key sequence lookup table */
    csi_intro_char,         /* CSI introducer (e.g. '[') */
    ss3_intro_char,         /* SS3 introducer (e.g. 'O') */
    esc_timeout,           /* timeout for lone ESC detection */
    kb_xxxx_is_terminator, /* sequence termination rule */
    kb_xxxx_handle_esc     /* ESC handler function */
};

2. Create a key definition table
File: kb_definition_xxxx.c

Purpose:
Defines how escape sequences map to internal key codes.  This is a simple lookup table used by the parser.

Example:
const kb_seq_map_t kb_xxxx_seq_table[] = {
    {"[A", UI_KEY_UP},
    {"[B", UI_KEY_DOWN},
    {"[C", UI_KEY_RIGHT},
    {"[D", UI_KEY_LEFT},
    {0, 0}
};

3. Register the backend
File: kb_definition_tables.h

Purpose:
Declares available keyboard backends so they can be selected by the application.

Add:
const kb_backend_t kb_backend_xxxx;

4. Select the keyboard in the application

File: main.c

Purpose:
Selects which keyboard backend is active at runtime (via command-line option or configuration).

Example:
if (strcmp(argv[1], "xxxx") == 0)
    ui_kb_backend = &kb_backend_xxxx;

5. (Optional) Add automated tests
File: keyboard_test.c

Purpose:
Adds verification cases for the new keyboard backend.

Example:

run_test_case("XXXX UP", "\x1B[A", UI_KEY_UP, &kb_backend_xxxx);
6. Update build system

Files (as applicable):
build.sh   (Debian Build Script)
Makefile.armm0  (FUZIX platform specific makefile)

Purpose:
Ensure new source files are compiled and linked.

Add:
kb_backend_xxxx.c
kb_definition_xxxx.c

##Summary

To add a new keyboard type, you only need to:

Create 1 backend file
Create 1 key definition file
Add 1 line to a header
Select it in main.c
(Optional) Add test cases
Update build script

No changes are required to:
ui_keyboard_parser.c
ui_keyboard_parser.h
