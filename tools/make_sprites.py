#!/usr/bin/env python3
"""Запекает растровые спрайт-листы персонажей из вырезанных эскизов (tools/sprite_extract.py).
Использование:  python3 -I tools/make_sprites.py <base_dir> <out_assets_dir> [preview.png]
Результат: assets/spr_<имя>.bmp (24 бит, чёрный 0,0,0 = прозрачно) и assets/spr_<имя>.txt (клетки, анимации, тайминги).
Анимации строятся из поз эскиза: наклон корпуса, шаг ногами, сжатие/растяжение, вращение, части (веник, посох, нога) и эффекты."""
import math, os, sys
import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage as ndi

ANIMS = ['idle', 'walk', 'run', 'jump', 'land', 'fall', 'glide', 'slide', 'activate', 'climb', 'melee', 'ranged', 'super', 'hurt', 'death', 'falldmg']
#          кадров, тиков на кадр, цикл
TIMING = {'idle': (4, 12, 1), 'walk': (6, 6, 1), 'run': (6, 4, 1), 'jump': (3, 6, 0), 'land': (2, 5, 0), 'fall': (2, 8, 1),
          'glide': (8, 2, 1), 'slide': (2, 8, 1), 'activate': (4, 10, 1), 'climb': (4, 8, 1), 'melee': (8, 3, 0),
          'ranged': (9, 3, 0), 'super': (12, 4, 0), 'hurt': (3, 7, 0), 'death': (8, 7, 0), 'falldmg': (7, 7, 0)}
OUTLINE = (22, 16, 30)

# ------------------------------------------------------------------ загрузка и масштаб
def load_png(path, flip=False, tint=None):
    im = Image.open(path).convert('RGBA')
    if flip: im = im.transpose(Image.FLIP_LEFT_RIGHT)
    a = np.asarray(im).copy()
    if tint == 'red':   # синий костюм -> бордовый (тяжелоатлет)
        r, g, b = a[..., 0].astype(int), a[..., 1].astype(int), a[..., 2].astype(int)
        m = (b > r + 22) & (b > 40)
        a[..., 0] = np.where(m, np.clip(b * 1.15 + 18, 0, 255), a[..., 0])
        a[..., 2] = np.where(m, np.clip(r * 0.7 + 10, 0, 255), a[..., 2])
        a[..., 1] = np.where(m, np.clip(g * 0.65, 0, 255), a[..., 1])
    return a

def downscale(a, scale, colors=40):
    """площадное усреднение с предумножением альфы, жёсткий край, ограниченная палитра, 1-px контур"""
    h, w = a.shape[:2]
    nh, nw = max(1, round(h * scale)), max(1, round(w * scale))
    f = a.astype(np.float32)
    al = f[..., 3:4] / 255.0
    pm = np.concatenate([f[..., :3] * al, al * 255.0], -1)
    chans = [np.asarray(Image.fromarray(pm[..., i]).resize((nw, nh), Image.BOX)) for i in range(4)]
    pm2 = np.stack(chans, -1)
    al2 = pm2[..., 3:4] / 255.0
    rgb = np.where(al2 > 0.01, pm2[..., :3] / np.maximum(al2, 0.01), 0)
    mask = (al2[..., 0] >= 0.5)
    rgb = np.clip(rgb, 0, 255).astype(np.uint8)
    # палитра: квантуем только непрозрачные пиксели
    if mask.any() and colors:
        im = Image.fromarray(rgb).quantize(colors=colors, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE).convert('RGB')
        rgb = np.asarray(im)
    out = np.zeros((nh, nw, 4), np.uint8)
    out[..., :3] = rgb; out[..., 3] = mask * 255
    return outline(out)

def outline(a, col=OUTLINE):
    h, w = a.shape[:2]
    pad = np.zeros((h + 2, w + 2, 4), np.uint8); pad[1:-1, 1:-1] = a
    m = pad[..., 3] > 0
    d = ndi.binary_dilation(m, structure=np.array([[0, 1, 0], [1, 1, 1], [0, 1, 0]])) & ~m
    pad[d] = (*col, 255)
    return pad

def crop_alpha(a):
    ys, xs = np.nonzero(a[..., 3])
    return a[ys.min():ys.max() + 1, xs.min():xs.max() + 1]

def feet_x(a):
    """центр ступней: x-центроид нижних ~10% строк"""
    ys, xs = np.nonzero(a[..., 3])
    y1 = ys.max(); band = max(2, int((y1 - ys.min()) * 0.10))
    sel = ys >= y1 - band
    return float(xs[sel].mean())

# ------------------------------------------------------------------ слои и преобразования
class Canvas:
    def __init__(self, cw, ch):
        self.cw, self.ch = cw, ch
        self.ax, self.ay = cw // 2, ch - 3          # якорь: центр ног на земле

    def place(self, base, ax_in_base):
        c = np.zeros((self.ch, self.cw, 4), np.uint8)
        h, w = base.shape[:2]
        ox = int(round(self.ax - ax_in_base)); oy = self.ay + 1 - h
        y0, x0 = max(0, oy), max(0, ox)
        y1, x1 = min(self.ch, oy + h), min(self.cw, ox + w)
        c[y0:y1, x0:x1] = base[y0 - oy:y1 - oy, x0 - ox:x1 - ox]
        return c, oy

def xf(layer, px, py, rot=0.0, sx=1.0, sy=1.0, dx=0.0, dy=0.0, mirror=False):
    """поворот по часовой (градусы, экран y вниз) вокруг (px,py), масштаб, сдвиг; ближайший сосед"""
    H, W = layer.shape[:2]
    ys, xs = np.mgrid[0:H, 0:W]
    x = xs - dx - px; y = ys - dy - py
    th = math.radians(rot); c, s = math.cos(th), math.sin(th)
    xr = x * c + y * s; yr = -x * s + y * c
    xr /= sx; yr /= sy
    if mirror: xr = -xr
    sxp = np.rint(xr + px).astype(int); syp = np.rint(yr + py).astype(int)
    ok = (sxp >= 0) & (sxp < W) & (syp >= 0) & (syp < H)
    out = np.zeros_like(layer)
    out[ok] = layer[syp[ok], sxp[ok]]
    return out

def over(dst, src):
    m = src[..., 3] > 0
    dst = dst.copy(); dst[m] = src[m]
    return dst

def split_rows(c, row):
    up = c.copy(); lo = c.copy()
    up[row:] = 0; lo[:row - 3] = 0      # нижний слой заходит под верхний на 3 строки: при сдвиге нет щели
    return up, lo

def shear_rows(layer, hip, feet, amp, lift=0):
    """горизонтальный сдвиг строк тем сильнее, чем ниже (ступни ходят), и подъём"""
    out = layer.copy(); out[hip:] = 0
    H = layer.shape[0]
    for y in range(hip, min(H, feet + 1)):
        wgt = (y - hip) / max(1, feet - hip)
        d = int(round(amp * wgt))
        yy = y - (int(round(lift * wgt)))
        if 0 <= yy < H:
            row = np.roll(layer[y], d, axis=0)
            if d > 0: row[:d] = 0
            elif d < 0: row[d:] = 0
            m = row[:, 3] > 0
            out[yy][m] = row[m]
    return out

def smooth(t): t = min(1.0, max(0.0, t)); return t * t * (3 - 2 * t)

def tween(t, keys):
    """keys: [(t, value)...] линейно"""
    if t <= keys[0][0]: return keys[0][1]
    for (t0, v0), (t1, v1) in zip(keys, keys[1:]):
        if t <= t1:
            return v0 + (v1 - v0) * smooth((t - t0) / max(1e-6, t1 - t0))
    return keys[-1][1]

# ------------------------------------------------------------------ эффекты (рисуются на готовом кадре)
def fx_draw(c, fn):
    im = Image.fromarray(c, 'RGBA'); d = ImageDraw.Draw(im)
    fn(d)
    return np.asarray(im).copy()

def fx_arc(c, cx, cy, r, a0, a1, col=(255, 250, 220), thick=2):
    def f(d):
        for k in range(thick):
            d.arc([cx - r + k, cy - r + k, cx + r - k, cy + r - k], a0, a1, fill=(*col, 255))
    return fx_draw(c, f)

def fx_disc(c, cx, cy, r, col):
    return fx_draw(c, lambda d: d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(*col, 255)))

def fx_lines(c, segs, col, w=1):
    return fx_draw(c, lambda d: [d.line(s, fill=(*col, 255), width=w) for s in segs])

# ------------------------------------------------------------------ конфигурации персонажей
class Char:
    def __init__(self, name, cell, bases, hip, anchor_pose='idle', split=True, part=None):
        self.name, self.cell, self.bases, self.hip, self.split, self.part = name, cell, bases, hip, split, part
        self.anchor_pose = anchor_pose
        self.legrow = None

def build(base_dir):
    P = lambda n: os.path.join(base_dir, n + '.png')
    chars = {}

    def prep(poses, target_h, ref, mult=None):
        """poses: {имя: массив RGBA}; масштаб по высоте опорной позы; возвращает {имя: (спрайт, x_ступней)}"""
        sc = target_h / crop_alpha(poses[ref]).shape[0]
        out = {}
        for k, a in poses.items():
            a = crop_alpha(a)
            s = downscale(a, sc * (mult or {}).get(k, 1.0))
            out[k] = (s, feet_x(s))
        return out

    # ----- Пингви
    pe = prep({'idle': load_png(P('pengi_idle')), 'gun': load_png(P('pengi_gun')), 'back': load_png(P('pengi_back'))}, 36, 'idle')
    chars['pengi'] = Char('pengi', (84, 64), pe, 0.72, split=True)
    chars['pengi'].pelvis = 1
    chars['pengi'].legrow = 0.88           # у пингвина ноги - только ступни
    # ----- Кибердед
    pe = prep({'idle': load_png(P('bum_idle')), 'wrench': load_png(P('bum_attack'))}, 40, 'idle')
    chars['bum'] = Char('bum', (84, 68), pe, 0.62)
    # ----- Вахтёрша (в эскизе только поза с веником)
    pe = prep({'broom': load_png(P('door_attack'))}, 40, 'broom')
    chars['doorkeeper'] = Char('doorkeeper', (96, 68), pe, 0.62, anchor_pose='broom')
    chars['doorkeeper'].pelvis = 1
    chars['doorkeeper'].legrow = 0.80      # ноги спрятаны под пальто: качаем только ступни у подола
    # ----- Домина
    pe = prep({'idle': load_png(P('mis_idle')), 'saw': load_png(P('mis_saw'))}, 44, 'idle', mult={'saw': 1.22})
    chars['mistress'] = Char('mistress', (96, 72), pe, 0.55)
    # ----- Ракетчик (смотрит влево в эскизе - отражаем)
    pe = prep({'idle': load_png(P('rocket'), flip=True)}, 44, 'idle')
    chars['scout'] = Char('scout', (96, 72), pe, 0.55)
    chars['scout'].legrow = 0.74           # ниже ракеты
    chars['scout'].dup = True             # в эскизе видна одна нога: вторую достраиваем
    # ----- Качок
    pe = prep({'idle': load_png(P('body'), flip=True)}, 44, 'idle')
    chars['body'] = Char('body', (84, 72), pe, 0.56)
    # ----- Тяжелоатлет: тот же качок крупнее в 1.5 раза, бордовый костюм
    pe = prep({'idle': load_png(P('body'), flip=True, tint='red')}, 66, 'idle')
    chars['heavy'] = Char('heavy', (120, 108), pe, 0.56)
    # ----- Профессор (посох переносим в правую руку)
    pe = prep({'idle': load_png(P('prof'), flip=True)}, 44, 'idle')
    chars['boss'] = Char('boss', (96, 84), pe, 0.62)
    return chars

# ------------------------------------------------------------------ сборка кадров
def part_mask_cols(sp, x0, x1=None):
    m = np.zeros(sp.shape[:2], bool); m[:, x0:x1] = True
    return m & (sp[..., 3] > 0)

def make_frames(ch):
    cv = Canvas(*ch.cell)
    cw, chh = ch.cell
    bases = ch.bases
    ax_pose = ch.anchor_pose
    placed = {}
    for k, (sp, fx_) in bases.items():
        c, oy = cv.place(sp, fx_)
        placed[k] = (c, oy, sp.shape[0], sp.shape[1], fx_)
    ref = placed[ax_pose]
    H0 = ref[2]
    top = ref[1]
    hip = int(top + ch.hip * H0)
    feet = cv.ay
    name = ch.name
    out = {a: [] for a in ANIMS}

    def pick(pose): return placed.get(pose, placed[ax_pose])[0]

    # --- разрез ног: граница между ногами (min занятость столбцов в нижней части)
    def leg_split(c):
        """граница между ногами: середина настоящего просвета, а если ноги слиты - линия по центру ступней"""
        lo = c[hip:feet + 1, :, 3] > 0
        sub = lo[int(lo.shape[0] * 0.55):]
        occ = sub.sum(0)
        xs = np.nonzero(c[..., 3].any(0))[0]
        x0, x1 = xs.min(), xs.max()
        a, b = cv.ax - int((x1 - x0) * 0.18), cv.ax + int((x1 - x0) * 0.18)
        seg = occ[a:b + 1]
        if len(seg) and seg.min() == 0:
            z = np.nonzero(seg == 0)[0]
            return a + int(z.mean())
        return cv.ax

    cache_split = {}
    lrow = int(top + ch.legrow * H0) if getattr(ch, 'legrow', None) else hip      # строка, от которой качаются ноги

    def compose(pose='idle', uprot=0, updx=0, updy=0, upsy=1.0, sh=0.0, ph=0.0, lift=0, kick=0,
                grot=0, gsx=1.0, gsy=1.0, gdx=0, gdy=0, mirror=False, part=None, bodyfx=None, swing=0.0):
        c = pick(pose)
        if ch.split and name not in ('boss', 'doorkeeper'):         # шов между ногами: даже стоя видно две ноги
            key = pose
            if key not in cache_split: cache_split[key] = leg_split(c)
            c = c.copy(); r0 = int(hip + (0.72 if name == 'pengi' else 0.38) * (feet - hip))
            col = cache_split[key]
            for yy in range(r0, feet + 1):
                if c[yy, col, 3] > 0 and c[yy, col - 1, 3] > 0 and c[yy, col + 1, 3] > 0: c[yy, col] = (*OUTLINE, 255)
        if part is not None:
            c = part(c)
        pel = getattr(ch, 'pelvis', 4)                        # таз остаётся на месте, качаются только ноги ниже него: между ногами нет пустоты
        up, lo = split_rows(c, lrow + pel)
        # ноги: жёсткие куски, качаются от бедра (никаких сдвигов строк - тело не корёжится)
        if (sh or lift or kick) and (ch.split or getattr(ch, 'dup', False)):
            key = pose
            if key not in cache_split: cache_split[key] = leg_split(c)
            sx_ = cache_split[key]
            if getattr(ch, 'dup', False):
                back = lo.copy(); m = back[..., 3] > 0
                back[m, :3] = (back[m, :3].astype(float) * 0.66).astype(np.uint8)
                front = lo.copy()
            else:
                back = lo.copy(); back[:, sx_:] = 0
                front = lo.copy(); front[:, :sx_] = 0
                m = back[..., 3] > 0
                back[m, :3] = (back[m, :3].astype(float) * 0.82).astype(np.uint8)

            def pivot_x(layer):
                xs_ = np.nonzero(layer[lrow + pel:lrow + pel + 3, :, 3].any(0))[0]
                return float(xs_.mean()) if len(xs_) else float(cv.ax)

            def swing_leg(layer, p, extra=0.0, fixed=None):
                # шаг: нога переносится целиком (форма не меняется, нет "треугольника"), стопа вперёд - с подъёмом; опорная нога на земле
                if fixed is not None:
                    return xf(layer, pivot_x(layer), lrow + pel, rot=fixed)
                lf = lift * max(0.0, math.cos(p))
                return xf(layer, pivot_x(layer), lrow + pel, rot=-0.8 * sh * math.sin(p), dx=int(round(sh * math.sin(p))), dy=-int(round(lf)))
            if kick:                                       # удар ногой: одна нога вперёд-вверх, вторая стоит на земле
                front = swing_leg(front, 0.0, fixed=-kick)
                back = swing_leg(back, 0.0, fixed=4.0)
            else:
                front = swing_leg(front, ph)
                back = swing_leg(back, ph + math.pi)
            lo = over(back, front)
        # корпус
        up = xf(up, ref_x(c), hip, rot=uprot, sx=1.0, sy=upsy, dx=updx, dy=updy)
        fr = over(lo, up)
        fr = xf(fr, cv.ax, cv.ay, rot=grot, sx=gsx, sy=gsy, dx=gdx, dy=gdy, mirror=mirror)
        if bodyfx: fr = bodyfx(fr)
        return fr

    def ref_x(c):
        return cv.ax

    def hand_y():
        return top + 0.45 * H0

    # === части: веник (вахтёрша) и посох (профессор)
    def broom_part(angle):
        def f(c):
            xs = np.nonzero(c[..., 3].any(0))[0]; x0, x1 = xs.min(), xs.max()
            cut = int(x0 + (x1 - x0) * 0.60)
            mask = c.copy(); mask[:, :cut] = 0
            rest = c.copy(); rest[:, cut:] = 0
            col = np.nonzero(mask[:, cut, 3])[0]
            py = int(col.mean()) if len(col) else int(hand_y())
            rot = xf(mask, cut, py, rot=angle)
            return over(rest, rot)
        return f

    def staff_part(angle, gap=0):
        def f(c):
            xs = np.nonzero(c[..., 3].any(0))[0]; x0, x1 = xs.min(), xs.max()
            cut = int(x0 + (x1 - x0) * 0.82)
            mask = c.copy(); mask[:, :cut] = 0
            rest = c.copy(); rest[:, cut:] = 0
            ys_, xs_ = np.nonzero(mask[..., 3])
            if len(ys_) == 0: return c
            py = int(top + 0.50 * H0); px = int(xs_.mean())
            rot = xf(mask, px, py, rot=angle)
            return over(rest, rot)
        return f

    # === эффекты
    def slash(t, y_frac=0.55, reach=1.0, col=(255, 250, 220), low=False):
        r = int(0.5 * H0 * reach) + 4
        cx = cv.ax + 3; cy = int(cv.ay - (0.22 if low else y_frac) * H0)
        a0 = -75; a1 = -75 + int(135 * smooth((t - 0.30) / 0.22))
        def f(fr):
            if 0.30 <= t <= 0.66: fr = fx_arc(fr, cx, cy, r, a0, max(a0 + 8, a1), col, 2)
            return fr
        return f

    def ground_ring(t, t0, big=1.0):
        def f(fr):
            if t < t0 or t > t0 + 0.28: return fr
            k = (t - t0) / 0.28
            rr = int((8 + 26 * k) * big)
            return fx_draw(fr, lambda d: (d.arc([cv.ax - rr, cv.ay - 5, cv.ax + rr, cv.ay + 2], 180, 360, fill=(235, 245, 255, 255)),
                                         d.arc([cv.ax - rr, cv.ay - 6, cv.ax + rr, cv.ay + 1], 180, 360, fill=(235, 245, 255, 255))))
        return f

    def combine(*fs):
        def f(fr):
            for g in fs:
                if g: fr = g(fr)
            return fr
        return f

    # ================= общие циклы =================
    n = lambda a: TIMING[a][0]
    bob = {'pengi': 1, 'bum': 1, 'doorkeeper': 1, 'mistress': 1, 'scout': 1, 'body': 1, 'heavy': 2, 'boss': 1}[name]
    base_pose = 'broom' if name == 'doorkeeper' else 'idle'
    idle_part = (lambda t=0: broom_part(62 + 2 * math.sin(t * 6.28))) if name == 'doorkeeper' else (lambda t=0: None)
    idle_pose = base_pose

    # idle
    for i in range(n('idle')):
        t = i / n('idle'); s = math.sin(t * 6.283)
        pa = idle_part(t)
        if name == 'boss':
            out['idle'].append(compose(idle_pose, gdy=-3 + int(round(1.6 * s)), uprot=0.5 * s))
        else:
            out['idle'].append(compose(idle_pose, updy=int(round(0.6 * s)) * (1 if bob else 0), upsy=1.0 + 0.012 * s, part=pa))
    # walk / run
    for anim, (nm, amp, lean, hb, lft) in {'walk': (6, 4.0, 2.5, 1, 2), 'run': (6, 6.5, 7.0, 2, 3)}.items():
        for i in range(nm):
            ph = i / nm * 6.283
            sw = 0 if name in ('boss',) else amp * {'pengi': 0.6, 'doorkeeper': 0.9, 'mistress': 0.85}.get(name, 1.0)
            up_dy = -int(round(hb * (1.0 - abs(math.sin(ph))))) * (0 if name == 'boss' else 1)
            pa = idle_part(i / nm) if name == 'doorkeeper' else None
            if name == 'boss':
                out[anim].append(compose(idle_pose, gdy=-3 - int(round(1.5 * math.sin(ph))), uprot=lean * 0.8, grot=lean * 0.3))
            elif name == 'pengi':
                out[anim].append(compose(idle_pose, sh=sw, ph=ph, uprot=lean + 3.5 * math.sin(ph), updy=up_dy, grot=0, gdx=0))
            else:
                out[anim].append(compose(idle_pose, sh=sw, ph=ph, lift=lft, uprot=lean + 1.2 * math.sin(ph * 2), updy=up_dy, part=pa))
    # jump / land / fall
    jp = (lambda a: broom_part(a)) if name == 'doorkeeper' else (lambda a: None)
    out['jump'] = [compose(idle_pose, gsy=1.10, gsx=0.94, updy=-1, uprot=-3, part=jp(68) if name == 'doorkeeper' else None),
                   compose(idle_pose, gsy=1.04, gsx=0.98, gdy=-1, uprot=-2, part=jp(72) if name == 'doorkeeper' else None),
                   compose(idle_pose, gsy=0.98, gsx=1.0, uprot=4, kick=18 if ch.split and name not in ('boss',) else 0, part=jp(70) if name == 'doorkeeper' else None)]
    out['land'] = [compose(idle_pose, gsy=0.84, gsx=1.12, part=jp(60) if name == 'doorkeeper' else None),
                   compose(idle_pose, gsy=0.94, gsx=1.04, part=jp(62) if name == 'doorkeeper' else None)]
    out['fall'] = [compose(idle_pose, gsy=1.08, gsx=0.95, uprot=-6, updy=-1, part=jp(75) if name == 'doorkeeper' else None),
                   compose(idle_pose, gsy=1.12, gsx=0.93, uprot=-9, updy=-2, part=jp(80) if name == 'doorkeeper' else None)]
    if name == 'boss':
        out['jump'] = [compose(idle_pose, gdy=-3, grot=-3)] * 3
        out['land'] = [compose(idle_pose, gdy=-3)] * 2
        out['fall'] = [compose(idle_pose, gdy=-3, grot=-6)] * 2

    # glide (вращение волчком): растяжение по x через косинус + кольца вихря
    gl = []
    for i in range(n('glide')):
        phi = i / n('glide') * 6.283
        cx_ = math.cos(phi)
        sxs = max(0.74, abs(cx_))                      # не сжимается в нитку: поворот читается по зеркалу, наклону и вихрю
        def whirl(fr, i=i):
            ph0 = i / n('glide')
            def f(d):
                Hs = int(H0 * 0.95)
                for k in range(6):
                    yy = cv.ay - 4 - int(k * Hs / 6)
                    rw = 11 + int(k * 2.1)
                    off = int(3 * math.sin((ph0 + k * 0.17) * 6.283))
                    col = (150, 100, 70, 255) if k % 2 == 0 else (205, 205, 215, 255)
                    d.arc([cv.ax - rw + off, yy - 3, cv.ax + rw + off, yy + 3], 190 + k * 20, 350 + k * 20, fill=col)
            return fx_draw(fr, f)
        pose = 'idle' if name != 'doorkeeper' else 'broom'
        gl.append(compose(pose, gsx=sxs, mirror=cx_ < 0, gdy=-2 - int(round(1.5 * abs(math.sin(phi)))), uprot=9 * math.sin(phi * 2), grot=3 * math.sin(phi), part=broom_part(10) if name == 'doorkeeper' else None, bodyfx=whirl))
    out['glide'] = gl

    # slide (пингвин): лежит на брюхе головой вперёд
    for i in range(n('slide')):
        w_ = bases[ax_pose][0].shape[1]
        out['slide'].append(compose(idle_pose, grot=84, gsx=1.0, gsy=0.74, gdy=-int(w_ * 0.28), gdx=int(H0 * 0.18) if i == 0 else int(H0 * 0.18) + 1))
    # activate: наклон и «тык»
    for i in range(n('activate')):
        ph = i / n('activate') * 6.283
        pa = broom_part(40 + 25 * math.sin(ph)) if name == 'doorkeeper' else None
        out['activate'].append(compose(idle_pose, uprot=9 + 4 * math.sin(ph), updx=2, updy=int(round(1 + math.sin(ph))), part=pa))
    # climb
    for i in range(n('climb')):
        ph = i / n('climb') * 6.283
        out['climb'].append(compose(idle_pose, grot=7 * math.sin(ph), gdy=-int(round(1.2 * abs(math.sin(ph)))), updy=int(round(math.sin(ph)))))

    # пингвин на канате - вид со спины (эскиз pengi_back): ступни по очереди, лёгкое покачивание
    if name == 'pengi' and 'back' in placed:
        out['climb'] = []
        cb = placed['back'][0]
        yb = int(np.nonzero(cb[..., 3].any(1))[0].max())
        xs_b = np.nonzero(cb[..., 3].any(0))[0]; mid = int((xs_b.min() + xs_b.max()) // 2)
        cut = yb - 5
        for i in range(n('climb')):
            phc = i / n('climb') * 6.283
            body = cb.copy(); body[cut:] = 0
            legs = cb.copy(); legs[:cut - 3] = 0                 # заходит под тело на 3 строки: щели не будет
            Lg = legs.copy(); Lg[:, mid:] = 0
            Rg = legs.copy(); Rg[:, :mid] = 0
            d = 2.0 * math.sin(phc)
            Lg = xf(Lg, mid, cut, dy=-max(0.0, d))
            Rg = xf(Rg, mid, cut, dy=-max(0.0, -d))
            fr = over(over(Lg, Rg), body)
            fr = xf(fr, cv.ax, cv.ay, rot=2.0 * math.sin(phc), dx=int(round(math.sin(phc))), dy=-int(round(abs(math.sin(phc)))))
            out['climb'].append(fr)

    # ================= атаки =================
    def melee_params(t):
        rot = tween(t, [(0, 0), (0.28, -16), (0.42, 22), (0.62, 20), (1, 0)])
        dx = tween(t, [(0, 0), (0.28, -2), (0.42, 5), (0.62, 5), (1, 0)])
        return rot, dx
    for i in range(n('melee')):
        t = i / (n('melee') - 1)
        rot, dx = melee_params(t)
        if name == 'doorkeeper':
            ang = tween(t, [(0, 55), (0.28, -50), (0.45, 85), (0.65, 70), (1, 55)])
            out['melee'].append(compose('broom', uprot=rot * 0.6, updx=int(dx), part=broom_part(ang), bodyfx=slash(t, 0.35, 1.25)))
        elif name == 'bum':
            out['melee'].append(compose('wrench', uprot=rot, updx=int(dx), bodyfx=slash(t, 0.60, 0.9)))
        elif name == 'pengi':
            out['melee'].append(compose('idle', uprot=rot * 0.8, updx=int(dx * 1.2), gsy=1 - 0.05 * smooth(1 - abs(t - 0.45) * 4), bodyfx=slash(t, 0.45, 0.7, (200, 240, 255))))
        elif name == 'mistress':
            out['melee'].append(compose('idle', uprot=rot * 0.6, updx=int(dx), kick=(tween(t, [(0, 0), (0.28, 10), (0.42, 80), (0.62, 72), (1, 0)])), sh=0.01, ph=1.0, bodyfx=slash(t, 0.30, 0.95, (255, 230, 200), low=True)))
        elif name == 'scout':
            out['melee'].append(compose('idle', uprot=rot * 0.3, updx=int(dx * 0.4), kick=tween(t, [(0, 0), (0.28, 12), (0.42, 85), (0.62, 78), (1, 0)]), sh=0.01, ph=1.0, bodyfx=slash(t, 0.30, 0.9, (255, 220, 200), low=True)))
        elif name == 'body':
            out['melee'].append(compose('idle', uprot=rot, updx=int(dx * 1.3), bodyfx=slash(t, 0.55, 0.95, (255, 255, 235))))
        elif name == 'heavy':       # прыжок и удар о землю: ударная волна на t~0.5
            lift_ = tween(t, [(0, 0), (0.22, 0), (0.34, 16), (0.46, 16), (0.52, 0), (1, 0)])
            sq = tween(t, [(0, 1), (0.2, 0.86), (0.34, 1.12), (0.5, 1.0), (0.55, 0.82), (0.7, 0.95), (1, 1)])
            out['melee'].append(compose('idle', gdy=-int(lift_), gsy=sq, gsx=2 - sq if sq < 1 else 1 / sq, bodyfx=ground_ring(t, 0.5, 1.7)))
        else:  # boss
            out['melee'].append(compose('idle', gdy=-3, uprot=rot * 0.5, updx=int(dx * 0.5), bodyfx=slash(t, 0.55, 0.9, (220, 200, 255))))

    # --- дальняя атака
    for i in range(n('ranged')):
        t = i / (n('ranged') - 1)
        recoil = tween(t, [(0, 0), (0.3, 0), (0.38, -3), (0.7, -1), (1, 0)])
        if name == 'pengi':
            tipx, tipy = bases['gun'][0].shape[1], 0
            pose = 'idle' if t < 0.14 else 'gun'
            def mz(fr, t=t):
                if 0.30 <= t <= 0.55:
                    k = (t - 0.30) / 0.25
                    cx_ = cv.ax + int(H0 * 0.66); cy_ = int(top + 0.54 * H0)
                    fr = fx_disc(fr, cx_ + 1, cy_, int(5 - 3 * k), (160, 235, 255))
                    fr = fx_disc(fr, cx_ + 1, cy_, int(3 - 2 * k), (255, 255, 255))
                    fr = fx_lines(fr, [(cx_ + 3, cy_ - 3, cx_ + 9, cy_ - 6), (cx_ + 4, cy_ + 3, cx_ + 10, cy_ + 5)], (130, 220, 255), 1)
                return fr
            out['ranged'].append(compose(pose, updx=int(recoil), uprot=-2 if t > 0.3 else 0, bodyfx=mz))
        elif name == 'bum':    # бросок бутылки: замах назад - рывок вперёд
            rot = tween(t, [(0, 0), (0.28, -20), (0.45, 18), (0.7, 12), (1, 0)])
            out['ranged'].append(compose('wrench' if 0.3 < t < 0.5 else 'idle', uprot=rot, updx=int(tween(t, [(0, 0), (0.28, -2), (0.45, 3), (1, 0)]))))
        elif name == 'doorkeeper':
            ang = tween(t, [(0, 60), (0.3, 20), (0.45, -10), (0.7, 40), (1, 60)])
            rot = tween(t, [(0, 0), (0.28, -14), (0.45, 16), (0.7, 8), (1, 0)])
            out['ranged'].append(compose('broom', uprot=rot, part=broom_part(ang)))
        elif name == 'mistress':   # вход в волчок: руки/корпус вращаются, затем стойка
            out['ranged'].append(compose('idle', gsx=max(0.34, abs(math.cos(t * 6.283))), mirror=math.cos(t * 6.283) < 0, gdy=-int(2 * t)))
        elif name in ('scout',):   # пуск ракеты: отдача и огонь сзади
            def fl(fr, t=t):
                if 0.30 <= t <= 0.60:
                    cx_ = cv.ax - int(H0 * 0.55); cy_ = int(top + 0.50 * H0)
                    k = (t - 0.30) / 0.30
                    fr = fx_disc(fr, cx_, cy_, int(6 - 4 * k), (255, 170, 40)); fr = fx_disc(fr, cx_ - 2, cy_, int(3 - 2 * k), (255, 245, 150))
                return fr
            out['ranged'].append(compose('idle', updx=int(recoil * 1.3), uprot=-3 if t > 0.3 else 0, bodyfx=fl))
        elif name in ('body', 'heavy'):   # бросок гантели из-за головы
            rot = tween(t, [(0, 0), (0.30, -22), (0.46, 20), (0.7, 12), (1, 0)])
            out['ranged'].append(compose('idle', uprot=rot, updx=int(tween(t, [(0, 0), (0.3, -2), (0.46, 4), (1, 0)])), updy=int(tween(t, [(0, 0), (0.3, -2), (0.46, 1), (1, 0)]))))
        else:   # профессор: посох вперёд
            ang = tween(t, [(0, 0), (0.30, -10), (0.38, 80), (0.7, 78), (1, 0)])
            def bolt(fr, t=t):
                if 0.36 <= t <= 0.62:
                    cx_ = cv.ax + int(H0 * 0.50); cy_ = int(top + 0.48 * H0)
                    fr = fx_disc(fr, cx_, cy_, 4, (255, 120, 230)); fr = fx_disc(fr, cx_, cy_, 2, (255, 255, 255))
                return fr
            out['ranged'].append(compose('idle', gdy=-3, uprot=tween(t, [(0, 0), (0.3, -6), (0.4, 8), (1, 0)]), part=staff_part(ang), bodyfx=bolt))

    # --- суператака
    for i in range(n('super')):
        t = i / (n('super') - 1)
        if name == 'pengi':      # заряд плазмы -> мощный выстрел (t~0.5)
            def ch_(fr, t=t):
                cx_ = cv.ax + int(H0 * 0.66); cy_ = int(top + 0.54 * H0)
                if t < 0.5:
                    r = int(2 + 8 * t / 0.5)
                    fr = fx_disc(fr, cx_ + 2, cy_, r, (90, 190, 255)); fr = fx_disc(fr, cx_ + 2, cy_, max(1, r - 3), (230, 250, 255))
                elif t < 0.72:
                    fr = fx_disc(fr, cx_ + 3, cy_, 10, (160, 235, 255)); fr = fx_disc(fr, cx_ + 3, cy_, 6, (255, 255, 255))
                return fr
            out['super'].append(compose('gun' if t > 0.08 else 'idle', updx=-int(3 * smooth((t - 0.5) / 0.1)) if t > 0.5 else 0, uprot=-3, bodyfx=ch_))
        elif name == 'bum':      # ключ вверх -> удар вниз (t~0.5) -> ударная волна
            rot = tween(t, [(0, 0), (0.35, -24), (0.5, 30), (0.7, 26), (1, 0)])
            dx = tween(t, [(0, 0), (0.35, -3), (0.5, 6), (0.7, 6), (1, 0)])
            def sm(fr, t=t):
                if 0.48 <= t <= 0.8:
                    k = (t - 0.48) / 0.32; rr = int(6 + 22 * k)
                    fr = fx_draw(fr, lambda d: d.arc([cv.ax + 8 - rr, cv.ay - 6, cv.ax + 8 + rr, cv.ay + 3], 180, 360, fill=(255, 235, 170, 255)))
                return fr
            out['super'].append(compose('wrench', uprot=rot, updx=int(dx), updy=int(tween(t, [(0, 0), (0.35, -3), (0.5, 2), (1, 0)])), bodyfx=sm))
        elif name == 'doorkeeper':     # круговой взмах: поворот вокруг оси, веник вытянут
            phi = t * 6.283 * 1.5
            cxs = math.cos(phi)
            def sw(fr, t=t, phi=phi):
                if 0.2 < t < 0.85:
                    rr = int(H0 * 0.62)
                    fr = fx_draw(fr, lambda d: (d.arc([cv.ax - rr, cv.ay - int(H0 * 0.5) - 6, cv.ax + rr, cv.ay - int(H0 * 0.5) + 6], 0 + int(phi * 40) % 360, 160 + int(phi * 40) % 360, fill=(240, 235, 210, 255)),
                                                d.arc([cv.ax - rr, cv.ay - int(H0 * 0.5) - 5, cv.ax + rr, cv.ay - int(H0 * 0.5) + 7], 180 + int(phi * 40) % 360, 340 + int(phi * 40) % 360, fill=(200, 190, 150, 255))))
                return fr
            out['super'].append(compose('broom', gsx=max(0.34, abs(cxs)), mirror=cxs < 0, part=broom_part(8), bodyfx=sw))
        elif name == 'mistress':       # бензопила
            sh_ = 1 if i % 2 else -1
            def sp(fr, t=t, i=i):
                if 0.3 <= t <= 0.9:
                    cx_ = cv.ax + int(H0 * 0.78); cy_ = int(top + 0.70 * H0)
                    for k in range(5):
                        a = (i * 37 + k * 71) % 360; r = 3 + (k * 5) % 9
                        fr = fx_disc(fr, cx_ + int(r * math.cos(math.radians(a))), cy_ - abs(int(r * math.sin(math.radians(a)))) - 2, 1, (255, 215, 90))
                return fr
            out['super'].append(compose('saw', updx=int(tween(t, [(0, 0), (0.4, 3), (0.9, 3), (1, 0)])) + (sh_ if 0.3 < t < 0.9 else 0), uprot=tween(t, [(0, 0), (0.4, 6), (0.9, 6), (1, 0)]), bodyfx=sp))
        elif name == 'scout':
            out['super'].append(compose('idle'))
        elif name in ('body', 'heavy'):
            out['super'].append(compose('idle'))
        else:
            out['super'].append(compose('idle', gdy=-3))

    # --- урон, смерть, падение
    out['hurt'] = [compose(idle_pose, uprot=-12, updx=-2, gdx=-1, gsy=0.97, part=jp(80) if name == 'doorkeeper' else None),
                   compose(idle_pose, uprot=-9, updx=-2, gdx=-1, part=jp(78) if name == 'doorkeeper' else None),
                   compose(idle_pose, uprot=-4, updx=-1, part=jp(70) if name == 'doorkeeper' else None)]
    dpiv = (cv.ax - int(0.1 * H0), cv.ay)
    for i in range(n('death')):
        t = i / (n('death') - 1)
        if name == 'boss':
            rot = tween(t, [(0, 0), (0.5, -45), (1, -88)])
            out['death'].append(xf(compose(idle_pose, gdy=-3 + int(5 * smooth(t))), dpiv[0], dpiv[1], rot=rot, dy=int(2 * smooth(t))))
        else:
            rot = tween(t, [(0, 0), (0.25, -18), (0.8, -90), (1, -88)])
            drop = tween(t, [(0, 0), (0.5, 0), (0.8, 2), (1, 1)])
            hop = tween(t, [(0, 0), (0.2, 3), (0.5, 0), (1, 0)])
            c = compose(idle_pose, uprot=-10 * t, part=jp(85) if name == 'doorkeeper' else None)
            out['death'].append(xf(c, dpiv[0], dpiv[1], rot=rot, dx=int(-3 * smooth(t)), dy=int(drop - hop)))
    for i in range(n('falldmg')):
        t = i / (n('falldmg') - 1)
        rot = tween(t, [(0, 0), (0.25, -75), (0.7, -88), (0.85, -50), (1, -10)])
        sq = tween(t, [(0, 0.8), (0.2, 1.0)])
        c = compose(idle_pose, gsy=sq if t < 0.2 else 1.0, gsx=1.18 if t < 0.2 else 1.0, part=jp(85) if name == 'doorkeeper' else None)
        out['falldmg'].append(xf(c, dpiv[0], dpiv[1], rot=rot, dx=int(-2 * smooth(t))) if t >= 0.2 else c)

    # --- доски профессора (все кадры): под ногами
    if name == 'boss':
        for a, frames in out.items():
            for k, fr in enumerate(frames):
                frames[k] = board(fr, cv, a, k)
    return out

def board(fr, cv, anim, k):
    """летающий сноуборд под ногами + реактивный огонёк; боссу поднимаем кадр над доской"""
    hover = 0
    bob = int(round(math.sin(k * 1.7) * 1)) if anim in ('idle', 'run', 'walk') else 0
    def f(d):
        ax, ay = cv.ax, cv.ay
        y = ay - 1 + bob
        d.polygon([(ax - 24, y - 2), (ax + 20, y - 2), (ax + 27, y - 5), (ax + 23, y + 1), (ax - 22, y + 1)], fill=(120, 215, 255, 255))
        d.line([(ax - 23, y + 1), (ax + 22, y + 1)], fill=(40, 90, 150, 255), width=1)
        d.line([(ax - 20, y - 2), (ax + 18, y - 2)], fill=(235, 252, 255, 255), width=1)
        fl = 3 + (k % 2)
        d.polygon([(ax - 14, y + 2), (ax - 8, y + 2), (ax - 11, y + 2 + fl + 2)], fill=(255, 180, 60, 255))
        d.polygon([(ax + 6, y + 2), (ax + 12, y + 2), (ax + 9, y + 2 + fl + 2)], fill=(255, 220, 120, 255))
    im = Image.fromarray(fr, 'RGBA'); d = ImageDraw.Draw(im)
    # доска под персонажем: рисуем под спрайтом -> композиция
    under = Image.new('RGBA', im.size, (0, 0, 0, 0)); du = ImageDraw.Draw(under)
    f(du)
    arr = np.asarray(under).copy()
    return over(arr, fr)

# ------------------------------------------------------------------ вывод
def to_bgr_key(rgba):
    rgb = rgba[..., :3].copy()
    a = rgba[..., 3] > 0
    dark = a & (rgb.sum(-1) < 6)
    rgb[dark] = (1, 1, 1)
    rgb[~a] = (0, 0, 0)
    return rgb

OUTNAME = {'bum': 'cyberbum', 'doorkeeper': 'aunty', 'mistress': 'domina', 'scout': 'rocketpunk', 'body': 'dumbpunk', 'heavy': 'ramrod', 'boss': 'professor'}

def write_sheet(ch, frames, outdir):
    cw, chh = ch.cell
    oname = OUTNAME.get(ch.name, ch.name)
    cols = 12
    flat, table = [], []
    for a in ANIMS:
        n, dur, loop = TIMING[a]
        fl = frames[a]
        assert len(fl) == n, (ch.name, a, len(fl), n)
        table.append((a, len(flat), n, dur, loop))
        flat += fl
    rows = (len(flat) + cols - 1) // cols
    sheet = np.zeros((rows * chh, cols * cw, 3), np.uint8)
    for i, fr in enumerate(flat):
        r, c = divmod(i, cols)
        sheet[r * chh:(r + 1) * chh, c * cw:(c + 1) * cw] = to_bgr_key(fr)
    Image.fromarray(sheet).save(os.path.join(outdir, f'spr_{oname}.bmp'))
    with open(os.path.join(outdir, f'spr_{oname}.txt'), 'w') as f:
        f.write(f'cell {cw} {chh} {cols}\n')
        for i, (a, st, n, dur, loop) in enumerate(table):
            f.write(f'anim {i} {st} {n} {dur} {loop}\n')
    return flat

def preview(allframes, path):
    rows = []
    for name, ch, frames in allframes:
        cw, chh = ch.cell
        W = sum(TIMING[a][0] for a in ANIMS)
        pass
    # лист: по строке на персонажа, показываем по 1-2 кадра каждой анимации
    sel = []
    for name, ch, frames in allframes:
        cw, chh = ch.cell
        row = []
        for a in ANIMS:
            fl = frames[a]
            idxs = {1: [0]}.get(len(fl), [len(fl) // 3, (2 * len(fl)) // 3])
            for k in idxs: row.append(fl[k])
        sel.append((ch, row))
    cwm = max(c.cell[0] for c, _ in sel); chm = max(c.cell[1] for c, _ in sel)
    per = len(sel[0][1])
    cols = 16
    nrows_per = (per + cols - 1) // cols
    img = Image.new('RGB', (cols * cwm, len(sel) * nrows_per * chm), (110, 150, 190))
    for ri, (ch, row) in enumerate(sel):
        for k, fr in enumerate(row):
            r, c = divmod(k, cols)
            t = Image.fromarray(fr, 'RGBA')
            x = c * cwm + (cwm - ch.cell[0]) // 2; y = (ri * nrows_per + r) * chm + (chm - ch.cell[1])
            img.paste(t, (x, y), t)
    img.save(path)

def main():
    base_dir, outdir = sys.argv[1], sys.argv[2]
    os.makedirs(outdir, exist_ok=True)
    chars = build(base_dir)
    allf = []
    for name in ['pengi', 'bum', 'doorkeeper', 'mistress', 'scout', 'body', 'heavy', 'boss']:
        ch = chars[name]
        fr = make_frames(ch)
        write_sheet(ch, fr, outdir)
        allf.append((name, ch, fr))
        print('baked', name, sum(len(v) for v in fr.values()), 'frames')
    if len(sys.argv) > 3: preview(allf, sys.argv[3])

if __name__ == '__main__':
    main()
