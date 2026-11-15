/* SPDX-License-Identifier: MIT
 * Read selected Biscuit ALSA controls; never open a PCM or write a control.
 * Build on Linux: cc -std=c11 -D_GNU_SOURCE -O2 -Wall -Wextra -Werror ...
 */
#include <errno.h>
#include <fcntl.h>
#include <sound/asound.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <unistd.h>

static void json_string(const char *text) {
    putchar('"');
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        if (*p == '"' || *p == '\\') printf("\\%c", *p);
        else if (*p < 32 || *p >= 127) printf("\\u%04x", *p);
        else putchar(*p);
    }
    putchar('"');
}

static bool selected(const char *name) {
    static const char *const suffixes[] = {
        "Digital Volume Control", "MICPGA Volume Ctrl", "Left Fine Volume", "Right Fine Volume",
        "DIF1_L Input Gain", "DIF1_R Input Gain", "Left Mute", "Right Mute",
    };
    static const char *const playback[] = {
        "PCM Playback Volume", "HP Driver Gain Volume", "Audio_DacMux_Setting",
        "Right Channel Only", "Speaker_Amp_Switch", "Ext_Speaker_Amp_Switch",
        "DRC Control", "Ignore Ramp Up",
        "IDC Enable", /* Scalar diagnostic-mode flag only; never enable it. */
        "Tater ADC HPF Status", /* Read-only schema/corner/slot-mask/rate, no coefficients. */
    };
    if (strlen(name) > 6 && !strncmp(name, "ADC_", 4) && name[4] >= 'A' && name[4] <= 'D' && name[5] == ' ') {
        for (size_t i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); ++i)
            if (!strcmp(name + 6, suffixes[i])) return true;
        /* The input-route control repeats its ADC letter in the suffix. */
        char route[96];
        snprintf(route, sizeof(route), "ADC_%c Left Ip Select ADC_%c DIF1_L switch", name[4], name[4]);
        if (!strcmp(name, route)) return true;
        snprintf(route, sizeof(route), "ADC_%c Right Ip Select ADC_%c DIF1_R switch", name[4], name[4]);
        if (!strcmp(name, route)) return true;
    }
    for (size_t i = 0; i < sizeof(playback) / sizeof(playback[0]); ++i)
        if (!strcmp(name, playback[i])) return true;
    return false;
}

static bool dsp_name(const char *name) {
    return strstr(name, "DSP") || strstr(name, "dsp") || strstr(name, "Filter") ||
        strstr(name, "Processing") || strstr(name, "processing") || strstr(name, "Biquad");
}

static int self_test(void) {
    if (!selected("ADC_D Left Ip Select ADC_D DIF1_L switch") ||
        !selected("ADC_C MICPGA Volume Ctrl") || !selected("PCM Playback Volume") ||
        selected("ADC_E MICPGA Volume Ctrl") || selected("power_button_state") ||
        selected("biquad coefficients") || !selected("IDC Enable") ||
        !selected("Tater ADC HPF Status") ||
        !dsp_name("ADC_A Processing Block")) return 1;
    puts("read-only allowlist self-test passed");
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--self-test")) return self_test();
    if (argc != 2 || strcmp(argv[1], "--read-only-mixer")) {
        fputs("usage: biscuit_mixer_probe --read-only-mixer\n", stderr);
        return 2;
    }
    char hostname[128] = {0};
    struct utsname kernel;
    if (gethostname(hostname, sizeof(hostname) - 1) || uname(&kernel) ||
        strcmp(hostname, "tater-biscuit") || strcmp(kernel.release, "3.18.19-bt")) {
        fputs("refusing: expected Tater Biscuit on its qualified 3.18.19-bt kernel\n", stderr);
        return 1;
    }
    int fd = open("/dev/snd/controlC0", O_RDONLY | O_CLOEXEC);
    if (fd < 0) { perror("open read-only mixer"); return 1; }
    struct snd_ctl_card_info card = {0};
    if (ioctl(fd, SNDRV_CTL_IOCTL_CARD_INFO, &card) || strcmp((char *)card.id, "mtsndcard")) {
        fputs("refusing: unexpected ALSA card\n", stderr);
        close(fd);
        return 1;
    }
    struct snd_ctl_elem_list list = {0};
    if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_LIST, &list) || list.count > 4096) {
        fputs("invalid mixer inventory\n", stderr); close(fd); return 1;
    }
    unsigned count = list.count;
    struct snd_ctl_elem_id *ids = calloc(count ? count : 1, sizeof(*ids));
    if (!ids) { close(fd); return 1; }
    list.space = count;
    list.pids = ids;
    if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_LIST, &list) || list.used > count) {
        fputs("mixer inventory changed\n", stderr); free(ids); close(fd); return 1;
    }
    printf("{\"schema\":1,\"target\":\"biscuit\",\"read_only\":true,\"pcm_opened\":false,"
           "\"samples_read\":false,\"mixer_writes\":0,\"control_count\":%u,\"controls\":[", list.used);
    bool first = true;
    unsigned errors = 0;
    for (unsigned i = 0; i < list.used; ++i) {
        char name[sizeof(ids[i].name) + 1];
        memcpy(name, ids[i].name, sizeof(ids[i].name));
        name[sizeof(ids[i].name)] = 0;
        bool read_value = selected(name);
        if (!read_value && !dsp_name(name)) continue;
        if (!first) putchar(',');
        first = false;
        printf("{\"name\":"); json_string(name);
        struct snd_ctl_elem_info info = {0};
        info.id = ids[i];
        if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_INFO, &info)) {
            printf(",\"error\":\"info-failed\"}"); ++errors; continue;
        }
        printf(",\"type\":%u,\"count\":%u,\"value_read\":%s", info.type, info.count, read_value ? "true" : "false");
        if (info.type == SNDRV_CTL_ELEM_TYPE_INTEGER)
            printf(",\"min\":%ld,\"max\":%ld,\"step\":%ld", info.value.integer.min,
                   info.value.integer.max, info.value.integer.step);
        if (!read_value) { putchar('}'); continue; }
        if (info.count > 128 || (info.type != SNDRV_CTL_ELEM_TYPE_BOOLEAN &&
            info.type != SNDRV_CTL_ELEM_TYPE_INTEGER && info.type != SNDRV_CTL_ELEM_TYPE_ENUMERATED)) {
            printf(",\"error\":\"unsupported-value-type\"}"); ++errors; continue;
        }
        struct snd_ctl_elem_value value = {0};
        value.id = ids[i];
        if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_READ, &value)) {
            printf(",\"error\":\"read-failed\"}"); ++errors; continue;
        }
        printf(",\"values\":[");
        for (unsigned j = 0; j < info.count; ++j) {
            if (j) putchar(',');
            if (info.type == SNDRV_CTL_ELEM_TYPE_ENUMERATED) {
                struct snd_ctl_elem_info item = info;
                item.value.enumerated.item = value.value.enumerated.item[j];
                if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_INFO, &item)) {
                    printf("null"); ++errors;
                } else {
                    item.value.enumerated.name[sizeof(item.value.enumerated.name) - 1] = 0;
                    json_string(item.value.enumerated.name);
                }
            } else printf("%ld", value.value.integer.value[j]);
        }
        printf("]}");
    }
    printf("],\"errors\":%u}\n", errors);
    free(ids);
    close(fd);
    return errors ? 1 : 0;
}
