#ifndef NEUTRA_LAYOUT_H
#define NEUTRA_LAYOUT_H

#include <stdint.h>

#define LAYOUT_TEXT 64
#define LAYOUT_HREF 200
#define LAYOUT_WRAP 50
#define LAYOUT_RUNS 6
#define LAYOUT_FLAG_BOLD 1
#define LAYOUT_FLAG_CENTER 2
#define LAYOUT_FLAG_BG 4
#define LAYOUT_FLAG_BIG 8
#define LAYOUT_FLAG_MONO 16

#define LAYOUT_KIND_TEXT 0
#define LAYOUT_KIND_HEADING 1
#define LAYOUT_KIND_LINK 2
#define LAYOUT_KIND_IMAGE 3
#define LAYOUT_KIND_IMAGE_CONT 4

typedef struct {
    uint8_t start;
    uint32_t color;
    uint8_t flags;
} LayoutRun;

typedef struct {
    char text[LAYOUT_TEXT];
    uint8_t kind;
    uint8_t flags;
    uint8_t indent;
    uint32_t color;
    uint32_t bg;
    uint8_t nruns;
    LayoutRun runs[LAYOUT_RUNS];
    uint16_t img_w;
    uint16_t img_h;
    char href[LAYOUT_HREF];
} LayoutLine;

int layout_build(int root, LayoutLine* out, int max);

#endif
