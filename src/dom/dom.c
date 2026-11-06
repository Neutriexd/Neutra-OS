#include "dom.h"

static DomNode nodes[DOM_MAX];
static int node_count;
static char text_pool[DOM_TEXT_POOL];
static int text_used;
static int root_index;

static int lc(int c) {
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

static int str_eq(const char* a, const char* b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static int str_len(const char* s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static void copy_str(char* dst, const char* src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
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

static int alloc_node(int type) {
    if (node_count >= DOM_MAX) return -1;
    const int i = node_count++;
    DomNode* n = &nodes[i];
    n->type = type;
    n->tag[0] = '\0';
    n->attr_count = 0;
    n->text_off = 0;
    n->text_len = 0;
    n->parent = -1;
    n->first = -1;
    n->last = -1;
    n->next = -1;
    return i;
}

static int text_add(const char* s, int len) {
    if (text_used + len >= DOM_TEXT_POOL) return -1;
    const int off = text_used;
    for (int i = 0; i < len; i++) text_pool[text_used++] = s[i];
    text_pool[text_used] = '\0';
    text_used++;
    return off;
}

static void unlink_child(int child) {
    const int p = nodes[child].parent;
    if (p < 0) return;
    if (nodes[p].first == child) {
        nodes[p].first = nodes[child].next;
    } else {
        int prev = nodes[p].first;
        int guard = 0;
        while (prev >= 0 && nodes[prev].next != child && guard++ < DOM_MAX) prev = nodes[prev].next;
        if (prev >= 0) nodes[prev].next = nodes[child].next;
    }
    if (nodes[p].last == child) {
        int prev = nodes[p].first;
        int guard = 0;
        while (prev >= 0 && nodes[prev].next >= 0 && guard++ < DOM_MAX) prev = nodes[prev].next;
        nodes[p].last = prev;
    }
    nodes[child].parent = -1;
    nodes[child].next = -1;
}

static int is_ancestor(int anc, int node) {
    int guard = 0;
    for (int p = node; p >= 0 && guard++ < DOM_MAX; p = nodes[p].parent) {
        if (p == anc) return 1;
    }
    return 0;
}

static void link_child(int parent, int child) {
    if (parent == child || is_ancestor(child, parent)) return;
    unlink_child(child);
    nodes[child].parent = parent;
    nodes[child].next = -1;
    if (nodes[parent].first < 0) {
        nodes[parent].first = child;
    } else {
        nodes[nodes[parent].last].next = child;
    }
    nodes[parent].last = child;
}

static int make_text(int parent, const char* s, int len) {
    const int t = alloc_node(DOM_TYPE_TEXT);
    if (t < 0) return -1;
    const int off = text_add(s, len);
    if (off < 0) return -1;
    nodes[t].text_off = off;
    nodes[t].text_len = len;
    link_child(parent, t);
    return t;
}

static int is_void(const char* n) {
    static const char* const voids[] = {"area", "base", "br", "col", "embed", "hr", "img", "input",
                                        "link", "meta", "source", "track", "wbr", "param"};
    for (int i = 0; i < (int)(sizeof(voids) / sizeof(voids[0])); i++) {
        if (str_eq(n, voids[i])) return 1;
    }
    return 0;
}

static int is_raw(const char* n) {
    return str_eq(n, "script") || str_eq(n, "style");
}

static void parse_attrs(int node, const uint8_t* b, int from, int to) {
    int i = from;
    while (i < to) {
        while (i < to && (b[i] == ' ' || b[i] == '\t' || b[i] == '\n' || b[i] == '\r' || b[i] == '/')) i++;
        if (i >= to) break;
        char name[DOM_NAME];
        int k = 0;
        while (i < to && b[i] != ' ' && b[i] != '=' && b[i] != '/' && b[i] != '\t' && b[i] != '\n' && b[i] != '\r') {
            if (k < DOM_NAME - 1) name[k++] = (char)lc(b[i]);
            i++;
        }
        name[k] = '\0';
        if (k == 0) {
            i++;
            continue;
        }
        while (i < to && b[i] == ' ') i++;
        char value[DOM_VALUE];
        value[0] = '\0';
        if (i < to && b[i] == '=') {
            i++;
            while (i < to && b[i] == ' ') i++;
            char quote = 0;
            if (i < to && (b[i] == '"' || b[i] == '\'')) {
                quote = (char)b[i];
                i++;
            }
            int v = 0;
            while (i < to && v < DOM_VALUE - 1) {
                if (quote) {
                    if ((char)b[i] == quote) break;
                } else if (b[i] == ' ') {
                    break;
                }
                if (b[i] == '&') {
                    if (i + 4 < to && b[i + 1] == 'a' && b[i + 2] == 'm' && b[i + 3] == 'p' && b[i + 4] == ';') {
                        value[v++] = '&';
                        i += 5;
                        continue;
                    }
                    if (i + 5 < to && b[i + 1] == 'q' && b[i + 2] == 'u' && b[i + 3] == 'o' && b[i + 4] == 't' && b[i + 5] == ';') {
                        value[v++] = '"';
                        i += 6;
                        continue;
                    }
                    if (i + 3 < to && b[i + 1] == 'l' && b[i + 2] == 't' && b[i + 3] == ';') {
                        value[v++] = '<';
                        i += 4;
                        continue;
                    }
                    if (i + 3 < to && b[i + 1] == 'g' && b[i + 2] == 't' && b[i + 3] == ';') {
                        value[v++] = '>';
                        i += 4;
                        continue;
                    }
                    if (i + 4 < to && b[i + 1] == '#' && b[i + 2] == '3' && b[i + 3] == '9' && b[i + 4] == ';') {
                        value[v++] = '\'';
                        i += 5;
                        continue;
                    }
                }
                value[v++] = (char)b[i++];
            }
            value[v] = '\0';
            if (quote && i < to) i++;
        }
        DomNode* n = &nodes[node];
        if (n->attr_count < DOM_ATTRS) {
            copy_str(n->attrs[n->attr_count].name, name, DOM_NAME);
            copy_str(n->attrs[n->attr_count].value, value, DOM_VALUE);
            n->attr_count++;
        }
    }
}

static void parse_into(int root, const uint8_t* b, int n) {
    int stack[64];
    int sp = 0;
    stack[sp++] = root;
    char tb[2048];
    int tl = 0;
    int i = 0;

    while (i < n) {
        const uint8_t c = b[i];
        if (c == '<') {
            if (tl > 0) {
                make_text(stack[sp - 1], tb, tl);
                tl = 0;
            }
            if (i + 3 < n && b[i + 1] == '!' && b[i + 2] == '-' && b[i + 3] == '-') {
                const int e = ci_find(b, n, i + 4, "-->");
                i = e < 0 ? n : e + 3;
                continue;
            }
            if (i + 1 < n && (b[i + 1] == '!' || b[i + 1] == '?')) {
                int e = i;
                while (e < n && b[e] != '>') e++;
                i = e + 1;
                continue;
            }
            const int closing = (i + 1 < n && b[i + 1] == '/');
            int j = i + 1 + (closing ? 1 : 0);
            char name[DOM_NAME];
            int k = 0;
            while (j < n && k < DOM_NAME - 1 && ((b[j] >= 'a' && b[j] <= 'z') || (b[j] >= 'A' && b[j] <= 'Z') ||
                                                  (b[j] >= '0' && b[j] <= '9'))) {
                name[k++] = (char)lc(b[j]);
                j++;
            }
            name[k] = '\0';
            if (k == 0) {
                tb[tl++] = '<';
                i++;
                continue;
            }
            int e = j;
            while (e < n && b[e] != '>') e++;
            if (e >= n) break;
            if (closing) {
                for (int s = sp - 1; s >= 1; s--) {
                    if (str_eq(nodes[stack[s]].tag, name)) {
                        sp = s;
                        break;
                    }
                }
                i = e + 1;
                continue;
            }
            const int el = alloc_node(DOM_TYPE_ELEMENT);
            if (el < 0) break;
            copy_str(nodes[el].tag, name, DOM_NAME);
            link_child(stack[sp - 1], el);
            parse_attrs(el, b, j, e);
            i = e + 1;
            if (is_void(name)) continue;
            if (is_raw(name)) {
                char needle[DOM_NAME + 2];
                needle[0] = '<';
                needle[1] = '/';
                int q = 0;
                while (name[q] && q + 2 < (int)sizeof(needle) - 1) {
                    needle[q + 2] = name[q];
                    q++;
                }
                needle[q + 2] = '\0';
                const int close = ci_find(b, n, i, needle);
                int content_end = close < 0 ? n : close;
                int after = n;
                if (close >= 0) {
                    int g = close;
                    while (g < n && b[g] != '>') g++;
                    after = g + 1;
                }
                if (content_end > i) make_text(el, (const char*)(b + i), content_end - i);
                i = after;
                continue;
            }
            if (sp < 64) stack[sp++] = el;
            continue;
        }
        if (c == '&') {
            int e = i + 1;
            while (e < n && e < i + 9 && b[e] != ';' && b[e] != ' ' && b[e] != '<') e++;
            if (e < n && b[e] == ';') {
                char ent[9];
                int q = 0;
                for (int j = i + 1; j < e; j++) ent[q++] = (char)b[j];
                ent[q] = '\0';
                char value = 0;
                if (str_eq(ent, "amp")) value = '&';
                else if (str_eq(ent, "lt")) value = '<';
                else if (str_eq(ent, "gt")) value = '>';
                else if (str_eq(ent, "quot")) value = '"';
                else if (str_eq(ent, "apos") || str_eq(ent, "#39")) value = '\'';
                else if (str_eq(ent, "nbsp")) value = ' ';
                if (value && tl < (int)sizeof(tb) - 1) {
                    tb[tl++] = value;
                    i = e + 1;
                    continue;
                }
            }
            if (tl < (int)sizeof(tb) - 1) tb[tl++] = '&';
            i++;
            continue;
        }
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
            if ((tl == 0 || tb[tl - 1] != ' ') && tl < (int)sizeof(tb) - 1) tb[tl++] = ' ';
            i++;
            continue;
        }
        if (tl < (int)sizeof(tb) - 1) tb[tl++] = (c > 126 || c < 32) ? '?' : (char)c;
        i++;
    }
    if (tl > 0) make_text(stack[sp - 1], tb, tl);
}

void dom_reset(void) {
    node_count = 0;
    text_used = 0;
    root_index = -1;
}

int dom_parse_document(const uint8_t* b, int n) {
    dom_reset();
    root_index = alloc_node(DOM_TYPE_ELEMENT);
    copy_str(nodes[root_index].tag, "root", DOM_NAME);
    parse_into(root_index, b, n);
    return root_index;
}

const DomNode* dom_node(int i) {
    if (i < 0 || i >= node_count) return 0;
    return &nodes[i];
}

const char* dom_text_pool_at(int off) {
    return &text_pool[off];
}

int dom_body(void) {
    static int stack[DOM_MAX];
    int sp = 0;
    int steps = 0;
    stack[sp++] = root_index;
    while (sp > 0 && steps++ < DOM_MAX * 4) {
        const int cur = stack[--sp];
        if (str_eq(nodes[cur].tag, "body")) return cur;
        for (int c = nodes[cur].first; c >= 0; c = nodes[c].next) {
            if (nodes[c].type == DOM_TYPE_ELEMENT && sp < DOM_MAX) stack[sp++] = c;
        }
    }
    return root_index;
}

const char* dom_attr(int i, const char* name) {
    if (i < 0 || i >= node_count) return 0;
    const DomNode* n = &nodes[i];
    for (int a = 0; a < n->attr_count; a++) {
        if (str_eq(n->attrs[a].name, name)) return n->attrs[a].value;
    }
    return 0;
}

void dom_set_attr(int i, const char* name, const char* value) {
    if (i < 0 || i >= node_count) return;
    DomNode* n = &nodes[i];
    for (int a = 0; a < n->attr_count; a++) {
        if (str_eq(n->attrs[a].name, name)) {
            copy_str(n->attrs[a].value, value, DOM_VALUE);
            return;
        }
    }
    if (n->attr_count < DOM_ATTRS) {
        copy_str(n->attrs[n->attr_count].name, name, DOM_NAME);
        copy_str(n->attrs[n->attr_count].value, value, DOM_VALUE);
        n->attr_count++;
    }
}

int dom_get_by_id(const char* id) {
    for (int i = 0; i < node_count; i++) {
        if (nodes[i].type != DOM_TYPE_ELEMENT) continue;
        const char* v = dom_attr(i, "id");
        if (v && str_eq(v, id)) return i;
    }
    return -1;
}

int dom_create(const char* tag) {
    const int el = alloc_node(DOM_TYPE_ELEMENT);
    if (el < 0) return -1;
    copy_str(nodes[el].tag, tag, DOM_NAME);
    return el;
}

void dom_append(int parent, int child) {
    if (parent < 0 || child < 0 || parent >= node_count || child >= node_count) return;
    link_child(parent, child);
}

static int collect_text(int i, char* out, int pos, int max, int depth) {
    if (depth > 200) return pos;
    const DomNode* n = &nodes[i];
    if (n->type == DOM_TYPE_TEXT) {
        for (int k = 0; k < n->text_len && pos < max - 1; k++) out[pos++] = text_pool[n->text_off + k];
        return pos;
    }
    for (int c = n->first; c >= 0; c = nodes[c].next) pos = collect_text(c, out, pos, max, depth + 1);
    return pos;
}

int dom_text_content(int i, char* out, int max) {
    if (i < 0 || i >= node_count || max <= 0) {
        if (max > 0) out[0] = '\0';
        return 0;
    }
    const int pos = collect_text(i, out, 0, max, 0);
    out[pos] = '\0';
    return pos;
}

void dom_set_text(int i, const char* text) {
    if (i < 0 || i >= node_count) return;
    nodes[i].first = -1;
    nodes[i].last = -1;
    make_text(i, text, str_len(text));
}

void dom_set_html(int i, const char* html, int len) {
    if (i < 0 || i >= node_count) return;
    nodes[i].first = -1;
    nodes[i].last = -1;
    parse_into(i, (const uint8_t*)html, len);
}

static int collect_tag_rec(int i, const char* tag, int* out, int count, int max, int depth) {
    if (depth > 200) return count;
    if (nodes[i].type == DOM_TYPE_ELEMENT && str_eq(nodes[i].tag, tag) && count < max) out[count++] = i;
    for (int c = nodes[i].first; c >= 0; c = nodes[c].next) count = collect_tag_rec(c, tag, out, count, max, depth + 1);
    return count;
}

int dom_collect_tag(const char* tag, int* out, int max) {
    if (root_index < 0) return 0;
    return collect_tag_rec(root_index, tag, out, 0, max, 0);
}
