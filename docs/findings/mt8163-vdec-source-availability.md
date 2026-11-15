# MT8163 decoder source availability

Date: 2026-11-11

A public Lineage/Amazon hardware tree was inspected to determine whether the
working Checkers hardware-decoder path could be rebuilt from published
userspace source. The inspected revision was
`17e590e68ef6cc4ec7c7b531f7486188bc3d9287` from the public
`mt8163/android_hardware_amazon` repository. The checkout remains ignored
reference material and none of its source or binaries has been copied into this
repository.

The tree contains useful, real source, but not the decoder implementation:

- `libstagefrighthw` is an open Stagefright plugin that loads
  `libMtkOmxCore.so`.
- `libMtkOmxCore` is an open OMX registry/loader. It loads the component path
  selected by the registry and resolves `MtkOmxComponentCreate` with `dlsym`.
- the example registry explicitly maps the AVC decoder to
  `libMtkOmxVdec.so`;
- the public video-codec headers describe the MediaTek VAL structures and name
  `VAL_CHIP_NAME_MT8163`; and
- no `libMtkOmxVdec` build target and no implementation of
  `MtkOmxComponentCreate` are present. The open core build also names a vendor
  proprietary OpenMAX include directory.

Therefore this tree publishes the framework-facing loader and part of the ABI,
not the codec component that manages reference pictures, normal IDR/P/B frame
ordering, and the legacy `/dev/Vcodec` hardware contract. It does not close the
redistributable-decoder gap recorded by the live probe.

This result does clarify the practical clean boundary. Tater can keep its
renderer, conversion, lifecycle policy, and a narrow adapter open while an
owner-operated device supplies its matching stock decoder component locally.
That is usable on existing hardware, but it must not be described as a fully
open firmware image. A fully redistributable path still requires either:

1. a genuinely published decoder implementation compatible with this MT8163
   VDEC generation;
2. an independently implemented userspace component validated from public ABI
   and black-box behavior; or
3. a new MT8163 kernel port exposing a standard V4L2 memory-to-memory decoder.

The third-party projects found during the search that wrap or patch
`libMtkOmxVdec*`, `libvcodecdrv`, or `libvcodec_utility` do not meet that
requirement; they still depend on unavailable vendor implementations. They may
be useful behavioral references, but are not clean source for Tater.

The finding can be reproduced against any Android hardware source checkout:

```sh
python3 scripts/audit_mt8163_vdec_source.py /path/to/android_hardware_amazon
```

The JSON report intentionally distinguishes a dynamically loaded component
from a component factory definition or build target. It is a source-availability
audit, not a license determination.
