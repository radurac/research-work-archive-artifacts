#!/bin/bash

# AF_XDP Host-Cache
# pins the BPF maps, creates the qdisc
# attaches the TC encapsulation program to one single interface
# creates the bridge and enslaves that same interface to it
# note that the script does not pin IRQs
# they should be pinned to the core with the AF_XDP app 

PHY_1="eno1"
PHY_2="eno2"
BRIDGE="br0"
TC_ENCAPS_PATH="../af_xdp-host-cache/tc_encaps.o"
XDP_DEDUPE_PATH="../af_xdp-host-cache/xdp_dedupe_forward_xsk.o"


sudo mkdir /sys/fs/bpf/progs
sudo bpftool prog loadall $XDP_DEDUPE_PATH /sys/fs/bpf/progs pinmaps /sys/fs/bpf
sudo rm -r /sys/fs/bpf/progs

sudo tc qdisc add dev $PHY_1 clsact
sudo tc filter add dev $PHY_1 egress bpf da obj $TC_ENCAPS_PATH sec tc

sudo ip link add name $BRIDGE type bridge
sudo ip link set $PHY_1 master $BRIDGE
sudo ip link set dev $BRIDGE up


# sets the IRQ counts to 1
# basically disabling RSS
sudo ethtool -L $PHY_1 combined 1
sudo ethtool -L $PHY_2 combined 1

# disabling adaptive ITR
sudo ethtool -C $PHY_1 rx-usecs 0
sudo ethtool -C $PHY_2 rx-usecs 0

# sets the values of the knobs 
echo 2147483647 | sudo tee /sys/class/net/$PHY_1/napi_defer_hard_irqs
echo 2147483647 | sudo tee /sys/class/net/$PHY_2/napi_defer_hard_irqs

echo 5000 | sudo tee /sys/class/net/$PHY_1/gro_flush_timeout
echo 5000 | sudo tee /sys/class/net/$PHY_2/gro_flush_timeout
