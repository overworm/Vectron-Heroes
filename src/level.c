#include "level.h"
#include "lhmp.h"
#include "ui.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Свет для платформ (экранные оси, y вниз): слева-сверху-спереди */
#define LIGHT_X (-57)
#define LIGHT_Y (-70)
#define LIGHT_Z   89

/* ===================================================================== шум и цвет */
static uint32_t hash2(int x, int y) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
static int noise(int x, int y, int amp) { return (int)(hash2(x, y) % (uint32_t)(2 * amp + 1)) - amp; }
static int c8(int v) { return v < 1 ? 1 : v > 255 ? 255 : v; }          /* минимум 1: не чёрный -> не прозрачный */
static uint32_t rgb(int r, int g, int b) { return ARGB(255, c8(r), c8(g), c8(b)); }
static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }

/* ===================================================================== карта тайлов */
static int g_map2, g_map3, g_maptest;
static uint8_t T[MAP_H][MAP_W];     /* 0 пусто, 1 плита, 2 стена/кирпич */

static void fill(int x0, int y0, int x1, int y1, int t) {
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            if (x >= 0 && x < MAP_W && y >= 0 && y < MAP_H) T[y][x] = (uint8_t)t;
}

static const int FLOOR_ROW[4] = {35, 27, 19, 11};

static void gen_fg(Level *lv) {
    int W = lv->w, H = lv->h;
    for (int ty = 0; ty < MAP_H; ty++)
        for (int tx = 0; tx < MAP_W; tx++) {
            int t = T[ty][tx];
            if (!t) continue;
            for (int v = 0; v < TILE; v++)
                for (int u = 0; u < TILE; u++) {
                    int x = tx * TILE + u, y = ty * TILE + v;
                    int n = noise(x, y, 7), r, g, b;
                    if (t == 1) {                                  /* бетонная плита */
                        int s = ((x / 32) & 1) ? 4 : -3;
                        r = 104 + n + s; g = 98 + n + s; b = 116 + n + s;
                        if (v == 7 || v == 8) { r -= 14; g -= 14; b -= 12; }
                    } else {                                       /* кирпичная стена */
                        int row = y / 8, off = (row & 1) ? 8 : 0;
                        int mortar = (y % 8 == 0) || ((x + off) % 16 == 0);
                        int tint = (int)(hash2((x + off) / 16, row) % 20u) - 10;
                        if (mortar) { r = 70 + n; g = 64 + n; b = 78 + n; }
                        else { r = 128 + tint + n; g = 98 + tint + n; b = 92 + tint + n; }
                    }
                    lv->fg.px[y * W + x] = rgb(r, g, b);
                }
        }
    /* снег на верхних гранях: 3-4 px, неровный край */
    for (int x = 0; x < W; x++)
        for (int y = 0; y < H; y++) {
            if (!lv->fg.px[y * W + x]) continue;
            if (y > 0 && lv->fg.px[(y - 1) * W + x]) continue;
            int depth = 3 + (int)(hash2(x >> 1, 9) % 2u);
            for (int k = 0; k < depth && y + k < H && lv->fg.px[(y + k) * W + x]; k++) {
                int n = noise(x, y + k, 6);
                lv->fg.px[(y + k) * W + x] = k == depth - 1 ? rgb(190 + n, 205 + n, 225 + n) : rgb(238 + n / 2, 244 + n / 2, 252);
            }
        }
    /* контур: твёрдый пиксель рядом с пустым */
    uint32_t *copy = (uint32_t *)malloc((size_t)W * H * sizeof(uint32_t));
    memcpy(copy, lv->fg.px, (size_t)W * H * sizeof(uint32_t));
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            if (!copy[y * W + x]) continue;
            int edge = (x > 0 && !copy[y * W + x - 1]) || (x < W - 1 && !copy[y * W + x + 1]) ||
                       (y > 0 && !copy[(y - 1) * W + x]) || (y < H - 1 && !copy[(y + 1) * W + x]);
            if (edge && !(y == 0) && !(x == 0) && !(x == W - 1)) lv->fg.px[y * W + x] = rgb(36, 28, 48);
        }
    free(copy);
}

/* ===================================================================== объекты */
static int add_obj(Level *lv, int type, int x, int y, int w, int h) {
    if (lv->nobj >= MAX_OBJS) return -1;
    Obj *o = &lv->obj[lv->nobj];
    memset(o, 0, sizeof *o);
    o->type = (uint8_t)type;
    o->x = (int16_t)x; o->y = (int16_t)y; o->w = (int16_t)w; o->h = (int16_t)h;
    o->x0 = o->x1 = o->x; o->y0 = o->y1 = o->y;
    o->fx_ = INT_FX(x); o->fy_ = INT_FX(y);
    o->target = -1; o->visible = 1;
    switch (type) {
    case O_DOOR: case O_WALL_ICE: case O_WALL_WOOD: case O_WALL_STONE: case O_WALL_FIRE: case O_PLAT: case O_LIFT: o->solid = 1; break;
    case O_BRIDGE: o->visible = 0; break;
    case O_KEY: o->visible = 0; break;
    case O_ARTIFACT: o->visible = 0; break;
    default: break;
    }
    return lv->nobj++;
}

/* Псевдо-3D: albedo + нормали из градиента высоты. style: 0 заснеженный камень, 1 стальной лифт, 2 ледяной мост */
static void make_art(Obj *o, int style) {
    int w = o->w, h = o->h;
    o->alb = (uint32_t *)malloc((size_t)w * h * sizeof(uint32_t));
    o->nrm = (int8_t *)malloc((size_t)w * h * 3);
    float *hg = (float *)malloc((size_t)w * h * sizeof(float));
    for (int v = 0; v < h; v++)
        for (int u = 0; u < w; u++) {
            int d = u < v ? u : v;
            if (w - 1 - u < d) d = w - 1 - u;
            if (h - 1 - v < d) d = h - 1 - v;
            float b = d >= 3 ? 1.f : (float)d / 3.f;
            b = b * b * (3.f - 2.f * b);
            int n = noise(u, v, 8), r, g, bl;
            float bump = 0.f;
            if (style == 1) {                                          /* лифт: сталь и жёлто-чёрная кайма */
                int stripe = ((u + v) / 4) & 1;
                if (v >= h - 3) { r = stripe ? 216 : 40; g = stripe ? 176 : 40; bl = stripe ? 30 : 44; }
                else { r = 112 + n; g = 118 + n; bl = 128 + n; }
                bump = (u % 12 == 0) ? 1.f : 0.f;
            } else {
                int seam = (u % 16 == 0) || v == 4;
                if (style == 2) { r = 150 + n; g = 190 + n; bl = 220 + n; if (seam) { r -= 30; g -= 20; bl -= 10; } }
                else { r = 122 + n; g = 118 + n; bl = 130 + n; if (seam) { r -= 34; g -= 34; bl -= 30; } }
                if (v < 2 + (int)(hash2(u >> 1, 5) % 2u)) {                /* снежная шапка */
                    r = 240 + n / 3; g = 246 + n / 3; bl = 252; bump = 1.2f;
                }
                if (seam) bump -= 0.6f;
            }
            o->alb[v * w + u] = rgb(r, g, bl);
            float lum = (0.30f * (float)r + 0.59f * (float)g + 0.11f * (float)bl) / 255.f;
            hg[v * w + u] = b * 3.f + lum * 1.2f + bump;
        }
    for (int v = 0; v < h; v++)
        for (int u = 0; u < w; u++) {
            int ul = u > 0 ? u - 1 : 0, ur = u < w - 1 ? u + 1 : w - 1, vu = v > 0 ? v - 1 : 0, vd = v < h - 1 ? v + 1 : h - 1;
            float dx = (hg[v * w + ur] - hg[v * w + ul]) * 0.5f;
            float dy = (hg[vd * w + u] - hg[vu * w + u]) * 0.5f;
            float nx = -dx, ny = -dy, nz = 1.f;
            float inv = 1.f / sqrtf(nx * nx + ny * ny + nz * nz);
            int8_t *q = &o->nrm[(v * w + u) * 3];
            q[0] = (int8_t)(clampf(nx * inv, -1.f, 1.f) * 127.f);
            q[1] = (int8_t)(clampf(ny * inv, -1.f, 1.f) * 127.f);
            q[2] = (int8_t)(clampf(nz * inv, 0.f, 1.f) * 127.f);
        }
    free(hg);
}

static int add_plat(Level *lv, int x, int y, int w, int kind, int x1) {
    int i = add_obj(lv, O_PLAT, x, y, w, 8);
    if (i < 0) return i;
    lv->obj[i].kind = (uint8_t)kind;
    lv->obj[i].x1 = (int16_t)x1;
    lv->obj[i].dir = 1;
    make_art(&lv->obj[i], 0);
    return i;
}
static int add_lift(Level *lv, int x, int y_bottom, int y_top, int need) {
    int i = add_obj(lv, O_LIFT, x, y_bottom, 48, 8);
    if (i < 0) return -1;
    lv->obj[i].y0 = (int16_t)y_bottom; lv->obj[i].y1 = (int16_t)y_top;
    lv->obj[i].need = (uint8_t)need;
    make_art(&lv->obj[i], 1);
    return i;
}
static void spawn_at(Level *lv, int col, int floor_y) {
    if (lv->nsp < MAX_SPAWNS) { lv->sp[lv->nsp].x = (int16_t)(col * TILE + 8); lv->sp[lv->nsp].y = (int16_t)floor_y; lv->nsp++; }
}

/* запас временных объектов для боссов: 2 кнопки на полу и 3 платформы (скрыты и не твёрдые, пока не включены) */
static void boss_pool(Level *lv) {
    for (int i = 0; i < 2; i++) {
        int k = add_obj(lv, O_BUTTON, 0, 0, 14, 16);
        if (k >= 0) { lv->obj[k].visible = 0; lv->obj[k].on = 1; }
        lv->obj_bbtn[i] = k;
    }
    for (int i = 0; i < 3; i++) {
        int k = add_plat(lv, 0, 0, 48, PK_STATIC, 0);
        if (k >= 0) { lv->obj[k].visible = 0; lv->obj[k].solid = 0; }
        lv->obj_bplat[i] = k;
    }
    lv->mirror_m = lv->arena.ceil + lv->arena.floor - 1;
    lv->water_x0 = lv->arena.x0; lv->water_x1 = lv->arena.x1; lv->water_y = 0;
}

static void build_level1(Level *lv);
static void build_level2(Level *lv);
static void build_level3(Level *lv);
static void build_leveltest(Level *lv);

static void build_level(Level *lv) {
    if (lv->map_id >= 4) build_leveltest(lv);
    else if (lv->map_id == 3) build_level3(lv);
    else if (lv->map_id == 2) build_level2(lv);
    else build_level1(lv);
}

static void build_level1(Level *lv) {
    memset(T, 0, sizeof T);
    fill(0, 3, 1, 36, 2); fill(99, 3, 100, 36, 2);                 /* внешние стены */
    fill(1, 3, 99, 4, 1);                                          /* крыша */
    for (int f = 0; f < 4; f++) fill(1, FLOOR_ROW[f], 99, FLOOR_ROW[f] + 1, 1);
#define HOLE(f, c0, c1) fill((c0), FLOOR_ROW[f], (c1), FLOOR_ROW[f] + 1, 0)
    HOLE(1, 9, 11); HOLE(1, 14, 17); HOLE(1, 22, 28); HOLE(1, 44, 47); HOLE(1, 48, 61); HOLE(1, 76, 84); HOLE(1, 86, 89);
    HOLE(2, 14, 17); HOLE(2, 22, 28); HOLE(2, 86, 89);
    HOLE(3, 89, 92); HOLE(3, 96, 98);
#undef HOLE
    /* перегородки между этапами */
    fill(28, 28, 29, 32, 2);                                       /* над воротами 1 этажа */
    fill(28, 20, 29, 27, 2); fill(28, 12, 29, 19, 2); fill(28, 4, 29, 11, 2);
    fill(70, 12, 71, 19, 2);                                       /* зал босса на 3 этаже */
    fill(58, 4, 59, 11, 2);                                        /* коридор выхода на 4 этаже */
    fill(92, 20, 96, 26, 2);                                       /* низкий лаз (16 px) - только скользящий пингвин */

    lv->nobj = 0; lv->nsp = 0;
    /* ---- этап 1: канат (пингвин) -> кнопка -> лифт (вахтёрша) -> снежные платформы -> ключ -> ворота ---- */
    int rope1 = add_obj(lv, O_ROPE, 166, FLOOR2_Y, 4, FLOOR1_Y - FLOOR2_Y); lv->obj[rope1].dir = 1;
    /* верёвки между этажами — враги могут спускаться/подниматься */
    int rope12 = add_obj(lv, O_ROPE, 500, FLOOR2_Y, 4, FLOOR1_Y - FLOOR2_Y); lv->obj[rope12].dir = -1; lv->obj[rope12].kind = 1; lv->obj[rope12].visible = 0;
    int rope23 = add_obj(lv, O_ROPE, 700, FLOOR3_Y, 4, FLOOR2_Y - FLOOR3_Y); lv->obj[rope23].dir = 1; lv->obj[rope23].kind = 1; lv->obj[rope23].visible = 0;
    int rope23b = add_obj(lv, O_ROPE, 1100, FLOOR3_Y, 4, FLOOR2_Y - FLOOR3_Y); lv->obj[rope23b].dir = -1; lv->obj[rope23b].kind = 1; lv->obj[rope23b].visible = 0;
    int rope34 = add_obj(lv, O_ROPE, 1400, FLOOR4_Y, 4, FLOOR3_Y - FLOOR4_Y); lv->obj[rope34].dir = 1; lv->obj[rope34].kind = 1; lv->obj[rope34].visible = 0;
    int l1 = add_lift(lv, 14 * TILE, FLOOR1_Y, FLOOR3_Y, 1);
    int b1 = add_obj(lv, O_BUTTON, 19 * TILE, FLOOR2_Y - 16, 14, 16); lv->obj[b1].target = (int16_t)l1;
    /* Узкая кирпичная стена (1 тайл) между 2 и 3 этажом у правого края платформы
       перед дырой (col 22) — кнопка на col 19 свободна. */
    fill(21, 20, 22, 27, 2);
    static const struct { int x, y, kind, x1; } shaft[5] = {
        {352, 344, PK_STATIC, 0}, {400, 384, PK_COLLAPSE, 0}, {352, 424, PK_STATIC, 0},
        {400, 464, PK_COLLAPSE, 0}, {352, 504, PK_STATIC, 0} };
    for (int i = 0; i < 5; i++) add_plat(lv, shaft[i].x, shaft[i].y, 32, shaft[i].kind, shaft[i].x1);
    lv->obj_snow = add_obj(lv, O_SNOW, 356, FLOOR1_Y - 14, 36, 14);
    lv->obj_key = add_obj(lv, O_KEY, 366, FLOOR1_Y - 16, 14, 16);
    lv->obj[lv->obj_snow].target = (int16_t)lv->obj_key;
    lv->obj_door = add_obj(lv, O_DOOR, 28 * TILE, FLOOR1_Y - 48, 16, 48);
    /* бак на невидимой платформе: перекрывает 1 этаж слева от сугроба (твёрдый столб от потолка до пола - перепрыгнуть нельзя).
       Панель кибердеда справа поднимает платформу на рост героя: под баком можно пройти. Враги проходят сквозь него всегда. */
    lv->obj_crate = add_obj(lv, O_CRATE, 296, FLOOR1_Y - 112, 48, 112);
    {
        Obj *c = &lv->obj[lv->obj_crate];
        c->solid = 1; c->hero_only = 1; c->need = 1;
        c->y0 = FLOOR1_Y; c->y1 = FLOOR1_Y - 42;            /* низ платформы: пол -> выше на рост героя */
        c->vy_ = INT_FX(FLOOR1_Y);
    }
    int pc = add_obj(lv, O_PANEL_FIX, 27 * TILE - 4, FLOOR1_Y - 32, 16, 32); lv->obj[pc].target = (int16_t)lv->obj_crate;
    /* ---- этап 2: деревянная стена (дед) -> починить панель -> лифт (домина) -> пропасть -> мост ---- */
    add_obj(lv, O_WALL_WOOD, 35 * TILE, FLOOR1_Y - 112, 16, 112);
    int l2 = add_lift(lv, 44 * TILE, FLOOR1_Y, FLOOR2_Y, 1);
    int p1 = add_obj(lv, O_PANEL_FIX, 38 * TILE, FLOOR1_Y - 32, 16, 32); lv->obj[p1].target = (int16_t)l2;
    int br = add_obj(lv, O_BRIDGE, 48 * TILE, FLOOR2_Y, 13 * TILE, 8);
    lv->obj[br].need = 1; make_art(&lv->obj[br], 2);
    int pb = add_obj(lv, O_PANEL_BRIDGE, 62 * TILE, FLOOR2_Y - 32, 16, 32); lv->obj[pb].target = (int16_t)br;
    /* ---- этап 3: ледяная, огненная и каменная стены; платформы над пропастью ---- */
    add_obj(lv, O_WALL_ICE, 66 * TILE, FLOOR2_Y - 112, 16, 112);
    add_obj(lv, O_WALL_FIRE, 70 * TILE, FLOOR2_Y - 112, 16, 112);
    add_obj(lv, O_WALL_STONE, 74 * TILE, FLOOR2_Y - 112, 16, 112);
    add_plat(lv, 1228, FLOOR2_Y, 32, PK_MOVER, 1262);
    add_plat(lv, 1296, FLOOR2_Y, 32, PK_COLLAPSE, 0);
    /* ---- этап 4: панель + кнопка в лазе -> лифт на 3 этаж (зал босса) ---- */
    int l3 = add_lift(lv, 86 * TILE, FLOOR2_Y, FLOOR3_Y, 2);
    int p2 = add_obj(lv, O_PANEL_FIX, 85 * TILE, FLOOR2_Y - 32, 16, 32); lv->obj[p2].target = (int16_t)l3;
    int b2 = add_obj(lv, O_BUTTON, 98 * TILE - 8, FLOOR2_Y - 16, 14, 16); lv->obj[b2].target = (int16_t)l3;
    /* ---- зал босса, после него: канат, кнопка, лифт на 4 этаж, выход ---- */
    add_plat(lv, 1224, FLOOR3_Y - 44, 32, PK_STATIC, 0);
    add_plat(lv, 1312, FLOOR3_Y - 44, 32, PK_STATIC, 0);
    int l4 = add_lift(lv, 89 * TILE, FLOOR3_Y, FLOOR4_Y, 1);
    int rope2 = add_obj(lv, O_ROPE, 1556, FLOOR4_Y, 4, FLOOR3_Y - FLOOR4_Y); lv->obj[rope2].dir = -1;
    int b3 = add_obj(lv, O_BUTTON, 93 * TILE, FLOOR4_Y - 16, 14, 16); lv->obj[b3].target = (int16_t)l4;
    lv->obj_exit = add_obj(lv, O_EXIT, 62 * TILE, FLOOR4_Y - 48, 24, 48);
    lv->obj_artifact = add_obj(lv, O_ARTIFACT, 0, 0, 14, 16);

    /* точки появления врагов (центр, пол) */
    static const int s1[][2] = {{12, 1}, {19, 1}, {24, 1}, {4, 2}, {7, 2}, {12, 2}, {19, 2}, {20, 2}, {4, 3}, {10, 3}, {19, 3}, {20, 3}};
    for (unsigned i = 0; i < sizeof s1 / sizeof s1[0]; i++) spawn_at(lv, s1[i][0], FLOOR1_Y - (s1[i][1] - 1) * FLOOR_PITCH);
    static const int s2[][2] = {{31, 1}, {33, 1}, {38, 1}, {42, 1}, {52, 1}, {55, 1}, {32, 2}, {36, 2}, {40, 2}};
    for (unsigned i = 0; i < sizeof s2 / sizeof s2[0]; i++) spawn_at(lv, s2[i][0], FLOOR1_Y - (s2[i][1] - 1) * FLOOR_PITCH);
    static const int s3[][2] = {{63, 2}, {64, 2}, {68, 2}, {72, 2}, {78, 1}, {90, 2}, {91, 2}};
    for (unsigned i = 0; i < sizeof s3 / sizeof s3[0]; i++) spawn_at(lv, s3[i][0], s3[i][1] == 1 ? FLOOR1_Y : FLOOR2_Y);
    static const int s4[][2] = {{74, 3}, {78, 3}, {82, 3}, {94, 3}, {66, 4}, {72, 4}, {78, 4}, {84, 4}};
    for (unsigned i = 0; i < sizeof s4 / sizeof s4[0]; i++) spawn_at(lv, s4[i][0], FLOOR1_Y - (s4[i][1] - 1) * FLOOR_PITCH);

    for (int i = 0; i < 8; i++) { lv->hero_x[i] = 40 + i * 20; lv->hero_y[i] = FLOOR1_Y; }
    lv->arena = (Arena){1164, 1580, FLOOR3_Y, FLOOR4_Y + 16, 1182, 1566, 1370, 1430};
    boss_pool(lv);
}

/* ---- уровень 2: концертный зал, босс Maestro. 12 комнат (3 колонки x 4 этажа):
         01 05 09   этаж 4 (верх)
         02 06 10   этаж 3
         03 07 11   этаж 2
         04 08 12   этаж 1 (низ); старт в 04, босс в 08, выход в 11
   Колонки (тайлы): A 2..32 | стена 33 | B 34..65 | стена 66 | C 67..98.
   Плиты-перекрытия: S1 (ряд 11, между 01-02 / 05-06 / 09-10), S2 (ряд 19, между 02-03 / 06-07 / 10-11),
   S3 (ряд 27, между 03-04 / 07-08 / 11-12); нижний пол (ряд 35) всегда сплошной.
   Лифты "U..." (ULBM1, ULBM2, UPL1, UPL2, UOB, DBOSSLT) стоят в проёме на верхнем уровне и закрывают его;
   по питанию (кнопка) опускаются вниз, ждут пассажиров и поднимаются обратно. */
enum { LK_NORMAL = 0, LK_REST_TOP = 1, LK_ONCE_DOWN = 2, LK_ONCE_CYCLE = 3 };
static int add_lift_top(Level *lv, int x, int y_top, int y_bottom, int need, int kind) {
    int i = add_lift(lv, x, y_bottom, y_top, need);
    if (i < 0) return i;
    Obj *o = &lv->obj[i];
    o->kind = (uint8_t)kind; o->state = LS_TOP; o->timer = 0;
    o->fy_ = INT_FX(y_top); o->y = (int16_t)y_top;
    return i;
}
static void build_level2(Level *lv) {
    const int FR[4] = {FLOOR4_Y, FLOOR3_Y, FLOOR2_Y, FLOOR1_Y};     /* пол комнат ряда 0..3 сверху вниз */
    memset(T, 0, sizeof T);
    fill(0, 3, 1, 36, 2); fill(99, 3, 100, 36, 2);                 /* внешние стены */
    fill(1, 3, 99, 4, 1);                                          /* потолок */
    for (int f = 0; f < 4; f++) fill(1, FLOOR_ROW[f], 99, FLOOR_ROW[f] + 1, 1);   /* плиты S1..S3 и нижний пол */
    /* несущие стены между колонками: от плиты до плиты; разрушаемые стены (объекты) оставляют проём в тайлах */
    int top_row[4];                                                /* первый внутренний ряд комнаты ряда k (сверху вниз) */
    for (int k = 0; k < 4; k++) top_row[k] = FLOOR_ROW[3 - k] - 7;
    /* ось A|B (тайл 33): 01|05 лёд, 02|06 несущая с лазом, 03|07 дерево, 04|08 несущая */
    fill(33, top_row[1], 34, top_row[1] + 7, 2);
    fill(33, top_row[1] + 6, 34, top_row[1] + 7, 0);                /* лаз высотой 1 тайл у пола: скольжение Pengi */
    fill(33, top_row[3], 34, top_row[3] + 7, 2);
    /* ось B|C (тайл 66): 05|09 дерево, 06|10 камень, 07|11 несущая, 08|12 огонь */
    fill(66, top_row[2], 67, top_row[2] + 7, 2);
    /* проёмы в плитах по таблице */
#define GAP(f, c0, c1) fill((c0), FLOOR_ROW[f], (c1), FLOOR_ROW[f] + 1, 0)
    /* S1 (f=3): 01-02 DL1 (Domina), 05-06 UPL1, 09-10 DL2 (Domina) */
    GAP(3, 10, 32); GAP(3, 39, 42); GAP(3, 78, 96);
    /* S2 (f=2): 02-03 проём (подъём по ступеням и возврат), 06-07 несущая... под ULBM2, 10-11 UPL2 */
    GAP(2, 18, 21); GAP(2, 39, 42); GAP(2, 70, 73);
    /* S3 (f=1): 03-04 ULBM1, 07-08 DBOSSLT, 11-12 бак TR и лифт UOB */
    GAP(1, 25, 28); GAP(1, 39, 42); GAP(1, 83, 86); GAP(1, 75, 78);
#undef GAP

    lv->nobj = 0; lv->nsp = 0;

    /* ===== Cyberbum: панели лифтов (как на ур.1), не кнопки ===== */
    int ulbm1 = add_lift_top(lv, 25 * TILE, FLOOR2_Y, FLOOR1_Y, 1, LK_REST_TOP);
    int bm1 = add_obj(lv, O_PANEL_FIX, 21 * TILE, FLOOR1_Y - 32, 16, 32); lv->obj[bm1].target = (int16_t)ulbm1;
    int ulbm2 = add_lift_top(lv, 39 * TILE, FLOOR3_Y, FLOOR2_Y, 1, LK_REST_TOP);
    int bm2 = add_obj(lv, O_PANEL_FIX, 59 * TILE, FLOOR2_Y - 32, 16, 32); lv->obj[bm2].target = (int16_t)ulbm2;
    /* ===== Pengi: кнопки у зон скольжения ===== */
    int upl1 = add_lift_top(lv, 39 * TILE, FLOOR4_Y, FLOOR3_Y, 1, LK_REST_TOP);
    int pm1 = add_obj(lv, O_BUTTON, 29 * TILE, FLOOR3_Y - 16, 14, 16); lv->obj[pm1].target = (int16_t)upl1;
    int upl2 = add_lift_top(lv, 70 * TILE, FLOOR3_Y, FLOOR2_Y, 1, LK_REST_TOP);
    int pm2 = add_obj(lv, O_BUTTON, 6 * TILE, FLOOR4_Y - 16, 14, 16); lv->obj[pm2].target = (int16_t)upl2;
    /* ===== Domina: DM1 (02) -> DL1 (пол 01), DM2 (10) -> DL2 (пол 09) ===== */
    int dl1 = add_obj(lv, O_BRIDGE, 10 * TILE, FLOOR4_Y, 22 * TILE, 8);
    lv->obj[dl1].need = 1; make_art(&lv->obj[dl1], 2);
    int dm1 = add_obj(lv, O_PANEL_BRIDGE, 8 * TILE, FLOOR3_Y - 32, 16, 32); lv->obj[dm1].target = (int16_t)dl1;
    int dl2 = add_obj(lv, O_BRIDGE, 78 * TILE, FLOOR4_Y, 18 * TILE, 8);
    lv->obj[dl2].need = 1; make_art(&lv->obj[dl2], 2);
    int dm2 = add_obj(lv, O_PANEL_BRIDGE, 90 * TILE, FLOOR3_Y - 32, 16, 32); lv->obj[dm2].target = (int16_t)dl2;
    /* ===== Aunty: AS1 (03) -> ключ; AS2 (09) -> бонус (артефакт телепорта) ===== */
    /* Aunty: AS1 → ключ DOOR1 (kind 0); AS2 → ключ DOOR2 (kind 1) */
    lv->obj_snow = add_obj(lv, O_SNOW, 5 * TILE, FLOOR2_Y - 14, 36, 14);
    lv->obj_key = add_obj(lv, O_KEY, 5 * TILE + 10, FLOOR2_Y - 16, 14, 16);
    lv->obj[lv->obj_snow].target = (int16_t)lv->obj_key;
    lv->obj[lv->obj_key].kind = 0; lv->obj[lv->obj_key].visible = 0;
    int as2 = add_obj(lv, O_SNOW, 90 * TILE, FLOOR4_Y - 14, 36, 14);
    int key2 = add_obj(lv, O_KEY, 90 * TILE + 10, FLOOR4_Y - 16, 14, 16);
    lv->obj[as2].target = (int16_t)key2; lv->obj[key2].visible = 0; lv->obj[key2].kind = 1;
    /* двери: kind 0 = DOOR1 (ключ 1), kind 1 = DOOR2 (ключ 2) */
    lv->obj_door = add_obj(lv, O_DOOR, 49 * TILE, FLOOR2_Y - 112, 16, 112);
    lv->obj[lv->obj_door].kind = 0;
    int door2 = add_obj(lv, O_DOOR, 49 * TILE, FLOOR3_Y - 112, 16, 112);
    if (door2 >= 0) lv->obj[door2].kind = 1;
    /* ===== стены между комнатами ===== */
    add_obj(lv, O_WALL_ICE,   33 * TILE, FLOOR4_Y - 112, 16, 112);   /* 01|05 лёд: Pengi */
    add_obj(lv, O_WALL_WOOD,  33 * TILE, FLOOR2_Y - 112, 16, 112);   /* 03|07 дерево: Aunty */
    add_obj(lv, O_WALL_WOOD,  66 * TILE, FLOOR4_Y - 112, 16, 112);   /* 05|09 дерево: Aunty */
    add_obj(lv, O_WALL_STONE, 66 * TILE, FLOOR3_Y - 112, 16, 112);   /* 06|10 камень: Domina */
    add_obj(lv, O_WALL_FIRE,  66 * TILE, FLOOR1_Y - 112, 16, 112);   /* 08|12 огонь: Cyberbum */
    /* NEWWALL: короткая стена со скольжением Pengi (верхний этаж, слева) */
    fill(12, FLOOR_ROW[3] - 5, 13, FLOOR_ROW[3], 2);
    fill(12, FLOOR_ROW[3] - 1, 13, FLOOR_ROW[3], 0); /* лаз у пола для slide */
    /* одна ступень у проёма 02→03 (остальные платформы убраны — проход свободен) */
    /* ===== Nerd: бак TR на разрушаемой опоре над проёмом, TRB внизу, UOB (одноразовый спуск) ===== */
    /* UOB: до середины комнаты — безопасный спрыг */
    int uob = add_lift_top(lv, 75 * TILE, FLOOR2_Y, FLOOR2_Y + (FLOOR1_Y - FLOOR2_Y) / 2, 1, LK_ONCE_DOWN);
    int nerd_plat = add_plat(lv, 83 * TILE, FLOOR2_Y, 48, PK_NERD, 0);
    lv->obj_crate = add_obj(lv, O_CRATE, 83 * TILE, FLOOR2_Y - 112, 48, 112);
    {
        Obj *c = &lv->obj[lv->obj_crate];
        c->solid = 1; c->hero_only = 1; c->need = 1; c->kind = 1;
        c->y0 = FLOOR1_Y; c->y1 = FLOOR2_Y - 112; c->fy_ = INT_FX(c->y1);
        c->target = (int16_t)uob;
    }
    if (nerd_plat >= 0) lv->obj[nerd_plat].target = (int16_t)lv->obj_crate;
    {   /* TRB: метка на полу комнаты 12 под баком */
        int trb = add_obj(lv, O_BUTTON, 84 * TILE - 2, FLOOR1_Y - 16, 14, 16);
        lv->obj[trb].on = 1;
    }
    /* ===== DBOSSLT: после артефакта опускается в зал босса и возвращает героев в 07 ===== */
    int dbosslt = add_lift_top(lv, 39 * TILE, FLOOR2_Y, FLOOR1_Y, 1, LK_ONCE_CYCLE);
    lv->obj_artifact = add_obj(lv, O_ARTIFACT, 0, 0, 14, 16);
    lv->obj[lv->obj_artifact].target = (int16_t)dbosslt;
    /* ===== EXIT (комната 11) ===== */
    lv->obj_exit = add_obj(lv, O_EXIT, 94 * TILE, FLOOR2_Y - 48, 24, 48);
    /* ===== канаты врагов между этажами (скрыты, пока враг не ими пользуется) ===== */
    static const int rp[][3] = {   /* x тайл, этаж верхний 0..2, направление выхода */
        {4, 0, 1}, {30, 1, -1}, {4, 1, 1}, {56, 1, -1}, {45, 0, 1}, {60, 1, -1}, {72, 0, 1}, {96, 1, -1}, {92, 2, 1}, {70, 1, 1} };
    for (unsigned i = 0; i < sizeof rp / sizeof rp[0]; i++) {
        int up = FR[rp[i][1]], dn = FR[rp[i][1] + 1];
        int r = add_obj(lv, O_ROPE, rp[i][0] * TILE, up, 4, dn - up);
        if (r < 0) break;
        lv->obj[r].dir = (int8_t)rp[i][2]; lv->obj[r].kind = 1; lv->obj[r].visible = 0;
    }
    /* ===== точки появления врагов (тайл, этаж): не в зале босса и не на лифтах ===== */
    static const int sp[][2] = {
        {14, 560}, {20, 560}, {30, 560}, {4, 432}, {13, 432}, {30, 432}, {6, 304}, {14, 304}, {26, 304},
        {36, 432}, {55, 432}, {62, 432}, {36, 304}, {45, 304}, {58, 304}, {36, 176}, {56, 176},
        {70, 304}, {84, 304}, {70, 432}, {90, 432}, {72, 560}, {80, 560}, {95, 560}, {72, 176}, {4, 176} };
    for (unsigned i = 0; i < sizeof sp / sizeof sp[0]; i++) spawn_at(lv, sp[i][0], sp[i][1]);
    for (int i = 0; i < 8; i++) { lv->hero_x[i] = 40 + i * 18; lv->hero_y[i] = FLOOR1_Y; }
    /* зал босса: комната 08 (колонка B, нижний этаж) */
    lv->arena = (Arena){552, 1048, FLOOR1_Y, FLOOR2_Y + 24, 568, 1040, 0, 0};
    /* платформа укрытия (~44px, как у босса ур.1) */
    add_plat(lv, 944, FLOOR1_Y - 44, 56, PK_STATIC, 0);   /* правее: голова Maestro не мешает запрыгнуть */
    boss_pool(lv);
}

/* ---- уровень 3: 4 колонки x 3 ряда (карта от автора). Колонки: A 1..25 | B 27..50 | C 52..75 | D 77..98 (стены на тайлах 26, 51, 76).
   Ряды: T (пол FLOOR3_Y) / M (пол FLOOR2_Y) / B (пол FLOOR1_Y); над рядом T - сплошная крыша (ceiling).
   Пазл: RB1->RL1->DW2->DB3->DF3->CB4->CL4->AK5->AD5->DB6->DL6->CB7->CL7->PS/PB8->PL8->PW9->AK10->DB11->DF11->RB12->RL12->AD10->NP13->BOSS->EXIT */
static void build_level3(Level *lv) {
    const int RT = 19, RM = 27;                                   /* ряды плит: T/M и M/B */
    memset(T, 0, sizeof T);
    fill(0, 3, 1, 36, 2); fill(99, 3, 100, 36, 2);                /* внешние стены */
    fill(1, 3, 99, 11, 2); fill(1, 11, 99, 12, 1);                /* крыша: выше нельзя никому */
    fill(1, RT, 99, RT + 1, 1); fill(1, RM, 99, RM + 1, 1); fill(1, 35, 99, 36, 1);
    /* стены колонок */
    fill(26, 15, 27, 19, 2);                                      /* A|B, ряд T: низкая стена 4 тайла - только Domina через верх */
    fill(26, 20, 27, 27, 2);                                      /* A|B, ряд M: несущая */
    fill(51, 12, 52, 18, 2);                                      /* B|C, ряд T: PS - внизу лаз в 1 тайл для Pengi */
    fill(76, 20, 77, 27, 2);                                      /* C|D, ряд M: несущая */
    fill(36, 20, 37, 27, 2); fill(65, 20, 66, 27, 2);             /* стены зала босса */
    /* проёмы в плитах */
    fill(3, RT, 6, RT + 1, 0);   fill(3, RM, 6, RM + 1, 0);       /* RL1, CL4 */
    fill(10, RM, 26, RM + 1, 0);                                  /* DF3 (скрытый пол Domina) */
    fill(29, RT, 32, RT + 1, 0); fill(29, RM, 32, RM + 1, 0);     /* DL6, CL7 */
    fill(56, RT, 59, RT + 1, 0);                                  /* NP13 (платформа Nerd) */
    fill(62, RT, 65, RT + 1, 0);                                  /* лифт зала босса после артефакта */
    fill(77, RT, 91, RT + 1, 0);                                  /* DF11 (скрытый пол Domina) */
    fill(93, RT, 96, RT + 1, 0); fill(93, RM, 96, RM + 1, 0);     /* RL12, PL8 */
    /* высокие карнизы под выключатели Refreezer (достать можно только магнитом по стенам) */
    fill(1, 15, 8, 16, 2);                                        /* карниз у левой стены ряда T */

    lv->nobj = 0; lv->nsp = 0;
    /* ---- 1) RB1 под плитой + панель антиграв Refreezer -> RL1 ---- */
    int rl1 = add_lift_top(lv, 3 * TILE, FLOOR3_Y, FLOOR2_Y, 1, LK_REST_TOP);
    int rb1 = add_obj(lv, O_BUTTON, 21 * TILE, FLOOR4_Y + 16, 14, 16); /* на потолке (крыше) стартовой комнаты */
    lv->obj[rb1].target = (int16_t)rl1; lv->obj[rb1].kind = 3;
    int gp1 = add_obj(lv, O_PANEL_FIX, 26, FLOOR3_Y - 32, 16, 32);   /* панель антигравитации у RL1 */
    lv->obj[gp1].kind = 3; lv->obj[gp1].target = -1;
    /* ---- 2) DW2 каменная стена B0|B1 (Domina), 3) DB3 -> DF3 ---- */
    add_obj(lv, O_WALL_STONE, 26 * TILE, FLOOR1_Y - 112, 16, 112);
    int df3 = add_obj(lv, O_BRIDGE, 10 * TILE, FLOOR2_Y, 16 * TILE, 8);
    lv->obj[df3].need = 1; make_art(&lv->obj[df3], 2);
    int db3 = add_obj(lv, O_PANEL_BRIDGE, 41 * TILE, FLOOR1_Y - 32, 16, 32);
    lv->obj[db3].target = (int16_t)df3;
    /* ---- 4) CB4 (Cyberbum) -> CL4 лифт M->B ---- */
    int cl4 = add_lift_top(lv, 3 * TILE, FLOOR2_Y, FLOOR1_Y, 1, LK_REST_TOP);
    int cb4 = add_obj(lv, O_PANEL_FIX, 14 * TILE, FLOOR2_Y - 32, 16, 32);
    lv->obj[cb4].target = (int16_t)cl4;
    /* ---- 5) AK5: Aunty откапывает ключ в стартовой комнате ---- */
    lv->obj_snow = add_obj(lv, O_SNOW, 18 * TILE, FLOOR3_Y - 14, 36, 14);
    lv->obj_key = add_obj(lv, O_KEY, 18 * TILE + 10, FLOOR3_Y - 16, 14, 16);
    lv->obj[lv->obj_snow].target = (int16_t)lv->obj_key; lv->obj[lv->obj_key].visible = 0; lv->obj[lv->obj_key].kind = 0;
    /* ---- 6) AD5 дверь B1|B2 ---- */
    lv->obj_door = add_obj(lv, O_DOOR, 51 * TILE, FLOOR1_Y - 112, 16, 112);
    lv->obj[lv->obj_door].kind = 0;
    /* ---- 7) DB6 (Domina) -> DL6 лифт T->M ---- */
    int dl6 = add_lift_top(lv, 29 * TILE, FLOOR3_Y, FLOOR2_Y, 1, LK_REST_TOP);
    int db6 = add_obj(lv, O_PANEL_BRIDGE, 42 * TILE, FLOOR3_Y - 32, 16, 32);
    lv->obj[db6].target = (int16_t)dl6;
    /* ---- 8) CB7 (Cyberbum) -> CL7 лифт M->B ---- */
    int cl7 = add_lift_top(lv, 29 * TILE, FLOOR2_Y, FLOOR1_Y, 1, LK_REST_TOP);
    int cb7 = add_obj(lv, O_PANEL_FIX, 69 * TILE, FLOOR1_Y - 32, 16, 32);
    lv->obj[cb7].target = (int16_t)cl7;
    /* ---- 9) PB8 (Pengi, за лазом PS) -> PL8 лифт M->B справа ---- */
    int pl8 = add_lift_top(lv, 93 * TILE, FLOOR2_Y, FLOOR1_Y, 1, LK_REST_TOP);
    int pb8 = add_obj(lv, O_BUTTON, 70 * TILE, FLOOR3_Y - 16, 14, 16);
    lv->obj[pb8].target = (int16_t)pl8;
    /* ---- 10) PW9 ледяная стена B2|B3 (Pengi) ---- */
    add_obj(lv, O_WALL_ICE, 76 * TILE, FLOOR1_Y - 112, 16, 112);
    /* ---- 11) AK10: Aunty, ключ от AD10 ---- */
    int as10 = add_obj(lv, O_SNOW, 82 * TILE, FLOOR1_Y - 14, 36, 14);
    int key10 = add_obj(lv, O_KEY, 82 * TILE + 10, FLOOR1_Y - 16, 14, 16);
    lv->obj[as10].target = (int16_t)key10; lv->obj[key10].visible = 0; lv->obj[key10].kind = 1;
    /* ---- 12) DB11 (Domina) -> DF11 скрытый пол ряда T справа ---- */
    int df11 = add_obj(lv, O_BRIDGE, 77 * TILE, FLOOR3_Y, 14 * TILE, 8);
    lv->obj[df11].need = 1; make_art(&lv->obj[df11], 2);
    int db11 = add_obj(lv, O_PANEL_BRIDGE, 86 * TILE, FLOOR2_Y - 32, 16, 32);
    lv->obj[db11].target = (int16_t)df11;
    /* ---- 13) gp12 на полу у DB11; RB12 на ПОТОЛКЕ (низ плиты FLOOR3), не на DF11 ---- */
    int rl12 = add_lift_top(lv, 93 * TILE, FLOOR3_Y, FLOOR2_Y, 1, LK_REST_TOP);
    /* панель антиграв: несущий пол FLOOR2 рядом с Domina DB11 */
    int gp12 = add_obj(lv, O_PANEL_FIX, 88 * TILE, FLOOR2_Y - 32, 16, 32);
    lv->obj[gp12].kind = 3; lv->obj[gp12].target = (int16_t)rl12;
    /* кнопка Refreezer: нижняя сторона несущей плиты FLOOR3 (потолок комнаты FLOOR2).
       В режиме обратной гравитации Refreezer идёт по потолку и нажимает её. */
    int rb12 = add_obj(lv, O_BUTTON, 88 * TILE, FLOOR3_Y + 2, 14, 16);
    lv->obj[rb12].target = (int16_t)rl12; lv->obj[rb12].kind = 3;
    /* ---- 14) AD10 дверь C|D ряд T (ключ 2) ---- */
    int ad10 = add_obj(lv, O_DOOR, 76 * TILE, FLOOR3_Y - 112, 16, 112);
    lv->obj[ad10].kind = 1;
    /* ---- 15) NP13: Nerd ломает платформу над залом босса ---- */
    add_plat(lv, 56 * TILE, FLOOR3_Y, 48, PK_NERD, 0);
    /* ---- 16) после босса: артефакт, лифт в зал, EXIT ---- */
    int dbosslt = add_lift_top(lv, 62 * TILE, FLOOR3_Y, FLOOR2_Y, 1, LK_ONCE_CYCLE);
    lv->obj_artifact = add_obj(lv, O_ARTIFACT, 0, 0, 14, 16);
    lv->obj[lv->obj_artifact].target = (int16_t)dbosslt;
    lv->obj_exit = add_obj(lv, O_EXIT, 40 * TILE, FLOOR2_Y - 48, 24, 48);

    static const int sp3[][2] = {
        {10, 304}, {20, 304}, {36, 304}, {46, 304}, {60, 304}, {70, 304}, {92, 304}, {97, 304},
        {7, 432}, {33, 432}, {80, 432}, {88, 432}, {97, 432},
        {6, 560}, {14, 560}, {22, 560}, {33, 560}, {46, 560}, {56, 560}, {64, 560}, {72, 560}, {80, 560}, {90, 560}, {97, 560}
    };
    for (unsigned i = 0; i < sizeof sp3 / sizeof sp3[0]; i++) spawn_at(lv, sp3[i][0], sp3[i][1]);
    for (int i = 0; i < 8; i++) { lv->hero_x[i] = 170 + i * 16; lv->hero_y[i] = FLOOR3_Y; }
    lv->arena = (Arena){592, 1040, FLOOR2_Y, FLOOR3_Y + 24, 600, 1032, 0, 0};
    add_plat(lv, 760, FLOOR2_Y - 44, 56, PK_STATIC, 0);
    boss_pool(lv);
}


/* Тестовые уровни 4..9: две комнаты — слева с респауном врагов, справа зал босса. Проход - низкая арка. */
static void build_leveltest(Level *lv) {
    const int RM = 27;
    memset(T, 0, sizeof T);
    fill(0, 3, 1, 36, 2); fill(99, 3, 100, 36, 2);
    fill(1, 3, 99, 11, 2); fill(1, 11, 99, 12, 1);
    fill(1, RM, 99, RM + 1, 2); fill(1, 35, 99, 36, 1);
    fill(50, RM + 1, 51, RM + 4, 2);                              /* арка между комнатами: проход 4 тайла */
    lv->nobj = 0; lv->nsp = 0;
    lv->obj_artifact = add_obj(lv, O_ARTIFACT, 0, 0, 14, 16);
    lv->obj[lv->obj_artifact].target = -1;
    lv->obj_exit = add_obj(lv, O_EXIT, 94 * TILE, FLOOR1_Y - 48, 24, 48);
    static const int spt[] = {6, 14, 22, 30, 38, 46};
    for (unsigned i = 0; i < sizeof spt / sizeof spt[0]; i++) spawn_at(lv, spt[i], FLOOR1_Y);
    for (int i = 0; i < 8; i++) { lv->hero_x[i] = 60 + i * 16; lv->hero_y[i] = FLOOR1_Y; }
    lv->arena = (Arena){53 * TILE, 98 * TILE, FLOOR1_Y, FLOOR2_Y + 24, 52 * TILE + 8, 98 * TILE - 8, 0, 0};
    boss_pool(lv);
}

/* ===================================================================== загрузка */
static int try_load(Image *im, const char *dir, const char *name) {
    char p[512];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    if (image_load_bmp(im, p)) { fprintf(stderr, "[level] loaded %s\n", p); return 1; }
    return 0;
}

static Image fallback_bg(void) {
    Image im = image_create(VIEW_W, VIEW_H);
    for (int y = 0; y < VIEW_H; y++)
        for (int x = 0; x < VIEW_W; x++)
            im.px[y * VIEW_W + x] = rgb(30 + y / 3, 50 + y / 2, 100 + y / 2);
    return im;
}


/* ---- LHMP loader (v1): maps/levelN.map рядом с assets или в assets/maps ---- */

/* checkpoints по схемам автора (жёлтые треугольники 1S..NS): порядок = последовательность квестовой цепочки.
   Если уровень загружен из .map - берутся точки из файла (редактор карт), эта таблица - запасная. */
static void default_checkpoints(Level *lv, int map_id) {
    static const int T1[][2] = { {48, 560}, {480, 560}, {1440, 432} };
    static const int T2[][2] = { {40, 560}, {864, 432}, {876, 176}, {860, 304}, {1290, 560} };
    static const int T3[][2] = { {170, 304}, {187, 560}, {555, 304}, {1240, 560}, {1365, 304} };
    const int (*t)[2] = NULL; int n = 0;
    if (map_id == 1) { t = T1; n = 3; } else if (map_id == 2) { t = T2; n = 5; } else if (map_id == 3) { t = T3; n = 5; }
    lv->ncp = 0;
    for (int i = 0; i < n; i++) { lv->cp_x[i] = t[i][0]; lv->cp_y[i] = t[i][1]; lv->ncp++; }
}

static int lhmp_open_path(char *out, size_t n, const char *dir, int map_id) {
    snprintf(out, n, "%s/maps/level%d.map", dir, map_id);
    { FILE *f = fopen(out, "rb"); if (f) { fclose(f); return 1; } }
    snprintf(out, n, "%s/level%d.map", dir, map_id);
    { FILE *f = fopen(out, "rb"); if (f) { fclose(f); return 1; } }
    snprintf(out, n, "maps/level%d.map", map_id);
    { FILE *f = fopen(out, "rb"); if (f) { fclose(f); return 1; } }
    return 0;
}

static int level_load_lhmp(Level *lv, const char *dir, int map_id) {
    char path[512];
    if (!lhmp_open_path(path, sizeof path, dir, map_id)) return 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    LhmpHeader hdr;
    if (fread(&hdr, 1, sizeof hdr, f) != sizeof hdr) { fclose(f); return 0; }
    if (hdr.magic != LHMP_MAGIC || hdr.version != LHMP_VERSION) {
        fprintf(stderr, "[lhmp] bad header %s\n", path);
        fclose(f); return 0;
    }
    memset(T, 0, sizeof T);
    lv->nobj = 0; lv->nsp = 0; memset(lv->room_type, 0, sizeof lv->room_type);
    lv->obj_door = lv->obj_exit = lv->obj_artifact = lv->obj_key = lv->obj_snow = lv->obj_crate = -1;
    lv->obj_bbtn[0] = lv->obj_bbtn[1] = -1;
    lv->obj_bplat[0] = lv->obj_bplat[1] = lv->obj_bplat[2] = -1;

    int tw = hdr.width, th = hdr.height;
    size_t cells = (size_t)tw * (size_t)th;
    uint8_t *layer = (uint8_t *)malloc(cells);
    if (!layer || fread(layer, 1, cells, f) != cells) { free(layer); fclose(f); return 0; }
    for (int ty = 0; ty < th && ty < MAP_H; ty++)
        for (int tx = 0; tx < tw && tx < MAP_W; tx++)
            T[ty][tx] = layer[ty * tw + tx];
    free(layer);

    if (hdr.flags & LHMP_FLAG_MASK) {
        uint8_t *mask = (uint8_t *)malloc(cells);
        if (!mask || fread(mask, 1, cells, f) != cells) { free(mask); fclose(f); return 0; }
        /* mask applied after Level.mask alloc in level_load; stash in T already solid */
        free(mask);
    }

    for (int i = 0; i < hdr.n_obj && i < MAX_OBJS; i++) {
        LhmpObj raw;
        if (fread(&raw, 1, sizeof raw, f) != sizeof raw) { fclose(f); return 0; }
        if (raw.type > O_CRATE) { fprintf(stderr, "[lhmp] type %d not supported by the engine yet - skipped (obj %d)\n", raw.type, i); continue; }
        if (raw.type == O_PLAT && raw.kind > 3) raw.kind = 0;                 /* будущие виды мини-платформ пока как статичные */
        int idx = add_obj(lv, raw.type, raw.x, raw.y, raw.w, raw.h);
        if (idx < 0) break;
        Obj *o = &lv->obj[idx];
        o->kind = raw.kind;
        o->solid = (raw.flags & 1) ? 1 : 0;
        o->visible = (raw.flags & 2) ? 1 : 0;
        o->hero_only = (raw.flags & 4) ? 1 : 0;
        o->need = raw.need;
        o->x0 = raw.x0; o->y0 = raw.y0; o->x1 = raw.x1; o->y1 = raw.y1;
        o->target = raw.target;
        o->dir = (raw.dir == 255) ? -1 : (int8_t)raw.dir;
        o->fx_ = INT_FX(o->x); o->fy_ = INT_FX(o->y);
        if (o->type == O_LIFT) {
            o->state = LS_BOTTOM;
            make_art(o, 1);
        } else if (o->type == O_PLAT) {
            make_art(o, 0);
        } else if (o->type == O_BRIDGE) {
            make_art(o, 2);
        }
        switch (o->type) {
        case O_DOOR: if (lv->obj_door < 0) lv->obj_door = idx; break;
        case O_EXIT: lv->obj_exit = idx; break;
        case O_ARTIFACT: lv->obj_artifact = idx; break;
        case O_KEY: if (lv->obj_key < 0) lv->obj_key = idx; break;
        case O_SNOW: lv->obj_snow = idx; break;
        case O_CRATE: lv->obj_crate = idx; break;
        default: break;
        }
    }

    for (int i = 0; i < hdr.n_spawn && i < MAX_SPAWNS; i++) {
        int16_t xy[2];
        if (fread(xy, 2, 2, f) != 2) break;
        lv->sp[lv->nsp].x = xy[0]; lv->sp[lv->nsp].y = xy[1];
        lv->nsp++;
    }
    for (int r = 0; r < 12; r++) lv->room_type[r] = (hdr.reserved[r >> 1] >> ((r & 1) * 4)) & 15;   /* типы комнат: по nibble на комнату */
    lv->ncp = 0;
    for (int i = 0; i < hdr.n_cp; i++) {
        int16_t xy[2];
        if (fread(xy, 2, 2, f) != 2) break;
        if (lv->ncp < 8) { lv->cp_x[lv->ncp] = xy[0]; lv->cp_y[lv->ncp] = xy[1]; lv->ncp++; }
    }
    for (int i = 0; i < hdr.n_hero && i < 8; i++) {
        int16_t xy[2];
        if (fread(xy, 2, 2, f) != 2) break;
        lv->hero_x[i] = xy[0]; lv->hero_y[i] = xy[1];
    }
    for (int i = hdr.n_hero; i < 8; i++) {
        lv->hero_x[i] = lv->hero_x[0]; lv->hero_y[i] = lv->hero_y[0];
    }
    /* quest index list — skip */
    for (int i = 0; i < hdr.n_quest; i++) {
        uint16_t q; if (fread(&q, 2, 1, f) != 1) break; (void)q;
    }

    lv->arena = (Arena){
        hdr.arena_x0, hdr.arena_x1, hdr.arena_floor, hdr.arena_ceil,
        hdr.arena_bx0, hdr.arena_bx1, 0, 0
    };
    fclose(f);
    boss_pool(lv);
    fprintf(stderr, "[lhmp] loaded %s (%d objs, %d spawns)\n", path, lv->nobj, lv->nsp);
    return 1;
}


int level_load(Level *lv, const char *dir, int map_id) {
    memset(lv, 0, sizeof *lv);
    lv->map_id = map_id; g_map2 = (map_id == 2 || map_id == 3); g_map3 = (map_id == 3); g_maptest = (map_id >= 4);
    if (map_id >= 4) {
        char bn[16]; snprintf(bn, sizeof bn, "bg%d.bmp", map_id);
        if (!try_load(&lv->bg, dir, bn)) { if (!try_load(&lv->bg, dir, "bg3.bmp")) lv->bg = fallback_bg(); }
    } else if (map_id == 3) {
        if (!try_load(&lv->bg, dir, "bg3.bmp")) {
            if (!try_load(&lv->bg, dir, "bg2.bmp")) {
                if (!try_load(&lv->bg, dir, "bg.bmp")) lv->bg = fallback_bg();
            }
        }
    } else if (map_id == 2) {
        if (!try_load(&lv->bg, dir, "bg2.bmp")) {
            if (!try_load(&lv->bg, dir, "bg.bmp")) lv->bg = fallback_bg();
        }
    } else {
        if (!try_load(&lv->bg, dir, "bg.bmp")) lv->bg = fallback_bg();
    }
    if (!try_load(&lv->crate, dir, "crate.bmp")) lv->crate = (Image){0, 0, NULL};
    lv->w = WORLD_W; lv->h = WORLD_H;
    /* LHMP: только при LHMP=1 — карты ещё неполные (лестницы/шахты). По умолчанию hardcode. */
    /* уровни 1-3: .map только при LHMP=1 (по умолчанию встроенные); уровни 4+: .map используется всегда, если он есть */
    if ((getenv("LHMP") || map_id >= 4) && level_load_lhmp(lv, dir, map_id)) {
        if (map_id >= 4) g_maptest = 0;                 /* настоящая карта: обычная сетка комнат, а не тестовая на 2 комнаты */
        fprintf(stderr, "[level] using LHMP for map %d\n", map_id);
    }
    else {
        lv->ncp = 0;
        build_level(lv);
    }
    if (lv->ncp <= 0) default_checkpoints(lv, map_id);
    lv->mask = (uint8_t *)calloc((size_t)lv->w * lv->h, 1);
    for (int y = 0; y < lv->h; y++)
        for (int x = 0; x < lv->w; x++) lv->mask[y * lv->w + x] = T[y / TILE][x / TILE] != 0;
    Image fg = {0, 0, NULL};
    if (try_load(&fg, dir, "fg.bmp") && fg.w == lv->w && fg.h == lv->h) {     /* свой передний план: коллизии по его альфе */
        lv->fg = fg;
        for (int i = 0; i < lv->w * lv->h; i++) lv->mask[i] = (fg.px[i] >> 24) >= 128;
    } else {
        if (fg.px) image_free(&fg);
        lv->fg = image_create(lv->w, lv->h);
        gen_fg(lv);
    }
    return lv->bg.px && lv->fg.px && lv->mask;
}

void level_free(Level *lv) {
    for (int i = 0; i < lv->nobj; i++) { free(lv->obj[i].alb); free(lv->obj[i].nrm); }
    image_free(&lv->bg); image_free(&lv->fg); if (lv->crate.px) image_free(&lv->crate);
    free(lv->mask);
    memset(lv, 0, sizeof *lv);
}

int level_save_fg(const Level *lv, const char *path) { return image_save_bmp(&lv->fg, path); }

int level_stage(int px) {
    if (g_maptest) return px < 50 * TILE ? 0 : 3;
    if (g_map3) return px < 26 * TILE ? 0 : px < 51 * TILE ? 1 : px < 76 * TILE ? 2 : 3;
    if (g_map2) return px < 33 * TILE ? 0 : px < 66 * TILE ? 1 : 2;
    if (px < 28 * TILE) return 0;
    if (px < 62 * TILE) return 1;
    if (px < 84 * TILE) return 2;
    return 3;
}

/* Комнаты 0..11:
     0  4  8   ← этаж 4 (верх)
     1  5  9
     2  6 10
     3  7 11   ← этаж 1 (низ)
   колонки: x < 33*TILE | 33..66 | >=66  */
int level_room(int px, int py) {
    if (g_maptest) return (px < 50 * TILE ? 0 : 3) + 2;
    if (g_map3) {
        int c = px < 26 * TILE ? 0 : px < 51 * TILE ? 1 : px < 76 * TILE ? 2 : 3;
        int r = py > FLOOR2_Y + 24 ? 2 : py > FLOOR3_Y + 24 ? 1 : 0;
        return c * 3 + r;
    }
    int col = px < 33 * TILE ? 0 : (px < 66 * TILE ? 1 : 2);
    int fl; /* 0=низ (floor1), 3=верх (floor4) */
    if (py > FLOOR2_Y + 24) fl = 0;
    else if (py > FLOOR3_Y + 24) fl = 1;
    else if (py > FLOOR4_Y + 24) fl = 2;
    else fl = 3;
    return col * 4 + (3 - fl);
}

/* ===================================================================== запросы */
static int mask_rect(const Level *lv, int x0, int y0, int x1, int y1) {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= lv->w) x1 = lv->w - 1;
    if (y1 >= lv->h) y1 = lv->h - 1;
    for (int y = y0; y <= y1; y++) {
        const uint8_t *row = lv->mask + y * lv->w;
        for (int x = x0; x <= x1; x++) if (row[x]) return 1;
    }
    return 0;
}

static int g_mir_on, g_mir_m;
void level_set_mirror(int on, int m) { g_mir_on = on; g_mir_m = m; }

int level_solid_rect_team(const Level *lv, int x0, int y0, int x1, int y1, int team) {
    if (g_mir_on) { int a = g_mir_m - y1, b = g_mir_m - y0; y0 = a; y1 = b; }
    if (x0 > x1 || y0 > y1) return 0;
    if (x0 < 0 || x1 >= lv->w) return 1;
    if (y1 < 0 || y0 >= lv->h) return 0;
    if (mask_rect(lv, x0, y0, x1, y1)) return 1;              /* стены, полы этажей — всегда */
    /* Платформы / лифты / мосты: односторонние.
       Стоять можно (ноги на верхней грани ±2px) — и на статичных, и на движущихся.
       Телом/боком/снизу — пролетаем насквозь, пока ноги не касаются поверхности. */
    int feet_only = (y0 == y1);
    for (int i = 0; i < lv->nobj; i++) {
        const Obj *o = &lv->obj[i];
        if (!o->solid || (o->hero_only && team != 0)) continue;
        if (x1 < o->x || x0 >= o->x + o->w || y1 < o->y || y0 >= o->y + o->h) continue;
        if (o->type == O_PLAT || o->type == O_LIFT || o->type == O_BRIDGE) {
            if (!feet_only) continue;
            int top = g_mir_on ? (o->y + o->h - 1) : o->y;
            /* ноги на верхней грани: допуск 0..3px ниже (приземление / движущаяся платформа) */
            if (g_mir_on ? (y0 > top || y0 < top - 3) : (y0 < top || y0 > top + 3)) continue;
        }
        /* двери/стены после разрушения (on) уже solid=0 — сюда не попадают */
        return 1;
    }
    return 0;
}
int level_solid_rect(const Level *lv, int x0, int y0, int x1, int y1) { return level_solid_rect_team(lv, x0, y0, x1, y1, 1); }

int level_stand_obj(const Level *lv, int x0, int x1, int py) {
    for (int i = 0; i < lv->nobj; i++) {
        const Obj *o = &lv->obj[i];
        if (!o->solid || x1 < o->x || x0 >= o->x + o->w) continue;
        if (g_mir_on) { int r = g_mir_m - py, top = o->y + o->h - 1; if (r <= top && r >= top - 3) return i; continue; }
        if (py >= o->y && py <= o->y + 3) return i;
    }
    return -1;
}

void level_restore_platforms(Level *lv) {
    for (int i = 0; i < lv->nobj; i++) {
        Obj *o = &lv->obj[i];
        if (o->type != O_PLAT || o->kind != PK_COLLAPSE) continue;
        o->state = CS_IDLE;
        o->timer = 0;
        o->solid = 1;
        o->visible = 1;
        o->vy_ = 0;
        o->fy_ = INT_FX(o->y0);
        o->y = (int16_t)o->y0;
    }
}

void level_power(Level *lv, int idx) {
    if (idx < 0 || idx >= lv->nobj) return;
    if (lv->obj[idx].have < 250) lv->obj[idx].have++;
}

/* ===================================================================== обновление */
static void obj_place(Obj *o) { o->x = (int16_t)FX_INT(o->fx_); o->y = (int16_t)FX_INT(o->fy_); }

void level_update(Level *lv) {
    lv->frame++;
    for (int i = 0; i < lv->nobj; i++) {
        Obj *o = &lv->obj[i];
        int ox = o->x, oy = o->y;
        switch (o->type) {
        case O_PLAT:
            if (o->kind == PK_MOVER) {
                if (o->timer > 0) o->timer--;
                else {
                    o->fx_ += o->dir * FXF(0.6);
                    if (o->dir > 0 && FX_INT(o->fx_) >= o->x1) { o->fx_ = INT_FX(o->x1); o->dir = -1; o->timer = 30; }
                    if (o->dir < 0 && FX_INT(o->fx_) <= o->x0) { o->fx_ = INT_FX(o->x0); o->dir = 1; o->timer = 30; }
                }
            } else if (o->kind == PK_COLLAPSE) {
                switch (o->state) {
                case CS_IDLE: if (lv->occ[i]) { o->state = CS_SHAKE; o->timer = COLLAPSE_FRAMES; } break;
                case CS_SHAKE:
                    if (--o->timer <= 0) { o->state = CS_FALL; o->solid = 0; o->vy_ = 0; }
                    break;
                case CS_FALL:
                    o->vy_ += FXF(0.3); o->fy_ += o->vy_;
                    if (FX_INT(o->fy_) > o->y0 + 120) { o->state = CS_WAIT; o->timer = COLLAPSE_RESPAWN; o->visible = 0; }
                    break;
                case CS_WAIT:
                    if (--o->timer <= 0) { o->state = CS_RISE; o->visible = 1; o->fy_ = INT_FX(o->y0 + 48); }
                    break;
                case CS_RISE:
                    o->fy_ -= FXF(1.0);
                    if (FX_INT(o->fy_) <= o->y0) { o->fy_ = INT_FX(o->y0); o->state = CS_IDLE; o->solid = 1; }
                    break;
                }
            } else if (o->kind == PK_NERD) {
                /* ломается только супером Nerd (state: 0 idle, 1 shake, 2 gone) */
                if (o->state == 1) {
                    if (--o->timer <= 0) {
                        o->state = 2; o->solid = 0; o->visible = 0;
                        /* если бак (target) стоял на этой платформе — дать ему упасть */
                        if (o->target >= 0 && o->target < lv->nobj) {
                            Obj *c = &lv->obj[o->target];
                            if (c->type == O_CRATE && c->kind == 1 && c->state == 0)
                                level_power(lv, o->target);
                        }
                    }
                }
            }
            break;
        case O_LIFT: {
            int powered = o->have >= o->need;
            fx spd = LIFT_SPEED;
            int dwell = LIFT_DWELL;
            #define LIFT_SIG 90
            /* BFS: цепочка шахты (общий x + стык этажей) */
            int mates[8], nm = 0;
            int seen[MAX_OBJS];
            for (int z = 0; z < lv->nobj; z++) seen[z] = 0;
            mates[nm++] = i; seen[i] = 1;
            for (int qi = 0; qi < nm; qi++) {
                Obj *a = &lv->obj[mates[qi]];
                int ax = a->x + a->w / 2;
                for (int j = 0; j < lv->nobj && nm < 8; j++) {
                    if (seen[j]) continue;
                    Obj *b = &lv->obj[j];
                    if (b->type != O_LIFT) continue;
                    if (ABSI((b->x + b->w / 2) - ax) > 48) continue;
                    if (ABSI(a->y0 - b->y1) > 8 && ABSI(a->y1 - b->y0) > 8) continue;
                    seen[j] = 1; mates[nm++] = j;
                }
            }
            int on_list[8], non = 0, min_on = 9999;
            for (int m = 0; m < nm; m++) {
                Obj *b = &lv->obj[mates[m]];
                int on = (b->have >= b->need) && !(b->kind >= 2 && b->progress);
                if (!on) continue;
                on_list[non++] = mates[m];
                if (mates[m] < min_on) min_on = mates[m];
            }
            if (o->timer < 0) {
                o->timer++;
                if (o->timer == 0) {
                    if (o->dir > 0) o->state = LS_UP;
                    else if (o->dir < 0) o->state = LS_DOWN;
                    o->dir = 0;
                }
                break;
            }
            if (o->state == LS_UP) {
                o->fy_ -= spd;
                if (FX_INT(o->fy_) <= o->y1) {
                    o->fy_ = INT_FX(o->y1); o->state = LS_TOP;
                    o->timer = dwell;
                    if (o->kind == 1) o->progress = 0;
                }
                break;
            }
            if (o->state == LS_DOWN) {
                o->fy_ += spd;
                if (FX_INT(o->fy_) >= o->y0) {
                    o->fy_ = INT_FX(o->y0); o->state = LS_BOTTOM;
                    if (o->kind >= 2) o->progress = 1;
                    o->timer = (o->kind == 2) ? 0 : dwell;
                }
                break;
            }
            if (o->timer > 0) { o->timer--; break; }
            if (!powered) break;
            if (o->kind >= 2 && o->progress) break;
            if (o->kind == 2 && o->state == LS_BOTTOM) break;

            /* SINGLE: один ON — полный цикл туда-обратно */
            if (non <= 1) {
                if (o->state == LS_TOP) { o->dir = -1; o->timer = -LIFT_SIG; }
                else if (o->state == LS_BOTTOM) { o->dir = 1; o->timer = -LIFT_SIG; }
                break;
            }
            /* MULTI: лидер синхронизирует всех ON */
            if (i != min_on) break;
            {
                int all_idle = 1, all_top = 1, all_bot = 1;
                for (int m = 0; m < non; m++) {
                    Obj *b = &lv->obj[on_list[m]];
                    if (b->state == LS_UP || b->state == LS_DOWN || b->timer != 0) all_idle = 0;
                    if (b->state != LS_TOP) all_top = 0;
                    if (b->state != LS_BOTTOM) all_bot = 0;
                }
                if (!all_idle) break;
                int dir = 0;
                if (all_top) dir = -1;
                else if (all_bot) dir = 1;
                else {
                    /* смешанные: выровнять по лидеру */
                    int lead_top = (o->state == LS_TOP);
                    for (int m = 0; m < non; m++) {
                        Obj *b = &lv->obj[on_list[m]];
                        if (lead_top) {
                            b->fy_ = INT_FX(b->y1); b->y = (int16_t)b->y1; b->state = LS_TOP; b->timer = 0;
                        } else {
                            b->fy_ = INT_FX(b->y0); b->y = (int16_t)b->y0; b->state = LS_BOTTOM; b->timer = 0;
                        }
                    }
                    dir = lead_top ? -1 : 1;
                }
                for (int m = 0; m < non; m++) {
                    Obj *b = &lv->obj[on_list[m]];
                    if (b->kind == 2 && dir > 0) continue;
                    b->dir = (int8_t)dir;
                    b->timer = -LIFT_SIG;
                }
            }
            break; }
        case O_CRATE:                                   /* kind0: платформа поднимается (ур.1); kind1: бак падает вниз (ур.2 TR) */
            if (o->kind == 1) {
                if (o->have >= o->need && o->state == 0) { o->state = 1; o->solid = 0; o->on = 1; o->vy_ = 0; }
                if (o->state == 1) {
                    o->vy_ += FXF(0.35); o->fy_ += o->vy_;
                    if (FX_INT(o->fy_) + o->h >= o->y0) {
                        o->fy_ = INT_FX(o->y0 - o->h);
                        o->state = 2;
                        if (o->target >= 0) {
                            level_power(lv, o->target);   /* UOB */
                        }
                    }
                }
            } else if (o->have >= o->need) {
                o->on = 1;
                if (FX_INT(o->vy_) > o->y1) { o->vy_ -= LIFT_SPEED; if (FX_INT(o->vy_) < o->y1) o->vy_ = INT_FX(o->y1); }
            }
            if (o->kind != 1) o->h = (int16_t)(FX_INT(o->vy_) - o->y);
            break;
        case O_BRIDGE:
            if (!o->on && o->have >= o->need) {
                o->on = 1; o->solid = 1; o->visible = 1; o->timer = 0;
                /* зафиксировать Y на плите этажа (проём в тайлах) */
                o->fy_ = INT_FX(o->y); o->fx_ = INT_FX(o->x);
            }
            if (o->on) {
                o->solid = 1; o->visible = 1;
                if (o->timer < 30) o->timer++;
            }
            break;
        case O_DOOR: case O_WALL_ICE: case O_WALL_WOOD: case O_WALL_STONE: case O_WALL_FIRE:
            if (o->on) { o->solid = 0; if (o->timer < 40) o->timer++; }      /* timer - анимация разрушения */
            break;
        default: break;
        }
        obj_place(o);
        o->dx = (int16_t)(o->x - ox); o->dy = (int16_t)(o->y - oy);
    }
    memset(lv->occ, 0, sizeof lv->occ);
}

/* ===================================================================== отрисовка */
static void draw_art(const Obj *o, Image *fb, int cx, int cy, int alpha) {
    int jx = 0;
    if (o->type == O_PLAT && o->kind == PK_COLLAPSE && o->state == CS_SHAKE) jx = ((o->timer / 2) & 1) ? 1 : -1;
    for (int v = 0; v < o->h; v++) {
        int y = o->y + v - cy;
        if (y < 0 || y >= fb->h) continue;
        for (int u = 0; u < o->w; u++) {
            int x = o->x + u - cx + jx;
            if (x < 0 || x >= fb->w) continue;
            const int8_t *n = &o->nrm[(v * o->w + u) * 3];
            int dot = n[0] * LIGHT_X + n[1] * LIGHT_Y + n[2] * LIGHT_Z;
            int sh = 84 + (dot > 0 ? dot * 190 / 16129 : 0);
            uint32_t c = o->alb[v * o->w + u];
            int r = (int)((c >> 16) & 255) * sh >> 8, g = (int)((c >> 8) & 255) * sh >> 8, b = (int)(c & 255) * sh >> 8;
            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;
            fb->px[y * fb->w + x] = px_blend(fb->px[y * fb->w + x], ARGB(alpha, r, g, b));
        }
    }
}

static void draw_rope(const Obj *o, Image *fb, int cx, int cy) {
    for (int v = 0; v < o->h; v++) {
        int y = o->y + v - cy;
        if (y < 0 || y >= fb->h) continue;
        for (int u = 0; u < o->w; u++) {
            int x = o->x + u - cx;
            if (x < 0 || x >= fb->w) continue;
            int knot = (v % 12) < 2;
            uint32_t c = knot ? rgb(120, 86, 48) : (((v / 3) & 1) ? rgb(186, 146, 90) : rgb(164, 124, 74));
            if (u == 0) c = rgb(100, 72, 40);
            fb->px[y * fb->w + x] = c;
        }
    }
    ui_rect(fb, o->x - cx - 2, o->y - cy - 2, o->w + 4, 3, 0xFF5A4630u);          /* крепление наверху */
}

static void draw_wall(const Obj *o, Image *fb, int cx, int cy, int frame) {
    int x0 = o->x - cx, y0 = o->y - cy, w = o->w, h = o->h;
    if (o->on) {                                                                   /* обломки оседают */
        int t = o->timer;
        for (int k = 0; k < 14; k++) {
            int px = x0 + (int)(hash2(k, o->x) % (uint32_t)w), fall = t * 3 + (int)(hash2(k, 3) % 6u);
            int py = y0 + h - 4 - (int)(hash2(k, o->y) % 8u) + (fall > 20 ? 0 : -fall / 2);
            ui_rect(fb, px, py, 3, 3, o->type == O_WALL_WOOD ? 0xFF7A5230u : o->type == O_WALL_ICE ? 0xFFBEE6F4u : 0xFF8A8A98u);
        }
        return;
    }
    switch (o->type) {
    case O_WALL_ICE:
        ui_rect(fb, x0, y0, w, h, 0xFF9AD0E6u);
        for (int y = 0; y < h; y += 16) ui_rect(fb, x0, y0 + y, w, 1, 0xFFE4F6FCu);
        for (int k = 0; k < 6; k++) ui_rect(fb, x0 + 2 + (int)(hash2(k, 1) % (uint32_t)(w - 6)), y0 + (int)(hash2(k, 2) % (uint32_t)(h - 8)), 2, 6, 0xFFF2FCFFu);
        ui_rect(fb, x0, y0, 2, h, 0xFFC8EEF8u); ui_rect(fb, x0 + w - 2, y0, 2, h, 0xFF5E98B4u);
        ui_frame(fb, x0, y0, x0 + w - 1, y0 + h - 1, 0xFF3C6E88u);
        break;
    case O_WALL_WOOD:
        for (int y = 0; y < h; y += 14) {
            ui_rect(fb, x0, y0 + y, w, 14, (y / 14) & 1 ? 0xFF8A5E36u : 0xFF7A5230u);
            ui_rect(fb, x0, y0 + y, w, 1, 0xFF4A3018u);
            ui_rect(fb, x0 + 3, y0 + y + 6, 2, 2, 0xFF2A1A0Eu); ui_rect(fb, x0 + w - 5, y0 + y + 6, 2, 2, 0xFF2A1A0Eu);
        }
        ui_frame(fb, x0, y0, x0 + w - 1, y0 + h - 1, 0xFF3A2410u);
        break;
    case O_WALL_STONE:
        for (int y = 0; y < h; y += 16) {
            ui_rect(fb, x0, y0 + y, w, 16, ((y / 16) & 1) ? 0xFF8C8A98u : 0xFF7E7C8Cu);
            ui_rect(fb, x0, y0 + y, w, 1, 0xFF4A4858u);
            ui_rect(fb, x0 + ((y / 16) & 1 ? 8 : 4), y0 + y, 1, 16, 0xFF4A4858u);
        }
        ui_frame(fb, x0, y0, x0 + w - 1, y0 + h - 1, 0xFF2E2C3Cu);
        break;
    case O_WALL_FIRE: {
        ui_rect(fb, x0 + 2, y0, w - 4, h, 0x802A0A00u);
        for (int y = h - 4; y > 0; y -= 5) {
            int ph = (frame * 2 + y * 3) % 24;
            int wob = (int)(sinf((float)(frame + y) * 0.35f) * 3.f);
            ui_disc(fb, x0 + w / 2 + wob, y0 + y, 6 - (y * 3 / h), ph < 12 ? 0xE0FF6A10u : 0xE0FF9A20u);
            ui_disc(fb, x0 + w / 2 + wob, y0 + y + 1, 3, 0xF0FFE060u);
        }
        break; }
    case O_DOOR: {                                                                 /* стальные ворота с замком */
        ui_rect(fb, x0, y0, w, h, 0xFF6A7080u);
        for (int y = 4; y < h; y += 10) ui_rect(fb, x0, y0 + y, w, 2, 0xFF3A3E4Au);
        ui_rect(fb, x0, y0, 2, h, 0xFF9AA0B0u);
        ui_rect(fb, x0 + w - 6, y0 + h / 2 - 5, 5, 8, 0xFFD8B030u);                 /* замок */
        ui_rect(fb, x0 + w - 5, y0 + h / 2 - 8, 3, 3, 0xFFA08020u);
        ui_frame(fb, x0, y0, x0 + w - 1, y0 + h - 1, 0xFF22242Cu);
        break; }
    default: break;
    }
}

static void draw_crate(const Level *lv, const Obj *o, Image *fb, int cx, int cy) {
    int bottom = o->y + o->h;                           /* низ невидимой платформы */
    if (lv->crate.px) {
        int x = o->x + o->w / 2 - lv->crate.w / 2 - cx, y = bottom - lv->crate.h - cy;
        ui_blit_keyed(fb, &lv->crate, x, y, 0);
    } else {
        ui_rect(fb, o->x - cx, bottom - 36 - cy, o->w, 36, 0xFF2A7A44u);
        ui_rect(fb, o->x - cx, bottom - 36 - cy, o->w, 6, 0xFF30343Cu);
    }
}

static void draw_misc(const Obj *o, Image *fb, int cx, int cy, int frame) {
    int x = o->x - cx, y = o->y - cy, w = o->w, h = o->h;
    switch (o->type) {
    case O_BUTTON:
        if (o->kind == 3) {
            /* Refreezer ceiling switch — «перевёрнута» на нижней стороне плиты */
            ui_rect(fb, x, y, w, 6, 0xFF505868u);
            ui_rect(fb, x + 2, y + (o->on ? 5 : 2), w - 4, o->on ? 2 : 5, o->on ? 0xFF40D060u : 0xFFE8C040u);
            ui_rect(fb, x + 1, y + 5, w - 2, 1, 0xFF9AA0B0u);
        } else {
            ui_rect(fb, x, y + h - 6, w, 6, 0xFF505868u);
            ui_rect(fb, x + 2, y + h - (o->on ? 7 : 10), w - 4, o->on ? 2 : 5, o->on ? 0xFF40D060u : 0xFFE04040u);
            ui_rect(fb, x + 1, y + h - 6, w - 2, 1, 0xFF9AA0B0u);
        }
        break;
    case O_PANEL_FIX:
        ui_rect(fb, x, y, w, h, 0xFF4A5060u); ui_frame(fb, x, y, x + w - 1, y + h - 1, 0xFF22242Cu);
        ui_rect(fb, x + 3, y + 4, w - 6, h - 10, 0xFF1A1E26u);
        if (o->kind == 3) {
            /* антиграв-панель: жёлтая стрелка вверх */
            ui_rect(fb, x + 4, y + 6, w - 8, 3, 0xFFFFE060u);
            ui_rect(fb, x + w / 2 - 1, y + 10, 3, h - 16, 0xFFFFE060u);
            ui_rect(fb, x + w / 2 - 4, y + 8, 9, 3, 0xFFFFE060u);
            ui_rect(fb, x + w / 2 - 3, y + 6, 7, 2, 0xFFFFF0A0u);
        } else if (o->on) { ui_rect(fb, x + 4, y + 6, w - 8, 3, 0xFF40D060u); ui_rect(fb, x + 4, y + 12, w - 8, 3, 0xFF40D060u); }
        else {
            ui_rect(fb, x + 4, y + 6, w - 8, 3, 0xFFD04030u);
            if ((frame / 6) % 4 == 0) ui_rect(fb, x + 5 + (frame / 6) % 6, y + 12, 2, 2, 0xFFFFE060u);
            ui_line(fb, x + 3, y + h - 6, x + w - 4, y + h - 3, 0xFF000000u);
        }
        if (o->progress > 0 && !o->on && o->kind != 3) { ui_rect(fb, x, y - 6, w, 4, 0xFF101018u); ui_rect(fb, x + 1, y - 5, (w - 2) * o->progress / REPAIR_FRAMES, 2, 0xFF40D060u); }
        break;
    case O_PANEL_BRIDGE: {
        ui_rect(fb, x, y, w, h, 0xFF402050u); ui_frame(fb, x, y, x + w - 1, y + h - 1, 0xFF1C0C28u);
        uint32_t glow = o->on ? 0xFFE060FFu : (((frame / 20) & 1) ? 0xFFA040C0u : 0xFF602880u);
        ui_disc(fb, x + w / 2, y + h / 2, 5, glow); ui_disc(fb, x + w / 2, y + h / 2, 2, 0xFFFFFFFFu);
        break; }
    case O_SNOW:
        if (o->on) break;
        for (int k = 0; k < w; k++) {
            int hh = (int)(sinf((float)k / (float)w * 3.14159f) * (float)(h - 1)) + 1;
            ui_rect(fb, x + k, y + h - hh, 1, hh, ((k + (int)(hash2(k, 4) % 3u)) & 3) == 0 ? 0xFFD6E4F2u : 0xFFF4F8FCu);
        }
        if (o->progress > 0) { ui_rect(fb, x, y - 8, w, 4, 0xFF101018u); ui_rect(fb, x + 1, y - 7, (w - 2) * o->progress / SWEEP_FRAMES, 2, 0xFF60C0F0u); }
        break;
    case O_KEY:
        if (!o->visible) break;
        {
            int bob = (int)(sinf((float)frame * 0.1f) * 2.f);
            ui_disc(fb, x + 5, y + 5 + bob, 5, 0xFFE8C030u); ui_disc(fb, x + 5, y + 5 + bob, 2, 0xFF6A5010u);
            ui_rect(fb, x + 9, y + 4 + bob, 8, 3, 0xFFE8C030u); ui_rect(fb, x + 13, y + 7 + bob, 2, 4, 0xFFE8C030u); ui_rect(fb, x + 16, y + 7 + bob, 2, 3, 0xFFE8C030u);
        }
        break;
    case O_ARTIFACT:
        if (!o->visible) break;
        {
            int bob = (int)(sinf((float)frame * 0.12f) * 3.f), pul = 7 + (frame / 8) % 3;
            ui_disc(fb, x + 7, y + 8 + bob, pul + 3, 0x4060E0FFu);
            ui_disc(fb, x + 7, y + 8 + bob, 7, 0xFF40A0F0u); ui_disc(fb, x + 5, y + 6 + bob, 3, 0xFFE0F8FFu);
            ui_frame(fb, x + 1, y + 2 + bob, x + 13, y + 14 + bob, 0x80FFFFFFu);
        }
        break;
    case O_EXIT: {
        int open = o->on;
        ui_rect(fb, x - 3, y - 4, w + 6, h + 4, 0xFF3A3E50u);
        ui_rect(fb, x, y, w, h, open ? 0xFF50E080u : 0xFF20242Eu);
        if (!open) { ui_rect(fb, x + w / 2 - 4, y + h / 2 - 4, 8, 9, 0xFFD04040u); ui_rect(fb, x + w / 2 - 2, y + h / 2 - 8, 4, 4, 0xFF902020u); }
        else for (int k = 0; k < 4; k++) ui_rect(fb, x + 3 + k * 5, y + 4, 2, h - 8, 0xFFC8FFD8u);
        ui_frame(fb, x - 3, y - 4, x + w + 2, y + h - 1, 0xFF14161EU);
        break; }
    default: break;
    }
}

void level_render(Level *lv, Image *fb, int cam_x, int cam_y) {
    /* передний план с альфой */
    for (int y = 0; y < fb->h; y++) {
        int wy = y + cam_y;
        if (wy < 0 || wy >= lv->fg.h) continue;
        for (int x = 0; x < fb->w; x++) {
            int wx = x + cam_x;
            if (wx < 0 || wx >= lv->fg.w) continue;
            uint32_t s = lv->fg.px[wy * lv->fg.w + wx];
            if (s >> 24) fb->px[y * fb->w + x] = px_blend(fb->px[y * fb->w + x], s);
        }
    }
    /* объекты: сначала фон (верёвки, панели), затем стены и платформы */
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < lv->nobj; i++) {
            Obj *o = &lv->obj[i];
            int ox = o->x - cam_x, oy = o->y - cam_y;
            if (ox > fb->w + 8 || ox + o->w < -8 || oy > fb->h + 8 || oy + o->h < -8) continue;
            if (pass == 0) {
                if ((o->type == O_BUTTON || o->type == O_ROPE) && !o->visible) continue;
                if (o->type == O_ROPE) draw_rope(o, fb, cam_x, cam_y);
                else if (o->type == O_BUTTON || o->type == O_PANEL_FIX || o->type == O_PANEL_BRIDGE || o->type == O_SNOW ||
                         o->type == O_KEY || o->type == O_ARTIFACT || o->type == O_EXIT) draw_misc(o, fb, cam_x, cam_y, lv->frame);
            } else {
                if (!o->visible) continue;
                if (o->type == O_PLAT || o->type == O_LIFT) {
                    draw_art(o, fb, cam_x, cam_y, 255);
                    /* сигнал: тонкая стрелка, 2 вспышки */
                    if (o->type == O_LIFT && o->timer < 0) {
                        int phase = (-o->timer) / 22;
                        if (phase == 0 || phase == 2) {
                            int cx = ox + o->w / 2, cy = oy - 12;
                            uint32_t col = 0xFFFFF0A0u;
                            if (o->dir > 0) { /* ↑ тонкая */
                                ui_rect(fb, cx, cy - 8, 1, 10, col);
                                ui_rect(fb, cx - 2, cy - 6, 5, 1, col);
                                ui_rect(fb, cx - 1, cy - 7, 3, 1, col);
                            } else { /* ↓ */
                                ui_rect(fb, cx, cy - 8, 1, 10, col);
                                ui_rect(fb, cx - 2, cy - 1, 5, 1, col);   /* зеркало стрелки вверх: острие внизу */
                                ui_rect(fb, cx - 1, cy + 0, 3, 1, col);
                            }
                        }
                    }
                }
                else if (o->type == O_BRIDGE) draw_art(o, fb, cam_x, cam_y, o->on ? 60 + o->timer * 195 / 30 : 0);
                else if (o->type >= O_DOOR && o->type <= O_WALL_FIRE) draw_wall(o, fb, cam_x, cam_y, lv->frame);
                else if (o->type == O_CRATE) draw_crate(lv, o, fb, cam_x, cam_y);
            }
        }
}
