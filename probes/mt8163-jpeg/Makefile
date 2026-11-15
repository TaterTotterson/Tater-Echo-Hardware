CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Werror -std=c11
BUILD := build
PROBE := $(BUILD)/mt8163-jpeg-probe
ION_INSPECT := $(BUILD)/mt8163-ion-inspect
ION_BUFFER_PROBE := $(BUILD)/mt8163-ion-buffer-probe
ENCODER_CLOCK_HOLD := $(BUILD)/mt8163-encoder-clock-hold

.PHONY: all clean

all: $(PROBE) $(ION_INSPECT) $(ION_BUFFER_PROBE) $(ENCODER_CLOCK_HOLD)

$(PROBE): probe.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $<

$(ION_INSPECT): ion_inspect.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $<

$(ION_BUFFER_PROBE): ion_buffer_probe.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $<

$(ENCODER_CLOCK_HOLD): encoder_clock_hold.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(PROBE) $(ION_INSPECT) $(ION_BUFFER_PROBE) $(ENCODER_CLOCK_HOLD)
