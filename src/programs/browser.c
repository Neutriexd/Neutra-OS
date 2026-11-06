#include "idt.h"
#include "vga.h"
#include "kernel.h"
#include "graphics.h"
#include "shell_window.h"
#include "http.h"
#include "dom.h"
#include "layout.h"
#include "js_page.h"
#include "tls.h"
#include "css.h"
#include "png.h"
#include "browser.h"

void gfx_present(void);
void gfx_set_clip(int x, int y, int w, int h);
void gfx_reset_clip(void);
void redraw_desk_region(int x, int y, int width, int height);

#define BR_W 640
#define BR_H 440
#define BR_TITLE 22
#define BR_BTN 16
#define BR_TASKBAR 36
#define BR_TOOL_Y 28
#define BR_TOOL_H 26
#define BR_CONTENT_Y 62
#define BR_STATUS_H 22
#define BR_LINE_H 18
#define BR_CHAR_W 12
#define BR_WRAP 50
#define BR_LINE_MAX 1200
#define BR_LINE_LEN 64
#define BR_HREF_MAX 200
#define BR_BODY_MAX 262144
#define BR_HOST_MAX 96
#define BR_PATH_MAX 200
#define BR_URL_MAX 320
#define BR_HIST 24
#define BR_STATUS_MAX 120

static LayoutLine lines[BR_LINE_MAX];
static int line_count;
static uint8_t body[BR_BODY_MAX];
static int cur_https;
static char cur_host[BR_HOST_MAX];
static char cur_path[BR_PATH_MAX];
static char addr[BR_URL_MAX];
static int addr_len;
static int addr_fresh;
static char hist[BR_HIST][BR_URL_MAX];
static int hist_count;
static int hist_index;
static int scroll_top;
static char status_text[BR_STATUS_MAX];

static int br_x = 100;
static int br_y = 60;
static int br_placed;
static int br_exists;
static int br_minimized;
static int br_active;
static int br_minimize_request;
static int br_dragging;
static int br_drag_dx;
static int br_drag_dy;

int browser_is_open(void) { return br_exists; }

static int lc(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

static int str_len(const char* s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static void str_copy(char* dst, const char* src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int str_eq(const char* a, const char* b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static int starts_ci(const char* s, const char* p) {
    while (*p) {
        if (lc((unsigned char)*s) != lc((unsigned char)*p)) return 0;
        s++;
        p++;
    }
    return 1;
}

static void append_str(char* dst, const char* src, int max) {
    int n = str_len(dst);
    int i = 0;
    while (src[i] && n < max - 1) dst[n++] = src[i++];
    dst[n] = '\0';
}

static void append_int(char* dst, int value, int max) {
    char digits[16];
    int_to_str(value, digits, sizeof(digits));
    append_str(dst, digits, max);
}

static void set_status(const char* text) {
    str_copy(status_text, text, BR_STATUS_MAX);
}

static int ci_find(const uint8_t* b, int n, int from, const char* needle) {
    const int m = str_len(needle);
    for (int i = from; i + m <= n; i++) {
        int ok = 1;
        for (int j = 0; j < m; j++) {
            if (lc(b[i + j]) != lc((unsigned char)needle[j])) {
                ok = 0;
                break;
            }
        }
        if (ok) return i;
    }
    return -1;
}

static int split_url(const char* in, int* https, char* host, int hmax, char* path, int pmax) {
    const char* p = in;
    *https = 0;
    if (starts_ci(p, "https://")) {
        *https = 1;
        p += 8;
    } else if (starts_ci(p, "http://")) {
        p += 7;
    }
    int h = 0;
    while (*p && *p != '/' && *p != ' ' && h < hmax - 1) host[h++] = *p++;
    host[h] = '\0';
    if (h == 0) return 2;
    if (*p == '/') str_copy(path, p, pmax);
    else str_copy(path, "/", pmax);
    return 0;
}

static int resolve_ref(int base_https, const char* base_host, const char* base_path, const char* ref,
                       int* https, char* host, int hmax, char* path, int pmax) {
    if (ref[0] == '\0' || ref[0] == '#') return 2;
    if (starts_ci(ref, "http://") || starts_ci(ref, "https://")) return split_url(ref, https, host, hmax, path, pmax);
    *https = base_https;
    str_copy(host, base_host, hmax);
    if (ref[0] == '/') {
        str_copy(path, ref, pmax);
        return 0;
    }
    int last = 0;
    for (int i = 0; base_path[i]; i++) {
        if (base_path[i] == '/') last = i;
    }
    char dir[BR_PATH_MAX];
    int k = 0;
    for (int i = 0; i <= last && k < BR_PATH_MAX - 1; i++) dir[k++] = base_path[i];
    dir[k] = '\0';
    str_copy(path, dir, pmax);
    append_str(path, ref, pmax);
    return 0;
}

static void br_draw(void);
static void br_draw_now(void);

#define CSS_FETCH_MAX 262144
static uint8_t css_fetch_buf[CSS_FETCH_MAX];

static int has_stylesheet_rel(const char* rel) {
    for (int i = 0; rel[i]; i++) {
        int k = 0;
        while (rel[i + k] && (rel[i + k] | 32) == ("stylesheet"[k])) {
            k++;
            if (!"stylesheet"[k]) return 1;
        }
    }
    return 0;
}

static void load_stylesheets(int base_https, const char* base_host, const char* base_path) {
    int links[16];
    const int nlinks = dom_collect_tag("link", links, 16);
    for (int k = 0; k < nlinks; k++) {
        const char* rel = dom_attr(links[k], "rel");
        const char* href = dom_attr(links[k], "href");
        if (!rel || !href || !href[0] || !has_stylesheet_rel(rel)) continue;
        int https = 0;
        char host[BR_HOST_MAX];
        char path[BR_PATH_MAX];
        if (resolve_ref(base_https, base_host, base_path, href, &https, host, sizeof(host), path, sizeof(path)) != 0) continue;
        set_status("Loading stylesheet...");
        br_draw_now();
        HttpResponse res;
        const int clen = https ? https_fetch(host, path, css_fetch_buf, CSS_FETCH_MAX, &res)
                               : http_fetch(host, path, css_fetch_buf, CSS_FETCH_MAX, &res);
        if (clen > 0 && res.status == 200) css_add_sheet((const char*)css_fetch_buf, clen);
    }
}

static void load_fetch(int https, const char* host_in, const char* path_in, int push);
static void parse_page(const uint8_t* b, int n, int base_https, const char* base_host, const char* base_path);

static void text_clip(int x, int y, const char* text, int cols, uint32_t color);

#define LG_HIDDEN_MAX 16
#define LG_RESP_MAX 65536
#define LG_BODY_MAX 4096

static int lg_active;
static int lg_captcha;
static int lg_https;
static char lg_host[BR_HOST_MAX];
static char lg_path[BR_PATH_MAX];
static int lg_base_https;
static char lg_base_host[BR_HOST_MAX];
static char lg_base_path[BR_PATH_MAX];
static char lg_user_name[32];
static char lg_pass_name[32];
static char lg_user[64];
static char lg_pass[64];
static int lg_user_len;
static int lg_pass_len;
static int lg_focus;
static char lg_hidden_name[LG_HIDDEN_MAX][32];
static char lg_hidden_val[LG_HIDDEN_MAX][160];
static int lg_hidden_count;
static uint8_t lg_resp[LG_RESP_MAX];
static char lg_body[LG_BODY_MAX];

static int attr_eq(const char* v, const char* want) {
    if (!v) return 0;
    int i = 0;
    while (v[i] && want[i] && lc((unsigned char)v[i]) == lc((unsigned char)want[i])) i++;
    return v[i] == '\0' && want[i] == '\0';
}

static int attr_contains(const char* v, const char* want) {
    if (!v) return 0;
    for (int i = 0; v[i]; i++) {
        int k = 0;
        while (want[k] && v[i + k] && lc((unsigned char)v[i + k]) == lc((unsigned char)want[k])) k++;
        if (!want[k]) return 1;
    }
    return 0;
}

static int nearest_form(int node) {
    int p = dom_node(node) ? dom_node(node)->parent : -1;
    int guard = 0;
    while (p >= 0 && guard++ < 200) {
        const DomNode* n = dom_node(p);
        if (!n) break;
        if (str_eq(n->tag, "form")) return p;
        p = n->parent;
    }
    return -1;
}

static void copy_bounded(char* dst, const char* src, int max) {
    int k = 0;
    while (src[k] && k < max - 1) {
        dst[k] = src[k];
        k++;
    }
    dst[k] = '\0';
}

static void lg_scan(int base_https, const char* base_host, const char* base_path) {
    lg_active = 0;
    lg_captcha = 0;
    lg_hidden_count = 0;
    lg_user_name[0] = '\0';
    lg_pass_name[0] = '\0';
    int inputs[64];
    const int ni = dom_collect_tag("input", inputs, 64);
    int form = -1;
    for (int k = 0; k < ni; k++) {
        const char* name = dom_attr(inputs[k], "name");
        if (name && str_eq(name, "g-recaptcha-response")) lg_captcha = 1;
        if (attr_eq(dom_attr(inputs[k], "type"), "password")) {
            form = nearest_form(inputs[k]);
            if (form >= 0) break;
        }
    }
    int divs[64];
    const int nd = dom_collect_tag("div", divs, 64);
    for (int k = 0; k < nd; k++) {
        if (attr_contains(dom_attr(divs[k], "class"), "g-recaptcha")) lg_captcha = 1;
    }
    if (form < 0) return;

    for (int k = 0; k < ni; k++) {
        if (nearest_form(inputs[k]) != form) continue;
        const int input = inputs[k];
        const char* type = dom_attr(input, "type");
        const char* name = dom_attr(input, "name");
        const char* value = dom_attr(input, "value");
        if (!name || !name[0]) continue;
        if (attr_eq(type, "password")) {
            copy_bounded(lg_pass_name, name, 32);
        } else if (attr_eq(type, "hidden")) {
            if (lg_hidden_count < LG_HIDDEN_MAX) {
                copy_bounded(lg_hidden_name[lg_hidden_count], name, 32);
                copy_bounded(lg_hidden_val[lg_hidden_count], value ? value : "", 160);
                lg_hidden_count++;
            }
        } else if (!type || attr_eq(type, "text") || attr_eq(type, "email")) {
            if (!lg_user_name[0]) copy_bounded(lg_user_name, name, 32);
        }
    }
    const char* action = dom_attr(form, "action");
    int https = base_https;
    char host[BR_HOST_MAX];
    char path[BR_PATH_MAX];
    copy_bounded(host, base_host, sizeof(host));
    copy_bounded(path, base_path, sizeof(path));
    if (action && action[0]) resolve_ref(base_https, base_host, base_path, action, &https, host, sizeof(host), path, sizeof(path));
    lg_https = https;
    copy_bounded(lg_host, host, sizeof(lg_host));
    copy_bounded(lg_path, path, sizeof(lg_path));
    lg_base_https = base_https;
    copy_bounded(lg_base_host, base_host, sizeof(lg_base_host));
    copy_bounded(lg_base_path, base_path, sizeof(lg_base_path));
    lg_active = lg_pass_name[0] != '\0';
    lg_user[0] = '\0';
    lg_pass[0] = '\0';
    lg_user_len = 0;
    lg_pass_len = 0;
    lg_focus = 0;
}

static int urlenc_append(char* out, int pos, int max, const char* text) {
    for (int i = 0; text[i] && pos < max - 4; i++) {
        const unsigned char c = (unsigned char)text[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') {
            out[pos++] = (char)c;
        } else {
            static const char hex[] = "0123456789ABCDEF";
            out[pos++] = '%';
            out[pos++] = hex[c >> 4];
            out[pos++] = hex[c & 15];
        }
    }
    out[pos] = '\0';
    return pos;
}

static int urlenc_field(char* out, int pos, int max, const char* name, const char* value) {
    if (pos > 0 && pos < max - 1) out[pos++] = '&';
    pos = urlenc_append(out, pos, max, name);
    if (pos < max - 1) out[pos++] = '=';
    pos = urlenc_append(out, pos, max, value);
    out[pos] = '\0';
    return pos;
}

static void lg_submit(void) {
    if (lg_captcha) {
        set_status("uhh");
        return;
    }
    int pos = 0;
    lg_body[0] = '\0';
    for (int h = 0; h < lg_hidden_count; h++) pos = urlenc_field(lg_body, pos, LG_BODY_MAX, lg_hidden_name[h], lg_hidden_val[h]);
    if (lg_user_name[0]) pos = urlenc_field(lg_body, pos, LG_BODY_MAX, lg_user_name, lg_user);
    if (lg_pass_name[0]) pos = urlenc_field(lg_body, pos, LG_BODY_MAX, lg_pass_name, lg_pass);

    set_status("Logging in...");
    br_draw_now();
    HttpResponse res;
    const int len = http_request(lg_https, lg_host, lg_path, "POST", (const uint8_t*)lg_body, pos, lg_resp,
                                 LG_RESP_MAX, &res);
    if (len < 0) {
        char msg[BR_STATUS_MAX];
        msg[0] = '\0';
        append_str(msg, "Login request failed (code ", sizeof(msg));
        append_int(msg, len, sizeof(msg));
        append_str(msg, ")", sizeof(msg));
        set_status(msg);
        return;
    }
    if ((res.status == 301 || res.status == 302 || res.status == 303 || res.status == 307 || res.status == 308) &&
        res.location[0]) {
        int https = 0;
        char host[BR_HOST_MAX];
        char path[BR_PATH_MAX];
        if (resolve_ref(lg_base_https, lg_base_host, lg_base_path, res.location, &https, host, sizeof(host), path,
                        sizeof(path)) == 0) {
            load_fetch(https, host, path, 1);
            return;
        }
    }
    parse_page(lg_resp, len, lg_base_https, lg_base_host, lg_base_path);
    char msg[BR_STATUS_MAX];
    msg[0] = '\0';
    append_str(msg, "Login not accepted (HTTP ", sizeof(msg));
    append_int(msg, res.status, sizeof(msg));
    append_str(msg, ")", sizeof(msg));
    set_status(msg);
}

static void lg_key(char key) {
    if (key == 9) {
        lg_focus ^= 1;
    } else if (key == '\n') {
        lg_submit();
    } else if (key == '\b') {
        if (lg_focus == 0 && lg_user_len > 0) lg_user[--lg_user_len] = '\0';
        if (lg_focus == 1 && lg_pass_len > 0) lg_pass[--lg_pass_len] = '\0';
    } else if (key >= 32 && key <= 126) {
        if (lg_focus == 0 && lg_user_len < 63) {
            lg_user[lg_user_len++] = key;
            lg_user[lg_user_len] = '\0';
        }
        if (lg_focus == 1 && lg_pass_len < 63) {
            lg_pass[lg_pass_len++] = key;
            lg_pass[lg_pass_len] = '\0';
        }
    }
}

static void lg_draw(int x, int y) {
    const int bx = x + 8;
    const int by = y + BR_H - BR_STATUS_H - 4 - 74;
    draw_rect(bx, by, BR_W - 16, 70, 0xE6E6EE);
    draw_rect(bx, by, BR_W - 16, 1, 0x7080A0);
    vga_draw_text_at(bx + 12, by + 8, "User", 0x000000);
    vga_draw_text_at(bx + 12, by + 34, "Password", 0x000000);
    draw_rect(bx + 90, by + 6, 300, 20, lg_focus == 0 ? 0xFFFFFF : 0xF4F4F4);
    draw_rect(bx + 90, by + 30, 300, 20, lg_focus == 1 ? 0xFFFFFF : 0xF4F4F4);
    text_clip(bx + 96, by + 8, lg_user, 24, 0x000000);
    char stars[64];
    for (int i = 0; i < lg_pass_len && i < 63; i++) stars[i] = '*';
    stars[lg_pass_len] = '\0';
    text_clip(bx + 96, by + 32, stars, 24, 0x000000);
    draw_rect(bx + 410, by + 6, 110, 44, 0x3A6FD0);
    vga_draw_text_at(bx + 438, by + 20, "Login", 0xFFFFFF);
}

static void load_images(int base_https, const char* base_host, const char* base_path);

static void parse_page(const uint8_t* b, int n, int base_https, const char* base_host, const char* base_path) {
    set_status("Parsing page...");
    br_draw_now();
    const int root = dom_parse_document(b, n);
    lg_scan(base_https, base_host, base_path);
    css_reset();
    css_set_viewport(BR_W - 16);
    load_stylesheets(base_https, base_host, base_path);
    int styles[16];
    const int nstyles = dom_collect_tag("style", styles, 16);
    static char css_buf[8192];
    for (int k = 0; k < nstyles; k++) {
        const int clen = dom_text_content(styles[k], css_buf, sizeof(css_buf));
        css_add_sheet(css_buf, clen);
    }
    int scripts[16];
    const int ns = dom_collect_tag("script", scripts, 16);
    static char code[8192];
    set_status("Starting JavaScript...");
    br_draw_now();
    if (js_page_open() == 0) {
        for (int k = 0; k < ns; k++) {
            set_status("Running script...");
            br_draw_now();
            const int len = dom_text_content(scripts[k], code, sizeof(code));
            js_page_eval(code, len);
        }
    }
    set_status("Building layout...");
    br_draw_now();
    line_count = layout_build(root, lines, BR_LINE_MAX);
    load_images(base_https, base_host, base_path);
    js_page_close();
}

static void line_add(const char* text, int kind) {
    if (line_count >= BR_LINE_MAX) return;
    str_copy(lines[line_count].text, text, BR_LINE_LEN);
    lines[line_count].kind = (uint8_t)kind;
    lines[line_count].href[0] = '\0';
    line_count++;
}

static void finish_page(int https, const char* host, const char* path, int push) {
    cur_https = https;
    str_copy(cur_host, host, BR_HOST_MAX);
    str_copy(cur_path, path, BR_PATH_MAX);
    addr[0] = '\0';
    append_str(addr, https ? "https://" : "http://", BR_URL_MAX);
    append_str(addr, host, BR_URL_MAX);
    append_str(addr, path, BR_URL_MAX);
    addr_len = str_len(addr);
    addr_fresh = 1;
    scroll_top = 0;
    if (push) {
        if (hist_count == BR_HIST) {
            for (int i = 1; i < BR_HIST; i++) str_copy(hist[i - 1], hist[i], BR_URL_MAX);
            hist_count--;
        }
        if (hist_index + 1 < hist_count) hist_count = hist_index + 1;
        str_copy(hist[hist_count], addr, BR_URL_MAX);
        hist_count++;
        hist_index = hist_count - 1;
    }
}

static void show_message(int https, const char* host, const char* path, const char* title,
                         const char* l1, const char* l2, int push) {
    line_count = 0;
    line_add(title, 1);
    line_add("", 0);
    if (l1[0]) line_add(l1, 0);
    if (l2[0]) line_add(l2, 0);
    finish_page(https, host, path, push);
}

static void load_fetch(int https, const char* host_in, const char* path_in, int push) {
    char host[BR_HOST_MAX];
    char path[BR_PATH_MAX];
    str_copy(host, host_in, sizeof(host));
    str_copy(path, path_in, sizeof(path));
    int cur_s = https;
    int redirects = 0;
    set_status(https ? "Loading (https)..." : "Loading...");
    br_draw_now();
    HttpResponse res;
    int len = 0;
    for (;;) {
        len = cur_s ? https_fetch(host, path, body, BR_BODY_MAX, &res)
                    : http_fetch(host, path, body, BR_BODY_MAX, &res);
        if (len < 0) {
            char detail[BR_LINE_LEN];
            detail[0] = '\0';
            append_str(detail, "Error code ", sizeof(detail));
            append_int(detail, len, sizeof(detail));
            show_message(cur_s, host, path, "Cannot load page", "The host could not be reached.", detail, push);
            set_status("Failed to load");
            return;
        }
        const int code = res.status;
        if ((code == 301 || code == 302 || code == 303 || code == 307 || code == 308) && res.location[0] &&
            redirects < 5) {
            char nh[BR_HOST_MAX];
            char np[BR_PATH_MAX];
            int ns = 0;
            const int r = resolve_ref(cur_s, host, path, res.location, &ns, nh, sizeof(nh), np, sizeof(np));
            if (r != 0) break;
            cur_s = ns;
            str_copy(host, nh, sizeof(host));
            str_copy(path, np, sizeof(path));
            redirects++;
            continue;
        }
        break;
    }
    if (res.status >= 400 || res.status == 0) {
        char code_line[BR_LINE_LEN];
        code_line[0] = '\0';
        append_str(code_line, "HTTP status ", sizeof(code_line));
        append_int(code_line, res.status, sizeof(code_line));
        show_message(cur_s, host, path, "Page not available", code_line, "The server did not return the page.", push);
        set_status("Page not available");
        return;
    }
    parse_page(body, len, cur_s, host, path);
    if (line_count == 0) {
        show_message(cur_s, host, path, "No text to show",
                     "The page has no text that Neutra can display.",
                     "bro you are really comparing neutra with Linux nah...", push);
        set_status("No text (JavaScript page?)");
        return;
    }
    finish_page(cur_s, host, path, push);
    char s[BR_STATUS_MAX];
    s[0] = '\0';
    append_str(s, "HTTP ", sizeof(s));
    append_int(s, res.status, sizeof(s));
    set_status(s);
}

static void load_text(const char* text, int push) {
    int https = 0;
    char host[BR_HOST_MAX];
    char path[BR_PATH_MAX];
    if (split_url(text, &https, host, sizeof(host), path, sizeof(path)) != 0) {
        show_message(0, "", "/", "Invalid address", "Type a host name such as example.com", "", 0);
        set_status("Invalid address");
        return;
    }
    load_fetch(https, host, path, push);
}

static void navigate_href(const char* href) {
    int https = 0;
    char host[BR_HOST_MAX];
    char path[BR_PATH_MAX];
    if (resolve_ref(cur_https, cur_host, cur_path, href, &https, host, sizeof(host), path, sizeof(path)) == 0) {
        load_fetch(https, host, path, 1);
    }
}

static void go_history(int index) {
    if (index < 0 || index >= hist_count) return;
    hist_index = index;
    int https = 0;
    char host[BR_HOST_MAX];
    char path[BR_PATH_MAX];
    if (split_url(hist[index], &https, host, sizeof(host), path, sizeof(path)) == 0) {
        load_fetch(https, host, path, 0);
    }
}

static int line_count_now(void) {
    return line_count;
}

static void text_clip(int x, int y, const char* text, int cols, uint32_t color) {
    char shown[BR_LINE_LEN];
    int n = 0;
    while (text[n] && n < cols && n < BR_LINE_LEN - 1) {
        shown[n] = text[n];
        n++;
    }
    shown[n] = '\0';
    vga_draw_text_at(x, y, shown, color);
}

static void draw_bold(int x, int y, const char* text, int cols, uint32_t color) {
    text_clip(x, y, text, cols, color);
    text_clip(x + 1, y, text, cols, color);
}

static int in_box(int mx, int my, int x, int y, int w, int h) {
    return mx >= x && mx < x + w && my >= y && my < y + h;
}

#define IMG_SLOTS 8
#define IMG_BODY_MAX 65536
static uint8_t img_pix[IMG_SLOTS][PNG_MAX_DIM * PNG_MAX_DIM * 3];
static int img_pw[IMG_SLOTS];
static int img_ph[IMG_SLOTS];
static int img_count;
static int line_slot[BR_LINE_MAX];
static uint8_t img_body[IMG_BODY_MAX];

static void draw_runs(const LayoutLine* L, int tx, int ly, int len, int cols) {
    char seg[BR_LINE_LEN];
    if (L->nruns == 0) {
        if (L->flags & (LAYOUT_FLAG_BOLD | LAYOUT_FLAG_BIG)) draw_bold(tx, ly, L->text, cols, L->color);
        else text_clip(tx, ly, L->text, cols, L->color);
        if (L->kind == LAYOUT_KIND_LINK) draw_rect(tx, ly + 15, len * BR_CHAR_W, 1, L->color);
        return;
    }
    for (int r = 0; r < L->nruns; r++) {
        const int start = L->runs[r].start;
        int end = (r + 1 < L->nruns) ? L->runs[r + 1].start : len;
        if (end > len) end = len;
        if (start >= end) continue;
        int k = 0;
        for (int i = start; i < end && k < BR_LINE_LEN - 1; i++) seg[k++] = L->text[i];
        seg[k] = '\0';
        const int x = tx + start * BR_CHAR_W;
        if (L->runs[r].flags & (LAYOUT_FLAG_BOLD | LAYOUT_FLAG_BIG)) draw_bold(x, ly, seg, cols, L->runs[r].color);
        else text_clip(x, ly, seg, cols, L->runs[r].color);
        if (L->kind == LAYOUT_KIND_LINK) draw_rect(x, ly + 15, (end - start) * BR_CHAR_W, 1, L->runs[r].color);
    }
}

static void draw_image_box(int x, int y, int w, int h, int slot) {
    if (slot < 0) {
        draw_rect(x, y, w, h, 0xD0D0D0);
        vga_draw_text_at(x + 4, y + 4, "IMG", 0x404040);
        return;
    }
    const int iw = img_pw[slot];
    const int ih = img_ph[slot];
    for (int py = 0; py < h; py++) {
        int run_start = 0;
        uint32_t prev = 0;
        for (int px = 0; px <= w; px++) {
            uint32_t c = 0;
            if (px < w) {
                uint8_t sample[3];
                png_box_sample(img_pix[slot], iw, ih, w, h, px, py, sample);
                c = ((uint32_t)sample[0] << 16) | ((uint32_t)sample[1] << 8) | (uint32_t)sample[2];
            }
            if (px == 0) {
                prev = c;
                run_start = 0;
            } else if (px == w || c != prev) {
                draw_rect(x + run_start, y + py, px - run_start, 1, prev);
                run_start = px;
                prev = c;
            }
        }
    }
}

static void load_images(int base_https, const char* base_host, const char* base_path) {
    img_count = 0;
    for (int i = 0; i < line_count; i++) {
        line_slot[i] = -1;
        if (lines[i].kind != LAYOUT_KIND_IMAGE || !lines[i].href[0]) continue;
        if (img_count >= IMG_SLOTS) continue;
        int https = 0;
        char host[BR_HOST_MAX];
        char path[BR_PATH_MAX];
        if (resolve_ref(base_https, base_host, base_path, lines[i].href, &https, host, sizeof(host), path, sizeof(path)) != 0) continue;
        set_status("Loading image...");
        br_draw_now();
        HttpResponse res;
        const int len = https ? https_fetch(host, path, img_body, IMG_BODY_MAX, &res)
                              : http_fetch(host, path, img_body, IMG_BODY_MAX, &res);
        if (len <= 0) continue;
        int w = 0;
        int h = 0;
        if (png_decode(img_body, len, img_pix[img_count], PNG_MAX_DIM * PNG_MAX_DIM * 3, &w, &h) == 0) {
            img_pw[img_count] = w;
            img_ph[img_count] = h;
            line_slot[i] = img_count;
            img_count++;
        }
    }
}

static void br_draw(void) {
    const int x = br_x;
    const int y = br_y;

    draw_rect(x - 4, y - 4, BR_W + 8, BR_H + 8, 0x202020);
    draw_rect(x, y, BR_W, BR_H, 0xC0C0C0);
    draw_rect(x, y, BR_W, BR_TITLE, 0x0050A0);
    vga_draw_text_at(x + 10, y + 4, "NEUTRA BROWSER", 0xFFFFFF);
    draw_rect(x + BR_W - 2 * BR_BTN - 10, y + 3, BR_BTN, BR_BTN, 0x3A6FD0);
    draw_rect(x + BR_W - 2 * BR_BTN - 6, y + 3 + BR_BTN - 6, BR_BTN - 8, 3, 0xFFFFFF);
    draw_rect(x + BR_W - BR_BTN - 6, y + 3, BR_BTN, BR_BTN, 0xD04040);
    vga_draw_text_at(x + BR_W - BR_BTN - 3, y + 4, "X", 0xFFFFFF);

    const int ty = y + BR_TOOL_Y + 2;
    draw_rect(x + 8, ty, 30, BR_TOOL_H - 4, 0xD8D8D8);
    vga_draw_text_at(x + 16, ty + 4, "<", 0x000000);
    draw_rect(x + 42, ty, 30, BR_TOOL_H - 4, 0xD8D8D8);
    vga_draw_text_at(x + 50, ty + 4, ">", 0x000000);
    draw_rect(x + 76, ty, 30, BR_TOOL_H - 4, 0xD8D8D8);
    vga_draw_text_at(x + 84, ty + 4, "R", 0x000000);

    const int field_x = x + 112;
    const int field_w = BR_W - 112 - 96;
    draw_rect(field_x, ty, field_w, BR_TOOL_H - 4, 0xFFFFFF);
    draw_rect(field_x, ty, field_w, 1, 0x808080);
    draw_rect(field_x, ty + BR_TOOL_H - 5, field_w, 1, 0x808080);
    const int field_cols = (field_w - 12) / BR_CHAR_W;
    text_clip(field_x + 6, ty + 4, addr, field_cols, 0x000000);
    const int caret = addr_len < field_cols ? addr_len : field_cols;
    draw_rect(field_x + 6 + caret * BR_CHAR_W, ty + 2, 2, BR_TOOL_H - 8, 0x0050A0);

    draw_rect(x + BR_W - 88, ty, 80, BR_TOOL_H - 4, 0x3A6FD0);
    vga_draw_text_at(x + BR_W - 70, ty + 4, "Go", 0xFFFFFF);

    const int cx = x + 8;
    const int cy = y + BR_CONTENT_Y;
    const int cw = BR_W - 16;
    const int ch = BR_H - BR_CONTENT_Y - BR_STATUS_H - 10;
    draw_rect(cx, cy, cw, ch, 0xFFFFFF);
    const int cols = (cw - 20) / BR_CHAR_W;
    const int rows = ch / BR_LINE_H;
    for (int r = 0; r < rows; r++) {
        const int index = scroll_top + r;
        if (index >= line_count_now()) break;
        const LayoutLine* L = &lines[index];
        const int ly = cy + 6 + r * BR_LINE_H;
        if (L->bg != 0) draw_rect(cx + 2, ly - 2, cw - 4, BR_LINE_H, L->bg);
        if (L->kind == LAYOUT_KIND_IMAGE) {
            draw_image_box(cx + 10 + L->indent * BR_CHAR_W, ly - 2, L->img_w, L->img_h, line_slot[index]);
            continue;
        }
        if (L->kind == LAYOUT_KIND_IMAGE_CONT || L->text[0] == '\0') continue;
        int len = str_len(L->text);
        if (len > cols) len = cols;
        int tx = cx + 10 + L->indent * BR_CHAR_W;
        if (L->flags & LAYOUT_FLAG_CENTER) tx = cx + (cw - len * BR_CHAR_W) / 2;
        draw_runs(L, tx, ly, len, cols);
    }

    if (lg_active) lg_draw(x, y);
    draw_rect(x + 8, y + BR_H - BR_STATUS_H - 4, BR_W - 16, BR_STATUS_H, 0xD8D8D8);
    vga_draw_text_at(x + 14, y + BR_H - BR_STATUS_H, status_text, 0x000000);
    gfx_present();
}

static void br_draw_now(void) {
    if (br_exists && !br_minimized) br_draw();
}

static void clamp_scroll(void) {
    const int ch = BR_H - BR_CONTENT_Y - BR_STATUS_H - 10;
    const int rows = ch / BR_LINE_H;
    const int max_top = line_count - rows;
    if (max_top < 0) {
        scroll_top = 0;
        return;
    }
    if (scroll_top > max_top) scroll_top = max_top;
    if (scroll_top < 0) scroll_top = 0;
}

static void repaint_moved(int old_x, int old_y) {
    const int screen_h = (int)fb_info.framebuffer_height;
    int x0 = (old_x < br_x ? old_x : br_x) - 4;
    int y0 = (old_y < br_y ? old_y : br_y) - 4;
    int x1 = (old_x > br_x ? old_x : br_x) + BR_W + 4;
    int y1 = (old_y > br_y ? old_y : br_y) + BR_H + 4;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (y1 > screen_h - BR_TASKBAR) y1 = screen_h - BR_TASKBAR;
    redraw_desk_region(x0, y0, x1 - x0, y1 - y0);
    gfx_set_clip(x0, y0, x1 - x0, y1 - y0);
    shell_paint();
    br_draw();
    gfx_reset_clip();
    gfx_present();
}

static void close_browser(void) {
    br_exists = 0;
    br_minimized = 0;
    hist_count = 0;
    hist_index = -1;
    line_count = 0;
}

static void submit_address(void) {
    if (addr_len == 0) return;
    char text[BR_URL_MAX];
    str_copy(text, addr, sizeof(text));
    load_text(text, 1);
}

static void br_loop(void) {
    int previous_left = mouse_get_buttons() & 1;
    while (1) {
        char key = keyboard_try_getchar_poll();
        const int buttons = mouse_get_buttons();
        const int left = buttons & 1;
        int mouse_x;
        int mouse_y;
        mouse_get_position(&mouse_x, &mouse_y);
        int pressed = left && !previous_left;
        previous_left = left;
        int dirty = 0;
        const int ty = br_y + BR_TOOL_Y + 2;

        if (pressed && lg_active) {
            const int bx = br_x + 8;
            const int by = br_y + BR_H - BR_STATUS_H - 4 - 74;
            if (in_box(mouse_x, mouse_y, bx, by, BR_W - 16, 70)) {
                if (in_box(mouse_x, mouse_y, bx + 90, by + 6, 300, 20)) lg_focus = 0;
                else if (in_box(mouse_x, mouse_y, bx + 90, by + 30, 300, 20)) lg_focus = 1;
                else if (in_box(mouse_x, mouse_y, bx + 410, by + 6, 110, 44)) lg_submit();
                pressed = 0;
                dirty = 1;
            }
        }
        if (pressed) {
            if (in_box(mouse_x, mouse_y, br_x + BR_W - BR_BTN - 6, br_y + 3, BR_BTN, BR_BTN)) {
                close_browser();
                return;
            }
            if (in_box(mouse_x, mouse_y, br_x + BR_W - 2 * BR_BTN - 10, br_y + 3, BR_BTN, BR_BTN)) {
                br_minimized = 1;
                return;
            }
            if (in_box(mouse_x, mouse_y, br_x, br_y, BR_W - 2 * BR_BTN - 16, BR_TITLE)) {
                br_dragging = 1;
                br_drag_dx = mouse_x - br_x;
                br_drag_dy = mouse_y - br_y;
            } else if (in_box(mouse_x, mouse_y, br_x + 8, ty, 30, BR_TOOL_H - 4)) {
                if (hist_index > 0) go_history(hist_index - 1);
                dirty = 1;
            } else if (in_box(mouse_x, mouse_y, br_x + 42, ty, 30, BR_TOOL_H - 4)) {
                if (hist_index + 1 < hist_count) go_history(hist_index + 1);
                dirty = 1;
            } else if (in_box(mouse_x, mouse_y, br_x + 76, ty, 30, BR_TOOL_H - 4)) {
                if (cur_host[0]) load_fetch(cur_https, cur_host, cur_path, 0);
                dirty = 1;
            } else if (in_box(mouse_x, mouse_y, br_x + BR_W - 88, ty, 80, BR_TOOL_H - 4)) {
                submit_address();
                dirty = 1;
            } else if (in_box(mouse_x, mouse_y, br_x + 8, br_y + BR_CONTENT_Y, BR_W - 16,
                              BR_H - BR_CONTENT_Y - BR_STATUS_H - 10)) {
                const int top = br_y + BR_CONTENT_Y + 6;
                if (mouse_y >= top) {
                    const int index = scroll_top + (mouse_y - top) / BR_LINE_H;
                    if (index >= 0 && index < line_count && lines[index].href[0]) {
                        char href[BR_HREF_MAX];
                        str_copy(href, lines[index].href, sizeof(href));
                        navigate_href(href);
                        dirty = 1;
                    }
                }
            }
        }
        if (!left) br_dragging = 0;

        if (br_minimize_request) {
            br_minimize_request = 0;
            br_minimized = 1;
            return;
        }

        if (br_dragging) {
            const int old_x = br_x;
            const int old_y = br_y;
            int new_x = mouse_x - br_drag_dx;
            int new_y = mouse_y - br_drag_dy;
            const int max_x = (int)fb_info.framebuffer_width - BR_W;
            const int max_y = (int)fb_info.framebuffer_height - BR_TASKBAR - BR_H;
            if (new_x > max_x) new_x = max_x;
            if (new_y > max_y) new_y = max_y;
            if (new_x < 0) new_x = 0;
            if (new_y < 0) new_y = 0;
            if (new_x != old_x || new_y != old_y) {
                br_x = new_x;
                br_y = new_y;
                repaint_moved(old_x, old_y);
            }
        }

        if (key != 0 && lg_active && (key == 9 || key == '\n' || key == '\b' || (key >= 32 && key <= 126))) {
            lg_key(key);
            dirty = 1;
            key = 0;
        }
        if (key != 0) {
            if (key == 27) {
                br_minimized = 1;
                return;
            }
            if (key == 17) {
                close_browser();
                return;
            }
            if (key == 14) {
                scroll_top += 3;
                dirty = 1;
            } else if (key == 16) {
                scroll_top -= 3;
                dirty = 1;
            } else if (key == 2) {
                if (hist_index > 0) go_history(hist_index - 1);
                dirty = 1;
            } else if (key == 6) {
                if (hist_index + 1 < hist_count) go_history(hist_index + 1);
                dirty = 1;
            } else if (key == 18) {
                if (cur_host[0]) load_fetch(cur_https, cur_host, cur_path, 0);
                dirty = 1;
            } else if (key == '\n') {
                submit_address();
                dirty = 1;
            } else if (key == '\b') {
                if (addr_fresh) {
                    addr[0] = '\0';
                    addr_len = 0;
                    addr_fresh = 0;
                } else if (addr_len > 0) {
                    addr[--addr_len] = '\0';
                }
                dirty = 1;
            } else if (key >= 32 && key <= 126 && addr_len < BR_URL_MAX - 1) {
                if (addr_fresh) {
                    addr[0] = '\0';
                    addr_len = 0;
                    addr_fresh = 0;
                }
                addr[addr_len++] = key;
                addr[addr_len] = '\0';
                dirty = 1;
            }
        }

        clamp_scroll();
        if (dirty) br_draw();
    }
}

static void progress_cb(const char* text) {
    set_status(text);
    br_draw_now();
}

void browser_run(void) {
    http_set_progress(progress_cb);
    tls_set_progress(progress_cb);
    if (!fb_info.has_framebuffer) return;

    if (!br_placed) {
        br_x = ((int)fb_info.framebuffer_width - BR_W) / 2;
        br_y = ((int)fb_info.framebuffer_height - BR_TASKBAR - BR_H) / 2;
        if (br_x < 0) br_x = 0;
        if (br_y < 0) br_y = 0;
        br_placed = 1;
    }
    if (!br_exists) {
        br_exists = 1;
        hist_count = 0;
        hist_index = -1;
        cur_host[0] = '\0';
        cur_path[0] = '\0';
        addr[0] = '\0';
        addr_len = 0;
        addr_fresh = 0;
        line_count = 0;
        show_message(0, "", "/", "Neutra Browser", "Type an address above and press Enter.",
                     "Example: example.com or https://example.com", 0);
        set_status("Ready");
    }
    br_minimized = 0;
    br_minimize_request = 0;
    br_dragging = 0;
    br_active = 1;
    br_draw();
    br_loop();
    br_active = 0;
}