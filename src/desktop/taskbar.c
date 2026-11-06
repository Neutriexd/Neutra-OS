#include "taskbar.h"
#include "kernel.h"
#include "idt.h"
#include <stdint.h>
#include "graphics.h"
#include "vga.h"
#include "menu.h"
#include "shell_window.h"
#include "explorer.h"
#include "editor.h"
#include "developer_.h"
#include "program.h"
#include "taskbar_ui.h"

void gfx_present(void);

#define TASKBAR_HEIGHT 36
#define TASKBAR_FACE   0xA09C96
#define APP_BTN_X   108
#define APP_BTN_MAX 150
#define APP_BTN_MIN 60
#define APP_BTN_H   30
#define APP_BTN_GAP 4

enum { K_TERMINAL = 0, K_EXPLORER = 1, K_EDITOR = 2, K_DEVELOPER = 3, K_PROGRAM = 4 };

typedef struct {
    int kind;
    int slot;
} Entry;

static int collect(Entry* entries) {
    int n = 0;
    if (shell_is_open()) { entries[n].kind = K_TERMINAL; entries[n++].slot = 0; }
    if (explorer_is_open()) { entries[n].kind = K_EXPLORER; entries[n++].slot = 0; }
    if (editor_is_open()) { entries[n].kind = K_EDITOR; entries[n++].slot = 0; }
    if (developer_is_open()) { entries[n].kind = K_DEVELOPER; entries[n++].slot = 0; }
    for (int slot = 0; slot < PROGRAM_WINDOW_MAX; slot++) {
        if (program_window_used(slot)) { entries[n].kind = K_PROGRAM; entries[n++].slot = slot; }
    }
    return n;
}

static int taskbar_top(void) {
    return (int)fb_info.framebuffer_height - TASKBAR_HEIGHT;
}

static int button_width(int count) {
    if (count <= 0) return APP_BTN_MAX;
    int width = ((int)fb_info.framebuffer_width - APP_BTN_X - 8) / count - APP_BTN_GAP;
    if (width > APP_BTN_MAX) width = APP_BTN_MAX;
    if (width < APP_BTN_MIN) width = APP_BTN_MIN;
    return width;
}

static int in_rect(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

static int visible(const Entry* entry) {
    if (entry->kind == K_TERMINAL) return shell_is_open() && !shell_is_minimized();
    if (entry->kind == K_EXPLORER) return explorer_is_open() && !explorer_is_minimized();
    if (entry->kind == K_EDITOR) return editor_is_open() && !editor_is_minimized();
    if (entry->kind == K_DEVELOPER) return developer_is_open() && !developer_is_minimized();
    return !program_window_minimized(entry->slot);
}

static void draw_icon(const Entry* entry, int x, int y) {
    if (entry->kind == K_PROGRAM) {
        program_draw_icon(x, y, 18);
        return;
    }
    draw_rect(x, y, 18, 18, 0x1941A5);
    if (entry->kind == K_TERMINAL) {
        draw_rect(x + 1, y + 1, 16, 16, 0x000000);
        vga_draw_text_at(x + 4, y + 2, ">", 0x55FF55);
    } else if (entry->kind == K_EXPLORER) {
        draw_rect(x + 1, y + 1, 16, 16, 0xF2C14E);
        draw_rect(x + 2, y + 3, 7, 3, 0xD9A520);
        draw_rect(x + 2, y + 6, 14, 9, 0xFFE08A);
    } else if (entry->kind == K_EDITOR) {
        draw_rect(x + 1, y + 1, 16, 16, 0xFFFFFF);
        draw_rect(x + 3, y + 4, 12, 2, 0x4C6FD0);
        draw_rect(x + 3, y + 8, 12, 2, 0x4C6FD0);
        draw_rect(x + 3, y + 12, 8, 2, 0x4C6FD0);
    } else {
        draw_rect(x + 1, y + 1, 16, 16, 0x1E3A8A);
        vga_draw_text_at(x + 4, y + 2, "#", 0xFFFFFF);
    }
}

static void draw_button(const Entry* entry, int bx, int width) {
    const int by = taskbar_top() + 3;
    const int pressed = visible(entry);
    draw_rect(bx, by, width, APP_BTN_H, pressed ? 0xC8C4BE : TASKBAR_FACE);
    draw_rect(bx, by, width, 1, pressed ? 0x000000 : 0xFFFFFF);
    draw_rect(bx, by, 1, APP_BTN_H, pressed ? 0x000000 : 0xFFFFFF);
    draw_rect(bx, by + APP_BTN_H - 1, width, 1, pressed ? 0xFFFFFF : 0x000000);
    draw_rect(bx + width - 1, by, 1, APP_BTN_H, pressed ? 0xFFFFFF : 0x000000);
    draw_rect(bx + 1, by + 1, width - 2, 1, pressed ? 0x6B6862 : 0xD8D4CE);
    draw_rect(bx + 1, by + 1, 1, APP_BTN_H - 2, pressed ? 0x6B6862 : 0xD8D4CE);
    draw_rect(bx + 1, by + APP_BTN_H - 2, width - 2, 1, pressed ? 0xD8D4CE : 0x6B6862);
    draw_rect(bx + width - 2, by + 1, 1, APP_BTN_H - 2, pressed ? 0xD8D4CE : 0x6B6862);

    const int offset = pressed ? 1 : 0;
    draw_icon(entry, bx + 8 + offset, by + 6 + offset);
    const char* label = "Terminal";
    if (entry->kind == K_EXPLORER) label = "Explorer";
    else if (entry->kind == K_EDITOR) label = "Editor";
    else if (entry->kind == K_DEVELOPER) label = "Developer";
    else if (entry->kind == K_PROGRAM) label = program_window_title(entry->slot);
    char shown[24];
    const int max_chars = (width - 34) / 12;
    int n = 0;
    while (label[n] && n < max_chars && n < 23) {
        shown[n] = label[n];
        n++;
    }
    shown[n] = '\0';
    vga_draw_text_at(bx + 34 + offset, by + 8 + offset, shown, 0x000000);
}

static void draw_taskbar(void) {
    draw_rect(0, taskbar_top(), (int)fb_info.framebuffer_width, TASKBAR_HEIGHT, TASKBAR_FACE);
    menu_init();
    Entry entries[8];
    const int count = collect(entries);
    const int width = button_width(count);
    for (int i = 0; i < count; i++) draw_button(&entries[i], APP_BTN_X + i * (width + APP_BTN_GAP), width);
}

void taskbar_refresh(void) {
    if (!fb_info.has_framebuffer) return;
    draw_taskbar();
    gfx_present();
}

void init_taskbar(void) {
    taskbar_refresh();
}

static int kind_hit(int kind, int mouse_x, int mouse_y) {
    Entry entries[8];
    const int count = collect(entries);
    const int width = button_width(count);
    for (int i = 0; i < count; i++) {
        if (entries[i].kind == kind &&
            in_rect(mouse_x, mouse_y, APP_BTN_X + i * (width + APP_BTN_GAP), taskbar_top() + 3, width, APP_BTN_H)) return 1;
    }
    return 0;
}

int taskbar_explorer_button_hit(int mouse_x, int mouse_y) { return kind_hit(K_EXPLORER, mouse_x, mouse_y); }
int taskbar_editor_button_hit(int mouse_x, int mouse_y) { return kind_hit(K_EDITOR, mouse_x, mouse_y); }
int taskbar_developer_button_hit(int mouse_x, int mouse_y) { return kind_hit(K_DEVELOPER, mouse_x, mouse_y); }

int taskbar_mouse(int mouse_x, int mouse_y, int buttons) {
    static int prev_buttons;
    const int pressed = (buttons & 1) && !(prev_buttons & 1);
    prev_buttons = buttons;
    if (!pressed || !fb_info.has_framebuffer) return 0;

    Entry entries[8];
    const int count = collect(entries);
    const int width = button_width(count);
    for (int i = 0; i < count; i++) {
        if (!in_rect(mouse_x, mouse_y, APP_BTN_X + i * (width + APP_BTN_GAP), taskbar_top() + 3, width, APP_BTN_H)) continue;
        const int kind = entries[i].kind;
        if (kind == K_TERMINAL) {
            if (shell_is_minimized()) shell_restore();
            else shell_minimize();
        } else if (kind == K_EXPLORER) {
            if (explorer_is_active()) explorer_request_minimize();
            else if (explorer_is_minimized()) menu_request_explorer();
        } else if (kind == K_EDITOR) {
            if (editor_is_active()) editor_request_minimize();
            else if (editor_is_minimized()) menu_request_editor();
        } else if (kind == K_DEVELOPER) {
            if (developer_is_active()) developer_request_minimize();
            else if (developer_is_minimized()) menu_request_developer();
        } else {
            program_window_toggle(entries[i].slot);
        }
        return 1;
    }
    return 0;
}

void check_menu_click(void) {
}

void taskbar_poll(void) {
    check_menu_click();
}

void run_taskbar(void) {
    keep_alive();
}

void keep_alive(void) {
    while (1) {
        check_menu_click();
    }
}