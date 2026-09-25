#define _GNU_SOURCE
#include <arpa/inet.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include "ivshmem_vm_utils.h"
#include "../net_utils.h"
#include "../time_utils.h"
#include "vm_measure.h"
#include "efd_read.h"
#include "../measurement_constants.h"

// monitor unoptimized setup
// ALWAYS run with sudo chrt -f 30
// otherwise EEVDF does things we do not want
// also disable RT throttling beforehand
// with sudo sysctl -w kernel.sched_rt_runtime_us=-1

#define IFNAME "enp9s0"
#define DST_MAC "3c:ec:ef:62:ac:40"

typedef struct {
    atomic_int counter;
    uint8_t id_ring[4000];
} Control;

uint64_t yield_and_read(Control *ctrl, int vpac_efd, int sync_efd) {
    uint64_t u = 1;
    volatile atomic_int *counter = &(ctrl->counter);

    int old_counter = atomic_fetch_sub(counter, 1);
    if (old_counter == 1) {
        write(sync_efd, &u, sizeof(uint64_t));
    }

    int ret = efd_read(vpac_efd, &u);
    if (ret < 0) {
        perror("eventfd read failed");
        exit(EXIT_FAILURE);
    }

    uint64_t stream_id = u - 1;
    return stream_id;
}

// args: <vpac_count> <stream_count> <sample_count> <burn_in_us>
// vpac_count must be smaller than the stream_count;
// the streams are equally split between the VPACs
// initially the code assigned all streams to all VPACs
// considering that one protection function probably takes only tens to hundreds
// of ns, modeling protection functions as processes is perhaps unrealistic we
// measured 1 stream -> 1 VPAC for those reasons but the implementation would
// work for any number of VPACs

int main(int argc, char *argv[]) {

    if (argc < 5) {
        goto wrong_args;
    }

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

    if (mlockall(MCL_CURRENT | MCL_FUTURE)) {
        perror("mlock failed");
        exit(EXIT_FAILURE);
    }

    pthread_t main = pthread_self();
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(1, &cpuset);
    int s = pthread_setaffinity_np(main, sizeof(cpu_set_t), &cpuset);
    if (s != 0) {
        fprintf(stderr, "pthread_setaffinity_np failed: %s\n", strerror(s));
        exit(EXIT_FAILURE);
    }

    errno = 0;
    int vpac_count = strtol(argv[1], NULL, 0);
    if (errno) {
        goto wrong_args;
    }
    if (vpac_count < 1) {
        fprintf(stderr, "The vpac_count must be strictly positive\n");
        exit(EXIT_FAILURE);
    }

    int stream_count = strtol(argv[2], NULL, 0);
    if (errno) {
        goto wrong_args;
    }
    if (stream_count < 1 || stream_count > MAX_STREAMS) {
        fprintf(stderr, "The stream_count must be between 1 and 128\n");
        exit(EXIT_FAILURE);
    }

    int sample_count = strtol(argv[3], NULL, 0);
    if (errno) {
        goto wrong_args;
    }
    if (sample_count < 0) {
        fprintf(stderr, "The sample_count must be positive\n");
        exit(EXIT_FAILURE);
    }

    int burn_in_us = strtol(argv[4], NULL, 0);
    if (errno) {
        goto wrong_args;
    }
    if (burn_in_us < 0) {
        fprintf(stderr, "burn_in_us must be positive\n");
        exit(EXIT_FAILURE);
    }

    if (vpac_count > stream_count) {
        fprintf(stderr, "<stream_count> must be higher than <vpac_count>\n");
        exit(EXIT_FAILURE);
    }

    SV_Cache *sv_caches = ivshmem_init(MAX_STREAMS);
    if (!sv_caches) {
        fprintf(stderr, "Failed to initialize ivshmem region; the path to the "
                        "BAR in ivshmem_vm_utils.h is probably incorrect\n");
        exit(EXIT_FAILURE);
    }

    Control *ctrl =
        (Control *)mmap(NULL, sizeof(Control), PROT_READ | PROT_WRITE,
                        MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (ctrl == MAP_FAILED) {
        fprintf(stderr, "Failed to initialize control region\n");
        exit(EXIT_FAILURE);
    }

    memset(ctrl, 0, sizeof(Control));

    pid_t *pids = malloc(vpac_count * sizeof(pid_t));
    if (!pids) {
        goto alloc_failed;
    }
    int *eventfds = malloc(vpac_count * sizeof(int));
    if (!eventfds) {
        goto alloc_failed;
    }
    int sync_efd;
    volatile uint8_t *right;

    uint64_t u = 1;
    sync_efd = eventfd(0, 0);
    if (sync_efd == -1) {
        fprintf(stderr,
                "Failed to allocate the global synchronization eventfd\n");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < vpac_count; i++) {
        eventfds[i] = eventfd(0, 0);
        if (eventfds[i] == -1) {
            fprintf(stderr, "Failed to allocate one of the VPAC eventfds\n");
            exit(EXIT_FAILURE);
        }
        pid_t pid = fork();
        switch (pid) {
        case 0:
            for (int j = 0; j < i; j++) {
                close(eventfds[j]);
            }

            volatile uint64_t *values =
                malloc(sizeof(uint64_t) * VALUES_PER_ENTRY);
            if (!values) {
                goto alloc_failed;
            }
            uint64_t u = 1;
            uint8_t *ptr = calloc(stream_count, sizeof(uint8_t));
            if (!ptr) {
                goto alloc_failed;
            }

            int processed_frames = 0;
            uint32_t id = 0;

            struct sockaddr_ll socket_address;
            memset(&socket_address, 0, sizeof(struct sockaddr_ll));
            socket_address.sll_family = AF_PACKET;
            socket_address.sll_protocol = htons(0x88ba);
            socket_address.sll_ifindex = if_nametoindex(IFNAME);
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
            memcpy(sv_frame, dst_mac, ETH_ALEN);
            memcpy(socket_address.sll_addr, dst_mac, ETH_ALEN);

            int ret = efd_read(eventfds[i], &u);
            if (ret < 0) {
                perror("eventfd read failed");
                exit(EXIT_FAILURE);
            }
            uint64_t stream_id = u - 1;

            while (1) {

                SV_Cache *sv_cache = sv_caches + stream_id;
                Entry *entry = &(sv_cache->entries[ptr[stream_id]]);

                memcpy(values, entry->sv_values,
                       VALUES_PER_ENTRY * sizeof(uint64_t));
                burn_cpu(burn_in_us);
                memcpy(sv_frame + 33, entry->id, 4);

                id = (entry->id[0] << 24) + (entry->id[1] << 16) +
                     (entry->id[2] << 8) + entry->id[3];

                uint16_t id_ring_pos = id % 4000;
                ctrl->id_ring[id_ring_pos]++;

                if (ctrl->id_ring[id_ring_pos] == stream_count) {
                    ctrl->id_ring[id_ring_pos] = 0;
                    if (sendto(sockfd, sv_frame, sizeof(sv_frame), 0,
                               (struct sockaddr *)&socket_address,
                               sizeof(struct sockaddr_ll)) < 0) {
                        perror("send_to failed");
                        exit(EXIT_FAILURE);
                    }
                }

                processed_frames++;

                ptr[stream_id] = (ptr[stream_id] + 1) % NUMBER_OF_ENTRIES;
                stream_id = yield_and_read(ctrl, eventfds[i], sync_efd);
            }
            _exit(EXIT_SUCCESS);

        case -1:
            exit(EXIT_FAILURE);

        default:
            pids[i] = pid;
        }
    }

    uint8_t *ptr = calloc(stream_count, sizeof(uint8_t));
    if (!ptr) {
        goto alloc_failed;
    }

    int processed_frames = 0;
    volatile atomic_int *counter = &(ctrl->counter);
    do {
        for (uint64_t stream_id = 0; stream_id < stream_count; stream_id++) {
            right = &((sv_caches + stream_id)->right);
            if (ptr[stream_id] != *right) {
#ifdef PROCESSING_HISTO
                if (processed_frames >= DUMMY_SIZE * stream_count)
                    begin_processing();
#endif

                u = stream_id + 1;
                atomic_store(counter, 1);

                // protection algorithms for one stream
                // are not split across VPACs
                int vpac_id = (stream_id * vpac_count) / stream_count;

                write(eventfds[vpac_id], &u, sizeof(uint64_t));
                int ret = efd_read(sync_efd, &u);
                if (ret < 0) {
                    perror("eventfd read failed");
                    exit(EXIT_FAILURE);
                }
                ptr[stream_id] = (ptr[stream_id] + 1) % NUMBER_OF_ENTRIES;
                processed_frames += 1;

#ifdef PROCESSING_HISTO
                if (processed_frames > DUMMY_SIZE * stream_count)
                    end_processing(stream_count);
#endif
            }
        }
    } while (processed_frames != (sample_count + DUMMY_SIZE) * stream_count);

    for (size_t i = 0; i < vpac_count; i++)
        kill(pids[i], SIGKILL);

#ifdef PROCESSING_HISTO
    if (dump_processing_histo(stream_count) == 1) {
        fprintf(stderr, "Failed to dump processing histogram to file\n");
        exit(EXIT_FAILURE);
    }
#endif

    exit(EXIT_SUCCESS);

alloc_failed:
    fprintf(stderr, "Failed when trying to allocate memory\n");
    exit(EXIT_FAILURE);
wrong_args:
    fprintf(stderr,
            "args: <vpac_count> <stream_count> <sample_count> <burn_in_us>\n");
    exit(EXIT_FAILURE);
}
