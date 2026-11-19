/* Звук: SDL2_mixer (mp3/wav/ogg). Без MIXER — тихие заглушки.
   Сборка с музыкой: make lostgame MIXER=1
   MSYS2: pacman -S mingw-w64-x86_64-SDL2_mixer
*/
#include "sound.h"
#include "rig.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef HAVE_SDL_MIXER
#include <SDL_mixer.h>
#endif

static float sfx_vol = 0.8f, bgm_vol = 0.6f;
static int inited;
static char assets[512];

#ifdef HAVE_SDL_MIXER
static Mix_Chunk *common_chunk[SFX_COMMON_COUNT];
static Mix_Chunk *actor_chunk[RIG_COUNT][SFX_ACT_COUNT];
static Mix_Chunk *generic_chunk[SFX_ACT_COUNT];      /* общий звук, если у персонажа нет своего файла */
static Mix_Music *bgm_music[BGM_COUNT];
static int mixer_ok;
static void play_chunk(Mix_Chunk *c, int vol, int pan);
#endif

static const char *actor_sfx_name(int kind) { return (kind >= 0 && kind < RIG_COUNT) ? rig_get(kind)->name : "generic"; }

static const char *act_suffix(int act) {
    static const char *s[] = {"step","jump","land","melee","ranged","super","hurt","death"};
    if (act < 0 || act >= SFX_ACT_COUNT) return "step";
    return s[act];
}

static const char *common_name(int id) {
    static const char *s[] = {"pickup","door","panel","ui","explode","enemy_hit","zap","freeze","splash","flame","warn"};
    if (id < 0 || id >= SFX_COMMON_COUNT) return "ui";
    return s[id];
}

#ifdef HAVE_SDL_MIXER
/* пробует .mp3 и .wav */
static Mix_Chunk *load_chunk(const char *dir, const char *file) {
    char p[640];
    snprintf(p, sizeof p, "%s/sfx/%s.mp3", dir, file);
    Mix_Chunk *c = Mix_LoadWAV(p);
    if (c) return c;
    snprintf(p, sizeof p, "%s/sfx/%s.wav", dir, file);
    return Mix_LoadWAV(p);
}

static Mix_Music *load_music(const char *dir, const char *file) {
    char p[640];
    static const char *ext[] = {"mp3", "ogg", "wav", "mod", "mid", NULL};
    /* несколько корней: assets, ./assets, cwd */
    const char *roots[6];
    int nr = 0;
    roots[nr++] = dir;
    roots[nr++] = "assets";
    roots[nr++] = "./assets";
    roots[nr++] = "../assets";
    for (int r = 0; r < nr; r++) {
        for (int i = 0; ext[i]; i++) {
            snprintf(p, sizeof p, "%s/bgm/%s.%s", roots[r], file, ext[i]);
            Mix_Music *m = Mix_LoadMUS(p);
            if (m) { fprintf(stderr, "BGM loaded: %s\n", p); return m; }
            fprintf(stderr, "BGM try fail: %s (%s)\n", p, Mix_GetError());
        }
    }
    return NULL;
}
#endif

void sound_init(const char *assets_dir) {
    if (assets_dir) snprintf(assets, sizeof assets, "%s", assets_dir);
    else snprintf(assets, sizeof assets, "assets");
    inited = 1;

#ifdef HAVE_SDL_MIXER
    int mixflags = MIX_INIT_MP3 | MIX_INIT_OGG;
    int got = Mix_Init(mixflags);
    if ((got & MIX_INIT_MP3) == 0)
        fprintf(stderr, "Mix_Init: MP3 not available (%s). Try OGG or install libmpg123.\n", Mix_GetError());
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024) != 0) {
        fprintf(stderr, "Mix_OpenAudio: %s\n", Mix_GetError());
        mixer_ok = 0;
        return;
    }
    Mix_AllocateChannels(16);
    mixer_ok = 1;

    for (int i = 0; i < SFX_COMMON_COUNT; i++)
        common_chunk[i] = load_chunk(assets, common_name(i));

    for (int a = 0; a < SFX_ACT_COUNT; a++) {
        char file[128];
        snprintf(file, sizeof file, "generic_%s", act_suffix(a));
        generic_chunk[a] = load_chunk(assets, file);
    }
    for (int k = 0; k < RIG_COUNT; k++) {
        const char *nm = actor_sfx_name(k);
        for (int a = 0; a < SFX_ACT_COUNT; a++) {
            char file[128];
            snprintf(file, sizeof file, "%s_%s", nm, act_suffix(a));
            actor_chunk[k][a] = load_chunk(assets, file);
        }
    }

    /* level: сначала level1, потом level */
    bgm_music[BGM_LEVEL] = load_music(assets, "level1");
    if (!bgm_music[BGM_LEVEL]) bgm_music[BGM_LEVEL] = load_music(assets, "level");
    bgm_music[BGM_BOSS] = load_music(assets, "boss");
    bgm_music[BGM_WIN]  = load_music(assets, "win");
    bgm_music[BGM_GAMEOVER] = load_music(assets, "gameover");

    Mix_Volume(-1, (int)(sfx_vol * MIX_MAX_VOLUME));
    Mix_VolumeMusic((int)(bgm_vol * MIX_MAX_VOLUME));
    {
        int nfx = 0;
        for (int i = 0; i < SFX_COMMON_COUNT; i++) nfx += common_chunk[i] != NULL;
        for (int a = 0; a < SFX_ACT_COUNT; a++) nfx += generic_chunk[a] != NULL;
        for (int k = 0; k < RIG_COUNT; k++) for (int a = 0; a < SFX_ACT_COUNT; a++) nfx += actor_chunk[k][a] != NULL;
        fprintf(stderr, "Sound init OK (SDL2_mixer), звуковых файлов загружено: %d (искали в %s/sfx)\n", nfx, assets);
    }
#else
    fprintf(stderr, "Sound: built without SDL2_mixer (silent). Rebuild with MIXER=1\n");
#endif
}

void sound_shutdown(void) {
#ifdef HAVE_SDL_MIXER
    if (mixer_ok) {
        Mix_HaltMusic();
        Mix_HaltChannel(-1);
        for (int i = 0; i < SFX_COMMON_COUNT; i++)
            if (common_chunk[i]) { Mix_FreeChunk(common_chunk[i]); common_chunk[i] = NULL; }
        for (int k = 0; k < RIG_COUNT; k++)
            for (int a = 0; a < SFX_ACT_COUNT; a++)
                if (actor_chunk[k][a]) { Mix_FreeChunk(actor_chunk[k][a]); actor_chunk[k][a] = NULL; }
        for (int a = 0; a < SFX_ACT_COUNT; a++)
            if (generic_chunk[a]) { Mix_FreeChunk(generic_chunk[a]); generic_chunk[a] = NULL; }
        for (int i = 0; i < BGM_COUNT; i++)
            if (bgm_music[i]) { Mix_FreeMusic(bgm_music[i]); bgm_music[i] = NULL; }
        Mix_CloseAudio();
        Mix_Quit();
        mixer_ok = 0;
    }
#endif
    inited = 0;
}

void sound_play_sfx(int id) {
#ifdef HAVE_SDL_MIXER
    if (!mixer_ok || id < 0 || id >= SFX_COMMON_COUNT) return;
    play_chunk(common_chunk[id], 128, 127);
#else
    (void)id;
#endif
}

static int g_lx, g_ly;
void sound_listener(int x, int y) { g_lx = x; g_ly = y; }

#ifdef HAVE_SDL_MIXER
/* громкость 0..128 и панорама 0..254 по расстоянию до слушателя */
static int spatial(int x, int y, int *pan) {
    int dx = x - g_lx, dy = y - g_ly;
    int d = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy) / 2;
    int p = 127 + dx * 127 / 280; *pan = p < 0 ? 0 : p > 254 ? 254 : p;
    if (d <= 170) return 128;
    if (d >= 520) return 0;
    return 128 * (520 - d) / 350;
}
static void play_chunk(Mix_Chunk *c, int vol, int pan) {
    if (!c || vol <= 0) return;
    int ch = Mix_PlayChannel(-1, c, 0);
    if (ch < 0) return;
    Mix_Volume(ch, (int)(vol * sfx_vol * MIX_MAX_VOLUME / 128));
    Mix_SetPanning(ch, (Uint8)(254 - pan), (Uint8)pan);
}
static Mix_Chunk *actor_chunk_of(int kind, int act) {
    Mix_Chunk *c = actor_chunk[kind][act];
    return c ? c : generic_chunk[act];
}
#endif

void sound_play_actor(int kind, int act) {
#ifdef HAVE_SDL_MIXER
    if (!mixer_ok || act < 0 || act >= SFX_ACT_COUNT || kind < 0 || kind >= RIG_COUNT) return;
    play_chunk(actor_chunk_of(kind, act), 128, 127);
#else
    (void)kind; (void)act;
#endif
}

void sound_play_actor_at(int kind, int act, int x, int y) {
#ifdef HAVE_SDL_MIXER
    if (!mixer_ok || act < 0 || act >= SFX_ACT_COUNT || kind < 0 || kind >= RIG_COUNT) return;
    int pan; int v = spatial(x, y, &pan);
    play_chunk(actor_chunk_of(kind, act), v, pan);
#else
    (void)kind; (void)act; (void)x; (void)y;
#endif
}

void sound_play_sfx_at(int id, int x, int y) {
#ifdef HAVE_SDL_MIXER
    if (!mixer_ok || id < 0 || id >= SFX_COMMON_COUNT) return;
    int pan; int v = spatial(x, y, &pan);
    play_chunk(common_chunk[id], v, pan);
#else
    (void)id; (void)x; (void)y;
#endif
}

void sound_play_bgm(int id) {
#ifdef HAVE_SDL_MIXER
    if (!mixer_ok) {
        fprintf(stderr, "sound_play_bgm: mixer not ok (rebuild with MIXER=1 and SDL2_mixer)\n");
        return;
    }
    if (id < 0 || id >= BGM_COUNT) return;
    if (!bgm_music[id]) {
        fprintf(stderr, "BGM %d not loaded. Put level1.mp3 in assets/bgm/ next to the exe.\n", id);
        return;
    }
    if (Mix_PlayingMusic()) Mix_HaltMusic();
    /* win — один раз; остальная музыка зациклена */
    int loops = (id == BGM_WIN) ? 0 : -1;
    if (Mix_PlayMusic(bgm_music[id], loops) != 0)
        fprintf(stderr, "Mix_PlayMusic: %s\n", Mix_GetError());
    else
        fprintf(stderr, "BGM playing id=%d loops=%d\n", id, loops);
#else
    fprintf(stderr, "sound_play_bgm: built WITHOUT SDL2_mixer. Run: make lostgame MIXER=1\n");
    (void)id;
#endif
}

void sound_stop_bgm(void) {
#ifdef HAVE_SDL_MIXER
    if (mixer_ok) Mix_HaltMusic();
#endif
}

void sound_pause_all(int paused) {
#ifdef HAVE_SDL_MIXER
    if (!mixer_ok) return;
    if (paused) {
        Mix_PauseMusic();
        Mix_Pause(-1);
    } else {
        Mix_ResumeMusic();
        Mix_Resume(-1);
    }
#else
    (void)paused;
#endif
}

void sound_set_sfx_vol(float v) {
    sfx_vol = v < 0 ? 0 : v > 1 ? 1 : v;
#ifdef HAVE_SDL_MIXER
    if (mixer_ok) Mix_Volume(-1, (int)(sfx_vol * MIX_MAX_VOLUME));
#endif
}

void sound_set_bgm_vol(float v) {
    bgm_vol = v < 0 ? 0 : v > 1 ? 1 : v;
#ifdef HAVE_SDL_MIXER
    if (mixer_ok) Mix_VolumeMusic((int)(bgm_vol * MIX_MAX_VOLUME));
#endif
}
