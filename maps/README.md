# LHMP maps for Lost Hero

## Редактор карт (web)
```
python tools/mapedit.py            # откроет http://127.0.0.1:8765
```
- Select/move: перетаскивание, угол объекта = resize, Del = удалить, D = дубликат, стрелки = сдвиг (Shift = 1 px), Ctrl+Z / Ctrl+Y.
- Tile paint / erase / rect: твёрдые плитки (полы, стены). Колёсико = zoom, средняя кнопка или Pan = сдвиг.
- Palette слева: платформы, лифты/мосты, опасности, стены, интерактив (по вашему списку). ⚠ = тип сохраняется в карту, но движок пока его пропускает.
- Checkpoint: клик ставит 1S, 2S, … (порядок = квестовая цепочка; ↑↓ справа меняют порядок; максимум 8).
- Room type: клик по комнате переключает auto / open / closable / dead_end / special / boss (open = строго открытая, стартует исследованной).
- Boss arena: потяни прямоугольник. Quest: галочка у объекта; счётчик N/N справа.
- Save (Ctrl+S) пишет maps/levelN.json (+ .json.bak), «Save + Compile» ещё запускает mapc и кладёт levelN.map в maps/ и assets/maps/.
- Проверки справа: дубли имён, битые target, лифты без y1<y0, чекпойнт не на опоре, количество объектов.

## Компиляция вручную
```
python tools/mapc.py maps/level1.json --dump
python tools/mapc.py --list-types        # каталог объектов, engine / PLANNED
```
## В игре
`level_load()` берёт `assets/maps/levelN.map`, но только при `LHMP=1`; иначе встроенные уровни (в них те же checkpoints). Когда карты полностью совпадут с встроенными, условие `getenv("LHMP")` в level.c можно убрать.

## Формат JSON (добавлено в v97)
- `"rooms": ["auto","open",...]` — 12 типов комнат (колонка*4 + (3 - этаж); на уровне 3: колонка*3 + строка).
- `"checkpoints": [{"x":..,"y":..}, ...]` — по порядку 1S..NS.
- объект можно задать через `"cat": "PLAT_FALL"` (ключ каталога) вместо type/kind.
