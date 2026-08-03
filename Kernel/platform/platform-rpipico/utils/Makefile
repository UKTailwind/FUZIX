ROOT=../../../..
FUZIX_ROOT=$(ROOT)
USERCPU=armm0
VERBOSE=1
include $(FUZIX_ROOT)/Target/rules.$(USERCPU)

UTILS = forkroom.c picoctl.c picogpio.c gfxtest.c bufs.c arenatest.c exectest.c wraptest.c keydump.c psbench.c memprobe.c syscallbench.c ripple.c bmtest.c linebench.c saveimage.c loadimage.c
UTILSBIN = $(UTILS:.c=)

.PHONY: all clean

all: $(UTILSBIN)

clean:
	rm -f *.o
	rm -f $(UTILSBIN)

$(UTILSBIN): %: %.o
	$(LINKER) $(CRT0) $^ -o $@ $(LINKER_OPT) $(LINKER_TAIL)

%.o: %.c
	$(CC) $(CFLAGS) $(CCOPTS) -c $<
