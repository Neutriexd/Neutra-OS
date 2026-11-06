#include "tcp.h"
#include "net_proto.h"

#define TCP_HDR 20
#define SEG_MAX 1400
#define RX_MAX 262144
#define WAIT_LOOPS 1500000
#define IDLE_LOOPS 3000000
#define RETRIES 3
#define F_FIN 0x01
#define F_SYN 0x02
#define F_RST 0x04
#define F_PSH 0x08
#define F_ACK 0x10
#define ST_CLOSED 0
#define ST_SYN_SENT 1
#define ST_ESTABLISHED 2

static uint8_t remote_ip[4];
static uint16_t remote_port;
static uint16_t local_port = 49200;
static uint32_t our_seq;
static uint32_t remote_seq;
static uint32_t acked;
static int state;
static int rx_fin;
static uint8_t rx[RX_MAX];
static int rx_len;
static int rx_head;

static uint16_t rd16(const uint8_t* p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

static uint32_t rd32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void wr16(uint8_t* p, uint16_t value) {
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

static void wr32(uint8_t* p, uint32_t value) {
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static int same4(const uint8_t* a, const uint8_t* b) {
    for (int i = 0; i < 4; i++) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

static uint16_t tcp_checksum(const uint8_t* src, const uint8_t* dst, const uint8_t* seg, int len) {
    uint32_t sum = 0;
    sum += (uint32_t)((src[0] << 8) | src[1]) + (uint32_t)((src[2] << 8) | src[3]);
    sum += (uint32_t)((dst[0] << 8) | dst[1]) + (uint32_t)((dst[2] << 8) | dst[3]);
    sum += 6;
    sum += (uint32_t)len;
    int i = 0;
    for (; i + 1 < len; i += 2) sum += (uint32_t)((seg[i] << 8) | seg[i + 1]);
    if (len & 1) sum += (uint32_t)(seg[len - 1] << 8);
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

static int seg_send(uint32_t seq, uint8_t flags, const uint8_t* data, int dlen) {
    uint8_t seg[TCP_HDR + SEG_MAX];
    uint8_t our[4];
    if (dlen > SEG_MAX) return -1;
    net_proto_our_ip(our);
    wr16(seg, local_port);
    wr16(seg + 2, remote_port);
    wr32(seg + 4, seq);
    wr32(seg + 8, remote_seq);
    seg[12] = 0x50;
    seg[13] = flags;
    wr16(seg + 14, 8192);
    wr16(seg + 16, 0);
    wr16(seg + 18, 0);
    for (int i = 0; i < dlen; i++) seg[TCP_HDR + i] = data[i];
    wr16(seg + 16, tcp_checksum(our, remote_ip, seg, TCP_HDR + dlen));
    return net_proto_ip_send(remote_ip, 6, seg, TCP_HDR + dlen);
}

static void on_tcp(const uint8_t* src, const uint8_t* dst, const uint8_t* seg, int len) {
    (void)dst;
    if (len < TCP_HDR || !same4(src, remote_ip)) return;
    if (rd16(seg) != remote_port || rd16(seg + 2) != local_port) return;
    const int hlen = (seg[12] >> 4) * 4;
    if (hlen < TCP_HDR || hlen > len) return;
    const uint8_t flags = seg[13];
    const uint32_t seq = rd32(seg + 4);
    const uint32_t ack = rd32(seg + 8);
    const uint8_t* data = seg + hlen;
    const int dlen = len - hlen;

    if (flags & F_RST) {
        state = ST_CLOSED;
        rx_fin = 1;
        return;
    }
    if (state == ST_SYN_SENT) {
        if ((flags & (F_SYN | F_ACK)) == (F_SYN | F_ACK) && ack == our_seq + 1) {
            our_seq = ack;
            acked = ack;
            remote_seq = seq + 1;
            state = ST_ESTABLISHED;
            seg_send(our_seq, F_ACK, 0, 0);
        }
        return;
    }
    if (state != ST_ESTABLISHED) return;

    if (flags & F_ACK) acked = ack;

    if (dlen > 0) {
        if (seq == remote_seq) {
            const int room = RX_MAX - rx_len;
            const int take = dlen < room ? dlen : room;
            for (int i = 0; i < take; i++) rx[rx_len++] = data[i];
            remote_seq += (uint32_t)take;
        }
        seg_send(our_seq, F_ACK, 0, 0);
    }
    if ((flags & F_FIN) && seq + (uint32_t)dlen == remote_seq) {
        remote_seq += 1;
        rx_fin = 1;
        seg_send(our_seq, F_ACK, 0, 0);
    }
}

int tcp_connect(const uint8_t ip[4], uint16_t port) {
    for (int i = 0; i < 4; i++) remote_ip[i] = ip[i];
    remote_port = port;
    local_port = (local_port >= 65000) ? 49200 : (uint16_t)(local_port + 1);
    our_seq = 1000;
    acked = 1000;
    remote_seq = 0;
    rx_len = 0;
    rx_head = 0;
    rx_fin = 0;
    state = ST_SYN_SENT;
    net_proto_set_tcp_hook(on_tcp);
    for (int attempt = 0; attempt < RETRIES && state == ST_SYN_SENT; attempt++) {
        seg_send(our_seq, F_SYN, 0, 0);
        for (int i = 0; i < WAIT_LOOPS && state == ST_SYN_SENT; i++) net_proto_poll();
    }
    if (state != ST_ESTABLISHED) {
        state = ST_CLOSED;
        net_proto_set_tcp_hook(0);
        return -1;
    }
    return 0;
}

int tcp_probe(const uint8_t ip[4], uint16_t port) {
    if (tcp_connect(ip, port) != 0) return 0;
    tcp_close();
    return 1;
}

int tcp_send(const uint8_t* data, int len) {
    if (state != ST_ESTABLISHED) return -1;
    int off = 0;
    while (off < len) {
        const int chunk = (len - off) > SEG_MAX ? SEG_MAX : (len - off);
        const uint32_t end = our_seq + (uint32_t)chunk;
        int ok = 0;
        for (int attempt = 0; attempt < RETRIES && !ok; attempt++) {
            seg_send(our_seq, F_PSH | F_ACK, data + off, chunk);
            for (int i = 0; i < WAIT_LOOPS && !ok; i++) {
                net_proto_poll();
                if ((int32_t)(acked - end) >= 0) ok = 1;
            }
        }
        if (!ok) return -1;
        our_seq = end;
        off += chunk;
    }
    return len;
}

int tcp_recv_all(uint8_t* out, int max) {
    int idle = 0;
    int last = rx_len;
    while (!rx_fin && idle < IDLE_LOOPS) {
        net_proto_poll();
        if (rx_len != last) {
            last = rx_len;
            idle = 0;
        } else {
            idle++;
        }
    }
    int n = rx_len - rx_head;
    if (n > max) n = max;
    for (int i = 0; i < n; i++) out[i] = rx[rx_head + i];
    rx_head += n;
    return n;
}

int tcp_read(uint8_t* out, int max, int idle_limit) {
    int idle = 0;
    while (rx_head >= rx_len && !rx_fin && idle < idle_limit) {
        net_proto_poll();
        idle++;
    }
    if (rx_head >= rx_len) return rx_fin ? 0 : -1;
    int n = rx_len - rx_head;
    if (n > max) n = max;
    for (int i = 0; i < n; i++) out[i] = rx[rx_head + i];
    rx_head += n;
    if (rx_head == rx_len) {
        rx_head = 0;
        rx_len = 0;
    }
    return n;
}

void tcp_close(void) {
    if (state == ST_ESTABLISHED) {
        seg_send(our_seq, F_FIN | F_ACK, 0, 0);
        our_seq += 1;
    }
    state = ST_CLOSED;
    net_proto_set_tcp_hook(0);
}