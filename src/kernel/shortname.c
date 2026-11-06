#include "shortname.h"

static char upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static int allowed(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '~';
}

static char hex_digit(unsigned int v) {
    v &= 15;
    return (char)(v < 10 ? '0' + v : 'A' + v - 10);
}

int shortname_make(const char* input, const char* default_ext, char* out, int out_size) {
    int n = 0;
    int shortened = 0;
    out[n++] = '/';
    int last_slash = -1;
    for (int i = 0; input[i]; i++) {
        if (input[i] == '/') last_slash = i;
    }
    for (int i = 0; i <= last_slash && n < out_size - 14; i++) {
        if (i == 0 && input[i] == '/') continue;
        out[n++] = upper(input[i]);
    }
    const char* file = input + last_slash + 1;
    int length = 0;
    int dot = -1;
    unsigned int hash = 5381;
    while (file[length]) {
        if (file[length] == '.') dot = length;
        hash = hash * 33 + (unsigned char)upper(file[length]);
        length++;
    }
    const int base_end = dot >= 0 ? dot : length;
    char clean[48];
    int clean_length = 0;
    for (int i = 0; i < base_end && clean_length < 47; i++) {
        const char c = upper(file[i]);
        if (allowed(c)) clean[clean_length++] = c;
    }
    char base[9];
    int base_length = 0;
    if (clean_length == 0) {
        base[0] = 'F';
        base[1] = 'I';
        base[2] = 'L';
        base[3] = 'E';
        base_length = 4;
    } else if (clean_length > 8) {
        for (int i = 0; i < 5; i++) base[base_length++] = clean[i];
        base[base_length++] = '~';
        base[base_length++] = hex_digit(hash >> 4);
        base[base_length++] = hex_digit(hash);
        shortened = 1;
    } else {
        for (int i = 0; i < clean_length; i++) base[base_length++] = clean[i];
    }
    for (int i = 0; i < base_length && n < out_size - 6; i++) out[n++] = base[i];

    char ext[4];
    int ext_length = 0;
    int ext_seen = 0;
    if (dot >= 0) {
        for (int i = dot + 1; file[i]; i++) {
            const char c = upper(file[i]);
            if (!allowed(c)) continue;
            ext_seen++;
            if (ext_length < 3) ext[ext_length++] = c;
        }
        if (ext_seen > 3) shortened = 1;
    }
    if (ext_length == 0 && default_ext) {
        for (int i = 0; default_ext[i] && ext_length < 3; i++) ext[ext_length++] = upper(default_ext[i]);
    }
    if (ext_length > 0) {
        out[n++] = '.';
        for (int i = 0; i < ext_length && n < out_size - 1; i++) out[n++] = ext[i];
    }
    out[n] = '\0';
    return shortened;
}