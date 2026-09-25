#ifndef TIME_UTILS_H
#define TIME_UTILS_H

#include <stdint.h>
#include <time.h>

// This is the helper that I've used for simulating the protection workload
// Two things should be noted here:
// 1. I tried calibrating my own TSC timer and it worked
// a little bit better than this version, but the difference was really small
// so I stuck with the solution that is more robust, especially in a VM
// 2. A clock_gettime with CLOCK_MONOTONIC is not a system call,
// (see https://kernel-internals.org/syscalls/vdso/)
// but one with CLOCK_THREAD_CPUTIME_ID is. The overhead is too high
// so this function should not be implemented with the latter clock type.

static inline void burn_cpu(int microseconds) {
    if (!microseconds) {
        return;
    }
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);

    do {
        clock_gettime(CLOCK_MONOTONIC, &now);
    } while ((now.tv_sec - start.tv_sec) * 1000000 +
                 (now.tv_nsec - start.tv_nsec) * 0.001 <
             microseconds);
}

static inline int timespec_subtract(struct timespec *result,
                                    const struct timespec *x,
                                    const struct timespec *y) {
    result->tv_sec = x->tv_sec - y->tv_sec;
    result->tv_nsec = x->tv_nsec - y->tv_nsec;

    if (result->tv_nsec < 0) {
        result->tv_nsec += 1000000000;
        result->tv_sec -= 1;
    }

    return (result->tv_sec < 0);
}

static inline uint64_t ns_now_raw(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static inline void ns_to_timespec(uint64_t ns, struct timespec *ts) {
    ts->tv_sec = ns / 1000000000ull;
    ts->tv_nsec = ns % 1000000000ull;
}

#endif
