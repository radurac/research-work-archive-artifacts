#ifndef IVSHMEM_VM_UTILS_H
#define IVSHMEM_VM_UTILS_H

#include <sys/mman.h>

#include "../sv_cache.h"

#define IVSHMEM_DEV_PATH "/sys/bus/pci/devices/0000:02:01.0/resource2"

SV_Cache *ivshmem_init(uint8_t stream_count) {
    int fd;
    if ((fd = open(IVSHMEM_DEV_PATH, O_RDONLY, 0)) == -1) {
        fprintf(stderr, "Failed to open ivshmem device\n");
        return NULL;
    }

    SV_Cache *sv_caches = (SV_Cache *)mmap(
        NULL, sizeof(SV_Cache) * stream_count, PROT_READ, MAP_SHARED, fd, 0);
    if (sv_caches == MAP_FAILED) {
        return NULL;
    }

    return sv_caches;
}

#endif
