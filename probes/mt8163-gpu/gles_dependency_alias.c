/* SPDX-License-Identifier: MIT
 * A legacy DT_NEEDED alias, not fake GLES entry points. Link with the owner's
 * exact libGLES_mali.so using --no-as-needed so real API resolution stays in
 * that DDK. This file never creates a context or opens a hardware device.
 */
void tater_gles_dependency_alias(void) {}
