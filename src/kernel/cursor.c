#include <stdint.h>

#include "graphics.h"
#include "desktop.h"
#include "vga.h"
#include "shell_window.h"
#include "menu.h"
#include "explorer.h"
#include "editor.h"
#include "developer_.h"
#include "taskbar_ui.h"
#include "program.h"

void gfx_set_present_hook(void (*hook)(void));
void gfx_restore_rect(int x, int y, int w, int h);
void gfx_fb_putpixel(int x, int y, uint32_t color);

#define CURSOR_WIDTH 11
#define CURSOR_HEIGHT 16

static const char* const cursor_shape[CURSOR_HEIGHT] = {
    "X          ",
    "XX         ",
    "X.X        ",
    "X..X       ",
    "X...X      ",
    "X....X     ",
    "X.....X    ",
    "X......X   ",
    "X.......X  ",
    "X........X ",
    "X.....XXXXX",
    "X..X..X    ",
    "X.X X..X   ",
    "XX  X..X   ",
    "X    X..X  ",
    "     XXXX  "
};

static int initialized;
static int mouse_x;
static int mouse_y;
static int drawn;
static int drawn_x;
static int drawn_y;

static void erase_cursor(void) {
    if (!drawn) return;
    gfx_restore_rect(drawn_x, drawn_y, CURSOR_WIDTH, CURSOR_HEIGHT);
    drawn = 0;
}

static void draw_cursor(void) {
    if (!fb_info.has_framebuffer) return;
    erase_cursor();
    for (int row = 0; row < CURSOR_HEIGHT; row++) {
        for (int column = 0; column < CURSOR_WIDTH; column++) {
            const char pixel = cursor_shape[row][column];
            if (pixel == 'X') gfx_fb_putpixel(mouse_x + column, mouse_y + row, 0x000000);
            else if (pixel == '.') gfx_fb_putpixel(mouse_x + column, mouse_y + row, 0xFFFFFF);
        }
    }
    drawn_x = mouse_x;
    drawn_y = mouse_y;
    drawn = 1;
}

static void cursor_present_hook(void) {
    draw_cursor();
}

void cursor_init(void) {
    if (!fb_info.has_framebuffer) return;
    if (!initialized) {
        mouse_x = (int)fb_info.framebuffer_width / 2;
        mouse_y = (int)fb_info.framebuffer_height / 2;
        initialized = 1;
    }
    gfx_set_present_hook(cursor_present_hook);
    draw_cursor();
}

void pointer_dispatch(int mouse_x, int mouse_y, int buttons) {
    static int suppress;
    if (explorer_is_active() || editor_is_active() || developer_is_active()) return;

    const int left = (buttons & 1) != 0;
    const int by_menu = menu_mouse(mouse_x, mouse_y, buttons);
    const int by_taskbar = taskbar_mouse(mouse_x, mouse_y, buttons);
    const int by_programs = program_windows_mouse(mouse_x, mouse_y, buttons);
    const int by_desktop = desktop_pointer(mouse_x, mouse_y, buttons);
    if ((by_menu || by_taskbar || by_programs || by_desktop) && left) suppress = 1;
    if (!left) suppress = 0;
    shell_window_mouse(mouse_x, mouse_y, suppress ? 0 : buttons);
}

void vga_mouse_update(int delta_x, int delta_y, uint8_t buttons) {
    if (!fb_info.has_framebuffer) return;
    if (!initialized) cursor_init();

    erase_cursor();
    mouse_x += delta_x;
    mouse_y += delta_y;
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_x >= (int)fb_info.framebuffer_width) mouse_x = (int)fb_info.framebuffer_width - 1;
    if (mouse_y >= (int)fb_info.framebuffer_height) mouse_y = (int)fb_info.framebuffer_height - 1;

    pointer_dispatch(mouse_x, mouse_y, buttons);
    draw_cursor();
}
