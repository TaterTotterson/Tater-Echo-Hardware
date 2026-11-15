# SPDX-License-Identifier: MIT
# Versioned Checkers profile for the retained vendor-library ABI.
BUILD := build
CODEC := ../mt8163-vcodec
CFLAGS := -O2 -Wall -Wextra -Werror -std=c11 -O3 -mfpu=neon -mtune=cortex-a53
LINK := -Wl,--export-dynamic -Wl,--dynamic-linker,/data/tater-vdec-lineage-1/linker

.PHONY: all
all: $(BUILD)/tater-hw-vdec $(BUILD)/tater-hw-venc

$(BUILD)/tater-hw-vdec: decoder_main.c checkers_target.h $(CODEC)/tater_vdec_android.c $(CODEC)/tater_vdec_android.h $(CODEC)/omx_probe_android.c $(CODEC)/omx_probe_android.h $(CODEC)/cronos_linear_rgba.h
	mkdir -p $(BUILD)
	$(ANDROID_CC) $(CFLAGS) $(OMX_CFLAGS) -DTATER_CRONOS_ABI $(LINK) -o $@ decoder_main.c $(CODEC)/tater_vdec_android.c $(CODEC)/omx_probe_android.c -ldl

$(BUILD)/encoder_original.o: $(CODEC)/venc_probe_android.c $(CODEC)/tater_bayer10.h $(CODEC)/bayer10_scaled.h $(CODEC)/planar_chroma.h
	mkdir -p $(BUILD)
	$(ANDROID_CC) $(CFLAGS) $(OMX_CFLAGS) -Dmain=tater_checkers_encoder_main -c $(CODEC)/venc_probe_android.c -o $@

$(BUILD)/tater-hw-venc: encoder_main.c checkers_target.h $(BUILD)/encoder_original.o $(CODEC)/tater_bayer10.c $(CODEC)/tater_bayer10.h $(CODEC)/bayer10_scaled.h
	$(ANDROID_CC) $(CFLAGS) $(OMX_CFLAGS) $(LINK) -Wl,--wrap=dlclose -o $@ encoder_main.c $(BUILD)/encoder_original.o $(CODEC)/tater_bayer10.c -ldl
