all:	makecpmloader cpmload.bin fuzixload.bin

makecpmloader: makecpmloader.c

cpmload.bin:	cpmload.S
	$(CROSS_AS) $(ASOPTS) cpmload.S
	$(CROSS_LD) -b -C 0x100 -o cpmload.bin cpmload.o

fuzixload.bin:	fuzixload.S
	$(CROSS_AS) $(ASOPTS) fuzixload.S
	$(CROSS_LD) -b -C 0x100 -o fuzixload.bin fuzixload.o

clean:
	rm -f *~ *.s *.o *.rst *.lst *.asm *.bin *.sym *.rel *.map *.ihx makecpmloader
