#ifndef SV_CACHE_H
#define SV_CACHE_H

#define VALUES_PER_ENTRY 100
#define NUMBER_OF_ENTRIES 80

// the IVSHMEM cache
// one of these per-stream
typedef struct {
    uint64_t sv_values[VALUES_PER_ENTRY];
    uint8_t id[4];
} Entry;

typedef struct {
    Entry entries[NUMBER_OF_ENTRIES];
    uint8_t right;

} SV_Cache;

#endif
