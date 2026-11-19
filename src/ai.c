#include <stdlib.h>
/* Враги, ИИ героев-напарников, босс, снаряды, попадания. */
#include "game_priv.h"
#include "events.h"
#include "sound.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int sgn(int v) { return v > 0 ? 1 : v < 0 ? -1 : 0; }
static const Intent ZERO_IN = {0};

static void spawn_tick(void);

void ai_init(void) {
    memset(G.en, 0, sizeof G.en);
    memset(G.pj, 0, sizeof G.pj);
    memset(&G.boss, 0, sizeof G.boss);
    G.boss_state = BS_SLEEP;
    G.spawn_t = 150;
    G.spawn_ground_budget = -1;
    G.spawn_ground_pack = 0;
    G.spawn_fly_t = 0;
    G.spawn_water_t = 0;
    G.spawn_water_budget = -1;
    G.spawn_room_last = -1;
}

int enemies_alive(void) {
    int n = 0;
    for (int i = 0; i < EN_MAX; i++) if (G.en[i].active && G.en[i].hp > 0) n++;
    return n;
}

/* ===================================================================== попадания */

/* урон врагов по типу (ячейки HP) */
static int enemy_hit_dmg(int kind) {
    if (kind == RIG_ROCKETPUNK || kind == RIG_SCOUT || kind == RIG_DUMBPUNK || kind == RIG_BODY) return 1;
    if (kind == RIG_RAMROD || kind == RIG_HEAVY) return 2;
    if (kind == RIG_DELIVER) return 1;
    if (kind == RIG_FIREWALKER) return 3;
    if (kind == RIG_MONOMAN) return 2;
    if (kind == RIG_IRONFEM) return 4;
    if (kind == RIG_MEGADOG) return 3;
    if (kind == RIG_CAPSULO) return 4;
    return 1;
}
/* коэффициент попадания 80%: промах гасит ВЕСЬ взмах (окно dodge_t), иначе несколько кадров контакта дали бы ~100% */
static int hit_roll(Actor *t, int dmg) {
    if (dmg >= 99) return 1;                                   /* супер/таран/юла - без промахов */
    if (t->dodge_t > 0) return 0;
    if (t->invuln_t > 0 || t->hp <= 0) return 1;               /* actor_damage сам откажет */
    if ((int)(g_rand() % 100u) < 20) {
        t->dodge_t = 24;
        spark(FX_INT(t->x), FX_INT(t->y) - t->h - 4, 3, 0xFFFFFFFFu, 1);
        return 0;
    }
    return 1;
}
int hurt_hero(Actor *h, int dmg, int dir, fx kx, fx ky) {
    if (!hit_roll(h, dmg)) return 0;
    if (!actor_damage(h, dmg, dir, kx, ky, HURT_FRAMES)) return 0;
    spark(FX_INT(h->x), FX_INT(h->y) - h->h / 2, 7, 0xFFFFD040u, 2);
    if (h == player()) G.shake = 6;
    return 1;
}

/* misc2 врага: 0 = убит союзником, 1 = убит игроком (для раздельных фрагов) */
static int g_attacker = -1;                                          /* какой герой сейчас наносит удар (для переключения внимания врагов) */
static int hurt_enemy(Actor *e, int dmg, int dir, fx kx, fx ky) {
    if (e->kind == RIG_HEAVY) { kx /= 2; ky = ky * 2 / 3; }
    if (e->kind == RIG_CAPSULO && e->attack_kind != AT_NONE) {          /* чёрный щит на время стрельбы */
        spark(FX_INT(e->x), FX_INT(e->y) - e->h / 2, 3, 0xFFB0B8C8u, 2); return 0;
    }
    if (!hit_roll(e, dmg)) return 0;
    if (!actor_damage(e, dmg, dir, kx, ky, HURT_FRAMES)) return 0;
    if (g_attacker >= 0 && g_attacker != G.cur) { e->aggro_h = g_attacker; e->aggro_t = 30; }   /* короткий ответ напарнику, затем снова игрок */
    spark(FX_INT(e->x), FX_INT(e->y) - e->h / 2, 8, 0xFFFFF0A0u, 2);
    sound_play_sfx_at(SFX_ENEMY_HIT, FX_INT(e->x), FX_INT(e->y));
    /* атрибуция убийства: если рядом активный игрок — фраги игрока */
    Actor *p = player();
    if (p && p->hp > 0 && ABSI(FX_INT(p->x) - FX_INT(e->x)) < 220 && ABSI(FX_INT(p->y) - FX_INT(e->y)) < 100)
        e->misc2 = 1;
    else
        e->misc2 = 0;
    return 1;
}

int hurt_enemy_pub(Actor *e, int dmg, int dir, fx kx, fx ky) { return hurt_enemy(e, dmg, dir, kx, ky); }

/* Цель врага: МАКСИМАЛЬНЫЙ приоритет — игрок. На героев отвлекаются только если игрока не видно
   или напарник сам ударил (короткий aggro). Приманка Megadog перебивает всё. */
static int hero_visible_to(const Actor *h, const Actor *from, int max_dx, int max_dy, int need_alive) {
    if (!h->active || (need_alive && h->hp <= 0)) return 0;
    return ABSI(FX_INT(h->x) - FX_INT(from->x)) <= max_dx && ABSI(FX_INT(h->y) - FX_INT(from->y)) <= max_dy;
}
static Actor *enemy_target(Actor *e, int max_dx, int max_dy, int need_alive) {
    Actor *d = events_decoy();
    if (d && ABSI(FX_INT(d->x) - FX_INT(e->x)) <= max_dx * 2 && ABSI(FX_INT(d->y) - FX_INT(e->y)) <= max_dy) return d;
    Actor *pl = player();
    /* Stealth: игрок в закрытой комнате — враги других комнат (открытых) его не видят и дерутся с героями */
    if (pl) {
        int pr = level_room(FX_INT(pl->x), FX_INT(pl->y));
        if (pr >= 0 && pr < 12 && !G.explored[pr] && level_room(FX_INT(e->x), FX_INT(e->y)) != pr) pl = NULL;
    }
    /* игрок в зоне (широкий max_dy) — ВСЕГДА главная цель, aggro напарника не перебивает */
    if (pl && hero_visible_to(pl, e, max_dx, max_dy > 48 ? max_dy : 48, need_alive)) {
        e->aggro_h = G.cur;
        return pl;
    }
    if (e->aggro_t > 0) e->aggro_t--;
    if (e->aggro_t > 0 && e->aggro_h >= 0 && e->aggro_h < NUM_HEROES && e->aggro_h != G.cur && !G.solo_mode
        && hero_visible_to(&G.hero[e->aggro_h], e, max_dx, max_dy, need_alive))
        return &G.hero[e->aggro_h];
    Actor *best = NULL; int bd = 1 << 30;
    for (int i = 0; i < NUM_HEROES; i++) {
        Actor *h = &G.hero[i];
        if (G.solo_mode && i != G.cur) continue;                 /* solo: враги дерутся только с игроком, как в stealth */
        if (!hero_visible_to(h, e, max_dx, max_dy, need_alive)) continue;
        int dx = ABSI(FX_INT(h->x) - FX_INT(e->x));
        if (dx < bd) { bd = dx; best = h; e->aggro_h = i; }
    }
    if (best) e->aggro_t = 30;
    return best;
}

Actor *nearest_hero(const Actor *from, int max_dx, int max_dy, int need_alive) {
    if (from->team == 1) return enemy_target((Actor *)from, max_dx, max_dy, need_alive);
    Actor *best = NULL; int bd = 1 << 30;
    int fx_ = FX_INT(from->x), fy = FX_INT(from->y);
    for (int i = 0; i < NUM_HEROES; i++) {
        Actor *h = &G.hero[i];
        if (!h->active || (need_alive && h->hp <= 0)) continue;
        int dx = ABSI(FX_INT(h->x) - fx_), dy = ABSI(FX_INT(h->y) - fy);
        if (dx > max_dx || dy > max_dy) continue;
        if (dx < bd) { bd = dx; best = h; }
    }
    return best;
}

int melee_box_hit(const Actor *att, const Actor *t, int reach) {
    int ax0, ay0, ax1, ay1, tx0, ty0, tx1, ty1;
    actor_box(att, &ax0, &ay0, &ax1, &ay1); actor_box(t, &tx0, &ty0, &tx1, &ty1);
    int f0, f1;
    if (att->facing > 0) { f0 = ax1 - 2; f1 = ax1 + reach; } else { f1 = ax0 + 2; f0 = ax0 - reach; }
    return f1 >= tx0 && f0 <= tx1 && ay1 >= ty0 && ay0 <= ty1;
}

static int overlap(const Actor *a, const Actor *b, int pad) {
    int ax0, ay0, ax1, ay1, bx0, by0, bx1, by1;
    actor_box(a, &ax0, &ay0, &ax1, &ay1); actor_box(b, &bx0, &by0, &bx1, &by1);
    return ax1 + pad >= bx0 && ax0 - pad <= bx1 && ay1 + pad >= by0 && ay0 - pad <= by1;
}

/* ===================================================================== лазер Capsulo: луч под 45 градусов с отражением от стен/пола/потолка */
void laser_fire(int x, int y, int dirx, int diry) {
    for (int i = 0; i < HZ_MAX; i++) if (!G.hz[i].active) {
        Haz *z = &G.hz[i]; memset(z, 0, sizeof *z);
        z->active = 1; z->type = HZ_LASER; z->warn = 14; z->c = 34; z->dmg = 1;
        float fx_ = (float)x, fy_ = (float)y; int dx = dirx, dy = diry, len = 0;
        z->lx[0] = x; z->ly[0] = y; z->pts = 1;
        while (len < 360 && z->pts < 9) {
            int nx = (int)(fx_ + dx), ny = (int)(fy_ + dy);
            int sx = level_solid_rect(&G.lv, nx, (int)fy_, nx, (int)fy_), sy = level_solid_rect(&G.lv, (int)fx_, ny, (int)fx_, ny);
            int sd = level_solid_rect(&G.lv, nx, ny, nx, ny);
            if (sx || sy || sd) {
                z->lx[z->pts] = (int)fx_; z->ly[z->pts] = (int)fy_; z->pts++;
                if (sx) dx = -dx; if (sy) dy = -dy;
                if (!sx && !sy) { dx = -dx; dy = -dy; }
            } else { fx_ += (float)dx; fy_ += (float)dy; }
            len++;
        }
        if (z->pts < 10) { z->lx[z->pts] = (int)fx_; z->ly[z->pts] = (int)fy_; z->pts++; }
        sound_play_sfx_at(SFX_ZAP, x, y);
        return;
    }
}

/* ===================================================================== снаряды */
void fire_proj(int type, int team, int from_player, int x, int y, int dir, int dmg) {
    for (int i = 0; i < PJ_MAX; i++) {
        Proj *p = &G.pj[i];
        if (p->active) continue;
        memset(p, 0, sizeof *p);
        p->active = 1; p->type = type; p->team = team; p->from_player = from_player; p->dir = dir; p->dmg = dmg;
        p->x = INT_FX(x); p->y = INT_FX(y);
        switch (type) {
        case PJ_PLASMA:   p->vx = dir * FXF(4.5); p->life = 70; break;
        case PJ_BIG:      p->vx = dir * FXF(3.2); p->life = 90; break;
        case PJ_BOTTLE:   p->vx = dir * FXF(2.8); p->vy = -FXF(3.2); p->g = FXF(0.22); p->life = 140; break;
        case PJ_SNOW:     p->vx = dir * FXF(2.6); p->vy = -FXF(2.6); p->g = FXF(0.2); p->life = 100; break;
        case PJ_ROCKET:   p->vx = dir * FXF(2.6); p->life = 170; break;
        case PJ_DUMBBELL: p->vx = dir * FXF(2.4); p->vy = -FXF(3.0); p->g = FXF(0.2); p->life = 140; break;
        case PJ_PIZZA:    p->vx = dir * FXF(2.6); p->vy = -FXF(3.4); p->g = FXF(0.22); p->life = 150; break;
        case PJ_FIRE:     p->vx = dir * FXF(3.6); p->life = 80; break;
        case PJ_FIREBOT:  p->vx = dir * FXF(2.8); p->vy = -FXF(3.2); p->g = FXF(0.22); p->life = 140; break;
        case PJ_COMPUTER: p->vx = dir * FXF(2.8); p->vy = -FXF(2.4); p->g = FXF(0.2); p->life = 160; break;
        case PJ_BOULDER:  p->vx = dir * FXF(1.9); p->life = 420; break;
        case PJ_PIANO:    p->vx = dir * FXF(3.4); p->life = 330; break;
        case PJ_ROCKETB:  p->vx = dir * FXF(3.0); p->life = 160; break;
        default: p->life = 160; break;
        }
        return;
    }
}

Proj *fire_projx(int type, int team, int x, int y, float vx, float vy, int dmg, int life) {
    for (int i = 0; i < PJ_MAX; i++) {
        Proj *p = &G.pj[i];
        if (p->active) continue;
        memset(p, 0, sizeof *p);
        p->active = 1; p->type = type; p->team = team; p->dmg = dmg; p->life = life;
        p->x = INT_FX(x); p->y = INT_FX(y); p->vx = (fx)(vx * 256.f); p->vy = (fx)(vy * 256.f);
        p->dir = vx >= 0 ? 1 : -1;
        return p;
    }
    return NULL;
}

void rocket_splash(int x, int y) {
    spark(x, y, 18, 0xFFFF9020u, 3); spark(x, y, 8, 0xFFFFE070u, 2);
    sound_play_sfx_at(SFX_EXPLODE, x, y);
    G.shake = 4;
    for (int i = 0; i < NUM_HEROES; i++) {
        Actor *h = &G.hero[i];
        if (!h->active || h->hp <= 0) continue;
        int dx = FX_INT(h->x) - x, dy = FX_INT(h->y) - h->h / 2 - y;
        if (dx * dx + dy * dy < 26 * 26) hurt_hero(h, 1, sgn(dx) ? sgn(dx) : 1, FXF(2.6), FXF(3.0)); /* rocket default 1 */
    }
}

void rocket_splash_enemies(int x, int y) {                           /* взрыв ракеты союзной собаки */
    spark(x, y, 18, 0xFFFF9020u, 3); sound_play_sfx_at(SFX_EXPLODE, x, y);
    for (int i = 0; i < EN_MAX; i++) {
        Actor *e = &G.en[i];
        if (!e->active || e->hp <= 0) continue;
        int dx = FX_INT(e->x) - x, dy = FX_INT(e->y) - e->h / 2 - y;
        if (dx * dx + dy * dy < 34 * 34) { e->invuln_t = 0; actor_damage(e, 1, dx >= 0 ? 1 : -1, FXF(2.5), FXF(3.0), HURT_FRAMES); }
    }
}

/* бутылка Cyberbum и снежок Aunty: при падении/попадании расходится небольшая волна, задевающая ещё 1-2 врагов рядом */
__attribute__((unused)) static void wave_splash(Proj *p, int x, int y, const Actor *skip) {
    if (p->team != 0 || (p->type != PJ_BOTTLE && p->type != PJ_SNOW)) return;
    unsigned col = p->type == PJ_BOTTLE ? 0xFF80E0A0u : 0xFFF0F8FFu;
    for (int a = 0; a < 16; a++) { float t = a * 0.3927f; spark(x + (int)(cosf(t) * 22), y + (int)(sinf(t) * 12), 1, col, 1); }
    int hit = 0;
    for (int n = 0; n < 2; n++) {
        Actor *best = NULL; int bd = 30 * 30;
        for (int i = 0; i < EN_MAX; i++) {
            Actor *e = &G.en[i];
            if (!e->active || e->hp <= 0 || e == skip || e->invuln_t > 0) continue;
            int dx = FX_INT(e->x) - x, dy = FX_INT(e->y) - e->h / 2 - y;
            int d = dx * dx + dy * dy;
            if (d < bd) { bd = d; best = e; }
        }
        if (!best) break;
        hurt_enemy(best, 1, FX_INT(best->x) >= x ? 1 : -1, FXF(1.6), FXF(1.8));
        spark(FX_INT(best->x), FX_INT(best->y) - best->h / 2, 4, col, 2);
        hit++;
    }
    if (hit) game_log("Splash wave hit %d extra enemies", hit);
}

/* бутылка Cyberbum / снежок Aunty: при касании «брызгается» и убивает врагов в небольшом радиусе */
static void hero_splash(Proj *p, int x, int y) {
    if (p->team != 0 || (p->type != PJ_BOTTLE && p->type != PJ_SNOW && p->type != PJ_PLASMA && p->type != PJ_FIREBOT && p->type != PJ_ROCKETB)) return;
    int n = 0;
    for (int k = 0; k < EN_MAX; k++) {
        Actor *e = &G.en[k];
        if (!e->active || e->hp <= 0) continue;
        int dx = FX_INT(e->x) - x, dy = FX_INT(e->y) - e->h / 2 - y;
        if (ABSI(dx) > 30 || ABSI(dy) > 26) continue;
        e->invuln_t = 0;
        hurt_enemy(e, 1, dx >= 0 ? 1 : -1, FXF(2.0), FXF(2.4)); n++;
    }
    spark(x, y, 14, p->type == PJ_BOTTLE ? 0xFF80E0A0u : 0xFFF0F8FFu, 3);
    sound_play_sfx_at(SFX_SPLASH, x, y);
    (void)n;
}

/* точка попала в твёрдое: какой объект, и что с ним делает снаряд */
static void proj_on_solid(Proj *p, int x, int y) {
    Level *lv = &G.lv;
    for (int i = 0; i < lv->nobj; i++) {
        Obj *o = &lv->obj[i];
        if (!o->solid || o->on || x < o->x || x >= o->x + o->w || y < o->y || y >= o->y + o->h) continue;
        if (p->from_player && p->type == PJ_FIRE && o->type == O_WALL_ICE) { break_wall(o); }
        if (p->from_player && p->type == PJ_FIREBOT && o->type == O_WALL_WOOD) { break_wall(o); }
        if (p->from_player && p->type == PJ_BOTTLE && o->type == O_WALL_FIRE) { break_wall(o); say("Fire put out"); }
        break;
    }
    hero_splash(p, x, y);
    hero_splash(p, x, y);
    if ((p->type == PJ_MISSILE || p->type == PJ_PIZZA) && p->team == 0) rocket_splash_enemies(x, y);
    else if (p->type == PJ_GREEN && p->aux == 88) { boss_poison(x); spark(x, y, 12, 0xFF60E040u, 3); sound_play_sfx_at(SFX_SPLASH, x, y); }
    else if (p->type == PJ_ROCKET || p->type == PJ_PIZZA || p->type == PJ_MISSILE || p->type == PJ_HOMING) rocket_splash(x, y);
    else spark(x, y, p->type == PJ_BIG ? 12 : 5, p->type == PJ_BOTTLE ? 0xFF80E0A0u : (p->type == PJ_SNOW || p->type == PJ_SNOWBIG) ? 0xFFF0F8FFu : p->type == PJ_GREEN ? 0xFF60FF80u : 0xFF80F0FFu, 2);
    p->active = 0;
}

void projectiles_update(void) {
    for (int i = 0; i < PJ_MAX; i++) {
        Proj *p = &G.pj[i];
        if (!p->active) continue;
        if (--p->life <= 0) {
            if (p->type == PJ_MISSILE && p->aux == 77) { if (p->team == 1) rocket_splash(FX_INT(p->x), FX_INT(p->y)); else rocket_splash_enemies(FX_INT(p->x), FX_INT(p->y)); }
            p->active = 0; continue;
        }
        if (p->type == PJ_BOULDER) {                      /* катится по полу */
            int gx = FX_INT(p->x), gy = FX_INT(p->y);
            if (level_solid_rect(&G.lv, gx, gy + 11, gx, gy + 12)) { p->vy = 0; p->g = 0; } else p->g = FXF(0.3);
        }
        p->vy += p->g;
        if (p->type == PJ_HOMING) {                       /* самонаводящаяся ракета Watcher: ищет героя, которого выбрала */
            Actor *tg = &G.hero[p->aux % NUM_HEROES];
            if (tg->hp <= 0) tg = player();
            float dx = (float)(FX_INT(tg->x) - FX_INT(p->x)), dy = (float)(FX_INT(tg->y) - tg->h / 2 - FX_INT(p->y));
            float want = atan2f(dy, dx), cur = atan2f((float)p->vy, (float)p->vx), d = want - cur;
            while (d > 3.14159f) d -= 6.28318f;
            while (d < -3.14159f) d += 6.28318f;
            if (d > 0.07f) d = 0.07f; if (d < -0.07f) d = -0.07f;
            cur += d;
            p->vx = (fx)(cosf(cur) * 1.7f * 256.f); p->vy = (fx)(sinf(cur) * 1.7f * 256.f);
            if ((G.frame & 3) == 0) spark(FX_INT(p->x), FX_INT(p->y), 1, 0xFFC0C0C0u, 2);
        }
        if (p->type == PJ_PIZZA && p->team == 1) {          /* пицца рвётся рядом с героем */
            for (int k = 0; k < NUM_HEROES; k++) {
                Actor *h = &G.hero[k];
                if (!h->active || h->hp <= 0) continue;
                int ddx = FX_INT(h->x) - FX_INT(p->x), ddy = FX_INT(h->y) - h->h / 2 - FX_INT(p->y);
                if (ddx * ddx + ddy * ddy < 15 * 15) { rocket_splash(FX_INT(p->x), FX_INT(p->y)); p->active = 0; break; }
            }
            if (!p->active) continue;
        }
        int n = (int)((MAXI(ABSI(p->vx), ABSI(p->vy)) >> 8) + 1);
        for (int s = 0; s < n && p->active; s++) {
            p->x += p->vx / n; p->y += p->vy / n;
            int x = FX_INT(p->x), y = FX_INT(p->y);
            if (level_solid_rect(&G.lv, x, y, x, y)) { proj_on_solid(p, x, y); break; }
            if (y > G.lv.h + 60 || y < -60) { p->active = 0; break; }
            int r = p->type == PJ_BIG ? 6 : p->type == PJ_SNOWBIG ? 10 : p->type == PJ_BOULDER ? 11 : p->type == PJ_PIANO ? 12 : p->type == PJ_COMPUTER ? 5 : p->type == PJ_FIRE ? 5 : (p->type == PJ_GREEN || p->type == PJ_GAS) ? 5 : 3;
            if (p->team == 0) {
                for (int k = 0; k < EN_MAX && p->active; k++) {
                    Actor *e = &G.en[k];
                    if (!e->active || e->hp <= 0) continue;
                    int x0, y0, x1, y1; actor_box(e, &x0, &y0, &x1, &y1);
                    if (x + r < x0 || x - r > x1 || y + r < y0 || y - r > y1) continue;
                    if (hurt_enemy(e, p->dmg, p->dir, FXF(2.4), FXF(2.6))) { spark(x, y, 5, 0xFF80F0FFu, 2); hero_splash(p, x, y); if (p->type != PJ_BOULDER) p->active = 0; }
                }
                if (p->active && p->from_player && G.boss_state != BS_SLEEP && G.boss_state != BS_DEAD) {
                    int x0, y0, x1, y1; actor_box(&G.boss, &x0, &y0, &x1, &y1);
                    if (!(x + r < x0 || x - r > x1 || y + r < y0 || y - r > y1)) { boss_hurt(p->dmg > 2 ? 2 : p->dmg, p->dir); p->active = 0; }
                }
            } else {
                for (int k = 0; k < NUM_HEROES && p->active; k++) {
                    Actor *h = &G.hero[k];
                    if (!h->active || h->hp <= 0) continue;
                    int x0, y0, x1, y1; actor_box(h, &x0, &y0, &x1, &y1);
                    if (x + r < x0 || x - r > x1 || y + r < y0 || y - r > y1) continue;
                    /* Refreezer ловит гантели дверцей холодильника (activating / act_t) */
                    if (p->type == PJ_DUMBBELL && h->kind == RIG_REFREEZER && (h->activating || G.act_t[k] > 0)) {
                        p->active = 0;
                        spark(x, y, 8, 0xFF80FFA0u, 2);
                        sound_play_sfx_at(SFX_PICKUP, x, y);
                        if (k == G.cur || 1) say("Refreezer caught a dumbbell!");
                        continue;
                    }
                    if (p->type == PJ_ROCKET || p->type == PJ_PIZZA || p->type == PJ_MISSILE || p->type == PJ_HOMING) { rocket_splash(x, y); p->active = 0; }
                    else if (p->type == PJ_SNOWBIG) {                                /* снежная глыба замораживает героя */
                        if (h->invuln_t == 0 || h->frozen == 0) {
                            h->frozen = h->stun_t = 240; h->attack_kind = AT_NONE; h->vx = 0; h->on_rope = 0; h->spin_t = 0;
                            sound_play_sfx_at(SFX_FREEZE, FX_INT(h->x), FX_INT(h->y));
                            spark(FX_INT(h->x), FX_INT(h->y) - 14, 16, 0xFFD8F0FFu, 3);
                            if (h == player()) say("Frozen! Wait to thaw...");
                            p->active = 0;
                        }
                    }
                    else if (p->type == PJ_GAS) hurt_hero(h, p->dmg, p->dir, FXF(1.4), FXF(2.0));
                    else if (hurt_hero(h, p->dmg, p->dir, FXF(2.0), FXF(2.6))) p->active = 0;
                    else if (h->invuln_t > 0) { /* пролетает сквозь неуязвимого */ }
                }
            }
        }
    }
}

/* ===================================================================== атаки героев */
static const int HERO_REACH[RIG_COUNT] = {22, 26, 32, 26, 0, 0, 0, 0, 26, 22, 22};

static int wall_ability(int hero_kind, int wall_type) {
    /* огонь — только бутылка Cyberbum (PJ_BOTTLE), не супер */
    return (hero_kind == RIG_BUM && wall_type == O_WALL_WOOD) ||
           (hero_kind == RIG_DOORKEEPER && wall_type == O_WALL_WOOD) ||
           (hero_kind == RIG_MISTRESS && wall_type == O_WALL_STONE) ||
           (hero_kind == RIG_PENGI && wall_type == O_WALL_ICE);
}

static void super_walls(Actor *h, int reach, int is_player) {
    int x0, y0, x1, y1; actor_box(h, &x0, &y0, &x1, &y1);
    int f0 = h->facing > 0 ? x1 : x0 - reach, f1 = h->facing > 0 ? x1 + reach : x0;
    int any = 0;
    for (int i = 0; i < G.lv.nobj; i++) {
        Obj *o = &G.lv.obj[i];
        if (o->type < O_WALL_ICE || o->type > O_WALL_FIRE || o->on) continue;
        if (f1 < o->x || f0 > o->x + o->w || y1 < o->y || y0 > o->y + o->h) continue;
        any = 1;
        if (wall_ability(h->kind, o->type)) { if (is_player) break_wall(o); }
        else if (is_player) say(o->type == O_WALL_FIRE ? "Fire is put out by Cyberbum's bottle" : "This hero can't break this wall");
    }
    (void)any;
}

static void hit_all_enemies_in(Actor *h, int hi, int reach, int dmg, int both_sides) {
    for (int k = 0; k < EN_MAX; k++) {
        Actor *e = &G.en[k];
        if (!e->active || e->hp <= 0 || (h->hit_mask & (1 << k))) continue;
        int hit;
        if (both_sides) { int dx = ABSI(FX_INT(e->x) - FX_INT(h->x)), dy = ABSI(FX_INT(e->y) - FX_INT(h->y)); hit = dx <= reach && dy <= 24; }
        else hit = melee_box_hit(h, e, reach);
        if (hit) {
            h->hit_mask |= 1 << k;
            int dir = both_sides ? (FX_INT(e->x) >= FX_INT(h->x) ? 1 : -1) : h->facing;
            if (dmg >= 99) e->invuln_t = 0;                              /* суператака убивает даже сразу после появления */
            hurt_enemy(e, dmg, dir, FXF(2.4), FXF(2.6));
        }
    }
    if (hi == G.cur && !(h->hit_mask & 256) && G.boss_state != BS_SLEEP && G.boss_state != BS_DEAD) {
        int hit;
        if (both_sides) hit = ABSI(FX_INT(G.boss.x) - FX_INT(h->x)) <= reach && ABSI(FX_INT(G.boss.y) - FX_INT(h->y)) <= 50;
        else hit = melee_box_hit(h, &G.boss, reach);
        /* супер (dmg>=99) → 2 урона боссу; простая → 1; нужно 2*(3+level) HP */
        if (hit) { h->hit_mask |= 256; boss_hurt(dmg >= 99 ? 2 : (dmg > 0 ? dmg : 1), h->facing); }
    }
}

/* дальность супер-удара: Cyberbum/Aunty/Nerd — одинаково (как Nerd ~150) */
static int super_reach(const Actor *h) {
    if (h->kind == RIG_MISTRESS) return 36;                  /* бензопила: малая дальность */
    if (h->kind == RIG_BUM || h->kind == RIG_DOORKEEPER || h->kind == RIG_NERD) return 150;
    int w = h->w > 0 ? h->w : 12;
    return w * 6;
}

static void hero_attack_effects_impl(int hi) {
    Actor *h = &G.hero[hi];
    if (h->hp <= 0) return;
    int dashing = h->kind == RIG_REFREEZER && h->dash_t > 0;
    if (h->attack_kind == AT_NONE && !dashing && !(h->kind == RIG_MISTRESS && h->spin_t > 0)) return;      /* разгон и юла - не атаки, но тоже бьют */
    float ph = actor_attack_phase(h);
    int px = FX_INT(h->x), py = FX_INT(h->y), pl = hi == G.cur;
    int sreach = super_reach(h);
    if (h->attack_kind == AT_MELEE) {
        if (ph >= 0.30f && ph <= 0.70f) hit_all_enemies_in(h, hi, HERO_REACH[h->kind], 1, 0);
        /* ключ Cyberbum / метла Aunty: ближняя атака ломает свои стены */
        if ((h->kind == RIG_BUM || h->kind == RIG_DOORKEEPER) && ph >= 0.30f && !h->attack_fired) {
            h->attack_fired = 1;
            if (h->kind == RIG_DOORKEEPER) {                 /* метла сметает снег: каждый взмах = часть работы */
                int x0, y0, x1, y1; actor_box(h, &x0, &y0, &x1, &y1);
                for (int i = 0; i < G.lv.nobj; i++) {
                    Obj *o = &G.lv.obj[i];
                    if (o->type != O_SNOW || o->on) continue;
                    if (x1 + 20 < o->x || x0 - 20 > o->x + o->w || y1 < o->y || y0 > o->y + o->h) continue;
                    o->progress += SWEEP_FRAMES / 3 + 1;
                    spark(o->x + o->w / 2, o->y + 4, 6, 0xFFF4F8FCu, 2);
                    if (o->progress >= SWEEP_FRAMES) {
                        o->on = 1; if (o->target >= 0) G.lv.obj[o->target].visible = 1;
                        if (pl) say("A key under the snow!");
                    }
                }
            }
        }
    } else if (h->attack_kind == AT_RANGED) {
        if (ph >= 0.35f && !h->attack_fired) {
            h->attack_fired = 1;
            int sp = h->attack_special;
            if (sp == 1) {                                                 /* V */
                if (h->kind == RIG_PENGI) fire_proj(PJ_FIRE, 0, pl, px + h->facing * 16, py - 20, h->facing, 1);
                else if (h->kind == RIG_BUM) fire_proj(PJ_FIREBOT, 0, pl, px + h->facing * 10, py - 30, h->facing, 1);
                else if (h->kind == RIG_REFREEZER && pl) say("Wall flip: needs a tile wall (from level 5)");
                else if (h->kind == RIG_HERO7 && pl) say("No underwater gratings here (last 3 levels)");
            } else if (sp == 2) {                                          /* C: супер через бросок */
                if (h->kind == RIG_BUM) fire_proj(PJ_COMPUTER, 0, pl, px + h->facing * 10, py - 30, h->facing, 5);
                else if (h->kind == RIG_DOORKEEPER) fire_proj(PJ_BOULDER, 0, pl, px + h->facing * 14, py - 11, h->facing, 5);
                else if (h->kind == RIG_NERD) fire_proj(PJ_PIANO, 0, pl, px + h->facing * 20, py - 17, h->facing, 5);
            } else if (h->kind == RIG_HERO7) fire_proj(PJ_ROCKETB, 0, pl, px + h->facing * 24, py - 16, h->facing, 1);
            else if (h->kind == RIG_PENGI) fire_proj(PJ_PLASMA, 0, pl, px + h->facing * 16, py - 20, h->facing, 1);
            else if (h->kind == RIG_BUM) fire_proj(PJ_BOTTLE, 0, pl, px + h->facing * 10, py - 30, h->facing, 1);
            else if (h->kind == RIG_DOORKEEPER) fire_proj(PJ_SNOW, 0, pl, px + h->facing * 10, py - 28, h->facing, 1);
            else if (h->kind == RIG_NERD) events_guitar_throw(hi);
            else if (h->kind == RIG_MISTRESS) {                            /* кнут: полэкрана вперёд */
                int hit = 0;
                for (int k = 0; k < EN_MAX; k++) {
                    Actor *e = &G.en[k];
                    if (!e->active || e->hp <= 0) continue;
                    int dx = (FX_INT(e->x) - px) * h->facing, dy = ABSI(FX_INT(e->y) - py);
                    if (dx < 0 || dx > 160 || dy > 26 || blocked_between(px, FX_INT(e->x), py - 14)) continue;
                    hurt_enemy(e, 1, h->facing, FXF(2.0), FXF(2.0)); hit++;
                }
                if (hi == G.cur && G.boss_state != BS_SLEEP && G.boss_state != BS_DEAD) {
                    int dx = (FX_INT(G.boss.x) - px) * h->facing;
                    if (dx >= 0 && dx <= 160 && ABSI(FX_INT(G.boss.y) - py) <= 60) boss_hurt(1, h->facing);
                }
                G.whip_t = 14; G.whip_hi = hi;
                (void)hit;
            }
            else if (h->kind == RIG_REFREEZER) {
                /* средняя атака: только бросок гамбургера; дверца холодильника открывается лишь когда врагов нет */
                fire_proj(PJ_PIZZA, 0, pl, px + h->facing * 12, py - 22, h->facing, 1);
            }
        }
    } else if (h->attack_kind == AT_SUPER) {
        if (ph >= 0.50f && !h->attack_fired) {
            h->attack_fired = 1;
            G.shake = 4;
            for (int k = 0; k < EN_MAX; k++) {                             /* супер: все враги в 100 px по линии пола впереди гибнут */
                Actor *e = &G.en[k];
                if (!e->active || e->hp <= 0) continue;
                int dx = (FX_INT(e->x) - px) * h->facing;
                if (dx < -8 || dx > 100 || ABSI(FX_INT(e->y) - py) > 26) continue;
                if (blocked_between(px, FX_INT(e->x), py - 14)) continue;
                e->invuln_t = 0; e->dodge_t = 0;
                if (actor_damage(e, 99, h->facing, FXF(5.0), FXF(3.5), HURT_FRAMES)) {
                    spark(FX_INT(e->x), FX_INT(e->y) - 12, 10, 0xFFFFE080u, 3);
                    sound_play_sfx_at(SFX_ENEMY_HIT, FX_INT(e->x), FX_INT(e->y));
                    if (hi == G.cur) e->misc2 = 1;
                }
            }
            switch (h->kind) {
            case RIG_PENGI:                                                /* C: большой бластер, 5 квадратов */
                fire_proj(PJ_BIG, 0, pl, px + h->facing * 16, py - 20, h->facing, 5);
                hit_all_enemies_in(h, hi, sreach, 5, 1);
                break;
            case RIG_MISTRESS:                                             /* V: бензопила — стены Domina, врагам урон */
                super_walls(h, sreach, pl);
                hit_all_enemies_in(h, hi, sreach, 2, 0);
                spark(px + h->facing * 26, py - 18, 12, 0xFFFFD060u, 2);
                break;
            case RIG_NERD:                                                 /* V: гитара ломает платформу, урон как у X */
                hit_all_enemies_in(h, hi, sreach, 1, 1);
                spark(px + h->facing * 20, py - 16, 8, 0xFFFFE890u, 3);
                sound_play_sfx_at(SFX_ZAP, px, py);
                break;
            case RIG_HERO7: {                                              /* C: синий лазер по всем врагам на экране */
                int hit = 0;
                for (int k = 0; k < EN_MAX; k++) {
                    Actor *e = &G.en[k];
                    if (!e->active || e->hp <= 0) continue;
                    int ex = FX_INT(e->x) - G.cam_x, ey = FX_INT(e->y) - G.cam_y;
                    if (ex < -10 || ex > VIEW_W + 10 || ey < 0 || ey > VIEW_H + 20) continue;
                    e->invuln_t = 0; hurt_enemy(e, 5, FX_INT(e->x) >= px ? 1 : -1, FXF(1.0), FXF(1.0)); hit++;
                    int steps = 12;
                    for (int q = 0; q <= steps; q++) spark(px + (FX_INT(e->x) - px) * q / steps, py - 16 + (FX_INT(e->y) - 12 - (py - 16)) * q / steps, 1, 0xFF60C0FFu, 2);
                }
                if (hi == G.cur && G.boss_state != BS_SLEEP && G.boss_state != BS_DEAD) boss_hurt(2, h->facing);
                (void)hit;
                break; }
            case RIG_REFREEZER:                                            /* C: сбивает всех в сторону взгляда */
                for (int k = 0; k < EN_MAX; k++) {
                    Actor *e = &G.en[k];
                    if (!e->active || e->hp <= 0) continue;
                    int dx = (FX_INT(e->x) - px) * h->facing;
                    if (dx < -12 || dx > 220 || ABSI(FX_INT(e->y) - py) > 40) continue;
                    e->invuln_t = 0;
                    hurt_enemy(e, 1, h->facing, FXF(6.0), FXF(3.5));
                    spark(FX_INT(e->x), FX_INT(e->y) - 14, 6, 0xFFE8F4FFu, 3);
                }
                for (int d = 10; d <= 220; d += 10) spark(px + h->facing * d, py - 12, 1, 0xFFC8E8FFu, 2);
                break;
            default:
                break;
            }
        }
    }
    /* разгон Refreezer: убивает всех на пути */
    if (h->kind == RIG_REFREEZER && h->dash_t > 0) {
        for (int k = 0; k < EN_MAX; k++) {
            Actor *e = &G.en[k];
            if (!e->active || e->hp <= 0 || !overlap(h, e, 4)) continue;
            int dir = FX_INT(e->x) >= px ? 1 : -1;
            if (e->kind == RIG_MEGADOG || e->kind == RIG_FIREWALKER || e->kind == RIG_IRONFEM) {
                h->dash_t = 0; h->vx = -dir * FXF(1.5); spark(px + dir * 10, py - 12, 8, 0xFFC0C8D0u, 2); say("Refreezer is useless against this enemy");
                break;
            }
            e->invuln_t = 0;
            if (actor_damage(e, 99, dir, FXF(6.0), FXF(4.5), HURT_FRAMES)) {
                spark(FX_INT(e->x), FX_INT(e->y) - 12, 10, 0xFFE8F4FFu, 3);
                sound_play_sfx_at(SFX_ENEMY_HIT, FX_INT(e->x), FX_INT(e->y));
            }
        }
        if (hi == G.cur && G.boss_state != BS_SLEEP && G.boss_state != BS_DEAD && overlap(h, &G.boss, 4)) {
            if (h->spin_hit_t > 0) h->spin_hit_t--;
            else { h->spin_hit_t = 24; boss_hurt(G.boss.kind == RIG_BIGPUSSY ? 3 : 1, h->facing); }
        }
    } else if (h->kind == RIG_REFREEZER && h->spin_hit_t > 0) h->spin_hit_t--;
    /* юла Domina: при касании мгновенно убивает врагов; боссу — 2 урона (супер) */
    if (h->kind == RIG_MISTRESS && h->spin_t > 0) {
        int sx = FX_INT(h->x), sy = FX_INT(h->y);
        for (int k = 0; k < EN_MAX; k++) {
            Actor *e = &G.en[k];
            if (!e->active || e->hp <= 0) continue;
            if (!overlap(h, e, 10)) continue;
            e->invuln_t = 0;
            if (actor_damage(e, 99, FX_INT(e->x) >= sx ? 1 : -1, FXF(5.0), FXF(4.0), HURT_FRAMES)) {
                spark(FX_INT(e->x), FX_INT(e->y) - 12, 10, 0xFFFFD080u, 3);
                sound_play_sfx_at(SFX_ENEMY_HIT, FX_INT(e->x), FX_INT(e->y));
            }
        }
        if (hi == G.cur && G.boss_state != BS_SLEEP && G.boss_state != BS_DEAD && overlap(h, &G.boss, 10)) {
            if (h->spin_hit_t > 0) h->spin_hit_t--;
            else { h->spin_hit_t = 18; boss_hurt(2, h->facing); }
        }
    }
}

/* ===================================================================== напарники под управлением ИИ */
static int lift_gap_ahead(const Actor *a, int dir, int *gap) {
    int ax = FX_INT(a->x), ay = FX_INT(a->y), edge = ax + dir * (a->w / 2);
    int best = -1, bg = 1 << 30;
    for (int i = 0; i < G.lv.nobj; i++) {
        const Obj *o = &G.lv.obj[i];
        if (o->type != O_LIFT) continue;
        if (o->y < ay - 22 || o->y > ay + a->h + 14) continue;          /* на уровне прыжка или ниже не глубже роста */
        int near_x = dir > 0 ? o->x - edge : edge - (o->x + o->w);       /* зазор до ближнего края лифта */
        int far_x = dir > 0 ? o->x + o->w - edge : edge - o->x;
        if (far_x <= 0 || near_x > 44) continue;
        if (near_x < 0) near_x = 0;
        if (near_x < bg) { bg = near_x; best = i; }
    }
    if (gap) *gap = bg;
    return best;
}

static int ahead_ok(const Actor *a, int dir) {
    int px = FX_INT(a->x) + dir * (a->w / 2 + 4), py = FX_INT(a->y);
    if (!level_solid_rect_team(&G.lv, px, py, px, py + 1, a->team)) {
        /* стык / невидимая платформа / мост / край PLAT той же высоты — проходимо */
        int ok_same = 0;
        for (int d = 2; d <= 28; d += 2) {
            int x = px + dir * d;
            if (level_solid_rect_team(&G.lv, x, py, x, py + 1, a->team)) { ok_same = 1; break; }
            if (level_solid_rect_team(&G.lv, x, py - 3, x, py - 1, a->team)) { ok_same = 1; break; }
            if (level_solid_rect_team(&G.lv, x, py + 1, x, py + 4, a->team)) { ok_same = 1; break; }
        }
        /* объекты O_PLAT / O_BRIDGE / O_LIFT на той же оси впереди */
        if (!ok_same) {
            for (int i = 0; i < G.lv.nobj; i++) {
                const Obj *o = &G.lv.obj[i];
                if (!o->solid) continue;
                if (o->type != O_PLAT && o->type != O_BRIDGE && o->type != O_LIFT) continue;
                if (ABSI(o->y - py) > 6) continue;
                int near = dir > 0 ? o->x - px : px - (o->x + o->w);
                if (near >= 0 && near <= 28) { ok_same = 1; break; }
                if (px >= o->x && px <= o->x + o->w) { ok_same = 1; break; }
            }
        }
        if (!ok_same && lift_gap_ahead(a, dir, NULL) < 0) return 0;
    }
    if (level_solid_rect_team(&G.lv, px, py - a->h, px, py - 6, a->team)) return 0;
    return 1;
}

/* дошли до края с лифтом через зазор: отключаем защиту от падения и прыгаем на лифт */
static void lift_hop(Actor *a, Intent *in) {
    if (!in->dir || a->on_rope || !actor_on_ground(a, &G.lv)) return;
    if (a->stand_obj >= 0 && a->stand_obj < G.lv.nobj && G.lv.obj[a->stand_obj].type == O_LIFT) return;
    int px = FX_INT(a->x) + in->dir * (a->w / 2 + 4), py = FX_INT(a->y), gap = 0;
    if (level_solid_rect_team(&G.lv, px, py, px, py + 1, a->team)) return;
    int i = lift_gap_ahead(a, in->dir, &gap);
    if (i < 0) return;
    if (G.lv.obj[i].y < py - 4 || gap > 6) in->jump_pressed = 1;
}

#define GAP_JUMP_W  96   /* безопасная ширина прыжка союзников */
#define GAP_DROP_H  40
/* ширина/высота провала впереди; 1 = есть провал */
static int solid_near(const Actor *a, int x, int py);
static int gap_measure(const Actor *a, int dir, int *out_w, int *out_h) {
    int px = FX_INT(a->x), py = FX_INT(a->y);
    int foot = px + dir * (a->w / 2 + 2);
    if (level_solid_rect_team(&G.lv, foot + dir * 2, py, foot + dir * 2, py + 1, a->team)) {
        if (out_w) *out_w = 0; if (out_h) *out_h = 0; return 0;
    }
    int w = 0, land_y = -1;
    for (int d = 2; d <= 120; d += 2) {
        int x = foot + dir * d;
        if (solid_near(a, x, py)) { w = d; land_y = py; break; }
        /* O_PLAT / O_BRIDGE / O_LIFT на той же высоте */
        for (int i = 0; i < G.lv.nobj && land_y < 0; i++) {
            const Obj *o = &G.lv.obj[i];
            if (!o->solid) continue;
            if (o->type != O_PLAT && o->type != O_BRIDGE && o->type != O_LIFT) continue;
            if (ABSI(o->y - py) > 8) continue;
            if (x >= o->x && x <= o->x + o->w) { w = d; land_y = o->y; break; }
        }
    }
    if (land_y < 0) {
        for (int d = 2; d <= 100; d += 2) {
            int x = foot + dir * d;
            for (int dy = 4; dy <= 140; dy += 4) {
                if (level_solid_rect_team(&G.lv, x, py + dy, x, py + dy + 1, a->team)) {
                    w = d; land_y = py + dy; break;
                }
                for (int i = 0; i < G.lv.nobj && land_y < 0; i++) {
                    const Obj *o = &G.lv.obj[i];
                    if (!o->solid) continue;
                    if (o->type != O_PLAT && o->type != O_BRIDGE && o->type != O_LIFT) continue;
                    if (x < o->x || x > o->x + o->w) continue;
                    if (o->y >= py + dy - 2 && o->y <= py + dy + 6) {
                        w = d; land_y = o->y; break;
                    }
                }
            }
            if (land_y >= 0) break;
        }
    }
    if (out_w) *out_w = w > 0 ? w : 120;
    if (out_h) *out_h = land_y >= 0 ? (land_y - py) : 140;
    return 1;
}
static int solid_near(const Actor *a, int x, int py) {         /* опора ногами в x на высоте py-6..py+3 */
    for (int k = -4; k <= 4; k += 2) if (level_solid_rect_team(&G.lv, x, py + k, x, py + k, a->team)) return 1;
    return 0;
}
static int ledge_ahead(const Actor *a, int dir) {              /* впереди нет земли (микро-щели до 8 px - не обрыв) */
    int px = FX_INT(a->x) + dir * (a->w / 2 + 4), py = FX_INT(a->y);
    if (level_solid_rect_team(&G.lv, px, py, px, py + 1, a->team)) return 0;
    for (int d = 2; d <= 8; d += 2) {
        int x = px + dir * d;
        if (solid_near(a, x, py)) return 0;
    }
    return 1;
}
static int pit_near(int tx, int ty, int dir) {                  /* за героем в сторону удара обрыв: можно сбить вниз */
    for (int k = 12; k <= 44; k += 8) { int x = tx + dir * k; if (!level_solid_rect(&G.lv, x, ty, x, ty + 1)) return 1; }
    return 0;
}

/* работающий лифт между этажом актёра и этажом gy. 1 = актёр занят лифтом (ждёт, едет, сходит) */
/* прыжок через щель: ИИ не тормозит над пропастью и держит разгон до приземления (просто бег + прыжок, без «амплитуды») */
static void ai_gap_jump(Actor *h, int sd) {
    h->ai_jump_t = 90; h->ai_jump_dir = sd; h->facing = sd;
    fx v = h->vx * sd; if (v < FXF(2.0)) h->vx = sd * FXF(2.0);        /* гарантированная дальность */
}
static int nav_lift(Actor *a, Intent *in, int gx, int gy, int maxd) {
    int ax = FX_INT(a->x), ay = FX_INT(a->y);
    int best = -1, bd = 1 << 30;
    for (int i = 0; i < G.lv.nobj; i++) {
        const Obj *o = &G.lv.obj[i];
        if (o->type != O_LIFT || o->have < o->need) continue;
        int top = o->y1, bot = o->y0;
        int ok = (a->stand_obj == i && (ABSI(top - gy) <= 14 || ABSI(bot - gy) <= 14))
              || (ABSI(top - ay) <= 10 && ABSI(bot - gy) <= 14) || (ABSI(bot - ay) <= 10 && ABSI(top - gy) <= 14)
              || (ABSI(o->y - ay) <= 10);
        if (!ok) continue;
        if (a->team == 1) {                                  /* враги не уезжают на закрытый этаж (только в открытую комнату или к игроку) */
            int dest = (ABSI(top - ay) <= 12 || a->stand_obj == i && ABSI(o->y - top) <= 12) ? bot : top;
            int dr = level_room(o->x + o->w / 2, dest), gr = level_room(gx, gy);
            if (dr != gr && (dr < 0 || dr >= 12 || !G.explored[dr])) continue;
        }
        int d = ABSI(o->x + o->w / 2 - ax);
        /* приоритет стартующему (сигнал) или уже едущему лифту */
        if (o->timer < 0 || o->state == LS_UP || o->state == LS_DOWN) d -= 200;
        if (d > maxd || d >= bd) continue;
        bd = d; best = i;
    }
    if (best < 0) return 0;
    const Obj *o = &G.lv.obj[best];
    int cx = o->x + o->w / 2;
    int half = o->w / 2 + 4;
    /* уже на лифте */
    if (a->stand_obj == best) {
        if (o->state == LS_UP || o->state == LS_DOWN) {
            in->dir = 0; /* едем — стоим смирно */
            return 1;
        }
        if (ABSI(o->y - gy) <= 8) {
            int d = sgn(gx - cx); if (!d) d = a->facing;
            in->dir = d; a->facing = d;
        } else if (ABSI(cx - ax) > 6) {
            int d = sgn(cx - ax); in->dir = d; a->facing = d;
        } else {
            in->dir = 0;
        }
        return 1;
    }
    /* приоритет: сигналящий/едущий лифт той же шахты */
    int prefer = (o->timer < 0 || o->state == LS_UP || o->state == LS_DOWN);
    (void)prefer;
    /* лифт на нашем этаже: зайти в центр и СТОЯТЬ */
    if (ABSI(o->y - ay) <= 8) {
        if (ABSI(cx - ax) > 5) {
            int d = sgn(cx - ax);
            if (ABSI(cx - ax) <= half) {           /* временный якорь: у кабины — плавно подтягиваем в центр, без качания */
                in->dir = 0; a->vx = 0; a->x += INT_FX(d) / 2;
            } else if (ahead_ok(a, d) || ABSI(cx - ax) < 40) { in->dir = d; a->facing = d; }
            else in->dir = 0;
        } else {
            in->dir = 0;
            a->vx = 0;
        }
        return 1;
    }
    /* лифт уехал: подойти к шахте и СТОЯТЬ */
    if (ABSI(cx - ax) > half + 8) {
        int d = sgn(cx - ax);
        if (ahead_ok(a, d)) { in->dir = d; a->facing = d; }
        else in->dir = 0;
    } else {
        in->dir = 0;
        a->vx = 0;
    }
    return 1;
}

/* канат врага на нашем этаже: идти к нему и спускаться/подниматься. up/down - куда надо; ty<0 - любая высота */
static int rope_nav(Actor *e, Intent *in, int want_up, int want_down, int ty) {
    int ex = FX_INT(e->x), ey = FX_INT(e->y);
    int best = -1, bd = 1 << 30;
    for (int i = 0; i < G.lv.nobj; i++) {
        const Obj *o = &G.lv.obj[i];
        if (o->type != O_ROPE || o->kind != 1) continue;
        int top = o->y, bot = o->y + o->h;
        int here = want_down ? ABSI(top - ey) <= 12 : want_up ? ABSI(bot - ey) <= 12 : 0;
        if (!here) continue;
        if (ty >= 0 && want_down && bot < ty - 12) continue;
        if (ty >= 0 && want_up && top > ty + 12) continue;
        int d = ABSI(o->x + o->w / 2 - ex);
        if (d < bd) { bd = d; best = i; }
    }
    if (best < 0) return 0;
    Obj *o = &G.lv.obj[best];
    int cx = o->x + o->w / 2, d = sgn(cx - ex);
    if (ABSI(cx - ex) > 8) {
        if (!ahead_ok(e, d) && !(want_down && ABSI(cx - ex) < 30)) return 0;     /* путь к канату перекрыт */
        e->facing = d; in->dir = d;
    } else {
        o->visible = 1; e->misc = best; e->aim = want_up ? 1 : -1;
        if (want_up) in->up = 1; else in->down = 1;
    }
    return 1;
}

const char *const ENEMY_NAME[NUM_ENEMY_CLASSES] = {"Rocketpunk", "Dumbpunk", "Ramrod", "Deliver", "Firewalker", "Monoman", "Ironfem",
                                                    "Megadog", "Capsulo", "Enemy10", "Enemy11", "Enemy12"};
const int ENEMY_RIG[NUM_ENEMY_CLASSES] = {RIG_SCOUT, RIG_BODY, RIG_HEAVY, RIG_ENEMY4, RIG_ENEMY5, RIG_ENEMY6, RIG_ENEMY7,
                                          RIG_ENEMY8, RIG_ENEMY9, RIG_ENEMY10, RIG_ENEMY11, RIG_ENEMY12};
int enemy_class_loaded(int cls) { return cls >= 0 && cls < 9; }      /* классы 10..12 - в разработке */

/* дальность суператаки героя: у Пингви это снаряд, у остальных - удар вплотную */
static int super_range(const Actor *h) {
    switch (h->kind) { case RIG_PENGI: return 200; case RIG_BUM: return 180; case RIG_DOORKEEPER: return 200; case RIG_NERD: return 220; case RIG_REFREEZER: return 200; default: return 34 + 8; }
}

/* Напарник:
   - всегда атакует врагов на своём этаже;
   - в режиме группы (Q) после боя / между боями идёт к игроку;
   - «видит» игрока только на том же этаже (Δy ≤ 24);
   - на краю не прыгает в пропасть: ждёт sync (лифт/платформа игрока). */
void hero_attack_effects(int hi) { g_attacker = hi; hero_attack_effects_impl(hi); g_attacker = -1; }

static void hero_ai_core(int hi, Intent *in) {
    Actor *h = &G.hero[hi];
    memset(in, 0, sizeof *in);
    if (h->hp <= 0 || h->hurt_t > 0 || !h->active) return;
    if (h->ai_t > 0) h->ai_t--;
    int hx = FX_INT(h->x), hy = FX_INT(h->y);
    Actor *pl = player();
    if (!pl || pl->hp <= 0) return;
    /* sprinter_anchor: бой на месте, не следует за игроком, не зовёт лифт */
    if (G.sprinter[hi]) {
        /* только локальный бой; иначе стоим */
        Actor *t = NULL; int best = 1 << 30;
        for (int i = 0; i < EN_MAX; i++) {
            Actor *e = &G.en[i];
            if (!e->active || e->hp <= 0) continue;
            int dx = ABSI(FX_INT(e->x) - hx), dy = ABSI(FX_INT(e->y) - hy);
            if (dx > 160 || dy > 36 || dx >= best) continue;
            if (blocked_between(hx, FX_INT(e->x), hy - 8)) continue;
            t = e; best = dx;
        }
        if (t) {
            int tx = FX_INT(t->x), adx = ABSI(tx - hx), dir = sgn(tx - hx);
            h->facing = dir ? dir : h->facing;
            if (h->attack_kind != AT_NONE) return;
            int reach = HERO_REACH[h->kind] + 6;
            if (adx <= reach) { if (h->ai_t == 0) { in->melee = 1; h->ai_t = 26; } return; }
            if (adx >= 28 && h->cd_ranged == 0 && (h->kind != RIG_MISTRESS || adx <= 150)) { in->ranged = 1; return; }
            if (ahead_ok(h, dir) && !ledge_ahead(h, dir)) in->dir = dir;
        }
        return;
    }
    /* solo/stealth игрока: группа принудительно OFF, герои не летят/не слайдятся вместе */
    int solo = 0;
    if (pl->kind == RIG_MISTRESS && pl->spin_t > 0) solo = 1;
    if (pl->kind == RIG_PENGI && pl->sliding) solo = 1;
    if (pl->kind == RIG_MISTRESS && pl->spin_t > 0) solo = 1;
    if (G.flip_on) solo = 1;
    if (pl->kind == RIG_REFREEZER && pl->misc > 0 && pl->flip) solo = 1;
    if (!actor_on_ground(pl, &G.lv) && pl->vy > FXF(3.5)) solo = 1;   /* только локально: прыжки/падение сцепку не снимают */
    if (G.solo_mode) solo = 1;
    int follow = G.follow && !solo && !G.domina_stealth && G.stealth_cool <= 0 && !G.solo_mode;
    if (!follow) {
        /* Stealth / Solo: стоим, не лезем в закрытые комнаты */
        h->vx = 0; h->sliding = 0; h->spin_t = 0;
        if (h->h < h->full_h) h->h = h->full_h;
        memset(in, 0, sizeof *in);
        return;
    }
    int px = FX_INT(pl->x), py = FX_INT(pl->y);
    int same_floor = ABSI(hy - py) <= 24;
    int lost = ABSI(hx - px) > 200 || ABSI(hy - py) > 90;
    G.help_flag[hi] = (lost || h->ai_blk > 40) ? 1 : 0;
    /* solo: стоим / бой на месте, без преследования */
    if (solo) {
        h->spin_t = 0; h->sliding = 0; if (h->h < h->full_h) h->h = h->full_h;
        in->dir = 0;
        /* можно бить врагов рядом, но не прыгать/не слайдиться за игроком */
        return;
    }
    /* пока игрок падает / в воздухе вниз — герои стоят, не идут */
    if (!actor_on_ground(pl, &G.lv) && pl->vy > FXF(0.5)) {
        in->dir = 0;
        return;
    }

    /* упёрлись в стену телом? — направление от стены (0 = нет) */
    int wall_push = 0;
    {
        int wl = level_solid_rect_team(&G.lv, hx - h->w / 2 - 2, hy - h->h + 4, hx - h->w / 2 - 1, hy - 4, h->team);
        int wr = level_solid_rect_team(&G.lv, hx + h->w / 2 + 1, hy - h->h + 4, hx + h->w / 2 + 2, hy - 4, h->team);
        if (wl && !wr) wall_push = 1;       /* стена слева → уходим вправо */
        else if (wr && !wl) wall_push = -1; /* стена справа → уходим влево */
        else if (wl && wr) wall_push = (hx < VIEW_W / 2 + G.cam_x) ? 1 : -1;
    }

    /* ---- оборона от дальних снарядов (ракеты / гантели), враг может быть за краем экрана ---- */
    {
        int n_rock = 0, n_dumb = 0, near_rx = 0, near_dx = 0, near_ry = hy, near_dy = hy;
        for (int i = 0; i < PJ_MAX; i++) {
            Proj *p = &G.pj[i];
            if (!p->active || p->team != 1) continue;
            int px_ = FX_INT(p->x), py_ = FX_INT(p->y);
            int toward = (p->vx > 0 && px_ < FX_INT(pl->x)) || (p->vx < 0 && px_ > FX_INT(pl->x));
            int adx = ABSI(px_ - FX_INT(pl->x)), ady = ABSI(py_ - FX_INT(pl->y));
            if (!toward || ady > 40 || adx > 280) continue;
            if (p->type == PJ_ROCKET || p->type == PJ_MISSILE || p->type == PJ_HOMING) {
                n_rock++; if (adx < ABSI(near_rx - FX_INT(pl->x)) || !near_rx) { near_rx = px_; near_ry = py_; }
            } else if (p->type == PJ_DUMBBELL) {
                n_dumb++; if (adx < ABSI(near_dx - FX_INT(pl->x)) || !near_dx) { near_dx = px_; near_dy = py_; }
            }
        }
        if (n_rock > 0 || n_dumb > 0) {
            /* Nerd сбивает 1 ракету гитарой */
            if (n_rock >= 1 && h->kind == RIG_NERD && h->attack_kind == AT_NONE) {
                int sd = near_rx >= hx ? 1 : -1;
                h->facing = sd;
                if (h->cd_ranged == 0) { in->ranged = 1; return; } /* бросок гитары */
                if (ahead_ok(h, sd) && !ledge_ahead(h, sd)) in->dir = sd; /* к краю */
                else in->dir = 0;
                return;
            }
            /* 2+ ракеты / 2+ гантели — Domina юла в сторону снарядов */
            if ((n_rock >= 2 || n_dumb >= 2) && h->kind == RIG_MISTRESS && h->attack_kind == AT_NONE) {
                int tgt = n_rock >= 2 ? near_rx : near_dx;
                int sd = tgt >= hx ? 1 : -1;
                h->facing = sd;
                if (h->spin_cd == 0) { in->super = 1; return; } /* spin (C) */
                if (h->spin_t > 0) { in->dir = sd; return; }
                if (ahead_ok(h, sd) && !ledge_ahead(h, sd)) in->dir = sd;
                return;
            }
            /* Refreezer ловит гантели дверцей */
            if (n_dumb >= 1 && h->kind == RIG_REFREEZER && h->attack_kind == AT_NONE) {
                int sd = near_dx >= hx ? 1 : -1;
                h->facing = sd;
                G.act_t[hi] = 30; /* открыть дверцу */
                h->activating = 1;
                if (ahead_ok(h, sd) && !ledge_ahead(h, sd) && ABSI(near_dx - hx) > 20) in->dir = sd;
                return;
            }
        } else if (!G.follow && !pl->sliding && !(pl->kind == RIG_MISTRESS && pl->spin_t > 0)
                   && !(pl->kind == RIG_REFREEZER && pl->misc > 0 && pl->flip)
                   && !G.domina_stealth && actor_on_ground(pl, &G.lv)) {
            /* снарядов нет, stealth нет — авто Group ON если на этаже есть потеряшки */
            int any = 0, pr_ = level_room(FX_INT(pl->x), py);
            for (int k = 0; pr_ >= 0 && pr_ < 12 && G.explored[pr_] && k < NUM_HEROES; k++)
                if (k != G.cur && G.hero[k].active && G.hero[k].hp > 0 && ABSI(FX_INT(G.hero[k].y) - py) <= 24 && !wall_between(FX_INT(G.hero[k].x), FX_INT(pl->x), py) && same_platform(FX_INT(G.hero[k].x), FX_INT(pl->x), py)) any = 1;
            if (any && enemies_alive() == 0) {
                int pr = level_room(FX_INT(pl->x), FX_INT(pl->y));
                if (pr < 0 || (pr < 12 && G.explored[pr])) G.follow = 1;
            }
        }
    }

    /* Q на лифте: 1 сек — все бегут на лифт, бой игнорируется */
    if (G.lift_call_t > 0 && follow) {
        int li = h->misc;
        if (li < 0 || li >= G.lv.nobj || G.lv.obj[li].type != O_LIFT) {
            if (pl->stand_obj >= 0 && G.lv.obj[pl->stand_obj].type == O_LIFT) li = pl->stand_obj;
        }
        if (li >= 0 && li < G.lv.nobj && G.lv.obj[li].type == O_LIFT) {
            h->misc = li;
            if (nav_lift(h, in, G.lv.obj[li].x + G.lv.obj[li].w / 2, G.lv.obj[li].y, 900)) {
                h->ai_blk = 0;
                return;
            }
            /* телепорт на лифт если далеко и не успевают */
            if (G.lift_call_t <= 15 && h->stand_obj != li) {
                int cx = G.lv.obj[li].x + G.lv.obj[li].w / 2 + (hi - G.cur) * 4;
                int cy = G.lv.obj[li].y;
                actor_teleport(h, cx, cy);
                h->vx = h->vy = 0; h->post_x = cx; h->post_y = cy; h->ai_blk = 0;
            }
            return;
        }
    }

    /* нужен ли приоритет «дойти до игрока» над боем? (лифт/провал к игроку) */
    int need_reach_player = 0;
    if (follow && same_floor) {
        if (h->stand_obj >= 0 && h->stand_obj < G.lv.nobj && G.lv.obj[h->stand_obj].type == O_LIFT
            && pl->stand_obj != h->stand_obj)
            need_reach_player = 1;
        else if (ABSI(px - hx) > 40) {
            int sd0 = sgn(px - hx);
            if (sd0 && ledge_ahead(h, sd0)) need_reach_player = 1;
        }
    }

    /* 1) враг на этаже — атака, НО не блокируем сход с лифта / прыжок к игроку */
    Actor *t = NULL; int best = 1 << 30;
    for (int i = 0; i < EN_MAX; i++) {
        Actor *e = &G.en[i];
        if (!e->active || e->hp <= 0) continue;
        int dx = ABSI(FX_INT(e->x) - hx), dy = ABSI(FX_INT(e->y) - hy);
        if (dx > 220 || dy > 36 || dx >= best) continue;
        if (blocked_between(hx, FX_INT(e->x), hy - 8)) continue;
        t = e; best = dx;
    }
    if (t && !need_reach_player) {
        h->ai_blk = 0;
        int tx = FX_INT(t->x), adx = ABSI(tx - hx), dir = sgn(tx - hx);
        if (h->attack_kind == AT_NONE && !h->sliding) h->facing = dir ? dir : h->facing;
        if (h->attack_kind != AT_NONE) return;
        int reach = HERO_REACH[h->kind] + 6;
        int want_super = h->ai_state % 3 == 2 && (h->kind == RIG_MISTRESS ? h->cd_special == 0 : h->cd_super_set > 0);
        int gap_to_enemy = ledge_ahead(h, dir);
        if (want_super && h->cd_super == 0 && adx <= super_range(h) && (h->kind != RIG_PENGI || adx >= 10)) {
            if (h->kind == RIG_MISTRESS) { if (h->cd_special == 0) in->special = 1; }          /* Domina: бензопила на V */
            else if (h->kind == RIG_NERD && h->cd_special == 0 && h->ai_state % 2) in->special = 1;
            else in->super = 1;
            h->ai_state++; h->ai_t = 20; return;
        }
        if (h->kind == RIG_MISTRESS) {
            if (h->spin_t > 0) { if (adx > 4 && !gap_to_enemy) in->dir = dir; return; }
            if (h->spin_cd == 0 && adx <= 130 && adx > reach && h->ai_state % 3 != 2) { in->super = 1; return; }
        }
        if (adx <= reach && !gap_to_enemy) {
            if (h->ai_t == 0) { in->melee = 1; h->ai_t = 26; if (!want_super) h->ai_state++; }
            return;
        }
        if (adx >= 28 && (h->kind != RIG_MISTRESS || adx <= 150)
            && !blocked_between(hx, tx, hy - 8)) {
            if (h->cd_ranged == 0) { in->ranged = 1; if (!want_super) h->ai_state++; return; }
        }
        if (wall_push && dir == -wall_push) {
            if (ahead_ok(h, wall_push) && !ledge_ahead(h, wall_push)) in->dir = wall_push;
            return;
        }
        if (!gap_to_enemy && ahead_ok(h, dir)) in->dir = dir;
        return;
    }

    /* 2) без врагов: группа выкл — стоим, но отходим от стены (не дёргаться в коллизии) */
    if (!follow) {
        h->ai_blk = 0;
        if (wall_push && ahead_ok(h, wall_push) && !ledge_ahead(h, wall_push)) {
            in->dir = wall_push;
            h->facing = wall_push;
        }
        return;
    }

    /* 3) группа вкл: следовать за игроком */
    /* едем на лифте - стоим смирно (без прыжков), после остановки выбегаем */
    if (h->stand_obj >= 0 && h->stand_obj < G.lv.nobj && G.lv.obj[h->stand_obj].type == O_LIFT
        && (G.lv.obj[h->stand_obj].state == LS_UP || G.lv.obj[h->stand_obj].state == LS_DOWN)) {
        h->ai_blk = 0; in->dir = 0; return;
    }
    /* уже на лифте/платформе игрока — держимся у него */
    if (pl->stand_obj >= 0 && h->stand_obj == pl->stand_obj) {
        int d = px - hx;
        if (ABSI(d) > 6 && ahead_ok(h, sgn(d))) in->dir = sgn(d);
        h->ai_blk = 0;
        return;
    }
    /* герой на остановленном лифте, игрок на том же этаже — ПРИНУДИТЕЛЬНО сходим (прыжок если край) */
    if (h->stand_obj >= 0 && h->stand_obj < G.lv.nobj && G.lv.obj[h->stand_obj].type == O_LIFT
        && same_floor && (pl->stand_obj != h->stand_obj)) {
        const Obj *lo = &G.lv.obj[h->stand_obj];
        if (lo->state == LS_TOP || lo->state == LS_BOTTOM || ABSI(lo->y - py) <= 10) {
            int d = sgn(px - hx); if (!d) d = h->facing ? h->facing : 1;
            h->facing = d; h->ai_blk = 0;
            if (ledge_ahead(h, d)) {
                int gw = 120, gh = 140;
                gap_measure(h, d, &gw, &gh);
                if (gh <= 12 || gw <= GAP_JUMP_W) {
                    in->dir = d; in->jump_pressed = 1; /* перепрыгнуть стык лифт→платформа */
                } else {
                    in->dir = d; /* всё равно идём — лучше упасть на стык, чем дёргаться */
                }
            } else {
                in->dir = d;
            }
            return;
        }
    }

    /* другой этаж: идём к лифту игрока / целевому лифту */
    if (!same_floor) {
        if (pl->stand_obj >= 0 && G.lv.obj[pl->stand_obj].type == O_LIFT)
            h->misc = pl->stand_obj;
        if (h->misc >= 0 && h->misc < G.lv.nobj && G.lv.obj[h->misc].type == O_LIFT) {
            if (nav_lift(h, in, G.lv.obj[h->misc].x + G.lv.obj[h->misc].w / 2,
                         G.lv.obj[h->misc].y, 900)) return;
        }
        if (nav_lift(h, in, px, py, 900)) return;
        if (h->kind == RIG_PENGI) {
            for (int i = 0; i < G.lv.nobj; i++) {
                Obj *o = &G.lv.obj[i];
                if (o->type != O_ROPE) continue;
                if (ABSI(o->x - hx) < 28) { in->up = (py < hy); in->down = (py > hy); return; }
                if (ahead_ok(h, sgn(o->x - hx))) { in->dir = sgn(o->x - hx); return; }
            }
        }
        h->ai_blk++;
        if (h->ai_blk > 50) G.help_flag[hi] = 1;
        return;
    }

    /* игрок в воздухе / падает — союзники ждут на месте */
    if (!actor_on_ground(pl, &G.lv) || pl->fell || pl->hp <= 0) {
        in->dir = 0; h->ai_blk = 0; return;
    }
    /* один этаж / рядом: идём к игроку */
    int rank = 0;
    for (int k = 0; k < hi; k++) if (k != G.cur && G.hero[k].hp > 0 && G.hero[k].active) rank++;
    int form_gap = 10 + 8 * rank; /* спина к спине */
    int d = px - hx;
    if (ABSI(d) <= form_gap) { h->ai_blk = 0; return; }
    int sd = sgn(d);

    /* лифт игрока (едет) — садимся */
    if (pl->stand_obj >= 0 && G.lv.obj[pl->stand_obj].type == O_LIFT) {
        const Obj *lo = &G.lv.obj[pl->stand_obj];
        if (lo->state == LS_UP || lo->state == LS_DOWN) {
            h->misc = pl->stand_obj;
            if (nav_lift(h, in, px, py, 400)) return;
        }
    }

    /* ---- разбег: широкая щель, а скорости нет - отходим от края, чтобы набрать ход (иначе недолёт в пропасть) ---- */
    if (h->runup && actor_on_ground(h, &G.lv)) {
        int rd = h->runup, ed = 100;
        for (int d = 0; d < 100; d += 2) if (!solid_near(h, hx + rd * (h->w / 2 + 4 + d), hy)) { ed = d; break; }
        if (ed >= 50 || ledge_ahead(h, -rd) || !ahead_ok(h, -rd) || sgn(px - hx) != rd) h->runup = 0;
        else { in->dir = -rd; h->facing = -rd; h->ai_blk = 0; return; }
    }
    /* ---- обрыв в сторону игрока: прыжок ---- */
    if (ledge_ahead(h, sd) && actor_on_ground(h, &G.lv)) {
        int gw = 120, gh = 140;
        gap_measure(h, sd, &gw, &gh);
        /* только сплошные двери/стены — платформы не блок */
        int wall = 0;
        for (int i = 0; i < G.lv.nobj; i++) {
            const Obj *o = &G.lv.obj[i];
            if (!(o->type == O_DOOR || (o->type >= O_WALL_ICE && o->type <= O_WALL_FIRE)) || o->on || !o->solid) continue;
            int cx = o->x + o->w / 2;
            int x0 = hx < px ? hx : px, x1 = hx < px ? px : hx;
            if (cx > x0 && cx < x1 && hy - 8 >= o->y && hy - 8 < o->y + o->h) wall = 1;
        }
        if (wall) { in->dir = 0; h->facing = sd; h->ai_blk = 0; return; }

        /* если measure не нашёл опору, но игрок на той же высоте рядом — прыгаем к нему */
        if (gw >= 120 && ABSI(hy - py) <= 16 && ABSI(px - hx) <= GAP_JUMP_W + 20) {
            gw = ABSI(px - hx); gh = 0;
        }

        if (gw > 20 && gw <= GAP_JUMP_W && gh <= h->h + 8 && ABSI(h->vx) < FXF(2.1)
            && ahead_ok(h, -sd) && !ledge_ahead(h, -sd)) {
            h->runup = sd; in->dir = -sd; h->facing = -sd; h->ai_blk = 0; return;
        }
        if (gw > 8 && gw <= GAP_JUMP_W && gh <= 16
            && level_solid_rect(&G.lv, hx + sd * (gw + 8) - 4, hy, hx + sd * (gw + 8) + 4, hy + 1)) {
            in->dir = sd; in->jump_pressed = 1; in->jump_held = 1; ai_gap_jump(h, sd);
            h->ai_blk = 0; h->post_x = hx + sd * (gw + 16); h->post_y = hy;
            return;
        }
        if (gw > 0 && gw <= GAP_JUMP_W && gh <= h->h + 12) {
            in->dir = sd; in->jump_pressed = 1; in->jump_held = 1; ai_gap_jump(h, sd);
            h->ai_blk = 0; return;
        }
        /* слишком широко/глубоко — ждём на краю (не прыгаем в пропасть повторно) */
        in->dir = 0; h->facing = sd; h->vx = 0; h->ai_blk = 0;
        return;
    }

    /* нет обрыва — бежим к игроку */
    if (ahead_ok(h, sd)) {
        in->dir = sd; h->ai_blk = 0;
    } else {
        /* впереди стена, но игрок там — не разворачиваться туда-сюда */
        h->facing = sd;
        h->ai_blk++;
        if (h->ai_blk > 45) G.help_flag[hi] = 1;
    }
}

void hero_ai(int hi, Intent *in) {
    hero_ai_core(hi, in);
    {   /* не заходить в закрытую комнату: стоп у границы (если игрок не там) */
        Actor *h = &G.hero[hi];
        if (in->dir && h->hp > 0 && actor_on_ground(h, &G.lv)) {
            int hx = FX_INT(h->x), hy = FX_INT(h->y);
            int r0 = level_room(hx, hy - 4), r1 = level_room(hx + in->dir * (h->w / 2 + 6), hy - 4);
            if (r1 != r0 && r1 >= 0 && r1 < 12 && !G.explored[r1]) { in->dir = 0; h->vx = 0; }
        }
    }
    {   /* защита от цикла: много прыжков на одном месте — просим вертолёт/лифт */
        static int lx[NUM_HEROES], ln[NUM_HEROES], lt[NUM_HEROES];
        Actor *h = &G.hero[hi];
        if (hi == G.cur) { ln[hi] = 0; }
        else {
            if (ABSI(FX_INT(h->x) - lx[hi]) > 14) { lx[hi] = FX_INT(h->x); ln[hi] = 0; lt[hi] = G.frame; }
            if (in->jump_pressed && G.frame - lt[hi] < 900) { if (++ln[hi] >= 6) { ln[hi] = 0; G.rescue_req[hi] = 1; lt[hi] = G.frame; } }
            if (G.frame - lt[hi] >= 900) { ln[hi] = 0; lt[hi] = G.frame; }
        }
    }
    if (in->dir && !in->jump_pressed && G.hero[hi].attack_kind == AT_NONE) lift_hop(&G.hero[hi], in);
    {   /* прыжок ИИ: короткое удержание, без «полёта в космос» */
        Actor *h = &G.hero[hi]; const Actor *pl = player();
        if (h->ai_jump_t > 0 && !in->jump_pressed && actor_on_ground(h, &G.lv)) h->ai_jump_t = 0;
        if (in->jump_pressed) in->jump_held = 1;
        if (pl && h->hp > 0 && !h->on_rope && h->attack_kind == AT_NONE && !actor_on_ground(h, &G.lv) && !h->sliding && h->stand_obj < 0) {
            /* держим кнопку, пока летим вверх (иначе срабатывает «обрезка» прыжка и герои не перепрыгивают пропасти); после вершины отпускаем */
            in->jump_held = h->vy < 0 ? 1 : 0;
            int d = FX_INT(pl->x) - FX_INT(h->x);
            if (h->ai_jump_t > 0) { h->ai_jump_t--; in->dir = h->ai_jump_dir; if (h->vx * h->ai_jump_dir < FXF(1.6)) h->vx = h->ai_jump_dir * FXF(1.6); }   /* прыжок через щель: держим курс */
            else if (in->dir == 0 && ABSI(d) > 6 && ABSI(d) < 120 && G.follow) in->dir = sgn(d);
            /* не разгоняемся над пропастью (только если это не намеренный прыжок через щель) */
            if (h->vy > 0 && h->ai_jump_t <= 0) {
                int vs = h->vx > 0 ? 1 : h->vx < 0 ? -1 : 0;
                if (vs && ledge_ahead(h, vs)) { in->dir = 0; h->vx = h->vx / 2; }
            }
        }
    }
    {   /* страховка: на краю обрыва без прыжка - жёсткий стоп (нет скольжения в пропасть) */
        Actor *h = &G.hero[hi];
        int d = in->dir ? in->dir : (h->vx > 0 ? 1 : h->vx < 0 ? -1 : 0);
        if (d && !in->jump_pressed && !h->on_rope && !h->sliding && h->hp > 0 && actor_on_ground(h, &G.lv)
            && !(h->kind == RIG_REFREEZER && h->misc > 0 && h->flip) && !(h->kind == RIG_MISTRESS && h->spin_t > 0)
            && ledge_ahead(h, d)) { in->dir = 0; h->vx = 0; }
    }
}

/* ===================================================================== враги */
static void ram_die(Actor *e) {                                      /* врезался не в героя: разбился */
    spark(FX_INT(e->x), FX_INT(e->y) - e->h / 2, 22, 0xFFFFA030u, 3);
    sound_play_sfx_at(SFX_EXPLODE, FX_INT(e->x), FX_INT(e->y));
    e->invuln_t = 0; actor_damage(e, 99, 0, 0, 0, 0);
    G.shake = 5;
}

/* ближайшая колонка без перекрытий между двумя высотами (шахта): куда лететь Monoman, чтобы сменить этаж */
static int find_shaft(int ex, int y_from, int y_to, int h) {
    int up = y_from < y_to ? y_from : y_to, low = y_from < y_to ? y_to : y_from;
    for (int d = 0; d <= 380; d += 8)
        for (int sgn_ = -1; sgn_ <= 1; sgn_ += 2) {
            int x = ex + sgn_ * d;
            if (x < 24 || x > WORLD_W - 24) continue;
            if (!level_solid_rect(&G.lv, x - 8, up - h, x + 8, low - 1)) return x;
        }
    return -1;
}

static int in_boss_area(int x, int y);
static void monoman_think(Actor *e, Intent *in, Actor *t) {
    int ex = FX_INT(e->x), ey = FX_INT(e->y);
    fx tvx = 0, tvy = 0;
    if (e->attack_kind != AT_NONE) { e->vx = e->vx * 7 / 8; e->vy = e->vy * 7 / 8; return; }
    if (!t) {                                                         /* никого рядом: дрейф к ближайшему герою */
        Actor *g = nearest_hero(e, 700, 400, 1);
        if (!g) { e->vx = e->vx * 7 / 8; e->vy = e->vy * 7 / 8; return; }
        t = g;
    }
    int tx = FX_INT(t->x), ty = FX_INT(t->y);
    int adx = ABSI(tx - ex), side = ex >= tx ? 1 : -1;
    int goal_y = ty - 16, goal_x = adx > 150 ? tx + side * 120 : adx < 70 ? tx + side * 110 : ex;
    if (ABSI(goal_y - ey) > 36) {                                     /* на другом этаже: сначала к шахте */
        int sx = find_shaft(ex, ey, goal_y, e->h);
        if (sx >= 0 && ABSI(sx - ex) > 5) { goal_x = sx; goal_y = ey; }
    }
    tvx = (fx)((goal_x - ex) * 14); tvy = (fx)((goal_y - ey) * 14);
    fx vmax = FXF(1.5);
    if (tvx > vmax) tvx = vmax; if (tvx < -vmax) tvx = -vmax;
    if (tvy > FXF(1.2)) tvy = FXF(1.2); if (tvy < -FXF(1.2)) tvy = -FXF(1.2);
    e->vx += (tvx - e->vx) / 8; e->vy += (tvy - e->vy) / 8;
    if (ex < 24 && e->vx < 0) e->vx = 0;
    if (ex > WORLD_W - 24 && e->vx > 0) e->vx = 0;
    e->facing = tx < ex ? -1 : 1;
    int level_ok = ABSI(ey - (ty - 16)) <= 16;
    if (level_ok && adx >= 50 && adx <= 190 && e->cd_ranged == 0) in->ranged = 1;
    else if (level_ok && adx <= 120 && e->cd_super == 0 && !blocked_between(ex, tx, ey - 10)) in->super = 1;
}

/* собственный телепорт врага на пол рядом с героем h (110+ px сбоку, на твёрдой земле). 1 = успех */
static int enemy_teleport_near(Actor *e, const Actor *h) {
    int ex = FX_INT(e->x), ey = FX_INT(e->y), hx = FX_INT(h->x), hy = FX_INT(h->y);
    for (int tr = 0; tr < 12; tr++) {
        int side = (tr & 1) ? 1 : -1, x = hx + side * (110 + 30 * (tr / 2));
        if (x < 32 || x > WORLD_W - 32 || in_boss_area(x, hy)) continue;
        if (!level_solid_rect(&G.lv, x - 6, hy, x + 6, hy + 1)) continue;
        if (level_solid_rect(&G.lv, x - e->w / 2, hy - e->h, x + e->w / 2, hy - 6)) continue;
        { int dr = level_room(x, hy), hr = level_room(hx, hy);                      /* только в открытую комнату или в комнату цели: не на закрытый этаж */
          if (dr != hr && (dr < 0 || dr >= 12 || !G.explored[dr])) continue; }
        spark(ex, ey - e->h / 2, 10, 0xFFA0E0FFu, 2);
        e->x = INT_FX(x); e->y = INT_FX(hy); e->vx = e->vy = 0; e->nav_mode = 0; e->stuck_x = x; e->stuck_t = 0; e->unr_t = 0;
        spark(x, hy - e->h / 2, 10, 0xFFA0E0FFu, 2);
        game_log("Enemy teleported to a hero floor (x=%d)", x);
        return 1;
    }
    return 0;
}

/* сценарий 3: игрок закончил stealth-манёвр (Pengi слайд, Domina над стеной, Submarine в воде) - враги за преградой телепортируются к нему */
void enemies_gather_to_player(void) {
    Actor *p = player();
    if (!p || p->hp <= 0 || p->kind == RIG_REFREEZER) return;           /* у Refreezer обратная гравитация сводит врагов с ума - не работает */
    int px = FX_INT(p->x), py = FX_INT(p->y);
    for (int k = 0; k < EN_MAX; k++) {
        Actor *e = &G.en[k];
        if (!e->active || e->hp <= 0 || e->kind == RIG_MONOMAN || e->attack_kind != AT_NONE) continue;
        int ex = FX_INT(e->x), ey = FX_INT(e->y);
        if (ABSI(ex - px) > 700 || ABSI(ey - py) > 200) continue;
        int sep = ABSI(ey - py) > 40 || wall_between(ex, px, ey - 8) || !same_platform(ex, px, py);
        if (!sep) continue;
        enemy_teleport_near(e, p);
    }
}

static void enemy_think(Actor *e, Intent *in) {
    memset(in, 0, sizeof *in);
    int ex = FX_INT(e->x), ey = FX_INT(e->y);
    int grounded = actor_on_ground(e, &G.lv);
    if (e->ai_t > 0) e->ai_t--;
    if (e->on_rope && (e->kind == RIG_SCOUT || e->kind == RIG_BODY)) {            /* едет по канату: дискретный спуск/подъём */
        e->ai_state = 3; e->hurt_t = 0; e->stun_t = 0; e->soft = 45; e->nav_mode = 0; e->nav_t = 0;
        if (e->aim > 0) in->up = 1; else in->down = 1;
        return;
    }
    if (e->ai_state == 3) {                                             /* доехал: канат исчезает */
        e->ai_state = 0;
        if (e->misc >= 0 && e->misc < G.lv.nobj && G.lv.obj[e->misc].type == O_ROPE) {
            int busy = 0;
            for (int k = 0; k < NUM_HEROES; k++) if (G.hero[k].on_rope && G.hero[k].rope_obj == e->misc) busy = 1;
            for (int k = 0; k < EN_MAX; k++) if (&G.en[k] != e && G.en[k].active && G.en[k].on_rope && G.en[k].rope_obj == e->misc) busy = 1;
            if (!busy) G.lv.obj[e->misc].visible = 0;
        }
        e->misc = 0;
    }
    if (e->hurt_t > 0 || e->stun_t > 0) { e->charging = 0; if (e->ai_state == 2) e->ai_state = 0; return; }
    Actor *t = enemy_target(e, 500, 160, 1);                          /* игрок в приоритете, часть врагов переключается на других героев */
    if (t && ABSI(FX_INT(t->y) - ey) <= 56 && blocked_between(ex, FX_INT(t->x), ey - 8)) {
        Actor *pl = player();                                         /* за стеной на этаже - пробуем игрока, иначе никого */
        t = (pl && pl != t && pl->hp > 0 && ABSI(FX_INT(pl->y) - ey) <= 160 && ABSI(FX_INT(pl->x) - ex) <= 500 && !(ABSI(FX_INT(pl->y) - ey) <= 56 && blocked_between(ex, FX_INT(pl->x), ey - 8))) ? pl : NULL;
    }

    /* антигравитация Refreezer: враги хаотично крутятся, не атакуют */
    if (G.flip_on) {
        e->facing = ((G.frame / 8 + e->kind) & 1) ? 1 : -1;
        in->dir = ((G.frame / 12 + (int)(e - G.en)) % 3) - 1;
        e->attack_kind = AT_NONE;
        e->charging = 0;
        return;
    }
    if (e->kind == RIG_MONOMAN) { monoman_think(e, in, t); return; }
    if (e->ai_state == 2) {                                         /* таран: разгон */
        in->dir = e->facing;
        e->charging = 1;
        int blocked = e->ai_t < 70 && e->vx == 0;
        int suicide = e->kind == RIG_HEAVY && e->misc == 1;         /* Ramrod готов сорваться в пропасть вместе с героем */
        int stop = e->ai_t == 0 || blocked || (suicide ? 0 : ledge_ahead(e, e->facing)) || (suicide && level_solid_rect_team(&G.lv, FX_INT(e->x) + e->facing * (e->w / 2 + 4), FX_INT(e->y) - e->h, FX_INT(e->x) + e->facing * (e->w / 2 + 4), FX_INT(e->y) - 6, 1));
        if (stop) {
            e->charging = 0; e->ai_state = 0; e->vx /= 2;
            if (e->kind == RIG_DELIVER) { ram_die(e); }
            else if (e->kind == RIG_FIREWALKER && ++e->misc2 >= 2) { ram_die(e); }
            else e->stun_t = 40;
        }
        return;
    }
    if (!t) {                                                        /* цели не видно: идём к позиции игрока */
        e->ai_state = 0;
        Actor *g = player();
        if (g->hp <= 0) { g = NULL; for (int i = 0; i < NUM_HEROES; i++) if (G.hero[i].active && G.hero[i].hp > 0) { g = &G.hero[i]; break; } }
        if (!g) return;
        int gx = FX_INT(g->x), gy = FX_INT(g->y), dyf = gy - ey;
        /* застрял и никого не видит 2.5 с: собственный телепорт на этаж случайного героя */
        if (e->kind != RIG_MONOMAN && e->attack_kind == AT_NONE && grounded && !(e->stand_obj >= 0 && G.lv.obj[e->stand_obj].type == O_LIFT)) {
            if (ABSI(ex - e->stuck_x) > 6) { e->stuck_x = ex; e->stuck_t = 0; }
            else if (++e->stuck_t > 150) {
                e->stuck_t = 0;
                Actor *cand[NUM_HEROES]; int nc = 0;
                for (int i = 0; i < NUM_HEROES; i++) if (G.hero[i].active && G.hero[i].hp > 0) cand[nc++] = &G.hero[i];
                if (nc) {
                    enemy_teleport_near(e, cand[g_rand() % (uint32_t)nc]);
                }
                return;
            }
        } else { e->stuck_t = 0; e->stuck_x = ex; }
        if (ABSI(dyf) > 40 && grounded) {                            /* игрок на другом этаже: лифт, иначе канат */
            if (e->kind != RIG_MONOMAN && nav_lift(e, in, gx, gy, 700)) return;
            if ((e->kind == RIG_SCOUT || e->kind == RIG_BODY || e->kind == RIG_HEAVY) && rope_nav(e, in, dyf < 0, dyf > 0, gy)) return;
        }
        if (e->nav_mode == 2) {                                      /* у края: ищем канат на другой этаж */
            int up = ey >= FLOOR1_Y - 20;
            if (grounded && (e->kind == RIG_SCOUT || e->kind == RIG_BODY || e->kind == RIG_HEAVY) && rope_nav(e, in, up, !up, -1)) { e->nav_t = 0; return; }
            if (grounded && e->kind != RIG_SCOUT && e->kind != RIG_BODY && e->kind != RIG_HEAVY && e->kind != RIG_MONOMAN && nav_lift(e, in, gx, up ? ey - 128 : ey + 128, 700)) return;
            if (++e->nav_t > 120) { e->nav_mode = 0; e->nav_t = 0; }
            return;
        }
        if (e->nav_mode == 1) {                                      /* упёрлись: идём в обратную сторону до края */
            if (ahead_ok(e, e->nav_dir)) { in->dir = e->nav_dir; e->facing = e->nav_dir; }
            else { e->nav_mode = 2; e->nav_t = 0; }
            return;
        }
        int d = sgn(gx - ex);
        if (!d) { e->nav_t = 0; return; }
        if (ahead_ok(e, d)) { in->dir = d; e->facing = d; e->nav_t = 0; }
        else if (++e->nav_t > 20) { e->nav_mode = 1; e->nav_dir = -d; e->nav_t = 0; }
        return;
    }
    e->nav_mode = 0; e->nav_t = 0;
    e->ai_state = 1;
    int tx = FX_INT(t->x), adx = ABSI(tx - ex), dir = sgn(tx - ex), dy = ABSI(FX_INT(t->y) - ey);
    if (e->attack_kind == AT_NONE && dir) e->facing = dir;
    /* супер врагов отключён — слишком сильны против героев */
    if (e->attack_kind != AT_NONE) { e->unr_t = 0; if (e->kind == RIG_HEAVY && e->attack_kind == AT_SUPER) in->dir = dir; return; }
    /* цель видна, но дойти нельзя (пропасть/стена без лифта и каната, другой этаж): через ~2.5 с телепорт на платформу героев */
    if (e->kind != RIG_MONOMAN && grounded && !(e->stand_obj >= 0 && G.lv.obj[e->stand_obj].type == O_LIFT)) {
        int unreach = (adx > 36 && !ahead_ok(e, dir)) || dy > 56;
        if (unreach && ++e->unr_t > 150 && enemy_teleport_near(e, t)) return;
        if (!unreach) e->unr_t = 0;
    } else e->unr_t = 0;

    /* ---- смена этажа: лифт или канат (канат раскрывается на время проезда) ---- */
    if (dy > 40 && grounded && e->kind != RIG_MONOMAN) {
        int ty = FX_INT(t->y);
        int want_up = ty < ey - 20, want_down = ty > ey + 20;
        if (dy > 100 && nav_lift(e, in, tx, ty, 250)) return;
        if ((e->kind == RIG_SCOUT || e->kind == RIG_BODY) && rope_nav(e, in, want_up, want_down, ty)) return;
    }
    if (dy > 40 && (e->kind == RIG_SCOUT || e->kind == RIG_BODY) && grounded) {
        if (e->kind == RIG_SCOUT && adx < 100 && dy < 150) {
            in->jump_pressed = 1;
            if (dir) { e->facing = dir; in->dir = dir; }
            return;
        }
    }

    switch (e->kind) {
    case RIG_HEAVY: {
        int pit = pit_near(tx, FX_INT(t->y), dir);
        if (0) { actor_start_attack(e, AT_SUPER); e->spin_cd = 360; } /* super off */      /* вращение со штангой: 2 с, 2 оборота */
        else if (adx <= 38 && dy <= 20 && e->cd_super == 0) { in->melee = 1; e->cd_super = 110; }
        else if (adx >= 90 && grounded && e->cd_ranged == 0 && (ahead_ok(e, dir) || pit) && dy <= 24) {
            e->ai_state = 2; e->ai_t = 45; e->cd_ranged = 90; e->charging = 1;
            e->misc = pit && (g_rand() % 3u) ? 1 : 0;             /* с обрывом за героем: таранит и падает сам */
        } else if (adx > 34 && ahead_ok(e, dir)) in->dir = dir;
        break; }
    case RIG_DELIVER: case RIG_FIREWALKER: case RIG_IRONFEM: {
        int range_lo = e->kind == RIG_DELIVER ? 80 : e->kind == RIG_FIREWALKER ? 24 : 60;
        int range_hi = e->kind == RIG_DELIVER ? 230 : e->kind == RIG_FIREWALKER ? 66 : 120;
        int vert_ok = e->kind == RIG_FIREWALKER ? dy <= 22 : dy <= 40;
        if (0) {
            e->ai_state = 2; e->ai_t = 100; e->cd_super = 420; e->charging = 1;
        } else if (0) {
            e->ai_state = 2; e->ai_t = 100; e->cd_super = 420; e->charging = 1;
        } else if (adx >= range_lo && adx <= range_hi && vert_ok && e->cd_ranged == 0) {
            in->ranged = 1;
            if (e->kind == RIG_FIREWALKER) e->aim = 0;
        } else if (adx <= 30 && dy <= 24) { in->melee = 1; }
        else if (adx > 26 && ahead_ok(e, dir)) in->dir = dir;          /* всегда движется к игроку, не стоит в ожидании выстрела */
        break; }
    case RIG_MEGADOG: {                                              /* бежит к героям, кусает вплотную, издали - ракета в точку */
        if (adx <= 36 && dy <= 24 && e->ai_t == 0) { in->melee = 1; e->ai_t = 50; }
        else if (adx >= 100 && adx <= 340 && dy <= 130 && e->cd_ranged == 0 && g_rand() % 6u == 0) in->ranged = 1;
        else if (adx > 28 && ahead_ok(e, dir)) in->dir = dir;
        break; }
    case RIG_CAPSULO: {
        if (e->misc2 > 0) { e->misc2--; if (adx > 40 && ahead_ok(e, dir)) in->dir = dir; break; }   /* пауза 5 с: щита нет, стреляй в окно; враг идёт к игроку */
        if (adx >= 70 && adx <= 250 && dy <= 110) {
            if (dy <= 20 && (g_rand() % 100u) < 55u) in->ranged = 1;   /* плазма параллельно земле */
            else in->melee = 1;                                        /* отражающийся лазер под 45 градусов */
        } else if (adx > 40 && ahead_ok(e, dir)) in->dir = dir;
        break; }
    default: {
        int melee_r = 32, rmin = 70, rmax = e->kind == RIG_SCOUT ? 270 : 220;
        /* враг всегда идёт к цели; стреляет, когда заряд готов, и бьёт вплотную */
        if (adx <= melee_r && dy <= 24) { in->melee = 1; }
        else if (adx >= rmin && adx <= rmax && dy <= 40 && e->cd_ranged == 0) in->ranged = 1;
        else if (adx > melee_r - 8 && (ahead_ok(e, dir) || dy > 24)) in->dir = dir;
        break; }
    }
}

static void enemy_attack_effects(Actor *e) {
    int ex = FX_INT(e->x), ey = FX_INT(e->y);
    if (e->hp <= 0) return;
    if (e->kind == RIG_HEAVY && e->attack_kind == AT_SUPER) {          /* штанга: бьёт всех вокруг, пока вертится */
        if (e->attack_t % 14 == 0) e->hit_mask = 0;
        for (int i = 0; i < NUM_HEROES; i++) {
            Actor *h = &G.hero[i];
            if (!h->active || h->hp <= 0 || (e->hit_mask & (1 << i))) continue;
            if (ABSI(FX_INT(h->x) - ex) <= 44 && ABSI(FX_INT(h->y) - ey) <= 30) { e->hit_mask |= 1 << i; hurt_hero(h, enemy_hit_dmg(e->kind), FX_INT(h->x) >= ex ? 1 : -1, FXF(3.0), FXF(2.8)); }
        }
        if (e->attack_t % 20 == 0) { sound_play_sfx_at(SFX_FLAME, ex, ey); spark(ex + (e->attack_t % 40 ? 30 : -30), ey - 20, 3, 0xFFE0E8F8u, 2); }
        return;
    }
    if (e->charging) {                                                /* таран */
        for (int i = 0; i < NUM_HEROES; i++) {
            Actor *h = &G.hero[i];
            if (!h->active || h->hp <= 0 || !overlap(e, h, 1)) continue;
            if (hurt_hero(h, enemy_hit_dmg(e->kind), e->facing, FXF(4.2), FXF(3.2))) {
                e->charging = 0; e->ai_state = 0; e->stun_t = 35; e->vx = 0;
            }
        }
    }
    if (e->kind == RIG_IRONFEM && e->hp > 0) {                        /* прыжок на Ironfem сверху убивает её без урона героям */
        int top = ey - e->h;
        for (int i = 0; i < NUM_HEROES; i++) {
            Actor *h = &G.hero[i];
            if (!h->active || h->hp <= 0 || h->vy <= FXF(0.8)) continue;
            int hy = FX_INT(h->y), hx = FX_INT(h->x);
            if (hy >= top - 3 && hy <= top + 9 && ABSI(hx - ex) <= e->w / 2 + h->w / 2) {
                h->vy = -FXF(3.6); h->coyote = 0; h->fall_immune = 0;
                spark(ex, top, 14, 0xFFFFE070u, 2);
                e->invuln_t = 0; actor_damage(e, 99, hx >= ex ? -1 : 1, FXF(1.5), FXF(2.0), 10);
                if (h == player()) say("Stomped Ironfem!");
                return;
            }
        }
    }
    if (e->kind == RIG_CAPSULO) {                                       /* стрельба под щитом, затем 5 секунд паузы */
        if (e->attack_kind == AT_NONE) { if (e->aim == 1) { e->aim = 0; e->misc2 = 300; } return; }
        e->aim = 1;
        float ph_ = actor_attack_phase(e);
        if (ph_ >= 0.4f && !e->attack_fired) {
            e->attack_fired = 1;
            if (e->attack_kind == AT_RANGED) {
                fire_projx(PJ_PLASMA, 1, ex + e->facing * 22, ey - 26, (float)e->facing * 2.6f, 0.f, enemy_hit_dmg(e->kind), 90);
                sound_play_sfx_at(SFX_ZAP, ex, ey);
            } else {
                Actor *tg = nearest_hero(e, 320, 200, 1);
                int down = tg ? FX_INT(tg->y) - tg->h / 2 >= ey - 26 : 1;
                laser_fire(ex + e->facing * 22, ey - 26, e->facing, down ? 1 : -1);
            }
        }
        return;
    }
    if (e->kind == RIG_MEGADOG && e->hp > 0) {
        if (e->attack_kind == AT_NONE) return;
        if (e->attack_kind == AT_MELEE) {                                 /* укус зубами */
            if (actor_attack_phase(e) >= 0.4f && !e->attack_fired) {
                e->attack_fired = 1;
                for (int i = 0; i < NUM_HEROES; i++) {
                    Actor *h = &G.hero[i];
                    if (h->active && h->hp > 0 && melee_box_hit(e, h, 18)) { hurt_hero(h, enemy_hit_dmg(e->kind), e->facing, FXF(2.4), FXF(2.0)); sound_play_actor_at(e->kind, SFX_ACT_MELEE, ex, ey); }
                }
            }
            return;
        }
        if (actor_attack_phase(e) >= 0.4f && !e->attack_fired) {
            e->attack_fired = 1;
            Actor *tg = nearest_hero(e, 420, 220, 1);
            int tx_ = tg ? FX_INT(tg->x) : ex + e->facing * 160, ty_ = tg ? FX_INT(tg->y) - tg->h / 2 : ey - 20;
            int mx = ex + e->facing * 6, my = ey - 18;
            float dxm = (float)(tx_ - mx), dym = (float)(ty_ - my), len = sqrtf(dxm * dxm + dym * dym);
            if (len < 20.f) len = 20.f;
            Proj *pr = fire_projx(PJ_MISSILE, 1, mx, my - 6, dxm / len * 2.4f, dym / len * 2.4f, enemy_hit_dmg(e->kind), (int)(len / 2.4f) + 1);
            if (pr) pr->aux = 77;                                       /* летит в точку и рвётся там */
            sound_play_actor_at(e->kind, SFX_ACT_RANGED, ex, ey);
        }
        return;
    }
    if (e->attack_kind == AT_NONE) { if (e->kind == RIG_FIREWALKER) e->aim = 0; return; }
    float ph = actor_attack_phase(e);
    if (e->kind == RIG_FIREWALKER && e->attack_kind == AT_RANGED) {      /* огонь: цепь оранжевого цвета строго вдоль земли, длина - около ширины врага */
        int mx = ex + e->facing * 24, my = ey - 24;
        int maxl = e->w * 2; if (maxl < 40) maxl = 40; if (maxl > 56) maxl = 56;
        int len = ph < 0.55f ? (int)((float)maxl * ph / 0.55f) : (int)((float)maxl * (1.f - (ph - 0.55f) / 0.45f));
        e->misc = len;
        if ((G.frame & 7) == 0) sound_play_sfx_at(SFX_FLAME, ex, ey);
        int x0 = e->facing > 0 ? mx : mx - len, x1 = e->facing > 0 ? mx + len : mx;
        for (int i = 0; i < NUM_HEROES; i++) {
            Actor *h = &G.hero[i];
            if (!h->active || h->hp <= 0) continue;
            int hx0, hy0, hx1, hy1; actor_box(h, &hx0, &hy0, &hx1, &hy1);
            if (x1 >= hx0 && x0 <= hx1 && my + 6 >= hy0 && my - 6 <= hy1 && !blocked_between(ex, FX_INT(h->x), my))
                hurt_hero(h, enemy_hit_dmg(e->kind), e->facing, FXF(1.8), FXF(2.4));
        }
        return;
    }
    if (e->kind == RIG_IRONFEM && e->attack_kind == AT_RANGED) {         /* цепь летит вперёд до границы экрана, потом втягивается */
        int mx = ex + e->facing * 24, my = ey - 24;
        int maxl = e->facing > 0 ? G.cam_x + VIEW_W - mx : mx - G.cam_x;
        if (maxl > 110) maxl = 110;                                      /* цепь короче, чтобы не бить слишком далеко */
        if (maxl < 40) maxl = 40;
        int len = ph < 0.55f ? (int)((float)maxl * ph / 0.55f) : (int)((float)maxl * (1.f - (ph - 0.55f) / 0.45f));
        e->misc = len;
        int x0 = e->facing > 0 ? mx : mx - len, x1 = e->facing > 0 ? mx + len : mx;
        for (int i = 0; i < NUM_HEROES; i++) {
            Actor *h = &G.hero[i];
            if (!h->active || h->hp <= 0) continue;
            int hx0, hy0, hx1, hy1; actor_box(h, &hx0, &hy0, &hx1, &hy1);
            if (x1 >= hx0 && x0 <= hx1 && my + 5 >= hy0 && my - 5 <= hy1 && !blocked_between(ex, FX_INT(h->x), my))
                hurt_hero(h, enemy_hit_dmg(e->kind), e->facing, FXF(2.4), FXF(2.6));
        }
        return;
    }
    if (e->kind == RIG_MONOMAN) {
        if (e->attack_kind == AT_RANGED && ph >= 0.35f && !e->attack_fired) {         /* дробовик: веер из пяти дробин */
            e->attack_fired = 1;
            Actor *tg = nearest_hero(e, 320, 200, 1);
            float base = e->facing > 0 ? 0.f : 3.14159f;
            if (tg) base = atan2f((float)(FX_INT(tg->y) - tg->h / 2 - (ey - 22)), (float)(FX_INT(tg->x) - ex));
            sound_play_sfx_at(SFX_EXPLODE, ex, ey);
            for (int k = -2; k <= 2; k++) fire_projx(PJ_PELLET, 1, ex + e->facing * 20, ey - 22, cosf(base + (float)k * 0.10f) * 3.8f, sinf(base + (float)k * 0.10f) * 3.8f, enemy_hit_dmg(e->kind), 30);
            spark(ex + e->facing * 24, ey - 22, 6, 0xFFFFE070u, 2);
        } else if (e->attack_kind == AT_SUPER && ph >= 0.28f && ph <= 0.78f && (G.frame & 3) == 0) {   /* выхлоп: облака газа вперёд */
            fire_projx(PJ_GAS, 1, ex + e->facing * 22, ey - 12 + (int)(g_rand() % 7u) - 3, (float)e->facing * (1.2f + (float)(g_rand() % 10u) * 0.1f), ((float)(g_rand() % 10u) - 5.f) * 0.04f, enemy_hit_dmg(e->kind), 36);
        }
        return;
    }
    if (e->attack_kind == AT_MELEE) {
        if (e->kind == RIG_HEAVY) {                                   /* прыжок: ударная волна подбрасывает */
            if (ph >= 0.50f && !e->attack_fired) {
                e->attack_fired = 1;
                spark(ex, ey - 2, 14, 0xFFE8F0FFu, 3); G.shake = 8;
                for (int i = 0; i < NUM_HEROES; i++) {
                    Actor *h = &G.hero[i];
                    if (!h->active || h->hp <= 0) continue;
                    int dx = FX_INT(h->x) - ex, dy = FX_INT(h->y) - ey;
                    if (ABSI(dx) <= 50 && ABSI(dy) <= 14 && actor_on_ground(h, &G.lv)) {
                        h->on_rope = 0; h->sliding = 0; h->h = h->full_h; h->coyote = 0;
                        h->vy = -FXF(6.2); h->vx = (dx >= 0 ? 1 : -1) * FXF(1.4); h->launched = 1;
                        h->attack_kind = AT_NONE; h->invuln_t = 0;
                        animator_restart(&h->anim, A_JUMP);
                    }
                }
            }
        } else if (ph >= 0.35f && ph <= 0.70f) {
            for (int i = 0; i < NUM_HEROES; i++) {
                Actor *h = &G.hero[i];
                if (!h->active || h->hp <= 0 || (e->hit_mask & (1 << i))) continue;
                if (melee_box_hit(e, h, 22)) { e->hit_mask |= 1 << i; hurt_hero(h, enemy_hit_dmg(e->kind), e->facing, FXF(2.4), FXF(2.6)); }
            }
        }
    } else if (e->attack_kind == AT_RANGED && ph >= 0.35f && !e->attack_fired) {
        e->attack_fired = 1;
        if (e->kind == RIG_DELIVER) {                                  /* пицца по дуге в позицию героя */
            Actor *tg = nearest_hero(e, 300, 160, 1);
            float dxx = tg ? (float)ABSI(FX_INT(tg->x) - ex) : 120.f;
            float vx = dxx / 31.f; if (vx < 1.0f) vx = 1.0f; if (vx > 4.4f) vx = 4.4f;
            Proj *pz = fire_projx(PJ_PIZZA, 1, ex + e->facing * 16, ey - 26, (float)e->facing * vx, -3.4f, enemy_hit_dmg(e->kind), 150);
            if (pz) { pz->g = FXF(0.22); pz->dir = e->facing; }
        } else fire_proj(e->kind == RIG_SCOUT ? PJ_ROCKET : PJ_DUMBBELL, 1, 0, ex + e->facing * 14, ey - 28, e->facing, enemy_hit_dmg(e->kind));
    }
}

static void class_cd(int kind) {                                       /* Megadog и Capsulo: повторное появление не раньше чем через 10 секунд */
    if (kind == RIG_MEGADOG) G.cls_cd[7] = 600;
    if (kind == RIG_CAPSULO) G.cls_cd[8] = 600;
}

void enemies_update(void) {
    if (G.whip_t > 0) G.whip_t--;
    for (int c = 0; c < NUM_ENEMY_CLASSES; c++) if (G.cls_cd[c] > 0) G.cls_cd[c]--;
    for (int i = 0; i < G.lv.nobj; i++) {                                 /* канаты врагов исчезают, когда ими никто не пользуется */
        Obj *o = &G.lv.obj[i];
        if (o->type != O_ROPE || o->kind != 1 || !o->visible) continue;
        int busy = 0;
        for (int k = 0; k < NUM_HEROES; k++) if (G.hero[k].on_rope && G.hero[k].rope_obj == i) busy = 1;
        for (int k = 0; k < EN_MAX; k++) if (G.en[k].active && G.en[k].on_rope && G.en[k].rope_obj == i) busy = 1;
        if (busy) o->timer = 0; else if (++o->timer > 90) { o->visible = 0; o->timer = 0; }
    }
    Actor *pl = player();
    int ply = pl ? FX_INT(pl->y) : 0, plx = pl ? FX_INT(pl->x) : 0;
    if (pl && (G.pl_floor_y == 0 || actor_on_ground(pl, &G.lv))) {
        const Obj *lo = (pl->stand_obj >= 0 && pl->stand_obj < G.lv.nobj && G.lv.obj[pl->stand_obj].type == O_LIFT) ? &G.lv.obj[pl->stand_obj] : NULL;
        if (lo && (lo->state == LS_UP || lo->state == LS_DOWN)) G.pl_floor_y = lo->state == LS_UP ? lo->y1 : lo->y0;   /* на лифте: этаж = пункт назначения, враги не повторяют ход кабины */
        else G.pl_floor_y = ply;
    }
    for (int i = 0; i < EN_MAX; i++) {
        Actor *e = &G.en[i];
        if (!e->active) continue;
        if (e->hp > 0) {
            /* дискретные этажи: игрок выше — враг телепортируется сразу; ниже — через 4 с */
            ply = G.pl_floor_y;
            if (pl && pl->hp > 0 && ABSI(FX_INT(e->y) - ply) > 40 && e->kind != RIG_MONOMAN) {
                if (FX_INT(e->y) > ply) e->tele_wait = 0;                 /* игрок поднялся: враг сразу на этаж выше */
                else if (e->tele_wait <= 0) e->tele_wait = 240;           /* игрок спустился: ждём 4 с */
                if (e->tele_wait > 0 && --e->tele_wait > 0) goto en_skip_tele;
                int sx = -1;
                for (int t = 0; t < 6 && sx < 0; t++) {
                    int x = (t % 2 == 0) ? (G.cam_x + 28 + t * 8) : (G.cam_x + VIEW_W - 28 - t * 8);
                    if (x < 24) x = 24; if (x > WORLD_W - 24) x = WORLD_W - 24;
                    if (ABSI(x - plx) < 60) continue;
                    if (!level_solid_rect(&G.lv, x - 4, ply, x + 4, ply + 1)) continue;
                    if (level_solid_rect(&G.lv, x - 6, ply - 40, x + 6, ply - 6)) continue;
                    if (blocked_between(x, plx, ply - 8)) continue;
                    if (in_boss_area(x, ply)) continue;
                    sx = x;
                }
                if (sx < 0) {
                    sx = plx + (g_rand() & 1 ? 100 : -100);
                    if (sx < 32) sx = 32;
                    if (sx > WORLD_W - 32) sx = WORLD_W - 32;
                }
                actor_teleport(e, sx, ply);
                e->vx = e->vy = 0; e->facing = plx < sx ? -1 : 1;
                e->post_x = sx; e->post_y = ply;
                spark(sx, ply - 16, 8, 0xFFFF8060u, 2);
            }
            en_skip_tele:;
            if (!(pl && pl->hp > 0 && ABSI(FX_INT(e->y) - G.pl_floor_y) > 40)) e->tele_wait = 0;
            Intent in;
            enemy_think(e, &in);
            if (e->kind != RIG_MONOMAN && in.dir && !in.jump_pressed && e->attack_kind == AT_NONE && e->ai_state != 2) lift_hop(e, &in);
            actor_update(e, &G.lv, &in);
            enemy_attack_effects(e);
            Actor *n = nearest_hero(e, DESPAWN_DIST, 140, 0);
            if (!n) { e->active = 0; class_cd(e->kind); }
        } else {
            if (e->ai_state != 99) {
                e->ai_state = 99; G.stat_kills++;
                team_kill_from(e->misc2 ? 0 : 1);
                class_cd(e->kind);
                /* после зачистки всех — пауза респавна 5 с; иначе обычный интервал */
                if (G.spawn_t > 90 || G.spawn_ground_budget > 0) G.spawn_t = (G.spawn_ground_budget > 0 && G.explored[level_room(FX_INT(e->x), FX_INT(e->y))]) ? 60 : G.spawn_t;
            }
            actor_update(e, &G.lv, &ZERO_IN);
            if (e->dead_t > 50) e->active = 0;
        }
        if (e->active && e->stand_obj >= 0) G.lv.occ[e->stand_obj] = 1;
    }
    spawn_tick();
}

/* арена босса: враги там никогда не появляются */
static int in_boss_area(int x, int y) {
    return x >= G.lv.arena.trig_x0 - 8 && x <= G.lv.arena.trig_x1 + 8 && y >= G.lv.arena.ceil && y <= G.lv.arena.floor + 10;
}

static int heroes_linked(void) {
    int n = 1; /* игрок */
    Actor *pl = player();
    if (!pl) return 1;
    int py = FX_INT(pl->y);
    for (int i = 0; i < NUM_HEROES; i++) {
        if (i == G.cur || !G.hero[i].active || G.hero[i].hp <= 0) continue;
        if (G.follow || ABSI(FX_INT(G.hero[i].y) - py) <= 40) n++;
    }
    return n < 1 ? 1 : n;
}
static int count_ground_in_room(int room) {
    int n = 0;
    for (int i = 0; i < EN_MAX; i++) {
        Actor *e = &G.en[i];
        if (!e->active || e->hp <= 0) continue;
        if (e->kind == RIG_MONOMAN) continue; /* летающий */
        if (level_room(FX_INT(e->x), FX_INT(e->y)) == room) n++;
    }
    return n;
}
static int count_fly_in_room(int room) {
    int n = 0;
    for (int i = 0; i < EN_MAX; i++) {
        Actor *e = &G.en[i];
        if (!e->active || e->hp <= 0) continue;
        if (e->kind != RIG_MONOMAN) continue;
        if (level_room(FX_INT(e->x), FX_INT(e->y)) == room) n++;
    }
    return n;
}
static int player_in_water(void) {
    Actor *pl = player();
    if (!pl || G.lv.water_y <= 0) return 0;
    int px = FX_INT(pl->x), py = FX_INT(pl->y);
    return px >= G.lv.water_x0 && px <= G.lv.water_x1 && py - pl->h / 2 > G.lv.water_y;
}
static int player_air_mode(void) {
    Actor *pl = player();
    if (!pl) return 0;
    if (pl->kind == RIG_MISTRESS && pl->spin_t > 0) return 1;
    if (pl->kind == RIG_REFREEZER && pl->misc > 0 && pl->flip) return 1;
    if (!actor_on_ground(pl, &G.lv) && ABSI(pl->vy) > FXF(1.0)) return 1;
    return 0;
}
/* свободный коридор вокруг героя по горизонтали: < 240 px — комната слишком мала для респауна */
static int tiny_room(const Actor *pl) {
    int px = FX_INT(pl->x), py = FX_INT(pl->y), l = px, r = px;
    while (px - l < 300 && !level_solid_rect(&G.lv, l - 1, py - 20, l - 1, py - 6)) l--;
    while (r - px < 300 && !level_solid_rect(&G.lv, r + 1, py - 20, r + 1, py - 6)) r++;
    return r - l < VIEW_W / 4;                          /* Stealth: комната уже 1/4 экрана — враги не появляются */
}
static int pick_spawn_xy(int *ox, int *oy, int need_ground) {
    Actor *pl = player();
    if (!pl) return 0;
    int plx = FX_INT(pl->x), ply = FX_INT(pl->y);
    if (need_ground) { if (!actor_on_ground(pl, &G.lv)) return 0; ply = G.pl_floor_y ? G.pl_floor_y : ply; }   /* дискретный пол: только на опоре игрока */
    int room = level_room(plx, ply);
    int edge_try[4] = { G.cam_x + 24, G.cam_x + VIEW_W - 24, plx - 120, plx + 120 };
    for (int t = 0; t < 8; t++) {
        int x = edge_try[t % 4] + (t >= 4 ? (t % 2 ? 16 : -16) : 0);
        if (x < 24) x = 24;
        if (x > WORLD_W - 24) x = WORLD_W - 24;
        if (ABSI(x - plx) < 60) continue;
        if (level_room(x, ply) != room) continue;
        if (in_boss_area(x, ply)) continue;
        if (need_ground) {
            if (!level_solid_rect(&G.lv, x - 4, ply, x + 4, ply + 1)) continue;
            if (level_solid_rect(&G.lv, x - 6, ply - 40, x + 6, ply - 6)) continue;
            if (blocked_between(x, plx, ply - 8)) continue;
        }
        int clash = 0;
        for (int k = 0; k < EN_MAX; k++)
            if (G.en[k].active && G.en[k].hp > 0 && ABSI(FX_INT(G.en[k].x) - x) < 32) clash = 1;
        if (!clash) { *ox = x; *oy = need_ground ? ply : (ply - 40); return 1; }
    }
    return 0;
}
static int spawn_one(int kind, int flying) {
    int slot = -1;
    for (int i = 0; i < EN_MAX; i++) if (!G.en[i].active) { slot = i; break; }
    if (slot < 0) return 0;
    int sx, sy;
    if (!pick_spawn_xy(&sx, &sy, !flying)) return 0;
    Actor *e = &G.en[slot];
    Actor *pl = player();
    actor_init(e, kind, 1, sx, flying ? sy : (kind == RIG_MONOMAN ? sy - 16 : sy));
    e->post_x = sx; e->post_y = flying ? sy : FX_INT(pl->y);
    e->facing = FX_INT(pl->x) < sx ? -1 : 1;
    if (flying) e->flying = 1;
    return 1;
}
static int pick_ground_kind(void) {
    int ncls = 3 + (G.level_no - 1); if (ncls > NUM_ENEMY_CLASSES) ncls = NUM_ENEMY_CLASSES;
    int w[NUM_ENEMY_CLASSES], total = 0;
    for (int c = 0; c < ncls; c++) {
        if (!enemy_class_loaded(c)) { w[c] = 0; continue; }
        if (ENEMY_RIG[c] == RIG_MONOMAN) { w[c] = 0; continue; } /* летающий отдельно */
        if (c == 2) w[c] = 8; else if (c == 0) w[c] = 40; else if (c == 1) w[c] = 35; else w[c] = 15;
        total += w[c];
    }
    if (!total) return RIG_SCOUT;
    int r = (int)(g_rand() % (uint32_t)total);
    for (int c = 0; c < ncls; c++) { if (r < w[c]) return ENEMY_RIG[c]; r -= w[c]; }
    return RIG_SCOUT;
}

void spawn_add(int force_near) {
    (void)force_near;
    /* совместимость: одиночный вызов = 1 ground attempt */
    if (enemies_alive() >= ENEMY_CAP) { G.spawn_t = 30; return; }
    spawn_one(pick_ground_kind(), 0);
    G.spawn_t = SPAWN_FRAMES;
}

static int monoman_allowed(void) { return G.level_no >= 3; }

static void spawn_tick(void) {
    { static int nos = -1; if (nos < 0) nos = getenv("NO_SPAWN") ? 1 : 0; if (nos) return; }   /* отладка */
    /* во время боя с боссом и после победы — никаких врагов (в т.ч. летающих) */
    if (G.boss_state != BS_SLEEP) return;
    if (G.boss_seen && G.boss_state == BS_DEAD) return;
    /* после смерти босса boss_seen сбрасывается; доп. блок: пока artifact не взят и босс был — уже в DEAD+seen */
    Actor *pl = player();
    if (!pl || pl->hp <= 0) return;
    int room = level_room(FX_INT(pl->x), FX_INT(pl->y));
    if (room < 0 || room >= 12) return;
    /* враги закрытых комнат живут только пока игрок внутри: ушёл (или сменил героя) — они исчезают */
    if ((G.frame & 15) == 0)
        for (int i = 0; i < EN_MAX; i++) {
            Actor *e = &G.en[i];
            if (!e->active || e->hp <= 0) continue;
            int er = level_room(FX_INT(e->x), FX_INT(e->y));
            if (er >= 0 && er < 12 && er != room && !G.explored[er]) e->active = 0;
        }
    if (room != G.spawn_room_last) {
        G.spawn_room_last = room;
        G.spawn_ground_budget = -1;
        G.spawn_ground_pack = 0;
        G.spawn_water_budget = -1;
        if (!G.ambushed[room]) {                                       /* новый stage: засада - волна стартует сразу */
            G.ambushed[room] = 1;
            G.spawn_t = (!G.explored[room] || G.solo_mode) ? 60 : 0;
            G.wave_t = 0;
        } else if ((!G.explored[room] || G.solo_mode) && G.spawn_t < 300) G.spawn_t = 300;   /* stealth/solo: первый враг не сразу */
    }
    int open = G.explored[room];
    int gentle = !open || G.solo_mode;                   /* Stealth / Solo: игрок один — врагов меньше */
    if (!open && tiny_room(pl)) return;                  /* крошечная закрытая комната (кнопка Pengi и т.п.) — негде сражаться */
    int air = player_air_mode();
    int water = player_in_water();
    int linked = heroes_linked();
    int ground_n = count_ground_in_room(room);
    int fly_n = count_fly_in_room(room);

    if (G.spawn_t > 0) G.spawn_t--;
    if (G.spawn_fly_t > 0) G.spawn_fly_t--;
    if (G.spawn_water_t > 0) G.spawn_water_t--;

    if (water) {
        if (open) {
            if (G.spawn_water_budget < 0) G.spawn_water_budget = linked * 2;
            if (G.spawn_water_budget > 0 && ground_n < 3 && G.spawn_water_t <= 0 && enemies_alive() < ENEMY_CAP) {
                int n = 3; if (n > G.spawn_water_budget) n = G.spawn_water_budget;
                for (int k = 0; k < n; k++) if (spawn_one(pick_ground_kind(), 0)) G.spawn_water_budget--;
                G.spawn_water_t = 300;
            } else if (G.spawn_water_budget <= 0 && G.spawn_water_t <= 0) {
                G.spawn_water_budget = linked * 2;
                G.spawn_water_t = 300;
            }
        } else {
            if (ground_n < 3 && G.spawn_water_t <= 0 && enemies_alive() < ENEMY_CAP) {
                for (int k = 0; k < 3; k++) spawn_one(pick_ground_kind(), 0);
                G.spawn_water_t = 240;
            }
        }
        return;
    }

    if (air) {
        if (!monoman_allowed()) return;
        int maxf = (open && !G.solo_mode) ? 2 : 1;
        int iv = open ? 1200 : 540; /* открытая: 2 каждые 20 с */
        if (fly_n < maxf && G.spawn_fly_t <= 0 && enemies_alive() < ENEMY_CAP) {
            int need = maxf - fly_n;
            for (int k = 0; k < need; k++) spawn_one(RIG_MONOMAN, 1);
            G.spawn_fly_t = iv;
        }
        return;
    }

    if (open) {
        if (G.spawn_ground_budget < 0) {
            G.spawn_ground_budget = linked * 2;
            G.spawn_ground_pack = 0;
        }
        if (G.spawn_ground_budget > 0 && ++G.wave_t > 1800) {          /* волна длится не дольше 30 с */
            G.spawn_ground_budget = 0; G.wave_t = 0;
            if (ground_n == 0) G.spawn_t = 1800;
        }
        if (G.spawn_ground_budget > 0) {
            /* старт волны — до 3 врагов, далее по одному на каждого убитого, пока бюджет не кончится */
            if (ground_n < (gentle ? 1 : 3) && G.spawn_t <= 0 && enemies_alive() < (gentle ? 2 : ENEMY_CAP)) {
                if (spawn_one(pick_ground_kind(), 0)) G.spawn_ground_budget--;
                G.spawn_t = gentle ? 600 : (G.spawn_ground_pack++ < 3 ? 30 : 300);
            }
        } else if (ground_n == 0 && G.spawn_t <= 0) {
            G.spawn_ground_budget = linked * 2;
            G.spawn_ground_pack = 0; G.wave_t = 0;
            G.spawn_t = 1800; /* пауза 30 сек после волны */
        }
        /* летающие открытых: 2 каждые 20 сек */
        if (!gentle && monoman_allowed() && fly_n < 2 && G.spawn_fly_t <= 0 && enemies_alive() < ENEMY_CAP) {
            int need = 2 - fly_n;
            for (int k = 0; k < need; k++) spawn_one(RIG_MONOMAN, 1);
            G.spawn_fly_t = 2400;
        }
    } else {
        if (ground_n + fly_n < 2 && G.spawn_t <= 0 && enemies_alive() < 2) {
            spawn_one(pick_ground_kind(), 0);
            G.spawn_t = 900; /* закрытая комната (stealth): интервал 15 с, максимум 2 врага */
        }
        if (monoman_allowed() && ground_n + fly_n < 1 && G.spawn_fly_t <= 0 && enemies_alive() < 2) {
            spawn_one(RIG_MONOMAN, 1);
            G.spawn_fly_t = 1200;
        }
    }
}


/* ===================================================================== дополнительная отрисовка врагов */
void enemy_draw_extra(Image *fb, int cx, int cy) {
    for (int i = 0; i < EN_MAX; i++) {
        const Actor *e = &G.en[i];
        if (!e->active || e->hp <= 0 || e->attack_kind != AT_RANGED) continue;
        int ex = FX_INT(e->x), ey = FX_INT(e->y);
        if (e->kind == RIG_FIREWALKER && e->misc > 2) {
            int mx = ex + e->facing * 24, my = ey - 24, len = e->misc;
            for (int d = 0; d <= len; d += 5) {
                int px = mx + e->facing * d - cx, py = my + (d / 5 % 2) - cy;
                ui_rect(fb, px - 1, py - 1, 3, 3, (d / 5) % 2 ? 0xFFFFB030u : 0xFFD05010u);
                if (((G.frame >> 1) + d / 5) % 3 == 0) ui_rect(fb, px, py - 3, 1, 2, 0xC0FFE060u);      /* язычки пламени */
            }
            int hx = mx + e->facing * len - cx;
            ui_disc(fb, hx, my - cy, 3, 0xFFFF8020u); ui_disc(fb, hx, my - cy, 1, 0xFFFFF0A0u);
        }
        if (e->kind == RIG_IRONFEM && e->misc > 2) {
            int mx = ex + e->facing * 24, my = ey - 24, len = e->misc;
            for (int d = 0; d <= len; d += 5) {
                int px = mx + e->facing * d - cx, py = my + (d / 5 % 2) - cy;
                ui_rect(fb, px - 1, py - 1, 3, 3, (d / 5) % 2 ? 0xFFB0B4C0u : 0xFF60646Eu);
            }
            int hx = mx + e->facing * len - cx;
            ui_disc(fb, hx, my - cy, 3, 0xFFD0D4E0u); ui_disc(fb, hx, my - cy, 1, 0xFF303038u);
        }
    }
}
