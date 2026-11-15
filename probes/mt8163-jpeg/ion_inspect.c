#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

#define ION_NODE "/dev/ion"
#define ION_MAX_HEAPS 32U
#define ION_HEAP_NAME_LENGTH 32U

struct ion_heap_data {
    char name[ION_HEAP_NAME_LENGTH];
    uint32_t type;
    uint32_t heap_id;
    uint32_t reserved0;
    uint32_t reserved1;
    uint32_t reserved2;
};

struct ion_heap_query {
    uint32_t count;
    uint32_t reserved0;
    uint64_t heaps;
    uint32_t reserved1;
    uint32_t reserved2;
};

#define ION_IOC_HEAP_QUERY _IOWR('I', 8, struct ion_heap_query)

_Static_assert(sizeof(struct ion_heap_data) == 52,
               "ION heap data ABI must remain 52 bytes");
_Static_assert(sizeof(struct ion_heap_query) == 24,
               "ION heap query ABI must remain 24 bytes");

static void print_json_string(const char *value, size_t capacity) {
    putchar('"');
    for (size_t index = 0; index < capacity && value[index] != '\0'; ++index) {
        unsigned char byte = (unsigned char)value[index];
        if (byte == '"' || byte == '\\') {
            putchar('\\');
            putchar((int)byte);
        } else if (byte >= 0x20 && byte <= 0x7e) {
            putchar((int)byte);
        } else {
            printf("\\u%04x", (unsigned int)byte);
        }
    }
    putchar('"');
}

int main(void) {
    struct ion_heap_query query = {0};
    struct ion_heap_data *heaps = NULL;
    int data_query_errno = 0;
    int descriptor = open(ION_NODE, O_RDONLY | O_CLOEXEC);
    int result = 2;

    if (descriptor < 0) {
        fprintf(stderr, "could not open %s: %s\n", ION_NODE, strerror(errno));
        return result;
    }
    if (ioctl(descriptor, ION_IOC_HEAP_QUERY, &query) != 0) {
        fprintf(stderr, "ION heap-count query failed: %s\n", strerror(errno));
        goto cleanup;
    }
    if (query.count == 0 || query.count > ION_MAX_HEAPS) {
        fprintf(stderr, "refusing unexpected ION heap count %u\n", query.count);
        goto cleanup;
    }

    heaps = calloc(query.count, sizeof(*heaps));
    if (heaps == NULL) {
        fprintf(stderr, "could not allocate heap result array\n");
        goto cleanup;
    }
    query.heaps = (uint64_t)(uintptr_t)heaps;
    if (ioctl(descriptor, ION_IOC_HEAP_QUERY, &query) != 0) {
        data_query_errno = errno;
        /*
         * The MT8163 vendor tree copies the heap array and count, but leaves
         * ion_query_heaps()'s return value initialized to -EINVAL. Accept that
         * one known post-copy status; all other errors remain fatal.
         */
        if (data_query_errno != EINVAL) {
            fprintf(stderr, "ION heap-data query failed: %s\n",
                    strerror(data_query_errno));
            goto cleanup;
        }
    }

    printf("{\"node\":\"%s\",\"heap_count\":%u,", ION_NODE, query.count);
    if (data_query_errno == EINVAL) {
        printf("\"vendor_post_copy_status\":\"EINVAL\",");
    }
    printf("\"heaps\":[");
    for (uint32_t index = 0; index < query.count; ++index) {
        if (index != 0) {
            putchar(',');
        }
        printf("{\"name\":");
        print_json_string(heaps[index].name, sizeof(heaps[index].name));
        printf(",\"type\":%u,\"id\":%u}", heaps[index].type,
               heaps[index].heap_id);
    }
    puts("]}");
    result = 0;

cleanup:
    free(heaps);
    close(descriptor);
    return result;
}
