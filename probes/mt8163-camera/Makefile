CC ?= arm-linux-musleabihf-gcc
HOST_CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Werror -std=c11
BUILD_DIR ?= build
KERNEL_SRC ?= ../../.tater-re/references/kernel-8d928c5176cc
CAMERA_INC := $(KERNEL_SRC)/drivers/misc/mediatek/cameraisp/src/mt8163/inc
IMGSENSOR_INC := $(KERNEL_SRC)/drivers/misc/mediatek/imgsensor/inc
TARGETS := \
	$(BUILD_DIR)/checkers-lens-cover-query \
	$(BUILD_DIR)/checkers-camera-abi-dump

.PHONY: all host-check clean

all: $(TARGETS)

$(BUILD_DIR)/checkers-lens-cover-query: lens_cover_query.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -static $< -o $@

$(BUILD_DIR)/checkers-camera-abi-dump: camera_abi_dump.c | $(BUILD_DIR)
	@test -f "$(CAMERA_INC)/camera_isp.h" || \
		{ echo "KERNEL_SRC must name the exact public MT8163 source" >&2; exit 1; }
	$(CC) $(CFLAGS) -static -I$(CAMERA_INC) -I$(IMGSENSOR_INC) $< -o $@

$(BUILD_DIR):
	mkdir -p $@

host-check: $(BUILD_DIR)/camera-state-machine-selftest
	$(BUILD_DIR)/camera-state-machine-selftest

$(BUILD_DIR)/camera-state-machine-selftest: \
		test_pattern_state.c state_machine_selftest.c test_pattern_state.h | $(BUILD_DIR)
	$(HOST_CC) $(CFLAGS) test_pattern_state.c state_machine_selftest.c -o $@

clean:
	rm -rf $(BUILD_DIR)
