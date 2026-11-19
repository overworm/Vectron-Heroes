# Lost Hero Map Package (LHMP v1)

## Contents
- `docs/LHMP_SPEC.md` — binary format specification
- `include/lhmp.h` — C header for loaders (PC / PS1 / N64 / …)
- `tools/mapc.py` — JSON → `.map` compiler (Python 3.10+)
- `maps/level1.json` — Level 1 authoring source
- `maps/level1.map` — compiled binary (after mapc)

## Compile level 1 (Windows)
```bat
cd tools
python mapc.py ..\maps\level1.json --dump
```
Output: `maps\level1.map`

## Profiles
```bat
python mapc.py ..\maps\level1.json --profile snes
python mapc.py ..\maps\level1.json --profile ps1
python mapc.py ..\maps\level1.json --profile pc
```

## Next step (game build)
C side still uses `build_level1()` in `level.c`. After you confirm the `.map`:
1. Add `lhmp.h` to the project
2. Implement `level_load_map(Level*, path)` filling tiles/objects from LHMP
3. Call it from `level_load()` when `level1.map` is present, else fallback to `build_level1()`

No engine change is required to **inspect** the pipeline — only mapc + JSON.
