#!/bin/bash

# Shared-Nothing
# loads the PRP kernel module, places it on top of the two physical interfaces
# creates the bridge and enslaves the PRP interface to the bridge
# note that the script does not pin IRQs
# for the RTT measurement they have to be pinned to the same core
# for the throughput measurement one measurement is obtained
# by pinning them to one core 
# the other one by pinning them to separate cores

PHY_1="eno1"
PHY_2="eno2"
PRP="prp0"
BRIDGE="br0"

sudo modprobe hsr
sudo ip link add name $PRP type hsr slave1 $PHY_1 slave2 $PHY_2 supervision 45 proto 1
sudo ip link set dev $PRP up

sudo ip link add name $BRIDGE type bridge
sudo ip link set $PRP master $BRIDGE
sudo ip link set dev $BRIDGE up

# sets the IRQ counts to 1
# basically disabling RSS
sudo ethtool -L $PHY_1 combined 1
sudo ethtool -L $PHY_2 combined 1

# disabling adaptive ITR
sudo ethtool -C $PHY_1 rx-usecs 0
sudo ethtool -C $PHY_2 rx-usecs 0
