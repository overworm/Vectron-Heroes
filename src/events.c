/* Уличный телефон: звонит всегда; ответ (E) -> случайно: подарок / телевизор / мошенник со взрывом будки */
#include <math.h>
#include "level.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "events.h"
#include "game_priv.h"
#include "sound.h"

#define PHONE_X (E.px)
#define PHONE_Y (E.py)
#define RING_FRAMES 300       /* звонит 5 секунд */
#define ABSENT_FRAMES 1800    /* потом пропадает на 30 секунд */
#define BONUS_FRAMES 600
enum { PH_RING, PH_TALK, PH_TV, PH_SCAM, PH_GONE };
enum { OUT_GOOD, OUT_TV, OUT_SCAM };

static Image g_img, g_guitar_img;
static struct {
    int st, t, out, who;
    int px, py, maxst, stage;
    int tv_on, tv_x, tv_y, tv_vy, tv_fy, tv_t, tv_lock;
    int box_on, box_x, box_y, box_vy, box_fy;
    int inv_t, nosp_t, dog_t;
    int boom_t;
    int shatter_t, shatter_x, shatter_y;
} E;
static Actor g_dog;
static int g_dog_rt, g_dog_tp, g_dog_bite, g_dog_stk, g_dog_lx;

void events_init(const char *dir) {
    char p[512];
    snprintf(p, sizeof p, "%s/phone.bmp", dir);
    if (!g_img.px) image_load_bmp(&g_img, p);
    snprintf(p, sizeof p, "%s/guitar.bmp", dir);
    if (!g_guitar_img.px) image_load_bmp(&g_guitar_img, p);
    events_reset();
}
static struct { int on, hero, dir, t, leg; float x, y; unsigned mask; int boss_hit; } GT;      /* гитара-бумеранг Nerd */
static int g_drink_ph[NUM_HEROES], g_drink_t[NUM_HEROES], g_thirst_cd, g_restock_t, g_raid_cd, g_wave_t;
static int g_boss_ph;
void events_reset(void) { g_boss_ph = 0; memset(&E, 0, sizeof E); E.px = 250; E.py = FLOOR1_Y; memset(&g_dog, 0, sizeof g_dog); g_dog_rt = g_dog_tp = g_dog_bite = g_dog_stk = 0; memset(&GT, 0, sizeof GT); memset(g_drink_ph, 0, sizeof g_drink_ph); memset(g_drink_t, 0, sizeof g_drink_t); g_thirst_cd = 600; g_restock_t = 0; g_raid_cd = 0; g_fridge = 3; }

void events_guitar_throw(int hi) {
    Actor *h = &G.hero[hi];
    if (GT.on) return;
    GT.on = 1; GT.hero = hi; GT.dir = h->facing >= 0 ? 1 : -1; GT.t = 0; GT.leg = 0; GT.mask = 0; GT.boss_hit = 0;
    GT.x = (float)(FX_INT(h->x) + GT.dir * 12); GT.y = (float)(FX_INT(h->y) - 24);
}

static int hero_of_kind(int kind) { for (int i = 0; i < NUM_HEROES; i++) if (G.hero[i].kind == kind) return i; return -1; }

void events_fridge_heal(int hi) {
    Actor *r = &G.hero[hi];
    /* короткая анимация двери — не блокируем повторный бросок гамбургера */
    G.act_t[hi] = 18;
    if (g_fridge <= 1) { say("Fridge is nearly empty, one portion is kept"); return; }
    int healed = 0;
    for (int i = 0; i < NUM_HEROES; i++) {
        Actor *h = &G.hero[i];
        if (i == hi || h->hp <= 0 || h->hp >= h->maxhp) continue;
        if (ABSI(FX_INT(h->x) - FX_INT(r->x)) < 56 && ABSI(FX_INT(h->y) - FX_INT(r->y)) < 20) { h->hp++; healed++; spark(FX_INT(h->x), FX_INT(h->y) - 20, 6, 0xFF80FF90u, 2); }
    }
    if (healed) { g_fridge--; say("Refreezer treats the team"); sound_play_sfx_at(SFX_PICKUP, FX_INT(r->x), FX_INT(r->y)); }
}

static void enemy_in_box(float x, float y, int rx, int ry, unsigned *mask, int dir, int dmg) {
    for (int k = 0; k < EN_MAX; k++) {
        Actor *e = &G.en[k];
        if (!e->active || e->hp <= 0 || (*mask & (1u << k))) continue;
        if (ABSI(FX_INT(e->x) - (int)x) <= rx + e->w / 2 && ABSI(FX_INT(e->y) - e->h / 2 - (int)y) <= ry + e->h / 2) {
            *mask |= 1u << k; hurt_enemy_pub(e, dmg, dir, FXF(2.0), FXF(2.4));
        }
    }
}

void events_hero_override(int hi, Intent *in) {
    Actor *h = &G.hero[hi];
    int ri = hero_of_kind(RIG_REFREEZER);
    if (ri < 0 || ri == G.cur || h->hp <= 0) { g_drink_ph[hi] = 0; return; }
    if (h->kind != RIG_BUM && h->kind != RIG_NERD) return;
    Actor *r = &G.hero[ri];
    int dx = FX_INT(r->x) - FX_INT(h->x), dy = FX_INT(r->y) - FX_INT(h->y);
    int busy = 0;
    for (int k = 0; k < EN_MAX; k++) if (G.en[k].active && G.en[k].hp > 0 && ABSI(FX_INT(G.en[k].x) - FX_INT(h->x)) < 220 && ABSI(FX_INT(G.en[k].y) - FX_INT(h->y)) < 40) busy = 1;
    if (g_drink_ph[hi] == 0) {
        if (busy || g_thirst_cd > 0 || g_fridge <= 1 || ABSI(dx) > 170 || ABSI(dy) > 20 || h->attack_kind != AT_NONE) return;
        g_drink_ph[hi] = 1; g_thirst_cd = 900;
    }
    if (busy || g_fridge <= 1 || ABSI(dy) > 20) { g_drink_ph[hi] = 0; return; }
    if (g_drink_ph[hi] == 1) {
        if (ABSI(dx) > 20) { in->dir = dx > 0 ? 1 : -1; }
        else {
            g_drink_ph[hi] = 2; g_drink_t[hi] = 240; h->facing = dx >= 0 ? 1 : -1;
            /* дверцу открываем только если Refreezer под ИИ — игрока не блокируем */
            if (ri != G.cur) G.act_t[ri] = 90;
        }
    } else if (g_drink_ph[hi] == 2) {
        memset(in, 0, sizeof *in); G.act_t[hi] = 2;
        h->facing = dx >= 0 ? 1 : -1;
        if (--g_drink_t[hi] <= 0) {
            g_drink_ph[hi] = 0; g_fridge--;
            if (h->hp < h->maxhp) h->hp++;
            spark(FX_INT(r->x), FX_INT(r->y) - 20, 8, 0xFFE0A040u, 2);
            sound_play_sfx_at(SFX_PICKUP, FX_INT(r->x), FX_INT(r->y));
            game_log("%s took a bottle from the fridge (%d left)", h->kind == RIG_BUM ? "Cyberbum" : "Nerd", g_fridge);
        }
    }
}

static int g_dinner_t = 0;   /* таймер «dinner time» — каждые 20 сек игрового времени */
static void fridge_update(void) {
    int ri = hero_of_kind(RIG_REFREEZER);
    if (g_thirst_cd > 0) g_thirst_cd--;
    if (g_raid_cd > 0) g_raid_cd--;
    if (g_fridge < 3) { if (++g_restock_t >= 2400) { g_restock_t = 0; g_fridge++; game_log("Fridge restocked (%d)", g_fridge); } } else g_restock_t = 0;
    if (ri < 0 || G.hero[ri].hp <= 0) return;
    Actor *r = &G.hero[ri];
    /* каждые 20 сек (1200 кадров) — +1 жизнь всем героям + 2-секундный танец */
    if (ri == G.cur) g_dinner_t = 0;                          /* под управлением игрока dinner time не запускается */
    else if (++g_dinner_t >= 1200) {
        g_dinner_t = 0;
        int added = 0;
        for (int i = 0; i < NUM_HEROES; i++) {
            if (G.hero[i].active && !G.out[i] && game_add_life(i)) added++;
        }
        if (added || 1) {
            say("dinner time and new energy");
            G.act_t[ri] = 120; /* 2 сек «танца»: facing flip в override */
            r->misc2 = 120;    /* счётчик танца */
            sound_play_sfx_at(SFX_PICKUP, FX_INT(r->x), FX_INT(r->y));
            game_log("Refreezer: dinner time — lives for the team");
        }
    }
    if (r->misc2 > 0) {
        r->misc2--;
        if ((r->misc2 / 10) % 2) r->facing = -r->facing; /* поворот влево-вправо */
        if (ri != G.cur) { /* ИИ стоит и «танцует» */ }
    }
    /* автооткрытие двери только у ИИ-Refreezer, не под управлением игрока */
    if (ri != G.cur && G.frame % 300 == 150 && r->attack_kind == AT_NONE && ABSI(r->vx) < FXF(0.3)) {
        int foes = 0, buddy = 0;                                  /* дверца - шутка: только когда рядом нет врагов и кто-то стоит у холодильника */
        for (int k = 0; k < EN_MAX; k++) if (G.en[k].active && G.en[k].hp > 0 && ABSI(FX_INT(G.en[k].x) - FX_INT(r->x)) < 320 && ABSI(FX_INT(G.en[k].y) - FX_INT(r->y)) < 80) foes = 1;
        for (int k = 0; k < NUM_HEROES; k++) if (k != ri && G.hero[k].active && G.hero[k].hp > 0 && ABSI(FX_INT(G.hero[k].x) - FX_INT(r->x)) < 48 && ABSI(FX_INT(G.hero[k].y) - FX_INT(r->y)) < 20) buddy = 1;
        if (!foes && buddy) { events_fridge_heal(ri); G.act_t[ri] = 90; }
    }
    if (ri == G.cur && g_fridge > 0 && g_raid_cd == 0) {                       /* налёт: три "качка" опустошают холодильник */
        int idx[EN_MAX], n = 0;
        for (int k = 0; k < EN_MAX; k++) {
            Actor *e = &G.en[k];
            if (!e->active || e->hp <= 0) continue;
            if (!(e->kind == RIG_SCOUT || e->kind == RIG_BODY || e->kind == RIG_MONOMAN || e->kind == RIG_HEAVY)) continue;
            if (ABSI(FX_INT(e->x) - FX_INT(r->x)) < 90 && ABSI(FX_INT(e->y) - FX_INT(r->y)) < 40) idx[n++] = k;
        }
        if (n >= 3) {
            for (int j = 0; j < 3; j++) { Actor *e = &G.en[idx[j]]; spark(FX_INT(e->x), FX_INT(e->y) - 16, 14, 0xFFE0E8F0u, 3); e->active = 0; }
            g_fridge = 0; g_restock_t = 0; g_raid_cd = 300; G.act_t[ri] = 60; G.shake = 6;
            sound_play_sfx_at(SFX_PICKUP, FX_INT(r->x), FX_INT(r->y));
            say("Three enemies emptied the fridge and ran away!"); game_log("Fridge raid: 3 enemies emptied it");
        }
    }
}

static void guitar_update(void) {
    if (GT.on) {
        Actor *h = &G.hero[GT.hero];
        GT.t++;
        float tx = (float)FX_INT(h->x), ty = (float)(FX_INT(h->y) - 22);
        if (GT.leg == 0) {
            GT.x += GT.dir * 3.4f;
            if (GT.t > 42 || level_solid_rect(&G.lv, (int)GT.x, (int)GT.y, (int)GT.x, (int)GT.y)) { GT.leg = 1; GT.mask = 0; GT.boss_hit = 0; }
        } else {
            float dx = tx - GT.x, dy = ty - GT.y, d = sqrtf(dx * dx + dy * dy);
            if (d < 9.f || GT.t > 160 || h->hp <= 0) { GT.on = 0; sound_play_sfx_at(SFX_UI, (int)tx, (int)ty); return; }
            GT.x += dx / d * 4.2f; GT.y += dy / d * 4.2f;
        }
        enemy_in_box(GT.x, GT.y, 9, 9, &GT.mask, GT.dir * (GT.leg ? -1 : 1), 1);
        /* гитара сбивает вражеские ракеты (1 шт. за проход) */
        for (int i = 0; i < PJ_MAX; i++) {
            Proj *p = &G.pj[i];
            if (!p->active || p->team != 1) continue;
            if (p->type != PJ_ROCKET && p->type != PJ_MISSILE && p->type != PJ_HOMING) continue;
            int ddx = FX_INT(p->x) - (int)GT.x, ddy = FX_INT(p->y) - (int)GT.y;
            if (ddx * ddx + ddy * ddy < 14 * 14) {
                rocket_splash_enemies(FX_INT(p->x), FX_INT(p->y));
                p->active = 0;
                spark((int)GT.x, (int)GT.y, 10, 0xFFFFA040u, 3);
                sound_play_sfx_at(SFX_EXPLODE, (int)GT.x, (int)GT.y);
                break; /* одна ракета за тик */
            }
        }
        if (GT.hero == G.cur && !GT.boss_hit && G.boss_state != BS_SLEEP && G.boss_state != BS_DEAD &&
            ABSI(FX_INT(G.boss.x) - (int)GT.x) <= 9 + G.boss.w / 2 && ABSI(FX_INT(G.boss.y) - G.boss.h / 2 - (int)GT.y) <= 9 + G.boss.h / 2) { GT.boss_hit = 1; boss_hurt(1, GT.dir); }
        if (GT.t % 6 == 0) spark((int)GT.x, (int)GT.y, 1, 0xFFE8C070u, 1);
    }
    /* суперсила Nerd: звуковая волна бьёт по этажу выше и ниже */
    for (int i = 0; i < NUM_HEROES; i++) {
        Actor *h = &G.hero[i];
        if (!h->active || h->kind != RIG_NERD || h->hp <= 0 || h->attack_kind != AT_SUPER || h->attack_special != 1) continue;
        float ph = actor_attack_phase(h);
        if (ph < 0.12f || ph > 0.96f) continue;
        if (h->attack_t % 12 == 0) {
            sound_play_sfx_at(SFX_ZAP, FX_INT(h->x), FX_INT(h->y));
            for (int k = 0; k < EN_MAX; k++) {
                Actor *e = &G.en[k];
                if (!e->active || e->hp <= 0) continue;
                if (ABSI(FX_INT(e->x) - FX_INT(h->x)) <= 190 && ABSI(FX_INT(e->y) - FX_INT(h->y)) <= 140) {
                    e->invuln_t = 0; if (hurt_enemy_pub(e, 1, FX_INT(e->x) >= FX_INT(h->x) ? 1 : -1, FXF(1.2), FXF(1.6))) spark(FX_INT(e->x), FX_INT(e->y) - 16, 5, 0xFFFFE890u, 2);
                }
            }
        }
        if (i == G.cur && h->attack_t % 24 == 0 && G.boss_state != BS_SLEEP && G.boss_state != BS_DEAD &&
            ABSI(FX_INT(G.boss.x) - FX_INT(h->x)) <= 200 && ABSI(FX_INT(G.boss.y) - FX_INT(h->y)) <= 150) boss_hurt(1, h->facing);
        /* Nerd super: ломает платформы PK_NERD (опора бака и др. на новых уровнях).
           НЕ поднимает бак уровня 1 (kind 0) — это только панель Cyberbum. */
        for (int j = 0; j < G.lv.nobj; j++) {
            Obj *o = &G.lv.obj[j];
            if (o->type != O_PLAT || o->kind != PK_NERD || o->state != 0 || !o->solid) continue;
            int cx = o->x + o->w / 2, cy = o->y;
            if (ABSI(FX_INT(h->x) - cx) > 100 || ABSI(FX_INT(h->y) - cy) > 120) continue;
            o->state = 1; o->timer = 20; /* shake → destroy */
            say("Platform shattered!");
            sound_play_sfx_at(SFX_ZAP, cx, cy);
            spark(cx, cy, 18, 0xFF80FF60u, 3);
        }
    }
}
Actor *events_decoy(void) { return E.dog_t > 0 && g_dog.active ? &g_dog : NULL; }
int events_nospawn(void) { return E.nosp_t > 0; }

int events_near(int px, int py) {
    if (E.st != PH_RING) return 0;
    return ABSI(px - PHONE_X) < 26 && ABSI(py - PHONE_Y) < 8;
}

void events_answer(int hi) {
    if (E.st != PH_RING) return;
    E.who = hi; E.st = PH_TALK; E.t = 0;
    { static const int W[9] = {OUT_GOOD, OUT_GOOD, OUT_GOOD, OUT_GOOD, OUT_GOOD, OUT_TV, OUT_TV, OUT_SCAM, OUT_SCAM};   /* 5 хороших, 2 телевизора, 2 мошенника */
      E.out = W[g_rand() % 9u]; }
    if (getenv("EV_OUT")) E.out = atoi(getenv("EV_OUT"));
    const char *nm[3] = {"gift", "TV", "scam"};
    game_log("Phone: answered by hero %d, outcome %s", hi + 1, nm[E.out]);
    sound_play_sfx_at(SFX_UI, PHONE_X, PHONE_Y);
}

static void give_bonus(void) {
    static const int BT[8] = {2, 2, 0, 1, 3, 4, 2, 2};            /* Megadog чаще: 4 из 8 */
    int b = BT[g_rand() % 8u];
    if (getenv("EV_BONUS")) b = atoi(getenv("EV_BONUS"));
    if (b == 3) { if (game_add_life(E.who)) game_log("Bonus: extra life"); else { if (G.tele < MAX_TELEPORTS) G.tele++; say("Bonus: teleport artifact"); } }
    else if (b == 4) { if (G.tele < MAX_TELEPORTS) G.tele++; say("Bonus: teleport artifact"); game_log("Bonus: teleport artifact"); }
    else if (b == 0) { E.nosp_t = BONUS_FRAMES; say("Bonus: no enemy respawn for 10 s"); game_log("Bonus: no respawn 10 s"); }
    else if (b == 1) { E.inv_t = BONUS_FRAMES; say("Bonus: invulnerable for 10 s"); game_log("Bonus: invulnerability 10 s"); }
    else {
        Actor *p = player();
        E.dog_t = BONUS_FRAMES; g_dog_rt = 60;
        actor_init(&g_dog, RIG_MEGADOG, 0, FX_INT(p->x) - 30, FX_INT(p->y));
        g_dog.hp = g_dog.maxhp = 99; g_dog.immortal = 1; g_dog.facing = 1;
        say("Bonus: friendly Megadog for 10 s"); game_log("Bonus: friendly Megadog 10 s");
    }
    sound_play_sfx_at(SFX_PICKUP, FX_INT(player()->x), FX_INT(player()->y));
    spark(E.box_x, E.box_y - 8, 16, 0xFFFFE060u, 2);
}

void events_cheat_bonus(void) { E.who = G.cur; E.box_x = FX_INT(player()->x); E.box_y = FX_INT(player()->y); give_bonus(); }

static void kill_in_radius(int x, int y, int rx, int ry) {
    for (int i = 0; i < NUM_HEROES; i++) {
        Actor *h = &G.hero[i];
        if (!h->active || h->hp <= 0) continue;
        if (ABSI(FX_INT(h->x) - x) < rx && ABSI(FX_INT(h->y) - h->h / 2 - y) < ry) { h->invuln_t = 0; h->hp = 1; hurt_hero(h, 99, FX_INT(h->x) >= x ? 1 : -1, FXF(3), FXF(4)); }
    }
}

void events_update(void) {
    Actor *pl = player();
    guitar_update(); fridge_update();
    /* бонусы */
    if (E.nosp_t > 0) { E.nosp_t--; if (G.spawn_t < 30) G.spawn_t = 30; }
    if (E.inv_t > 0) { E.inv_t--; for (int i = 0; i < NUM_HEROES; i++) if (G.hero[i].hp > 0 && G.hero[i].invuln_t < 3) G.hero[i].invuln_t = 3; }
    if (E.dog_t > 0) {
        E.dog_t--;
        Intent it; memset(&it, 0, sizeof it);
        g_dog.hp = 99; g_dog.invuln_t = 3;
        if (g_dog_rt > 0) g_dog_rt--;
        if (g_dog_tp > 0) g_dog_tp--;
        /* охота: ближайший враг (на любом этаже в пределах экрана и далее) */
        Actor *tg = NULL; int bd = 1 << 30;
        for (int i = 0; i < EN_MAX; i++) {
            Actor *e = &G.en[i];
            if (!e->active || e->hp <= 0) continue;
            int dx = ABSI(FX_INT(e->x) - FX_INT(g_dog.x)), dy = ABSI(FX_INT(e->y) - FX_INT(g_dog.y));
            int d = dx + dy * 3;
            if (dx < 700 && dy < 320 && d < bd) { bd = d; tg = e; }
        }
        int dgx = FX_INT(g_dog.x), dgy = FX_INT(g_dog.y);
        if (tg) {
            int tx = FX_INT(tg->x), dx = tx - dgx, dyy = FX_INT(tg->y) - dgy;
            g_dog.facing = dx >= 0 ? 1 : -1;
            if (ABSI(dgx - g_dog_lx) > 1 || ABSI(dx) <= 40) g_dog_stk = 0; else g_dog_stk++;
            g_dog_lx = dgx;
            if (ABSI(dyy) > 40 || blocked_between(dgx, tx, dgy - 8) || (g_dog_stk > 40 && ABSI(dx) > 40)) {   /* враг на другом этаже или за стеной: телепорт с эффектом, не чаще раза в 2 с */
                if (g_dog_tp == 0) {
                    int nx = tx + (dx >= 0 ? 1 : -1) * 30;                       /* с дальней стороны от собаки: так не остаться по ту же сторону стены */
                    if (!level_solid_rect(&G.lv, nx - 4, FX_INT(tg->y), nx + 4, FX_INT(tg->y) + 1) || level_solid_rect(&G.lv, nx - 6, FX_INT(tg->y) - 24, nx + 6, FX_INT(tg->y) - 6)) nx = tx - (dx >= 0 ? 1 : -1) * 30;
                    spark(dgx, dgy - 10, 10, 0xFF80C0FFu, 2);
                    g_dog.x = INT_FX(nx); g_dog.y = INT_FX(FX_INT(tg->y)); g_dog.vx = g_dog.vy = 0; g_dog_tp = 120;
                    spark(nx, FX_INT(tg->y) - 10, 10, 0xFF80C0FFu, 2);
                }
            } else if (ABSI(dx) > 26) it.dir = dx > 0 ? 1 : -1;
            else if (g_dog.attack_kind == AT_NONE) it.melee = 1;
            /* укус */
            if (ABSI(dx) <= 34 && ABSI(dyy) <= 30 && g_dog_bite <= 0) {
                g_dog_bite = 30;
                tg->invuln_t = 0; hurt_enemy_pub(tg, 3, dx >= 0 ? 1 : -1, FXF(2.4), FXF(2.2));
                spark(tx, FX_INT(tg->y) - tg->h / 2, 6, 0xFFFFE0A0u, 2);
            }
            /* ракета издалека */
            if (g_dog_rt <= 0 && ABSI(dx) > 60 && ABSI(dx) < 380 && ABSI(dyy) < 60) {
                g_dog_rt = 90;
                int sx = dgx + g_dog.facing * 16, sy = dgy - 14;
                int ty = FX_INT(tg->y) - tg->h / 2;
                for (int k = 1; k < 8; k++) spark(sx + (tx - sx) * k / 8, sy + (ty - sy) * k / 8, 1, 0xFFFFC040u, 2);
                rocket_splash_enemies(tx, ty);
                sound_play_sfx_at(SFX_FLAME, sx, sy);
            }
        } else if (boss_active() && ABSI(FX_INT(G.boss.x) - dgx) < 900) {   /* босс: собака кусает и его */
            int dx = FX_INT(G.boss.x) - dgx;
            g_dog.facing = dx >= 0 ? 1 : -1;
            if (ABSI(dx) > 40) it.dir = dx > 0 ? 1 : -1;
            else if (g_dog_bite <= 0) { g_dog_bite = 50; it.melee = 1; boss_hurt(1, g_dog.facing); spark(FX_INT(G.boss.x), FX_INT(G.boss.y) - 20, 6, 0xFFFFE0A0u, 2); }
        } else {                                                    /* врагов нет: держимся рядом с игроком */
            int dx = FX_INT(pl->x) - dgx;
            if (ABSI(dx) > 70) it.dir = dx > 0 ? 1 : -1;
            if (ABSI(FX_INT(pl->y) - dgy) > 60 && g_dog_tp == 0) { g_dog.x = INT_FX(FX_INT(pl->x) - 30); g_dog.y = pl->y; g_dog.vx = g_dog.vy = 0; g_dog_tp = 120; }
        }
        if (g_dog_bite > 0) g_dog_bite--;
        if (getenv("DOG_DBG") && G.frame % 30 == 0) fprintf(stderr, "dog x=%d y=%d tg=%d bite=%d act=%d anim=%d hp=%d\n", FX_INT(g_dog.x), FX_INT(g_dog.y), tg ? (int)(tg - G.en) : -1, g_dog_bite, g_dog.active, g_dog.anim.id, g_dog.hp);
        actor_update(&g_dog, &G.lv, &it);
        g_dog.team = 0; g_dog.invuln_t = 3;
        if (E.dog_t == 0) { spark(FX_INT(g_dog.x), FX_INT(g_dog.y) - 10, 14, 0xFF80C0FFu, 2); g_dog.active = 0; }
    }
    /* бой с боссом: телефон ставится в арену (у левого края), звонит через ~10 с и затем регулярно */
    if (boss_active() && !g_boss_ph && E.st != PH_TALK && E.st != PH_SCAM && !E.tv_on && !E.box_on) {
        E.px = G.lv.arena.x0 + 56; E.py = G.lv.arena.floor; g_boss_ph = 1;
        E.st = PH_GONE; E.t = ABSENT_FRAMES - 600; game_log("Phone placed in boss arena");
    }
    if (!boss_active() && g_boss_ph && G.boss_state == BS_SLEEP) g_boss_ph = 0;
    /* телефон переезжает на новый этап уровня, когда игрок прошёл предыдущий */
    { int ps = level_stage(FX_INT(pl->x)); if (ps > E.maxst) E.maxst = ps; }
    if (!g_boss_ph && E.maxst != E.stage && E.st != PH_TALK && E.st != PH_SCAM && !E.tv_on && !E.box_on) {
        static const int SX[4] = {250, 470, 1010, 1380};
        static const int SX2[4] = {250, 720, 1280, 1280}, SY2[4] = {FLOOR1_Y, FLOOR2_Y, FLOOR2_Y, FLOOR2_Y};
        int x = G.map_no == 2 ? SX2[E.maxst] : SX[E.maxst], y = G.map_no == 2 ? SY2[E.maxst] : FLOOR1_Y;
        for (int k = 0; k < 14 && !level_solid_rect(&G.lv, x - 6, y, x + 6, y + 1); k++) x += 16;
        E.px = x; E.py = y; E.stage = E.maxst;
        E.st = PH_GONE; E.t = ABSENT_FRAMES - 300;
        game_log("Phone moved to stage %d (x=%d)", E.stage + 1, x);
    }
    /* телефон */
    E.t++;
    /* во время босса и после победы телефон не звонит */
    if (G.boss_state != BS_SLEEP) {
        if (E.st == PH_RING || E.st == PH_TALK || E.st == PH_SCAM) { E.st = PH_GONE; E.t = 0; }
    }
    if (E.st == PH_RING) {
        /* звук только 2 раза, потом тишина до исчезновения */
        if (E.t == 1 || E.t == 40) sound_play_sfx_at(SFX_WARN, PHONE_X, PHONE_Y - 30);
        if (E.t >= RING_FRAMES) { E.st = PH_GONE; E.t = 0; game_log("Phone stopped ringing and disappeared"); }
    } else if (E.st == PH_TALK) {
        if (E.t > 100) {
            Actor *h = &G.hero[E.who]; int hx = FX_INT(h->x);
            if (E.out == OUT_GOOD) {
                E.box_on = 1; E.box_x = hx + (h->facing >= 0 ? 34 : -34); E.box_y = FX_INT(h->y) - 190; E.box_vy = 0; E.box_fy = FX_INT(h->y);
                E.st = PH_GONE; E.t = 0; E.boom_t = 0;
            } else if (E.out == OUT_TV) {
                E.tv_on = 1; E.tv_x = hx; E.tv_fy = FX_INT(h->y); E.tv_y = E.tv_fy - 260; E.tv_vy = 0; E.tv_t = 0; E.tv_lock = 0;
                E.st = PH_TV; E.t = 0;
                sound_play_sfx_at(SFX_WARN, hx, E.tv_fy);
            } else { E.st = PH_SCAM; E.t = 0; }
        }
    } else if (E.st == PH_SCAM) {
        if (E.t % 30 == 1) sound_play_sfx_at(SFX_WARN, PHONE_X, PHONE_Y - 30);
        if (E.t >= 120) {
            spark(PHONE_X, PHONE_Y - 28, 40, 0xFFFF8020u, 4); spark(PHONE_X, PHONE_Y - 28, 20, 0xFFFFF0A0u, 3);
            sound_play_sfx_at(SFX_EXPLODE, PHONE_X, PHONE_Y); G.shake = 14;
            kill_in_radius(PHONE_X, PHONE_Y - 20, 58, 44);
            game_log("Phone booth exploded");
            E.st = PH_GONE; E.t = 0;
        }
    } else if (E.st == PH_GONE) {
        if (E.t > ABSENT_FRAMES && !E.tv_on && !E.box_on && G.boss_state == BS_SLEEP) { E.st = PH_RING; E.t = 0; game_log("Phone is ringing again"); }
    } else if (E.st == PH_TV) { /* ждём падения */ }
    /* телевизор */
    if (E.tv_on) {
        E.tv_t++;
        Actor *h = &G.hero[E.who];
        if (E.tv_t < 45 && h->hp > 0) E.tv_x = FX_INT(h->x);        /* пока высоко - следует за героем, потом фиксируется */
        if (E.tv_t % 20 == 1 && E.tv_y + 30 < E.tv_fy) sound_play_sfx_at(SFX_WARN, E.tv_x, E.tv_fy);
        if (E.tv_t > 25) { E.tv_vy += 1; if (E.tv_vy > 14) E.tv_vy = 14; E.tv_y += E.tv_vy; }
        if (E.tv_y >= E.tv_fy) {
            E.tv_on = 0; E.shatter_t = 40; E.shatter_x = E.tv_x; E.shatter_y = E.tv_fy;
            spark(E.tv_x, E.tv_fy - 10, 30, 0xFFB0C8D8u, 3); spark(E.tv_x, E.tv_fy - 10, 14, 0xFFFFE070u, 2);
            sound_play_sfx_at(SFX_EXPLODE, E.tv_x, E.tv_fy); G.shake = 10;
            kill_in_radius(E.tv_x, E.tv_fy - 14, 22, 20);
            for (int n = 0; n < 2; n++)
                for (int k = 0; k < EN_MAX; k++) if (!G.en[k].active) {
                    int sx = FX_INT(G.hero[E.who].x) + (n ? 70 : -70);
                    if (sx < 40) sx = 40;
                    actor_init(&G.en[k], RIG_HEAVY, 1, sx, E.tv_fy); G.en[k].post_x = sx; G.en[k].facing = n ? -1 : 1; break;
                }
            game_log("TV crashed, 2 Ramrod appeared");
            E.st = PH_GONE; E.t = 0;
        }
    }
    if (E.shatter_t > 0) E.shatter_t--;
    /* коробка */
    if (E.box_on) {
        E.box_vy += 1; if (E.box_vy > 8) E.box_vy = 8;
        if (E.box_y < E.box_fy) { E.box_y += E.box_vy; if (E.box_y >= E.box_fy) { E.box_y = E.box_fy; spark(E.box_x, E.box_y, 6, 0xFFD0B070u, 2); sound_play_sfx_at(SFX_DOOR, E.box_x, E.box_y); } }
        else for (int i = 0; i < NUM_HEROES; i++) {
            Actor *h = &G.hero[i];
            if (h->hp > 0 && ABSI(FX_INT(h->x) - E.box_x) < 18 && ABSI(FX_INT(h->y) - E.box_y) < 20) { E.box_on = 0; give_bonus(); break; }
        }
    }
}

/* -------------------------------------------------------------------- рисование */
static void cloud(Image *fb, int x, int y, uint32_t col, uint32_t edge) {
    static const int cx_[] = {-22, -12, 0, 12, 22, -14, 0, 14};
    static const int cy_[] = {2, -6, -9, -6, 2, 8, 10, 8};
    static const int r_[] = {9, 11, 12, 11, 9, 8, 9, 8};
    for (int k = 0; k < 8; k++) ui_disc(fb, x + cx_[k], y + cy_[k], r_[k] + 1, edge);
    for (int k = 0; k < 8; k++) ui_disc(fb, x + cx_[k], y + cy_[k], r_[k], col);
}
static void head_good(Image *fb, int x, int y, int t) {
    ui_disc(fb, x, y, 9, 0xFFF0C890u);
    ui_rect(fb, x - 8, y - 3, 7, 4, 0xFF101010u); ui_rect(fb, x + 1, y - 3, 7, 4, 0xFF101010u); ui_rect(fb, x - 1, y - 2, 2, 1, 0xFF101010u);
    ui_rect(fb, x - 7, y - 2, 2, 1, 0xFF8090A0u);
    int open = (t / 6) % 2;
    ui_line(fb, x - 5, y + 3, x - 3, y + 6, 0xFF802020u); ui_line(fb, x - 3, y + 6, x + 3, y + 6 + open, 0xFF802020u); ui_line(fb, x + 3, y + 6 + open, x + 5, y + 3, 0xFF802020u);
}
static void head_scam(Image *fb, int x, int y, int t) {
    ui_disc(fb, x, y, 9, 0xFFC8B8A8u);
    ui_rect(fb, x - 9, y - 1, 19, 10, 0xFF202028u);                 /* маска */
    ui_rect(fb, x - 8, y - 5, 6, 3, 0xFF000000u); ui_rect(fb, x + 2, y - 5, 6, 3, 0xFF000000u);
    ui_rect(fb, x - 6, y - 4, 2, 1, 0xFFFF4040u); ui_rect(fb, x + 4, y - 4, 2, 1, 0xFFFF4040u);
    ui_rect(fb, x - 9, y - 8, 19, 3, 0xFF501010u);                  /* шапка */
    ui_rect(fb, x - 3, y + 2 + (t / 5) % 2, 7, 1, 0xFF808088u);
}

void events_draw(Image *fb, int cx, int cy) {
    int px = PHONE_X - cx, py = PHONE_Y - cy;
    if (E.st != PH_GONE || E.t < 0) {
        if (E.st != PH_GONE && g_img.px) {
            int jx = E.st == PH_RING && (G.frame / 3) % 2 ? 1 : 0;
            if (E.st == PH_SCAM) jx = (G.frame / 2) % 2 ? 2 : -2;
            ui_blit_keyed(fb, &g_img, px - g_img.w / 2 + jx, py - g_img.h, 0);
        }
        if (E.st == PH_RING) {                                       /* звонок */
            int ph = (G.frame / 6) % 3;
            for (int k = 0; k <= ph; k++) { ui_frame(fb, px - 13 - k * 3, py - 52 - k * 2, px + 13 + k * 3, py - 44 + k * 2, 0x80FFE060u - ((uint32_t)k << 26)); }
            ui_text_center(fb, px, py - 70, "RING!", (G.frame / 8) % 2 ? 0xFFFFE060u : 0xFFFFA040u);
        }
    }
    if (E.st == PH_TALK || E.st == PH_SCAM) {
        int bx = px, by = py - 92 - (E.st == PH_SCAM ? 0 : 0);
        int good = E.out == OUT_GOOD, tv = E.out == OUT_TV;
        if (E.st == PH_TALK && E.t > 8) {
            cloud(fb, bx, by, tv || !good ? 0xFFC8C8D0u : 0xFFF8F8FFu, 0xFF405060u);
            if (good) head_good(fb, bx, by, G.frame); else head_scam(fb, bx, by, G.frame);
            ui_text_center(fb, bx, by - 36, good ? "Hello! A gift for you!" : tv ? "Hello! A parcel for you!" : "Your booth is leaking...", good ? 0xFFFFF0A0u : 0xFFFFA0A0u);
        }
        if (E.st == PH_SCAM) {
            cloud(fb, bx, by, 0xFFC8C8D0u, 0xFF405060u); head_scam(fb, bx, by, G.frame);
            int left = (120 - E.t + 59) / 60;
            char b[32]; snprintf(b, sizeof b, "BOOM IN %d", left);
            ui_text_center(fb, bx, by - 36, "Ha ha! BOOM!", 0xFFFFA0A0u);
            ui_rect(fb, px - 18, py - 66, 36, 11, (G.frame / 4) % 2 ? 0xE0801010u : 0xE0300808u);
            ui_text_center(fb, px, py - 65, b, 0xFFFFFFFFu);
        }
    }
    if (E.tv_on) {
        int tx = E.tv_x - cx, ty = E.tv_y - cy, fy = E.tv_fy - cy;
        int grow = 4 + (E.tv_t > 70 ? 12 : E.tv_t / 6);
        ui_rect(fb, tx - grow, fy - 2, grow * 2, 3, 0xA0000000u);        /* тень на полу */
        if (E.tv_y + 36 > E.tv_fy - 80 || E.tv_t % 6 < 3) ui_frame(fb, tx - 22, fy - 3, tx + 22, fy, 0xFFFF4040u);
        ui_rect(fb, tx - 20, ty - 36, 40, 34, 0xFF383840u); ui_frame(fb, tx - 20, ty - 36, tx + 19, ty - 3, 0xFF9098A8u);
        ui_rect(fb, tx - 16, ty - 32, 28, 26, (G.frame / 3) % 2 ? 0xFF90B8D8u : 0xFFB0D0E8u);
        ui_rect(fb, tx + 14, ty - 30, 3, 3, 0xFFFF4040u); ui_rect(fb, tx + 14, ty - 24, 3, 3, 0xFF60FF60u);
        ui_rect(fb, tx - 12, ty - 2, 4, 3, 0xFF202028u); ui_rect(fb, tx + 8, ty - 2, 4, 3, 0xFF202028u);
        ui_line(fb, tx - 4, ty - 36, tx - 12, ty - 46, 0xFFB0B0B8u); ui_line(fb, tx + 4, ty - 36, tx + 12, ty - 46, 0xFFB0B0B8u);
    }
    if (E.shatter_t > 0) {
        int sx = E.shatter_x - cx, sy = E.shatter_y - cy, a = E.shatter_t * 6;
        for (int k = 0; k < 8; k++) ui_rect(fb, sx - 20 + k * 6, sy - 3 - (k * 5) % 4, 4, 3, ((uint32_t)a << 24) | 0x90A0B0u);
    }
    if (E.box_on) {
        int bx = E.box_x - cx, by = E.box_y - cy;
        ui_rect(fb, bx - 9, by - 16, 18, 16, 0xFFC08838u); ui_frame(fb, bx - 9, by - 16, bx + 8, by - 1, 0xFF603810u);
        ui_rect(fb, bx - 1, by - 16, 3, 16, 0xFFE03030u); ui_rect(fb, bx - 9, by - 9, 18, 3, 0xFFE03030u);
        ui_text_center(fb, bx, by - 28, "?", (G.frame / 6) % 2 ? 0xFFFFFF60u : 0xFFFFFFFFu);
    }
    if (E.dog_t > 0 && g_dog.active) {
        actor_render(fb, &g_dog, cx, cy, (G.frame / 4) % 2 ? 40 : 0);
        ui_frame(fb, FX_INT(g_dog.x) - cx - 14, FX_INT(g_dog.y) - cy - 34, FX_INT(g_dog.x) - cx + 14, FX_INT(g_dog.y) - cy - 31, 0x8060C0FFu);
    }
}

void events_draw_world(Image *fb, int cx, int cy) {
    if (GT.on && g_guitar_img.px) {
        int S = g_guitar_img.h, f = (G.frame / 2) % 8;
        Image sub = { S, S, NULL };
        static uint32_t buf[64 * 64];
        if (S <= 64) {
            for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) buf[y * S + x] = g_guitar_img.px[y * g_guitar_img.w + f * S + x];
            sub.px = buf; ui_blit_keyed(fb, &sub, (int)GT.x - cx - S / 2, (int)GT.y - cy - S / 2, 0);
        }
    }
    for (int i = 0; i < NUM_HEROES; i++) {                                   /* звуковые волны Nerd */
        const Actor *h = &G.hero[i];
        if (!h->active || h->kind != RIG_NERD || h->hp <= 0 || h->attack_kind != AT_SUPER || h->attack_special != 1) continue;
        int x = FX_INT(h->x) - cx + h->facing * 14, y = FX_INT(h->y) - cy - 20;
        for (int k = 0; k < 2; k++) {
            int r = 8 + ((G.frame * 2 + k * 20) % 50);          /* графика меньше радиуса атаки (190 px) */
            uint32_t a = (uint32_t)(200 - r * 3 / 2);
            ui_disc(fb, x, y, r, 0);
            for (int t = -50; t <= 50; t += 5) {
                float ang = (float)t * 0.01745f * 1.7f;
                ui_rect(fb, x + (int)(cosf(ang) * (float)r) * h->facing, y + (int)(sinf(ang) * (float)r * 0.9f), 3, 3, ((a + 55) << 24) | 0xFFF0A0u);
            }
        }
        for (int k = -1; k <= 1; k += 2) ui_rect(fb, x - 30 + (G.frame * 3) % 60, y + k * 70, 3, 3, 0xA0FFF0A0u);
    }
}

void events_draw_hud(Image *fb) {
    int y = 52;
    char b[48];
    if (hero_of_kind(RIG_REFREEZER) >= 0) { snprintf(b, sizeof b, "FRIDGE %d/3", g_fridge); ui_text(fb, 4, 52 + 0 * y, b, g_fridge ? 0xFFA0E0FFu : 0xFFFF8080u); y = 62; }
    if (E.inv_t > 0) { snprintf(b, sizeof b, "INVULNERABLE %d", (E.inv_t + 59) / 60); ui_text(fb, 4, y, b, 0xFFFFE060u); y += 10; }
    if (E.nosp_t > 0) { snprintf(b, sizeof b, "NO RESPAWN %d", (E.nosp_t + 59) / 60); ui_text(fb, 4, y, b, 0xFF80FFA0u); y += 10; }
    if (E.dog_t > 0) { snprintf(b, sizeof b, "MEGADOG %d", (E.dog_t + 59) / 60); ui_text(fb, 4, y, b, 0xFF80C0FFu); y += 10; }
}
