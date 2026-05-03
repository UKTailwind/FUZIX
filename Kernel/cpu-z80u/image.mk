tools/visualizefcc: tools/visualizefcc.c

tools/pack85: tools/pack85.c

tools/doubleup: tools/doubleup.c

fuzix.bin: target $(OBJS) tools/pack85 tools/visualizefcc tools/doubleup
	+$(MAKE) -C platform/platform-$(TARGET) image
	(cd platform/platform-$(TARGET); ../../tools/visualizefcc) <fuzix.map
