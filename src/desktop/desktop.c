#include "desktop.h"
#include "vga.h"
#include "graphics.h"
#include "font.h"
#include "menu.h"

#define COLOR_TOP       0x071426
#define COLOR_BOTTOM    0x102E52
#define COLOR_ACCENT    0x2677D8
#define COLOR_ACCENT_LT 0x54B7E8

static int icon_x[3] = {24, 24, 24};
static int icon_y[3] = {24, 84, 144};
static int extra_icon_visible;
static int hot_icon = -1;
static int dragging_icon = -1;
static int drag_dx, drag_dy;
static int last_click_icon = -1;
static unsigned int click_window;
static int previous_left;
static int previous_right;

static int intersects(int x, int y, int w, int h, int rx, int ry, int rw, int rh) {
    return x < rx + rw && x + w > rx && y < ry + rh && y + h > ry;
}

static void draw_shortcut(int x, int y, const char* label, int kind, int hot, int rx, int ry, int rw, int rh) {
    if (intersects(x, y, 34, 34, rx, ry, rw, rh)) {
        if (hot) draw_rect(x - 5, y - 5, 44, 60, 0x28547D);
        if (kind == 0) {
            draw_rect(x + 3, y + 3, 27, 25, hot ? 0xE9F8FF : 0xC8E7F5);
            draw_rect(x + 3, y + 3, 27, 5, hot ? COLOR_ACCENT_LT : COLOR_ACCENT);
            draw_rect(x + 3, y + 29, 27, 2, hot ? 0x83C9E8 : 0x5A88A8);
            draw_rect(x + 6, y + 12, 21, 2, hot ? 0x8DC7E4 : 0x6D9DB8);
            draw_rect(x + 6, y + 18, 17, 2, hot ? 0x8DC7E4 : 0x6D9DB8);
        } else {
            draw_rect(x + 4, y + 3, 25, 27, hot ? 0xFFEB91 : 0xF7D96A);
            draw_rect(x + 4, y + 3, 15, 5, hot ? 0xF5D55A : 0xE6BD3C);
            draw_rect(x + 4, y + 8, 25, 19, hot ? 0xFFF1B5 : 0xFFE99A);
            draw_rect(x + 7, y + 13, 19, 2, 0xE2C66D);
        }
    }
    if (intersects(x - 4, y + 37, 102, 16, rx, ry, rw, rh)) {
        const uint32_t text_color = hot ? 0xFFFFFF : 0xE4EDF7;
        vga_draw_text_at(x - 4, y + 37, label, text_color);
        vga_draw_text_at(x - 3, y + 37, label, text_color);
    }
}

void redraw_desk_region(int x, int y, int width, int height) {
    if (!fb_info.has_framebuffer || width <= 0 || height <= 0) return;
    const int sw = (int)fb_info.framebuffer_width;
    const int sh = (int)fb_info.framebuffer_height;
    gfx_set_clip(x, y, width, height);

    /* Deep blue desktop with a soft vertical light wash. */
    for (int py = 0; py < sh; py += 4) {
        const int t = py * 100 / (sh ? sh : 1);
        const uint32_t r = 7 + (12 * t) / 100;
        const uint32_t g = 20 + (32 * t) / 100;
        const uint32_t b = 38 + (47 * t) / 100;
        const uint32_t color = (r << 16) | (g << 8) | b;
        draw_rect(0, py, sw, 4, color);
    }

    /* Layered curved ribbons add depth while staying cheap to redraw. */
    const int cx = sw * 3 / 4;
    const int cy = sh * 2 / 5;
    int radius = sw / 3;
    if (radius < 120) radius = 120;
    for (int band = 0; band < 5; band++) {
        const int inset = band * 22;
        const uint32_t shade = band & 1 ? 0x123D70 : 0x164A83;
        for (int yy = cy - radius + inset; yy < cy + radius - inset; yy += 3) {
            int dy = yy - cy;
            if (dy < 0) dy = -dy;
            const int span = radius - inset - dy;
            if (span > 0) draw_rect(cx - span, yy, span * 2, 3, shade);
        }
    }
    draw_rect(0, sh - 3, sw, 3, COLOR_ACCENT);

    /* Desktop shortcuts */
    draw_shortcut(icon_x[0], icon_y[0], "THIS PC", 0, hot_icon == 0, x, y, width, height);
    draw_shortcut(icon_x[1], icon_y[1], "FILES", 1, hot_icon == 1, x, y, width, height);
    if (extra_icon_visible)
        draw_shortcut(icon_x[2], icon_y[2], "NEW FILE", 1, hot_icon == 2, x, y, width, height);
    gfx_reset_clip();
}

static int icon_at(int x, int y) {
    for (int i = 0; i < 3; i++) {
        if (i == 2 && !extra_icon_visible) continue;
        if (x >= icon_x[i] - 5 && x < icon_x[i] + 39 &&
            y >= icon_y[i] - 5 && y < icon_y[i] + 55) return i;
    }
    return -1;
}

int desktop_pointer(int mouse_x, int mouse_y, int buttons) {
    if (!fb_info.has_framebuffer) return 0;
    const int left = (buttons & 1) != 0;
    const int right = (buttons & 2) != 0;
    const int pressed = left && !previous_left;
    const int right_pressed = right && !previous_right;
    int changed = 0;
    if (click_window) click_window--;

    const int under = icon_at(mouse_x, mouse_y);
    if (right_pressed && under < 0 && !extra_icon_visible) {
        extra_icon_visible = 1;
        icon_x[2] = mouse_x - 16;
        icon_y[2] = mouse_y - 16;
        if (icon_x[2] < 4) icon_x[2] = 4;
        if (icon_y[2] < 4) icon_y[2] = 4;
        redraw_desk_region(icon_x[2] - 8, icon_y[2] - 8, 112, 74);
        gfx_present();
        changed = 1;
    }
    if (!left && under != hot_icon) {
        const int old = hot_icon;
        hot_icon = under;
        if (old >= 0) redraw_desk_region(icon_x[old] - 8, icon_y[old] - 8, 112, 74);
        if (under >= 0) redraw_desk_region(icon_x[under] - 8, icon_y[under] - 8, 112, 74);
        if (old >= 0 || under >= 0) { gfx_present(); changed = 1; }
    }

    if (pressed && under >= 0) {
        dragging_icon = under;
        drag_dx = mouse_x - icon_x[under];
        drag_dy = mouse_y - icon_y[under];
        if (last_click_icon == under && click_window > 0) {
            dragging_icon = -1;
            last_click_icon = -1;
            click_window = 0;
            menu_request_explorer();
        } else {
            last_click_icon = under;
            click_window = 28;
        }
        changed = 1;
    }

    if (left && dragging_icon >= 0) {
        const int i = dragging_icon;
        int nx = mouse_x - drag_dx;
        int ny = mouse_y - drag_dy;
        const int max_x = (int)fb_info.framebuffer_width - 44;
        const int max_y = (int)fb_info.framebuffer_height - 62;
        if (nx < 4) nx = 4;
        if (ny < 4) ny = 4;
        if (nx > max_x) nx = max_x;
        if (ny > max_y) ny = max_y;
        if (nx != icon_x[i] || ny != icon_y[i]) {
            const int ox = icon_x[i], oy = icon_y[i];
            icon_x[i] = nx; icon_y[i] = ny;
            hot_icon = i;
            redraw_desk_region(ox - 8, oy - 8, 112, 74);
            redraw_desk_region(nx - 8, ny - 8, 112, 74);
            gfx_present();
            changed = 1;
        }
    }
    if (!left) dragging_icon = -1;
    previous_left = left;
    previous_right = right;
    return changed;
}

void init_desk(void) {
    if (!fb_info.has_framebuffer) return;
    redraw_desk_region(0, 0, (int)fb_info.framebuffer_width, (int)fb_info.framebuffer_height);
    gfx_present();
}
