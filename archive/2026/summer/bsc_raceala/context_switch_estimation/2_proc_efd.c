#define _GNU_SOURCE
#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <string.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/eventfd.h>

#include "../time_utils.h"

// average IPC protocol cost estimation
// ALWAYS run as sudo chrt -f 30 ./2_proc_efd
// otherwise EEVDF does things we do not want
// also disable RT throttling beforehand
// with sudo sysctl -w kernel.sched_rt_runtime_us=-1

int main() {

    struct timespec beg, end, res;

    pthread_t test = pthread_self();
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(1, &cpuset);
    int s = pthread_setaffinity_np(test, sizeof(cpu_set_t), &cpuset);
    if (s != 0) {
        fprintf(stderr, "pthread_setaffinity_np failed: %s\n", strerror(s));
        exit(EXIT_FAILURE);
    }

    int efd_child = eventfd(0, 0);
    int efd_parent = eventfd(0, 0);

    pid_t pid = fork();
    switch (pid) {
    case 0:
        // child

        uint64_t u_child = 1;

        for (int i = 0; i < 10000; i++) {
            read(efd_parent, &u_child, sizeof(uint64_t));
            write(efd_child, &u_child, sizeof(uint64_t));
        }
        exit(0);
    case -1:
        exit(1);

    default:
        // parent

        uint64_t u_parent = 1;

        clock_gettime(CLOCK_MONOTONIC, &beg);

        for (int i = 0; i < 10000; i++) {
            write(efd_parent, &u_parent, sizeof(uint64_t));
            read(efd_child, &u_parent, sizeof(uint64_t));
        }

        clock_gettime(CLOCK_MONOTONIC, &end);
        timespec_subtract(&res, &end, &beg);

        printf("seconds: %lu nanoseconds: %lu\n", res.tv_sec, res.tv_nsec);

        wait(NULL);
        exit(0);
    }
}
