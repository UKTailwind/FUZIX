tools/visualizefcc: tools/visualizefcc.c

tools/pack85: tools/pack85.c

tools/doubleup: tools/doubleup.c

cpm-loader-fcc/cpmload.bin: cpm-loader-fcc/cpmload.S cpm-loader-fcc/fuzixload.S cpm-loader-fcc/makecpmloader.c
	+$(MAKE) -C cpm-loader-fcc

fuzix.bin: target $(OBJS) tools/pack85 tools/visualizefcc tools/doubleup cpm-loader-fcc/cpmload.bin
	+$(MAKE) -C platform/platform-$(TARGET) image
	(cd platform/platform-$(TARGET); ../../tools/visualizefcc) <fuzix.map
