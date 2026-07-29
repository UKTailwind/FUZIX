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

check: all
	@mkdir -p $(BUILD)/work
	@fail=0; \
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
