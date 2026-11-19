#!/usr/bin/env python3
"""Спрайты новых врагов и боссов (Deliver, Firewalker, Monoman, Ironfem, Maestro, BigPussy, Coach, Hacker,
Watcher, Boot, SuperDrom, Drunky) из эскизов tools/concept/*.jpg.  SuperDrom и Drunky нарисованы кодом.
Запуск:  python3 -I tools/make_sprites_ext.py [out_assets_dir]   (по умолчанию assets)
Формат листов тот же, что у make_sprites.py. Все спрайты смотрят ВПРАВО (движок отражает сам)."""
import math, os, sys
import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage as ndi
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import make_sprites as ms
import sprite_extract as se

CONCEPT = os.path.join(HERE, 'concept')
ANIMS = ms.ANIMS
OL = ms.OUTLINE
# n, ticks, loop - по умолчанию как у героев; для боссов и врагов можно переопределять
DEF = dict(ms.TIMING)

# ------------------------------------------------------------------ вырезание эскиза
def cut(name, lum=186, sat=26, clean=True, shadow=True):
    a = se.load(os.path.join(CONCEPT, name + '.jpg'))
    m = se.largest(se.remove_bg(a, lum, sat))
    if clean: m = se.clean(m)
    if shadow: m = se.unshadow(a, m)
    m = ndi.binary_fill_holes(m)
    return se.crop(se.rgba(a, m))

def prep(name, target_h, flip=False, anchor='bbox', **kw):
    a = cut(name, **kw)
    if flip: a = a[:, ::-1].copy()
    sc = target_h / a.shape[0]
    s = ms.downscale(a, sc)
    s = ms.crop_alpha(s)
    ax = (np.nonzero(s[..., 3].any(0))[0].mean()) if anchor == 'bbox' else ms.feet_x(s)
    return s, float(ax)

def keep_rows(s, frac_top):
    """верхняя часть спрайта (для 'застрял в земле')"""
    h = int(s[0].shape[0] * frac_top)
    return s[0][:h].copy(), s[1]

def over_below(base, extra, dy):
    """дорисовать extra под base (расширяет холст вниз)"""
    pass

def add_board(sp, w=None, glow=(90, 220, 255)):
    """парящая доска под ногами: возвращает новый спрайт с доской снизу"""
    s, ax = sp
    h, W = s.shape[:2]
    bw = w or int(W * 0.9)
    out = np.zeros((h + 5, max(W, bw + 2), 4), np.uint8)
    ox = (out.shape[1] - W) // 2
    out[:h, ox:ox + W] = s
    cx = out.shape[1] // 2
    im = Image.fromarray(out, 'RGBA'); d = ImageDraw.Draw(im)
    d.rounded_rectangle([cx - bw // 2, h - 2, cx + bw // 2, h + 1], 1, fill=(52, 56, 70, 255), outline=(*OL, 255))
    d.line([cx - bw // 2 + 2, h + 1, cx + bw // 2 - 2, h + 1], fill=(*glow, 255))
    d.line([cx - bw // 4, h + 2, cx + bw // 4, h + 2], fill=(*glow, 255))
    d.line([cx - bw // 6, h + 3, cx + bw // 6, h + 3], fill=(int(glow[0] * .6), int(glow[1] * .6), int(glow[2] * .6), 255))
    out = np.asarray(im).copy()
    return out, float(ax + ox)

# ------------------------------------------------------------------ кадр
class Cv:
    def __init__(self, cw, ch):
        self.cw, self.ch = cw, ch; self.ax, self.ay = cw // 2, ch - 3

    def layer(self, sp, ox=0, oy=0):
        s, ax = sp
        c = np.zeros((self.ch, self.cw, 4), np.uint8)
        h, w = s.shape[:2]
        x0 = int(round(self.ax - ax)) + ox; y0 = self.ay + 1 - h + oy
        ya, xa = max(0, y0), max(0, x0); yb, xb = min(self.ch, y0 + h), min(self.cw, x0 + w)
        if ya < yb and xa < xb: c[ya:yb, xa:xb] = s[ya - y0:yb - y0, xa - x0:xb - x0]
        return c

def whiten(c, k):
    o = c.copy(); m = o[..., 3] > 0
    o[..., :3][m] = (o[..., :3][m].astype(int) + (255 - o[..., :3][m].astype(int)) * k).astype(np.uint8)
    return o

def tint(c, col, k):
    o = c.copy(); m = o[..., 3] > 0
    for i in range(3): o[..., i][m] = (o[..., i][m].astype(int) * (1 - k) + col[i] * k).astype(np.uint8)
    return o

def dissolve(c, keep, seed=0):
    """порциями убирает пиксели (материализация/растворение): keep 0..1"""
    H, W = c.shape[:2]
    ys, xs = np.mgrid[0:H, 0:W]
    bay = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]) / 16.0
    th = bay[(ys + seed) % 4, (xs + seed * 2) % 4]
    o = c.copy(); o[th >= keep] = 0
    return o

def tf(cv, sp, rot=0.0, sx=1.0, sy=1.0, dx=0, dy=0, pv=(0.0, 0.0), ox=0, oy=0, mirror=False):
    """поворот/масштаб вокруг опорной точки pv=(сдвиг x px, доля высоты от низа)"""
    L = cv.layer(sp, ox, oy)
    h = sp[0].shape[0]
    px = cv.ax + pv[0]; py = cv.ay - h * pv[1]
    return ms.xf(L, px, py, rot, sx, sy, dx, dy, mirror=mirror)

def sparks(c, pts, col, r=1):
    for (x, y) in pts:
        c = ms.fx_disc(c, int(x), int(y), r, col)
    return c

def speed_lines(c, cv, h, n=3, back=1, col=(230, 236, 250), seed=0):
    rnd = np.random.RandomState(seed)
    segs = []
    for k in range(n):
        y = cv.ay - int(h * (0.15 + 0.7 * rnd.rand())); x1 = cv.ax - back * int(10 + 6 * rnd.rand()); x0 = x1 - back * int(8 + 8 * rnd.rand())
        segs.append((x0, y, x1, y))
    return ms.fx_lines(c, segs, col)

def dust(c, cv, t, wide=14, col=(210, 214, 226)):
    r = 1 + int(3 * t)
    for s in (-1, 1):
        c = ms.fx_disc(c, cv.ax + s * int(wide * (0.3 + t)), cv.ay - r, r, col)
    return c

def stars(c, cx, cy, ph, n=3, r=7):
    pts = [(cx + r * math.cos(ph + k * 2 * math.pi / n), cy + r * 0.45 * math.sin(ph + k * 2 * math.pi / n)) for k in range(n)]
    return sparks(c, pts, (255, 230, 90), 1)

def blast(c, cx, cy, t, col=(255, 200, 80), r0=3, r1=14):
    r = int(r0 + (r1 - r0) * t)
    c = ms.fx_disc(c, cx, cy, r, col)
    return ms.fx_disc(c, cx, cy, max(1, r // 2), (255, 250, 220))

# ------------------------------------------------------------------ стандартный набор анимаций
def anim_frames(cv, sp, spec):
    """spec: dict с параметрами и необязательными переопределениями spec['<anim>'] = f(i, n) -> кадр"""
    H = sp[0].shape[0]
    P = dict(ib=1.0, wb=1.5, wl=2.0, rl=6.0, rb=2.0, hov=0); P.update(spec.get('p', {}))
    muz = spec.get('muzzle', (0.9, 0.55))     # дуло: доля ширины, доля высоты от низа
    W = sp[0].shape[1]
    mx = int(cv.ax + (muz[0] - 0.5) * W); my = int(cv.ay - H * muz[1])
    out = {}
    def tfn(**k): return tf(cv, sp, **k)
    ov = spec.get('over', {})
    def mk(name, n, fn):
        if name in ov: out[name] = [ov[name](i, n) for i in range(n)]
        else: out[name] = [fn(i, n) for i in range(n)]
    hv = P['hov']
    ph = lambda i, n: 2 * math.pi * i / n
    mk('idle', DEF['idle'][0], lambda i, n: tfn(dy=-int(round(hv + P['ib'] * math.sin(ph(i, n)))), sy=1 + 0.012 * math.sin(ph(i, n))))
    mk('walk', DEF['walk'][0], lambda i, n: tfn(dy=-int(round(hv + P['wb'] * abs(math.sin(ph(i, n) / 2)))), rot=P['wl'] * math.sin(ph(i, n)), dx=int(round(math.sin(ph(i, n)))) ))
    mk('run', DEF['run'][0], lambda i, n: speed_lines(tfn(dy=-int(round(hv + P['rb'] * abs(math.sin(ph(i, n) / 2)))), rot=P['rl'], dx=int(round(1.5 * math.sin(ph(i, n))))), cv, H, 3, 1, seed=i))
    mk('jump', 3, lambda i, n: tfn(sy=[0.9, 1.08, 1.05][i], sx=[1.08, 0.95, 0.97][i], dy=-int(hv + [0, 2, 4][i])))
    mk('land', 2, lambda i, n: dust(tfn(sy=[0.84, 0.95][i], sx=[1.12, 1.04][i]), cv, i / 2))
    mk('fall', 2, lambda i, n: tfn(sy=1.06, sx=0.96, rot=[-3, 3][i], dy=-int(hv)))
    mk('glide', 8, lambda i, n: tfn(rot=360 * i / n if False else 0, dy=-int(hv + math.sin(ph(i, n)))))
    mk('slide', 2, lambda i, n: tfn(sy=0.9, sx=1.06, rot=[4, 6][i]))
    mk('activate', 4, lambda i, n: tfn(dy=-int(hv + 1.5 * math.sin(ph(i, n))), rot=2 * math.sin(ph(i, n))))
    mk('climb', 4, lambda i, n: tfn(dy=-int(hv + math.sin(ph(i, n)))))
    # ближний бой: рывок вперёд
    def melee(i, n):
        t = i / (n - 1); k = math.sin(math.pi * min(1.0, t * 1.1))
        f = tfn(dx=int(round(6 * k)), rot=-5 * (1 - min(1, t * 3)) + 9 * k, dy=-int(hv))
        if 0.3 < t < 0.75: f = ms.fx_arc(f, mx - 4, my, 14, -70, 70, (255, 250, 220), 2)
        return f
    mk('melee', DEF['melee'][0], melee)
    def ranged(i, n):
        t = i / (n - 1); rec = max(0, 1 - abs(t - 0.38) * 4)
        f = tfn(dx=-int(round(3 * rec)), rot=-6 * rec + 3 * (1 - t) * (t < 0.3), dy=-int(hv))
        if 0.32 < t < 0.5: f = blast(f, mx + 2, my, (t - 0.32) / 0.18, (255, 210, 90), 2, 6)
        return f
    mk('ranged', DEF['ranged'][0], ranged)
    def sup(i, n):
        t = i / (n - 1)
        lean = 12 * smooth_ramp(t)
        f = tfn(rot=lean, dx=int(8 * t), dy=-int(hv))
        return speed_lines(f, cv, H, 4, 1, (255, 255, 255), seed=i)
    mk('super', DEF['super'][0], sup)
    def hurt(i, n): return whiten(tfn(dx=[-3, -4, -2][i], rot=[-8, -10, -4][i], dy=-int(hv)), [0.8, 0.5, 0.1][i])
    mk('hurt', 3, hurt)
    def death(i, n):
        t = i / (n - 1)
        f = tfn(rot=70 * smooth_ramp(t), dx=int(-8 * t), dy=int(6 * t), sy=1 - 0.2 * t)
        f = whiten(f, 0.6 * (1 - t) * (i % 2))
        if i >= 2: f = blast(f, cv.ax + int(-6 * t), cv.ay - int(H * 0.5), (t), (255, 170, 60), 2, 12)
        return dissolve(f, 1.0 - 0.65 * max(0, t - 0.55) / 0.45, i) if i >= 5 else f
    mk('death', 8, death)
    mk('falldmg', 7, lambda i, n: stars(tfn(sy=0.88 + 0.03 * (i % 2), sx=1.08, rot=[-5, 5][i % 2]), cv.ax, cv.ay - H - 3, i, 3))
    return out

def smooth_ramp(t): t = min(1, max(0, t)); return t * t * (3 - 2 * t)

def write(name, cv, frames, outdir, timing=None, box=None):
    T = dict(DEF); T.update(timing or {})
    cw, chh = cv.cw, cv.ch
    cols = 12; flat = []; table = []
    for a in ANIMS:
        fl = frames[a]; n = len(fl)
        dur, loop = T[a][1], T[a][2]
        table.append((a, len(flat), n, dur, loop)); flat += fl
    rows = (len(flat) + cols - 1) // cols
    sheet = np.zeros((rows * chh, cols * cw, 3), np.uint8)
    for i, fr in enumerate(flat):
        r, c = divmod(i, cols)
        assert fr.shape == (chh, cw, 4), (name, i, fr.shape)
        sheet[r * chh:(r + 1) * chh, c * cw:(c + 1) * cw] = ms.to_bgr_key(fr)
    Image.fromarray(sheet).save(os.path.join(outdir, f'spr_{name}.bmp'))
    with open(os.path.join(outdir, f'spr_{name}.txt'), 'w') as f:
        f.write(f'cell {cw} {chh} {cols}\n')
        for i, (a, st, n, dur, loop) in enumerate(table): f.write(f'anim {i} {st} {n} {dur} {loop}\n')
    if box:
        with open(os.path.join(outdir, f'{name}_hit.txt'), 'w') as f: f.write(f'box {box[0]} {box[1]}\n')
    return flat

def body_box(sp):
    s = sp[0]; return s.shape[1], s.shape[0]

def auto_muzzle(sp):
    s = sp[0]; ys, xs = np.nonzero(s[..., 3]); xm = xs.max()
    sel = xs >= xm - 2
    return (xm + 1) / s.shape[1], 1 - float(ys[sel].mean()) / s.shape[0]

def mirror_turn(cv, sp, t, hv=0):
    """разворот вокруг вертикальной оси: t 0..1 -> sx 1..-1"""
    a = math.pi * t; sx = math.cos(a)
    if abs(sx) < 0.12: sx = 0.12 if sx >= 0 else -0.12
    return tf(cv, sp, sx=sx, dy=-int(hv), pv=(0, 0.0))

def smoke(c, x, y, t, col=(150, 190, 120)):
    for k in range(3):
        r = 2 + int(3 * t) + k % 2
        c = ms.fx_disc(c, int(x + (k + 1) * 4 * t * 2), int(y - k * 2 * t - 2 * math.sin(k + t * 3)), r, col)
    return c

# ---------------------------------------------------------------- ironfem
def b_ironfem():
    sp = prep('ironfem', 46, anchor='feet')
    cv = Cv(88, 64)
    spec = dict(p=dict(hov=0, wb=1.0, wl=1.5, rl=7.0, rb=1.0), muzzle=(0.74, 0.62))
    spec['over'] = {}
    def ranged(i, n):    # бросок цепи: откидывается назад и резко вперёд
        t = i / (n - 1); k = math.sin(math.pi * t)
        f = tf(cv, sp, rot=-6 * (1 - t) * (t < 0.4) + 8 * max(0, 1 - abs(t - 0.45) * 5), dx=int(4 * max(0, 1 - abs(t - 0.45) * 4)))
        return f
    spec['over']['ranged'] = ranged
    fr = anim_frames(cv, sp, spec)
    return 'ironfem', cv, fr, {}, body_box(sp)

# ---------------------------------------------------------------- monoman
def b_monoman():
    sp = prep('monoman', 46, anchor='feet', lum=190, sat=30)
    cv = Cv(96, 66)
    mz = auto_muzzle(sp)
    spec = dict(p=dict(hov=3, ib=1.5, wb=1.5, wl=3.0, rl=8.0, rb=1.5), muzzle=mz)
    def sup(i, n):                            # развернул мотоцикл выхлопом вперёд, пыхнул газом
        t = i / (n - 1); H = sp[0].shape[0]
        if t < 0.25: return mirror_turn(cv, sp, t / 0.25, 3)
        if t > 0.8: return mirror_turn(cv, sp, 1 - (t - 0.8) / 0.2, 3)
        f = tf(cv, sp, sx=-1, dy=-3, rot=-4 * math.sin(t * 30))
        W = sp[0].shape[1]
        return smoke(f, cv.ax + W // 2 - 6, cv.ay - 8 - 3, (t - 0.25) / 0.55, (140, 200, 90))
    spec['over'] = {'super': sup}
    fr = anim_frames(cv, sp, spec)
    return 'monoman', cv, fr, {}, body_box(sp)

# ---------------------------------------------------------------- deliver
def b_deliver():
    sp = prep('deliver', 38, flip=True)
    cv = Cv(88, 56)
    spec = dict(p=dict(wb=1.0, wl=1.5, rl=4.0, rb=1.0), muzzle=(0.95, 0.52))
    def ranged(i, n):
        t = i / (n - 1)
        wind = smooth_ramp(t / 0.35) if t < 0.35 else 1 - smooth_ramp((t - 0.35) / 0.25)
        return tf(cv, sp, rot=-9 * wind * (t < 0.4) + 6 * max(0, 1 - abs(t - 0.45) * 6), dx=-int(3 * wind) + int(4 * max(0, 1 - abs(t - 0.45) * 5)))
    spec['over'] = {'ranged': ranged}
    fr = anim_frames(cv, sp, spec)
    return 'deliver', cv, fr, {}, body_box(sp)

# ---------------------------------------------------------------- firewalker
def b_firewalker():
    sp = prep('firewalker', 42)
    cv = Cv(100, 62)
    mz = auto_muzzle(sp)
    spec = dict(p=dict(wb=1.0, wl=1.0, rl=3.0, rb=1.0), muzzle=mz)
    def ranged(i, n):     # длинная струя: сопло вперёд, корпус дрожит, вспышка на сопле
        t = i / (n - 1); H, W = sp[0].shape[:2]
        f = tf(cv, sp, dx=-2 + (i % 2), rot=-2 + (i % 2))
        mx_ = int(cv.ax + (mz[0] - 0.5) * W) + 2; my_ = int(cv.ay - H * mz[1])
        if 0.15 < t < 0.9:
            f = ms.fx_disc(f, mx_ + 1, my_, 3, (255, 150, 30)); f = ms.fx_disc(f, mx_ + 1, my_, 1, (255, 240, 160))
        return f
    spec['over'] = {'ranged': ranged}
    fr = anim_frames(cv, sp, spec)
    return 'firewalker', cv, fr, {}, body_box(sp)

# ---------------------------------------------------------------- maestro
def b_maestro():
    tb = add_board(prep('maestro_trombone', 48, anchor='feet', lum=200, sat=30, clean=False), w=26)
    kit = prep('maestro_drums', 56, anchor='bbox', lum=200, sat=30, clean=False)
    cv = Cv(116, 76)
    spec = dict(p=dict(hov=3, ib=1.5, wb=1.5, wl=2.0, rl=7.0, rb=1.5), muzzle=(0.98, 0.7))
    def play(i, n):                 # кулиса ходит туда-сюда, ноты летят
        t = i / (n - 1); s, ax = tb; W = s.shape[1]
        a = s.copy(); cut_x = int(W * 0.58)
        sl = np.zeros_like(a); sh = [2, -1, 1, -2, 2, -1, 1, -2][i % 8]
        sl[:, cut_x:] = a[:, cut_x:]; a[:, cut_x:] = 0
        sl = np.roll(sl, sh, axis=1)
        a = ms.over(a, sl)
        f = tf(cv, (a, ax), dx=int(round(2 + 2 * math.sin(t * 9))), rot=3 + 2 * math.sin(t * 6), dy=-3 - int(math.sin(t * 9)))
        x0 = cv.ax + W // 2; y0 = cv.ay - 22
        for k in range(2):
            u = (t * 1.6 + k * 0.5) % 1.0
            f = ms.fx_disc(f, int(x0 + 8 + 16 * u), int(y0 - 6 - 12 * u + 3 * math.sin(u * 8)), 2, (255, 235, 120))
            f = ms.fx_lines(f, [(int(x0 + 10 + 16 * u), int(y0 - 6 - 12 * u + 3 * math.sin(u * 8)), int(x0 + 10 + 16 * u), int(y0 - 14 - 12 * u + 3 * math.sin(u * 8)))], (255, 235, 120))
        return f
    def drums(i, n):
        t = i / (n - 1)
        f = tf(cv, kit, dy=int(round(math.sin(t * 40))), rot=0.8 * math.sin(t * 36))   # цикл: барабанщик играет всё время фазы
        if i % 2 == 0:      # вспышки по тарелкам
            f = ms.fx_arc(f, cv.ax - 24, cv.ay - 42, 6, 0, 360, (255, 245, 180), 1)
        return f
    spec['over'] = {'melee': play, 'ranged': play, 'super': drums}
    fr = anim_frames(cv, tb, spec)
    return 'maestro', cv, fr, {'super': (12, 3, 1)}, (20, 46)

# ---------------------------------------------------------------- bigpussy
def b_bigpussy():
    front = prep('bigpussy_front', 56, anchor='bbox', clean=False)
    side = prep('bigpussy_side', 56, anchor='bbox', clean=False)
    jump = prep('bigpussy_jump', 50, anchor='bbox', clean=False)
    sideb = add_board(side, w=34, glow=(255, 120, 200))
    cv = Cv(96, 72)
    P = dict(wb=1.0, wl=3.0, rl=4.0, rb=1.0)
    ff = anim_frames(cv, front, dict(p=dict(ib=1.2)))
    fs = anim_frames(cv, side, dict(p=P, muzzle=(0.45, 0.28)))
    fb = anim_frames(cv, sideb, dict(p=dict(hov=3)))
    out = dict(ff)
    for a in ('walk', 'run', 'melee', 'slide'): out[a] = fs[a]
    def pulse(i, n):             # лежит на доске, магнитофон пульсирует волнами
        t = i / (n - 1); s, ax = sideb
        f = tf(cv, sideb, dy=-3 - int(math.sin(t * 12)), rot=1.5 * math.sin(t * 8))
        bx = cv.ax - 6; by = cv.ay - 14
        for k in range(2):
            u = (t * 2.2 + k * 0.5) % 1.0
            f = ms.fx_arc(f, bx, by, int(8 + 18 * u), -50, 50, (255, 130, 210), 2)
        return f
    out['ranged'] = [pulse(i, 9) for i in range(9)]
    out['melee'] = [pulse(i, 8) for i in range(8)]
    out['jump'] = [tf(cv, jump, sy=[0.85, 1.05, 1.0][i], sx=[1.1, 0.96, 1.0][i], dy=-[0, 4, 8][i]) for i in range(3)]
    out['fall'] = [tf(cv, jump, dy=-6, rot=[-2, 2][i]) for i in range(2)]
    def lea(i, n):               # присела и прыгнула, шлёп вниз
        t = i / (n - 1)
        if t < 0.35: return tf(cv, front, sy=1 - 0.22 * smooth_ramp(t / 0.35), sx=1 + 0.12 * smooth_ramp(t / 0.35))
        if t < 0.8: return tf(cv, jump, dy=-int(14 * math.sin(math.pi * (t - 0.35) / 0.45)))
        return dust(tf(cv, front, sy=0.78, sx=1.15), cv, (t - 0.8) / 0.2, 22)
    out['super'] = [lea(i, 12) for i in range(12)]
    # застряла в земле: видны голова и плечи
    s, ax = front; h = s.shape[0]
    top = (s[:int(h * 0.55)].copy(), ax)
    def stuck(i, n):
        f = tf(cv, top, dy=0, oy=-0)
        # почва: торчит из земли - нижний край обрезан неровно, трещины и камешки
        sh = top[0].shape[0]; y0 = cv.ay - sh + 1
        f[cv.ay - 1:] = 0
        f = ms.fx_lines(f, [(cv.ax - 34, cv.ay, cv.ax - 20, cv.ay - 3), (cv.ax + 20, cv.ay - 3, cv.ax + 34, cv.ay), (cv.ax - 18, cv.ay - 1, cv.ax - 8, cv.ay - 1), (cv.ax + 8, cv.ay - 1, cv.ax + 18, cv.ay - 1)], (92, 70, 54), 2)
        f = ms.fx_disc(f, cv.ax - 24, cv.ay - 4, 2, (110, 86, 66)); f = ms.fx_disc(f, cv.ax + 26, cv.ay - 5, 2, (110, 86, 66))
        return stars(f, cv.ax, cv.ay - sh - 4, i * 0.9, 3, 10)
    out['falldmg'] = [stuck(i, 4) for i in range(4)]
    return 'bigpussy', cv, out, {'falldmg': (4, 12, 1)}, (24, 52)

# ---------------------------------------------------------------- coach
def b_coach():
    walk = prep('coach_walk', 42, flip=True, anchor='feet', clean=False)
    stand = prep('coach_stand', 42, flip=True, anchor='feet', clean=False)
    cv = Cv(80, 64)
    base = anim_frames(cv, stand, dict(p=dict(ib=1.0, wb=1.0, wl=1.0, rl=5.0), muzzle=(0.55, 0.62)))
    out = dict(base)
    def wk(i, n, amp=1):
        sp_ = walk if i % 2 == 0 else stand
        return tf(cv, sp_, dy=-int(1.5 * abs(math.sin(math.pi * i / n * 2))) * amp)
    out['walk'] = [wk(i, 6) for i in range(6)]
    out['run'] = [speed_lines(tf(cv, walk if i % 2 == 0 else stand, rot=6, dy=-2 * (i % 2), dx=1), cv, 42, 3, 1, seed=i) for i in range(6)]
    def cast(i, n):                       # рука вверх, молнии вокруг портфеля
        t = i / (n - 1); f = tf(cv, walk, dx=-2 * (t < 0.5), rot=-4 * math.sin(t * 6))
        rnd = np.random.RandomState(i)
        pts = []
        for k in range(4):
            x = cv.ax + int(rnd.randint(-16, 18)); pts += [(x, cv.ay - 46 - rnd.randint(0, 6)), (x + rnd.randint(-5, 6), cv.ay - 54 - rnd.randint(0, 10))]
        f = ms.fx_lines(f, [(*pts[2 * k], *pts[2 * k + 1]) for k in range(4)], (190, 230, 255), 1)
        if 0.2 < t < 0.85: f = ms.fx_arc(f, cv.ax, cv.ay - 22, 22 + (i % 2) * 2, 0, 360, (150, 190, 255), 1)
        return f
    out['ranged'] = [cast(i, 9) for i in range(9)]
    out['melee'] = [tf(cv, walk, dx=int(5 * math.sin(math.pi * i / 7)), rot=8 * math.sin(math.pi * i / 7)) for i in range(8)]
    def flipspell(i, n):                  # парит, аура
        t = i / (n - 1); f = tf(cv, stand, dy=-int(4 + 2 * math.sin(t * 10)), sx=1.0)
        for k in range(2):
            u = (t * 1.5 + k * 0.5) % 1.0
            f = ms.fx_arc(f, cv.ax, cv.ay - 22, int(10 + 24 * u), 0, 360, (255, 150, 255), 1)
        return f
    out['super'] = [flipspell(i, 12) for i in range(12)]
    out['falldmg'] = [stars(tf(cv, stand, sy=0.86, rot=9 + (i % 2), dy=1, dx=3), cv.ax + 3, cv.ay - 42, i * 0.8, 3, 9) for i in range(4)]
    return 'coach', cv, out, {'falldmg': (4, 12, 1)}, (14, 40)

# ---------------------------------------------------------------- hacker
def b_hacker():
    sp = prep('hacker', 58, anchor='bbox', lum=200, sat=40, clean=False)
    cv = Cv(88, 74)
    spec = dict(p=dict(wb=0.6, wl=4.0, rl=8.0, rb=1.0, ib=0.8), muzzle=(0.62, 0.55))
    def ranged(i, n):                      # печатает, нарастает свечение, отдача; он же ожидание
        t = i / (n - 1); H, W = sp[0].shape[:2]
        f = tf(cv, sp, rot=-1.5 * math.sin(t * 20), dx=-int(2 * max(0, 1 - abs(t - 0.6) * 5)))
        gx = cv.ax + int(W * 0.18); gy = cv.ay - int(H * 0.52)
        f = ms.fx_disc(f, gx, gy, 2 + int(3 * t), (80, 255, 120)) if i % 2 == 0 else f
        if 0.55 < t < 0.85: f = ms.fx_disc(f, cv.ax + W // 2 - 4, gy, 4, (150, 255, 170)); f = ms.fx_disc(f, cv.ax + W // 2 - 4, gy, 2, (240, 255, 240))
        return f
    def sup(i, n):
        t = i / (n - 1); f = tf(cv, sp, dy=-int(2 * math.sin(t * 14)))
        rnd = np.random.RandomState(i)
        H, W = sp[0].shape[:2]
        segs = []
        for k in range(5):
            a = rnd.rand() * 6.28; r0 = W * 0.5; r1 = r0 + 4 + rnd.rand() * 6
            segs.append((cv.ax + r0 * math.cos(a), cv.ay - H / 2 + r0 * math.sin(a), cv.ax + r1 * math.cos(a + .1), cv.ay - H / 2 + r1 * math.sin(a + .1)))
        return ms.fx_lines(f, [tuple(int(v) for v in s) for s in segs], (200, 255, 255), 1)
    spec['over'] = {'ranged': ranged, 'super': sup, 'melee': ranged}
    fr = anim_frames(cv, sp, spec)
    fr['falldmg'] = [ranged(i, 7) for i in range(7)]
    return 'hacker', cv, fr, {'ranged': (9, 4, 1), 'falldmg': (4, 12, 1)}, body_box(sp)

# ---------------------------------------------------------------- watcher
def b_watcher():
    sp = prep('watcher', 30, flip=True, anchor='bbox', lum=190, sat=30)
    cv = Cv(64, 44)
    spec = dict(p=dict(hov=0, ib=2.0, wb=1.0, wl=5.0, rl=10.0, rb=1.0), muzzle=(0.95, 0.5))
    def wing(sp_, i, k=2):          # крылья трепещут: верхние строки сдвигаются
        s, ax = sp_; a = s.copy(); H = a.shape[0]
        a[:int(H * 0.45)] = np.roll(a[:int(H * 0.45)], [0, 1, 0, -1][i % 4], axis=0) if False else a[:int(H * 0.45)]
        return a, ax
    def hover(i, n): return tf(cv, sp, dy=-int(round(2 * math.sin(2 * math.pi * i / n))), rot=2 * math.sin(2 * math.pi * i / n), sy=1 + 0.03 * (i % 2))
    def drop(i, n):               # наклон вниз, люк, растущий снежный ком
        t = i / (n - 1); f = tf(cv, sp, rot=18 * math.sin(math.pi * min(1, t * 1.3)), dy=-2)
        r = int(2 + 7 * smooth_ramp(t))
        f = ms.fx_disc(f, cv.ax - 2, cv.ay - 2 + 0, r, (236, 244, 252)); f = ms.fx_disc(f, cv.ax - 4, cv.ay - 4, max(1, r // 3), (255, 255, 255))
        return f
    def rocket(i, n):
        t = i / (n - 1); f = tf(cv, sp, rot=-8 * math.sin(math.pi * t), dx=-int(2 * math.sin(math.pi * t)))
        if 0.35 < t < 0.7: f = blast(f, cv.ax + 12, cv.ay - 12, (t - 0.35) / 0.35, (255, 200, 80), 2, 6)
        return f
    spec['over'] = {'idle': hover, 'ranged': drop, 'melee': drop, 'super': rocket}
    fr = anim_frames(cv, sp, spec)
    fr['falldmg'] = [tf(cv, sp, dy=-int(1 + (i % 2)), rot=3 * (i % 2)) for i in range(4)]
    return 'watcher', cv, fr, {'falldmg': (4, 10, 1)}, body_box(sp)

# ---------------------------------------------------------------- boot
def b_boot():
    sp = prep('boot_camera', 70, anchor='bbox', lum=200, sat=34, clean=False)
    cv = Cv(112, 92)
    spec = dict(p=dict(hov=0, ib=1.5, wb=1.5, wl=3.0, rl=6.0, rb=2.0), muzzle=(0.9, 0.35))
    def stomp(i, n):
        t = i / (n - 1)
        if t < 0.4: return tf(cv, sp, dy=-int(26 * smooth_ramp(t / 0.4)), rot=-8 * smooth_ramp(t / 0.4))
        if t < 0.55: u = (t - 0.4) / 0.15; return tf(cv, sp, dy=-int(26 * (1 - u)), rot=-8 * (1 - u) + 4 * u)
        u = (t - 0.55) / 0.45
        return dust(tf(cv, sp, sy=0.88 + 0.12 * smooth_ramp(u), sx=1.1 - 0.1 * smooth_ramp(u)), cv, min(1, u * 1.2), 32)
    def stuck(i, n):
        f = tf(cv, sp, sy=0.97, dy=2 + (i % 2))
        H, W = sp[0].shape[:2]
        rnd = np.random.RandomState(1)
        segs = []
        for k in range(7):
            x = cv.ax - W // 2 + 6 + int(rnd.rand() * (W - 12)); L = 3 + int(rnd.rand() * 6) + (2 if (i + k) % 3 == 0 else 0)
            segs.append((x, cv.ay - 2, x, cv.ay + 1 + L if cv.ay + 1 + L < cv.ch else cv.ch - 1))
        f = ms.fx_lines(f, segs, (92, 112, 40), 2)
        f = ms.fx_disc(f, cv.ax + int(W * 0.3), cv.ay - int(H * 0.2), 2, (255, 60, 60) if i % 2 else (255, 200, 80))
        return f
    def hov(i, n): return tf(cv, sp, dy=-int(14 + 2 * math.sin(2 * math.pi * i / n)), rot=-4 + 2 * math.sin(2 * math.pi * i / n))
    spec['over'] = {'super': stomp, 'idle': hov, 'walk': lambda i, n: tf(cv, sp, dy=-int(14 + 2 * math.sin(2 * math.pi * i / n)), rot=-10 + 3 * math.sin(2 * math.pi * i / n)),
                    'run': lambda i, n: tf(cv, sp, dy=-14, rot=-14),
                    'melee': lambda i, n: tf(cv, sp, dy=-14, rot=-12 + 20 * math.sin(math.pi * i / (n - 1))),
                    'ranged': lambda i, n: tf(cv, sp, dy=-14, rot=-12 + 20 * math.sin(math.pi * i / (n - 1)))}
    fr = anim_frames(cv, sp, spec)
    fr['falldmg'] = [stuck(i, 4) for i in range(4)]
    return 'boot', cv, fr, {'falldmg': (4, 10, 1), 'super': (12, 4, 0)}, body_box(sp)

# ================================================================== нарисованные кодом: SuperDrom и Drunky
S4 = 4
def new4(w, h):
    im = Image.new('RGBA', (w, h), (0, 0, 0, 0)); return im, ImageDraw.Draw(im)

def fin4(im, colors=36):
    a = np.asarray(im).copy()
    s = ms.downscale(a, 1.0 / S4, colors)
    return ms.crop_alpha(s)

def thick(d, p0, p1, w, col):
    d.line([p0, p1], fill=col, width=w)
    r = w // 2
    for p in (p0, p1): d.ellipse([p[0] - r, p[1] - r, p[0] + r, p[1] + r], fill=col)

def heli_img(phase, tilt=0.0):
    im, d = new4(240, 170)
    RED = (206, 44, 52, 255); DRED = (150, 28, 38, 255); GREY = (120, 126, 140, 255); LG = (190, 196, 208, 255); K = (22, 16, 30, 255)
    # хвост
    d.polygon([(92, 90), (92, 108), (20, 100), (20, 94)], fill=GREY, outline=K)
    d.polygon([(22, 96), (8, 66), (32, 66), (38, 98)], fill=RED, outline=K)
    rl = int(26 * abs(math.cos(phase * 1.7)) + 6)
    d.ellipse([10 - 4, 86 - rl // 2, 10 + 4, 86 + rl // 2], fill=(60, 62, 72, 255), outline=K)
    # лыжи и ракетные подвески
    d.line([(96, 150), (196, 150)], fill=K, width=8); d.line([(96, 150), (196, 150)], fill=LG, width=4)
    for x in (112, 176): d.line([(x, 124), (x - 4, 150)], fill=K, width=6); d.line([(x, 124), (x - 4, 150)], fill=GREY, width=3)
    d.rounded_rectangle([118, 126, 170, 142], 5, fill=(70, 74, 88, 255), outline=K, width=3)
    for x in (126, 142, 158): d.ellipse([x, 129, x + 10, 139], fill=(230, 90, 40, 255), outline=K)
    # корпус и кабина
    d.ellipse([84, 62, 204, 134], fill=RED, outline=K, width=4)
    d.pieslice([84, 62, 204, 134], 20, 160, fill=DRED)
    d.ellipse([84, 62, 204, 134], outline=K, width=4)
    d.rectangle([112, 104, 188, 110], fill=(245, 245, 250, 255))
    d.ellipse([140, 70, 200, 116], fill=(120, 214, 232, 255), outline=K, width=4)
    d.ellipse([150, 76, 170, 88], fill=(200, 244, 250, 255))
    for ex in (158, 178):                                    # злые глазки-камеры
        d.ellipse([ex - 5, 90, ex + 5, 102], fill=(250, 250, 250, 255), outline=K, width=2)
        d.ellipse([ex - 1, 94, ex + 4, 100], fill=(210, 20, 30, 255))
    d.line([(150, 86), (164, 92)], fill=K, width=4); d.line([(188, 86), (172, 92)], fill=K, width=4)
    # мачта и винт
    d.rectangle([132, 52, 142, 68], fill=GREY, outline=K, width=2)
    L = int(100 * abs(math.cos(phase))) + 14
    d.ellipse([137 - L, 46, 137 + L, 54], fill=(70, 74, 86, 255), outline=K, width=2)
    d.ellipse([131, 46, 143, 56], fill=LG, outline=K, width=2)
    return im

def saw_img(rot):
    im, d = new4(240, 170)
    cx, cy, R = 130, 100, 68
    K = (22, 16, 30, 255); ST = (176, 182, 196, 255); DS = (112, 118, 134, 255)
    n = 20
    pts = []
    for k in range(n):
        a0 = rot + 2 * math.pi * k / n
        for a, r in ((a0, R), (a0 + math.pi / n * 0.9, R + 16), (a0 + math.pi / n * 1.9, R)):
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    d.polygon(pts, fill=ST, outline=K)
    d.ellipse([cx - R + 8, cy - R + 8, cx + R - 8, cy + R - 8], fill=DS, outline=K, width=3)
    for k in range(5):
        a = rot * 1.0 + k * 2 * math.pi / 5
        d.line([(cx, cy), (cx + (R - 12) * math.cos(a), cy + (R - 12) * math.sin(a))], fill=K, width=5)
    d.ellipse([cx - 24, cy - 24, cx + 24, cy + 24], fill=(206, 44, 52, 255), outline=K, width=4)
    d.ellipse([cx - 9, cy - 9, cx + 9, cy + 9], fill=(250, 220, 90, 255), outline=K, width=3)
    d.ellipse([cx - 14, cy - 14, cx - 6, cy - 8], fill=(255, 150, 150, 255))
    return im

def heli_sprite(phase, tilt=0):
    s = fin4(heli_img(phase)); return s, float(np.nonzero(s[..., 3].any(0))[0].mean())

def saw_sprite(rot):
    s = fin4(saw_img(rot)); return s, float(np.nonzero(s[..., 3].any(0))[0].mean())

def b_superdrom():
    cv = Cv(96, 66)
    sets = []
    for k in range(4):
        sp = heli_sprite(k * 0.9)
        spec = dict(p=dict(hov=4, ib=1.5, wb=1.5, wl=4.0, rl=9.0, rb=1.5), muzzle=(0.55, 0.28))
        def ranged(i, n, sp=sp):                    # залп ракет из подвесок
            t = i / (n - 1); f = tf(cv, sp, dx=-int(3 * max(0, 1 - abs(t - 0.4) * 5)), rot=-5 * max(0, 1 - abs(t - 0.4) * 4), dy=-4)
            if 0.3 < t < 0.7:
                for j in range(3): f = blast(f, cv.ax + 10 + 4 * j, cv.ay - 12 + 3 * j, (t - 0.3) / 0.4, (255, 190, 70), 1, 4)
            return f
        spec['over'] = {'ranged': ranged, 'melee': ranged}
        sets.append(anim_frames(cv, sp, spec))
    out = {a: [sets[i % 4][a][i] for i in range(len(sets[0][a]))] for a in ANIMS}
    heli0 = heli_sprite(0.3)
    sawsp = [saw_sprite(k * 0.26) for k in range(12)]
    def mixf(t, i, rev=False):
        t = 1 - t if rev else t
        h = tf(cv, heli0, sx=1 - 0.75 * t, sy=1 - 0.75 * t, rot=120 * t, dy=-int(4 - 4 * t), pv=(0, 0.45))
        s = tf(cv, sawsp[i % 12], sx=0.35 + 0.65 * t, sy=0.35 + 0.65 * t, dy=-int(4 - 4 * t), pv=(0, 0.5))
        f = ms.over(s, h) if t < 0.5 else ms.over(h, s)
        return f
    out['activate'] = [mixf(i / 5, i) for i in range(6)]
    out['climb'] = [mixf(i / 3, i, True) for i in range(4)]
    def rollsaw(i, n):                              # диск катится, искры снизу
        f = tf(cv, sawsp[i % 12], dy=0)
        rnd = np.random.RandomState(i)
        f = sparks(f, [(cv.ax + rnd.randint(-14, 15), cv.ay - rnd.randint(0, 3)) for _ in range(3)], (255, 220, 120), 1)
        return f
    out['super'] = [rollsaw(i, 12) for i in range(12)]
    out['falldmg'] = [out['idle'][i % 4] for i in range(4)]
    return 'superdrom', cv, out, {'activate': (6, 4, 0), 'climb': (4, 4, 0), 'super': (12, 3, 1), 'falldmg': (4, 12, 1), 'idle': (4, 3, 1), 'walk': (6, 3, 1), 'run': (6, 3, 1)}, (30, 38)

def drunk_img(lean=0.0, bob=0, lL=0, lR=0, arm=-35, mouth=0, tilt=0.0, water=0, kneel=0, swing=0):
    im, d = new4(208, 232)
    K = (22, 16, 30, 255)
    TR = (72, 80, 100, 255); BO = (54, 42, 36, 255); COAT = (126, 96, 66, 255); COATD = (92, 68, 48, 255); SH = (214, 198, 150, 255)
    SK = (226, 160, 140, 255); SKD = (190, 120, 108, 255); GL = (60, 160, 80, 255); GLH = (140, 220, 150, 255)
    gx, gy = 104, 224
    hipx, hipy = gx + lean * 0.3, 140 - bob + kneel
    # ноги: колено с изгибом
    for lx, col in ((lR, (58, 64, 82, 255)), (lL, TR)):
        fx_, fy_ = gx + lx, gy - (6 if lx > 6 else 0)
        kx, ky = (hipx + fx_) / 2 + 6, (hipy + fy_) / 2 - 2 + kneel * 0.2
        thick(d, (hipx + (4 if col == TR else -4), hipy), (kx, ky), 24, K)
        thick(d, (kx, ky), (fx_, fy_ - 8), 22, K)
        thick(d, (hipx + (4 if col == TR else -4), hipy), (kx, ky), 18, col)
        thick(d, (kx, ky), (fx_, fy_ - 8), 16, col)
        d.ellipse([fx_ - 12, fy_ - 14, fx_ + 22, fy_ + 2], fill=BO, outline=K, width=3)
    # корпус
    sx_ = hipx + lean
    d.polygon([(sx_ - 34, 86 - bob * 0.5), (sx_ + 34, 86 - bob * 0.5), (hipx + 36, hipy + 6), (hipx - 36, hipy + 6)], fill=COAT, outline=K)
    d.polygon([(sx_ - 8, 86 - bob * 0.5), (sx_ + 12, 86 - bob * 0.5), (hipx + 12, hipy + 4), (hipx - 8, hipy + 4)], fill=SH)
    for (px, py) in ((sx_ + 2, 110), (sx_ - 2, 126), (sx_ + 6, 100)):
        d.ellipse([px - 4, py - 3, px + 4, py + 3], fill=(150, 120, 60, 255))
    d.line([(sx_ - 8, 86), (hipx - 8, hipy + 4)], fill=COATD, width=4)
    # задняя рука
    thick(d, (sx_ - 24, 94), (sx_ - 34 - swing * 0.2, 130), 18, K); thick(d, (sx_ - 24, 94), (sx_ - 34 - swing * 0.2, 130), 12, COATD)
    d.ellipse([sx_ - 42, 126, sx_ - 28, 140], fill=SK, outline=K, width=2)
    # голова
    hx, hy = sx_ + 8 + tilt * 0.4, 62 - bob * 0.5 + abs(tilt) * 0.3
    d.ellipse([hx - 26, hy - 26, hx + 26, hy + 26], fill=SK, outline=K, width=4)
    d.polygon([(hx - 24, hy + 4), (hx + 24, hy + 4), (hx + 20, hy + 30), (hx + 4, hy + 40), (hx - 14, hy + 32)], fill=(170, 170, 176, 255), outline=K)
    for k in range(7):
        d.line([(hx - 24 + 8 * k, hy - 18), (hx - 28 + 9 * k + (k % 2) * 6, hy - 36 - (k % 3) * 3)], fill=(150, 150, 156, 255), width=5)
    d.ellipse([hx + 12, hy - 8, hx + 32, hy + 8], fill=(220, 74, 74, 255), outline=K, width=3)
    d.ellipse([hx + 2, hy - 14, hx + 18, hy - 2], fill=(250, 250, 250, 255), outline=K, width=2)
    d.ellipse([hx + 9, hy - 11, hx + 15, hy - 5], fill=(200, 20, 30, 255))
    d.line([(hx - 4, hy - 20), (hx + 18, hy - 12)], fill=K, width=5)
    if mouth:
        d.ellipse([hx + 4, hy + 8, hx + 22, hy + 8 + mouth], fill=(60, 10, 20, 255), outline=K, width=3)
    else:
        d.line([(hx + 4, hy + 14), (hx + 20, hy + 12)], fill=K, width=3)
    if water:
        for k in range(water):
            d.ellipse([hx + 22 + k * 20, hy + 6 + (k % 2) * 6, hx + 40 + k * 20, hy + 18 + (k % 2) * 6], fill=(100, 190, 255, 255), outline=(40, 100, 200, 255), width=2)
    # передняя рука с бутылкой
    shx, shy = sx_ + 26, 92 - bob * 0.5
    a = math.radians(arm)
    ex, ey = shx + 30 * math.cos(a + 0.5), shy + 30 * math.sin(a + 0.5) + 14
    hx2, hy2 = ex + 28 * math.cos(a), ey + 28 * math.sin(a) + 6
    thick(d, (shx, shy), (ex, ey), 20, K); thick(d, (shx, shy), (ex, ey), 14, COAT)
    thick(d, (ex, ey), (hx2, hy2), 18, K); thick(d, (ex, ey), (hx2, hy2), 12, COAT)
    d.ellipse([hx2 - 8, hy2 - 8, hx2 + 8, hy2 + 8], fill=SK, outline=K, width=2)
    bx, by = hx2 + 10 * math.cos(a - 1.2), hy2 + 10 * math.sin(a - 1.2)
    ang = a - 1.2 - 0.3
    nx, ny = math.cos(ang), math.sin(ang)
    p0 = (hx2 - nx * 6, hy2 - ny * 6); p1 = (hx2 + nx * 34, hy2 + ny * 34)
    thick(d, p0, p1, 18, K); thick(d, p0, p1, 12, GL)
    d.line([(p0[0] + 2, p0[1] - 2), (p1[0] - nx * 14, p1[1] - ny * 14)], fill=GLH, width=3)
    nk = (p1[0] + nx * 12, p1[1] + ny * 12); thick(d, p1, nk, 9, K); thick(d, p1, nk, 5, GL)
    return im

def drunk_sprite(**k):
    s = fin4(drunk_img(**k)); return s, float(np.nonzero(s[..., 3][-6:].any(0))[0].mean()) if s[..., 3][-6:].any() else s.shape[1] / 2

def b_drunky():
    cv = Cv(96, 72)
    base = drunk_sprite()
    # центровка по ступням: ax = центр между ступнями
    W = base[0].shape[1]
    def DS(**k):
        s, _ = drunk_sprite(**k)
        # выровнять так, чтобы низ и центр ног были одинаковыми: берём центр по столбцам нижних 6 строк
        ys, xs = np.nonzero(s[..., 3][-8:]); ax = float(xs.mean()) if len(xs) else s.shape[1] / 2
        return s, ax
    sp0 = DS()
    spec = dict(p=dict(ib=1.0, wb=1.0, wl=2.0, rl=5.0, rb=1.0), muzzle=(0.6, 0.78))
    out = anim_frames(cv, sp0, spec)
    ph = lambda i, n: 2 * math.pi * i / n
    out['idle'] = [tf(cv, DS(lean=3 * math.sin(ph(i, 4)), bob=1 + int(1.5 * math.sin(ph(i, 4))), arm=-35 + 6 * math.sin(ph(i, 4)))) for i in range(4)]
    def stag(i, n, k, bob_):
        s_ = math.sin(ph(i, n)); c_ = math.cos(ph(i, n))
        return tf(cv, DS(lean=k * 0.5 * s_ + 4, bob=int(bob_ * abs(c_)), lL=int(k * 4 * s_), lR=int(-k * 4 * s_), arm=-35 + 12 * c_, swing=k * 2 * s_))
    out['walk'] = [stag(i, 6, 5, 3) for i in range(6)]
    out['run'] = [speed_lines(stag(i, 6, 7, 4), cv, 46, 2, 1, seed=i) for i in range(6)]
    def melee(i, n):
        t = i / (n - 1); a = -140 + 200 * smooth_ramp(min(1, t * 1.4))
        f = tf(cv, DS(lean=8 * math.sin(math.pi * t), arm=a, bob=0))
        if 0.3 < t < 0.75: f = ms.fx_arc(f, cv.ax + 14, cv.ay - 30, 18, -80, 80, (255, 250, 220), 2)
        return f
    out['melee'] = [melee(i, 8) for i in range(8)]
    def burp(i, n):
        t = i / (n - 1); m = int(16 * math.sin(math.pi * min(1, t * 1.1)))
        s_ = DS(lean=-6 * math.sin(math.pi * t), tilt=-10 * math.sin(math.pi * t), mouth=max(0, m), bob=0)
        f = tf(cv, s_, sx=1 + 0.05 * math.sin(math.pi * t))
        return f
    out['ranged'] = [burp(i, 9) for i in range(9)]
    def sup(i, n):
        t = i / (n - 1); arm = -110 if t > 0.1 else -35
        if t < 0.45: return tf(cv, DS(lean=-4, tilt=-12, arm=-100, mouth=6 + int(8 * t), bob=0))
        return tf(cv, DS(lean=-6, tilt=-12, arm=-100, mouth=18, water=1 + int((t - 0.45) * 5), bob=(i % 2)))
    out['super'] = [sup(i, 12) for i in range(12)]
    out['falldmg'] = [stars(tf(cv, DS(lean=14, kneel=26, bob=0, tilt=8, lL=14, lR=-10, arm=60)), cv.ax + 6, cv.ay - 50, i * 0.9, 3, 9) for i in range(4)]
    return 'drunky', cv, out, {'falldmg': (4, 12, 1)}, (18, 44)

BUILD = [b_deliver, b_firewalker, b_monoman, b_ironfem, b_maestro, b_bigpussy, b_coach, b_hacker, b_watcher, b_boot, b_superdrom, b_drunky]

def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', 'assets')
    os.makedirs(out, exist_ok=True)
    prev = []
    for fn in BUILD:
        name, cv, fr, timing, box = fn()
        for a in ANIMS: assert len(fr[a]) >= 1, (name, a)
        flat = write(name, cv, fr, out, timing, box)
        prev.append((name, cv, fr))
        print('baked', name, cv.cw, cv.ch, len(flat), 'frames')
    if len(sys.argv) > 2:
        os.makedirs(sys.argv[2], exist_ok=True)
        for name, cv, fr in prev:
            T = dict(DEF)
            cols = max(len(v) for v in fr.values())
            img = Image.new('RGB', (cols * cv.cw, len(ANIMS) * cv.ch), (110, 150, 190))
            for r, a in enumerate(ANIMS):
                for c, f in enumerate(fr[a]):
                    t = Image.fromarray(f, 'RGBA'); img.paste(t, (c * cv.cw, r * cv.ch), t)
            img.save(os.path.join(sys.argv[2], name + '.png'))

if __name__ == '__main__':
    main()
