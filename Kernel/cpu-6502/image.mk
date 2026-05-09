tools/visualizefcc: tools/visualizefcc.c

tools/hogfather: tools/hogfather.c

fuzix.bin: target $(OBJS) tools/visualizefcc
	+make -C platform/platform-$(TARGET) image
	tools/visualizefcc <fuzix.map
	tools/hogfather ../fuzix.map | sort -nr >../fuzix.hogs
