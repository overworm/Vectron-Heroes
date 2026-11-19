Музыка (mp3/ogg/wav):
  level1.mp3  — уровень
  boss.mp3    — босс
  win.mp3     — победа

Сборка БЕЗ музыки (по умолчанию, всегда компилируется):
  make lostgame

Сборка С музыкой:
  pacman -S mingw-w64-x86_64-SDL2_mixer
  make clean
  make lostgame MIXER=1

Файл level1.mp3 должен лежать в assets/bgm/ рядом с exe.
В консоли при успехе: "BGM loaded: assets/bgm/level1.mp3"
