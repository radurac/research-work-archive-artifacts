#ifndef DPDK_COMMON_H
#define DPDK_COMMON_H

#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>

// Contains some constants and the port_init() function
// That will be used in the DPDK Host-Cache and in the
// DPDK Load-Gen

#define RX_RING_SIZE 2048
#define TX_RING_SIZE 2048
#define NUM_MBUFS 32767
#define MBUF_CACHE_SIZE 512
#define BURST_SIZE 32
#define MBUF_TAILROOM 6

static inline int port_init(uint16_t port, struct rte_mempool *mbuf_pool) {
    struct rte_eth_conf port_conf = {
        .rxmode = {.max_lro_pkt_size = RTE_ETHER_MAX_LEN},
    };
    int retval;

    if (!rte_eth_dev_is_valid_port(port))
        return -1;

    retval = rte_eth_dev_configure(port, 1, 1, &port_conf);
    if (retval != 0)
        return retval;

    retval = rte_eth_rx_queue_setup(
        port, 0, RX_RING_SIZE, rte_eth_dev_socket_id(port), NULL, mbuf_pool);
    if (retval < 0)
        return retval;

    retval = rte_eth_tx_queue_setup(port, 0, TX_RING_SIZE,
                                    rte_eth_dev_socket_id(port), NULL);
    if (retval < 0)
        return retval;

    retval = rte_eth_dev_start(port);
    if (retval < 0)
        return retval;

    // the destination MAC of the VPAC response
    // can be any address whatsoever
    rte_eth_promiscuous_enable(port);
    return 0;
}

#endif
