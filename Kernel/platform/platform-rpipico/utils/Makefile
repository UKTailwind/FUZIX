ROOT=../../../..
FUZIX_ROOT=$(ROOT)
USERCPU=armm0
VERBOSE=1
include $(FUZIX_ROOT)/Target/rules.$(USERCPU)

UTILS = libmbench.c gpioin.c allocbench.c forkroom.c picoctl.c picogpio.c gpiotog.c gfxtest.c bufs.c arenatest.c exectest.c wraptest.c keydump.c psbench.c memprobe.c syscallbench.c ripple.c bmtest.c linebench.c saveimage.c loadimage.c mp3bench.c pcmtest.c playmp3.c playwav.c playflac.c i2ctest.c locktest.c adval.c keyprobe.c i2c2probe.c lltest.c blitbench.c fifotest.c sc2test.c playsnd.c snd.c pcmpace.c sndclient.c fontdef.c
UTILSBIN = $(UTILS:.c=)

# mp3bench is the ONE program here allowed near the hardware FPU, and
# only when asked - see PC3-MP3-PLAN.md.  The whole no-FP-context-save
# argument rests on exactly one process at a time having live FP state,
# and that is BUILD discipline, not something the code can enforce: a
# second program compiled with -mfpu would corrupt this one's decoder
# state and produce garbage audio rather than a crash.  So if a rule
# like this is ever added for anything else, the plan has to be re-read
# first.
#
#	make mp3bench			soft float, exactly as shipped
#	make mp3bench FPU=1		hardware single precision
#	make mp3bench OPT=-O2		-Os is a handicap on a DSP inner loop
#
# FPU=1 needs a kernel granting CP10/CP11; without one the first float
# instruction takes a UsageFault, which is a clear enough answer.
FPFLAGS = -mfloat-abi=softfp -mfpu=fpv5-sp-d16

ifdef FPU
mp3bench.o: CCOPTS += $(FPFLAGS)
endif
mp3bench.o: CCOPTS += $(OPT)

# playmp3 is NOT optional about it: measured, soft float decodes at
# 0.59x real time and the FPU at 3.00x, so a soft-float build of this
# would simply stutter.  It is the second and last program permitted
# the flags - and it holds the PCM stream open while it runs, which is
# what keeps "exactly one FP process" true, since the kernel saves no
# FP context across a context switch (see main.c).
#
# playwav and playflac deliberately DO NOT get these flags.  Both are
# integer codecs - WAV is a format conversion and FLAC's predictor and
# residuals are fixed point - so neither has a float inner loop to lose,
# and staying soft-float keeps the count of programs in that argument at
# two.  They hold the same exclusive PCM stream, so adding the flags
# later would not break the invariant, but it is not free to reason
# about and there is nothing to gain: measure first.
playmp3.o: CCOPTS += $(FPFLAGS)

.PHONY: all clean

all: $(UTILSBIN) playmod loadjpg loadpng

clean:
	rm -f *.o
	rm -f $(UTILSBIN) playmod loadjpg loadpng

# playmod links the vendored hxcmod tracker alongside its own object
playmod: playmod.o hxcmod.o
	$(LINKER) $(CRT0) $^ -o $@ $(LINKER_OPT) $(LINKER_TAIL)

# loadjpg links MMBasic's own picojpeg, unmodified, the same way.
# It is NOT in UTILS because that list drives the one-object pattern
# rule below.  loadpng links MMBasic's upng on the same terms.
loadpng: loadpng.o upng.o upng_pc3.o
	$(LINKER) $(CRT0) $^ -o $@ $(LINKER_OPT) $(LINKER_TAIL)

# upng reads its own file through MMBasic file numbers; loadpng reads
# the file itself and calls upng_new_from_bytes, so that entry point is
# compiled out.
upng.o: CCOPTS += -DUPNG_NO_FILE

loadjpg: loadjpg.o picojpeg.o
	$(LINKER) $(CRT0) $^ -o $@ $(LINKER_OPT) $(LINKER_TAIL)

$(UTILSBIN): %: %.o
	$(LINKER) $(CRT0) $^ -o $@ $(LINKER_OPT) $(LINKER_TAIL)

%.o: %.c
	$(CC) $(CFLAGS) $(CCOPTS) -c $<
