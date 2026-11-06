#include "system.h"
#include "developer_.h"
#include "cde.h"
#include "bmp.h"
#include "taskbar_ui.h"
#include "shell_window.h"

void gfx_set_clip(int x, int y, int w, int h);
void gfx_reset_clip(void);
void redraw_desk_region(int x, int y, int width, int height);

extern DISK kernel_fat_disk;
extern int kernel_fat_mounted;
extern char keyboard_getchar_poll(void);

#define DV_W 800
#define DV_H 520
#define DV_BTN 16
#define DV_TITLE_H 22
#define DV_TASKBAR 36
#define DV_TAB_Y 22
#define DV_STATUS_H 22

#define PIC_STRIDE 48
#define PIC_BG 15
#define PIC_X 10
#define PIC_Y 112
#define PIC_VIEW 256
#define PREVIEW_X 440

#define PANEL_W 200
#define PANEL_ROWS 16
#define PANEL_ROW_H 20
#define FILE_MAX 32

#define CD_BUF 8192
#define CD_COLS 46
#define CD_ROWS 14
#define CD_X 10
#define CD_Y 86
#define LINE_H 18
#define CHAR_W 12

#define LOG_LINES 64
#define LOG_COLS 64
#define NAME_LEN 64

enum { TAB_SHELL = 0, TAB_CODE = 1, TAB_BRUSH = 2 };
enum { TOOL_PEN = 0, TOOL_LINE = 1, TOOL_RECT = 2, TOOL_FILL = 3 };

static const uint32_t dv_palette[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA,
    0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF,
    0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF
};

static const char* const dv_tab_names[3] = {"Shell", "Code", "Brush"};

static char dv_log_lines[LOG_LINES][LOG_COLS + 1];
static int dv_log_count;

static int dv_x = 80;
static int dv_y = 40;
static int dv_placed;
static int dv_exists;
static int dv_minimized;
static int dv_active;
static int dv_minimize_request;
static int dv_dragging;
static int dv_drag_dx;
static int dv_drag_dy;
static int dv_tab = TAB_CODE;
static int dv_armed;
static const char* dv_status = "";

static uint8_t pic[PIC_STRIDE * PIC_STRIDE];
static int pic_size = 32;
static int pic_tool = TOOL_PEN;
static int pic_color = 0;
static int pic_modified;
static int pic_drawing;
static int pic_start_x;
static int pic_start_y;
static int pic_cur_x;
static int pic_cur_y;
static int pic_last_x = -1;
static int pic_last_y = -1;
static char pic_name[NAME_LEN];
static char pic_files[FILE_MAX][13];
static int pic_count;
static int pic_scroll;
static uint16_t pic_stack[PIC_STRIDE * PIC_STRIDE];
static uint8_t bmp_row[4096];
static uint32_t bmp_palette[256];
static uint8_t bmp_out[256 + 1280];

static char cd_buf[CD_BUF];
static int cd_len;
static int cd_cur;
static int cd_top;
static int cd_left;
static int cd_modified;
static char cd_name[NAME_LEN];
static char cd_files[FILE_MAX][13];
static int cd_count;
static int cd_scroll;

int developer_is_open(void) { return dv_exists; }
int developer_is_minimized(void) { return dv_exists && dv_minimized; }
int developer_is_active(void) { return dv_active; }
void developer_request_minimize(void) { dv_minimize_request = 1; }

void developer_log(const char* text) {
    if (dv_log_count == LOG_LINES) {
        for (int i = 1; i < LOG_LINES; i++) {
            for (int j = 0; j <= LOG_COLS; j++) dv_log_lines[i - 1][j] = dv_log_lines[i][j];
        }
        dv_log_count--;
    }
    char* destination = dv_log_lines[dv_log_count++];
    int n = 0;
    while (text[n] && n < LOG_COLS) {
        destination[n] = text[n];
        n++;
    }
    destination[n] = '\0';
}

static int dv_len(const char* text) {
    int n = 0;
    while (text[n]) n++;
    return n;
}

static void dv_append(char* out, int* n, const char* text, int max) {
    for (int i = 0; text[i] && *n < max - 1; i++) out[(*n)++] = text[i];
    out[*n] = '\0';
}

static void dv_append_int(char* out, int* n, int value, int max) {
    char digits[16];
    int_to_str(value, digits, sizeof(digits));
    dv_append(out, n, digits, max);
}

static void dv_log_text(const char* a, const char* b) {
    char message[LOG_COLS + 1];
    int n = 0;
    message[0] = '\0';
    dv_append(message, &n, a, sizeof(message));
    dv_append(message, &n, b, sizeof(message));
    developer_log(message);
}

static void dv_fill(int x, int y, int w, int h, uint32_t color) {
    draw_rect(x, y, w, h, color);
}

static void dv_text(int x, int y, const char* text, uint32_t color) {
    vga_draw_text_at(x, y, text, color);
}

static void dv_text_clip(int x, int y, const char* text, int max_cols, uint32_t color) {
    char shown[LOG_COLS + 1];
    int n = 0;
    while (text[n] && n < max_cols && n < LOG_COLS) {
        shown[n] = text[n];
        n++;
    }
    shown[n] = '\0';
    vga_draw_text_at(x, y, shown, color);
}


#define SHELL_GFX_W 480
#define SHELL_GFX_H 320
#define SHELL_TEXT_MAX 48
#define SHELL_TEXT_LEN 96
#define CDE_IMAGE_MAX 8192

typedef struct {
    int x;
    int y;
    uint32_t color;
    char text[SHELL_TEXT_LEN];
} ShellTextItem;

static char shell_lines[LOG_LINES][LOG_COLS + 1];
static int shell_line_count = 1;
static int shell_column;
static uint32_t shell_pixels[SHELL_GFX_W * SHELL_GFX_H];
static int shell_graphics_active;
static int shell_graphics_width = SHELL_GFX_W;
static int shell_graphics_height = SHELL_GFX_H;
static uint32_t shell_draw_color = 0xFFFFFF;
static ShellTextItem shell_text_items[SHELL_TEXT_MAX];
static int shell_text_count;
static int shell_key_buffer;
static int shell_stop_requested;
static uint8_t shell_cde_image[CDE_IMAGE_MAX];

static void paint_shell(void);
static void draw_all(void);

static void shell_new_line(void) {
    if (shell_line_count < LOG_LINES) {
        shell_line_count++;
    } else {
        for (int i = 1; i < LOG_LINES; i++) {
            for (int j = 0; j <= LOG_COLS; j++) shell_lines[i - 1][j] = shell_lines[i][j];
        }
    }
    shell_column = 0;
    shell_lines[shell_line_count - 1][0] = '\0';
}

static void shell_put_char(char c) {
    if (c == '\n') {
        shell_new_line();
        return;
    }
    if (c == '\r') {
        shell_column = 0;
        shell_lines[shell_line_count - 1][0] = '\0';
        return;
    }
    if (c == '\b') {
        if (shell_column > 0) shell_column--;
        shell_lines[shell_line_count - 1][shell_column] = '\0';
        return;
    }
    if ((unsigned char)c < 32) return;
    if (shell_column >= LOG_COLS) shell_new_line();
    shell_lines[shell_line_count - 1][shell_column++] = c;
    shell_lines[shell_line_count - 1][shell_column] = '\0';
}

static void shell_write_text(const char* text) {
    for (int i = 0; text[i]; i++) shell_put_char(text[i]);
}

static void shell_clear_canvas(void) {
    for (int i = 0; i < SHELL_GFX_W * SHELL_GFX_H; i++) shell_pixels[i] = 0x000000;
    shell_text_count = 0;
}

static void shell_clear_output(void) {
    for (int i = 0; i < LOG_LINES; i++) shell_lines[i][0] = '\0';
    shell_line_count = 1;
    shell_column = 0;
    shell_graphics_active = 0;
    shell_graphics_width = SHELL_GFX_W;
    shell_graphics_height = SHELL_GFX_H;
    shell_key_buffer = 0;
    shell_stop_requested = 0;
    shell_clear_canvas();
}

static void shell_canvas_pixel(int x, int y, uint32_t color) {
    if (x < 0 || y < 0 || x >= shell_graphics_width || y >= shell_graphics_height) return;
    shell_pixels[y * SHELL_GFX_W + x] = color & 0xFFFFFF;
}

static void shell_canvas_rect(int x, int y, int w, int h, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    int64_t x1 = (int64_t)x + w;
    int64_t y1 = (int64_t)y + h;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    if (x1 > shell_graphics_width) x1 = shell_graphics_width;
    if (y1 > shell_graphics_height) y1 = shell_graphics_height;
    if (x1 <= x0 || y1 <= y0) return;
    for (int yy = y0; yy < (int)y1; yy++) {
        for (int xx = x0; xx < (int)x1; xx++) shell_pixels[yy * SHELL_GFX_W + xx] = color & 0xFFFFFF;
    }
}

static void shell_canvas_line(int x0, int y0, int x1, int y1, uint32_t color) {
    if (x0 < -1024) x0 = -1024;
    if (x0 > 1024) x0 = 1024;
    if (x1 < -1024) x1 = -1024;
    if (x1 > 1024) x1 = 1024;
    if (y0 < -1024) y0 = -1024;
    if (y0 > 1024) y0 = 1024;
    if (y1 < -1024) y1 = -1024;
    if (y1 > 1024) y1 = 1024;
    int dx = x1 >= x0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;
    int dy = y1 >= y0 ? y0 - y1 : y1 - y0;
    int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (int count = 0; count < 4096; count++) {
        shell_canvas_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        const int twice = error * 2;
        if (twice >= dy) { error += dy; x0 += sx; }
        if (twice <= dx) { error += dx; y0 += sy; }
    }
}

static void shell_blit_canvas(void) {
    if (!shell_graphics_active) return;
    const int ox = dv_x + 10;
    const int oy = dv_y + 88;
    gfx_set_clip(ox, oy, shell_graphics_width, shell_graphics_height);
    for (int y = 0; y < shell_graphics_height; y++) {
        gfx_draw_span(ox, oy + y, shell_graphics_width, &shell_pixels[y * SHELL_GFX_W]);
    }
    for (int i = 0; i < shell_text_count; i++) {
        vga_draw_text_at(ox + shell_text_items[i].x, oy + shell_text_items[i].y,
                         shell_text_items[i].text, shell_text_items[i].color);
    }
    gfx_reset_clip();
}

static int shell_host_win_create(int x, int y, int w, int h, const char* title, int title_length) {
    (void)x; (void)y; (void)title; (void)title_length;
    if (w <= 0 || h <= 0) return -1;
    shell_graphics_width = w > SHELL_GFX_W ? SHELL_GFX_W : w;
    shell_graphics_height = h > SHELL_GFX_H ? SHELL_GFX_H : h;
    shell_graphics_active = 1;
    shell_draw_color = 0xFFFFFF;
    shell_clear_canvas();
    return 0;
}

static void shell_host_win_char(int id, char c) {
    if (id == 0) shell_put_char(c);
}

static void shell_host_win_color(int id, uint32_t color) {
    if (id == 0) shell_draw_color = color & 0xFFFFFF;
}

static void shell_host_win_pixel(int id, int x, int y) {
    if (id == 0) shell_canvas_pixel(x, y, shell_draw_color);
}

static void shell_host_win_rect(int id, int x, int y, int w, int h) {
    if (id == 0) shell_canvas_rect(x, y, w, h, shell_draw_color);
}

static void shell_host_win_line(int id, int x0, int y0, int x1, int y1) {
    if (id == 0) shell_canvas_line(x0, y0, x1, y1, shell_draw_color);
}

static void shell_host_win_text(int id, int x, int y, const char* text, int length) {
    if (id != 0 || text == 0 || length <= 0 || shell_text_count >= SHELL_TEXT_MAX) return;
    ShellTextItem* item = &shell_text_items[shell_text_count++];
    item->x = x;
    item->y = y;
    item->color = shell_draw_color;
    int n = length < SHELL_TEXT_LEN - 1 ? length : SHELL_TEXT_LEN - 1;
    for (int i = 0; i < n; i++) item->text[i] = text[i];
    item->text[n] = '\0';
}

static void shell_host_win_clear(int id) {
    if (id == 0) shell_clear_canvas();
}

static void shell_host_win_refresh(int id) {
    if (id != 0) return;
    paint_shell();
    present();
}

static void shell_host_win_close(int id) {
    if (id == 0) shell_graphics_active = 0;
}

static int shell_host_get_key(void) {
    if (shell_key_buffer) {
        const int key = shell_key_buffer;
        shell_key_buffer = 0;
        return key;
    }
    return keyboard_try_getchar_poll();
}

static int shell_host_should_stop(void) {
    if (shell_stop_requested || dv_minimize_request || !dv_exists) return 1;
    if (!shell_key_buffer) {
        const int key = keyboard_try_getchar_poll();
        if (key == 27) {
            shell_stop_requested = 1;
            return 1;
        }
        if (key != 0) shell_key_buffer = key;
    }
    return 0;
}

static void shell_host_mainloop(void) {
    if (shell_graphics_active) shell_host_win_refresh(0);
}

static void shell_host_finish(void) {
    shell_stop_requested = 0;
}

static void dv_line(int x0, int y0, int x1, int y1, uint32_t color) {
    set_color(color);
    line(x0, y0, x1, y1);
}

static void dv_border(int x, int y, int w, int h, uint32_t color) {
    dv_line(x, y, x + w - 1, y, color);
    dv_line(x + w - 1, y, x + w - 1, y + h - 1, color);
    dv_line(x + w - 1, y + h - 1, x, y + h - 1, color);
    dv_line(x, y + h - 1, x, y, color);
}

static int dv_in(int mx, int my, int x, int y, int w, int h) {
    return mx >= x && mx < x + w && my >= y && my < y + h;
}

static void dv_button(int x, int y, int w, int h, const char* label, int active) {
    dv_fill(x, y, w, h, active ? 0xB8B4AE : 0xD8D4CE);
    dv_fill(x, y, w, 1, active ? 0x404040 : 0xFFFFFF);
    dv_fill(x, y, 1, h, active ? 0x404040 : 0xFFFFFF);
    dv_fill(x, y + h - 1, w, 1, active ? 0xFFFFFF : 0x404040);
    dv_fill(x + w - 1, y, 1, h, active ? 0xFFFFFF : 0x404040);
    const int tx = x + (w - dv_len(label) * CHAR_W) / 2;
    dv_text(tx + (active ? 1 : 0), y + (h - 14) / 2 + (active ? 1 : 0), label, 0x000000);
}

static void dv_show(void) {
    present();
}

static void dv_reset(void) {
    shell_clear_output();
    cd_len = 0;
    cd_cur = 0;
    cd_top = 0;
    cd_left = 0;
    cd_modified = 0;
    cd_name[0] = '\0';
    for (int i = 0; i < PIC_STRIDE * PIC_STRIDE; i++) pic[i] = PIC_BG;
    pic_modified = 0;
    pic_name[0] = '\0';
    pic_drawing = 0;
    pic_last_x = -1;
    pic_last_y = -1;
    dv_armed = 0;
    dv_status = "";
}

static void make_name(const char* input, const char* extension, char* out) {
    int n = 0;
    int has_dot = 0;
    if (input[0] != '/') out[n++] = '/';
    for (int i = 0; input[i] && n < NAME_LEN - 5; i++) {
        char c = input[i];
        if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
        if (c == '.') has_dot = 1;
        out[n++] = c;
    }
    if (!has_dot) {
        for (int i = 0; extension[i]; i++) out[n++] = extension[i];
    }
    out[n] = '\0';
}

static int list_files(const char* extension, char names[][13], int max) {
    int count = 0;
    if (!kernel_fat_mounted) return 0;
    FAT_File* directory = FAT_Open(&kernel_fat_disk, "/");
    if (directory == NULL) return 0;
    FAT_DirectoryEntry entry;
    while (count < max && FAT_ReadEntry(&kernel_fat_disk, directory, &entry)) {
        if (entry.Name[0] == 0) break;
        if (entry.Name[0] == 0xE5 || entry.Attributes == FAT_ATTRIBUTE_LFN ||
            (entry.Attributes & FAT_ATTRIBUTE_VOLUME_ID) ||
            (entry.Attributes & FAT_ATTRIBUTE_DIRECTORY)) continue;
        if (entry.Name[8] != (uint8_t)extension[0] || entry.Name[9] != (uint8_t)extension[1] ||
            entry.Name[10] != (uint8_t)extension[2]) continue;
        int n = 0;
        for (int i = 0; i < 8 && entry.Name[i] != ' '; i++) names[count][n++] = (char)entry.Name[i];
        names[count][n++] = '.';
        for (int i = 8; i < 11 && entry.Name[i] != ' '; i++) names[count][n++] = (char)entry.Name[i];
        names[count][n] = '\0';
        count++;
    }
    FAT_Close(directory);
    return count;
}

static void refresh_lists(void) {
    pic_count = list_files("BMP", pic_files, FILE_MAX);
    cd_count = list_files("DCE", cd_files, FILE_MAX);
    if (pic_scroll > pic_count) pic_scroll = 0;
    if (cd_scroll > cd_count) cd_scroll = 0;
}

static uint32_t le32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t le16(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

static void put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static int nearest_color(uint32_t rgb) {
    const int r = (int)((rgb >> 16) & 0xFF);
    const int g = (int)((rgb >> 8) & 0xFF);
    const int b = (int)(rgb & 0xFF);
    int best = 0;
    int best_distance = 0x7FFFFFFF;
    for (int i = 0; i < 16; i++) {
        const int pr = (int)((dv_palette[i] >> 16) & 0xFF);
        const int pg = (int)((dv_palette[i] >> 8) & 0xFF);
        const int pb = (int)(dv_palette[i] & 0xFF);
        const int distance = (r - pr) * (r - pr) + (g - pg) * (g - pg) + (b - pb) * (b - pb);
        if (distance < best_distance) {
            best_distance = distance;
            best = i;
        }
    }
    return best;
}

static int pic_zoom(void) {
    return PIC_VIEW / pic_size;
}

static void pic_put(int x, int y, int color) {
    if (x < 0 || y < 0 || x >= pic_size || y >= pic_size) return;
    pic[y * PIC_STRIDE + x] = (uint8_t)color;
}

static void pic_draw_line(int x0, int y0, int x1, int y1, int color) {
    int dx = x1 >= x0 ? x1 - x0 : x0 - x1;
    int dy = y1 >= y0 ? y0 - y1 : y1 - y0;
    const int sx = x0 < x1 ? 1 : -1;
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    while (1) {
        pic_put(x0, y0, color);
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

static void pic_draw_rect(int x0, int y0, int x1, int y1, int color) {
    pic_draw_line(x0, y0, x1, y0, color);
    pic_draw_line(x1, y0, x1, y1, color);
    pic_draw_line(x1, y1, x0, y1, color);
    pic_draw_line(x0, y1, x0, y0, color);
}

static void pic_fill(int x, int y, int color) {
    if (x < 0 || y < 0 || x >= pic_size || y >= pic_size) return;
    const uint8_t target = pic[y * PIC_STRIDE + x];
    if (target == color) return;
    int top = 0;
    pic[y * PIC_STRIDE + x] = (uint8_t)color;
    pic_stack[top++] = (uint16_t)(y * PIC_STRIDE + x);
    while (top > 0) {
        const int index = pic_stack[--top];
        const int px = index % PIC_STRIDE;
        const int py = index / PIC_STRIDE;
        const int nx[4] = {px - 1, px + 1, px, px};
        const int ny[4] = {py, py, py - 1, py + 1};
        for (int i = 0; i < 4; i++) {
            if (nx[i] < 0 || ny[i] < 0 || nx[i] >= pic_size || ny[i] >= pic_size) continue;
            if (pic[ny[i] * PIC_STRIDE + nx[i]] != target) continue;
            pic[ny[i] * PIC_STRIDE + nx[i]] = (uint8_t)color;
            pic_stack[top++] = (uint16_t)(ny[i] * PIC_STRIDE + nx[i]);
        }
    }
}

static void pic_clear(void) {
    for (int i = 0; i < PIC_STRIDE * PIC_STRIDE; i++) pic[i] = PIC_BG;
}

static int pic_cell(int mx, int my, int* cx, int* cy, int clamp) {
    const int zoom = pic_zoom();
    int x = (mx - (dv_x + PIC_X));
    int y = (my - (dv_y + PIC_Y));
    if (!clamp && (x < 0 || y < 0)) return 0;
    x = x < 0 ? 0 : x / zoom;
    y = y < 0 ? 0 : y / zoom;
    if (clamp) {
        if (x >= pic_size) x = pic_size - 1;
        if (y >= pic_size) y = pic_size - 1;
    }
    *cx = x;
    *cy = y;
    return x < pic_size && y < pic_size;
}

static int pic_encode(void) {
    const int row_bytes = ((pic_size * 4 + 31) / 32) * 4;
    const int image_bytes = row_bytes * pic_size;
    const int offset = 14 + 40 + 64;
    const int total = offset + image_bytes;
    for (int i = 0; i < total; i++) bmp_out[i] = 0;
    bmp_out[0] = 'B';
    bmp_out[1] = 'M';
    put32(bmp_out + 2, (uint32_t)total);
    put32(bmp_out + 10, (uint32_t)offset);
    put32(bmp_out + 14, 40);
    put32(bmp_out + 18, (uint32_t)pic_size);
    put32(bmp_out + 22, (uint32_t)pic_size);
    bmp_out[26] = 1;
    bmp_out[28] = 4;
    put32(bmp_out + 34, (uint32_t)image_bytes);
    put32(bmp_out + 38, 2835);
    put32(bmp_out + 42, 2835);
    put32(bmp_out + 46, 16);
    put32(bmp_out + 50, 16);
    for (int i = 0; i < 16; i++) {
        bmp_out[54 + i * 4] = (uint8_t)(dv_palette[i] & 0xFF);
        bmp_out[55 + i * 4] = (uint8_t)((dv_palette[i] >> 8) & 0xFF);
        bmp_out[56 + i * 4] = (uint8_t)((dv_palette[i] >> 16) & 0xFF);
    }
    for (int row = 0; row < pic_size; row++) {
        const int source_row = pic_size - 1 - row;
        for (int x = 0; x < pic_size; x++) {
            const uint8_t value = pic[source_row * PIC_STRIDE + x] & 15;
            uint8_t* byte = bmp_out + offset + row * row_bytes + x / 2;
            if (x & 1) *byte |= value;
            else *byte |= (uint8_t)(value << 4);
        }
    }
    return total;
}

static int dv_prompt(const char* title, char* out, int max) {
    const int x = dv_x + (DV_W - 344) / 2;
    const int y = dv_y + DV_H / 2 - 43;
    int length = 0;
    out[0] = '\0';
    while (1) {
        dv_fill(x - 3, y - 3, 350, 92, 0x202020);
        dv_fill(x, y, 344, 86, 0xC0C0C0);
        dv_fill(x, y, 344, 20, 0x0050A0);
        dv_text(x + 10, y + 3, title, 0xFFFFFF);
        dv_text(x + 12, y + 28, "FILE NAME:", 0x000000);
        dv_fill(x + 12, y + 47, 316, 20, 0xFFFFFF);
        dv_text(x + 18, y + 50, out, 0x000000);
        dv_show();
        const char key = keyboard_getchar_poll();
        if (key == 27) return 0;
        if (key == '\n') return length > 0;
        if (key == '\b') {
            if (length > 0) out[--length] = '\0';
            continue;
        }
        char value = key;
        if (value >= 'a' && value <= 'z') value -= 'a' - 'A';
        if (((value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9') ||
             value == '.' || value == '_' || value == '-') && length < max - 1) {
            out[length++] = value;
            out[length] = '\0';
        }
    }
}

static int write_file(const char* path, const void* data, uint32_t length) {
    if (!kernel_fat_mounted) return 0;
    FAT_File* file = FAT_Open(&kernel_fat_disk, path);
    int created = 0;
    if (file != NULL && file->IsDirectory) {
        FAT_Close(file);
        return 0;
    }
    if (file == NULL) {
        file = FAT_Create(&kernel_fat_disk, path);
        created = file != NULL;
    }
    if (file == NULL) return 0;
    const int saved = FAT_Save(&kernel_fat_disk, file, data, length);
    FAT_Close(file);
    if (!saved && created) FAT_Remove(&kernel_fat_disk, path, false);
    return saved;
}


static void compiled_path(const char* source_path, char* out, int max) {
    int n = 0;
    int dot = -1;
    int slash = -1;
    while (source_path[n] && n < max - 1) {
        out[n] = source_path[n];
        if (source_path[n] == '/') slash = n;
        if (source_path[n] == '.') dot = n;
        n++;
    }
    if (dot > slash) n = dot;
    if (n > max - 5) n = max - 5;
    out[n++] = '.';
    out[n++] = 'C';
    out[n++] = 'D';
    out[n++] = 'E';
    out[n] = '\0';
}

static int compile_current(void) {
    char error[128];
    char output_path[NAME_LEN];
    if (!cd_name[0]) {
        char input[NAME_LEN];
        if (!dv_prompt("COMPILE AS", input, sizeof(input))) {
            dv_status = "COMPILE CANCELLED";
            return 1;
        }
        make_name(input, ".DCE", cd_name);
    }

    dv_tab = TAB_SHELL;
    dv_status = "COMPILING CDE...";
    draw_all();
    cde_set_image_loader(bmp_image_loader);
    const int image_size = cde_compile(cd_buf, cd_len, shell_cde_image,
                                       sizeof(shell_cde_image), error, sizeof(error));
    if (image_size <= 0) {
        shell_write_text("[COMPILE ERROR] ");
        shell_write_text(error[0] ? error : "UNKNOWN COMPILER ERROR");
        shell_put_char('\n');
        dv_status = "COMPILE FAILED - SEE SHELL";
        return 1;
    }

    compiled_path(cd_name, output_path, sizeof(output_path));
    if (!write_file(output_path, shell_cde_image, (uint32_t)image_size)) {
        shell_write_text("[COMPILE ERROR] CANNOT WRITE ");
        shell_write_text(output_path);
        shell_put_char('\n');
        dv_status = "CANNOT SAVE .CDE FILE";
        return 1;
    }

    shell_write_text("[COMPILED] ");
    shell_write_text(output_path);
    shell_write_text(" ( ");
    {
        char count_text[16];
        int n = 0;
        int value = image_size;
        char rev[16];
        int digits = 0;
        do { rev[digits++] = (char)('0' + value % 10); value /= 10; } while (value > 0 && digits < 15);
        while (digits > 0) count_text[n++] = rev[--digits];
        count_text[n] = '\0';
        shell_write_text(count_text);
    }
    shell_write_text(" bytes )\n");

    dv_status = "COMPILED - START WITH: run NAME";
    shell_write_text("[HINT] start it with: run ");
    shell_write_text(output_path + 1);
    shell_put_char('\n');

    return 1;
}

static void pic_save(int ask) {
    if (ask || !pic_name[0]) {
        char input[NAME_LEN];
        if (!dv_prompt("SAVE PICTURE AS", input, sizeof(input))) {
            dv_status = "CANCELLED";
            return;
        }
        make_name(input, ".BMP", pic_name);
    }
    const int total = pic_encode();
    if (!write_file(pic_name, bmp_out, (uint32_t)total)) {
        dv_status = "SAVE FAILED";
        dv_log_text("save failed: ", pic_name);
        return;
    }
    pic_modified = 0;
    dv_status = "SAVED";
    dv_log_text("saved ", pic_name);
    refresh_lists();
}

static int pic_open(const char* name) {
    char path[NAME_LEN];
    make_name(name, ".BMP", path);
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
        if (FAT_Read(&kernel_fat_disk, file, colors * 4, bmp_row) != colors * 4) {
            FAT_Close(file);
            return 0;
        }
        for (uint32_t i = 0; i < colors; i++) {
            bmp_palette[i] = ((uint32_t)bmp_row[i * 4 + 2] << 16) | ((uint32_t)bmp_row[i * 4 + 1] << 8) |
                             (uint32_t)bmp_row[i * 4];
        }
    }
    const uint32_t stride = (((uint32_t)width * (uint32_t)bpp + 31) / 32) * 4;
    const uint32_t needed = ((uint32_t)width * (uint32_t)bpp + 7) / 8;
    if (stride > sizeof(bmp_row)) {
        FAT_Close(file);
        return 0;
    }
    const int need = width > height ? width : height;
    const int size = need <= 16 ? 16 : (need <= 32 ? 32 : 48);
    int out_w = width;
    int out_h = height;
    if (need > 48) {
        if (width >= height) {
            out_w = 48;
            out_h = height * 48 / width;
        } else {
            out_h = 48;
            out_w = width * 48 / height;
        }
        if (out_w < 1) out_w = 1;
        if (out_h < 1) out_h = 1;
    }
    pic_clear();
    pic_size = size;
    int last_row = -1;
    for (int dy = 0; dy < out_h; dy++) {
        const int sy = dy * height / out_h;
        if (sy != last_row) {
            const int row = top_down ? sy : height - 1 - sy;
            file->Position = data_offset + (uint32_t)row * stride;
            if (FAT_Read(&kernel_fat_disk, file, stride, bmp_row) < needed) {
                FAT_Close(file);
                return 0;
            }
            last_row = sy;
        }
        for (int dx = 0; dx < out_w; dx++) {
            const int sx = dx * width / out_w;
            uint32_t rgb;
            if (bpp == 4) {
                const uint8_t byte = bmp_row[sx / 2];
                rgb = bmp_palette[(sx & 1) ? (byte & 15) : (byte >> 4)];
            } else if (bpp == 8) {
                rgb = bmp_palette[bmp_row[sx]];
            } else {
                const int step = bpp / 8;
                rgb = ((uint32_t)bmp_row[sx * step + 2] << 16) | ((uint32_t)bmp_row[sx * step + 1] << 8) |
                      (uint32_t)bmp_row[sx * step];
            }
            pic[dy * PIC_STRIDE + dx] = (uint8_t)nearest_color(rgb);
        }
    }
    FAT_Close(file);
    int n = 0;
    for (int i = 0; path[i] && n < NAME_LEN - 1; i++) pic_name[n++] = path[i];
    pic_name[n] = '\0';
    pic_modified = 0;
    return 1;
}

static int cd_line_start(int index) {
    while (index > 0 && cd_buf[index - 1] != '\n') index--;
    return index;
}

static int cd_line_end(int index) {
    while (index < cd_len && cd_buf[index] != '\n') index++;
    return index;
}

static void cd_cursor_pos(int* row, int* column) {
    int count = 0;
    for (int i = 0; i < cd_cur; i++) {
        if (cd_buf[i] == '\n') count++;
    }
    *row = count;
    *column = cd_cur - cd_line_start(cd_cur);
}

static void cd_insert(char c) {
    if (cd_len >= CD_BUF - 1) {
        dv_status = "BUFFER FULL";
        return;
    }
    for (int i = cd_len; i > cd_cur; i--) cd_buf[i] = cd_buf[i - 1];
    cd_buf[cd_cur++] = c;
    cd_len++;
    cd_modified = 1;
}

static void cd_backspace(void) {
    if (cd_cur <= 0) return;
    for (int i = cd_cur - 1; i < cd_len - 1; i++) cd_buf[i] = cd_buf[i + 1];
    cd_cur--;
    cd_len--;
    cd_modified = 1;
}

static void cd_move_up(void) {
    const int start = cd_line_start(cd_cur);
    const int column = cd_cur - start;
    if (start == 0) return;
    const int previous_end = start - 1;
    const int previous_start = cd_line_start(previous_end);
    const int previous_len = previous_end - previous_start;
    cd_cur = previous_start + (column < previous_len ? column : previous_len);
}

static void cd_move_down(void) {
    const int start = cd_line_start(cd_cur);
    const int column = cd_cur - start;
    const int end = cd_line_end(cd_cur);
    if (end >= cd_len) return;
    const int next_start = end + 1;
    const int next_len = cd_line_end(next_start) - next_start;
    cd_cur = next_start + (column < next_len ? column : next_len);
}

static void cd_scroll_to_cursor(void) {
    int row;
    int column;
    cd_cursor_pos(&row, &column);
    if (row < cd_top) cd_top = row;
    if (row >= cd_top + CD_ROWS) cd_top = row - CD_ROWS + 1;
    if (column < cd_left) cd_left = column;
    if (column >= cd_left + CD_COLS) cd_left = column - CD_COLS + 1;
}

static void cd_click(int mx, int my) {
    const int row = (my - (dv_y + CD_Y)) / LINE_H;
    const int column = (mx - (dv_x + CD_X)) / CHAR_W + cd_left;
    const int target = cd_top + row;
    int index = 0;
    int current = 0;
    while (current < target && index < cd_len) {
        if (cd_buf[index] == '\n') current++;
        index++;
    }
    if (current < target) {
        cd_cur = cd_len;
        return;
    }
    const int length = cd_line_end(index) - index;
    cd_cur = index + (column < length ? column : length);
}

static void cd_save(int ask) {
    if (ask || !cd_name[0]) {
        char input[NAME_LEN];
        if (!dv_prompt("SAVE CODE AS", input, sizeof(input))) {
            dv_status = "CANCELLED";
            return;
        }
        make_name(input, ".DCE", cd_name);
    }
    if (!write_file(cd_name, cd_buf, (uint32_t)cd_len)) {
        dv_status = "SAVE FAILED (TOO BIG?)";
        dv_log_text("save failed: ", cd_name);
        return;
    }
    cd_modified = 0;
    dv_status = "SAVED";
    dv_log_text("saved ", cd_name);
    refresh_lists();
}

static int cd_load(const char* name) {
    char path[NAME_LEN];
    make_name(name, ".DCE", path);
    if (!kernel_fat_mounted) return 0;
    FAT_File* file = FAT_Open(&kernel_fat_disk, path);
    if (file == NULL || file->IsDirectory) {
        if (file != NULL) FAT_Close(file);
        return 0;
    }
    cd_len = 0;
    while (cd_len < CD_BUF - 1) {
        const uint32_t count = FAT_Read(&kernel_fat_disk, file, (uint32_t)(CD_BUF - 1 - cd_len), cd_buf + cd_len);
        if (count == 0) break;
        cd_len += (int)count;
    }
    FAT_Close(file);
    int out = 0;
    for (int i = 0; i < cd_len; i++) {
        if (cd_buf[i] != '\r') cd_buf[out++] = cd_buf[i];
    }
    cd_len = out;
    cd_cur = 0;
    cd_top = 0;
    cd_left = 0;
    cd_modified = 0;
    int n = 0;
    for (int i = 0; path[i] && n < NAME_LEN - 1; i++) cd_name[n++] = path[i];
    cd_name[n] = '\0';
    return 1;
}

static void cd_new(void) {
    cd_len = 0;
    cd_cur = 0;
    cd_top = 0;
    cd_left = 0;
    cd_modified = 0;
    cd_name[0] = '\0';
}

static int panel_x(void) { return dv_x + DV_W - PANEL_W - 10; }
static int panel_y(void) { return dv_y + 58; }

static int panel_row_at(int mx, int my, int count, int scroll) {
    if (!dv_in(mx, my, panel_x(), panel_y() + 22, PANEL_W, PANEL_ROWS * PANEL_ROW_H)) return -1;
    const int index = scroll + (my - panel_y() - 22) / PANEL_ROW_H;
    return index < count ? index : -1;
}

static int panel_scroll_hit(int mx, int my) {
    const int by = panel_y() + 22 + PANEL_ROWS * PANEL_ROW_H + 4;
    if (dv_in(mx, my, panel_x(), by, 96, 22)) return -1;
    if (dv_in(mx, my, panel_x() + 104, by, 96, 22)) return 1;
    return 0;
}

static void paint_panel(const char* title, char names[][13], int count, int scroll, const char* current) {
    const int px = panel_x();
    const int py = panel_y();
    dv_fill(px, py, PANEL_W, 20, 0x0050A0);
    dv_text(px + 8, py + 3, title, 0xFFFFFF);
    dv_fill(px, py + 22, PANEL_W, PANEL_ROWS * PANEL_ROW_H, 0xFFFFFF);
    dv_border(px, py + 22, PANEL_W, PANEL_ROWS * PANEL_ROW_H, 0x808080);
    for (int row = 0; row < PANEL_ROWS; row++) {
        const int index = scroll + row;
        if (index >= count) break;
        const int selected = current[0] && strcmp(names[index], current + 1) == 0;
        if (selected) dv_fill(px + 1, py + 22 + row * PANEL_ROW_H, PANEL_W - 2, PANEL_ROW_H, 0xCCE0FF);
        dv_text(px + 8, py + 22 + row * PANEL_ROW_H + 3, names[index], 0x000000);
    }
    const int by = py + 22 + PANEL_ROWS * PANEL_ROW_H + 4;
    dv_button(px, by, 96, 22, "Up", 0);
    dv_button(px + 104, by, 96, 22, "Down", 0);
}

static void paint_log(int x, int y, int w, int h) {
    dv_fill(x, y, w, h, 0x000000);
    dv_border(x, y, w, h, 0x808080);
    const int rows = (h - 8) / LINE_H;
    const int cols = (w - 12) / CHAR_W;
    const int start = dv_log_count > rows ? dv_log_count - rows : 0;
    for (int row = 0; row < rows && start + row < dv_log_count; row++) {
        dv_text_clip(x + 6, y + 4 + row * LINE_H, dv_log_lines[start + row], cols, 0x55FF55);
    }
}

static void paint_canvas(void) {
    const int zoom = pic_zoom();
    const int view = zoom * pic_size;
    const int ox = dv_x + PIC_X;
    const int oy = dv_y + PIC_Y;
    dv_fill(ox - 2, oy - 2, PIC_VIEW + 4, PIC_VIEW + 4, 0x808080);
    for (int cy = 0; cy < pic_size; cy++) {
        for (int cx = 0; cx < pic_size; cx++) {
            dv_fill(ox + cx * zoom, oy + cy * zoom, zoom, zoom, dv_palette[pic[cy * PIC_STRIDE + cx] & 15]);
        }
    }
    dv_border(ox - 1, oy - 1, view + 2, view + 2, 0x000000);

    const int px = dv_x + PREVIEW_X;
    const int py = dv_y + PIC_Y;
    dv_fill(px - 2, py - 2, PIC_STRIDE + 4, PIC_STRIDE + 4, 0xC0C0C0);
    dv_border(px - 1, py - 1, pic_size + 2, pic_size + 2, 0x000000);
    int current = -1;
    for (int y = 0; y < pic_size; y++) {
        for (int x = 0; x < pic_size; x++) {
            const int value = pic[y * PIC_STRIDE + x] & 15;
            if (value != current) {
                set_color(dv_palette[value]);
                current = value;
            }
            putpixel(px + x, py + y);
        }
    }

    if (pic_drawing && (pic_tool == TOOL_LINE || pic_tool == TOOL_RECT)) {
        const int sx = ox + pic_start_x * zoom + zoom / 2;
        const int sy = oy + pic_start_y * zoom + zoom / 2;
        const int ex = ox + pic_cur_x * zoom + zoom / 2;
        const int ey = oy + pic_cur_y * zoom + zoom / 2;
        const uint32_t color = dv_palette[pic_color] == 0xFFFFFF ? 0x000000 : dv_palette[pic_color];
        if (pic_tool == TOOL_LINE) {
            dv_line(sx, sy, ex, ey, color);
        } else {
            dv_line(sx, sy, ex, sy, color);
            dv_line(ex, sy, ex, ey, color);
            dv_line(ex, ey, sx, ey, color);
            dv_line(sx, ey, sx, sy, color);
        }
    }
}

static void paint_pictures(void) {
    static const char* const tools[4] = {"Pen", "Line", "Rect", "Fill"};
    static const char* const sizes[3] = {"16", "32", "48"};
    static const int size_values[3] = {16, 32, 48};
    const int x = dv_x;
    const int y = dv_y;
    for (int i = 0; i < 4; i++) dv_button(x + 10 + i * 66, y + 58, 62, 22, tools[i], pic_tool == i);
    dv_button(x + 10 + 4 * 66, y + 58, 62, 22, "Clear", 0);
    dv_text(x + 10, y + 88, "SIZE", 0x000000);
    for (int i = 0; i < 3; i++) dv_button(x + 60 + i * 44, y + 84, 40, 22, sizes[i], pic_size == size_values[i]);
    dv_button(x + 196, y + 84, 58, 22, "Save", 0);
    dv_button(x + 258, y + 84, 96, 22, "Save As", 0);
    dv_button(x + 358, y + 84, 52, 22, "New", 0);
    paint_canvas();
    for (int i = 0; i < 16; i++) {
        const int px = x + 290 + (i % 4) * 32;
        const int py = y + 112 + (i / 4) * 32;
        dv_fill(px, py, 28, 28, i == pic_color ? 0xFFFFFF : 0x404040);
        dv_fill(px + 2, py + 2, 24, 24, dv_palette[i]);
        if (i == pic_color) dv_border(px, py, 28, 28, 0x000000);
    }
    dv_fill(x + 290, y + 250, 124, 28, 0x404040);
    dv_fill(x + 292, y + 252, 120, 24, dv_palette[pic_color]);
    dv_text(x + 290, y + 286, "COLOR", 0x000000);
    dv_text(x + PREVIEW_X, y + 170, "PREVIEW", 0x000000);
    dv_text(x + PREVIEW_X, y + 190, pic_name[0] ? pic_name + 1 : "UNTITLED", 0x000000);
    paint_panel("BRUSH", pic_files, pic_count, pic_scroll, pic_name);
}

static void paint_code(void) {
    const int x = dv_x;
    const int y = dv_y;
    cd_scroll_to_cursor();
    dv_button(x + 10, y + 58, 48, 22, "New", 0);
    dv_button(x + 62, y + 58, 58, 22, "Save", 0);
    dv_button(x + 124, y + 58, 96, 22, "Save As", 0);
    dv_button(x + 224, y + 58, 84, 22, "Compile", 0);
    char title[NAME_LEN + 4];
    int n = 0;
    title[0] = '\0';
    dv_append(title, &n, cd_name[0] ? cd_name + 1 : "UNTITLED", sizeof(title));
    if (cd_modified) dv_append(title, &n, " *", sizeof(title));
    dv_text(x + 318, y + 62, title, 0x000000);

    dv_fill(x + CD_X - 2, y + CD_Y - 2, CD_COLS * CHAR_W + 4, CD_ROWS * LINE_H + 4, 0xFFFFFF);
    dv_border(x + CD_X - 2, y + CD_Y - 2, CD_COLS * CHAR_W + 4, CD_ROWS * LINE_H + 4, 0x808080);
    int index = 0;
    int row_number = 0;
    while (row_number < cd_top && index < cd_len) {
        if (cd_buf[index] == '\n') row_number++;
        index++;
    }
    for (int row = 0; row < CD_ROWS; row++) {
        if (index > cd_len) break;
        const int end = cd_line_end(index);
        char text[CD_COLS + 1];
        int count = 0;
        for (int i = index + cd_left; i < end && count < CD_COLS; i++) {
            const char c = cd_buf[i];
            text[count++] = (c >= 32 && c <= 126) ? c : ' ';
        }
        text[count] = '\0';
        if (count > 0) dv_text(x + CD_X, y + CD_Y + row * LINE_H, text, 0x000000);
        index = end + 1;
    }
    int cursor_row;
    int cursor_column;
    cd_cursor_pos(&cursor_row, &cursor_column);
    if (cursor_row >= cd_top && cursor_row < cd_top + CD_ROWS &&
        cursor_column >= cd_left && cursor_column <= cd_left + CD_COLS) {
        dv_fill(x + CD_X + (cursor_column - cd_left) * CHAR_W,
                y + CD_Y + (cursor_row - cd_top) * LINE_H, 2, 16, 0x0050A0);
    }
    paint_panel("DCE FILES", cd_files, cd_count, cd_scroll, cd_name);
    paint_log(x + CD_X - 2, y + 346, CD_COLS * CHAR_W + 4, 140);
}

static void paint_shell_lines(int x, int y, int w, int h, uint32_t color) {
    const int rows = (h - 8) / LINE_H;
    const int cols = (w - 12) / CHAR_W;
    const int start = shell_line_count > rows ? shell_line_count - rows : 0;
    for (int row = 0; row < rows && start + row < shell_line_count; row++) {
        dv_text_clip(x + 6, y + 4 + row * LINE_H, shell_lines[start + row], cols, color);
    }
}

static void paint_shell(void) {
    const int x = dv_x;
    const int y = dv_y;
    dv_button(x + 10, y + 58, 80, 22, "Clear", 0);
    dv_text(x + 104, y + 62, "CDE OUTPUT", 0x000000);
    dv_fill(x + 10, y + 88, DV_W - 20, DV_H - 88 - 34, 0x000000);
    dv_border(x + 10, y + 88, DV_W - 20, DV_H - 88 - 34, 0x808080);
    if (shell_graphics_active) {
        dv_fill(x + 10, y + 88, shell_graphics_width, shell_graphics_height, 0x000000);
        shell_blit_canvas();
        dv_border(x + 10, y + 88, shell_graphics_width, shell_graphics_height, 0x606060);
        dv_fill(x + 500, y + 88, DV_W - 510, DV_H - 88 - 34, 0x080D12);
        dv_text(x + 510, y + 92, "PRINT OUTPUT", 0x55FF55);
        paint_shell_lines(x + 500, y + 108, DV_W - 510, DV_H - 108 - 30, 0x55FF55);
    } else {
        paint_shell_lines(x + 10, y + 88, DV_W - 20, DV_H - 88 - 34, 0x55FF55);
    }
}

static void paint_all(void) {
    const int x = dv_x;
    const int y = dv_y;
    dv_fill(x - 4, y - 4, DV_W + 8, DV_H + 8, 0x202020);
    dv_fill(x, y, DV_W, DV_H, 0xC0C0C0);
    dv_fill(x, y, DV_W, DV_TITLE_H, 0x0050A0);
    dv_text(x + 10, y + 4, "DEVELOPER", 0xFFFFFF);
    const int close_x = x + DV_W - DV_BTN - 6;
    const int min_x = x + DV_W - 2 * DV_BTN - 10;
    dv_fill(min_x, y + 3, DV_BTN, DV_BTN, 0x3A6FD0);
    dv_fill(min_x + 4, y + 3 + DV_BTN - 6, DV_BTN - 8, 3, 0xFFFFFF);
    dv_fill(close_x, y + 3, DV_BTN, DV_BTN, 0xD04040);
    dv_text(close_x + 3, y + 4, "X", 0xFFFFFF);

    dv_fill(x, y + DV_TITLE_H, DV_W, 28, 0xD8D8D8);
    for (int i = 0; i < 3; i++) dv_button(x + 8 + i * 108, y + DV_TAB_Y + 2, 104, 24, dv_tab_names[i], dv_tab == i);

    if (dv_tab == TAB_BRUSH) paint_pictures();
    else if (dv_tab == TAB_CODE) paint_code();
    else paint_shell();

    dv_fill(x + 8, y + DV_H - DV_STATUS_H - 4, DV_W - 16, DV_STATUS_H, 0xD8D8D8);
    char status[80];
    int n = 0;
    status[0] = '\0';
    if (dv_status[0]) dv_append(status, &n, dv_status, sizeof(status));
    else if (dv_tab == TAB_BRUSH) dv_append(status, &n, "LMB DRAW  RMB ERASE  ^S SAVE", sizeof(status));
    else if (dv_tab == TAB_CODE) dv_append(status, &n, "^S SAVE  COMPILE CREATES .CDE AND SHOWS OUTPUT", sizeof(status));
    else dv_append(status, &n, "PRINT OUTPUT  |  COMPILE IN CODE  |  CLEAR HERE", sizeof(status));
    dv_text(x + 14, y + DV_H - DV_STATUS_H, status, 0x000000);
}

static void draw_all(void) {
    paint_all();
    dv_show();
}

static void draw_canvas_only(void) {
    paint_canvas();
    dv_show();
}

static void set_tab(int tab) {
    if (tab == dv_tab) return;
    dv_tab = tab;
    dv_status = "";
    if (tab != TAB_SHELL) refresh_lists();
}

static int hit_close(int mx, int my) {
    return dv_in(mx, my, dv_x + DV_W - DV_BTN - 6, dv_y + 3, DV_BTN, DV_BTN);
}

static int hit_minimize(int mx, int my) {
    return dv_in(mx, my, dv_x + DV_W - 2 * DV_BTN - 10, dv_y + 3, DV_BTN, DV_BTN);
}

static int hit_title(int mx, int my) {
    return dv_in(mx, my, dv_x, dv_y, DV_W - 2 * DV_BTN - 16, DV_TITLE_H);
}

static int hit_tab(int mx, int my) {
    for (int i = 0; i < 3; i++) {
        if (dv_in(mx, my, dv_x + 8 + i * 108, dv_y + DV_TAB_Y + 2, 104, 24)) return i;
    }
    return -1;
}

static void close_app(void) {
    dv_exists = 0;
    dv_minimized = 0;
    dv_reset();
}

static int may_close(void) {
    if ((!cd_modified && !pic_modified) || dv_armed) return 1;
    dv_armed = 1;
    dv_status = "UNSAVED! CLOSE AGAIN";
    return 0;
}

static int press_pictures(int mx, int my) {
    static const int size_values[3] = {16, 32, 48};
    const int x = dv_x;
    const int y = dv_y;
    for (int i = 0; i < 4; i++) {
        if (dv_in(mx, my, x + 10 + i * 66, y + 58, 62, 22)) {
            pic_tool = i;
            pic_drawing = 0;
            return 1;
        }
    }
    if (dv_in(mx, my, x + 10 + 4 * 66, y + 58, 62, 22)) {
        pic_clear();
        pic_modified = 1;
        return 1;
    }
    for (int i = 0; i < 3; i++) {
        if (dv_in(mx, my, x + 60 + i * 44, y + 84, 40, 22)) {
            pic_size = size_values[i];
            pic_clear();
            pic_modified = 0;
            pic_name[0] = '\0';
            return 1;
        }
    }
    if (dv_in(mx, my, x + 196, y + 84, 58, 22)) {
        pic_save(0);
        return 1;
    }
    if (dv_in(mx, my, x + 258, y + 84, 96, 22)) {
        pic_save(1);
        return 1;
    }
    if (dv_in(mx, my, x + 358, y + 84, 52, 22)) {
        pic_clear();
        pic_modified = 0;
        pic_name[0] = '\0';
        dv_status = "NEW PICTURE";
        return 1;
    }
    for (int i = 0; i < 16; i++) {
        if (dv_in(mx, my, x + 290 + (i % 4) * 32, y + 112 + (i / 4) * 32, 28, 28)) {
            pic_color = i;
            return 1;
        }
    }
    const int scroll = panel_scroll_hit(mx, my);
    if (scroll != 0) {
        pic_scroll += scroll;
        if (pic_scroll < 0) pic_scroll = 0;
        if (pic_scroll > pic_count - 1) pic_scroll = pic_count > 0 ? pic_count - 1 : 0;
        return 1;
    }
    const int row = panel_row_at(mx, my, pic_count, pic_scroll);
    if (row >= 0) {
        if (pic_open(pic_files[row])) {
            dv_status = "OPENED";
            dv_log_text("opened ", pic_files[row]);
        } else {
            dv_status = "CANNOT OPEN BMP";
            dv_log_text("cannot open ", pic_files[row]);
        }
        return 1;
    }
    int cx;
    int cy;
    if (pic_cell(mx, my, &cx, &cy, 0)) {
        if (pic_tool == TOOL_PEN) {
            pic_drawing = 1;
            pic_put(cx, cy, pic_color);
            pic_last_x = cx;
            pic_last_y = cy;
            pic_modified = 1;
        } else if (pic_tool == TOOL_FILL) {
            pic_fill(cx, cy, pic_color);
            pic_modified = 1;
        } else {
            pic_drawing = 1;
            pic_start_x = cx;
            pic_start_y = cy;
            pic_cur_x = cx;
            pic_cur_y = cy;
        }
        return 2;
    }
    return 0;
}

static int press_code(int mx, int my) {
    const int x = dv_x;
    const int y = dv_y;
    if (dv_in(mx, my, x + 10, y + 58, 48, 22)) {
        cd_new();
        dv_status = "NEW FILE";
        return 1;
    }
    if (dv_in(mx, my, x + 62, y + 58, 58, 22)) {
        cd_save(0);
        return 1;
    }
    if (dv_in(mx, my, x + 124, y + 58, 96, 22)) {
        cd_save(1);
        return 1;
    }
    if (dv_in(mx, my, x + 224, y + 58, 84, 22)) {
        return compile_current();
    }
    const int scroll = panel_scroll_hit(mx, my);
    if (scroll != 0) {
        cd_scroll += scroll;
        if (cd_scroll < 0) cd_scroll = 0;
        if (cd_scroll > cd_count - 1) cd_scroll = cd_count > 0 ? cd_count - 1 : 0;
        return 1;
    }
    const int row = panel_row_at(mx, my, cd_count, cd_scroll);
    if (row >= 0) {
        if (cd_load(cd_files[row])) {
            dv_status = "OPENED";
            dv_log_text("opened ", cd_files[row]);
        } else {
            dv_status = "CANNOT OPEN";
        }
        return 1;
    }
    if (dv_in(mx, my, x + CD_X, y + CD_Y, CD_COLS * CHAR_W, CD_ROWS * LINE_H)) {
        cd_click(mx, my);
        return 1;
    }
    return 0;
}

static int press_shell(int mx, int my) {
    if (dv_in(mx, my, dv_x + 10, dv_y + 58, 80, 22)) {
        shell_clear_output();
        dv_status = "OUTPUT CLEARED";
        return 1;
    }
    return 0;
}

static int hold_pictures(int mx, int my, int left, int right) {
    int cx;
    int cy;
    if (pic_tool == TOOL_PEN && (left || right) && pic_cell(mx, my, &cx, &cy, 0)) {
        if (cx == pic_last_x && cy == pic_last_y) return 0;
        const int color = right ? PIC_BG : pic_color;
        if (pic_last_x >= 0) pic_draw_line(pic_last_x, pic_last_y, cx, cy, color);
        else pic_put(cx, cy, color);
        pic_last_x = cx;
        pic_last_y = cy;
        pic_modified = 1;
        return 2;
    }
    if ((pic_tool == TOOL_LINE || pic_tool == TOOL_RECT) && left && pic_drawing) {
        pic_cell(mx, my, &cx, &cy, 1);
        if (cx != pic_cur_x || cy != pic_cur_y) {
            pic_cur_x = cx;
            pic_cur_y = cy;
            return 2;
        }
    }
    return 0;
}

static int release_pictures(int mx, int my) {
    int cx;
    int cy;
    int result = 0;
    if (pic_drawing && (pic_tool == TOOL_LINE || pic_tool == TOOL_RECT)) {
        pic_cell(mx, my, &cx, &cy, 1);
        if (pic_tool == TOOL_LINE) pic_draw_line(pic_start_x, pic_start_y, cx, cy, pic_color);
        else pic_draw_rect(pic_start_x, pic_start_y, cx, cy, pic_color);
        pic_modified = 1;
        result = 2;
    }
    pic_drawing = 0;
    pic_last_x = -1;
    pic_last_y = -1;
    return result;
}

static int handle_key(char key) {
    if (key == 27) return -1;
    if (key == 17) {
        if (may_close()) {
            close_app();
            return -2;
        }
        return 1;
    }
    dv_armed = 0;
    int changed = 0;
    if (dv_tab == TAB_CODE) {
        if (key == 19) cd_save(0);
        else if (key == 2) { if (cd_cur > 0) cd_cur--; }
        else if (key == 6) { if (cd_cur < cd_len) cd_cur++; }
        else if (key == 16) cd_move_up();
        else if (key == 14) cd_move_down();
        else if (key == 1) cd_cur = cd_line_start(cd_cur);
        else if (key == 5) cd_cur = cd_line_end(cd_cur);
        else if (key == '\b') cd_backspace();
        else if (key == '\n') cd_insert('\n');
        else if (key == '\t') { for (int i = 0; i < 4; i++) cd_insert(' '); }
        else if (key >= 32 && key <= 126) cd_insert(key);
        else return 0;
        changed = 1;
    } else if (dv_tab == TAB_BRUSH) {
        if (key == 19) {
            pic_save(0);
            changed = 1;
        }
    }
    if (changed && key != 19) dv_status = "";
    return changed;
}

static void repaint_moved(int old_x, int old_y) {
    const int screen_h = (int)fb_info.framebuffer_height;
    int x0 = (old_x < dv_x ? old_x : dv_x) - 4;
    int y0 = (old_y < dv_y ? old_y : dv_y) - 4;
    int x1 = (old_x > dv_x ? old_x : dv_x) + DV_W + 4;
    int y1 = (old_y > dv_y ? old_y : dv_y) + DV_H + 4;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (y1 > screen_h - DV_TASKBAR) y1 = screen_h - DV_TASKBAR;
    redraw_desk_region(x0, y0, x1 - x0, y1 - y0);
    gfx_set_clip(x0, y0, x1 - x0, y1 - y0);
    shell_paint();
    paint_all();
    gfx_reset_clip();
    dv_show();
}

static void run_loop(void) {
    int previous_buttons = mouse_get_buttons();
    while (1) {
        const char key = keyboard_try_getchar_poll();
        const int buttons = mouse_get_buttons();
        int mouse_x;
        int mouse_y;
        mouse_get_position(&mouse_x, &mouse_y);
        const int left = (buttons & 1) != 0;
        const int right = (buttons & 2) != 0;
        const int pressed = left && !(previous_buttons & 1);
        const int released = !left && (previous_buttons & 1);
        previous_buttons = buttons;
        int dirty = 0;

        if (pressed) {
            if (hit_close(mouse_x, mouse_y)) {
                if (may_close()) {
                    close_app();
                    return;
                }
                dirty = 1;
            } else if (hit_minimize(mouse_x, mouse_y) || taskbar_developer_button_hit(mouse_x, mouse_y)) {
                dv_minimized = 1;
                return;
            } else if (hit_title(mouse_x, mouse_y)) {
                dv_dragging = 1;
                dv_drag_dx = mouse_x - dv_x;
                dv_drag_dy = mouse_y - dv_y;
            } else {
                const int tab = hit_tab(mouse_x, mouse_y);
                if (tab >= 0) {
                    set_tab(tab);
                    dirty = 1;
                } else if (dv_tab == TAB_BRUSH) {
                    dirty = press_pictures(mouse_x, mouse_y);
                } else if (dv_tab == TAB_CODE) {
                    dirty = press_code(mouse_x, mouse_y);
                } else {
                    dirty = press_shell(mouse_x, mouse_y);
                }
            }
        } else if ((left || right) && dv_tab == TAB_BRUSH && !dv_dragging) {
            const int result = hold_pictures(mouse_x, mouse_y, left, right);
            if (result > dirty) dirty = result;
        }
        if (released && dv_tab == TAB_BRUSH) {
            const int result = release_pictures(mouse_x, mouse_y);
            if (result > dirty) dirty = result;
        }
        if (!left) dv_dragging = 0;

        if (dv_minimize_request) {
            dv_minimize_request = 0;
            dv_minimized = 1;
            return;
        }

        if (dv_dragging) {
            const int old_win_x = dv_x;
            const int old_win_y = dv_y;
            int new_x = mouse_x - dv_drag_dx;
            int new_y = mouse_y - dv_drag_dy;
            const int max_x = (int)fb_info.framebuffer_width - DV_W;
            const int max_y = (int)fb_info.framebuffer_height - DV_TASKBAR - DV_H;
            if (new_x > max_x) new_x = max_x;
            if (new_y > max_y) new_y = max_y;
            if (new_x < 0) new_x = 0;
            if (new_y < 0) new_y = 0;
            if (new_x != old_win_x || new_y != old_win_y) {
                dv_x = new_x;
                dv_y = new_y;
                repaint_moved(old_win_x, old_win_y);
            }
        }

        if (key != 0) {
            const int result = handle_key(key);
            if (result == -1) {
                dv_minimized = 1;
                return;
            }
            if (result == -2) return;
            if (result > 0) dirty = 1;
        }

        if (dirty == 1) draw_all();
        else if (dirty == 2) draw_canvas_only();
    }
}

void developer_run(void) {
    if (!fb_info.has_framebuffer) return;

    if (!dv_placed) {
        dv_x = ((int)fb_info.framebuffer_width - DV_W) / 2;
        dv_y = ((int)fb_info.framebuffer_height - DV_TASKBAR - DV_H) / 2;
        if (dv_x < 0) dv_x = 0;
        if (dv_y < 0) dv_y = 0;
        dv_placed = 1;
    }
    if (!dv_exists) {
        dv_reset();
        dv_exists = 1;
        developer_log("developer started");
    }
    dv_minimized = 0;
    dv_minimize_request = 0;
    dv_dragging = 0;
    refresh_lists();
    dv_active = 1;
    taskbar_refresh();
    draw_all();
    run_loop();
    dv_active = 0;
}
