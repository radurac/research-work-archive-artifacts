# Virtualized Protection and Control Network Stack Evaluation

Three network stacks that deliver IEC 61850 Sampled Values over PRP into a VM running protection algorithms 
(VPACs), plus the load generator and microbenchmarks used to evaluate them.


## Layout

| Directory | Runs on | What it is |
| --- | --- | --- |
| `load-gen/` | traffic generator | DPDK generator simulating merging units on two PRP LANs, also measures round-trip latency |
| `dpdk-host-cache/` | host | DPDK host cache: PRP dedup in userspace, processed values into IVSHMEM, vhost-user from the VM |
| `af_xdp-host-cache/` | host | AF_XDP host cache: PRP dedup in eBPF (XDP on ingress, TC on egress), processed values into IVSHMEM, vhost-net from the VM |
| `shared-nothing-vm/` | guest | baseline client, receives raw frames over `AF_PACKET` sockets |
| `host-cache-vm/` | guest | IVSHMEM clients: no-monitor setup (one VPAC), unoptimized and optimized monitor setup (multiple VPACs)  |
| `context_switch_estimation/` | anywhere | eventfd ping-pong microbenchmarks |
| `setup_scripts/` | host | NIC, bridge, PRP and eBPF configuration |
| `vm_xmls/` | host | libvirt domain definitions for the two VM configurations |

## Build

Targets are per-machine, so there is no `make all`:

```
make loadgen     # traffic generator          needs DPDK
make host-dpdk   # host, DPDK variant         needs DPDK
make host-xdp    # host, AF_XDP variant       needs libxdp + libbpf + clang
make vm          # the four guest clients     no external dependencies
make tools       # dedupe_test + microbenchmarks
make check       # run the dedup regression tests
```

## Running

Create the output directories first, the binaries abort if they are missing,
and the paths are relative to the working directory.

```
make ivshmem     # host only: create /dev/shm/ivshmem at the right size
```

Then, on the host, one of:

```
setup_scripts/shared_nothing_setup.sh
setup_scripts/af_xdp_setup.sh          # cleanup: af_xdp_setup_cleanup.sh
```
The DPDK variant doesn't need a special setup script.

Arguments:

```
load-gen/dpdk_multiple_streams_server  <EAL args> -- <mode> <sample_count> <stream_count>
    mode 0 = latency, 1 = host throughput sweep, 2 = worst-case burst, 3 = vm throughput sweep

dpdk-host-cache/dpdk_host_cache        <EAL args> -- <burn_in_us>
af_xdp-host-cache/af_xdp_host_cache    <ifname_a> <ifname_b> <burn_in_us>

host-cache-vm/client_ivshmem           <mode> <sample_count> [<stream_count> <burn_in_us>]
host-cache-vm/client_ivshmem_efd_sync[_opt]  <vpac_count> <stream_count> <sample_count> <burn_in_us>
shared-nothing-vm/client_multiple_streams    <stream_count> <sample_count> <burn_in_us>
```

Run the microbenchmarks and the monitor host-cache client processes with the FIFO real-time scheduler
( before measurements `sysctl kernel.sched_rt_runtime_us=-1` and `chrt -f 30` when starting them).

