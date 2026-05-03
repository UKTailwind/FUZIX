fuzix.bin: target $(OBJS)
	+make -C platform/platform-$(TARGET) image
	tools/visualizefcc <fuzix.map
