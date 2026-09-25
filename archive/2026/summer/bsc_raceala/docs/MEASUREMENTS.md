# Measurement protocol


Three machines are involved: the **load-gen machine** (christina), the **DuT host** (river-host), and the
**DuT VM** (river-vm). 

---

## 1. General setup

### 1.1 Hosts (`river`, `christina`)

Both machines run NixOS, their configurations are in `docs/nix/`. The `river.nix` file 
corresponds to the non-DPDK variants, minimal modifications have to be made for the
DPDK-variant. In summary: SMT disabled, cores 8–11 isolated (`isolcpus`, `nohz_full`, `rcu_nocbs`) with
all IRQs and kernel threads pinned to cores 0–7, C-states disabled,
performance governor, 20 × 1 GiB hugepages. 

For most Linux distributions, disabling `irqbalance` explicitly is also
important.

`intel_idle.max_cstate=0` disables the `intel_idle` driver, so the kernel falls
back to `acpi_idle`. Access to ACPI is needed to disable C1 and C2,
and we get that access through `cpupower`:

```bash
sudo cpupower idle-set -d 2   
sudo cpupower idle-set -d 1  
```


### 1.2 DuT VM (Ubuntu)

In `/etc/default/grub`:

```
GRUB_CMDLINE_LINUX_DEFAULT="console=ttyS0 isolcpus=1 nohz_full=1 rcu_nocbs=1 \
    kthread_cpus=0 irqaffinity=0 nomodeset video=off virtio_net.napi_tx=0"
GRUB_TERMINAL="serial console"
```

Add to both `/etc/systemd/system.conf` and `/etc/systemd/user.conf`:

```
CPUAffinity=0
```

Core 1 is the protection core and core 0 is the housekeeping core.

---

## 2. Per-stack setup

Do this once per stack, before any of the measurements in §3.

### 2.1 Shared-Nothing

```bash
sudo ./shared_nothing_setup.sh
```
Define the VM using the template provided in `vm_xmls/ubuntu_vhost_net.xml`.


Pin the two receive IRQs to a housekeeping core:

```bash
echo $PACKET_PROC_CORE | sudo tee /proc/irq/$PHY_1_RX_IRQ/smp_affinity_list
echo $PACKET_PROC_CORE | sudo tee /proc/irq/$PHY_2_RX_IRQ/smp_affinity_list
```

In the VM, pin the receive IRQ to the protection core (core 1 in our setup):

```bash
echo $PROTECTION_CORE | sudo tee /proc/irq/$VIRT_RX_IRQ/smp_affinity_list
```

Cleanup, delete the bridge and the PRP interface:

```bash
sudo ip link delete $BRIDGE
sudo ip link delete $PRP_INTERFACE
```

> **Note.** `$PACKET_PROC_CORE` does not have to be isolated, the one important
> thing is that no other IRQs run there.

### 2.2 AF_XDP

```bash
sudo ./af_xdp_setup.sh
```

Define the VM using the template provided in `vm_xmls/ubuntu_vhost_net.xml`.

After starting the host-cache, pin the two receive IRQs to the core that runs
the host-cache (core 9 in our setup):

```bash
echo $HOST_CACHE_CORE | sudo tee /proc/irq/$PHY_1_RX_IRQ/smp_affinity_list
echo $HOST_CACHE_CORE | sudo tee /proc/irq/$PHY_2_RX_IRQ/smp_affinity_list
```

In the VM:

```bash
sysctl kernel.sched_rt_runtime_us=-1
```

Cleanup:

```bash
sudo ./af_xdp_cleanup.sh
```

> **Note.** Pin the IRQs after starting the host-cache. We first attach XDP in
> the host-cache code and it might reset the affinities.

### 2.3 DPDK

Define the VM using the template provided in `vm_xmls/ubuntu_vhost_user.xml`.

In the VM:

```bash
sysctl kernel.sched_rt_runtime_us=-1
```

### 2.4 XDP Plain

```bash
sudo ./xdp_plain_setup.sh
```
Define the VM using the template provided in `vm_xmls/ubuntu_vhost_net.xml`.


Pin the two receive IRQs to a housekeeping core:

```bash
echo $PACKET_PROC_CORE | sudo tee /proc/irq/$PHY_1_RX_IRQ/smp_affinity_list
echo $PACKET_PROC_CORE | sudo tee /proc/irq/$PHY_2_RX_IRQ/smp_affinity_list
```

In the VM, pin the receive IRQ to the protection core (core 1 in our setup):

```bash
echo $PROTECTION_CORE | sudo tee /proc/irq/$VIRT_RX_IRQ/smp_affinity_list
```

Cleanup:

```bash
sudo ./xdp_plain_setup_cleanup.sh
```

---

## 3. Measurements

The EAL arguments for the load generator are the same throughout:

```
-l 8,9 -n 4 --socket-mem=1024 -a 0000:01:00.0 -a 0000:01:00.1
```

abbreviated below as `$EAL`. The three arguments after it are
`<mode> <sample_count> <stream_count>`, with

| mode | loop |
| --- | --- |
| 0 | latency |
| 1 | host throughput sweep |
| 2 | worst-case burst |
| 3 | VM throughput sweep |

The processes should be started in the order of the listing.


### 3.1 RTT

#### Shared-Nothing

```bash
sudo ./mdb_set_latency.sh
```

| Machine | Command |
| --- | --- |
| DuT VM | `sudo ./client_multiple_streams 1 10000000 0` |
| load-gen | `sudo ./dpdk_multiple_streams_server $EAL 0 10000000 1` |

#### AF_XDP

| Machine | Command |
| --- | --- |
| DuT host | `sudo ./af_xdp_host_cache eno1 eno2 0` |
| DuT host | pin IRQs as described in §2.2 |
| DuT VM | `sudo chrt -f 30 ./client_ivshmem_efd_sync 1 1 10000000 0` |
| load-gen | `sudo ./dpdk_multiple_streams_server $EAL 0 10000000 1` |

#### DPDK

| Machine | Command |
| --- | --- |
| DuT host | `sudo ./dpdk_host_cache -l 9 -n 4 --socket-mem=1024 -a 0000:01:00.0 -a 0000:01:00.1 0` |
| DuT VM | `sudo chrt -f 30 ./client_ivshmem_efd_sync 1 1 10000000 0` |
| load-gen | `sudo ./dpdk_multiple_streams_server $EAL 0 10000000 1` |

### 3.2 Host-side CPU usage

#### Shared-Nothing

- for the one-core setup pin the receive IRQs to the same core as described in §2.4
- for the two-core setup pin the receive IRQs to different cores

```bash
sudo ./mdb_set_host_throughput.sh
```

| Machine | Command |
| --- | --- |
| DuT VM | `sudo ./client_multiple_streams 2 1300000 0` |
| load-gen | `sudo ./dpdk_multiple_streams_server $EAL 1 100000 2` |

#### AF_XDP

| Machine | Command |
| --- | --- |
| DuT host | `sudo ./af_xdp_host_cache eno1 eno2 0` |
| DuT host | pin IRQs as described in §2.2 |
| DuT VM | `sudo ./client_ivshmem 0 1300000 2 0` |
| load-gen | `sudo ./dpdk_multiple_streams_server $EAL 1 100000 2` |

#### DPDK

| Machine | Command |
| --- | --- |
| DuT host | `sudo ./dpdk_host_cache -l 9 -n 4 --socket-mem=1024 -a 0000:01:00.0 -a 0000:01:00.1 0` |
| DuT VM | `sudo ./client_ivshmem 0 1300000 2 0` |
| load-gen | `sudo ./dpdk_multiple_streams_server $EAL 1 100000 2` |

#### XDP plain

- for the one-core setup pin the receive IRQs to the same core as described in §2.1
- for the two-core setup pin the receive IRQs to different cores

```bash
sudo ./mdb_set_host_throughput.sh
```

| Machine | Command |
| --- | --- |
| DuT VM | `sudo ./client_multiple_streams 2 1300000 0` |
| load-gen | `sudo ./dpdk_multiple_streams_server $EAL 1 100000 2` |


### 3.3 Worst-case burst — DPDK only

| Machine | Command |
| --- | --- |
| DuT host | `sudo ./dpdk_host_cache -l 9 -n 4 --socket-mem=1024 -a 0000:01:00.0 -a 0000:01:00.1 2` |
| DuT VM | `sudo chrt -f 30 ./client_ivshmem_efd_sync 1 4 15000000 4` |
| load-gen | `sudo ./dpdk_multiple_streams_server $EAL 2 1000000 4` |

> **Note.** Mode 2 runs
> `MAX_DELAY_IN_WINDOWS` phases of `sample_count * windows` ids each, so the VM
> must be given `1000000 * (1+2+3+4+5) = 15000000`. Changing one number without the other makes the run hang.

### 3.4 VM capacity — DPDK only

AF_XDP is almost the same, + ~1.8 µs constant because of the vhost-net `send()`.

Make sure the `histograms` folder is created in `host-cache-vm`.

#### No-monitor setup

| Machine | Command |
| --- | --- |
| DuT host | `sudo ./dpdk_host_cache -l 9 -n 4 --socket-mem=1024 -a 0000:01:00.0 -a 0000:01:00.1 0` |
| DuT VM | `sudo ./client_ivshmem 1 100000` |
| load-gen | `sudo ./dpdk_multiple_streams_server $EAL 3 100000 1` |

#### Monitor setup

```bash
make vm PROCESSING_HISTO=1
```

| Machine | Command |
| --- | --- |
| DuT VM | `sudo chrt -f 30 ./client_ivshmem_efd_sync 1 <stream_count> 100000 0` |
| load-gen | `sudo ./dpdk_multiple_streams_server $EAL 0 100000 <stream_count>` |

Repeat with `<stream_count>` in {1, 2, 3, 4, 6, 8, 11, 16, 23, 32, 45, 64}.
91 and 128 are beyond capacity from management cost alone.

### 3.5 Cost microbenchmarks

#### Send

```bash
make vm SEND_HISTO=1
```

**Vhost-net send (AF_XDP)**

| Machine | Command |
| --- | --- |
| DuT host | `sudo ./af_xdp_host_cache eno1 eno2 0` |
| DuT host | pin IRQs as described in §2.2 |
| DuT VM | `sudo ./client_ivshmem 0 100000 1 0` |
| load-gen | `sudo ./dpdk_multiple_streams_server $EAL 0 100000 1` |

**Vhost-user send (DPDK, no kick)**

| Machine | Command |
| --- | --- |
| DuT host | `sudo ./dpdk_host_cache -l 9 -n 4 --socket-mem=1024 -a 0000:01:00.0 -a 0000:01:00.1 0` |
| DuT VM | `sudo ./client_ivshmem 0 100000 1 0` |
| load-gen | `sudo ./dpdk_multiple_streams_server $EAL 0 100000 1` |

#### IPC

```bash
sudo chrt -f 30 ./2_proc_efd_histo > ipc_histo
```

