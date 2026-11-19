# LHMP — Lost Hero Map Format v1

## Purpose
Portable level format for Lost Hero. Same binary loads on PC, PS1, PS2, N64,
Saturn, SNES, Jaguar and any other C target.

Authoring: JSON (UTF-8) → `tools/mapc.py` → binary `.map` (little-endian).
Runtime: C loader reads only `.map` (no JSON on console).

## Units
- Coordinates in **pixels**, origin top-left, Y down (same as current engine).
- Default tile size: **16×16**.
- Floors (surface Y): 560, 432, 304, 176 (`FLOOR1..4_Y`).
- No floating point in binary. Fixed-point used only in engine.

## File layout (little-endian)

```
Offset  Size  Field
0       4     magic      "LHMP"
4       2     version    1
6       2     flags      bit0 = has collision layer (mask)
8       2     width      tiles X
10      2     height     tiles Y
12      1     tile_w     usually 16
13      1     tile_h     usually 16
14      1     n_layers   1..4 (0=tiles solid, 1=deco optional)
15      1     n_cp       checkpoints (0..8)
16      2     n_obj      objects
18      2     n_spawn    enemy spawn points
20      1     n_quest    quest entries (indices into objects[] or special)
21      1     n_hero     hero spawn slots (usually 7)
22      2     map_id     1..N
24      2     bg_id      background asset id
26      2     boss_id    boss kind id (0 = none)
28      2     arena_x0   boss trigger / arena (pixels)
30      2     arena_x1
32      2     arena_floor
34      2     arena_ceil
36      2     arena_bx0  flight bounds
38      2     arena_bx1
40      8     reserved   zero
48      —     payload
```

Header size = **48 bytes**.

### Payload order
1. `width * height * n_layers` × u8  — tile indices (0 = empty)
2. Optional mask: if flags&1 → `width * height` × u8 (1 = solid geometry)
3. `n_obj` × ObjRec (24 bytes each)
4. `n_spawn` × SpawnRec (4 bytes: i16 x, i16 y)
5. `n_cp` × CheckpointRec (4 bytes: i16 x, i16 y)
6. `n_hero` × SpawnRec (hero starts)
7. `n_quest` × u16  — object index that counts as a quest step

### ObjRec (24 bytes)
```
u8  type        // ObjType enum (see below)
u8  kind        // platform kind / button kind / door key id
u8  flags       // bit0 solid, bit1 visible, bit2 hero_only, bit3 quest
u8  need        // power needed (lifts/bridges)
i16 x, y        // top-left / surface
u16 w, h
i16 x0, y0      // path start (lifts) or 0
i16 x1, y1      // path end
i16 target      // linked object index, or -1
u8  dir         // rope/lift direction sign as 0/1/255=-1
u8  reserved
```

### ObjType (must match engine)
```
0  O_NONE
1  O_DOOR
2  O_WALL_ICE
3  O_WALL_WOOD
4  O_WALL_STONE
5  O_WALL_FIRE
6  O_PLAT
7  O_LIFT
8  O_BRIDGE
9  O_ROPE
10 O_BUTTON
11 O_PANEL_FIX
12 O_PANEL_BRIDGE
13 O_SNOW
14 O_KEY
15 O_ARTIFACT
16 O_EXIT
17 O_CRATE
```

### Platform kind (Obj.kind for O_PLAT)
0 STATIC, 1 MOVER, 2 COLLAPSE, 3 NERD

### Tile values (layer 0)
0 empty, 1 floor/solid slab, 2 wall, 3 ice, 4 hazard (optional)

## JSON schema (authoring)
See `maps/level1.json`. Fields mirror binary; `target` may be **name** string
resolved by mapc to object index.

## Limits (retro-safe defaults)
| Platform | max tiles | max obj | max file |
|----------|-----------|---------|----------|
| SNES     | 128×64    | 64      | 64 KB    |
| PS1      | 256×64    | 128     | 256 KB   |
| PC       | 512×128   | 128     | 1 MB     |

mapc.py enforces `--profile snes|ps1|pc`.

## Build pipeline
```
maps/level1.json  --mapc.py-->  maps/level1.map
game (C)  level_load_map("maps/level1.map")
```

C loader side is a separate step (level_load_map + fill Level from LHMP).

## Addendum v1.1 (совместим с v1)
- Header reserved[0..5]: типы 12 комнат, по 4 бита (0 auto, 1 open, 2 closable, 3 dead_end, 4 special, 5 boss).
- ObjType 18..28 (планируемые): PENGI_GAP, DOMINA_GAP, WALL_INVIS, GRATE, WALL_PLASMA, CHAIN, GATE, WATER, PLASMA, ZEROG, CEILING. Загрузчик пропускает неизвестные типы; kind>3 у O_PLAT читается как STATIC.
- Checkpoints читаются загрузчиком (до 8), порядок = цепочка 1S..NS.
