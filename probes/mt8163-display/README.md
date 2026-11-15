# MT8163 display query

This target-gated probe queries the public framebuffer and MediaTek display
session ABIs without changing display state. It opens `/dev/fb0` and
`/dev/mtk_disp_mgr` read-only, reads geometry, asks for the primary session
description and display capabilities, checks suspend state, and returns the LCM
index. It does not mmap framebuffer memory, allocate a layer, submit a buffer,
wait on a fence, pan, blank, or issue a trigger.

Build a static ARMv7 binary with a musl cross compiler:

```sh
make CC=arm-linux-musleabihf-gcc
```

Run only on the owner-operated Checkers target:

```sh
./mt8163-display-query --read-only-query --target checkers
```

The ABI is transcribed from MediaTek's published MT8163 `disp_session.h` and
the standard Linux framebuffer API. Physical framebuffer addresses are never
printed.

## Presentation contract (source-mapped, not executed)

The public driver confirms the complete ION/fence path used by Android's
composer:

1. `CREATE_SESSION` returns a session ID for a primary, external, or memory
   session.
2. `PREPARE_INPUT_BUFFER` imports an ION file descriptor into a selected layer
   timeline and returns a fence plus buffer index.
3. `SET_INPUT_BUFFER` supplies the format, source pitch and crop, destination
   rectangle, alpha/security settings, and returned buffer index for up to
   four validated hardware overlay layers.
4. `GET_PRESENT_FENCE` optionally creates a primary-session present fence.
5. `TRIGGER_SESSION` commits the cached primary layer state and advances the
   present timeline.
6. `DESTROY_SESSION` releases the session and its fence timelines.

Checkers reports direct-link, single-pass output, four hardware layers, and
`DISP_FEATURE_NO_PARGB`. The driver validates RGB565 through YV12 but the
hardware decoder's 16x1 block YUV output is not directly one of those layer
formats. It must first be converted to a supported layout.

The current Tater renderer already owns `/dev/fb0`, rotates Checkers into its
portrait panel, cycles all three framebuffer pages, and provides
`PresentTransientRegions` specifically for rapidly replaced frame regions.
Consequently, the least disruptive first presentation route is:

```text
VDEC private block-YUV frame -> conversion -> Tater RGBA canvas
    -> PresentTransientRegions -> inactive fb page -> FBIOPAN_DISPLAY
```

Creating a second primary display-manager producer while that renderer is
active would introduce competing ownership and fence timelines. This source
map intentionally does not execute the mutating session/fence/trigger calls.
