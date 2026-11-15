# License and source notices

Tater's original code and documentation without a separate license notice are
Copyright (c) 2026 Tater Totterson AI LLC and licensed under the GNU Affero
General Public License version 3 only (`AGPL-3.0-only`); see [LICENSE](LICENSE).

File-specific notices take precedence:

- Helpers and diagnostics marked `SPDX-License-Identifier: MIT` remain MIT
  licensed; see [LICENSES/MIT.txt](LICENSES/MIT.txt).
- Linux kernel modules, kernel-source patches and device-tree work marked
  GPL-2.0-only (or GPL-2.0), including files under `kernel-patches/`, retain
  the Linux GPLv2 terms; see [LICENSES/GPL-2.0-only.txt](LICENSES/GPL-2.0-only.txt).
  They are separate kernel components, not relicensed AGPLv3 modules.
- Public Android headers, NDKs, Linux source and other external build inputs
  retain their upstream notices and licenses. Build recipes identify pinned
  source inputs; they do not replace the upstream license terms.

Historical `Tater Hardware Research` author labels identify this project's
previous name; they are retained in existing patches and diagnostic metadata.

Proprietary Amazon, MediaTek and Arm runtime libraries/firmware, device backups,
calibration, keys and recordings are not part of this repository or licensed
by it. Owner-local build/provisioning inputs stay private and ignored. Source
publication does not grant redistribution rights to those inputs.
