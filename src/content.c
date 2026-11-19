#include "content.h"

/* Враги 1-3 готовы (ракетчик = "красный", качок = "синий", тяжелоатлет = "супер-качок", появляется реже всех). Остальные - заготовки. */
const EnemyDef ENEMY_DEFS[ENEMY_TYPES] = {
    {"scout",  "Ракетчик",      RIG_SCOUT, 2, 40, 1},
    {"body",   "Качок",         RIG_BODY,  4, 40, 1},
    {"heavy",  "Тяжелоатлет",   RIG_HEAVY, 6, 14, 1},
    {"enemy4",  "Враг 4",       RIG_BODY,  4, 30, 0},
    {"enemy5",  "Враг 5",       RIG_BODY,  4, 30, 0},
    {"enemy6",  "Враг 6",       RIG_BODY,  4, 30, 0},
    {"enemy7",  "Враг 7",       RIG_BODY,  5, 30, 0},
    {"enemy8",  "Враг 8",       RIG_BODY,  5, 30, 0},
    {"enemy9",  "Враг 9",       RIG_BODY,  5, 30, 0},
    {"enemy10", "Враг 10",      RIG_BODY,  6, 30, 0},
    {"enemy11", "Враг 11",      RIG_BODY,  6, 30, 0},
    {"enemy12", "Враг 12",      RIG_BODY,  6, 30, 0},
};

/* Босс 1 - Профессор на сноуборде (готов). Остальные - заготовки. */
const BossDef BOSS_DEFS[BOSS_TYPES] = {
    {"boss",  "Профессор", RIG_BOSS, 12, 1},
    {"boss2", "Босс 2",    RIG_BOSS, 14, 0}, {"boss3", "Босс 3", RIG_BOSS, 16, 0}, {"boss4", "Босс 4", RIG_BOSS, 18, 0},
    {"boss5", "Босс 5",    RIG_BOSS, 20, 0}, {"boss6", "Босс 6", RIG_BOSS, 22, 0}, {"boss7", "Босс 7", RIG_BOSS, 24, 0},
    {"boss8", "Босс 8",    RIG_BOSS, 26, 0}, {"boss9", "Босс 9", RIG_BOSS, 30, 0},
};

/* Четыре героя готовы, ещё три - "?" (в HUD показываются квадратами со знаком вопроса). */
const HeroDef HERO_DEFS[HERO_TYPES] = {
    {"pengi", "Пингви", 1}, {"bum", "Кибердед", 1}, {"doorkeeper", "Вахтёрша", 1}, {"mistress", "Домина", 1},
    {"hero5", "?", 0}, {"hero6", "?", 0}, {"hero7", "?", 0},
};

const LevelDef LEVEL_DEFS[LEVEL_COUNT] = {
    {"Двор",            1, { 0,  1,  2, -1}, 0},
    {"Подвал",          0, { 0,  1,  3,  4}, 1},
    {"Склад",           0, { 1,  2,  4,  5}, 2},
    {"Котельная",       0, { 3,  4,  5,  6}, 3},
    {"Крыши",           0, { 2,  5,  6,  7}, 4},
    {"Метро",           0, { 4,  6,  7,  8}, 5},
    {"Завод",           0, { 5,  7,  8,  9}, 6},
    {"Лаборатория",     0, { 6,  8,  9, 10}, 7},
    {"Башня профессора",0, { 7,  9, 10, 11}, 8},
};

int content_pick_enemy(int level, int stage, int heavies, uint32_t rnd) {
    const LevelDef *L = &LEVEL_DEFS[level < 0 ? 0 : level >= LEVEL_COUNT ? LEVEL_COUNT - 1 : level];
    int w[4], tot = 0;
    for (int i = 0; i < 4; i++) {
        int id = L->enemies[i];
        w[i] = 0;
        if (id < 0 || !ENEMY_DEFS[id].implemented) continue;
        w[i] = ENEMY_DEFS[id].weight;
        if (id == 2 && (stage == 0 || heavies >= (stage >= 2 ? 2 : 1))) w[i] = 0;     /* тяжёлых нет на 1 этапе и не больше 1-2 одновременно */
        tot += w[i];
    }
    if (tot <= 0) return 1;
    int r = (int)(rnd % (uint32_t)tot);
    for (int i = 0; i < 4; i++) { if (r < w[i]) return L->enemies[i]; r -= w[i]; }
    return 1;
}
