#!/bin/bash

# XDP plain
# note that the script does not pin IRQs
# for the throughput measurement one measurement is obtained
# by pinning them to one core 
# the other one by pinning them to separate cores

PHY_1="eno1"
PHY_2="eno2"
BRIDGE="br0"
TC_ENCAPS_PATH="../af_xdp-host-cache/tc_encaps.o"
XDP_DEDUPE_PATH="../af_xdp-host-cache/xdp_dedupe_forward.o"


sudo ip link add name $BRIDGE type bridge
sudo ip link set $PHY_1 master $BRIDGE
sudo ip link set $PHY_2 master $BRIDGE
sudo ip link set dev $BRIDGE up

# sets the IRQ counts to 1
# basically disabling RSS
sudo ethtool -L $PHY_1 combined 1
sudo ethtool -L $PHY_2 combined 1

# disabling adaptive ITR
sudo ethtool -C $PHY_1 rx-usecs 0
sudo ethtool -C $PHY_2 rx-usecs 0

# load the xdp program (ingress)
sudo xdp-loader load $PHY_1 $XDP_DEDUPE_PATH --pin-path /sys/fs/bpf
sudo xdp-loader load $PHY_2 $XDP_DEDUPE_PATH --pin-path /sys/fs/bpf


# load the tc program (egress)
sudo tc qdisc add dev $PHY_1 clsact
sudo tc filter add dev $PHY_1 egress bpf da obj $TC_ENCAPS_PATH sec tc

# isolate the interfaces
# bridge only forwards frame to eno1

sudo bridge link set dev $PHY_1 isolated on
sudo bridge link set dev $PHY_2 isolated on

sudo bridge link set dev $PHY_1 learning off
sudo bridge link set dev $PHY_2 learning off

sudo bridge fdb flush dev $PHY_1 master
sudo bridge fdb flush dev $PHY_2 master

sudo bridge fdb add 3c:ec:ef:62:ac:40 dev $PHY_1 master static
