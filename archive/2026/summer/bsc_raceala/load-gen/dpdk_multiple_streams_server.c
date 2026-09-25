#define _GNU_SOURCE
#include <err.h>
#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <net/if.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include <rte_common.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_hexdump.h>
#include <rte_mbuf.h>
#include <rte_version.h>

#include "../dpdk_common.h"
#include "dpdk_prp_utils.h"
#include "../sv_frame.h"
#include "../time_utils.h"
#include "../measurement_constants.h"

// the load-gen assumes no packet loss
// in our measurement setup, packet loss
// implies that there is something wrong
// with the code or with the test bench
// load-gen hangs === packet loss

// if set to 0, there are no PRP specific correctness checks
// if set to 1, PRP checks are on the hotpath
// all measurements in the thesis were made with correctness = 0

#define CORRECTNESS 0

// MAX_DELAY_IN_WINDOWS - size of the worst case burst
// 5 corresponds to 4 * 250 us => assumes a network with a worst-case delay of 1

#define MAX_DELAY_IN_WINDOWS 5

#define LATENCY_RESULTS_PATH "/scratch/radu/af-xdp/latency"
#define HOST_THROUGHPUT_RESULTS_PATH                                           \
    "/scratch/radu/shared-nothing/host_throughput"
#define WC_BURST_RESULTS_PATH "/scratch/radu/dpdk/wc_burst"
#define VM_THROUGHPUT_RESULTS_PATH "/scratch/radu/af-xdp/vm_throughput"

static uint64_t next = 0;

// simulating MU packet rates
void wait_period_250us(void) {
    const uint64_t period = 250000ull;

    uint64_t now = ns_now_raw();
    if (next == 0)
        next = now + period;
    else
        next += period;

    const uint64_t spin_margin = 10000ull;

    if (next > spin_margin) {
        uint64_t sleep_until = next - spin_margin;
        struct timespec ts;
        ns_to_timespec(sleep_until, &ts);
        clock_nanosleep(CLOCK_MONOTONIC_RAW, TIMER_ABSTIME, &ts, NULL);
    }

    while (ns_now_raw() < next) {
        __asm__ volatile("pause");
    }
}

static inline bool check_SV(struct rte_mbuf *m) {
    uint8_t *data = rte_pktmbuf_mtod(m, uint8_t *);
    if (data[12] != 0x88 || data[13] != 0xba) {
        return false;
    }
    return true;
}

int sample_count;
uint64_t *latencies;
struct timespec *beg;
PRP_State prp_state = {};
uint8_t sv_frame[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3c, 0xec, 0xef, 0x62, 0xac, 0x40,
    0x88, 0xba, 0x40, 0x1,  0x0,  0x66, 0x0,  0x0,  0x0,  0x0,  0x60, 0x5c,
    0x80, 0x1,  0x1,  0xa2, 0x57, 0x30, 0x55, 0x80, 0x4,  0x34, 0x30, 0x30,
    0x31, 0x82, 0x2,  0x1,  0x18, 0x83, 0x4,  0x0,  0x0,  0x0,  0x1,  0x85,
    0x1,  0x2,  0x87, 0x40, 0xff, 0xfe, 0x59, 0x82, 0x0,  0x0,  0x0,  0x0,
    0x0,  0x4,  0x3d, 0xdc, 0x0,  0x0,  0x0,  0x0,  0xff, 0xfd, 0x6f, 0x5c,
    0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x6,  0xba, 0x0,  0x0,  0x20, 0x0,
    0xff, 0x8d, 0xf4, 0x0,  0x0,  0x0,  0x0,  0x0,  0x1,  0x1d, 0xfb, 0xc2,
    0x0,  0x0,  0x0,  0x0,  0xff, 0x55, 0x60, 0xc,  0x0,  0x0,  0x0,  0x0,
    0x0,  0x1,  0x4f, 0xce, 0x0,  0x0,  0x20, 0x0,  0x0,  0x0,  0x0,  0x0,
    0x0,  0x0};

uint16_t host_throughput_points[] = {2,  3,  4,  6,  8,  11, 16,
                                     23, 32, 45, 64, 91, 128};
uint16_t vm_throughput_points[] = {1,  2,  3,  4,  6,  8,  11,
                                   16, 23, 32, 45, 64, 91, 128};

static struct rte_mempool *mbuf_pool;
static const uint16_t port_phy0 = 0;
static const uint16_t port_phy1 = 1;

static volatile bool force_quit = false;

static void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        force_quit = true;
    }
}

unsigned int process_packet(struct rte_mbuf *mbuf) {
    struct timespec end;
    struct timespec res;
    uint8_t *mbuf_data = rte_pktmbuf_mtod(mbuf, uint8_t *);
    uint16_t ether = (mbuf_data[12] << 8) + mbuf_data[13];
    if (ether != 0x88ba) {
        rte_pktmbuf_free(mbuf);
        return 0;
    }

    uint32_t id = read_id_from_frame(mbuf_data);
    if (id >= sample_count) {
        rte_pktmbuf_free(mbuf);
        force_quit = true;
        fprintf(stderr, "Ending measurement: id %u higher than sample_count\n",
                id);
        return 0;
    }

    if (!CORRECTNESS) {
        if (!latencies[id]) {
            clock_gettime(CLOCK_MONOTONIC_RAW, &end);
            timespec_subtract(&res, &end, beg + (id % 4000));
            latencies[id] = res.tv_nsec;
        }
        goto cleanup;
    }
    switch (prp_rx_check_update(mbuf_data, mbuf->pkt_len, &prp_state)) {
    case INVALID:
        rte_exit(EXIT_FAILURE, "CORRECTNESS : failed rx check update\n");
    case ACCEPT:
        clock_gettime(CLOCK_MONOTONIC_RAW, &end);
        timespec_subtract(&res, &end, beg + (id % 4000));
        latencies[id] = res.tv_nsec;
        break;
    case DROP:
        // nothing
    }

cleanup:
    rte_pktmbuf_free(mbuf);
    return 1;
}

// sending/receiving DUMMY_SIZE windows of traffic before the measurement to
// fault everything on the hotpath

void warmup() {

    struct rte_mbuf *bufs[BURST_SIZE];

    write_id_to_frame(UINT32_MAX, sv_frame);

    for (size_t i = 0; i < DUMMY_SIZE; i++) {
        for (uint8_t svid = 0; svid < prp_state.stream_count - 1; svid++) {
            write_sv_addr_to_frame(svid, sv_frame);
            if (!prp_sendto(sv_frame, sizeof(sv_frame), &prp_state, 0, BOTH))
                rte_exit(EXIT_FAILURE, "tx fail at warmup\n");
        }
        write_sv_addr_to_frame(prp_state.stream_count - 1, sv_frame);
        if (!prp_sendto(sv_frame, sizeof(sv_frame), &prp_state, 1, BOTH))
            rte_exit(EXIT_FAILURE, "tx fail at warmup\n");
        wait_period_250us();
    }

    uint32_t processed_frames = 0;
    while (processed_frames < DUMMY_SIZE * 2 && !force_quit) {
        int n = rte_eth_rx_burst(port_phy0, 0, bufs, BURST_SIZE);
        if (n > 0) {
            for (int i = 0; i < n; i++) {
                if (check_SV(bufs[i])) {
                    processed_frames += 1;
                }
                rte_pktmbuf_free(bufs[i]);
            }
        }

        n = rte_eth_rx_burst(port_phy1, 0, bufs, BURST_SIZE);
        if (n > 0) {
            for (int i = 0; i < n; i++) {
                if (check_SV(bufs[i])) {
                    processed_frames += 1;
                }
                rte_pktmbuf_free(bufs[i]);
            }
        }
    }
    rte_delay_ms(10);
}

// as with warmup(), this is strictly for measurement purposes
// it is easier to notify the host cache on the other machine that a measurement
// has ended in this way such that it could reset the pointers in IVSHMEM
// instead of modifying the different versions of the monitor
// this makes it so that multiple measurements can run without having to restart
// the VM the exact meaning of byte RESET_POS in the sv_frame is irrelevant

void reset() {
    sv_frame[RESET_POS] = RESET_VAL;
    prp_sendto(sv_frame, sizeof(sv_frame), &prp_state, 0, A);
}

int thread(void *buf) {

    (void)buf;
    struct rte_mbuf *bufs[BURST_SIZE];
    uint32_t processed_frames = 0;
    while (processed_frames < 2 * sample_count && !force_quit) {
        int n = rte_eth_rx_burst(port_phy0, 0, bufs, BURST_SIZE);
        if (n > 0) {
            for (int i = 0; i < n; i++) {
                processed_frames += process_packet(bufs[i]);
            }
        }
        n = rte_eth_rx_burst(port_phy1, 0, bufs, BURST_SIZE);
        if (n > 0) {
            for (int i = 0; i < n; i++) {
                processed_frames += process_packet(bufs[i]);
            }
        }
    }
    return 0;
}
void wc_burst_loop() {

    for (uint8_t windows = 1; windows <= MAX_DELAY_IN_WINDOWS; windows++) {

        sample_count *= windows;
        prp_state.sample_count = sample_count;

        if (rte_eal_remote_launch(thread, NULL, 9) < 0) {
            rte_exit(EXIT_FAILURE, "Could not launch receiver\n");
        }

        next = 0;
        uint32_t id = 0;

        while (id < sample_count && !force_quit) {
            for (size_t i = 0; i < windows; i++) {
                write_id_to_frame(id, sv_frame);
                clock_gettime(CLOCK_MONOTONIC_RAW, beg + (id % 4000));
                for (uint8_t svid = 0; svid < prp_state.stream_count - 1;
                     svid++) {
                    write_sv_addr_to_frame(svid, sv_frame);
                    if (!prp_sendto(sv_frame, sizeof(sv_frame), &prp_state, 0,
                                    BOTH))
                        rte_exit(EXIT_FAILURE, "tx failed\n");
                }
                write_sv_addr_to_frame(prp_state.stream_count - 1, sv_frame);
                if (!prp_sendto(sv_frame, sizeof(sv_frame), &prp_state, 1,
                                BOTH))
                    rte_exit(EXIT_FAILURE, "tx failed\n");

                id++;
            }
            wait_period_250us();
        }

        rte_eal_wait_lcore(9);

        char *path;
        asprintf(&path, "%s/latencies_%u_windows_delay", WC_BURST_RESULTS_PATH,
                 windows);
        int fd = open(path, O_CREAT | O_RDWR, S_IROTH | S_IWUSR | S_IRUSR);
        if (fd == -1) {
            rte_exit(EXIT_FAILURE,
                     "failed to open fd for dumping measurements, directories "
                     "might not be created\n");
        }
        free(path);
        for (size_t i = windows - 1; i < sample_count; i += windows)
            write(fd, latencies + i, sizeof(uint64_t));
        close(fd);
        if (CORRECTNESS)
            flush_prp_samples(&prp_state);
        memset(latencies, 0, sample_count * sizeof(uint64_t));
        sample_count /= windows;
    }
}

void host_throughput_loop() {

    for (size_t i = 0; i < sizeof(host_throughput_points) / sizeof(uint16_t);
         i++) {
        uint16_t stream_count = host_throughput_points[i];
        printf("%u\n", stream_count);

        if (rte_eal_remote_launch(thread, NULL, 9) < 0) {
            rte_exit(EXIT_FAILURE, "Could not launch receiver\n");
        }

        next = 0;
        for (uint32_t id = 0; id < sample_count; id++) {
            write_id_to_frame(id, sv_frame);
            clock_gettime(CLOCK_MONOTONIC_RAW, beg + (id % 4000));
            for (uint8_t svid = 2; svid < stream_count; svid++) {

                write_sv_addr_to_frame(svid, sv_frame);
                if (!prp_sendto(sv_frame, sizeof(sv_frame), &prp_state, 0,
                                BOTH))
                    rte_exit(EXIT_FAILURE, "tx failed\n");
            }

            write_sv_addr_to_frame(0, sv_frame);
            if (!prp_sendto(sv_frame, sizeof(sv_frame), &prp_state, 0, A))
                rte_exit(EXIT_FAILURE, "tx failed\n");

            write_sv_addr_to_frame(1, sv_frame);
            if (!prp_sendto(sv_frame, sizeof(sv_frame), &prp_state, 1, B))
                rte_exit(EXIT_FAILURE, "tx failed\n");

            if (force_quit)
                return;

            wait_period_250us();
        }

        rte_eal_wait_lcore(9);

        char *path;
        asprintf(&path, "%s/latencies_%u_stream", HOST_THROUGHPUT_RESULTS_PATH,
                 stream_count);
        int fd = open(path, O_CREAT | O_RDWR, S_IROTH | S_IWUSR | S_IRUSR);
        if (fd == -1) {
            rte_exit(EXIT_FAILURE,
                     "failed to open fd for dumping measurements, directories "
                     "might not be created\n");
        }
        free(path);
        write(fd, latencies, sample_count * 8);
        close(fd);
        if (CORRECTNESS)
            flush_prp_samples(&prp_state);
        memset(latencies, 0, sample_count * 8);
    }
}

void vm_throughput_loop() {

    for (size_t i = 0; i < sizeof(vm_throughput_points) / sizeof(uint16_t);
         i++) {
        uint16_t stream_count = vm_throughput_points[i];
        printf("%u\n", stream_count);

        if (rte_eal_remote_launch(thread, NULL, 9) < 0) {
            rte_exit(EXIT_FAILURE, "Could not launch receiver\n");
        }

        next = 0;
        for (uint32_t id = 0; id < sample_count; id++) {
            write_id_to_frame(id, sv_frame);
            clock_gettime(CLOCK_MONOTONIC_RAW, beg + (id % 4000));
            for (uint8_t svid = 0; svid < stream_count - 1; svid++) {

                write_sv_addr_to_frame(svid, sv_frame);
                if (!prp_sendto(sv_frame, sizeof(sv_frame), &prp_state, 0,
                                BOTH))
                    rte_exit(EXIT_FAILURE, "tx failed\n");
            }

            write_sv_addr_to_frame(stream_count - 1, sv_frame);
            if (!prp_sendto(sv_frame, sizeof(sv_frame), &prp_state, 1, BOTH))
                rte_exit(EXIT_FAILURE, "tx failed\n");
            if (force_quit)
                return;

            wait_period_250us();
        }

        rte_eal_wait_lcore(9);

        char *path;
        asprintf(&path, "%s/latencies_%u_stream", VM_THROUGHPUT_RESULTS_PATH,
                 stream_count);
        int fd = open(path, O_CREAT | O_RDWR, S_IROTH | S_IWUSR | S_IRUSR);
        if (fd == -1) {
            rte_exit(EXIT_FAILURE,
                     "failed to open fd for dumping measurements, directories "
                     "might not be created\n");
        }
        free(path);
        write(fd, latencies, sample_count * 8);
        close(fd);
        if (CORRECTNESS)
            flush_prp_samples(&prp_state);
        memset(latencies, 0, sample_count * 8);
    }
}

void latency_loop() {

    if (rte_eal_remote_launch(thread, NULL, 9) < 0) {
        rte_exit(EXIT_FAILURE, "Could not launch receiver\n");
    }

    next = 0;

    for (uint32_t id = 0; id < sample_count; id++) {
        write_id_to_frame(id, sv_frame);
        clock_gettime(CLOCK_MONOTONIC_RAW, beg + (id % 4000));
        for (uint8_t svid = 0; svid < prp_state.stream_count - 1; svid++) {
            write_sv_addr_to_frame(svid, sv_frame);
            if (!prp_sendto(sv_frame, sizeof(sv_frame), &prp_state, 0, BOTH))
                rte_exit(EXIT_FAILURE, "tx failed\n");
        }
        write_sv_addr_to_frame(prp_state.stream_count - 1, sv_frame);
        if (!prp_sendto(sv_frame, sizeof(sv_frame), &prp_state, 1, BOTH))
            rte_exit(EXIT_FAILURE, "tx failed\n");
        wait_period_250us();
        if (force_quit) {
            break;
        }
    }

    rte_eal_wait_lcore(9);

    char *path;
    asprintf(&path, "%s/latencies", LATENCY_RESULTS_PATH);
    int fd = open(path, O_CREAT | O_RDWR, S_IROTH | S_IWUSR | S_IRUSR);
    if (fd == -1) {
        rte_exit(EXIT_FAILURE, "failed to open fd for dumping measurements, "
                               "directories might not be created\n");
    }
    free(path);
    write(fd, latencies, sample_count * 8);
    close(fd);
}

// args: <mode> <sample_count> <stream_count>
// mode = 0 => latency measurement
// mode = 1 => host_throughput measurement
// mode = 2 => worst-case burst measurement
// mode = 3 => vm_throughput measurement
// stream_count is ignored for modes 1 and 3
int main(int argc, char *argv[]) {

    if (mlockall(MCL_CURRENT | MCL_FUTURE)) {
        perror("mlock failed");
        exit(EXIT_FAILURE);
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    int ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "EAL init failed\n");
    argc -= ret;
    argv += ret;

    if (argc < 4) {
        goto wrong_args;
    }

    mbuf_pool = rte_pktmbuf_pool_create(
        "MBUF_POOL", NUM_MBUFS, MBUF_CACHE_SIZE, 0,
        RTE_MBUF_DEFAULT_BUF_SIZE + MBUF_TAILROOM, rte_socket_id());
    if (!mbuf_pool)
        rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");
    if (port_init(port_phy0, mbuf_pool) != 0)
        rte_exit(EXIT_FAILURE, "Cannot init port 0\n");
    if (port_init(port_phy1, mbuf_pool) != 0)
        rte_exit(EXIT_FAILURE, "Cannot init port 1\n");

    errno = 0;
    int mode = strtol(argv[1], NULL, 0);
    if (errno) {
        goto wrong_args;
    }

    sample_count = strtol(argv[2], NULL, 0);
    if (errno) {
        goto wrong_args;
    }
    if (sample_count < 0) {
        rte_exit(EXIT_FAILURE, "The sample count must be positive\n");
    }

    int stream_count = strtol(argv[3], NULL, 0);
    if (errno) {
        goto wrong_args;
    }
    if (stream_count < 1 || stream_count > MAX_STREAMS) {
        rte_exit(EXIT_FAILURE, "stream count must be between 1 and 128\n");
    }

    // actually allocates 1GB either way
    latencies = (uint64_t *)mmap(
        NULL, sample_count * 8 * MAX_DELAY_IN_WINDOWS, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_HUGE_1GB, -1, 0);
    if (latencies == MAP_FAILED) {
        rte_exit(EXIT_FAILURE,
                 "hugetlb mmap failed, load_gen is expecting 1GB hugepages \n");
    }
    memset(latencies, 0, sample_count * 8);

    beg = malloc(4000 * sizeof(struct timespec));
    if (!beg) {
        goto alloc_failed;
    }
    memset(beg, 0, sizeof(struct timespec) * 4000);

    if (CORRECTNESS) {
        prp_state.sample_count = sample_count;
        prp_state.prp_samples =
            calloc(sample_count * MAX_DELAY_IN_WINDOWS, sizeof(PRP_Sample));
        if (!prp_state.prp_samples) {
            goto alloc_failed;
        }
        memset(prp_state.prp_samples, 0,
               sample_count * MAX_DELAY_IN_WINDOWS * sizeof(PRP_Sample));
    }
    prp_state.stream_count = stream_count;
    prp_state.seq_tx = 0;

    prp_state.mbuf_pool = mbuf_pool;
    prp_state.port_phy0 = port_phy0;
    prp_state.port_phy1 = port_phy1;

    struct rte_eth_link link;

    for (int i = 0; i < 20; i++) {
        rte_eth_link_get_nowait(port_phy1, &link);
        if (link.link_status == RTE_ETH_LINK_UP)
            break;
        rte_delay_ms(500);
    }

    for (int i = 0; i < 20; i++) {
        rte_eth_link_get_nowait(port_phy0, &link);
        if (link.link_status == RTE_ETH_LINK_UP)
            break;
        rte_delay_ms(500);
    }
    rte_delay_ms(2000);
    struct rte_eth_link link0, link1;
    rte_eth_link_get_nowait(port_phy0, &link0);
    rte_eth_link_get_nowait(port_phy1, &link1);

    printf("port_phy0 (%u): %s, speed %u Mbps, %s\n", port_phy0,
           link0.link_status ? "UP" : "DOWN", link0.link_speed,
           link0.link_duplex == RTE_ETH_LINK_FULL_DUPLEX ? "full" : "half");
    printf("port_phy1 (%u): %s, speed %u Mbps, %s\n", port_phy1,
           link1.link_status ? "UP" : "DOWN", link1.link_speed,
           link1.link_duplex == RTE_ETH_LINK_FULL_DUPLEX ? "full" : "half");

    if (mode == 1) {
        stream_count = host_throughput_points[0];
        prp_state.stream_count = host_throughput_points[0];
    }

    if (mode == 3) {
        stream_count = vm_throughput_points[0];
        prp_state.stream_count = vm_throughput_points[0];
    }

    warmup();

    switch (mode) {

    case 0:
        latency_loop();
        break;

    case 1:
        host_throughput_loop();
        break;

    case 2:
        wc_burst_loop();
        break;

    case 3:
        vm_throughput_loop();
        break;

    default:
        reset();
        rte_exit(EXIT_FAILURE,
                 "invalid mode: 0 - latency, 1 - host_throughput, 2 - "
                 "worst-case burst, 3 - vm_throughput\n");
    }

    reset();

    rte_eth_dev_stop(port_phy0);
    rte_eth_dev_stop(port_phy1);
    rte_eth_dev_close(port_phy0);
    rte_eth_dev_close(port_phy1);
    rte_eal_cleanup();

    exit(EXIT_SUCCESS);

wrong_args:
    rte_exit(EXIT_FAILURE,
             "args: EAL args + <mode> <sample_count> <stream_count>\n");
alloc_failed:
    rte_exit(EXIT_FAILURE, "Failed to allocate memory\n");
}
