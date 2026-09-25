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
#include <signal.h>

#include "../time_utils.h"

// histogram IPC protocol cost estimation
// ALWAYS run as sudo chrt -f 30 ./2_proc_efd_histo > file
// otherwise EEVDF does things we do not want
// also disable RT throttling beforehand
// with sudo sysctl -w kernel.sched_rt_runtime_us=-1

int main() {

    struct timespec beg, end, res;

    uint32_t histo[40000] = {};

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

        for (int i = 0; i < 100002; i++) {
            read(efd_parent, &u_child, sizeof(uint64_t));
            write(efd_child, &u_child, sizeof(uint64_t));
        }
        exit(EXIT_SUCCESS);
    case -1:
        exit(EXIT_FAILURE);

    default:
        // parent

        uint64_t u_parent = 1;

        write(efd_parent, &u_parent, sizeof(uint64_t));
        read(efd_child, &u_parent, sizeof(uint64_t));

        for (int i = 0; i < 100000; i++) {
            clock_gettime(CLOCK_MONOTONIC, &beg);
            write(efd_parent, &u_parent, sizeof(uint64_t));
            read(efd_child, &u_parent, sizeof(uint64_t));
            clock_gettime(CLOCK_MONOTONIC, &end);
            timespec_subtract(&res, &end, &beg);
            if (res.tv_nsec >= 100000) {
                fprintf(stderr,
                        "huge outlier %lu pos %d\n, should not happen, abort "
                        "measurement\n",
                        res.tv_nsec, i);
                kill(pid, SIGKILL);
                exit(EXIT_FAILURE);
            }
            histo[res.tv_nsec / 25]++;
        }

        // redirect to file
        for (size_t i = 0; i < 40000; i++) {
            printf("%u\n", histo[i]);
        }

        kill(pid, SIGKILL);
        exit(EXIT_SUCCESS);
    }
}
