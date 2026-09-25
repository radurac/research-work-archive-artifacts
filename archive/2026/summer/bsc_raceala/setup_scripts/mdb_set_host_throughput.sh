#!/bin/bash

BRIDGE="br0"
TAP0="tap0"
TAP1="tap1"

# Shared-Nothing only
# for the host throughput measurement
# configures the bridge to redirect the first two streams to VM 0
# and the other streams to VM 1
# note that both VMs must be started, QEMU creates the tap device at runtime
# the assumption is that the bridge has already been created
# by the setup script


sudo bridge mdb flush dev $BRIDGE

sudo bridge link set dev $TAP0 mcast_flood off
sudo bridge link set dev $TAP1 mcast_flood off

sudo bridge link set dev $TAP0 learning off
sudo bridge link set dev $TAP1 learning off


sudo bridge mdb add dev $BRIDGE port $TAP0 grp 01:0c:cd:04:00:00 permanent
sudo bridge mdb add dev $BRIDGE port $TAP0 grp 01:0c:cd:04:00:01 permanent

for i in {2..255}; do
    HEX_SUFFIX=$(printf "%02x" $i)
    MAC_ADDR="01:0c:cd:04:00:${HEX_SUFFIX}"
    
    sudo bridge mdb add dev $BRIDGE port $TAP1 grp $MAC_ADDR permanent
done

bridge mdb show dev $BRIDGE
