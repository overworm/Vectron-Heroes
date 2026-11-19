#ifndef PLATFORM_H
#define PLATFORM_H
#include <stdint.h>

/* Тонкий слой под ОС/консоль. Игровое ядро знает только эти функции:
   для SNES/NES/MD/GBA пишется свой platform_xxx.c (ввод, вывод кадра, время). */
typedef struct {
    uint8_t left, right, up, down;        /* ходьба, канат, присесть/скольжение */
    uint8_t jump, melee, ranged, super, special, use, group, tele;
    uint8_t pick[8];                      /* выбор героя 1..6 */
    uint8_t next;                         /* Tab / R1: следующий герой */
    uint8_t prev_hero;                    /* L1: предыдущий герой */
    uint8_t debug, quit;
    uint8_t pgup, pgdn;                   /* PgUp/PgDn: уровень 1..3 */
    uint8_t bosskey;                      /* B: комната босса */
    uint8_t pause;                        /* P: пауза */
    uint8_t quest_dbg;                    /* F8: debug квестов */
} Input;

int      plat_init(const char *title, int view_w, int view_h, int win_w, int win_h);
void     plat_poll(Input *in);                                   /* удерживаемые клавиши */
void     plat_present(const uint32_t *argb, int w, int h);       /* показать кадр */
uint32_t plat_ticks_ms(void);
void     plat_sleep_ms(uint32_t ms);
void     plat_shutdown(void);

#endif
