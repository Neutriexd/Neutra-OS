#ifndef NEUTRA_INFLATE_H
#define NEUTRA_INFLATE_H

#include <stdint.h>

int is_gzip(const uint8_t* in, int inlen);
int gunzip(const uint8_t* in, int inlen, uint8_t* out, int outmax);
int inflate_raw(const uint8_t* in, int inlen, uint8_t* out, int outmax);

#endif
