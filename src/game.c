/* Игра: управление героями, головоломки, камера, фон с машинами, HUD. Враги/босс/снаряды - в ai.c. */
#include "game.h"
#include "game_priv.h"
extern int g_debug_quest_ini;
#include "events.h"
#include "sound.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Game G;

static const char *HERO_NAME[NUM_HEROES] = {"Pengi", "Cyberbum", "Aunty", "Domina", "Nerd", "Refreezer", "Batyscaphe"};
static int in_arena_zone(int x, int y);
static void stage_checkpoint(int px, int *ox, int *oy);
static void clamp_spawn_open(int *sx, int *sy);

static const int HERO_KIND[NUM_HEROES] = {RIG_PENGI, RIG_BUM, RIG_DOORKEEPER, RIG_MISTRESS, RIG_NERD, RIG_REFREEZER, RIG_HERO7};
static const char *HERO_FILE[NUM_HEROES] = {"pengi", "cyberbum", "aunty", "domina", "nerd", "refreezer", "batyscaphe"};
static const uint32_t HERO_COL[NUM_HEROES] = {0xFF60C0FFu, 0xFFF0A050u, 0xFFE070C0u, 0xFFD060FFu, 0xFF70D090u, 0xFFB0C8D8u, 0xFF4090D0u};
static uint8_t g_worked[MAX_OBJS];
static char g_hint[96];

/* ===================================================================== мелочи */
uint32_t g_rand(void) { G.rng ^= G.rng << 13; G.rng ^= G.rng >> 17; G.rng ^= G.rng << 5; return G.rng; }
void say(const char *s) { snprintf(G.msg, sizeof G.msg, "%s", s); G.msg_t = 180; }
Actor *player(void) { return &G.hero[G.cur]; }

void spark(int x, int y, int n, uint32_t col, int size) {
    for (int i = 0; i < n; i++)
        for (int k = 0; k < FX_MAX; k++) {
            Spark *s = &G.sp[k];
            if (s->active) continue;
            s->active = 1; s->life = s->max = 14 + (int)(g_rand() % 14u);
            s->x = INT_FX(x); s->y = INT_FX(y);
            s->vx = (fx)((int)(g_rand() % 600u) - 300); s->vy = -(fx)(g_rand() % 500u) - 60;
            s->col = col; s->size = size;
            break;
        }
}

int blocked_between(int x0, int x1, int y) {
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    for (int i = 0; i < G.lv.nobj; i++) {
        const Obj *o = &G.lv.obj[i];
        if (!(o->type == O_DOOR || (o->type >= O_WALL_ICE && o->type <= O_WALL_FIRE)) || o->on || !o->solid) continue;
        int cx = o->x + o->w / 2;
        if (cx > x0 && cx < x1 && y >= o->y && y < o->y + o->h) return 1;
    }
    return 0;
}

/* любая стена (тайлы, двери, разрушаемые стены) на уровне тела между двумя x; одно-сторонние платформы не в счёт */
/* одна непрерывная опора между x0 и x1 (микро-щели до 6 px не в счёт): игрок и герои на одной платформе */
int same_platform(int x0, int x1, int y) {
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    int run = 0;
    for (int x = x0; x <= x1; x += 2) {
        int ok = level_solid_rect_team(&G.lv, x, y, x, y, 0) || level_solid_rect_team(&G.lv, x, y + 1, x, y + 1, 0) ||
                 level_solid_rect_team(&G.lv, x, y - 1, x, y - 1, 0);
        if (ok) run = 0; else if ((run += 2) > 6) return 0;
    }
    return 1;
}

int wall_between(int x0, int x1, int y) {
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (blocked_between(x0, x1, y)) return 1;
    for (int x = x0 + 8; x < x1 - 4; x += 4)
        if (level_solid_rect_team(&G.lv, x, y - 20, x, y - 6, 0)) return 1;
    return 0;
}

void break_wall(Obj *o) {
    if (o->on) return;
    sound_play_sfx_at(SFX_EXPLODE, o->x, o->y);
    o->on = 1; o->timer = 0;
    uint32_t col = o->type == O_WALL_WOOD ? 0xFF8A5E36u : o->type == O_WALL_ICE ? 0xFFC8EEF8u : o->type == O_WALL_FIRE ? 0xFFFFB040u : 0xFF9A98A8u;
    spark(o->x + o->w / 2, o->y + o->h / 2, 30, col, 3);
    spark(o->x + o->w / 2, o->y + o->h - 10, 20, col, 2);
    G.shake = 14;
}

void hit_feedback(const Actor *a, int big) { (void)a; (void)big; }

/* ===================================================================== герои */
static void init_hero(int i) {
    Actor *h = &G.hero[i];
    actor_init(h, HERO_KIND[i], 0, G.lv.hero_x[i], G.lv.hero_y[i]);
    h->immortal = 0; /* все получают урон; AI не тратит жизни */
    h->post_x = G.lv.hero_x[i]; h->post_y = G.lv.hero_y[i];
}

static void switch_hero(int to) {
    if (to < 0 || to >= NUM_HEROES || to == G.cur) return;
    if (G.out[to] || G.lives[to] < 1) { say("No lives — wait for Refreezer"); return; }
    /* во время переноса вертолётом переключение запрещено */
    if (G.heli.on && (G.heli.mask & (1 << to))) { say("Hero is in transit"); return; }
    /* на арене босса: ровно один активный боец */
    if (G.boss_hide_allies) {
        Actor *p = &G.hero[G.cur];
        int px = FX_INT(p->x), py = FX_INT(p->y);
        Actor *h = &G.hero[to];
        for (int k = 0; k < NUM_HEROES; k++) G.hero[k].active = 0; /* все в запас */
        actor_init(h, HERO_KIND[to], 0, px, py);
        h->active = 1; h->invuln_t = 60; h->immortal = 0; h->hp = h->maxhp;
        h->post_x = px; h->post_y = py;
        G.cur = to;
        (void)p;
        sound_play_sfx(SFX_UI);
        char b[64]; snprintf(b, sizeof b, "Boss fighter: %s", HERO_NAME[to]); say(b);
        return;
    }
    if (G.hero[to].hp <= 0) return;
    Actor *o = &G.hero[G.cur];
    o->post_x = FX_INT(o->x); o->post_y = FX_INT(o->y); o->ai_t = 0;
    o->immortal = 0;
    /* sprinter_anchor: бывший игрок фиксирует свою группу */
    G.sprinter[G.cur] = 1;
    G.lost_t[G.cur] = 0;
    {
        int og = G.hero_gid[G.cur];
        if (og >= 0 && og < 7) {
            G.g_leader[og] = G.cur;
            G.g_floor[og] = FX_INT(o->y);
            G.g_active[og] = 1;
        }
    }
    G.cur = to;
    G.pl_gy = FX_INT(G.hero[to].y);
    G.hero[to].immortal = 0;
    G.sprinter[to] = 0;
    sound_play_sfx(SFX_UI);
    char b[72]; snprintf(b, sizeof b, "Control: %s | %s = sprinter (g%d)", HERO_NAME[to], HERO_NAME[o - G.hero], G.hero_gid[o - G.hero]);
    say(b);
}

static int room_explored(int r) {
    if (r < 0 || r >= 12) return 0;
    return G.explored[r];
}
/* закрыть комнату (group off / падение) — герои штатно туда не телепортятся */
static void close_room_at(int px, int py) {
    int r = level_room(px, py);
    if (r >= 0 && r < 12) G.explored[r] = 0;
}
/* открыть комнату только штатным входом: на земле/лифте/мосту, не stealth */
static void mark_explored(int px, int py) {
    int r = level_room(px, py);
    if (r < 0 || r >= 12) return;
    Actor *p = player();
    if (!p || p->hp <= 0) return;
    /* stealth / нештатные режимы — комнату НЕ открываем */
    if (G.domina_stealth) return;
    if (p->kind == RIG_MISTRESS && p->spin_t > 0) return;
    if (p->kind == RIG_PENGI && p->sliding) return;
    if (p->kind == RIG_REFREEZER && p->misc > 0 && p->flip) return;
    if (!actor_on_ground(p, &G.lv) && p->vy > FXF(3.0)) return; /* freefall */
    if (p->kind == RIG_HERO7 && G.lv.water_y > 0 && FX_INT(p->x) >= G.lv.water_x0 && FX_INT(p->x) <= G.lv.water_x1 && FX_INT(p->y) - p->h / 2 > G.lv.water_y) return;   /* подводное плавание = stealth-умение */
    /* штатные способы открыть комнату: бег по сплошному полу, активный лифт, безопасный спрыг (до 80 px) на платформу/пол.
       Падение на этаж, перелёт Domina, антиграв Refreezer, слайд Pengi и плавание комнату НЕ открывают. */
    if (!actor_on_ground(p, &G.lv)) return;
    if (G.explored[r]) { G.pl_gy = py; return; }
    int ok = 0;
    if (p->stand_obj >= 0 && p->stand_obj < G.lv.nobj) {
        const Obj *o = &G.lv.obj[p->stand_obj];
        if (o->type == O_LIFT && o->have >= o->need) ok = 1;                       /* подъём/спуск строго на активном лифте */
    }
    if (G.pl_rope_t > 0) ok = 1;                                                   /* верёвка Pengi — штатный подъём */
    if (p->stand_obj >= 0 && p->stand_obj < G.lv.nobj && G.lv.obj[p->stand_obj].type == O_PLAT && G.pl_gy > 0 && ABSI(py - G.pl_gy) <= 140) ok = 1;   /* мини-платформа */
    if (G.pl_gy > 0) {
        int dy = py - G.pl_gy;
        if (ABSI(dy) <= 24) ok = 1;                                                /* свободный переход пешком */
        else if (dy > 24) ok = 1;                                                  /* спрыг/падение обычным способом (не stealth): игрок не должен застревать в закрытой комнате */
    }
    if (ok) { G.explored[r] = 1; G.pl_gy = py; game_log("Room %d opened (explored on foot / lift)", r); }
}
static void safe_inward(int *sx, int *sy) {
    int x = *sx, y = *sy;
    if (x < 16) x = 16;
    int left_solid = level_solid_rect(&G.lv, x - 48, y, x - 8, y + 1);
    int right_solid = level_solid_rect(&G.lv, x + 8, y, x + 48, y + 1);
    int under = level_solid_rect(&G.lv, x - 4, y, x + 4, y + 1);
    if (!left_solid && right_solid) x += 80;
    else if (!right_solid && left_solid) x -= 80;
    else if (!left_solid && !right_solid) {
        for (int d = 8; d < 120; d += 8) {
            if (level_solid_rect(&G.lv, x + d - 4, y, x + d + 4, y + 1)) { x += d; break; }
            if (level_solid_rect(&G.lv, x - d - 4, y, x - d + 4, y + 1)) { x -= d; break; }
        }
    }
    if (!level_solid_rect(&G.lv, x - 4, y, x + 4, y + 1)) {
        for (int dy = 0; dy < 40; dy++) {
            if (level_solid_rect(&G.lv, x - 4, y - dy, x + 4, y - dy + 1)) { y -= dy; break; }
        }
    }
    if (x < 24) x = 24;
    if (x > G.lv.w - 24) x = G.lv.w - 24;
    *sx = x; *sy = y;
    (void)under;
}
/* явные checkpoint (не босс, не закрытые комнаты) */
void setup_checkpoints(void) {
    int n = 0;
    #define CP(xx,yy) do { if (n < 8) { G.cp_x[n] = (xx); G.cp_y[n] = (yy); n++; } } while (0)
    if (G.lv.ncp > 0) {
        for (int i = 0; i < G.lv.ncp && i < 8; i++) CP(G.lv.cp_x[i], G.lv.cp_y[i]);
    } else if (G.level_no == 1) {
        /* 1S старт, 2S после двери, 3S перед боссом (НЕ в огненной стене 70*TILE) */
        CP(48, FLOOR1_Y);
        CP(30 * TILE, FLOOR1_Y);
        CP(78 * TILE, FLOOR2_Y);   /* после stone wall 74*, до лифта босса */
    } else if (G.level_no == 2) {
        CP(60, FLOOR1_Y);
        CP(34 * TILE, FLOOR1_Y);
        CP(68 * TILE, FLOOR2_Y);
    } else if (G.level_no == 3) {
        CP(170, FLOOR3_Y);
        CP(28 * TILE, FLOOR3_Y);
        CP(70 * TILE, FLOOR3_Y);
    } else {
        CP(48, FLOOR1_Y);
    }
    #undef CP
    G.cp_n = n;
    G.cp_stage = 0; /* стартовый всегда открыт */
}

/* читы: UU DD □○ level+ | DD UU ○□ level- | LL RR □○ quest debug
   □ = jump (не d-pad up), ○ = melee/special */
static int g_cheat_buf[12], g_cheat_n, g_cheat_age;
static void cheat_push(int code) {
    if (g_cheat_n < 12) g_cheat_buf[g_cheat_n++] = code;
    else {
        for (int i = 0; i < 11; i++) g_cheat_buf[i] = g_cheat_buf[i + 1];
        g_cheat_buf[11] = code;
    }
    g_cheat_age = 300; /* 5 с на ввод */
}
static int cheat_match(const int *seq, int n) {
    if (g_cheat_n < n) return 0;
    for (int i = 0; i < n; i++)
        if (g_cheat_buf[g_cheat_n - n + i] != seq[i]) return 0;
    return 1;
}
static void cheat_codes_update(const Input *in, const Input *prev) {
    enum { CU=1, CD=2, CL=3, CR=4, CSQ=5, CCI=6 };
    /* D-pad / arrows — только направления (jump с up не пишем как □) */
    if (in->up && !prev->up) cheat_push(CU);
    if (in->down && !prev->down) cheat_push(CD);
    if (in->left && !prev->left) cheat_push(CL);
    if (in->right && !prev->right) cheat_push(CR);
    /* □: jump, но не если это тот же кадр что up (клавиша W/Up) */
    if (in->jump && !prev->jump && !(in->up && !prev->up)) cheat_push(CSQ);
    /* ○: melee или special */
    if ((in->melee && !prev->melee) || (in->special && !prev->special)) cheat_push(CCI);

    if (g_cheat_age > 0) {
        if (--g_cheat_age == 0) g_cheat_n = 0;
    }

    {
        static const int next_lv[] = {CU, CU, CD, CD, CSQ, CCI};
        static const int prev_lv[] = {CD, CD, CU, CU, CCI, CSQ};
        static const int qdbg[]   = {CL, CL, CR, CR, CSQ, CCI};
        if (cheat_match(next_lv, 6)) {
            int n = G.level_no + 1;
            if (n > NUM_LEVELS) n = NUM_LEVELS;
            game_set_level_no(n);
            { char b[48]; snprintf(b, sizeof b, "CHEAT: level %d", n); say(b); }
            g_cheat_n = 0;
        } else if (cheat_match(prev_lv, 6)) {
            int n = G.level_no - 1;
            if (n < 1) n = 1;
            game_set_level_no(n);
            { char b[48]; snprintf(b, sizeof b, "CHEAT: level %d", n); say(b); }
            g_cheat_n = 0;
        } else if (cheat_match(qdbg, 6)) {
            G.debug_quest = !G.debug_quest;
            say(G.debug_quest ? "CHEAT: quest debug ON" : "CHEAT: quest debug OFF");
            g_cheat_n = 0;
        }
    }
}

static void load_settings_ini(void) {
    G.debug_log = 0;
    G.debug_quest = 0;
    FILE *f = fopen("settings.ini", "r");
    if (!f) f = fopen("data/settings.ini", "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == ';') continue;
        int v;
        if (sscanf(line, "debug_log = %d", &v) == 1 || sscanf(line, "debug_log=%d", &v) == 1)
            G.debug_log = v ? 1 : 0;
        if (sscanf(line, "debug_quest_mode = %d", &v) == 1 || sscanf(line, "debug_quest_mode=%d", &v) == 1)
            G.debug_quest = v ? 1 : 0;
    }
    fclose(f);
}
static void update_cp_stage(void) {
    Actor *p = player();
    if (!p || p->hp <= 0) return;
    /* только СЛЕДУЮЩИЙ знак, только при касании столба (не издалека) */
    int px = FX_INT(p->x), py = FX_INT(p->y);
    int next = G.cp_stage + 1;
    if (next < 0 || next >= G.cp_n) return;
    if (ABSI(px - G.cp_x[next]) <= 18 && ABSI(py - G.cp_y[next]) <= 28) {
        G.cp_stage = next;
        { char b[48]; snprintf(b, sizeof b, "Checkpoint %d/%d", next + 1, G.cp_n); say(b); }
        game_log("CP activated %d at %d,%d", next + 1, G.cp_x[next], G.cp_y[next]);
    }
}
static void stage_checkpoint(int px, int *ox, int *oy) {
    (void)px;
    /* всегда последний АКТИВНЫЙ checkpoint (не 1S, если уже прошли дальше) */
    int i = G.cp_stage;
    if (i < 0) i = 0;
    if (G.cp_n <= 0) { *ox = 48; *oy = FLOOR1_Y; return; }
    if (i >= G.cp_n) i = G.cp_n - 1;
    *ox = G.cp_x[i]; *oy = G.cp_y[i];
    if (!level_solid_rect(&G.lv, *ox - 4, *oy, *ox + 4, *oy + 1)) {
        for (int dy = 0; dy < 80; dy += 4) {
            if (level_solid_rect(&G.lv, *ox - 4, *oy - dy, *ox + 4, *oy - dy + 1)) { *oy -= dy; break; }
        }
    }
    /* никогда в арену босса */
    if (in_arena_zone(*ox, *oy)) {
        *ox = G.cp_x[0]; *oy = G.cp_y[0];
    }
}
/* респаун: платформа падения если open; иначе checkpoint stage; никогда арена босса */
static void clamp_spawn_open(int *sx, int *sy) {
    int x = *sx, y = *sy;
    if (in_arena_zone(x, y)) {
        stage_checkpoint(x, &x, &y);
        *sx = x; *sy = y; return;
    }
    int r = level_room(x, y);
    if (r >= 0 && r < 12 && G.explored[r]) {
        *sx = x; *sy = y; return;
    }
    Actor *p = player();
    int refx = p ? FX_INT(p->x) : x;
    if (p && p->safe_x > 16) {
        int sr = level_room(p->safe_x, p->safe_y);
        if (sr >= 0 && sr < 12 && G.explored[sr] && !in_arena_zone(p->safe_x, p->safe_y)) {
            *sx = p->safe_x; *sy = p->safe_y; return;
        }
    }
    stage_checkpoint(refx, &x, &y);
    *sx = x; *sy = y;
}
/* лифт powered обслуживает этот этаж → комната доступна штатно */

/* этаж доступен обычным героям, только если есть powered-лифт на этот Y или стартовый CP */
static int floor_open_for_team(int py) {
    if (G.cp_n > 0 && ABSI(py - G.cp_y[0]) <= 40) return 1; /* 1S всегда */
    for (int i = 0; i < G.lv.nobj; i++) {
        const Obj *o = &G.lv.obj[i];
        if (o->type != O_LIFT || o->have < o->need) continue;
        if (ABSI(o->y0 - py) <= 36 || ABSI(o->y1 - py) <= 36 || ABSI(o->y - py) <= 36)
            return 1;
    }
    /* видимый мост как пол — только если уже включён */
    for (int i = 0; i < G.lv.nobj; i++) {
        const Obj *o = &G.lv.obj[i];
        if (o->type != O_BRIDGE || !o->on) continue;
        if (ABSI(o->y - py) <= 16) return 1;
    }
    return 0;
}
static int room_lift_access(int px, int py) {
    for (int i = 0; i < G.lv.nobj; i++) {
        const Obj *o = &G.lv.obj[i];
        if (o->type != O_LIFT || o->have < o->need) continue;
        if (ABSI((o->x + o->w / 2) - px) > 200) continue; /* грубо та же колонка */
        if (ABSI(o->y0 - py) <= 28 || ABSI(o->y1 - py) <= 28 || ABSI(o->y - py) <= 28)
            return 1;
    }
    /* активный мост/невидимка под ногами */
    for (int i = 0; i < G.lv.nobj; i++) {
        const Obj *o = &G.lv.obj[i];
        if (o->type != O_BRIDGE || !o->on || !o->solid) continue;
        if (px >= o->x - 4 && px <= o->x + o->w + 4 && ABSI(o->y - py) <= 12)
            return 1;
    }
    return 0;
}
/* спец-режим допускает нахождение в закрытой */
static int hero_special_access(const Actor *h) {
    if (!h) return 0;
    if (h->kind == RIG_MISTRESS && (h->spin_t > 0 || G.domina_stealth)) return 1;
    if (h->kind == RIG_PENGI && h->sliding) return 1;
    if (h->kind == RIG_REFREEZER && h->misc > 0 && h->flip) return 1;
    /* стоит на powered-лифте */
    if (h->stand_obj >= 0 && h->stand_obj < G.lv.nobj) {
        const Obj *o = &G.lv.obj[h->stand_obj];
        if (o->type == O_LIFT && o->have >= o->need) return 1;
    }
    return 0;
}
/* смерть только если УПАЛ в закрытую (большой vy / fell), не за простое нахождение */
static void closed_room_kill_check(int hi) {
    Actor *h = &G.hero[hi];
    if (!h->active || h->hp <= 0) return;
    int hx = FX_INT(h->x), hy = FX_INT(h->y);
    int r = level_room(hx, hy);
    if (r < 0 || r >= 12) return;
    if (G.explored[r]) return;
    if (hero_special_access(h)) return;
    if (room_lift_access(hx, hy)) {
        G.explored[r] = 1;
        return;
    }
    /* падение в закрытую: fell уже выставлен физикой, или жёсткое приземление */
    int hard_land = (h->fell || h->vy > FXF(5.0));
    if (!hard_land) {
        /* союзник без спецдоступа в закрытой / без лифта → только 1S, НЕ к игроку */
        if (hi != G.cur && (actor_on_ground(h, &G.lv) || h->stand_obj >= 0 || h->vy > FXF(2.0))) {
            int sx = G.cp_n > 0 ? G.cp_x[0] : 48;
            int sy = G.cp_n > 0 ? G.cp_y[0] : FLOOR1_Y;
            if (floor_open_for_team(hy) && room_explored(r)) {
                /* этаж уже открыт лифтом — можно к игроку */
                Actor *p = player();
                if (p && p->hp > 0 && !G.domina_stealth) {
                    sx = FX_INT(p->x); sy = FX_INT(p->y);
                }
            }
            clamp_spawn_open(&sx, &sy);
            actor_teleport(h, sx, sy);
            h->vx = h->vy = 0; h->invuln_t = 60; h->fell = 0;
        }
        return;
    }
    /* фатальное падение в закрытую */
    h->fell = 1;
    h->hp = 0;
    {
        int sx, sy;
        if (hi != G.cur) {
            /* AI: строго checkpoint 1S, не этаж Domina-stealth */
            sx = G.cp_n > 0 ? G.cp_x[0] : 48;
            sy = G.cp_n > 0 ? G.cp_y[0] : FLOOR1_Y;
        } else {
            stage_checkpoint(hx, &sx, &sy);
        }
        safe_inward(&sx, &sy);
        clamp_spawn_open(&sx, &sy);
        G.hero_dead_x[hi] = sx; G.hero_dead_y[hi] = sy;
        actor_teleport(h, sx, sy);
        h->vx = h->vy = 0;
        h->soft = 0; h->fall_immune = 1; h->invuln_t = 90;
        h->fell = 0;
    }
    if (hi == G.cur)
        say("Closed room — back to checkpoint");
}
/* сдвинуть точку респауна на 60px внутрь платформы от края пропасти */
static void emergency_to_player(Actor *h) {
    Actor *p = player();
    if (!p || p->hp <= 0) return;
    int px = FX_INT(p->x), py = FX_INT(p->y);
    if (in_arena_zone(px, py)) {
        stage_checkpoint(px, &px, &py);
    }
    clamp_spawn_open(&px, &py);
    actor_teleport(h, px + (h - G.hero - G.cur) * 14, py);
    h->post_x = px; h->post_y = py; h->invuln_t = 60; h->fell = 0;
}

static void respawn_hero(int hi) {
    Actor *h = &G.hero[hi];
    int imm = h->immortal;
    int sx, sy;
    if (G.hero_dead_t[hi] >= 0 && (G.hero_dead_x[hi] || G.hero_dead_y[hi])) {
        sx = G.hero_dead_x[hi]; sy = G.hero_dead_y[hi];
    } else {
        sx = h->safe_x; sy = h->safe_y;
    }
    if (sx < 16 || sy < 16) {
        sx = G.lv.hero_x[hi] > 0 ? G.lv.hero_x[hi] : 40;
        sy = G.lv.hero_y[hi] > 0 ? G.lv.hero_y[hi] : FLOOR1_Y;
    }
    if (hi != G.cur && G.hero[G.cur].hp > 0) {
        sx = FX_INT(G.hero[G.cur].x) + (hi - G.cur) * 14;
        sy = FX_INT(G.hero[G.cur].y);
    }
    clamp_spawn_open(&sx, &sy);
    actor_init(h, HERO_KIND[hi], 0, sx, sy);
    h->immortal = imm; h->post_x = sx; h->post_y = sy; h->invuln_t = 90;
    if (hi == G.cur) say("Respawn in open room");
}


/* ----- жизни, телепорт, воскрешение ----- */
static void lives_reset(void) {
    for (int i = 0; i < NUM_HEROES; i++) { G.lives[i] = START_LIVES; G.out[i] = 0; G.dying[i] = 0; G.help_flag[i] = 0; }
    G.team_kills = 0; G.player_kills = 0; G.ally_kills = 0;
    G.tele = START_TELEPORTS; G.boss_out_t = 0; G.group_t = 0; G.lift_call_t = 0; G.boss_hide_allies = 0;
    G.gameover_t = 0;
}

/* полный рестарт уровня с начала (3 жизни), до MAX_FULL_RESPAWNS раз; иначе GAME OVER */
static void full_level_restart_or_gameover(void) {
    if (G.full_respawns < MAX_FULL_RESPAWNS) {
        G.full_respawns++;
        lives_reset();
        for (int i = 0; i < NUM_HEROES; i++) {
            Actor *h = &G.hero[i];
            actor_init(h, HERO_KIND[i], 0, G.lv.hero_x[i], G.lv.hero_y[i]);
            h->immortal = (i != 0);
            h->post_x = G.lv.hero_x[i]; h->post_y = G.lv.hero_y[i];
            h->invuln_t = 90;
        }
        G.cur = 0; G.hero[0].immortal = 0;
        G.have_key = 0; G.have_artifact = 0;
        G.cam_init = 0;
        char b[80];
        snprintf(b, sizeof b, "Restart %d/%d — back to start", G.full_respawns, MAX_FULL_RESPAWNS);
        say(b);
        sound_play_bgm(BGM_LEVEL);
        return;
    }
    /* GAME OVER */
    G.gameover_t = 180; /* 3 сек */
    sound_play_bgm(BGM_GAMEOVER);
    say("GAME OVER");
}
int game_add_life(int hi) {
    if (hi < 0 || hi >= NUM_HEROES || G.lives[hi] >= MAX_LIVES) return 0;
    G.lives[hi]++;
    if (G.out[hi]) { G.out[hi] = 0; G.dying[hi] = 0; Actor *p = player(); G.hero[hi].dead_t = 0; actor_init(&G.hero[hi], HERO_KIND[hi], 0, FX_INT(p->x), FX_INT(p->y)); G.hero[hi].invuln_t = 90; }
    char b[64]; snprintf(b, sizeof b, "+1 life: %s (%d)", HERO_NAME[hi], G.lives[hi]); say(b);
    sound_play_sfx(SFX_PICKUP);
    return 1;
}
/* выбывший герой (0 жизней) возвращается к команде с START_LIVES жизнями; 1 = кто-то воскрес */
static int revive_out_hero(void) {
    int cand[NUM_HEROES], n = 0;
    for (int i = 0; i < NUM_HEROES; i++) if (G.out[i] && i != G.cur) cand[n++] = i;
    if (!n) { for (int i = 0; i < NUM_HEROES; i++) if (G.out[i]) cand[n++] = i; }
    if (!n) return 0;
    int hi = cand[g_rand() % (uint32_t)n];
    Actor *p = player();
    G.out[hi] = 0; G.dying[hi] = 0; G.lives[hi] = START_LIVES;
    Actor *h = &G.hero[hi];
    int px = FX_INT(p->x), py = FX_INT(p->y);
    actor_init(h, HERO_KIND[hi], 0, px + (hi % 2 ? 24 : -24), py);
    h->post_x = px; h->post_y = py; h->invuln_t = 120; h->active = 1;
    h->immortal = (hi != G.cur);
    spark(FX_INT(h->x), FX_INT(h->y) - 14, 16, 0xFF80E0FFu, 3);
    char b[80]; snprintf(b, sizeof b, "%s is back with %d lives!", HERO_NAME[hi], START_LIVES); say(b);
    sound_play_sfx(SFX_PICKUP);
    game_log("Frags resurrected %s with %d lives", HERO_NAME[hi], START_LIVES);
    return 1;
}
/* who: 0 = игрок (G.cur), 1 = союзник-ИИ. Счётчики независимы.
   Игрок: каждые 10 убийств → +1 жизнь активному + 1 телепорт.
   Союзники: каждые 30 убийств (суммарно) → +1 жизнь активному + 1 телепорт. */
void team_kill(void) { team_kill_from(1); }
void team_kill_from(int from_ai) {
    if (from_ai) {
        if (++G.ally_kills < 10) return;   /* союзники: 10 врагов → +1 жизнь */
        G.ally_kills = 0;
    } else {
        if (++G.player_kills < 4) return;  /* игрок: 4 врага → +1 жизнь */
        G.player_kills = 0;
    }
    /* есть выбывшие герои: фраги воскрешают одного из них со стандартным запасом жизней (процесс бесконечный) */
    if (revive_out_hero()) { if (G.tele < MAX_TELEPORTS) G.tele++; return; }
    /* жизни игрока/героев не выше MAX_LIVES (3) */
    if (G.lives[G.cur] < MAX_LIVES) game_add_life(G.cur);
    if (G.tele < MAX_TELEPORTS) G.tele++;
    game_log("%s kills: +1 life for %s, +1 teleport", from_ai ? "Ally" : "Player", HERO_NAME[G.cur]);
}
void game_team_to(int x, int y) {
    int n = 0;
    for (int i = 0; i < NUM_HEROES; i++) {
        Actor *h = &G.hero[i];
        if (i == G.cur || !h->active || h->hp <= 0) continue;
        int dx = (n % 2 ? 1 : -1) * (14 + 12 * (n / 2)); n++;
        actor_teleport(h, x + dx, y); h->post_x = x + dx; h->post_y = y; h->invuln_t = 45; h->vx = h->vy = 0;
        spark(x + dx, y - 14, 8, 0xFF80E0FFu, 2);
    }
}
void game_teleport_team(void) {
    if (G.tele <= 0) { say("No teleport artifacts left"); return; }
    Actor *p = player();
    if (p->hp <= 0) return;
    /* если не Domina и она в stealth — телепорт снимает stealth и зовёт её */
    if (G.domina_stealth && p->kind != RIG_MISTRESS) {
        G.domina_stealth = 0;
        G.follow = 1;
        for (int i = 0; i < NUM_HEROES; i++) {
            if (G.hero[i].kind == RIG_MISTRESS) {
                G.hero[i].spin_t = 0;
                G.portal.mask |= 1 << i;
            }
        }
        say("Domina recalled — stealth OFF");
    }
    if (G.domina_stealth && p->kind == RIG_MISTRESS) {
        say("Stealth: teleport locked for Domina");
        return;
    }
    G.tele--;
    G.portal.on = 1; G.portal.t = 0; G.portal.n = 0; G.portal.mask = 0;
    G.portal.x = FX_INT(p->x) + p->facing * 30; G.portal.y = FX_INT(p->y);
    if (level_solid_rect(&G.lv, G.portal.x - 8, G.portal.y - p->h, G.portal.x + 8, G.portal.y - 6) || !level_solid_rect(&G.lv, G.portal.x - 4, G.portal.y, G.portal.x + 4, G.portal.y + 1)) G.portal.x = FX_INT(p->x) - p->facing * 30;
    for (int i = 0; i < NUM_HEROES; i++) if (i != G.cur && G.hero[i].active && G.hero[i].hp > 0) { G.portal.mask |= 1 << i; G.portal.n++; }
    spark(G.portal.x, G.portal.y - 16, 14, 0xFF80E0FFu, 3);
    sound_play_sfx(SFX_PICKUP);
    char b[64]; snprintf(b, sizeof b, "Teleport: team is here (%d left)", G.tele); say(b);
}

/* ----- группа: потеряшки возвращаются естественно, без мгновенных телепортов в точку игрока ----- */
static int path_clear_same_floor(int x0, int x1, int y) {
    if (ABSI(x0 - x1) > 900) return 0;
    if (blocked_between(x0, x1, y - 8)) return 0;
    if (wall_between(x0, x1, y - 8)) return 0;
    int lo = x0 < x1 ? x0 : x1, hi = x0 < x1 ? x1 : x0;
    int gap = 0;
    for (int x = lo; x <= hi; x += 4) {
        int solid = level_solid_rect_team(&G.lv, x, y, x, y + 1, 0) ||
                    level_solid_rect_team(&G.lv, x, y + 1, x, y + 1, 0);
        if (!solid) { if ((gap += 4) > 96) return 0; }
        else gap = 0;
    }
    return 1;
}
static int hero_lost(int i) {
    Actor *p = player(), *h = &G.hero[i];
    if (i == G.cur || !h->active || h->hp <= 0 || !p || p->hp <= 0) return 0;
    if (p->stand_obj >= 0 && h->stand_obj == p->stand_obj) return 0; /* вместе на лифте */
    return ABSI(FX_INT(h->x) - FX_INT(p->x)) > 220 || ABSI(FX_INT(h->y) - FX_INT(p->y)) > 48;
}
/* 1: появился у края экрана на несущей платформе (без стены и пропасти на краю) */
static int in_arena_zone(int x, int y) {
    const Arena *ar = &G.lv.arena;
    return G.boss_state == BS_SLEEP && x >= ar->trig_x0 - 8 && x <= ar->trig_x1 + 8 && y >= ar->ceil && y <= ar->floor + 10;
}
/* нижняя грань потолочной плиты (ceiling): выше неё нельзя ни героям, ни вертолёту. 0 - потолка нет */
static int ceiling_bottom(int x) {
    int y = 0;
    while (y < 200 && !level_solid_rect(&G.lv, x, y, x, y)) y++;
    if (y >= 200) return 0;
    while (y < 200 && level_solid_rect(&G.lv, x, y, x, y)) y++;
    return y;
}
/* сторона появления: 1) откуда шёл игрок 2) сторона лифта на экране */
static void preferred_sides(int *s0, int *s1) {
    Actor *p = player();
    int px = FX_INT(p->x), py = FX_INT(p->y);
    int from = G.approach_dir ? G.approach_dir : (p->facing ? -p->facing : -1);
    /* from = край, откуда пришёл игрок (левый край если шёл вправо) */
    *s0 = from < 0 ? -1 : 1;
    *s1 = 0;
    /* сторона ближайшего лифта, связанного с этажом игрока */
    int best = -1, bd = 1 << 30;
    for (int i = 0; i < G.lv.nobj; i++) {
        const Obj *o = &G.lv.obj[i];
        if (o->type != O_LIFT || o->have < o->need) continue;
        if (ABSI(o->y0 - py) > 20 && ABSI(o->y1 - py) > 20 && ABSI(o->y - py) > 20) continue;
        int cx = o->x + o->w / 2;
        int d = ABSI(cx - px);
        if (d < bd) { bd = d; best = cx; }
    }
    if (best >= 0) *s1 = best < px ? -1 : 1;
}
/* край экрана: ТОЛЬКО если герой реально далеко (>1.5 экрана) и путь по платформе закрыт.
   На той же платформе / рядом — НИКОГДА не телепортируем (AI просто добежит). */
static int bring_edge(int hi) {
    (void)hi;
    return 0; /* запрет телепорта с края — только бег / лифт / вертолёт */
}
/* лифт: не телепортируем на кабину — только назначаем цель, AI сам подойдёт.
   Телепорт на лифт только если герой уже у шахты (≤64px). */
static int bring_lift(int hi) {
    Actor *p = player(), *h = &G.hero[hi];
    if (!G.follow) return 0;
    if (!room_explored(level_room(FX_INT(p->x), FX_INT(p->y)))) return 0;
    int py = FX_INT(p->y), hy = FX_INT(h->y), hx = FX_INT(h->x);
    if (ABSI(hy - py) <= 28) return 0; /* тот же этаж — без лифта */
    int best = -1, bd = 1 << 30;
    for (int i = 0; i < G.lv.nobj; i++) {
        const Obj *o = &G.lv.obj[i];
        if (o->type != O_LIFT || o->have < o->need) continue;
        int cx = o->x + o->w / 2;
        int links = (ABSI(o->y0 - hy) <= 24 && ABSI(o->y1 - py) <= 24)
                 || (ABSI(o->y1 - hy) <= 24 && ABSI(o->y0 - py) <= 24)
                 || (ABSI(o->y - hy) <= 16);
        if (!links) continue;
        int d = ABSI(cx - hx);
        if (d < bd) { bd = d; best = i; }
    }
    if (best < 0) return 0;
    const Obj *o = &G.lv.obj[best];
    int cx = o->x + o->w / 2;
    h->misc = best; /* AI: nav_lift к этой кабине */
    h->ai_blk = 0;
    /* телепорт только если уже у шахты на своём этаже */
    if (ABSI(hx - cx) <= 64 && ABSI(hy - o->y) <= 20) {
        actor_teleport(h, cx + (hi - G.cur) * 3, o->y);
        h->post_x = FX_INT(h->x); h->post_y = o->y;
        h->vx = h->vy = 0; h->invuln_t = 30;
    }
    return 1;
}
static int floors_apart(int y0, int y1) {
    int d = ABSI(y0 - y1);
    return (d + 64) / 128; /* FLOOR_PITCH ≈ 128 */
}
static int on_screen_hero(int hi);
static int path_clear_same_floor(int x0, int x1, int y);
static void bring_heli(int hi) {
    Actor *p = player(), *h = &G.hero[hi];
    if (!G.follow || G.solo_mode) return;
    if (!p || !h || !h->active || h->hp <= 0) return;
    if (G.out[hi]) return;
    /* уже рядом — не нужен вертолёт */
    if (ABSI(FX_INT(h->x) - FX_INT(p->x)) < 100 && ABSI(FX_INT(h->y) - FX_INT(p->y)) <= 28)
        return;
    if (!getenv("LOST_HELI")) {   /* ступени возвращения: (а) на экране — идёт сам (лифт/бег); (б) вне экрана, лифт на экране — сажается в кабину;
           (в) лифт вне экрана — появляется у края экрана на этаже игрока и бежит; (г) пропасть без опор — вертолёт */
        int px0 = FX_INT(p->x), py0 = FX_INT(p->y), hx0 = FX_INT(h->x), hy0 = FX_INT(h->y);
        int fl = G.pl_floor_y ? G.pl_floor_y : py0;
        int best = -1, bd = 1 << 30;
        for (int k = 0; k < G.lv.nobj; k++) {
            const Obj *o = &G.lv.obj[k];
            if (o->type != O_LIFT || o->have < o->need) continue;
            if (!room_explored(level_room(o->x + o->w / 2, o->y))) continue;
            int links = (ABSI(o->y0 - hy0) <= 24 && ABSI(o->y1 - fl) <= 24) || (ABSI(o->y1 - hy0) <= 24 && ABSI(o->y0 - fl) <= 24);
            if (!links && ABSI(hy0 - fl) > 28) continue;
            int d = ABSI(o->x + o->w / 2 - px0);
            if (d < bd) { bd = d; best = k; }
        }
        int on_scr = on_screen_hero(hi);
        if (on_scr && G.lost_total[hi] < 480 && (ABSI(hy0 - fl) <= 28 ? path_clear_same_floor(hx0, px0, fl) : best >= 0)) {
            if (best >= 0) h->misc = best;
            return;                                                 /* (а) герой дойдёт сам: лифт или бег */
        }
        if (best >= 0) {                                            /* (б) лифт: кабина ЗА кадром на дальней остановке берёт героя и ПРИВОЗИТ на этаж игрока */
            const Obj *o = &G.lv.obj[best];
            int cx = o->x + o->w / 2;
            int far_y = ABSI(o->y1 - fl) <= 24 ? o->y0 : o->y1;
            int far_vis = cx >= G.cam_x - 12 && cx <= G.cam_x + VIEW_W + 12 && far_y >= G.cam_y - 12 && far_y <= G.cam_y + VIEW_H + 24;
            if (!far_vis) {
                int idle = (o->state == LS_TOP || o->state == LS_BOTTOM) && ABSI(o->y - far_y) <= 2;
                if (idle && !(G.heli.on && (G.heli.mask & (1 << hi)))) {
                    actor_teleport(h, cx + (hi % 3 - 1) * 6, far_y);
                    h->stand_obj = best; h->misc = best; h->vx = h->vy = 0; h->invuln_t = 120; h->ai_blk = 0; h->fell = 0;
                    h->post_x = FX_INT(h->x); h->post_y = far_y;
                    G.lost_t[hi] = 0; G.lost_total[hi] = 0;
                    return;
                }
                if (G.lost_total[hi] < 1500) return;                 /* ждём, пока лифт сам придёт на дальнюю остановку */
            }
        }
        {   /* (в) край экрана на этаже игрока, путь до игрока сплошной (прыжки через щели ИИ делает сам) */
            int from = G.approach_dir ? G.approach_dir : (p->facing ? -p->facing : -1);
            for (int t = 0; t < 2; t++) {
                int side = t == 0 ? from : -from;
                int x = side < 0 ? G.cam_x + 24 : G.cam_x + VIEW_W - 24;
                if (x < 24) x = 24; if (x > WORLD_W - 24) x = WORLD_W - 24;
                if (ABSI(x - px0) < 60) continue;
                if (!level_solid_rect(&G.lv, x - 4, fl, x + 4, fl + 1)) continue;
                if (level_solid_rect(&G.lv, x - 6, fl - 40, x + 6, fl - 6)) continue;
                if (!room_explored(level_room(x, fl)) || in_arena_zone(x, fl)) continue;
                if (!path_clear_same_floor(x, px0, fl)) continue;
                spark(hx0, hy0 - 14, 8, 0xFF80E0FFu, 2);
                actor_teleport(h, x, fl);
                h->vx = h->vy = 0; h->invuln_t = 90; h->ai_blk = 0; h->fell = 0; h->post_x = x; h->post_y = fl;
                spark(x, fl - 14, 8, 0xFFFFFFFFu, 2);
                G.lost_t[hi] = 0;
                return;
            }
        }
    }                                                               /* (г) иначе — вертолёт */
    int from = G.approach_dir ? G.approach_dir : (p->facing ? -p->facing : -1);
    if (!G.heli.on) {
        G.heli.on = 1; G.heli.t = 0; G.heli.mask = 0; G.heli.load = 0;
        G.heli.x0 = from < 0 ? G.cam_x - 40 : G.cam_x + VIEW_W + 40;
        G.heli.x = G.heli.x0; G.heli.y = G.cam_y + 20;
        int cb = ceiling_bottom(FX_INT(p->x)); if (G.heli.y < cb + 22) G.heli.y = cb + 22;
    }
    G.heli.mask |= 1 << hi;
    /* оживить на время рейса, если был 0 hp (баг) */
    if (h->hp <= 0) { h->hp = h->maxhp; G.dying[hi] = 0; }
    h->active = 1;
    spark(FX_INT(h->x), FX_INT(h->y) - 14, 8, 0xFF80E0FFu, 2);
    h->vx = h->vy = 0; h->invuln_t = 400; h->ai_blk = 0;
    G.lost_t[hi] = 0;
}
/* ---- группы / спринтеры ---- */
static int floor_band(int y) {
    if (y < FLOOR3_Y - 32) return 0;
    if (y < FLOOR2_Y - 32) return 1;
    if (y < FLOOR1_Y - 32) return 2;
    return 3;
}
static int group_alloc(int parent, int leader, int y) {
    for (int g = 1; g < 7; g++) if (!G.g_active[g]) {
        G.g_active[g] = 1; G.g_parent[g] = parent;
        G.g_leader[g] = leader; G.g_floor[g] = y;
        return g;
    }
    return parent >= 0 ? parent : 0;
}
static int on_screen_hero(int hi) {
    Actor *h = &G.hero[hi];
    int x = FX_INT(h->x), y = FX_INT(h->y);
    return x >= G.cam_x - 8 && x <= G.cam_x + VIEW_W + 8 &&
           y >= G.cam_y - 8 && y <= G.cam_y + VIEW_H + 8;
}
/* слияние: все из from_g → to_g, дети from переподчиняются to */
static void group_merge(int from_g, int to_g) {
    if (from_g == to_g || from_g < 0 || to_g < 0) return;
    for (int i = 0; i < NUM_HEROES; i++)
        if (G.hero_gid[i] == from_g) G.hero_gid[i] = to_g;
    for (int g = 0; g < 7; g++)
        if (G.g_active[g] && G.g_parent[g] == from_g) G.g_parent[g] = to_g;
    G.g_active[from_g] = 0;
    G.g_leader[from_g] = -1;
}
static void groups_update_lost(void) {
    Actor *p = player();
    if (!p || p->hp <= 0) return;
    int pg = G.hero_gid[G.cur];
    int px = FX_INT(p->x), py = FX_INT(p->y);
    int solo = 0;
    if (p->kind == RIG_MISTRESS && p->spin_t > 0) solo = 1;
    if (p->kind == RIG_PENGI && p->sliding) solo = 1;
    if (p->kind == RIG_REFREEZER && p->misc > 0 && p->flip) solo = 1;
    if (G.domina_stealth) solo = 1;

    for (int i = 0; i < NUM_HEROES; i++) {
        if (i == G.cur || !G.hero[i].active || G.hero[i].hp <= 0) continue;
        Actor *h = &G.hero[i];
        int hx = FX_INT(h->x), hy = FX_INT(h->y);
        int same_fl = ABSI(hy - py) <= 28;

        /* на экране + тот же этаж + не solo → в группе игрока (физически) */
        if (!solo && on_screen_hero(i) && same_fl && path_clear_same_floor(hx, px, py)) {
            if (G.hero_gid[i] != pg) {
                int old = G.hero_gid[i];
                G.hero_gid[i] = pg;
                /* если весь old опустел — деактивировать */
                int left = 0;
                for (int k = 0; k < NUM_HEROES; k++) if (G.hero_gid[k] == old) left++;
                if (!left && old > 0) G.g_active[old] = 0;
            }
            /* спринтеры других групп НЕ телепортятся — только снятие флага при слиянии у лидера */
            continue;
        }

        /* спринтер другой группы — независим, не трогаем */
        if (G.sprinter[i]) continue;

        /* тот же этаж, за экраном, путь чист → может вернуться (остаётся в своей/родительской) */
        if (same_fl && path_clear_same_floor(hx, px, py)) continue;

        /* другой этаж или путь закрыт → lost-группа */
        if (G.hero_gid[i] == pg) {
            int hfb = floor_band(hy);
            int found = -1;
            for (int g = 1; g < 7; g++) {
                if (!G.g_active[g] || G.g_parent[g] != pg) continue;
                if (floor_band(G.g_floor[g]) == hfb) { found = g; break; }
            }
            if (found < 0) found = group_alloc(pg, i, hy);
            G.hero_gid[i] = found;
            if (G.g_leader[found] < 0 || !G.hero[G.g_leader[found]].active)
                G.g_leader[found] = i;
            G.g_floor[found] = hy;
        }
    }
}
static int can_call_hero(int hi) {
    if (G.sprinter[hi]) return 0; /* чужой спринтер — только вручную */
    int pg = G.hero_gid[G.cur];
    int hg = G.hero_gid[hi];
    if (hg == pg) return 1;
    int g = hg, guard = 0;
    while (g >= 0 && g < 7 && G.g_active[g] && guard++ < 8) {
        if (G.g_parent[g] == pg) return 1;
        g = G.g_parent[g];
    }
    return 0;
}
static int route_lost(int hi) {
    Actor *p = player(), *h = &G.hero[hi];
    if (floors_apart(FX_INT(h->y), FX_INT(p->y)) >= 2) {
        /* нет лифта между этажами — необратимо */
        int hy = FX_INT(h->y), py = FX_INT(p->y);
        for (int k = 0; k < G.lv.nobj; k++) {
            const Obj *o = &G.lv.obj[k];
            if (o->type != O_LIFT || o->have < o->need) continue;
            if ((ABSI(o->y0 - hy) <= 24 && ABSI(o->y1 - py) <= 24) ||
                (ABSI(o->y1 - hy) <= 24 && ABSI(o->y0 - py) <= 24))
                return 0;
        }
        return 1;
    }
    return 0;
}
static void bring_hero(int hi) {
    /* не тащить команду к checkpoint мёртвого / stealth / solo */
    if (G.dying[G.cur] || G.hero[G.cur].hp <= 0) return;
    if (!G.follow || G.solo_mode || G.domina_stealth) return;
    Actor *p = player(), *h = &G.hero[hi];
    if (!p || !h || !h->active || h->hp <= 0 || hi == G.cur) return;
    int px = FX_INT(p->x), py = FX_INT(p->y);
    int hx = FX_INT(h->x), hy = FX_INT(h->y);
    int dx = ABSI(hx - px), dy = ABSI(hy - py);

    /* на лифте: ждём до 3 с (180 кадров), затем teleport */
    int on_lift = (h->stand_obj >= 0 && h->stand_obj < G.lv.nobj
                   && G.lv.obj[h->stand_obj].type == O_LIFT);
    static int lift_wait[8];
    if (on_lift && dy > 28) {
        lift_wait[hi]++;
        if (G.last_player_lift >= 0) h->misc = G.last_player_lift;
        if (lift_wait[hi] < 1200) {
            bring_lift(hi); /* AI сам едет */
            return;
        }
        /* таймаут — teleport к игроку */
        lift_wait[hi] = 0;
        int tx = px + (hi - G.cur) * 14, ty = py;
        clamp_spawn_open(&tx, &ty);
        spark(hx, hy - 14, 10, 0xFF80E0FFu, 2);
        actor_teleport(h, tx, ty);
        spark(tx, ty - 14, 10, 0xFF80E0FFu, 2);
        h->vx = h->vy = 0; h->invuln_t = 45; h->ai_blk = 0;
        G.lost_t[hi] = 0;
        return;
    } else {
        lift_wait[hi] = 0;
    }

    /* тот же этаж, в пределах экрана — сам добежит */
    if (dy <= 36 && dx <= VIEW_W) {
        G.lost_t[hi] = 0;
        return;
    }

    /* дальше экрана: лифт / край экрана / вертолёт (без мгновенного телепорта к игроку) */
    bring_heli(hi);
    return;
    /* очень далеко / другой этаж без лифта — вертолёт */
    if (G.lost_t[hi] > 120 || dx > VIEW_W || dy > FLOOR_PITCH)
        bring_heli(hi);
}
/* игрок дошёл до спринтера другой группы → merge */
static void sprinter_catchup(void) {
    Actor *p = player();
    if (!p || p->hp <= 0) return;
    int px = FX_INT(p->x), py = FX_INT(p->y);
    int pg = G.hero_gid[G.cur];
    for (int i = 0; i < NUM_HEROES; i++) {
        if (i == G.cur || !G.hero[i].active || G.hero[i].hp <= 0) continue;
        if (!G.sprinter[i]) continue;
        Actor *h = &G.hero[i];
        int lift_link = -1;
        if (ABSI(FX_INT(h->y) - py) > 24) {                  /* другой этаж: оба на экране и между этажами есть рабочий лифт */
            if (!G.follow || G.solo_mode || G.domina_stealth || !on_screen_hero(i)) continue;
            for (int k = 0; k < G.lv.nobj; k++) {
                const Obj *o = &G.lv.obj[k];
                if (o->type != O_LIFT || o->have < o->need) continue;
                int hy = FX_INT(h->y);
                if (!((ABSI(o->y0 - hy) <= 24 && ABSI(o->y1 - py) <= 24) || (ABSI(o->y1 - hy) <= 24 && ABSI(o->y0 - py) <= 24))) continue;
                int cx = o->x + o->w / 2;
                if (cx < G.cam_x - 8 || cx > G.cam_x + VIEW_W + 8) continue;
                lift_link = k; break;
            }
            if (lift_link < 0) continue;
        } else {
            if (ABSI(FX_INT(h->x) - px) > 48) continue;
            if (blocked_between(FX_INT(h->x), px, py - 8)) continue;
        }
        int og = G.hero_gid[i];
        /* вливаем группу спринтеров в группу игрока */
        group_merge(og, pg);
        G.sprinter[i] = 0;
        /* если игрок был вложенной группой, а встретил root-спринтера — игрок становится root-спринтером */
        if (og == 0 || G.g_parent[pg] == og) {
            /* merge уже в pg; спринтер root снят; игрок — новый якорь */
            G.sprinter[G.cur] = 1;
            for (int k = 0; k < NUM_HEROES; k++)
                if (k != G.cur) G.sprinter[k] = 0;
            G.hero_gid[G.cur] = 0;
            for (int k = 0; k < NUM_HEROES; k++)
                if (G.hero_gid[k] == pg) G.hero_gid[k] = 0;
            group_merge(pg, 0);
            G.g_active[0] = 1; G.g_leader[0] = G.cur; G.g_parent[0] = -1;
        }
        h->ai_blk = 0;
        if (lift_link >= 0) { h->misc = lift_link; say("Groups merged: sprinter takes the lift"); }
        else say("Groups merged");
    }
}
/* страховка: у каждой чужой группы (не группа игрока) должен быть живой sprinter-якорь, иначе она теряется */
static void sprinter_repair(void) {
    if ((G.frame & 7) != 0) return;
    int pg = G.hero_gid[G.cur];
    for (int gid = 0; gid < 7; gid++) {
        if (gid == pg) continue;
        int first = -1, has = 0;
        for (int i = 0; i < NUM_HEROES; i++) {
            if (i == G.cur || G.hero_gid[i] != gid) continue;
            if (!G.hero[i].active || G.hero[i].hp <= 0 || G.out[i]) continue;
            if (first < 0) first = i;
            if (G.sprinter[i]) has = 1;
        }
        if (first >= 0 && !has) {
            int lead = G.g_leader[gid];
            if (lead >= 0 && lead < NUM_HEROES && lead != G.cur && G.hero_gid[lead] == gid && G.hero[lead].active && G.hero[lead].hp > 0 && !G.out[lead]) first = lead;
            G.sprinter[first] = 1; G.g_leader[gid] = first; G.g_active[gid] = 1;
            game_log("Sprinter restored for group %d: hero %d", gid, first);
        }
    }
}
/* ИИ-герой оказался в закрытой комнате (верёвка, сбой маршрута): через 3 с вертолёт/лифт возвращает его к игроку */
static void closed_room_rescue(void) {
    static int t[NUM_HEROES];
    for (int i = 0; i < NUM_HEROES; i++) if (G.rescue_req[i]) { G.rescue_req[i] = 0; if (G.follow && !G.solo_mode && i != G.cur) { game_log("Rescue: hero %d looped jumps", i); bring_heli(i); } }
    Actor *p = player();
    if (!p || !G.follow || G.solo_mode || G.domina_stealth) { memset(t, 0, sizeof t); return; }
    for (int i = 0; i < NUM_HEROES; i++) {
        Actor *h = &G.hero[i];
        if (i == G.cur || !h->active || h->hp <= 0 || G.out[i] || (G.heli.mask & (1 << i))) { t[i] = 0; continue; }
        int r = level_room(FX_INT(h->x), FX_INT(h->y));
        if (r < 0 || r >= 12 || G.explored[r] || hero_special_access(h)) { t[i] = 0; continue; }
        if (++t[i] > 180) {
            t[i] = 0; G.hero_gid[i] = G.hero_gid[G.cur]; G.sprinter[i] = 0;
            game_log("Rescue: hero %d was in closed room %d", i, r);
            bring_heli(i);
        }
    }
}
static void group_gather(void) {
    groups_update_lost();
    for (int i = 0; i < NUM_HEROES; i++) {
        if (i == G.cur || !G.hero[i].active || G.hero[i].hp <= 0) continue;
        if (!can_call_hero(i)) continue;
        bring_hero(i);
    }
}
static int natural_route(int i) {                    /* герой может сам добраться: рабочий лифт соединяет его этаж и этаж игрока */
    Actor *p = player(), *h = &G.hero[i];
    int hy = FX_INT(h->y), py = FX_INT(p->y);
    if (ABSI(hy - py) <= 48) return ABSI(FX_INT(h->x) - FX_INT(p->x)) < 700;   /* один этаж: просто бежать */
    for (int k = 0; k < G.lv.nobj; k++) {
        const Obj *o = &G.lv.obj[k];
        if (o->type != O_LIFT || o->have < o->need) continue;
        if ((ABSI(o->y0 - hy) <= 20 && ABSI(o->y1 - py) <= 20) || (ABSI(o->y1 - hy) <= 20 && ABSI(o->y0 - py) <= 20)) return 1;
    }
    return 0;
}
static void sync_group_platforms(void) {
    Actor *p = player();
    if (!p || p->hp <= 0) return;
    /* вертолёт: не летает / не высаживает при group OFF или закрытой комнате */
    if (G.heli.on) {
        int px = FX_INT(p->x), py = FX_INT(p->y);
        /* игрок в закрытой/stealth-комнате: вертолёт ждёт снаружи, комнату НЕ открывает */
        int pr_open = 1;
        { int pr = level_room(px, py);
          if (pr < 0 || pr >= 12 || !G.explored[pr]) { pr_open = 0; G.heli.t = 0; } }
        /* пустой вертолёт — сразу выключаем */
        if (G.heli.mask == 0) {
            G.heli.on = 0;
        } else {
            G.heli.t++;
            int tgt = -1;
            for (int i = 0; i < NUM_HEROES; i++) {
                if (!(G.heli.mask & (1 << i))) continue;
                if (G.hero[i].hp <= 0) { G.heli.mask &= ~(1 << i); G.heli.load &= ~(1 << i); continue; }
                if (!(G.heli.load & (1 << i)) && tgt < 0) tgt = i;
            }
            int tx = px, ty = py - 64;
            if (tgt >= 0) { tx = FX_INT(G.hero[tgt].x); ty = FX_INT(G.hero[tgt].y) - 64; }
            { int cb = ceiling_bottom(G.heli.x < 8 ? 8 : G.heli.x); int cbt = ceiling_bottom(tx); if (cbt > cb) cb = cbt;
              if (ty < cb + 22) ty = cb + 22; }
            if (G.heli.t < 4) G.heli.y = G.cam_y + 20;
            { int dx = tx - G.heli.x, dy = ty - G.heli.y;
              int sx = dx / 10; if (sx > 5) sx = 5; if (sx < -5) sx = -5; if (sx == 0 && dx) sx = dx > 0 ? 1 : -1;
              int sy = dy / 10; if (sy > 4) sy = 4; if (sy < -4) sy = -4; if (sy == 0 && dy) sy = dy > 0 ? 1 : -1;
              G.heli.x += sx; G.heli.y += sy; }
            if (tgt >= 0) {                                     /* долетели до потеряшки — забираем */
                if (ABSI(G.heli.x - tx) < 10 && ABSI(G.heli.y - ty) < 12) {
                    G.heli.load |= 1 << tgt;
                    spark(FX_INT(G.hero[tgt].x), FX_INT(G.hero[tgt].y) - 14, 8, 0xFFFFFFFFu, 2);
                }
            }
            for (int i = 0; i < NUM_HEROES; i++) {              /* подобранные висят на тросе */
                if (!(G.heli.load & (1 << i))) continue;
                Actor *h = &G.hero[i];
                actor_teleport(h, G.heli.x + (i - G.cur) * 5, G.heli.y + 26); h->vx = h->vy = 0; h->invuln_t = 30; h->post_x = G.heli.x; h->post_y = G.heli.y + 26;
            }
            if (tgt < 0 && pr_open && ABSI(G.heli.x - px) < 14 && ABSI(G.heli.y - ty) < 14) {   /* прилетели к игроку — отдаём */
                for (int i = 0; i < NUM_HEROES; i++) {
                    if (!(G.heli.mask & (1 << i))) continue;
                    Actor *h = &G.hero[i];
                    int lx = px + (i - G.cur) * 10, ly = G.pl_floor_y ? G.pl_floor_y : py;
                    if (ly <= 16) ly = FLOOR4_Y;
                    if (!level_solid_rect(&G.lv, lx - 4, ly, lx + 4, ly + 1)) lx = px;
                    if (blocked_between(lx, px, ly - 8)) lx = px + (lx < px ? 20 : -20);
                    actor_teleport(h, lx, ly);
                    h->vx = h->vy = 0; h->invuln_t = 90; h->post_x = lx; h->post_y = ly; h->fell = 0;
                    G.heli.mask &= ~(1 << i); G.heli.load &= ~(1 << i);
                }
                G.heli.on = 0;
            }
            if (G.heli.t > 1500) {                              /* страховка: слишком долго — сразу к игроку */
                for (int i = 0; i < NUM_HEROES; i++) if (G.heli.mask & (1 << i)) { actor_teleport(&G.hero[i], px, py); G.hero[i].vx = G.hero[i].vy = 0; G.hero[i].invuln_t = 90; }
                G.heli.mask = G.heli.load = 0; G.heli.on = 0;
            }
        }
    }
    /* портал телепорта */
    if (G.portal.on) {
        G.portal.t++;
        int k = 0;
        for (int i = 0; i < NUM_HEROES; i++) {
            if (!(G.portal.mask & (1 << i))) continue;
            int due = 30 + 16 * k++;
            if (G.portal.t < due) continue;
            Actor *h = &G.hero[i];
            G.portal.mask &= ~(1 << i);
            if (h->hp <= 0 || !h->active) continue;
            spark(FX_INT(h->x), FX_INT(h->y) - 14, 8, 0xFF80E0FFu, 2);
            actor_teleport(h, G.portal.x, G.portal.y); h->post_x = G.portal.x; h->post_y = G.portal.y; h->invuln_t = 60; h->vx = (G.portal.x >= FX_INT(p->x) ? 1 : -1) * FXF(1.2); h->vy = 0; h->fell = 0;
            spark(G.portal.x, G.portal.y - 14, 10, 0xFFFFFFFFu, 2);
        }
        if (G.portal.t > 40 + 16 * G.portal.n + 30) G.portal.on = 0;
    }
    /* простая логика: на экране сам; иначе лифт (если виден) / край / вертолёт */
    for (int i = 0; i < NUM_HEROES; i++) {
        if (i == G.cur || !G.hero[i].active || G.hero[i].hp <= 0) {
            G.lost_t[i] = 0; continue;
        }
        if (G.solo_mode || !G.follow) { G.lost_t[i] = 0; continue; }
        if (G.heli.mask & (1 << i)) continue;
        if (!hero_lost(i)) { G.lost_t[i] = 0; G.lost_total[i] = 0; continue; }
        { int pr_ = level_room(FX_INT(p->x), FX_INT(p->y));                  /* игрок в закрытой/stealth-комнате: героев не возвращаем (иначе вертолёт открыл бы комнату) */
          if (pr_ < 0 || pr_ >= 12 || !G.explored[pr_] || G.stealth_cool > 0) { G.lost_t[i] = 0; continue; } }
        if (G.lost_total[i] < 100000) G.lost_total[i]++;
        if (++G.lost_t[i] > 240) { /* 4 с */
            G.lost_t[i] = 0;
            bring_heli(i); /* внутри: лифт если на экране → край → heli */
        }
    }
}


/* квесты до босса: только кнопки, панели, разрушаемые платформы (Nerd) */
static int quest_obj_done(const Obj *o) {
    int t = o->type;
    /* Nerd-платформы не блокируют босса (отдельный гейт по уровню) */
    if (t == O_SNOW) return o->on;
    if (t == O_KEY) return o->on || ((G.have_key & 1) != 0); /* подобран / have_key */
    if (t == O_BUTTON) {
        if (!o->visible) return -1;
        /* кнопка 4 этажа (после босса) — не для входа к боссу */
        if (o->y < FLOOR3_Y - 48) return -1;
        return o->on;
    }
    if (t == O_PANEL_FIX) {
        if (o->kind == 3) return -1;
        return o->on;
    }
    if (t == O_PANEL_BRIDGE) return o->on;
    return -1;
}
static void quest_count(int *done, int *total) {
    int d = 0, n = 0;
    const Arena *ar = &G.lv.arena;
    for (int i = 0; i < G.lv.nobj; i++) {
        const Obj *o = &G.lv.obj[i];
        int q = quest_obj_done(o);
        if (q < 0) continue;
        /* пропуск объектов в зоне босса */
        if (o->x + o->w >= ar->trig_x0 - 8 && o->x <= ar->trig_x1 + 8
            && o->y >= ar->ceil - 20 && o->y <= ar->floor + 12) continue;
        if (o->type == O_BUTTON && !o->visible) continue;
        n++;
        if (q) d++;
    }
    *done = d; *total = n;
}
int game_boss_unlocked(void) {
    if (getenv("NO_BOSS_GATE")) return 1;
    int d, n; quest_count(&d, &n);
    return n > 0 && d >= n;
}
static void boss_room_gate(void) {
    Actor *p = player();
    if (!p || p->hp <= 0) return;
    if (G.boss_state != BS_SLEEP) return;
    if (game_boss_unlocked()) return;
    const Arena *ar = &G.lv.arena;
    int px = FX_INT(p->x), py = FX_INT(p->y);
    if (px < ar->trig_x0 - 8 || px > ar->trig_x1 + 8) return;
    if (py < ar->ceil || py > ar->floor + 12) return;
    int cx, cy; stage_checkpoint(px, &cx, &cy);
    actor_teleport(p, cx, cy);
    p->vx = p->vy = 0; p->invuln_t = 30;
    { int d,n; quest_count(&d,&n); char b[64]; snprintf(b,sizeof b,"Boss locked: quest %d/%d",d,n); say(b); }
}


/* ВРАГИ за стеной → фаза сквозь несущую к героям (не герои) */
static void enemy_phase_walls(void) {
    Actor *p = player();
    if (!p || p->hp <= 0) return;
    int px = FX_INT(p->x), py = FX_INT(p->y);
    for (int i = 0; i < EN_MAX; i++) {
        Actor *e = &G.en[i];
        if (!e->active || e->hp <= 0) continue;
        int ex = FX_INT(e->x), ey = FX_INT(e->y);
        if (ABSI(ey - py) > 48) continue;
        if (!wall_between(ex, px, ey - 8) && !wall_between(ex, px, py - 8)) continue;
        if (e->nav_t > 0) { e->nav_t--; continue; }
        int step = ex < px ? 6 : -6;
        int x = ex;
        for (int k = 0; k < 100; k++) {
            x += step;
            if (!level_solid_rect_team(&G.lv, x - 2, ey - e->h + 4, x + 2, ey - 4, 1)) {
                int dest = x;
                for (int j = 0; j < 50; j++) {
                    dest += step;
                    if (!level_solid_rect_team(&G.lv, dest - e->w/2, ey - e->h, dest + e->w/2, ey - 1, 1)
                        && level_solid_rect(&G.lv, dest - 4, ey, dest + 4, ey + 1)) {
                        spark(ex, ey - 14, 8, 0xFFFF6040u, 2);
                        actor_teleport(e, dest, ey);
                        e->vx = e->vy = 0; e->invuln_t = 20; e->nav_t = 90;
                        spark(dest, ey - 14, 8, 0xFFFFA080u, 2);
                        break;
                    }
                }
                break;
            }
        }
    }
}
/* stealth-режим не дольше STEALTH_LIMIT: потом игрок гибнет и воскресает на checkpoint со сброшенными stealth/solo */
static void stealth_timer(void) {
    Actor *p = player();
    if (!p || p->hp <= 0 || G.boss_state != BS_SLEEP) { G.stealth_t = 0; return; }
    int ab = (p->kind == RIG_MISTRESS && (p->spin_t > 0 || G.domina_stealth)) || (p->kind == RIG_PENGI && p->sliding)
          || (p->kind == RIG_REFREEZER && p->misc > 0 && p->flip);
    int pr = level_room(FX_INT(p->x), FX_INT(p->y));
    int closed = pr >= 0 && pr < 12 && !G.explored[pr];
    if (getenv("STEALTH_TEST")) ab = 1;
    if (ab || closed || G.stealth_cool > 0 || G.domina_stealth) { if (G.stealth_t < STEALTH_LIMIT + 5) G.stealth_t++; }
    else G.stealth_t = 0;
    if (G.stealth_t >= STEALTH_LIMIT) {
        G.stealth_t = 0;
        G.domina_stealth = 0; G.solo_mode = 0; G.follow = 1; G.stealth_cool = 0;
        p->spin_t = 0; p->sliding = 0; if (p->kind == RIG_REFREEZER) p->misc = 0;
        p->fell = 1; p->invuln_t = 0;                                  /* respawn: последний активный checkpoint */
        actor_damage(p, 99, 0, 0, 0, 0);
        say("Stealth timeout - back to checkpoint");
        game_log("Stealth timeout: player killed, respawn at checkpoint");
    }
}
/* переход stealth-манёвра игрока в обычный режим: враги за преградой телепортируются к игроку */
static void stealth_end_enemy_follow(void) {
    static int prev;
    Actor *p = player();
    int now = 0;
    if (p && p->hp > 0) {
        if (p->kind == RIG_PENGI && p->sliding) now = 1;
        else if (p->kind == RIG_MISTRESS && (p->spin_t > 0 || G.domina_stealth)) now = 1;
        else if (p->kind == RIG_HERO7 && G.lv.water_y > 0 && FX_INT(p->x) >= G.lv.water_x0 && FX_INT(p->x) <= G.lv.water_x1 && FX_INT(p->y) - p->h / 2 > G.lv.water_y) now = 2;
    }
    if (prev == 1 && now == 0) enemies_gather_to_player();
    if (prev == 0 && now == 2) enemies_gather_to_player();
    prev = now;
}
static void group_auto_off(void) {
    Actor *p = player();
    if (!p) return;
    int stealth = 0;
    if (p->kind == RIG_MISTRESS && p->spin_t > 0) stealth = 1;
    if (p->kind == RIG_PENGI && p->sliding) stealth = 1;
    if (p->kind == RIG_REFREEZER && p->misc > 0 && p->flip) stealth = 1;
    if (G.domina_stealth) stealth = 1;
    if (stealth) {
        G.stealth_cool = 45;
        /* как Solo: союзники стоят, не бегут за игроком */
        for (int i = 0; i < NUM_HEROES; i++) {
            if (i == G.cur || !G.hero[i].active || G.hero[i].hp <= 0) continue;
            Actor *h = &G.hero[i];
            h->vx = 0; h->ai_blk = 0;
            h->spin_t = 0; h->sliding = 0;
            if (h->kind != RIG_REFREEZER) h->misc = 0;
        }
    } else if (G.stealth_cool > 0) G.stealth_cool--;
}
/* если игрок не в stealth и на той же платформе/этаже где потеряшки — автоматически включить группу */
static void group_auto_on(void) {
    /* сцепка только через Q: не авто-включаем follow */
    if (G.solo_mode) return;
    if (!G.follow) { G.follow = 1; return; } /* страховка: вне Solo всегда сцепка */
    if (G.domina_stealth || G.stealth_cool > 0) return;
    Actor *p = player();
    if (!p || p->hp <= 0 || !actor_on_ground(p, &G.lv)) return;
    /* stealth modes already turned follow off; only re-enable on solid platform/lift/bridge */
    if (p->kind == RIG_MISTRESS && p->spin_t > 0) return;
    if (p->kind == RIG_PENGI && p->sliding) return;
    if (p->kind == RIG_REFREEZER && p->misc > 0 && p->flip) return;
    int py = FX_INT(p->y);
    int any_lost = 0;
    { int pr = level_room(FX_INT(p->x), py); if (pr < 0 || pr >= 12 || !G.explored[pr]) return; }   /* только в открытой комнате */
    for (int i = 0; i < NUM_HEROES; i++) {
        if (i == G.cur || !G.hero[i].active || G.hero[i].hp <= 0) continue;
        if (ABSI(FX_INT(G.hero[i].y) - py) <= 24 && !wall_between(FX_INT(G.hero[i].x), FX_INT(p->x), py) &&
            same_platform(FX_INT(G.hero[i].x), FX_INT(p->x), py)) any_lost = 1;
    }
    if (any_lost) {
        G.follow = 1;
        G.group_t = 0;
        say("Group ON: allies found you");
    }
}
int game_ground_near(int x, int y) {                 /* ближайший x с твёрдой опорой под ногами (не в шахте/пропасти) */
    for (int d = 0; d < 400; d += 6)
        for (int s = -1; s <= 1; s += 2) {
            int xx = x + s * d;
            if (xx < 20 || xx > G.lv.w - 20) continue;
            if (level_solid_rect(&G.lv, xx - 6, y, xx + 6, y + 2) && !level_solid_rect(&G.lv, xx - 6, y - 24, xx + 6, y - 2)) return xx;
        }
    return x;
}
void game_revive_all(void) {
    Actor *p = player();
    int n = 0;
    for (int i = 0; i < NUM_HEROES; i++) {
        Actor *h = &G.hero[i];
        if (h->hp > 0) continue;
        if (G.lives[i] < 1) G.lives[i] = 1;
        G.out[i] = 0; G.dying[i] = 0;
        actor_init(h, HERO_KIND[i], 0, game_ground_near(FX_INT(p->x) + 16 + 14 * n, FX_INT(p->y)), FX_INT(p->y)); h->invuln_t = 90; n++;
    }
    if (n) { say("The boss is defeated: everyone is back!"); game_log("Boss defeated: %d heroes resurrected", n); }
}

static Intent player_intent(void) {
    Intent in;
    memset(&in, 0, sizeof in);
    const Input *I = &G.in, *P = &G.prev;
    in.dir = (I->right ? 1 : 0) - (I->left ? 1 : 0);
    in.up = I->up; in.down = I->down;
    in.jump_held = I->jump; in.jump_pressed = I->jump && !P->jump;
    in.melee = I->melee && !P->melee; in.ranged = I->ranged && !P->ranged; in.super = I->super && !P->super;
    in.special = I->special && !P->special;
    in.use = I->use;
    return in;
}

/* ===================================================================== головоломки */
static void hint(const char *s) { snprintf(g_hint, sizeof g_hint, "%s", s); }

static int i_is_boss_btn(int i) { return i == G.lv.obj_bbtn[0] || i == G.lv.obj_bbtn[1]; }

static void interact(int hi, int use_held, int use_edge) {
    Actor *h = &G.hero[hi];
    Level *lv = &G.lv;
    int px = FX_INT(h->x), py = FX_INT(h->y);
    g_hint[0] = 0;
    memset(g_worked, 0, sizeof g_worked);
    if (h->hp > 0 && h->hurt_t == 0 && h->stun_t == 0 && (actor_on_ground(h, lv) || (h->flip && ABSI(h->vy) < FXF(0.6))) && !h->sliding) {
        int best = -1, bd = 1 << 30;
        for (int i = 0; i < lv->nobj; i++) {
            Obj *o = &lv->obj[i];
            int t = o->type;
            if (t == O_BUTTON && !o->visible) continue;
            if (!(t == O_BUTTON || t == O_PANEL_FIX || t == O_PANEL_BRIDGE || t == O_SNOW || t == O_DOOR || t == O_EXIT ||
                  t == O_LIFT || (t >= O_WALL_ICE && t <= O_WALL_FIRE))) continue;
            if ((t == O_BUTTON || t == O_PANEL_FIX || t == O_PANEL_BRIDGE || t == O_SNOW || t == O_DOOR || (t >= O_WALL_ICE && t <= O_WALL_FIRE)) && o->on) continue;
            if (t == O_LIFT && o->kind != 2) continue; /* E только на Once Elevator (UOB) */
            int reach = (t >= O_WALL_ICE && t <= O_WALL_FIRE) ? 46 : INTERACT_REACH;
            int dx = px < o->x ? o->x - px : px > o->x + o->w ? px - (o->x + o->w) : 0;
            /* Refreezer на антиграв: кнопки под плитой — расширенный вертикальный reach */
            if (h->flip && t == O_BUTTON && o->kind == 3) {
                /* широкая зона: кнопка под плитой DF11 */
                if (dx > reach + 24) continue;
                if (ABSI((o->y + o->h / 2) - (py - h->h / 2)) > 64) continue;
            } else if (dx > reach || o->y >= py + 3 || o->y + o->h <= py - h->h) continue;
            if (dx < bd) { bd = dx; best = i; }
        }
        if (best < 0 && events_near(px, py)) { hint("E: answer the phone"); if (use_edge) { events_answer(hi); G.act_t[hi] = 20; } }
        if (best >= 0) {
            Obj *o = &lv->obj[best];
            switch (o->type) {
            case O_BUTTON:
                /* kind=3: light switch — только Refreezer; иначе Pengi; кнопки босса — любой */
                if (o->kind == 3) {
                    if (h->kind != RIG_REFREEZER) { hint("Refreezer flips light switches"); break; }
                    hint("E: flip light switch");
                    if (use_edge) {
                        o->on = 1; level_power(lv, o->target); G.act_t[hi] = 24;
                        if (h->misc > 90) h->misc = 90;                       /* переключатель нажат: спецрежим скоро закончится */
                        spark(o->x + 7, o->y + 6, 10, 0xFFFFF080u, 2);
                        say("Light switch ON — elevator powered");
                        sound_play_sfx_at(SFX_PANEL, o->x, o->y);
                    }
                    break;
                }
                if (!i_is_boss_btn(best) && h->kind != RIG_PENGI) { hint("Pengi presses buttons"); break; }
                hint("E: press button");
                if (use_edge) { o->on = 1; if (i_is_boss_btn(best)) boss_on_button(best); else level_power(lv, o->target); G.act_t[hi] = 24; spark(o->x + 7, o->y + 6, 8, 0xFF60FF80u, 2); say("Button pressed"); sound_play_sfx_at(SFX_PANEL, o->x, o->y); }
                break;
            case O_PANEL_FIX:
                /* kind=3: панель антигравитации Refreezer (жёлтая стрелка вверх) */
                if (o->kind == 3) {
                    if (h->kind != RIG_REFREEZER) { hint("Refreezer only: reverse gravity"); break; }
                    if (h->misc > 0) { hint("Gravity already reversed"); break; }
                    /* gp12: нужен DF11 (target bridge уже on) */
                    if (o->target >= 0 && o->target < lv->nobj) {
                        Obj *dep = &lv->obj[o->target];
                        if (dep->type == O_BRIDGE && !dep->on) {
                            hint("Need Domina bridge DF11 first");
                            break;
                        }
                    }
                    hint("E: reverse gravity");
                    if (use_edge) {
                        actor_set_flip(h, lv, 1);
                        h->misc = 1200;
                        h->fall_immune = 1;
                        G.flip_on = 1;
                        G.act_t[hi] = 24;
                        say("Anti-gravity ON — press ceiling switch RB12");
                        sound_play_sfx_at(SFX_PANEL, o->x, o->y);
                        spark(o->x + 8, o->y + 10, 16, 0xFFFFE060u, 2);
                    }
                    break;
                }
                if (h->kind != RIG_BUM) { hint("Cyberbum repairs panels"); break; }
                hint("E (hold): repair panel");
                if (use_held) {
                    g_worked[best] = 1; G.act_t[hi] = 2;
                    if (++o->progress >= REPAIR_FRAMES) { o->on = 1; level_power(lv, o->target); say("Panel repaired, the lift has power"); sound_play_sfx_at(SFX_PANEL, o->x, o->y); spark(o->x + 8, o->y + 10, 14, 0xFF60FF80u, 2); }
                }
                break;
            case O_PANEL_BRIDGE:
                if (h->kind != RIG_MISTRESS) { hint("Domina activates bridges"); break; }
                hint("E: activate bridge");
                if (use_edge) {
                    o->on = 1;
                    level_power(lv, o->target);
                    /* сразу solid — иначе падение сквозь DF11 в тот же кадр */
                    if (o->target >= 0 && o->target < lv->nobj) {
                        Obj *br = &lv->obj[o->target];
                        if (br->type == O_BRIDGE || br->type == O_LIFT) {
                            br->on = 1; br->solid = 1; br->visible = 1;
                            if (br->have < br->need) br->have = br->need;
                        }
                    }
                    G.act_t[hi] = 30;
                    spark(o->x + 8, o->y + 12, 16, 0xFFE060FFu, 2);
                    say("Bridge appeared!");
                    sound_play_sfx_at(SFX_PANEL, o->x, o->y);
                    if (o->target >= 0 && o->target < lv->nobj) {
                        Obj *br = &lv->obj[o->target];
                        int bx = br->x + br->w / 2;
                        int by = br->y;
                        spark(FX_INT(h->x), FX_INT(h->y) - 14, 12, 0xFF80E0FFu, 3);
                        actor_teleport(h, bx, by);
                        h->vx = h->vy = 0;
                        h->post_x = bx; h->post_y = by;
                        h->spin_t = 0; h->soft = 0; h->fall_immune = 0;
                        h->stand_obj = o->target;
                        spark(bx, by - 14, 14, 0xFF80E0FFu, 3);
                        /* открыть комнату моста, снять stealth */
                        {
                            int r = level_room(bx, by);
                            if (r >= 0 && r < 12) G.explored[r] = 1;
                        }
                        G.domina_stealth = 0;
                        G.bridge_tp_t = 45; /* ~0.75с запрет T */
                        G.bridge_tp_x = bx; G.bridge_tp_y = by;
                        if (!G.solo_mode) { G.follow = 1; group_gather(); }
                        say("Domina teleported to bridge");
                    }
                }
                break;
            case O_LIFT:
                /* UOB (Once Elevator): E только если все живые герои на лифте */
                if (o->kind != 2 || o->state != LS_TOP || o->progress) break;
                {
                    int all = 1, any = 0;
                    for (int k = 0; k < NUM_HEROES; k++) {
                        Actor *hh = &G.hero[k];
                        if (!hh->active || hh->hp <= 0) continue;
                        any = 1;
                        if (hh->stand_obj != best) { all = 0; break; }
                    }
                    if (!any || !all) { hint("All heroes must stand on the lift"); break; }
                    hint("E: lower Once Elevator");
                    if (use_edge) {
                        o->have = 99;
                        o->state = LS_DOWN;
                        G.act_t[hi] = 20;
                        say("Once Elevator descending");
                        sound_play_sfx_at(SFX_PANEL, o->x, o->y);
                    }
                }
                break;
            case O_SNOW:
                if (h->kind != RIG_DOORKEEPER) { hint("Aunty clears the snow"); break; }
                hint("E (hold): clear snow");
                if (use_held) {
                    g_worked[best] = 1; G.act_t[hi] = 2;
                    if (h->anim.id == A_ACTIVATE && (G.frame & 7) == 0) spark(o->x + (int)(g_rand() % (uint32_t)o->w), o->y + 4, 3, 0xFFF4F8FCu, 2);
                    if (++o->progress >= SWEEP_FRAMES) {
                        o->on = 1;
                        if (o->target >= 0) { lv->obj[o->target].visible = 1; }
                        say("A key under the snow!"); spark(o->x + o->w / 2, o->y + 6, 20, 0xFFF4F8FCu, 2);
                    }
                }
                break;
            case O_DOOR: {
                /* ключ DOOR1 (bit0) открывает только kind=0; ключ DOOR2 (bit1) — kind=1 */
                int need = (o->kind == 1) ? 2 : 1;
                if (!(G.have_key & need)) {
                    hint(o->kind == 1 ? "Need DOOR2 key" : "Need DOOR1 key");
                    break;
                }
                hint("E: open gate with matching key");
                if (use_edge) {
                    o->on = 1; o->timer = 0; o->solid = 0;
                    G.act_t[hi] = 30;
                    say(o->kind == 1 ? "DOOR2 open" : "DOOR1 open");
                    sound_play_sfx_at(SFX_DOOR, o->x, o->y);
                    spark(o->x + 8, o->y + 24, 12, 0xFFB0B8C8u, 2);
                }
                break; }
            case O_EXIT:
                if (!G.have_artifact) { hint("Exit closed: artifact needed"); break; }
                {
                    int all_near = 1, ex = o->x + o->w / 2, ey = o->y + o->h;
                    for (int k = 0; k < NUM_HEROES; k++) {
                        Actor *hh = &G.hero[k];
                        if (!hh->active || hh->hp <= 0) continue;
                        if (ABSI(FX_INT(hh->x) - ex) > 48 || ABSI(FX_INT(hh->y) - ey) > 40)
                            all_near = 0;
                    }
                    if (!all_near) { hint("Bring all heroes to the exit with the artifact"); break; }
                }
                hint("E: leave the level (all together)");
                if (use_edge) { G.mode = MODE_WIN; G.win_t = 0; sound_play_bgm(BGM_WIN); }
                break;
            case O_WALL_ICE: hint(h->kind == RIG_PENGI ? "Ice wall: Pengi's super attack (L)" : "Only Pengi's super breaks ice"); break;
            case O_WALL_WOOD: hint(h->kind == RIG_BUM ? "Wooden wall: Cyberbum's wrench (Z)" : "Cyberbum breaks wood"); break;
            case O_WALL_STONE: hint(h->kind == RIG_MISTRESS ? "Stone wall: Domina's chainsaw (X)" : "Domina saws stone"); break;
            case O_WALL_FIRE: hint(h->kind == RIG_BUM ? "Fire: throw the bottle (C)" : "Cyberbum's bottle puts out fire"); break;
            default: break;
            }
        }
    }
    for (int i = 0; i < lv->nobj; i++)                       /* незавершённая работа остывает */
        if (!g_worked[i] && lv->obj[i].progress > 0 && !lv->obj[i].on) { lv->obj[i].progress -= 2; if (lv->obj[i].progress < 0) lv->obj[i].progress = 0; }
}

static void pickups(void) {
    Level *lv = &G.lv;
    Obj *a = &lv->obj[lv->obj_artifact];
    for (int i = 0; i < NUM_HEROES; i++) {
        Actor *h = &G.hero[i];
        if (h->hp <= 0) continue;
        int x0, y0, x1, y1; actor_box(h, &x0, &y0, &x1, &y1);
        for (int j = 0; j < lv->nobj; j++) {
            Obj *k = &lv->obj[j];
            if (k->type != O_KEY || !k->visible || k->on) continue;
            if (x1 >= k->x && x0 <= k->x + k->w && y1 >= k->y && y0 <= k->y + k->h) {
                k->on = 1; k->visible = 0;
                /* kind 0 = DOOR1, kind 1 = DOOR2, kind 2+ = телепорт-бонус */
                if (k->kind == 0) { G.have_key |= 1; say("DOOR1 key found!"); }
                else if (k->kind == 1) { G.have_key |= 2; say("DOOR2 key found!"); }
                else { if (G.tele < MAX_TELEPORTS) G.tele++; say("Teleport artifact under the snow!"); }
                sound_play_sfx(SFX_PICKUP);
                spark(k->x + 8, k->y + 8, 14, 0xFFE8C030u, 2);
            }
        }
        if (a->visible && !a->on && x1 >= a->x && x0 <= a->x + a->w && y1 >= a->y && y0 <= a->y + a->h) {
            a->on = 1; a->visible = 0; G.have_artifact = 1; lv->obj[lv->obj_exit].on = 1;
            if (a->target >= 0) level_power(lv, a->target);  /* DBOSSLT auto-lift after artifact */
            say("Artifact! The exit is open"); sound_play_sfx(SFX_PICKUP);
            spark(a->x + 8, a->y + 8, 30, 0xFF60C0FFu, 3);
        }
    }
}

/* ===================================================================== тестовые команды (GAME_CMDS) */
typedef struct { int frame; char op[8]; int a, b, c; } Cmd;
static Cmd g_cmd[96]; static int g_ncmd, g_trace;

static void cmds_parse(void) {
    const char *s = getenv("GAME_CMDS");
    g_trace = getenv("GAME_TRACE") ? atoi(getenv("GAME_TRACE")) : 0;
    while (s && *s && g_ncmd < 96) {
        Cmd *c = &g_cmd[g_ncmd];
        memset(c, 0, sizeof *c);
        if (sscanf(s, "%d:%7[a-z]:%d:%d:%d", &c->frame, c->op, &c->a, &c->b, &c->c) < 2) break;
        g_ncmd++;
        const char *e = strchr(s, ',');
        if (!e) break;
        s = e + 1;
    }
}

static void cmds_run(void) {
    for (int i = 0; i < g_ncmd; i++) {
        Cmd *c = &g_cmd[i];
        if (c->frame != G.frame) continue;
        if (!strcmp(c->op, "tp")) { Actor *h = &G.hero[c->a % NUM_HEROES]; actor_teleport(h, c->b, c->c); h->post_x = c->b; h->post_y = c->c; }
        else if (!strcmp(c->op, "pick")) switch_hero(c->a);
        else if (!strcmp(c->op, "key")) { G.have_key = 1; G.lv.obj[G.lv.obj_key].on = 1; G.lv.obj[G.lv.obj_key].visible = 0; }
        else if (!strcmp(c->op, "open")) { Obj *o = &G.lv.obj[c->a]; if (o->type >= O_DOOR && o->type <= O_WALL_FIRE) break_wall(o); else o->on = 1; }
        else if (!strcmp(c->op, "art")) { G.have_artifact = 1; G.lv.obj[G.lv.obj_exit].on = 1; }
        else if (!strcmp(c->op, "spawn")) { for (int k = 0; k < EN_MAX; k++) if (!G.en[k].active) { actor_init(&G.en[k], c->a, 1, c->b, c->c); G.en[k].post_x = c->b; break; } }
        else if (!strcmp(c->op, "power")) level_power(&G.lv, c->a);
        else if (!strcmp(c->op, "kill")) { for (int k = 0; k < EN_MAX; k++) if (G.en[k].active && G.en[k].hp > 0) { G.en[k].invuln_t = 0; actor_damage(&G.en[k], 99, 1, FXF(2), FXF(2), 10); } }
        else if (!strcmp(c->op, "hp")) { G.hero[c->a % NUM_HEROES].hp = c->b; }
        else if (!strcmp(c->op, "bosshp")) { if (G.boss_state != BS_SLEEP && G.boss_state != BS_DEAD) { G.boss.hp = c->a; } }
        else if (!strcmp(c->op, "level")) game_set_level_no(c->a);
        else if (!strcmp(c->op, "objs")) { for (int k = 0; k < G.lv.nobj; k++) { const Obj *o = &G.lv.obj[k]; if (o->type == O_PLAT || o->type == O_ROPE || o->type == O_BRIDGE) continue; fprintf(stderr, "OBJ %d type=%d kind=%d x=%d y=%d on=%d vis=%d need=%d have=%d\n", k, o->type, o->kind, o->x, o->y, o->on, o->visible, o->need, o->have); } fprintf(stderr, "ARENA %d..%d trig %d..%d\n", G.lv.arena.x0, G.lv.arena.x1, G.lv.arena.trig_x0, G.lv.arena.trig_x1); }
        else if (!strcmp(c->op, "bbtn")) { if (G.lv.obj_bbtn[c->a & 1] >= 0) boss_on_button(G.lv.obj_bbtn[c->a & 1]); }
        else if (!strcmp(c->op, "end")) { G.mode = MODE_END; G.end_t = 0; }
        else if (!strcmp(c->op, "card")) { G.card_t = 150; }
        else if (!strcmp(c->op, "nospawn")) { G.spawn_t = 1 << 28; }
        else if (!strcmp(c->op, "bonus")) { events_cheat_bonus(); }
        else if (!strcmp(c->op, "heli")) { bring_heli(c->a); }
        else if (!strcmp(c->op, "lift")) { bring_lift(c->a); }
        else if (!strcmp(c->op, "explore")) { memset(G.explored, 1, sizeof G.explored); }
        else if (!strcmp(c->op, "group")) { G.follow = c->a; if (G.follow) group_gather(); }
        else if (!strcmp(c->op, "tele")) { game_teleport_team(); }
        else if (!strcmp(c->op, "stealth")) { G.domina_stealth = c->a; }
        else if (!strcmp(c->op, "dump")) {
            for (int k = 0; k < G.lv.nobj; k++) { const Obj *o = &G.lv.obj[k]; fprintf(stderr, "obj %2d type=%d kind=%d x=%d y=%d w=%d h=%d target=%d need=%d have=%d on=%d st=%d\n", k, o->type, o->kind, o->x, o->y, o->w, o->h, o->target, o->need, o->have, o->on, o->state); }
        }
    }
}

/* ===================================================================== фон: машины */
static void cars_init(const char *dir) {
    char p[512], line[128];
    snprintf(p, sizeof p, "%s/cars.txt", dir);
    FILE *f = fopen(p, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f) && G.car_n < NCARS) {
        int idx, x, y, w;
        if (sscanf(line, "%d %d %d %d", &idx, &x, &y, &w) != 4) continue;
        snprintf(p, sizeof p, "%s/car%d.bmp", dir, idx);
        int n = G.car_n;
        if (!image_load_bmp(&G.car[n], p)) continue;
        G.car_x[n] = x * 100; G.car_y[n] = y; G.car_w[n] = G.car[n].w;
        G.car_v[n] = 25 + (idx * 37) % 55;
        G.car_n++;
    }
    fclose(f);
}

static void cars_update(void) {
    if (G.map_no >= 2) return; /* indoor maps — без уличных машин */
    for (int i = 0; i < G.car_n; i++) {
        G.car_x[i] -= G.car_v[i];
        if (G.car_x[i] / 100 + G.car_w[i] < 0) G.car_x[i] = (VIEW_W + 8 + (int)(g_rand() % 120u)) * 100;
    }
}

/* ===================================================================== инициализация */
void game_set_start(int x, int y) { G.start_x = x; G.start_y = y; }

char g_assets[512];
static const char *const QUEST_NAME[9] = {"Urban City", "Concert Hall", "Construction Site", "Library Room", "Toxic Lab", "Entrance", "Main Factory", "Restricted Area", "Kozem Core"};
static Image g_epi_img;
static Image g_heli_img, g_prolog_img, g_computer_img, g_piano_img, g_baty_land, g_baty_water;

int game_init(const char *dir) {
    int sx = G.start_x, sy = G.start_y;
    memset(&G, 0, sizeof G);
    G.start_x = sx; G.start_y = sy;
    G.rng = 0x1234567u;
    snprintf(g_assets, sizeof g_assets, "%s", dir ? dir : "assets");
    if (!level_load(&G.lv, g_assets, 1)) return 0;
    G.fb = image_create(VIEW_W, VIEW_H);
    char p[512];
    for (int i = 0; i < NUM_HEROES; i++) { snprintf(p, sizeof p, "%s/portrait_%s.bmp", g_assets, HERO_FILE[i]); image_load_bmp(&G.portrait[i], p); }
    { char hp_[600]; snprintf(hp_, sizeof hp_, "%s/heli.bmp", g_assets); image_load_bmp(&g_heli_img, hp_);
      snprintf(hp_, sizeof hp_, "%s/prolog.bmp", g_assets); if (!g_prolog_img.px) image_load_bmp(&g_prolog_img, hp_);
      snprintf(hp_, sizeof hp_, "%s/computer.bmp", g_assets); if (!g_computer_img.px) image_load_bmp(&g_computer_img, hp_);
      snprintf(hp_, sizeof hp_, "%s/piano.bmp", g_assets); if (!g_piano_img.px) image_load_bmp(&g_piano_img, hp_);
      snprintf(hp_, sizeof hp_, "%s/baty_land.bmp", g_assets); if (!g_baty_land.px) image_load_bmp(&g_baty_land, hp_);
      snprintf(hp_, sizeof hp_, "%s/baty_water.bmp", g_assets); if (!g_baty_water.px) image_load_bmp(&g_baty_water, hp_);
      snprintf(hp_, sizeof hp_, "%s/epilogue.bmp", g_assets); if (!g_epi_img.px) image_load_bmp(&g_epi_img, hp_);
      G.prolog_t = (g_prolog_img.px && !getenv("NULL_FRAMES") && !getenv("GAME_CMDS")) ? 360 : 0;
      G.card_t = (!getenv("NULL_FRAMES") && !getenv("GAME_CMDS")) ? 150 : 0; }
    cars_init(g_assets);
    events_init(g_assets);
    lives_reset();
    G.full_respawns = 0;
    G.gameover_t = 0; G.paused = 0;
    G.debug_quest = g_debug_quest_ini || (getenv("DEBUG_QUEST") != NULL);
    memset(&G.heli, 0, sizeof G.heli); G.bridge_tp_t = 0; memset(&G.portal, 0, sizeof G.portal); memset(G.lost_t, 0, sizeof G.lost_t);
    G.solo_mode = 0; G.follow = 1; G.cp_stage = 0; G.sprinter[G.cur] = 1; G.hero_gid[G.cur] = 0;
    memset(G.explored, 0, sizeof G.explored); memset(G.ambushed, 0, sizeof G.ambushed);
    memset(&G.heli, 0, sizeof G.heli);
    G.cur = 0; G.follow = 1; G.level_no = 1; G.map_no = 1; G.boss_kind = BOSS_RIG[0];
    setup_checkpoints(); /* строго после level_no */
    {
        int sx = G.lv.hero_x[0], sy = G.lv.hero_y[0];
        G.explored[level_room(sx, sy)] = 1; G.ambushed[level_room(sx, sy)] = 1;
        for (int r_ = 0; r_ < 12; r_++) if (G.lv.room_type[r_] == 1) G.explored[r_] = 1;
        G.pl_gy = sy;
    }
    for (int i = 0; i < NUM_HEROES; i++) init_hero(i);
    if (G.start_x >= 0) { actor_teleport(&G.hero[0], G.start_x, G.start_y >= 0 ? G.start_y : FLOOR1_Y); G.hero[0].post_x = G.start_x; }
    ai_init();
    spawn_add(1); spawn_add(1);
    G.spawn_t = 600; /* 10 с на старте level 1 */
    G.spawn_fly_t = 600;
    G.fade_t = 45;
    G.lift_enemy_floor = -1;
    game_log("Game start: level 1, boss %s. PgUp/PgDn = debug level 1..9", BOSS_NAME[0]);
    cmds_parse();
    say("1-4 or Tab: switch hero. To the gate!");
    return 1;
}

void game_shutdown(void) {
    level_free(&G.lv);
    image_free(&G.fb);
    for (int i = 0; i < NUM_HEROES; i++) image_free(&G.portrait[i]);
    image_free(&g_heli_img);
    for (int i = 0; i < G.car_n; i++) image_free(&G.car[i]);
}

const uint32_t *game_pixels(void) { return G.fb.px; }

/* ===================================================================== обновление */
static void camera_update(void) {
    Actor *p = player();
    int px = FX_INT(p->x), py = FX_INT(p->y);
    int tx = px - VIEW_W / 2, ty = py - 168;
    int maxx = G.lv.w - VIEW_W, maxy = G.lv.h - VIEW_H;
    if (tx < 0) tx = 0;
    if (tx > maxx) tx = maxx;
    if (ty < 0) ty = 0;
    if (ty > maxy) ty = maxy;
    if (!G.cam_init) { G.cam_x = tx; G.cam_y = ty; G.cam_init = 1; return; }
    int dx = tx - G.cam_x, dy = ty - G.cam_y;
    if (ABSI(dx) > 20) { int s = dx > 0 ? 1 : -1; int st = (dx - s * 20) / 6; if (!st) st = s; G.cam_x += st; }
    if (ABSI(dy) > 3) { int s = dy > 0 ? 1 : -1; int st = (dy - s * 3) / 10; if (!st) st = s; G.cam_y += st; }
    if (G.cam_x < 0) G.cam_x = 0;
    if (G.cam_x > maxx) G.cam_x = maxx;
    if (G.cam_y < 0) G.cam_y = 0;
    if (G.cam_y > maxy) G.cam_y = maxy;
}

static void sparks_update(void) {
    for (int i = 0; i < FX_MAX; i++) {
        Spark *s = &G.sp[i];
        if (!s->active) continue;
        if (--s->life <= 0) { s->active = 0; continue; }
        s->x += s->vx; s->y += s->vy; s->vy += FXF(0.12);
    }
}

void game_update(const Input *in) {
    static int settings_once;
    if (!settings_once) { load_settings_ini(); settings_once = 1; }
    G.prev = G.in; G.in = *in;
    if (G.prolog_t > 0) {      /* пролог: любая клавиша пропускает */
        G.prolog_t--;
        if (G.frame > 0 || G.prolog_t < 330) {
            if (in->jump || in->melee || in->ranged || in->use || in->super || in->group || in->tele) G.prolog_t = 0;
        }
        return;
    }
    if (G.mode == MODE_END) {                                   /* эпилог: The Bad End */
        G.end_t++;
        if (G.end_t > 300 && (in->jump || in->melee || in->ranged || in->use || in->super || in->group || in->tele)) {
            G.mode = MODE_PLAY; G.end_t = 0; G.full_respawns = 0; lives_reset(); game_set_level_no(1); sound_play_bgm(BGM_LEVEL);
        }
        return;
    }
    if (G.card_t > 0) { G.card_t--; return; }                  /* заставка «Quest N: …» — симуляция стоит */
    /* пауза: P — как на старых консолях; не тикает симуляция */
    if (in->pause && !G.prev.pause) {
        G.paused = !G.paused;
        sound_pause_all(G.paused);
        if (G.paused) say("PAUSED"); else say("Resume");
    }
    if (G.paused) return;
    cheat_codes_update(in, &G.prev);
    G.frame++;
    if (G.fade_t > 0) G.fade_t--;
    if (in->debug && !G.prev.debug) G.debug = !G.debug;
    if (in->quest_dbg && !G.prev.quest_dbg) {
        G.debug_quest = !G.debug_quest;
        say(G.debug_quest ? "Quest debug ON (yellow outlines)" : "Quest debug OFF");
    }
    cmds_run();
    if (G.gameover_t > 0) {
        G.gameover_t--;
        sparks_update(); cars_update();
        if (G.gameover_t == 0) {
            /* после GAME OVER — уровень с самого начала, счётчик респавнов сброшен */
            G.full_respawns = 0;
            lives_reset();
            game_set_level_no(G.level_no);
            sound_play_bgm(BGM_LEVEL);
        }
        return;
    }
    if (G.mode == MODE_WIN) {
        G.win_t++;
        sparks_update(); cars_update();
        /* 0..60 fade→black, 60..100 hold, 100 load next + fade-in */
        if (G.win_t == 100) {
            int next = G.level_no + 1;
            if (next > NUM_LEVELS) {                          /* после последнего уровня — эпилог */
                G.mode = MODE_END; G.end_t = 0; G.win_t = 0; sound_stop_bgm(); sound_play_bgm(BGM_GAMEOVER);
                return;
            }
            G.mode = MODE_PLAY; G.win_t = 0;
            G.full_respawns = 0;
            game_set_level_no(next);
            sound_stop_bgm();
            sound_play_bgm(BGM_LEVEL);
        }
        return;
    }

    for (int i = 0; i < NUM_HEROES; i++) if (in->pick[i] && !G.prev.pick[i]) switch_hero(i);
    if (in->next && !G.prev.next)
        for (int k = 1; k <= NUM_HEROES; k++) {
            int t = (G.cur + k) % NUM_HEROES;
            if (G.hero[t].hp > 0 || (!G.out[t] && G.boss_hide_allies)) { switch_hero(t); break; }
        }
    if (in->prev_hero && !G.prev.prev_hero)
        for (int k = 1; k <= NUM_HEROES; k++) {
            int t = (G.cur - k + NUM_HEROES) % NUM_HEROES;
            if (G.hero[t].hp > 0 || (!G.out[t] && G.boss_hide_allies)) { switch_hero(t); break; }
        }
    /* карта изученных комнат + сторона подхода игрока */
    {
        Actor *p = player();
        if (p && p->hp > 0) {
            if (p->on_rope) G.pl_rope_t = 90; else if (G.pl_rope_t > 0 && actor_on_ground(p, &G.lv)) G.pl_rope_t--; else if (G.pl_rope_t > 0) G.pl_rope_t--;
            mark_explored(FX_INT(p->x), FX_INT(p->y));
            /* направление движения → «откуда пришёл» = противоположная сторона */
            if (p->vx > FXF(0.4)) G.approach_dir = -1;
            else if (p->vx < -FXF(0.4)) G.approach_dir = 1;
            else if (p->facing) G.approach_dir = -p->facing;
            /* waypoints: точка каждые ~48px или смена этажа / лифт */
            {
                int px = FX_INT(p->x), py = FX_INT(p->y);
                int li = (p->stand_obj >= 0 && p->stand_obj < G.lv.nobj && G.lv.obj[p->stand_obj].type == O_LIFT)
                         ? p->stand_obj : -1;
                if (li >= 0) G.last_player_lift = li;
                int need = 0;
                if (G.wp_n == 0) need = 1;
                else {
                    int lx = G.wp_x[G.wp_n - 1], ly = G.wp_y[G.wp_n - 1];
                    if (ABSI(px - lx) > 48 || ABSI(py - ly) > 24) need = 1;
                    if (li >= 0 && G.wp_lift[G.wp_n - 1] != li) need = 1;
                }
                if (need && G.follow && !G.solo_mode) {
                    if (G.wp_n >= 48) {
                        for (int k = 0; k < 47; k++) {
                            G.wp_x[k] = G.wp_x[k+1]; G.wp_y[k] = G.wp_y[k+1]; G.wp_lift[k] = G.wp_lift[k+1];
                        }
                        G.wp_n = 47;
                    }
                    G.wp_x[G.wp_n] = px; G.wp_y[G.wp_n] = py; G.wp_lift[G.wp_n] = li;
                    G.wp_n++;
                }
            }
        }
    }

    /* Q: нечёт = group+waypoints; чёт = solo (все в root ждут). Закрытая комната — запрет. */
    if (in->group && !G.prev.group) {
        Actor *p = player();
        int px = p ? FX_INT(p->x) : 0, py = p ? FX_INT(p->y) : 0;
        int closed = p && !room_explored(level_room(px, py));
        if (G.domina_stealth || closed) {
            say(closed ? "Closed room: Q locked (stealth only)" : "Stealth: group locked");
        } else {
            int odd = !(G.follow && !G.solo_mode);   /* от реального состояния, а не от счётчика */
            G.q_presses = odd ? 1 : 0;
            int on_lift = p && p->stand_obj >= 0 && p->stand_obj < G.lv.nobj
                          && G.lv.obj[p->stand_obj].type == O_LIFT;
            if (odd) {
                /* GROUP ON: якорь, waypoints, созыв */
                G.follow = 1; G.solo_mode = 0; G.group_t = 0;
                if (on_lift) {
                    G.lift_call_t = 60;
                    int li = p->stand_obj;
                    Obj *lo = &G.lv.obj[li];
                    G.last_player_lift = li;
                    /* ВСЕ герои группы — в точку игрока на лифте */
                    for (int i = 0; i < NUM_HEROES; i++) {
                        if (!G.hero[i].active || G.hero[i].hp <= 0) continue;
                        if (G.sprinter[i] && i != G.cur) continue;
                        G.hero[i].ai_blk = 0; G.hero[i].misc = li;
                        G.hero[i].attack_kind = AT_NONE;
                        if (i != G.cur) {
                            spark(FX_INT(G.hero[i].x), FX_INT(G.hero[i].y) - 14, 6, 0xFF80E0FFu, 2);
                            actor_teleport(&G.hero[i], px + (i - G.cur) * 6, py);
                            G.hero[i].stand_obj = li;
                            G.hero[i].vx = G.hero[i].vy = 0;
                            G.hero_gid[i] = G.hero_gid[G.cur];
                        }
                    }
                    int dest_y = (lo->state == LS_UP || lo->state == LS_DOWN)
                        ? ((lo->state == LS_UP) ? lo->y1 : lo->y0)
                        : ((ABSI(py - lo->y0) < ABSI(py - lo->y1)) ? lo->y1 : lo->y0);
                    G.lift_enemy_floor = -1; (void)dest_y;      /* враги едут на лифте сами (nav_lift), мгновенного телепорта нет */
                    say("Lift: group locked in");
                } else {
                    /* Group ON: все в gid игрока; рядом — телепорт, далеко — вертолёт */
                    for (int i = 0; i < NUM_HEROES; i++) {
                        if (i == G.cur || !G.hero[i].active || G.hero[i].hp <= 0) continue;
                        G.sprinter[i] = 0;
                        G.hero_gid[i] = G.hero_gid[G.cur];
                        G.lost_t[i] = 0;
                        G.hero[i].ai_blk = 0;
                        int hx = FX_INT(G.hero[i].x), hy = FX_INT(G.hero[i].y);
                        (void)hx; (void)hy;
                        bring_heli(i);                       /* ступени: сам / в кабину / от края экрана / вертолёт — без мгновенного телепорта */
                    }
                    say("Group ON: gather / heli");
                }
            } else {
                /* SOLO: герои ждут рядом с игроком (не в post, не закрывая комнату) */
                G.follow = 0; G.solo_mode = 1;
                G.wp_n = 0;
                for (int i = 0; i < NUM_HEROES; i++) {
                    if (i != G.cur) G.sprinter[i] = 0;
                    G.hero[i].ai_blk = 0;
                    G.hero[i].vx = 0;
                    G.lost_t[i] = 0;
                    /* остаются на своих местах */
                    G.hero[i].post_x = FX_INT(G.hero[i].x);
                    G.hero[i].post_y = FX_INT(G.hero[i].y);
                }
                say("Solo mode: heroes stay put");
            }
        }
    }
    if (G.lift_call_t > 0) G.lift_call_t--;
    /* после Q-лифта: когда лифт остановился у dest — телепорт врагов на этот этаж */
    if (G.lift_enemy_floor >= 0) {
        Actor *p = player();
        int ready = 0;
        if (p && p->stand_obj >= 0 && p->stand_obj < G.lv.nobj) {
            Obj *lo = &G.lv.obj[p->stand_obj];
            if (lo->type == O_LIFT && (lo->state == LS_TOP || lo->state == LS_BOTTOM)
                && ABSI(lo->y - G.lift_enemy_floor) <= 12)
                ready = 1;
        }
        if (ready || G.lift_call_t == 0) {
            int fy = G.lift_enemy_floor;
            int plx = p ? FX_INT(p->x) : 80;
            for (int k = 0; k < EN_MAX; k++) {
                Actor *e = &G.en[k];
                if (!e->active || e->hp <= 0) continue;
                spark(FX_INT(e->x), FX_INT(e->y) - 14, 10, 0xFF80E0FFu, 2);
                int sx = plx + ((k & 1) ? 90 : -90) + (k % 3) * 20;
                if (sx < 32) sx = 32; if (sx > WORLD_W - 32) sx = WORLD_W - 32;
                actor_teleport(e, sx, fy);
                e->post_x = sx; e->post_y = fy; e->vx = e->vy = 0;
                e->attack_kind = AT_NONE; e->stun_t = 30;
                spark(sx, fy - 14, 10, 0xFFFFFFFFu, 2);
            }
            G.lift_enemy_floor = -1;
        }
    }
    if (in->tele && !G.prev.tele) {
        if (G.bridge_tp_t > 0)
            say("Teleport locked: arriving on bridge");
        else if (G.domina_stealth && player() && player()->kind == RIG_MISTRESS)
            say("Stealth: teleport locked");
        else game_teleport_team();
    }
    if (G.bridge_tp_t > 0) G.bridge_tp_t--;
    /* Domina stealth: юла + (закрытая комната ИЛИ стена до героев) */
    {
        Actor *p = player();
        int di = -1;
        for (int i = 0; i < NUM_HEROES; i++) if (G.hero[i].kind == RIG_MISTRESS) di = i;
        Actor *dom = di >= 0 ? &G.hero[di] : NULL;
        if (dom && dom->active && (dom->spin_t > 0 || G.domina_stealth)) {
            int dx = FX_INT(dom->x), dy = FX_INT(dom->y);
            int wall = 0, near_ally = 0;
            for (int i = 0; i < NUM_HEROES; i++) {
                if (i == di || !G.hero[i].active || G.hero[i].hp <= 0) continue;
                int hx = FX_INT(G.hero[i].x), hy = FX_INT(G.hero[i].y);
                if (ABSI(hy - dy) > 48) continue;
                if (wall_between(dx, hx, dy)) wall = 1;
                else if (ABSI(hx - dx) < 80) near_ally = 1;
            }
            int r = level_room(dx, dy);
            int closed = (r >= 0 && r < 12 && !G.explored[r]);
            if (dom->spin_t > 0 || closed || wall) {
                G.domina_stealth = 1;
                G.follow = 0;
                /* союзники не копируют юлу / не лезут в stealth-зону */
                for (int i = 0; i < NUM_HEROES; i++) {
                    if (i == di) continue;
                    G.hero[i].spin_t = 0;
                    G.hero[i].sliding = 0;
                }
            }
            /* выкл stealth: рядом с героями или открытая комната, на земле, без юлы */
            if (G.domina_stealth && dom->spin_t == 0 && actor_on_ground(dom, &G.lv)
                && (near_ally || !closed) && !wall) {
                G.domina_stealth = 0;
                if (!G.solo_mode) {
                    G.follow = 1;
                    group_gather();
                }
                say("Stealth OFF — group ON");
            }
        }
    }
    /* Pengi slide / спецрежим игрока = разрыв группы как Solo */
    {
        Actor *pl = player();
        if (pl && pl->hp > 0) {
            int stealth_like = 0;
            if (pl->kind == RIG_PENGI && pl->sliding) stealth_like = 1;
            if (pl->kind == RIG_MISTRESS && pl->spin_t > 0) stealth_like = 1;
            if (G.flip_on) stealth_like = 1;
            if (stealth_like) {
                G.follow = 0;
                for (int i = 0; i < NUM_HEROES; i++) {
                    if (i == G.cur) continue;
                    G.hero[i].sliding = 0;
                    G.hero[i].spin_t = 0;
                    if (G.hero[i].h < G.hero[i].full_h) G.hero[i].h = G.hero[i].full_h;
                }
            } else if (!G.solo_mode && !G.domina_stealth && !G.follow) {
                /* вышли из stealth-like рядом с союзниками → group ON */
                int near = 0;
                int px = FX_INT(pl->x), py = FX_INT(pl->y);
                for (int i = 0; i < NUM_HEROES; i++) {
                    if (i == G.cur || !G.hero[i].active || G.hero[i].hp <= 0) continue;
                    if (ABSI(FX_INT(G.hero[i].x) - px) < 90 && ABSI(FX_INT(G.hero[i].y) - py) < 40)
                        near = 1;
                }
                if (near && actor_on_ground(pl, &G.lv)) {
                    G.follow = 1;
                    say("Group ON: allies found you");
                }
            }
        }
    }
    /* Domina в stealth активировала лифт / любой триггер → телепорт к игроку (как для моста) */
    {
        static int prev_trig = -1;
        int trig = 0;
        for (int i = 0; i < G.lv.nobj; i++) { const Obj *o = &G.lv.obj[i]; if (o->on || (o->type == O_LIFT && o->have >= o->need)) trig++; }
        if (prev_trig >= 0 && trig > prev_trig && G.domina_stealth) {
            int di = -1; for (int i = 0; i < NUM_HEROES; i++) if (G.hero[i].kind == RIG_MISTRESS) di = i;
            Actor *pl2 = player();
            if (di >= 0 && di != G.cur && pl2 && G.hero[di].hp > 0) {
                Actor *d = &G.hero[di];
                int tx = FX_INT(pl2->x) - 26 * (d->facing >= 0 ? 1 : -1), ty = FX_INT(pl2->y);
                if (level_solid_rect(&G.lv, tx - 6, ty - 20, tx + 6, ty - 2)) tx = FX_INT(pl2->x);
                spark(FX_INT(d->x), FX_INT(d->y) - 14, 12, 0xFF80E0FFu, 3);
                actor_teleport(d, tx, ty); d->vx = d->vy = 0; d->spin_t = 0; d->fall_immune = 0;
                spark(tx, ty - 14, 12, 0xFF80E0FFu, 3);
                G.domina_stealth = 0;
                if (!G.solo_mode) G.follow = 1;
                say("Domina teleported back (trigger activated)");
            }
        }
        prev_trig = trig;
    }
    level_update(&G.lv);
    /* powered-лифт / активный мост открывает комнаты на своих этажах */
    for (int i = 0; i < G.lv.nobj; i++) {
        Obj *o = &G.lv.obj[i];
        if (o->type == O_LIFT && o->have >= o->need) {
            int cx = o->x + o->w / 2;
            int r0 = level_room(cx, o->y0), r1 = level_room(cx, o->y1);
            if (r0 >= 0 && r0 < 12) G.explored[r0] = 1;
            if (r1 >= 0 && r1 < 12) G.explored[r1] = 1;
        }
        if (o->type == O_BRIDGE && o->on && o->solid) {
            int r = level_room(o->x + o->w / 2, o->y);
            if (r >= 0 && r < 12) G.explored[r] = 1;
        }
    }
    /* лифты и платформы несут тех, кто на них стоит */
    for (int i = 0; i < NUM_HEROES; i++) { Actor *a = &G.hero[i]; if (a->active && a->stand_obj >= 0) { const Obj *o = &G.lv.obj[a->stand_obj]; if (o->dx || o->dy) actor_translate(a, &G.lv, o->dx, o->dy); } }
    for (int i = 0; i < EN_MAX; i++) { Actor *a = &G.en[i]; if (a->active && a->stand_obj >= 0) { const Obj *o = &G.lv.obj[a->stand_obj]; if (o->dx || o->dy) actor_translate(a, &G.lv, o->dx, o->dy); } }

    interact(G.cur, G.in.use, G.in.use && !G.prev.use);

    for (int i = 0; i < NUM_HEROES; i++) {
        Actor *h = &G.hero[i];
        if (!h->active) continue; /* скрытые на арене босса не симулируются */
        Intent it;
        if (i == G.cur) it = player_intent(); else { hero_ai(i, &it); events_hero_override(i, &it); }
        if (G.act_t[i] > 0) { h->activating = 1; G.act_t[i]--; }
        actor_update(h, &G.lv, &it);
        hero_attack_effects(i);
        if (h->stand_obj >= 0) G.lv.occ[h->stand_obj] = 1;
        /* ceiling: платформа на самом верху уровня (y≈0). Запрет выше неё.
           Только для союзников → старт уровня. Игрок НЕ телепортируется. */
        {
            int cy = FX_INT(h->y);
            int on_ceiling = (cy <= 12); /* ноги на/выше ceiling-платформы y=0 */
            if (h->hp > 0 && on_ceiling) {
                if (i != G.cur) {
                    int sx = G.lv.hero_x[i] > 0 ? G.lv.hero_x[i] : (G.start_x > 0 ? G.start_x : 40);
                    int sy = G.lv.hero_y[i] > 0 ? G.lv.hero_y[i] : FLOOR1_Y;
                    if (sy < FLOOR3_Y) sy = FLOOR1_Y;
                    actor_teleport(h, sx, sy); h->vx = h->vy = 0; h->invuln_t = 60;
                } else {
                    /* игрок: только выталкиваем вниз на верхний игровой этаж */
                    actor_teleport(h, FX_INT(h->x), FLOOR4_Y);
                    h->vx = 0; h->vy = 0; h->invuln_t = 30;
                }
            }
        }
        /* зал босса закрыт для союзников, пока бой не начат: любой, кто там оказался, возвращается к игроку */
        if (G.boss_state == BS_SLEEP && i != G.cur && h->hp > 0) {
            const Arena *ar = &G.lv.arena;
            int hx = FX_INT(h->x), hy = FX_INT(h->y), px_ = FX_INT(player()->x), py_ = FX_INT(player()->y);
            int in_h = hx >= ar->trig_x0 && hx <= ar->trig_x1 && hy >= ar->ceil && hy <= ar->floor + 10;
            int in_p = px_ >= ar->trig_x0 && px_ <= ar->trig_x1 && py_ >= ar->ceil && py_ <= ar->floor + 10;
            if (in_h && !in_p) emergency_to_player(h);
        }
        /* падение: респаун на краю платформы (safe), не в начало уровня */
        if (h->fell) {
            if (i == G.cur) {
                close_room_at(h->safe_x, h->safe_y);
                level_restore_platforms(&G.lv); /* исчезающие мини-платформы сразу снова */
            }
            int sx = h->safe_x, sy = h->safe_y;
            if (sx < 16 || sy < 16) {
                sx = G.hero_dead_x[i] ? G.hero_dead_x[i] : (G.lv.hero_x[i] > 0 ? G.lv.hero_x[i] : 40);
                sy = G.hero_dead_y[i] ? G.hero_dead_y[i] : (G.lv.hero_y[i] > 0 ? G.lv.hero_y[i] : FLOOR1_Y);
            }
            safe_inward(&sx, &sy);
            clamp_spawn_open(&sx, &sy); /* никогда в закрытый stage */
            G.hero_dead_x[i] = sx; G.hero_dead_y[i] = sy;
            if (i == G.cur) {
                if (!G.dying[i]) {
                    G.dying[i] = 1;
                    if (G.lives[i] > 0) G.lives[i]--; else G.out[i] = 1;
                }
                if (!G.out[i]) {
                    actor_init(h, h->kind, 0, sx, sy);
                    h->immortal = 0; h->post_x = sx; h->post_y = sy;
                    h->invuln_t = 90; h->hp = h->maxhp;
                    h->soft = 0; h->fall_immune = 0; h->spin_t = 0;
                    G.dying[i] = 0;
                    say("Respawn inward from edge");
                }
            } else {
                actor_init(h, h->kind, 0, sx, sy);
                h->immortal = 0; h->post_x = sx; h->post_y = sy;
                h->invuln_t = 60; h->hp = h->maxhp;
                h->soft = 0; h->fall_immune = 0;
                G.dying[i] = 0;
            }
            h->fell = 0; h->vx = h->vy = 0;
        }
        /* закрытая комната без допуска → смерть */
        closed_room_kill_check(i);
        /* AI в закрытой без допуска уже убит выше; emergency только если ещё жив и special */
        if (h->active && h->hp > 0 && i != G.cur) {
            int r = level_room(FX_INT(h->x), FX_INT(h->y));
            if (!room_explored(r) && !hero_special_access(h) && !room_lift_access(FX_INT(h->x), FX_INT(h->y)))
                ; /* already handled by closed_room_kill_check */
            else if (!room_explored(r) && hero_special_access(h))
                ; /* special mode OK */
        }
        if (h->hp <= 0) {
            if (!G.dying[i]) {
                G.dying[i] = 1;
                /* точка респауна = край платформы (safe), не дно пропасти */
                {
                    int dx, dy;
                    if (i != G.cur) {
                        /* погиб союзник-ИИ: респаун на последнем активном checkpoint, к игроку вернётся лифтом / с края экрана / вертолётом */
                        stage_checkpoint(FX_INT(h->x), &dx, &dy);
                        dx += (i - G.cur) * 10;
                    } else if (h->fell) {
                        /* падение с высоты → активный checkpoint (без циклов в пропасти) */
                        G.hero_fall_t[i] = G.frame;
                        stage_checkpoint(FX_INT(h->x), &dx, &dy);
                        safe_inward(&dx, &dy);
                    } else {
                        /* урон игрока: точка гибели */
                        dx = FX_INT(h->x); dy = FX_INT(h->y);
                    }
                    clamp_spawn_open(&dx, &dy);
                    G.hero_dead_x[i] = dx;
                    G.hero_dead_y[i] = dy;
                }
                G.hero_dead_t[i] = 240; /* 4 с до респауна AI на checkpoint */
                if (i == G.cur) {
                    if (G.lives[i] > 0) G.lives[i]--; else G.out[i] = 1;
                    level_restore_platforms(&G.lv);
                    if (G.out[i]) {
                        /* нет жизней после падения → checkpoint */
                        int cx, cy; stage_checkpoint(FX_INT(h->x), &cx, &cy);
                        G.hero_dead_x[i] = cx; G.hero_dead_y[i] = cy;
                        char b[64]; snprintf(b, sizeof b, "%s is out of lives", HERO_NAME[i]); say(b);
                    }
                }
            }
            int in_boss = (G.boss_state != BS_SLEEP && G.boss_state != BS_DEAD);
            if (in_boss) {
                /* босс: умерший герой остаётся мёртвым до победы; переключаемся на другого */
                if (i == G.cur && h->dead_t > 40) {
                    int cand[NUM_HEROES], n = 0;
                    for (int k = 0; k < NUM_HEROES; k++) {
                        if (k == G.cur || G.out[k]) continue;
                        if (G.hero[k].hp > 0 || G.boss_hide_allies) cand[n++] = k;
                    }
                    if (n) {
                        int t = cand[g_rand() % (uint32_t)n];
                        /* восстановить кандидата на арене */
                        G.hero[t].active = 1;
                        if (G.hero[t].hp <= 0) G.hero[t].hp = G.hero[t].maxhp;
                        {
                            const Arena *ar = &G.lv.arena;
                            actor_teleport(&G.hero[t], ar->x0 + 48, ar->floor);
                            G.hero[t].post_x = ar->x0 + 48; G.hero[t].post_y = ar->floor;
                            G.hero[t].vx = G.hero[t].vy = 0; G.hero[t].invuln_t = 90;
                            G.hero[t].immortal = 0; G.hero[t].attack_kind = AT_NONE;
                        }
                        switch_hero(t);
                        say("Hero down — switch fighter!");
                    }
                }
                /* не респауним до конца боя */
            } else if (i == G.cur) {
                if (!G.out[i] && h->dead_t > 600) { respawn_hero(i); G.dying[i] = 0; }
                /* сразу переключить на живого; остальных НЕ трогаем (остаются на местах) */
                else if (h->dead_t == 2) {
                    int cand[NUM_HEROES], n = 0;
                    for (int k = 0; k < NUM_HEROES; k++)
                        if (k != G.cur && G.hero[k].hp > 0 && G.hero[k].active && !G.out[k] && !G.dying[k])
                            cand[n++] = k;
                    if (n) {
                        int t = cand[g_rand() % (uint32_t)n];
                        /* только смена управления — без телепорта команды */
                        switch_hero(t);
                        say("Switched — allies stay put");
                    }
                }
            } else {
                /* AI: респаун через 4с на месте смерти, жизни не тратятся */
                if (G.hero_dead_t[i] > 0) G.hero_dead_t[i]--;
                if (G.hero_dead_t[i] == 0 && h->dead_t > 10) {
                    int sx = G.hero_dead_x[i], sy = G.hero_dead_y[i];
                    if (sx < 16 || sy < 16 || !room_explored(level_room(sx, sy))) {
                        stage_checkpoint(sx > 0 ? sx : 48, &sx, &sy);
                    }
                    clamp_spawn_open(&sx, &sy);
                    actor_init(h, HERO_KIND[i], 0, sx, sy);
                    h->active = 1; h->hp = h->maxhp; h->invuln_t = 90;
                    h->post_x = sx; h->post_y = sy; h->immortal = 0;
                    G.dying[i] = 0; h->dead_t = 0;
                    spark(sx, sy - 14, 10, 0xFF80E0FFu, 2);
                }
            }
        }
        /* напарники и герой вне боя медленно лечатся */
        if (h->hp > 0 && h->hp < h->maxhp) { if (h->hurt_t > 0 || h->invuln_t > 30) h->ai_aux = 0; else if (++h->ai_aux >= 600) { h->hp++; h->ai_aux = 0; } }
        if (h->immortal && h->hp < h->maxhp && h->hp > 0) { /* напарник не теряет здоровье, но показываем лечение */ }
    }
    update_cp_stage();
    enemy_phase_walls();
    boss_room_gate();
    stealth_timer();
    stealth_end_enemy_follow();
    group_auto_off();
    group_auto_on();
    sync_group_platforms();
    groups_update_lost();
    sprinter_catchup();
    sprinter_repair();
    
    /* союзники не должны быть на этаже без powered-лифта (кроме special) */
    for (int i = 0; i < NUM_HEROES; i++) {
        if (i == G.cur) continue;
        Actor *h = &G.hero[i];
        if (!h->active || h->hp <= 0) continue;
        if (hero_special_access(h)) continue;
        int hy = FX_INT(h->y);
        if (floor_open_for_team(hy)) continue;
        if (!actor_on_ground(h, &G.lv) && h->stand_obj < 0) continue;              /* в прыжке/падении этаж не определён: не выкидываем */
        if (G.heli.on && (G.heli.mask & (1 << i))) continue;                       /* в вертолёте */
        if (h->stand_obj >= 0 && h->stand_obj < G.lv.nobj && G.lv.obj[h->stand_obj].type == O_LIFT) continue;   /* едет на лифте */
        { Actor *pp = player();                                                   /* этаж игрока для команды всегда открыт (вертолёт/лифт/край экрана доставили сюда) */
          if (pp && pp->hp > 0 && !G.domina_stealth && ABSI(hy - FX_INT(pp->y)) <= 40 && G.follow && !G.solo_mode
              && room_explored(level_room(FX_INT(pp->x), FX_INT(pp->y)))) continue; }
        /* выкинуть на 1S */
        int sx = G.cp_n > 0 ? G.cp_x[0] : 48;
        int sy = G.cp_n > 0 ? G.cp_y[0] : FLOOR1_Y;
        actor_teleport(h, sx + i * 12, sy);
        h->vx = h->vy = 0; h->invuln_t = 45; h->sliding = 0; h->spin_t = 0;
    }
    
    /* Pengi slide / Domina spin: group OFF, союзников не тянем в stealth-зону */
    {
        Actor *pl = player();
        if (pl && pl->hp > 0) {
            int stealth = 0;
            if (pl->kind == RIG_PENGI && pl->sliding) stealth = 1;
            if (pl->kind == RIG_MISTRESS && (pl->spin_t > 0 || G.domina_stealth)) stealth = 1;
            if (pl->kind == RIG_REFREEZER && pl->misc > 0 && pl->flip) stealth = 1;
            if (stealth) {
                G.follow = 0;
                int px = FX_INT(pl->x), py = FX_INT(pl->y);
                int pr = level_room(px, py);
                for (int i = 0; i < NUM_HEROES; i++) {
                    if (i == G.cur || !G.hero[i].active || G.hero[i].hp <= 0) continue;
                    Actor *h = &G.hero[i];
                    h->sliding = 0; h->spin_t = 0;
                    int hx = FX_INT(h->x), hy = FX_INT(h->y);
                    int hr = level_room(hx, hy);
                    /* если союзник оказался рядом в закрытой/той же stealth-комнате — на активный CP */
                    if (ABSI(hx - px) < 120 && ABSI(hy - py) < 64) {
                        if ((pr >= 0 && !room_explored(pr)) || (hr >= 0 && !room_explored(hr)) || stealth) {
                            int sx, sy;
                            stage_checkpoint(hx, &sx, &sy);
                            safe_inward(&sx, &sy);
                            actor_teleport(h, sx + i * 10, sy);
                            h->vx = h->vy = 0; h->invuln_t = 30;
                        }
                    }
                }
            }
        }
    }

    closed_room_rescue();
    /* антигравитация: flip_on ТОЛЬКО пока у Refreezer есть активный таймер (misc>0 и flip) */
    {
        int was_flip = G.flip_on;
        int any_ag = 0;
        for (int i = 0; i < NUM_HEROES; i++) {
            Actor *h = &G.hero[i];
            if (h->kind != RIG_REFREEZER || !h->active) continue;
            if (h->flip && h->misc > 0) { any_ag = 1; continue; }
            if (h->flip && h->misc <= 0) {
                actor_set_flip(h, &G.lv, 0);
                h->fall_immune = 0;
                h->flip = 0;
                Actor *pl = player();
                if (i != G.cur) {
                    if (pl && pl->hp > 0) actor_teleport(h, FX_INT(pl->x) + (i - G.cur) * 14, FX_INT(pl->y));
                } else {
                    int best = -1, bd = 1 << 30;
                    for (int k = 0; k < NUM_HEROES; k++) {
                        if (k == i || !G.hero[k].active || G.hero[k].hp <= 0) continue;
                        int d = ABSI(FX_INT(G.hero[k].x) - FX_INT(h->x)) + ABSI(FX_INT(G.hero[k].y) - FX_INT(h->y));
                        if (d < bd) { bd = d; best = k; }
                    }
                    if (best >= 0) actor_teleport(h, FX_INT(G.hero[best].x) + 12, FX_INT(G.hero[best].y));
                }
                h->vx = h->vy = 0; h->post_x = FX_INT(h->x); h->post_y = FX_INT(h->y);
                h->soft = 90;
                say("Gravity restored");
            }
        }
        G.flip_on = any_ag;
        /* сброс «хаоса» врагов при выходе из антигравитации */
        if (was_flip && !G.flip_on) {
            for (int k = 0; k < EN_MAX; k++) {
                Actor *e = &G.en[k];
                if (!e->active || e->hp <= 0) continue;
                e->attack_kind = AT_NONE;
                e->charging = 0;
                e->ai_state = 0;
                e->ai_t = 0;
                e->stun_t = 0;
            }
        }
    }
    /* UOB больше не спускается сам — только по E, когда все герои на лифте (см. interact) */
    { int alive = 0, comeback = 0;
      for (int i = 0; i < NUM_HEROES; i++) { if (G.hero[i].hp > 0 && G.hero[i].active) alive++; else if (!G.out[i]) comeback++; }
      if (!alive && !comeback && G.boss_state == BS_SLEEP) { full_level_restart_or_gameover(); return; }
    }
    /* PgUp/PgDn: уровни 1..3 (пока конечный — 3) */
    if (in->pgup && !G.prev.pgup) {
        int n = G.level_no + 1; if (n > NUM_LEVELS) n = NUM_LEVELS;
        game_set_level_no(n);
        { char b[40]; snprintf(b, sizeof b, "Level %d", n); say(b); }
    }
    if (in->pgdn && !G.prev.pgdn) {
        int n = G.level_no - 1; if (n < 1) n = 1;
        game_set_level_no(n);
        { char b[40]; snprintf(b, sizeof b, "Level %d", n); say(b); }
    }
    /* B: принудительно в комнату босса */
    if (in->bosskey && !G.prev.bosskey && G.boss_state == BS_SLEEP) {
        const Arena *ar = &G.lv.arena;
        int bx = (ar->x0 + ar->x1) / 2, by = ar->floor;
        for (int i = 0; i < NUM_HEROES; i++) {
            if (!G.hero[i].active) continue;
            actor_teleport(&G.hero[i], bx + (i - 2) * 14, by);
            G.hero[i].post_x = bx; G.hero[i].post_y = by;
            spark(bx + (i - 2) * 14, by - 16, 10, 0xFF80E0FFu, 2);
        }
        say("Boss chamber");
    }
    enemies_update();
    boss_update();
    hazards_update();
    events_update();
    projectiles_update();
    pickups();
    sparks_update();
    cars_update();
    camera_update();
    sound_listener(G.cam_x + VIEW_W / 2, G.cam_y + VIEW_H / 2);
    if (G.msg_t > 0) G.msg_t--;
    for (int i = 0; i < 5; i++) if (G.logt[i] > 0) G.logt[i]--;
    if (G.shake > 0) G.shake--;
    if (g_trace && G.frame % g_trace == 0) {
        const Actor *p = player();
        fprintf(stderr, "f=%4d cur=%d x=%4d y=%4d vx=%5d vy=%5d st=%2d hp=%d | en=%d boss=%d/%d | key=%d art=%d | cam=%d,%d kills=%d\n", G.frame, G.cur, FX_INT(p->x), FX_INT(p->y),
                p->vx, p->vy, p->st, p->hp, enemies_alive(), G.boss_state, G.boss.hp, G.have_key, G.have_artifact, G.cam_x, G.cam_y, G.stat_kills);
        fprintf(stderr, "        [stealth=%d] heroes x=%d,%d,%d,%d y=%d,%d,%d,%d hp=%d,%d,%d,%d follow=%d\n", G.domina_stealth, FX_INT(G.hero[0].x), FX_INT(G.hero[1].x), FX_INT(G.hero[2].x), FX_INT(G.hero[3].x), FX_INT(G.hero[0].y), FX_INT(G.hero[1].y), FX_INT(G.hero[2].y), FX_INT(G.hero[3].y),
                G.hero[0].hp, G.hero[1].hp, G.hero[2].hp, G.hero[3].hp, G.follow);
        fprintf(stderr, "        heli on=%d x=%d y=%d mask=%d load=%d\n", G.heli.on, G.heli.x, G.heli.y, G.heli.mask, G.heli.load);
        fprintf(stderr, "        h4-6 x=%d,%d,%d y=%d,%d,%d hp=%d,%d,%d rope=%d,%d,%d\n", FX_INT(G.hero[4].x), FX_INT(G.hero[5].x), FX_INT(G.hero[6].x), FX_INT(G.hero[4].y), FX_INT(G.hero[5].y), FX_INT(G.hero[6].y), G.hero[4].hp, G.hero[5].hp, G.hero[6].hp, G.hero[4].on_rope, G.hero[5].on_rope, G.hero[6].on_rope);
        for (int k = 0; k < EN_MAX; k++) if (G.en[k].active && G.en[k].hp > 0) fprintf(stderr, "        en%d kind=%d x=%d y=%d\n", k, G.en[k].kind, FX_INT(G.en[k].x), FX_INT(G.en[k].y));
    }
}

/* ===================================================================== рисование */
void actor_render(Image *fb, const Actor *a, int cx, int cy, int flash) {
    if (a->kind == RIG_HERO7) {                                     /* Батискаф: две картинки — суша и вода */
        int wet = G.lv.water_y > 0 && FX_INT(a->x) >= G.lv.water_x0 && FX_INT(a->x) <= G.lv.water_x1 && FX_INT(a->y) - a->h / 2 > G.lv.water_y;
        const Image *im = wet && g_baty_water.px ? &g_baty_water : &g_baty_land;
        if (im->px) {
            int lunge = a->attack_kind == AT_MELEE ? a->facing * 3 : 0;
            int bx = FX_INT(a->x) - cx - im->w / 2 + lunge, by = FX_INT(a->y) - cy - im->h;
            if (wet) by = FX_INT(a->y) - cy - a->h / 2 - im->h / 2;
            if (a->hp <= 0) by += 4;
            if (flash && (G.frame / 2) & 1) { ui_rect(fb, bx + 2, by + 2, im->w - 4, im->h - 4, 0x60FFFFFFu); }
            ui_blit_keyed(fb, im, bx, by, a->facing > 0);
            return;
        }
    }
    if (!a->flip) { model_draw_anim(fb, rig_get(a->kind), &a->anim, FX_INT(a->x) - cx, FX_INT(a->y) - cy, a->facing, flash); return; }
    /* перевёрнутая гравитация: рисуем во временный кадр и отражаем по вертикали (ноги на потолке) */
    enum { TW = 200, TH = 150, AX = 100, AY = 130 };
    static Image tmp;
    if (!tmp.px) tmp = image_create(TW, TH);
    for (int i = 0; i < TW * TH; i++) tmp.px[i] = 0xFF010203u;
    model_draw_anim(&tmp, rig_get(a->kind), &a->anim, AX, AY, a->facing, flash);
    int sx = FX_INT(a->x) - cx - AX, top = FX_INT(a->y) - a->h - cy;
    for (int r = 0; r < TH; r++) {
        int y = top + (AY - r);
        if (y < 0 || y >= fb->h) continue;
        for (int x = 0; x < TW; x++) {
            uint32_t c = tmp.px[r * TW + x];
            int px = sx + x;
            if (c == 0xFF010203u || px < 0 || px >= fb->w) continue;
            fb->px[y * fb->w + px] = c | 0xFF000000u;
        }
    }
}


static void draw_proj(Image *fb, const Proj *p, int cx, int cy) {
    int x = FX_INT(p->x) - cx, y = FX_INT(p->y) - cy;
    switch (p->type) {
    case PJ_PLASMA: ui_disc(fb, x, y, 4, 0x6040E0FFu); ui_disc(fb, x, y, 2, 0xFF80F8FFu); ui_line(fb, x, y, x - p->dir * 8, y, 0x8040C0FFu); break;
    case PJ_BIG: ui_disc(fb, x, y, 10, 0x5040E0FFu); ui_disc(fb, x, y, 7, 0xFF60D8FFu); ui_disc(fb, x, y, 3, 0xFFFFFFFFu); break;
    case PJ_BOTTLE: ui_rect(fb, x - 2, y - 3, 4, 7, 0xFF50C070u); ui_rect(fb, x - 1, y - 6, 2, 3, 0xFF80E0A0u); break;
    case PJ_SNOW: ui_disc(fb, x, y, 3, 0xFFF4F8FFu); break;
    case PJ_ROCKET:
        ui_rect(fb, x - 5, y - 2, 10, 4, 0xFFC8C8D0u); ui_rect(fb, p->dir > 0 ? x + 4 : x - 6, y - 2, 2, 4, 0xFFE04030u);
        ui_disc(fb, x - p->dir * 7, y, 3 + (G.frame & 1), 0xE0FFA030u);
        break;
    case PJ_DUMBBELL: {
        int k = (G.frame / 3) & 1;
        ui_rect(fb, x - 5, y - 1 + k, 10, 2, 0xFF303038u);
        ui_disc(fb, x - 5, y + k, 3, 0xFF202028u); ui_disc(fb, x + 5, y + k, 3, 0xFF202028u);
        break; }
    case PJ_BOLT: ui_disc(fb, x, y, 5, 0x60FF40C0u); ui_disc(fb, x, y, 3, 0xFFFF80E0u); ui_disc(fb, x, y, 1, 0xFFFFFFFFu); break;
    case PJ_BOOK: { int f = (G.frame / 4) & 1; ui_rect(fb, x - 5, y - 4, 10, 8, 0xFF7A2820u); ui_rect(fb, x - 5, y - 4, 2, 8, 0xFF501810u); ui_rect(fb, x - 3 + f, y - 3, 7, 6, 0xFFF0E8D0u); ui_rect(fb, x - 2 + f, y - 1, 5, 1, 0xFF907050u); break; }
    case PJ_WHISKY: ui_rect(fb, x - 2, y - 4, 4, 8, 0xFFB06A20u); ui_rect(fb, x - 1, y - 8, 2, 4, 0xFFD09040u); ui_rect(fb, x - 1, y - 9, 2, 1, 0xFF603010u); ui_rect(fb, x - 1, y - 2, 2, 3, 0xFFF0E0A0u); break;
    case PJ_PIZZA: ui_disc(fb, x, y, 5, 0xFFE0A030u); ui_disc(fb, x, y, 3, 0xFFD04030u); ui_disc(fb, x - 1, y - 1, 1, 0xFFF0F0A0u); break;
    case PJ_GREEN: ui_disc(fb, x, y, 6, 0x6030E050u); ui_disc(fb, x, y, 4, 0xFF70FF90u); ui_disc(fb, x, y, 2, 0xFFE8FFE8u); break;
    case PJ_SNOWBIG: ui_disc(fb, x, y, 11, 0xE0E8F4FFu); ui_disc(fb, x - 3, y - 3, 5, 0xFFFFFFFFu); ui_disc(fb, x + 4, y + 3, 4, 0xFFC0D8F0u); break;
    case PJ_HOMING: ui_rect(fb, x - 5, y - 2, 10, 4, 0xFFD8D8E0u); ui_disc(fb, x, y, 3, 0xFFE04030u); ui_disc(fb, x - p->dir * 7, y, 3 + (G.frame & 1), 0xE0FFA030u); break;
    case PJ_PELLET: ui_disc(fb, x, y, 2, 0xFFFFE890u); break;
    case PJ_FIRE: ui_disc(fb, x, y, 6, 0x80E05010u); ui_disc(fb, x, y, 4, 0xFFFF9020u); ui_disc(fb, x - p->dir * 2, y, 2, 0xFFFFE070u); if (G.frame & 1) ui_disc(fb, x - p->dir * 6, y - 2, 2, 0xC0FF6010u); break;
    case PJ_FIREBOT: ui_rect(fb, x - 2, y - 3, 4, 7, 0xFF181820u); ui_rect(fb, x - 1, y - 6, 2, 3, 0xFF303040u); ui_disc(fb, x, y - 8 - (G.frame & 1), 2, 0xFFFF8020u); break;
    case PJ_COMPUTER: if (g_computer_img.px) ui_blit_keyed(fb, &g_computer_img, x - g_computer_img.w / 2, y - g_computer_img.h / 2, 0); else ui_rect(fb, x - 8, y - 8, 16, 16, 0xFFC0B090u); break;
    case PJ_PIANO: if (g_piano_img.px) ui_blit_keyed(fb, &g_piano_img, x - g_piano_img.w / 2, y - g_piano_img.h / 2, 0); else ui_rect(fb, x - 12, y - 10, 24, 20, 0xFF804020u); break;
    case PJ_BOULDER: { int a = (int)(FX_INT(p->x) / 3) % 4; ui_disc(fb, x, y, 11, 0xFFE0E8F4u); ui_disc(fb, x - 3, y - 3, 5, 0xFFFFFFFFu); ui_disc(fb, x + (a - 2) * 3, y + (a % 2) * 4 - 2, 3, 0xFFB8D0E8u); break; }
    case PJ_ROCKETB: ui_rect(fb, x - 5, y - 2, 10, 4, 0xFF303038u); ui_rect(fb, p->dir > 0 ? x + 4 : x - 6, y - 2, 2, 4, 0xFFE04030u); ui_disc(fb, x - p->dir * 7, y, 3 + (G.frame & 1), 0xE0FFA030u); break;
    case PJ_LASER: ui_rect(fb, x - 6, y - 1, 12, 3, 0xFF60C0FFu); break;
    case PJ_GAS: ui_disc(fb, x, y, 6, 0x60A0B080u); ui_disc(fb, x + 2, y - 2, 4, 0x70C0D0A0u); break;
    case PJ_MISSILE: ui_rect(fb, x - 4, y - 1, 8, 3, 0xFFB0B8C8u); ui_disc(fb, x, y, 2, 0xFFE04030u); ui_disc(fb, x - p->dir * 6, y, 2 + (G.frame & 1), 0xE0FFB040u); break;
    default: break;
    }
}

static void draw_hp_hearts(Image *fb, int x, int y, int hp, int max, uint32_t col) {
    for (int i = 0; i < max; i++) ui_rect(fb, x + i * 7, y, 6, 5, i < hp ? col : 0xFF301820u);
}


static void draw_stage_checkpoints(Image *fb) {
    for (int i = 0; i < G.cp_n; i++) {
        int x = G.cp_x[i], y = G.cp_y[i] - 40;
        int sx = x - G.cam_x, sy = y - G.cam_y;
        if (sx < -24 || sx > VIEW_W + 24 || sy < -40 || sy > VIEW_H + 10) continue;
        int unlocked = (i <= G.cp_stage);
        uint32_t cross = unlocked ? 0xFF28A048u : 0xFF808880u;
        ui_rect(fb, sx, sy, 16, 16, 0xFF808890u);
        ui_rect(fb, sx + 1, sy + 1, 14, 14, 0xFFFFFFFFu);
        ui_rect(fb, sx + 7, sy + 3, 2, 10, cross);
        ui_rect(fb, sx + 3, sy + 7, 10, 2, cross);
        ui_rect(fb, sx + 6, sy + 16, 4, 24, 0xFF606870u);
    }
}

static void draw_quest_debug(Image *fb) {
    if (!G.debug_quest) return;
    const Arena *ar = &G.lv.arena;
    int cx = G.cam_x, cy = G.cam_y;
    for (int i = 0; i < G.lv.nobj; i++) {
        const Obj *o = &G.lv.obj[i];
        int q = quest_obj_done(o);
        if (q < 0) continue;
        if (o->x + o->w >= ar->trig_x0 - 8 && o->x <= ar->trig_x1 + 8
            && o->y >= ar->ceil - 20 && o->y <= ar->floor + 12) continue;
        if (o->type == O_BUTTON && !o->visible) continue;
        if (q) continue; /* только невыполненные */
        int x0 = o->x - cx - 2, y0 = o->y - cy - 2;
        int w = o->w + 4, h = o->h + 4;
        uint32_t col = 0xFFFFE020u;
        ui_rect(fb, x0, y0, w, 2, col);
        ui_rect(fb, x0, y0 + h - 2, w, 2, col);
        ui_rect(fb, x0, y0, 2, h, col);
        ui_rect(fb, x0 + w - 2, y0, 2, h, col);
    }
}
static void draw_hud(Image *fb) {
    for (int i = 0; i < NUM_HEROES; i++) {
        int x = 4 + i * 43, y = 4;
        const Actor *h = &G.hero[i];
        /* окантовка: активный — жёлтая; помощь — мягкое красное моргание;
           дистанция — голубая → серая (ближе = серее, дальше = голубее) */
        uint32_t fcol = 0xFF404858u;
        if (i == G.cur) {
            fcol = 0xFFFFE060u;
        } else if (h->active && h->hp > 0) {
            if (G.help_flag[i]) {
                /* мягкое периодическое моргание красным */
                int ph = (G.frame % 40);
                int a = ph < 20 ? ph * 12 : (40 - ph) * 12; /* 0..240..0 */
                if (a < 40) a = 40;
                fcol = 0xFF000000u | ((uint32_t)a << 16) | ((uint32_t)(a / 4) << 8) | (uint32_t)(a / 4);
            } else {
                Actor *pl = player();
                int dist = 0;
                if (pl) {
                    int dx = ABSI(FX_INT(h->x) - FX_INT(pl->x));
                    int dy = ABSI(FX_INT(h->y) - FX_INT(pl->y));
                    dist = dx > dy ? dx : dy;
                }
                /* 0..220 → серый (близко) .. голубой (далеко) */
                if (dist > 220) dist = 220;
                int t = dist; /* 0 near gray, 220 far blue */
                int r = 64 + (220 - t) * 40 / 220;
                int g = 72 + (220 - t) * 30 / 220;
                int b = 88 + t * 100 / 220;
                fcol = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            }
        }
        ui_rect(fb, x, y, 38, 38, 0xC0101820u);
        if (G.portrait[i].px) ui_blit_keyed(fb, &G.portrait[i], x + 3, y + 3, 0);
        /* в вертолёте — ч/б (неактивен) */
        if (G.heli.on && (G.heli.mask & (1 << i))) ui_rect(fb, x + 3, y + 3, 32, 32, 0xB0101018u);
        if (h->hp <= 0) ui_rect(fb, x + 3, y + 3, 32, 32, 0xA0200000u);
        ui_frame(fb, x, y, x + 37, y + 37, fcol);
        if (i == G.cur) ui_frame(fb, x - 1, y - 1, x + 38, y + 38, fcol);
        char n[4]; snprintf(n, sizeof n, "%d", i + 1);
        ui_text(fb, x + 4, y + 25, n, i == G.cur ? 0xFFFFE060u : 0xFFB0B8C8u);
        { char lb[8]; snprintf(lb, sizeof lb, "x%d", G.lives[i]); ui_text(fb, x + 36 - ui_text_width(lb) - 1, y + 28, lb, G.out[i] ? 0xFFE05050u : 0xFFFFE8A0u); }
        draw_hp_hearts(fb, x + 1, y + 41, h->hp, h->maxhp, i == G.cur ? 0xFFE04050u : 0xFF907080u);
        if (h->hp > 0) {                                                   /* синий мини-бар супера */
            int sset = h->cd_super_set > 0 ? h->cd_super_set : 1;
            int w = h->cd_super_set > 0 ? 36 * (sset - h->cd_super) / sset : 0;
            if (w < 0) w = 0;
            if (w > 36) w = 36;
            ui_rect(fb, x + 1, y + 49, 36, 2, 0xFF101830u);
            ui_rect(fb, x + 1, y + 49, w, 2, w >= 36 ? 0xFF60C0FFu : 0xFF3070D0u);
        } else if (!G.out[i] && G.dying[i]) {                              /* жёлтая шкала воскрешения 10 с */
            int max_t = 600;
            int left = G.hero_dead_t[i] > 0 ? G.hero_dead_t[i] : (max_t - h->dead_t);
            if (left < 0) left = 0;
            if (left > max_t) left = max_t;
            int w = 36 * (max_t - left) / max_t;
            if (w < 0) w = 0;
            if (w > 36) w = 36;
            ui_rect(fb, x + 1, y + 49, 36, 2, 0xFF302010u);
            ui_rect(fb, x + 1, y + 49, w, 2, 0xFFFFD040u);
        }
    }
    /* иконка цепи под аватаром Domina (слот 3) */
    {
        int cx = 4 + 3 * 43 + 12, cy = 56;
        int linked = G.follow && !G.solo_mode;
        uint32_t col = linked ? 0xFFE0E8F0u : 0xFF606870u;
        if (linked) {
            /* сомкнутая цепь: два звена */
            ui_rect(fb, cx, cy + 4, 10, 3, col);
            ui_rect(fb, cx + 8, cy + 4, 10, 3, col);
            ui_rect(fb, cx + 2, cy + 1, 3, 9, col);
            ui_rect(fb, cx + 13, cy + 1, 3, 9, col);
        } else {
            /* разомкнутая: звено + разрыв */
            ui_rect(fb, cx, cy + 4, 8, 3, col);
            ui_rect(fb, cx + 12, cy + 4, 8, 3, col);
            ui_rect(fb, cx + 1, cy + 1, 3, 9, col);
            ui_rect(fb, cx + 14, cy + 1, 3, 9, col);
        }
        /* мини-список групп справа от иконки */
        int gy = cy;
        for (int g = 0; g < 7 && gy < 90; g++) {
            if (!G.g_active[g]) continue;
            int n = 0;
            for (int i = 0; i < NUM_HEROES; i++) if (G.hero_gid[i] == g && G.hero[i].hp > 0) n++;
            if (!n) continue;
            char gb[16];
            if (g == 0) snprintf(gb, sizeof gb, "R%d", n);
            else snprintf(gb, sizeof gb, "g%d:%d", g, n);
            ui_text(fb, cx + 22, gy, gb, g == G.hero_gid[G.cur] ? 0xFFFFE060u : 0xFFA0A8B0u);
            gy += 9;
        }
    }
    char b[64];
    snprintf(b, sizeof b, "Stage %d/4", level_stage(FX_INT(player()->x)) + 1);
    ui_text(fb, VIEW_W - ui_text_width(b) - 4, 76, b, 0xFFE8F0FFu);
    {
        int qd = 0, qn = 0; quest_count(&qd, &qn);
        snprintf(b, sizeof b, "Quest %d/%d", qd, qn);
        uint32_t qc = (qn > 0 && qd >= qn) ? 0xFF60FF80u : 0xFFFFE060u;
        ui_text(fb, VIEW_W - ui_text_width(b) - 4, 88, b, qc);
        snprintf(b, sizeof b, "CP %d/%d", G.cp_stage + 1, G.cp_n > 0 ? G.cp_n : 1);
        ui_text(fb, VIEW_W - ui_text_width(b) - 4, 100, b, 0xFFA0E0FFu);
    }
    snprintf(b, sizeof b, "LEVEL %d  boss %s", G.level_no, BOSS_NAME[G.level_no - 1]);
    ui_text(fb, VIEW_W - ui_text_width(b) - 4, 56, b, 0xFFFFD890u);
    for (int i = 0; i < 5; i++) if (G.logt[i] > 0) ui_text(fb, 4, 76 + i * 10, G.logl[i], G.logt[i] > 60 ? 0xFFB0F0B0u : 0x80B0F0B0u);
    { char tb[32]; snprintf(tb, sizeof tb, "R: teleport x%d", G.tele); ui_text(fb, VIEW_W - ui_text_width(tb) - 4, 66, tb, G.tele ? 0xFF80E0FFu : 0xFF708090u); }
    if (G.have_key & 1) ui_text(fb, VIEW_W - 72, 88, "K1", 0xFFE8C030u);
    if (G.have_key & 2) ui_text(fb, VIEW_W - 48, 88, "K2", 0xFFE8C030u);
    if (G.have_artifact) ui_text(fb, VIEW_W - 60, 98, "Artifact", 0xFF60C0FFu);
    {   /* индикатор группы / solo / stealth — всегда виден справа, поверх всего HUD */
        const Actor *pp_ = player(); int st = G.domina_stealth || (pp_ && ((pp_->kind == RIG_MISTRESS && pp_->spin_t > 0) || (pp_->kind == RIG_PENGI && pp_->sliding) || (pp_->kind == RIG_REFREEZER && pp_->misc > 0 && pp_->flip)));
        char lab_b[24];
        if (st && !G.solo_mode) snprintf(lab_b, sizeof lab_b, "STEALTH %d", (STEALTH_LIMIT - G.stealth_t + 59) / 60);
        const char *lab = G.solo_mode ? "SOLO" : st ? lab_b : G.follow ? "GROUP" : "GROUP OFF";
        uint32_t lc = G.solo_mode ? 0xFFFFA040u : st ? 0xFF80E0FFu : G.follow ? 0xFF60FF80u : 0xFF909AA8u;
        int tw = ui_text_width(lab), x0 = VIEW_W - tw - 28, y0 = 110;
        ui_rect(fb, x0 - 3, y0 - 2, tw + 30, 13, 0xC0101820u);
        ui_rect(fb, x0, y0 + 3, 7, 2, lc); ui_rect(fb, x0 + 5, y0 + 3, 7, 2, lc);          /* звенья цепи */
        ui_rect(fb, x0 + 1, y0 + 1, 2, 6, lc); ui_rect(fb, x0 + 10, y0 + 1, 2, 6, lc);
        if (G.solo_mode || !G.follow) ui_rect(fb, x0 + 5, y0, 2, 8, 0xFF101820u);          /* разрыв звена */
        ui_text(fb, x0 + 17, y0, lab, lc);
    }
    if (G.boss_state != BS_SLEEP && G.boss.active && G.boss_state != BS_DEAD) {
        int w = 126, x = 4, y = 58; /* ниже аватаров и сердец (y≈45) */
        ui_rect(fb, x - 1, y - 1, w + 2, 8, 0xFF101018u);
        ui_rect(fb, x, y, w * G.boss.hp / (G.boss.maxhp ? G.boss.maxhp : BOSS_HP), 6, 0xFFD03048u);
        ui_text(fb, x + w + 6, y - 1, BOSS_NAME[G.level_no - 1], 0xFFFFE0E0u);
        if (!G.boss_vuln) ui_text(fb, x + w + 6 + ui_text_width(BOSS_NAME[G.level_no - 1]) + 6, y - 1, "INVULNERABLE", 0xFF8090B0u);
    }
    if (G.msg_t > 0) {
        int a = G.msg_t > 30 ? 255 : G.msg_t * 255 / 30;
        int w = ui_text_width(G.msg) + 12, x = VIEW_W / 2 - w / 2, y = VIEW_H - 38;
        ui_rect(fb, x, y - 2, w, 15, (uint32_t)(a * 170 / 255) << 24 | 0x101018u);
        ui_text_center(fb, VIEW_W / 2, y, G.msg, (uint32_t)a << 24 | 0xF0F4FFu);
    }
    if (g_hint[0]) ui_text_center(fb, VIEW_W / 2, VIEW_H - 18, g_hint, 0xFFFFE890u);
}

static void draw_debug(Image *fb, int cx, int cy) {
    for (int i = 0; i < NUM_HEROES; i++) { int x0, y0, x1, y1; actor_box(&G.hero[i], &x0, &y0, &x1, &y1); ui_frame(fb, x0 - cx, y0 - cy, x1 - cx, y1 - cy, i == G.cur ? 0xFF40FF40u : 0xFF40A0FFu); }
    for (int i = 0; i < EN_MAX; i++) if (G.en[i].active) { int x0, y0, x1, y1; actor_box(&G.en[i], &x0, &y0, &x1, &y1); ui_frame(fb, x0 - cx, y0 - cy, x1 - cx, y1 - cy, 0xFFFF4040u); }
    if (G.boss.active) { int x0, y0, x1, y1; actor_box(&G.boss, &x0, &y0, &x1, &y1); ui_frame(fb, x0 - cx, y0 - cy, x1 - cx, y1 - cy, 0xFFFF40FFu); }
    for (int i = 0; i < G.lv.nobj; i++) { const Obj *o = &G.lv.obj[i]; ui_frame(fb, o->x - cx, o->y - cy, o->x + o->w - 1 - cx, o->y + o->h - 1 - cy, o->solid ? 0xFFFFFF40u : 0xFF808080u); }
    char b[96];
    const Actor *p = player();
    snprintf(b, sizeof b, "x=%d y=%d vx=%d vy=%d st=%d en=%d", FX_INT(p->x), FX_INT(p->y), p->vx, p->vy, p->st, enemies_alive());
    ui_text(fb, 4, 54, b, 0xFFFFFFFFu);
}

void game_render(void) {
    Image *fb = &G.fb;
    if (G.prolog_t > 0 && g_prolog_img.px) {
        for (int y = 0; y < VIEW_H && y < g_prolog_img.h; y++)
            for (int x = 0; x < VIEW_W && x < g_prolog_img.w; x++) fb->px[y * VIEW_W + x] = g_prolog_img.px[y * g_prolog_img.w + x];
        if ((G.prolog_t / 30) & 1) ui_text_center(fb, VIEW_W / 2, 238, "PRESS ANY KEY", 0xFFFFE060u);
        return;
    }
    if (G.mode == MODE_END) {
        for (int i = 0; i < VIEW_W * VIEW_H; i++) fb->px[i] = 0xFF000000u;
        if (G.end_t > 30 && g_epi_img.px) {
            int a = G.end_t < 120 ? (G.end_t - 30) * 255 / 90 : 255;                 /* появление из темноты */
            for (int y = 0; y < VIEW_H && y < g_epi_img.h; y++)
                for (int x = 0; x < VIEW_W && x < g_epi_img.w; x++) {
                    uint32_t c = g_epi_img.px[y * g_epi_img.w + x];
                    uint32_t r = ((c >> 16) & 255) * (uint32_t)a / 255, gg = ((c >> 8) & 255) * (uint32_t)a / 255, b = (c & 255) * (uint32_t)a / 255;
                    fb->px[y * VIEW_W + x] = 0xFF000000u | (r << 16) | (gg << 8) | b;
                }
        }
        if (G.end_t > 60) ui_text_center(fb, VIEW_W / 2, 24, "The Bad End", 0xFFE02828u);
        if (G.end_t > 300 && ((G.end_t / 30) & 1)) ui_text_center(fb, VIEW_W / 2, 242, "PRESS ANY KEY", 0xFFFFE060u);
        return;
    }
    if (G.card_t > 0) {                                           /* заставка уровня на чёрном экране */
        for (int i = 0; i < VIEW_W * VIEW_H; i++) fb->px[i] = 0xFF000000u;
        char qb[64]; int q = G.level_no >= 1 && G.level_no <= 9 ? G.level_no : 1;
        snprintf(qb, sizeof qb, "Quest %d: %s", q, QUEST_NAME[q - 1]);
        ui_text_center(fb, VIEW_W / 2, 118, qb, 0xFFE8F0FFu);
        return;
    }
    int shx = 0, shy = 0;
    if (G.shake > 0) { shx = (int)(g_rand() % 3u) - 1; shy = (int)(g_rand() % 3u) - 1; }
    int cx = G.cam_x + shx, cy = G.cam_y + shy;
    if (cx < 0) cx = 0;
    if (cy < 0) cy = 0;
    /* статичный задний фон + машины внизу */
    for (int y = 0; y < VIEW_H; y++)
        for (int x = 0; x < VIEW_W; x++) {
            uint32_t c;
            if (x < G.lv.bg.w && y < G.lv.bg.h) c = G.lv.bg.px[y * G.lv.bg.w + x];
            else if (x < G.lv.bg.w && G.lv.bg.h > 0) {            /* фон ниже картинки: последняя строка, потемнее (земля) */
                uint32_t q = G.lv.bg.px[(G.lv.bg.h - 1) * G.lv.bg.w + x];
                int k = y - G.lv.bg.h; int m = 70 - k / 3; if (m < 35) m = 35;
                c = ((((q >> 16) & 255) * m / 100) << 16) | ((((q >> 8) & 255) * m / 100) << 8) | ((q & 255) * m / 100);
            } else c = 0xFF203050u;
            fb->px[y * VIEW_W + x] = c | 0xFF000000u;
        }
    if (G.map_no < 2) {   /* машины только на уличных уровнях */
        int ord[NCARS];
        for (int i = 0; i < G.car_n; i++) ord[i] = i;
        for (int i = 1; i < G.car_n; i++) {
            int v = ord[i], kv = G.car_y[v] + G.car[v].h, j = i - 1;
            while (j >= 0 && G.car_y[ord[j]] + G.car[ord[j]].h > kv) { ord[j + 1] = ord[j]; j--; }
            ord[j + 1] = v;
        }
        for (int k = 0; k < G.car_n; k++) { int i = ord[k]; ui_blit_keyed(fb, &G.car[i], G.car_x[i] / 100, G.car_y[i], 0); }
    }
    level_render(&G.lv, fb, cx, cy);
    draw_stage_checkpoints(fb);
    draw_quest_debug(fb);

    for (int i = 0; i < EN_MAX; i++) {
        const Actor *e = &G.en[i];
        if (!e->active) continue;
        if (e->hp <= 0 && e->dead_t > 30 && (G.frame / 3) % 2) continue;
        actor_render(fb, e, cx, cy, e->hurt_t > 0 ? 150 : 0);
        if (e->hp > 0 && e->maxhp > 0 && e->maxhp <= 5) {                 /* HP врага: 5 квадратов над головой */
            int bx = FX_INT(e->x) - cx - 17, by = FX_INT(e->y) - cy - e->full_h - 8;
            ui_rect(fb, bx - 1, by - 1, 35, 6, 0xA0000000u);
            for (int k = 0; k < 5; k++) ui_rect(fb, bx + k * 7, by, 6, 4, k < e->hp ? 0xFFE04040u : 0xFF381418u);
        }
    }
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < NUM_HEROES; i++) {
            if ((i == G.cur) != (pass == 1)) continue;
            const Actor *h = &G.hero[i];
            if (!h->active) continue;
            /* no blink on appear/teleport invuln; flash only on hurt */
            int fl = (h->hurt_t > 0) ? 150 : 0;
            /* после анимации смерти — скрыть на время воскрешения */
            if (h->hp <= 0 && h->dead_t > 40) continue;
            actor_render(fb, h, cx, cy, fl);
            /* цепочка над головой союзников в режиме группы (эскиз: два звена) */
            if (G.follow && i != G.cur && h->hp > 0) {
                int sx = FX_INT(h->x) - cx, sy = FX_INT(h->y) - cy - h->full_h - 6;
                /* left link */
                ui_rect(fb, sx - 6, sy, 5, 3, 0xFF202028u);
                ui_rect(fb, sx - 5, sy + 1, 3, 1, 0xFFD0D8E0u);
                /* right link overlapping */
                ui_rect(fb, sx - 1, sy, 5, 3, 0xFF202028u);
                ui_rect(fb, sx, sy + 1, 3, 1, 0xFFD0D8E0u);
            }
            if (h->spin_t > 0) {                                           /* вихрь волчка */
                int sx = FX_INT(h->x) - cx, sy = FX_INT(h->y) - cy - 18;
                for (int k = 0; k < 6; k++) {
                    float a = (float)(G.frame * 0.4f + k * 1.047f);
                    ui_disc(fb, sx + (int)(cosf(a) * 15.f), sy + (int)(sinf(a) * 5.f) + (k % 3) * 6 - 6, 1, 0xC0F0F8FFu);
                }
            }
        }
    if (G.boss.active) actor_render(fb, &G.boss, cx, cy, G.boss.hurt_t > 0 ? 160 : 0);
    enemy_draw_extra(fb, cx, cy);
    events_draw(fb, cx, cy);
    events_draw_world(fb, cx, cy);
    if (G.portal.on) {                                                      /* портал: вращающиеся кольца */
        int x = G.portal.x - cx, y = G.portal.y - cy - 20, t = G.portal.t;
        int open = t < 20 ? t * 12 / 20 + 2 : (t > 40 + 16 * G.portal.n + 10 ? 2 : 14);
        for (int k = 0; k < 3; k++) for (int a = 0; a < 20; a++) {
            float ang = (float)a * 0.3142f + (float)(G.frame * (k + 1)) * 0.07f;
            ui_rect(fb, x + (int)(cosf(ang) * (float)(open - k * 3) * 0.55f), y + (int)(sinf(ang) * (float)(open * 2 - k * 4)), 2, 2, k == 0 ? 0xE0A0F0FFu : k == 1 ? 0xC080C0FFu : 0xA0FFFFFFu);
        }
        ui_disc(fb, x, y, open / 3, 0x80FFFFFFu);
    }
    if (G.heli.on) {                                                        /* вертолёт-эвакуатор */
        int x = G.heli.x - cx, y = G.heli.y - cy;
        static int hx_prev = 0; static int hflip = 0;
        if (G.heli.x > hx_prev + 0) hflip = 1; else if (G.heli.x < hx_prev) hflip = 0;
        hx_prev = G.heli.x;
        if (g_heli_img.px) {
            for (int i = 0; i < NUM_HEROES; i++) {
                if (!(G.heli.load & (1 << i))) continue;
                int rx = G.heli.x + (i - G.cur) * 5 - cx;
                for (int ry = y + 6; ry < y + 26 - 10; ry++) ui_rect(fb, rx, ry, 1, 1, 0xFF303830u);
            }
            ui_blit_keyed(fb, &g_heli_img, x - g_heli_img.w / 2, y - 14, hflip);
        }
    }
    if (G.whip_t > 0 && G.whip_hi >= 0 && G.whip_hi < NUM_HEROES) {         /* кнут Domina */
        const Actor *w = &G.hero[G.whip_hi];
        int dir = w->facing >= 0 ? 1 : -1;
        int len = G.whip_t > 7 ? (14 - G.whip_t) * 24 : G.whip_t * 24;           /* вылет и возврат */
        if (len > 160) len = 160;
        int x0 = FX_INT(w->x) + dir * 10 - cx, y0 = FX_INT(w->y) - 20 - cy, px_ = x0, py_ = y0;
        for (int d = 4; d <= len; d += 4) {
            int yy = y0 + (int)(sinf((float)d * 0.22f + (float)G.frame * 0.5f) * 3.f * (float)d / 160.f);
            int xx = x0 + dir * d;
            ui_line(fb, px_, py_, xx, yy, 0xFF7A4A20u); ui_line(fb, px_, py_ + 1, xx, yy + 1, 0xFFB07838u);
            px_ = xx; py_ = yy;
        }
        if (len > 8) { ui_disc(fb, px_, py_, 2, 0xFFFFE080u); ui_rect(fb, px_ - 1, py_ - 1, 2, 2, 0xFFFFFFFFu); }
    }
    boss_draw_world(fb, cx, cy);
    for (int i = 0; i < PJ_MAX; i++) if (G.pj[i].active) draw_proj(fb, &G.pj[i], cx, cy);
    for (int i = 0; i < FX_MAX; i++) {
        const Spark *s = &G.sp[i];
        if (!s->active) continue;
        uint32_t a = (uint32_t)(s->life * 255 / s->max);
        ui_rect(fb, FX_INT(s->x) - cx, FX_INT(s->y) - cy, s->size, s->size, (s->col & 0x00FFFFFFu) | (a << 24));
    }
    /* маркер активного героя */
    {
        const Actor *p = player();
        int mx = FX_INT(p->x) - cx, my = FX_INT(p->y) - cy - p->full_h * (p->kind == RIG_HEAVY ? 3 : 2) / 2 - 8 + (int)(sinf((float)G.frame * 0.15f) * 2.f);
        if (p->hp > 0) for (int k = 0; k < 4; k++) ui_rect(fb, mx - 3 + k, my - 4 + k, 7 - 2 * k, 1, HERO_COL[G.cur]);
    }
    boss_draw_overlay(fb, cx, cy);
    if (G.debug) draw_debug(fb, cx, cy);
    draw_hud(fb);
    events_draw_hud(fb);
    if (G.mode == MODE_WIN) {
        /* плавное затемнение до чёрного */
        int a = G.win_t < 60 ? (G.win_t * 255 / 60) : 255;
        ui_rect(fb, 0, 0, VIEW_W, VIEW_H, (uint32_t)a << 24);
        if (G.win_t > 20 && G.win_t < 80) {
            ui_text_center(fb, VIEW_W / 2, 90, "LEVEL COMPLETE!", 0xFFFFE060u);
            ui_text_center(fb, VIEW_W / 2, 112, "Next level...", 0xFFE8F0FFu);
            char b[64]; snprintf(b, sizeof b, "Enemies defeated: %d", G.stat_kills);
            ui_text_center(fb, VIEW_W / 2, 128, b, 0xFFE8F0FFu);
        }
    }
    /* fade-in из чёрного на старте уровня */
    if (G.mode == MODE_PLAY && G.fade_t > 0) {
        int a = G.fade_t * 255 / 45;
        if (a > 255) a = 255;
        ui_rect(fb, 0, 0, VIEW_W, VIEW_H, (uint32_t)a << 24);
    }
    if (G.gameover_t > 0) {
        ui_rect(fb, 0, 0, VIEW_W, VIEW_H, 0xC0080810u);
        ui_text_center(fb, VIEW_W / 2, 100, "GAME OVER", 0xFFFF4040u);
        ui_text_center(fb, VIEW_W / 2, 124, "Restarting level...", 0xFFC0C8D0u);
    }
    if (G.paused) {
        ui_rect(fb, 0, 0, VIEW_W, VIEW_H, 0x90081018u);
        ui_text_center(fb, VIEW_W / 2, 110, "PAUSED", 0xFFFFE060u);
        ui_text_center(fb, VIEW_W / 2, 130, "Press P to resume", 0xFFC0C8D0u);
    }
}
