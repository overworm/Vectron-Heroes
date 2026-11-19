#ifndef IMAGE_H
#define IMAGE_H
#include <stdint.h>

/* Пиксель 0xAARRGGBB. Прозрачность слоёв: 24-бит BMP -> чёрный (0,0,0) = прозрачный,
   32-бит BMP -> используется альфа-канал файла. */
typedef struct { int w, h; uint32_t *px; } Image;

#define ARGB(a,r,g,b) (((uint32_t)(a)<<24)|((uint32_t)(r)<<16)|((uint32_t)(g)<<8)|(uint32_t)(b))

Image    image_create(int w, int h);
void     image_free(Image *im);
int      image_load_bmp(Image *im, const char *path);   /* 1 = ok */
int      image_save_bmp(const Image *im, const char *path);
uint32_t px_blend(uint32_t dst, uint32_t src);          /* src over dst */

#endif
