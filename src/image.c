#include "image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Image image_create(int w, int h) {
    Image im;
    im.w = w; im.h = h;
    im.px = (uint32_t *)calloc((size_t)w * (size_t)h, sizeof(uint32_t));
    return im;
}

void image_free(Image *im) {
    free(im->px);
    im->px = NULL;
    im->w = im->h = 0;
}

uint32_t px_blend(uint32_t d, uint32_t s) {
    uint32_t a = s >> 24;
    if (a == 0) return d;
    if (a == 255) return s;
    uint32_t ia = 255 - a;
    uint32_t r = (((s >> 16) & 255) * a + ((d >> 16) & 255) * ia) / 255;
    uint32_t g = (((s >> 8) & 255) * a + ((d >> 8) & 255) * ia) / 255;
    uint32_t b = ((s & 255) * a + (d & 255) * ia) / 255;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }

int image_load_bmp(Image *im, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 54) { fclose(f); return 0; }
    uint8_t *b = (uint8_t *)malloc((size_t)sz);
    if (!b || fread(b, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); free(b); return 0; }
    fclose(f);

    int ok = 0;
    if (b[0] == 'B' && b[1] == 'M') {
        uint32_t off = rd32(b + 10), hdr = rd32(b + 14);
        int w = (int32_t)rd32(b + 18), h = (int32_t)rd32(b + 22);
        int bpp = (int)rd16(b + 28);
        uint32_t comp = rd32(b + 30);
        int topdown = h < 0;
        if (h < 0) h = -h;
        if (hdr >= 40 && w > 0 && h > 0 && (bpp == 24 || bpp == 32) && (comp == 0 || comp == 3)) {
            size_t stride = (((size_t)w * (size_t)bpp + 31) / 32) * 4;
            if ((size_t)off + stride * (size_t)h <= (size_t)sz) {
                int any_alpha = 0;
                if (bpp == 32) {
                    for (int y = 0; y < h && !any_alpha; y++)
                        for (int x = 0; x < w; x++)
                            if (b[off + stride * (size_t)y + (size_t)x * 4 + 3]) { any_alpha = 1; break; }
                }
                *im = image_create(w, h);
                for (int y = 0; y < h; y++) {
                    const uint8_t *row = b + off + stride * (size_t)(topdown ? y : h - 1 - y);
                    for (int x = 0; x < w; x++) {
                        const uint8_t *p = row + (size_t)x * (bpp / 8);
                        uint32_t bl = p[0], g = p[1], r = p[2], a;
                        if (bpp == 32 && any_alpha) a = p[3];
                        else a = (r | g | bl) ? 255 : 0;       /* ключ по чёрному (0,0,0) */
                        im->px[y * w + x] = ARGB(a, r, g, bl);
                    }
                }
                ok = 1;
            }
        }
    }
    free(b);
    return ok;
}

int image_save_bmp(const Image *im, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    int w = im->w, h = im->h;
    uint32_t stride = ((uint32_t)w * 3 + 3) & ~3u;
    uint32_t dsz = stride * (uint32_t)h, fsz = 54 + dsz;
    uint8_t hd[54];
    memset(hd, 0, sizeof hd);
    hd[0] = 'B'; hd[1] = 'M';
    hd[2] = fsz & 255; hd[3] = (fsz >> 8) & 255; hd[4] = (fsz >> 16) & 255; hd[5] = (uint8_t)(fsz >> 24);
    hd[10] = 54; hd[14] = 40;
    hd[18] = w & 255; hd[19] = (w >> 8) & 255; hd[20] = (w >> 16) & 255; hd[21] = (uint8_t)(w >> 24);
    hd[22] = h & 255; hd[23] = (h >> 8) & 255; hd[24] = (h >> 16) & 255; hd[25] = (uint8_t)(h >> 24);
    hd[26] = 1; hd[28] = 24;
    hd[34] = dsz & 255; hd[35] = (dsz >> 8) & 255; hd[36] = (dsz >> 16) & 255; hd[37] = (uint8_t)(dsz >> 24);
    fwrite(hd, 1, 54, f);
    uint8_t *row = (uint8_t *)calloc(1, stride);
    for (int y = h - 1; y >= 0; y--) {
        for (int x = 0; x < w; x++) {
            uint32_t c = im->px[y * w + x];
            if ((c >> 24) < 128) c = 0;               /* прозрачное -> чёрный */
            row[x * 3 + 0] = c & 255;
            row[x * 3 + 1] = (c >> 8) & 255;
            row[x * 3 + 2] = (c >> 16) & 255;
        }
        fwrite(row, 1, stride, f);
    }
    free(row);
    fclose(f);
    return 1;
}
