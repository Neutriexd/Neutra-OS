#ifndef NEUTRA_RTL8139_H
#define NEUTRA_RTL8139_H

#include <stdint.h>

int rtl8139_init(void);
int rtl8139_ready(void);
void rtl8139_mac(uint8_t out[6]);
int rtl8139_send(const uint8_t* frame, int length);
int rtl8139_receive(uint8_t* out, int max);

#endif