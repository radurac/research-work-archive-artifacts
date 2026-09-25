#include <stdint.h>
#include <stdbool.h>

#define NUM_DEVS 128
#define DEDUPE_RING_CAPACITY 4096

// no frames sent by a device for 20 ms
// reset corresponding valid_ring
#define INACTIVITY_THRESHOLD_IN_NS 20000000

// the deduplication algorithm
// the pseudocode presented in the thesis
// but with a lot of bitwise arithmetic
bool dedupe_alg(uint8_t *epoch_ring, uint8_t *valid_ring, uint16_t seq) {

    uint16_t epoch = seq >> 12;

    uint16_t index = seq & 4095;
    uint16_t epoch_index = index >> 1;
    uint16_t valid_index = index >> 3;
    uint8_t nibble_index = index & 1;
    uint8_t bit_index = index & 7;

    uint8_t valid_bit = valid_ring[valid_index] & (1 << bit_index);
    uint8_t stored_epoch;

    if (nibble_index == 0) {
        stored_epoch = epoch_ring[epoch_index] & 15;
        epoch_ring[epoch_index] += epoch - stored_epoch;
    } else {
        stored_epoch = epoch_ring[epoch_index] >> 4;
        epoch_ring[epoch_index] = (epoch_ring[epoch_index] & 15) + (epoch << 4);
    }
    if (epoch == stored_epoch && valid_bit) {
        return false;
    }

    valid_ring[valid_index] = valid_ring[valid_index] | (1 << bit_index);

    return true;
}
