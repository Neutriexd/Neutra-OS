#include <stddef.h>
#include "bmp.h"
#include "fat.h"

extern DISK kernel_fat_disk;
extern int kernel_fat_mounted;

#define STRIDE 48

static const uint32_t palette16[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF
};

static uint8_t row_buffer[4096];
static uint32_t file_palette[256];

static uint32_t le32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t le16(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

static int nearest(uint32_t rgb) {
    const int r = (int)((rgb >> 16) & 0xFF);
    const int g = (int)((rgb >> 8) & 0xFF);
    const int b = (int)(rgb & 0xFF);
    int best = 0;
    int best_distance = 0x7FFFFFFF;
    for (int i = 0; i < 16; i++) {
        const int pr = (int)((palette16[i] >> 16) & 0xFF);
        const int pg = (int)((palette16[i] >> 8) & 0xFF);
        const int pb = (int)(palette16[i] & 0xFF);
        const int distance = (r - pr) * (r - pr) + (g - pg) * (g - pg) + (b - pb) * (b - pb);
        if (distance < best_distance) {
            best_distance = distance;
            best = i;
        }
    }
    return best;
}

int bmp_load_indices(const char* path, uint8_t* out, int* out_width, int* out_height) {
    if (!kernel_fat_mounted) return 0;
    FAT_File* file = FAT_Open(&kernel_fat_disk, path);
    if (file == NULL || file->IsDirectory) {
        if (file != NULL) FAT_Close(file);
        return 0;
    }
    uint8_t header[54];
    if (FAT_Read(&kernel_fat_disk, file, sizeof(header), header) != sizeof(header) ||
        header[0] != 'B' || header[1] != 'M') {
        FAT_Close(file);
        return 0;
    }
    const uint32_t data_offset = le32(header + 10);
    const uint32_t dib = le32(header + 14);
    int width = (int)le32(header + 18);
    int height = (int)le32(header + 22);
    const int bpp = (int)le16(header + 28);
    const uint32_t compression = le32(header + 30);
    uint32_t colors = le32(header + 46);
    const int top_down = height < 0;
    if (height < 0) height = -height;
    if (dib < 40 || compression != 0 || width <= 0 || width > 1024 || height <= 0 || height > 4096 ||
        (bpp != 4 && bpp != 8 && bpp != 24 && bpp != 32)) {
        FAT_Close(file);
        return 0;
    }
    if (bpp <= 8) {
        if (colors == 0 || colors > 256) colors = (uint32_t)(1 << bpp);
        file->Position = 14 + dib;
        if (FAT_Read(&kernel_fat_disk, file, colors * 4, row_buffer) != colors * 4) {
            FAT_Close(file);
            return 0;
        }
        for (uint32_t i = 0; i < colors; i++) {
            file_palette[i] = ((uint32_t)row_buffer[i * 4 + 2] << 16) | ((uint32_t)row_buffer[i * 4 + 1] << 8) |
                              (uint32_t)row_buffer[i * 4];
        }
    }
    const uint32_t stride = (((uint32_t)width * (uint32_t)bpp + 31) / 32) * 4;
    const uint32_t needed = ((uint32_t)width * (uint32_t)bpp + 7) / 8;
    if (stride > sizeof(row_buffer)) {
        FAT_Close(file);
        return 0;
    }
    int out_w = width;
    int out_h = height;
    if (width > STRIDE || height > STRIDE) {
        if (width >= height) {
            out_w = STRIDE;
            out_h = height * STRIDE / width;
        } else {
            out_h = STRIDE;
            out_w = width * STRIDE / height;
        }
        if (out_w < 1) out_w = 1;
        if (out_h < 1) out_h = 1;
    }
    int last_row = -1;
    for (int dy = 0; dy < out_h; dy++) {
        const int sy = dy * height / out_h;
        if (sy != last_row) {
            const int row = top_down ? sy : height - 1 - sy;
            file->Position = data_offset + (uint32_t)row * stride;
            if (FAT_Read(&kernel_fat_disk, file, stride, row_buffer) < needed) {
                FAT_Close(file);
                return 0;
            }
            last_row = sy;
        }
        for (int dx = 0; dx < out_w; dx++) {
            const int sx = dx * width / out_w;
            uint32_t rgb;
            if (bpp == 4) {
                const uint8_t byte = row_buffer[sx / 2];
                rgb = file_palette[(sx & 1) ? (byte & 15) : (byte >> 4)];
            } else if (bpp == 8) {
                rgb = file_palette[row_buffer[sx]];
            } else {
                const int step = bpp / 8;
                rgb = ((uint32_t)row_buffer[sx * step + 2] << 16) | ((uint32_t)row_buffer[sx * step + 1] << 8) |
                      (uint32_t)row_buffer[sx * step];
            }
            out[dy * STRIDE + dx] = (uint8_t)nearest(rgb);
        }
    }
    FAT_Close(file);
    *out_width = out_w;
    *out_height = out_h;
    return 1;
}

int bmp_image_loader(const char* name, uint8_t* out, int* width, int* height) {
    char path[64];
    int n = 0;
    if (name[0] != '/') path[n++] = '/';
    for (int i = 0; name[i] && n < 62; i++) {
        char c = name[i];
        if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
        path[n++] = c;
    }
    path[n] = '\0';
    return bmp_load_indices(path, out, width, height);
}