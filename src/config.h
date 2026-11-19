#ifndef CONFIG_H
#define CONFIG_H
#include "types.h"

#define GAME_TITLE "Lost Hero - A/D move, W/Space jump, J melee, K ranged, L super, E use, Q group, 1-4/Tab hero, F1 debug"

/* Внутреннее разрешение рисования. 320x256 * 4 = 1280x1024 (целочисленный масштаб).
   Для консолей меняется только здесь: GBA 240x160, SNES 256x224, NES 256x240, MD 320x224. */
#define VIEW_W 320
#define VIEW_H 256
#define WIN_W  1280
#define WIN_H  1024
#define TICKS_PER_SEC 60

/* Мир: тайлы 16x16, 4 этажа по 128 px */
#define TILE 16
#define MAP_W 100
#define MAP_H 36
#define WORLD_W (MAP_W * TILE)
#define WORLD_H (MAP_H * TILE)

/* Физика (fx 24.8, пикселей на кадр при 60 Гц) */
#define STEP_UP 3
#define COYOTE_FRAMES 4
#define MAX_PUSH 40
#define GRAVITY         FXF(0.30)
#define MAX_FALL        FXF(9.0)
#define ACCEL_GROUND    FXF(0.08)
#define ACCEL_AIR       FXF(0.05)
#define FRICTION_GROUND FXF(0.14)
#define FRICTION_AIR    FXF(0.01)
#define WALK_THRESH     FXF(0.15)
#define RUN_THRESH      FXF(1.5)
#define ROPE_SPEED      FXF(1.4)
#define SPIN_FRAMES     300       /* 5 секунд юлы Domina: первые 2 с (SPIN_GRACE) касание пола её не выключает */
#define SPIN_GRACE      120
#define SPIN_COOLDOWN   180

/* Бой (кадры) */
#define HURT_FRAMES      20
#define INVULN_HERO      50
#define INVULN_ENEMY     10
#define FALLDMG_FRAMES   46
#define ENEMY_CAP        7        /* максимум врагов одновременно (3 умирают — ещё 3 приходят) */
#define SPAWN_FRAMES     110      /* 3 секунды после смерти до появления нового */
#define DESPAWN_DIST     560

/* Игра */
#define NUM_HEROES 7
#define INTERACT_REACH 22         /* дальность "использовать" (E), px */
#define REPAIR_FRAMES 120
#define SWEEP_FRAMES  100
#define COLLAPSE_FRAMES 120       /* платформа проваливается через 2 секунды */
#define COLLAPSE_RESPAWN 180      /* и снова поднимается */
#define LIFT_SPEED FXF(1.2)
#define LIFT_DWELL 200
#define BOSS_HP 12

#define START_LIVES      3        /* запас жизней героя на старте */
#define MAX_LIVES        3        /* жёсткий лимит жизней (старт / воскрешение / фраги) */
#define KILLS_PER_LIFE   10       /* legacy; фактически: игрок 4, союзники 10 */
#define START_TELEPORTS  2        /* артефактов телепорта в начале каждого уровня */
#define MAX_TELEPORTS    2        /* жёсткий потолок телепортов */
#define MAX_FULL_RESPAWNS 5       /* полных рестартов уровня после исчерпания жизней до GAME OVER */

#endif
