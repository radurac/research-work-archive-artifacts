#ifndef IVSHMEM_HOST_UTILS_H
#define IVSHMEM_HOST_UTILS_H

#include <stdatomic.h>
#include <sys/mman.h>

#include "sv_cache.h"
#include "time_utils.h"

#define IVSHMEM_NAME "ivshmem"

// this ivshmem_init() function
// does not actually create the ivshmem file host-side
// you must create it yourself with
// truncate -s 16M /dev/shm/IVSHMEM_NAME
SV_Cache *ivshmem_init(uint8_t stream_count) {
    int fd;
    if ((fd = shm_open(IVSHMEM_NAME, O_RDWR, 0)) == -1) {
        return NULL;
    }

    SV_Cache *sv_caches =
        (SV_Cache *)mmap(NULL, sizeof(SV_Cache) * stream_count,
                         PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (sv_caches == MAP_FAILED) {
        return NULL;
    }

    memset(sv_caches, 0, sizeof(SV_Cache) * stream_count);
    return sv_caches;
}

void measure_sv(unsigned char *data, SV_Cache *sv_cache, int burn_in_us) {

    uint8_t right = sv_cache->right;
    Entry *entry = &(sv_cache->entries[right]);

    entry->id[0] = data[33];
    entry->id[1] = data[34];
    entry->id[2] = data[35];
    entry->id[3] = data[36];

    // force memory pressure (realistic because the phasor computation uses
    // previous values)
    for (size_t i = 0; i < VALUES_PER_ENTRY; i++) {
        entry->sv_values[i] += 1;
    }
    burn_cpu(burn_in_us);

    // note that right is one byte long
    // a torn read is not a concern here
    right = (right + 1) % NUMBER_OF_ENTRIES;
    sv_cache->right = right;
}

#endif
