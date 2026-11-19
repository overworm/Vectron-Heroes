#include "actor.h"
#include "game_priv.h"
extern uint32_t g_rand(void);
#include "config.h"
#include "sound.h"
#include <string.h>

int g_fridge = 3;
typedef struct { int w, h; fx max_vx, jump_vy, hurt_vy, crash_vy; int hp, immune, cd_r, cd_s; } KindDef;
/* Индексируется по RigId; для будущих героев/врагов/боссов — копии ближайших. */
static const KindDef KD[RIG_COUNT] = {
    /* hurt_vy / crash_vy: ~1 рост без урона, ~2 роста = смертельное падение
       v≈sqrt(2*0.3*h): h=40 → ~4.9, h=80 → ~6.9, h=100 → ~7.7 */
    /* Прыжок героев = как у Pengi (5.6), чтобы AI мог следовать за игроком */
    /* PENGI      */ {12, 32, FXF(2.4), FXF(5.6), FXF(7.2), FXF(8.8), 5, 0,  24, 150},
    /* BUM        */ {12, 36, FXF(2.4), FXF(5.6), FXF(7.2), FXF(8.8), 5, 0,  50, 120},
    /* DOORKEEPER */ {12, 36, FXF(2.4), FXF(5.6), FXF(7.0), FXF(8.6), 5, 0,  70, 150},
    /* MISTRESS   */ {12, 40, FXF(2.4), FXF(5.6), FXF(7.2), FXF(8.8), 5, 0,   0, 150}, /* без юлы — обычное падение */
    /* SCOUT      */ {12, 40, FXF(1.6), FXF(5.0), FXF(7.2), FXF(8.6), 2, 0, 140,   0},
    /* BODY       */ {14, 40, FXF(1.5), FXF(5.0), FXF(7.2), FXF(8.6), 4, 0, 120,   0},
    /* HEAVY      */ {20, 60, FXF(1.2), FXF(5.0), FXF(7.4), FXF(9.0), 6, 0,   0,   0},
    /* BOSS       */ {16, 40, 0, 0, 0, 0, BOSS_HP, 1, 0, 0},
    /* HERO5-7    */ {12, 38, FXF(2.4), FXF(5.6), FXF(7.2), FXF(8.8), 5, 0,  70, 600},    /* Nerd */
                     {14, 30, FXF(2.4), FXF(5.6), FXF(99),  FXF(99),  6, 0,  30, 480},    /* Refreezer */
                     {16, 32, FXF(2.2), FXF(5.6), FXF(7.0), FXF(8.5), 6, 0,  70, 1200},   /* Batyscaphe */
    /* ENEMY4-12  */ {18, 32, FXF(1.4), FXF(0), FXF(7.2), FXF(8.6), 3, 0, 150, 0},
                     {18, 32, FXF(1.2), FXF(0), FXF(7.2), FXF(8.6), 5, 0, 170, 0},
                     {16, 40, FXF(1.6), FXF(0), FXF(99),  FXF(99),  4, 1, 110, 320},
                     {14, 40, FXF(1.7), FXF(0), FXF(7.2), FXF(8.6), 5, 0, 170, 0},
                     {30, 30, FXF(1.9), FXF(0), FXF(7.2), FXF(8.6), 5, 0, 120, 0},
                     {14, 44, FXF(1.0), FXF(0), FXF(7.2), FXF(8.6), 5, 0, 150, 0},
                     {20, 60, FXF(1.2), FXF(5.0), FXF(7.4), FXF(9.0), 6, 0,   0, 0},
                     {12, 40, FXF(1.6), FXF(5.0), FXF(7.2), FXF(8.6), 2, 0, 140, 0},
                     {14, 40, FXF(1.5), FXF(5.0), FXF(7.2), FXF(8.6), 4, 0, 120, 0},
    /* BOSS2-9    */ {22, 48, 0, 0, 0, 0, BOSS_HP, 1, 0, 0},      /* Maestro */
                     {26, 52, 0, 0, 0, 0, BOSS_HP, 1, 0, 0},      /* BigPussy */
                     {14, 40, 0, 0, 0, 0, BOSS_HP, 1, 0, 0},      /* Coach */
                     {30, 56, 0, 0, 0, 0, BOSS_HP, 1, 0, 0},      /* Hacker */
                     {24, 26, 0, 0, 0, 0, BOSS_HP, 1, 0, 0},      /* Watcher */
                     {40, 64, 0, 0, 0, 0, BOSS_HP, 1, 0, 0},      /* Boot */
                     {44, 28, 0, 0, 0, 0, BOSS_HP, 1, 0, 0},      /* SuperDrom */
                     {36, 36, 0, 0, 0, 0, BOSS_HP, 1, 0, 0},      /* Drunky Kozemekis (сноуборд) */
};

#define JUMP_CUT FXF(2.0)
#define SLIDE_H 14
#define SLIDE_FRAMES 45
#define SPIN_SPEED FXF(2.2)

/* бокс: колонки [px-w/2, px-w/2+w-1], строки [py-h, py-1]; ноги стоят на строке py */
static int box_solid(const Level *lv, const Actor *a, int px, int py) {
    int x0 = px - a->w / 2;
    return level_solid_rect_team(lv, x0, py - a->h, x0 + a->w - 1, py - 1, a->team);
}
static int feet_solid(const Level *lv, const Actor *a, int px, int py) {
    int x0 = px - a->w / 2;
    return level_solid_rect_team(lv, x0, py, x0 + a->w - 1, py, a->team);
}
/* юла Domina: несущие платформы / лифты / мосты = полноценные препятствия (не one-way) */
static int spin_plat_hit(const Level *lv, int x0, int y0, int x1, int y1) {
    for (int i = 0; i < lv->nobj; i++) {
        const Obj *o = &lv->obj[i];
        if (!o->solid) continue;
        if (o->type != O_PLAT && o->type != O_LIFT && o->type != O_BRIDGE) continue;
        if (x1 < o->x || x0 >= o->x + o->w || y1 < o->y || y0 >= o->y + o->h) continue;
        return 1;
    }
    return 0;
}
static int box_solid_spin(const Level *lv, const Actor *a, int px, int py) {
    int x0 = px - a->w / 2;
    int y0 = py - a->h, y1 = py - 1, x1 = x0 + a->w - 1;
    if (level_solid_rect_team(lv, x0, y0, x1, y1, a->team)) return 1; /* стены/маска */
    return spin_plat_hit(lv, x0, y0, x1, y1); /* + тело платформ/лифтов */
}

void actor_box(const Actor *a, int *x0, int *y0, int *x1, int *y1) {
    int px = FX_INT(a->x), py = FX_INT(a->y);
    *x0 = px - a->w / 2; *x1 = *x0 + a->w - 1; *y0 = py - a->h; *y1 = py - 1;
}
float actor_attack_phase(const Actor *a) { return a->attack_len > 0 ? (float)a->attack_t / (float)a->attack_len : 0.f; }
int actor_on_ground(const Actor *a, const Level *lv) { return feet_solid(lv, a, FX_INT(a->x), FX_INT(a->y)) && a->vy >= 0; }

void actor_teleport(Actor *a, int px, int py) {
    a->x = INT_FX(px) + 128; a->y = INT_FX(py); a->vx = a->vy = 0;
    a->safe_x = px; a->safe_y = py; a->on_rope = 0; a->fell = 0;
}

void actor_init(Actor *a, int kind, int team, int px, int py) {
    memset(a, 0, sizeof *a);
    const KindDef *k = &KD[kind];
    a->active = 1; a->kind = kind; a->team = team;
    a->x = INT_FX(px) + 128; a->y = INT_FX(py);
    a->w = k->w; a->h = a->full_h = k->h;
    a->facing = 1;
    a->maxhp = a->hp = k->hp;
    a->max_vx = k->max_vx; a->jump_vy = k->jump_vy; a->hurt_vy = k->hurt_vy; a->crash_vy = k->crash_vy;
    a->fall_immune = k->immune;
    a->cd_ranged_set = k->cd_r; a->cd_super_set = k->cd_s;
    if (team == 1 && !RIG_IS_BOSS(kind)) {
        a->maxhp = a->hp = 5;
        /* стрельба чаще: ~1.5 с между выстрелами, старт почти сразу */
        if (a->cd_ranged_set > 0) { a->cd_ranged_set = 180; a->cd_ranged = 30 + (int)(g_rand() % 90u); }
        a->cd_super_set = 600;
        a->cd_super = 180 + (int)(g_rand() % 420u);
    } else if (team == 0) {                                      /* герои: 4с дальняя / 10с супер */
        a->cd_ranged_set = 240; a->cd_ranged = 0;
        a->cd_super_set = kind == RIG_HERO7 ? 1200 : 600;
        a->cd_super = 0;
    }
    a->safe_x = px; a->safe_y = py;
    a->rope_obj = -1; a->stand_obj = -1;
    a->flying = RIG_IS_BOSS(kind) || kind == RIG_MONOMAN;
    a->st = ST_IDLE;
    animator_init(&a->anim, rig_get(kind), A_IDLE);
}

static fx approach0(fx v, fx f) {
    if (v > 0) { v -= f; if (v < 0) v = 0; }
    else if (v < 0) { v += f; if (v > 0) v = 0; }
    return v;
}

int actor_damage(Actor *a, int dmg, int from_dir, fx kb_x, fx kb_y, int stun) {
    if (!a->active || a->hp <= 0 || a->invuln_t > 0) return 0;
    /* волчок домины: неуязвима, юла не сбивается */
    if (a->kind == RIG_MISTRESS && a->spin_t > 0) return 0;
    if (a->kind == RIG_REFREEZER && a->team == 0 && a->dash_t > 0) return 0;     /* на разгоне холодильник неуязвим */
    if (!a->immortal) a->hp -= dmg;
    if (a->on_rope) a->soft = 0;                                 /* сбитый с каната падает по-настоящему */
    a->attack_kind = AT_NONE; a->attack_t = 0; a->on_rope = 0; a->launched = 0; a->charging = 0;
    if (a->hp <= 0) {
        a->hp = 0;
        sound_play_actor_at(a->kind, SFX_ACT_DEATH, FX_INT(a->x), FX_INT(a->y));
        if (from_dir) a->vx = from_dir * kb_x;
        a->spin_t = 0; a->sliding = 0; a->h = a->full_h; a->frozen = 0;
        if (a->kind == RIG_MONOMAN) { a->flying = 0; a->vy = 0; }
        animator_restart(&a->anim, A_DEATH);
        return 1;
    }
    sound_play_actor_at(a->kind, SFX_ACT_HURT, FX_INT(a->x), FX_INT(a->y));
    a->hurt_t = stun; a->fdmg = 0;
    a->invuln_t = a->team == 0 ? INVULN_HERO : INVULN_ENEMY + stun;
    if (from_dir) a->vx = from_dir * kb_x;
    if (kb_y > 0) a->vy = -kb_y;
    /* после удара можно упасть с края (иначе герои сами не сходят с платформы) */
    if (a->team == 0 && (from_dir || kb_y > 0)) a->soft = 50;
    animator_restart(&a->anim, A_HURT);
    return 1;
}

/* ---------- движение по пикселям: туннелирования нет ---------- */
static void move_x(const Level *lv, Actor *a, int grounded) {
    int spinning = a->kind == RIG_MISTRESS && a->spin_t > 0;
    fx tx = a->x + a->vx;
    int cur = FX_INT(a->x), tgt = FX_INT(tx), py = FX_INT(a->y), blocked = 0;
    while (cur != tgt) {
        int step = tgt > cur ? 1 : -1, nx = cur + step;
        int hit = spinning ? box_solid_spin(lv, a, nx, py) : box_solid(lv, a, nx, py);
        if (!hit) { cur = nx; continue; }
        int ok = 0;
        if (grounded && !spinning)
            for (int up = 1; up <= STEP_UP; up++)
                if (!box_solid(lv, a, nx, py - up)) { cur = nx; py -= up; ok = 1; break; }
        if (!ok) { blocked = 1; break; }
    }
    a->y = INT_FX(py) + (a->y & 255);
    if (blocked) { a->x = INT_FX(cur) + 128; a->vx = 0; }
    else a->x = tx;
}

static int move_y(const Level *lv, Actor *a, fx *impact) {
    int spinning = a->kind == RIG_MISTRESS && a->spin_t > 0;
    fx ty = a->y + a->vy;
    int px = FX_INT(a->x), cur = FX_INT(a->y), tgt = FX_INT(ty), landed = 0, blocked = 0;
    while (cur != tgt) {
        int step = tgt > cur ? 1 : -1;
        int next = cur + step;
        /* падение: ноги ловят односторонние платформы */
        if (step > 0 && feet_solid(lv, a, px, next)) {
            *impact = a->vy; a->y = INT_FX(next); a->vy = 0; return 1;
        }
        /* юла: тело не проходит сквозь платформы/лифты/стены (вверх и вниз) */
        int hit = spinning ? box_solid_spin(lv, a, px, next) : box_solid(lv, a, px, next);
        if (!hit) cur = next;
        else { blocked = 1; landed = step > 0; break; }
    }
    if (blocked) { *impact = a->vy; a->y = INT_FX(cur); a->vy = 0; }
    else {
        a->y = ty;
        if (a->vy > 0 && feet_solid(lv, a, px, FX_INT(ty))) { *impact = a->vy; a->vy = 0; landed = 1; }
    }
    return landed;
}

void actor_translate(Actor *a, const Level *lv, int dx, int dy) {
    int px = FX_INT(a->x), py = FX_INT(a->y);
    while (dx) {
        int s = dx > 0 ? 1 : -1;
        if (box_solid(lv, a, px + s, py)) break;
        px += s; dx -= s;
    }
    while (dy) {
        int s = dy > 0 ? 1 : -1;
        if (s < 0 && box_solid(lv, a, px, py + s)) break;          /* упёрся головой */
        py += s; dy -= s;
    }
    a->x = INT_FX(px) + (a->x & 255); a->y = INT_FX(py) + (a->y & 255);
}

/* Контроль провала в геометрию: вытолкнуть в ближайшее свободное место, иначе на последнюю безопасную позицию */
static void unstick(const Level *lv, Actor *a) {
    int px = FX_INT(a->x), py = FX_INT(a->y);
    if (!box_solid(lv, a, px, py)) return;
    for (int r = 1; r <= MAX_PUSH; r++) {
        int cx[5] = {0, -r, r, -r, r}, cy[5] = {-r, -r, -r, 0, 0};
        for (int k = 0; k < 5; k++)
            if (!box_solid(lv, a, px + cx[k], py + cy[k]) && (cx[k] == 0 || level_room(px + cx[k], py + cy[k]) == level_room(px, py))) {   /* выталкивание не переносит через несущую стену в соседнюю комнату */
                a->x = INT_FX(px + cx[k]) + 128; a->y = INT_FX(py + cy[k]); a->vy = 0;
                return;
            }
    }
    px = a->safe_x; py = a->safe_y;
    if (box_solid(lv, a, px, py)) {
        px = FX_INT(a->x); py = FX_INT(a->y);
        while (py > -a->h - 1 && box_solid(lv, a, px, py)) py--;
    }
    a->x = INT_FX(px) + 128; a->y = INT_FX(py); a->vx = a->vy = 0;
}

void actor_start_attack(Actor *a, int kind) {
    int anim = kind == AT_MELEE ? A_MELEE : kind == AT_RANGED ? A_RANGED : A_SUPER;
    a->attack_kind = kind; a->attack_t = 0; a->attack_fired = 0; a->hit_mask = 0;
    a->attack_len = rig_anim_dur(a->anim.rig, anim);
    animator_restart(&a->anim, anim);
    sound_play_actor_at(a->kind, kind == AT_MELEE ? SFX_ACT_MELEE : kind == AT_RANGED ? SFX_ACT_RANGED : SFX_ACT_SUPER, FX_INT(a->x), FX_INT(a->y));
}

static void try_rope(Actor *a, const Level *lv, const Intent *in, int grounded) {
    /* пингвин + враги (не босс/не герои-не пингвин? враги могут) */
    /* Pengi + враги rocketpunk/dumbpunk/ramrod могут цепляться за канат */
    int can = !a->flip && ((a->kind == RIG_PENGI) ||
        (a->team == 1 && (a->kind == RIG_SCOUT || a->kind == RIG_BODY || a->kind == RIG_HEAVY)));
    if (!can || a->on_rope || a->hurt_t > 0 || a->stun_t > 0) return;
    if (!in->up && !in->down) return;
    int px = FX_INT(a->x), py = FX_INT(a->y);
    for (int i = 0; i < lv->nobj; i++) {
        const Obj *o = &lv->obj[i];
        if (o->type != O_ROPE || !o->visible) continue;
        int cx = o->x + o->w / 2;
        if (px < cx - 14 || px > cx + 14) continue;
        if (in->up && py >= o->y - 2 && py <= o->y + o->h + 2 && (py > o->y + 2)) {
            a->on_rope = 1; a->rope_obj = i; a->vx = a->vy = 0; a->x = INT_FX(cx);
            return;
        }
        /* спуск: стоим у верха верёвки (на верхней платформе) */
        if (in->down && grounded && py >= o->y - 6 && py <= o->y + 10) {
            a->on_rope = 1; a->rope_obj = i; a->vx = a->vy = 0; a->x = INT_FX(cx); a->y = INT_FX(o->y + 4);
            return;
        }
        /* враг может схватиться с середины, если уже рядом по Y */
        if (a->team == 1 && in->down && py > o->y + 6 && py < o->y + o->h - 4) {
            a->on_rope = 1; a->rope_obj = i; a->vx = a->vy = 0; a->x = INT_FX(cx);
            return;
        }
    }
}

static void actor_update_inner(Actor *a, Level *lv, const Intent *in) {
    if (!a->active) return;
    if (a->invuln_t > 0) a->invuln_t--;
    if (a->frozen > 0) a->frozen--;
    if (a->soft > 0) a->soft--;
    if (a->hurt_t > 0) a->hurt_t--;
    if (a->stun_t > 0) a->stun_t--;
    if (a->land_t > 0) a->land_t--;
    if (a->cd_ranged > 0) a->cd_ranged--;
    if (a->cd_super > 0) a->cd_super--;
    if (a->cd_special > 0) a->cd_special--;
    if (a->spin_cd > 0) a->spin_cd--;
    if (a->hp <= 0) a->dead_t++;

    if (a->flying) {                                   /* босс: ИИ задаёт скорость и анимацию сам */
        if (a->hp > 0 || a->dead_t < 1) { a->x += a->vx; a->y += a->vy; }
        else { a->vy += FXF(0.25); if (a->vy > MAX_FALL) a->vy = MAX_FALL; a->y += a->vy; a->x += a->vx; }
        if (a->kind == RIG_MONOMAN) {                  /* летающий враг: атаки и анимацию ведёт сам актёр */
            if (a->attack_kind != AT_NONE && ++a->attack_t >= a->attack_len) { a->attack_kind = AT_NONE; a->attack_t = 0; }
            if (a->hp > 0 && a->attack_kind == AT_NONE && a->hurt_t == 0) {
                a->vx = approach0(a->vx, FXF(0.03));
                if (in->melee || in->ranged) actor_start_attack(a, AT_RANGED), a->cd_ranged = a->cd_ranged_set;
                else if (in->super && a->cd_super == 0) { actor_start_attack(a, AT_SUPER); a->cd_super = a->cd_super_set; }
            }
            if (a->hp > 0) {
                if (a->attack_kind == AT_NONE) animator_play(&a->anim, a->hurt_t > 0 ? A_HURT : ABSI(a->vx) >= FXF(1.2) ? A_RUN : ABSI(a->vx) >= FXF(0.2) ? A_WALK : A_IDLE);
                a->anim.speed = 1.f;
            }
        }
        animator_update(&a->anim);
        return;
    }

    int px = FX_INT(a->x), py = FX_INT(a->y);
    int was_grounded = a->vy >= 0 && feet_solid(lv, a, px, py);
    int grounded = was_grounded;
    int alive = a->hp > 0;
    int ctrl = alive && a->hurt_t == 0 && a->stun_t == 0;

    /* ---- канат ---- */
    if (ctrl) try_rope(a, lv, in, grounded);
    if (a->on_rope && alive) {
        const Obj *r = &lv->obj[a->rope_obj];
        int cx = r->x + r->w / 2;
        a->vx = 0; a->vy = 0; a->x = INT_FX(cx);
        a->sliding = 0; a->h = a->full_h;
        if (in->jump_pressed && !in->up) {                    /* спрыгнуть с каната */
            a->on_rope = 0; a->vy = -FXF(4.0); a->vx = (in->dir ? in->dir : a->facing) * FXF(1.6); a->coyote = 0;
        } else {
            if (in->up) a->y -= ROPE_SPEED; else if (in->down) a->y += ROPE_SPEED;
            if (FX_INT(a->y) <= r->y) {                       /* добрался до верха: перепрыгнуть на этаж */
                a->on_rope = 0; a->y = INT_FX(r->y); a->vy = -FXF(3.6); a->vx = r->dir * FXF(1.8); a->facing = r->dir;
            } else if (FX_INT(a->y) >= r->y + r->h) {          /* внизу - сойти */
                a->on_rope = 0; a->y = INT_FX(r->y + r->h); a->vy = 0;
            }
        }
        if (a->on_rope) {
            a->st = ST_CLIMB;
            animator_play(&a->anim, A_CLIMB);
            a->anim.speed = (in->up || in->down) ? 1.f : 0.f;
            animator_update(&a->anim);
            return;
        }
        px = FX_INT(a->x); py = FX_INT(a->y); grounded = 0;
    }

    if (grounded) a->coyote = COYOTE_FRAMES; else if (a->coyote > 0) a->coyote--;

    /* ---- волчок домины: до 4 секунд, неуязвима, сбивает врагов ---- */
    if (a->spin_t > 0 && alive) {
        a->spin_t--;
        /* выключить повторным K или по таймеру (урон больше не сбивает) */
        if ((in->super && a->spin_t < SPIN_FRAMES - 20) || a->spin_t == 0) {
            a->spin_t = 0; a->spin_cd = SPIN_COOLDOWN;
        }
    }
    int spinning = a->spin_t > 0;

    if (ctrl) {
        int dir = in->dir;
        int ground_lock = a->attack_kind != AT_NONE && grounded && !((a->attack_kind == AT_MELEE || a->attack_kind == AT_SUPER) && a->kind == RIG_HEAVY);
        if (ground_lock) dir = 0;
        if (a->activating) dir = 0;
        /* -- скольжение пингвина -- */
        if (a->kind == RIG_PENGI && !a->sliding && grounded && in->down && ABSI(a->vx) >= FXF(1.4) && a->attack_kind == AT_NONE) {
            a->sliding = 1; a->slide_t = SLIDE_FRAMES; a->h = SLIDE_H;
            a->facing = a->vx > 0 ? 1 : -1;
        }
        if (a->sliding) {
            if (a->slide_t > 0) a->slide_t--;
            if (a->slide_t == 0 && in->down) {                /* ползёт на брюхе */
                a->vx = dir * FXF(1.0);
                if (dir) a->facing = dir;
            } else if (a->slide_t > 0) a->vx = approach0(a->vx, FXF(0.012));
            if (!in->down && a->slide_t == 0) {
                int save_h = a->h;
                a->h = a->full_h;
                if (box_solid(lv, a, px, py)) a->h = save_h; else a->sliding = 0;
            } else if (!in->down && a->slide_t > 0 && grounded) {
                a->slide_t = 1;
            }
            if (a->sliding && in->jump_pressed) { /* из-под потолка не прыгаем */ }
        } else if (spinning) {
            fx tvx = dir * SPIN_SPEED;
            if (dir) a->facing = dir;
            if (a->vx < tvx) { a->vx += FXF(0.15); if (a->vx > tvx) a->vx = tvx; }
            else if (a->vx > tvx) { a->vx -= FXF(0.15); if (a->vx < tvx) a->vx = tvx; }
            a->vy = in->up ? -FXF(1.0) : in->down ? FXF(1.0) : 0;
        } else {
            if (a->kind == RIG_REFREEZER && a->dash_t > 0) { dir = a->facing; }
            if (dir) {
                if (a->attack_kind == AT_NONE) a->facing = dir;
                fx maxv = a->charging ? FXF(3.4) : a->dash_t > 0 ? FXF(4.4) : a->max_vx;
                fx acc = grounded ? ACCEL_GROUND : ACCEL_AIR;
                if (a->charging) acc = FXF(0.14);
                if (a->dash_t > 0) acc = FXF(0.3);
                if ((dir > 0 && a->vx < 0) || (dir < 0 && a->vx > 0)) acc *= 3;
                a->vx += dir * acc;
                if (a->vx > maxv) a->vx = maxv;
                if (a->vx < -maxv) a->vx = -maxv;
                /* защита от падения ИИ - в ai.c (hero_ai): здесь она гасила разбег прыжка и мигала на мостах/лифтах */
            } else {
                a->vx = approach0(a->vx, grounded ? FRICTION_GROUND : FRICTION_AIR);
            }
        }
        if (!a->sliding && in->jump_pressed && a->coyote > 0 && a->attack_kind == AT_NONE && !a->activating) {
            a->vy = -a->jump_vy; a->coyote = 0; grounded = 0;
            sound_play_actor_at(a->kind, SFX_ACT_JUMP, FX_INT(a->x), FX_INT(a->y));
        }
        if (!in->jump_held && a->vy < -JUMP_CUT && !spinning) a->vy = -JUMP_CUT;

        /* -- атаки -- */
        if (a->attack_kind == AT_NONE && !a->sliding && !a->activating && a->team == 0 && (a->kind <= RIG_MISTRESS || a->kind == RIG_HERO5 || a->kind == RIG_HERO6 || a->kind == RIG_HERO7)) {
            /* схема героя: Z ближняя, X дальняя (заряд 4 с), C супер (10 с), V особая */
            a->attack_special = 0;
            if (in->melee) actor_start_attack(a, AT_MELEE);
            else if (in->super) {
                if (a->kind == RIG_MISTRESS) {
                    if (!spinning && a->spin_cd == 0) {
                        a->spin_t = SPIN_FRAMES; a->attack_kind = AT_NONE; a->vy = 0;
                        animator_restart(&a->anim, A_RANGED);
                        sound_play_actor_at(a->kind, SFX_ACT_RANGED, FX_INT(a->x), FX_INT(a->y));
                    }
                } else if (a->cd_super == 0 && !spinning) {
                    if (a->kind == RIG_PENGI || a->kind == RIG_REFREEZER || a->kind == RIG_HERO7) actor_start_attack(a, AT_SUPER);
                    else { actor_start_attack(a, AT_RANGED); a->attack_special = 2; }
                    a->cd_super = a->cd_super_set;
                }
            } else if (in->ranged) {
                if (a->cd_ranged == 0 && !spinning) {
                    actor_start_attack(a, AT_RANGED);
                    /* игрок: без CD; AI-герои: 4 с */
                    if (a->team == 0) {
                        int is_pl = 0;
                        for (int hi = 0; hi < NUM_HEROES; hi++)
                            if (&G.hero[hi] == a && hi == G.cur) { is_pl = 1; break; }
                        a->cd_ranged = is_pl ? 0 : 240;
                    } else a->cd_ranged = a->cd_ranged_set;
                }
            } else if (in->special) {
                if (a->cd_special == 0 && !spinning) {
                    if (a->kind == RIG_MISTRESS || a->kind == RIG_NERD) { actor_start_attack(a, AT_SUPER); a->attack_special = 1; a->cd_special = 60; }
                    else if (a->kind != RIG_DOORKEEPER) { actor_start_attack(a, AT_RANGED); a->attack_special = 1; a->cd_special = 60; }
                }
            }
        } else if (a->attack_kind == AT_NONE && !a->sliding && !a->activating) {
            int bt = a->kind == RIG_BUM || a->kind == RIG_DOORKEEPER;       /* Cyberbum/Aunty: ключ/метла = ближняя, бутылка/снежок = супер */
            if (in->melee || (bt && in->ranged)) actor_start_attack(a, AT_MELEE);
            else if (in->super && a->kind == RIG_MISTRESS && !spinning && a->spin_cd == 0) {      /* Domina: C = юла и полёт */
                a->spin_t = SPIN_FRAMES; a->attack_kind = AT_NONE; a->vy = 0;
                animator_restart(&a->anim, A_RANGED);
                sound_play_actor_at(a->kind, SFX_ACT_RANGED, FX_INT(a->x), FX_INT(a->y));
            } else if (in->ranged && a->kind == RIG_MISTRESS && a->cd_super == 0 && !spinning) { actor_start_attack(a, AT_SUPER); a->cd_super = a->cd_super_set; }   /* X = бензопила */
            else if (in->ranged && !bt && a->kind != RIG_MISTRESS && a->cd_ranged == 0) { actor_start_attack(a, AT_RANGED); a->cd_ranged = a->cd_ranged_set; }
            else if (in->super && bt && a->cd_super == 0) { actor_start_attack(a, AT_RANGED); a->cd_super = a->cd_super_set > 0 ? a->cd_super_set : 90; }
            else if (in->super && a->kind == RIG_REFREEZER && a->team == 0 && a->cd_super == 0) {
                a->dash_t = 120; a->cd_super = a->cd_super_set;
                sound_play_actor_at(a->kind, SFX_ACT_SUPER, FX_INT(a->x), FX_INT(a->y));
            }
            else if (in->super && a->kind != RIG_MISTRESS && !bt && a->cd_super == 0 && a->cd_super_set > 0) { actor_start_attack(a, AT_SUPER); a->cd_super = a->cd_super_set; }
        }
    } else {
        a->vx = approach0(a->vx, grounded ? FRICTION_GROUND : FRICTION_AIR);
        if (a->sliding && !alive) { a->sliding = 0; a->h = a->full_h; }
    }

    /* Refreezer антигравитация: таймер; половинная гравитация; без урона от падения */
    if (a->kind == RIG_REFREEZER && a->misc > 0 && alive && a->flip) {
        a->misc--;
        a->fall_immune = 1;
        a->soft = 30;
        if (a->misc == 0) {
            /* конец режима → телепорт к игроку / старт */
            actor_set_flip(a, lv, 0);
            a->fall_immune = 0;
            a->soft = 120;
            if (a->team == 0) {
                a->vx = 0; a->vy = 0;
            }
        }
    }

    if (a->dodge_t > 0) a->dodge_t--;
    if (a->dash_t > 0) { a->dash_t--; if (!alive || a->hurt_t > 0) a->dash_t = 0; }
    if (a->attack_kind != AT_NONE) {
        if (++a->attack_t >= a->attack_len) { a->attack_kind = AT_NONE; a->attack_t = 0; }
    }

    int wet = lv->water_y > 0 && px >= lv->water_x0 && px <= lv->water_x1 && py - a->h / 2 > lv->water_y && py <= lv->arena.floor + 6 && alive;
    int magnet = 0; /* старый магнит снят */
    int antigrav = (a->kind == RIG_REFREEZER && a->misc > 0 && a->flip);
    if (spinning) { /* без гравитации */ }
    else if (antigrav) {
        /* половинная гравитация (в зеркале это «падение» к потолку) */
        if (grounded) a->vy = 0;
        else { a->vy += GRAVITY / 2; if (a->vy > MAX_FALL / 2) a->vy = MAX_FALL / 2; }
    }
    else if (wet) {                                           /* вода: плавают клавишами вверх/прыжок */
        a->vy += FXF(0.07);
        if (ctrl && in->jump_pressed) a->vy = -FXF(2.1);
        else if (ctrl && in->up) a->vy -= FXF(0.16);
        if (ctrl && in->down) a->vy += FXF(0.10);
        if (a->vy < -FXF(2.2)) a->vy = -FXF(2.2);
        if (a->vy > FXF(1.0)) a->vy = FXF(1.0);
        fx lim = a->max_vx * 3 / 4;
        if (a->vx > lim) a->vx = lim; else if (a->vx < -lim) a->vx = -lim;
    }
    else if (grounded) a->vy = 0;
    else { a->vy += GRAVITY; if (a->vy > MAX_FALL) a->vy = MAX_FALL; }

    move_x(lv, a, grounded && !spinning && !magnet);
    fx impact = 0;
    int landed = move_y(lv, a, &impact);
    /* юла + касание платформы → юла OFF, soft-landing (высота падения = 0), stealth снаружи */
    int spin_land = 0;
    if (spinning && alive && a->spin_t < SPIN_FRAMES - SPIN_GRACE && feet_solid(lv, a, FX_INT(a->x), FX_INT(a->y))) {
        a->spin_t = 0;
        a->spin_cd = SPIN_COOLDOWN;
        a->vy = 0;
        spinning = 0;
        landed = 1;
        spin_land = 1;
        /* юла гасит падение: сброс счётчика высоты + короткая soft-защита */
        a->safe_x = FX_INT(a->x);
        a->safe_y = FX_INT(a->y);
        a->soft = 20;
        a->launched = 0;
    }
    if (spinning && a->vy == 0) landed = 0;

    /* ---- приземление: смерть если высота > ~2 ростов; приземление с юлы всегда безопасно ---- */
    if (landed && !was_grounded && alive && !wet && !antigrav) {
        int dmged = 0;
        int fall_h = py - a->safe_y; /* y растёт вниз */
        if (fall_h < 0) fall_h = 0;
        int death_h = a->h * 2 + 16;
        int immune_fall = a->fall_immune || spin_land
            || (a->kind == RIG_MISTRESS && a->spin_t > 0);
        if (spin_land) {
            /* юла = мягкое приземление, без смерти */
            a->land_t = 8;
            sound_play_actor_at(a->kind, SFX_ACT_LAND, FX_INT(a->x), FX_INT(a->y));
        } else if (a->launched) {
            a->launched = 0; a->invuln_t = 0;
            a->hp = 0; a->vx = 0; a->fell = 1; /* респаун на safe_x/y */
            animator_restart(&a->anim, A_DEATH); dmged = 1;
            sound_play_actor_at(a->kind, SFX_ACT_DEATH, FX_INT(a->x), FX_INT(a->y));
        } else if (a->soft == 0 && !immune_fall && fall_h > death_h) {
            a->hp = 0; a->vx = 0; a->fell = 1; /* респаун на краю платформы */
            animator_restart(&a->anim, A_DEATH); dmged = 1;
            sound_play_actor_at(a->kind, SFX_ACT_DEATH, FX_INT(a->x), FX_INT(a->y));
        }
        if (!dmged && !spin_land && impact > FXF(2.5)) {
            a->land_t = 10;
            sound_play_actor_at(a->kind, SFX_ACT_LAND, FX_INT(a->x), FX_INT(a->y));
        }
    }

    unstick(lv, a);
    px = FX_INT(a->x); py = FX_INT(a->y);
    if (alive && a->kind != RIG_HERO7 && lv->water_y > 0 && px >= lv->water_x0 && px <= lv->water_x1 && py <= lv->arena.floor + 6 && py - a->h + 4 > lv->water_y) {   /* голова под водой: тонем */
        if (++a->drown_t > 200) { a->drown_t = 140; a->invuln_t = 0; actor_damage(a, 1, 0, 0, 0, 0); }
    } else a->drown_t = 0;
    a->stand_obj = -1;
    if (feet_solid(lv, a, px, py) && !box_solid(lv, a, px, py)) {
        if (a->vy >= 0) a->vy = 0;
        int so = level_stand_obj(lv, px - a->w / 2, px - a->w / 2 + a->w - 1, py);
        a->stand_obj = so;
        /* любая опора: точка респауна и отсчёт высоты падения */
        if (a->hp > 0 && (so < 0 || (so >= 0 && (lv->obj[so].type == O_BRIDGE || lv->obj[so].type == O_PLAT ||
            lv->obj[so].type == O_LIFT)))) {
            a->safe_x = px; a->safe_y = py;
        }
    }
    /* бесконечная пропасть за картой — всегда смерть */
    if (py > lv->h + 40 && a->hp > 0) {
        a->fell = 1; a->hp = 0; animator_restart(&a->anim, A_DEATH);
        sound_play_actor_at(a->kind, SFX_ACT_DEATH, px, py);
    }
    if (py > lv->h + 240) { a->y = INT_FX(lv->h + 240); a->vy = 0; }

    /* ---- состояние -> анимация ---- */
    int vxa = ABSI(a->vx);
    int air = !(feet_solid(lv, a, px, py) && a->vy >= 0);
    ActorState st;
    int anim;
    if (a->hp <= 0) { st = ST_DEAD; anim = A_DEATH; }
    else if (a->stun_t > 0 && a->fdmg) { st = ST_FALLDMG; anim = A_FALLDMG; }
    else if (a->hurt_t > 0) { st = ST_HURT; anim = A_HURT; }
    else if (a->attack_kind) { st = ST_ATTACK; anim = a->attack_kind == AT_MELEE ? A_MELEE : a->attack_kind == AT_RANGED ? A_RANGED : A_SUPER; }
    else if (a->sliding) { st = ST_SLIDE; anim = A_SLIDE; }
    else if (a->dash_t > 0) { st = ST_RUN; anim = A_SUPER; }
    else if (a->spin_t > 0) { st = ST_SPIN; anim = A_GLIDE; }
    else if (a->activating && !air) { st = ST_ACTIVATE; anim = (a->kind == RIG_REFREEZER && g_fridge > 0) ? A_CLIMB : A_ACTIVATE; }
    else if (air) { if (a->vy > FXF(1.0)) { st = ST_FALL; anim = A_FALL; } else { st = ST_JUMP; anim = A_JUMP; } }
    else if (a->land_t > 0) { st = ST_LAND; anim = A_LAND; }
    else if (vxa >= RUN_THRESH) { st = ST_RUN; anim = A_RUN; }
    else if (vxa >= WALK_THRESH) { st = ST_WALK; anim = A_WALK; }
    else { st = ST_IDLE; anim = A_IDLE; }
    a->st = st;
    if ((st == ST_WALK || st == ST_RUN) && !air) {             /* шаг каждые ~26 px пути */
        a->stepc += vxa;
        if (a->stepc >= FXF(26)) { a->stepc = 0; sound_play_actor_at(a->kind, SFX_ACT_STEP, px, py); }
    } else a->stepc = 0;
    if (!a->own_anim) animator_play(&a->anim, anim);               /* босс сам выбирает анимацию (apply в boss.c) */
    float sp = 1.f;
    if (st == ST_WALK) { sp = (float)vxa / (float)FXF(1.2); if (sp < 0.5f) sp = 0.5f; if (sp > 1.6f) sp = 1.6f; }
    if (st == ST_RUN)  { sp = (float)vxa / (float)FXF(2.2); if (sp < 0.8f) sp = 0.8f; if (sp > 1.4f) sp = 1.4f; }
    if (a->own_anim) sp = 1.f;
    a->anim.speed = sp;
    animator_update(&a->anim);
    a->activating = 0;
}

/* Обновление с учётом перевёрнутой гравитации: физика идёт в зеркальном мире (y' = M + 1 + h - y), снаружи координаты настоящие. */
static void actor_update_impl(Actor *a, Level *lv, const Intent *in);
/* Domina в юле пролетает сквозь мини-платформы босс-зала (особый класс платформ) */
void actor_update(Actor *a, Level *lv, const Intent *in) {
    uint8_t sv[3] = {0, 0, 0}; int ghost = a->kind == RIG_MISTRESS && a->spin_t > 0 && a->team == 0;
    if (ghost) for (int i = 0; i < 3; i++) { int k = lv->obj_bplat[i]; if (k >= 0) { sv[i] = lv->obj[k].solid; lv->obj[k].solid = 0; } }
    actor_update_impl(a, lv, in);
    if (ghost) for (int i = 0; i < 3; i++) { int k = lv->obj_bplat[i]; if (k >= 0) lv->obj[k].solid = sv[i]; }
}
static void actor_update_impl(Actor *a, Level *lv, const Intent *in) {
    if (a->active && a->flip && !a->flying) {
        int M = lv->mirror_m;
        a->y = INT_FX(M + 1 + a->h - FX_INT(a->y)) + (a->y & 255);
        level_set_mirror(1, M);
        actor_update_inner(a, lv, in);
        level_set_mirror(0, 0);
        a->y = INT_FX(M + 1 + a->h - FX_INT(a->y)) + (a->y & 255);
    } else actor_update_inner(a, lv, in);
}

void actor_set_flip(Actor *a, Level *lv, int on) {
    on = on != 0;
    if (!a->active || a->flip == on) return;
    a->soft = 600;
    a->flip = on;
    a->on_rope = 0; a->sliding = 0; a->h = a->full_h; a->vy = 0; a->coyote = 0; a->spin_t = 0;
    if (on) {
        /* центр зеркала - по комнате героя: потолок = нижняя грань плиты над этажом (F-112), M = 2F-113; в зале босса - по арене */
        {
            int py = FX_INT(a->y), px = FX_INT(a->x);
            const Arena *ar = &lv->arena;
            int in_arena = px >= ar->trig_x0 && px <= ar->trig_x1 && py >= ar->ceil && py <= ar->floor + 10;
            int F = FLOOR4_Y;
            if (py > FLOOR4_Y + 6) F = FLOOR3_Y;
            if (py > FLOOR3_Y + 6) F = FLOOR2_Y;
            if (py > FLOOR2_Y + 6) F = FLOOR1_Y;
            lv->mirror_m = in_arena ? ar->ceil + ar->floor - 1 : 2 * F - 113;
        }
        a->safe_x2 = a->safe_x; a->safe_y2 = a->safe_y;
        a->safe_x = FX_INT(a->x); a->safe_y = lv->mirror_m + 1 + a->h - FX_INT(a->y);
    } else { a->safe_x = a->safe_x2; a->safe_y = a->safe_y2; lv->mirror_m = lv->arena.ceil + lv->arena.floor - 1; }
}
