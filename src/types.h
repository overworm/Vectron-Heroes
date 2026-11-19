#ifndef TYPES_H
#define TYPES_H
#include <stdint.h>

/* Фиксированная точка 24.8: вся физика целочисленная (удобно для SNES/NES/MD/GBA). */
typedef int32_t fx;
#define FX_SHIFT 8
#define FXF(f)    ((fx)((f) * 256.0))       /* константа, считается на этапе компиляции */
#define INT_FX(i) ((fx)(i) * 256)
#define FX_INT(v) ((int)((v) >> FX_SHIFT))  /* floor */

#define ABSI(a)   ((a) < 0 ? -(a) : (a))
#define MINI(a,b) ((a) < (b) ? (a) : (b))
#define MAXI(a,b) ((a) > (b) ? (a) : (b))

#endif
