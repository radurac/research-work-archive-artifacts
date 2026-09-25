#include "dedupe.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>

// some very basic tests for the deduplication algorithm

uint8_t epoch_ring[DEDUPE_RING_CAPACITY >> 1];
uint8_t valid_ring[DEDUPE_RING_CAPACITY >> 3];

void reset() {
    memset(epoch_ring, 0, sizeof(epoch_ring));
    memset(valid_ring, 0, sizeof(valid_ring));
}

// basic test - no losses, no asymmetric delays

void basic_test() {
    for (size_t i = 0; i <= 131070; i++) {
        if (!dedupe_alg(epoch_ring, valid_ring, i % 65536))
            goto failed;
        if (dedupe_alg(epoch_ring, valid_ring, i % 65536))
            goto failed;
    }

    printf("Basic test: passed\n");
    return;

failed:
    printf("Basic test: failed\n");
}

// simulating one epoch lost (~1 ms of downtime) on both lans

void loss_test() {
    for (size_t i = 0; i <= 131070; i++) {
        if (!(i % 4096)) {
            i += 4095;
            continue;
        }
        if (!dedupe_alg(epoch_ring, valid_ring, i % 65536))
            goto failed;
        if (dedupe_alg(epoch_ring, valid_ring, i % 65536))
            goto failed;
    }

    printf("Loss test: passed\n");
    return;

failed:
    printf("Loss test: failed\n");
}

// simulating one lan being at least 250 us slower than the other one
// whether the roles change is irrelevant with this approach (unlike the
// window-based dedupe)

void asymmetry_test() {
    if (!dedupe_alg(epoch_ring, valid_ring, 0))
        goto failed;

    for (size_t i = 1; i <= 131070; i++) {
        if (!dedupe_alg(epoch_ring, valid_ring, i % 65536))
            goto failed;
        if (dedupe_alg(epoch_ring, valid_ring, (i - 1) % 65536))
            goto failed;
    }

    printf("Asymmetry test: passed\n");
    return;

failed:
    printf("Asymmetry test: failed\n");
}

void drop_both_failure_test() {

    // warm the ring up
    for (size_t i = 0; i < 4096; i++) {
        if (!dedupe_alg(epoch_ring, valid_ring, i))
            goto failed;
        if (dedupe_alg(epoch_ring, valid_ring, i))
            goto failed;
    }

    // always skip the first sequence number in the epoch
    for (size_t i = 4096; i < 65536; i++) {
        if (!(i % 4096)) {
            continue;
        }
        if (!dedupe_alg(epoch_ring, valid_ring, i % 65536))
            goto failed;
        if (dedupe_alg(epoch_ring, valid_ring, i % 65536))
            goto failed;
    }

    // note that in a perfect world we should NOT drop here
    // however, this is expected behavior with this algorithm as described in
    // the thesis
    if (dedupe_alg(epoch_ring, valid_ring, 0))
        goto failed;

    printf("Drop both failure behavior as expected\n");
    return;

failed:
    printf("Unexpected drop both failure behavior\n");
}

void accept_both_failure_test() {

    // warm the ring up
    for (size_t i = 0; i < 8192; i++) {
        // lan A only; simulate asymmetric delays of over 1 ms (assuming 250 us
        // Sampled Values)
        if (!dedupe_alg(epoch_ring, valid_ring, i))
            goto failed;
    }

    for (size_t i = 0; i < 4096; i++) {
        // lan B only; note that in a perfect world we should NOT accept here
        // however, this is expected behavior with this algorithm as described
        // in the thesis
        if (!dedupe_alg(epoch_ring, valid_ring, i))
            goto failed;
    }

    printf("Accept both failure behavior as expected\n");
    return;
failed:
    printf("Unexpected accept both failure behavior\n");
}

int main() {

    basic_test();
    reset();

    loss_test();
    reset();

    asymmetry_test();
    reset();

    drop_both_failure_test();
    reset();

    accept_both_failure_test();
}
