#include "net.h"
#include "net_proto.h"
#include "vga.h"
#include "dns.h"
#include "tcp.h"

#define ETH_HLEN 14
#define ETYPE_ARP 0x0806
#define ETYPE_IP 0x0800
#define IP_PROTO_ICMP 1
#define IP_PROTO_UDP 17
#define IP_PROTO_TCP 6
#define ICMP_ECHO_REPLY 0
#define ICMP_ECHO_REQUEST 8
#define FRAME_MAX 1514
#define ICMP_MAX 1400
#define WAIT_LOOPS 1500000

static uint8_t our_ip[4] = {10, 0, 2, 15};
static uint8_t gw_ip[4] = {10, 0, 2, 2};
static uint8_t gw_mac[6];
static uint8_t dns_ip[4] = {10, 0, 2, 3};
static uint8_t subnet_mask[4] = {255, 255, 255, 0};
static int gw_known;
static uint8_t our_mac[6];
static uint16_t ip_id;
static uint16_t ping_id = 0x4E55;
static int ping_reply_seq;
static net_udp_hook_t udp_hook;
static net_tcp_hook_t tcp_hook;
static uint8_t ping_target[4];

static uint16_t rd16(const uint8_t* p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

static void wr16(uint8_t* p, uint16_t value) {
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

static void copy_bytes(uint8_t* dst, const uint8_t* src, int n) {
    for (int i = 0; i < n; i++) dst[i] = src[i];
}

static int same_bytes(const uint8_t* a, const uint8_t* b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

static uint16_t checksum(const uint8_t* data, int len) {
    uint32_t sum = 0;
    int i = 0;
    for (; i + 1 < len; i += 2) sum += (uint32_t)((data[i] << 8) | data[i + 1]);
    if (len & 1) sum += (uint32_t)(data[len - 1] << 8);
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

static void put_hex(uint8_t value, unsigned char color) {
    const char* digits = "0123456789abcdef";
    vga_putchar(digits[value >> 4], color);
    vga_putchar(digits[value & 15], color);
}

static void put_mac(const uint8_t* mac, unsigned char color) {
    for (int i = 0; i < 6; i++) {
        put_hex(mac[i], color);
        if (i < 5) vga_putchar(':', color);
    }
}

static void put_ip(const uint8_t* ip, unsigned char color) {
    for (int i = 0; i < 4; i++) {
        vga_print_int(ip[i], color);
        if (i < 3) vga_putchar('.', color);
    }
}

static int send_frame(const uint8_t dst[6], uint16_t type, const uint8_t* payload, int payload_len) {
    uint8_t frame[FRAME_MAX];
    if (ETH_HLEN + payload_len > FRAME_MAX) return -1;
    copy_bytes(frame, dst, 6);
    copy_bytes(frame + 6, our_mac, 6);
    wr16(frame + 12, type);
    copy_bytes(frame + ETH_HLEN, payload, payload_len);
    return net_send(frame, ETH_HLEN + payload_len);
}

static int arp_send(uint16_t oper, const uint8_t dst[6], const uint8_t target_mac[6], const uint8_t target_ip[4]) {
    uint8_t p[28];
    net_mac(our_mac);
    wr16(p, 1);
    wr16(p + 2, ETYPE_IP);
    p[4] = 6;
    p[5] = 4;
    wr16(p + 6, oper);
    copy_bytes(p + 8, our_mac, 6);
    copy_bytes(p + 14, our_ip, 4);
    copy_bytes(p + 18, target_mac, 6);
    copy_bytes(p + 24, target_ip, 4);
    return send_frame(dst, ETYPE_ARP, p, 28);
}

static void handle_arp(const uint8_t* p, int len) {
    if (len < 28) return;
    const uint16_t oper = rd16(p + 6);
    if (oper == 2 && same_bytes(p + 14, gw_ip, 4)) {
        copy_bytes(gw_mac, p + 8, 6);
        gw_known = 1;
    } else if (oper == 1 && same_bytes(p + 24, our_ip, 4)) {
        arp_send(2, p + 8, p + 8, p + 14);
    }
}

static int build_ip_and_send(const uint8_t dst_ip[4], const uint8_t dst_mac[6], uint8_t proto,
                             const uint8_t* payload, int payload_len) {
    uint8_t ip[20 + ICMP_MAX];
    if (payload_len > ICMP_MAX) return -1;
    net_mac(our_mac);
    ip[0] = 0x45;
    ip[1] = 0;
    wr16(ip + 2, (uint16_t)(20 + payload_len));
    wr16(ip + 4, ip_id++);
    wr16(ip + 6, 0x4000);
    ip[8] = 64;
    ip[9] = proto;
    wr16(ip + 10, 0);
    copy_bytes(ip + 12, our_ip, 4);
    copy_bytes(ip + 16, dst_ip, 4);
    wr16(ip + 10, checksum(ip, 20));
    copy_bytes(ip + 20, payload, payload_len);
    return send_frame(dst_mac, ETYPE_IP, ip, 20 + payload_len);
}

static int icmp_send(const uint8_t dst_ip[4], const uint8_t dst_mac[6], uint8_t type,
                     uint16_t id, uint16_t seq, const uint8_t* data, int data_len) {
    uint8_t icmp[ICMP_MAX];
    if (8 + data_len > ICMP_MAX) return -1;
    icmp[0] = type;
    icmp[1] = 0;
    wr16(icmp + 2, 0);
    wr16(icmp + 4, id);
    wr16(icmp + 6, seq);
    copy_bytes(icmp + 8, data, data_len);
    const int icmp_len = 8 + data_len;
    wr16(icmp + 2, checksum(icmp, icmp_len));
    return build_ip_and_send(dst_ip, dst_mac, IP_PROTO_ICMP, icmp, icmp_len);
}

static void handle_ip(const uint8_t* p, int len, const uint8_t* eth_src) {
    if (len < 20) return;
    const int ihl = (p[0] & 15) * 4;
    if (ihl < 20 || len < ihl) return;
    const int total = rd16(p + 2);
    if (total > len || total < ihl) return;

    if (p[9] == IP_PROTO_TCP) {
        if (tcp_hook) tcp_hook(p + 12, p + 16, p + ihl, total - ihl);
        return;
    }
    if (p[9] == IP_PROTO_UDP) {
        if (total < ihl + 8) return;
        const uint8_t* udp = p + ihl;
        const int udp_len = rd16(udp + 4);
        if (udp_len < 8 || ihl + udp_len > total) return;
        if (udp_hook) udp_hook(p + 12, rd16(udp), rd16(udp + 2), udp + 8, udp_len - 8);
        return;
    }
    if (p[9] != IP_PROTO_ICMP || total < ihl + 8) return;

    const uint8_t* icmp = p + ihl;
    const int icmp_len = total - ihl;
    const uint8_t type = icmp[0];
    const uint16_t id = rd16(icmp + 4);
    const uint16_t seq = rd16(icmp + 6);

    if (type == ICMP_ECHO_REQUEST && same_bytes(p + 16, our_ip, 4)) {
        icmp_send(p + 12, eth_src, ICMP_ECHO_REPLY, id, seq, icmp + 8, icmp_len - 8);
    } else if (type == ICMP_ECHO_REPLY && same_bytes(p + 12, ping_target, 4) && id == ping_id) {
        ping_reply_seq = seq;
    }
}

void net_proto_poll(void) {
    uint8_t frame[1600];
    int n;
    while ((n = net_receive(frame, sizeof(frame))) > 0) {
        if (n < ETH_HLEN) continue;
        const uint16_t type = rd16(frame + 12);
        if (type == ETYPE_ARP) handle_arp(frame + ETH_HLEN, n - ETH_HLEN);
        else if (type == ETYPE_IP) handle_ip(frame + ETH_HLEN, n - ETH_HLEN, frame + 6);
    }
}

static int resolve_gateway(void) {
    if (gw_known) return 1;
    const uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    const uint8_t zero[6] = {0, 0, 0, 0, 0, 0};
    arp_send(1, broadcast, zero, gw_ip);
    for (int i = 0; i < WAIT_LOOPS && !gw_known; i++) net_proto_poll();
    return gw_known;
}

int net_proto_gateway_ready(void) {
    return resolve_gateway();
}

void net_proto_set_udp_hook(net_udp_hook_t hook) {
    udp_hook = hook;
}

int net_proto_ip_send(const uint8_t dst_ip[4], uint8_t proto, const uint8_t* payload, int payload_len) {
    if (!resolve_gateway()) return -1;
    return build_ip_and_send(dst_ip, gw_mac, proto, payload, payload_len);
}

void net_cmd_arp(void) {
    const unsigned char color = MAKE_COLOR(COLOR_BLACK, COLOR_WHITE);
    const unsigned char red = MAKE_COLOR(COLOR_BLACK, COLOR_RED);
    if (!net_ready()) {
        vga_write("arp: no network card\n", red);
        return;
    }
    if (!resolve_gateway()) {
        vga_write("arp: no answer from ", red);
        put_ip(gw_ip, red);
        vga_putchar('\n', red);
        return;
    }
    vga_write("arp: ", color);
    put_ip(gw_ip, color);
    vga_write(" is at ", color);
    put_mac(gw_mac, color);
    vga_putchar('\n', color);
}

static int parse_ip(const char* text, uint8_t out[4]) {
    int value = 0;
    int digits = 0;
    int part = 0;
    for (int i = 0;; i++) {
        const char c = text[i];
        if (c >= '0' && c <= '9') {
            value = value * 10 + (c - '0');
            digits++;
            if (value > 255) return 0;
        } else if (c == '.' || c == '\0' || c == ' ') {
            if (digits == 0 || part > 3) return 0;
            out[part++] = (uint8_t)value;
            value = 0;
            digits = 0;
            if (c == '\0' || c == ' ') return part == 4;
        } else {
            return 0;
        }
    }
}

void net_cmd_ping(const char* text) {
    const unsigned char color = MAKE_COLOR(COLOR_BLACK, COLOR_WHITE);
    const unsigned char red = MAKE_COLOR(COLOR_BLACK, COLOR_RED);
    uint8_t target[4];
    if (!net_ready()) {
        vga_write("ping: no network card\n", red);
        return;
    }
    if (text[0] == '\0') {
        vga_write("usage: ping <ip or domain>\n", red);
        return;
    }
    if (!parse_ip(text, target)) {
        if (!dns_resolve(text, target)) {
            vga_write("ping: cannot resolve ", red);
            vga_write(text, red);
            vga_putchar('\n', red);
            return;
        }
        vga_write("ping ", color);
        vga_write(text, color);
        vga_write(" (", color);
        put_ip(target, color);
        vga_write(")\n", color);
    }
    for (int i = 0; i < 4; i++) ping_target[i] = target[i];
    if (!resolve_gateway()) {
        vga_write("ping: gateway does not answer ARP\n", red);
        return;
    }

    static const uint8_t payload[32] = {
        'N', 'E', 'U', 'T', 'R', 'A', ' ', 'P', 'I', 'N', 'G', ' ', 0, 1, 2, 3,
        4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19
    };
    int received = 0;
    for (uint16_t seq = 1; seq <= 4; seq++) {
        ping_reply_seq = -1;
        icmp_send(target, gw_mac, ICMP_ECHO_REQUEST, ping_id, seq, payload, sizeof(payload));
        for (int i = 0; i < WAIT_LOOPS && ping_reply_seq != seq; i++) net_proto_poll();
        vga_write("ping seq=", color);
        vga_print_int(seq, color);
        if (ping_reply_seq == seq) {
            received++;
            vga_write(": reply from ", color);
            put_ip(target, color);
            vga_putchar('\n', color);
        } else {
            vga_write(": timeout\n", red);
        }
    }
    vga_write("ping: ", color);
    vga_print_int(received, color);
    vga_write(" of 4 replies\n", color);

    if (received == 0) {
        vga_write("icmp gets no reply (QEMU user network does not forward ping), trying tcp port 80\n", color);
        int reached = 0;
        for (uint16_t seq = 1; seq <= 4; seq++) {
            vga_write("tcp ping seq=", color);
            vga_print_int(seq, color);
            if (tcp_probe(target, 80)) {
                reached++;
                vga_write(": connected to ", color);
                put_ip(target, color);
                vga_write(" port 80\n", color);
            } else {
                vga_write(": timeout\n", red);
            }
        }
        vga_write("tcp ping: ", color);
        vga_print_int(reached, color);
        vga_write(" of 4 connected\n", color);
    }
}

void net_proto_set_tcp_hook(net_tcp_hook_t hook) {
    tcp_hook = hook;
}

void net_proto_our_ip(uint8_t out[4]) {
    for (int i = 0; i < 4; i++) out[i] = our_ip[i];
}

static void put_mac_dashes(const uint8_t* mac, unsigned char color) {
    for (int i = 0; i < 6; i++) {
        put_hex(mac[i], color);
        if (i < 5) vga_putchar('-', color);
    }
}

void net_cmd_ipconfig(void) {
    const unsigned char color = MAKE_COLOR(COLOR_BLACK, COLOR_WHITE);
    const unsigned char red = MAKE_COLOR(COLOR_BLACK, COLOR_RED);
    vga_write("\nNeutra IP Configuration\n\n", color);
    vga_write("Ethernet adapter Neutra:\n\n", color);
    if (!net_ready()) {
        vga_write("   Media State . . . . . . . . . . . : ", red);
        vga_write("Disconnected\n", red);
        return;
    }
    uint8_t mac[6];
    net_mac(mac);
    vga_write("   Media State . . . . . . . . . . . : Connected\n", color);
    vga_write("   Connection-specific DNS Suffix  . :\n", color);
    vga_write("   IPv4 Address. . . . . . . . . . . : ", color);
    put_ip(our_ip, color);
    vga_putchar('\n', color);
    vga_write("   Subnet Mask . . . . . . . . . . . : ", color);
    put_ip(subnet_mask, color);
    vga_putchar('\n', color);
    vga_write("   Default Gateway . . . . . . . . . : ", color);
    put_ip(gw_ip, color);
    vga_putchar('\n', color);
    vga_write("   DNS Server  . . . . . . . . . . . : ", color);
    put_ip(dns_ip, color);
    vga_putchar('\n', color);
    vga_write("   Physical Address. . . . . . . . . : ", color);
    put_mac_dashes(mac, color);
    vga_putchar('\n', color);
}