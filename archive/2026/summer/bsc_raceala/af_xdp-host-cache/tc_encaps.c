
#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/types.h>

#include <bpf/bpf_endian.h>
#include <bpf/bpf_helpers.h>

#define TC_ACT_OK 0
#define TC_ACT_SHOT 2
#define TC_ACT_REDIRECT 7

// the ifindex of the second physical interface
#define LAN_B_IFINDEX 6

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u32);
    __uint(pinning, LIBBPF_PIN_BY_NAME);
} sequence_number SEC(".maps");

SEC("tc")
int tc_egress_encaps(struct __sk_buff *skb) {

    void *data_end = (void *)(long)skb->data_end;
    void *data = (void *)(long)skb->data;

    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end) {
        return TC_ACT_OK;
    }

    __u16 ethtype = bpf_ntohs(eth->h_proto);
    if (ethtype != 0x88baU) {
        return TC_ACT_OK;
    }

    int ret = bpf_skb_change_tail(skb, skb->len + 6, 0);
    if (ret < 0) {
        return TC_ACT_SHOT;
    }

    data_end = (void *)(long)skb->data_end;
    data = (void *)(long)skb->data;

    unsigned char prp_trailer[6];

    __u32 key = 0;
    __u32 *seq_ptr = bpf_map_lookup_elem(&sequence_number, &key);
    if (!seq_ptr) {
        return TC_ACT_SHOT;
    }

    // automatic wraparound at 2^32
    __u32 seq = __sync_fetch_and_add(seq_ptr, 1);
    seq %= 65536;

    // sequence number
    prp_trailer[0] = seq >> 8;
    prp_trailer[1] = seq & 255;

    // first nibble LAN B; second nibble the most significant nibble of size
    prp_trailer[2] = (0xb0 | (((skb->len - 14) >> 8) & 15));

    prp_trailer[3] = (skb->len - 14) & 255;

    // PRP suffix
    prp_trailer[4] = 0x88;
    prp_trailer[5] = 0xfb;

    __u32 offset = (__u32)(data_end - data) - 6;
    if (offset + 2 > 0xffff) {
        return TC_ACT_OK;
    }

    ret = bpf_skb_store_bytes(skb, offset, prp_trailer, 6, 0);
    if (ret < 0) {
        return TC_ACT_SHOT;
    }
    ret = bpf_clone_redirect(skb, LAN_B_IFINDEX, 0);
    if (ret < 0) {
        return TC_ACT_SHOT;
    }

    data_end = (void *)(long)skb->data_end;
    data = (void *)(long)skb->data;

    eth = data;
    if ((void *)(eth + 1) > data_end) {
        return TC_ACT_OK;
    }

    // first nibble LAN A; second nibble the most significant nibble of size
    prp_trailer[2] = 0xa0 | (((skb->len - 14) >> 8) & 15);
    ret = bpf_skb_store_bytes(skb, offset + 2, prp_trailer + 2, 1, 0);
    if (ret < 0) {
        return TC_ACT_SHOT;
    }

    return TC_ACT_OK;
}
