#ifndef NEUTRA_SYSTEM_H
#define NEUTRA_SYSTEM_H

#include <stddef.h>
#include <stdint.h>

#include "ata.h"
#include "desktop.h"
#include "disk.h"
#include "explorer.h"
#include "fat.h"
#include "graphics.h"
#include "idt.h"
#include "kernel.h"
#include "scheduler.h"
#include "shell.h"
#include "vga.h"
#include "window.h"

/* =========================
   Window
   ========================= */

void window_system_init(void);

void window_open(const char* title);
void window_close(const char* title);
void window_redraw(const char* title);

int window_is_open(void);
int window_is_minimized(void);

void window_minimize(void);
void window_restore(const char* title);

void window_paint(void);

void window_mouse(int mouse_x, int mouse_y, int buttons);

void window_set_background_painter(
    void (*paint)(int x, int y, int w, int h)
);

void create_window(
    int x,
    int y,
    int width,
    int height,
    const char* title
);
/* =========================
   Drawing
   ========================= */

void set_color(uint32_t color);
uint32_t get_color(void);

void putpixel(int x, int y);

void line(int x0, int y0, int x1, int y1);

void rect(int x, int y, int width, int height);

void clear_screen(uint32_t color);

void present(void);

/* =========================
   BMP
   ========================= */

int display_bmp(
    const void* bmp_data,
    uint32_t bmp_length,
    int x,
    int y,
    int width,
    int height,
    int size
);

#endif