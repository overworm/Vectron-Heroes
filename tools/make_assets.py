#!/usr/bin/env python3
"""Готовит ассеты игры (BMP, чёрный 0,0,0 = прозрачно) из картинок-эскизов.
Использование: python3 tools/make_assets.py <skybox.jpg> <папка_PengiCore> <папка_assets>
Создаёт: bg.bmp (320x256 без машин), car*.bmp + cars.txt (спрайты машин),
         portrait_*.bmp (32x32), ../src/font_data.h (шрифт 6x11 с кириллицей)."""
import sys, os, struct
import numpy as np
from PIL import Image, ImageFont, ImageDraw
from collections import deque
from numpy.lib.stride_tricks import sliding_window_view

def save_bmp(path, arr):
    h, w, _ = arr.shape
    stride = (w * 3 + 3) & ~3
    out = bytearray(b'BM' + struct.pack('<IHHI', 54 + stride * h, 0, 0, 54))
    out += struct.pack('<IiiHHIIiiII', 40, w, h, 1, 24, 0, stride * h, 2835, 2835, 0, 0)
    pad = b'\0' * (stride - w * 3)
    for y in range(h - 1, -1, -1):
        out += arr[y][:, ::-1].tobytes() + pad
    open(path, 'wb').write(out)

def keyed(rgb, alpha):
    a = rgb.copy()
    a[(a.sum(axis=2) == 0)] = (1, 1, 1)       # настоящий чёрный не должен стать прозрачным
    a[~alpha] = 0
    return a

def components(mask, minsize):
    h, w = mask.shape
    seen = np.zeros_like(mask, bool)
    res = []
    for y in range(h):
        for x in range(w):
            if mask[y, x] and not seen[y, x]:
                q = deque([(y, x)]); seen[y, x] = True; pts = []
                while q:
                    cy, cx = q.popleft(); pts.append((cy, cx))
                    for dy in (-1, 0, 1):
                        for dx in (-1, 0, 1):
                            ny, nx = cy + dy, cx + dx
                            if 0 <= ny < h and 0 <= nx < w and mask[ny, nx] and not seen[ny, nx]:
                                seen[ny, nx] = True; q.append((ny, nx))
                if len(pts) >= minsize: res.append(pts)
    return res

def make_bg_and_cars(sky_path, out):
    im = np.array(Image.open(sky_path).convert('RGB')).astype(np.int32)
    H, W, _ = im.shape
    Y0 = 826                                    # ниже - заснеженная стоянка с машинами
    lot = im[Y0:]
    bright = lot.min(axis=2) > 215
    snow = np.median(lot[bright], axis=0)
    carmask = np.abs(lot - snow).sum(axis=2) > 70
    clean = im.copy()
    fill = np.zeros(lot.shape, np.int32)
    for y in range(lot.shape[0]):
        ok = ~carmask[y] & bright[y]
        fill[y] = np.median(lot[y][ok], axis=0) if ok.any() else snow
    cars = []
    for pts in components(carmask, 400):
        ys = [p[0] for p in pts]; xs = [p[1] for p in pts]
        y0, y1, x0, x1 = min(ys), max(ys) + 1, min(xs), max(xs) + 1
        if (y1 - y0) < 30 or (x1 - x0) < 60: continue               # не машина
        m = np.zeros((y1 - y0, x1 - x0), bool)
        for (y, x) in pts: m[y - y0, x - x0] = True
        outside = np.zeros_like(m); q = deque()                      # закрыть окна: заливка снаружи
        for y in range(m.shape[0]):
            for x in (0, m.shape[1] - 1):
                if not m[y, x] and not outside[y, x]: outside[y, x] = True; q.append((y, x))
        for x in range(m.shape[1]):
            for y in (0, m.shape[0] - 1):
                if not m[y, x] and not outside[y, x]: outside[y, x] = True; q.append((y, x))
        while q:
            cy, cx = q.popleft()
            for dy, dx in ((1,0),(-1,0),(0,1),(0,-1)):
                ny, nx = cy + dy, cx + dx
                if 0 <= ny < m.shape[0] and 0 <= nx < m.shape[1] and not m[ny, nx] and not outside[ny, nx]:
                    outside[ny, nx] = True; q.append((ny, nx))
        solid = ~outside
        cars.append((x0, y0 + Y0, solid, lot[y0:y1, x0:x1].astype(np.uint8)))
        big = np.zeros(carmask.shape, bool); big[y0:y1, x0:x1] = solid
        grown = sliding_window_view(np.pad(big, 3), (7, 7)).any(axis=(2, 3))   # стереть с запасом под тень
        clean[Y0:][grown] = fill[grown]
    cars.sort(key=lambda c: c[0])
    top = H - int(W * 256 / 320)                                    # кроп 5:4 снизу
    bg = np.array(Image.fromarray(clean[top:].astype(np.uint8)).resize((320, 256), Image.BOX))
    bg[(bg.sum(axis=2) == 0)] = (1, 1, 1)
    save_bmp(os.path.join(out, 'bg.bmp'), bg)
    sc = 320 / W
    lines = []
    for i, (x0, y0, solid, rgb) in enumerate(cars):
        nw, nh = max(8, round(rgb.shape[1] * sc)), max(6, round(rgb.shape[0] * sc))
        img = Image.fromarray(rgb).resize((nw, nh), Image.BOX)
        a = np.array(Image.fromarray((solid * 255).astype(np.uint8)).resize((nw, nh), Image.BOX)) >= 128
        save_bmp(os.path.join(out, 'car%d.bmp' % i), keyed(np.array(img), a))
        lines.append('%d %d %d %d' % (i, round(x0 * sc), round((y0 - top) * sc), nw))
    open(os.path.join(out, 'cars.txt'), 'w').write('\n'.join(lines) + '\n')
    print('cars:', len(cars))

PORTRAITS = {   # имя: (файл, кадр головы в пикселях эскиза)
    'pengi':      ('pengi_idle.jpg',          (505, 70, 905, 430)),
    'cyberbum':   ('bum_idle.jpg',            (585, 0, 885, 320)),
    'aunty':   ('doorkeeper_attack.jpg',   (610, 0, 880, 300)),
    'domina':     ('enemy_mistress_idle.jpg', (115, 5, 305, 150)),
}
def make_portraits(pc, out):
    for name, (f, box) in PORTRAITS.items():
        im = Image.open(os.path.join(pc, f)).convert('RGB').crop(box)
        w, h = im.size; s = max(w, h)
        sq = Image.new('RGB', (s, s), (255, 255, 255)); sq.paste(im, ((s - w) // 2, (s - h) // 2))
        a = np.array(sq.resize((32, 32), Image.BOX)).astype(np.int32)
        a[a.min(axis=2) > 232] = (30, 38, 58)
        save_bmp(os.path.join(out, 'portrait_%s.bmp' % name), keyed(a.astype(np.uint8), np.ones((32, 32), bool)))

def make_font(header_path):
    font = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf', 9)
    CW, CH = 6, 11
    cps = list(range(32, 127)) + [0x401] + list(range(0x410, 0x450)) + [0x451]
    rows = []
    for cp in cps:
        img = Image.new('1', (CW, CH), 0)
        d = ImageDraw.Draw(img); d.fontmode = '1'
        d.text((0, 0), chr(cp), font=font, fill=1)
        px = img.load()
        rows.append([sum((1 << (CW - 1 - x)) for x in range(CW) if px[x, y]) for y in range(CH)])
    with open(header_path, 'w') as f:
        f.write('/* generated by tools/make_assets.py: DejaVu Sans Mono 9px, %dx%d, ASCII + Cyrillic */\n' % (CW, CH))
        f.write('#define FONT_CW %d\n#define FONT_CH %d\n#define FONT_COUNT %d\n' % (CW, CH, len(cps)))
        f.write('static const unsigned short FONT_CP[FONT_COUNT] = {%s};\n' % ','.join(str(c) for c in cps))
        f.write('static const unsigned char FONT_ROWS[FONT_COUNT][FONT_CH] = {\n')
        for cp, r in zip(cps, rows):
            f.write('  {%s},\n' % ','.join('0x%02X' % v for v in r))
        f.write('};\n')

if __name__ == '__main__':
    sky, pc, out = sys.argv[1:4]
    os.makedirs(out, exist_ok=True)
    make_bg_and_cars(sky, out)
    make_portraits(pc, out)
    make_font(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src', 'font_data.h'))
