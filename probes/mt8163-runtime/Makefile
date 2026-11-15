CC ?= arm-linux-musleabihf-gcc
CFLAGS ?= -O2 -Wall -Wextra -Werror -std=c11
BUILD_DIR ?= build

.PHONY: all clean

all: $(BUILD_DIR)/checkers-runtime-probe

$(BUILD_DIR)/checkers-runtime-probe: runtime_probe.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -static $< -o $@

$(BUILD_DIR):
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)
