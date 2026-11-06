#ifndef NEUTRA_PNG_H
#define NEUTRA_PNG_H

#include <stdint.h>

#define PNG_MAX_DIM 128

int png_decode(const uint8_t* data, int len, uint8_t* rgb, int rgb_max, int* w, int* h);
void png_box_sample(const uint8_t* rgb, int iw, int ih, int w, int h, int px, int py, uint8_t* out);

#endif
