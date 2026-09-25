#define _GNU_SOURCE
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_hexdump.h>
#include <rte_mbuf.h>
#include <rte_vhost.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>

#include "dedupe.h"
#include "../dpdk_common.h"
#include "../ivshmem_host_utils.h"
#include "../time_utils.h"
#include "../measurement_constants.h"

typedef struct {
    // dedupe
    uint8_t epoch_rings[NUM_DEVS][DEDUPE_RING_CAPACITY >> 1];
    uint8_t valid_rings[NUM_DEVS][DEDUPE_RING_CAPACITY >> 3];
    struct timespec last_seen[NUM_DEVS];
    // encaps
    uint16_t seq;

} PRP_State;

int stream_count;
int burn_in_us;
SV_Cache *sv_caches;

static const uint16_t port_phy0 = 0;
static const uint16_t port_phy1 = 1;
static volatile bool force_quit = false;
static int vhost_vid = -1;

static int new_device(int vid) {
    printf("\n[vhost] VM connected, vid=%d\n", vid);
    vhost_vid = vid;

    // disables VM -> vhost-user handler kick
    rte_vhost_enable_guest_notification(vid, 1, 0);
    return 0;
}

static void destroy_device(int vid) {
    printf("\n[vhost] VM disconnected, vid=%d\n", vid);
    if (vhost_vid == vid)
        vhost_vid = -1;
}

static const struct rte_vhost_device_ops vhost_ops = {
    .new_device = new_device,
    .destroy_device = destroy_device,
};

static void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        force_quit = true;
    }
}

static inline void encaps_lanA(struct rte_mbuf *m, PRP_State *prp_state) {
    uint8_t *data = (uint8_t *)rte_pktmbuf_append(m, 6);
    if (!data) {
        rte_exit(EXIT_FAILURE, "Not enough tailroom for encapsulation\n");
    }
    data[0] = prp_state->seq >> 8;
    data[1] = prp_state->seq & 255;
    data[2] = 0xa0 | (((m->pkt_len - 14) >> 8) & 15);

    // assume one single segment
    data[3] = (m->pkt_len - 14) & 255;
    data[4] = 0x88;
    data[5] = 0xfb;

    prp_state->seq += 1;
}

static inline bool check_SV(struct rte_mbuf *m) {
    uint8_t *data = rte_pktmbuf_mtod(m, uint8_t *);
    if (data[12] != 0x88 || data[13] != 0xba) {
        return false;
    }
    return true;
}

static inline bool check_and_reset(struct rte_mbuf *m, PRP_State *prp_state) {
    uint8_t *data = rte_pktmbuf_mtod(m, uint8_t *);
    if (data[12] != 0x88 || data[13] != 0xba)
        return false;
    if (data[RESET_POS] == RESET_VAL) {
        for (size_t stream_id = 0; stream_id < stream_count; stream_id++) {
            SV_Cache *sv_cache = sv_caches + stream_id;
            sv_cache->right = 0;
        }
        return true;
    }
    return false;
}

static inline void encaps_lanB(struct rte_mbuf *m) {
    uint8_t *data = rte_pktmbuf_mtod(m, uint8_t *);
    data[5] += 1;
    data[m->pkt_len - 4] = 0xb0 | (((m->pkt_len - 14) >> 8) & 15);
}

static inline bool dedupe(struct rte_mbuf *m, PRP_State *prp_state) {
    uint8_t *data = rte_pktmbuf_mtod(m, uint8_t *);
    uint16_t ether = (data[12] << 8) + data[13];
    if (ether != 0x88ba) {
        printf("incorrect PRP suffix\n");
        return false;
    }

    uint8_t svid = data[5];
    if (svid >= NUM_DEVS) {
        return false;
    }

    uint8_t *epoch_ring = prp_state->epoch_rings[svid];
    uint8_t *valid_ring = prp_state->valid_rings[svid];

    struct timespec now, res;
    clock_gettime(CLOCK_MONOTONIC, &now);
    timespec_subtract(&res, &now, &(prp_state->last_seen[svid]));

    if (res.tv_sec != 0 || res.tv_nsec > INACTIVITY_THRESHOLD_IN_NS) {
        memset(valid_ring, 0, DEDUPE_RING_CAPACITY >> 3);
    }
    prp_state->last_seen[svid] = now;

    uint8_t *prp_trailer = data + m->pkt_len - 6;
    if (prp_trailer[4] != 0x88 || prp_trailer[5] != 0xfb) {
        return false;
    }
    uint16_t seq = (prp_trailer[0] << 8) + prp_trailer[1];

    return dedupe_alg(epoch_ring, valid_ring, seq);
}

static inline void precondition(struct rte_mbuf *m) {

    unsigned char *data = rte_pktmbuf_mtod(m, unsigned char *);
    uint8_t stream_id = data[5];

    SV_Cache *sv_cache = sv_caches + stream_id;
    measure_sv(data, sv_cache, burn_in_us);
}

int main(int argc, char *argv[]) {
    if (mlockall(MCL_CURRENT | MCL_FUTURE)) {
        perror("mlock failed");
        exit(EXIT_FAILURE);
    }

    int ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "EAL init failed\n");

    argc -= ret;
    argv += ret;

    stream_count = NUM_DEVS;

    if (argc < 2) {
        rte_exit(EXIT_FAILURE, "you need to pass burn_in_us as an argument\n");
    }

    errno = 0;
    burn_in_us = strtol(argv[1], NULL, 0);
    if (errno || burn_in_us < 0) {
        rte_exit(EXIT_FAILURE, "burn_in_us must be positive\n");
    }

    sv_caches = ivshmem_init(stream_count);
    if (!sv_caches) {
        rte_exit(EXIT_FAILURE, "Failed to initialize ivshmem region\n");
    }

    PRP_State *prp_state = malloc(sizeof(PRP_State));
    if (!prp_state) {
        rte_exit(EXIT_FAILURE, "Failed to initialize ivshmem region\n");
    }

    memset(prp_state, 0, sizeof(PRP_State));

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    for (size_t i = 0; i < NUM_DEVS; i++)
        prp_state->last_seen[i] = now;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    struct rte_mempool *mbuf_pool = rte_pktmbuf_pool_create(
        "MBUF_POOL", NUM_MBUFS, MBUF_CACHE_SIZE, 0,
        RTE_MBUF_DEFAULT_BUF_SIZE + MBUF_TAILROOM, rte_socket_id());
    if (!mbuf_pool)
        rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

    if (port_init(port_phy0, mbuf_pool) != 0)
        rte_exit(EXIT_FAILURE, "Cannot init port 0\n");
    if (port_init(port_phy1, mbuf_pool) != 0)
        rte_exit(EXIT_FAILURE, "Cannot init port 1\n");

    const char *socket_path = "/tmp/vhost-user1";
    unlink(socket_path);

    if (rte_vhost_driver_register(socket_path, 0) != 0)
        rte_exit(EXIT_FAILURE, "vhost driver register failed\n");
    if (rte_vhost_driver_callback_register(socket_path, &vhost_ops) != 0)
        rte_exit(EXIT_FAILURE, "vhost callback register failed\n");

    if (rte_vhost_driver_start(socket_path) != 0)
        rte_exit(EXIT_FAILURE, "vhost driver start failed\n");

    printf("\nWaiting for VM on %s \n", socket_path);

    uint32_t counter = 0;

    struct rte_mbuf *bufs[BURST_SIZE];

    while (!force_quit) {
        if (vhost_vid == -1) {
            usleep(1000);
            continue;
        }

        for (;;) {
            uint16_t n = rte_vhost_dequeue_burst(vhost_vid, 1, mbuf_pool, bufs,
                                                 BURST_SIZE);
            if (n == 0)
                break;
            for (uint16_t i = 0; i < n; i++) {
                if (!check_SV(bufs[i])) {
                    rte_pktmbuf_free(bufs[i]);
                    continue;
                }
                encaps_lanA(bufs[i], prp_state);
                struct rte_mbuf *mcopy =
                    rte_pktmbuf_copy(bufs[i], mbuf_pool, 0, UINT32_MAX);
                if (rte_eth_tx_burst(port_phy0, 0, &bufs[i], 1) == 0) {
                    fprintf(stderr, "TX failed on port 0\n");
                    rte_pktmbuf_free(bufs[i]);
                }
                if (mcopy) {
                    encaps_lanB(mcopy);
                    if (rte_eth_tx_burst(port_phy1, 0, &mcopy, 1) == 0) {
                        fprintf(stderr, "TX failed on port 1\n");
                        rte_pktmbuf_free(mcopy);
                    }
                } else {
                    fprintf(stderr, "mcopy failed\n");
                }
            }
        }
        {
            uint16_t n;
            if ((n = rte_eth_rx_burst(port_phy0, 0, bufs, BURST_SIZE)) > 0) {
                for (size_t i = 0; i < n; i++) {
                    if (!check_and_reset(bufs[i], prp_state) &&
                        dedupe(bufs[i], prp_state)) {
                        counter++;
                        precondition(bufs[i]);
                        rte_pktmbuf_free(bufs[i]);
                    } else {
                        rte_pktmbuf_free(bufs[i]);
                    }
                }
            }
        }

        {
            uint16_t n;
            if ((n = rte_eth_rx_burst(port_phy1, 0, bufs, BURST_SIZE)) > 0) {
                for (size_t i = 0; i < n; i++) {
                    if (!check_and_reset(bufs[i], prp_state) &&
                        dedupe(bufs[i], prp_state)) {
                        counter++;
                        precondition(bufs[i]);
                        rte_pktmbuf_free(bufs[i]);
                    } else {
                        rte_pktmbuf_free(bufs[i]);
                    }
                }
            }
        }
    }
    printf("%u\n", counter);
    rte_eth_dev_stop(port_phy0);
    rte_eth_dev_stop(port_phy1);
    rte_vhost_driver_unregister(socket_path);
    rte_eal_cleanup();

    return 0;
}
