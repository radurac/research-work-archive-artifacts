#!/bin/bash

# AF_XDP cleanup

PHY_1="eno1"
PHY_2="eno2"
BRIDGE="br0"

sudo tc qdisc del dev $PHY_1 clsact

sudo rm /sys/fs/bpf/epoch_map
sudo rm /sys/fs/bpf/valid_map
sudo rm /sys/fs/bpf/last_seen_map
sudo rm /sys/fs/bpf/xsks_map

sudo rm -r /sys/fs/bpf/xdp
sudo rm -r /sys/fs/bpf/tc

sudo ip link delete $BRIDGE


echo 0 | sudo tee /sys/class/net/$PHY_1/napi_defer_hard_irqs
echo 0 | sudo tee /sys/class/net/$PHY_2/napi_defer_hard_irqs

echo 0 | sudo tee /sys/class/net/$PHY_1/gro_flush_timeout
echo 0 | sudo tee /sys/class/net/$PHY_2/gro_flush_timeout

