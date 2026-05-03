tools/visualizefcc: tools/visualizefcc.c

fuzix.bin: target $(OBJS) tools/visualizefcc
	+make -C platform/platform-$(TARGET) image
	tools/visualizefcc <fuzix.map
