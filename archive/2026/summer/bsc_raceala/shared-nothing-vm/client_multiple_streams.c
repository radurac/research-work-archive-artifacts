#define _GNU_SOURCE
#include <arpa/inet.h>
#include <err.h>
#include <errno.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../net_utils.h"
#include "../sv_frame.h"
#include "../time_utils.h"
#include "../measurement_constants.h"

#define IFNAME "enp8s0"
#define DST_MAC "3c:ec:ef:62:ac:40"


// Pin the receive IRQ to core 1
// Nothing else is needed


int main(int argc, char *argv[]) {

    if (argc < 4) {
        goto wrong_args;
    }

    pthread_t self = pthread_self();
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(1, &cpuset);
    int s = pthread_setaffinity_np(self, sizeof(cpu_set_t), &cpuset);
    if (s != 0) {
        fprintf(stderr, "pthread_setaffinity_np failed: %s\n", strerror(s));
        exit(EXIT_FAILURE);
    }

    unsigned char sv_frame[] = {
        0x3c, 0xec, 0xef, 0x62, 0xac, 0x40, 0x52, 0x54, 0x00, 0xc5, 0x66, 0xf5,
        0x88, 0xba, 0x40, 0x1,  0x0,  0x66, 0x0,  0x0,  0x0,  0x0,  0x60, 0x5c,
        0x80, 0x1,  0x1,  0xa2, 0x57, 0x30, 0x55, 0x80, 0x4,  0x34, 0x30, 0x30,
        0x31, 0x82, 0x2,  0x1,  0x18, 0x83, 0x4,  0x0,  0x0,  0x0,  0x1,  0x85,
        0x1,  0x2,  0x87, 0x40, 0xff, 0xfe, 0x59, 0x82, 0x0,  0x0,  0x0,  0x0,
        0x0,  0x4,  0x3d, 0xdc, 0x0,  0x0,  0x0,  0x0,  0xff, 0xfd, 0x6f, 0x5c,
        0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x6,  0xba, 0x0,  0x0,  0x20, 0x0,
        0xff, 0x8d, 0xf4, 0x0,  0x0,  0x0,  0x0,  0x0,  0x1,  0x1d, 0xfb, 0xc2,
        0x0,  0x0,  0x0,  0x0,  0xff, 0x55, 0x60, 0xc,  0x0,  0x0,  0x0,  0x0,
        0x0,  0x1,  0x4f, 0xce, 0x0,  0x0,  0x20, 0x0};

    errno = 0;
    int stream_count = strtol(argv[1], NULL, 0);
    if (errno) {
        goto wrong_args;
    }
    if (stream_count < 1 || stream_count > MAX_STREAMS) {
        fprintf(stderr, "The stream_count must be between 1 and 128\n");
        exit(EXIT_FAILURE);
    }

    int sample_count = strtol(argv[2], NULL, 0);
    if (errno) {
        goto wrong_args;
    }
    if (sample_count < 0) {
        fprintf(stderr, "The sample_count must be positive\n");
        exit(EXIT_FAILURE);
    }

    int burn_in_us = strtol(argv[3], NULL, 0);
    if (errno) {
        goto wrong_args;
    }
    if (burn_in_us < 0) {
        fprintf(stderr, "burn_in_us must be positive\n");
        exit(EXIT_FAILURE);
    }

    int recv_sockfd = socket(AF_PACKET, SOCK_RAW, htons(0x88ba));
    if (recv_sockfd == -1) {
        perror("socket init failed");
        exit(EXIT_FAILURE);
    }

    unsigned char *svr_frame = malloc(200);
    if (!svr_frame) {
        goto alloc_failed;
    }

    struct sockaddr_ll socket_address;
    memset(&socket_address, 0, sizeof(struct sockaddr_ll));
    socket_address.sll_family = AF_PACKET;
    socket_address.sll_protocol = htons(0x88ba);
    socket_address.sll_ifindex = if_nametoindex(IFNAME);
    int send_sockfd = socket(AF_PACKET, SOCK_RAW, 0);
    if (send_sockfd == -1) {
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

    uint8_t id_ring[4000] = {};
    uint32_t id;

    for (int i = 0; i < (sample_count + DUMMY_SIZE) * stream_count; i++) {
        if (recv(recv_sockfd, svr_frame, 200, 0) == -1) {
            perror("error recv");
            exit(EXIT_FAILURE);
        }
        burn_cpu(burn_in_us);

        copy_id_to_frame(svr_frame + 33, sv_frame);
        id = read_id_bytes(svr_frame + 33);

        uint16_t id_ring_pos = id % 4000;
        id_ring[id_ring_pos] += 1;

        if (id_ring[id_ring_pos] == stream_count) {
            id_ring[id_ring_pos] = 0;
            if (sendto(send_sockfd, sv_frame, sizeof(sv_frame), 0,
                       (struct sockaddr *)&socket_address,
                       sizeof(struct sockaddr_ll)) < 0) {
                perror("sendto failed");
                exit(EXIT_FAILURE);
            }
        }
    }

    exit(EXIT_SUCCESS);

alloc_failed:
    fprintf(stderr, "Failed to allocate memory\n");
    exit(EXIT_FAILURE);
wrong_args:
    fprintf(stderr, "args: <stream_count> <sample_count> <burn_in_us>\n");
    exit(EXIT_FAILURE);
}
