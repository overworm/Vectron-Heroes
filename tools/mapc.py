#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""LHMP map compiler — JSON → binary .map (Python 3.10+, Windows OK)

Usage:
  python mapc.py ../maps/level1.json
  python mapc.py ../maps/level1.json -o ../maps/level1.map
  python mapc.py ../maps/level1.json --profile snes --dump
"""
from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

MAGIC = b"LHMP"
VERSION = 1
HEADER_SIZE = 48
OBJ_SIZE = 24

OBJ_TYPE = {
    "O_NONE": 0, "O_DOOR": 1, "O_WALL_ICE": 2, "O_WALL_WOOD": 3,
    "O_WALL_STONE": 4, "O_WALL_FIRE": 5, "O_PLAT": 6, "O_LIFT": 7,
    "O_BRIDGE": 8, "O_ROPE": 9, "O_BUTTON": 10, "O_PANEL_FIX": 11,
    "O_PANEL_BRIDGE": 12, "O_SNOW": 13, "O_KEY": 14, "O_ARTIFACT": 15,
    "O_EXIT": 16, "O_CRATE": 17,
    # --- планируемые типы (компилируются, движок пока их пропускает; см. CATALOG, engine=False) ---
    "O_WALL_PENGI_GAP": 18, "O_WALL_DOMINA_GAP": 19, "O_WALL_INVIS": 20, "O_GRATE": 21,
    "O_WALL_PLASMA": 22, "O_CHAIN": 23, "O_GATE": 24, "O_WATER": 25, "O_PLASMA": 26,
    "O_ZEROG": 27, "O_CEILING": 28,
}

# Комнаты (12 шт.: колонка*4 + (3 - этаж), этаж 0 = нижний). Код типа → имя
ROOM_TYPES = {"auto": 0, "open": 1, "closable": 2, "dead_end": 3, "special": 4, "boss": 5}

# Каталог объектов для редактора карт: (ключ, подпись, группа, O_тип, kind, w, h, engine, цвет, подсказка)
# engine=True  - движок это уже умеет; False - сохраняется в карту, но игра пока пропускает/рисует статикой
CATALOG = [
    # ---- платформы ----
    ("PLAT_MINI",    "Bearing mini-platform",        "Platforms", "O_PLAT", 0, 32, 8,   True,  "#c8a060", "несущая мини-платформа"),
    ("PLAT_FALL",    "Falling mini-platform",        "Platforms", "O_PLAT", 2, 32, 8,   True,  "#d08040", "падающая мини-платформа"),
    ("PLAT_MOVE_H",  "Mini-platform moving H",       "Platforms", "O_PLAT", 1, 32, 8,   True,  "#60c0d0", "двигается по горизонтали: x..x1"),
    ("PLAT_BREAK",   "Destructible platform",        "Platforms", "O_PLAT", 3, 48, 8,   True,  "#d06060", "разрушаемая платформа (Nerd)"),
    ("PLAT_VANISH",  "Vanishing mini-platform",      "Platforms", "O_PLAT", 4, 32, 8,   False, "#a0a0e0", "исчезающая мини-платформа"),
    ("PLAT_MOVE_V",  "Mini-platform moving V",       "Platforms", "O_PLAT", 5, 32, 8,   False, "#60d0a0", "двигается по вертикали: y..y1"),
    ("PLAT_WATER",   "Surface (water) platform",     "Platforms", "O_PLAT", 6, 48, 8,   False, "#5080e0", "надводная платформа"),
    ("PLAT_RAFT",    "Raft",                         "Platforms", "O_PLAT", 7, 48, 8,   False, "#9a7040", "плот"),
    ("LIFT",         "Lift (moving)",                "Lifts / bridges", "O_LIFT", 0, 48, 8, True, "#ff9030", "двигающийся лифт y0..y1; need = питание"),
    ("LIFT_INACTIVE","Lift (inactive until powered)","Lifts / bridges", "O_LIFT", 0, 48, 8, True, "#c07020", "неактивный лифт: need>=1, включается панелью/кнопкой"),
    ("LIFT_FALL",    "Falling lift",                 "Lifts / bridges", "O_LIFT", 2, 48, 8, True, "#e05020", "падающий лифт (kind 2)"),
    ("BRIDGE_INVIS", "Invisible bridge",             "Lifts / bridges", "O_BRIDGE", 0, 160, 8, True, "#3050d0", "мост-невидимка (Domina)"),
    ("ROPE",         "Rope",                         "Lifts / bridges", "O_ROPE", 0, 4, 128, True, "#b08850", "верёвка; dir = ±1"),
    ("WATER_BOOTY",  "Boss water threshold (Booty)", "Hazards", "O_WATER", 0, 160, 16, False, "#3070ff", "водный порог босса Booty (урон)"),
    ("PLASMA_BOSS",  "Boss plasma threshold",        "Hazards", "O_PLASMA", 0, 160, 16, False, "#40ffa0", "плазма последнего босса"),
    ("ZEROG",        "Zero-gravity block",           "Hazards", "O_ZEROG", 0, 48, 48,  False, "#c080ff", "блок невесомости"),
    ("CEILING",      "Ceiling",                      "Hazards", "O_CEILING", 0, 160, 8, False, "#808890", "потолок (выше нельзя)"),
    # ---- стены / преграды ----
    ("DOOR",         "Door (key)",                   "Walls", "O_DOOR", 0, 16, 48,       True,  "#c09040", "дверь (ключ kind)"),
    ("WALL_ICE",     "Pengi ice wall",               "Walls", "O_WALL_ICE", 0, 16, 112,  True,  "#a0e0ff", "ледяная стена Pengi"),
    ("WALL_WOOD",    "Cyberbum wooden wall",         "Walls", "O_WALL_WOOD", 0, 16, 112, True,  "#a07040", "деревянная стена Cyberbum"),
    ("WALL_FIRE",    "Cyberbum fire wall",           "Walls", "O_WALL_FIRE", 0, 16, 112, True,  "#ff5020", "огненная стена Cyberbum"),
    ("WALL_STONE",   "Domina concrete wall",         "Walls", "O_WALL_STONE", 0, 16, 112, True, "#9098a0", "бетонная стена Domina"),
    ("PENGI_GAP",    "Pengi opening (slide)",        "Walls", "O_WALL_PENGI_GAP", 0, 16, 112, False, "#60ffff", "Pengi-проём: нижний проём для слайда"),
    ("DOMINA_GAP",   "Domina opening (fly)",         "Walls", "O_WALL_DOMINA_GAP", 0, 16, 112, False, "#ff60ff", "Domina-проём: верхний проём для юлы"),
    ("WALL_INVIS",   "Invisible wall",               "Walls", "O_WALL_INVIS", 0, 16, 112, False, "#606060", "невидимая стена"),
    ("GRATE",        "Submarine metal grate",        "Walls", "O_GRATE", 0, 16, 64,      False, "#70a0c0", "металлическая решётка Submarine"),
    ("WALL_PLASMA",  "Refreezer green plasma wall",  "Walls", "O_WALL_PLASMA", 0, 16, 112, False, "#40ff60", "плазменная зелёная стена Refreezer"),
    ("BIN",          "Trash bin (static/rising)",    "Walls", "O_CRATE", 0, 48, 112,     True,  "#50a050", "мусорный бак непадающий / платформа"),
    ("BIN_FALL",     "Trash bin (falling)",          "Walls", "O_CRATE", 1, 48, 48,      True,  "#308030", "мусорный бак падающий"),
    ("CHAIN",        "Raisable chain",               "Walls", "O_CHAIN", 0, 8, 96,       False, "#d0d0d0", "поднимаемая цепь"),
    ("GATE",         "Submarine cast-iron gate",     "Walls", "O_GATE", 0, 16, 112,      False, "#303040", "чугунные чёрные ворота Submarine"),
    # ---- интерактив ----
    ("BUTTON",       "Button",                       "Interactive", "O_BUTTON", 0, 14, 16, True, "#ff4040", "кнопка; target = что включает"),
    ("BUTTON_CEIL",  "Button (ceiling, Refreezer)",  "Interactive", "O_BUTTON", 3, 14, 16, True, "#ff8080", "кнопка на потолке (kind 3)"),
    ("PANEL",        "Panel (lift/crate)",           "Interactive", "O_PANEL_FIX", 0, 16, 32, True, "#40a0ff", "панель Cyberbum/Aunty"),
    ("PANEL_AG",     "Panel (antigravity)",          "Interactive", "O_PANEL_FIX", 3, 16, 32, True, "#8060ff", "панель антигравитации (kind 3)"),
    ("PANEL_BRIDGE", "Panel (invisible bridge)",     "Interactive", "O_PANEL_BRIDGE", 0, 16, 32, True, "#a040ff", "панель Domina: включает мост"),
    ("SNOW",         "Snow heap (hides key)",        "Interactive", "O_SNOW", 0, 36, 14, True, "#e0f0ff", "сугроб: под ним ключ"),
    ("KEY",          "Key",                          "Interactive", "O_KEY", 0, 14, 16, True, "#ffe040", "ключ"),
    ("ARTIFACT",     "Artifact",                     "Interactive", "O_ARTIFACT", 0, 14, 16, True, "#ffffff", "артефакт"),
    ("EXIT",         "Exit",                         "Interactive", "O_EXIT", 0, 24, 48, True, "#40e060", "выход"),
]
CATALOG_BY_KEY = {c[0]: c for c in CATALOG}

PROFILES = {
    "snes": {"max_w": 128, "max_h": 64, "max_obj": 64, "max_bytes": 65536},
    "ps1":  {"max_w": 256, "max_h": 64, "max_obj": 128, "max_bytes": 262144},
    "pc":   {"max_w": 512, "max_h": 128, "max_obj": 128, "max_bytes": 1048576},
}


def die(msg: str) -> None:
    print(f"mapc: error: {msg}", file=sys.stderr)
    sys.exit(1)


def pack_obj(o: dict, name_to_idx: dict[str, int]) -> bytes:
    if "cat" in o and "type" not in o:                      # короткая запись через каталог
        c = CATALOG_BY_KEY.get(o["cat"])
        if not c:
            die(f"unknown cat '{o['cat']}'")
        o = dict(o, type=c[3], kind=o.get("kind", c[4]))
    t = o.get("type", "O_NONE")
    if isinstance(t, str):
        if t not in OBJ_TYPE:
            die(f"unknown type {t}")
        type_id = OBJ_TYPE[t]
    else:
        type_id = int(t)

    kind = int(o.get("kind", 0))
    flags = 0
    if o.get("solid", type_id in (1, 2, 3, 4, 5, 6, 7, 17)):
        flags |= 1
    if o.get("visible", True):
        flags |= 2
    if o.get("hero_only", False):
        flags |= 4
    if o.get("quest", False):
        flags |= 8

    need = int(o.get("need", 0))
    x = int(o.get("x", 0))
    y = int(o.get("y", 0))
    w = int(o.get("w", 16))
    h = int(o.get("h", 16))
    x0 = int(o.get("x0", x))
    y0 = int(o.get("y0", y))
    x1 = int(o.get("x1", x))
    y1 = int(o.get("y1", y))

    target = o.get("target", -1)
    if isinstance(target, str):
        if target not in name_to_idx:
            die(f"object '{o.get('name','?')}' target '{target}' not found")
        target = name_to_idx[target]
    target = int(target)

    dir_ = int(o.get("dir", 0))
    if dir_ < 0:
        dir_ = 255

    return struct.pack(
        "<BBBBhhHHhhhhhBB",
        type_id, kind, flags, need,
        x, y, w, h,
        x0, y0, x1, y1,
        target, dir_, 0,
    )


def compile_map(data: dict, profile: str) -> bytes:
    lim = PROFILES[profile]
    meta = data.get("meta", {})
    size = data.get("size", {})
    width = int(size.get("width", 0))
    height = int(size.get("height", 0))
    if width <= 0 or height <= 0:
        die("size.width/height required")
    if width > lim["max_w"] or height > lim["max_h"]:
        die(f"size {width}x{height} exceeds profile {profile}")

    tile_w = int(meta.get("tile_w", 16))
    tile_h = int(meta.get("tile_h", 16))
    tiles = data.get("tiles")
    if not tiles or len(tiles) != height:
        die("tiles must be height rows")
    for row in tiles:
        if len(row) != width:
            die("each tiles row must match width")

    # layers: single solid layer for v1
    n_layers = 1
    layer_bytes = bytes(int(v) & 0xFF for row in tiles for v in row)

    # mask from tiles: non-zero solid
    mask = bytes(1 if int(v) else 0 for row in tiles for v in row)
    flags = 1  # has mask

    objects = data.get("objects", [])
    if len(objects) > lim["max_obj"]:
        die(f"too many objects ({len(objects)} > {lim['max_obj']})")

    name_to_idx: dict[str, int] = {}
    for i, o in enumerate(objects):
        n = o.get("name")
        if n:
            if n in name_to_idx:
                die(f"duplicate object name '{n}'")
            name_to_idx[n] = i

    obj_blob = b"".join(pack_obj(o, name_to_idx) for o in objects)

    quest_indices = []
    for i, o in enumerate(objects):
        if o.get("quest"):
            quest_indices.append(i)

    spawns = data.get("spawns", [])
    spawn_blob = b"".join(
        struct.pack("<hh", int(s["x"]), int(s["y"])) for s in spawns
    )

    cps = data.get("checkpoints", [])
    if len(cps) > 8:
        die("max 8 checkpoints")
    cp_blob = b"".join(
        struct.pack("<hh", int(c["x"]), int(c["y"])) for c in cps
    )

    heroes = data.get("heroes", [])
    if not heroes:
        heroes = [{"x": 48, "y": 560}]
    hero_blob = b"".join(
        struct.pack("<hh", int(h["x"]), int(h["y"])) for h in heroes
    )

    quest_blob = b"".join(struct.pack("<H", q) for q in quest_indices)

    arena = data.get("arena", {})
    header = bytearray(HEADER_SIZE)
    header[0:4] = MAGIC
    struct.pack_into("<H", header, 4, VERSION)
    struct.pack_into("<H", header, 6, flags)
    struct.pack_into("<H", header, 8, width)
    struct.pack_into("<H", header, 10, height)
    header[12] = tile_w & 0xFF
    header[13] = tile_h & 0xFF
    header[14] = n_layers & 0xFF
    header[15] = len(cps) & 0xFF
    struct.pack_into("<H", header, 16, len(objects))
    struct.pack_into("<H", header, 18, len(spawns))
    header[20] = len(quest_indices) & 0xFF
    header[21] = len(heroes) & 0xFF
    struct.pack_into("<H", header, 22, int(meta.get("map_id", 1)))
    struct.pack_into("<H", header, 24, int(meta.get("bg_id", 1)))
    struct.pack_into("<H", header, 26, int(meta.get("boss_id", 0)))
    struct.pack_into("<h", header, 28, int(arena.get("trig_x0", 0)))
    struct.pack_into("<h", header, 30, int(arena.get("trig_x1", 0)))
    struct.pack_into("<h", header, 32, int(arena.get("floor", 0)))
    struct.pack_into("<h", header, 34, int(arena.get("ceil", 0)))
    struct.pack_into("<h", header, 36, int(arena.get("x0", 0)))
    struct.pack_into("<h", header, 38, int(arena.get("x1", 0)))
    rooms = data.get("rooms", [])                           # 12 типов комнат (имя или число), по nibble на комнату в reserved
    for r, rt in enumerate(rooms[:12]):
        code = ROOM_TYPES.get(rt, 0) if isinstance(rt, str) else int(rt)
        header[40 + (r >> 1)] |= (code & 15) << ((r & 1) * 4)
    header = bytes(header)

    blob = header + layer_bytes + mask + obj_blob + spawn_blob + cp_blob + hero_blob + quest_blob
    if len(blob) > lim["max_bytes"]:
        die(f"output {len(blob)} bytes exceeds profile {profile} limit {lim['max_bytes']}")
    return blob


def dump_info(blob: bytes) -> None:
    magic = blob[0:4]
    ver, flags, w, h = struct.unpack_from("<HHHH", blob, 4)
    tw, th, nl, ncp = blob[12], blob[13], blob[14], blob[15]
    nobj, nsp = struct.unpack_from("<HH", blob, 16)
    nq, nh = blob[20], blob[21]
    print(f"magic={magic!r} ver={ver} flags={flags} size={w}x{h} tile={tw}x{th}")
    print(f"layers={nl} objs={nobj} spawns={nsp} cp={ncp} heroes={nh} quests={nq}")
    print(f"file_size={len(blob)} bytes")


def list_types() -> None:
    for c in CATALOG:
        print(f"{c[0]:15} {c[3]:18} kind={c[4]} {'engine' if c[7] else 'PLANNED'}  {c[1]}")


def main() -> None:
    ap = argparse.ArgumentParser(description="Compile Lost Hero JSON map to LHMP binary")
    ap.add_argument("json_path", type=Path, nargs="?")
    ap.add_argument("--list-types", action="store_true", help="show object catalog and exit")
    ap.add_argument("-o", "--output", type=Path, default=None)
    ap.add_argument("--profile", choices=sorted(PROFILES), default="pc")
    ap.add_argument("--dump", action="store_true", help="print header summary")
    args = ap.parse_args()
    if args.list_types:
        list_types()
        return
    if not args.json_path or not args.json_path.is_file():
        die(f"file not found: {args.json_path}")

    data = json.loads(args.json_path.read_text(encoding="utf-8"))
    profile = data.get("meta", {}).get("profile", args.profile) or args.profile
    if args.profile != "pc":
        profile = args.profile

    blob = compile_map(data, profile)
    out = args.output
    if out is None:
        out = args.json_path.with_suffix(".map")
    out.write_bytes(blob)
    print(f"wrote {out} ({len(blob)} bytes, profile={profile})")
    if args.dump:
        dump_info(blob)


if __name__ == "__main__":
    main()
