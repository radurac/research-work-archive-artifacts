#define _GNU_SOURCE
#include <arpa/inet.h>
#include <err.h>
#include <fcntl.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>
#include <x86intrin.h>
#include "errno.h"

#include "ivshmem_vm_utils.h"
#include "../net_utils.h"
#include "../time_utils.h"
#include "vm_measure.h"
#include "../measurement_constants.h"

// no-monitor setup

#define IFINDEX "enp9s0"
#define DST_MAC "3c:ec:ef:62:ac:40"

uint16_t throughput_points[] = {1,  2,  3,  4,  6,  8,  11,
                                16, 23, 32, 45, 64, 91, 128};

unsigned char sv_frame[] = {
    0x3c, 0xec, 0xef, 0x62, 0xac, 0x40, 0x52, 0x54, 0x00, 0xd8, 0x6d, 0x54,
    0x88, 0xba, 0x40, 0x1,  0x0,  0x66, 0x0,  0x0,  0x0,  0x0,  0x60, 0x5c,
    0x80, 0x1,  0x1,  0xa2, 0x57, 0x30, 0x55, 0x80, 0x4,  0x34, 0x30, 0x30,
    0x31, 0x82, 0x2,  0x1,  0x18, 0x83, 0x4,  0x0,  0x0,  0x0,  0x1,  0x85,
    0x1,  0x2,  0x87, 0x40, 0xff, 0xfe, 0x59, 0x82, 0x0,  0x0,  0x0,  0x0,
    0x0,  0x4,  0x3d, 0xdc, 0x0,  0x0,  0x0,  0x0,  0xff, 0xfd, 0x6f, 0x5c,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x6,  0xba, 0x0,  0x0,  0x20, 0x0,
    0xff, 0x8d, 0xf4, 0x0,  0x0,  0x0,  0x0,  0x0,  0x1,  0x1d, 0xfb, 0xc2,
    0x0,  0x0,  0x0,  0x0,  0xff, 0x55, 0x60, 0xc,  0x0,  0x0,  0x0,  0x0,
    0x0,  0x1,  0x4f, 0xce, 0x0,  0x0,  0x20, 0x0};

int burn_in_us;

int main(int argc, char *argv[]) {
    if (argc < 3) {
        goto wrong_args;
    }

    if (mlockall(MCL_CURRENT | MCL_FUTURE)) {
        perror("mlock failed");
        exit(EXIT_FAILURE);
    }

    pthread_t announcer = pthread_self();
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(1, &cpuset);
    int s = pthread_setaffinity_np(announcer, sizeof(cpu_set_t), &cpuset);
    if (s != 0) {
        fprintf(stderr, "pthread_setaffinity_np failed: %s\n", strerror(s));
        exit(EXIT_FAILURE);
    }

    errno = 0;
    int mode = strtol(argv[1], NULL, 0);
    if (errno) {
        goto wrong_args;
    }

    int sample_count = strtol(argv[2], NULL, 0);
    if (errno) {
        goto wrong_args;
    }
    if (sample_count < 0) {
        fprintf(stderr, "The sample count cannot be negative\n");
        exit(EXIT_FAILURE);
    }

    int stream_count;
    int step = 0;

    if (mode == 0) {
        if (argc < 5) {
            goto wrong_args;
        }

        stream_count = strtol(argv[3], NULL, 0);
        if (errno) {
            goto wrong_args;
        }
        if (stream_count < 1 || stream_count > MAX_STREAMS) {
            fprintf(stderr, "stream count must be between 1 and 128\n");
            exit(EXIT_FAILURE);
        }
        burn_in_us = strtol(argv[4], NULL, 0);
        if (errno) {
            goto wrong_args;
        }
        if (burn_in_us < 0) {
            fprintf(stderr, "burn_in_us must be positive\n");
            exit(EXIT_FAILURE);
        }
    } else if (mode == 1) {

        // will increase stepwise during the measurement
        stream_count = throughput_points[step++];
        burn_in_us = 0;
    } else {
        fprintf(
            stderr,
            "Only mode 0 - regular and mode 1 - throughput are supported\n");
        exit(EXIT_FAILURE);
    }

    SV_Cache *sv_caches = ivshmem_init(MAX_STREAMS);
    if (!sv_caches) {
        fprintf(stderr, "Failed to initialize the ivshmem region\n");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_ll socket_address;
    memset(&socket_address, 0, sizeof(struct sockaddr_ll));
    socket_address.sll_family = AF_PACKET;
    socket_address.sll_protocol = htons(0x88ba);
    socket_address.sll_ifindex = if_nametoindex(IFINDEX);
    int sockfd = socket(AF_PACKET, SOCK_RAW, 0);
    if (sockfd == -1) {
        perror("socket init failed");
        exit(EXIT_FAILURE);
    }
    if (socket_address.sll_ifindex == 0) {
        perror("if_nametoindex failed");
        exit(EXIT_FAILURE);
    }
    socket_address.sll_halen = ETH_ALEN;
    uint8_t *dst_mac = malloc(ETH_ALEN);
    if (!dst_mac) {
        goto alloc_failed;
    }
    mac_addr_a2n(dst_mac, DST_MAC);
    memcpy(socket_address.sll_addr, dst_mac, ETH_ALEN);

    uint8_t *ptr = calloc(MAX_STREAMS, sizeof(uint8_t));
    if (!ptr) {
        goto alloc_failed;
    }

    volatile uint64_t *values = malloc(sizeof(uint64_t) * VALUES_PER_ENTRY);
    if (!values) {
        goto alloc_failed;
    }
    int processed_frames = 0;
    volatile uint8_t *right;
    uint32_t id;
    uint8_t id_ring[4000] = {};

    while (1) {
        for (uint8_t stream_id = 0; stream_id < stream_count; stream_id++) {
            SV_Cache *sv_cache = sv_caches + stream_id;
            right = &(sv_cache->right);
            if (ptr[stream_id] != *right) {
                if (mode == 1 &&
                    (stream_count != 1 || processed_frames >= DUMMY_SIZE))
                    begin_processing();

                Entry *entry = &(sv_cache->entries[ptr[stream_id]]);

                memcpy(values, entry->sv_values,
                       VALUES_PER_ENTRY * sizeof(uint64_t));
                burn_cpu(burn_in_us);

                memcpy(sv_frame + 33, entry->id, 4);

                id = (entry->id[0] << 24) + (entry->id[1] << 16) +
                     (entry->id[2] << 8) + entry->id[3];

                uint16_t id_ring_pos = id % 4000;
                id_ring[id_ring_pos] += 1;

                if (id_ring[id_ring_pos] == stream_count) {
                    id_ring[id_ring_pos] = 0;
#ifdef SEND_HISTO
                    begin_send();
#endif
                    if (sendto(sockfd, sv_frame, sizeof(sv_frame), 0,
                               (struct sockaddr *)&socket_address,
                               sizeof(struct sockaddr_ll)) < 0) {
                        perror("send_to failed");
                        exit(EXIT_FAILURE);
                    }
#ifdef SEND_HISTO
                    end_send();
#endif
                }

                ptr[stream_id] = (ptr[stream_id] + 1) % NUMBER_OF_ENTRIES;
                processed_frames++;
                if (mode == 1 &&
                    (stream_count != 1 || processed_frames > DUMMY_SIZE))
                    end_processing(stream_count);
            }
        }
        if (mode == 0 &&
            processed_frames == (sample_count + DUMMY_SIZE) * stream_count) {
            break;
        } else if (mode == 1 &&
                   ((stream_count == 1 &&
                     processed_frames == sample_count + DUMMY_SIZE) ||
                    (stream_count != 1 &&
                     processed_frames == sample_count * stream_count))) {
            memset(id_ring, 0, sizeof(id_ring));
            if (dump_processing_histo(stream_count) == 1) {
                fprintf(stderr,
                        "Failed to dump processing histogram to file\n");
                exit(EXIT_FAILURE);
            }
            if (stream_count == MAX_STREAMS)
                break;

            stream_count = throughput_points[step++];
            processed_frames = 0;
        }
    }

#ifdef SEND_HISTO
    if (dump_send_histo() == 1) {
        fprintf(stderr, "Failed to dump send histogram to file\n");
        exit(EXIT_FAILURE);
    }
#endif

    exit(EXIT_SUCCESS);

alloc_failed:
    fprintf(stderr, "Failed when trying to allocate memory\n");
    exit(EXIT_FAILURE);
wrong_args:
    fprintf(stderr,
            "Usage:\n ./client_ivshmem 0 <sample_count> <stream_count> "
            "<burn_in_us> for a regular measurement\n ./client_ivshmem 1 "
            "<sample_count> for the VM CPU Time measurement\n");
    exit(EXIT_FAILURE);
}
