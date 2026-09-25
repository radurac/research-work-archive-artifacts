#!/bin/bash

# Shared-Nothing only
# configures the bridge to redirect all streams to VM 0 
# note that the VM must be started, QEMU creates the tap device at runtime
# the assumption is that the bridge has already been created
# by the setup script

BRIDGE="br0"
TAP0="tap0"

sudo bridge mdb flush dev $BRIDGE

sudo bridge link set dev $TAP0 mcast_flood off

sudo bridge link set dev $TAP0 learning off

for i in {0..255}; do
    HEX_SUFFIX=$(printf "%02x" $i)
    MAC_ADDR="01:0c:cd:04:00:${HEX_SUFFIX}"
    
    sudo bridge mdb add dev $BRIDGE port $TAP0 grp $MAC_ADDR permanent
done

bridge mdb show dev $BRIDGE
