#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Lost Hero map editor (web) — только стандартная библиотека Python 3.10+.

Запуск (из папки игры):
    python tools/mapedit.py                 # maps/ рядом с игрой, http://127.0.0.1:8765
    python tools/mapedit.py --maps maps --port 8765 --no-browser

Редактор правит maps/levelN.json (тот же формат, что понимает tools/mapc.py),
кнопка Compile вызывает mapc и кладёт levelN.map в maps/ (и в assets/maps/, если папка есть).
Сервер слушает только 127.0.0.1.
"""
from __future__ import annotations

import argparse
import json
import re
import shutil
import sys
import threading
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import mapc  # noqa: E402

NAME_RE = re.compile(r"^[A-Za-z0-9_\-]{1,40}$")


def new_map(map_id: int) -> dict:
    w, h = 100, 36
    tiles = [[0] * w for _ in range(h)]
    for x in range(w):                      # нижний пол (FLOOR1 = 560 -> строка 35)
        tiles[35][x] = 1
    return {
        "meta": {"name": f"Level {map_id}", "map_id": map_id, "bg_id": map_id, "boss_id": map_id,
                 "tile_w": 16, "tile_h": 16, "profile": "pc"},
        "size": {"width": w, "height": h},
        "arena": {"trig_x0": 0, "trig_x1": 0, "floor": 0, "ceil": 0, "x0": 0, "x1": 0},
        "rooms": ["auto"] * 12,
        "tiles": tiles, "objects": [], "spawns": [], "checkpoints": [{"x": 48, "y": 560}],
        "heroes": [{"x": 40 + 14 * i, "y": 560} for i in range(7)],
    }


class Handler(BaseHTTPRequestHandler):
    maps_dir: Path = Path("maps")

    def log_message(self, fmt, *args):  # тише
        pass

    def _send(self, code: int, body: bytes, ctype: str = "application/json; charset=utf-8"):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _json(self, obj, code: int = 200):
        self._send(code, json.dumps(obj, ensure_ascii=False).encode("utf-8"))

    def _name(self, q) -> str | None:
        n = (q.get("name") or [""])[0]
        return n if NAME_RE.match(n) else None

    def do_GET(self):
        u = urlparse(self.path)
        q = parse_qs(u.query)
        if u.path in ("/", "/index.html"):
            self._send(200, (HERE / "mapedit.html").read_bytes(), "text/html; charset=utf-8")
        elif u.path == "/api/catalog":
            cat = [dict(key=c[0], label=c[1], group=c[2], type=c[3], kind=c[4], w=c[5], h=c[6],
                        engine=c[7], color=c[8], hint=c[9]) for c in mapc.CATALOG]
            self._json({"catalog": cat, "room_types": list(mapc.ROOM_TYPES.keys())})
        elif u.path == "/api/maps":
            names = sorted(p.stem for p in self.maps_dir.glob("*.json"))
            self._json({"maps": names, "dir": str(self.maps_dir)})
        elif u.path == "/api/map":
            n = self._name(q)
            p = self.maps_dir / f"{n}.json" if n else None
            if not p or not p.is_file():
                return self._json({"error": "not found"}, 404)
            self._send(200, p.read_bytes())
        else:
            self._send(404, b"not found", "text/plain")

    def do_POST(self):
        u = urlparse(self.path)
        q = parse_qs(u.query)
        n = self._name(q)
        if not n:
            return self._json({"error": "bad name"}, 400)
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length) if length else b""
        p = self.maps_dir / f"{n}.json"
        if u.path == "/api/map":
            try:
                data = json.loads(body.decode("utf-8"))
            except Exception as e:  # noqa: BLE001
                return self._json({"error": f"bad json: {e}"}, 400)
            if p.is_file():
                shutil.copyfile(p, p.with_suffix(".json.bak"))
            p.write_text(json.dumps(data, ensure_ascii=False, separators=(",", ":")), encoding="utf-8")
            self._json({"ok": True, "bytes": p.stat().st_size})
        elif u.path == "/api/new":
            if p.exists():
                return self._json({"error": "already exists"}, 409)
            m = re.search(r"(\d+)$", n)
            p.write_text(json.dumps(new_map(int(m.group(1)) if m else 1), separators=(",", ":")), encoding="utf-8")
            self._json({"ok": True})
        elif u.path == "/api/compile":
            if not p.is_file():
                return self._json({"error": "save first"}, 404)
            try:
                data = json.loads(p.read_text(encoding="utf-8"))
                profile = data.get("meta", {}).get("profile", "pc") or "pc"
                blob = mapc.compile_map(data, profile)
            except SystemExit:
                return self._json({"error": "mapc rejected the map (see server console)"}, 400)
            except Exception as e:  # noqa: BLE001
                return self._json({"error": str(e)}, 400)
            out = p.with_suffix(".map")
            out.write_bytes(blob)
            copies = [str(out)]
            for extra in (self.maps_dir.parent / "assets" / "maps",):
                if extra.is_dir():
                    shutil.copyfile(out, extra / out.name)
                    copies.append(str(extra / out.name))
            self._json({"ok": True, "bytes": len(blob), "files": copies})
        else:
            self._send(404, b"not found", "text/plain")


def main() -> None:
    ap = argparse.ArgumentParser(description="Lost Hero web map editor")
    ap.add_argument("--maps", type=Path, default=HERE.parent / "maps")
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--no-browser", action="store_true")
    a = ap.parse_args()
    a.maps.mkdir(parents=True, exist_ok=True)
    Handler.maps_dir = a.maps.resolve()
    srv = ThreadingHTTPServer(("127.0.0.1", a.port), Handler)
    url = f"http://127.0.0.1:{a.port}/"
    print(f"Lost Hero map editor: {url}  (maps: {Handler.maps_dir})  Ctrl+C = stop")
    if not a.no_browser:
        threading.Timer(0.6, lambda: webbrowser.open(url)).start()
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
