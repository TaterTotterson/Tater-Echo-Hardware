CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Werror -std=c11
BUILD := build
PROBE := $(BUILD)/mt8163-jpeg-probe

.PHONY: all clean

all: $(PROBE)

$(PROBE): probe.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(PROBE)
