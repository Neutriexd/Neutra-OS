#ifndef NEUTRA_NET_H
#define NEUTRA_NET_H

#include <stdint.h>

int net_init(void);
int net_ready(void);
void net_mac(uint8_t out[6]);
int net_send(const uint8_t* frame, int length);
int net_receive(uint8_t* out, int max);
void net_print_info(void);

#endif