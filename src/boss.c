/* Bosses of levels 1..9, their hazards (waves, lightning, gas, water, darkness), game log and level switch. */
#include <stdlib.h>
#include <stdio.h>
#include "game_priv.h"
#include "events.h"
#include "sound.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

const char *const BOSS_NAME[NUM_LEVELS] = {"PROFESSOR", "MAESTRO", "BIGPUSSY", "COACH", "HACKER", "WATCHER", "BOOT", "SUPERDROM", "DRUNKY KOZEMEKIS"};
const int BOSS_RIG[NUM_LEVELS] = {RIG_PROFESSOR, RIG_MAESTRO, RIG_BIGPUSSY, RIG_COACH, RIG_HACKER, RIG_WATCHER, RIG_BOOTBOSS, RIG_SUPERDROM, RIG_DRUNKY};
static const char *const BOSS_HINT[NUM_LEVELS] = {
    "Hit the Professor while he dives!", "Maestro: jump over the drum wave!", "BigPussy: hit her when she is stuck in the floor!",
    "Coach: hide on the platform from her eye lasers, hit her when tired, drop the bookshelf on her!", "Hacker: attack only during his 4 s pause!",
    "Watcher: press the floor button to kill the rocket!", "Boot: hit him in the first 5 s, then find a platform!",
    "SuperDrom: heli shoots, the saw chases you!", "Kozemekis: dodge bottles, jump on platforms from the poison; at low health feed him with the Refreezer!"};

static const Intent ZERO_IN = {0};
#define ARN (G.lv.arena)
#define FL  (G.lv.arena.floor)
#define CLAMP_X(x) MAXI(ARN.x0 + 24, MINI(ARN.x1 - 24, (x)))

static int sgn(int v) { return v > 0 ? 1 : v < 0 ? -1 : 0; }
static int irand(int a, int b) { return a + (int)(g_rand() % (uint32_t)(b - a + 1)); }

/* ===================================================================== log */
void game_log(const char *fmt, ...) {
    char buf[112];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    fprintf(stderr, "[game] %s\n", buf);
    FILE *f = fopen("game.log", "a");
    if (f) { fprintf(f, "f=%d %s\n", G.frame, buf); fclose(f); }
    for (int i = 0; i < 4; i++) { memcpy(G.logl[i], G.logl[i + 1], sizeof G.logl[i]); G.logt[i] = G.logt[i + 1]; }
    snprintf(G.logl[4], sizeof G.logl[4], "%s", buf); G.logt[4] = 900;
}

/* ===================================================================== context */
typedef struct { fx gx, gy, vmax; int anim, ground, free_x; } Ctl;
static Ctl C;

/* дискретный выстрел параллельно земле: высота по последней позиции цели, один снаряд — можно перепрыгнуть */
static void hshot(int type, Actor *b, Actor *t, float sp, int life) {
    int bx = FX_INT(b->x), dir = FX_INT(t->x) >= bx ? 1 : -1;
    fire_projx(type, 1, bx + dir * 20, FX_INT(t->y) - t->h / 2, (float)dir * sp, 0.f, 1, life);
}
static void phase(int p) { G.bst = p; G.boss_t = 0; G.bx[9] = 0; }
static void face(Actor *b, int tx) { b->facing = tx < FX_INT(b->x) ? -1 : 1; }
static void go(int x, int y, float v) { C.gx = INT_FX(x); C.gy = INT_FX(y); C.vmax = (fx)(v * 256.f); }
static int move_anim(const Actor *b) { return ABSI(b->vx) >= FXF(1.2) ? A_RUN : ABSI(b->vx) >= FXF(0.2) ? A_WALK : A_IDLE; }
static void start_anim(Actor *b, int anim, int act) {
    animator_restart(&b->anim, anim);
    if (act >= 0) sound_play_actor_at(b->kind, act, FX_INT(b->x), FX_INT(b->y));
}

static int hero_in_arena(const Actor *h) {
    int x = FX_INT(h->x), y = FX_INT(h->y);
    return h->active && h->hp > 0 && x >= ARN.trig_x0 && x <= ARN.trig_x1 && y >= ARN.ceil && y <= FL + 10;
}
static Actor *pick_target(const Actor *b) {            /* босс видит только позицию игрока, остальных героев - никогда */
    (void)b;
    if (player()->hp > 0) return player();
    for (int i = 0; i < NUM_HEROES; i++) if (G.hero[i].hp > 0) return &G.hero[i];
    return &G.hero[0];
}

static int box_hit(const Actor *a, const Actor *b, int pad) {
    int ax0, ay0, ax1, ay1, bx0, by0, bx1, by1;
    actor_box(a, &ax0, &ay0, &ax1, &ay1); actor_box(b, &bx0, &by0, &bx1, &by1);
    return ax1 + pad >= bx0 && ax0 - pad <= bx1 && ay1 + pad >= by0 && ay0 - pad <= by1;
}
static void touch(Actor *b, int dmg, float kx, float ky) {
    for (int i = 0; i < NUM_HEROES; i++) {
        Actor *h = &G.hero[i];
        if (h->hp > 0 && box_hit(b, h, 0)) hurt_hero(h, dmg, sgn(FX_INT(h->x) - FX_INT(b->x)) ? sgn(FX_INT(h->x) - FX_INT(b->x)) : 1, (fx)(kx * 256.f), (fx)(ky * 256.f));
    }
}
static int in_hole(int x) { return x > ARN.hole0 - 14 && x < ARN.hole1 + 14; }

/* ===================================================================== hazards */
static Haz *hz_add(int type, int x, int y) {
    for (int i = 0; i < HZ_MAX; i++) if (!G.hz[i].active) { Haz *z = &G.hz[i]; memset(z, 0, sizeof *z); z->active = 1; z->type = type; z->x = x; z->y = y; return z; }
    return NULL;
}
static void ring(int x, int dir, float speed, int maxr, int hgt, int dmg) {
    Haz *z = hz_add(HZ_RING, x, FL); if (!z) return;
    z->vx = speed; z->b = maxr; z->dir = dir; z->hgt = hgt; z->dmg = dmg;
    sound_play_sfx_at(SFX_ZAP, x, FL);
}
static void bolt(int x, int warn) {
    Haz *z = hz_add(HZ_BOLT, x, 0); if (!z) return;
    z->a = ARN.ceil; z->b = FL; z->warn = warn; z->dmg = 1;
    sound_play_sfx_at(SFX_WARN, x, FL - 40);
}
static void cloud(float x, float y, float vx, int maxr, int life) {
    Haz *z = hz_add(HZ_CLOUD, (int)x, (int)y); if (!z) return;
    z->px = x; z->py = y; z->vx = vx; z->a = maxr; z->c = life; z->dmg = 1;
}

void hazards_update(void) {
    for (int i = 0; i < HZ_MAX; i++) {
        Haz *z = &G.hz[i];
        if (!z->active) continue;
        z->t++;
        if (z->type == HZ_RING) {
            z->px += z->vx;
            if (z->px > (float)z->b) { z->active = 0; continue; }
            for (int k = 0; k < NUM_HEROES; k++) {
                Actor *h = &G.hero[k];
                if (!h->active || h->hp <= 0 || (z->flag >> k & 1)) continue;
                int hx = FX_INT(h->x) - z->x, hy = FX_INT(h->y);
                if (z->dir && hx * z->dir < 0) continue;
                if (ABSI(ABSI(hx) - (int)z->px) > 7 || hy < z->y - z->hgt || hy > z->y + 6) continue;
                if (hurt_hero(h, z->dmg, hx >= 0 ? 1 : -1, FXF(2.4), FXF(2.8))) z->flag |= 1 << k;
            }
        } else if (z->type == HZ_BOLT) {
            if (z->t == z->warn) { sound_play_sfx_at(SFX_EXPLODE, z->x, FL - 40); G.shake = 8; G.bx[8] = 4; }
            if (z->t >= z->warn && z->t < z->warn + 8)
                for (int k = 0; k < NUM_HEROES; k++) {
                    Actor *h = &G.hero[k];
                    if (!h->active || h->hp <= 0 || (z->flag >> k & 1)) continue;
                    if (ABSI(FX_INT(h->x) - z->x) <= 9 && FX_INT(h->y) >= z->a && FX_INT(h->y) <= z->b + 8)
                        if (hurt_hero(h, z->dmg, 1, FXF(1.0), FXF(2.6))) z->flag |= 1 << k;
                }
            if (z->t >= z->warn + 12) z->active = 0;
        } else if (z->type == HZ_LASER) {
            if (z->t > z->c) { z->active = 0; continue; }
            if (z->t < z->warn) continue;
            for (int k = 0; k < NUM_HEROES; k++) {
                Actor *h = &G.hero[k];
                if (!h->active || h->hp <= 0) continue;
                int x0, y0, x1, y1; actor_box(h, &x0, &y0, &x1, &y1);
                for (int s = 0; s + 1 < z->pts; s++) {
                    int ax = z->lx[s], ay = z->ly[s], bx = z->lx[s + 1], by = z->ly[s + 1];
                    int n = MAXI(ABSI(bx - ax), ABSI(by - ay)), hit = 0;
                    for (int q = 0; q <= n && !hit; q += 2) {
                        int px = ax + (n ? (bx - ax) * q / n : 0), py = ay + (n ? (by - ay) * q / n : 0);
                        hit = px >= x0 - 1 && px <= x1 + 1 && py >= y0 - 1 && py <= y1 + 1;
                    }
                    if (hit) { hurt_hero(h, z->dmg, 1, FXF(1.8), FXF(2.4)); break; }
                }
            }
        } else if (z->type == HZ_POOL) {
            if (z->a < 400) z->a += 2;
            if (z->t > z->c) { z->active = 0; continue; }
            for (int k = 0; k < NUM_HEROES; k++) {
                Actor *h = &G.hero[k];
                if (!h->active || h->hp <= 0) continue;
                int hx = FX_INT(h->x) - z->x, hy = FX_INT(h->y);
                if (ABSI(hx) <= z->a && hy >= z->y - 3 && hy <= z->y + 8 && FX_INT(h->x) >= ARN.x0 && FX_INT(h->x) <= ARN.x1) hurt_hero(h, z->dmg, hx >= 0 ? 1 : -1, FXF(1.0), FXF(2.6));
            }
        } else if (z->type == HZ_CLOUD) {
            z->px += z->vx;
            int r = MINI(z->a, 6 + z->t / 3);
            if (z->t > z->c || z->px < ARN.x0 - 10 || z->px > ARN.x1 + 10) { z->active = 0; continue; }
            for (int k = 0; k < NUM_HEROES; k++) {
                Actor *h = &G.hero[k];
                if (!h->active || h->hp <= 0) continue;
                int dx = FX_INT(h->x) - (int)z->px, dy = FX_INT(h->y) - h->h / 2 - (int)z->py;
                if (dx * dx + dy * dy < r * r) hurt_hero(h, z->dmg, sgn(dx) ? sgn(dx) : 1, FXF(1.6), FXF(2.2));
            }
        }
    }
    if (G.boss_kind == RIG_BOOTBOSS && G.lv.water_y > 0 && G.boss_state != BS_SLEEP && G.boss_state != BS_DEAD)    /* вода Booty бьёт тех, кто в ней (спасает мини-платформа) */
        for (int k = 0; k < NUM_HEROES; k++) {
            Actor *h = &G.hero[k];
            if (!h->active || h->hp <= 0 || h->kind == RIG_HERO7) continue;
            if (FX_INT(h->x) >= ARN.x0 && FX_INT(h->x) <= ARN.x1 && FX_INT(h->y) > G.lv.water_y + 2) hurt_hero(h, 1, 1, FXF(0.4), FXF(2.2));
        }
    if (G.bx[8] > 0) G.bx[8]--;
    /* temporary button timeout (Watcher) */
    if (G.boss_kind == RIG_WATCHER && G.bx[5] > 0 && --G.bx[5] == 0) {
        Obj *o = &G.lv.obj[G.lv.obj_bbtn[0]]; o->visible = 0; o->on = 1;
    }
}

static void place_button_y(int idx, int x, int y);
static void place_button(int idx, int x) {
    int k = G.lv.obj_bbtn[idx]; if (k < 0) return;
    Obj *o = &G.lv.obj[k];
    o->x = (int16_t)x; o->y = (int16_t)(FL - 16); o->w = 14; o->h = 16;
    o->visible = 1; o->on = 0; o->target = -1;
    spark(x + 7, FL - 10, 10, 0xFF60FF80u, 2);
}
static int random_free_x(int avoid_x) {
    for (int t = 0; t < 30; t++) {
        int x = irand(ARN.x0 + 30, ARN.x1 - 30);
        if (!in_hole(x) && ABSI(x - avoid_x) > 50) return x;
    }
    return ARN.x0 + 40;
}
static void plat_set(int idx, int x, int y, int on) {
    int k = G.lv.obj_bplat[idx]; if (k < 0) return;
    Obj *o = &G.lv.obj[k];
    o->x = (int16_t)x; o->y = (int16_t)y; o->fx_ = INT_FX(x); o->fy_ = INT_FX(y);
    o->visible = (uint8_t)on; o->solid = (uint8_t)on;
}

static void flip_all(int on) {
    for (int i = 0; i < NUM_HEROES; i++) {
        Actor *h = &G.hero[i];
        if (on ? (h->active && h->hp > 0 && hero_in_arena(h)) : h->flip) actor_set_flip(h, &G.lv, on);
    }
    for (int i = 0; i < EN_MAX; i++) {
        Actor *e = &G.en[i];
        int x = FX_INT(e->x), y = FX_INT(e->y);
        if (on ? (e->active && e->hp > 0 && !e->flying && x >= ARN.x0 && x <= ARN.x1 && y >= ARN.ceil && y <= FL + 4) : e->flip) actor_set_flip(e, &G.lv, on);
    }
    G.flip_on = on;
}

static void clear_effects(void) {
    if (G.flip_on || G.boss.flip) flip_all(0);
    G.boss.flip = 0;
    G.dark = 0; G.flip_on = 0;
    memset(G.hz, 0, sizeof G.hz);
    G.lv.water_y = 0;
    for (int i = 0; i < 2; i++) { int k = G.lv.obj_bbtn[i]; if (k >= 0) { G.lv.obj[k].visible = 0; G.lv.obj[k].on = 1; } }
    for (int i = 0; i < 3; i++) plat_set(i, 0, 0, 0);
    for (int i = 0; i < PJ_MAX; i++) if (G.pj[i].active && (G.pj[i].type == PJ_HOMING || G.pj[i].type == PJ_SNOWBIG || G.pj[i].type == PJ_GREEN || G.pj[i].type == PJ_MISSILE)) G.pj[i].active = 0;
    for (int i = 0; i < NUM_HEROES; i++) { G.hero[i].frozen = 0; G.hero[i].drown_t = 0; }
    memset(G.bx, 0, sizeof G.bx);
}

void boss_reset(void) {
    clear_effects();
    memset(&G.boss, 0, sizeof G.boss);
    G.boss_state = BS_SLEEP; G.bst = 0; G.boss_t = 0; G.boss_vuln = 0;
    G.boss_kind = BOSS_RIG[(G.level_no >= 1 && G.level_no <= NUM_LEVELS ? G.level_no : 1) - 1];
    if (G.boss_seen) { G.boss_seen = 0; sound_play_bgm(BGM_LEVEL); }
}

int boss_active(void) { return G.boss_state != BS_SLEEP && G.boss_state != BS_DEAD && G.boss.active; }

/* ===================================================================== damage to the boss */
int boss_hurt(int dmg, int dir) {
    Actor *b = &G.boss;
    if (G.boss_state == BS_SLEEP || G.boss_state == BS_DEAD || !b->active) return 0;
    if (!G.boss_vuln) {
        if (b->invuln_t == 0) {
            spark(FX_INT(b->x), FX_INT(b->y) - b->h / 2, 4, 0xFFB0B8C8u, 2); b->invuln_t = 12;
            if (!G.bx[9]) { G.bx[9] = 1; say(BOSS_HINT[G.level_no - 1]); }
        }
        return 0;
    }
    if (G.boss_kind == RIG_DRUNKY) { if (b->hp - dmg < 5) dmg = b->hp - 5; if (dmg <= 0) return 0; }
    if (!actor_damage(b, dmg, dir, 0, 0, 12)) return 0;
    spark(FX_INT(b->x), FX_INT(b->y) - b->h / 2, 10, 0xFFFFE070u, 2);
    sound_play_sfx_at(SFX_ENEMY_HIT, FX_INT(b->x), FX_INT(b->y));
    if (b->hp <= 0) {
        G.boss_state = BS_DEAD; G.boss_t = 0;
        clear_effects();
        G.spawn_t = 1 << 28; /* враги больше не появляются */
        G.boss_hide_allies = 0;
        game_log("Boss %s defeated", BOSS_NAME[G.level_no - 1]);
        /* все герои воскресают на этаже босса */
        game_revive_all();
        for (int k = 0; k < NUM_HEROES; k++) {
            Actor *h = &G.hero[k];
            int kind = h->kind ? h->kind : RIG_PENGI;
            if (!h->active || h->hp <= 0) {
                actor_init(h, kind, 0, game_ground_near(FX_INT(b->x) + (k - 2) * 18, FL), FL);
                h->active = 1; h->invuln_t = 90;
            }
            h->immortal = (k != G.cur);
            G.out[k] = 0; G.dying[k] = 0;
            if (G.lives[k] < 1) G.lives[k] = 1;
            h->post_x = FX_INT(h->x); h->post_y = FL;
        }
        say("Boss down! Team is back — go to the EXIT");
        sound_play_bgm(BGM_LEVEL); /* после босса — обычная музыка уровня */
    }
    return 1;
}

void boss_on_button(int obj) {
    int which = obj == G.lv.obj_bbtn[0] ? 0 : obj == G.lv.obj_bbtn[1] ? 1 : -1;
    if (which < 0) return;
    Obj *o = &G.lv.obj[obj]; o->visible = 0; o->on = 1;
    if (G.boss_kind == RIG_COACH) {
        int mid = (ARN.x0 + ARN.x1) / 2;
        if (which == 0 && G.bst == 3) { place_button_y(1, mid + 90 + 17, FL - 72); say("Now the right platform!"); }
        else if (which == 1 && G.bst == 3 && G.bx[4] == 0) { G.bx[4] = 40; say("The bookshelf falls!"); sound_play_sfx(SFX_WARN); }
    } else if (G.boss_kind == RIG_WATCHER) {
        for (int i = 0; i < PJ_MAX; i++) if (G.pj[i].active && G.pj[i].type == PJ_HOMING) {
            spark(FX_INT(G.pj[i].x), FX_INT(G.pj[i].y), 18, 0xFFFFC040u, 3); G.pj[i].active = 0;
        }
        G.bx[5] = 0; say("Rocket destroyed!"); sound_play_sfx(SFX_EXPLODE);
    } else if (G.boss_kind == RIG_DRUNKY && G.bx[3]) {
        G.lv.water_y = 0; G.bx[3] = 0; say("Drained!"); sound_play_sfx(SFX_SPLASH);
        phase(4);
    }
}

/* ===================================================================== motion */
static void apply(Actor *b) {
    fx dx = C.gx - b->x, dy = C.gy - b->y;
    fx tvx = dx / 20, tvy = dy / 20;
    if (tvx > C.vmax) tvx = C.vmax;
    if (tvx < -C.vmax) tvx = -C.vmax;
    if (tvy > C.vmax) tvy = C.vmax;
    if (tvy < -C.vmax) tvy = -C.vmax;
    b->vx += (tvx - b->vx) / 6; b->vy += (tvy - b->vy) / 6;
    int nx = FX_INT(b->x + b->vx);
    if (nx < ARN.x0 + b->w / 2 && b->vx < 0) b->vx = 0;
    if (nx > ARN.x1 - b->w / 2 && b->vx > 0) b->vx = 0;
    if (!b->flip && FX_INT(b->y + b->vy) < ARN.ceil + 22 && b->vy < 0) b->vy = 0;
    int anim = C.anim;
    if (b->hurt_t > 0 && (anim == A_IDLE || anim == A_WALK || anim == A_RUN)) anim = A_HURT;
    animator_play(&b->anim, anim);
    b->own_anim = 1;
    b->anim.speed = 1.f;
}

/* ===================================================================== bosses */
static void b_professor(Actor *b, Actor *t) {
    int bx = FX_INT(b->x), by = FX_INT(b->y), tx = FX_INT(t->x), ty = FX_INT(t->y);
    switch (G.bst) {
    case 0: {
        int side = bx < tx ? -1 : 1;
        go(tx + side * 120, 240 + (int)(sinf((float)G.frame * 0.05f) * 10.f), 1.6f);
        face(b, tx); C.anim = move_anim(b);
        if (++G.boss_t >= 90) { phase(1); G.bx[0] = b->hp <= BOSS_HP / 2 ? 3 : 2; }
        break; }
    case 1:
        go(bx, by, 0.4f); face(b, tx); C.anim = A_RANGED;
        G.boss_t++;
        if (G.boss_t == 1) start_anim(b, A_RANGED, SFX_ACT_RANGED);
        if (G.boss_t == 12) hshot(PJ_BOLT, b, t, 2.1f, 220);
        if (G.boss_t >= 78) { G.boss_t = 0; if (--G.bx[0] <= 0) phase(2); }
        break;
    case 2:
        G.boss_t++; C.anim = A_RUN;
        if (G.boss_t < 36) { go(tx, FL - 8, 2.0f); face(b, tx); }
        else if (G.boss_t < 90) { C.gx = b->x + INT_FX(b->facing * 300); C.gy = INT_FX(FL - 8); C.vmax = FXF(3.2); }
        else phase(3);
        if (G.boss_t >= 30 && G.boss_t < 90) touch(b, 1, 3.4f, 3.0f);
        break;
    default:
        go(bx, FL - 8, 0.3f); C.anim = A_IDLE;
        if (++G.boss_t >= 75) phase(0);
        break;
    }
}

static void b_maestro(Actor *b, Actor *t) {
    /* Тромбон: 3 звуковые волны (ring) с паузой 2с → барабаны 5с в центре → сон 5с (уязвим) → цикл */
    int bx = FX_INT(b->x), tx = FX_INT(t->x);
    int mid = (ARN.x0 + ARN.x1) / 2;
    int dir = (tx >= bx) ? 1 : -1;
    switch (G.bst) {
    case 0: /* короткий подход, затем тромбон */
        go(tx - dir * 60, FL - 4, 1.6f); face(b, tx); C.anim = move_anim(b);
        if (++G.boss_t > 60 || ABSI(tx - bx) <= 80) { phase(1); G.bx[0] = 0; }
        break;
    case 1: /* тромбон: 3 звуковые волны, между ними пауза 2 с (120 кадров) */
        go(bx, FL - 4, 0.3f); face(b, tx); C.anim = A_RANGED; G.bx[2] = 0;
        if (G.boss_t == 0) start_anim(b, A_RANGED, SFX_ACT_RANGED);
        /* волны на кадрах 10, 130, 250 */
        if (G.boss_t == 10 || G.boss_t == 130 || G.boss_t == 250) {
            start_anim(b, A_RANGED, SFX_ACT_RANGED);
            ring(bx + dir * 8, dir, 2.6f, 240, 14, 1); /* звуковая волна по полу в сторону игрока */
            G.shake = 3;
            G.bx[0]++;
        }
        if (++G.boss_t >= 370) { phase(2); G.bx[0] = 0; } /* после 3-й волны + пауза */
        break;
    case 2: /* барабаны в центре, 5 секунд непрерывно — анимация барабанщика */
        go(mid, FL - 4, 2.2f); C.anim = A_SUPER; G.bx[2] = 1;
        if (G.boss_t == 0) start_anim(b, A_SUPER, SFX_ACT_SUPER);
        else if (b->anim.id != A_SUPER) animator_restart(&b->anim, A_SUPER);
        /* волны каждые ~0.5 с от центра в обе стороны */
        if (G.boss_t >= 20 && (G.boss_t - 20) % 50 == 0) {
            ring(mid, 0, 2.8f, 260, 10, 2);
            G.shake = 6;
            sound_play_sfx_at(SFX_ACT_SUPER, mid, FL);
        }
        if (++G.boss_t >= 300) { G.bx[2] = 0; phase(3); }
        break;
    default: /* сон / уязвим 5 секунд — время бить */
        go(mid, FL - 4, 0.4f); C.anim = A_IDLE; G.bx[2] = 0;
        G.boss_vuln = 1;
        if (++G.boss_t >= 300) phase(1); /* снова тромбон */
        break;
    }
}

static void b_bigpussy(Actor *b, Actor *t) {
    int bx = FX_INT(b->x), tx = FX_INT(t->x);
    G.boss_vuln = G.bst == 3;
    switch (G.bst) {
    case 0:
        go(tx - sgn(tx - bx) * 110, FL, 1.6f); face(b, tx); C.anim = move_anim(b);
        if (++G.boss_t > 70) phase(G.bx[0] >= 2 ? 2 : 1);
        break;
    case 1:
        go(bx, FL, 0.2f); face(b, tx); C.anim = A_RANGED;
        if (G.boss_t == 0) start_anim(b, A_RANGED, SFX_ACT_RANGED);
        if (G.boss_t == 14) ring(bx + b->facing * 20, b->facing, 2.2f, 200, 14, 1);
        if (++G.boss_t >= 30) { G.bx[0]++; phase(0); }
        break;
    case 2: {
        C.anim = A_SUPER; go(bx, FL, 0.2f);
        if (G.boss_t == 0) start_anim(b, A_SUPER, SFX_ACT_SUPER);
        if (G.boss_t < 20) face(b, tx);
        if (G.boss_t == 20) {
            int lx = tx; if (in_hole(lx)) lx += lx < (ARN.hole0 + ARN.hole1) / 2 ? -30 : 30;
            G.bx[3] = CLAMP_X(lx); G.bx[4] = bx;
        }
        if (G.boss_t >= 20 && G.boss_t < 46) {
            float u = (float)(G.boss_t - 20) / 26.f;
            b->x = INT_FX(G.bx[4] + (int)((float)(G.bx[3] - G.bx[4]) * u));
            b->y = INT_FX(FL - (int)(96.f * sinf(u * 3.14159f)));
            b->vx = b->vy = 0; C.gx = b->x; C.gy = b->y;
        }
        G.boss_t++;
        if (G.boss_t == 46) {
            b->y = INT_FX(FL); b->vy = 0; touch(b, 2, 3.0f, 3.2f);
            ring(FX_INT(b->x), 0, 2.4f, 100, 10, 1); G.shake = 14; spark(FX_INT(b->x), FL - 4, 24, 0xFFA09080u, 3);
            sound_play_sfx_at(SFX_EXPLODE, FX_INT(b->x), FL);
            G.bx[0] = 0; phase(3);
        }
        break; }
    default:
        go(bx, FL, 0.1f); C.anim = A_FALLDMG;
        if (++G.boss_t >= 240) phase(0);
        break;
    }
}

static void place_button_y(int idx, int x, int y) {                   /* кнопка на платформе */
    int k = G.lv.obj_bbtn[idx]; if (k < 0) return;
    Obj *o = &G.lv.obj[k];
    o->x = (int16_t)x; o->y = (int16_t)(y - 16); o->w = 14; o->h = 16;
    o->visible = 1; o->on = 0; o->target = -1;
    spark(x + 7, y - 10, 10, 0xFF60FF80u, 2);
}
/* Coach: глаза-лазеры. 1) бег влево-вправо с лазером параллельно земле 5 с (неуязвима; спасает платформа сверху)
   2) устала 5 с — можно бить 3) стреляет книгами по одной; игрок прыгает на платформу с кнопкой 1, затем 2 -> полка падает на неё */
static void b_coach(Actor *b, Actor *t) {
    int bx = FX_INT(b->x), tx = FX_INT(t->x);
    int mid = (ARN.x0 + ARN.x1) / 2;
    G.boss_vuln = G.bst == 2 || G.bst == 4;
    switch (G.bst) {
    case 0:                                                            /* подходит к игроку */
        go(tx, FL, 1.4f); face(b, tx); C.anim = move_anim(b); touch(b, 1, 2.4f, 2.4f);
        if (++G.boss_t > 90) { phase(1); G.bx[3] = bx < mid ? 1 : -1; game_log("Coach: eye lasers!"); say("Eye lasers! Jump on the upper platform!"); sound_play_sfx(SFX_WARN); }
        break;
    case 1: {                                                          /* лазерный забег */
        if (G.boss_t == 0) plat_set(2, mid - 32, FL - 48, 1);
        int edge = G.bx[3] > 0 ? ARN.x1 - 44 : ARN.x0 + 44;
        go(edge, FL, 3.0f); C.anim = A_RUN; b->facing = G.bx[3];
        if (ABSI(edge - bx) < 20) G.bx[3] = -G.bx[3];
        int ey = FL - 38, bx0 = bx + b->facing * 6, bx1 = bx + b->facing * 230;
        int lo = MINI(bx0, bx1), hi = MAXI(bx0, bx1);
        for (int i = 0; i < NUM_HEROES; i++) {
            Actor *h = &G.hero[i];
            if (!h->active || h->hp <= 0) continue;
            int x0, y0, x1, y1; actor_box(h, &x0, &y0, &x1, &y1);
            if (x1 >= lo && x0 <= hi && y1 >= ey - 2 && y0 <= ey + 2) hurt_hero(h, 1, b->facing, FXF(2.0), FXF(2.4));
        }
        touch(b, 1, 2.4f, 2.4f);
        if ((G.frame & 7) == 0) spark(bx0 + b->facing * 40, ey, 1, 0xFFFF6060u, 2);
        if (++G.boss_t >= 300) { plat_set(2, 0, 0, 0); phase(2); say("Coach is exhausted! Hit her!"); }
        break; }
    case 2:                                                            /* устала */
        go(bx, FL, 0.1f); C.anim = A_FALLDMG;
        if (++G.boss_t >= 300) phase(3);
        break;
    case 3: {                                                          /* книги + платформы с кнопками */
        if (G.boss_t == 0) {
            plat_set(0, mid - 140, FL - 40, 1); plat_set(1, mid + 90, FL - 72, 1);
            place_button_y(0, mid - 140 + 17, FL - 40); G.lv.obj[G.lv.obj_bbtn[1]].visible = 0;
            G.bx[2] = 0; G.bx[4] = 0;
            say("Books! Press the button on the left platform, then the right one!");
        }
        go(bx, FL, 0.2f); face(b, tx); C.anim = A_RANGED;
        if (G.boss_t % 100 == 40) { Actor *tt = t; int dir = FX_INT(tt->x) >= bx ? 1 : -1;
            fire_projx(PJ_BOOK, 1, bx + dir * 18, FX_INT(tt->y) - tt->h / 2, (float)dir * 2.0f, 0.f, 1, 260); start_anim(b, A_RANGED, SFX_ACT_RANGED); }
        if (G.bx[4] > 0) {                                             /* падает полка */
            if (--G.bx[4] == 0) {
                G.boss_vuln = 1; boss_hurt(2, b->facing); G.boss_vuln = 0;
                spark(bx, FL - 20, 30, 0xFFC09060u, 3); G.shake = 12; sound_play_sfx_at(SFX_EXPLODE, bx, FL);
                plat_set(0, 0, 0, 0); plat_set(1, 0, 0, 0);
                for (int k = 0; k < 2; k++) { Obj *o = &G.lv.obj[G.lv.obj_bbtn[k]]; o->visible = 0; o->on = 1; }
                phase(4);
            }
            break;
        }
        if (++G.boss_t >= 1100) {                                      /* не справились — цикл заново */
            plat_set(0, 0, 0, 0); plat_set(1, 0, 0, 0);
            for (int k = 0; k < 2; k++) { Obj *o = &G.lv.obj[G.lv.obj_bbtn[k]]; o->visible = 0; o->on = 1; }
            phase(0);
        }
        break; }
    default:                                                           /* оглушена после полки */
        go(bx, FL, 0.1f); C.anim = A_FALLDMG;
        if (++G.boss_t >= 150) phase(0);
        break;
    }
}

static void b_hacker(Actor *b, Actor *t) {
    int bx = FX_INT(b->x), by = FX_INT(b->y), tx = FX_INT(t->x), ty = FX_INT(t->y) - t->h / 2;
    int hy = FL - 12 + (int)(sinf((float)G.frame * 0.06f) * 5.f);
    G.boss_vuln = G.bst == 3;
    switch (G.bst) {
    case 0:
        go(tx + (bx < tx ? -1 : 1) * 130, hy, 1.0f); face(b, tx); C.anim = A_IDLE;
        if (++G.boss_t > 70) phase((g_rand() % 100u) < 65 ? 1 : 2);
        break;
    case 1:
        go(bx, hy, 0.3f); face(b, tx); C.anim = A_RANGED;
        if (G.boss_t == 0) start_anim(b, A_RANGED, SFX_ACT_RANGED);
        if (G.boss_t == 10 || G.boss_t == 75) { hshot(PJ_GREEN, b, t, 1.9f, 240); sound_play_sfx_at(SFX_ZAP, bx, by); }
        if (++G.boss_t >= 110) phase(3);
        break;
    case 2:
        go(bx, hy, 0.3f); face(b, tx); C.anim = A_RANGED;
        if (G.boss_t == 10 || G.boss_t == 32 || G.boss_t == 54) {
            Actor *h = &G.hero[irand(0, NUM_HEROES - 1)];
            bolt(hero_in_arena(h) ? FX_INT(h->x) : tx, 34);
        }
        if (++G.boss_t >= 100) phase(3);
        break;
    default:
        go(bx, hy, 0.2f); C.anim = A_RANGED;
        if (++G.boss_t >= 240) phase(0);
        break;
    }
}

static void b_watcher(Actor *b, Actor *t) {
    int bx = FX_INT(b->x), by = FX_INT(b->y), tx = FX_INT(t->x);
    int hy = ARN.ceil + 40 + (int)(sinf((float)G.frame * 0.07f) * 6.f);
    G.boss_vuln = G.bst == 3;
    switch (G.bst) {
    case 0:
        go(tx, hy, 1.5f); face(b, tx); C.anim = A_IDLE;
        if (++G.boss_t > 60) {
            int homing = 0;
            for (int i = 0; i < PJ_MAX; i++) if (G.pj[i].active && G.pj[i].type == PJ_HOMING) homing = 1;
            phase(!homing && (g_rand() % 100u) < 45 ? 2 : 1);
        }
        break;
    case 1:
        go(G.boss_t < 14 ? tx : bx, hy, 1.6f); face(b, tx); C.anim = A_RANGED;
        if (G.boss_t == 0) start_anim(b, A_RANGED, SFX_ACT_RANGED);
        if (G.boss_t == 14) {
            Proj *p = fire_projx(PJ_SNOWBIG, 1, bx, by + 6, 0.f, 1.0f, 1, 200);
            if (p) p->g = FXF(0.08);
        }
        if (++G.boss_t >= 30) phase(3);
        break;
    case 2:
        go(bx, hy, 0.3f); face(b, tx); C.anim = A_RANGED;
        if (G.boss_t == 0) start_anim(b, A_RANGED, SFX_ACT_RANGED);
        if (G.boss_t == 12) {
            Proj *p = fire_projx(PJ_HOMING, 1, bx, by + 6, 0.f, 1.0f, 1, 420);
            if (p) p->aux = (int)(t - G.hero);
            place_button(0, CLAMP_X((tx + bx) / 2));
            G.bx[5] = 420; say("Missile! Press the floor button (E)!"); sound_play_sfx(SFX_WARN);
        }
        if (++G.boss_t >= 30) phase(3);
        break;
    default:
        go(bx, FL - 22, 1.0f); C.anim = A_FALLDMG;
        if (++G.boss_t >= 240) phase(0);
        break;
    }
}

static void b_boot(Actor *b, Actor *t) {
    (void)t;
    int bx = FX_INT(b->x);
    G.boss_vuln = G.bst == 2 && G.boss_t < 300;
    switch (G.bst) {
    case 0:
        if (G.boss_t == 0) G.bx[0] = random_free_x(bx) ;
        go(G.bx[0], FL, 1.8f); face(b, G.bx[0]); C.anim = move_anim(b); touch(b, 1, 2.4f, 2.4f);
        if (++G.boss_t > 200 || ABSI(G.bx[0] - bx) < 6) phase(1);
        break;
    case 1:
        go(bx, FL, 0.1f); C.anim = A_MELEE;
        if (G.boss_t == 0) start_anim(b, A_MELEE, SFX_ACT_MELEE);
        if (G.boss_t == 14) {
            ring(bx, 0, 2.6f, 90, 10, 1); touch(b, 2, 2.8f, 3.2f); G.shake = 14; sound_play_sfx_at(SFX_EXPLODE, bx, FL);
            spark(bx, FL - 4, 22, 0xFF907040u, 3);
        }
        if (++G.boss_t >= 26) { phase(2); game_log("Boot stuck in goo for 10 s"); }
        break;
    case 2: {
        go(bx, FL, 0.0f); C.anim = A_FALLDMG; b->vx = 0;
        if (G.boss_t == 0) {
            int side = bx < (ARN.x0 + ARN.x1) / 2 ? 1 : -1;
            int px0 = CLAMP_X(bx + side * 70) - 24;
            plat_set(0, px0, FL - 40, 1); plat_set(1, CLAMP_X(bx + side * 118) - 24, FL - 72, 1);
            say("Platform appeared! Hit the boot now!");
        }
        if (G.boss_t == 300) { G.lv.water_y = FL - 1; sound_play_sfx(SFX_SPLASH); say("Water is rising! Climb the platforms!"); game_log("Boot: flood"); }
        if (G.boss_t > 300 && G.boss_t <= 600) G.lv.water_y = FL - 1 - (G.boss_t - 300) * 64 / 300;
        if (++G.boss_t >= 600) {
            G.lv.water_y = 0; plat_set(0, 0, 0, 0); plat_set(1, 0, 0, 0); sound_play_sfx(SFX_SPLASH);
            phase(0);
        }
        break; }
    }
}

static void b_superdrom(Actor *b, Actor *t) {
    int bx = FX_INT(b->x), by = FX_INT(b->y), tx = FX_INT(t->x), ty = FX_INT(t->y) - t->h / 2;
    int hy = FL - 70 + (int)(sinf((float)G.frame * 0.08f) * 6.f);
    switch (G.bst) {
    case 0:
        go(tx + (bx < tx ? -1 : 1) * 110, hy, 1.8f); face(b, tx); C.anim = A_IDLE;
        if (++G.boss_t > 70) phase(1);
        break;
    case 1:
        go(bx, hy, 0.4f); face(b, tx); C.anim = A_RANGED;
        if (G.boss_t == 0) start_anim(b, A_RANGED, SFX_ACT_RANGED);
        if (G.boss_t == 6 || G.boss_t == 50) { hshot(PJ_PLASMA, b, t, 2.2f, 240); sound_play_sfx_at(SFX_ZAP, bx, by); }
        if (++G.boss_t >= 72) { if (++G.bx[0] >= 2) { G.bx[0] = 0; phase(2); } else phase(0); }
        break;
    case 2:
        go(bx, FL - 6, 1.5f); C.anim = A_ACTIVATE;
        if (G.boss_t == 0) { start_anim(b, A_ACTIVATE, SFX_ACT_SUPER); game_log("SuperDrom: saw mode"); }
        if (++G.boss_t >= 26) { phase(3); G.bx[1] = 0; G.bx[2] = bx < (ARN.x0 + ARN.x1) / 2 ? 1 : -1; }
        break;
    case 3: {
        int edge = G.bx[2] > 0 ? ARN.x1 - 24 : ARN.x0 + 24;
        go(edge, FL, 3.4f); C.anim = A_SUPER; b->facing = G.bx[2];
        touch(b, 2, 3.2f, 3.0f);
        if ((G.frame & 7) == 0) spark(bx, FL - 2, 2, 0xFFFFC040u, 2);
        if (ABSI(edge - bx) < 10) { G.bx[2] = -G.bx[2]; if (++G.bx[1] >= 3) phase(4); }
        if (++G.boss_t > 600) phase(4);
        break; }
    default:
        go(bx, hy, 1.5f); C.anim = A_CLIMB;
        if (G.boss_t == 0) start_anim(b, A_CLIMB, -1);
        if (++G.boss_t >= 20) phase(0);
        break;
    }
}

static void pool_add(int x) {
    Haz *z = hz_add(HZ_POOL, x, FL); if (!z) return;
    z->a = 4; z->c = 300; z->dmg = 1;   /* кислота лежит 5 с */
}
void boss_poison(int x) { if (G.boss_kind == RIG_DRUNKY) pool_add(x); }

static int pools_active(void) { for (int i = 0; i < HZ_MAX; i++) if (G.hz[i].active && G.hz[i].type == HZ_POOL) return 1; return 0; }

static int refreezer_idx(void) { for (int i = 0; i < NUM_HEROES; i++) if (G.hero[i].kind == RIG_REFREEZER) return i; return -1; }

static void drop_artifact(int ax) {
    Obj *a = &G.lv.obj[G.lv.obj_artifact];
    if (in_hole(ax)) ax = 1300;
    a->x = (int16_t)(ax - 7); a->y = (int16_t)(FL - 18);
    a->fx_ = INT_FX(a->x); a->fy_ = INT_FX(a->y);
    a->visible = 1; a->on = 0;
    spark(ax, FL - 12, 24, 0xFF60C0FFu, 3);
}

/* Drunky Kozemekis: летает на сноуборде, бросает по 2 бутылки в секунду, раз в 5 с рыгает ядовитой жижей (лужа на полу, спасают платформы).
   Когда сил почти нет - его нужно накормить из холодильника Refreezer (E рядом), после чего он исчезает навсегда. */
static void b_drunky(Actor *b, Actor *t) {
    int bx = FX_INT(b->x), by = FX_INT(b->y), tx = FX_INT(t->x);
    touch(b, 1, 2.0f, 2.4f);
    if (G.bx[0] == 0) { G.bx[0] = 1; G.bx[4] = 300; }                      /* первый запуск */
    /* платформы есть, пока жижа на полу */
    int pools = pools_active();
    if (pools && !G.bx[6]) { G.bx[6] = 1; plat_set(0, ARN.x0 + 24, FL - 40, 1); plat_set(1, 1292, FL - 70, 1); plat_set(2, 1470, FL - 40, 1); }
    if (!pools && G.bx[6]) { G.bx[6] = 0; for (int i = 0; i < 3; i++) plat_set(i, 0, 0, 0); }
    if (b->hp <= 8 && G.bst < 3 && !G.bx[5]) { phase(3); G.bx[7] = 0; if (g_fridge < 1) g_fridge = 1; }
    switch (G.bst) {
    case 0:                                                             /* случайный полёт и бутылки */
        face(b, tx); C.anim = A_RUN;
        if (G.boss_t % 75 == 0) { G.bx[1] = irand(ARN.x0 + 30, ARN.x1 - 30); G.bx[2] = irand(ARN.ceil + 52, FL - 34); }
        go(G.bx[1], G.bx[2], 1.5f);
        if (G.boss_t % 90 == 30) { hshot(PJ_WHISKY, b, t, 2.1f, 200); sound_play_actor_at(b->kind, SFX_ACT_RANGED, bx, by); }
        G.boss_t++; G.bx[4]--;
        if (G.bx[4] <= 0) { G.bx[4] = 300; phase(1); }
        break;
    case 1:                                                             /* льёт кислоту на пол */
        go(bx, by, 0.2f); face(b, tx); C.anim = A_SUPER;
        if (G.boss_t == 0) start_anim(b, A_SUPER, SFX_ACT_SUPER);
        if (G.boss_t == 36) {
            int ax = tx; if (in_hole(ax)) ax = ax < (ARN.hole0 + ARN.hole1) / 2 ? ARN.hole0 - 30 : ARN.hole1 + 30;
            pool_add(ax); pool_add(CLAMP_X(ax + (ax < 1300 ? 90 : -90)));
            sound_play_sfx_at(SFX_SPLASH, ax, FL); game_log("Kozemekis pours acid (5 s)");
            say("Acid! Jump on the platforms!");
        }
        if (++G.boss_t >= 50) phase(2);
        G.bx[4]--;
        break;
    case 2:                                                             /* пока кислота на полу: не атакует, висит вокруг игрока */
        go(tx + (int)(cosf((float)G.frame * 0.04f) * 110.f), FL - 90 + (int)(sinf((float)G.frame * 0.08f) * 20.f), 1.4f);
        face(b, tx); C.anim = A_RUN;
        if (!pools_active() || ++G.boss_t > 420) { G.bx[4] = 300; phase(0); }
        break;
    case 3:                                                             /* выдохся: неуязвим, ждёт Refreezer */
        G.boss_vuln = 0;
        go(1300, FL - 26, 0.8f); face(b, tx); C.anim = A_FALLDMG;
        if (G.boss_t > 1500) { G.bx[5] = 1; G.bx[4] = 300; phase(0); say("Kozemekis recovered!"); break; }   /* не накормили — не виснем, бой продолжается */
        if (G.boss_t++ == 0) { say("Exhausted! Select Refreezer (6), go to him, E"); game_log("Kozemekis: low health, feed him with the Refreezer"); }
        {
            int ri = refreezer_idx();
            if (ri >= 0 && G.hero[ri].hp > 0) {
                Actor *r = &G.hero[ri];
                int near_ = ABSI(FX_INT(r->x) - bx) <= 50 && ABSI(FX_INT(r->y) - by) <= 70;
                if (near_ && ri == G.cur) {
                    if (g_fridge <= 0) { if (G.frame % 120 == 0) say("Fridge is empty! Wait for a restock"); }
                    else if (G.in.use && !G.prev.use) { phase(4); G.bx[8] = ri; break; }
                    else if (G.frame % 90 == 0) say("E: feed Kozemekis");
                }
            }
        }
        break;
    default: {                                                          /* финал: бутылки и сэндвич, исчезает навсегда */
        G.boss_vuln = 0;
        Actor *r = &G.hero[G.bx[8]];
        go(FX_INT(r->x) + 26, FX_INT(r->y) - 22, 1.4f); face(b, FX_INT(r->x)); C.anim = A_IDLE;
        G.act_t[G.bx[8]] = 2;
        if (G.boss_t % 12 == 0 && G.boss_t < 100) spark(bx - b->facing * 10, by - 20, 3, G.boss_t % 24 ? 0xFFE0A040u : 0xFF70D060u, 2);
        if (G.boss_t == 100) {
            spark(bx, by - 20, 40, 0xFFFFF0A0u, 4); spark(bx, by - 20, 20, 0xFF60E060u, 3);
            sound_play_sfx_at(SFX_EXPLODE, bx, by); G.shake = 8;
            g_fridge = 0;
            clear_effects();
            drop_artifact(bx > 0 ? MINI(ARN.x1 - 24, MAXI(ARN.x0 + 24, bx)) : 1300);
            G.boss_state = BS_DEAD; b->active = 0;
            game_revive_all();
            say("Kozemekis ate everything and vanished! The target card dropped."); game_log("Boss DRUNKY KOZEMEKIS gone forever (fed by Refreezer)");
            return;
        }
        G.boss_t++;
        break; }
    }
}

/* ===================================================================== main update */
void boss_update(void) {
    if (getenv("BOSS_DBG") && G.frame % 30 == 0 && G.boss_state != BS_SLEEP) fprintf(stderr, "boss st=%d bst=%d t=%d hp=%d x=%d vuln=%d bx2=%d bx4=%d\n", G.boss_state, G.bst, G.boss_t, G.boss.hp, FX_INT(G.boss.x), G.boss_vuln, G.bx[2], G.bx[4]);
    if (G.boss_state == BS_DEAD && G.boss_seen) { G.boss_seen = 0; G.boss_out_t = 0; sound_play_bgm(BGM_LEVEL); }   /* босс побеждён: музыка уровня */
    Actor *b = &G.boss;
    if (G.boss_state != BS_SLEEP) {                                 /* зал босса — особая комната (не Stealth Room): во время боя и после победы открыта */
        int r = level_room((ARN.x0 + ARN.x1) / 2, FL - 4);
        if (r >= 0 && r < 12) G.explored[r] = 1;
    }
    if (G.boss_state == BS_SLEEP) {
        int trigger = 0;
        {                                                            /* бой начинает только игрок; союзники в зал босса не заходят */
            Actor *h = player();
            if (h && h->active && h->hp > 0) {
                int hx = FX_INT(h->x), hy = FX_INT(h->y);
                if (hx >= ARN.trig_x0 && hx < ARN.trig_x1 && ABSI(hy - FL) <= 8 && game_boss_unlocked()) trigger = 1;
            }
        }
        if (trigger) {
            /* бой ведёт тот, кем управлял игрок до входа; его телепортирует в арену */
            int fighter = G.cur;
            if (fighter < 0 || fighter >= NUM_HEROES) fighter = 0;
            if (G.hero[fighter].hp <= 0) {
                for (int k = 0; k < NUM_HEROES; k++)
                    if (G.hero[k].active && G.hero[k].hp > 0) { fighter = k; break; }
            }
            int kind = G.boss_kind;
            /* на земле: Maestro, BigPussy, Coach, Boot; летают остальные */
            int ground = kind == RIG_MAESTRO || kind == RIG_BIGPUSSY || kind == RIG_COACH || kind == RIG_BOOTBOSS;
            actor_init(b, kind, 1, (ARN.x0 + ARN.x1) / 2, ground ? FL : ARN.ceil + 40);
            b->flying = !ground;
            /* HP: ур.N → 2*(3+N) ударов (супер=2, простая=1) */
            b->maxhp = b->hp = 2 * (3 + G.level_no);
            if (kind == RIG_DRUNKY) { b->hp = b->maxhp = 30; }
            b->facing = -1; b->flip = 0;
            G.boss_state = BS_HOVER; phase(0); G.boss_t = -30; G.boss_seen = 1; memset(G.bx, 0, sizeof G.bx);
            sound_play_bgm(BGM_BOSS); G.boss_out_t = 0;
            G.spawn_t = 1 << 28;
            for (int k = 0; k < EN_MAX; k++) G.en[k].active = 0;
            /* все союзники исчезают; остаётся только fighter */
            G.boss_hide_allies = 1;
            for (int k = 0; k < NUM_HEROES; k++) {
                Actor *o = &G.hero[k];
                /* сброс атак (Nerd super и пр.), чтобы не стреляли «в воздух» вне арены */
                o->attack_kind = AT_NONE; o->attack_t = 0; o->attack_fired = 0;
                o->spin_t = 0; o->sliding = 0; o->dash_t = 0;
                if (k == fighter) {
                    o->active = 1;
                    if (o->hp <= 0) o->hp = o->maxhp;
                    actor_teleport(o, ARN.x0 + 48, FL);
                    o->post_x = ARN.x0 + 48; o->post_y = FL;
                    o->vx = o->vy = 0; o->invuln_t = 60; o->immortal = 0;
                    o->fell = 0;
                } else {
                    o->active = 0;
                }
            }
            G.cur = fighter;
            char m[96]; snprintf(m, sizeof m, "%s! Pick a hero (1-%d)", BOSS_NAME[G.level_no - 1], NUM_HEROES);
            say(m);
            game_log("Boss fight: %s (level %d), fighter=%d", BOSS_NAME[G.level_no - 1], G.level_no, fighter);
            return;
        }
        return;
    }
    if (!b->active) return;
    if (G.boss_state != BS_DEAD) {
        /* boss.mp3 на всю битву, даже если игрок у стены/вышел из арены */
        if (player()->hp > 0 && !hero_in_arena(player())) {
            if (++G.boss_out_t == 30) {
                game_team_to(FX_INT(player()->x), FX_INT(player()->y));
                say("Out of the boss area: the team joins you");
            }
        } else G.boss_out_t = 0;
        /* музыка босса не сбрасывается до победы */
    }
    C.gx = b->x; C.gy = b->y; C.vmax = FXF(1.6); C.anim = A_IDLE;
    if (G.boss_state == BS_DEAD) {
        if (b->dead_t == 1) animator_restart(&b->anim, A_DEATH);
        b->vx = 0;
        if (FX_INT(b->y) >= FL) { b->y = INT_FX(FL); b->vy = 0; }
        G.boss_t++;
        if (G.boss_t == 70) {                                          /* artifact */
            Obj *a = &G.lv.obj[G.lv.obj_artifact];
            int ax = FX_INT(b->x);
            if (in_hole(ax)) ax = 1300;
            a->x = (int16_t)(ax - 7); a->y = (int16_t)(FL - 18);
            a->fx_ = INT_FX(a->x); a->fy_ = INT_FX(a->y);
            a->visible = 1; a->on = 0;
            spark(ax, FL - 12, 24, 0xFF60C0FFu, 3);
        }
    } else {
        Actor *t = pick_target(b);
        G.boss_vuln = 1;
        if (G.boss_t < 0) { G.boss_t++; go(FX_INT(b->x), FX_INT(b->y), 0.2f); }
        else switch (G.boss_kind) {
            case RIG_PROFESSOR: b_professor(b, t); break;
            case RIG_MAESTRO: b_maestro(b, t); break;
            case RIG_BIGPUSSY: b_bigpussy(b, t); break;
            case RIG_COACH: b_coach(b, t); break;
            case RIG_HACKER: b_hacker(b, t); break;
            case RIG_WATCHER: b_watcher(b, t); break;
            case RIG_BOOTBOSS: b_boot(b, t); break;
            case RIG_SUPERDROM: b_superdrom(b, t); break;
            case RIG_DRUNKY: b_drunky(b, t); break;
            default: break;
        }
        apply(b);
    }
    b->hit_mask = 0;
    actor_update(b, &G.lv, &ZERO_IN);
    if (G.boss_state == BS_DEAD && b->dead_t > 400) b->active = 0;
}

/* ===================================================================== level switch */
void game_set_level_no(int n) {
    if (n < 1) n = 1;
    if (n > NUM_LEVELS) n = NUM_LEVELS;
    G.level_no = n;
    /* map 1 → level 1; map 2 → level 2; map 3 → level 3+ */
    int map_id = n >= 4 ? n : (n >= 3) ? 3 : (n >= 2) ? 2 : 1;
    G.map_no = map_id;
    extern char g_assets[512];
    level_free(&G.lv);
    if (!level_load(&G.lv, g_assets, map_id)) {
        game_log("Failed to load map %d, falling back to map 1", map_id);
        level_load(&G.lv, g_assets, 1);
        G.map_no = 1;
    }
    boss_reset(); events_reset();
    memset(G.en, 0, sizeof G.en); memset(G.pj, 0, sizeof G.pj);
    G.have_key = 0; G.have_artifact = 0;
    G.spawn_t = 600; /* 10 с пауза спавна в начале любого уровня */
    G.tele = START_TELEPORTS; G.boss_out_t = 0;
    G.spawn_ground_budget = -1; G.spawn_ground_pack = 0;
    G.spawn_fly_t = 600; G.spawn_water_t = 0; G.spawn_water_budget = -1;
    G.spawn_room_last = -1; G.pl_floor_y = 0;
    G.fade_t = 45; /* fade-in из чёрного (в т.ч. level 1) */
    G.card_t = (!getenv("NULL_FRAMES") && !getenv("GAME_CMDS")) ? 150 : 0;   /* заставка Quest N */
    G.domina_stealth = 0;
    G.lift_enemy_floor = -1;
    memset(G.sprinter, 0, sizeof G.sprinter);
    for (int i = 0; i < NUM_HEROES; i++) { G.hero_gid[i] = 0; G.hero_dead_t[i] = 0; }
    for (int g = 0; g < 7; g++) {
        G.g_parent[g] = -1; G.g_active[g] = 0; G.g_leader[g] = -1; G.g_floor[g] = 0;
    }
    G.g_active[0] = 1; G.g_leader[0] = 0; G.g_parent[0] = -1;
    G.wp_n = 0; G.solo_mode = 0; G.q_presses = 1; /* старт в group (нечёт) */
    G.last_player_lift = -1;
    G.follow = 1;
    G.cam_init = 0; G.paused = 0;
    memset(G.explored, 0, sizeof G.explored);
    G.explored[level_room(G.lv.hero_x[0], G.lv.hero_y[0])] = 1;
    memset(G.ambushed, 0, sizeof G.ambushed); G.ambushed[level_room(G.lv.hero_x[0], G.lv.hero_y[0])] = 1;
    for (int r_ = 0; r_ < 12; r_++) if (G.lv.room_type[r_] == 1) G.explored[r_] = 1;   /* строго открытые комнаты из карты */
    setup_checkpoints(); /* CP после level_no + load */
    G.pl_gy = G.lv.hero_y[0];
    for (int i = 0; i < NUM_HEROES; i++) {
        Actor *h = &G.hero[i];
        int kind = h->kind > 0 ? h->kind : (i == 0 ? RIG_PENGI : i == 1 ? RIG_BUM : i == 2 ? RIG_DOORKEEPER : i == 3 ? RIG_MISTRESS : i == 4 ? RIG_NERD : i == 5 ? RIG_REFREEZER : RIG_HERO7);
        actor_init(h, kind, 0, G.lv.hero_x[i], G.lv.hero_y[i]);
        h->immortal = (i != 0);
        h->post_x = G.lv.hero_x[i]; h->post_y = G.lv.hero_y[i];
        h->hp = h->maxhp; h->active = 1;
        G.out[i] = 0; G.dying[i] = 0;
        G.lives[i] = START_LIVES;
    }
    G.cur = 0; G.hero[0].immortal = 0;
    int ncls = 3 + (n - 1); if (ncls > NUM_ENEMY_CLASSES) ncls = NUM_ENEMY_CLASSES;
    game_log("=== LEVEL %d === map %d boss: %s, enemy classes: %d (limit 7 loaded)", n, G.map_no, BOSS_NAME[n - 1], ncls);
    if (n >= 2 && ncls > 3) {
        int c = ncls - 1;
        if (enemy_class_loaded(c)) game_log("New enemy class #%d: %s (2 on scene)", c + 1, ENEMY_NAME[c]);
    }
    for (int c = 7; c < ncls; c++) if (!enemy_class_loaded(c)) game_log("  class #%d %s: IN DEVELOPMENT / NOT LOADED", c + 1, ENEMY_NAME[c]);
    char m[64]; snprintf(m, sizeof m, "Level %d: boss %s", n, BOSS_NAME[n - 1]); say(m);
    sound_stop_bgm();
    sound_play_bgm(BGM_LEVEL);
}

/* ===================================================================== drawing */
static void draw_ring(Image *fb, const Haz *z, int cx, int cy) {
    int x = z->x - cx, y = z->y - cy, r = (int)z->px;
    uint32_t col = z->dmg >= 2 ? 0xC0FF8030u : 0xC0FFF080u;
    for (int s = -1; s <= 1; s += 2) {
        if (z->dir && s != z->dir) continue;
        for (int k = 0; k < 14; k += 2) ui_rect(fb, x + s * (r - k / 3) - 1, y - 2 - k, 3, 2, col);
    }
    ui_rect(fb, x - r, y - 1, 2 * r, 1, 0x40FFF080u);
}
void boss_draw_world(Image *fb, int cx, int cy) {
    for (int i = 0; i < HZ_MAX; i++) {
        const Haz *z = &G.hz[i];
        if (!z->active) continue;
        if (z->type == HZ_RING) draw_ring(fb, z, cx, cy);
        else if (z->type == HZ_BOLT) {
            int x = z->x - cx, y0 = z->a - cy, y1 = z->b - cy;
            if (z->t < z->warn) { if ((z->t / 3) & 1) ui_rect(fb, x, y0, 1, y1 - y0, 0x80FF4040u); ui_rect(fb, x - 6, y1 - 1, 13, 2, 0x80FF4040u); }
            else if (z->t < z->warn + 8) {
                int px = x;
                for (int y = y0; y < y1; y += 6) { int nx = x + (int)(g_rand() % 9u) - 4; ui_line(fb, px, y, nx, y + 6, 0xFFE8F4FFu); ui_line(fb, px + 1, y, nx + 1, y + 6, 0x8080C0FFu); px = nx; }
            }
        } else if (z->type == HZ_LASER) {
            int on = z->t >= z->warn;
            for (int s = 0; s + 1 < z->pts; s++) {
                int ax = z->lx[s] - cx, ay = z->ly[s] - cy, bx = z->lx[s + 1] - cx, by = z->ly[s + 1] - cy;
                if (!on) { if ((z->t / 2) & 1) ui_line(fb, ax, ay, bx, by, 0x80FF4060u); continue; }
                ui_line(fb, ax, ay - 1, bx, by - 1, 0x80FF2060u); ui_line(fb, ax, ay + 1, bx, by + 1, 0x80FF2060u);
                ui_line(fb, ax, ay, bx, by, 0xFFFFE8F0u);
            }
        } else if (z->type == HZ_POOL) {
            int x0 = MAXI(ARN.x0, z->x - z->a), x1 = MINI(ARN.x1, z->x + z->a);
            ui_rect(fb, x0 - cx, z->y - 5 - cy, x1 - x0, 5, 0xB040C030u);
            ui_rect(fb, x0 - cx, z->y - 6 - cy, x1 - x0, 1, 0xC0A0F070u);
            for (int xx = x0; xx < x1; xx += 14) { int ph = (G.frame / 6 + xx) % 6; ui_rect(fb, xx - cx + ph, z->y - 8 - cy - (ph == 2), 2, 2, 0xA080FF80u); }
        } else if (z->type == HZ_CLOUD) {
            int r = MINI(z->a, 6 + z->t / 3);
            ui_disc(fb, (int)z->px - cx, (int)z->py - cy, r, 0x5060D040u);
            ui_disc(fb, (int)z->px - cx + 3, (int)z->py - cy - 2, r * 2 / 3, 0x6080F060u);
        }
    }
    if (boss_active() && G.boss_kind == RIG_DRUNKY) {                  /* вонь: зелёные волны за сноубордом */
        const Actor *b = &G.boss;
        int x = FX_INT(b->x) - cx, y = FX_INT(b->y) - cy;
        for (int k = 0; k < 7; k++) {
            int ph = (G.frame * 2 + k * 23) % 60;
            int sx = x - b->facing * (6 + ph / 2) + (int)(sinf((float)(G.frame + k * 9) * 0.2f) * 4.f), sy = y - 8 - k * 4 - ph / 6;
            ui_rect(fb, sx - 1, sy, 2, 4, (uint32_t)(150 - ph * 2) << 24 | 0x70D040u);
            ui_rect(fb, sx + 1, sy + 2, 2, 3, (uint32_t)(110 - ph) << 24 | 0x90E060u);
        }
    }
    if (boss_active() && G.boss_kind == RIG_COACH) {                    /* глаза Coach на голове, лазеры из них */
        const Actor *b = &G.boss;
        int ex = FX_INT(b->x) - cx + b->facing * 3, ey = FL - 38 - cy;
        if (G.bst == 1) {
            int x0 = ex + b->facing * 3, x1 = ex + b->facing * 230;
            int lo = x0 < x1 ? x0 : x1, hi = x0 < x1 ? x1 : x0;
            ui_rect(fb, lo, ey - 2, hi - lo, 5, 0x60FF2040u);
            ui_rect(fb, lo, ey - 1, hi - lo, 3, 0xC0FF6060u);
            ui_rect(fb, lo, ey, hi - lo, 1, 0xFFFFF0F0u);
        }
        ui_disc(fb, ex - 1, ey, 2, G.bst == 1 ? 0x90FF2020u : 0x50FF2020u); ui_rect(fb, ex - 1, ey, 1, 1, 0xFFFFE0A0u);
        ui_disc(fb, ex + 3, ey, 2, G.bst == 1 ? 0x90FF2020u : 0x50FF2020u); ui_rect(fb, ex + 3, ey, 1, 1, 0xFFFFE0A0u);
        if (G.bst == 3 && G.bx[4] > 0) {                                /* падающая полка с книгами */
            int sy = ARN.ceil + (40 - G.bx[4]) * (FL - ARN.ceil - 36) / 40 - cy, sx = FX_INT(b->x) - cx;
            ui_rect(fb, sx - 22, sy, 44, 36, 0xFF6A4020u); ui_frame(fb, sx - 22, sy, sx + 21, sy + 35, 0xFF2A1808u);
            for (int r = 0; r < 3; r++) for (int k = 0; k < 7; k++) ui_rect(fb, sx - 20 + k * 6, sy + 3 + r * 11, 5, 8, (k + r) % 2 ? 0xFFB03020u : 0xFF2060A0u);
        }
    }
    if (boss_active()) {
        const Actor *b = &G.boss;
        /* барабанная установка рисуется спрайтом Maestro (анимация super) */
        if (G.boss_kind == RIG_BOOTBOSS && G.bst == 2) {                  /* sticky goo */
            int x = FX_INT(b->x) - cx, y = FL - cy;
            ui_rect(fb, x - 36, y - 3, 72, 3, 0xC0604020u); ui_rect(fb, x - 28, y - 5, 56, 2, 0xA0704828u);
        }
    }
    if (G.lv.water_y > 0) {                                               /* water */
        int x0 = G.lv.water_x0 - cx, x1 = G.lv.water_x1 - cx, y0 = G.lv.water_y - cy, y1 = FL + 4 - cy;
        ui_rect(fb, x0, y0, x1 - x0, y1 - y0, 0x502070D0u);
        for (int x = x0; x < x1; x += 2) ui_rect(fb, x, y0 + (int)(sinf((float)(x + G.frame) * 0.2f) * 1.5f), 2, 2, 0xB0C0F0FFu);
    }
    for (int i = 0; i < NUM_HEROES; i++) {                                /* frozen heroes */
        const Actor *h = &G.hero[i];
        if (h->frozen <= 0 || h->hp <= 0) continue;
        int x0, y0, x1, y1; actor_box(h, &x0, &y0, &x1, &y1);
        ui_rect(fb, x0 - 3 - cx, y0 - 3 - cy, x1 - x0 + 7, y1 - y0 + 7, 0x80B8E8FFu);
        ui_frame(fb, x0 - 3 - cx, y0 - 3 - cy, x1 + 3 - cx, y1 + 3 - cy, 0xFFE8F8FFu);
        ui_line(fb, x0 - cx, y0 - cy + 6, x1 - cx, y1 - cy - 6, 0xA0FFFFFFu);
    }
}

void boss_draw_overlay(Image *fb, int cx, int cy) {
    if (G.dark <= 0) return;
    int dk = G.dark - (G.bx[8] > 0 ? 150 : 0); if (dk < 0) dk = 0;
    const Actor *p = player();
    int lx = FX_INT(p->x) - cx, ly = FX_INT(p->y) - p->h / 2 - cy;
    for (int y = 0; y < fb->h; y++)
        for (int x = 0; x < fb->w; x++) {
            int dx = x - lx, dy = y - ly, d2 = dx * dx + dy * dy, a = dk;
            if (d2 < 30 * 30 && dk > 60) a = dk - (dk - 60) * (30 * 30 - d2) / (30 * 30);
            fb->px[y * fb->w + x] = px_blend(fb->px[y * fb->w + x], (uint32_t)a << 24 | 0x02030Au);
        }
}
