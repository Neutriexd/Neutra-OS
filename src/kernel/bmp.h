#ifndef NEUTRA_BMP_H
#define NEUTRA_BMP_H

#include <stdint.h>

int bmp_load_indices(const char* path, uint8_t* out, int* width, int* height);
int bmp_image_loader(const char* name, uint8_t* out, int* width, int* height);

#endif