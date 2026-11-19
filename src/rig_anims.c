/* Библиотека анимаций: общая основа (ходьба, бег, прыжок...) + индивидуальные атаки каждого героя и врага.
   Знаки: rz>0 у висящей конечности = вперёд; chest rz<0 = наклон вперёд; rx у плеча разводит руку вбок (z);
   pelvis rz=-90 - лежит лицом вниз, +90 - на спине. Ключи задают только отличия от нейтральной позы. */
#include "rig.h"
#include <math.h>
#include <string.h>

typedef struct { int n; float x, y, z; } Tw;

static Pose neutral(void) {
    Pose p;
    memset(&p, 0, sizeof p);
    p.rot[N_SH_L][0] = 8;  p.rot[N_SH_R][0] = -8;
    p.rot[N_EL_L][2] = 10; p.rot[N_EL_R][2] = 10;
    return p;
}
static void add_key(Anim *a, float t, float rx, float ry, float rz, const Tw *w, int nw) {
    if (a->n >= MAX_KEYS) return;
    Key *k = &a->k[a->n++];
    k->t = t; k->p = neutral();
    k->p.root[0] = rx; k->p.root[1] = ry; k->p.root[2] = rz;
    for (int i = 0; i < nw; i++) { k->p.rot[w[i].n][0] = w[i].x; k->p.rot[w[i].n][1] = w[i].y; k->p.rot[w[i].n][2] = w[i].z; }
}
#define K(a, t, ...)  do { const Tw _w[] = {__VA_ARGS__}; add_key((a), (t), 0, 0, 0, _w, (int)(sizeof _w / sizeof _w[0])); } while (0)
#define KR(a, t, rx, ry, rz, ...) do { const Tw _w[] = {__VA_ARGS__}; add_key((a), (t), (rx), (ry), (rz), _w, (int)(sizeof _w / sizeof _w[0])); } while (0)

static Anim *A(Rig *r, int id, int dur, int loop, int ease) {
    Anim *a = &r->anims[id];
    memset(a, 0, sizeof *a);
    a->dur = dur; a->loop = loop; a->ease = ease;
    return a;
}
static void copy_anim(Rig *r, int dst, int src) { r->anims[dst] = r->anims[src]; }

/* высота центра таза над землёй в лежачей позе (со знаком минус - опускает корень) */
static float lie_y(const Rig *r) {
    float m = r->nodes[N_PELVIS].r > r->nodes[N_CHEST].r ? r->nodes[N_PELVIS].r : r->nodes[N_CHEST].r;
    return -(r->nodes[N_PELVIS].off[1] - m - 0.5f);
}

/* Циклическая походка: hip - размах бёдер, knee - сгиб колена, arm/elbow - руки, armb - постоянный подъём рук,
   lean - наклон корпуса (минус = вперёд), bob - покачивание, twist - скручивание корпуса */
static void gait(Anim *a, int nk, float hip, float knee, float arm, float elbow, float armb, float lean, float bob, float twist) {
    /* Двуногая походка: ноги чередуются, колено сгибается при переносе, обе стопы касаются земли в фазе опоры.
       s = sin (фаза), c = cos. Бёдра в противофазе; колено сгибается только у переносимой ноги. */
    for (int i = 0; i <= nk; i++) {
        float th = 6.2831853f * (float)i / (float)nk, s = sinf(th), c = cosf(th);
        float knee_l = -knee * fmaxf(0.f, c);           /* левое колено сгибается когда правая нога впереди (c>0) */
        float knee_r = -knee * fmaxf(0.f, -c);          /* правое — когда левая впереди */
        /* лёгкий подъём корня только в середине шага, не «прыжок» */
        float bob_y = -bob * (s * s) * 0.55f;
        KR(a, (float)i / (float)nk, 0, bob_y, 0,
           {N_HIP_L, 0, 0, hip * s}, {N_HIP_R, 0, 0, -hip * s},
           {N_KNEE_L, 0, 0, knee_l}, {N_KNEE_R, 0, 0, knee_r},
           {N_FOOT_L, 0, 0, -knee_l * 0.35f}, {N_FOOT_R, 0, 0, -knee_r * 0.35f},
           {N_SH_L, 8, 0, armb - arm * s}, {N_SH_R, -8, 0, armb + arm * s},
           {N_EL_L, 0, 0, elbow}, {N_EL_R, 0, 0, elbow},
           {N_CHEST, 0, twist * s, lean}, {N_PELVIS, 0, -twist * s, 0});
    }
}

typedef struct {
    float wh, wk, wa, we, wl, wb, wt;        /* walk: hip knee arm elbow lean bob twist */
    float rh, rk, ra, re, rl, rb, rt;        /* run */
    float armb, ie;                          /* постоянный подъём рук и сгиб локтей (ходьба/покой) */
    int wdur, rdur;
} Style;

static void build_common(Rig *r, const Style *s) {
    Anim *a;
    float ly = lie_y(r);

    a = A(r, A_IDLE, 90, 1, 1);
    K(a, 0.f, {N_HIP_L, 0, 0, 3}, {N_HIP_R, 0, 0, -3}, {N_SH_L, 8, 0, s->armb}, {N_SH_R, -8, 0, s->armb}, {N_EL_L, 0, 0, s->ie}, {N_EL_R, 0, 0, s->ie});
    KR(a, 0.5f, 0, -0.8f, 0, {N_HIP_L, 0, 0, 3}, {N_HIP_R, 0, 0, -3}, {N_KNEE_L, 0, 0, -4}, {N_KNEE_R, 0, 0, -4},
       {N_CHEST, 0, 0, -2}, {N_HEAD, 0, 0, 2}, {N_SH_L, 8, 0, s->armb + 3}, {N_SH_R, -8, 0, s->armb + 3}, {N_EL_L, 0, 0, s->ie}, {N_EL_R, 0, 0, s->ie});
    K(a, 1.f, {N_HIP_L, 0, 0, 3}, {N_HIP_R, 0, 0, -3}, {N_SH_L, 8, 0, s->armb}, {N_SH_R, -8, 0, s->armb}, {N_EL_L, 0, 0, s->ie}, {N_EL_R, 0, 0, s->ie});

    a = A(r, A_WALK, s->wdur, 1, 0); gait(a, 12, s->wh, s->wk, s->wa, s->we, s->armb, s->wl, s->wb, s->wt);
    a = A(r, A_RUN, s->rdur, 1, 0);  gait(a, 12, s->rh, s->rk, s->ra, s->re, s->armb * 0.6f, s->rl, s->rb, s->rt);

    a = A(r, A_JUMP, 18, 0, 1);
    KR(a, 0.f, 0, -5, 0, {N_HIP_L, 0, 0, 35}, {N_HIP_R, 0, 0, 35}, {N_KNEE_L, 0, 0, -75}, {N_KNEE_R, 0, 0, -75}, {N_CHEST, 0, 0, -8},
       {N_SH_L, 8, 0, -30}, {N_SH_R, -8, 0, -30});
    K(a, 0.35f, {N_HIP_L, 0, 0, 30}, {N_KNEE_L, 0, 0, -45}, {N_HIP_R, 0, 0, -12}, {N_KNEE_R, 0, 0, -25},
      {N_SH_L, 8, 0, 130}, {N_SH_R, -8, 0, 125}, {N_EL_L, 0, 0, 15}, {N_EL_R, 0, 0, 15});
    K(a, 1.f, {N_HIP_L, 0, 0, 32}, {N_KNEE_L, 0, 0, -50}, {N_HIP_R, 0, 0, -14}, {N_KNEE_R, 0, 0, -30},
      {N_SH_L, 8, 0, 142}, {N_SH_R, -8, 0, 136}, {N_EL_L, 0, 0, 15}, {N_EL_R, 0, 0, 15});

    a = A(r, A_LAND, 10, 0, 1);
    KR(a, 0.f, 0, -6.5f, 0, {N_HIP_L, 0, 0, 40}, {N_HIP_R, 0, 0, 40}, {N_KNEE_L, 0, 0, -80}, {N_KNEE_R, 0, 0, -80}, {N_CHEST, 0, 0, -14},
       {N_SH_L, 8, 0, -20}, {N_SH_R, -8, 0, -20});
    KR(a, 0.5f, 0, -4, 0, {N_HIP_L, 0, 0, 28}, {N_HIP_R, 0, 0, 28}, {N_KNEE_L, 0, 0, -55}, {N_KNEE_R, 0, 0, -55}, {N_CHEST, 0, 0, -8});
    K(a, 1.f, {N_HIP_L, 0, 0, 3}, {N_HIP_R, 0, 0, -3});

    a = A(r, A_FALL, 24, 1, 1);
    K(a, 0.f, {N_SH_L, 8, 0, 150}, {N_SH_R, -8, 0, 140}, {N_EL_L, 0, 0, 20}, {N_EL_R, 0, 0, 20},
      {N_HIP_L, 0, 0, 18}, {N_KNEE_L, 0, 0, -45}, {N_HIP_R, 0, 0, -15}, {N_KNEE_R, 0, 0, -25}, {N_CHEST, 0, 0, 4});
    K(a, 0.5f, {N_SH_L, 8, 0, 135}, {N_SH_R, -8, 0, 155}, {N_EL_L, 0, 0, 20}, {N_EL_R, 0, 0, 20},
      {N_HIP_L, 0, 0, 8}, {N_KNEE_L, 0, 0, -30}, {N_HIP_R, 0, 0, -22}, {N_KNEE_R, 0, 0, -45}, {N_CHEST, 0, 0, 4});
    K(a, 1.f, {N_SH_L, 8, 0, 150}, {N_SH_R, -8, 0, 140}, {N_EL_L, 0, 0, 20}, {N_EL_R, 0, 0, 20},
      {N_HIP_L, 0, 0, 18}, {N_KNEE_L, 0, 0, -45}, {N_HIP_R, 0, 0, -15}, {N_KNEE_R, 0, 0, -25}, {N_CHEST, 0, 0, 4});

    a = A(r, A_CLIMB, 24, 1, 1);                       /* подъём по канату: руки вверх по очереди, ноги подтягиваются */
    K(a, 0.f, {N_SH_L, 8, 0, 165}, {N_EL_L, 0, 0, 10}, {N_SH_R, -8, 0, 105}, {N_EL_R, 0, 0, 60},
      {N_HIP_L, 0, 0, 55}, {N_KNEE_L, 0, 0, -75}, {N_HIP_R, 0, 0, 12}, {N_KNEE_R, 0, 0, -20}, {N_CHEST, 0, 0, -6});
    K(a, 0.5f, {N_SH_R, -8, 0, 165}, {N_EL_R, 0, 0, 10}, {N_SH_L, 8, 0, 105}, {N_EL_L, 0, 0, 60},
      {N_HIP_R, 0, 0, 55}, {N_KNEE_R, 0, 0, -75}, {N_HIP_L, 0, 0, 12}, {N_KNEE_L, 0, 0, -20}, {N_CHEST, 0, 0, -6});
    K(a, 1.f, {N_SH_L, 8, 0, 165}, {N_EL_L, 0, 0, 10}, {N_SH_R, -8, 0, 105}, {N_EL_R, 0, 0, 60},
      {N_HIP_L, 0, 0, 55}, {N_KNEE_L, 0, 0, -75}, {N_HIP_R, 0, 0, 12}, {N_KNEE_R, 0, 0, -20}, {N_CHEST, 0, 0, -6});

    a = A(r, A_ACTIVATE, 40, 1, 1);                    /* работает руками перед собой */
    KR(a, 0.f, 0, -1, 0, {N_CHEST, 0, 0, -12}, {N_SH_R, -8, 0, 55}, {N_EL_R, 0, 0, 45}, {N_SH_L, 8, 0, 45}, {N_EL_L, 0, 0, 55},
       {N_HIP_L, 0, 0, 8}, {N_HIP_R, 0, 0, 8}, {N_KNEE_L, 0, 0, -10}, {N_KNEE_R, 0, 0, -10});
    KR(a, 0.5f, 0, -1, 0, {N_CHEST, 0, 0, -14}, {N_SH_R, -8, 0, 72}, {N_EL_R, 0, 0, 35}, {N_SH_L, 8, 0, 55}, {N_EL_L, 0, 0, 45},
       {N_HIP_L, 0, 0, 8}, {N_HIP_R, 0, 0, 8}, {N_KNEE_L, 0, 0, -10}, {N_KNEE_R, 0, 0, -10});
    KR(a, 1.f, 0, -1, 0, {N_CHEST, 0, 0, -12}, {N_SH_R, -8, 0, 55}, {N_EL_R, 0, 0, 45}, {N_SH_L, 8, 0, 45}, {N_EL_L, 0, 0, 55},
       {N_HIP_L, 0, 0, 8}, {N_HIP_R, 0, 0, 8}, {N_KNEE_L, 0, 0, -10}, {N_KNEE_R, 0, 0, -10});

    a = A(r, A_HURT, 20, 0, 1);
    K(a, 0.f, {N_PELVIS, 0, 0, 0});
    KR(a, 0.2f, -3, -2, 0, {N_CHEST, 0, 0, 20}, {N_HEAD, 0, 0, 25}, {N_SH_L, 8, 0, -45}, {N_SH_R, -8, 0, -50}, {N_EL_L, 0, 0, 40}, {N_EL_R, 0, 0, 40},
       {N_HIP_L, 0, 0, 10}, {N_KNEE_L, 0, 0, -30}, {N_KNEE_R, 0, 0, -25});
    KR(a, 0.6f, -2, -1.5f, 0, {N_CHEST, 0, 0, 14}, {N_HEAD, 0, 0, 15}, {N_SH_L, 8, 0, -30}, {N_SH_R, -8, 0, -35}, {N_EL_L, 0, 0, 30}, {N_EL_R, 0, 0, 30},
       {N_KNEE_L, 0, 0, -20}, {N_KNEE_R, 0, 0, -18});
    K(a, 1.f, {N_CHEST, 0, 0, 6});

    a = A(r, A_DEATH, 50, 0, 1);                       /* колени подгибаются, падает на спину и лежит */
    K(a, 0.f, {N_PELVIS, 0, 0, 0});
    KR(a, 0.25f, 0, -6, 0, {N_HIP_L, 0, 0, 30}, {N_HIP_R, 0, 0, 30}, {N_KNEE_L, 0, 0, -60}, {N_KNEE_R, 0, 0, -60}, {N_CHEST, 0, 0, 15}, {N_HEAD, 0, 0, 20},
       {N_PELVIS, 0, 0, 10}, {N_SH_L, 8, 0, -40}, {N_SH_R, -8, 0, -30});
    KR(a, 0.6f, -4, ly * 0.6f, 0, {N_PELVIS, 0, 0, 70}, {N_KNEE_L, 0, 0, -40}, {N_KNEE_R, 0, 0, -40}, {N_CHEST, 0, 0, 10},
       {N_SH_L, 8, 0, -60}, {N_SH_R, -8, 0, -60});
    KR(a, 1.f, -6, ly, 0, {N_PELVIS, 0, 0, 90}, {N_KNEE_L, 0, 0, -10}, {N_KNEE_R, 0, 0, -10}, {N_SH_L, 40, 0, -20}, {N_SH_R, -40, 0, -20});

    a = A(r, A_FALLDMG, 46, 0, 1);                     /* ударился при падении: сложился, лежит лицом вниз, поднимается */
    K(a, 0.f, {N_PELVIS, 0, 0, 0});
    KR(a, 0.14f, 0, -8, 0, {N_HIP_L, 0, 0, 50}, {N_HIP_R, 0, 0, 50}, {N_KNEE_L, 0, 0, -90}, {N_KNEE_R, 0, 0, -90}, {N_CHEST, 0, 0, -25},
       {N_SH_L, 8, 0, 60}, {N_SH_R, -8, 0, 60}, {N_EL_L, 0, 0, 20}, {N_EL_R, 0, 0, 20});
    KR(a, 0.4f, 2, ly, 0, {N_PELVIS, 0, 0, -88}, {N_SH_L, 8, 0, 175}, {N_SH_R, -8, 0, 175}, {N_HEAD, 0, 0, 40}, {N_KNEE_L, 0, 0, -15}, {N_KNEE_R, 0, 0, -10});
    KR(a, 0.75f, 2, ly, 0, {N_PELVIS, 0, 0, -88}, {N_SH_L, 8, 0, 170}, {N_SH_R, -8, 0, 170}, {N_HEAD, 0, 0, 30}, {N_KNEE_L, 0, 0, -25}, {N_KNEE_R, 0, 0, -20});
    KR(a, 1.f, 0, -6, 0, {N_PELVIS, 0, 0, -25}, {N_HIP_L, 0, 0, 40}, {N_HIP_R, 0, 0, 40}, {N_KNEE_L, 0, 0, -70}, {N_KNEE_R, 0, 0, -70}, {N_CHEST, 0, 0, -10});

    /* заготовки для анимаций, переопределяемых персонажами */
    copy_anim(r, A_GLIDE, A_FALL);
    copy_anim(r, A_SLIDE, A_LAND);
    r->anims[A_SLIDE].loop = 1;
    copy_anim(r, A_MELEE, A_ACTIVATE); r->anims[A_MELEE].loop = 0; r->anims[A_MELEE].dur = 20;
    copy_anim(r, A_RANGED, A_MELEE);
    copy_anim(r, A_SUPER, A_MELEE);
}

/* ---------------------------------------------------------------- герои */
static void anims_pengi(Rig *r) {
    Style s = {18, 25, 20, 10, -2, 1.2f, 6,   30, 45, 35, 20, -10, 2, 8,   0, 10, 44, 26};
    build_common(r, &s);
    Anim *a;
    float ly = lie_y(r);
    a = A(r, A_MELEE, 14, 0, 1);                       /* удар ластой */
    K(a, 0.f, {N_PELVIS, 0, 0, 0});
    K(a, 0.3f, {N_SH_R, -8, 0, -50}, {N_CHEST, 0, -30, 0}, {N_EL_R, 0, 0, 20});
    K(a, 0.6f, {N_SH_R, -8, 0, 110}, {N_EL_R, 0, 0, 0}, {N_CHEST, 0, 30, -8});
    K(a, 1.f, {N_PELVIS, 0, 0, 0});
    a = A(r, A_RANGED, 16, 0, 1);                      /* бластер в две руки */
    K(a, 0.f, {N_SH_R, -8, 0, 80}, {N_EL_R, 0, 0, 10}, {N_SH_L, 8, 0, 70}, {N_EL_L, 0, 0, 35}, {N_CHEST, 0, 0, 4});
    KR(a, 0.25f, -2, 0, 0, {N_SH_R, -8, 0, 95}, {N_EL_R, 0, 0, 5}, {N_SH_L, 8, 0, 85}, {N_EL_L, 0, 0, 30}, {N_CHEST, 0, 0, 6});
    K(a, 1.f, {N_SH_R, -8, 0, 82}, {N_EL_R, 0, 0, 10}, {N_SH_L, 8, 0, 72}, {N_EL_L, 0, 0, 35}, {N_CHEST, 0, 0, 4});
    a = A(r, A_SUPER, 34, 0, 1);                       /* заряженный выстрел */
    K(a, 0.f, {N_SH_R, -8, 0, 80}, {N_EL_R, 0, 0, 10}, {N_SH_L, 8, 0, 70}, {N_EL_L, 0, 0, 35});
    KR(a, 0.4f, -1, 0, 0, {N_SH_R, -8, 0, 70}, {N_EL_R, 0, 0, 30}, {N_SH_L, 8, 0, 62}, {N_EL_L, 0, 0, 45}, {N_CHEST, 0, 0, 12});
    KR(a, 0.55f, -5, 0, 0, {N_SH_R, -8, 0, 100}, {N_EL_R, 0, 0, 0}, {N_SH_L, 8, 0, 92}, {N_EL_L, 0, 0, 20}, {N_CHEST, 0, 0, 10});
    K(a, 1.f, {N_SH_R, -8, 0, 82}, {N_EL_R, 0, 0, 10}, {N_SH_L, 8, 0, 72}, {N_EL_L, 0, 0, 35});
    a = A(r, A_SLIDE, 20, 1, 0);                       /* скольжение на брюшке */
    KR(a, 0.f, 3, ly, 0, {N_PELVIS, 0, 0, -86}, {N_SH_L, 8, 0, 170}, {N_SH_R, -8, 0, 170}, {N_HEAD, 0, 0, 62}, {N_KNEE_L, 0, 0, -12}, {N_KNEE_R, 0, 0, -8});
    KR(a, 1.f, 3, ly, 0, {N_PELVIS, 0, 0, -86}, {N_SH_L, 8, 0, 170}, {N_SH_R, -8, 0, 170}, {N_HEAD, 0, 0, 62}, {N_KNEE_L, 0, 0, -12}, {N_KNEE_R, 0, 0, -8});
}

static void anims_bum(Rig *r) {
    Style s = {22, 40, 22, 20, -6, 1.0f, 6,   44, 75, 50, 70, -14, 2, 10,   0, 12, 46, 28};
    build_common(r, &s);
    Anim *a;
    a = A(r, A_MELEE, 22, 0, 1);                       /* гаечный ключ: замах и удар сверху */
    K(a, 0.f, {N_SH_R, -8, 0, -70}, {N_EL_R, 0, 0, 40}, {N_CHEST, 0, -25, 0});
    K(a, 0.25f, {N_SH_R, -8, 0, 160}, {N_EL_R, 0, 0, 20}, {N_CHEST, 0, -30, 6});
    K(a, 0.5f, {N_SH_R, -8, 0, 95}, {N_EL_R, 0, 0, 0}, {N_CHEST, 0, 30, -14});
    K(a, 0.7f, {N_SH_R, -8, 0, 85}, {N_EL_R, 0, 0, 5}, {N_CHEST, 0, 20, -10});
    K(a, 1.f, {N_PELVIS, 0, 0, 0});
    a = A(r, A_RANGED, 22, 0, 1);                      /* бросок бутылки */
    K(a, 0.f, {N_SH_R, -8, 0, -80}, {N_EL_R, 0, 0, 50}, {N_CHEST, 0, -30, 4});
    K(a, 0.35f, {N_SH_R, -8, 0, 150}, {N_EL_R, 0, 0, 0}, {N_CHEST, 0, 30, -10});
    K(a, 1.f, {N_PELVIS, 0, 0, 0});
    a = A(r, A_SUPER, 44, 0, 1);                       /* обе руки над головой и удар */
    K(a, 0.f, {N_PELVIS, 0, 0, 0});
    K(a, 0.35f, {N_SH_R, -8, 0, 170}, {N_EL_R, 0, 0, 30}, {N_SH_L, 8, 0, 150}, {N_EL_L, 0, 0, 40}, {N_CHEST, 0, 0, 12});
    K(a, 0.55f, {N_SH_R, -8, 0, 172}, {N_EL_R, 0, 0, 30}, {N_SH_L, 8, 0, 152}, {N_EL_L, 0, 0, 40}, {N_CHEST, 0, 0, 14});
    KR(a, 0.68f, 2, -2, 0, {N_SH_R, -8, 0, 70}, {N_EL_R, 0, 0, 0}, {N_SH_L, 8, 0, 60}, {N_EL_L, 0, 0, 10}, {N_CHEST, 0, 0, -32});
    KR(a, 0.8f, 2, -2, 0, {N_SH_R, -8, 0, 68}, {N_EL_R, 0, 0, 0}, {N_SH_L, 8, 0, 58}, {N_EL_L, 0, 0, 10}, {N_CHEST, 0, 0, -30});
    K(a, 1.f, {N_PELVIS, 0, 0, 0});
}

static void anims_doorkeeper(Rig *r) {
    Style s = {18, 30, 12, 25, -4, 1.4f, 10,   34, 55, 30, 55, -12, 2.2f, 12,   0, 12, 44, 28};
    build_common(r, &s);
    Anim *a;
    a = A(r, A_MELEE, 22, 0, 1);                       /* веником */
    K(a, 0.f, {N_SH_R, -8, 0, -35}, {N_EL_R, 0, 0, 40}, {N_SH_L, 8, 0, -10}, {N_EL_L, 0, 0, 60}, {N_CHEST, 0, -35, 0});
    K(a, 0.35f, {N_SH_R, -8, 0, 95}, {N_EL_R, 0, 0, 10}, {N_SH_L, 8, 0, 70}, {N_EL_L, 0, 0, 20}, {N_CHEST, 0, 40, -8});
    K(a, 0.7f, {N_SH_R, -8, 0, 110}, {N_EL_R, 0, 0, 5}, {N_SH_L, 8, 0, 80}, {N_EL_L, 0, 0, 20}, {N_CHEST, 0, 30, -6});
    K(a, 1.f, {N_PELVIS, 0, 0, 0});
    copy_anim(r, A_RANGED, A_MELEE);
    a = A(r, A_SUPER, 40, 0, 0);                       /* оборот с веником: естественное вращение, обе ноги */
    for (int i = 0; i <= 4; i++) {
        float t = 0.25f * (float)i, ry = 90.f * (float)i;
        KR(a, t, 0, 0, 0,
           {N_PELVIS, 0, ry, 0},
           {N_SH_R, -8, 0, 100}, {N_EL_R, 0, 0, 5},
           {N_SH_L, 8, 0, 85}, {N_EL_L, 0, 0, 15},
           {N_HIP_L, 0, 0, 15}, {N_HIP_R, 0, 0, -15},
           {N_KNEE_L, 0, 0, -20}, {N_KNEE_R, 0, 0, -20},
           {N_CHEST, 0, 0, -6});
    }
    a = A(r, A_ACTIVATE, 24, 1, 1);                    /* подметает снег */
    KR(a, 0.f, 0, -1, 0, {N_CHEST, 0, 0, -16}, {N_SH_R, -8, 0, 38}, {N_EL_R, 0, 0, 14}, {N_SH_L, 8, 0, 30}, {N_EL_L, 0, 0, 20}, {N_HIP_L, 0, 0, 10}, {N_HIP_R, 0, 0, 6});
    KR(a, 0.5f, 0, -1, 0, {N_CHEST, 0, 0, -18}, {N_SH_R, -8, 0, 62}, {N_EL_R, 0, 0, 10}, {N_SH_L, 8, 0, 52}, {N_EL_L, 0, 0, 15}, {N_HIP_L, 0, 0, 6}, {N_HIP_R, 0, 0, 10});
    KR(a, 1.f, 0, -1, 0, {N_CHEST, 0, 0, -16}, {N_SH_R, -8, 0, 38}, {N_EL_R, 0, 0, 14}, {N_SH_L, 8, 0, 30}, {N_EL_L, 0, 0, 20}, {N_HIP_L, 0, 0, 10}, {N_HIP_R, 0, 0, 6});
}

static void anims_mistress(Rig *r) {
    Style s = {24, 36, 14, 15, -3, 1.2f, 14,   54, 85, 40, 80, -12, 2.2f, 14,   0, 10, 40, 26};
    build_common(r, &s);
    Anim *a;
    a = A(r, A_MELEE, 24, 0, 1);                       /* бензопила: взмах — обе ноги в стойке, корпус и руки */
    K(a, 0.f, {N_SH_R, -8, 0, 60}, {N_EL_R, 0, 0, 20}, {N_SH_L, 8, 0, 55}, {N_EL_L, 0, 0, 35},
      {N_CHEST, 0, -20, 0}, {N_HIP_L, 0, 0, 12}, {N_HIP_R, 0, 0, -18}, {N_KNEE_L, 0, 0, -15}, {N_KNEE_R, 0, 0, -25});
    K(a, 0.3f, {N_SH_R, -8, 0, 95}, {N_EL_R, 0, 0, 10}, {N_SH_L, 8, 0, 85}, {N_EL_L, 0, 0, 25},
      {N_CHEST, 0, 0, 0}, {N_HIP_L, 0, 0, 18}, {N_HIP_R, 0, 0, -22}, {N_KNEE_L, 0, 0, -20}, {N_KNEE_R, 0, 0, -30});
    K(a, 0.55f, {N_SH_R, -8, 0, 108}, {N_EL_R, 0, 0, 5}, {N_SH_L, 8, 0, 98}, {N_EL_L, 0, 0, 20},
      {N_CHEST, 0, 35, -6}, {N_HIP_L, 0, 0, 8}, {N_HIP_R, 0, 0, -10}, {N_KNEE_L, 0, 0, -12}, {N_KNEE_R, 0, 0, -18});
    K(a, 1.f, {N_PELVIS, 0, 0, 0}, {N_HIP_L, 0, 0, 3}, {N_HIP_R, 0, 0, -3});
    a = A(r, A_SUPER, 50, 0, 1);                       /* бензопила: прорезает стену — полный размер, обе ноги */
    K(a, 0.f, {N_PELVIS, 0, 0, 0});
    K(a, 0.3f, {N_SH_R, -8, 0, 20}, {N_EL_R, 0, 0, 70}, {N_SH_L, 8, 0, 15}, {N_EL_L, 0, 0, 70},
      {N_CHEST, 0, -35, 8}, {N_HIP_L, 0, 0, 15}, {N_HIP_R, 0, 0, -20}, {N_KNEE_L, 0, 0, -20}, {N_KNEE_R, 0, 0, -28});
    KR(a, 0.5f, 0, 0, 0, {N_SH_R, -8, 0, 100}, {N_EL_R, 0, 0, 5}, {N_SH_L, 8, 0, 90}, {N_EL_L, 0, 0, 15},
       {N_CHEST, 0, 25, -18}, {N_HIP_L, 0, 0, 20}, {N_HIP_R, 0, 0, -25}, {N_KNEE_L, 0, 0, -25}, {N_KNEE_R, 0, 0, -32});
    KR(a, 0.8f, 0, 0, 0, {N_SH_R, -8, 0, 102}, {N_EL_R, 0, 0, 5}, {N_SH_L, 8, 0, 92}, {N_EL_L, 0, 0, 15},
       {N_CHEST, 0, 25, -18}, {N_HIP_L, 0, 0, 18}, {N_HIP_R, 0, 0, -22}, {N_KNEE_L, 0, 0, -22}, {N_KNEE_R, 0, 0, -30});
    K(a, 1.f, {N_PELVIS, 0, 0, 0});
    a = A(r, A_RANGED, 12, 0, 1);                      /* вход в волчок: руки в стороны */
    K(a, 0.f, {N_PELVIS, 0, 0, 0});
    K(a, 1.f, {N_SH_L, 85, 0, 0}, {N_SH_R, -85, 0, 0}, {N_EL_L, 0, 0, 0}, {N_EL_R, 0, 0, 0},
      {N_HIP_L, 0, 0, 8}, {N_HIP_R, 0, 0, -8}, {N_KNEE_L, 0, 0, -12}, {N_KNEE_R, 0, 0, -12});
    a = A(r, A_GLIDE, 24, 1, 0);                       /* вращение юлой — без сжатия по Y, естественный оборот */
    for (int i = 0; i <= 4; i++) {
        float t = 0.25f * (float)i;
        float ry = 90.f * (float)i;
        /* лёгкое покачивание, ноги слегка врозь, руки горизонтально — полный рост */
        KR(a, t, 0, 0, 0,
           {N_PELVIS, 0, ry, 0},
           {N_SH_L, 90, 0, 5}, {N_SH_R, -90, 0, 5},
           {N_EL_L, 0, 0, 5}, {N_EL_R, 0, 0, 5},
           {N_HIP_L, 0, 0, 12}, {N_HIP_R, 0, 0, -12},
           {N_KNEE_L, 0, 0, -18}, {N_KNEE_R, 0, 0, -18},
           {N_CHEST, 0, 0, -4});
    }
}

/* ---------------------------------------------------------------- враги */
static void anims_scout(Rig *r) {
    Style s = {24, 40, 5, 55, -4, 1.0f, 6,   48, 80, 8, 60, -14, 2.2f, 8,   42, 55, 44, 26};
    build_common(r, &s);
    Anim *a;
    a = A(r, A_MELEE, 20, 0, 1);                       /* каратэ: удар ногой */
    K(a, 0.f, {N_SH_R, -8, 0, 60}, {N_EL_R, 0, 0, 90}, {N_SH_L, 8, 0, 60}, {N_EL_L, 0, 0, 90});
    K(a, 0.3f, {N_HIP_R, 0, 0, 70}, {N_KNEE_R, 0, 0, -90}, {N_CHEST, 0, 0, 10}, {N_SH_R, -8, 0, 60}, {N_EL_R, 0, 0, 90}, {N_SH_L, 8, 0, 60}, {N_EL_L, 0, 0, 90});
    KR(a, 0.5f, -2, 0, 0, {N_HIP_R, 0, 0, 100}, {N_KNEE_R, 0, 0, 0}, {N_CHEST, 0, 0, 14}, {N_SH_L, 8, 0, -30}, {N_SH_R, -8, 0, -20}, {N_EL_L, 0, 0, 30}, {N_EL_R, 0, 0, 30});
    K(a, 0.75f, {N_HIP_R, 0, 0, 50}, {N_KNEE_R, 0, 0, -60}, {N_CHEST, 0, 0, 6});
    K(a, 1.f, {N_PELVIS, 0, 0, 0});
    a = A(r, A_RANGED, 26, 0, 1);                      /* ракетница */
    K(a, 0.f, {N_SH_R, -8, 0, 50}, {N_EL_R, 0, 0, 40}, {N_SH_L, 8, 0, 45}, {N_EL_L, 0, 0, 50});
    K(a, 0.35f, {N_SH_R, -8, 0, 85}, {N_EL_R, 0, 0, 10}, {N_SH_L, 8, 0, 75}, {N_EL_L, 0, 0, 25}, {N_CHEST, 0, 0, -2});
    KR(a, 0.5f, -2, 0, 0, {N_SH_R, -8, 0, 95}, {N_EL_R, 0, 0, 5}, {N_SH_L, 8, 0, 85}, {N_EL_L, 0, 0, 20}, {N_CHEST, 0, 0, 4});
    K(a, 1.f, {N_SH_R, -8, 0, 60}, {N_EL_R, 0, 0, 30}, {N_SH_L, 8, 0, 50}, {N_EL_L, 0, 0, 45});
}

static void anims_body(Rig *r) {
    Style s = {20, 34, 16, 20, -3, 1.4f, 8,   40, 65, 35, 60, -12, 2.5f, 10,   0, 15, 44, 26};
    build_common(r, &s);
    Anim *a;
    a = A(r, A_MELEE, 18, 0, 1);                       /* два прямых кулаком */
    K(a, 0.f, {N_SH_R, -8, 0, 50}, {N_EL_R, 0, 0, 100}, {N_SH_L, 8, 0, 50}, {N_EL_L, 0, 0, 100});
    K(a, 0.3f, {N_SH_R, -8, 0, -40}, {N_EL_R, 0, 0, 60}, {N_SH_L, 8, 0, 50}, {N_EL_L, 0, 0, 100}, {N_CHEST, 0, -25, 0});
    K(a, 0.5f, {N_SH_R, -8, 0, 95}, {N_EL_R, 0, 0, 0}, {N_SH_L, 8, 0, 50}, {N_EL_L, 0, 0, 100}, {N_CHEST, 0, 30, -8});
    K(a, 0.8f, {N_SH_R, -8, 0, 50}, {N_EL_R, 0, 0, 100}, {N_SH_L, 8, 0, 95}, {N_EL_L, 0, 0, 0}, {N_CHEST, 0, -20, -6});
    K(a, 1.f, {N_PELVIS, 0, 0, 0});
    a = A(r, A_RANGED, 26, 0, 1);                      /* бросок гантели из-за головы */
    K(a, 0.f, {N_SH_R, -8, 0, 160}, {N_EL_R, 0, 0, 100}, {N_CHEST, 0, 0, 14});
    K(a, 0.2f, {N_SH_R, -8, 0, 165}, {N_EL_R, 0, 0, 110}, {N_CHEST, 0, 0, 16});
    K(a, 0.45f, {N_SH_R, -8, 0, 85}, {N_EL_R, 0, 0, 0}, {N_CHEST, 0, 20, -18});
    K(a, 1.f, {N_PELVIS, 0, 0, 0});
}

static void anims_heavy(Rig *r) {
    Style s = {18, 30, 12, 20, -4, 1.6f, 6,   34, 55, 20, 60, -22, 2.6f, 8,   0, 15, 48, 24};
    build_common(r, &s);
    Anim *a;
    a = A(r, A_MELEE, 40, 0, 1);                       /* прыжок и удар об пол */
    KR(a, 0.f, 0, -6, 0, {N_HIP_L, 0, 0, 40}, {N_HIP_R, 0, 0, 40}, {N_KNEE_L, 0, 0, -80}, {N_KNEE_R, 0, 0, -80}, {N_SH_L, 8, 0, -40}, {N_SH_R, -8, 0, -40}, {N_CHEST, 0, 0, -14});
    K(a, 0.22f, {N_HIP_L, 0, 0, 30}, {N_HIP_R, 0, 0, -10}, {N_KNEE_L, 0, 0, -45}, {N_KNEE_R, 0, 0, -20}, {N_SH_L, 8, 0, 170}, {N_SH_R, -8, 0, 170}, {N_EL_L, 0, 0, 20}, {N_EL_R, 0, 0, 20});
    K(a, 0.78f, {N_HIP_L, 0, 0, 25}, {N_HIP_R, 0, 0, -8}, {N_KNEE_L, 0, 0, -40}, {N_KNEE_R, 0, 0, -20}, {N_SH_L, 8, 0, 175}, {N_SH_R, -8, 0, 175}, {N_EL_L, 0, 0, 20}, {N_EL_R, 0, 0, 20});
    KR(a, 0.9f, 2, -3, 0, {N_HIP_L, 0, 0, 20}, {N_HIP_R, 0, 0, 20}, {N_KNEE_L, 0, 0, -45}, {N_KNEE_R, 0, 0, -45}, {N_SH_L, 8, 0, 55}, {N_SH_R, -8, 0, 55}, {N_CHEST, 0, 0, -34});
    K(a, 1.f, {N_CHEST, 0, 0, -10});
    copy_anim(r, A_RANGED, A_MELEE);
}

static void anims_boss(Rig *r) {
    Style s = {8, 12, 6, 15, -8, 0.6f, 4,   10, 15, 6, 15, -30, 0.4f, 3,   0, 10, 50, 34};
    build_common(r, &s);
    Anim *a;
    /* стоит на летающем сноуборде, посох вертикально (hand rz=180 разворачивает посох вверх) */
    a = A(r, A_IDLE, 70, 1, 1);
    KR(a, 0.f, 0, 0, 0, {N_HIP_L, 0, 0, 14}, {N_HIP_R, 0, 0, -12}, {N_KNEE_L, 0, 0, -10}, {N_SH_R, -8, 0, 8}, {N_EL_R, 0, 0, 20}, {N_HAND_R, 0, 0, 180});
    KR(a, 0.5f, 0, 1.5f, 0, {N_HIP_L, 0, 0, 14}, {N_HIP_R, 0, 0, -12}, {N_KNEE_L, 0, 0, -12}, {N_SH_R, -8, 0, 10}, {N_EL_R, 0, 0, 22}, {N_HAND_R, 0, 0, 180}, {N_CHEST, 0, 0, -2});
    KR(a, 1.f, 0, 0, 0, {N_HIP_L, 0, 0, 14}, {N_HIP_R, 0, 0, -12}, {N_KNEE_L, 0, 0, -10}, {N_SH_R, -8, 0, 8}, {N_EL_R, 0, 0, 20}, {N_HAND_R, 0, 0, 180});
    a = A(r, A_WALK, 40, 1, 1);                        /* скользит: лёгкий наклон */
    KR(a, 0.f, 0, 0, 0, {N_HIP_L, 0, 0, 16}, {N_HIP_R, 0, 0, -14}, {N_KNEE_L, 0, 0, -14}, {N_CHEST, 0, 0, -8}, {N_SH_R, -8, 0, 8}, {N_EL_R, 0, 0, 20}, {N_HAND_R, 0, 0, 180});
    KR(a, 0.5f, 0, 1, 0, {N_HIP_L, 0, 0, 16}, {N_HIP_R, 0, 0, -14}, {N_KNEE_L, 0, 0, -16}, {N_CHEST, 0, 0, -10}, {N_SH_R, -8, 0, 10}, {N_EL_R, 0, 0, 22}, {N_HAND_R, 0, 0, 180});
    KR(a, 1.f, 0, 0, 0, {N_HIP_L, 0, 0, 16}, {N_HIP_R, 0, 0, -14}, {N_KNEE_L, 0, 0, -14}, {N_CHEST, 0, 0, -8}, {N_SH_R, -8, 0, 8}, {N_EL_R, 0, 0, 20}, {N_HAND_R, 0, 0, 180});
    a = A(r, A_RUN, 24, 1, 1);                         /* пикирование: сильный наклон, руки назад */
    KR(a, 0.f, 0, 0, 0, {N_CHEST, 0, 0, -34}, {N_HIP_L, 0, 0, 20}, {N_HIP_R, 0, 0, -22}, {N_KNEE_L, 0, 0, -30}, {N_SH_L, 8, 0, -30}, {N_SH_R, -8, 0, -20}, {N_HAND_R, 0, 0, 180}, {N_HEAD, 0, 0, 20});
    KR(a, 1.f, 0, 0, 0, {N_CHEST, 0, 0, -34}, {N_HIP_L, 0, 0, 20}, {N_HIP_R, 0, 0, -22}, {N_KNEE_L, 0, 0, -30}, {N_SH_L, 8, 0, -30}, {N_SH_R, -8, 0, -20}, {N_HAND_R, 0, 0, 180}, {N_HEAD, 0, 0, 20});
    a = A(r, A_RANGED, 20, 0, 1);                      /* выстрел посохом: посох направлен вперёд (hand rz=0) */
    K(a, 0.f, {N_HIP_L, 0, 0, 14}, {N_HIP_R, 0, 0, -12}, {N_SH_R, -8, 0, 8}, {N_EL_R, 0, 0, 20}, {N_HAND_R, 0, 0, 180});
    K(a, 0.35f, {N_HIP_L, 0, 0, 14}, {N_HIP_R, 0, 0, -12}, {N_SH_R, -8, 0, 60}, {N_EL_R, 0, 0, 40}, {N_HAND_R, 0, 0, 90}, {N_CHEST, 0, 0, 8});
    KR(a, 0.55f, -2, 0, 0, {N_HIP_L, 0, 0, 14}, {N_HIP_R, 0, 0, -12}, {N_SH_R, -8, 0, 92}, {N_EL_R, 0, 0, 0}, {N_HAND_R, 0, 0, 0}, {N_CHEST, 0, 0, 2});
    K(a, 1.f, {N_HIP_L, 0, 0, 14}, {N_HIP_R, 0, 0, -12}, {N_SH_R, -8, 0, 20}, {N_EL_R, 0, 0, 25}, {N_HAND_R, 0, 0, 160});
    copy_anim(r, A_MELEE, A_RANGED);
    /* у остальных анимаций посох тоже держится вертикально */
    for (int i = 0; i < A_COUNT; i++) {
        if (i == A_IDLE || i == A_WALK || i == A_RUN || i == A_RANGED || i == A_MELEE) continue;
        for (int k = 0; k < r->anims[i].n; k++) r->anims[i].k[k].p.rot[N_HAND_R][2] = 180;
    }
    a = A(r, A_DEATH, 60, 0, 1);                       /* летит кувырком */
    K(a, 0.f, {N_HAND_R, 0, 0, 180});
    K(a, 0.4f, {N_PELVIS, 0, 0, -45}, {N_SH_L, 8, 0, 140}, {N_SH_R, -8, 0, 140}, {N_KNEE_L, 0, 0, -50}, {N_KNEE_R, 0, 0, -40}, {N_HAND_R, 0, 0, 180});
    K(a, 1.f, {N_PELVIS, 0, 0, -95}, {N_SH_L, 8, 0, 150}, {N_SH_R, -8, 0, 120}, {N_KNEE_L, 0, 0, -20}, {N_KNEE_R, 0, 0, -30}, {N_HAND_R, 0, 0, 180});
}

void rig_build_anims(Rig *r, int id) {
    switch (id) {
    case RIG_PENGI: case RIG_HERO5: anims_pengi(r); break;
    case RIG_BUM: case RIG_HERO6: anims_bum(r); break;
    case RIG_DOORKEEPER: case RIG_HERO7: anims_doorkeeper(r); break;
    case RIG_MISTRESS: anims_mistress(r); break;
    case RIG_SCOUT: case RIG_ENEMY4: case RIG_ENEMY7: case RIG_ENEMY10: anims_scout(r); break;
    case RIG_BODY: case RIG_ENEMY5: case RIG_ENEMY8: case RIG_ENEMY11: anims_body(r); break;
    case RIG_HEAVY: case RIG_ENEMY6: case RIG_ENEMY9: case RIG_ENEMY12: anims_heavy(r); break;
    case RIG_BOSS: case RIG_BOSS3: case RIG_BOSS4: case RIG_BOSS5:
    case RIG_BOSS6: case RIG_BOSS7: case RIG_BOSS8: case RIG_BOSS9: anims_boss(r); break;
    case RIG_BOSS2: /* Maestro: тромбон (RANGED) + барабаны (SUPER) */
        anims_boss(r);
        {
            Anim *a;
            a = A(r, A_RANGED, 28, 0, 1); /* тромбон вперёд */
            K(a, 0.f, {N_SH_R, -8, 0, 40}, {N_EL_R, 0, 0, 30}, {N_HAND_R, 0, 0, 20}, {N_SH_L, 8, 0, 50}, {N_EL_L, 0, 0, 40}, {N_CHEST, 0, 0, 6});
            KR(a, 0.4f, -1, 0, 0, {N_SH_R, -8, 0, 70}, {N_EL_R, 0, 0, 10}, {N_HAND_R, 0, 0, 0}, {N_SH_L, 8, 0, 60}, {N_CHEST, 0, 0, 10});
            K(a, 1.f, {N_SH_R, -8, 0, 45}, {N_EL_R, 0, 0, 25}, {N_HAND_R, 0, 0, 15}, {N_SH_L, 8, 0, 52});
            a = A(r, A_SUPER, 24, 1, 1); /* барабанщик: руки бьют вниз */
            KR(a, 0.f, 0, 0, 0, {N_SH_L, 8, 0, 100}, {N_EL_L, 0, 0, 20}, {N_SH_R, -8, 0, 100}, {N_EL_R, 0, 0, 20}, {N_HAND_R, 0, 0, 90}, {N_CHEST, 0, 0, 4});
            KR(a, 0.25f, 0, -2, 0, {N_SH_L, 8, 0, 130}, {N_EL_L, 0, 0, 0}, {N_SH_R, -8, 0, 70}, {N_EL_R, 0, 0, 40}, {N_HAND_R, 0, 0, 90}, {N_CHEST, 0, 0, 8});
            KR(a, 0.5f, 0, 0, 0, {N_SH_L, 8, 0, 70}, {N_EL_L, 0, 0, 40}, {N_SH_R, -8, 0, 130}, {N_EL_R, 0, 0, 0}, {N_HAND_R, 0, 0, 90}, {N_CHEST, 0, 0, 4});
            KR(a, 0.75f, 0, -2, 0, {N_SH_L, 8, 0, 120}, {N_EL_L, 0, 0, 5}, {N_SH_R, -8, 0, 80}, {N_EL_R, 0, 0, 35}, {N_HAND_R, 0, 0, 90});
            KR(a, 1.f, 0, 0, 0, {N_SH_L, 8, 0, 100}, {N_EL_L, 0, 0, 20}, {N_SH_R, -8, 0, 100}, {N_EL_R, 0, 0, 20}, {N_HAND_R, 0, 0, 90});
        }
        break;
    default: break;
    }
}
