#ifndef UI_H
#define UI_H
#include "image.h"

/* Простые средства рисования для HUD и эффектов (всё в экранных координатах кадра). */
void ui_rect(Image *fb, int x, int y, int w, int h, uint32_t argb);        /* заливка с альфой */
void ui_frame(Image *fb, int x0, int y0, int x1, int y1, uint32_t argb);   /* контур включительно */
void ui_disc(Image *fb, int cx, int cy, int r, uint32_t argb);
void ui_line(Image *fb, int x0, int y0, int x1, int y1, uint32_t argb);
int  ui_text_width(const char *utf8);
void ui_text(Image *fb, int x, int y, const char *utf8, uint32_t argb);    /* с тёмной тенью */
void ui_text_center(Image *fb, int cx, int y, const char *utf8, uint32_t argb);
void ui_blit_keyed(Image *fb, const Image *src, int x, int y, int flip);   /* чёрный 0,0,0 или альфа=0 - прозрачно */

#endif
