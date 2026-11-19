#include "ui.h"
#include "font_data.h"
#include <string.h>

void ui_rect(Image *fb, int x, int y, int w, int h, uint32_t c) {
    for (int yy = y; yy < y + h; yy++) {
        if (yy < 0 || yy >= fb->h) continue;
        for (int xx = x; xx < x + w; xx++)
            if (xx >= 0 && xx < fb->w) fb->px[yy * fb->w + xx] = px_blend(fb->px[yy * fb->w + xx], c);
    }
}
void ui_frame(Image *fb, int x0, int y0, int x1, int y1, uint32_t c) {
    ui_rect(fb, x0, y0, x1 - x0 + 1, 1, c); ui_rect(fb, x0, y1, x1 - x0 + 1, 1, c);
    ui_rect(fb, x0, y0 + 1, 1, y1 - y0 - 1, c); ui_rect(fb, x1, y0 + 1, 1, y1 - y0 - 1, c);
}
void ui_disc(Image *fb, int cx, int cy, int r, uint32_t c) {
    for (int y = -r; y <= r; y++)
        for (int x = -r; x <= r; x++)
            if (x * x + y * y <= r * r) {
                int px = cx + x, py = cy + y;
                if (px >= 0 && px < fb->w && py >= 0 && py < fb->h) fb->px[py * fb->w + px] = px_blend(fb->px[py * fb->w + px], c);
            }
}
void ui_line(Image *fb, int x0, int y0, int x1, int y1, uint32_t c) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
        if (x0 >= 0 && x0 < fb->w && y0 >= 0 && y0 < fb->h) fb->px[y0 * fb->w + x0] = px_blend(fb->px[y0 * fb->w + x0], c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* UTF-8 -> кодпоинт; возвращает длину символа */
static int utf8_next(const unsigned char *s, unsigned *cp) {
    if (s[0] < 0x80) { *cp = s[0]; return 1; }
    if ((s[0] & 0xE0) == 0xC0 && s[1]) { *cp = ((unsigned)(s[0] & 0x1F) << 6) | (s[1] & 0x3F); return 2; }
    if ((s[0] & 0xF0) == 0xE0 && s[1] && s[2]) { *cp = ((unsigned)(s[0] & 0x0F) << 12) | ((unsigned)(s[1] & 0x3F) << 6) | (s[2] & 0x3F); return 3; }
    *cp = '?';
    return 1;
}
static int glyph_index(unsigned cp) {
    for (int i = 0; i < FONT_COUNT; i++) if (FONT_CP[i] == cp) return i;
    return 0x3F - 32;
}
int ui_text_width(const char *t) {
    int n = 0;
    const unsigned char *s = (const unsigned char *)t;
    while (*s) { unsigned cp; s += utf8_next(s, &cp); n++; }
    return n * FONT_CW;
}
static void glyph(Image *fb, int x, int y, int gi, uint32_t c) {
    for (int yy = 0; yy < FONT_CH; yy++) {
        unsigned char row = FONT_ROWS[gi][yy];
        for (int xx = 0; xx < FONT_CW; xx++)
            if ((row >> (FONT_CW - 1 - xx)) & 1) {
                int px = x + xx, py = y + yy;
                if (px >= 0 && px < fb->w && py >= 0 && py < fb->h) fb->px[py * fb->w + px] = px_blend(fb->px[py * fb->w + px], c);
            }
    }
}
void ui_text(Image *fb, int x, int y, const char *t, uint32_t c) {
    const unsigned char *s = (const unsigned char *)t;
    int cx = x;
    while (*s) {
        unsigned cp;
        s += utf8_next(s, &cp);
        int gi = glyph_index(cp);
        glyph(fb, cx + 1, y + 1, gi, 0xC0000000u);
        glyph(fb, cx, y, gi, c);
        cx += FONT_CW;
    }
}
void ui_text_center(Image *fb, int cx, int y, const char *t, uint32_t c) { ui_text(fb, cx - ui_text_width(t) / 2, y, t, c); }

void ui_blit_keyed(Image *fb, const Image *src, int x, int y, int flip) {
    for (int yy = 0; yy < src->h; yy++) {
        int py = y + yy;
        if (py < 0 || py >= fb->h) continue;
        for (int xx = 0; xx < src->w; xx++) {
            int px = x + xx;
            if (px < 0 || px >= fb->w) continue;
            uint32_t s = src->px[yy * src->w + (flip ? src->w - 1 - xx : xx)];
            if (s >> 24) fb->px[py * fb->w + px] = s | 0xFF000000u;
        }
    }
}
