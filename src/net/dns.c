#include "dns.h"
#include "net_proto.h"
#include "vga.h"

#define DNS_PORT 53
#define DNS_SRC_PORT 40000
#define DNS_SERVER_IP_0 10
#define DNS_TRIES 3
#define DNS_WAIT_LOOPS 1500000
#define DNS_MAX 512

static uint16_t awaited_id;
static int answered;
static uint8_t answer_ip[4];

static uint16_t rd16(const uint8_t* p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

static void wr16(uint8_t* p, uint16_t value) {
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

static int encode_name(const char* name, uint8_t* out, int max) {
    int pos = 0;
    int label_start = pos;
    pos++;
    int label_len = 0;
    for (int i = 0;; i++) {
        const char c = name[i];
        if (c == '.' || c == '\0') {
            if (label_len == 0) {
                if (c == '\0') break;
                return -1;
            }
            if (label_len > 63) return -1;
            out[label_start] = (uint8_t)label_len;
            if (c == '\0') {
                if (pos + 1 > max) return -1;
                out[pos++] = 0;
                return pos;
            }
            label_start = pos;
            pos++;
            label_len = 0;
        } else {
            if (pos + 1 > max) return -1;
            out[pos++] = (uint8_t)c;
            label_len++;
        }
    }
    if (label_len != 0) return -1;
    if (pos + 1 > max) return -1;
    out[label_start] = 0;
    out[pos++] = 0;
    return pos;
}

static int skip_name(const uint8_t* data, int len, int pos) {
    while (pos < len) {
        const uint8_t l = data[pos];
        if (l == 0) return pos + 1;
        if ((l & 0xC0) == 0xC0) return pos + 2;
        pos += l + 1;
    }
    return -1;
}

static void on_udp(const uint8_t* src_ip, uint16_t src_port, uint16_t dst_port,
                   const uint8_t* data, int length) {
    (void)src_ip;
    if (src_port != DNS_PORT || dst_port != DNS_SRC_PORT || length < 12) return;
    if (rd16(data) != awaited_id) return;
    if ((data[2] & 0x80) == 0) return;
    if ((data[3] & 0x0F) != 0) return;
    const int ancount = rd16(data + 6);
    if (ancount == 0) return;

    int pos = skip_name(data, length, 12);
    if (pos < 0) return;
    pos += 4;
    for (int i = 0; i < ancount; i++) {
        pos = skip_name(data, length, pos);
        if (pos < 0 || pos + 10 > length) return;
        const uint16_t type = rd16(data + pos);
        const uint16_t rdlen = rd16(data + pos + 8);
        pos += 10;
        if (pos + rdlen > length) return;
        if (type == 1 && rdlen == 4) {
            for (int k = 0; k < 4; k++) answer_ip[k] = data[pos + k];
            answered = 1;
            return;
        }
        pos += rdlen;
    }
}

int dns_resolve(const char* name, uint8_t ip_out[4]) {
    static uint8_t packet[DNS_MAX];
    uint8_t dns_server[4] = {DNS_SERVER_IP_0, 0, 2, 3};

    uint8_t query[DNS_MAX];
    wr16(query, awaited_id = (uint16_t)(awaited_id + 0x1234));
    wr16(query + 2, 0x0100);
    wr16(query + 4, 1);
    wr16(query + 6, 0);
    wr16(query + 8, 0);
    wr16(query + 10, 0);
    const int name_len = encode_name(name, query + 12, DNS_MAX - 12 - 4);
    if (name_len < 0) return 0;
    wr16(query + 12 + name_len, 1);
    wr16(query + 12 + name_len + 2, 1);
    const int dns_len = 12 + name_len + 4;

    wr16(packet, DNS_SRC_PORT);
    wr16(packet + 2, DNS_PORT);
    wr16(packet + 4, (uint16_t)(8 + dns_len));
    wr16(packet + 6, 0);
    for (int i = 0; i < dns_len; i++) packet[8 + i] = query[i];

    net_proto_set_udp_hook(on_udp);
    for (int attempt = 0; attempt < DNS_TRIES && !answered; attempt++) {
        net_proto_ip_send(dns_server, 17, packet, 8 + dns_len);
        for (int i = 0; i < DNS_WAIT_LOOPS && !answered; i++) net_proto_poll();
    }
    net_proto_set_udp_hook(0);
    if (!answered) return 0;
    for (int i = 0; i < 4; i++) ip_out[i] = answer_ip[i];
    answered = 0;
    return 1;
}

void dns_cmd(const char* name) {
    const unsigned char color = MAKE_COLOR(COLOR_BLACK, COLOR_WHITE);
    const unsigned char red = MAKE_COLOR(COLOR_BLACK, COLOR_RED);
    if (name[0] == '\0') {
        vga_write("usage: dns <name>\n", red);
        return;
    }
    uint8_t ip[4];
    if (!dns_resolve(name, ip)) {
        vga_write("dns: no answer for ", red);
        vga_write(name, red);
        vga_putchar('\n', red);
        return;
    }
    vga_write(name, color);
    vga_write(" -> ", color);
    for (int i = 0; i < 4; i++) {
        vga_print_int(ip[i], color);
        if (i < 3) vga_putchar('.', color);
    }
    vga_putchar('\n', color);
}