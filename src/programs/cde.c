#include <stdint.h>
#include "cde.h"

enum {
    OP_NOP = 0, OP_PUSH = 1, OP_POP = 2, OP_ADD = 3, OP_SUB = 4, OP_MUL = 5, OP_DIV = 6, OP_MOD = 7,
    OP_PRINT = 8, OP_PRINT_CHAR = 9, OP_SET_VAR = 10, OP_GET_VAR = 11, OP_JMP = 12, OP_JZ = 13,
    OP_JNZ = 14, OP_CALL = 15, OP_RET = 16, OP_PRINT_STR = 21,
    OP_EQ = 0x20, OP_NE = 0x21, OP_LT = 0x22, OP_GT = 0x23, OP_LE = 0x24, OP_GE = 0x25,
    OP_KEY = 0x36, OP_MAINLOOP = 0x37,
    OP_WIN_CREATE = 0x40, OP_WIN_PRINT_STR = 0x41, OP_WIN_PRINT_INT = 0x42, OP_WIN_PRINT_CHAR = 0x43,
    OP_WIN_REFRESH = 0x44, OP_WIN_CLEAR = 0x45, OP_WIN_COLOR = 0x46, OP_WIN_PIXEL = 0x47,
    OP_WIN_RECT = 0x48, OP_WIN_LINE = 0x49, OP_WIN_TEXT = 0x4A, OP_WIN_CLOSE = 0x4B, OP_WIN_IMAGE = 0x4C,
    OP_HALT = 0xFF
};

enum { BLOCK_IF = 1, BLOCK_ELSE = 2, BLOCK_WHILE = 3, BLOCK_FUNC = 4 };
enum { KIND_NUMBER = 0, KIND_TEXT = 1 };

#define HEADER_SIZE 32
#define CODE_MAX 2048
#define STR_DATA_MAX 1024
#define VAR_MAX 64
#define STR_MAX 64
#define LABEL_MAX 32
#define FUNC_MAX 16
#define PATCH_MAX 96
#define BLOCK_MAX 16
#define NAME_LEN 40
#define LINE_MAX 200
#define STACK_MAX 256
#define CALL_MAX 64
#define STEP_LIMIT 200000000L
#define IMG_MAX 6
#define IMG_STRIDE 48
#define IMG_PIXELS 2304

typedef struct {
    int kind;
    int jz_pos;
    int jmp_pos;
    int start;
} Block;

static uint8_t code[CODE_MAX];
static int code_len;
static uint8_t str_data[STR_DATA_MAX];
static int str_data_len;
static int str_start[STR_MAX];
static int str_size[STR_MAX];
static int str_count;
static char var_name[VAR_MAX][NAME_LEN];
static uint8_t var_kind[VAR_MAX];
static int var_count;
static char label_name[LABEL_MAX][NAME_LEN];
static int label_addr[LABEL_MAX];
static int label_count;
static char func_name[FUNC_MAX][NAME_LEN];
static int func_arity[FUNC_MAX];
static int func_addr[FUNC_MAX];
static int func_count;
static int patch_pos[PATCH_MAX];
static char patch_name[PATCH_MAX][NAME_LEN];
static int patch_argc[PATCH_MAX];
static int patch_call[PATCH_MAX];
static int patch_line[PATCH_MAX];
static int patch_count;
static Block blocks[BLOCK_MAX];
static int block_count;
static int in_header;
static int cur_func;
static int header_flags;
static CdeImageLoader image_loader;
static char image_name[IMG_MAX][NAME_LEN];
static char image_file[IMG_MAX][40];
static uint8_t cimg_embedded[IMG_MAX];
static int cimg_name[IMG_MAX];
static int cimg_w[IMG_MAX];
static int cimg_h[IMG_MAX];
static uint8_t cimg_pixels[IMG_MAX][IMG_PIXELS];
static int image_count;
static uint8_t run_img[IMG_MAX][IMG_PIXELS];
static int run_img_w[IMG_MAX];
static int run_img_h[IMG_MAX];
static int run_img_count;

void cde_set_image_loader(CdeImageLoader loader) {
    image_loader = loader;
}

static const char* cur;
static int cur_line;
static int failed;
static char* error_out;
static int error_cap;

static const uint32_t run_palette[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF
};

static void put_text(char* out, int* n, const char* text, int cap) {
    for (int i = 0; text[i] && *n < cap - 1; i++) out[(*n)++] = text[i];
    out[*n] = '\0';
}

static void put_number(char* out, int* n, int value, int cap) {
    char digits[16];
    int count = 0;
    unsigned int v = value < 0 ? (unsigned int)(-value) : (unsigned int)value;
    if (v == 0) digits[count++] = '0';
    while (v > 0 && count < 15) {
        digits[count++] = (char)('0' + v % 10);
        v /= 10;
    }
    if (value < 0 && *n < cap - 1) out[(*n)++] = '-';
    while (count > 0 && *n < cap - 1) out[(*n)++] = digits[--count];
    out[*n] = '\0';
}

static void fail_at(int line, const char* message, const char* detail) {
    if (failed) return;
    failed = 1;
    if (error_out == 0 || error_cap <= 0) return;
    int n = 0;
    error_out[0] = '\0';
    put_text(error_out, &n, "LINE ", error_cap);
    put_number(error_out, &n, line, error_cap);
    put_text(error_out, &n, ": ", error_cap);
    put_text(error_out, &n, message, error_cap);
    if (detail) {
        put_text(error_out, &n, " ", error_cap);
        put_text(error_out, &n, detail, error_cap);
    }
}

static void fail(const char* message) {
    fail_at(cur_line, message, 0);
}

static int same(const char* a, const char* b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static void copy_name(char* destination, const char* source) {
    int n = 0;
    while (source[n] && n < NAME_LEN - 1) {
        destination[n] = source[n];
        n++;
    }
    destination[n] = '\0';
}

static void emit(int value) {
    if (code_len >= CODE_MAX) {
        fail("PROGRAM TOO LARGE");
        return;
    }
    code[code_len++] = (uint8_t)value;
}

static void emit32(int32_t value) {
    emit(value & 255);
    emit((value >> 8) & 255);
    emit((value >> 16) & 255);
    emit((value >> 24) & 255);
}

static void patch32(int pos, int32_t value) {
    if (pos < 0 || pos + 4 > code_len) return;
    code[pos] = (uint8_t)(value & 255);
    code[pos + 1] = (uint8_t)((value >> 8) & 255);
    code[pos + 2] = (uint8_t)((value >> 16) & 255);
    code[pos + 3] = (uint8_t)((value >> 24) & 255);
}

static void skip_space(void) {
    while (*cur == ' ' || *cur == '\t') cur++;
}

static int is_digit(char c) { return c >= '0' && c <= '9'; }
static int is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static int is_alnum(char c) { return is_alpha(c) || is_digit(c); }

static int read_ident(char* out) {
    skip_space();
    if (!is_alpha(*cur)) return 0;
    int n = 0;
    while (is_alnum(*cur)) {
        if (n < NAME_LEN - 1) out[n++] = *cur;
        cur++;
    }
    out[n] = '\0';
    return 1;
}

static void scoped_name(char* out, const char* name) {
    int n = 0;
    const char* function = func_name[cur_func];
    for (int i = 0; function[i] && n < NAME_LEN - 2; i++) out[n++] = function[i];
    out[n++] = '.';
    for (int i = 0; name[i] && n < NAME_LEN - 1; i++) out[n++] = name[i];
    out[n] = '\0';
}

static int lookup(const char* name) {
    for (int i = 0; i < var_count; i++) {
        if (same(var_name[i], name)) return i;
    }
    return -1;
}

static int find_var(const char* name) {
    if (cur_func >= 0) {
        char scoped[NAME_LEN];
        scoped_name(scoped, name);
        const int index = lookup(scoped);
        if (index >= 0) return index;
    }
    return lookup(name);
}

static int declare_var(const char* name, int kind) {
    char scoped[NAME_LEN];
    const char* actual = name;
    if (cur_func >= 0) {
        scoped_name(scoped, name);
        actual = scoped;
    }
    const int existing = lookup(actual);
    if (existing >= 0) {
        var_kind[existing] = (uint8_t)kind;
        return existing;
    }
    if (var_count >= VAR_MAX) {
        fail("TOO MANY VARIABLES");
        return 0;
    }
    copy_name(var_name[var_count], actual);
    var_kind[var_count] = (uint8_t)kind;
    return var_count++;
}

static char escape_char(char c) {
    if (c == 'n') return '\n';
    if (c == 't') return '\t';
    if (c == 'r') return '\r';
    if (c == '0') return '\0';
    return c;
}

static int find_func(const char* name) {
    for (int i = 0; i < func_count; i++) {
        if (same(func_name[i], name)) return i;
    }
    return -1;
}

static void parse_cmp(void);

static void emit_call(const char* name, int argc) {
    if (patch_count >= PATCH_MAX) {
        fail("TOO MANY JUMPS");
        return;
    }
    emit(OP_CALL);
    patch_pos[patch_count] = code_len;
    copy_name(patch_name[patch_count], name);
    patch_argc[patch_count] = argc;
    patch_call[patch_count] = 1;
    patch_line[patch_count] = cur_line;
    patch_count++;
    emit32(0);
}

static void compile_call(const char* name) {
    int argc = 0;
    cur++;
    skip_space();
    while (*cur != ')' && !failed) {
        parse_cmp();
        argc++;
        skip_space();
        if (*cur == ',') cur++;
        else break;
        skip_space();
    }
    if (*cur == ')') cur++;
    else fail("MISSING )");
    emit_call(name, argc);
}

static void parse_primary(void) {
    skip_space();
    if (*cur == '(') {
        cur++;
        parse_cmp();
        skip_space();
        if (*cur == ')') cur++;
        else fail("MISSING )");
        return;
    }
    if (is_digit(*cur)) {
        int32_t value = 0;
        if (cur[0] == '0' && (cur[1] == 'x' || cur[1] == 'X')) {
            cur += 2;
            int digits = 0;
            while (1) {
                char c = *cur;
                int d;
                if (c >= '0' && c <= '9') d = c - '0';
                else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
                else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
                else break;
                value = (int32_t)(((uint32_t)value << 4) | (uint32_t)d);
                digits++;
                cur++;
            }
            if (digits == 0) fail("BAD HEX NUMBER");
        } else {
            while (is_digit(*cur)) {
                value = value * 10 + (*cur - '0');
                cur++;
            }
        }
        emit(OP_PUSH);
        emit32(value);
        return;
    }
    if (*cur == '\'') {
        cur++;
        char c = *cur;
        if (c == '\\') {
            cur++;
            c = escape_char(*cur);
        }
        if (*cur) cur++;
        if (*cur == '\'') cur++;
        else fail("BAD CHARACTER");
        emit(OP_PUSH);
        emit32((int32_t)c);
        return;
    }
    if (is_alpha(*cur)) {
        char name[NAME_LEN];
        read_ident(name);
        skip_space();
        if (*cur == '(') {
            if (same(name, "window")) {
                fail("window() ONLY AFTER =");
                return;
            }
            compile_call(name);
            return;
        }
        const int index = find_var(name);
        if (index < 0) {
            fail_at(cur_line, "UNKNOWN VARIABLE", name);
            return;
        }
        if (var_kind[index] == KIND_TEXT) {
            fail("TEXT VARIABLE USED AS NUMBER");
            return;
        }
        emit(OP_GET_VAR);
        emit(index);
        return;
    }
    fail("BAD EXPRESSION");
}

static void parse_unary(void) {
    skip_space();
    if (*cur == '-') {
        cur++;
        emit(OP_PUSH);
        emit32(0);
        parse_unary();
        emit(OP_SUB);
        return;
    }
    parse_primary();
}

static void parse_mul(void) {
    parse_unary();
    while (!failed) {
        skip_space();
        const char c = *cur;
        if (c != '*' && c != '/' && c != '%') break;
        cur++;
        parse_unary();
        emit(c == '*' ? OP_MUL : (c == '/' ? OP_DIV : OP_MOD));
    }
}

static void parse_add(void) {
    parse_mul();
    while (!failed) {
        skip_space();
        const char c = *cur;
        if (c != '+' && c != '-') break;
        cur++;
        parse_mul();
        emit(c == '+' ? OP_ADD : OP_SUB);
    }
}

static void parse_cmp(void) {
    parse_add();
    while (!failed) {
        skip_space();
        int op = -1;
        int width = 0;
        if (cur[0] == '=' && cur[1] == '=') { op = OP_EQ; width = 2; }
        else if (cur[0] == '!' && cur[1] == '=') { op = OP_NE; width = 2; }
        else if (cur[0] == '<' && cur[1] == '=') { op = OP_LE; width = 2; }
        else if (cur[0] == '>' && cur[1] == '=') { op = OP_GE; width = 2; }
        else if (cur[0] == '<') { op = OP_LT; width = 1; }
        else if (cur[0] == '>') { op = OP_GT; width = 1; }
        if (op < 0) break;
        cur += width;
        parse_add();
        emit(op);
    }
}

static void expect_end(void) {
    skip_space();
    if (*cur != '\0') fail("UNEXPECTED TEXT");
}

static void expect_char(char c, const char* message) {
    skip_space();
    if (*cur == c) cur++;
    else fail(message);
}

static int parse_string_literal(void) {
    cur++;
    const int start = str_data_len;
    int n = 0;
    while (*cur && *cur != '"') {
        char c = *cur++;
        if (c == '\\' && *cur) c = escape_char(*cur++);
        if (str_data_len >= STR_DATA_MAX) {
            fail("TEXT TOO LARGE");
            return -1;
        }
        str_data[str_data_len++] = (uint8_t)c;
        n++;
    }
    if (*cur != '"') {
        fail("MISSING QUOTE");
        return -1;
    }
    cur++;
    if (str_count >= STR_MAX) {
        fail("TOO MANY TEXTS");
        return -1;
    }
    str_start[str_count] = start;
    str_size[str_count] = n;
    return str_count++;
}

static int add_literal(const char* text) {
    if (str_count >= STR_MAX) {
        fail("TOO MANY TEXTS");
        return -1;
    }
    const int start = str_data_len;
    int n = 0;
    while (text[n] && str_data_len < STR_DATA_MAX) {
        str_data[str_data_len++] = (uint8_t)text[n++];
    }
    str_start[str_count] = start;
    str_size[str_count] = n;
    return str_count++;
}

static void compile_text_value(void) {
    skip_space();
    if (*cur == '"') {
        const int id = parse_string_literal();
        if (id < 0) return;
        emit(OP_PUSH);
        emit32(id);
        return;
    }
    char name[NAME_LEN];
    if (!read_ident(name)) {
        fail("TEXT EXPECTED");
        return;
    }
    const int index = find_var(name);
    if (index >= 0 && var_kind[index] == KIND_TEXT) {
        emit(OP_GET_VAR);
        emit(index);
        return;
    }
    const int id = add_literal(name);
    if (id < 0) return;
    emit(OP_PUSH);
    emit32(id);
}

static void compile_print_items(char* rest, int window_var) {
    int length = 0;
    while (rest[length]) length++;
    while (length > 0 && (rest[length - 1] == ' ' || rest[length - 1] == '\t')) rest[--length] = '\0';
    int newline = 0;
    if (length >= 2 && rest[length - 1] == 'n' && rest[length - 2] == '-' &&
        (length == 2 || rest[length - 3] == ' ' || rest[length - 3] == '\t' || rest[length - 3] == '"')) {
        newline = 1;
        length -= 2;
        rest[length] = '\0';
        while (length > 0 && (rest[length - 1] == ' ' || rest[length - 1] == '\t')) rest[--length] = '\0';
    }
    cur = rest;
    int count = 0;
    while (!failed) {
        skip_space();
        if (*cur == '\0') break;
        if (window_var >= 0) {
            emit(OP_GET_VAR);
            emit(window_var);
        }
        if (*cur == '"') {
            const int id = parse_string_literal();
            if (id < 0) return;
            emit(OP_PUSH);
            emit32(id);
            emit(window_var >= 0 ? OP_WIN_PRINT_STR : OP_PRINT_STR);
        } else {
            const char* mark = cur;
            char name[NAME_LEN];
            int handled = 0;
            if (read_ident(name)) {
                const int index = find_var(name);
                skip_space();
                if (index >= 0 && var_kind[index] == KIND_TEXT && (*cur == ',' || *cur == '\0')) {
                    emit(OP_GET_VAR);
                    emit(index);
                    emit(window_var >= 0 ? OP_WIN_PRINT_STR : OP_PRINT_STR);
                    handled = 1;
                }
            }
            if (!handled) {
                cur = mark;
                parse_cmp();
                emit(window_var >= 0 ? OP_WIN_PRINT_INT : OP_PRINT);
            }
        }
        count++;
        skip_space();
        if (*cur == ',') {
            cur++;
            continue;
        }
        break;
    }
    expect_end();
    if (count == 0 && !newline) fail("NOTHING TO PRINT");
    if (newline) {
        if (window_var >= 0) {
            emit(OP_GET_VAR);
            emit(window_var);
        }
        emit(OP_PUSH);
        emit32(10);
        emit(window_var >= 0 ? OP_WIN_PRINT_CHAR : OP_PRINT_CHAR);
    }
}

static void define_label(const char* name) {
    for (int i = 0; i < label_count; i++) {
        if (same(label_name[i], name)) {
            fail("LABEL DEFINED TWICE");
            return;
        }
    }
    if (label_count >= LABEL_MAX) {
        fail("TOO MANY LABELS");
        return;
    }
    copy_name(label_name[label_count], name);
    label_addr[label_count] = code_len;
    label_count++;
}

static void emit_jump(const char* label) {
    if (patch_count >= PATCH_MAX) {
        fail("TOO MANY JUMPS");
        return;
    }
    emit(OP_JMP);
    patch_pos[patch_count] = code_len;
    copy_name(patch_name[patch_count], label);
    patch_argc[patch_count] = 0;
    patch_call[patch_count] = 0;
    patch_line[patch_count] = cur_line;
    patch_count++;
    emit32(0);
}

static int try_window_create(void) {
    const char* mark = cur;
    char word[NAME_LEN];
    if (read_ident(word) && same(word, "window")) {
        skip_space();
        if (*cur == '(') {
            cur++;
            for (int i = 0; i < 4 && !failed; i++) {
                parse_cmp();
                if (i < 3) expect_char(',', "MISSING COMMA");
                else {
                    skip_space();
                    if (*cur == ',') cur++;
                }
            }
            skip_space();
            if (*cur == ')') {
                const int empty = add_literal("");
                if (empty < 0) return 1;
                emit(OP_PUSH);
                emit32(empty);
            } else {
                compile_text_value();
            }
            expect_char(')', "MISSING )");
            expect_end();
            emit(OP_WIN_CREATE);
            return 1;
        }
    }
    cur = mark;
    return 0;
}

static void compile_args(int count) {
    expect_char('(', "MISSING (");
    for (int i = 0; i < count && !failed; i++) {
        if (i > 0) expect_char(',', "MISSING COMMA");
        parse_cmp();
    }
    expect_char(')', "MISSING )");
    expect_end();
}

static int same_nocase(const char* a, const char* b) {
    while (*a && *b) {
        char x = *a;
        char y = *b;
        if (x >= 'a' && x <= 'z') x = (char)(x - 32);
        if (y >= 'a' && y <= 'z') y = (char)(y - 32);
        if (x != y) return 0;
        a++;
        b++;
    }
    return *a == *b;
}

static int find_image_file(const char* file) {
    for (int i = 0; i < image_count; i++) {
        if (same_nocase(image_file[i], file)) return i;
    }
    return -1;
}

static int add_image(const char* alias, const char* file, int external) {
    if (image_count >= IMG_MAX) {
        fail("TOO MANY IMAGES");
        return -1;
    }
    if (alias[0]) {
        for (int i = 0; i < image_count; i++) {
            if (same(image_name[i], alias)) {
                fail("IMAGE DEFINED TWICE");
                return -1;
            }
        }
    }
    const int id = add_literal(file);
    if (id < 0) return -1;
    const int index = image_count++;
    copy_name(image_name[index], alias);
    int n = 0;
    while (file[n] && n < 39) {
        image_file[index][n] = file[n];
        n++;
    }
    image_file[index][n] = '\0';
    cimg_embedded[index] = (uint8_t)(external ? 0 : 1);
    cimg_name[index] = id;
    cimg_w[index] = 0;
    cimg_h[index] = 0;
    if (!external) {
        if (!image_loader) {
            fail("NO IMAGE LOADER");
            return -1;
        }
        if (!image_loader(file, cimg_pixels[index], &cimg_w[index], &cimg_h[index])) {
            fail_at(cur_line, "CANNOT LOAD IMAGE", file);
            return -1;
        }
    }
    return index;
}

static int read_file_spec(char* out) {
    skip_space();
    int n = 0;
    if (*cur == '"') {
        cur++;
        while (*cur && *cur != '"' && n < 39) out[n++] = *cur++;
        if (*cur == '"') cur++;
        out[n] = '\0';
        return 2;
    }
    if (!is_alpha(*cur) && !is_digit(*cur)) return 0;
    while ((is_alnum(*cur) || *cur == '-') && n < 39) out[n++] = *cur++;
    int dotted = 0;
    if (*cur == '.') {
        dotted = 1;
        out[n++] = *cur++;
        while ((is_alnum(*cur) || *cur == '-') && n < 39) out[n++] = *cur++;
    }
    out[n] = '\0';
    return dotted ? 2 : 1;
}

static void compile_window_method(int window_var, char* line) {
    char method[NAME_LEN];
    cur++;
    if (!read_ident(method)) {
        fail("METHOD NAME EXPECTED");
        return;
    }
    if (same(method, "print")) {
        compile_print_items(line + (cur - line), window_var);
        return;
    }
    emit(OP_GET_VAR);
    emit(window_var);
    if (same(method, "refresh") || same(method, "clear") || same(method, "close")) {
        skip_space();
        if (*cur == '(') {
            cur++;
            expect_char(')', "MISSING )");
        }
        expect_end();
        emit(same(method, "refresh") ? OP_WIN_REFRESH : (same(method, "clear") ? OP_WIN_CLEAR : OP_WIN_CLOSE));
    } else if (same(method, "color")) {
        compile_args(1);
        emit(OP_WIN_COLOR);
    } else if (same(method, "pixel")) {
        compile_args(2);
        emit(OP_WIN_PIXEL);
    } else if (same(method, "rect")) {
        compile_args(4);
        emit(OP_WIN_RECT);
    } else if (same(method, "line")) {
        compile_args(4);
        emit(OP_WIN_LINE);
    } else if (same(method, "image")) {
        char spec[40];
        expect_char('(', "MISSING (");
        parse_cmp();
        expect_char(',', "MISSING COMMA");
        parse_cmp();
        expect_char(',', "MISSING COMMA");
        const int kind = read_file_spec(spec);
        if (kind == 0) {
            fail("IMAGE NAME EXPECTED");
            return;
        }
        int found = -1;
        if (kind == 2) {
            found = find_image_file(spec);
            if (found < 0) found = add_image("", spec, 0);
            if (found < 0) return;
        } else {
            for (int i = 0; i < image_count; i++) {
                if (same(image_name[i], spec)) found = i;
            }
            if (found < 0) {
                fail_at(cur_line, "UNKNOWN IMAGE", spec);
                return;
            }
        }
        emit(OP_PUSH);
        emit32(found);
        expect_char(')', "MISSING )");
        expect_end();
        emit(OP_WIN_IMAGE);
    } else if (same(method, "text")) {
        expect_char('(', "MISSING (");
        parse_cmp();
        expect_char(',', "MISSING COMMA");
        parse_cmp();
        expect_char(',', "MISSING COMMA");
        compile_text_value();
        expect_char(')', "MISSING )");
        expect_end();
        emit(OP_WIN_TEXT);
    } else {
        fail_at(cur_line, "UNKNOWN METHOD", method);
    }
}

static int is_keyword(const char* word) {
    static const char* const keywords[] = {
        "var", "str", "print", "if", "else", "end", "while", "goto", "halt", "func", "return", "key", "mainloop", "image"
    };
    for (int i = 0; i < (int)(sizeof(keywords) / sizeof(keywords[0])); i++) {
        if (same(word, keywords[i])) return 1;
    }
    return 0;
}

static void compile_header_line(char* line) {
    cur = line;
    char word[NAME_LEN];
    if (!read_ident(word) || !same(word, "var")) return;
    char name[NAME_LEN];
    if (!read_ident(name)) return;
    skip_space();
    if (*cur != '=') return;
    cur++;
    skip_space();
    uint32_t value = 0;
    int digits = 0;
    if (cur[0] == '0' && (cur[1] == 'x' || cur[1] == 'X')) {
        cur += 2;
        while (1) {
            char c = *cur;
            int d;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else break;
            value = (value << 4) | (uint32_t)d;
            digits++;
            cur++;
        }
    } else {
        while (is_digit(*cur)) {
            value = value * 10 + (uint32_t)(*cur - '0');
            digits++;
            cur++;
        }
    }
    if (digits > 0 && same(name, "mode") && value == CDE_MAGIC_VALUE) header_flags |= 1;
}

static void compile_assignment_text(const char* name) {
    skip_space();
    int index = find_var(name);
    if (index < 0) index = declare_var(name, KIND_TEXT);
    else if (var_kind[index] != KIND_TEXT) index = declare_var(name, KIND_TEXT);
    compile_text_value();
    expect_end();
    emit(OP_SET_VAR);
    emit(index);
}

static void compile_assignment(const char* name, int declare) {
    skip_space();
    int index = find_var(name);
    const char* mark = cur;
    if (try_window_create()) {
        if (index < 0 || declare) index = declare_var(name, KIND_NUMBER);
        emit(OP_SET_VAR);
        emit(index);
        return;
    }
    cur = mark;
    skip_space();
    if (*cur == '"') {
        if (index < 0 || declare) index = declare_var(name, KIND_TEXT);
        else if (var_kind[index] != KIND_TEXT) {
            fail("NUMBER VARIABLE GETS TEXT");
            return;
        }
        const int id = parse_string_literal();
        if (id < 0) return;
        emit(OP_PUSH);
        emit32(id);
    } else {
        if (index < 0) {
            if (!declare) {
                fail_at(cur_line, "UNKNOWN VARIABLE (USE var FIRST)", name);
                return;
            }
        }
        if (index >= 0 && var_kind[index] == KIND_TEXT && !declare) {
            fail("TEXT VARIABLE GETS NUMBER");
            return;
        }
        parse_cmp();
        if (index < 0 || declare) index = declare_var(name, KIND_NUMBER);
    }
    expect_end();
    emit(OP_SET_VAR);
    emit(index);
}

static void compile_statement(char* line) {
    cur = line;
    skip_space();
    if (*cur == '\0') return;
    if (*cur == '.') {
        char word[NAME_LEN];
        cur++;
        if (read_ident(word)) {
            if (same(word, "header")) in_header = 1;
            else if (same(word, "section")) in_header = 0;
        }
        return;
    }
    if (in_header) {
        compile_header_line(line);
        return;
    }

    char word[NAME_LEN];
    if (!read_ident(word)) {
        fail("SYNTAX ERROR");
        return;
    }
    skip_space();
    if (*cur == ':') {
        cur++;
        define_label(word);
        expect_end();
        return;
    }

    if (!is_keyword(word)) {
        if (*cur == '.') {
            const int index = find_var(word);
            if (index < 0) {
                fail_at(cur_line, "UNKNOWN VARIABLE", word);
                return;
            }
            compile_window_method(index, line);
        } else if (*cur == '=' && cur[1] != '=') {
            cur++;
            compile_assignment(word, 0);
        } else if (*cur == '(') {
            compile_call(word);
            expect_end();
            emit(OP_POP);
        } else {
            fail_at(cur_line, "UNKNOWN STATEMENT", word);
        }
        return;
    }

    if (same(word, "str")) {
        char name[NAME_LEN];
        if (!read_ident(name)) {
            fail("TEXT VARIABLE NAME EXPECTED");
            return;
        }
        skip_space();
        if (*cur == '=') {
            cur++;
            compile_assignment_text(name);
        } else {
            const int index = declare_var(name, KIND_TEXT);
            const int empty = add_literal("");
            if (empty < 0) return;
            emit(OP_PUSH);
            emit32(empty);
            emit(OP_SET_VAR);
            emit(index);
            expect_end();
        }
    } else if (same(word, "var")) {
        char name[NAME_LEN];
        if (!read_ident(name)) {
            fail("VARIABLE NAME EXPECTED");
            return;
        }
        skip_space();
        if (*cur == '=') {
            cur++;
            compile_assignment(name, 1);
        } else {
            const int index = declare_var(name, KIND_NUMBER);
            emit(OP_PUSH);
            emit32(0);
            emit(OP_SET_VAR);
            emit(index);
            expect_end();
        }
    } else if (same(word, "print")) {
        compile_print_items(line + (cur - line), -1);
    } else if (same(word, "if") || same(word, "while")) {
        if (block_count >= BLOCK_MAX) {
            fail("BLOCKS NESTED TOO DEEP");
            return;
        }
        const int start = code_len;
        parse_cmp();
        expect_end();
        emit(OP_JZ);
        blocks[block_count].kind = same(word, "while") ? BLOCK_WHILE : BLOCK_IF;
        blocks[block_count].jz_pos = code_len;
        blocks[block_count].jmp_pos = -1;
        blocks[block_count].start = start;
        block_count++;
        emit32(0);
    } else if (same(word, "else")) {
        expect_end();
        if (block_count == 0 || blocks[block_count - 1].kind != BLOCK_IF) {
            fail("else WITHOUT if");
            return;
        }
        Block* block = &blocks[block_count - 1];
        emit(OP_JMP);
        block->jmp_pos = code_len;
        emit32(0);
        patch32(block->jz_pos, code_len);
        block->kind = BLOCK_ELSE;
    } else if (same(word, "end")) {
        expect_end();
        if (block_count == 0) {
            fail("end WITHOUT BLOCK");
            return;
        }
        Block* block = &blocks[--block_count];
        if (block->kind == BLOCK_WHILE) {
            emit(OP_JMP);
            emit32(block->start);
            patch32(block->jz_pos, code_len);
        } else if (block->kind == BLOCK_IF) {
            patch32(block->jz_pos, code_len);
        } else if (block->kind == BLOCK_ELSE) {
            patch32(block->jmp_pos, code_len);
        } else {
            emit(OP_PUSH);
            emit32(0);
            emit(OP_RET);
            patch32(block->jmp_pos, code_len);
            cur_func = -1;
        }
    } else if (same(word, "func")) {
        char name[NAME_LEN];
        if (cur_func >= 0) {
            fail("FUNCTIONS CANNOT BE NESTED");
            return;
        }
        if (!read_ident(name)) {
            fail("FUNCTION NAME EXPECTED");
            return;
        }
        if (find_func(name) >= 0 || func_count >= FUNC_MAX || block_count >= BLOCK_MAX) {
            fail("FUNCTION ALREADY DEFINED OR TOO MANY");
            return;
        }
        expect_char('(', "MISSING (");
        const int function = func_count++;
        copy_name(func_name[function], name);
        func_arity[function] = 0;
        emit(OP_JMP);
        blocks[block_count].kind = BLOCK_FUNC;
        blocks[block_count].jz_pos = -1;
        blocks[block_count].jmp_pos = code_len;
        blocks[block_count].start = code_len;
        block_count++;
        emit32(0);
        func_addr[function] = code_len;
        cur_func = function;
        int params[16];
        int param_count = 0;
        skip_space();
        while (*cur != ')' && *cur != '\0' && !failed && param_count < 16) {
            char parameter[NAME_LEN];
            if (!read_ident(parameter)) {
                fail("PARAMETER NAME EXPECTED");
                return;
            }
            params[param_count++] = declare_var(parameter, KIND_NUMBER);
            skip_space();
            if (*cur == ',') cur++;
            skip_space();
        }
        expect_char(')', "MISSING )");
        expect_end();
        func_arity[function] = param_count;
        for (int i = param_count - 1; i >= 0; i--) {
            emit(OP_SET_VAR);
            emit(params[i]);
        }
    } else if (same(word, "return")) {
        if (cur_func < 0) {
            fail("return OUTSIDE FUNCTION");
            return;
        }
        skip_space();
        if (*cur == '\0') {
            emit(OP_PUSH);
            emit32(0);
        } else {
            parse_cmp();
            expect_end();
        }
        emit(OP_RET);
    } else if (same(word, "image")) {
        char name[NAME_LEN];
        char spec[40];
        char option[NAME_LEN];
        if (!read_ident(name)) {
            fail("IMAGE NAME EXPECTED");
            return;
        }
        expect_char('=', "MISSING =");
        if (read_file_spec(spec) != 2) {
            fail("FILE NAME EXPECTED");
            return;
        }
        int external = 0;
        if (read_ident(option)) {
            if (same(option, "external")) external = 1;
            else {
                fail("UNKNOWN IMAGE OPTION");
                return;
            }
        }
        expect_end();
        add_image(name, spec, external);
    } else if (same(word, "goto")) {
        char label[NAME_LEN];
        if (!read_ident(label)) {
            fail("LABEL EXPECTED");
            return;
        }
        expect_end();
        emit_jump(label);
    } else if (same(word, "halt")) {
        expect_end();
        emit(OP_HALT);
    } else if (same(word, "mainloop")) {
        expect_end();
        emit(OP_MAINLOOP);
    } else if (same(word, "key")) {
        char name[NAME_LEN];
        if (!read_ident(name)) {
            fail("VARIABLE NAME EXPECTED");
            return;
        }
        int index = find_var(name);
        if (index < 0) index = declare_var(name, KIND_NUMBER);
        expect_end();
        emit(OP_KEY);
        emit(OP_SET_VAR);
        emit(index);
    }
}

static void strip_comment(char* line) {
    int in_string = 0;
    for (int i = 0; line[i]; i++) {
        if (line[i] == '"') in_string = !in_string;
        if (!in_string && line[i] == '/' && line[i + 1] == '/') {
            line[i] = '\0';
            break;
        }
    }
}

static void write32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

int cde_compile(const char* source, int length, uint8_t* out, int capacity, char* error, int error_size) {
    code_len = 0;
    str_data_len = 0;
    str_count = 0;
    var_count = 0;
    label_count = 0;
    func_count = 0;
    patch_count = 0;
    block_count = 0;
    in_header = 0;
    cur_func = -1;
    header_flags = 0;
    image_count = 0;
    failed = 0;
    cur_line = 0;
    error_out = error;
    error_cap = error_size;
    if (error && error_size > 0) error[0] = '\0';

    int position = 0;
    while (position <= length && !failed) {
        char line[LINE_MAX];
        int n = 0;
        while (position < length && source[position] != '\n') {
            if (source[position] != '\r' && n < LINE_MAX - 1) line[n++] = source[position];
            position++;
        }
        position++;
        line[n] = '\0';
        cur_line++;
        strip_comment(line);
        n = 0;
        while (line[n]) n++;
        while (n > 0 && (line[n - 1] == ' ' || line[n - 1] == '\t')) line[--n] = '\0';
        compile_statement(line);
    }
    if (!failed && block_count != 0) fail("MISSING end");
    if (!failed) emit(OP_HALT);

    for (int i = 0; i < patch_count && !failed; i++) {
        if (patch_call[i]) {
            const int function = find_func(patch_name[i]);
            if (function < 0) fail_at(patch_line[i], "UNKNOWN FUNCTION", patch_name[i]);
            else if (func_arity[function] != patch_argc[i]) fail_at(patch_line[i], "WRONG ARGUMENT COUNT FOR", patch_name[i]);
            else patch32(patch_pos[i], func_addr[function]);
        } else {
            int target = -1;
            for (int j = 0; j < label_count; j++) {
                if (same(label_name[j], patch_name[i])) target = label_addr[j];
            }
            if (target < 0) fail_at(patch_line[i], "UNKNOWN LABEL", patch_name[i]);
            else patch32(patch_pos[i], target);
        }
    }
    if (failed) return 0;

    int data_size = 5;
    for (int i = 0; i < str_count; i++) data_size += 4 + str_size[i];
    data_size += 4;
    for (int i = 0; i < image_count; i++) {
        data_size += 10;
        if (cimg_embedded[i]) data_size += (cimg_w[i] * cimg_h[i] + 1) / 2;
    }
    const int total = HEADER_SIZE + code_len + data_size;
    if (total > capacity) {
        fail_at(0, "PROGRAM TOO LARGE FOR OUTPUT", 0);
        return 0;
    }

    for (int i = 0; i < total; i++) out[i] = 0;
    write32(out, CDE_MAGIC_VALUE);
    out[4] = 1;
    out[5] = (uint8_t)header_flags;
    out[6] = HEADER_SIZE;
    out[7] = 0;
    write32(out + 8, HEADER_SIZE);
    write32(out + 12, (uint32_t)code_len);
    write32(out + 16, (uint32_t)(HEADER_SIZE + code_len));
    write32(out + 20, (uint32_t)data_size);
    write32(out + 24, 0);
    write32(out + 28, (uint32_t)code_len);
    for (int i = 0; i < code_len; i++) out[HEADER_SIZE + i] = code[i];
    uint8_t* data = out + HEADER_SIZE + code_len;
    data[0] = 0xFF;
    write32(data + 1, (uint32_t)str_count);
    int offset = 5;
    for (int i = 0; i < str_count; i++) {
        write32(data + offset, (uint32_t)str_size[i]);
        offset += 4;
        for (int j = 0; j < str_size[i]; j++) data[offset++] = str_data[str_start[i] + j];
    }
    write32(data + offset, (uint32_t)image_count);
    offset += 4;
    for (int i = 0; i < image_count; i++) {
        data[offset++] = cimg_embedded[i];
        data[offset++] = 0;
        data[offset++] = (uint8_t)cimg_w[i];
        data[offset++] = (uint8_t)(cimg_w[i] >> 8);
        data[offset++] = (uint8_t)cimg_h[i];
        data[offset++] = (uint8_t)(cimg_h[i] >> 8);
        write32(data + offset, (uint32_t)cimg_name[i]);
        offset += 4;
        if (cimg_embedded[i]) {
            const int pixels = cimg_w[i] * cimg_h[i];
            for (int p = 0; p < pixels; p += 2) {
                const int first = cimg_pixels[i][(p / cimg_w[i]) * IMG_STRIDE + (p % cimg_w[i])] & 15;
                int second = 0;
                if (p + 1 < pixels) second = cimg_pixels[i][((p + 1) / cimg_w[i]) * IMG_STRIDE + ((p + 1) % cimg_w[i])] & 15;
                data[offset++] = (uint8_t)((first << 4) | second);
            }
        }
    }
    return total;
}

static uint32_t read32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int cde_is_autostart(const uint8_t* header, int length) {
    if (header == 0 || length < HEADER_SIZE) return 0;
    return read32(header) == CDE_MAGIC_VALUE && header[4] == 1 && (header[5] & 1);
}

static void run_fail(char* error, int error_size, const char* message) {
    if (error == 0 || error_size <= 0) return;
    int n = 0;
    error[0] = '\0';
    put_text(error, &n, message, error_size);
}

static void win_print_int(const CdeHost* host, int id, int32_t value) {
    if (!host->win_char) return;
    char digits[16];
    int count = 0;
    uint32_t v = value < 0 ? (uint32_t)(-(int64_t)value) : (uint32_t)value;
    if (v == 0) digits[count++] = '0';
    while (v > 0 && count < 15) {
        digits[count++] = (char)('0' + v % 10);
        v /= 10;
    }
    if (value < 0) host->win_char(id, '-');
    while (count > 0) host->win_char(id, digits[--count]);
}

static void console_print_int(const CdeHost* host, int32_t value) {
    if (!host->put_char) return;
    char digits[16];
    int count = 0;
    uint32_t v = value < 0 ? (uint32_t)(-(int64_t)value) : (uint32_t)value;
    if (v == 0) digits[count++] = '0';
    while (v > 0 && count < 15) {
        digits[count++] = (char)('0' + v % 10);
        v /= 10;
    }
    if (value < 0) host->put_char('-');
    while (count > 0) host->put_char(digits[--count]);
}

static int run_inner(const uint8_t* image, int length, const CdeHost* host, char* error, int error_size) {
    if (error && error_size > 0) error[0] = '\0';
    if (image == 0 || host == 0 || length < HEADER_SIZE) {
        run_fail(error, error_size, "NOT A CDE FILE");
        return CDE_RUN_ERROR;
    }
    if (read32(image) != CDE_MAGIC_VALUE || image[4] != 1) {
        run_fail(error, error_size, "BAD CDE HEADER");
        return CDE_RUN_ERROR;
    }
    const uint32_t code_offset = read32(image + 8);
    const uint32_t code_size = read32(image + 12);
    const uint32_t data_offset = read32(image + 16);
    const uint32_t data_size = read32(image + 20);
    const uint32_t entry = read32(image + 24);
    if (code_offset + code_size > (uint32_t)length || data_offset + data_size > (uint32_t)length || entry >= code_size) {
        run_fail(error, error_size, "CDE FILE TRUNCATED");
        return CDE_RUN_ERROR;
    }
    const uint8_t* program = image + code_offset;
    const uint8_t* data = image + data_offset;

    int text_start[STR_MAX];
    int text_size[STR_MAX];
    int text_count = 0;
    uint32_t data_pos = 5;
    run_img_count = 0;
    if (data_size >= 5 && data[0] == 0xFF) {
        const uint32_t declared = read32(data + 1);
        uint32_t offset = 5;
        for (uint32_t i = 0; i < declared && i < STR_MAX; i++) {
            if (offset + 4 > data_size) break;
            const uint32_t size = read32(data + offset);
            offset += 4;
            if (offset + size > data_size) break;
            text_start[text_count] = (int)offset;
            text_size[text_count] = (int)size;
            text_count++;
            offset += size;
        }
        data_pos = offset;
    }
    if (data_size >= 5 && data[0] == 0xFF && data_pos + 4 <= data_size) {
        const uint32_t declared = read32(data + data_pos);
        uint32_t offset = data_pos + 4;
        for (uint32_t i = 0; i < declared && i < IMG_MAX; i++) {
            if (offset + 10 > data_size) break;
            const int embedded = data[offset];
            int width = data[offset + 2] | (data[offset + 3] << 8);
            int height = data[offset + 4] | (data[offset + 5] << 8);
            const uint32_t name_id = read32(data + offset + 6);
            offset += 10;
            if (embedded && (width <= 0 || height <= 0 || width > IMG_STRIDE || height > IMG_STRIDE)) {
                run_fail(error, error_size, "BAD IMAGE DATA");
                return CDE_RUN_ERROR;
            }
            if (embedded) {
                const uint32_t packed = (uint32_t)((width * height + 1) / 2);
                if (offset + packed > data_size) {
                    run_fail(error, error_size, "BAD IMAGE DATA");
                    return CDE_RUN_ERROR;
                }
                for (int p = 0; p < width * height; p++) {
                    const uint8_t byte = data[offset + (uint32_t)(p / 2)];
                    run_img[i][p] = (p & 1) ? (byte & 15) : (byte >> 4);
                }
                offset += packed;
            } else {
                char file[40];
                int n = 0;
                static uint8_t temp[IMG_PIXELS];
                int loaded_w = 0;
                int loaded_h = 0;
                if (name_id < (uint32_t)text_count) {
                    while (n < text_size[name_id] && n < 39) {
                        file[n] = (char)data[text_start[name_id] + n];
                        n++;
                    }
                }
                file[n] = '\0';
                if (!image_loader || !image_loader(file, temp, &loaded_w, &loaded_h)) {
                    int m = 0;
                    if (error && error_size > 0) {
                        error[0] = '\0';
                        put_text(error, &m, "MISSING IMAGE: ", error_size);
                        put_text(error, &m, file, error_size);
                    }
                    return CDE_RUN_ERROR;
                }
                width = loaded_w;
                height = loaded_h;
                for (int y = 0; y < height; y++) {
                    for (int x = 0; x < width; x++) run_img[i][y * width + x] = temp[y * IMG_STRIDE + x];
                }
            }
            run_img_w[i] = width;
            run_img_h[i] = height;
            run_img_count = (int)i + 1;
        }
    }

    int32_t stack[STACK_MAX];
    int32_t vars[VAR_MAX];
    uint32_t calls[CALL_MAX];
    int sp = 0;
    int cp = 0;
    for (int i = 0; i < VAR_MAX; i++) vars[i] = 0;
    uint32_t pc = entry;
    long steps = 0;
    int fault = 0;

    while (1) {
        if (pc >= code_size) {
            run_fail(error, error_size, "PROGRAM COUNTER OUT OF RANGE");
            return CDE_RUN_ERROR;
        }
        steps++;
        if ((steps & 0x7FF) == 0 && host->should_stop && host->should_stop()) return CDE_RUN_STOPPED;
        if (steps > STEP_LIMIT) {
            run_fail(error, error_size, "INSTRUCTION LIMIT REACHED");
            return CDE_RUN_ERROR;
        }
        const uint8_t op = program[pc++];
        int32_t a;
        int32_t b;
        switch (op) {
            case OP_NOP:
                break;
            case OP_PUSH:
                if (pc + 4 > code_size || sp >= STACK_MAX) fault = 1;
                else {
                    stack[sp++] = (int32_t)read32(program + pc);
                    pc += 4;
                }
                break;
            case OP_POP:
                if (sp > 0) sp--;
                else fault = 1;
                break;
            case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_MOD:
            case OP_EQ: case OP_NE: case OP_LT: case OP_GT: case OP_LE: case OP_GE:
                if (sp < 2) {
                    fault = 1;
                    break;
                }
                b = stack[--sp];
                a = stack[--sp];
                if (op == OP_ADD) a = (int32_t)((uint32_t)a + (uint32_t)b);
                else if (op == OP_SUB) a = (int32_t)((uint32_t)a - (uint32_t)b);
                else if (op == OP_MUL) a = (int32_t)((uint32_t)a * (uint32_t)b);
                else if (op == OP_DIV || op == OP_MOD) {
                    if (b == 0) {
                        run_fail(error, error_size, "DIVISION BY ZERO");
                        return CDE_RUN_ERROR;
                    }
                    a = op == OP_DIV ? a / b : a % b;
                } else if (op == OP_EQ) a = a == b;
                else if (op == OP_NE) a = a != b;
                else if (op == OP_LT) a = a < b;
                else if (op == OP_GT) a = a > b;
                else if (op == OP_LE) a = a <= b;
                else a = a >= b;
                stack[sp++] = a;
                break;
            case OP_PRINT:
                if (sp < 1) fault = 1;
                else console_print_int(host, stack[--sp]);
                break;
            case OP_PRINT_CHAR:
                if (sp < 1) fault = 1;
                else {
                    a = stack[--sp];
                    if (host->put_char) host->put_char((char)a);
                }
                break;
            case OP_PRINT_STR:
                if (sp < 1) {
                    fault = 1;
                    break;
                }
                a = stack[--sp];
                if (a >= 0 && a < text_count && host->put_char) {
                    for (int i = 0; i < text_size[a]; i++) host->put_char((char)data[text_start[a] + i]);
                }
                break;
            case OP_SET_VAR:
                if (pc >= code_size || sp < 1 || program[pc] >= VAR_MAX) fault = 1;
                else vars[program[pc++]] = stack[--sp];
                break;
            case OP_GET_VAR:
                if (pc >= code_size || sp >= STACK_MAX || program[pc] >= VAR_MAX) fault = 1;
                else stack[sp++] = vars[program[pc++]];
                break;
            case OP_JMP:
                if (pc + 4 > code_size) fault = 1;
                else {
                    const uint32_t target = read32(program + pc);
                    if (target >= code_size) fault = 1;
                    else pc = target;
                }
                break;
            case OP_JZ: case OP_JNZ:
                if (pc + 4 > code_size || sp < 1) {
                    fault = 1;
                    break;
                }
                a = stack[--sp];
                {
                    const uint32_t target = read32(program + pc);
                    pc += 4;
                    if ((op == OP_JZ && a == 0) || (op == OP_JNZ && a != 0)) {
                        if (target >= code_size) fault = 1;
                        else pc = target;
                    }
                }
                break;
            case OP_CALL:
                if (pc + 4 > code_size || cp >= CALL_MAX) {
                    fault = 1;
                    break;
                }
                {
                    const uint32_t target = read32(program + pc);
                    pc += 4;
                    if (target >= code_size) fault = 1;
                    else {
                        calls[cp++] = pc;
                        pc = target;
                    }
                }
                break;
            case OP_RET:
                if (cp < 1) fault = 1;
                else pc = calls[--cp];
                break;
            case OP_KEY:
                if (sp >= STACK_MAX) fault = 1;
                else stack[sp++] = host->get_key ? host->get_key() : 0;
                break;
            case OP_MAINLOOP:
                if (host->mainloop) host->mainloop();
                if (host->should_stop && host->should_stop()) return CDE_RUN_STOPPED;
                break;
            case OP_WIN_CREATE: {
                if (sp < 5) {
                    fault = 1;
                    break;
                }
                const int32_t title = stack[--sp];
                const int32_t h = stack[--sp];
                const int32_t w = stack[--sp];
                const int32_t y = stack[--sp];
                const int32_t x = stack[--sp];
                int id = -1;
                if (host->win_create) {
                    if (title >= 0 && title < text_count) {
                        id = host->win_create(x, y, w, h, (const char*)(data + text_start[title]), text_size[title]);
                    } else {
                        id = host->win_create(x, y, w, h, "", 0);
                    }
                }
                stack[sp++] = id;
                break;
            }
            case OP_WIN_PRINT_STR: {
                if (sp < 2) {
                    fault = 1;
                    break;
                }
                const int32_t text = stack[--sp];
                const int32_t id = stack[--sp];
                if (id >= 0 && text >= 0 && text < text_count && host->win_char) {
                    for (int i = 0; i < text_size[text]; i++) host->win_char(id, (char)data[text_start[text] + i]);
                }
                break;
            }
            case OP_WIN_PRINT_INT: {
                if (sp < 2) {
                    fault = 1;
                    break;
                }
                const int32_t value = stack[--sp];
                const int32_t id = stack[--sp];
                if (id >= 0) win_print_int(host, id, value);
                break;
            }
            case OP_WIN_PRINT_CHAR: {
                if (sp < 2) {
                    fault = 1;
                    break;
                }
                const int32_t value = stack[--sp];
                const int32_t id = stack[--sp];
                if (id >= 0 && host->win_char) host->win_char(id, (char)value);
                break;
            }
            case OP_WIN_REFRESH: case OP_WIN_CLEAR: case OP_WIN_CLOSE: {
                if (sp < 1) {
                    fault = 1;
                    break;
                }
                const int32_t id = stack[--sp];
                if (id < 0) break;
                if (op == OP_WIN_REFRESH && host->win_refresh) host->win_refresh(id);
                else if (op == OP_WIN_CLEAR && host->win_clear) host->win_clear(id);
                else if (op == OP_WIN_CLOSE && host->win_close) host->win_close(id);
                break;
            }
            case OP_WIN_COLOR: {
                if (sp < 2) {
                    fault = 1;
                    break;
                }
                const int32_t value = stack[--sp];
                const int32_t id = stack[--sp];
                if (id >= 0 && host->win_color) host->win_color(id, value >= 0 && value < 16 ? run_palette[value] : (uint32_t)value);
                break;
            }
            case OP_WIN_PIXEL: {
                if (sp < 3) {
                    fault = 1;
                    break;
                }
                const int32_t y = stack[--sp];
                const int32_t x = stack[--sp];
                const int32_t id = stack[--sp];
                if (id >= 0 && host->win_pixel) host->win_pixel(id, x, y);
                break;
            }
            case OP_WIN_RECT: case OP_WIN_LINE: {
                if (sp < 5) {
                    fault = 1;
                    break;
                }
                const int32_t v4 = stack[--sp];
                const int32_t v3 = stack[--sp];
                const int32_t v2 = stack[--sp];
                const int32_t v1 = stack[--sp];
                const int32_t id = stack[--sp];
                if (id < 0) break;
                if (op == OP_WIN_RECT && host->win_rect) host->win_rect(id, v1, v2, v3, v4);
                else if (op == OP_WIN_LINE && host->win_line) host->win_line(id, v1, v2, v3, v4);
                break;
            }
            case OP_WIN_IMAGE: {
                if (sp < 4) {
                    fault = 1;
                    break;
                }
                const int32_t index = stack[--sp];
                const int32_t y = stack[--sp];
                const int32_t x = stack[--sp];
                const int32_t id = stack[--sp];
                if (id >= 0 && index >= 0 && index < run_img_count && host->win_image) {
                    host->win_image(id, x, y, run_img[index], run_img_w[index], run_img_h[index]);
                }
                break;
            }
            case OP_WIN_TEXT: {
                if (sp < 4) {
                    fault = 1;
                    break;
                }
                const int32_t text = stack[--sp];
                const int32_t y = stack[--sp];
                const int32_t x = stack[--sp];
                const int32_t id = stack[--sp];
                if (id >= 0 && text >= 0 && text < text_count && host->win_text) {
                    host->win_text(id, x, y, (const char*)(data + text_start[text]), text_size[text]);
                }
                break;
            }
            case OP_HALT:
                return CDE_RUN_OK;
            default:
                run_fail(error, error_size, "UNKNOWN INSTRUCTION");
                return CDE_RUN_ERROR;
        }
        if (fault) {
            run_fail(error, error_size, "STACK OR OPERAND ERROR");
            return CDE_RUN_ERROR;
        }
    }
}

int cde_run(const uint8_t* image, int length, const CdeHost* host, char* error, int error_size) {
    const int result = run_inner(image, length, host, error, error_size);
    if (host && host->finish) host->finish();
    return result;
}