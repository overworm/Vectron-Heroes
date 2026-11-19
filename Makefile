CC ?= gcc
CFLAGS ?= -O2 -std=gnu99 -Wall -Wextra
CORE = src/main.c src/game.c src/ai.c src/boss.c src/events.c src/actor.c src/rig.c src/rig_anims.c src/level.c src/image.c src/ui.c src/sound.c

ifeq ($(OS),Windows_NT)
  GAME = lostgame.exe
  HEAD = headless.exe
else
  GAME = lostgame_bin
  HEAD = headless
endif

.PHONY: all lostgame headless clean
all: $(GAME) $(HEAD)
lostgame: $(GAME)
headless: $(HEAD)

# Звук и музыка: SDL2_mixer определяется автоматически (MSYS2: pacman -S mingw-w64-x86_64-SDL2_mixer).
#   make lostgame            - со звуком, если SDL2_mixer найден (иначе без звука и с предупреждением)
#   make lostgame MIXER=0    - принудительно без звука
#   make lostgame MIXER=1    - принудительно со звуком (ошибка сборки, если библиотеки нет)
MIXER ?= auto
ifeq ($(MIXER),auto)
  SDL_PREFIX := $(shell sdl2-config --prefix 2>/dev/null)
  ifneq ($(wildcard $(SDL_PREFIX)/include/SDL2/SDL_mixer.h),)
    MIXER := 1
  else
    MIXER := 0
    $(warning SDL2_mixer не найден: игра соберётся БЕЗ ЗВУКА. Установите: pacman -S mingw-w64-x86_64-SDL2_mixer и пересоберите: make clean)
  endif
endif
ifeq ($(MIXER),1)
  CFLAGS += -DHAVE_SDL_MIXER
  MIXER_LIBS = -lSDL2_mixer
endif

$(GAME): $(CORE) src/platform_sdl.c
	$(CC) $(CFLAGS) $^ -o $@ `sdl2-config --cflags --libs` $(MIXER_LIBS) -lm

$(HEAD): $(CORE) src/platform_null.c
	$(CC) $(filter-out -DHAVE_SDL_MIXER,$(CFLAGS)) -DPLAT_FIXED_STEP $^ -o $@ -lm

clean:
	rm -f lostgame.exe lostgame_bin headless headless.exe
