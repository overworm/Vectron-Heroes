#ifndef RIG_H
#define RIG_H
#include "image.h"

/* Скелет из шариков (у каждого персонажа свои пропорции, цвета и реквизит) + библиотека анимаций.
   Оси модели: X вперёд, Y вверх, Z к зрителю. Углы - градусы (rx, ry, rz).
   rz > 0 у висящей вниз конечности = вперёд (+X). Родитель всегда раньше потомка, узел 0 - корень.
   Первые 15 узлов одинаковы у всех (тело, голова, руки, ноги); остальные (до 24) - одежда и детали. */
#define MAX_NODES 24
#define MAX_KEYS 14
#define MAX_PROPS 4
#define MAX_PROP_BALLS 16

enum {
    N_PELVIS, N_CHEST, N_HEAD,
    N_SH_L, N_EL_L, N_HAND_L,
    N_SH_R, N_EL_R, N_HAND_R,
    N_HIP_L, N_KNEE_L, N_FOOT_L,
    N_HIP_R, N_KNEE_R, N_FOOT_R,
    N_BASE_COUNT
};

enum { SL_SKIN, SL_TOP, SL_PANTS, SL_SHOE, SL_ACC1, SL_ACC2, SL_ACC3, SL_METAL, SL_COUNT };
typedef struct { uint32_t col[SL_COUNT]; } Palette;      /* 0xRRGGBB */

/* Одинаковый набор анимаций у всех. Герои: все 16. Враги: idle, walk, run, melee (вблизи),
   ranged (издали), hurt, death, falldmg (удар о землю после падения). */
typedef enum {
    A_IDLE, A_WALK, A_RUN, A_JUMP, A_LAND, A_FALL, A_GLIDE, A_SLIDE,
    A_ACTIVATE, A_CLIMB, A_MELEE, A_RANGED, A_SUPER, A_HURT, A_DEATH, A_FALLDMG,
    A_COUNT
} AnimId;

typedef enum {
    /* существующие (порядок не менять — индексы в ассетах и save) */
    RIG_PENGI = 0, RIG_BUM, RIG_DOORKEEPER, RIG_MISTRESS,
    RIG_SCOUT, RIG_BODY, RIG_HEAVY, RIG_BOSS,
    /* будущие герои (UI: квадраты со знаком вопроса) */
    RIG_HERO5, RIG_HERO6, RIG_HERO7,
    /* будущие враги (9 шт., спрайты позже) — всего 12 врагов на 9 уровней */
    RIG_ENEMY4, RIG_ENEMY5, RIG_ENEMY6, RIG_ENEMY7, RIG_ENEMY8,
    RIG_ENEMY9, RIG_ENEMY10, RIG_ENEMY11, RIG_ENEMY12,
    /* будущие боссы (8 шт.) — всего 9 боссов */
    RIG_BOSS2, RIG_BOSS3, RIG_BOSS4, RIG_BOSS5,
    RIG_BOSS6, RIG_BOSS7, RIG_BOSS8, RIG_BOSS9,
    RIG_COUNT
} RigId;

/* английские имена (файлы assets: spr_<имя>, sfx/<имя>_<действие>) */
#define RIG_CYBERBUM  RIG_BUM
#define RIG_NERD       RIG_HERO5
#define RIG_REFREEZER  RIG_HERO6
#define RIG_AUNTY     RIG_DOORKEEPER
#define RIG_WATCHMAN  RIG_DOORKEEPER
#define RIG_DOMINA    RIG_MISTRESS
#define RIG_ROCKETPUNK RIG_SCOUT
#define RIG_FITPUNK   RIG_SCOUT        /* спортсмен с ракетницей */
#define RIG_DUMBPUNK  RIG_BODY         /* спортсмен с гантелями */
#define RIG_RAMROD    RIG_HEAVY        /* суперкачок */
#define RIG_DELIVER    RIG_ENEMY4
#define RIG_FIREWALKER RIG_ENEMY5
#define RIG_MONOMAN    RIG_ENEMY6
#define RIG_MEGADOG   RIG_ENEMY8
#define RIG_CAPSULO   RIG_ENEMY9
#define RIG_IRONFEM    RIG_ENEMY7
#define RIG_PROFESSOR  RIG_BOSS        /* босс 1 */
#define RIG_MAESTRO    RIG_BOSS2
#define RIG_BIGPUSSY   RIG_BOSS3
#define RIG_COACH      RIG_BOSS4
#define RIG_HACKER     RIG_BOSS5
#define RIG_WATCHER    RIG_BOSS6
#define RIG_BOOTBOSS   RIG_BOSS7
#define RIG_SUPERDROM  RIG_BOSS8
#define RIG_DRUNKY     RIG_BOSS9
#define RIG_IS_BOSS(k) ((k) == RIG_BOSS || ((k) >= RIG_BOSS2 && (k) <= RIG_BOSS9))


typedef struct { float rot[MAX_NODES][3]; float root[3]; } Pose;
typedef struct { float t; Pose p; } Key;
typedef struct { int n; Key k[MAX_KEYS]; int dur; int loop; int ease; } Anim;

typedef struct { int parent; float off[3]; float r, seg_r; int slot, seg_slot; char name[16]; } Node;
typedef struct { float x, y, z, r; int slot; } PBall;
typedef struct { int node, n; unsigned anim_mask; PBall b[MAX_PROP_BALLS]; } Prop;   /* anim_mask: биты AnimId, когда виден */

typedef struct {
    const char *name;
    Node nodes[MAX_NODES]; int n;
    Anim anims[A_COUNT];
    Prop props[MAX_PROPS]; int nprops;
    Palette pal;
    float scale;                 /* множитель при рисовании (тяжелоатлет 1.5) */
    /* растровые спрайты (assets/spr_<имя>.bmp/.txt): если загружены, рисуются вместо шариков */
    Image spr; int spr_ok, spr_flip, spr_cw, spr_ch, spr_cols;
    int spr_start[A_COUNT], spr_n[A_COUNT], spr_dur[A_COUNT], spr_loop[A_COUNT];
    /* вид со спины (пингвин на канате): assets/pengi_back.bmp */
    Image spr_back; int spr_back_ok;
} Rig;

typedef struct {
    const Rig *rig;
    int   id;
    float time;       /* в кадрах 60 Гц */
    float speed;
    int   blend;      /* оставшиеся кадры кросс-фейда */
    int   done;       /* одноразовая анимация доиграла */
    Pose  from, cur;
} Animator;

void rigs_init(void);                                   /* строит все риги (встроенные) */
const Rig *rig_get(int id);
int  rig_save(const Rig *r, const char *path);          /* .anim: скелет + 16 анимаций (текст) */
int  rig_load(int id, const char *path);
int  rig_load_sprites(int id, const char *dir);          /* 1 = спрайты найдены */                /* заменяет узлы и анимации; при ошибке риг не меняется */
int  rig_anim_dur(const Rig *r, int anim);              /* длительность в кадрах */
const char *rig_anim_name(int anim);
void rig_pose_at(const Rig *r, int anim, float tn, Pose *out);   /* поза анимации в момент tn 0..1 (для просмотра) */

void animator_init(Animator *a, const Rig *r, int id);
void animator_play(Animator *a, int id);                /* смена анимации с плавным переходом */
void animator_restart(Animator *a, int id);             /* с нуля */
void animator_update(Animator *a);                      /* 1 кадр игры */

/* sx, sy - экранная позиция центра ног. facing: +1 вправо, -1 влево. flash 0..255 - вспышка белым. */
void model_draw_anim(Image *fb, const Rig *rig, const Animator *an, int sx, int sy, int facing, int flash);   /* спрайт или шарики */
void model_draw(Image *fb, const Rig *r, const Pose *pose, int anim, int sx, int sy, int facing, int flash);

#endif
