#include <locale.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/system_properties.h>
#include <time.h>

typedef void (*property_read_callback)(void *cookie, const char *name,
                                       const char *value, uint32_t serial);

double strtod_l(const char *text, char **end, locale_t locale) {
    (void)locale;
    return strtod(text, end);
}

float strtof_l(const char *text, char **end, locale_t locale) {
    (void)locale;
    return strtof(text, end);
}

void __system_property_read_callback(const prop_info *info,
                                     property_read_callback callback,
                                     void *cookie) {
    char name[PROP_NAME_MAX];
    char value[PROP_VALUE_MAX];

    if (info == NULL || callback == NULL)
        return;
    if (__system_property_read(info, name, value) < 0)
        return;
    callback(cookie, name, value, 0);
}

bool __system_property_wait(const prop_info *info, uint32_t old_serial,
                            uint32_t *new_serial,
                            const struct timespec *timeout) {
    (void)info;
    (void)timeout;
    if (new_serial != NULL)
        *new_serial = old_serial;
    return false;
}

int __android_log_is_debuggable(void) {
    return 0;
}
