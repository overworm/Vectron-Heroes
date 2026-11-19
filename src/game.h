#ifndef GAME_H
#define GAME_H
#include "platform.h"
#include <stdint.h>

int  game_init(const char *assets_dir);
void game_set_start(int x, int y);          /* отладка: где появляется игрок (-1 = по умолчанию) */
void game_update(const Input *in);          /* один шаг 60 Гц */
void game_render(void);
const uint32_t *game_pixels(void);
void game_shutdown(void);

#endif
