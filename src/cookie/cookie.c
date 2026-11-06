#include "cookie.h"

#define COOKIE_MAX 32
#define COOKIE_NAME 40
#define COOKIE_VALUE 200
#define COOKIE_PATH 48
#define COOKIE_HOST 64

typedef struct {
    int used;
    char host[COOKIE_HOST];
    char name[COOKIE_NAME];
    char value[COOKIE_VALUE];
    char path[COOKIE_PATH];
} Cookie;

static Cookie jar[COOKIE_MAX];

static int lc(int c) {
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

static int starts_ci(const char* s, const char* p) {
    while (*p) {
        if (lc((unsigned char)*s) != lc((unsigned char)*p)) return 0;
        s++;
        p++;
    }
    return 1;
}

static int eq_ci(const char* a, const char* b) {
    while (*a && lc((unsigned char)*a) == lc((unsigned char)*b)) {
        a++;
        b++;
    }
    return lc((unsigned char)*a) == lc((unsigned char)*b);
}

static void copy_n(char* dst, const char* src, int n, int max) {
    int k = 0;
    while (k < n && k < max - 1) {
        dst[k] = src[k];
        k++;
    }
    dst[k] = '\0';
}

static int trim_len(const char* s, int n, int* start) {
    int a = 0;
    while (a < n && (s[a] == ' ' || s[a] == '\t')) a++;
    int b = n;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) b--;
    *start = a;
    return b - a;
}

static int find_host_name(const char* host, const char* name) {
    for (int i = 0; i < COOKIE_MAX; i++) {
        if (jar[i].used && eq_ci(jar[i].host, host) && jar[i].name[0] && eq_ci(jar[i].name, name)) return i;
    }
    return -1;
}

static void remove_cookie(int idx) {
    jar[idx].used = 0;
    jar[idx].name[0] = '\0';
}

static int free_slot(void) {
    for (int i = 0; i < COOKIE_MAX; i++) {
        if (!jar[i].used) return i;
    }
    return 0;
}

static void store_one(const char* host, const char* line, int len) {
    int colon = -1;
    for (int i = 0; i < len; i++) {
        if (line[i] == ':') {
            colon = i;
            break;
        }
    }
    if (colon < 0) return;
    int s = 0;
    const int vlen = trim_len(line + colon + 1, len - colon - 1, &s);
    const char* v = line + colon + 1 + s;

    int semi = vlen;
    for (int i = 0; i < vlen; i++) {
        if (v[i] == ';') {
            semi = i;
            break;
        }
    }
    int eq = -1;
    for (int i = 0; i < semi; i++) {
        if (v[i] == '=') {
            eq = i;
            break;
        }
    }
    if (eq <= 0) return;
    int ns = 0;
    const int nlen = trim_len(v, eq, &ns);
    char name[COOKIE_NAME];
    copy_n(name, v + ns, nlen, COOKIE_NAME);
    int vs = 0;
    const int vallen = trim_len(v + eq + 1, semi - eq - 1, &vs);
    char value[COOKIE_VALUE];
    copy_n(value, v + eq + 1 + vs, vallen, COOKIE_VALUE);

    char path[COOKIE_PATH];
    copy_n(path, "/", 2, COOKIE_PATH);
    int delete_it = 0;
    int pos = semi;
    while (pos < vlen) {
        pos++;
        int attr_end = pos;
        while (attr_end < vlen && v[attr_end] != ';') attr_end++;
        int as = 0;
        const int al = trim_len(v + pos, attr_end - pos, &as);
        const char* a = v + pos + as;
        if (al >= 5 && starts_ci(a, "path=")) {
            copy_n(path, a + 5, al - 5, COOKIE_PATH);
        } else if (al >= 8 && starts_ci(a, "max-age=") && a[8] == '0' && al == 9) {
            delete_it = 1;
        }
        pos = attr_end;
    }

    const int idx = find_host_name(host, name);
    if (delete_it) {
        if (idx >= 0) remove_cookie(idx);
        return;
    }
    const int slot = idx >= 0 ? idx : free_slot();
    jar[slot].used = 1;
    copy_n(jar[slot].host, host, COOKIE_HOST, COOKIE_HOST);
    copy_n(jar[slot].name, name, COOKIE_NAME, COOKIE_NAME);
    copy_n(jar[slot].value, value, COOKIE_VALUE, COOKIE_VALUE);
    copy_n(jar[slot].path, path, COOKIE_PATH, COOKIE_PATH);
}

void cookie_store(const char* host, const char* headers, int hlen) {
    int i = 0;
    while (i < hlen) {
        int e = i;
        while (e < hlen && headers[e] != '\r' && headers[e] != '\n') e++;
        if (e - i > 11 && starts_ci(headers + i, "set-cookie:")) store_one(host, headers + i, e - i);
        i = e;
        while (i < hlen && (headers[i] == '\r' || headers[i] == '\n')) i++;
    }
}

static int path_matches(const char* cookie_path, const char* req_path) {
    int i = 0;
    while (cookie_path[i] && req_path[i] && cookie_path[i] == req_path[i]) i++;
    return cookie_path[i] == '\0';
}

int cookie_header(const char* host, const char* path, char* out, int max) {
    int pos = 0;
    out[0] = '\0';
    for (int i = 0; i < COOKIE_MAX; i++) {
        if (!jar[i].used || !eq_ci(jar[i].host, host) || !path_matches(jar[i].path, path)) continue;
        if (pos > 0 && pos < max - 2) {
            out[pos++] = ';';
            out[pos++] = ' ';
        }
        for (int k = 0; jar[i].name[k] && pos < max - 1; k++) out[pos++] = jar[i].name[k];
        if (pos < max - 1) out[pos++] = '=';
        for (int k = 0; jar[i].value[k] && pos < max - 1; k++) out[pos++] = jar[i].value[k];
    }
    out[pos] = '\0';
    return pos;
}
