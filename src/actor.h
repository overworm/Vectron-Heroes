#ifndef ACTOR_HDR_H
#define ACTOR_HDR_H
#include "types.h"
#include "level.h"
#include "rig.h"

typedef enum {
    ST_IDLE, ST_WALK, ST_RUN, ST_JUMP, ST_FALL, ST_LAND, ST_CLIMB, ST_SLIDE, ST_SPIN,
    ST_ATTACK, ST_ACTIVATE, ST_HURT, ST_FALLDMG, ST_DEAD
} ActorState;

enum { AT_NONE, AT_MELEE, AT_RANGED, AT_SUPER };

/* Намерение: его формирует игрок (клавиатура) или ИИ. melee/ranged/super - нажатие (фронт), use - удержание */
typedef struct { int dir, up, down, jump_pressed, jump_held, melee, ranged, super, use, special; } Intent;

typedef struct {
    int active, kind, team;          /* kind = RigId; team 0 = герои, 1 = враги */
    int immortal;                    /* герои не под управлением игрока */
    int flying;                      /* босс: без гравитации и коллизий, скорость задаёт ИИ */
    fx x, y, vx, vy, max_vx;         /* x - центр, y - уровень ног */
    int w, h, full_h;                /* текущий и полный бокс */
    int facing, hp, maxhp;
    fx jump_vy, hurt_vy, crash_vy;
    int fall_immune;
    ActorState st;
    Animator anim;
    int attack_kind, attack_t, attack_len, attack_fired, hit_mask;
    int cd_ranged, cd_super, cd_ranged_set, cd_super_set;
    int cd_special, attack_special;              /* V: перезарядка; 0 обычная атака, 1 = V, 2 = супер через анимацию дальнего */
    int ai_jump_t, ai_jump_dir; int hurt_t, invuln_t, dodge_t, dead_t, stun_t, fdmg, coyote, land_t;
    int on_rope, rope_obj, sliding, slide_t, spin_t, spin_cd, spin_hit_t;
    int launched, charging, activating;
    int safe_x, safe_y, fell;
    int ai_t, ai_state, ai_aux, post_x, post_y, ai_blk, runup, mag_air;
    int nav_mode, nav_dir, nav_t, stuck_t, stuck_x, unr_t, own_anim, aggro_h, aggro_t;          /* навигация: 0 идти к цели, 1 уйти к краю, 2 сменить этаж */
    int stepc;                                  /* накопленный путь для звука шагов */
    int stand_obj;
    int flip;                                   /* 1: гравитация перевёрнута (босс Coach): физика в зеркальном мире */
    int frozen;                                 /* кадры заморозки (Watcher): управлять нельзя */
    int drown_t;                                /* сколько кадров голова под водой */
    int tele_wait;                              /* враг: ждёт 4 с перед телепортом на этаж игрока */
    int charge;                                 /* заряд оружия (кадры) */
    int misc, misc2, aim;                       /* свои счётчики ИИ врагов: таран/промахи/угол огнемёта */
    int dash_t;                                 /* Refreezer: кадры разгона (неуязвим, сносит врагов) */
    int soft;                                   /* кадры 'мягких падений': разбиться насмерть нельзя, только ушиб */
    int safe_x2, safe_y2;                       /* настоящая безопасная точка на время переворота */
} Actor;

extern int g_fridge;                                                             /* запас холодильника Refreezer (0..3) */
void actor_init(Actor *a, int kind, int team, int px, int py);
void actor_update(Actor *a, Level *lv, const Intent *in);
int  actor_damage(Actor *a, int dmg, int from_dir, fx kb_x, fx kb_y, int stun);   /* 1 если урон нанесён */
void actor_translate(Actor *a, const Level *lv, int dx, int dy);                 /* перенос лифтом/платформой */
void actor_box(const Actor *a, int *x0, int *y0, int *x1, int *y1);              /* бокс в мире, включительно */
float actor_attack_phase(const Actor *a);                                        /* 0..1 прогресс атаки */
int  actor_on_ground(const Actor *a, const Level *lv);
void actor_teleport(Actor *a, int px, int py);
void actor_start_attack(Actor *a, int kind);
void actor_set_flip(Actor *a, Level *lv, int on);                            /* переворот гравитации (центр зеркала берётся из lv->mirror_m) */

#endif
