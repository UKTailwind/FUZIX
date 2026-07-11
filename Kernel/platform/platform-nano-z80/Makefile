CROSS_CCOPTS += -I../../dev/

CSRCS = devfd.c devtty.c
CSRCS += devices.c main.c

DISCSRCS = discard.c

ASRCS = nano-z80.S vt.S crt0.S
ASRCS += tricks.S commonmem.S

#DISCARD_DSRCS = ../../dev/tinydisk_discard.c ../../dev/tinyide_discard.c
#DISCARD_DSRCS += ../../dev/ds1302_discard.c
#DSRCS = ../../dev/tinydisk.c ../../dev/tinyide.c ../../dev/mbr.c
#DSRCS += ../../dev/propio2.c ../../dev/ds1302.c
#DSRCS += ../../dev/devfd.c
#NSRCS = ../../dev/net/net_w5x00.c ../../dev/net/net_native.c

#DASRCS = ../../dev/rbcfd9266_hw.S ../../dev/ds1302_rbc.S ../../dev/tinyide_ppide_rbc.S

COBJS = $(CSRCS:.c=.o)
AOBJS = $(ASRCS:.S=.o)
#NOBJS = $(patsubst ../../dev/net/%.c,%.o, $(NSRCS))
DISCOBJS = $(DISCSRCS:.c=.o)
DISCARD_DOBJS = $(patsubst ../../dev/%.c,%.o, $(DISCARD_DSRCS))
#DOBJS = $(patsubst ../../dev/%.c,%.o, $(DSRCS))
#DAOBJS = $(patsubst ../../dev/%.S,%.o, $(DASRCS))

OBJS  = $(COBJS) $(AOBJS) $(DISCOBJS) $(DISCARD_DOBJS)

JUNK =  *.lst *.asm *.Sym *.rst *.lnk *.s *.\^ *.\# *.\% *.\@

all:	$(OBJS)

$(COBJS): %.o: %.c
	$(CROSS_CC) $(CROSS_CCOPTS) -c $<

$(DISCOBJS): %.o: %.c
	$(CROSS_CC) $(CROSS_CCOPTS) $(CROSS_CC_SEGDISC) -c $<

$(DOBJS): %.o: ../../dev/%.c
	$(CROSS_CC) $(CROSS_CCOPTS) -c $<

$(DISCARD_DOBJS): %.o: ../../dev/%.c
	$(CROSS_CC) $(CROSS_CCOPTS) $(CROSS_CC_SEGDISC) -c $<

$(AOBJS): %.o: %.S
	$(CROSS_AS) $(ASOPTS) $<

$(DAOBJS): %.o: ../../dev/%.S
	$(CROSS_AS) $(ASOPTS) $<

clean:
	rm -f $(OBJS) $(JUNK)  core *~ fuzix.romwbw fuzix.bin fusix.tmpmap

#
#	Attach the kernel to the boot block
#
image:
	$(CROSS_LD) -b -C 0x0088 -S 0xF400 -f CLDBbXSs -o fuzix.bin \
	crt0.o commonmem.o nano-z80.o vt.o tricks.o devtty.o devfd.o main.o \
	discard.o \
	../../start.o \
	../../version.o ../../cpu-z80u/lowlevel-z80u.o ../../usermem.o \
	../../timer.o ../../devsys.o ../../kdata.o devices.o ../../devio.o \
	../../filesys.o ../../blk512.o ../../process.o \
	../../inode.o ../../syscall_exec.o ../../syscall_exec16.o \
	../../syscall_fs.o ../../syscall_fs2.o ../../syscall_fs3.o \
	../../syscall_proc.o ../../syscall_other.o \
	../../tty.o ../../mm.o ../../mm/memalloc_none.o \
	../../swap.o ../../mm/bankfixed.o ../../vt.o \
	/opt/fcc/lib/z80/libz80.a -m fuzix.tmpmap
	perl -lpe '$$_=hex' fuzix.tmpmap | paste -d" " - fuzix.tmpmap  |  sort -n |  cut -d" " -f 2- >../../fuzix.map
	../../tools/pack85 <../../fuzix.map fuzix.bin ../../fuzix.bin
	dd if=../../fuzix.bin of=../../fuzix.strip bs=1 skip=136

#
#	Compile up the boot block
#
#bootblock:
#	asz80 boot-romwbw.S
#	ldz80 -b boot-romwbw.o -o boot-romwbw.tmp
#	# Chop off the leading 61440 bytes we don't want
#	dd if=boot-romwbw.tmp of=boot-romwbw.bin bs=512 count=2 skip=120

IMAGES = $(FUZIX_ROOT)/Images/$(TARGET)

diskimage:
	# Make a blank disk image with partition
	dd if=$(FUZIX_ROOT)/Standalone/filesystem-src/parttab.40M of=$(IMAGES)/disk.img bs=40017920 conv=sync
	#dd if=$(FUZIX_ROOT)/Standalone/filesystem-src/parttab.20M of=$(IMAGES)/disk.img bs=20152320 conv=sync
	# Add the file system
	#dd if=$(IMAGES)/filesys8.img of=$(IMAGES)/disk.img bs=512 seek=2048 conv=notrunc
	dd if=$(IMAGES)/filesys.img of=$(IMAGES)/disk.img bs=512 seek=2048 conv=notrunc
	# Add the kernel
	#dd if=fuzix.romwbw of=$(IMAGES)/disk.img bs=512 seek=2 conv=notrunc
	# Make an emulator image of it
	#cat $(FUZIX_ROOT)/Standalone/filesystem-src/idehdr.20M $(IMAGES)/disk.img > $(IMAGES)/emu-ide.img
