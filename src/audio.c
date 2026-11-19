#include "audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *const SFX_NAMES[SFX_COUNT] = {
    "jump", "land", "hurt", "death", "swing", "hit", "shot", "super", "throw", "rocket", "explode",
    "button", "repair", "lift", "wall", "pickup", "door", "switch", "hook", "crash", "boss", "spin", "win", "step"
};

static char g_music[64];
static int g_lx, g_ly;
const char *audio_current_music(void) { return g_music; }
void audio_listener(int x, int y) { g_lx = x; g_ly = y; }

static int dist_volume(int x, int y, int *pan) {                     /* 0..128 */
    int dx = x - g_lx, dy = y - g_ly;
    int d = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy) / 2;
    if (pan) { int p = 127 + dx * 127 / 260; *pan = p < 0 ? 0 : p > 254 ? 254 : p; }
    if (d <= 170) return 128;
    if (d >= 520) return 0;
    return 128 * (520 - d) / 350;
}

#ifdef USE_MIXER
#include <SDL.h>
#include <SDL_mixer.h>

static Mix_Chunk *g_chunk[SFX_COUNT];
static Mix_Music *g_mus;
static char g_root[400];
static int g_ok;

static int exists(const char *p) { FILE *f = fopen(p, "rb"); if (!f) return 0; fclose(f); return 1; }
static int find_file(char *out, size_t n, const char *dir, const char *name) {
    static const char *ext[] = {".mp3", ".ogg", ".wav"};
    for (int i = 0; i < 3; i++) { snprintf(out, n, "%s/%s/%s%s", g_root, dir, name, ext[i]); if (exists(out)) return 1; }
    return 0;
}

int audio_init(const char *root) {
    snprintf(g_root, sizeof g_root, "%s", root);
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) { fprintf(stderr, "[audio] SDL audio: %s\n", SDL_GetError()); return 0; }
    Mix_Init(MIX_INIT_MP3 | MIX_INIT_OGG);
    if (Mix_OpenAudio(32000, MIX_DEFAULT_FORMAT, 2, 1024) != 0) { fprintf(stderr, "[audio] Mix_OpenAudio: %s\n", Mix_GetError()); return 0; }
    Mix_AllocateChannels(8);                                         /* как 8 голосов SNES */
    int n = 0;
    for (int i = 0; i < SFX_COUNT; i++) {
        char p[512];
        if (find_file(p, sizeof p, "sfx", SFX_NAMES[i])) { g_chunk[i] = Mix_LoadWAV(p); if (g_chunk[i]) n++; else fprintf(stderr, "[audio] %s: %s\n", p, Mix_GetError()); }
    }
    fprintf(stderr, "[audio] эффектов загружено: %d из %d\n", n, SFX_COUNT);
    Mix_VolumeMusic(88);
    g_ok = 1;
    return 1;
}

void audio_shutdown(void) {
    if (!g_ok) return;
    Mix_HaltMusic(); if (g_mus) Mix_FreeMusic(g_mus);
    for (int i = 0; i < SFX_COUNT; i++) if (g_chunk[i]) Mix_FreeChunk(g_chunk[i]);
    Mix_CloseAudio(); Mix_Quit();
    g_ok = 0;
}

static void play(int id, int vol, int pan) {
    if (!g_ok || id < 0 || id >= SFX_COUNT || !g_chunk[id] || vol <= 0) return;
    int ch = Mix_PlayChannel(-1, g_chunk[id], 0);
    if (ch < 0) return;
    Mix_Volume(ch, vol);
    Mix_SetPanning(ch, (Uint8)(254 - pan), (Uint8)pan);
}
void audio_sfx(int id) { play(id, 128, 127); }
void audio_sfx_at(int id, int x, int y) { int pan; int v = dist_volume(x, y, &pan); play(id, v, pan); }

void audio_music(const char *name, int loop) {
    if (!g_ok) return;
    if (name && !strcmp(name, g_music)) return;
    Mix_HaltMusic();
    if (g_mus) { Mix_FreeMusic(g_mus); g_mus = NULL; }
    g_music[0] = 0;
    if (!name) return;
    char p[512];
    if (!find_file(p, sizeof p, "bgm", name)) return;
    g_mus = Mix_LoadMUS(p);
    if (!g_mus) { fprintf(stderr, "[audio] %s: %s\n", p, Mix_GetError()); return; }
    snprintf(g_music, sizeof g_music, "%s", name);
    Mix_PlayMusic(g_mus, loop ? -1 : 1);
}
#else
/* без SDL_mixer (headless и сборка NOAUDIO=1): только журнал, если задан AUDIO_LOG */
int audio_init(const char *root) { (void)root; return 0; }
void audio_shutdown(void) {}
void audio_sfx(int id) { if (getenv("AUDIO_LOG")) fprintf(stderr, "sfx %s\n", SFX_NAMES[id]); }
void audio_sfx_at(int id, int x, int y) { if (getenv("AUDIO_LOG") && dist_volume(x, y, NULL) > 0) fprintf(stderr, "sfx %s @%d,%d\n", SFX_NAMES[id], x, y); }
void audio_music(const char *name, int loop) {
    (void)loop;
    if (name && !strcmp(name, g_music)) return;
    snprintf(g_music, sizeof g_music, "%s", name ? name : "");
    if (getenv("AUDIO_LOG")) fprintf(stderr, "music %s\n", name ? name : "(тишина)");
}
#endif
