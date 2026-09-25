#ifndef SV_FRAME_H
#define SV_FRAME_H

#include <stdint.h>
#include <string.h>

static const uint8_t id_pos = 33;

static const unsigned char base_multicast_sv[6] = {0x01, 0x0c, 0xcd,
                                                   0x04, 0x00, 0x00};

// The "id" has no meaning under SV
// It was a sample counter used for my measurements
// the positions in the frame are also not relevant
static inline uint32_t read_id_from_frame(unsigned char *sv_frame) {
    return (sv_frame[33] << 24) + (sv_frame[34] << 16) + (sv_frame[35] << 8) +
           sv_frame[36];
}

static inline void write_id_to_frame(uint32_t id, unsigned char *sv_frame) {
    sv_frame[33] = id >> 24;
    sv_frame[34] = (id >> 16) & 255;
    sv_frame[35] = (id >> 8) & 255;
    sv_frame[36] = id & 255;
}

// This also changes the source MAC address to force
// the PRP kernel module to allocate structures for each stream
static inline void write_sv_addr_to_frame(uint8_t svid,
                                          unsigned char *sv_frame) {
    memcpy(sv_frame, base_multicast_sv, 6);
    sv_frame[5] = svid;
    sv_frame[11] = svid;
}

static inline void copy_id_to_frame(unsigned char id[4],
                                    unsigned char sv_frame[]) {
    sv_frame[33] = id[0];
    sv_frame[34] = id[1];
    sv_frame[35] = id[2];
    sv_frame[36] = id[3];
}

static inline uint32_t read_id_bytes(unsigned char byte_id[4]) {
    uint32_t id = (byte_id[0] << 24) + (byte_id[1] << 16) + (byte_id[2] << 8) +
                  byte_id[3];
    return id;
}

#endif
