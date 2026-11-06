#ifndef NEUTRA_DNS_H
#define NEUTRA_DNS_H

#include <stdint.h>

int dns_resolve(const char* name, uint8_t ip_out[4]);
void dns_cmd(const char* name);

#endif