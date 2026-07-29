# mmb2c - translate the test programs and build them
#
#   make            translate and build every tests/*.bas
#   make run        ... and run them
#   make xcheck     compare the number formatting against the interpreter's own
#   make clean

CC      ?= gcc
CFLAGS  ?= -std=c99 -Wall -Wextra -Wno-unused-parameter -I.
LDLIBS  ?= -lm
PYTHON  ?= python3

TESTS   := $(basename $(notdir $(wildcard tests/*.bas)))
BUILD   := build

.PHONY: all run xcheck clean

all: $(addprefix $(BUILD)/,$(TESTS))

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/%.c: tests/%.bas mmb2c.py | $(BUILD)
	$(PYTHON) mmb2c.py $< -o $@

$(BUILD)/%: $(BUILD)/%.c mmb_runtime.c mmb_runtime.h
	$(CC) $(CFLAGS) -o $@ $< mmb_runtime.c $(LDLIBS)

run: all
	@mkdir -p $(BUILD)/work
	@for t in $(TESTS); do \
	  echo "=== $$t ==="; \
	  ( cd $(BUILD)/work && rm -rf * && ../$$t ); \
	done

xcheck: | $(BUILD)
	$(CC) -std=c99 -w -I. -Itests/xcheck -o $(BUILD)/xcheck \
	      tests/xcheck/xcheck.c mmb_runtime.c $(LDLIBS)
	@$(BUILD)/xcheck

clean:
	rm -rf $(BUILD)
