#ifndef NET_UTILS_H
#define NET_UTILS_H

#include <stdint.h>
#include <stdio.h>

// mac parsing

static inline int parse_mac(char *str, unsigned char *sll_addr) {
    unsigned int mac[6];
    if (sscanf(str, "%x:%x:%x:%x:%x:%x", &mac[0], &mac[1], &mac[2], &mac[3],
               &mac[4], &mac[5]) == 6 &&
        mac[0] < 256 && mac[1] < 256 && mac[2] < 256 && mac[3] < 256 &&
        mac[4] < 256 && mac[5] < 256) {
        for (int i = 0; i < 6; i++)
            sll_addr[i] = (unsigned char)mac[i];
        return 0;
    }
    return -1;
}

static inline void mac_addr_a2n(uint8_t *mac_addr, char *str) {
    int i;
    for (i = 0; i < 6; i++) {
        unsigned int val;
        sscanf(str + 3 * i, "%2x", &val);
        mac_addr[i] = (uint8_t)val;
    }
}

#endif
