#include "rig.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void rig_build_anims(Rig *r, int id);      /* rig_anims.c */

static Rig g_rigs[RIG_COUNT];
const Rig *rig_get(int id) { return &g_rigs[id]; }

static const char *ANIM_NAMES[A_COUNT] = {
    "idle", "walk", "run", "jump", "land", "fall", "glide", "slide",
    "activate", "climb", "melee", "ranged", "super", "hurt", "death", "falldmg"
};
const char *rig_anim_name(int a) { return ANIM_NAMES[a]; }
int rig_anim_dur(const Rig *r, int a) { return r->spr_ok ? r->spr_n[a] * r->spr_dur[a] : r->anims[a].dur; }

/* ---------------------------------------------------------------- построение узлов */
typedef struct {
    float thigh, shin, foot_r, hip_w, hip_drop, foot_x;
    float torso, neck, upper, fore, sh_w, sh_h;
    float pelvis_r, chest_r, chest_seg, head_r, neck_seg, arm_r, arm_seg, hand_r, leg_r, leg_seg;
    int s_pelvis, s_chest, s_head, s_arm, s_hand, s_leg, s_shin, s_foot;
} Spec;

static void nset(Rig *r, int i, int parent, float x, float y, float z, float rad, float seg, int slot, int sslot, const char *name) {
    Node *n = &r->nodes[i];
    n->parent = parent; n->off[0] = x; n->off[1] = y; n->off[2] = z;
    n->r = rad; n->seg_r = seg; n->slot = slot; n->seg_slot = sslot;
    snprintf(n->name, sizeof n->name, "%s", name);
}

static void build_base(Rig *r, const Spec *s) {
    float py = s->hip_drop + s->thigh + s->shin + s->foot_r;
    nset(r, N_PELVIS, -1, 0, py, 0, s->pelvis_r, 0, s->s_pelvis, s->s_pelvis, "pelvis");
    nset(r, N_CHEST, 0, 0, s->torso, 0, s->chest_r, s->chest_seg, s->s_chest, s->s_chest, "chest");
    nset(r, N_HEAD, 1, 0, s->neck, 0, s->head_r, s->neck_seg, s->s_head, s->s_head, "head");
    for (int side = 0; side < 2; side++) {                     /* 0 = левая (дальняя, z<0), 1 = правая */
        float z = side ? s->sh_w : -s->sh_w, hz = side ? s->hip_w : -s->hip_w;
        int a = N_SH_L + side * 3, h = N_HIP_L + side * 3;
        nset(r, a, 1, 0, s->sh_h, z, s->arm_r, 0, s->s_arm, s->s_arm, side ? "shoulder_r" : "shoulder_l");
        nset(r, a + 1, a, 0, -s->upper, 0, s->arm_r, s->arm_seg, s->s_arm, s->s_arm, side ? "elbow_r" : "elbow_l");
        nset(r, a + 2, a + 1, 0, -s->fore, 0, s->hand_r, s->arm_seg * 0.9f, s->s_hand, s->s_arm, side ? "hand_r" : "hand_l");
        nset(r, h, 0, 0, -s->hip_drop, hz, s->leg_r, 0, s->s_leg, s->s_leg, side ? "hip_r" : "hip_l");
        nset(r, h + 1, h, 0, -s->thigh, 0, s->leg_r, s->leg_seg, s->s_leg, s->s_leg, side ? "knee_r" : "knee_l");
        nset(r, h + 2, h + 1, s->foot_x, -s->shin, 0, s->foot_r, s->leg_seg * 0.9f, s->s_foot, s->s_shin, side ? "foot_r" : "foot_l");
    }
    r->n = N_BASE_COUNT;
}
static void extra(Rig *r, int parent, float x, float y, float z, float rad, int slot) {
    nset(r, r->n, parent, x, y, z, rad, 0, slot, slot, "extra");
    r->n++;
}
static void add_prop(Rig *r, int node, unsigned mask, const PBall *b, int n) {
    Prop *p = &r->props[r->nprops++];
    p->node = node; p->n = n; p->anim_mask = mask;
    memcpy(p->b, b, sizeof(PBall) * (size_t)n);
}
#define BIT(a) (1u << (a))
#define ALLA 0xFFFFu
#define INK (-1)           /* слот "тёмная краска": глаза и очки */

static void pal(Rig *r, uint32_t skin, uint32_t top, uint32_t pants, uint32_t shoe, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t metal) {
    uint32_t c[SL_COUNT] = {skin, top, pants, shoe, a1, a2, a3, metal};
    memcpy(r->pal.col, c, sizeof c);
}

/* ---------------------------------------------------------------- герои */
static void build_pengi(Rig *r) {
    Spec s = {2.5f, 2.5f, 2.8f, 2.6f, 1.5f, 2.0f,  6.0f, 8.0f, 3.5f, 3.5f, 6.5f, 3.0f,
              6.5f, 7.6f, 6.5f, 4.8f, 4.0f, 2.4f, 2.2f, 2.4f, 2.4f, 2.4f,
              SL_TOP, SL_TOP, SL_SKIN, SL_TOP, SL_TOP, SL_SHOE, SL_SHOE, SL_SHOE};
    r->name = "pengi"; r->scale = 1.f;
    build_base(r, &s);
    extra(r, 1, 3.6f, -1.2f, 0, 6.4f, SL_SKIN);           /* 15 брюшко */
    extra(r, 0, 3.2f, 1.0f, 0, 5.6f, SL_SKIN);            /* 16 */
    extra(r, 2, 4.6f, -0.8f, 0, 1.7f, SL_SHOE);           /* 17 клюв */
    extra(r, 2, 0.0f, 3.0f, 0, 6.4f, SL_ACC1);            /* 18 шапка-ушанка */
    extra(r, 2, -1.5f, 3.5f, 3.6f, 3.8f, SL_ACC2);        /* 19 мех */
    extra(r, 2, -2.0f, -0.5f, 4.4f, 2.8f, SL_ACC2);       /* 20 ухо шапки */
    extra(r, 2, 2.5f, -1.0f, 3.0f, 1.2f, SL_ACC3);        /* 21 румянец */
    extra(r, 2, 3.2f, 0.8f, 2.4f, 0.9f, INK);             /* 22 глаз */
    pal(r, 0xEFE6C8, 0x3D5278, 0x34476A, 0xE0803A, 0x5A4B3C, 0xDCD0AC, 0xD84848, 0x707A88);
    PBall gun[] = {{0, 2, 0, 2.6f, SL_METAL}, {0, -2, 0, 3.2f, SL_METAL}, {0, -7, 0, 2.8f, SL_ACC3}, {0, -11, 0, 2.6f, SL_METAL},
                   {0, -14, 0, 2.6f, SL_ACC3}, {0, -17, 0, 2.2f, SL_METAL}, {0, -20, 0, 1.8f, SL_SKIN}};
    add_prop(r, N_HAND_R, BIT(A_RANGED) | BIT(A_SUPER), gun, 7);
}

static void build_bum(Rig *r) {
    Spec s = {6.5f, 5.5f, 3.2f, 2.4f, 2.0f, 2.0f,  6.0f, 6.8f, 5.5f, 5.2f, 4.4f, 3.2f,
              4.8f, 5.6f, 4.2f, 4.4f, 1.8f, 2.2f, 2.1f, 2.2f, 2.6f, 2.7f,
              SL_TOP, SL_TOP, SL_SKIN, SL_TOP, SL_SKIN, SL_PANTS, SL_PANTS, SL_SHOE};
    r->name = "bum"; r->scale = 1.f;
    build_base(r, &s);
    extra(r, 2, 0.5f, 3.6f, 0, 4.6f, SL_ACC1);            /* 15 кепка */
    extra(r, 2, 3.6f, 2.6f, 0, 2.0f, SL_ACC1);            /* 16 козырёк */
    extra(r, 2, 2.6f, -3.2f, 0, 3.4f, SL_ACC2);           /* 17 борода */
    extra(r, 2, 4.4f, -0.4f, 0, 1.9f, SL_ACC3);           /* 18 красный нос */
    extra(r, 2, -3.2f, 0.0f, 0, 3.0f, SL_ACC2);           /* 19 волосы */
    extra(r, 0, 0.0f, -2.0f, 0, 5.4f, SL_TOP);            /* 20 полы куртки */
    extra(r, 1, 1.5f, -3.0f, 3.8f, 1.2f, SL_METAL);       /* 21 бутылка в кармане */
    extra(r, 2, 3.0f, 0.9f, 1.9f, 0.9f, INK);             /* 22 глаз */
    pal(r, 0xD9A27E, 0x6E7440, 0x34456A, 0x6B4A2C, 0x7B735F, 0x6B4B2E, 0xD65A5A, 0xB4BAC4);
    PBall wr[] = {{0, -2, 0, 1.4f, SL_METAL}, {0, -6, 0, 1.4f, SL_METAL}, {0, -10, 0, 1.5f, SL_METAL}, {0, -13, 0, 3.0f, SL_METAL},
                  {1.8f, -16, 0, 1.6f, SL_METAL}, {-1.8f, -16, 0, 1.6f, SL_METAL}};
    add_prop(r, N_HAND_R, BIT(A_MELEE) | BIT(A_SUPER) | BIT(A_ACTIVATE), wr, 6);
    PBall bt[] = {{0, -3, 0, 2.0f, SL_ACC3}, {0, -6.5f, 0, 1.5f, SL_ACC3}, {0, -8.5f, 0, 1.0f, SL_METAL}};
    add_prop(r, N_HAND_R, BIT(A_RANGED), bt, 3);
}

static void build_doorkeeper(Rig *r) {
    Spec s = {5.0f, 4.5f, 3.6f, 3.4f, 2.0f, 2.0f,  5.0f, 7.0f, 5.0f, 4.6f, 5.6f, 3.0f,
              7.4f, 8.0f, 6.5f, 4.6f, 2.4f, 2.8f, 2.8f, 2.4f, 3.2f, 3.3f,
              SL_TOP, SL_TOP, SL_SKIN, SL_TOP, SL_SKIN, SL_PANTS, SL_PANTS, SL_SHOE};
    r->name = "doorkeeper"; r->scale = 1.f;
    build_base(r, &s);
    extra(r, 0, 0.5f, -2.5f, 0, 7.4f, SL_TOP);            /* 15 подол пальто */
    extra(r, 0, 0.5f, -5.5f, 0, 7.0f, SL_ACC2);           /* 16 меховая кайма */
    extra(r, 1, -7.0f, 0.5f, 0, 6.0f, SL_ACC3);           /* 17 рюкзак */
    extra(r, 1, -7.0f, -3.5f, 0, 4.5f, SL_ACC3);          /* 18 */
    extra(r, 2, 0.0f, 3.5f, 0, 5.2f, SL_ACC1);            /* 19 вязаная шапка */
    extra(r, 2, -0.5f, 8.5f, 0, 3.0f, SL_ACC2);           /* 20 помпон */
    extra(r, 2, -3.4f, -1.0f, 0, 3.4f, SL_PANTS);         /* 21 волосы */
    extra(r, 1, 0.5f, 3.5f, 0, 4.6f, SL_ACC2);            /* 22 воротник */
    extra(r, 2, 3.2f, 0.6f, 1.8f, 0.9f, INK);             /* 23 глаз */
    pal(r, 0xC98E6B, 0x46618A, 0x2E3340, 0x7A6240, 0x7A5A3C, 0xB89B66, 0x68703A, 0x9AA0A8);
    PBall br[] = {{0, 6, 0, 1.2f, SL_ACC1}, {0, 2, 0, 1.2f, SL_ACC1}, {0, -2, 0, 1.2f, SL_ACC1}, {0, -6, 0, 1.2f, SL_ACC1},
                  {0, -10, 0, 1.2f, SL_ACC1}, {0, -14, 0, 1.3f, SL_ACC1}, {0, -17, 0, 2.4f, SL_ACC2}, {0, -20, 0, 3.4f, SL_ACC2},
                  {0, -23, 0, 4.2f, SL_ACC2}, {0, -26, 0, 4.4f, SL_ACC1}};
    for (int i = 0; i < 10; i++) br[i].y += 14.f;           /* веник стоит на земле, ручка выше руки */
    add_prop(r, N_HAND_R, ALLA & ~(BIT(A_CLIMB) | BIT(A_DEATH) | BIT(A_FALLDMG)), br, 10);
}

static void build_mistress(Rig *r) {
    Spec s = {8.5f, 8.5f, 2.4f, 2.2f, 2.0f, 1.6f,  6.5f, 7.0f, 6.0f, 5.6f, 3.8f, 3.2f,
              4.2f, 4.6f, 3.6f, 3.6f, 1.6f, 1.8f, 1.8f, 1.9f, 2.3f, 2.4f,
              SL_PANTS, SL_TOP, SL_SKIN, SL_TOP, SL_TOP, SL_PANTS, SL_SHOE, SL_SHOE};
    r->name = "mistress"; r->scale = 1.f;
    build_base(r, &s);
    extra(r, 2, -1.5f, 1.5f, 0, 4.0f, SL_ACC1);           /* 15 волосы */
    extra(r, 2, -4.5f, -3.0f, 0, 3.4f, SL_ACC1);          /* 16 */
    extra(r, 1, -5.5f, -1.0f, 0, 3.2f, SL_ACC1);          /* 17 */
    extra(r, 1, -6.5f, -6.0f, 0, 2.8f, SL_ACC1);          /* 18 */
    extra(r, 1, -6.5f, -11.0f, 0, 2.4f, SL_ACC1);         /* 19 */
    extra(r, 2, 2.8f, 0.8f, 1.8f, 1.7f, SL_ACC2);         /* 20 розовые очки */
    extra(r, 1, 2.4f, 0.2f, 0, 3.0f, SL_TOP);             /* 21 грудь */
    pal(r, 0xC8906A, 0x23232B, 0x1C1C22, 0x7A4C2A, 0x7B4326, 0xC060A0, 0x9A9AA6, 0xBFC5CD);
    PBall cs[] = {{0, 2, 0, 3.2f, SL_SHOE}, {0, -3, 0, 3.4f, SL_SHOE}, {0, -7, 0, 2.2f, SL_METAL}, {0, -11, 0, 2.2f, SL_METAL},
                  {0, -15, 0, 2.2f, SL_METAL}, {0, -19, 0, 2.2f, SL_METAL}, {0, -23, 0, 2.2f, SL_METAL}, {0, -27, 0, 2.2f, SL_METAL}};
    add_prop(r, N_HAND_R, BIT(A_MELEE) | BIT(A_SUPER) | BIT(A_ACTIVATE), cs, 8);
}

/* ---------------------------------------------------------------- враги */
static void build_scout(Rig *r) {
    Spec s = {7.0f, 6.0f, 3.2f, 2.4f, 2.0f, 2.0f,  6.0f, 6.6f, 5.4f, 5.0f, 4.6f, 3.0f,
              4.4f, 5.2f, 4.0f, 4.4f, 1.8f, 2.2f, 2.1f, 2.2f, 2.4f, 2.5f,
              SL_PANTS, SL_TOP, SL_ACC2, SL_TOP, SL_SKIN, SL_PANTS, SL_PANTS, SL_SHOE};
    r->name = "scout"; r->scale = 1.f;
    build_base(r, &s);
    extra(r, 2, -0.6f, 1.2f, 0, 5.8f, SL_ACC1);           /* 15 капюшон */
    extra(r, 2, -3.5f, -1.5f, 0, 3.2f, SL_ACC1);          /* 16 */
    extra(r, 2, 2.6f, 0.4f, 0, 2.0f, SL_ACC3);            /* 17 золотая кайма */
    extra(r, 1, -4.5f, 1.0f, -3.2f, 1.6f, SL_ACC3);       /* 18 ракета за спиной */
    extra(r, 1, -4.5f, 4.6f, -3.2f, 1.1f, SL_SHOE);       /* 19 */
    extra(r, 2, 2.4f, 0.0f, 0, 3.6f, SL_ACC2);            /* 20 тёмное лицо в капюшоне */
    pal(r, 0xB07A55, 0xF0F0EE, 0xC8CAD2, 0xB02A2A, 0xF4F4F0, 0x0E0E1A, 0xD8A828, 0x8A8E98);
    PBall rl[] = {{0, 9, 0, 2.4f, SL_ACC3}, {0, 5, 0, 2.6f, SL_ACC3}, {0, 1, 0, 2.6f, SL_ACC3}, {0, -3, 0, 2.6f, SL_SHOE},
                  {0, -7, 0, 2.6f, SL_ACC3}, {0, -11, 0, 2.6f, SL_SHOE}, {0, -15, 0, 2.6f, SL_ACC3}, {0, -18.5f, 0, 2.2f, SL_SHOE},
                  {0, -21.5f, 0, 1.4f, SL_SHOE}, {-2, 12, 0, 2.2f, SL_SHOE}, {2, 12, 0, 2.2f, SL_SHOE}};
    add_prop(r, N_HAND_R, ALLA & ~(BIT(A_MELEE) | BIT(A_DEATH) | BIT(A_FALLDMG)), rl, 11);
}

static void build_body_common(Rig *r, const char *name, float scale) {
    Spec s = {7.0f, 6.5f, 3.4f, 3.0f, 2.0f, 2.2f,  6.5f, 6.0f, 6.0f, 5.4f, 7.0f, 3.2f,
              5.4f, 7.6f, 5.6f, 4.4f, 2.6f, 3.6f, 3.6f, 3.2f, 3.3f, 3.4f,
              SL_PANTS, SL_TOP, SL_ACC2, SL_SKIN, SL_SKIN, SL_PANTS, SL_PANTS, SL_SHOE};
    r->name = name; r->scale = scale;
    build_base(r, &s);
    extra(r, 2, -0.5f, 1.0f, 0, 5.4f, SL_ACC1);           /* 15 капюшон/шлем */
    extra(r, 2, 2.2f, 0.0f, 0, 3.4f, SL_ACC2);            /* 16 тёмное лицо */
    extra(r, 1, 0.0f, 4.5f, 0, 4.0f, SL_TOP);             /* 17 воротник */
    extra(r, 1, -2.5f, -1.0f, 0, 7.2f, SL_TOP);           /* 18 спина */
    extra(r, 1, 0.0f, 2.5f, -6.5f, 3.8f, SL_TOP);         /* 19 плечо */
    extra(r, 1, 0.0f, 2.5f, 6.5f, 3.8f, SL_TOP);          /* 20 плечо */
    PBall db[] = {{0, -3, -3.2f, 3.0f, SL_METAL}, {0, -3, -1.0f, 1.3f, SL_ACC3}, {0, -3, 1.0f, 1.3f, SL_ACC3}, {0, -3, 3.2f, 3.0f, SL_METAL}};
    unsigned m = ALLA & ~(BIT(A_MELEE) | BIT(A_DEATH) | BIT(A_FALLDMG));
    add_prop(r, N_HAND_R, m, db, 4);
    add_prop(r, N_HAND_L, m, db, 4);
}
static void build_body(Rig *r) {
    build_body_common(r, "body", 1.f);
    pal(r, 0xD6A27C, 0x1E2B58, 0x1E2B58, 0xB0302A, 0x2C323E, 0x08080E, 0xE8E8EE, 0x4A4E58);
}
static void build_heavy(Rig *r) {
    build_body_common(r, "heavy", 1.5f);
    pal(r, 0xD6A27C, 0x5A1E2A, 0x45161F, 0x222226, 0x1E1E24, 0x08080E, 0xE8C8C8, 0x3A3A42);
}

static void build_boss(Rig *r) {
    Spec s = {7.0f, 6.5f, 3.0f, 2.4f, 2.0f, 2.0f,  6.5f, 6.8f, 5.5f, 5.2f, 4.4f, 3.2f,
              5.0f, 5.4f, 4.2f, 4.3f, 1.8f, 2.2f, 2.2f, 2.2f, 2.6f, 2.7f,
              SL_TOP, SL_TOP, SL_SKIN, SL_TOP, SL_SKIN, SL_PANTS, SL_PANTS, SL_SHOE};
    r->name = "boss"; r->scale = 1.f;
    build_base(r, &s);
    extra(r, 0, 0.0f, -3.5f, 0, 5.6f, SL_TOP);            /* 15 длинный халат */
    extra(r, 0, 0.0f, -8.0f, 0, 5.0f, SL_TOP);            /* 16 */
    extra(r, 2, 0.0f, 3.2f, 0, 5.4f, SL_ACC1);            /* 17 ушанка */
    extra(r, 2, -1.0f, -1.5f, -4.6f, 2.6f, SL_ACC1);      /* 18 */
    extra(r, 2, -1.0f, -1.5f, 4.6f, 2.6f, SL_ACC1);       /* 19 */
    extra(r, 2, 3.4f, -1.4f, 0, 2.6f, SL_ACC2);           /* 20 маска */
    extra(r, 2, 3.6f, 1.0f, 0, 1.6f, INK);                /* 21 тёмные очки */
    extra(r, 1, 1.8f, 3.4f, 0, 2.0f, SL_ACC3);            /* 22 воротник */
    pal(r, 0xC89070, 0xE8E8F4, 0x1C1C24, 0x14141A, 0x4A4C56, 0x9CD2E2, 0x2E8A7A, 0xB8C0CC);
    PBall st[] = {{0, 16, 0, 1.2f, SL_ACC1}, {0, 12, 0, 1.2f, SL_ACC1}, {0, 8, 0, 1.2f, SL_ACC1}, {0, 4, 0, 1.2f, SL_ACC1},
                  {0, 0, 0, 1.2f, SL_ACC1}, {0, -4, 0, 1.2f, SL_ACC1}, {0, -8, 0, 1.2f, SL_ACC1}, {0, -12, 0, 2.6f, SL_ACC2},
                  {0, -16, 0, 1.2f, SL_ACC1}, {0, -20, 0, 1.2f, SL_ACC1}, {0, -24, 0, 1.2f, SL_ACC1}, {1.6f, -27, 0, 1.2f, SL_ACC1},
                  {3.6f, -27.5f, 0, 1.2f, SL_ACC1}, {5.2f, -25.5f, 0, 1.2f, SL_ACC1}};
    add_prop(r, N_HAND_R, ALLA, st, 14);
    PBall bd[] = {{-13, -19.5f, 0, 2.3f, SL_ACC2}, {-9, -19.5f, 0, 2.5f, SL_METAL}, {-5, -19.5f, 0, 2.5f, SL_METAL}, {-1, -19.5f, 0, 2.5f, SL_METAL},
                  {3, -19.5f, 0, 2.5f, SL_METAL}, {7, -19.5f, 0, 2.5f, SL_METAL}, {11, -19.5f, 0, 2.5f, SL_METAL}, {14, -19, 0, 2.2f, SL_ACC2}};
    add_prop(r, N_PELVIS, ALLA, bd, 8);
}

void rigs_init(void) {
    memset(g_rigs, 0, sizeof g_rigs);
    build_pengi(&g_rigs[RIG_PENGI]);
    build_bum(&g_rigs[RIG_BUM]);
    build_doorkeeper(&g_rigs[RIG_DOORKEEPER]);
    build_mistress(&g_rigs[RIG_MISTRESS]);
    build_scout(&g_rigs[RIG_SCOUT]);
    build_body(&g_rigs[RIG_BODY]);
    build_heavy(&g_rigs[RIG_HEAVY]);
    build_boss(&g_rigs[RIG_BOSS]);
    /* заглушки будущих героев/врагов/боссов (спрайты позже): копируем ближайших */
    g_rigs[RIG_HERO5] = g_rigs[RIG_PENGI]; g_rigs[RIG_HERO5].name = "nerd";
    g_rigs[RIG_HERO6] = g_rigs[RIG_BUM];   g_rigs[RIG_HERO6].name = "refreezer";
    g_rigs[RIG_HERO7] = g_rigs[RIG_DOORKEEPER]; g_rigs[RIG_HERO7].name = "batyscaphe";
    for (int i = RIG_ENEMY4; i <= RIG_ENEMY12; i++) {
        g_rigs[i] = g_rigs[RIG_SCOUT + ((i - RIG_ENEMY4) % 3)];
        g_rigs[i].name = "enemy";
    }
    for (int i = RIG_BOSS2; i <= RIG_BOSS9; i++) {
        g_rigs[i] = g_rigs[RIG_BOSS];
        g_rigs[i].name = "boss";
    }
    /* Maestro: тромбон не в SUPER; вместо палочек для барабанов */
    {
        Rig *m = &g_rigs[RIG_BOSS2];
        for (int pi = 0; pi < m->nprops; pi++)
            m->props[pi].anim_mask &= ~BIT(A_SUPER); /* убрать посох/тромбон на барабанах */
        PBall stick[] = {
            {0, 0, 0, 1.6f, SL_METAL}, {0, -4, 0, 1.4f, SL_METAL},
            {0, -8, 0, 1.3f, SL_ACC2}, {0, -11, 0, 1.8f, SL_ACC2}
        };
        add_prop(m, N_HAND_R, BIT(A_SUPER), stick, 4);
        add_prop(m, N_HAND_L, BIT(A_SUPER), stick, 4);
    }
    static const char *RN[RIG_COUNT] = {"pengi", "cyberbum", "aunty", "domina", "rocketpunk", "dumbpunk", "ramrod", "professor",
        "nerd", "refreezer", "hero7", "deliver", "firewalker", "monoman", "ironfem", "megadog", "capsulo", "enemy10", "enemy11", "enemy12",
        "maestro", "bigpussy", "coach", "hacker", "watcher", "boot", "superdrom", "drunky"};
    for (int i = 0; i < RIG_COUNT; i++) { g_rigs[i].name = RN[i]; rig_build_anims(&g_rigs[i], i); }
}

/* ---------------------------------------------------------------- матрицы 3x3 */
typedef struct { float m[9]; } M3;
static M3 m_mul(M3 a, M3 b) {
    M3 o;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            o.m[i * 3 + j] = a.m[i * 3] * b.m[j] + a.m[i * 3 + 1] * b.m[3 + j] + a.m[i * 3 + 2] * b.m[6 + j];
    return o;
}
static M3 m_euler(float rx, float ry, float rz) {                /* R = Rz * Ry * Rx */
    const float d = 3.14159265f / 180.f;
    float cx = cosf(rx * d), sx = sinf(rx * d), cy = cosf(ry * d), sy = sinf(ry * d), cz = cosf(rz * d), sz = sinf(rz * d);
    M3 X = {{1, 0, 0, 0, cx, -sx, 0, sx, cx}};
    M3 Y = {{cy, 0, sy, 0, 1, 0, -sy, 0, cy}};
    M3 Z = {{cz, -sz, 0, sz, cz, 0, 0, 0, 1}};
    return m_mul(Z, m_mul(Y, X));
}
static void m_vec(const M3 *a, const float *v, float *o) {
    o[0] = a->m[0] * v[0] + a->m[1] * v[1] + a->m[2] * v[2];
    o[1] = a->m[3] * v[0] + a->m[4] * v[1] + a->m[5] * v[2];
    o[2] = a->m[6] * v[0] + a->m[7] * v[1] + a->m[8] * v[2];
}

/* ---------------------------------------------------------------- проигрыватель */
#define BLEND_FRAMES 5
static void pose_lerp(Pose *o, const Pose *a, const Pose *b, float t) {
    float *po = (float *)o; const float *pa = (const float *)a, *pb = (const float *)b;
    for (size_t i = 0; i < sizeof(Pose) / sizeof(float); i++) po[i] = pa[i] + (pb[i] - pa[i]) * t;
}
static void anim_eval(const Anim *an, float tn, Pose *out) {
    int i = 0;
    while (i < an->n - 2 && tn > an->k[i + 1].t) i++;
    float span = an->k[i + 1].t - an->k[i].t;
    float u = span > 0 ? (tn - an->k[i].t) / span : 0;
    if (u < 0) u = 0;
    if (u > 1) u = 1;
    if (an->ease) u = u * u * (3.f - 2.f * u);
    pose_lerp(out, &an->k[i].p, &an->k[i + 1].p, u);
}
void rig_pose_at(const Rig *r, int anim, float tn, Pose *out) { anim_eval(&r->anims[anim], tn, out); }

void animator_init(Animator *a, const Rig *r, int id) {
    memset(a, 0, sizeof *a);
    a->rig = r; a->id = id; a->speed = 1.f;
    if (!r->spr_ok) anim_eval(&r->anims[id], 0.f, &a->cur);
}
void animator_play(Animator *a, int id) {
    if (a->id == id) return;
    a->from = a->cur; a->blend = a->rig->spr_ok ? 0 : BLEND_FRAMES;
    a->id = id; a->time = 0; a->done = 0;
}
void animator_restart(Animator *a, int id) {
    a->from = a->cur; a->blend = a->rig->spr_ok ? 0 : 2;
    a->id = id; a->time = 0; a->done = 0;
}
void animator_update(Animator *a) {
    if (a->rig->spr_ok) {                                 /* спрайты: время в тиках, кадр выбирается при рисовании */
        float total = (float)(a->rig->spr_n[a->id] * a->rig->spr_dur[a->id]);
        a->time += a->speed;
        if (a->rig->spr_loop[a->id]) { while (a->time >= total) a->time -= total; }
        else if (a->time >= total) { a->time = total; a->done = 1; }
        return;
    }
    const Anim *an = &a->rig->anims[a->id];
    a->time += a->speed;
    if (an->loop) { while (a->time >= (float)an->dur) a->time -= (float)an->dur; }
    else if (a->time >= (float)an->dur) { a->time = (float)an->dur; a->done = 1; }
    Pose p;
    anim_eval(an, a->time / (float)an->dur, &p);
    if (a->blend > 0) {
        pose_lerp(&a->cur, &a->from, &p, 1.f - (float)a->blend / (float)BLEND_FRAMES);
        a->blend--;
    } else a->cur = p;
}

/* ---------------------------------------------------------------- рисование шариков */
typedef struct { float x, y, z, r; int slot; } Ball;
#define MAX_BALLS 360
#define INK_RGB 0x1A1420u

static void ball_flat(Image *fb, float cx, float cy, float r, uint32_t col) {
    int x0 = (int)floorf(cx - r), x1 = (int)ceilf(cx + r), y0 = (int)floorf(cy - r), y1 = (int)ceilf(cy + r);
    float r2 = r * r;
    for (int y = y0; y < y1; y++) {
        if (y < 0 || y >= fb->h) continue;
        for (int x = x0; x < x1; x++) {
            if (x < 0 || x >= fb->w) continue;
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            if (dx * dx + dy * dy <= r2) fb->px[y * fb->w + x] = col;
        }
    }
}
static void ball_shaded(Image *fb, float cx, float cy, float r, uint32_t rgb, int flash) {
    const float LX = -0.45f, LY = 0.55f, LZ = 0.70f;
    int x0 = (int)floorf(cx - r), x1 = (int)ceilf(cx + r), y0 = (int)floorf(cy - r), y1 = (int)ceilf(cy + r);
    float r2 = r * r;
    for (int y = y0; y < y1; y++) {
        if (y < 0 || y >= fb->h) continue;
        for (int x = x0; x < x1; x++) {
            if (x < 0 || x >= fb->w) continue;
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy, d2 = dx * dx + dy * dy;
            if (d2 > r2) continue;
            float nz = sqrtf(1.f - d2 / r2), nx = dx / r, ny = -dy / r;
            float lam = nx * LX + ny * LY + nz * LZ;
            float s = lam < 0.05f ? 0.50f : lam < 0.40f ? 0.72f : lam < 0.75f ? 0.92f : 1.10f;     /* 4 ступени, "cel" */
            int rr = (int)(((rgb >> 16) & 255) * s), gg = (int)(((rgb >> 8) & 255) * s), bb = (int)((rgb & 255) * s);
            if (rr > 255) rr = 255;
            if (gg > 255) gg = 255;
            if (bb > 255) bb = 255;
            rr += (255 - rr) * flash / 255; gg += (255 - gg) * flash / 255; bb += (255 - bb) * flash / 255;
            fb->px[y * fb->w + x] = ARGB(255, rr, gg, bb);
        }
    }
}

void model_draw(Image *fb, const Rig *rig, const Pose *pose, int anim, int sx, int sy, int facing, int flash) {
    float pos[MAX_NODES][3];
    M3 R[MAX_NODES];
    M3 view = m_euler(0.f, facing > 0 ? 25.f : 205.f, 0.f);

    for (int i = 0; i < rig->n; i++) {
        const Node *n = &rig->nodes[i];
        M3 loc = m_euler(pose->rot[i][0], pose->rot[i][1], pose->rot[i][2]);
        if (n->parent < 0) {
            float o[3] = {n->off[0] + pose->root[0], n->off[1] + pose->root[1], n->off[2] + pose->root[2]};
            R[i] = m_mul(view, loc);
            m_vec(&view, o, pos[i]);
        } else {
            int p = n->parent;
            float t[3];
            R[i] = m_mul(R[p], loc);
            m_vec(&R[p], n->off, t);
            pos[i][0] = pos[p][0] + t[0]; pos[i][1] = pos[p][1] + t[1]; pos[i][2] = pos[p][2] + t[2];
        }
    }

    Ball balls[MAX_BALLS];
    int nb = 0;
    for (int i = 0; i < rig->n && nb < MAX_BALLS - 24; i++) {
        const Node *n = &rig->nodes[i];
        balls[nb++] = (Ball){pos[i][0], pos[i][1], pos[i][2], n->r, n->slot};
        if (n->seg_r > 0 && n->parent >= 0) {                  /* кость = цепочка шариков */
            const float *a = pos[n->parent], *b = pos[i];
            float dx = b[0] - a[0], dy = b[1] - a[1], dz = b[2] - a[2];
            float len = sqrtf(dx * dx + dy * dy + dz * dz);
            int cnt = (int)ceilf(len / (n->seg_r * 0.8f));
            for (int k = 1; k < cnt && nb < MAX_BALLS - 24; k++) {
                float t = (float)k / (float)cnt;
                balls[nb++] = (Ball){a[0] + dx * t, a[1] + dy * t, a[2] + dz * t, n->seg_r, n->seg_slot};
            }
        }
    }
    for (int pi = 0; pi < rig->nprops; pi++) {                 /* реквизит: шарики в системе узла */
        const Prop *pr = &rig->props[pi];
        if (!(pr->anim_mask & (1u << anim))) continue;
        for (int k = 0; k < pr->n && nb < MAX_BALLS; k++) {
            float w[3];
            float v[3] = {pr->b[k].x, pr->b[k].y, pr->b[k].z};
            m_vec(&R[pr->node], v, w);
            balls[nb++] = (Ball){pos[pr->node][0] + w[0], pos[pr->node][1] + w[1], pos[pr->node][2] + w[2], pr->b[k].r, pr->b[k].slot};
        }
    }

    float sc = rig->scale;
    for (int i = 0; i < nb; i++) { balls[i].x *= sc; balls[i].y *= sc; balls[i].z *= sc; balls[i].r *= sc; }

    for (int i = 0; i < nb; i++) ball_flat(fb, sx + balls[i].x, sy - balls[i].y, balls[i].r + 1.0f, 0xFF14101Cu);
    int order[MAX_BALLS];
    for (int i = 0; i < nb; i++) {
        int j = i;
        while (j > 0 && balls[order[j - 1]].z > balls[i].z) { order[j] = order[j - 1]; j--; }
        order[j] = i;
    }
    for (int k = 0; k < nb; k++) {
        const Ball *b = &balls[order[k]];
        uint32_t c = b->slot < 0 ? INK_RGB : rig->pal.col[b->slot];
        ball_shaded(fb, sx + b->x, sy - b->y, b->r, c, flash);
    }
}

/* ---------------------------------------------------------------- файл .anim (текст)
   # комментарий
   rig <имя>
   node <имя> <родитель|-1> <ox> <oy> <oz> <радиус> <радиус_кости> <слот> <слот_кости>   (слоты: skin top pants shoe acc1 acc2 acc3 metal ink)
   anim <имя> <длительность_кадров_60Гц> <loop 0/1> <ease 0/1>
   key <t 0..1> <root x y z> <rx ry rz для каждого узла по порядку>
   Родитель раньше потомка; узел 0 - корень. Реквизит и палитра остаются встроенными. */
static const char *SLOT_NAMES[SL_COUNT + 1] = {"skin", "top", "pants", "shoe", "acc1", "acc2", "acc3", "metal", "ink"};
static int slot_parse(const char *w) {
    for (int i = 0; i <= SL_COUNT; i++) if (!strcmp(w, SLOT_NAMES[i])) return i == SL_COUNT ? INK : i;
    return SL_SKIN;
}
static const char *slot_name(int s) { return s < 0 ? "ink" : SLOT_NAMES[s]; }

int rig_save(const Rig *r, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return 0;
    fprintf(f, "# Lost Hero rig: skeleton + animations. Angles in degrees, 1 unit = 1 px, feet at y=0.\n");
    fprintf(f, "# Axes: X forward, Y up, Z toward viewer. rz>0 on a hanging limb swings it forward.\n");
    fprintf(f, "rig %s\n", r->name);
    for (int i = 0; i < r->n; i++) {
        const Node *n = &r->nodes[i];
        fprintf(f, "node %s %d %.3f %.3f %.3f %.3f %.3f %s %s\n", n->name, n->parent, n->off[0], n->off[1], n->off[2], n->r, n->seg_r,
                slot_name(n->slot), slot_name(n->seg_slot));
    }
    for (int a = 0; a < A_COUNT; a++) {
        const Anim *an = &r->anims[a];
        fprintf(f, "\nanim %s %d %d %d\n", ANIM_NAMES[a], an->dur, an->loop, an->ease);
        for (int k = 0; k < an->n; k++) {
            const Pose *p = &an->k[k].p;
            fprintf(f, "key %.4f %.3f %.3f %.3f", an->k[k].t, p->root[0], p->root[1], p->root[2]);
            for (int i = 0; i < r->n; i++) fprintf(f, "  %.3f %.3f %.3f", p->rot[i][0], p->rot[i][1], p->rot[i][2]);
            fprintf(f, "\n");
        }
    }
    fclose(f);
    return 1;
}

int rig_load(int id, const char *path) {
    Rig *r = &g_rigs[id];
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    static Node nn[MAX_NODES];
    static Anim na[A_COUNT];
    static char line[16384];
    int nc = 0, cur = -1, have[A_COUNT] = {0}, lineno = 0, ok = 1;
    memset(nn, 0, sizeof nn);
    memset(na, 0, sizeof na);
    while (ok && fgets(line, sizeof line, f)) {
        lineno++;
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\r' || !*p) continue;
        char tok[32];
        int n = 0;
        if (sscanf(p, "%31s%n", tok, &n) != 1) continue;
        p += n;
        if (!strcmp(tok, "rig")) continue;
        if (!strcmp(tok, "node")) {
            char name[64], s1[32], s2[32];
            Node nd;
            memset(&nd, 0, sizeof nd);
            if (nc >= MAX_NODES || sscanf(p, "%63s %d %f %f %f %f %f %31s %31s", name, &nd.parent, &nd.off[0], &nd.off[1], &nd.off[2],
                                          &nd.r, &nd.seg_r, s1, s2) != 9 ||
                nd.parent >= nc || nd.parent < -1 || (nc == 0) != (nd.parent == -1)) {
                fprintf(stderr, "[rig] %s:%d: bad node line (max %d nodes, parent must come first, only node 0 is root)\n", path, lineno, MAX_NODES);
                ok = 0; break;
            }
            nd.slot = slot_parse(s1); nd.seg_slot = slot_parse(s2);
            snprintf(nd.name, sizeof nd.name, "%.15s", name);
            nn[nc++] = nd;
        } else if (!strcmp(tok, "anim")) {
            char name[32];
            int dur, loop, ease, aid = -1;
            if (sscanf(p, "%31s %d %d %d", name, &dur, &loop, &ease) != 4 || dur <= 0) ok = 0;
            for (int i = 0; ok && i < A_COUNT; i++) if (!strcmp(name, ANIM_NAMES[i])) aid = i;
            if (!ok || aid < 0 || have[aid]) { fprintf(stderr, "[rig] %s:%d: bad or duplicate anim line\n", path, lineno); ok = 0; break; }
            have[aid] = 1; cur = aid;
            na[aid].dur = dur; na[aid].loop = loop != 0; na[aid].ease = ease != 0;
        } else if (!strcmp(tok, "key")) {
            if (cur < 0 || nc == 0 || na[cur].n >= MAX_KEYS) { fprintf(stderr, "[rig] %s:%d: key outside anim or too many keys (max %d)\n", path, lineno, MAX_KEYS); ok = 0; break; }
            Key *k = &na[cur].k[na[cur].n];
            float v[4 + MAX_NODES * 3];
            int good = 1;
            const char *q = p;
            for (int i = 0; i < 4 + nc * 3 && good; i++) { char *e; v[i] = strtof(q, &e); if (e == q) good = 0; q = e; }
            if (!good || (na[cur].n > 0 && v[0] < na[cur].k[na[cur].n - 1].t)) {
                fprintf(stderr, "[rig] %s:%d: bad key (need t + root xyz + %d rotations, t ascending)\n", path, lineno, nc);
                ok = 0; break;
            }
            k->t = v[0];
            k->p.root[0] = v[1]; k->p.root[1] = v[2]; k->p.root[2] = v[3];
            for (int i = 0; i < nc; i++) { k->p.rot[i][0] = v[4 + i * 3]; k->p.rot[i][1] = v[5 + i * 3]; k->p.rot[i][2] = v[6 + i * 3]; }
            na[cur].n++;
        } else { fprintf(stderr, "[rig] %s:%d: unknown keyword '%s'\n", path, lineno, tok); ok = 0; }
    }
    fclose(f);
    for (int i = 0; ok && i < A_COUNT; i++)
        if (!have[i] || na[i].n < 2) { fprintf(stderr, "[rig] %s: anim '%s' missing or has <2 keys\n", path, ANIM_NAMES[i]); ok = 0; }
    if (ok && nc < N_BASE_COUNT) { fprintf(stderr, "[rig] %s: need at least %d nodes\n", path, N_BASE_COUNT); ok = 0; }
    if (!ok) { fprintf(stderr, "[rig] %s rejected, keeping built-in rig\n", path); return 0; }
    memcpy(r->nodes, nn, sizeof(Node) * (size_t)nc);
    r->n = nc;
    memcpy(r->anims, na, sizeof na);
    return 1;
}

/* ---------------------------------------------------------------- растровые спрайты */
int rig_load_sprites(int id, const char *dir) {
    Rig *r = &g_rigs[id];
    char p[512], line[160];
    snprintf(p, sizeof p, "%s/spr_%s.txt", dir, r->name);
    FILE *f = fopen(p, "r");
    if (!f) return 0;
    int cw = 0, ch = 0, cols = 0, n = 0;
    int st[A_COUNT] = {0}, cnt[A_COUNT] = {0}, du[A_COUNT] = {0}, lp[A_COUNT] = {0};
    while (fgets(line, sizeof line, f)) {
        int i, a, b, c, d, e;
        if (sscanf(line, "cell %d %d %d", &a, &b, &c) == 3) { cw = a; ch = b; cols = c; }
        else if (sscanf(line, "anim %d %d %d %d %d", &i, &a, &b, &c, &d) == 5) { (void)e; if (i >= 0 && i < A_COUNT) { st[i] = a; cnt[i] = b; du[i] = c; lp[i] = d; n++; } }
    }
    fclose(f);
    if (!cw || !ch || !cols || n < A_COUNT) return 0;
    snprintf(p, sizeof p, "%s/spr_%s.bmp", dir, r->name);
    Image im;
    if (!image_load_bmp(&im, p)) return 0;
    if (r->spr.px) image_free(&r->spr);
    r->spr = im; r->spr_cw = cw; r->spr_ch = ch; r->spr_cols = cols;
    for (int i = 0; i < A_COUNT; i++) { r->spr_start[i] = st[i]; r->spr_n[i] = cnt[i] > 0 ? cnt[i] : 1; r->spr_dur[i] = du[i] > 0 ? du[i] : 1; r->spr_loop[i] = lp[i]; }
    r->spr_ok = 1;
    r->spr_flip = strcmp(r->name, "ramrod") == 0;      /* лист Ramrod нарисован лицом влево */
    return 1;
}

static void sprite_draw(Image *fb, const Rig *r, const Animator *an, int sx, int sy, int facing, int flash) {
    int id = an->id;
    if (r->spr_flip) facing = -facing;
    int n = r->spr_n[id], k = (int)(an->time / (float)r->spr_dur[id]);
    if (k >= n) k = n - 1;
    if (k < 0) k = 0;
    int idx = r->spr_start[id] + k, col = idx % r->spr_cols, row = idx / r->spr_cols;
    int cw = r->spr_cw, ch = r->spr_ch;
    /* draw row by row from sheet */
    int ax = cw / 2, ay = ch - 3;
    for (int y = 0; y < ch; y++) {
        int dy = sy - 1 - (ay - y);
        if (dy < 0 || dy >= fb->h) continue;
        const uint32_t *rowp = r->spr.px + (row * ch + y) * r->spr.w + col * cw;
        for (int x = 0; x < cw; x++) {
            uint32_t c = rowp[x];
            if (!(c >> 24)) continue;
            int dx = facing >= 0 ? sx + (x - ax) : sx - (x - ax);
            if (dx < 0 || dx >= fb->w) continue;
            if (flash) {
                int rr = (c >> 16) & 255, gg = (c >> 8) & 255, bb = c & 255;
                rr += (255 - rr) * flash / 255; gg += (255 - gg) * flash / 255; bb += (255 - bb) * flash / 255;
                c = ARGB(255, rr, gg, bb);
            }
            fb->px[dy * fb->w + dx] = c;
        }
    }
}

void model_draw_anim(Image *fb, const Rig *rig, const Animator *an, int sx, int sy, int facing, int flash) {
    if (rig->spr_ok) sprite_draw(fb, rig, an, sx, sy, facing, flash);
    else model_draw(fb, rig, &an->cur, an->id, sx, sy, facing, flash);
}
