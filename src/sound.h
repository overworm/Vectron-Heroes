#ifndef SOUND_H
#define SOUND_H
/* Звуковая система: SNES-стиль, mp3/wav из assets/sfx и assets/bgm.
   У каждого героя/врага/босса — свои step/jump/attack.
   У героев ещё 3 атаки: melee, ranged, super. */

/* Базовые общие эффекты */
enum {
    SFX_PICKUP, SFX_DOOR, SFX_PANEL, SFX_UI, SFX_EXPLODE, SFX_ENEMY_HIT, SFX_ZAP, SFX_FREEZE, SFX_SPLASH, SFX_FLAME, SFX_WARN,
    SFX_COMMON_COUNT
};

/* Пер-персонажные слоты: step, jump, land, melee, ranged, super, hurt, death */
enum {
    SFX_ACT_STEP = 0, SFX_ACT_JUMP, SFX_ACT_LAND,
    SFX_ACT_MELEE, SFX_ACT_RANGED, SFX_ACT_SUPER,
    SFX_ACT_HURT, SFX_ACT_DEATH,
    SFX_ACT_COUNT
};

/* Максимум актёров со своими звуками: 7 героев + 12 врагов + 9 боссов */
#define SFX_ACTOR_MAX 28

enum {
    BGM_LEVEL, BGM_BOSS, BGM_WIN, BGM_GAMEOVER, BGM_COUNT
};

void sound_init(const char *assets_dir);
void sound_shutdown(void);

/* Общие эффекты */
void sound_play_sfx(int common_id);

/* Персонаж: kind = RigId (0..RIG_COUNT-1), act = SFX_ACT_* */
void sound_play_actor(int kind, int act);

/* то же, но звук привязан к месту: тише с расстоянием от "ушей" (центр камеры), панорама слева/справа */
void sound_listener(int x, int y);
void sound_play_actor_at(int kind, int act, int x, int y);
void sound_play_sfx_at(int common_id, int x, int y);

void sound_play_bgm(int id);
void sound_stop_bgm(void);
void sound_pause_all(int paused);   /* 1=pause music+sfx, 0=resume */
void sound_set_sfx_vol(float v);
void sound_set_bgm_vol(float v);

#endif
