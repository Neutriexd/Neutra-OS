#include <stdint.h>
#include <stddef.h>
#include "graphics.h"
#include "vga.h"
#include "kernel.h"
#include "idt.h"
#include "fat.h"
#include "font.h"
#include "cde.h"
#include "bmp.h"
#include "program.h"
#include "program.icon.h"
#include "shell_window.h"
#include "taskbar_ui.h"
#include "developer_.h"

void gfx_present(void);
void gfx_set_clip(int x, int y, int w, int h);
void gfx_reset_clip(void);
void gfx_draw_span(int x, int y, int count, const uint32_t* pixels);
void redraw_desk_region(int x, int y, int width, int height);

extern DISK kernel_fat_disk;
extern int kernel_fat_mounted;

#define PW_CW 480
#define PW_CH 320
#define CELL_W 12
#define CELL_H 16
#define MARGIN 4
#define TASKBAR 36
#define IMAGE_MAX 16384

typedef struct {
    int used;
    int minimized;
    int x, y, w, h;
    char title[40];
    uint32_t color;
    uint32_t background;
    int cx, cy;
} ProgWin;

static const uint32_t palette[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF
};

static ProgWin pw[PROGRAM_WINDOW_MAX];
static uint32_t pw_canvas[PROGRAM_WINDOW_MAX][PW_CW * PW_CH];
static int order[PROGRAM_WINDOW_MAX];
static int order_count;
static int dirty[PROGRAM_WINDOW_MAX];
static int console_slot = -1;
static int owner_mask;
static int stop_flag;
static int pending_key;
static char prog_title[40];
static uint8_t prog_image[IMAGE_MAX];

static int ow(int s) { return pw[s].w + 8; }
static int oh(int s) { return pw[s].h + 30; }

static void z_remove(int s) {
    int n = 0;
    for (int i = 0; i < order_count; i++) {
        if (order[i] != s) order[n++] = order[i];
    }
    order_count = n;
}

static void z_push(int s) {
    z_remove(s);
    order[order_count++] = s;
}

static void canvas_fill(int s, uint32_t color) {
    uint32_t* p = pw_canvas[s];
    for (int i = 0; i < PW_CW * PW_CH; i++) p[i] = color;
}

static void cv_pixel(int s, int x, int y, uint32_t color) {
    if (x < 0 || y < 0 || x >= pw[s].w || y >= pw[s].h) return;
    pw_canvas[s][y * PW_CW + x] = color;
}

static void cv_rect(int s, int x, int y, int w, int h, uint32_t color) {
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > pw[s].w ? pw[s].w : x + w;
    int y1 = y + h > pw[s].h ? pw[s].h : y + h;
    for (int yy = y0; yy < y1; yy++) {
        for (int xx = x0; xx < x1; xx++) pw_canvas[s][yy * PW_CW + xx] = color;
    }
}

static void cv_line(int s, int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = x1 >= x0 ? x1 - x0 : x0 - x1;
    int dy = y1 >= y0 ? y0 - y1 : y1 - y0;
    const int sx = x0 < x1 ? 1 : -1;
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (int guard = 0; guard < 4000; guard++) {
        cv_pixel(s, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        const int doubled = 2 * error;
        if (doubled >= dy) {
            error += dy;
            x0 += sx;
        }
        if (doubled <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

static void cv_glyph(int s, int x, int y, char c, uint32_t color) {
    const uint8_t* glyph = font_glyph((unsigned char)c);
    if (glyph == 0) return;
    for (int row = 0; row < 7; row++) {
        for (int column = 0; column < 5; column++) {
            if (glyph[row] & (1u << (4 - column))) cv_rect(s, x + column * 2, y + row * 2, 2, 2, color);
        }
    }
}

static void cv_scroll(int s) {
    for (int y = 0; y + CELL_H < pw[s].h; y++) {
        for (int x = 0; x < pw[s].w; x++) pw_canvas[s][y * PW_CW + x] = pw_canvas[s][(y + CELL_H) * PW_CW + x];
    }
    cv_rect(s, 0, pw[s].h - CELL_H, pw[s].w, CELL_H, pw[s].background);
}

static void cv_newline(int s) {
    pw[s].cx = MARGIN;
    pw[s].cy += CELL_H;
    while (pw[s].cy + CELL_H > pw[s].h) {
        cv_scroll(s);
        pw[s].cy -= CELL_H;
    }
}

static void cv_putc(int s, char c) {
    if (c == '\n') {
        cv_newline(s);
        return;
    }
    if (c == '\r') return;
    if (c == '\t') {
        for (int i = 0; i < 4; i++) cv_putc(s, ' ');
        return;
    }
    if (pw[s].cx + CELL_W > pw[s].w) cv_newline(s);
    cv_glyph(s, pw[s].cx, pw[s].cy + 1, c, pw[s].color);
    pw[s].cx += CELL_W;
}

static void pw_paint(int s) {
    const int x = pw[s].x;
    const int y = pw[s].y;
    const int w = ow(s);
    const int h = oh(s);
    draw_rect(x - 2, y - 2, w + 4, h + 4, 0x202020);
    draw_rect(x, y, w, h, 0xC0C0C0);
    draw_rect(x, y, w, 22, 0x0050A0);
    vga_draw_text_at(x + 8, y + 4, pw[s].title, 0xFFFFFF);
    draw_rect(x + w - 42, y + 3, 16, 16, 0x3A6FD0);
    draw_rect(x + w - 38, y + 3 + 10, 8, 3, 0xFFFFFF);
    draw_rect(x + w - 22, y + 3, 16, 16, 0xD04040);
    vga_draw_text_at(x + w - 19, y + 4, "X", 0xFFFFFF);
    draw_rect(x + 3, y + 25, pw[s].w + 2, pw[s].h + 2, 0x404040);
    for (int row = 0; row < pw[s].h; row++) {
        gfx_draw_span(x + 4, y + 26 + row, pw[s].w, pw_canvas[s] + row * PW_CW);
    }
}

void program_windows_paint(void) {
    for (int k = 0; k < order_count; k++) {
        const int s = order[k];
        if (pw[s].used && !pw[s].minimized) pw_paint(s);
    }
}

static void repaint(int x, int y, int w, int h) {
    const int screen_h = (int)fb_info.framebuffer_height;
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (y + h > screen_h - TASKBAR) h = screen_h - TASKBAR - y;
    if (w <= 0 || h <= 0) return;
    redraw_desk_region(x, y, w, h);
    gfx_set_clip(x, y, w, h);
    shell_paint();
    gfx_reset_clip();
    gfx_present();
}

static void repaint_window(int s) {
    repaint(pw[s].x - 4, pw[s].y - 4, ow(s) + 8, oh(s) + 8);
}

static int pw_create(int x, int y, int w, int h, const char* title, int title_length) {
    if (!fb_info.has_framebuffer) return -1;
    int s = -1;
    for (int i = 0; i < PROGRAM_WINDOW_MAX; i++) {
        if (!pw[i].used) {
            s = i;
            break;
        }
    }
    if (s < 0) return -1;
    if (w < 120) w = 120;
    if (h < 60) h = 60;
    if (w > PW_CW) w = PW_CW;
    if (h > PW_CH) h = PW_CH;
    const int sw = (int)fb_info.framebuffer_width;
    const int sh = (int)fb_info.framebuffer_height;
    if (x + w + 8 > sw) x = sw - w - 8;
    if (y + h + 30 > sh - TASKBAR) y = sh - TASKBAR - h - 30;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    pw[s].used = 1;
    pw[s].minimized = 0;
    pw[s].x = x;
    pw[s].y = y;
    pw[s].w = w;
    pw[s].h = h;
    const int max_chars = (w + 8 - 70) / 12;
    int n = 0;
    while (n < title_length && n < max_chars && n < 39) {
        pw[s].title[n] = title[n];
        n++;
    }
    pw[s].title[n] = '\0';
    pw[s].background = 0xF0F0F0;
    pw[s].color = 0x000000;
    pw[s].cx = MARGIN;
    pw[s].cy = MARGIN;
    canvas_fill(s, pw[s].background);
    z_push(s);
    taskbar_refresh();
    repaint_window(s);
    return s;
}

static void pw_close(int s) {
    if (!pw[s].used) return;
    const int x = pw[s].x - 4;
    const int y = pw[s].y - 4;
    const int w = ow(s) + 8;
    const int h = oh(s) + 8;
    pw[s].used = 0;
    z_remove(s);
    if (s == console_slot) console_slot = -1;
    repaint(x, y, w, h);
    taskbar_refresh();
}

int program_window_used(int slot) { return slot >= 0 && slot < PROGRAM_WINDOW_MAX && pw[slot].used; }
const char* program_window_title(int slot) { return pw[slot].title; }
int program_window_minimized(int slot) { return pw[slot].minimized; }

void program_window_toggle(int slot) {
    if (!program_window_used(slot)) return;
    if (pw[slot].minimized) {
        pw[slot].minimized = 0;
        z_push(slot);
    } else {
        pw[slot].minimized = 1;
    }
    repaint_window(slot);
    taskbar_refresh();
}

static int in_box(int mx, int my, int x, int y, int w, int h) {
    return mx >= x && mx < x + w && my >= y && my < y + h;
}

int program_windows_mouse(int mouse_x, int mouse_y, int buttons) {
    static int previous;
    static int drag = -1;
    static int drag_dx;
    static int drag_dy;
    const int left = (buttons & 1) != 0;
    const int pressed = left && !(previous & 1);
    previous = buttons;
    if (!left) drag = -1;
    int consumed = 0;

    if (pressed) {
        for (int k = order_count - 1; k >= 0; k--) {
            const int s = order[k];
            if (!pw[s].used || pw[s].minimized) continue;
            if (!in_box(mouse_x, mouse_y, pw[s].x, pw[s].y, ow(s), oh(s))) continue;
            consumed = 1;
            if (in_box(mouse_x, mouse_y, pw[s].x + ow(s) - 22, pw[s].y + 3, 16, 16)) {
                pw_close(s);
                return 1;
            }
            if (in_box(mouse_x, mouse_y, pw[s].x + ow(s) - 42, pw[s].y + 3, 16, 16)) {
                program_window_toggle(s);
                return 1;
            }
            if (k != order_count - 1) {
                z_push(s);
                repaint_window(s);
            }
            if (mouse_y < pw[s].y + 22) {
                drag = s;
                drag_dx = mouse_x - pw[s].x;
                drag_dy = mouse_y - pw[s].y;
            }
            break;
        }
    }

    if (left && drag >= 0 && pw[drag].used) {
        const int sw = (int)fb_info.framebuffer_width;
        const int sh = (int)fb_info.framebuffer_height;
        const int old_x = pw[drag].x;
        const int old_y = pw[drag].y;
        int new_x = mouse_x - drag_dx;
        int new_y = mouse_y - drag_dy;
        if (new_x > sw - ow(drag)) new_x = sw - ow(drag);
        if (new_y > sh - TASKBAR - oh(drag)) new_y = sh - TASKBAR - oh(drag);
        if (new_x < 0) new_x = 0;
        if (new_y < 0) new_y = 0;
        consumed = 1;
        if (new_x != old_x || new_y != old_y) {
            pw[drag].x = new_x;
            pw[drag].y = new_y;
            const int x0 = (old_x < new_x ? old_x : new_x) - 4;
            const int y0 = (old_y < new_y ? old_y : new_y) - 4;
            const int x1 = (old_x > new_x ? old_x : new_x) + ow(drag) + 4;
            const int y1 = (old_y > new_y ? old_y : new_y) + oh(drag) + 4;
            repaint(x0, y0, x1 - x0, y1 - y0);
        }
    }
    return consumed;
}

static void flush_dirty(void) {
    for (int i = 0; i < PROGRAM_WINDOW_MAX; i++) {
        if (dirty[i] && pw[i].used && !pw[i].minimized) repaint_window(i);
        dirty[i] = 0;
    }
}

static void pump(void) {
    const char key = keyboard_try_getchar_poll();
    if (key == 27) stop_flag = 1;
    else if (key != 0 && pending_key == 0) pending_key = (unsigned char)key;
    int mouse_x;
    int mouse_y;
    mouse_get_position(&mouse_x, &mouse_y);
    const int buttons = mouse_get_buttons();
    program_windows_mouse(mouse_x, mouse_y, buttons);
    taskbar_mouse(mouse_x, mouse_y, buttons);
}

static int valid(int id) { return id >= 0 && id < PROGRAM_WINDOW_MAX && pw[id].used; }

static void console_char(char c) {
    if (console_slot < 0 || !pw[console_slot].used) {
        console_slot = pw_create(60, 60, 480, 240, prog_title, 39);
        if (console_slot < 0) return;
        pw[console_slot].background = 0x000000;
        pw[console_slot].color = 0xAAAAAA;
        canvas_fill(console_slot, 0x000000);
        owner_mask |= 1 << console_slot;
    }
    cv_putc(console_slot, c);
    dirty[console_slot] = 1;
}

static void console_text(const char* text) {
    for (int i = 0; text[i]; i++) console_char(text[i]);
}

static void host_put_char(char c) { console_char(c); }

static int host_win_create(int x, int y, int w, int h, const char* title, int length) {
    const int s = pw_create(x, y, w, h, title, length);
    if (s >= 0) owner_mask |= 1 << s;
    return s;
}

static void host_win_char(int id, char c) {
    if (!valid(id)) return;
    cv_putc(id, c);
    dirty[id] = 1;
}

static void host_win_color(int id, uint32_t rgb) {
    if (valid(id)) pw[id].color = rgb;
}

static void host_win_pixel(int id, int x, int y) {
    if (!valid(id)) return;
    cv_pixel(id, x, y, pw[id].color);
    dirty[id] = 1;
}

static void host_win_rect(int id, int x, int y, int w, int h) {
    if (!valid(id)) return;
    cv_rect(id, x, y, w, h, pw[id].color);
    dirty[id] = 1;
}

static void host_win_line(int id, int x0, int y0, int x1, int y1) {
    if (!valid(id)) return;
    cv_line(id, x0, y0, x1, y1, pw[id].color);
    dirty[id] = 1;
}

static void host_win_text(int id, int x, int y, const char* text, int length) {
    if (!valid(id)) return;
    for (int i = 0; i < length; i++) cv_glyph(id, x + i * CELL_W, y, text[i], pw[id].color);
    dirty[id] = 1;
}

static void host_win_image(int id, int x, int y, const uint8_t* indices, int width, int height) {
    if (!valid(id)) return;
    for (int yy = 0; yy < height; yy++) {
        for (int xx = 0; xx < width; xx++) cv_pixel(id, x + xx, y + yy, palette[indices[yy * width + xx] & 15]);
    }
    dirty[id] = 1;
}

static void host_win_clear(int id) {
    if (!valid(id)) return;
    canvas_fill(id, pw[id].background);
    pw[id].cx = MARGIN;
    pw[id].cy = MARGIN;
    dirty[id] = 1;
}

static void host_win_refresh(int id) {
    if (valid(id) && !pw[id].minimized) {
        repaint_window(id);
        dirty[id] = 0;
    }
}

static void host_win_close(int id) {
    if (valid(id)) pw_close(id);
}

static int host_get_key(void) {
    pump();
    const int key = pending_key;
    pending_key = 0;
    return key;
}

static int host_should_stop(void) {
    pump();
    flush_dirty();
    return stop_flag;
}

static int owned_open(void) {
    for (int i = 0; i < PROGRAM_WINDOW_MAX; i++) {
        if ((owner_mask & (1 << i)) && pw[i].used) return 1;
    }
    return 0;
}

static void host_mainloop(void) {
    flush_dirty();
    while (!stop_flag && owned_open()) {
        pump();
        flush_dirty();
    }
}

int program_execute_image(const uint8_t* image, int length, const char* title) {
    int n = 0;
    while (title[n] && n < 39) {
        prog_title[n] = title[n];
        n++;
    }
    prog_title[n] = '\0';
    owner_mask = 0;
    stop_flag = 0;
    pending_key = 0;
    for (int i = 0; i < PROGRAM_WINDOW_MAX; i++) dirty[i] = 0;

    CdeHost host = {
        host_put_char, host_win_create, host_win_char, host_win_color, host_win_pixel, host_win_rect,
        host_win_line, host_win_text, host_win_image, host_win_clear, host_win_refresh, host_win_close,
        host_get_key, host_should_stop, host_mainloop, 0
    };
    char error[100];
    const int result = cde_run(image, length, &host, error, sizeof(error));
    if (result == CDE_RUN_ERROR) {
        developer_log(error);
        console_char('\0');
        if (console_slot >= 0) pw[console_slot].color = 0xFF5555;
        console_text("ERROR: ");
        console_text(error);
    } else if (result == CDE_RUN_STOPPED) {
        developer_log("program stopped");
    }
    flush_dirty();

    if (console_slot >= 0 && pw[console_slot].used) {
        pw[console_slot].color = 0xAAAAAA;
        console_text("\n[PRESS ANY KEY]");
        flush_dirty();
        pending_key = 0;
        stop_flag = 0;
        while (console_slot >= 0 && !pending_key && !stop_flag) pump();
        if (console_slot >= 0) pw_close(console_slot);
    }
    stop_flag = 0;
    pending_key = 0;
    return result;
}

int program_run_file(const char* path) {
    if (!kernel_fat_mounted) return 0;
    FAT_File* file = FAT_Open(&kernel_fat_disk, path);
    if (file == NULL || file->IsDirectory) {
        if (file != NULL) FAT_Close(file);
        return 0;
    }
    int length = 0;
    while (length < IMAGE_MAX) {
        const uint32_t count = FAT_Read(&kernel_fat_disk, file, (uint32_t)(IMAGE_MAX - length), prog_image + length);
        if (count == 0) break;
        length += (int)count;
    }
    FAT_Close(file);
    if (length <= 0) return 0;
    char title[40];
    int start = 0;
    for (int i = 0; path[i]; i++) {
        if (path[i] == '/') start = i + 1;
    }
    int n = 0;
    for (int i = start; path[i] && path[i] != '.' && n < 39; i++) title[n++] = path[i];
    title[n] = '\0';
    program_execute_image(prog_image, length, title);
    return 1;
}

int program_list_files(char names[][13], int max) {
    int count = 0;
    if (!kernel_fat_mounted) return 0;
    FAT_File* directory = FAT_Open(&kernel_fat_disk, "/");
    if (directory == NULL) return 0;
    FAT_DirectoryEntry entry;
    while (count < max && FAT_ReadEntry(&kernel_fat_disk, directory, &entry)) {
        if (entry.Name[0] == 0) break;
        if (entry.Name[0] == 0xE5 || entry.Attributes == FAT_ATTRIBUTE_LFN ||
            (entry.Attributes & FAT_ATTRIBUTE_VOLUME_ID) || (entry.Attributes & FAT_ATTRIBUTE_DIRECTORY)) continue;
        if (entry.Name[8] != 'C' || entry.Name[9] != 'D' || entry.Name[10] != 'E') continue;
        int n = 0;
        for (int i = 0; i < 8 && entry.Name[i] != ' '; i++) names[count][n++] = (char)entry.Name[i];
        names[count][n++] = '.';
        names[count][n++] = 'C';
        names[count][n++] = 'D';
        names[count][n++] = 'E';
        names[count][n] = '\0';
        count++;
    }
    FAT_Close(directory);
    return count;
}

void program_autostart(void) {
    char names[16][13];
    const int count = program_list_files(names, 16);
    for (int i = 0; i < count; i++) {
        char path[16];
        path[0] = '/';
        int n = 1;
        for (int j = 0; names[i][j] && n < 15; j++) path[n++] = names[i][j];
        path[n] = '\0';
        FAT_File* file = FAT_Open(&kernel_fat_disk, path);
        if (file == NULL) continue;
        uint8_t header[32];
        const uint32_t got = FAT_Read(&kernel_fat_disk, file, sizeof(header), header);
        FAT_Close(file);
        if (got == sizeof(header) && cde_is_autostart(header, (int)got)) program_run_file(path);
    }
}

void program_draw_icon(int x, int y, int size) {
    for (int dy = 0; dy < size; dy++) {
        for (int dx = 0; dx < size; dx++) {
            const int value = program_icon[(dy * PROGRAM_ICON_SIZE / size) * PROGRAM_ICON_SIZE +
                                           (dx * PROGRAM_ICON_SIZE / size)];
            if (value == PROGRAM_ICON_CLEAR) continue;
            draw_rect(x + dx, y + dy, 1, 1, palette[value & 15]);
        }
    }
}