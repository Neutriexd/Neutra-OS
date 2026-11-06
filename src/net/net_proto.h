#ifndef NEUTRA_NET_PROTO_H
#define NEUTRA_NET_PROTO_H

#include <stdint.h>

typedef void (*net_udp_hook_t)(const uint8_t* src_ip, uint16_t src_port, uint16_t dst_port,
                               const uint8_t* data, int length);

typedef void (*net_tcp_hook_t)(const uint8_t* src_ip, const uint8_t* dst_ip,
                               const uint8_t* segment, int length);

void net_proto_poll(void);
void net_proto_set_tcp_hook(net_tcp_hook_t hook);
void net_proto_our_ip(uint8_t out[4]);
void net_proto_set_udp_hook(net_udp_hook_t hook);
int net_proto_gateway_ready(void);
int net_proto_ip_send(const uint8_t dst_ip[4], uint8_t proto, const uint8_t* payload, int payload_len);
void net_cmd_arp(void);
void net_cmd_ipconfig(void);
void net_cmd_ping(const char* text);

#endif