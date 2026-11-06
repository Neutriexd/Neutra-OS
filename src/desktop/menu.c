#include <stdint.h>

#include "graphics.h"
#include "vga.h"
#include "shell_window.h"
#include "explorer.h"
#include "editor.h"
#include "developer_.h"
#include "browser.h"
#include "program.h"
#include "menu.h"
#include "taskbar_ui.h"

void gfx_present(void);
void redraw_desk_region(int x, int y, int width, int height);
void init_taskbar(void);

#define TASKBAR_HEIGHT 36
#define TASKBAR_FACE   0xA09C96

#define BTN_X 2
#define BTN_W 100
#define BTN_H 30

#define MENU_W   320
#define LEFT_W   170
#define HEADER_H 56
#define ROW_H    48
#define BODY_ROWS 8
#define FOOTER_H 44
#define BODY_H   (ROW_H * BODY_ROWS)
#define MENU_H   (HEADER_H + BODY_H + FOOTER_H)

#define COL_FRAME    0x1941A5
#define COL_BLUE     0x245EDC
#define COL_LEFT     0xFFFFFF
#define COL_RIGHT    0xD3E5FA
#define COL_SEP      0x95BDE7
#define COL_HOVER    0x316AC5

enum { ACT_NONE = 0, ACT_TERMINAL, ACT_EXPLORER, ACT_EDITOR, ACT_DEVELOPER, ACT_BROWSER, ACT_POWEROFF };

typedef struct {
    const char* label;
    int action;
} MenuItem;

static const MenuItem left_items[] = {
    {"Terminal", ACT_TERMINAL},
    {"Explorer", ACT_EXPLORER},
    {"Editor", ACT_EDITOR},
    {"Developer", ACT_DEVELOPER},
    {"Browser", ACT_BROWSER},
};
static char prog_names[8][13];
static int prog_count;
static int pending_program;
static char pending_program_name[13];
#define LEFT_COUNT  ((int)(sizeof(left_items) / sizeof(left_items[0])))
#define RIGHT_COUNT prog_count

#define HOVER_NONE  (-1)
#define HOVER_RIGHT 10
#define HOVER_POWER 20

static int menu_open_flag;
static int hover = HOVER_NONE;
static int pending_explorer;
static int pending_editor;
static int pending_developer;
static int pending_browser;
static int prev_buttons;

static int taskbar_top(void) {
    return (int)fb_info.framebuffer_height - TASKBAR_HEIGHT;
}

static int menu_left(void) {
    return 0;
}

static int menu_top(void) {
    return taskbar_top() - MENU_H;
}

static int in_rect(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

static int in_menu(int x, int y) {
    return in_rect(x, y, menu_left(), menu_top(), MENU_W, MENU_H);
}

static int in_start_button(int x, int y) {
    return in_rect(x, y, BTN_X, taskbar_top() + 3, BTN_W, BTN_H);
}

static int power_button_x(void) { return menu_left() + MENU_W - 150; }
static int power_button_y(void) { return menu_top() + MENU_H - FOOTER_H + 6; }

static int hit_item(int x, int y) {
    if (!in_menu(x, y)) return HOVER_NONE;
    const int rx = x - menu_left();
    const int ry = y - menu_top();

    if (ry >= HEADER_H && ry < HEADER_H + BODY_H) {
        const int row = (ry - HEADER_H) / ROW_H;
        if (rx < LEFT_W) {
            if (row < LEFT_COUNT) return row;
        } else if (row < RIGHT_COUNT) {
            return HOVER_RIGHT + row;
        }
        return HOVER_NONE;
    }
    if (in_rect(x, y, power_button_x(), power_button_y(), 140, FOOTER_H - 12)) return HOVER_POWER;
    return HOVER_NONE;
}

static void draw_triangle_logo(int x, int y, int w, int h) {
    static const uint32_t bands[3] = {0xF25022, 0x7FBA00, 0x00A4EF};
    for (int row = 0; row < h; row++) {
        const int half = h > 1 ? (row * (w - 1)) / (2 * (h - 1)) : 0;
        const int span = 2 * half + 1;
        const int left = x + (w - 1) / 2 - half;
        draw_rect(left, y + row, span, 1, bands[(row * 3) / h]);
    }
}

static void draw_bold_text(int x, int y, const char* text, uint32_t color) {
    vga_draw_text_at(x, y, text, color);
    vga_draw_text_at(x + 1, y, text, color);
}

static void draw_start_button(int pressed) {
    const int bx = BTN_X;
    const int by = taskbar_top() + 3;
    const uint32_t hi = 0xFFFFFF;
    const uint32_t hi2 = 0xD8D4CE;
    const uint32_t lo = 0x000000;
    const uint32_t lo2 = 0x6B6862;

    draw_rect(bx, by, BTN_W, BTN_H, TASKBAR_FACE);
    draw_rect(bx, by, BTN_W, 1, pressed ? lo : hi);
    draw_rect(bx, by, 1, BTN_H, pressed ? lo : hi);
    draw_rect(bx, by + BTN_H - 1, BTN_W, 1, pressed ? hi : lo);
    draw_rect(bx + BTN_W - 1, by, 1, BTN_H, pressed ? hi : lo);
    draw_rect(bx + 1, by + 1, BTN_W - 2, 1, pressed ? lo2 : hi2);
    draw_rect(bx + 1, by + 1, 1, BTN_H - 2, pressed ? lo2 : hi2);
    draw_rect(bx + 1, by + BTN_H - 2, BTN_W - 2, 1, pressed ? hi2 : lo2);
    draw_rect(bx + BTN_W - 2, by + 1, 1, BTN_H - 2, pressed ? hi2 : lo2);

    const int offset = pressed ? 1 : 0;
    draw_triangle_logo(bx + 9 + offset, by + 6 + offset, 21, 18);
    draw_bold_text(bx + 40 + offset, by + 8 + offset, "Start", 0x000000);
}

static void draw_icon(int x, int y, int action) {
    draw_rect(x, y, 28, 28, 0x1941A5);
    if (action == ACT_TERMINAL) {
        draw_rect(x + 1, y + 1, 26, 26, 0x000000);
        vga_draw_text_at(x + 8, y + 7, ">", 0x55FF55);
    } else if (action == ACT_EXPLORER) {
        draw_rect(x + 1, y + 1, 26, 26, 0xF2C14E);
        draw_rect(x + 3, y + 5, 10, 4, 0xD9A520);
        draw_rect(x + 3, y + 9, 22, 14, 0xFFE08A);
    } else if (action == ACT_EDITOR) {
        draw_rect(x + 1, y + 1, 26, 26, 0xFFFFFF);
        draw_rect(x + 5, y + 6, 18, 2, 0x4C6FD0);
        draw_rect(x + 5, y + 11, 18, 2, 0x4C6FD0);
        draw_rect(x + 5, y + 16, 12, 2, 0x4C6FD0);
        draw_rect(x + 5, y + 21, 15, 2, 0x4C6FD0);
    } else if (action == ACT_BROWSER) {
        draw_rect(x + 1, y + 1, 26, 26, 0x2A8AC8);
        draw_rect(x + 5, y + 5, 18, 3, 0xFFFFFF);
        draw_rect(x + 5, y + 10, 18, 14, 0xE8F4FF);
    } else if (action == ACT_DEVELOPER) {
        draw_rect(x + 1, y + 1, 26, 26, 0x1E3A8A);
        vga_draw_text_at(x + 2, y + 7, "{}", 0xFFFFFF);
    } else if (action == ACT_POWEROFF) {
        draw_rect(x + 1, y + 1, 26, 26, 0xD04030);
        vga_draw_text_at(x + 9, y + 7, "|", 0xFFFFFF);
    } else {
        draw_rect(x + 1, y + 1, 26, 26, 0x4C8BD8);
    }
}

static void draw_item(int x, int y, int w, const MenuItem* item, int hovered) {
    if (hovered) draw_rect(x, y, w, ROW_H, COL_HOVER);
    draw_icon(x + 8, y + 10, item->action);
    vga_draw_text_at(x + 46, y + 17, item->label, hovered ? 0xFFFFFF : 0x000000);
}

static void draw_menu(void) {
    const int x = menu_left();
    const int y = menu_top();

    draw_rect(x, y, MENU_W, MENU_H, COL_FRAME);

    draw_rect(x + 2, y + 2, MENU_W - 4, HEADER_H - 2, COL_BLUE);
    draw_rect(x + 10, y + 9, 38, 38, 0xFFFFFF);
    draw_triangle_logo(x + 19, y + 15, 20, 26);
    draw_bold_text(x + 58, y + 21, "Neutra", 0xFFFFFF);

    draw_rect(x + 2, y + HEADER_H, LEFT_W - 2, BODY_H, COL_LEFT);
    draw_rect(x + LEFT_W, y + HEADER_H, MENU_W - LEFT_W - 2, BODY_H, COL_RIGHT);
    draw_rect(x + LEFT_W, y + HEADER_H, 1, BODY_H, COL_SEP);

    for (int i = 0; i < LEFT_COUNT; i++) {
        draw_item(x + 2, y + HEADER_H + i * ROW_H, LEFT_W - 2, &left_items[i], hover == i);
    }
    for (int i = 0; i < RIGHT_COUNT; i++) {
        const int item_y = y + HEADER_H + i * ROW_H;
        if (hover == HOVER_RIGHT + i) draw_rect(x + LEFT_W + 1, item_y, MENU_W - LEFT_W - 3, ROW_H, COL_HOVER);
        program_draw_icon(x + LEFT_W + 9, item_y + 10, 28);
        char shown[9];
        int n = 0;
        while (prog_names[i][n] && prog_names[i][n] != '.' && n < 8) {
            shown[n] = prog_names[i][n];
            n++;
        }
        shown[n] = '\0';
        vga_draw_text_at(x + LEFT_W + 46, item_y + 17, shown, hover == HOVER_RIGHT + i ? 0xFFFFFF : 0x000000);
    }
    if (RIGHT_COUNT == 0) vga_draw_text_at(x + LEFT_W + 10, y + HEADER_H + 17, "NO PROGRAMS", 0x808080);

    draw_rect(x + 2, y + MENU_H - FOOTER_H, MENU_W - 4, FOOTER_H - 2, COL_BLUE);
    const int px = power_button_x();
    const int py = power_button_y();
    if (hover == HOVER_POWER) draw_rect(px, py, 140, FOOTER_H - 12, 0x4A7FE6);
    draw_icon(px + 6, py + 2, ACT_POWEROFF);
    draw_bold_text(px + 42, py + 10, "Turn Off", 0xFFFFFF);
}

int menu_is_open(void) {
    return menu_open_flag;
}

void menu_init(void) {
    if (!fb_info.has_framebuffer) return;
    draw_start_button(menu_open_flag);
}

void menu_open(void) {
    if (menu_open_flag || !fb_info.has_framebuffer) return;
    menu_open_flag = 1;
    hover = HOVER_NONE;
    prog_count = program_list_files(prog_names, 8);
    draw_menu();
    draw_start_button(1);
    gfx_present();
}

void menu_close(void) {
    if (!menu_open_flag) return;
    menu_open_flag = 0;
    hover = HOVER_NONE;
    redraw_desk_region(menu_left(), menu_top(), MENU_W, MENU_H);
    shell_paint();
    draw_start_button(0);
    gfx_present();
}

void menu_toggle(void) {
    if (menu_open_flag) menu_close();
    else menu_open();
}

void menu_redraw_if_open(void) {
    if (menu_open_flag) draw_menu();
}

static inline void power_outw(uint16_t port, uint16_t value) {
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

static void power_off(void) {
    power_outw(0x604, 0x2000);
    power_outw(0xB004, 0x2000);
    power_outw(0x4004, 0x3400);
    __asm__ volatile("cli");
    while (1) __asm__ volatile("hlt");
}

static void activate(int item) {
    int action = ACT_NONE;
    if (item >= 0 && item < LEFT_COUNT) action = left_items[item].action;
    else if (item >= HOVER_RIGHT && item < HOVER_RIGHT + RIGHT_COUNT) {
        int n = 0;
        while (prog_names[item - HOVER_RIGHT][n] && n < 12) {
            pending_program_name[n] = prog_names[item - HOVER_RIGHT][n];
            n++;
        }
        pending_program_name[n] = '\0';
        menu_close();
        pending_program = 1;
        return;
    } else if (item == HOVER_POWER) action = ACT_POWEROFF;

    if (action == ACT_POWEROFF) {
        power_off();
        return;
    }
    menu_close();
    if (action == ACT_TERMINAL) {
        if (!shell_is_open()) shell_open();
        else if (shell_is_minimized()) shell_restore();
        else shell_minimize();
    } else if (action == ACT_EXPLORER) {
        pending_explorer = 1;
    } else if (action == ACT_EDITOR) {
        pending_editor = 1;
    } else if (action == ACT_DEVELOPER) {
        pending_developer = 1;
    } else if (action == ACT_BROWSER) {
        pending_browser = 1;
    }
}

void menu_request_explorer(void) {
    pending_explorer = 1;
}

void menu_request_editor(void) {
    pending_editor = 1;
}

void menu_request_developer(void) {
    pending_developer = 1;
}

void menu_restore_screen(void) {
    if (!fb_info.has_framebuffer) return;
    redraw_desk_region(0, 0, (int)fb_info.framebuffer_width, (int)fb_info.framebuffer_height);
    shell_paint();
    taskbar_refresh();
}

void menu_run_pending(void) {
    if (pending_explorer) {
        pending_explorer = 0;
        explorer_run();
        menu_restore_screen();
    }
    if (pending_editor) {
        pending_editor = 0;
        editor_run();
        menu_restore_screen();
    }
    if (pending_developer) {
        pending_developer = 0;
        developer_run();
        menu_restore_screen();
    }
    if (pending_browser) {
        pending_browser = 0;
        browser_run();
        menu_restore_screen();
    }
    if (pending_program) {
        pending_program = 0;
        char path[16];
        path[0] = '/';
        int n = 1;
        for (int i = 0; pending_program_name[i] && n < 15; i++) path[n++] = pending_program_name[i];
        path[n] = '\0';
        program_run_file(path);
        menu_restore_screen();
    }
}

int menu_mouse(int mouse_x, int mouse_y, int buttons) {
    const int left = (buttons & 1) != 0;
    const int pressed = left && !(prev_buttons & 1);
    prev_buttons = buttons;
    if (!fb_info.has_framebuffer) return 0;

    if (menu_open_flag) {
        const int item = hit_item(mouse_x, mouse_y);
        if (item != hover) {
            hover = item;
            draw_menu();
            gfx_present();
        }
    }
    if (!pressed) return 0;

    if (in_start_button(mouse_x, mouse_y)) {
        menu_toggle();
        return 1;
    }
    if (menu_open_flag) {
        if (in_menu(mouse_x, mouse_y)) {
            if (hover != HOVER_NONE) activate(hover);
        } else {
            menu_close();
        }
        return 1;
    }
    return 0;
}

void menu_on_scancode(uint8_t scancode) {
    static int extended;
    static int ctrl;

    if (scancode == 0xE0) {
        extended = 1;
        return;
    }
    if (extended) {
        extended = 0;
        if (scancode == 0x5B || scancode == 0x5C) menu_toggle();
        else if (scancode == 0x1D) ctrl = 1;
        else if (scancode == 0x9D) ctrl = 0;
        return;
    }
    if (scancode == 0x1D) ctrl = 1;
    else if (scancode == 0x9D) ctrl = 0;
    else if (scancode == 0x01) {
        if (ctrl) menu_toggle();
        else if (menu_open_flag) menu_close();
    }
}