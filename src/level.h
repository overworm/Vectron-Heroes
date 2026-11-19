#ifndef LEVEL_HDR_H
#define LEVEL_HDR_H
#include "image.h"
#include "config.h"
#include <stdint.h>

/* Этажи: Y поверхности пола (там стоят ноги). Шаг 128 px, плита толщиной 1 тайл. */
#define FLOOR1_Y 560
#define FLOOR2_Y 432
#define FLOOR3_Y 304
#define FLOOR4_Y 176
#define FLOOR_PITCH 128

#define MAX_OBJS 128
#define MAX_SPAWNS 48

typedef enum {
    O_NONE, O_DOOR, O_WALL_ICE, O_WALL_WOOD, O_WALL_STONE, O_WALL_FIRE,
    O_PLAT, O_LIFT, O_BRIDGE, O_ROPE,
    O_BUTTON, O_PANEL_FIX, O_PANEL_BRIDGE,
    O_SNOW, O_KEY, O_ARTIFACT, O_EXIT,
    O_CRATE                      /* бак на невидимой платформе: твёрдый только для героев, по триггеру поднимается и пропускает под собой */
} ObjType;

enum { PK_STATIC, PK_MOVER, PK_COLLAPSE, PK_NERD }; /* PK_NERD: платформа, которую ломает супер Nerd */
enum { CS_IDLE, CS_SHAKE, CS_FALL, CS_WAIT, CS_RISE };    /* состояния проваливающейся платформы */
enum { LS_BOTTOM, LS_UP, LS_TOP, LS_DOWN };               /* состояния лифта */

/* Объект уровня. Платформы, лифты и мосты рисуются в "псевдо-3D": текстура + карта нормалей из градиента высоты. */
typedef struct {
    uint8_t type, kind, solid, visible, hero_only;
    uint8_t on;                    /* открыто / сломано / включено / нажато / починено */
    uint8_t need, have, state;     /* питание лифтов и мостов: работает при have >= need */
    int16_t x, y, w, h;            /* текущий прямоугольник (y - верхняя поверхность) */
    int16_t x0, y0, x1, y1;        /* крайние положения пути */
    int16_t dx, dy;                /* смещение за последний кадр (перенос пассажиров) */
    int16_t target;                /* кого включает кнопка/панель; для снега - какой ключ открыть */
    int16_t timer, progress;
    int8_t dir;
    fx fx_, fy_, vy_;              /* точные координаты */
    uint32_t *alb; int8_t *nrm;    /* искусство платформы (3 байта нормали на пиксель) */
} Obj;

typedef struct { int16_t x, y; } Spawn;     /* x - центр, y - уровень ног */

/* Зал босса: триггер появления (по x и этажу), границы полёта */
typedef struct { int trig_x0, trig_x1, floor, ceil, x0, x1, hole0, hole1; } Arena;   /* hole - шахта лифта в полу зала (опасное место) */

typedef struct {
    Image bg, fg, crate;
    uint8_t *mask;                 /* коллизии статической геометрии (1 = твёрдо) */
    int w, h;
    Obj obj[MAX_OBJS]; int nobj;
    Spawn sp[MAX_SPAWNS]; int nsp;
    int hero_x[8], hero_y[8];
    uint8_t room_type[12];           /* 0 авто, 1 открытая, 2 открываемая, 3 тупик, 4 спец(stealth), 5 босс (из .map) */
    int ncp, cp_x[8], cp_y[8];       /* checkpoints по порядку квестовой цепочки (1S, 2S, ...) */
    uint8_t occ[MAX_OBJS];         /* игра отмечает: на объекте стоят (для проваливающихся платформ) */
    int frame;
    int obj_door, obj_exit, obj_artifact, obj_key, obj_snow, obj_crate;   /* индексы ключевых объектов */
    int obj_bbtn[2], obj_bplat[3];  /* запас объектов для боссов: временные кнопки и платформы (скрыты, пока не нужны) */
    Arena arena;
    int mirror_m;                  /* центр зеркала для перевёрнутой гравитации: y' = mirror_m - y */
    int water_y, water_x0, water_x1; /* поверхность воды (0 - нет воды) и её границы по x */
    int map_id;                    /* 1..3 */
} Level;

int  level_load(Level *lv, const char *assets_dir, int map_id);
void level_set_mirror(int on, int m);                            /* запросы твёрдости видят мир отражённым по y */
void level_free(Level *lv);
int  level_save_fg(const Level *lv, const char *path);

/* 1, если в прямоугольнике (включительно) есть твёрдое: геометрия или активный объект.
   За левым/правым краем - стены, выше/ниже мира - свободно (внизу пропасть). */
int  level_solid_rect(const Level *lv, int x0, int y0, int x1, int y1);        /* без объектов hero_only: враги, снаряды */
int  level_solid_rect_team(const Level *lv, int x0, int y0, int x1, int y1, int team);   /* team 0 (герои) видят и объекты hero_only */
int  level_stand_obj(const Level *lv, int x0, int x1, int py);   /* твёрдый объект, на верху которого стоят ноги, иначе -1 */

void level_update(Level *lv);
void level_restore_platforms(Level *lv); /* PK_COLLAPSE сразу на место после смерти */                                    /* платформы, лифты, мосты: 1 кадр */
void level_power(Level *lv, int idx);                            /* подать питание объекту (have++) */
void level_render(Level *lv, Image *fb, int cam_x, int cam_y);   /* передний план + объекты (без персонажей) */
int  level_stage(int px);                                        /* 0..3 по горизонтали */
int  level_room(int px, int py);                                 /* 0..11: 3 колонки × 4 этажа */

#endif
