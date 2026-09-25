#!/bin/bash

# XDP plain cleanup

PHY_1="eno1"
PHY_2="eno2"
BRIDGE="br0"

sudo tc qdisc del dev $PHY_1 clsact

sudo xdp-loader unload eno1 --all
sudo xdp-loader unload eno2 --all

sudo rm /sys/fs/bpf/epoch_map
sudo rm /sys/fs/bpf/valid_map
sudo rm /sys/fs/bpf/last_seen_map

sudo rm -r /sys/fs/bpf/xdp
sudo rm -r /sys/fs/bpf/tc

sudo ip link delete $BRIDGE

