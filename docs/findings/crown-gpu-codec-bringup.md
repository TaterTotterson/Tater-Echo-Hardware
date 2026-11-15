# Crown GPU and codec development checkpoint

Date: 2026-11-14. Owner-authorized development, not public-release approval.

## Target and source boundaries

Crown is the first-generation Echo Show 8 (C7H6N3), MT8163, with a 1280×800
landscape workspace and an 800×1280 physical framebuffer. The new helper
checks Crown's exact product token, kernel release, panel allocation and depth
before hardware access. It is not an impersonated Checkers/Cronos target.

Original source adds a native-geometry GPU variant and a Crown OMX encoder;
the latter uses the known newer linear-buffer ABI and OV9734 BGGR geometry.
Only the explicit Crown build selects these guards. Clean builds reproduce
these SHA-256 identities:

- GPU: `93d741156d90f2bfb000552abd17a56870ad11f0723aaa8e3dcdd3eb4d022de3`.
- Decoder: `abb64de71075441c32f43a7c8336bacc6364a34958f50028df5884ca2cc84d30`.
- Encoder: `6b0ea3d6d44f9f75b266b3cad352f2924c60f1aab096254df6c58d1548b3131c`.

Existing Checkers/Cronos helper outputs remain byte-identical. Each firmware
target pins its own helpers and runtime identities. Public helper bundles
contain exactly three original executables and metadata, never proprietary
libraries. Collected Crown vendor dependencies match the reviewed newer
runtime inputs; owner-local runtime directories were added only where absent.
The API-28 GPU tree is `bc75d30a7be167b827e89b1d7629ec5dc4b528c99106d0b1e9beed02b4815561`;
the 66-file headless codec tree is
`8564d383faba23c32545037f62a7ab9ddddf227a61790470dbc883e6eb871f0a`.
The optional stack-dump replacement does not replace ION allocation/codec work.

## Generated-only results

- All 1,024,000 rotation/swizzle pixels match the reference.
- The production Go menu matches every native-size pixel.
- Sixty direct-video/retained-control offscreen compositions average about
  30.44 FPS; this is not an end-to-end call frame-rate measurement.
- Bounded encoder/decoder jobs complete explicit OMX lifecycle teardown.
- The production hardware receiver preserves all 120 paced generated IDs.
- No scene/audio recordings were retained for these checks.

## Installed application and human checks

The normal application updater installed a bridge first, then the complete
`v4.2.3-crown-hardware.3` app. Both ordinary five-minute health gates passed
without a settle override. All nine installed component hashes match; pairing
and configuration digests remain unchanged. The system/store are read-only;
neither rootfs nor boot/recovery was flashed by this application trial.

The owner passed menu/fades/touch/EQ, Graphics: GPU, first-word capture, volume,
physical microphone privacy, spoken-response interruption and camera-cover
checks. The installed renderer reports GPU scenes and approximately 30 FPS at
idle. A bounded on-demand balanced camera stream measured 768×432, 16.5 source
FPS, 15.1 encoded FPS and 1.21 Mbit/s; encoder/sensor ownership released afterward.
Bayer development/copies remain CPU work. This is not all-hardware/zero-copy
capture and does not enable Cronos' special held-slot policy on Crown.

The owner's mixed call looked correct, but process/host inspection proved
JPEG fallback: the Tater host's qualified-board allowlist omitted Crown.
A small host fix plus compatibility/relay tests is staged; the host restart
and actual full-duplex hardware-call qualification remain for morning.
Do not conflate this fallback call with hardware-call success.

## Offline camera-clock candidate

The source camera cadence suggests investigating the clock-handle bug already
confirmed on Checkers/Cronos. It is not yet confirmed by Crown clock readback.
No speculative ISP jobs, clock pokes or power-sequence experiments were run.

The original boot/config and loaded wireless modules were read-only backed up
privately. The exact configuration SHA-256 is
`3f42a0d20ce666127cf65a97bb2b453cea23453b17465afe823362660d870e52`.
The existing clock-handle patch builds with original GCC 8.3 and preserves
`4.9.337-g8d928c5176cc`. All 202 Wi-Fi and 96 Bluetooth imported symbol CRCs match.
All five original appended DTBs and the original rescue ramdisk are retained.
The offline kernel payload SHA-256 is
`3afeb5b7e836e0783a6bfa689c163b7ae30ae4b170ebdbdabc37dac291a35cc0`;
the 13,477,888-byte private boot candidate is
`ff21813871ea862ecbaf3aa9103f7ba055471685c98e6c4addc65fb55ace5871`.
Neither candidate was installed. A successful compile/ABI check is not a safe
boot or clock-rate proof. Target evidence, verified recovery/backups and a
recoverable installed/readback trial are still required.

The older installed rootfs also has a mismatched ONNX runtime for the new app's
optional NWW confirmation. The new full rootfs contains the pinned runtime,
but the application-only MWW/boundary smoke check does not qualify that system
update. Final fleet OTA/rollback, thermal and factory/provisioning checks remain
deferred. Proprietary inputs, donor boot and serial-numbered logs stay private.
