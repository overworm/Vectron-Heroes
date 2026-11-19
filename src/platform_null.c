/* Безоконная платформа для автотестов и CI: ввод по сценарию, кадры сохраняются в BMP.
   NULL_SCRIPT="0-60:R,50-70:RJ"   буквы: L R U(вверх) N(вниз) J(прыжок) M(ближняя) G(дальняя) S(супер) E(использовать) T(Tab) D(debug) 1-4(выбор героя) P(PgUp) B(PgDn)
   NULL_SHOTS="60,120"   NULL_FRAMES=300   NULL_OUT=dir */
#include "platform.h"
#include "image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int a, b; unsigned m; } Seg;
static Seg g_seg[256];
static int g_nseg, g_shots[64], g_nshots, g_frame, g_max = 600;
static char g_out[256] = ".";

enum { M_L = 1, M_R = 2, M_U = 4, M_N = 8, M_J = 16, M_M = 32, M_G = 64, M_S = 128, M_E = 256, M_T = 512, M_D = 1024, M_1 = 2048, M_2 = 4096, M_3 = 8192, M_4 = 16384, M_Q = 32768, M_PU = 65536, M_PD = 131072, M_5 = 262144, M_6 = 524288, M_V = 1048576, M_W = 2097152, M_7 = 4194304 };

int plat_init(const char *title, int vw, int vh, int ww, int wh) {
    (void)title; (void)vw; (void)vh; (void)ww; (void)wh;
    const char *s = getenv("NULL_SCRIPT");
    while (s && *s && g_nseg < 256) {
        char *e;
        int a = (int)strtol(s, &e, 10), b;
        s = e;
        if (*s == '-') { b = (int)strtol(s + 1, &e, 10); s = e; } else b = a;
        unsigned m = 0;
        if (*s == ':') s++;
        while (*s && *s != ',') {
            switch (*s) {
            case 'L': m |= M_L; break; case 'R': m |= M_R; break; case 'U': m |= M_U; break; case 'N': m |= M_N; break;
            case 'J': m |= M_J; break; case 'M': m |= M_M; break; case 'G': m |= M_G; break; case 'S': m |= M_S; break;
            case 'E': m |= M_E; break; case 'T': m |= M_T; break; case 'D': m |= M_D; break; case 'Q': m |= M_Q; break; case 'V': m |= M_V; break; case 'W': m |= M_W; break; case '7': m |= M_7; break; case 'P': m |= M_PU; break; case 'B': m |= M_PD; break;
            case '1': m |= M_1; break; case '2': m |= M_2; break; case '3': m |= M_3; break; case '4': m |= M_4; break; case '5': m |= M_5; break; case '6': m |= M_6; break;
            default: break;
            }
            s++;
        }
        if (*s == ',') s++;
        g_seg[g_nseg].a = a; g_seg[g_nseg].b = b; g_seg[g_nseg].m = m; g_nseg++;
    }
    s = getenv("NULL_SHOTS");
    while (s && *s && g_nshots < 64) {
        char *e;
        g_shots[g_nshots++] = (int)strtol(s, &e, 10);
        if (e == s) { g_nshots--; break; }
        s = *e == ',' ? e + 1 : e;
        if (*e != ',') break;
    }
    if (getenv("NULL_FRAMES")) g_max = atoi(getenv("NULL_FRAMES"));
    if (getenv("NULL_OUT")) snprintf(g_out, sizeof g_out, "%s", getenv("NULL_OUT"));
    return 1;
}

void plat_poll(Input *in) {
    unsigned m = 0;
    for (int i = 0; i < g_nseg; i++)
        if (g_frame >= g_seg[i].a && g_frame <= g_seg[i].b) m |= g_seg[i].m;
    memset(in, 0, sizeof *in);
    in->left = !!(m & M_L); in->right = !!(m & M_R); in->up = !!(m & M_U); in->down = !!(m & M_N);
    in->jump = !!(m & M_J); in->melee = !!(m & M_M); in->ranged = !!(m & M_G); in->super = !!(m & M_S); in->special = !!(m & M_W);
    in->use = !!(m & M_E); in->next = !!(m & M_T); in->debug = !!(m & M_D); in->group = !!(m & M_Q); in->tele = !!(m & M_V);
    in->pick[0] = !!(m & M_1); in->pick[1] = !!(m & M_2); in->pick[2] = !!(m & M_3); in->pick[3] = !!(m & M_4);
    in->pick[4] = !!(m & M_5); in->pick[5] = !!(m & M_6); in->pick[6] = !!(m & M_7);
    in->pgup = !!(m & M_PU); in->pgdn = !!(m & M_PD);
    in->quit = g_frame >= g_max;
}

void plat_present(const uint32_t *argb, int w, int h) {
    for (int i = 0; i < g_nshots; i++)
        if (g_shots[i] == g_frame) {
            char p[512];
            Image im = {w, h, (uint32_t *)argb};
            snprintf(p, sizeof p, "%s/shot_%04d.bmp", g_out, g_frame);
            image_save_bmp(&im, p);
        }
    g_frame++;
}

uint32_t plat_ticks_ms(void) { return (uint32_t)g_frame * 17u; }
void plat_sleep_ms(uint32_t ms) { (void)ms; }
void plat_shutdown(void) {}
