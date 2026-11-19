#ifndef EVENTS_H
#define EVENTS_H
#include "image.h"
#include "actor.h"
/* Случайные события: уличный телефон (бонус / телевизор / мошенник), бонусы на 10 секунд */
void events_init(const char *dir);
void events_reset(void);
void events_update(void);
int  events_near(int px, int py);              /* герой достаточно близко, чтобы ответить */
void events_answer(int hi);
void events_draw(Image *fb, int cx, int cy);
void events_draw_world(Image *fb, int cx, int cy);   /* гитара, звуковые волны (над героями) */
void events_draw_hud(Image *fb);
Actor *events_decoy(void);                     /* дружеский Megadog: враги атакуют его; NULL если нет */
int  events_nospawn(void);
void events_guitar_throw(int hi);              /* Nerd: гитара-бумеранг */
void events_cheat_bonus(void);                /* тест: выдать случайный бонус (EV_BONUS=2 - Megadog) */
void events_fridge_heal(int hi);               /* Refreezer открывает дверцу: лечит соседей */
void events_hero_override(int hi, Intent *in); /* напарники ходят пить к холодильнику */
#endif
