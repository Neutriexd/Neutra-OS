#include <stdint.h>
#include "vga.h"
#include "graphics.h"
#include "font.h"

static const uint8_t font_letters_digits[36][7] = {
    {14, 17, 17, 31, 17, 17, 17}, {30, 17, 17, 30, 17, 17, 30},
    {14, 17, 16, 16, 16, 17, 14}, {30, 17, 17, 17, 17, 17, 30},
    {31, 16, 16, 30, 16, 16, 31}, {31, 16, 16, 30, 16, 16, 16},
    {14, 17, 16, 23, 17, 17, 15}, {17, 17, 17, 31, 17, 17, 17},
    {14, 4, 4, 4, 4, 4, 14}, {7, 2, 2, 2, 18, 18, 12},
    {17, 18, 20, 24, 20, 18, 17}, {16, 16, 16, 16, 16, 16, 31},
    {17, 27, 21, 21, 17, 17, 17}, {17, 25, 21, 19, 17, 17, 17},
    {14, 17, 17, 17, 17, 17, 14}, {30, 17, 17, 30, 16, 16, 16},
    {14, 17, 17, 17, 21, 18, 13}, {30, 17, 17, 30, 20, 18, 17},
    {15, 16, 16, 14, 1, 1, 30}, {31, 4, 4, 4, 4, 4, 4},
    {17, 17, 17, 17, 17, 17, 14}, {17, 17, 17, 17, 17, 10, 4},
    {17, 17, 17, 21, 21, 21, 10}, {17, 17, 10, 4, 10, 17, 17},
    {17, 17, 10, 4, 4, 4, 4}, {31, 1, 2, 4, 8, 16, 31},
    {14, 17, 19, 21, 25, 17, 14}, {4, 12, 4, 4, 4, 4, 14},
    {14, 17, 1, 2, 4, 8, 31}, {30, 1, 1, 14, 1, 1, 30},
    {2, 6, 10, 18, 31, 2, 2}, {31, 16, 16, 30, 1, 1, 30},
    {14, 16, 16, 30, 17, 17, 14}, {31, 1, 2, 4, 8, 8, 8},
    {14, 17, 17, 14, 17, 17, 14}, {14, 17, 17, 15, 1, 1, 14}
};

const uint8_t* font_glyph(unsigned char c) {
    static const uint8_t glyph_question[7] = {14, 17, 1, 2, 4, 0, 4};
    static const uint8_t glyph_at[7] = {14, 17, 23, 21, 23, 16, 14};
    static const uint8_t glyph_colon[7] = {0, 4, 4, 0, 4, 4, 0};
    static const uint8_t glyph_dot[7] = {0, 0, 0, 0, 0, 4, 4};
    static const uint8_t glyph_comma[7] = {0, 0, 0, 0, 0, 4, 8};
    static const uint8_t glyph_dash[7] = {0, 0, 0, 31, 0, 0, 0};
    static const uint8_t glyph_underscore[7] = {0, 0, 0, 0, 0, 0, 31};
    static const uint8_t glyph_slash[7] = {1, 2, 2, 4, 8, 8, 16};
    static const uint8_t glyph_backslash[7] = {16, 8, 8, 4, 2, 2, 1};
    static const uint8_t glyph_dollar[7] = {4, 15, 20, 14, 5, 30, 4};
    static const uint8_t glyph_apostrophe[7] = {4, 4, 8, 0, 0, 0, 0};
    static const uint8_t glyph_quote[7] = {10, 10, 10, 0, 0, 0, 0};
    static const uint8_t glyph_plus[7] = {0, 4, 4, 31, 4, 4, 0};
    static const uint8_t glyph_equal[7] = {0, 0, 31, 0, 31, 0, 0};
    static const uint8_t glyph_left_paren[7] = {2, 4, 8, 8, 8, 4, 2};
    static const uint8_t glyph_right_paren[7] = {8, 4, 2, 2, 2, 4, 8};
    static const uint8_t glyph_exclamation[7] = {4, 4, 4, 4, 4, 0, 4};
    static const uint8_t glyph_pipe[7] = {4, 4, 4, 4, 4, 4, 4};
    static const uint8_t glyph_star[7] = {0, 21, 14, 31, 14, 21, 0};
    static const uint8_t glyph_hash[7] = {10, 10, 31, 10, 31, 10, 10};
    static const uint8_t glyph_less[7] = {2, 4, 8, 16, 8, 4, 2};
    static const uint8_t glyph_greater[7] = {8, 4, 2, 1, 2, 4, 8};
    static const uint8_t glyph_left_bracket[7] = {14, 8, 8, 8, 8, 8, 14};
    static const uint8_t glyph_right_bracket[7] = {14, 2, 2, 2, 2, 2, 14};
    static const uint8_t glyph_left_brace[7] = {2, 4, 4, 8, 4, 4, 2};
    static const uint8_t glyph_right_brace[7] = {8, 4, 4, 2, 4, 4, 8};
    static const uint8_t glyph_percent[7] = {25, 25, 2, 4, 8, 19, 19};
    static const uint8_t glyph_ampersand[7] = {12, 18, 20, 8, 21, 18, 13};
    static const uint8_t glyph_caret[7] = {4, 10, 17, 0, 0, 0, 0};
    static const uint8_t glyph_tilde[7] = {0, 0, 8, 21, 2, 0, 0};
    static const uint8_t glyph_grave[7] = {8, 4, 0, 0, 0, 0, 0};

    if (c < ' ') return 0;
    if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
    if (c >= 'A' && c <= 'Z') return font_letters_digits[c - 'A'];
    if (c >= '0' && c <= '9') return font_letters_digits[26 + c - '0'];

    switch (c) {
        case ' ': return 0;
        case '@': return glyph_at;
        case ':': case ';': return glyph_colon;
        case '.': return glyph_dot;
        case ',': return glyph_comma;
        case '-': return glyph_dash;
        case '_': return glyph_underscore;
        case '/': return glyph_slash;
        case '\\': return glyph_backslash;
        case '$': return glyph_dollar;
        case '\'': return glyph_apostrophe;
        case '"': return glyph_quote;
        case '+': return glyph_plus;
        case '=': return glyph_equal;
        case '(': return glyph_left_paren;
        case ')': return glyph_right_paren;
        case '!': return glyph_exclamation;
        case '|': return glyph_pipe;
        case '*': return glyph_star;
        case '#': return glyph_hash;
        case '<': return glyph_less;
        case '>': return glyph_greater;
        case '[': return glyph_left_bracket;
        case ']': return glyph_right_bracket;
        case '{': return glyph_left_brace;
        case '}': return glyph_right_brace;
        case '%': return glyph_percent;
        case '&': return glyph_ampersand;
        case '^': return glyph_caret;
        case '~': return glyph_tilde;
        case '`': return glyph_grave;
        case '?': return glyph_question;
        default: return glyph_question;
    }
}

void vga_draw_text_at(int x, int y, const char* text, uint32_t color) {
    while (*text) {
        const uint8_t* glyph = font_glyph((unsigned char)*text++);
        if (glyph) {
            for (int row = 0; row < 7; row++) {
                for (int column = 0; column < 5; column++) {
                    if (glyph[row] & (1u << (4 - column))) {
                        draw_rect(x + column * 2, y + row * 2, 2, 2, color);
                    }
                }
            }
        }
        x += 12;
    }
}