/* LHMP v1 — shared with mapc.py / any C port */
#ifndef LHMP_H
#define LHMP_H
#include <stdint.h>

#define LHMP_MAGIC   0x504D484Cu  /* 'LHMP' LE */
#define LHMP_VERSION 1
#define LHMP_HEADER_SIZE 48
#define LHMP_OBJ_SIZE    24

#define LHMP_FLAG_MASK   1u

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;
    uint16_t version, flags;
    uint16_t width, height;
    uint8_t  tile_w, tile_h, n_layers, n_cp;
    uint16_t n_obj, n_spawn;
    uint8_t  n_quest, n_hero;
    uint16_t map_id, bg_id, boss_id;
    int16_t  arena_x0, arena_x1, arena_floor, arena_ceil;
    int16_t  arena_bx0, arena_bx1;
    uint8_t  reserved[8];
} LhmpHeader;

typedef struct {
    uint8_t type, kind, flags, need;
    int16_t x, y;
    uint16_t w, h;
    int16_t x0, y0, x1, y1;
    int16_t target;
    uint8_t dir, pad;
} LhmpObj;
#pragma pack(pop)

#endif
