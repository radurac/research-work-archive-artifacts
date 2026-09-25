#ifndef VM_MEASURE_H
#define VM_MEASURE_H

#include "../time_utils.h"

// both of these are in nanoseconds
// MAX_VALUE % BIN_WIDTH assumed to be 0
#define BIN_WIDTH 25
#define MAX_VALUE 500000

#define SEND_HISTO_PATH "histograms"
#define PROCESSING_HISTO_PATH "histograms"

// 20000 entries for each with a BIN_WIDTH of 25 nanoseconds and a MAX_VALUE of
// 500 us
static uint32_t send_histo[MAX_VALUE / BIN_WIDTH];
static uint32_t processing_histo[MAX_VALUE / BIN_WIDTH];

static struct timespec send_beg;
static struct timespec processing_beg;

static uint32_t processing_acc;
static uint16_t processing_count;

void begin_send() {
    clock_gettime(CLOCK_MONOTONIC, &send_beg);
}

void end_send() {
    struct timespec send_end, send_res;

    clock_gettime(CLOCK_MONOTONIC, &send_end);
    timespec_subtract(&send_res, &send_end, &send_beg);

    if (send_res.tv_nsec >= MAX_VALUE) {
        fprintf(stderr,
                "Histogram is not wide enough, send outlier %ld spotted. "
                "Something is probably off in the setup.\n",
                send_res.tv_nsec);
        return;
    }
    send_histo[send_res.tv_nsec / BIN_WIDTH]++;
}

void begin_processing() {
    clock_gettime(CLOCK_MONOTONIC, &processing_beg);
    processing_count++;
}

void end_processing(uint16_t stream_count) {
    struct timespec processing_end, processing_res;

    clock_gettime(CLOCK_MONOTONIC, &processing_end);
    timespec_subtract(&processing_res, &processing_end, &processing_beg);
    processing_acc += processing_res.tv_nsec;

    if (processing_count == stream_count) {
        if (processing_acc >= MAX_VALUE) {
            fprintf(
                stderr,
                "Histogram is not wide enough, processing outlier %d spotted",
                processing_acc);
            return;
        }
        processing_histo[processing_acc / BIN_WIDTH]++;
        processing_acc = 0;
        processing_count = 0;
    }
}

int dump_send_histo() {

    char *path;
    asprintf(&path, "%s/send_histo.txt", SEND_HISTO_PATH);

    FILE *file = fopen(path, "w");
    if (file == NULL) {
        free(path);
        return 1;
    }
    for (size_t i = 0; i < MAX_VALUE / BIN_WIDTH; i++) {
        fprintf(file, "%u\n", send_histo[i]);
    }

    fclose(file);
    free(path);

    return 0;
}

int dump_processing_histo(int stream_count) {
    char *path;
    asprintf(&path, "%s/processing_histo_%d_streams.txt", PROCESSING_HISTO_PATH,
             stream_count);

    FILE *file = fopen(path, "w");
    if (file == NULL) {
        free(path);
        return 1;
    }
    for (size_t i = 0; i < MAX_VALUE / BIN_WIDTH; i++) {
        fprintf(file, "%u\n", processing_histo[i]);
    }

    fclose(file);
    memset(processing_histo, 0, sizeof(processing_histo));
    free(path);

    return 0;
}

#endif
