# nano-z80
The [nano-z80](https://github.com/venomix666/nano-z80) is a Z80 computer build around the inexpensive [Tang Nano 20K](https://wiki.sipeed.com/hardware/en/tang/tang-nano-20k/nano-20k.html) FPGA board. It runs the T80 core at 25.125 MHz, has 8 MB of SDRAM, HDMI output and uses a micro-SD card for storage.

## Supported hardware
4 80x30 video terminals (on the HDMI output), switchable with F1-F4
USB keyboard, interrupt driven    
2 serial ports with RX interrupts (one built on the USB-C connector, one on the carrier board)  
Timer interrupt (100 Hz) + seconds from a psuedo RTC  
SD card storage, only one hardcoded partition at the moment

## TODO
Use tinydisk instead of completely hardcoded partition locations
Implement support for direct transfers
More complete IOCTL for VTs and UARTs
Swap
SLIP networking

## Install
Write the filesystem image to the SD-card at an offset of 0x100000:  
(insert dd command)  
Copy fuzix.com to the CP/M a drive using cpmcp or xmodem.
Start fuzix from CP/M by running fuzix.com

