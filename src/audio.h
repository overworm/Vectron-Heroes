#ifndef AUDIO_H
#define AUDIO_H
/* Звук и музыка. Эффекты: sfx/<имя>.mp3 (или .wav/.ogg), музыка: bgm/<имя>.mp3 (или .ogg/.wav).
   Если файла нет - эффект молчит. Стиль и ограничения - как у SNES: 32 кГц, 8 голосов. Шаблонные звуки делает tools/make_sfx.py. */

enum {
    SFX_JUMP, SFX_LAND, SFX_HURT, SFX_DEATH, SFX_SWING, SFX_HIT, SFX_SHOT, SFX_SUPER, SFX_THROW, SFX_ROCKET, SFX_EXPLODE,
    SFX_BUTTON, SFX_REPAIR, SFX_LIFT, SFX_WALL, SFX_PICKUP, SFX_DOOR, SFX_SWITCH, SFX_HOOK, SFX_CRASH, SFX_BOSS, SFX_SPIN,
    SFX_WIN, SFX_STEP, SFX_COUNT
};

extern const char *const SFX_NAMES[SFX_COUNT];

int  audio_init(const char *root_dir);                    /* root_dir содержит sfx/ и bgm/; 0 - звука не будет, игра работает */
void audio_shutdown(void);
void audio_listener(int x, int y);                        /* где "уши" (центр камеры): громкость падает с расстоянием */
void audio_sfx(int id);                                   /* на полную громкость */
void audio_sfx_at(int id, int x, int y);                  /* с учётом расстояния и панорамы */
void audio_music(const char *name, int loop);             /* bgm/<name>.mp3; NULL - тишина */
const char *audio_current_music(void);
#endif
