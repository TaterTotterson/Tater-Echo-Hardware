CC ?= cc
HOST_CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Werror -std=c11
BUILD := build
PROBE := $(BUILD)/mt8163-vcodec-lock-probe
ANDROID_CAPABILITY_PROBE := $(BUILD)/mt8163-vcodec-capability-probe
ANDROID_LIFECYCLE_PROBE := $(BUILD)/mt8163-vcodec-lifecycle-probe
ANDROID_MEMORY_PROBE := $(BUILD)/mt8163-vcodec-memory-probe
ANDROID_MVA_PROBE := $(BUILD)/mt8163-vcodec-mva-probe
ANDROID_INIT_PROBE := $(BUILD)/mt8163-vcodec-init-probe
ANDROID_OMX_PROBE := $(BUILD)/mt8163-vcodec-omx-probe
ANDROID_OMX_NATIVE_PROBE := $(BUILD)/mt8163-vcodec-omx-native-probe
ANDROID_OMX_CAPTURE_SHIM := $(BUILD)/mt8163-vcodec-omx-capture-shim.so
ANDROID_TATER_VDEC := $(BUILD)/tater-vdec
ANDROID_TATER_VENC := $(BUILD)/tater-venc
HOST_PACKETIZER := $(BUILD)/tater-h264-packetize
ANDROID_TATER_VDEC_LINKER ?= /data/tater-vdec/linker

.PHONY: all host-check host-packetizer android-capability android-lifecycle android-memory \
	android-mva android-init android-omx android-omx-native \
	android-omx-capture-shim android-tater-vdec android-tater-venc clean

all: $(PROBE)

host-check: $(BUILD)/mtk-yuv420-block-selftest
	$(BUILD)/mtk-yuv420-block-selftest

host-packetizer: $(HOST_PACKETIZER)

$(HOST_PACKETIZER): h264_packetize.c
	mkdir -p $(BUILD)
	$(HOST_CC) $(CFLAGS) h264_packetize.c -o $@

$(BUILD)/mtk-yuv420-block-selftest: \
		mtk_yuv420_block.c yuv_block_selftest.c mtk_yuv420_block.h
	mkdir -p $(BUILD)
	$(HOST_CC) $(CFLAGS) mtk_yuv420_block.c yuv_block_selftest.c -o $@

android-capability: $(ANDROID_CAPABILITY_PROBE)

android-lifecycle: $(ANDROID_LIFECYCLE_PROBE)

android-memory: $(ANDROID_MEMORY_PROBE)

android-mva: $(ANDROID_MVA_PROBE)

android-init: $(ANDROID_INIT_PROBE)

android-omx: $(ANDROID_OMX_PROBE)

android-omx-native: $(ANDROID_OMX_NATIVE_PROBE)

android-omx-capture-shim: $(ANDROID_OMX_CAPTURE_SHIM)

android-tater-vdec: $(ANDROID_TATER_VDEC)

android-tater-venc: $(ANDROID_TATER_VENC)

android-cronos-vdec: $(BUILD)/tater-cronos-vdec

android-cronos-venc: $(BUILD)/tater-cronos-venc

android-crown-vdec: $(BUILD)/tater-crown-vdec

android-crown-venc: $(BUILD)/tater-crown-venc

android-rook-vdec: $(BUILD)/tater-rook-vdec

android-rook-venc: $(BUILD)/tater-rook-venc

$(BUILD)/tater-rook-vdec: omx_probe_android.c omx_probe_android.h tater_vdec_android.c tater_vdec_android.h tater_vdec_main_android.c cronos_linear_rgba.h ../mt8163-gpu/rook_target.h
	mkdir -p $(BUILD)
	$(ANDROID_CC) $(ANDROID_CFLAGS) $(OMX_CFLAGS) -DTATER_CRONOS_ABI -DTATER_GPU_ROOK \
		-O3 -mfpu=neon -mtune=cortex-a53 \
		-Wl,--export-dynamic -Wl,--dynamic-linker,$(ANDROID_TATER_VDEC_LINKER) \
		-o $@ tater_vdec_main_android.c tater_vdec_android.c omx_probe_android.c -ldl

$(BUILD)/tater-rook-venc: rook_venc_android.c rook_bayer8.h planar_chroma.h ../mt8163-gpu/rook_target.h
	mkdir -p $(BUILD)
	$(ANDROID_CC) $(ANDROID_CFLAGS) $(OMX_CFLAGS) -DTATER_GPU_ROOK -O3 -mfpu=neon -mtune=cortex-a53 \
		-Wl,--export-dynamic -Wl,--dynamic-linker,$(ANDROID_TATER_VDEC_LINKER) \
		-o $@ rook_venc_android.c -ldl

$(BUILD)/tater-crown-vdec: omx_probe_android.c omx_probe_android.h tater_vdec_android.c tater_vdec_android.h tater_vdec_main_android.c cronos_linear_rgba.h ../mt8163-gpu/crown_target.h
	mkdir -p $(BUILD)
	$(ANDROID_CC) $(ANDROID_CFLAGS) $(OMX_CFLAGS) -DTATER_CRONOS_ABI -DTATER_GPU_CROWN \
		-O3 -mfpu=neon -mtune=cortex-a53 \
		-Wl,--export-dynamic -Wl,--dynamic-linker,$(ANDROID_TATER_VDEC_LINKER) \
		-o $@ tater_vdec_main_android.c tater_vdec_android.c omx_probe_android.c -ldl

$(BUILD)/tater-crown-venc: crown_venc_android.c tater_bayer10.c tater_bayer10.h bayer10_scaled.h planar_chroma.h ../mt8163-gpu/crown_target.h
	mkdir -p $(BUILD)
	$(ANDROID_CC) $(ANDROID_CFLAGS) $(OMX_CFLAGS) -O3 -mfpu=neon -mtune=cortex-a53 \
		-Wl,--export-dynamic -Wl,--dynamic-linker,$(ANDROID_TATER_VDEC_LINKER) \
		-o $@ crown_venc_android.c tater_bayer10.c -ldl

$(BUILD)/tater-cronos-vdec: omx_probe_android.c omx_probe_android.h \
		tater_vdec_android.c tater_vdec_android.h tater_vdec_main_android.c \
		cronos_linear_rgba.h ../mt8163-gpu/cronos_target.h
	mkdir -p $(BUILD)
	$(ANDROID_CC) $(ANDROID_CFLAGS) $(OMX_CFLAGS) -DTATER_CRONOS_ABI \
		-O3 -mfpu=neon -mtune=cortex-a53 \
		-Wl,--export-dynamic -Wl,--dynamic-linker,$(ANDROID_TATER_VDEC_LINKER) \
		-o $@ tater_vdec_main_android.c tater_vdec_android.c omx_probe_android.c -ldl

$(BUILD)/tater-cronos-venc: cronos_venc_android.c cronos_bayer10.c cronos_bayer10.h bayer10_scaled.h planar_chroma.h \
		../mt8163-gpu/cronos_target.h
	mkdir -p $(BUILD)
	$(ANDROID_CC) $(ANDROID_CFLAGS) $(OMX_CFLAGS) -O3 -mfpu=neon -mtune=cortex-a53 \
		-Wl,--export-dynamic -Wl,--dynamic-linker,$(ANDROID_TATER_VDEC_LINKER) \
		-o $@ cronos_venc_android.c cronos_bayer10.c -ldl

$(ANDROID_TATER_VENC): venc_probe_android.c tater_bayer10.c tater_bayer10.h bayer10_scaled.h planar_chroma.h
	mkdir -p $(BUILD)
	$(ANDROID_CC) $(ANDROID_CFLAGS) $(OMX_CFLAGS) -O3 -mfpu=neon -mtune=cortex-a53 \
		-Wl,--export-dynamic -Wl,--dynamic-linker,$(ANDROID_TATER_VDEC_LINKER) \
		-o $@ venc_probe_android.c tater_bayer10.c -ldl

$(PROBE): lock_probe.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $<

$(ANDROID_CAPABILITY_PROBE): capability_probe_android.c
	mkdir -p $(BUILD)
	$(ANDROID_CC) $(ANDROID_CFLAGS) -o $@ $< -ldl

$(ANDROID_LIFECYCLE_PROBE): lifecycle_probe_android.c
	mkdir -p $(BUILD)
	$(ANDROID_CC) $(ANDROID_CFLAGS) -o $@ $< -ldl

$(ANDROID_MEMORY_PROBE): memory_probe_android.c
	mkdir -p $(BUILD)
	$(ANDROID_CC) $(ANDROID_CFLAGS) -o $@ $< -ldl

$(ANDROID_MVA_PROBE): mva_probe_android.c
	mkdir -p $(BUILD)
	$(ANDROID_CC) $(ANDROID_CFLAGS) -o $@ $< -ldl

$(ANDROID_INIT_PROBE): init_probe_android.c mtk_yuv420_block.c \
		mtk_yuv420_block.h
	mkdir -p $(BUILD)
	$(ANDROID_CC) $(ANDROID_CFLAGS) -o $@ init_probe_android.c \
		mtk_yuv420_block.c -ldl

$(ANDROID_OMX_PROBE): omx_probe_android.c omx_probe_android.h \
		omx_probe_main_android.c
	mkdir -p $(BUILD)
	$(ANDROID_CC) $(ANDROID_CFLAGS) $(OMX_CFLAGS) -o $@ \
		omx_probe_main_android.c omx_probe_android.c -ldl

$(ANDROID_OMX_NATIVE_PROBE): omx_probe_android.c \
		omx_probe_android.h omx_probe_main_android.c \
		omx_native_buffer_android.cpp omx_native_buffer_android.h \
		mtk_yuv420_block.c mtk_yuv420_block.h
	mkdir -p $(BUILD)
	$(ANDROID_CC) $(ANDROID_CFLAGS) $(OMX_CFLAGS) \
		-DTATER_NATIVE_BUFFER_SUPPORT -c omx_probe_android.c \
		-o $(BUILD)/omx_probe_native.o
	$(ANDROID_CC) $(ANDROID_CFLAGS) $(OMX_CFLAGS) \
		-c omx_probe_main_android.c \
		-o $(BUILD)/omx_probe_main_native.o
	$(ANDROID_CXX) $(ANDROID_CXXFLAGS) $(OMX_CFLAGS) \
		$(ANDROID_NATIVE_CFLAGS) -c omx_native_buffer_android.cpp \
		-o $(BUILD)/omx_native_buffer_android.o
	$(ANDROID_CC) $(ANDROID_CFLAGS) -c mtk_yuv420_block.c \
		-o $(BUILD)/omx_native_yuv_block.o
	$(ANDROID_CXX) -nostdlib++ $(ANDROID_LDFLAGS) -o $@ \
		$(BUILD)/omx_probe_main_native.o \
		$(BUILD)/omx_probe_native.o \
		$(BUILD)/omx_native_buffer_android.o \
		$(BUILD)/omx_native_yuv_block.o \
		-L$(ANDROID_STOCK_LIB_DIR) \
		-Wl,-rpath-link,$(ANDROID_STOCK_LIB_DIR) \
		-lui -lutils -lhardware -lcutils -lsync -lstdc++ -llog -ldl

$(ANDROID_TATER_VDEC): omx_probe_android.c omx_probe_android.h \
		tater_vdec_android.c tater_vdec_android.h \
		tater_vdec_main_android.c omx_native_buffer_android.cpp \
		omx_native_buffer_android.h mtk_yuv420_block.c mtk_yuv420_block.h
	mkdir -p $(BUILD)
	$(ANDROID_CC) $(ANDROID_CFLAGS) $(OMX_CFLAGS) \
		-DTATER_NATIVE_BUFFER_SUPPORT -c omx_probe_android.c \
		-o $(BUILD)/tater_vdec_omx.o
	$(ANDROID_CC) $(ANDROID_CFLAGS) $(OMX_CFLAGS) \
		-c tater_vdec_android.c -o $(BUILD)/tater_vdec_android.o
	$(ANDROID_CC) $(ANDROID_CFLAGS) $(OMX_CFLAGS) \
		-c tater_vdec_main_android.c -o $(BUILD)/tater_vdec_main_android.o
	$(ANDROID_CXX) $(ANDROID_CXXFLAGS) $(OMX_CFLAGS) \
		$(ANDROID_NATIVE_CFLAGS) -c omx_native_buffer_android.cpp \
		-o $(BUILD)/tater_vdec_native_buffer.o
	$(ANDROID_CC) $(ANDROID_CFLAGS) -O3 -mfpu=neon -mtune=cortex-a53 -c mtk_yuv420_block.c \
		-o $(BUILD)/tater_vdec_yuv_block.o
	$(ANDROID_CXX) -nostdlib++ $(ANDROID_LDFLAGS) \
		-Wl,--export-dynamic \
		-Wl,--dynamic-linker,$(ANDROID_TATER_VDEC_LINKER) -o $@ \
		$(BUILD)/tater_vdec_main_android.o \
		$(BUILD)/tater_vdec_android.o $(BUILD)/tater_vdec_omx.o \
		$(BUILD)/tater_vdec_native_buffer.o \
		$(BUILD)/tater_vdec_yuv_block.o \
		-L$(ANDROID_STOCK_LIB_DIR) \
		-Wl,-rpath-link,$(ANDROID_STOCK_LIB_DIR) \
		-lui -lutils -lhardware -lcutils -lsync -lstdc++ -llog -ldl

$(ANDROID_OMX_CAPTURE_SHIM): omx_capture_shim_android.c \
		mtk_yuv420_block.c mtk_yuv420_block.h
	mkdir -p $(BUILD)
	$(ANDROID_CC) $(ANDROID_CFLAGS) -shared -fPIC -o $@ \
		omx_capture_shim_android.c mtk_yuv420_block.c -ldl

clean:
	rm -f $(PROBE) $(ANDROID_CAPABILITY_PROBE) $(ANDROID_LIFECYCLE_PROBE) \
		$(ANDROID_MEMORY_PROBE) $(ANDROID_MVA_PROBE) $(ANDROID_INIT_PROBE) \
		$(ANDROID_OMX_PROBE) $(ANDROID_OMX_NATIVE_PROBE) \
		$(ANDROID_OMX_CAPTURE_SHIM) $(ANDROID_TATER_VDEC) $(ANDROID_TATER_VENC)
	rm -f $(BUILD)/omx_probe_native.o \
		$(BUILD)/omx_probe_main_native.o \
		$(BUILD)/omx_native_buffer_android.o \
		$(BUILD)/omx_native_yuv_block.o
	rm -f $(BUILD)/tater_vdec_main_android.o \
		$(BUILD)/tater_vdec_android.o $(BUILD)/tater_vdec_omx.o \
		$(BUILD)/tater_vdec_native_buffer.o \
		$(BUILD)/tater_vdec_yuv_block.o
	rm -f $(BUILD)/mtk-yuv420-block-selftest
	rm -f $(HOST_PACKETIZER)
