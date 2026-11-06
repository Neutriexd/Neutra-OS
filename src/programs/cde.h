#ifndef NEUTRA_CDE_H
#define NEUTRA_CDE_H

#include <stdint.h>

#define CDE_RUN_ERROR 0
#define CDE_RUN_OK 1
#define CDE_RUN_STOPPED 2

#define CDE_MAGIC_VALUE 0x43444501u

typedef struct {
    void (*put_char)(char c);
    int (*win_create)(int x, int y, int w, int h, const char* title, int title_length);
    void (*win_char)(int id, char c);
    void (*win_color)(int id, uint32_t rgb);
    void (*win_pixel)(int id, int x, int y);
    void (*win_rect)(int id, int x, int y, int w, int h);
    void (*win_line)(int id, int x0, int y0, int x1, int y1);
    void (*win_text)(int id, int x, int y, const char* text, int length);
    void (*win_image)(int id, int x, int y, const uint8_t* indices, int width, int height);
    void (*win_clear)(int id);
    void (*win_refresh)(int id);
    void (*win_close)(int id);
    int (*get_key)(void);
    int (*should_stop)(void);
    void (*mainloop)(void);
    void (*finish)(void);
} CdeHost;

typedef int (*CdeImageLoader)(const char* name, uint8_t* indices48, int* width, int* height);
void cde_set_image_loader(CdeImageLoader loader);

int cde_compile(const char* source, int length, uint8_t* out, int capacity, char* error, int error_size);
int cde_run(const uint8_t* image, int length, const CdeHost* host, char* error, int error_size);
int cde_is_autostart(const uint8_t* header, int length);

#endif