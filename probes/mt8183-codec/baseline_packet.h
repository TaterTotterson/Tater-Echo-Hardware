/* Original bounded Annex-B access-unit gate. No codec implementation. */
#ifndef TATER_ATHENA_BASELINE_PACKET_H
#define TATER_ATHENA_BASELINE_PACKET_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define AP_INPUT_CAPACITY (1024U * 1024U)
#define AP_PARAMETER_CAPACITY 32768U
struct ap_parameter_sets {
    unsigned char sps[AP_PARAMETER_CAPACITY], pps[AP_PARAMETER_CAPACITY];
    unsigned sps_bytes, pps_bytes;
};
static unsigned ap_prefix(const unsigned char *p, size_t bytes, size_t at) {
    if (at + 4 < bytes && !p[at] && !p[at+1] && !p[at+2] && p[at+3] == 1) return 4;
    if (at + 3 < bytes && !p[at] && !p[at+1] && p[at+2] == 1) return 3;
    return 0;
}
static int ap_set(unsigned char *dst, unsigned *stored, const unsigned char *src, unsigned bytes) {
    if (bytes > AP_PARAMETER_CAPACITY) return -1;
    if (*stored) return *stored == bytes && !memcmp(dst, src, bytes) ? 0 : -1;
    memcpy(dst, src, bytes); *stored = bytes; return 0;
}
static int ap_ue(const unsigned char *p, unsigned bytes, unsigned *bit, unsigned *value) {
    unsigned zeros = 0, result = 1;
    while (*bit < bytes * 8 && !(p[*bit / 8] & (0x80U >> (*bit % 8)))) {
        ++*bit; if (++zeros > 30) return -1;
    }
    if (*bit >= bytes * 8 || bytes * 8 - ++*bit < zeros) return -1;
    for (unsigned i = 0; i < zeros; i++, ++*bit)
        result = (result << 1) | !!(p[*bit / 8] & (0x80U >> (*bit % 8)));
    *value = result - 1; return 0;
}
static int ap_slice(const unsigned char *p, unsigned bytes, unsigned kind) {
    unsigned char rbsp[32]; unsigned used = 0, zero_count = 0, bit = 0, first, type;
    for (unsigned i = 0; i < bytes && used < sizeof rbsp; i++) {
        if (zero_count >= 2 && p[i] == 3) {
            if (i + 1 == bytes || p[i+1] > 3) return -1;
            zero_count = 0; continue;
        }
        rbsp[used++] = p[i]; zero_count = p[i] ? 0 : zero_count + 1;
    }
    if (ap_ue(rbsp, used, &bit, &first) || first || ap_ue(rbsp, used, &bit, &type) || type > 9)
        return -1;
    /* Baseline progressive single-slice I/P only. Never silently accept B. */
    type %= 5;
    return (type == 0 || type == 2) && (kind != 5 || type == 2) ? 0 : -1;
}
/* Can compact video in place: destination never overtakes the parsed source.
 * Parameter-set changes, multi-slice pictures and unsupported NALs fail closed.
 * Returns 1 for one picture, 0 for parameter sets only, -1 for rejection. */
static int ap_baseline_packet(struct ap_parameter_sets *sets, unsigned char *packet,
                              unsigned bytes, unsigned *video_bytes) {
    unsigned slices = 0; *video_bytes = 0;
    if (!bytes || bytes > AP_INPUT_CAPACITY - 65536U - 64U || !ap_prefix(packet, bytes, 0)) return -1;
    for (unsigned at = 0; at < bytes;) {
        unsigned prefix = ap_prefix(packet, bytes, at);
        if (!prefix || packet[at + prefix] & 0x80) return -1;
        unsigned next = at + prefix + 1;
        while (next < bytes && !ap_prefix(packet, bytes, next)) next++;
        unsigned kind = packet[at + prefix] & 31, length = next - at;
        if (kind == 7) {
            if (length < prefix + 4 || packet[at + prefix + 1] != 66 ||
                (packet[at + prefix + 2] & 3) ||
                ap_set(sets->sps, &sets->sps_bytes, packet + at, length)) return -1;
        } else if (kind == 8) {
            if (length < prefix + 2 || ap_set(sets->pps, &sets->pps_bytes, packet + at, length)) return -1;
        } else if (kind == 1 || kind == 5 || kind == 6) {
            if ((kind == 1 || kind == 5) &&
                (++slices > 1 || ap_slice(packet + at + prefix + 1, length - prefix - 1, kind))) return -1;
            memmove(packet + *video_bytes, packet + at, length); *video_bytes += length;
        } else if (kind != 9) return -1;
        at = next;
    }
    if (!sets->sps_bytes || !sets->pps_bytes ||
        sets->sps_bytes + sets->pps_bytes + *video_bytes + 6U + 64U > AP_INPUT_CAPACITY) return -1;
    return slices ? 1 : (*video_bytes ? -1 : 0);
}
#endif
