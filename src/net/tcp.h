#ifndef NEUTRA_TCP_H
#define NEUTRA_TCP_H

#include <stdint.h>

int tcp_connect(const uint8_t ip[4], uint16_t port);
int tcp_probe(const uint8_t ip[4], uint16_t port);
int tcp_send(const uint8_t* data, int len);
int tcp_recv_all(uint8_t* out, int max);
int tcp_read(uint8_t* out, int max, int idle_limit);
void tcp_close(void);

#endif