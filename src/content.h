#ifndef CONTENT_H
#define CONTENT_H
/* Заготовки контента на всю игру: 9 уровней, 12 типов врагов, 9 боссов, 7 героев.
   implemented = 0: записи заложены (имена, характеристики, привязка к уровням), но спрайтов и поведения пока нет -
   такие враги/боссы не появляются в игре. Когда нарисуете спрайты: положите assets/spr_<id>.bmp/.txt, поставьте implemented = 1. */
#include "rig.h"

#define LEVEL_COUNT 9
#define ENEMY_TYPES 12
#define BOSS_TYPES  9
#define HERO_TYPES  7

typedef struct {
    const char *id;          /* имя файла спрайтов: assets/spr_<id>.bmp */
    const char *name;        /* название в игре */
    int rig;                 /* какой готовый риг использовать, пока нет своего */
    int hp, weight;          /* жизни; вес при случайном появлении (чем меньше, тем реже) */
    int implemented;
} EnemyDef;

typedef struct { const char *id, *name; int rig, hp, implemented; } BossDef;
typedef struct { const char *id, *name; int implemented; } HeroDef;

typedef struct {
    const char *name;        /* название уровня */
    int implemented;
    int enemies[4];          /* какие враги живут на уровне (индексы в ENEMY_DEFS), -1 - нет */
    int boss;                /* индекс в BOSS_DEFS */
} LevelDef;

extern const EnemyDef ENEMY_DEFS[ENEMY_TYPES];
extern const BossDef  BOSS_DEFS[BOSS_TYPES];
extern const HeroDef  HERO_DEFS[HERO_TYPES];
extern const LevelDef LEVEL_DEFS[LEVEL_COUNT];

/* Выбор случайного врага для уровня level на этапе stage: только implemented, учитывает веса. heavies - сколько тяжёлых уже на поле */
int content_pick_enemy(int level, int stage, int heavies, uint32_t rnd);
#endif
