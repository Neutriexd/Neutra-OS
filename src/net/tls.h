#ifndef NEUTRA_TLS_H
#define NEUTRA_TLS_H

#include <stdint.h>

int tls_open(const uint8_t ip[4], uint16_t port, const char* host);
int tls_write(const uint8_t* data, int len);
int tls_read(uint8_t* out, int max);
void tls_close(void);
int tls_last_error(void);
void tls_set_progress(void (*fn)(const char*));

#endif