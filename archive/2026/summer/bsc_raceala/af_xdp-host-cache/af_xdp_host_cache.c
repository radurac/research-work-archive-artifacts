
#define _GNU_SOURCE
#include <bpf/bpf.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/if_xdp.h>
#include <net/if.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <xdp/xsk.h>

#include "../ivshmem_host_utils.h"
#include "../time_utils.h"
#include "../measurement_constants.h"

#define NUM_FRAMES 4096
#define FRAME_SIZE 2048
#define UMEM_SIZE (NUM_FRAMES * FRAME_SIZE)
#define INVALID_UMEM_FRAME UINT64_MAX

#define NUM_QUEUES 2
#define FRAMES_PER_QUEUE (NUM_FRAMES / NUM_QUEUES)
#define XSK_BIND_MODE XDP_ZEROCOPY

#define RX_BATCH_SIZE 8
#define BUSY_POLL_US 2
#define BUSY_POLL_BUDGET 8

#ifndef XDP_FLAGS_MODES
#define XDP_FLAGS_MODES 3
#endif

#ifndef XDP_FLAGS_UPDATE_IF_NOEXIST
#define XDP_FLAGS_UPDATE_IF_NOEXIST (1U << 0)
#endif

#ifndef XDP_FLAGS_SKB_MODE
#define XDP_FLAGS_SKB_MODE (1U << 1)
#endif

#ifndef XDP_FLAGS_DRV_MODE
#define XDP_FLAGS_DRV_MODE (1U << 2)
#endif

#ifndef XDP_FLAGS_GENERIC
#define XDP_FLAGS_GENERIC XDP_FLAGS_SKB_MODE
#endif

#ifndef XDP_FLAGS_DRV
#define XDP_FLAGS_DRV XDP_FLAGS_DRV_MODE
#endif

#define HOST_CACHE_CORE 9

struct nic_xdp {
    struct bpf_object *obj;
    int map_fd;
    unsigned int ifindex;
};

int burn_in_us;
SV_Cache *sv_caches;

struct xsk_queue {
    const char *ifname;
    __u32 queue_id;
    struct xsk_socket *xsk;
    struct xsk_ring_cons rx;
    struct xsk_ring_prod tx;
    struct xsk_ring_prod fq;
    struct xsk_ring_cons cq;
    int fd;
    __u32 base_frame;
};

// load XDP programs
int nic_xdp_load(struct nic_xdp *n, const char *ifname, const char *obj_path) {
    int err;

    n->ifindex = if_nametoindex(ifname);
    if (!n->ifindex) {
        fprintf(stderr, "if_nametoindex(%s): %s\n", ifname, strerror(errno));
        return -1;
    }

    n->obj = bpf_object__open_file(obj_path, NULL);
    if (!n->obj) {
        fprintf(stderr, "open %s: %s\n", obj_path, strerror(errno));
        return -1;
    }

    err = bpf_object__load(n->obj);
    if (err) {
        fprintf(stderr, "load %s: %s\n", obj_path, strerror(-err));
        goto err_close;
    }

    struct bpf_program *prog = bpf_object__next_program(n->obj, NULL);
    if (!prog) {
        fprintf(stderr, "no program in %s\n", obj_path);
        goto err_close;
    }

    err =
        bpf_xdp_attach(n->ifindex, bpf_program__fd(prog),
                       XDP_FLAGS_DRV_MODE | XDP_FLAGS_UPDATE_IF_NOEXIST, NULL);
    if (err) {
        fprintf(stderr, "attach to %s (native): %s\n", ifname, strerror(-err));
        goto err_close;
    }

    n->map_fd = bpf_object__find_map_fd_by_name(n->obj, "xsks_map");
    if (n->map_fd < 0) {
        fprintf(stderr, "xsks_map not found in %s\n", obj_path);
        goto err_detach;
    }
    return 0;

err_detach:
    bpf_xdp_detach(n->ifindex, XDP_FLAGS_DRV_MODE, NULL);
err_close:
    bpf_object__close(n->obj);
    n->obj = NULL;
    return -1;
}

// unload XDP programs
void nic_xdp_unload(struct nic_xdp *n) {
    if (!n->obj)
        return;
    bpf_xdp_detach(n->ifindex, XDP_FLAGS_DRV_MODE, NULL);
    bpf_object__close(n->obj);
    n->obj = NULL;
}

int counter;

static volatile sig_atomic_t running = 1;
static void sig_handler(int sig) {
    running = 0;
}

static void process_packet(void *packet_data, __u32 len) {

    unsigned char *sv_frame = (unsigned char *)packet_data;
    if (sv_frame[12] != 0x88U || sv_frame[13] != 0xbaU) {
        return;
    }
    if (sv_frame[RESET_POS] == RESET_VAL) {
        for (size_t stream_id = 0; stream_id < MAX_STREAMS; stream_id++) {
            SV_Cache *sv_cache = sv_caches + stream_id;
            sv_cache->right = 0;
        }
        return;
    }
    counter++;

    uint8_t stream_id = sv_frame[5];
    if (stream_id > 127) {
        fprintf(stderr,
                "stream_id %u in the SV frame, higher than 127, not valid\n",
                stream_id);
        return;
    }

    SV_Cache *sv_cache = sv_caches + stream_id;
    measure_sv(sv_frame, sv_cache, burn_in_us);
}

// busy poll AF_XDP
static void apply_busy_poll(int fd) {
    int on = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_PREFER_BUSY_POLL, &on, sizeof(on)) < 0)
        fprintf(stderr, "SO_PREFER_BUSY_POLL failed: %s\n", strerror(errno));

    int usec = BUSY_POLL_US;
    if (setsockopt(fd, SOL_SOCKET, SO_BUSY_POLL, &usec, sizeof(usec)) < 0)
        fprintf(stderr, "SO_BUSY_POLL failed: %s\n", strerror(errno));

    int budget = BUSY_POLL_BUDGET;
    if (setsockopt(fd, SOL_SOCKET, SO_BUSY_POLL_BUDGET, &budget,
                   sizeof(budget)) < 0)
        fprintf(stderr, "SO_BUSY_POLL_BUDGET failed: %s\n", strerror(errno));
}

// gives over the empty descriptors to the kernel
static int fill_queue(struct xsk_queue *q) {
    __u32 idx;
    unsigned int n = xsk_ring_prod__reserve(&q->fq, FRAMES_PER_QUEUE, &idx);
    if (n != FRAMES_PER_QUEUE) {
        fprintf(stderr, "could not reserve fill ring slots (%u)\n", n);
        return -1;
    }
    for (unsigned int i = 0; i < FRAMES_PER_QUEUE; i++)
        *xsk_ring_prod__fill_addr(&q->fq, idx + i) =
            (q->base_frame + i) * (__u64)FRAME_SIZE;
    xsk_ring_prod__submit(&q->fq, FRAMES_PER_QUEUE);
    return 0;
}

static void handle_queue(struct xsk_queue *q, void *umem_buffer) {
    (void)recvfrom(q->fd, NULL, 0, MSG_DONTWAIT, NULL, NULL);

    __u32 idx_rx = 0;
    unsigned int rcvd = xsk_ring_cons__peek(&q->rx, RX_BATCH_SIZE, &idx_rx);
    if (rcvd == 0)
        return;

    __u32 idx_fq = 0;
    while (xsk_ring_prod__reserve(&q->fq, rcvd, &idx_fq) != rcvd)
        ;

    for (unsigned int i = 0; i < rcvd; i++) {
        const struct xdp_desc *desc =
            xsk_ring_cons__rx_desc(&q->rx, idx_rx + i);
        __u64 addr = desc->addr;
        __u32 len = desc->len;
        __u64 orig = xsk_umem__extract_addr(addr);
        __u64 offset = xsk_umem__extract_offset(addr);
        void *pkt = xsk_umem__get_data(umem_buffer, orig) + offset;

        process_packet(pkt, len);

        *xsk_ring_prod__fill_addr(&q->fq, idx_fq + i) = orig;
    }

    xsk_ring_cons__release(&q->rx, rcvd);
    xsk_ring_prod__submit(&q->fq, rcvd);
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: %s <ifname_a> <ifname_b> <burn_in_us>\n",
                argv[0]);
        exit(EXIT_FAILURE);
    }
    int ret;

    pthread_t self = pthread_self();
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(HOST_CACHE_CORE, &cpuset);
    int s = pthread_setaffinity_np(self, sizeof(cpu_set_t), &cpuset);
    if (s != 0) {
        fprintf(stderr, "pthread_setaffinity_np failed: %s\n", strerror(s));
        exit(EXIT_FAILURE);
    }

    errno = 0;

    burn_in_us = strtol(argv[3], NULL, 0);
    if (errno) {
        fprintf(stderr, "usage: %s <ifname_a> <ifname_b> <burn_in_us>\n",
                argv[0]);
        exit(EXIT_FAILURE);
    }
    if (burn_in_us < 0) {
        fprintf(stderr, "burn_in_us must be positive\n");
        exit(EXIT_FAILURE);
    }

    sv_caches = ivshmem_init(MAX_STREAMS);
    if (!sv_caches) {
        fprintf(stderr, "Failed to initialize ivshmem region\n");
        exit(EXIT_FAILURE);
    }

    struct rlimit rlim = {RLIM_INFINITY, RLIM_INFINITY};
    setrlimit(RLIMIT_MEMLOCK, &rlim);

    signal(SIGINT, sig_handler);

    void *umem_buffer;
    ret = posix_memalign(&umem_buffer, getpagesize(), UMEM_SIZE);
    if (ret != 0) {
        perror("UMEM alignment failed");
        return 1;
    }

    struct xsk_queue queues[NUM_QUEUES];
    memset(queues, 0, sizeof(queues));

    queues[0].ifname = argv[1];
    queues[1].ifname = argv[2];
    for (int q = 0; q < NUM_QUEUES; q++) {
        queues[q].queue_id = 0;
        queues[q].base_frame = q * FRAMES_PER_QUEUE;
    }

    struct xsk_umem *umem;

    struct xsk_umem_config umem_cfg = {.fill_size = NUM_FRAMES,
                                       .comp_size = NUM_FRAMES,
                                       .frame_size = FRAME_SIZE,
                                       .frame_headroom =
                                           XSK_UMEM__DEFAULT_FRAME_HEADROOM,
                                       .flags = 0};

    ret = xsk_umem__create(&umem, umem_buffer, UMEM_SIZE, &queues[0].fq,
                           &queues[0].cq, &umem_cfg);
    if (ret < 0) {
        fprintf(stderr, "xsk_umem__create failed (%d)\n", ret);
        return 1;
    }

    struct xsk_socket_config xsk_cfg = {
        .rx_size = NUM_FRAMES,
        .tx_size = NUM_FRAMES,
        .xdp_flags = XDP_FLAGS_DRV_MODE,
        .bind_flags = XSK_BIND_MODE | XDP_USE_NEED_WAKEUP,
        .libxdp_flags = XSK_LIBXDP_FLAGS__INHIBIT_PROG_LOAD};

    struct nic_xdp nics[NUM_QUEUES];
    if (nic_xdp_load(&nics[0], queues[0].ifname, "xdp_dedupe_forward_xsk.o") <
        0)
        return 1;
    if (nic_xdp_load(&nics[1], queues[1].ifname, "xdp_dedupe_forward_xsk.o") <
        0)
        return 1;

    for (int q = 0; q < NUM_QUEUES; q++) {
        if (q == 0) {
            ret = xsk_socket__create(&queues[0].xsk, queues[0].ifname,
                                     queues[0].queue_id, umem, &queues[0].rx,
                                     &queues[0].tx, &xsk_cfg);
        } else {
            ret = xsk_socket__create_shared(
                &queues[q].xsk, queues[q].ifname, queues[q].queue_id, umem,
                &queues[q].rx, &queues[q].tx, &queues[q].fq, &queues[q].cq,
                &xsk_cfg);
        }
        __u32 key = queues[q].queue_id;
        int xskfd = xsk_socket__fd(queues[q].xsk);
        if (bpf_map_update_elem(nics[q].map_fd, &key, &xskfd, 0) < 0) {
            fprintf(stderr, "xsksmap update for %s: %s\n", queues[q].ifname,
                    strerror(errno));

            for (int q = 0; q < NUM_QUEUES; q++)
                nic_xdp_unload(&nics[q]);
            return 1;
        }
        if (ret < 0) {
            fprintf(stderr, "socket create for %s failed (%d)\n",
                    queues[q].ifname, ret);

            for (int q = 0; q < NUM_QUEUES; q++)
                nic_xdp_unload(&nics[q]);
            return 1;
        }

        queues[q].fd = xsk_socket__fd(queues[q].xsk);
        apply_busy_poll(queues[q].fd);
        if (fill_queue(&queues[q]) < 0) {
            for (int q = 0; q < NUM_QUEUES; q++)
                nic_xdp_unload(&nics[q]);
            return 1;
        }
    }

    while (running) {
        for (int q = 0; q < NUM_QUEUES; q++)
            handle_queue(&queues[q], umem_buffer);
    }

    for (int q = NUM_QUEUES - 1; q >= 0; q--) {
        __u32 key = queues[q].queue_id;
        bpf_map_delete_elem(nics[q].map_fd, &key);
        xsk_socket__delete(queues[q].xsk);
    }
    xsk_umem__delete(umem);
    for (int q = 0; q < NUM_QUEUES; q++)
        nic_xdp_unload(&nics[q]);
    free(umem_buffer);
    printf("%d\n", counter);
    return 0;
}
