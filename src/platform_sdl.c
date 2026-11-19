/* Платформенный слой на SDL2: Windows 10 / Linux / macOS. */
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include "platform.h"
#include <stdio.h>

static SDL_Window *g_win;
static SDL_Renderer *g_ren;
static SDL_Texture *g_tex;
static int g_quit;
static SDL_GameController *g_pad;
static int g_sel_hold;   /* кадры удержания Select */
static int g_sel_tele_fired;

int plat_init(const char *title, int vw, int vh, int ww, int wh) {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL_Init: %s (retry video only)\n", SDL_GetError());
        if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL_Init video: %s\n", SDL_GetError()); return 0; }
    }
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            g_pad = SDL_GameControllerOpen(i);
            if (g_pad) { fprintf(stderr, "Gamepad: %s\n", SDL_GameControllerName(g_pad)); break; }
        }
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    g_win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, ww, wh,
                             SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!g_win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 0; }
    g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g_ren) g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_SOFTWARE);
    if (!g_ren) { fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError()); return 0; }
    SDL_RenderSetLogicalSize(g_ren, vw, vh);
    if (ww % vw == 0 && wh % vh == 0) SDL_RenderSetIntegerScale(g_ren, SDL_TRUE);
    else SDL_RenderSetIntegerScale(g_ren, SDL_FALSE);
    g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, vw, vh);
    return g_tex != NULL;
}

static int pad_btn(SDL_GameControllerButton b) {
    return g_pad && SDL_GameControllerGetButton(g_pad, b);
}
static int pad_axis(SDL_GameControllerAxis a, int neg) {
    if (!g_pad) return 0;
    int v = SDL_GameControllerGetAxis(g_pad, a);
    if (neg) return v < -16000;
    return v > 16000;
}

void plat_poll(Input *in) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) g_quit = 1;
        if (e.type == SDL_CONTROLLERDEVICEADDED && !g_pad) {
            if (SDL_IsGameController(e.cdevice.which))
                g_pad = SDL_GameControllerOpen(e.cdevice.which);
        }
        if (e.type == SDL_CONTROLLERDEVICEREMOVED && g_pad) {
            SDL_JoystickID id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(g_pad));
            if (e.cdevice.which == id) { SDL_GameControllerClose(g_pad); g_pad = NULL; }
        }
    }
    const Uint8 *k = SDL_GetKeyboardState(NULL);

    /* DualShock2-like (актуальная раскладка):
       D-pad L/R walk, Up optional jump, Down slide
       □ Jump | ✕ Use | ○ Melee | △ Super
       R2 Ranged | L2 Special
       L1 prev hero | R1 next hero
       Select tap = Solo/Group | Select hold 1s = Teleport
       Start = Pause
       Cheats: UU DD □○ level+ | DD UU ○□ level- | LL RR □○ quest debug
    */
    int pL = pad_btn(SDL_CONTROLLER_BUTTON_DPAD_LEFT)  || pad_axis(SDL_CONTROLLER_AXIS_LEFTX, 1);
    int pR = pad_btn(SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || pad_axis(SDL_CONTROLLER_AXIS_LEFTX, 0);
    int pU = pad_btn(SDL_CONTROLLER_BUTTON_DPAD_UP);
    int pD = pad_btn(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    int pR2 = pad_axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 0);
    int pL2 = pad_axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 0);
    int pTri = pad_btn(SDL_CONTROLLER_BUTTON_Y);  /* △ */
    int pCir = pad_btn(SDL_CONTROLLER_BUTTON_B);  /* ○ */
    int pSq  = pad_btn(SDL_CONTROLLER_BUTTON_X);  /* □ */
    int pX   = pad_btn(SDL_CONTROLLER_BUTTON_A);  /* ✕ */
    int pL1  = pad_btn(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    int pR1  = pad_btn(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    int pSel = pad_btn(SDL_CONTROLLER_BUTTON_BACK);
    int pSt  = pad_btn(SDL_CONTROLLER_BUTTON_START);

    /* Select: short tap = group, hold 4s (240f) = tele once */
    int pad_group = 0, pad_tele = 0;
    if (pSel) {
        g_sel_hold++;
        if (g_sel_hold >= 60 && !g_sel_tele_fired) {
            pad_tele = 1;
            g_sel_tele_fired = 1;
        }
    } else {
        if (g_sel_hold > 0 && g_sel_hold < 60 && !g_sel_tele_fired)
            pad_group = 1; /* отпускание до 4 с = solo/group */
        g_sel_hold = 0;
        g_sel_tele_fired = 0;
    }

    in->left   = k[SDL_SCANCODE_LEFT]  || k[SDL_SCANCODE_A] || pL;
    in->right  = k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D] || pR;
    in->up     = k[SDL_SCANCODE_UP]    || k[SDL_SCANCODE_W] || pU;
    in->down   = k[SDL_SCANCODE_DOWN]  || k[SDL_SCANCODE_S] || pD;
    in->jump   = k[SDL_SCANCODE_SPACE] || k[SDL_SCANCODE_W] || k[SDL_SCANCODE_UP] || pSq || pU;
    in->melee  = k[SDL_SCANCODE_J] || k[SDL_SCANCODE_Z] || pCir;
    in->ranged = k[SDL_SCANCODE_K] || k[SDL_SCANCODE_X] || pR2;
    in->super  = k[SDL_SCANCODE_L] || k[SDL_SCANCODE_C] || pTri;
    in->special = k[SDL_SCANCODE_V] || pL2;
    in->use    = k[SDL_SCANCODE_E] || k[SDL_SCANCODE_RETURN] || pX;
    in->group  = k[SDL_SCANCODE_Q] || pad_group;
    in->tele   = k[SDL_SCANCODE_T] || pad_tele;
    for (int i = 0; i < 7; i++) in->pick[i] = k[SDL_SCANCODE_1 + i];
    in->next   = k[SDL_SCANCODE_TAB] || pR1;
    in->prev_hero = pL1;
    in->debug  = k[SDL_SCANCODE_F1];
    in->pgup   = k[SDL_SCANCODE_PAGEUP];
    in->pgdn   = k[SDL_SCANCODE_PAGEDOWN];
    in->bosskey = k[SDL_SCANCODE_B];
    in->pause  = k[SDL_SCANCODE_P] || pSt;
    in->quest_dbg = k[SDL_SCANCODE_F8]; /* только чит с геймпада: LL RR □○ */
    in->quit   = g_quit || k[SDL_SCANCODE_ESCAPE];
}

void plat_present(const uint32_t *argb, int w, int h) {
    (void)h;
    SDL_UpdateTexture(g_tex, NULL, argb, w * 4);
    SDL_RenderClear(g_ren);
    SDL_RenderCopy(g_ren, g_tex, NULL, NULL);
    SDL_RenderPresent(g_ren);
}

uint32_t plat_ticks_ms(void) { return SDL_GetTicks(); }
void plat_sleep_ms(uint32_t ms) { SDL_Delay(ms); }

void plat_shutdown(void) {
    if (g_pad) { SDL_GameControllerClose(g_pad); g_pad = NULL; }
    if (g_tex) SDL_DestroyTexture(g_tex);
    if (g_ren) SDL_DestroyRenderer(g_ren);
    if (g_win) SDL_DestroyWindow(g_win);
    SDL_Quit();
}
