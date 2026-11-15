ANDROID_CFLAGS ?= -O2 -Wall -Wextra -Werror -std=c11
ANDROID_LINKER ?= /tmp/android-gpu/linker
ANDROID_API28_LINKER ?= /tmp/android-gpu28/linker
BUILD := build
PROBE := $(BUILD)/mt8163-mali-gles2-probe
API28_BUILD := $(BUILD)/api28
API28_PROBE := $(API28_BUILD)/mt8163-mali-gles2-probe
API28_ROTATE := $(API28_BUILD)/tater-gpu-rotate
BAYER_BENCH := $(API28_BUILD)/cronos-bayer-es2 $(API28_BUILD)/cronos-bayer-es3 $(API28_BUILD)/cronos-bayer-hybrid
BAYER_DEPENDENCIES := bayer10_offscreen_android.c cronos_target.h ../mt8163-vcodec/bayer10_scaled.h
API28_SHIMS := \
	$(API28_BUILD)/libbionic28compat.so \
	$(API28_BUILD)/libged.so \
	$(API28_BUILD)/libgpu_aux.so \
	$(API28_BUILD)/libgralloc_extra.so \
	$(API28_BUILD)/libnativewindow.so \
	$(API28_BUILD)/libvndksupport.so

.PHONY: android android-api28 android-bayer-bench clean

android: $(PROBE)

$(PROBE): gles2_pbuffer_probe_android.c
	mkdir -p $(BUILD)
	$(ANDROID_CC) $(ANDROID_CFLAGS) \
		-Wl,--dynamic-linker,$(ANDROID_LINKER) -o $@ $< -ldl

android-api28: $(API28_PROBE) $(API28_ROTATE) $(API28_SHIMS)

# Explicit opt-in research targets. Never included in the release helper build.
android-bayer-bench: $(BAYER_BENCH)

$(API28_BUILD)/cronos-bayer-es2: $(BAYER_DEPENDENCIES)
	mkdir -p $(API28_BUILD)
	$(ANDROID_CC) $(ANDROID_CFLAGS) -DITERATIONS=64 \
		-Wl,--dynamic-linker,/data/tater-gpu/linker -o $@ $< -ldl -lm

$(API28_BUILD)/cronos-bayer-es3: $(BAYER_DEPENDENCIES)
	mkdir -p $(API28_BUILD)
	$(ANDROID_CC) $(ANDROID_CFLAGS) -DTATER_GPU_BAYER_ES3 -DITERATIONS=64 \
		-Wl,--dynamic-linker,/data/tater-gpu/linker -o $@ $< -ldl -lm

$(API28_BUILD)/cronos-bayer-hybrid: $(BAYER_DEPENDENCIES)
	mkdir -p $(API28_BUILD)
	$(ANDROID_CC) $(ANDROID_CFLAGS) -DTATER_GPU_BAYER_ES3 -DTATER_GPU_BAYER_UNPACKED -DITERATIONS=64 \
		-Wl,--dynamic-linker,/data/tater-gpu/linker -o $@ $< -ldl -lm

$(API28_PROBE): gles2_pbuffer_probe_android.c
	mkdir -p $(API28_BUILD)
	$(ANDROID_CC) $(ANDROID_CFLAGS) \
		-Wl,--dynamic-linker,$(ANDROID_API28_LINKER) -o $@ $< -ldl

$(API28_ROTATE): rgba_rotate_stream_android.c
	mkdir -p $(API28_BUILD)
	$(ANDROID_CC) $(ANDROID_CFLAGS) \
		-Wl,--dynamic-linker,/data/tater-gpu/linker -o $@ $< -ldl

$(API28_BUILD)/libbionic28compat.so: bionic28_compat.c bionic28_compat.map
	mkdir -p $(API28_BUILD)
	$(ANDROID_API28_CC) -shared -fPIC -Os -Wall -Wextra -Werror \
		-Wl,-soname,libbionic28compat.so \
		-Wl,--version-script,bionic28_compat.map -o $@ $<

$(API28_BUILD)/libged.so: gpu_optional_shims.c libged.map
	mkdir -p $(API28_BUILD)
	$(ANDROID_CC) -shared -fPIC -Os -Wall -Wextra -Werror \
		-Wl,-soname,libged.so -Wl,--version-script,libged.map -o $@ $<

$(API28_BUILD)/libgpu_aux.so: gpu_optional_shims.c libgpu_aux.map
	mkdir -p $(API28_BUILD)
	$(ANDROID_CC) -shared -fPIC -Os -Wall -Wextra -Werror \
		-Wl,-soname,libgpu_aux.so \
		-Wl,--version-script,libgpu_aux.map -o $@ $<

$(API28_BUILD)/libgralloc_extra.so: gpu_optional_shims.c libgralloc_extra.map
	mkdir -p $(API28_BUILD)
	$(ANDROID_CC) -shared -fPIC -Os -Wall -Wextra -Werror \
		-Wl,-soname,libgralloc_extra.so \
		-Wl,--version-script,libgralloc_extra.map -o $@ $<

$(API28_BUILD)/libnativewindow.so: nativewindow_shim.c
	mkdir -p $(API28_BUILD)
	$(ANDROID_CC) -shared -fPIC -Os -Wall -Wextra -Werror \
		-Wl,-soname,libnativewindow.so -o $@ $<

$(API28_BUILD)/libvndksupport.so: vndksupport_shim.c
	mkdir -p $(API28_BUILD)
	$(ANDROID_CC) -shared -fPIC -Os -Wall -Wextra -Werror \
		-Wl,-soname,libvndksupport.so -o $@ $< -ldl

clean:
	rm -f $(PROBE) $(API28_PROBE) $(API28_ROTATE) $(API28_SHIMS) $(BAYER_BENCH)
