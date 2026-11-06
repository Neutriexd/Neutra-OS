#include "editor.h"
#include "idt.h"
#include "vga.h"
#include "kernel.h"
#include "graphics.h"
#include "fat.h"
#include "shell_window.h"
#include "taskbar_ui.h"

void gfx_present(void);
void gfx_set_clip(int x, int y, int w, int h);
void gfx_reset_clip(void);
void redraw_desk_region(int x, int y, int width, int height);

extern DISK kernel_fat_disk;
extern int kernel_fat_mounted;
extern char keyboard_getchar_poll(void);

#define ED_W 640
#define ED_H 420
#define ED_BTN 16
#define ED_TITLE_H 22
#define ED_TASKBAR 36
#define ED_BUF 4096
#define ED_LINE_H 18
#define ED_CHAR_W 12
#define ED_TEXT_X 10
#define ED_TEXT_Y 50
#define ED_COLS 51
#define ED_ROWS 18
#define ED_STATUS_H 22
#define ED_NAME_MAX 64

static char ed_buf[ED_BUF];
static int ed_len;
static int ed_cur;
static int ed_top;
static int ed_left;
static int ed_modified;
static int ed_armed;
static char ed_name[ED_NAME_MAX];
static const char* ed_status = "";

static int ed_x = 100;
static int ed_y = 80;
static int ed_placed;
static int ed_exists;
static int ed_minimized;
static int ed_active;
static int ed_minimize_request;
static int ed_dragging;
static int ed_drag_dx;
static int ed_drag_dy;
static int ed_menu_open;
static int ed_menu_hover = -1;

int editor_is_open(void) { return ed_exists; }
int editor_is_minimized(void) { return ed_exists && ed_minimized; }
int editor_is_active(void) { return ed_active; }
void editor_request_minimize(void) { ed_minimize_request = 1; }

static void ed_reset(void) {
    ed_len = 0;
    ed_cur = 0;
    ed_top = 0;
    ed_left = 0;
    ed_modified = 0;
    ed_armed = 0;
    ed_name[0] = '\0';
    ed_status = "";
}

void init_editor(void) {
    ed_reset();
}

static int ed_line_start(int index) {
    while (index > 0 && ed_buf[index - 1] != '\n') index--;
    return index;
}

static int ed_line_end(int index) {
    while (index < ed_len && ed_buf[index] != '\n') index++;
    return index;
}

static void ed_cursor_pos(int* line, int* column) {
    int l = 0;
    for (int i = 0; i < ed_cur; i++) {
        if (ed_buf[i] == '\n') l++;
    }
    *line = l;
    *column = ed_cur - ed_line_start(ed_cur);
}

static void ed_insert(char c) {
    if (ed_len >= ED_BUF - 1) {
        ed_status = "BUFFER FULL";
        return;
    }
    for (int i = ed_len; i > ed_cur; i--) ed_buf[i] = ed_buf[i - 1];
    ed_buf[ed_cur++] = c;
    ed_len++;
    ed_modified = 1;
}

static void ed_backspace(void) {
    if (ed_cur <= 0) return;
    for (int i = ed_cur - 1; i < ed_len - 1; i++) ed_buf[i] = ed_buf[i + 1];
    ed_cur--;
    ed_len--;
    ed_modified = 1;
}

static void ed_move_up(void) {
    const int start = ed_line_start(ed_cur);
    const int column = ed_cur - start;
    if (start == 0) return;
    const int prev_end = start - 1;
    const int prev_start = ed_line_start(prev_end);
    const int prev_len = prev_end - prev_start;
    ed_cur = prev_start + (column < prev_len ? column : prev_len);
}

static void ed_move_down(void) {
    const int start = ed_line_start(ed_cur);
    const int column = ed_cur - start;
    const int end = ed_line_end(ed_cur);
    if (end >= ed_len) return;
    const int next_start = end + 1;
    const int next_end = ed_line_end(next_start);
    const int next_len = next_end - next_start;
    ed_cur = next_start + (column < next_len ? column : next_len);
}

static void ed_scroll(void) {
    int line;
    int column;
    ed_cursor_pos(&line, &column);
    if (line < ed_top) ed_top = line;
    if (line >= ed_top + ED_ROWS) ed_top = line - ED_ROWS + 1;
    if (column < ed_left) ed_left = column;
    if (column >= ed_left + ED_COLS) ed_left = column - ED_COLS + 1;
}

static void ed_click(int mouse_x, int mouse_y) {
    const int row = (mouse_y - (ed_y + ED_TEXT_Y)) / ED_LINE_H;
    const int column = (mouse_x - (ed_x + ED_TEXT_X)) / ED_CHAR_W + ed_left;
    const int line = ed_top + row;

    int index = 0;
    int current = 0;
    while (current < line && index < ed_len) {
        if (ed_buf[index] == '\n') current++;
        index++;
    }
    if (current < line) {
        ed_cur = ed_len;
        return;
    }
    const int end = ed_line_end(index);
    const int line_len = end - index;
    ed_cur = index + (column < line_len ? column : line_len);
}

static void ed_set_name(const char* input) {
    int n = 0;
    if (input[0] != '/') ed_name[n++] = '/';
    for (int i = 0; input[i] && n < ED_NAME_MAX - 1; i++) {
        char c = input[i];
        if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
        ed_name[n++] = c;
    }
    ed_name[n] = '\0';
}

static void ed_load(const char* path) {
    ed_reset();
    ed_set_name(path);
    if (!kernel_fat_mounted) {
        ed_status = "NO DISK";
        return;
    }
    FAT_File* file = FAT_Open(&kernel_fat_disk, ed_name);
    if (file == NULL) {
        ed_status = "NEW FILE";
        return;
    }
    if (file->IsDirectory) {
        FAT_Close(file);
        ed_name[0] = '\0';
        ed_status = "IS A FOLDER";
        return;
    }
    while (ed_len < ED_BUF - 1) {
        const uint32_t count = FAT_Read(&kernel_fat_disk, file, ED_BUF - 1 - ed_len, ed_buf + ed_len);
        if (count == 0) break;
        ed_len += (int)count;
    }
    FAT_Close(file);
    for (int i = 0; i < ed_len; i++) {
        if (ed_buf[i] == '\r') {
            for (int j = i; j < ed_len - 1; j++) ed_buf[j] = ed_buf[j + 1];
            ed_len--;
            i--;
        }
    }
    ed_status = "OPENED";
}

void editor_open_file(const char* path) {
    ed_load(path);
    ed_exists = 1;
}

static void ed_text(int x, int y, const char* text, uint32_t color) {
    vga_draw_text_at(x, y, text, color);
}

static void ed_append(char* out, int* n, const char* text, int max) {
    for (int i = 0; text[i] && *n < max - 1; i++) out[(*n)++] = text[i];
    out[*n] = '\0';
}

static void ed_append_int(char* out, int* n, int value, int max) {
    char digits[16];
    int_to_str(value, digits, sizeof(digits));
    ed_append(out, n, digits, max);
}

#define ED_MENU_ITEMS 5
#define ED_MENU_X 6
#define ED_MENU_Y 46
#define ED_MENU_W 200
#define ED_MENU_ROW 24

static const char* const ed_menu_labels[ED_MENU_ITEMS] = {"New", "Open...", "Save", "Save As...", "Exit"};
static const char* const ed_menu_keys[ED_MENU_ITEMS] = {"", "^O", "^S", "", "^Q"};

static int ed_menu_item_y(int item) {
    return ed_y + ED_MENU_Y + 4 + item * ED_MENU_ROW + (item == ED_MENU_ITEMS - 1 ? 6 : 0);
}

static int ed_menu_height(void) {
    return 4 + ED_MENU_ITEMS * ED_MENU_ROW + 6 + 4;
}

static int ed_menu_item_at(int mx, int my) {
    if (mx < ed_x + ED_MENU_X || mx >= ed_x + ED_MENU_X + ED_MENU_W) return -1;
    for (int i = 0; i < ED_MENU_ITEMS; i++) {
        const int top = ed_menu_item_y(i);
        if (my >= top && my < top + ED_MENU_ROW) return i;
    }
    return -1;
}

static int ed_hit_file_button(int mx, int my) {
    return mx >= ed_x + ED_MENU_X && mx < ed_x + ED_MENU_X + 60 &&
           my >= ed_y + ED_TITLE_H + 1 && my < ed_y + ED_TITLE_H + 23;
}

static void ed_paint(void) {
    ed_scroll();

    const int x = ed_x;
    const int y = ed_y;

    draw_rect(x - 4, y - 4, ED_W + 8, ED_H + 8, 0x202020);
    draw_rect(x, y, ED_W, ED_H, 0xC0C0C0);
    draw_rect(x, y, ED_W, ED_TITLE_H, 0x0050A0);

    char title[80];
    int n = 0;
    title[0] = '\0';
    ed_append(title, &n, "EDITOR - ", sizeof(title));
    ed_append(title, &n, ed_name[0] ? ed_name : "UNTITLED", sizeof(title));
    if (ed_modified) ed_append(title, &n, " *", sizeof(title));
    ed_text(x + 10, y + 4, title, 0xFFFFFF);

    const int close_x = x + ED_W - ED_BTN - 6;
    const int min_x = x + ED_W - 2 * ED_BTN - 10;
    draw_rect(min_x, y + 3, ED_BTN, ED_BTN, 0x3A6FD0);
    draw_rect(min_x + 4, y + 3 + ED_BTN - 6, ED_BTN - 8, 3, 0xFFFFFF);
    draw_rect(close_x, y + 3, ED_BTN, ED_BTN, 0xD04040);
    ed_text(close_x + 3, y + 4, "X", 0xFFFFFF);

    draw_rect(x, y + ED_TITLE_H, ED_W, 24, 0xD8D8D8);
    draw_rect(x + ED_MENU_X, y + ED_TITLE_H + 1, 60, 22, ed_menu_open ? 0xA8A8A8 : 0xD8D8D8);
    ed_text(x + ED_MENU_X + 8, y + ED_TITLE_H + 5, "File", 0x000000);

    draw_rect(x + ED_TEXT_X - 2, y + ED_TEXT_Y - 2, ED_COLS * ED_CHAR_W + 4, ED_ROWS * ED_LINE_H + 4, 0xFFFFFF);

    int index = 0;
    int line_no = 0;
    while (line_no < ed_top && index < ed_len) {
        if (ed_buf[index] == '\n') line_no++;
        index++;
    }
    for (int row = 0; row < ED_ROWS; row++) {
        if (index > ed_len) break;
        const int end = ed_line_end(index);
        char text[ED_COLS + 1];
        int count = 0;
        for (int i = index + ed_left; i < end && count < ED_COLS; i++) {
            const char c = ed_buf[i];
            text[count++] = (c >= 32 && c <= 126) ? c : ' ';
        }
        text[count] = '\0';
        if (count > 0) ed_text(x + ED_TEXT_X, y + ED_TEXT_Y + row * ED_LINE_H, text, 0x000000);
        index = end + 1;
    }

    int cur_line;
    int cur_col;
    ed_cursor_pos(&cur_line, &cur_col);
    if (cur_line >= ed_top && cur_line < ed_top + ED_ROWS &&
        cur_col >= ed_left && cur_col <= ed_left + ED_COLS) {
        draw_rect(x + ED_TEXT_X + (cur_col - ed_left) * ED_CHAR_W,
                  y + ED_TEXT_Y + (cur_line - ed_top) * ED_LINE_H, 2, 16, 0x0050A0);
    }

    draw_rect(x + 8, y + ED_H - ED_STATUS_H - 4, ED_W - 16, ED_STATUS_H, 0xD8D8D8);
    char status[64];
    int s = 0;
    status[0] = '\0';
    ed_append(status, &s, "LN ", sizeof(status));
    ed_append_int(status, &s, cur_line + 1, sizeof(status));
    ed_append(status, &s, " COL ", sizeof(status));
    ed_append_int(status, &s, cur_col + 1, sizeof(status));
    ed_append(status, &s, "  ", sizeof(status));
    ed_append(status, &s, ed_status[0] ? ed_status : "^S SAVE ^O OPEN ^Q QUIT", sizeof(status));
    ed_text(x + 14, y + ED_H - ED_STATUS_H, status, 0x000000);

    if (ed_menu_open) {
        const int menu_x = x + ED_MENU_X;
        const int menu_y = y + ED_MENU_Y;
        draw_rect(menu_x - 1, menu_y - 1, ED_MENU_W + 2, ed_menu_height() + 2, 0x202020);
        draw_rect(menu_x, menu_y, ED_MENU_W, ed_menu_height(), 0xF0F0F0);
        for (int i = 0; i < ED_MENU_ITEMS; i++) {
            const int top = ed_menu_item_y(i);
            if (i == ED_MENU_ITEMS - 1) draw_rect(menu_x + 6, top - 4, ED_MENU_W - 12, 1, 0x909090);
            if (i == ed_menu_hover) draw_rect(menu_x + 2, top, ED_MENU_W - 4, ED_MENU_ROW, 0x316AC5);
            const uint32_t color = i == ed_menu_hover ? 0xFFFFFF : 0x000000;
            ed_text(menu_x + 12, top + 5, ed_menu_labels[i], color);
            if (ed_menu_keys[i][0]) ed_text(menu_x + ED_MENU_W - 40, top + 5, ed_menu_keys[i], color);
        }
    }
}

static void ed_draw(void) {
    ed_paint();
    gfx_present();
}

static int ed_prompt(const char* title, char* out, int max) {
    const int x = ed_x + (ED_W - 344) / 2;
    const int y = ed_y + ED_H / 2 - 43;
    int length = 0;
    out[0] = '\0';
    while (1) {
        draw_rect(x - 3, y - 3, 350, 92, 0x202020);
        draw_rect(x, y, 344, 86, 0xC0C0C0);
        draw_rect(x, y, 344, 20, 0x0050A0);
        ed_text(x + 10, y + 3, title, 0xFFFFFF);
        ed_text(x + 12, y + 28, "FILE NAME:", 0x000000);
        draw_rect(x + 12, y + 47, 316, 20, 0xFFFFFF);
        ed_text(x + 18, y + 50, out, 0x000000);
        gfx_present();

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
             value == '.' || value == '_' || value == '-' || value == '/') && length < max - 1) {
            out[length++] = value;
            out[length] = '\0';
        }
    }
}

static void ed_save(void) {
    if (!kernel_fat_mounted) {
        ed_status = "NO DISK";
        return;
    }
    if (!ed_name[0]) {
        char name[ED_NAME_MAX];
        if (!ed_prompt("SAVE AS", name, sizeof(name))) {
            ed_status = "CANCELLED";
            return;
        }
        ed_set_name(name);
    }

    FAT_File* file = FAT_Open(&kernel_fat_disk, ed_name);
    int created = 0;
    if (file != NULL && file->IsDirectory) {
        FAT_Close(file);
        ed_status = "IS A FOLDER";
        return;
    }
    if (file == NULL) {
        file = FAT_Create(&kernel_fat_disk, ed_name);
        created = file != NULL;
    }
    if (file == NULL) {
        ed_status = "CREATE FAILED";
        return;
    }
    const int saved = FAT_Save(&kernel_fat_disk, file, ed_buf, (uint32_t)ed_len);
    FAT_Close(file);
    if (!saved) {
        if (created) FAT_Remove(&kernel_fat_disk, ed_name, false);
        ed_status = "SAVE FAILED (TOO BIG?)";
        return;
    }
    ed_modified = 0;
    ed_status = "SAVED";
}

static void ed_open_prompt(void) {
    if (ed_modified && !ed_armed) {
        ed_armed = 1;
        ed_status = "UNSAVED! ^O AGAIN";
        return;
    }
    ed_armed = 0;
    char name[ED_NAME_MAX];
    if (!ed_prompt("OPEN FILE", name, sizeof(name))) {
        ed_status = "CANCELLED";
        return;
    }
    ed_load(name);
}

static void ed_save_as(void) {
    char name[ED_NAME_MAX];
    if (!ed_prompt("SAVE AS", name, sizeof(name))) {
        ed_status = "CANCELLED";
        return;
    }
    ed_set_name(name);
    ed_save();
}

static void ed_new(void) {
    if (ed_modified && !ed_armed) {
        ed_armed = 1;
        ed_status = "UNSAVED! NEW AGAIN";
        return;
    }
    ed_reset();
    ed_status = "NEW";
}

static void ed_close(void) {
    ed_exists = 0;
    ed_minimized = 0;
    ed_menu_open = 0;
    ed_menu_hover = -1;
    ed_reset();
}

static int ed_may_close(void) {
    if (!ed_modified || ed_armed) return 1;
    ed_armed = 1;
    ed_status = "UNSAVED! CLOSE AGAIN";
    return 0;
}

static int ed_menu_action(int item) {
    if (item == 0) ed_new();
    else if (item == 1) ed_open_prompt();
    else if (item == 2) ed_save();
    else if (item == 3) ed_save_as();
    else if (item == 4) {
        if (ed_may_close()) {
            ed_close();
            return 1;
        }
    }
    return 0;
}

static int ed_hit_close(int mx, int my) {
    const int cx = ed_x + ED_W - ED_BTN - 6;
    return mx >= cx && mx < cx + ED_BTN && my >= ed_y + 3 && my < ed_y + 3 + ED_BTN;
}

static int ed_hit_minimize(int mx, int my) {
    const int cx = ed_x + ED_W - 2 * ED_BTN - 10;
    return mx >= cx && mx < cx + ED_BTN && my >= ed_y + 3 && my < ed_y + 3 + ED_BTN;
}

static int ed_hit_title(int mx, int my) {
    return mx >= ed_x && mx < ed_x + ED_W - 2 * ED_BTN - 16 && my >= ed_y && my < ed_y + ED_TITLE_H;
}

static int ed_hit_text(int mx, int my) {
    return mx >= ed_x + ED_TEXT_X && mx < ed_x + ED_TEXT_X + ED_COLS * ED_CHAR_W &&
           my >= ed_y + ED_TEXT_Y && my < ed_y + ED_TEXT_Y + ED_ROWS * ED_LINE_H;
}

static void ed_repaint_moved(int old_x, int old_y) {
    const int screen_h = (int)fb_info.framebuffer_height;
    int x0 = (old_x < ed_x ? old_x : ed_x) - 4;
    int y0 = (old_y < ed_y ? old_y : ed_y) - 4;
    int x1 = (old_x > ed_x ? old_x : ed_x) + ED_W + 4;
    int y1 = (old_y > ed_y ? old_y : ed_y) + ED_H + 4;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (y1 > screen_h - ED_TASKBAR) y1 = screen_h - ED_TASKBAR;

    redraw_desk_region(x0, y0, x1 - x0, y1 - y0);
    gfx_set_clip(x0, y0, x1 - x0, y1 - y0);
    shell_paint();
    ed_paint();
    gfx_reset_clip();
    gfx_present();
}

static void ed_loop(void) {
    int previous_left_button = mouse_get_buttons() & 1;
    while (1) {
        const char key = keyboard_try_getchar_poll();
        const int left_button = mouse_get_buttons() & 1;
        int mouse_x;
        int mouse_y;
        mouse_get_position(&mouse_x, &mouse_y);
        const int pressed = left_button && !previous_left_button;
        previous_left_button = left_button;
        int dirty = 0;

        if (ed_menu_open) {
            const int hover = ed_menu_item_at(mouse_x, mouse_y);
            if (hover != ed_menu_hover) {
                ed_menu_hover = hover;
                dirty = 1;
            }
        }

        if (pressed) {
            if (ed_menu_open) {
                const int item = ed_menu_item_at(mouse_x, mouse_y);
                ed_menu_open = 0;
                ed_menu_hover = -1;
                dirty = 1;
                if (item >= 0 && ed_menu_action(item)) return;
            } else if (ed_hit_close(mouse_x, mouse_y)) {
                if (ed_may_close()) {
                    ed_close();
                    return;
                }
                dirty = 1;
            } else if (ed_hit_file_button(mouse_x, mouse_y)) {
                ed_menu_open = 1;
                ed_menu_hover = -1;
                dirty = 1;
            } else if (ed_hit_minimize(mouse_x, mouse_y) ||
                       taskbar_editor_button_hit(mouse_x, mouse_y)) {
                ed_minimized = 1;
                return;
            } else if (ed_hit_title(mouse_x, mouse_y)) {
                ed_dragging = 1;
                ed_drag_dx = mouse_x - ed_x;
                ed_drag_dy = mouse_y - ed_y;
            } else if (ed_hit_text(mouse_x, mouse_y)) {
                ed_click(mouse_x, mouse_y);
                dirty = 1;
            }
        }
        if (!left_button) ed_dragging = 0;
        if (ed_minimize_request) {
            ed_minimize_request = 0;
            ed_minimized = 1;
            return;
        }

        if (ed_dragging) {
            const int old_win_x = ed_x;
            const int old_win_y = ed_y;
            int new_x = mouse_x - ed_drag_dx;
            int new_y = mouse_y - ed_drag_dy;
            const int max_x = (int)fb_info.framebuffer_width - ED_W;
            const int max_y = (int)fb_info.framebuffer_height - ED_TASKBAR - ED_H;
            if (new_x > max_x) new_x = max_x;
            if (new_y > max_y) new_y = max_y;
            if (new_x < 0) new_x = 0;
            if (new_y < 0) new_y = 0;
            if (new_x != old_win_x || new_y != old_win_y) {
                ed_x = new_x;
                ed_y = new_y;
                ed_repaint_moved(old_win_x, old_win_y);
            }
        }

        if (key != 0) {
            if (key == 27) {
                if (ed_menu_open) {
                    ed_menu_open = 0;
                    ed_menu_hover = -1;
                    dirty = 1;
                } else {
                    ed_minimized = 1;
                    return;
                }
            } else if (key == 17) {
                if (ed_may_close()) {
                    ed_close();
                    return;
                }
                dirty = 1;
            } else {
                if (key != 15) ed_armed = 0;
                if (key == 19) ed_save();
                else if (key == 15) ed_open_prompt();
                else if (key == 2) { if (ed_cur > 0) ed_cur--; }
                else if (key == 6) { if (ed_cur < ed_len) ed_cur++; }
                else if (key == 16) ed_move_up();
                else if (key == 14) ed_move_down();
                else if (key == 1) ed_cur = ed_line_start(ed_cur);
                else if (key == 5) ed_cur = ed_line_end(ed_cur);
                else if (key == '\b') ed_backspace();
                else if (key == '\n') ed_insert('\n');
                else if (key == '\t') { for (int i = 0; i < 4; i++) ed_insert(' '); }
                else if (key >= 32 && key <= 126) ed_insert(key);
                else continue;
                if (key != 19 && key != 15) ed_status = "";
                dirty = 1;
            }
        }

        if (dirty) ed_draw();
    }
}

void editor_run(void) {
    if (!fb_info.has_framebuffer) return;

    if (!ed_placed) {
        const int screen_w = (int)fb_info.framebuffer_width;
        const int screen_h = (int)fb_info.framebuffer_height;
        ed_x = (screen_w - ED_W) / 2;
        ed_y = (screen_h - ED_TASKBAR - ED_H) / 2;
        if (ed_x < 0) ed_x = 0;
        if (ed_y < 0) ed_y = 0;
        ed_placed = 1;
    }
    if (!ed_exists) {
        ed_reset();
        ed_exists = 1;
    }

    ed_minimized = 0;
    ed_minimize_request = 0;
    ed_dragging = 0;
    ed_menu_open = 0;
    ed_menu_hover = -1;
    ed_active = 1;
    taskbar_refresh();
    ed_draw();
    ed_loop();
    ed_active = 0;
}