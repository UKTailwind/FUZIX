# mmb2c - translate the test programs and build them
#
#   make            translate and build every tests/*.bas
#   make run        ... and run them
#   make check      ... run them and diff against tests/*.expected
#   make bless      regenerate tests/*.expected from the current build
#   make xcheck     compare the number formatting against the interpreter's own
#   make clean

CC      ?= gcc
CFLAGS  ?= -std=c99 -Wall -Wextra -Wno-unused-parameter -I.
LDLIBS  ?= -lm
PYTHON  ?= python3

TESTS   := $(basename $(notdir $(wildcard tests/*.bas)))
BUILD   := build

.PHONY: all run check bless xcheck clean

all: $(addprefix $(BUILD)/,$(TESTS))

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/%.c: tests/%.bas mmb2c.py | $(BUILD)
	$(PYTHON) mmb2c.py $< -o $@

$(BUILD)/%: $(BUILD)/%.c mmb_runtime.c mmb_runtime.h
	$(CC) $(CFLAGS) -o $@ $< mmb_runtime.c $(LDLIBS)

# samples/ holds the programs that need a screen or a keyboard, so they
# are built but never run - see samples/README.md.  Building them is
# still worth doing: it is what catches a translator change that breaks
# a shape only they use.
SAMPLES := $(basename $(notdir $(wildcard samples/*.bas)))

.PHONY: samples
samples: $(addprefix $(BUILD)/s_,$(SAMPLES))

$(BUILD)/s_%.c: samples/%.bas mmb2c.py | $(BUILD)
	$(PYTHON) mmb2c.py $< -o $@

$(BUILD)/s_%: $(BUILD)/s_%.c mmb_runtime.c mmb_runtime.h
	$(CC) $(CFLAGS) -o $@ $< mmb_runtime.c $(LDLIBS)

# a test may come with  tests/<name>.in  (stdin) and
# tests/<name>.expected (what it should print).  Volatile lines - the ones
# that report elapsed time - are filtered out of both sides.
FILTER := grep -v -e 'Time taken'

run: all
	@mkdir -p $(BUILD)/work
	@for t in $(TESTS); do \
	  echo "=== $$t ==="; \
	  ( cd $(BUILD)/work && rm -rf * && \
	    if [ -f ../../tests/$$t.in ]; then ../$$t < ../../tests/$$t.in; \
	    else ../$$t; fi ); \
	done

# the pixel-exact engine checks: a fake framebuffer behind mm_fb_*,
# every blit operation compared against an independent model.  The .bas
# gates cannot see pixels (the host has no display); this can.
$(BUILD)/blitharness: tests/blitharness.c mmb_blit.h mmb_runtime.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/blitharness.c $(LDLIBS)

# The same idea for GUI BITMAP, and LINE's four width algorithms, were
# proven by tests/guiharness.c and tests/lineharness.c - LOST in the
# 2026-08-19 subtree move into FUZIX (commit 4f2fa8d9e brought the
# Makefile that names them but only blitharness.c itself).  The
# wildcard below picks them back up the moment the files are restored;
# until then check runs the harnesses that exist rather than nothing.
HARNESS_SRC = $(wildcard tests/*harness.c)
HARNESSES = $(patsubst tests/%.c,$(BUILD)/%,$(HARNESS_SRC))

$(BUILD)/%harness: tests/%harness.c mmb_runtime.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

# THE GATE MUST NOT FIND THE MACHINE'S OWN PC3 TOOLS.
#
# A translated program runs its helpers by bare name through the PATH -
# PLAY spawns playsnd, SAVE IMAGE spawns saveimage - and this gate's
# expected output was written on a machine that had none of them: PLAY
# says "Sound output did not start" and that IS the answer being
# checked.  Install the pc3host package on the development machine and
# /usr/bin/playsnd appears, so the gate silently starts testing the
# INSTALLED tools against a display server instead: three spare
# "playsnd: sound output in use" lines and a failure that is the
# machine, not the code.
#
# So the helpers are shadowed by stubs that exit 127, which is exactly
# what execvp does when it finds nothing.  The gate then measures the
# reference runtime, wherever it is run.
SHADOW := $(BUILD)/notools
SHADOWED := playsnd playmod playmp3 playwav playflac \
            saveimage loadimage loadjpg loadpng bcrun

$(SHADOW):
	@mkdir -p $@
	@for p in $(SHADOWED); do \
	  printf '#!/bin/sh\nexit 127\n' > $@/$$p; chmod 755 $@/$$p; \
	done

check: all $(HARNESSES) $(SHADOW)
	@for h in $(HARNESSES); do \
	  $$h || { echo "  FAIL $$h"; exit 1; }; \
	done
	@mkdir -p $(BUILD)/work
	@fail=0; \
	PATH=$(CURDIR)/$(SHADOW):$$PATH; export PATH; \
	for t in $(TESTS); do \
	  ( cd $(BUILD)/work && rm -rf * && \
	    if [ -f ../../tests/$$t.in ]; then ../$$t < ../../tests/$$t.in; \
	    else ../$$t; fi ) > $(BUILD)/$$t.got 2>&1; \
	  if [ ! -f tests/$$t.expected ]; then \
	    echo "  ?  $$t (no tests/$$t.expected)"; continue; \
	  fi; \
	  tr -d '\r' < $(BUILD)/$$t.got | $(FILTER) > $(BUILD)/$$t.a; \
	  tr -d '\r' < tests/$$t.expected | $(FILTER) > $(BUILD)/$$t.b; \
	  if cmp -s $(BUILD)/$$t.a $(BUILD)/$$t.b; then \
	    echo "  ok $$t"; \
	  else \
	    echo "  FAIL $$t"; diff $(BUILD)/$$t.b $(BUILD)/$$t.a | head -20; \
	    fail=1; \
	  fi; \
	done; \
	exit $$fail

# regenerate the .expected files from the current build - only do this
# when you have checked the new output is right
bless: all
	@mkdir -p $(BUILD)/work
	@for t in $(TESTS); do \
	  ( cd $(BUILD)/work && rm -rf * && \
	    if [ -f ../../tests/$$t.in ]; then ../$$t < ../../tests/$$t.in; \
	    else ../$$t; fi ) > tests/$$t.expected 2>&1; \
	  echo "blessed tests/$$t.expected"; \
	done

xcheck: | $(BUILD)
	$(CC) -std=c99 -w -I. -Itests/xcheck -o $(BUILD)/xcheck \
	      tests/xcheck/xcheck.c mmb_runtime.c $(LDLIBS)
	@$(BUILD)/xcheck

clean:
	rm -rf $(BUILD)
