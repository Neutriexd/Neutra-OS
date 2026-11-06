#include "png.h"
#include "inflate.h"

#define IDAT_MAX 65536
#define RAW_MAX 90000

static uint8_t idat[IDAT_MAX];
static uint8_t raw[RAW_MAX];
static uint8_t prev_row[PNG_MAX_DIM * 4];
static uint8_t cur_row[PNG_MAX_DIM * 4];

static uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int absdiff(int a, int b) {
    return a > b ? a - b : b - a;
}

static uint8_t paeth(uint8_t a, uint8_t b, uint8_t c) {
    const int p = (int)a + (int)b - (int)c;
    const int pa = absdiff(p, a);
    const int pb = absdiff(p, b);
    const int pc = absdiff(p, c);
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

static void unfilter_row(int type, uint8_t* cur, const uint8_t* prev, int stride, int bpp) {
    for (int i = 0; i < stride; i++) {
        const uint8_t a = i >= bpp ? cur[i - bpp] : 0;
        const uint8_t b = prev[i];
        const uint8_t c = i >= bpp ? prev[i - bpp] : 0;
        uint8_t v = cur[i];
        if (type == 1) v = (uint8_t)(v + a);
        else if (type == 2) v = (uint8_t)(v + b);
        else if (type == 3) v = (uint8_t)(v + (((int)a + (int)b) >> 1));
        else if (type == 4) v = (uint8_t)(v + paeth(a, b, c));
        cur[i] = v;
    }
}

int png_decode(const uint8_t* data, int len, uint8_t* rgb, int rgb_max, int* w_out, int* h_out) {
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (len < 8) return -1;
    for (int i = 0; i < 8; i++) {
        if (data[i] != sig[i]) return -1;
    }
    int pos = 8;
    int w = 0;
    int h = 0;
    int depth = 0;
    int ctype = -1;
    int interlace = 0;
    int idat_len = 0;
    uint8_t palette[256 * 3];
    int pal_count = 0;
    int have_ihdr = 0;
    while (pos + 8 <= len) {
        const uint32_t clen = be32(data + pos);
        const uint8_t* type = data + pos + 4;
        const uint8_t* body = data + pos + 8;
        if (clen > (uint32_t)(len - pos - 12)) return -2;
        if (type[0] == 'I' && type[1] == 'H' && type[2] == 'D' && type[3] == 'R') {
            w = (int)be32(body);
            h = (int)be32(body + 4);
            depth = body[8];
            ctype = body[9];
            interlace = body[12];
            have_ihdr = 1;
        } else if (type[0] == 'P' && type[1] == 'L' && type[2] == 'T' && type[3] == 'E') {
            pal_count = (int)clen / 3;
            if (pal_count > 256) pal_count = 256;
            for (int i = 0; i < pal_count * 3; i++) palette[i] = body[i];
        } else if (type[0] == 'I' && type[1] == 'D' && type[2] == 'A' && type[3] == 'T') {
            if (idat_len + (int)clen > IDAT_MAX) return -3;
            for (uint32_t i = 0; i < clen; i++) idat[idat_len++] = body[i];
        } else if (type[0] == 'I' && type[1] == 'E' && type[2] == 'N' && type[3] == 'D') {
            break;
        }
        pos += 12 + (int)clen;
    }
    if (!have_ihdr || depth != 8 || interlace != 0) return -4;
    if (w <= 0 || h <= 0 || w > PNG_MAX_DIM || h > PNG_MAX_DIM) return -5;
    int bpp;
    if (ctype == 0) bpp = 1;
    else if (ctype == 2) bpp = 3;
    else if (ctype == 3) bpp = 1;
    else if (ctype == 4) bpp = 2;
    else if (ctype == 6) bpp = 4;
    else return -6;
    if (ctype == 3 && pal_count == 0) return -7;
    const int stride = w * bpp;
    const int expect = h * (1 + stride);
    if (expect > RAW_MAX) return -8;
    if (idat_len < 2) return -9;
    const int got = inflate_raw(idat + 2, idat_len - 2, raw, RAW_MAX);
    if (got < expect) return -10;
    if (w * h * 3 > rgb_max) return -11;

    for (int i = 0; i < stride; i++) prev_row[i] = 0;
    for (int y = 0; y < h; y++) {
        const uint8_t* line = raw + y * (1 + stride);
        const int ftype = line[0];
        for (int i = 0; i < stride; i++) cur_row[i] = line[1 + i];
        unfilter_row(ftype, cur_row, prev_row, stride, bpp);
        for (int x = 0; x < w; x++) {
            uint8_t r, g, b;
            uint8_t a = 255;
            const uint8_t* p = cur_row + x * bpp;
            if (ctype == 0) {
                r = g = b = p[0];
            } else if (ctype == 2) {
                r = p[0];
                g = p[1];
                b = p[2];
            } else if (ctype == 3) {
                const int idx = p[0];
                r = idx < pal_count ? palette[idx * 3] : 0;
                g = idx < pal_count ? palette[idx * 3 + 1] : 0;
                b = idx < pal_count ? palette[idx * 3 + 2] : 0;
            } else if (ctype == 4) {
                r = g = b = p[0];
                a = p[1];
            } else {
                r = p[0];
                g = p[1];
                b = p[2];
                a = p[3];
            }
            if (a < 255) {
                r = (uint8_t)((r * a + 255 * (255 - a)) / 255);
                g = (uint8_t)((g * a + 255 * (255 - a)) / 255);
                b = (uint8_t)((b * a + 255 * (255 - a)) / 255);
            }
            uint8_t* o = rgb + (y * w + x) * 3;
            o[0] = r;
            o[1] = g;
            o[2] = b;
        }
        for (int i = 0; i < stride; i++) prev_row[i] = cur_row[i];
    }
    *w_out = w;
    *h_out = h;
    return 0;
}

void png_box_sample(const uint8_t* rgb, int iw, int ih, int w, int h, int px, int py, uint8_t* out) {
    int x0 = px * iw / w;
    int x1 = (px + 1) * iw / w;
    int y0 = py * ih / h;
    int y1 = (py + 1) * ih / h;
    if (x1 <= x0) x1 = x0 + 1;
    if (y1 <= y0) y1 = y0 + 1;
    if (x1 > iw) x1 = iw;
    if (y1 > ih) y1 = ih;
    uint32_t sr = 0;
    uint32_t sg = 0;
    uint32_t sb = 0;
    uint32_t count = 0;
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            const uint8_t* p = rgb + (y * iw + x) * 3;
            sr += p[0];
            sg += p[1];
            sb += p[2];
            count++;
        }
    }
    if (count == 0) count = 1;
    out[0] = (uint8_t)(sr / count);
    out[1] = (uint8_t)(sg / count);
    out[2] = (uint8_t)(sb / count);
}
