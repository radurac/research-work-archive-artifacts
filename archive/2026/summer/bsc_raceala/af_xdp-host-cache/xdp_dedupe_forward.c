#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/types.h>

#include <bpf/bpf_endian.h>
#include <bpf/bpf_helpers.h>

// this is the XDP plain eBPF program
// the only difference between this and the AF_XDP one
// is that it XDP_PASSes valid critical traffic
// instead of XDP_REDIRECTing it

#define NUM_DEVICES 128
// no frames sent by a device for 20 ms
// => reset corresponding valid ring
#define INACTIVITY_THRESHOLD_IN_NS 20000000

struct epoch_map_elem {
    unsigned char epoch_ring[2048];
    struct bpf_spin_lock lock;
};

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, NUM_DEVICES);
    __type(key, __u32);
    __type(value, struct epoch_map_elem);
    __uint(pinning, LIBBPF_PIN_BY_NAME);
} epoch_map SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, NUM_DEVICES);
    __type(key, __u32);
    __type(value, unsigned char[512]);
    __uint(pinning, LIBBPF_PIN_BY_NAME);
} valid_map SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, NUM_DEVICES);
    __type(key, __u32);
    __type(value, __u64);
    __uint(pinning, LIBBPF_PIN_BY_NAME);
} last_seen_map SEC(".maps");

SEC("xdp")
int xdp_dedupe_forward(struct xdp_md *ctx) {
    void *data_end = (void *)(long)ctx->data_end;
    void *data = (void *)(long)ctx->data;

    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end) {
        return XDP_DROP;
    }

    // best-effort traffic
    if (bpf_ntohs(eth->h_proto) != 0x88ba) {
        return XDP_PASS;
    }

    __u32 svid = (__u32)eth->h_dest[5];

    __u8 prp_trailer[6];
    __u32 offset = (__u32)(data_end - data) - 6;

    if (offset + 6 > 0xffff)
        return XDP_DROP;

    if (bpf_xdp_load_bytes(ctx, offset, prp_trailer, sizeof(prp_trailer)) < 0)
        return XDP_DROP;

    // invalid PRP suffix
    if (prp_trailer[4] != 0x88 || prp_trailer[5] != 0xfb) {
        return XDP_DROP;
    }

    __u16 seq = (prp_trailer[0] << 8) + prp_trailer[1];

    struct epoch_map_elem *epoch_elem = bpf_map_lookup_elem(&epoch_map, &svid);
    unsigned char *valid_ring = bpf_map_lookup_elem(&valid_map, &svid);
    __u64 *last_seen = bpf_map_lookup_elem(&last_seen_map, &svid);

    if (!epoch_elem) {
        return XDP_DROP;
    }
    if (!valid_ring) {
        return XDP_DROP;
    }
    if (!last_seen) {
        return XDP_DROP;
    }

    unsigned char *epoch_ring = epoch_elem->epoch_ring;

    __u16 index = seq & 4095;
    __u8 epoch = seq >> 12;
    __u8 nibble_index = index & 1;
    __u8 bit_index = index & 7;

    __u64 now = bpf_ktime_get_ns();

    struct bpf_spin_lock *lock = &(epoch_elem->lock);

    bpf_spin_lock(lock);

    if (now > *last_seen) {
        if (now - *last_seen > INACTIVITY_THRESHOLD_IN_NS) {
            __builtin_memset(valid_ring, 0, 512);
        }
        *last_seen = now;
    }

    __u8 stored_epoch;
    __u16 epoch_index = (index >> 1) & 2047;
    __u16 valid_index = (index >> 3) & 511;
    __u8 valid_bit = valid_ring[valid_index] & (1 << bit_index);

    // case distinction based on the position of the relevant nibble, already
    // replacing the epoch with the current one if they are the same and the
    // frame will be dropped then nothing changes either way

    if (nibble_index == 0) {
        stored_epoch = epoch_ring[epoch_index & 2047] & 15;
        epoch_ring[epoch_index & 2047] =
            epoch_ring[epoch_index & 2047] - stored_epoch + epoch;
    } else {
        stored_epoch = epoch_ring[epoch_index & 2047] >> 4;
        epoch_ring[epoch_index & 2047] =
            (epoch_ring[epoch_index & 2047] & 15) + (epoch << 4);
    }

    if (epoch == stored_epoch && valid_bit) {
        bpf_spin_unlock(lock);
        return XDP_DROP;
    }

    // setting the valid bit regardless of
    // what was there beforehand
    valid_ring[valid_index] = valid_ring[valid_index] | (1 << bit_index);
    bpf_spin_unlock(lock);

    // removing the PRP trailer
    if (bpf_xdp_adjust_tail(ctx, -6) < 0) {
        return XDP_DROP;
    }

    return XDP_PASS;
}
