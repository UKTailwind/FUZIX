$(call find-makefile)

ctype-test.srcs = ctype.c
$(call build, ctype-test, host-test)

strtol-test.srcs = strtol.c
$(call build, strtol-test, host-test)

