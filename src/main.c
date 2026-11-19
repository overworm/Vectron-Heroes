#include "game.h"
#include "config.h"
#include "image.h"
#include "level.h"
#include "platform.h"
#include "rig.h"
#include "sound.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

/* --export-assets: сохранить передний план (fg.bmp) и анимации (*.anim) как основу для своих */
static void export_assets(const char *dir) {
    char p[512];
    MKDIR(dir);
    Level lv;
    if (level_load(&lv, dir, 1)) { snprintf(p, sizeof p, "%s/fg.bmp", dir); level_save_fg(&lv, p); level_free(&lv); }
    for (int i = 0; i < RIG_COUNT; i++) { const Rig *r = rig_get(i); snprintf(p, sizeof p, "%s/%s.anim", dir, r->name); rig_save(r, p); }
    printf("Saved fg.bmp (black = transparent) and *.anim into %s/\n", dir);
}

int g_debug_quest_ini = 0;
/* settings.ini: scale=N  или  window_w=.. window_h=..  (рядом с exe) */
static void load_settings(int *ww, int *wh) {
    *ww = WIN_W; *wh = WIN_H;
    FILE *f = fopen("settings.ini", "r");
    if (!f) return;
    char line[256];
    int scale = 4, fw = 0, fh = 0;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == ';' || line[0] == '\n') continue;
        char key[64]; int val;
        if (sscanf(line, " %63[^=]= %d", key, &val) == 2) {
            if (!strcmp(key, "scale")) scale = val;
            else if (!strcmp(key, "window_w")) fw = val;
            else if (!strcmp(key, "window_h")) fh = val;
            else if (!strcmp(key, "debug_quest_mode")) {
                extern int g_debug_quest_ini; g_debug_quest_ini = val;
            }
        }
    }
    fclose(f);
    if (fw > 0 && fh > 0) { *ww = fw; *wh = fh; }
    else if (scale >= 1 && scale <= 8) { *ww = VIEW_W * scale; *wh = VIEW_H * scale; }
}

int main(int argc, char **argv) {
    const char *dir = "assets";
    int export_only = 0, sx = -1, sy = -1;
    int ww = WIN_W, wh = WIN_H;
    load_settings(&ww, &wh);
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--export-assets")) export_only = 1;
        else if (!strcmp(argv[i], "--assets") && i + 1 < argc) dir = argv[++i];
        else if (!strcmp(argv[i], "--start-x") && i + 1 < argc) sx = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--start-y") && i + 1 < argc) sy = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc) {
            int sc = atoi(argv[++i]);
            if (sc >= 1 && sc <= 8) { ww = VIEW_W * sc; wh = VIEW_H * sc; }
        }
        else if (!strcmp(argv[i], "--window") && i + 1 < argc) {
            int w = 0, h = 0;
            if (sscanf(argv[++i], "%dx%d", &w, &h) == 2 && w > 0 && h > 0) { ww = w; wh = h; }
        }
    }
    rigs_init();
    for (int i = 0; i < RIG_COUNT; i++) {          /* свои анимации из Blender/редактора: assets/<имя>.anim заменяют встроенные */
        char p[512]; snprintf(p, sizeof p, "%s/%s.anim", dir, rig_get(i)->name);
        FILE *f = fopen(p, "r");
        if (f) { fclose(f); if (!export_only) rig_load(i, p); }
    }
    for (int i = 0; i < RIG_COUNT; i++) rig_load_sprites(i, dir);      /* растровые спрайты вместо шариков, если лежат в assets/ */
    if (export_only) { export_assets(dir); return 0; }
    printf("Window %dx%d (internal %dx%d)\n", ww, wh, VIEW_W, VIEW_H);
    if (!plat_init(GAME_TITLE, VIEW_W, VIEW_H, ww, wh)) return 1;
    game_set_start(sx, sy);
    sound_init(dir);
    if (!game_init(dir)) { fprintf(stderr, "game_init failed\n"); sound_shutdown(); plat_shutdown(); return 1; }
    sound_play_bgm(BGM_LEVEL);
    Input in;
    memset(&in, 0, sizeof in);
#ifdef PLAT_FIXED_STEP
    for (;;) {
        plat_poll(&in);
        if (in.quit) break;
        game_update(&in);
        game_render();
        plat_present(game_pixels(), VIEW_W, VIEW_H);
    }
#else
    uint32_t prev = plat_ticks_ms(), acc = 0;
    for (;;) {
        uint32_t now = plat_ticks_ms();
        acc += (now - prev) * TICKS_PER_SEC; prev = now;
        if (acc > 250u * TICKS_PER_SEC) acc = 250u * TICKS_PER_SEC;
        plat_poll(&in);
        if (in.quit) break;
        while (acc >= 1000) { game_update(&in); acc -= 1000; }
        game_render();
        plat_present(game_pixels(), VIEW_W, VIEW_H);
        plat_sleep_ms(1);
    }
#endif
    game_shutdown();
    sound_shutdown();
    plat_shutdown();
    return 0;
}
