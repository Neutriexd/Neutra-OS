#include "system.h"
#include <stdint.h>
#include <stddef.h>

#include "kernel.h"
#include "vga.h"
#include "graphics.h"
#include "font.h"
#include "shell_window.h"
#include "menu.h"
#include "taskbar_ui.h"

#define SYSTEM_BMP_MAX_DIMENSION 8192

#ifndef VGA_EV_CELL
#define VGA_EV_CELL 0
#endif

#ifndef VGA_EV_SCROLL
#define VGA_EV_SCROLL 1
#endif

#ifndef VGA_EV_CLEAR
#define VGA_EV_CLEAR 2
#endif

#define FALLBACK_BACKGROUND 0x0878B0
#define TASKBAR_HEIGHT 36
#define FB_CELL_WIDTH 10
#define FB_CELL_HEIGHT 16
#define WINDOW_TITLE_HEIGHT 36
#define WINDOW_MAX_WIDTH 900
#define WINDOW_MAX_HEIGHT 540
#define CLOSE_SIZE 18

typedef void (*vga_listener_t)(int event, int x, int y);

void vga_set_listener(vga_listener_t listener);
uint16_t vga_get_cell(int x, int y);
void gfx_present(void);

static const uint32_t palette[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA,
    0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF,
    0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF
};

/* =========================
   Window state
   ========================= */

static int placed = 0;
static int minimized = 0;
static int window_open_state = 0;

static int dragging = 0;
static int drag_dx = 0;
static int drag_dy = 0;
static int prev_buttons = 0;

static int window_x = 0;
static int window_y = 0;

static int window_width_value = 0;
static int window_height_value = 0;

static const char* window_title = "Window";

static void (*background_painter)(int x, int y, int w, int h) = NULL;

static uint32_t system_draw_color = 0xFFFFFF;

static uint32_t system_bmp_row[SYSTEM_BMP_MAX_DIMENSION];

/* =========================
   Helpers
   ========================= */

static uint16_t read_u16(const uint8_t* data)
{
    return (uint16_t)data[0] |
           ((uint16_t)data[1] << 8);
}

static uint32_t read_u32(const uint8_t* data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

/* =========================
   Drawing
   ========================= */

void set_color(uint32_t color)
{
    system_draw_color = color & 0xFFFFFF;
}

uint32_t get_color(void)
{
    return system_draw_color;
}

void putpixel(int x, int y)
{
    putpixel_rgb(x, y, system_draw_color);
}

void line(int x0, int y0, int x1, int y1)
{
    draw_line(x0, y0, x1, y1, system_draw_color);
}

void rect(int x, int y, int width, int height)
{
    draw_rect(x, y, width, height, system_draw_color);
}

void clear_screen(uint32_t color)
{
    fb_clear(color & 0xFFFFFF);
}

void present(void)
{
    gfx_present();
}

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
)
{
    if (bmp_data == NULL ||
        bmp_length < 54 ||
        size < 0 ||
        !fb_info.has_framebuffer)
    {
        return 0;
    }

    const uint8_t* bmp = (const uint8_t*)bmp_data;

    if (bmp[0] != 'B' || bmp[1] != 'M')
        return 0;

    const uint32_t pixel_offset = read_u32(bmp + 10);
    const uint32_t dib_size = read_u32(bmp + 14);

    if (dib_size < 40 ||
        (uint64_t)14 + dib_size > bmp_length ||
        pixel_offset < 14 + dib_size)
    {
        return 0;
    }

    const int32_t source_width =
        (int32_t)read_u32(bmp + 18);

    const int32_t signed_height =
        (int32_t)read_u32(bmp + 22);

    const uint16_t planes =
        read_u16(bmp + 26);

    const uint16_t bits_per_pixel =
        read_u16(bmp + 28);

    const uint32_t compression =
        read_u32(bmp + 30);

    if (source_width <= 0 ||
        signed_height == 0 ||
        signed_height == (-2147483647 - 1) ||
        source_width > SYSTEM_BMP_MAX_DIMENSION ||
        planes != 1 ||
        (bits_per_pixel != 24 && bits_per_pixel != 32) ||
        compression != 0)
    {
        return 0;
    }

    const int source_height =
        signed_height < 0 ? -signed_height : signed_height;

    if (source_height > SYSTEM_BMP_MAX_DIMENSION)
        return 0;

    const uint32_t row_bits =
        (uint32_t)source_width * bits_per_pixel;

    const uint32_t source_stride =
        ((row_bits + 31u) / 32u) * 4u;

    const uint64_t image_end =
        (uint64_t)pixel_offset +
        source_stride * (uint32_t)source_height;

    if (image_end > bmp_length)
        return 0;

    int target_width;
    int target_height;

    if (size > 0)
    {
        target_width = size;
        target_height = size;
    }
    else if (width <= 0 && height <= 0)
    {
        target_width = source_width;
        target_height = source_height;
    }
    else if (width <= 0)
    {
        target_height = height;
        target_width =
            (source_width * height) / source_height;
    }
    else if (height <= 0)
    {
        target_width = width;
        target_height =
            (source_height * width) / source_width;
    }
    else
    {
        target_width = width;
        target_height = height;
    }

    if (target_width <= 0 ||
        target_height <= 0 ||
        target_width > SYSTEM_BMP_MAX_DIMENSION ||
        target_height > SYSTEM_BMP_MAX_DIMENSION)
    {
        return 0;
    }

    const uint32_t bytes_per_pixel =
        bits_per_pixel / 8;

    for (int target_y = 0;
         target_y < target_height;
         target_y++)
    {
        const int source_y =
            (target_y * source_height) / target_height;

        const int stored_y =
            signed_height > 0
                ? source_height - 1 - source_y
                : source_y;

        const uint8_t* source_row =
            bmp +
            pixel_offset +
            (uint64_t)(uint32_t)stored_y * source_stride;

        for (int target_x = 0;
             target_x < target_width;
             target_x++)
        {
            const int source_x =
                (target_x * source_width) / target_width;

            const uint8_t* pixel =
                source_row +
                (uint32_t)source_x * bytes_per_pixel;

            system_bmp_row[target_x] =
                ((uint32_t)pixel[2] << 16) |
                ((uint32_t)pixel[1] << 8) |
                pixel[0];
        }

        gfx_draw_span(
            x,
            y + target_y,
            target_width,
            system_bmp_row
        );
    }

    gfx_present();

    return 1;
}

/* =========================
   Window helpers
   ========================= */

static int window_width(void)
{
    if (window_width_value > 0)
        return window_width_value;

    int width =
        (int)fb_info.framebuffer_width - 24;

    return width < WINDOW_MAX_WIDTH
        ? width
        : WINDOW_MAX_WIDTH;
}

static int window_height(void)
{
    if (window_height_value > 0)
        return window_height_value;

    int height =
        (int)fb_info.framebuffer_height -
        24 -
        TASKBAR_HEIGHT;

    return height < WINDOW_MAX_HEIGHT
        ? height
        : WINDOW_MAX_HEIGHT;
}

static void place_window(void)
{
    if (placed || !fb_info.has_framebuffer)
        return;

    window_x =
        ((int)fb_info.framebuffer_width -
         window_width()) / 2;

    window_y =
        ((int)fb_info.framebuffer_height -
         TASKBAR_HEIGHT -
         window_height()) / 2;

    if (window_y < 0)
        window_y = 0;

    placed = 1;
}

static int in_min_button(int x, int y)
{
    const int bx =
        window_x +
        window_width() -
        2 * CLOSE_SIZE -
        14;

    const int by =
        window_y + 4;

    return x >= bx &&
           x < bx + CLOSE_SIZE &&
           y >= by &&
           y < by + CLOSE_SIZE;
}

static int in_close_button(int x, int y)
{
    const int bx =
        window_x +
        window_width() -
        CLOSE_SIZE -
        8;

    const int by =
        window_y + 4;

    return x >= bx &&
           x < bx + CLOSE_SIZE &&
           y >= by &&
           y < by + CLOSE_SIZE;
}

static void paint_background(
    int x,
    int y,
    int w,
    int h
)
{
    if (background_painter)
        background_painter(x, y, w, h);
    else
        draw_rect(
            x,
            y,
            w,
            h,
            FALLBACK_BACKGROUND
        );
}

/* =========================
   Window drawing
   ========================= */

static void draw_chrome(const char* name)
{
    const int width = window_width();
    const int height = window_height();

    draw_rect(
        window_x,
        window_y,
        width,
        height,
        0x202020
    );

    draw_rect(
        window_x + 1,
        window_y + 1,
        width - 2,
        25,
        0x0078D4
    );

    draw_rect(
        window_x + 1,
        window_y + 26,
        width - 2,
        height - 27,
        0x000000
    );

    vga_draw_text_at(
        window_x + 10,
        window_y + 7,
        name,
        0xF0F4F8
    );

    const int bx =
        window_x +
        width -
        CLOSE_SIZE -
        8;

    draw_rect(
        bx,
        window_y + 4,
        CLOSE_SIZE,
        CLOSE_SIZE,
        0xD04040
    );

    vga_draw_text_at(
        bx + 4,
        window_y + 6,
        "X",
        0xFFFFFF
    );

    const int mx =
        window_x +
        width -
        2 * CLOSE_SIZE -
        14;

    draw_rect(
        mx,
        window_y + 4,
        CLOSE_SIZE,
        CLOSE_SIZE,
        0x3A6FD0
    );

    draw_rect(
        mx + 4,
        window_y + 4 + CLOSE_SIZE - 7,
        CLOSE_SIZE - 8,
        3,
        0xFFFFFF
    );
}

static void render_cell(int x, int y)
{
    const int origin_x =
        window_x +
        16 +
        x * FB_CELL_WIDTH;

    const int origin_y =
        window_y +
        50 +
        y * FB_CELL_HEIGHT;

    if (origin_x + FB_CELL_WIDTH >
            window_x + window_width() - 12 ||
        origin_y + FB_CELL_HEIGHT >
            window_y + window_height() - 12)
    {
        return;
    }

    const uint16_t cell =
        vga_get_cell(x, y);

    const unsigned char c =
        (unsigned char)(cell & 0xFF);

    const unsigned char attributes =
        (unsigned char)(cell >> 8);

    const uint32_t foreground =
        palette[attributes & 0x0F];

    const uint32_t background =
        palette[(attributes >> 4) & 0x0F];

    draw_rect(
        origin_x,
        origin_y,
        FB_CELL_WIDTH,
        FB_CELL_HEIGHT,
        background
    );

    const uint8_t* glyph =
        font_glyph(c);

    if (glyph == NULL)
        return;

    for (int row = 0; row < 7; row++)
    {
        for (int column = 0; column < 5; column++)
        {
            if (glyph[row] &
                (1u << (4 - column)))
            {
                draw_rect(
                    origin_x + column * 2,
                    origin_y + row * 2 + 1,
                    2,
                    2,
                    foreground
                );
            }
        }
    }
}

static void render_all(void)
{
    for (int y = 0; y < VGA_HEIGHT; y++)
    {
        for (int x = 0; x < VGA_WIDTH; x++)
        {
            render_cell(x, y);
        }
    }
}

/* =========================
   Window API
   ========================= */

void window_set_background_painter(
    void (*paint)(int x, int y, int w, int h)
)
{
    background_painter = paint;
}

void window_open(const char* title)
{
    if (window_open_state ||
        !fb_info.has_framebuffer)
        return;

    if (title != NULL)
        window_title = title;

    place_window();

    window_open_state = 1;
    minimized = 0;

    draw_chrome(window_title);
    render_all();

    gfx_present();
    taskbar_refresh();
}

void window_close(const char* title)
{
    (void)title;

    if (!window_open_state)
        return;

    window_open_state = 0;
    minimized = 0;
    dragging = 0;

    paint_background(
        window_x - 2,
        window_y - 2,
        window_width() + 8,
        window_height() + 10
    );

    gfx_present();
    taskbar_refresh();
}

int window_is_open(void)
{
    return window_open_state;
}

void window_redraw(const char* title)
{
    if (!fb_info.has_framebuffer)
        return;

    if (window_open_state && minimized)
        return;

    if (!window_open_state)
    {
        window_open(title);
        return;
    }

    draw_chrome(title);
    render_all();

    gfx_present();
}

int window_is_minimized(void)
{
    return window_open_state && minimized;
}

void window_minimize(void)
{
    if (!window_open_state || minimized)
        return;

    minimized = 1;
    dragging = 0;

    paint_background(
        window_x - 2,
        window_y - 2,
        window_width() + 8,
        window_height() + 10
    );

    gfx_present();
    taskbar_refresh();
}

void window_restore(const char* title)
{
    if (!window_open_state || !minimized)
        return;

    if (title != NULL)
        window_title = title;

    minimized = 0;

    draw_chrome(window_title);
    render_all();

    gfx_present();
    taskbar_refresh();
}

void window_paint(void)
{
    if (!fb_info.has_framebuffer ||
        !window_open_state ||
        minimized)
        return;

    draw_chrome(window_title);
    render_all();
}

/* =========================
   VGA
   ========================= */

static void on_vga_event(
    int event,
    int x,
    int y
)
{
    if (!fb_info.has_framebuffer)
        return;

    if (!window_open_state)
    {
        window_open(window_title);
        return;
    }

    if (minimized)
        return;

    if (event == VGA_EV_CELL)
        render_cell(x, y);
    else
        render_all();

    menu_redraw_if_open();
    gfx_present();
}

void window_system_init(void)
{
    vga_set_listener(on_vga_event);
}

/* =========================
   Mouse
   ========================= */

void window_mouse(
    int mouse_x,
    int mouse_y,
    int buttons
)
{
    const int left =
        (buttons & 1) != 0;

    const int pressed =
        left && !(prev_buttons & 1);

    prev_buttons = buttons;

    if (!window_open_state || minimized)
        return;

    if (pressed)
    {
        if (in_close_button(mouse_x, mouse_y))
        {
            window_close(window_title);
            return;
        }

        if (in_min_button(mouse_x, mouse_y))
        {
            window_minimize();
            return;
        }

        if (mouse_x >= window_x + 1 &&
            mouse_x <
                window_x +
                window_width() -
                2 * CLOSE_SIZE -
                20 &&
            mouse_y >= window_y &&
            mouse_y <
                window_y +
                WINDOW_TITLE_HEIGHT)
        {
            dragging = 1;

            drag_dx =
                mouse_x - window_x;

            drag_dy =
                mouse_y - window_y;
        }
    }

    if (!left)
        dragging = 0;

    if (!dragging)
        return;

    const int old_x = window_x;
    const int old_y = window_y;

    window_x =
        mouse_x - drag_dx;

    window_y =
        mouse_y - drag_dy;

    const int max_x =
        (int)fb_info.framebuffer_width -
        window_width();

    const int max_y =
        (int)fb_info.framebuffer_height -
        TASKBAR_HEIGHT -
        window_height();

    if (window_x < 0)
        window_x = 0;

    if (window_y < 0)
        window_y = 0;

    if (window_x > max_x)
        window_x = max_x;

    if (window_y > max_y)
        window_y = max_y;

    if (window_y < 0)
        window_y = 0;

    if (window_x != old_x ||window_y != old_y){
        paint_background(
            old_x,
            old_y,
            window_width() + 7,
            window_height() + 9
        );

        draw_chrome(window_title);
        render_all();

        menu_redraw_if_open();
        gfx_present();
    }
}


void create_window(
    int x,
    int y,
    int width,
    int height,
    const char* title
)
{
    if (!fb_info.has_framebuffer)
        return;

    if (width <= 0)
        width = 400;

    if (height <= 0)
        height = 300;

    if (width > WINDOW_MAX_WIDTH)
        width = WINDOW_MAX_WIDTH;

    if (height > WINDOW_MAX_HEIGHT)
        height = WINDOW_MAX_HEIGHT;

    if (x < 0)
        x = 0;

    if (y < 0)
        y = 0;

    window_x = x;
    window_y = y;

    window_width_value = width;
    window_height_value = height;

    if (title != NULL)
        window_title = title;
    else
        window_title = "Window";

    placed = 1;
    minimized = 0;
    window_open_state = 1;

    draw_chrome(window_title);
    render_all();

    gfx_present();
    taskbar_refresh();
}