#include <inttypes.h>
#include <stdbool.h>
#include <string.h>
#include <sys/socket.h>

#include "../sv_frame.h"

enum Action { DROP, ACCEPT, INVALID };

enum Dest { A, B, BOTH };

typedef struct {
    uint16_t seq;
    bool lanA;
    bool lanB;
} PRP_Sample;

typedef struct {
    // for dupe/encaps
    uint16_t seq_tx;
    int stream_count;
    uint16_t port_phy0;
    uint16_t port_phy1;
    struct rte_mempool *mbuf_pool;

    // for dedupe/decaps
    uint32_t sample_count;
    PRP_Sample *prp_samples; // of size sample count, lookup table keyed by id
} PRP_State;

static const uint8_t PRP_SUFFIX[2] = {0x88, 0xfb};
static const uint8_t LAN_A = 0xa;
static const uint8_t LAN_B = 0xb;

struct rte_mbuf *prepare_sv_frame(const uint8_t *data, uint16_t data_size,
                                  struct rte_mempool *mbuf_pool) {
    struct rte_mbuf *m = rte_pktmbuf_alloc(mbuf_pool);
    if (m == NULL)
        return NULL;

    uint8_t *mbuf_data = (uint8_t *)rte_pktmbuf_append(m, data_size);
    if (mbuf_data == NULL) {
        rte_pktmbuf_free(m);
        return NULL;
    }
    rte_memcpy(mbuf_data, data, data_size);
    return m;
}

static inline void flush_prp_samples(PRP_State *prp_state) {
    memset(prp_state->prp_samples, 0,
           sizeof(PRP_Sample) * prp_state->sample_count);
}

bool prp_sendto(uint8_t *sv_frame, uint32_t len, PRP_State *prp_state,
                int inc_seq, enum Dest dest) {

    memcpy(sv_frame + len - 2, PRP_SUFFIX, 2);

    // the size is the length of the frame
    // without the PRP suffix and the header
    uint16_t size = len - 14;

    sv_frame[len - 3] = size & 0xff;

    // left nibble is LAN_A
    // right nibble is the most significant nibble of size
    sv_frame[len - 4] = (size >> 8) + (LAN_A << 4);

    sv_frame[len - 5] = prp_state->seq_tx & 0xff;
    sv_frame[len - 6] = prp_state->seq_tx >> 8;

    if (dest == A || dest == BOTH) {
        struct rte_mbuf *mbuf_lan_A =
            prepare_sv_frame(sv_frame, len, prp_state->mbuf_pool);
        if (!mbuf_lan_A) {
            fprintf(stderr, "Failed to allocate memory for mbuf\n");
            return false;
        }
        if (rte_eth_tx_burst(prp_state->port_phy0, 0, &mbuf_lan_A, 1) == 0) {
            fprintf(stderr, "rte_eth_tx_burst failed on lan A");
            rte_pktmbuf_free(mbuf_lan_A);
            return false;
        }
    }

    if (dest == B || dest == BOTH) {
        sv_frame[len - 4] = sv_frame[len - 4] - (LAN_A << 4) + (LAN_B << 4);

        struct rte_mbuf *mbuf_lan_B =
            prepare_sv_frame(sv_frame, len, prp_state->mbuf_pool);
        if (!mbuf_lan_B) {
            fprintf(stderr, "Failed to allocate memory for mbuf\n");
            return false;
        }

        if (rte_eth_tx_burst(prp_state->port_phy1, 0, &mbuf_lan_B, 1) == 0) {
            fprintf(stderr, "rte_eth_tx_burst failed on lan B");
            rte_pktmbuf_free(mbuf_lan_B);
            return false;
        }
    }

    // we are simulating multiple MU devices
    // logically we have to keep stream_count sequence numbers
    // but we just keep one and increment it at the end of the window
    if (inc_seq)
        prp_state->seq_tx = (prp_state->seq_tx + 1) % 65536;
    return true;
}

// only executes if CORRECTNESS 1 is set in the load-gen
enum Action prp_rx_check_update(uint8_t *sv_frame, uint32_t len,
                                PRP_State *prp_state) {
    if (memcmp(sv_frame + len - 2, PRP_SUFFIX, 2)) {
        fprintf(stderr, "Incorrect PRP suffix received\n");
        return INVALID;
    }

    uint16_t size = ((sv_frame[len - 4] & 0xF) << 8) + sv_frame[len - 3];
    uint8_t lane = (sv_frame[len - 4] >> 4) & 0xF;
    if (lane != LAN_A && lane != LAN_B) {
        fprintf(stderr, "Incorrect lane field received\n");
        return INVALID;
    }
    if (size != len - 14) {
        fprintf(stderr,
                "Incorrect size field received: expected %" PRIu32
                " but got %" PRIu16 "\n",
                len - 14, size);
        return INVALID;
    }

    uint16_t seq = (sv_frame[len - 6] << 8) + sv_frame[len - 5];
    uint32_t id = read_id_from_frame(sv_frame);

    if (id >= prp_state->sample_count) {
        fprintf(stderr, "Out-of-bounds id found in the frame\n");
        return INVALID;
    }

    enum Action action;

    PRP_Sample *prp_sample = prp_state->prp_samples + id;
    if (!prp_sample->lanA && !prp_sample->lanB) {
        prp_sample->seq = seq;
        action = ACCEPT;
    } else {
        if (seq != prp_sample->seq) {
            fprintf(stderr, "Different sequence numbers for the same id\n");
            return INVALID;
        }
        if (prp_sample->lanA && lane == LAN_A) {
            fprintf(stderr, "Repeat on LAN A\n");
            return INVALID;
        }
        if (prp_sample->lanB && lane == LAN_B) {
            fprintf(stderr, "Repeat on LAN B\n");
            return INVALID;
        }
        action = DROP;
    }

    if (lane == LAN_A) {
        prp_sample->lanA = true;
    } else {
        prp_sample->lanB = true;
    }

    return action;
}
