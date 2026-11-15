// SPDX-License-Identifier: MIT
// Narrow public-library compatibility for the headless Cronos experiment.
// These functions do not open devices, alter properties, or stub codec work.
#include <cstdio>
#include <cstring>
#include <string_view>

extern "C" char *fgets_unlocked(char *buffer, int capacity, FILE *file) {
    // Taking stdio's ordinary lock is safe for this diagnostic reader.
    return fgets(buffer, capacity, file);
}

namespace android {
namespace base {
bool StartsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() &&
        (prefix.empty() || memcmp(value.data(), prefix.data(), prefix.size()) == 0);
}
}
}
