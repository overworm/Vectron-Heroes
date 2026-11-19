#ifndef GAME_PRIV_H
#define GAME_PRIV_H
/* Внутреннее состояние игры. Делится между game.c (герои, головоломки, камера, рисование) и ai.c (враги, босс, снаряды). */
#include "actor.h"
#include "config.h"
#include "level.h"
#include "platform.h"
#include "rig.h"
#include "ui.h"

#define EN_MAX 8            /* слоты врагов (живых не больше ENEMY_CAP, остальные - трупы) */
#define PJ_MAX 48
#define FX_MAX 96
#define NCARS 11

enum { PJ_PLASMA, PJ_BIG, PJ_BOTTLE, PJ_SNOW, PJ_ROCKET, PJ_DUMBBELL, PJ_BOLT,
       PJ_PIZZA, PJ_GREEN, PJ_SNOWBIG, PJ_HOMING, PJ_PELLET, PJ_GAS, PJ_MISSILE, PJ_WHISKY,
       PJ_FIRE, PJ_FIREBOT, PJ_COMPUTER, PJ_BOULDER, PJ_PIANO, PJ_ROCKETB, PJ_LASER, PJ_BOOK };
enum { HZ_RING, HZ_BOLT, HZ_CLOUD, HZ_LASER, HZ_POOL };                /* опасные зоны боссов: кольцо волны, молния сверху, облако газа */
enum { BS_SLEEP, BS_HOVER, BS_SHOOT, BS_DIVE, BS_REST, BS_DEAD };
enum { MODE_PLAY, MODE_WIN, MODE_END };

typedef struct { int active, type, team, from_player, life, dmg, dir; fx x, y, vx, vy, g; int aux; } Proj;
typedef struct { int active, type, x, y, t, a, b, c, dmg, flag, dir, hgt, warn, pts; int lx[10], ly[10]; float px, py, vx, vy; } Haz;
#define HZ_MAX 24
#define NUM_LEVELS 9
#define NUM_ENEMY_CLASSES 12
typedef struct { int active, life, max; fx x, y, vx, vy; uint32_t col; int size; } Spark;

typedef struct {
    Level lv;
    Actor hero[NUM_HEROES];
    Actor en[EN_MAX];
    Actor boss;
    Proj pj[PJ_MAX];
    Spark sp[FX_MAX];
    int follow;                      /* Q: союзники идут за игроком */
    int cur;                         /* кем управляет игрок */
    int mode, win_t;
    int frame, spawn_t;
    int have_key, have_artifact, boss_seen, boss_state, boss_t, boss_shots;
    int act_t[NUM_HEROES];           /* кадры анимации "активация" после нажатия */
    int respawn_t[NUM_HEROES];
    int cam_x, cam_y, cam_init;
    int rescue_req[NUM_HEROES];        /* ИИ просит вертолёт/лифт (зациклился в прыжках) */
    int pl_rope_t;                     /* кадры после схода игрока с верёвки (верёвка = штатный подъём) */
    int lost_total[NUM_HEROES];        /* сколько кадров герой потеряшка подряд (эскалация возвращения) */
    int pl_gy;                         /* y последней опоры игрока в открытой комнате (цепочка исследования) */
    int pl_floor_y;                    /* устойчивый этаж игрока (последняя опора)*/
    int shake;
    Input in, prev;
    char msg[96]; int msg_t;
    int debug;
    int start_x, start_y;
    Image portrait[NUM_HEROES];
    Image car[NCARS]; int car_n, car_x[NCARS], car_y[NCARS], car_w[NCARS], car_v[NCARS];
    Image fb;
    uint32_t rng;
    int stat_kills;
    int lives[NUM_HEROES], out[NUM_HEROES], dying[NUM_HEROES];   /* жизни героев (у каждого свои) */
    int team_kills, tele, boss_out_t;   /* legacy / артефакты телепорта / игрок вне арены босса */
    int player_kills, ally_kills;       /* независимые фраги: игрок /4, союзники /10 */
    int group_t;                       /* кадры режима группы (макс 10 сек = 600) */
    int whip_t, whip_hi;               /* кнут Domina: кадры показа / владелец */
    int stealth_t;                     /* кадров подряд в stealth/спецкомнате (лимит STEALTH_LIMIT) */
    int stealth_cool;                  /* гистерезис: после stealth группа не включается сразу */
    int lift_call_t;                   /* Q на лифте: 1 сек принудительный созыв всех на лифт */
    int boss_hide_allies;              /* 1: союзники скрыты на арене босса */
    int help_flag[NUM_HEROES];         /* герой в беде — красная окантовка */
    int full_respawns;                 /* сколько полных рестартов уровня уже было */
    int gameover_t;                    /* >0: GAME OVER, ждём конец музыки */
    int paused;                        /* 1: пауза (клавиша P) */
    int card_t, end_t;                 /* заставка «Quest N» / эпилог */
    int prolog_t;                      /* >0: показываем пролог */
    uint8_t explored[12];              /* открытые (штатно исследованные) комнаты 0..11 */
    /* спавн: открытая vs закрытая комната (spawn_t уже выше) */
    uint8_t ambushed[12]; int wave_t;   /* засада на новом stage; длительность волны */
    int spawn_ground_budget;           /* открытая: остаток наземных в волне (2×герои) */
    int spawn_ground_pack;             /* сколько наземных в текущей пачке (до 3) */
    int spawn_fly_t;                   /* таймер летающих */
    int spawn_water_t;                 /* таймер подводных */
    int spawn_water_budget;            /* открытая: бюджет подводных */
    int spawn_room_last;               /* последняя комната спавна */
    /* уровни/боссы */
    int level_no, map_no;            /* номер уровня 1..9 (набор врагов и босс) и номер карты 1..3 */
    int boss_kind, boss_vuln;        /* RigId босса; 1 - удары сейчас засчитываются */
    int bst, bx[10];                 /* внутренняя фаза и счётчики босса */
    int dark;                        /* 0..235 затемнение (Coach) */
    int flip_on;                     /* гравитация перевёрнута */
    Haz hz[HZ_MAX];
    int cls_cd[12];                  /* таймеры повторного появления классов (Megadog, Capsulo) */
    char logl[5][112]; int logt[5];  /* игровой лог на экране */
    struct { int on, t, x, y, x0, mask, load; } heli;        /* вертолёт: высаживает потеряшек к игроку */
    struct { int on, t, x, y, mask, n; } portal;       /* портал артефакта телепорта: герои выходят по одному */
    int lost_t[NUM_HEROES];                            /* сколько кадров герой отстал от игрока в режиме группы */
    int approach_dir;                                  /* −1/1: сторона, откуда шёл игрок */
    int fade_t;                                        /* >0: fade-in после старта уровня */
    int domina_stealth;                                /* Domina юла/стена: Q/T/group lock */
    int lift_enemy_floor;                              /* y этажа после Q-лифта (−1 нет) */
    int sprinter[NUM_HEROES];                          /* 1: sprinter_anchor */
    int hero_gid[NUM_HEROES];
    int g_parent[7];
    int g_floor[7];
    int g_active[7];
    int g_leader[7];
    /* waypoints пути игрока */
    int wp_x[48], wp_y[48], wp_lift[48];
    int wp_n;
    int solo_mode;                     /* чётный Q */
    int q_presses;
    int hero_dead_t[NUM_HEROES];
    int hero_dead_x[NUM_HEROES], hero_dead_y[NUM_HEROES];
    int hero_fall_t[NUM_HEROES]; /* frame последнего падения */
    int last_player_lift;
    int cp_stage; /* index of last unlocked checkpoint */
    int cp_n;     /* how many CP defined for level */
    int cp_x[8], cp_y[8]; /* checkpoint world coords */
    int debug_quest; /* F8 / settings.ini: подсветка квест-триггеров */
    int debug_log;  /* settings.ini debug_log=0/1: экранный game_log */
    int bridge_tp_t;  /* Domina auto-TP на мост: T запрещён */
    int bridge_tp_x, bridge_tp_y;
} Game;

extern Game G;

/* game.c */
uint32_t g_rand(void);
void say(const char *utf8);
void spark(int x, int y, int n, uint32_t col, int size);
Actor *player(void);
int  wall_between(int x0, int x1, int y);                      /* любая стена на уровне тела */
int  same_platform(int x0, int x1, int y);                    /* непрерывная опора между точками */
int  blocked_between(int x0, int x1, int y);                 /* закрытая стена или ворота между двумя x на высоте y */
void hit_feedback(const Actor *a, int big);
void break_wall(Obj *o);
int  melee_box_hit(const Actor *att, const Actor *t, int reach);
Actor *nearest_hero(const Actor *from, int max_dx, int max_dy, int need_alive);
void actor_render(Image *fb, const Actor *a, int cx, int cy, int flash);

/* ai.c */
void ai_init(void);
void enemies_update(void);
void boss_update(void);
void projectiles_update(void);
void spawn_add(int force_near);
void fire_proj(int type, int team, int from_player, int x, int y, int dir, int dmg);
void hero_attack_effects(int hi);        /* удар/выстрел/суператака героя: попадания и снаряды */
void hero_ai(int hi, Intent *in);        /* напарник: охраняет свой пост */
int  enemies_alive(void);
int  hurt_enemy_pub(Actor *e, int dmg, int dir, fx kx, fx ky);
int  hurt_hero(Actor *h, int dmg, int dir, fx kx, fx ky);
void team_kill(void);                      /* legacy: считается как ally kill */
void team_kill_from(int from_ai);          /* 0=игрок, 1=союзник */
int  game_add_life(int hi);                /* +1 жизнь герою (до MAX_LIVES); возвращает 1 если добавлена */
void game_teleport_team(void);             /* все живые герои - к игроку */
void game_revive_all(void);
int game_ground_near(int x, int y);
int game_boss_unlocked(void);                /* смерть босса: воскресить всех */
void game_team_to(int x, int y);           /* перенести всех живых героев (кроме игрока) в точку */
Proj *fire_projx(int type, int team, int x, int y, float vx, float vy, int dmg, int life);
void rocket_splash(int x, int y);
void rocket_splash_enemies(int x, int y);
void laser_fire(int x, int y, int dirx, int diry);
extern const char *const ENEMY_NAME[NUM_ENEMY_CLASSES];
extern const int ENEMY_RIG[NUM_ENEMY_CLASSES];
int  enemy_class_loaded(int cls);
void enemy_draw_extra(Image *fb, int cx, int cy);   /* огонь Firewalker, цепь Ironfem */

/* boss.c */
extern const char *const BOSS_NAME[NUM_LEVELS];
extern const int BOSS_RIG[NUM_LEVELS];
void boss_reset(void);                   /* убрать босса и все его эффекты (смена уровня, победа) */
int  boss_hurt(int dmg, int dir);
void boss_on_button(int obj);            /* нажата временная кнопка босса */
void hazards_update(void);
void boss_draw_world(Image *fb, int cx, int cy);   /* волны, молнии, облака, вода */
void boss_draw_overlay(Image *fb, int cx, int cy); /* затемнение с глазами */
int  boss_active(void);
void boss_poison(int x);                  /* Kozemekis: лужа ядовитой жижи на полу */
void game_log(const char *fmt, ...);
void game_set_level_no(int n);
void setup_checkpoints(void);
void enemies_gather_to_player(void);
#define STEALTH_LIMIT 600                /* 10 с в stealth-режиме, затем гибель и респаун на checkpoint */

#endif
