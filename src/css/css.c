#include "css.h"
#include "dom.h"

#define CSS_MAX_RULES 256
#define CSS_MAX_COMPOUNDS 4
#define CSS_MAX_DECLS 10

typedef struct {
    char tag[12];
    char id[32];
    char cls[32];
    int dead;
    int first;
    int last;
    int has_nth;
    int na;
    int nb;
    int has_not;
    char not_tag[12];
    char not_id[32];
    char not_cls[32];
} Compound;

typedef struct {
    char prop[40];
    char value[72];
} Decl;

typedef struct {
    int ncomp;
    Compound comps[CSS_MAX_COMPOUNDS];
    int spec;
    int ndecl;
    Decl decls[CSS_MAX_DECLS];
} Rule;

static Rule rules[CSS_MAX_RULES];
static int rule_count;
static int order[CSS_MAX_RULES];

static int lc(int c) {
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

static int is_space(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static int str_eq(const char* a, const char* b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static int starts_ci_css(const char* s, const char* p) {
    while (*p) {
        if (lc((unsigned char)*s) != lc((unsigned char)*p)) return 0;
        s++;
        p++;
    }
    return 1;
}

static int str_eq_ci(const char* a, const char* b) {
    while (*a && lc((unsigned char)*a) == lc((unsigned char)*b)) {
        a++;
        b++;
    }
    return lc((unsigned char)*a) == lc((unsigned char)*b);
}

static int str_contains_ci(const char* hay, const char* needle) {
    for (int i = 0; hay[i]; i++) {
        int k = 0;
        while (needle[k] && hay[i + k] && lc((unsigned char)hay[i + k]) == lc((unsigned char)needle[k])) k++;
        if (!needle[k]) return 1;
    }
    return 0;
}

static void copy_n(char* dst, const char* src, int n, int max) {
    int k = 0;
    while (k < n && k < max - 1) {
        dst[k] = src[k];
        k++;
    }
    dst[k] = '\0';
}

static void trim_copy(char* dst, const char* src, int n, int max) {
    int a = 0;
    int b = n;
    while (a < b && is_space((unsigned char)src[a])) a++;
    while (b > a && is_space((unsigned char)src[b - 1])) b--;
    copy_n(dst, src + a, b - a, max);
}

static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static const struct {
    const char* name;
    uint32_t value;
} named_colors[] = {
    {"black", 0x000000}, {"white", 0xFFFFFF}, {"red", 0xFF0000}, {"green", 0x008000},
    {"lime", 0x00FF00}, {"blue", 0x0000FF}, {"yellow", 0xFFFF00}, {"orange", 0xFFA500},
    {"navy", 0x000080}, {"purple", 0x800080}, {"teal", 0x008080}, {"maroon", 0x800000},
    {"olive", 0x808000}, {"gray", 0x808080}, {"grey", 0x808080}, {"silver", 0xC0C0C0},
    {"aqua", 0x00FFFF}, {"cyan", 0x00FFFF}, {"fuchsia", 0xFF00FF}, {"magenta", 0xFF00FF},
    {"brown", 0xA52A2A}, {"darkgray", 0xA9A9A9}, {"darkgrey", 0xA9A9A9}, {"lightgray", 0xD3D3D3},
    {"lightgrey", 0xD3D3D3}, {"gold", 0xFFD700}, {"pink", 0xFFC0CB}
};

static int parse_color(const char* v, uint32_t* out) {
    while (is_space((unsigned char)*v)) v++;
    if (v[0] == '#') {
        int n = 0;
        uint32_t c = 0;
        for (int i = 1; v[i] && hexval(v[i]) >= 0 && n < 6; i++, n++) c = (c << 4) | (uint32_t)hexval(v[i]);
        if (n == 3) {
            const uint32_t r = (c >> 8) & 15, g = (c >> 4) & 15, b = c & 15;
            *out = (r * 17u << 16) | (g * 17u << 8) | (b * 17u);
            return 1;
        }
        if (n == 6) {
            *out = c;
            return 1;
        }
        return 0;
    }
    if (lc((unsigned char)v[0]) == 'r' && lc((unsigned char)v[1]) == 'g' && lc((unsigned char)v[2]) == 'b' && v[3] == '(') {
        int parts[3] = {0, 0, 0};
        int p = 0;
        const char* s = v + 4;
        while (*s && p < 3) {
            if (*s >= '0' && *s <= '9') parts[p] = parts[p] * 10 + (*s - '0');
            else if (*s == ',') p++;
            else if (*s == ')') break;
            s++;
        }
        for (int i = 0; i < 3; i++) {
            if (parts[i] > 255) parts[i] = 255;
        }
        *out = ((uint32_t)parts[0] << 16) | ((uint32_t)parts[1] << 8) | (uint32_t)parts[2];
        return 1;
    }
    for (int i = 0; i < (int)(sizeof(named_colors) / sizeof(named_colors[0])); i++) {
        if (str_eq_ci(v, named_colors[i].name)) {
            *out = named_colors[i].value;
            return 1;
        }
    }
    return 0;
}

static int parse_px(const char* v, int* px) {
    while (is_space((unsigned char)*v)) v++;
    if (*v == '-') return 0;
    int value = 0;
    int digits = 0;
    while (*v >= '0' && *v <= '9') {
        value = value * 10 + (*v - '0');
        v++;
        digits++;
    }
    if (*v == '.') {
        v++;
        if (*v >= '0' && *v <= '9') v++;
    }
    if (!digits) return 0;
    if (str_eq_ci(v, "px") || *v == '\0' || is_space((unsigned char)*v)) {
        *px = value;
        return 1;
    }
    if (str_eq_ci(v, "em") || str_eq_ci(v, "rem")) {
        *px = value * 16;
        return 1;
    }
    if (str_eq_ci(v, "pt")) {
        *px = value * 4 / 3;
        return 1;
    }
    return 0;
}

static int px_to_lines(int px) {
    return (px + 9) / 18;
}

static int px_to_chars(int px) {
    return (px + 6) / 12;
}

static void apply_decl(const char* prop, const char* value, CssStyle* st) {
    int px = 0;
    if (str_eq(prop, "color")) {
        uint32_t c;
        if (parse_color(value, &c)) {
            st->has_color = 1;
            st->color = c;
        }
    } else if (str_eq(prop, "background-color") || str_eq(prop, "background")) {
        uint32_t c;
        if (str_eq_ci(value, "none")) {
            st->has_bg = 0;
        } else if (parse_color(value, &c)) {
            st->has_bg = 1;
            st->bg = c;
        }
    } else if (str_eq(prop, "font-weight")) {
        st->bold = (str_eq_ci(value, "bold") || str_eq(value, "700") || str_eq(value, "800") || str_eq(value, "900"));
        if (str_eq_ci(value, "normal") || str_eq(value, "400")) st->bold = 0;
    } else if (str_eq(prop, "font-size")) {
        if (parse_px(value, &px)) st->big = px >= 20;
    } else if (str_eq(prop, "font-family")) {
        st->mono = str_contains_ci(value, "mono") || str_contains_ci(value, "courier");
    } else if (str_eq(prop, "text-align")) {
        st->center = str_eq_ci(value, "center");
    } else if (str_eq(prop, "display")) {
        if (str_eq_ci(value, "none")) st->hidden = 1;
        else if (str_contains_ci(value, "grid")) st->display_grid = 1;
        else if (str_contains_ci(value, "flex")) st->display_grid = 1;
    } else if (str_eq(prop, "grid-template-columns")) {
        if (str_eq_ci(value, "none")) {
            st->grid_cols = 0;
        } else if (str_contains_ci(value, "repeat(")) {
            int n = 0;
            const char* p = value;
            while (*p && !(p[0] == '(' && p > value)) p++;
            if (*p == '(') p++;
            while (*p >= '0' && *p <= '9') {
                n = n * 10 + (*p - '0');
                p++;
            }
            st->grid_cols = n;
        }
    } else if (str_eq(prop, "visibility")) {
        if (str_eq_ci(value, "hidden")) st->hidden = 1;
    } else if (str_eq(prop, "margin-top") || str_eq(prop, "padding-top")) {
        if (parse_px(value, &px)) st->mt = px_to_lines(px);
    } else if (str_eq(prop, "margin-bottom") || str_eq(prop, "padding-bottom")) {
        if (parse_px(value, &px)) st->mb = px_to_lines(px);
    } else if (str_eq(prop, "margin-left") || str_eq(prop, "padding-left")) {
        if (parse_px(value, &px)) st->indent = px_to_chars(px);
    } else if (str_eq(prop, "margin") || str_eq(prop, "padding")) {
        int tok[4] = {0, 0, 0, 0};
        int ntok = 0;
        const char* s = value;
        while (*s && ntok < 4) {
            while (is_space((unsigned char)*s)) s++;
            if (!*s) break;
            char word[16];
            int w = 0;
            while (*s && !is_space((unsigned char)*s) && w < 15) word[w++] = *s++;
            word[w] = '\0';
            int v = 0;
            if (parse_px(word, &v)) tok[ntok] = v;
            ntok++;
        }
        if (ntok >= 1) {
            st->mt = px_to_lines(tok[0]);
            st->mb = px_to_lines(tok[0]);
        }
        if (ntok >= 2) st->indent = px_to_chars(tok[1]);
        else if (ntok == 1) st->indent = px_to_chars(tok[0]);
    }
}

static void parse_decls(const char* text, int len, Decl* decls, int* count, int max) {
    int start = 0;
    for (int i = 0; i <= len && *count < max; i++) {
        if (i == len || text[i] == ';') {
            int colon = -1;
            for (int j = start; j < i; j++) {
                if (text[j] == ':') {
                    colon = j;
                    break;
                }
            }
            if (colon > start) {
                Decl* d = &decls[*count];
                char raw[40];
                trim_copy(raw, text + start, colon - start, sizeof(raw));
                for (int k = 0; raw[k]; k++) raw[k] = (char)lc((unsigned char)raw[k]);
                copy_n(d->prop, raw, 39, 40);
                trim_copy(d->value, text + colon + 1, i - colon - 1, sizeof(d->value));
                if (d->prop[0]) (*count)++;
            }
            start = i + 1;
        }
    }
}

static int read_word(const char* s, int n, int* i, char* out, int max, const char* stops) {
    int m = 0;
    while (*i < n) {
        const char c = s[*i];
        if (c == '\\' && *i + 1 < n) {
            if (m < max - 1) out[m++] = s[*i + 1];
            *i += 2;
            continue;
        }
        int stop = c == '\0';
        for (int k = 0; stops[k]; k++) {
            if (c == stops[k]) stop = 1;
        }
        if (stop) break;
        if (m < max - 1) out[m++] = (char)lc((unsigned char)c);
        (*i)++;
    }
    out[m] = '\0';
    return m;
}

static int parse_nth(const char* arg, int* a, int* b) {
    char w[16];
    int i = 0;
    int n = 0;
    while (arg[n]) n++;
    trim_copy(w, arg, n, sizeof(w));
    for (int k = 0; w[k]; k++) w[k] = (char)lc((unsigned char)w[k]);
    if (str_eq(w, "odd")) {
        *a = 2;
        *b = 1;
        return 1;
    }
    if (str_eq(w, "even")) {
        *a = 2;
        *b = 0;
        return 1;
    }
    int sign_a = 1;
    int va = 0;
    int has_a = 0;
    if (w[i] == '-') {
        sign_a = -1;
        i++;
    } else if (w[i] == '+') {
        i++;
    }
    while (w[i] >= '0' && w[i] <= '9') {
        va = va * 10 + (w[i] - '0');
        i++;
        has_a = 1;
    }
    if (w[i] == 'n') {
        i++;
        *a = has_a ? sign_a * va : sign_a;
        int sign_b = 1;
        int vb = 0;
        if (w[i] == '-') sign_b = -1;
        if (w[i] == '-' || w[i] == '+') i++;
        while (w[i] >= '0' && w[i] <= '9') {
            vb = vb * 10 + (w[i] - '0');
            i++;
        }
        *b = sign_b * vb;
        return w[i] == '\0';
    }
    int vb = 0;
    if (w[i] == '\0') return 0;
    while (w[i] >= '0' && w[i] <= '9') {
        vb = vb * 10 + (w[i] - '0');
        i++;
    }
    *a = 0;
    *b = sign_a * vb;
    return w[i] == '\0';
}

static int parse_simple(const char* s, int n, char* tag, int tmax, char* id, int imax, char* cls, int cmax, int* dead) {
    int i = 0;
    tag[0] = '\0';
    id[0] = '\0';
    cls[0] = '\0';
    read_word(s, n, &i, tag, tmax, "#.:(");
    if (tag[0] == '*') tag[0] = '\0';
    while (i < n) {
        const char kind = s[i++];
        if (kind == '#') {
            read_word(s, n, &i, id, imax, "#.:(");
        } else if (kind == '.') {
            read_word(s, n, &i, cls, cmax, "#.:(");
        } else {
            *dead = 1;
            return 0;
        }
    }
    return 1;
}

static int parse_compound(const char* s, int n, Compound* c) {
    c->dead = 0;
    c->first = 0;
    c->last = 0;
    c->has_nth = 0;
    c->has_not = 0;
    c->tag[0] = '\0';
    c->id[0] = '\0';
    c->cls[0] = '\0';
    int i = 0;
    read_word(s, n, &i, c->tag, 12, "#.:");
    if (c->tag[0] == '*') c->tag[0] = '\0';
    while (i < n) {
        const char kind = s[i++];
        if (kind == '#') {
            read_word(s, n, &i, c->id, 32, "#.:");
        } else if (kind == '.') {
            read_word(s, n, &i, c->cls, 32, "#.:");
        } else if (kind == ':') {
            char name[24];
            read_word(s, n, &i, name, sizeof(name), "(#.:");
            char arg[64];
            arg[0] = '\0';
            if (i < n && s[i] == '(') {
                i++;
                int m = 0;
                while (i < n && s[i] != ')' && m < 63) arg[m++] = s[i++];
                arg[m] = '\0';
                if (i < n) i++;
            }
            if (str_eq(name, "first-child")) {
                c->first = 1;
            } else if (str_eq(name, "last-child")) {
                c->last = 1;
            } else if (str_eq(name, "nth-child")) {
                if (!parse_nth(arg, &c->na, &c->nb)) {
                    c->dead = 1;
                    return 0;
                }
                c->has_nth = 1;
            } else if (str_eq(name, "not")) {
                int dead = 0;
                int alen = 0;
                while (arg[alen]) alen++;
                if (!parse_simple(arg, alen, c->not_tag, 12, c->not_id, 32, c->not_cls, 32, &dead) || dead) {
                    c->dead = 1;
                    return 0;
                }
                c->has_not = 1;
            } else {
                c->dead = 1;
                return 0;
            }
        } else {
            c->dead = 1;
            return 0;
        }
    }
    return 1;
}

static int compute_spec(const Rule* r) {
    int spec = 0;
    for (int i = 0; i < r->ncomp; i++) {
        const Compound* c = &r->comps[i];
        if (c->id[0]) spec += 100;
        if (c->cls[0] || c->first || c->last || c->has_nth || c->has_not) spec += 10;
        if (c->tag[0]) spec += 1;
    }
    return spec;
}

static void sort_rules(void) {
    for (int i = 0; i < rule_count; i++) order[i] = i;
    for (int i = 1; i < rule_count; i++) {
        const int v = order[i];
        int j = i - 1;
        while (j >= 0 && rules[order[j]].spec > rules[v].spec) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = v;
    }
}

static void add_rule(const char* sel, int sel_len, const Decl* decls, int ndecl) {
    if (rule_count >= CSS_MAX_RULES) return;
    Rule* r = &rules[rule_count];
    r->ncomp = 0;
    r->ndecl = ndecl;
    for (int i = 0; i < ndecl; i++) r->decls[i] = decls[i];
    int i = 0;
    while (i < sel_len) {
        while (i < sel_len && (is_space((unsigned char)sel[i]) || sel[i] == '>' || sel[i] == '+' || sel[i] == '~')) i++;
        if (i >= sel_len) break;
        const int start = i;
        int depth = 0;
        while (i < sel_len) {
            const char c = sel[i];
            if (c == '\\' && i + 1 < sel_len) {
                i += 2;
                continue;
            }
            if (c == '(') depth++;
            if (c == ')') depth--;
            if (depth == 0 && (is_space((unsigned char)c) || c == '>' || c == '+' || c == '~')) break;
            i++;
        }
        if (r->ncomp >= CSS_MAX_COMPOUNDS) return;
        if (!parse_compound(sel + start, i - start, &r->comps[r->ncomp])) return;
        r->ncomp++;
    }
    if (r->ncomp == 0) return;
    r->spec = compute_spec(r);
    rule_count++;
}

void css_reset(void) {
    rule_count = 0;
}

static int css_viewport_width = 640;

void css_set_viewport(int width) {
    css_viewport_width = width;
}

static int media_matches(const char* q, int n) {
    int ok = 1;
    int any = 0;
    int i = 0;
    while (i < n) {
        if (q[i] != '(') {
            i++;
            continue;
        }
        int e = i + 1;
        while (e < n && q[e] != ')') e++;
        int colon = i + 1;
        while (colon < e && q[colon] != ':') colon++;
        char feat[24];
        int fk = 0;
        for (int k = i + 1; k < colon && fk < 23; k++) {
            if (q[k] != ' ') feat[fk++] = (char)lc((unsigned char)q[k]);
        }
        feat[fk] = '\0';
        int value = 0;
        for (int k = colon + 1; k < e; k++) {
            if (q[k] >= '0' && q[k] <= '9') value = value * 10 + (q[k] - '0');
        }
        any = 1;
        if (str_eq(feat, "min-width")) ok = ok && css_viewport_width >= value;
        else if (str_eq(feat, "max-width")) ok = ok && css_viewport_width <= value;
        else ok = 0;
        i = e + 1;
    }
    if (n >= 5 && starts_ci_css(q, "print")) return 0;
    return any && ok;
}

static void parse_sheet(const char* text, int start, int end, int depth) {
    int i = start;
    while (i < end && depth < 4) {
        if (text[i] == '/' && i + 1 < end && text[i + 1] == '*') {
            int e = i + 2;
            while (e + 1 < end && !(text[e] == '*' && text[e + 1] == '/')) e++;
            i = e + 2;
            continue;
        }
        if (is_space((unsigned char)text[i]) || text[i] == '}') {
            i++;
            continue;
        }
        const int head = i;
        while (i < end && text[i] != '{' && text[i] != ';') i++;
        if (i >= end) break;
        if (text[i] == ';') {
            i++;
            continue;
        }
        const int head_end = i;
        int depth_braces = 1;
        i++;
        const int body_start = i;
        while (i < end && depth_braces > 0) {
            if (text[i] == '{') depth_braces++;
            if (text[i] == '}') depth_braces--;
            i++;
        }
        const int body_end = i - 1;
        if (text[head] == '@') {
            if (head_end - head >= 6 && starts_ci_css(text + head, "@media")) {
                if (media_matches(text + head + 6, head_end - head - 6)) {
                    parse_sheet(text, body_start, body_end, depth + 1);
                }
            }
            continue;
        }
        Decl decls[CSS_MAX_DECLS];
        int nd = 0;
        parse_decls(text + body_start, body_end - body_start, decls, &nd, CSS_MAX_DECLS);
        int sstart = head;
        for (int k = head; k <= head_end; k++) {
            const int at_sep = (k == head_end) || (text[k] == ',' && (k == head || text[k - 1] != '\\'));
            if (at_sep) {
                add_rule(text + sstart, k - sstart, decls, nd);
                sstart = k + 1;
            }
        }
    }
}

void css_add_sheet(const char* text, int len) {
    parse_sheet(text, 0, len, 0);
    sort_rules();
}

static int has_class(int node, const char* cls) {
    const char* v = dom_attr(node, "class");
    if (!v) return 0;
    int i = 0;
    while (v[i]) {
        while (v[i] && is_space((unsigned char)v[i])) i++;
        const int start = i;
        while (v[i] && !is_space((unsigned char)v[i])) i++;
        if (i > start) {
            int k = 0;
            while (cls[k] && start + k < i && v[start + k] == cls[k]) k++;
            if (!cls[k] && start + k == i) return 1;
        }
    }
    return 0;
}

static int simple_match(int node, const char* tag, const char* id, const char* cls) {
    const DomNode* n = dom_node(node);
    if (!n || n->type != DOM_TYPE_ELEMENT) return 0;
    if (tag[0] && !str_eq(n->tag, tag)) return 0;
    if (id[0]) {
        const char* v = dom_attr(node, "id");
        if (!v || !str_eq(v, id)) return 0;
    }
    if (cls[0] && !has_class(node, cls)) return 0;
    return 1;
}

static void element_position(int node, int* pos, int* total) {
    *pos = 1;
    *total = 1;
    const DomNode* n = dom_node(node);
    if (!n || n->parent < 0) return;
    const DomNode* p = dom_node(n->parent);
    if (!p) return;
    int count = 0;
    int found = 0;
    for (int c = p->first; c >= 0; c = dom_node(c)->next) {
        if (dom_node(c)->type != DOM_TYPE_ELEMENT) continue;
        count++;
        if (c == node) {
            *pos = count;
            found = 1;
        }
    }
    *total = count;
    if (!found) *pos = 1;
}

static int nth_matches(int a, int b, int pos) {
    if (a == 0) return pos == b;
    const int d = pos - b;
    return d % a == 0 && d / a >= 0;
}

static int match_compound(int node, const Compound* c) {
    if (c->dead) return 0;
    if (!simple_match(node, c->tag, c->id, c->cls)) return 0;
    if (c->first || c->last || c->has_nth) {
        int pos;
        int total;
        element_position(node, &pos, &total);
        if (c->first && pos != 1) return 0;
        if (c->last && pos != total) return 0;
        if (c->has_nth && !nth_matches(c->na, c->nb, pos)) return 0;
    }
    if (c->has_not && simple_match(node, c->not_tag, c->not_id, c->not_cls)) return 0;
    return 1;
}

static int match_rule(int node, const Rule* r) {
    if (!match_compound(node, &r->comps[r->ncomp - 1])) return 0;
    int k = r->ncomp - 2;
    int p = dom_node(node) ? dom_node(node)->parent : -1;
    int guard = 0;
    while (k >= 0 && p >= 0 && guard++ < 200) {
        if (match_compound(p, &r->comps[k])) k--;
        p = dom_node(p)->parent;
    }
    return k < 0;
}

static void ua_defaults(int node, CssStyle* st) {
    const DomNode* n = dom_node(node);
    if (!n) return;
    const char* t = n->tag;
    if (t[0] == 'h' && t[1] >= '1' && t[1] <= '6' && t[2] == '\0') {
        st->bold = 1;
        st->has_color = 1;
        st->color = 0x000080;
        st->big = t[1] <= '2';
    } else if (str_eq(t, "a")) {
        st->has_color = 1;
        st->color = 0x0000EE;
    } else if (str_eq(t, "b") || str_eq(t, "strong")) {
        st->bold = 1;
    } else if (str_eq(t, "center")) {
        st->center = 1;
    } else if (str_eq(t, "code") || str_eq(t, "pre") || str_eq(t, "kbd") || str_eq(t, "samp")) {
        st->mono = 1;
    }
}

void css_compute(int node, const CssStyle* parent, CssStyle* out) {
    out->has_color = parent ? parent->has_color : 0;
    out->color = parent ? parent->color : 0;
    out->bold = parent ? parent->bold : 0;
    out->center = parent ? parent->center : 0;
    out->mono = parent ? parent->mono : 0;
    out->big = parent ? parent->big : 0;
    out->hidden = 0;
    out->has_bg = 0;
    out->bg = 0;
    out->mt = 0;
    out->mb = 0;
    out->indent = 0;
    out->display_grid = 0;
    out->grid_cols = 0;
    ua_defaults(node, out);
    for (int k = 0; k < rule_count; k++) {
        const Rule* r = &rules[order[k]];
        if (!match_rule(node, r)) continue;
        for (int d = 0; d < r->ndecl; d++) apply_decl(r->decls[d].prop, r->decls[d].value, out);
    }
    const char* inline_style = dom_attr(node, "style");
    if (inline_style) {
        Decl decls[CSS_MAX_DECLS];
        int nd = 0;
        int len = 0;
        while (inline_style[len]) len++;
        parse_decls(inline_style, len, decls, &nd, CSS_MAX_DECLS);
        for (int d = 0; d < nd; d++) apply_decl(decls[d].prop, decls[d].value, out);
    }
}
