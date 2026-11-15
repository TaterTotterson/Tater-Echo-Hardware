CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Werror -std=c11
BUILD := build
PROBE := $(BUILD)/mt8163-display-query

.PHONY: all clean

all: $(PROBE)

$(PROBE): display_query.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -static -o $@ $<

clean:
	rm -f $(PROBE)
