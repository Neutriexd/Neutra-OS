#include "layout.h"
#include "dom.h"
#include "css.h"

static LayoutLine* out_lines;
static int out_max;
static int out_count;
static int walk_budget;
static int layout_wrap = LAYOUT_WRAP;

#define GRID_CHILDREN 12
#define GRID_LINES 24
#define GRID_GAP 2

static LayoutLine grid_buf[GRID_CHILDREN][GRID_LINES];
static int grid_count[GRID_CHILDREN];

typedef struct {
    LayoutLine* out;
    int max;
    int count;
    char cur[LAYOUT_TEXT];
    int cur_len;
    int cur_kind;
    uint8_t cur_flags;
    uint8_t cur_indent;
    uint32_t cur_color;
    uint32_t cur_bg;
    uint8_t cur_nruns;
    LayoutRun cur_runs[LAYOUT_RUNS];
    char cur_href[LAYOUT_HREF];
    int cur_has_href;
    int in_link;
    char link_target[LAYOUT_HREF];
    int block_bg_on;
    uint32_t block_bg;
    int wrap;
    int budget;
} LayoutState;

static char cur[LAYOUT_TEXT];
static int cur_len;
static int cur_kind;
static uint8_t cur_flags;
static uint8_t cur_indent;
static uint32_t cur_color;
static uint32_t cur_bg;
static uint8_t cur_nruns;
static LayoutRun cur_runs[LAYOUT_RUNS];
static char cur_href[LAYOUT_HREF];
static int cur_has_href;
static int in_link;
static char link_target[LAYOUT_HREF];
static int block_bg_on;
static uint32_t block_bg;

static int str_eq(const char* a, const char* b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static int is_skip(const char* t) {
    return str_eq(t, "head") || str_eq(t, "script") || str_eq(t, "style") || str_eq(t, "title") ||
           str_eq(t, "noscript") || str_eq(t, "svg") || str_eq(t, "template") || str_eq(t, "meta") ||
           str_eq(t, "link");
}

static int is_block(const char* t) {
    static const char* const blocks[] = {"p", "ul", "ol", "table", "section", "article", "blockquote",
                                         "hr", "header", "footer", "nav", "pre", "dl", "form", "main"};
    for (int i = 0; i < (int)(sizeof(blocks) / sizeof(blocks[0])); i++) {
        if (str_eq(t, blocks[i])) return 1;
    }
    return 0;
}

static void copy_href(char* dst, const char* src) {
    int k = 0;
    while (src[k] && k < LAYOUT_HREF - 1) {
        dst[k] = src[k];
        k++;
    }
    dst[k] = '\0';
}

static void push_line(void) {
    if (cur_len == 0) {
        cur_has_href = 0;
        return;
    }
    if (out_count < out_max) {
        LayoutLine* L = &out_lines[out_count++];
        for (int i = 0; i < cur_len; i++) L->text[i] = cur[i];
        L->text[cur_len] = '\0';
        L->kind = (uint8_t)(cur_has_href ? LAYOUT_KIND_LINK : cur_kind);
        L->flags = cur_flags;
        L->indent = cur_indent;
        L->color = cur_color;
        L->bg = cur_bg;
        L->nruns = cur_nruns;
        for (int i = 0; i < cur_nruns; i++) L->runs[i] = cur_runs[i];
        L->img_w = 0;
        L->img_h = 0;
        if (cur_has_href) copy_href(L->href, cur_href);
        else L->href[0] = '\0';
    }
    cur_len = 0;
    cur_has_href = 0;
    cur_indent = 0;
    cur_nruns = 0;
}

static void add_line(int kind, uint32_t bg, const char* href, int w, int h, uint8_t flags) {
    if (out_count >= out_max) return;
    LayoutLine* L = &out_lines[out_count++];
    L->text[0] = '\0';
    L->kind = (uint8_t)kind;
    L->flags = flags;
    L->indent = 0;
    L->color = 0;
    L->bg = bg;
    L->nruns = 0;
    L->img_w = (uint16_t)w;
    L->img_h = (uint16_t)h;
    if (href) copy_href(L->href, href);
    else L->href[0] = '\0';
}

static void add_blank(int force) {
    if (out_count == 0 || out_count >= out_max) return;
    if (!force && out_lines[out_count - 1].text[0] == '\0') return;
    add_line(LAYOUT_KIND_TEXT, block_bg_on ? block_bg : 0, 0, 0, 0, 0);
}

static uint8_t style_flags(const CssStyle* st) {
    return (uint8_t)((st->bold ? LAYOUT_FLAG_BOLD : 0) | (st->big ? LAYOUT_FLAG_BIG : 0) |
                     (st->mono ? LAYOUT_FLAG_MONO : 0));
}

static void emit(char c, const CssStyle* st) {
    if (cur_len >= layout_wrap) {
        push_line();
        if (in_link) {
            cur_has_href = 1;
            copy_href(cur_href, link_target);
        }
        if (c == ' ') return;
    }
    if (cur_len == 0) {
        if (c == ' ') return;
        cur_color = st->has_color ? st->color : 0x000000;
        cur_bg = block_bg_on ? block_bg : 0;
        cur_indent = (uint8_t)(st->indent > 40 ? 40 : st->indent);
        cur_flags = (uint8_t)(style_flags(st) | (st->center ? LAYOUT_FLAG_CENTER : 0) |
                              (block_bg_on ? LAYOUT_FLAG_BG : 0));
        cur_nruns = 1;
        cur_runs[0].start = 0;
        cur_runs[0].color = cur_color;
        cur_runs[0].flags = style_flags(st);
    } else {
        const uint32_t color = st->has_color ? st->color : 0x000000;
        const uint8_t flags = style_flags(st);
        const LayoutRun* last = &cur_runs[cur_nruns - 1];
        if ((color != last->color || flags != last->flags) && cur_nruns < LAYOUT_RUNS && cur_len < 255) {
            cur_runs[cur_nruns].start = (uint8_t)cur_len;
            cur_runs[cur_nruns].color = color;
            cur_runs[cur_nruns].flags = flags;
            cur_nruns++;
        }
    }
    cur[cur_len++] = c;
    cur[cur_len] = '\0';
}

static void emit_text(const char* s, const CssStyle* st) {
    for (int i = 0; s[i]; i++) {
        const char c = s[i];
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
            if (cur_len > 0 && cur[cur_len - 1] != ' ') emit(' ', st);
        } else {
            emit((c < 32 || c > 126) ? '?' : c, st);
        }
    }
}

static void save_state(LayoutState* st) {
    st->out = out_lines;
    st->max = out_max;
    st->count = out_count;
    for (int i = 0; i < LAYOUT_TEXT; i++) st->cur[i] = cur[i];
    st->cur_len = cur_len;
    st->cur_kind = cur_kind;
    st->cur_flags = cur_flags;
    st->cur_indent = cur_indent;
    st->cur_color = cur_color;
    st->cur_bg = cur_bg;
    st->cur_nruns = cur_nruns;
    for (int i = 0; i < LAYOUT_RUNS; i++) st->cur_runs[i] = cur_runs[i];
    for (int i = 0; i < LAYOUT_HREF; i++) {
        st->cur_href[i] = cur_href[i];
        st->link_target[i] = link_target[i];
    }
    st->cur_has_href = cur_has_href;
    st->in_link = in_link;
    st->block_bg_on = block_bg_on;
    st->block_bg = block_bg;
    st->wrap = layout_wrap;
    st->budget = walk_budget;
}

static void restore_state(const LayoutState* st) {
    out_lines = st->out;
    out_max = st->max;
    out_count = st->count;
    for (int i = 0; i < LAYOUT_TEXT; i++) cur[i] = st->cur[i];
    cur_len = st->cur_len;
    cur_kind = st->cur_kind;
    cur_flags = st->cur_flags;
    cur_indent = st->cur_indent;
    cur_color = st->cur_color;
    cur_bg = st->cur_bg;
    cur_nruns = st->cur_nruns;
    for (int i = 0; i < LAYOUT_RUNS; i++) cur_runs[i] = st->cur_runs[i];
    for (int i = 0; i < LAYOUT_HREF; i++) {
        cur_href[i] = st->cur_href[i];
        link_target[i] = st->link_target[i];
    }
    cur_has_href = st->cur_has_href;
    in_link = st->in_link;
    block_bg_on = st->block_bg_on;
    block_bg = st->block_bg;
    layout_wrap = st->wrap;
    walk_budget = st->budget;
}

static void walk(int i, int depth, const CssStyle* parent);

static int sub_layout(int node, int depth, const CssStyle* parent, int wrap, LayoutLine* buf, int max) {
    LayoutState saved;
    save_state(&saved);
    out_lines = buf;
    out_max = max;
    out_count = 0;
    cur_len = 0;
    cur_nruns = 0;
    cur_has_href = 0;
    cur_kind = LAYOUT_KIND_TEXT;
    layout_wrap = wrap;
    walk_budget = 5000;
    walk(node, depth, parent);
    push_line();
    const int count = out_count;
    restore_state(&saved);
    return count;
}

static int is_blank_text(int node) {
    const DomNode* n = dom_node(node);
    if (!n || n->type != DOM_TYPE_TEXT) return 0;
    const char* src = dom_text_pool_at(n->text_off);
    for (int j = 0; j < n->text_len; j++) {
        if (src[j] != ' ' && src[j] != '\n' && src[j] != '\r' && src[j] != '\t') return 0;
    }
    return 1;
}

static void push_custom(const char* text, int len, const LayoutRun* runs, int nruns, int kind, uint8_t flags,
                        uint32_t bg, const char* href) {
    if (out_count >= out_max) return;
    LayoutLine* L = &out_lines[out_count++];
    for (int i = 0; i < len && i < LAYOUT_TEXT - 1; i++) L->text[i] = text[i];
    L->text[len] = '\0';
    L->kind = (uint8_t)kind;
    L->flags = flags;
    L->indent = 0;
    L->color = nruns > 0 ? runs[0].color : 0;
    L->bg = bg;
    L->nruns = (uint8_t)nruns;
    for (int i = 0; i < nruns; i++) L->runs[i] = runs[i];
    L->img_w = 0;
    L->img_h = 0;
    copy_href(L->href, href ? href : "");
}

static void compose_row(int r, int cols, int colw, int nc, int height) {
    for (int i = 0; i < height; i++) {
        char tline[LAYOUT_TEXT];
        for (int k = 0; k < LAYOUT_TEXT - 1; k++) tline[k] = ' ';
        LayoutRun runs[LAYOUT_RUNS];
        int nr = 0;
        int len = 0;
        int kind = LAYOUT_KIND_TEXT;
        uint8_t flags = 0;
        uint32_t bg = 0;
        char href[LAYOUT_HREF];
        href[0] = '\0';
        int have = 0;
        for (int c = 0; c < cols; c++) {
            const int k = r * cols + c;
            const int off = c * (colw + GRID_GAP);
            if (k >= nc || i >= grid_count[k]) continue;
            const LayoutLine* L = &grid_buf[k][i];
            int tl = 0;
            while (L->text[tl]) tl++;
            for (int j = 0; j < tl && off + j < LAYOUT_TEXT - 1; j++) tline[off + j] = L->text[j];
            if (off + tl > len) len = off + tl;
            for (int ru = 0; ru < L->nruns && nr < LAYOUT_RUNS; ru++) {
                runs[nr].start = (uint8_t)(off + L->runs[ru].start);
                runs[nr].color = L->runs[ru].color;
                runs[nr].flags = L->runs[ru].flags;
                nr++;
            }
            if (!have && tl > 0) {
                have = 1;
                kind = L->kind;
                flags = L->flags;
                bg = L->bg;
                copy_href(href, L->href);
            }
        }
        if (len == 0) {
            add_blank(0);
            continue;
        }
        if (nr == 0 || runs[0].start > 0) {
            for (int q = nr; q > 0 && q < LAYOUT_RUNS; q--) runs[q] = runs[q - 1];
            if (nr < LAYOUT_RUNS) nr++;
            runs[0].start = 0;
            runs[0].color = 0x000000;
            runs[0].flags = 0;
        }
        tline[len] = '\0';
        push_custom(tline, len, runs, nr, kind, flags, bg, href);
    }
}

static void layout_grid(int node, int depth, const CssStyle* st) {
    int cols = st->grid_cols;
    if (cols > 4) cols = 4;
    const int colw = (layout_wrap - (cols - 1) * GRID_GAP) / cols;
    int children[GRID_CHILDREN];
    int nc = 0;
    for (int c = dom_node(node)->first; c >= 0; c = dom_node(c)->next) {
        if (is_blank_text(c)) continue;
        if (nc < GRID_CHILDREN) children[nc++] = c;
    }
    if (colw < 8) {
        for (int c = dom_node(node)->first; c >= 0; c = dom_node(c)->next) walk(c, depth + 1, st);
        return;
    }
    for (int k = 0; k < nc; k++) {
        grid_count[k] = sub_layout(children[k], depth + 1, st, colw, grid_buf[k], GRID_LINES);
    }
    const int rows = (nc + cols - 1) / cols;
    for (int r = 0; r < rows; r++) {
        int height = 0;
        for (int c = 0; c < cols; c++) {
            const int k = r * cols + c;
            if (k < nc && grid_count[k] > height) height = grid_count[k];
        }
        compose_row(r, cols, colw, nc, height);
        if (r + 1 < rows) add_blank(0);
    }
}

static int parse_int_attr(int node, const char* name, int fallback) {
    const char* v = dom_attr(node, name);
    if (!v) return fallback;
    int n = 0;
    int i = 0;
    while (v[i] >= '0' && v[i] <= '9') {
        n = n * 10 + (v[i] - '0');
        i++;
    }
    if (i == 0) return fallback;
    return n;
}

static void walk(int i, int depth, const CssStyle* parent) {
    if (depth > 60 || walk_budget-- <= 0) return;
    const DomNode* n = dom_node(i);
    if (!n) return;

    if (n->type == DOM_TYPE_TEXT) {
        char buf[512];
        int k = 0;
        const char* src = dom_text_pool_at(n->text_off);
        for (int j = 0; j < n->text_len && k < (int)sizeof(buf) - 1; j++) buf[k++] = src[j];
        buf[k] = '\0';
        if (parent && !parent->hidden) emit_text(buf, parent);
        return;
    }

    const char* t = n->tag;
    if (is_skip(t)) return;

    CssStyle st;
    css_compute(i, parent, &st);
    if (st.hidden) return;

    const int saved_bg_on = block_bg_on;
    const uint32_t saved_bg = block_bg;
    if (st.has_bg) {
        block_bg_on = 1;
        block_bg = st.bg;
    }

    if (st.mt > 0) {
        push_line();
        for (int k = 0; k < st.mt && k < 3; k++) add_blank(1);
    }

    if (st.display_grid && st.grid_cols > 1) {
        layout_grid(i, depth, &st);
    } else if (str_eq(t, "img")) {
        const char* src = dom_attr(i, "src");
        int w = parse_int_attr(i, "width", 32);
        int h = parse_int_attr(i, "height", 32);
        if (w < 8) w = 8;
        if (w > 128) w = 128;
        if (h < 8) h = 8;
        if (h > 128) h = 128;
        push_line();
        if (src && src[0]) {
            const int rows = (h + 17) / 18;
            add_line(LAYOUT_KIND_IMAGE, block_bg_on ? block_bg : 0, src, w, h,
                     (uint8_t)((st.center ? LAYOUT_FLAG_CENTER : 0) | (block_bg_on ? LAYOUT_FLAG_BG : 0)));
            for (int r = 1; r < rows; r++) add_line(LAYOUT_KIND_IMAGE_CONT, block_bg_on ? block_bg : 0, 0, 0, 0, 0);
        }
    } else if (t[0] == 'h' && t[1] >= '1' && t[1] <= '6' && t[2] == '\0') {
        push_line();
        const int saved = cur_kind;
        cur_kind = LAYOUT_KIND_HEADING;
        for (int c = n->first; c >= 0; c = dom_node(c)->next) walk(c, depth + 1, &st);
        push_line();
        cur_kind = saved;
        add_blank(0);
    } else if (str_eq(t, "a")) {
        const char* href = dom_attr(i, "href");
        const int had_link = in_link;
        if (href && href[0]) {
            if (!cur_has_href) {
                cur_has_href = 1;
                copy_href(cur_href, href);
            }
            copy_href(link_target, href);
            in_link = 1;
        }
        for (int c = n->first; c >= 0; c = dom_node(c)->next) walk(c, depth + 1, &st);
        if (!had_link) in_link = 0;
    } else if (str_eq(t, "br")) {
        push_line();
    } else if (str_eq(t, "td") || str_eq(t, "th")) {
        emit(' ', &st);
        for (int c = n->first; c >= 0; c = dom_node(c)->next) walk(c, depth + 1, &st);
    } else if (is_block(t)) {
        push_line();
        add_blank(0);
        for (int c = n->first; c >= 0; c = dom_node(c)->next) walk(c, depth + 1, &st);
        push_line();
        add_blank(0);
    } else if (str_eq(t, "div") || str_eq(t, "li") || str_eq(t, "tr") || str_eq(t, "dd") || str_eq(t, "dt")) {
        push_line();
        for (int c = n->first; c >= 0; c = dom_node(c)->next) walk(c, depth + 1, &st);
        push_line();
    } else {
        for (int c = n->first; c >= 0; c = dom_node(c)->next) walk(c, depth + 1, &st);
    }

    if (st.mb > 0) {
        push_line();
        for (int k = 0; k < st.mb && k < 3; k++) add_blank(1);
    }

    block_bg_on = saved_bg_on;
    block_bg = saved_bg;
}

int layout_build(int root, LayoutLine* out, int max) {
    out_lines = out;
    out_max = max;
    out_count = 0;
    cur_len = 0;
    cur_kind = LAYOUT_KIND_TEXT;
    cur_flags = 0;
    cur_indent = 0;
    cur_color = 0;
    cur_bg = 0;
    cur_nruns = 0;
    cur_has_href = 0;
    in_link = 0;
    link_target[0] = '\0';
    block_bg_on = 0;
    block_bg = 0;
    walk_budget = 50000;
    walk(root, 0, 0);
    push_line();
    while (out_count > 0 && out_lines[out_count - 1].text[0] == '\0' && out_lines[out_count - 1].kind == 0) out_count--;
    return out_count;
}
