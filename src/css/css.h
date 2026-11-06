#ifndef NEUTRA_CSS_H
#define NEUTRA_CSS_H

#include <stdint.h>

typedef struct {
    int has_color;
    uint32_t color;
    int bold;
    int center;
    int hidden;
    int has_bg;
    uint32_t bg;
    int mt;
    int mb;
    int indent;
    int big;
    int mono;
    int display_grid;
    int grid_cols;
} CssStyle;

void css_reset(void);
void css_add_sheet(const char* text, int len);
void css_compute(int node, const CssStyle* parent, CssStyle* out);
void css_set_viewport(int width);

#endif
