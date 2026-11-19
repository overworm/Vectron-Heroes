#!/usr/bin/env python3
"""Спрайты из кадров пользователя (tools/art): Aunty, Cyberbum, Domina, Rocketpunk, Dumbpunk, Deliver, Ironfem, Monoman,
Firewalker, Megadog, Capsulo, SuperDrom. Реальные кадры ходьбы/атак + производные анимации из make_sprites_ext.anim_frames.
Запуск: python3 tools/make_sprites_user.py [out_assets_dir] [preview.png]"""
import math, os, sys
import numpy as np
from PIL import Image
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import make_sprites as ms
import make_sprites_ext as ex
import art_cut as ac

def prep(rel, target_h, flip=False, mult=1.0, anchor='torso', **kw):
    a = ac.cut(rel, flip=flip, **kw)
    sc = target_h * mult / a.shape[0]
    s = ms.crop_alpha(ms.downscale(a, sc))
    ys, xs = np.nonzero(s[..., 3])
    h = s.shape[0]
    if anchor == 'torso':
        sel = ys < ys.min() + h * 0.6
        ax = float(np.median(xs[sel]))
    elif anchor == 'bbox': ax = float(xs.mean())
    else: ax = ms.feet_x(s)
    return s, ax

def seq_frames(cv, seq, order, bob=0.0, dy0=0):
    out = []
    for k, idx in enumerate(order):
        d = -int(round(bob * abs(math.sin(math.pi * (k + 0.5) / len(order)))))
        out.append(ex.tf(cv, seq[idx], dy=d + dy0))
    return out

def over_time(frames_by_anim, timing, **kw): pass

GROUND_ANIMS = ('idle', 'walk', 'run', 'land', 'slide', 'activate', 'melee', 'ranged', 'super', 'hurt', 'falldmg', 'glide')
def ground_frame(cv, f):
    """сдвиг кадра по вертикали так, чтобы самая нижняя точка стояла на линии земли (cv.ay)"""
    ys = np.nonzero(f[..., 3].any(1))[0] if f.shape[-1] == 4 else np.nonzero((f.sum(-1) > 0).any(1))[0]
    if not len(ys): return f
    d = cv.ay - int(ys.max())
    if d == 0: return f
    out = np.zeros_like(f)
    if d > 0: out[d:] = f[:-d]
    else: out[:d] = f[-d:]
    return out

def finish(name, cv, sp_idle, spec, custom, timing, box=None, flying=False):
    fr = ex.anim_frames(cv, sp_idle, spec)
    fr.update(custom)
    if not flying and spec.get('p', {}).get('hov', 0) == 0:
        for a in GROUND_ANIMS:
            if a in fr: fr[a] = [ground_frame(cv, f) for f in fr[a]]
    ex.write(name, cv, fr, OUT, timing=timing, box=box)
    return name, cv, fr

def fx_swing(cv, c, cx, cy, r, a0, a1):
    return ms.fx_arc(c, cx, cy, r, a0, a1, (255, 250, 220), 2)

# ------------------------------------------------------------------ герои
def wave_fx(cv, f, t, y_off=2, col=(255, 250, 220)):
    """ударная волна по земле: две расходящиеся дуги"""
    r = 8 + int(26 * t)
    f = ms.fx_arc(f, cv.ax + 12, cv.ay - y_off - 4, r, -80, 80, col, 2)
    f = ms.fx_arc(f, cv.ax + 12, cv.ay - y_off - 4, max(4, r - 8), -70, 70, col, 1)
    return f

def aunty():
    H = 40
    s1 = prep('aunty/aunty_walk_step1.jpeg', H, mult=1.0); s2 = prep('aunty/aunty_walk_step2.jpeg', H)
    s3 = prep('aunty/aunty_walk_step3.jpg', H); s4 = prep('aunty/aunty_walk_step4.jpg', H)
    a1 = prep('aunty/aunty_attack.jpg', H); a2 = prep('aunty/aunty_attack2.jpeg', H)
    # стойка: без коричневой "земли" под сапогами (две нижние строки)
    sp3 = s3[0].copy(); ys = np.nonzero(sp3[..., 3].any(1))[0]
    sp3[ys.max() - 2:ys.max() + 1] = 0; s3 = (sp3, s3[1])
    # без метлы: убираем щётку слева и древко справа (полоса рук)
    nb = s3[0].copy(); hh, ww = nb.shape[:2]
    top = np.nonzero(nb[:int(hh * 0.35), :, 3].any(0))[0]
    bx0, bx1 = int(top.min()) - 1, int(top.max()) + 6
    band = slice(int(hh * 0.38), int(hh * 0.70))
    nb[band, :max(bx0, 0), :] = 0; nb[band, bx1:, :] = 0
    nobroom = (nb, s3[1])
    cv = ex.Cv(96, 68)
    spec = dict(p=dict(ib=0.8, wb=1.0, rl=5.0), muzzle=(0.9, 0.5))
    walk = seq_frames(cv, [s3, s4, s1, s2], [0, 1, 2, 3], bob=1)
    run = seq_frames(cv, [s3, s4, s1, s2], [2, 3, 2, 3, 1, 0], bob=2)
    run = [ms.fx_lines(f, [(cv.ax - 24, cv.ay - 14 - 6 * (i % 3), cv.ax - 32, cv.ay - 14 - 6 * (i % 3))], (230, 236, 250)) for i, f in enumerate(run)]
    melee = [ex.tf(cv, p, dx=d) for p, d in [(s3, -2), (a1, -3), (a1, 0), (a2, 3), (a2, 4), (a2, 2), (a1, 0), (s3, 0)]]
    melee[3] = fx_swing(cv, melee[3], cv.ax + 22, cv.ay - 20, 16, -80, 40); melee[4] = fx_swing(cv, melee[4], cv.ax + 24, cv.ay - 14, 18, -60, 60)
    idle = [ex.tf(cv, s3, dy=-int(round(0.8 * math.sin(2 * math.pi * i / 4)))) for i in range(4)]
    # копание метлой: частые короткие тычки вниз
    act = [ex.tf(cv, p, dx=d, dy=dy_) for p, d, dy_ in [(a1, 0, 0), (a2, 2, 0), (a1, 0, 0), (a2, 3, 0), (a1, 1, 0), (a2, 2, 0)]]
    # снежок: без метлы, бросок рукой
    ranged = [ex.tf(cv, nobroom, dx=d, rot=r) for d, r in [(-1, 0), (-3, -4), (-3, -6), (2, 4), (3, 6), (2, 3), (0, 0), (0, 0), (0, 0)]]
    # суператака: тяжёлый удар метлой с волной
    sup = []
    for i in range(14):
        t = i / 13
        if i < 4: f = ex.tf(cv, a1, dx=-3 - i, rot=-4 - 2 * i)
        elif i < 7: f = ex.tf(cv, a2, dx=2 + 3 * (i - 4), rot=6 + 3 * (i - 4))
        else: f = ex.tf(cv, a2, dx=8 - (i - 7), rot=8 - (i - 7))
        if i >= 5: f = wave_fx(cv, f, (i - 5) / 8)
        sup.append(f)
    custom = dict(walk=walk, run=run, melee=melee, ranged=ranged, idle=idle, activate=act, super=sup)
    tm = dict(walk=(4, 9, 1), run=(6, 4, 1), melee=(8, 3, 0), ranged=(9, 3, 0), activate=(6, 6, 1), super=(14, 4, 0))
    return finish('aunty', cv, s3, spec, custom, tm, (16, 40))

def cyberbum():
    H = 40
    idle = prep('cyberbum/cyberbum_idle.jpg', H)
    a = prep('cyberbum/cyberbum_attack.jpg', H, mult=1.0)
    w1 = prep('cyberbum/cyberbum_walk_step.jpeg', H); w2 = prep('cyberbum/cyberbum_walk_step2.jpeg', H); w3 = prep('cyberbum/cyberbum_walk_step3.jpeg', H)
    cv = ex.Cv(84, 68)
    spec = dict(p=dict(ib=0.8, wb=1.0, rl=4.0), muzzle=(0.8, 0.5))
    walk = seq_frames(cv, [w1, w2, w3, w2], [0, 1, 2, 3], bob=1)
    run = seq_frames(cv, [w1, w2, w3, w2], [0, 1, 2, 3, 0, 2], bob=2)
    melee = [ex.tf(cv, p, dx=d) for p, d in [(idle, 0), (a, -2), (a, -2), (a, 2), (a, 4), (a, 3), (a, 1), (idle, 0)]]
    melee[3] = fx_swing(cv, melee[3], cv.ax + 18, cv.ay - 26, 14, -70, 50); melee[4] = fx_swing(cv, melee[4], cv.ax + 20, cv.ay - 18, 16, -40, 70)
    # бутылка: в руке только бутылка, гаечного ключа нет (поза без ключа)
    ranged = [ex.tf(cv, idle, dx=d, rot=r) for d, r in [(0, 0), (-2, -6), (-3, -9), (1, 5), (3, 8), (2, 5), (0, 0), (0, 0), (0, 0)]]
    sup = []                                         # тяжёлый удар ключом с волной
    for i in range(14):
        if i < 4: f = ex.tf(cv, a, dx=-2 - i, rot=-5 - 3 * i)
        elif i < 7: f = ex.tf(cv, a, dx=3 + 3 * (i - 4), rot=8 + 4 * (i - 4))
        else: f = ex.tf(cv, a, dx=9 - (i - 7), rot=14 - 2 * (i - 7))
        if i >= 5: f = wave_fx(cv, f, (i - 5) / 8)
        sup.append(f)
    custom = dict(walk=walk, run=run, melee=melee, ranged=ranged, super=sup)
    tm = dict(walk=(4, 9, 1), run=(6, 4, 1), super=(14, 4, 0))
    return finish('cyberbum', cv, idle, spec, custom, tm, (14, 40))

def domina():
    H = 44
    idle = prep('domina/mistress_idle.jpg', H)
    w1 = prep('domina/mistress_walk1.jpg', H, flip=True); w2 = prep('domina/mistress_walk2.jpg', H, flip=True); w3 = prep('domina/mistress_walk3.jpg', H, flip=True)
    w4 = prep('domina/mistress_walk_step1.jpeg', H, flip=True); w5 = prep('domina/mistress_walk_step2.jpeg', H, flip=True)
    saw = prep('domina/mistress_attack.jpg', H, mult=1.1)
    cv = ex.Cv(96, 72)
    spec = dict(p=dict(ib=0.8, wb=1.0, rl=4.0), muzzle=(0.95, 0.35))
    # порядок кадров цикла бега: колено вверх -> шаг вперёд -> опора -> толчок назад (раньше шёл задом наперёд)
    walk = seq_frames(cv, [w2, w3, w1, w4], [0, 1, 2, 3], bob=1)
    run = seq_frames(cv, [w2, w3, w1, w4], [0, 1, 2, 3, 0, 1, 2, 3], bob=2)
    # простой удар: пинок ногой (подъём колена -> выброс ноги вперёд)
    kick = [(idle, 0, 0), (w3, -1, -3), (w3, 0, -4), (w2, 4, -9), (w2, 6, -12), (w2, 4, -8), (w3, 0, 0), (idle, 0, 0)]
    melee = [ex.tf(cv, p, dx=d, rot=r) for p, d, r in kick]
    for i in (3, 4, 5): melee[i] = ms.fx_arc(melee[i], cv.ax + 14, cv.ay - 20, 12 + 3 * (i - 3), -40, 50, (255, 240, 180), 2)
    sup = [ex.tf(cv, saw, dx=int(2 * math.sin(i * 1.7)), dy=0) for i in range(12)]
    for i in (3, 4, 5, 6, 7, 8): sup[i] = ms.fx_lines(sup[i], [(cv.ax + 20 + (i % 3) * 3, cv.ay - 12 - 2 * (i % 2), cv.ax + 38 + (i % 3) * 3, cv.ay - 12 - 2 * (i % 2))], (255, 240, 180))
    # волчок: поворот вокруг оси (сжатие по ширине, зеркало) + кольца вихря, замкнутый цикл
    glide = []
    for i in range(12):
        ph = 2 * math.pi * i / 12
        k = math.cos(ph)
        sxx = (1 if k >= 0 else -1) * max(0.18, abs(k))
        f = ex.tf(cv, idle, sx=sxx, dy=-2 - int(round(1.5 * math.sin(ph * 2))), rot=0)
        for ring, (rw, ry) in enumerate([(10, 8), (14, 22), (10, 36)]):
            f = ms.fx_arc(f, cv.ax, cv.ay - ry, rw + 2 * ring + (i % 3), 0 if (i + ring) % 2 else 180, 180 if (i + ring) % 2 else 360, (225, 205, 175), 1)
        glide.append(f)
    custom = dict(walk=walk, run=run, melee=melee, super=sup, glide=glide)
    tm = dict(walk=(4, 9, 1), run=(8, 4, 1), glide=(12, 3, 1))
    return finish('domina', cv, idle, spec, custom, tm, (12, 44))

# ------------------------------------------------------------------ враги
def rocketpunk():
    H = 44
    idle = prep('rocketpunk/rocketpunk.jpg', H, anchor='torso', lum=215, sat=30, flip=True)
    w = prep('rocketpunk/rocketpunk_walk.jpg', H, anchor='torso', lum=215, sat=30, flip=True)
    cv = ex.Cv(96, 72)
    spec = dict(p=dict(wb=1.0, wl=1.0, rl=4.0), muzzle=(0.98, 0.5))
    walk = seq_frames(cv, [idle, w], [0, 1, 0, 1], bob=1)
    return finish('rocketpunk', cv, idle, spec, dict(walk=walk), dict(walk=(4, 8, 1)), (16, 44))

def dumbpunk():
    H = 44
    idle = prep('dumbpunk/dumbpunk.jpg', H, lum=215, sat=30, dark=True)
    w1 = prep('dumbpunk/dumbpunk_walk1.jpg', H, lum=215, sat=30, dark=True); w2 = prep('dumbpunk/dumbpunk_walk2.jpg', H, lum=215, sat=30, dark=True)
    at = prep('dumbpunk/dumbpunk_attack.jpg', H, lum=215, sat=30, dark=True, mult=1.0)
    cv = ex.Cv(84, 72)
    spec = dict(p=dict(wb=1.0, wl=1.0, rl=4.0), muzzle=(0.9, 0.7))
    idle = w2
    walk = seq_frames(cv, [w1, w2], [0, 1, 0, 1], bob=1)
    melee = [ex.tf(cv, p, dx=d) for p, d in [(idle, 0), (idle, -2), (at, -3), (at, 0), (at, 2), (at, 2), (at, 0), (idle, 0)]]
    ranged = [ex.tf(cv, p, dx=d) for p, d in [(idle, 0), (idle, -2), (at, -2), (at, 0), (at, 3), (at, 2), (at, 1), (idle, 0), (idle, 0)]]
    return finish('dumbpunk', cv, idle, spec, dict(walk=walk, melee=melee, ranged=ranged), dict(walk=(4, 8, 1)), (16, 44))


# ------------------------------------------------------------------ враги: новые шаблоны
def prep_w(rel, target_w, flip=False, **kw):
    a = ac.cut(rel, flip=flip, **kw)
    s = ms.crop_alpha(ms.downscale(a, target_w / a.shape[1]))
    return s, float(np.nonzero(s[..., 3])[1].mean())

def prep_region(rel, box, target_h, flip=False, **kw):
    """кадр из листа: box=(x0,y0,x1,y1) в пикселях исходной картинки"""
    from PIL import Image as I
    im = I.open(os.path.join(ac.ART, rel)).convert('RGB').crop(box)
    tmp = os.path.join('/tmp', 'region_tmp.png'); im.save(tmp)
    kw.pop('anchor', None)
    a = ac.cut(tmp, flip=flip, **kw)
    s = ms.crop_alpha(ms.downscale(a, target_h / a.shape[0]))
    return s, float(np.nonzero(s[..., 3])[1].mean())

def deliver():
    H = 38
    idle = prep('deliver/deliver.jpg', H, flip=True, anchor='bbox', lum=215, sat=30)
    at = prep('deliver/deliver_attack.jpg', H, flip=True, anchor='bbox', lum=215, sat=30)
    cv = ex.Cv(88, 56)
    spec = dict(p=dict(wb=1.0, wl=1.5, rl=4.0, rb=1.0), muzzle=(0.95, 0.52))
    seqs = [(idle, -2, -3), (idle, -3, -5), (at, 0, 0), (at, 1, 0), (at, 2, 0), (at, 1, 0), (idle, 0, 0), (idle, 0, 0), (idle, 0, 0)]
    ranged = [ex.tf(cv, p, dx=d, rot=r) for p, d, r in seqs]
    return finish('deliver', cv, idle, spec, dict(ranged=ranged), {}, ex.body_box(idle))

def ironfem():
    H = 60
    idle = prep('ironfem/ironfem.jpg', H, anchor='bbox', lum=215, sat=30, dark=True)
    axe = prep('ironfem/ironfem_attack_axe.jpg', H, anchor='bbox', lum=215, sat=30, dark=True)
    chain = prep('ironfem/ironfem_attack_chain.jpg', H, anchor='bbox', lum=215, sat=30, dark=True)
    cv = ex.Cv(96, 76)
    spec = dict(p=dict(hov=0, wb=1.0, wl=1.5, rl=7.0, rb=1.0), muzzle=(0.74, 0.62))
    ranged = [ex.tf(cv, p, dx=d) for p, d in [(idle, -2), (idle, -3), (chain, -2), (chain, 0), (chain, 2), (chain, 3), (chain, 2), (idle, 0), (idle, 0)]]
    melee = [ex.tf(cv, p, dx=d) for p, d in [(idle, -2), (axe, -2), (axe, 0), (axe, 2), (axe, 3), (axe, 2), (idle, 0), (idle, 0)]]
    return finish('ironfem', cv, idle, spec, dict(ranged=ranged, melee=melee), {}, ex.body_box(idle))

def monoman():
    H = 46
    idle = prep('monoman/monoman.jpg', H, anchor='bbox', lum=215, sat=30, shadow=False)
    shot = prep('monoman/monoman_attack.jpg', H, anchor='bbox', lum=215, sat=30, shadow=False)
    gren = prep('monoman/monoman_attack2.jpg', H, anchor='bbox', lum=215, sat=30, shadow=False)
    cv = ex.Cv(96, 66)
    mz = ex.auto_muzzle(shot)
    spec = dict(p=dict(hov=3, ib=1.5, wb=1.5, wl=3.0, rl=8.0, rb=1.5), muzzle=mz)
    def sup(i, n):
        t = i / (n - 1)
        if t < 0.25: return ex.mirror_turn(cv, idle, t / 0.25, 3)
        if t > 0.8: return ex.mirror_turn(cv, idle, 1 - (t - 0.8) / 0.2, 3)
        f = ex.tf(cv, idle, sx=-1, dy=-3, rot=-4 * math.sin(t * 30))
        return ex.smoke(f, cv.ax + idle[0].shape[1] // 2 - 6, cv.ay - 11, (t - 0.25) / 0.55, (140, 200, 90))
    ranged = [ex.tf(cv, p, dy=-3, dx=d) for p, d in [(idle, -1), (idle, -2), (shot, -3), (shot, -1), (shot, 0), (shot, 0), (idle, 0), (idle, 0), (idle, 0)]]
    melee = [ex.tf(cv, p, dy=-3) for p in [idle, idle, gren, gren, gren, gren, idle, idle]]
    return finish('monoman', cv, idle, spec, dict(ranged=ranged, melee=melee, **{'super': [sup(i, 12) for i in range(12)]}), {}, ex.body_box(idle))

def firewalker():
    idle = prep('firewalker/firewalker.jpg', 32, anchor='bbox', lum=215, sat=30)
    cv = ex.Cv(100, 62)
    mz = ex.auto_muzzle(idle)
    spec = dict(p=dict(wb=1.0, wl=1.0, rl=3.0, rb=1.0), muzzle=mz)
    def ranged(i, n):
        t = i / (n - 1); H, W = idle[0].shape[:2]
        f = ex.tf(cv, idle, dx=-2 + (i % 2), rot=-2 + (i % 2))
        mx_ = int(cv.ax + (mz[0] - 0.5) * W) + 2; my_ = int(cv.ay - H * mz[1])
        if 0.15 < t < 0.9:
            f = ms.fx_disc(f, mx_ + 1, my_, 3, (255, 150, 30)); f = ms.fx_disc(f, mx_ + 1, my_, 1, (255, 240, 160))
        return f
    return finish('firewalker', cv, idle, spec, dict(ranged=[ranged(i, 9) for i in range(9)]), {'ranged': (9, 7, 0)}, ex.body_box(idle))

def megadog():
    rows = [(30, 310), (318, 568), (569, 818), (822, 1077)]
    cols3 = [(30, 330), (330, 640), (640, 938)]; cols2 = [(60, 470), (470, 938)]
    cells = []
    for ri, (y0, y1) in enumerate(rows):
        for (x0, x1) in (cols3 if ri == 0 else cols2 if ri < 3 else cols2[:1]):
            cells.append((x0, y0, x1, y1))
    frames = [prep_region('megadog/megadog_walk.jpg', c, 32, anchor='bbox', lum=215, sat=30, dark=True) for c in cells]
    cv = ex.Cv(72, 44)
    idle = frames[0]
    spec = dict(p=dict(ib=0.5, wb=0.5, wl=0.5, rl=2.0, rb=0.5), muzzle=(0.35, 0.95))
    walk = [ex.tf(cv, f, dy=0) for f in frames]
    run = [ex.tf(cv, frames[i % len(frames)], dx=1) for i in (0, 2, 4, 6, 1, 3, 5, 7)]
    ranged = [ex.tf(cv, frames[0], dx=-2 + (1 if i in (4, 5) else 0)) for i in range(9)]
    for i in (3, 4, 5): ranged[i] = ms.fx_disc(ranged[i], cv.ax - 6, cv.ay - 24, 3 - (i - 3) // 2 + 0, (255, 190, 70))
    return finish('megadog', cv, idle, spec, dict(walk=walk, run=run, ranged=ranged), dict(walk=(8, 4, 1), run=(8, 3, 1)), ex.body_box(idle))

def capsulo():
    H = 46
    win = prep('capsulo/capsuladoc.jpeg', H, anchor='bbox', lum=215, sat=30)
    wk = prep('capsulo/capsuladoc_walk.jpeg', H, anchor='bbox', lum=215, sat=30)
    laser = prep('capsulo/capsuledoc_attack_laser.jpeg', H, flip=True, anchor='bbox', lum=215, sat=30, shadow=True)
    plasma = prep('capsulo/capsuledoc_attack_plasma.jpeg', H, flip=True, anchor='bbox', lum=215, sat=30)
    wk2 = prep('capsulo/capsuledoc_walk2.jpeg', H, flip=True, anchor='bbox', lum=215, sat=30)
    cv = ex.Cv(88, 62)
    spec = dict(p=dict(wb=1.0, wl=1.0, rl=3.0, rb=1.0), muzzle=(0.6, 0.8))
    walk = [ex.tf(cv, p, dy=-int(b)) for p, b in [(win, 0), (wk, 1), (win, 0), (wk, 1)]]
    run = [ex.tf(cv, p, dy=-int(b)) for p, b in [(wk, 1), (win, 0), (wk, 1), (win, 0), (wk, 1), (win, 0)]]
    ranged = [ex.tf(cv, p, dx=d) for p, d in [(win, 0), (plasma, -1), (plasma, 0), (plasma, 1), (plasma, 2), (plasma, 2), (plasma, 1), (plasma, 0), (plasma, 0)]]
    melee = [ex.tf(cv, p, dx=d) for p, d in [(win, 0), (laser, -1), (laser, 0), (laser, 1), (laser, 2), (laser, 2), (laser, 1), (laser, 0)]]
    hurt = [ex.whiten(ex.tf(cv, win, dx=-3), 0.7), ex.whiten(ex.tf(cv, win, dx=-4), 0.4), ex.tf(cv, win, dx=-2)]
    return finish('capsulo', cv, win, spec, dict(walk=walk, run=run, ranged=ranged, melee=melee, hurt=hurt), dict(walk=(4, 9, 1), run=(6, 4, 1)), ex.body_box(win))

def superdrom():
    heli = prep_w('superdrom/superdrom_heli_attack.jpg', 62, lum=215, sat=30, shadow=False)
    saw = prep_w('superdrom/superdrom_saw_attack.jpg', 58, lum=215, sat=30, shadow=False)
    cv = ex.Cv(96, 66)
    spec = dict(p=dict(hov=0, ib=1.5, wb=1.5, wl=1.0, rl=4.0, rb=1.5), muzzle=(0.98, 0.55))
    ranged = []
    for i in range(9):
        f = ex.tf(cv, heli, dx=-(1 if i in (3, 5, 7) else 0))
        if 2 <= i <= 7:
            mx = cv.ax + heli[0].shape[1] // 2; my = cv.ay - int(heli[0].shape[0] * 0.55)
            f = ms.fx_disc(f, mx + 2, my, 3, (120, 200, 255)); f = ms.fx_disc(f, mx + 2, my, 1, (240, 250, 255))
        ranged.append(f)
    activate = []                                   # вертолёт -> пила: растворение и сборка
    for i in range(6):
        t = i / 5
        a_ = ex.dissolve(ex.tf(cv, heli), max(0.0, 1 - t * 1.4), i); b_ = ex.dissolve(ex.tf(cv, saw), min(1.0, max(0.0, t * 1.4 - 0.2)), i + 1)
        activate.append(ms.over(a_, b_))
    climb = list(reversed(activate[:4]))
    sup = []
    for i in range(12):
        f = ex.tf(cv, saw, dy=-(i % 2))
        sx = cv.ax + saw[0].shape[1] // 2 - 4; sy = cv.ay - int(saw[0].shape[0] * 0.5)
        f = ms.fx_lines(f, [(sx + 3, sy - 8 + (i * 3) % 14, sx + 12, sy - 10 + (i * 5) % 14)], (190, 235, 255))
        sup.append(f)
    return finish('superdrom', cv, heli, spec, dict(ranged=ranged, melee=ranged[:8], activate=activate, climb=climb, **{'super': sup}),
                  dict(activate=(6, 4, 0), climb=(4, 4, 0), super=(12, 3, 1)), (44, 24), flying=True)


# ------------------------------------------------------------------ Ramrod, Nerd, Refreezer, Kozemekis
def ramrod():
    H = 54
    kw = dict(lum=215, sat=30, dark=True)
    r1 = prep('ramrod/ramrod/ramrod_run_attack1.jpeg', H, **kw); r2 = prep('ramrod/ramrod/ramrod_run_attack2.jpeg', H, **kw)
    r3 = prep('ramrod/ramrod/ramrod_run_attack3.jpeg', H, **kw)
    idle = r1                       # стоит лицом вправо со штангой на плечах (раньше был вид со спины и он ходил спиной)
    spin = prep('ramrod/ramrod/ramrod_superattack.jpg', H * 1.3, shadow=False, lum=225, sat=22)   # вихрь входит в кадр, сам герой остаётся того же размера
    cv = ex.Cv(120, 100)
    spec = dict(p=dict(ib=0.8, wb=1.0, rl=3.0, rb=2.0), muzzle=(0.9, 0.5))
    walk = seq_frames(cv, [r1, r2, r3, r2], [0, 1, 2, 3], bob=1)
    run = seq_frames(cv, [r1, r2, r3, r2], [0, 1, 2, 3, 2, 1], bob=2)
    melee = [ex.tf(cv, p, dx=d) for p, d in [(r1, -3), (r1, -4), (r2, 0), (r3, 4), (r3, 6), (r2, 4), (r2, 2), (r1, 0)]]
    ranged = [ex.tf(cv, p, dx=d) for p, d in [(r1, 0), (r1, -2), (r1, -3), (r2, 2), (r3, 5), (r2, 3), (r1, 1), (r1, 0), (r1, 0)]]
    sup = [ex.tf(cv, spin, dx=0, mirror=(i % 6 >= 3)) for i in range(24)]
    custom = dict(walk=walk, run=run, melee=melee, ranged=ranged, super=sup, idle=[ex.tf(cv, idle, dy=-int(round(0.8 * math.sin(2 * math.pi * i / 4)))) for i in range(4)])
    tm = dict(walk=(4, 9, 1), run=(6, 5, 1), super=(24, 5, 1))
    return finish('ramrod', cv, idle, spec, custom, tm, (20, 54))

def nerd():
    H = 40
    kw = dict(lum=225, sat=26, shadow=False)          # без удаления серой "тени": это кроссовки
    idle = prep('nerd/nerd/nerd_idle.jpg', H, **kw)
    walk1 = prep('nerd/nerd/nerd_walk.jpeg', H, **kw)
    play = prep('nerd/nerd/nerd_guitarplay_attack.jpeg', H, **kw)
    swing = prep('nerd/nerd/nerd_attack_guitar.jpeg', H, **kw)
    # убрать плоскую серую тень под ногами у idle (тонкий эллипс ниже подошв)
    for sp in (idle, walk1, play, swing):
        a = sp[0]; ys = np.nonzero(a[..., 3].any(1))[0]
        for y in range(ys.max(), ys.max() - 3, -1):
            row = a[y]; op = row[..., 3] > 0
            if op.any() and (np.abs(row[op, :3].astype(int) - row[op, :3].astype(int).mean(-1, keepdims=True)).max() < 7) and 40 < row[op, :3].mean() < 110: a[y, :, 3] = 0
    cv = ex.Cv(84, 68)
    spec = dict(p=dict(ib=0.6, wb=1.0, rl=2.0), muzzle=(0.8, 0.6))
    walk = seq_frames(cv, [idle, walk1], [0, 1, 0, 1, 0, 1], bob=1)
    run = seq_frames(cv, [idle, walk1], [1, 0, 1, 0, 1, 0], bob=2)
    # простая атака: пинок ногой (поза шага с наклоном назад и выбросом вперёд)
    kick = [(idle, 0, 0), (walk1, -2, -4), (walk1, -1, -6), (walk1, 5, -12), (walk1, 7, -14), (walk1, 5, -9), (walk1, 1, -3), (idle, 0, 0)]
    melee = [ex.tf(cv, p, dx=d, rot=r) for p, d, r in kick]
    for i in (3, 4, 5): melee[i] = ms.fx_arc(melee[i], cv.ax + 14, cv.ay - 12, 10 + 3 * (i - 3), -40, 50, (255, 240, 180), 2)
    # дальняя: бросок гитары (поза с замахом)
    ranged = [ex.tf(cv, p, dx=d) for p, d in [(idle, 0), (swing, -3), (swing, -2), (swing, 2), (swing, 3), (idle, 1), (idle, 0), (idle, 0), (idle, 0)]]
    sup = []
    for i in range(24):
        f = ex.tf(cv, play, dx=0, dy=-(1 if (i // 2) % 2 else 0), rot=2 * math.sin(i * 0.9))
        if i % 3 == 0: f = ms.fx_arc(f, cv.ax + 20, cv.ay - 22, 10 + (i % 6) * 2, -50, 50, (255, 240, 160), 1)
        sup.append(f)
    custom = dict(walk=walk, run=run, melee=melee, ranged=ranged, super=sup)
    tm = dict(walk=(6, 12, 1), run=(6, 6, 1), melee=(8, 3, 0), ranged=(9, 3, 0), super=(24, 5, 1))
    g = prep('nerd/nerd/nerd_flying_guitar.jpeg', 20, anchor='bbox', lum=225, sat=22, shadow=False)[0]
    gh = g.shape[0]; S = int(max(g.shape[:2]) * 1.5) // 2 * 2 + 2
    strip = np.zeros((S, S * 8, 3), np.uint8)
    from PIL import Image as I
    for k in range(8):
        im = I.fromarray(g, 'RGBA'); canvas = I.new('RGBA', (S, S), (0, 0, 0, 0)); canvas.paste(im, ((S - g.shape[1]) // 2, (S - gh) // 2), im)
        r = np.array(canvas.rotate(-45 * k, resample=I.NEAREST))
        rgb = r[..., :3].copy(); al = r[..., 3] > 100
        rgb[al & (rgb.sum(-1) < 6)] = 1; rgb[~al] = 0
        strip[:, k * S:(k + 1) * S] = rgb
    I.fromarray(strip).save(os.path.join(OUT, 'guitar.bmp'))
    return finish('nerd', cv, idle, spec, custom, tm, (12, 38))

def refreezer():
    H = 34
    kw = dict(lum=215, sat=30)
    closed = prep('refreezer/refreezer/refreezer_move.jpeg', H, **kw)
    tilt = prep('refreezer/refreezer/refreezer_attack.jpeg', H, **kw)
    empty = prep('refreezer/refreezer/refreezer_empty.jpeg', H, **kw)
    full = prep('refreezer/refreezer/refreezer_superattack.jpeg', H, **kw)
    cv = ex.Cv(80, 52)
    spec = dict(p=dict(ib=0.4, wb=0.8, rl=0.0, rb=0.8), muzzle=(0.7, 0.5))
    # ходьба, бег и разгон: как стойка, без наклонов вправо/влево (наклон только у простой атаки)
    walk = [ex.tf(cv, closed, dy=-(i % 2)) for i in range(6)]
    run = [ms.fx_lines(ex.tf(cv, closed, dy=-(i % 2)), [(cv.ax - 18, cv.ay - 10 - 5 * (i % 3), cv.ax - 30, cv.ay - 10 - 5 * (i % 3))], (230, 236, 250)) for i in range(6)]
    melee = [ex.tf(cv, p, dx=d) for p, d in [(closed, 0), (closed, -3), (tilt, -2), (tilt, 4), (tilt, 7), (tilt, 4), (closed, 1), (closed, 0)]]
    ranged = [ex.tf(cv, p) for p in [closed, closed, full, full, full, full, closed, closed, closed]]
    sup = [ms.fx_lines(ex.tf(cv, closed, dx=int(1.5 * math.sin(i)), dy=-(i % 2)), [(cv.ax - 20, cv.ay - 8 - 6 * (i % 3), cv.ax - 36, cv.ay - 8 - 6 * (i % 3)), (cv.ax - 18, cv.ay - 22 + 3 * (i % 2), cv.ax - 30, cv.ay - 22 + 3 * (i % 2))], (255, 255, 255)) for i in range(8)]
    act = [ex.tf(cv, empty, dy=0) for i in range(4)]
    clm = [ex.tf(cv, full, dy=0) for i in range(4)]
    custom = dict(walk=walk, run=run, melee=melee, ranged=ranged, super=sup, activate=act, climb=clm)
    tm = dict(walk=(6, 5, 1), run=(6, 3, 1), super=(8, 3, 1), activate=(4, 10, 1), climb=(4, 10, 1))
    return finish('refreezer', cv, closed, spec, custom, tm, (22, 30))

def drunky():
    a = ac.cut('kozemekis2/kozemekis/kozemekis.jpeg', lum=235, sat=26)
    H0, W0 = a.shape[:2]
    ys, xs = np.nonzero(a[..., 3]); y0, y1, x0, x1 = ys.min(), ys.max(), xs.min(), xs.max()
    yy, xx = np.mgrid[:H0, :W0]
    gap = (yy > y0 + 0.58 * (y1 - y0)) & (xx > x0 + 0.28 * (x1 - x0)) & (xx < x0 + 0.72 * (x1 - x0)) & (a[..., :3].min(-1) > 205)
    a[gap, 3] = 0
    s = ms.crop_alpha(ms.downscale(a, 52.0 / a.shape[0]))
    sp = (s, float(np.nonzero(s[..., 3])[1].mean()))
    cv = ex.Cv(96, 72)
    spec = dict(p=dict(hov=0, ib=1.5, wb=1.5, wl=2.0, rl=4.0, rb=1.5), muzzle=(0.9, 0.7))
    W = s.shape[1]; Hh = s.shape[0]
    mouth = (cv.ax + int(W * 0.05), cv.ay - int(Hh * 0.62))
    sup = []
    for i in range(14):
        t = i / 13
        f = ex.tf(cv, sp, rot=-8 * math.sin(math.pi * t), dy=-int(1.5 * abs(math.sin(i))))
        if 4 <= i <= 11:
            for k in range(3):
                f = ms.fx_disc(f, mouth[0] + 6 + (i - 4) * 3 + k * 4, mouth[1] + int(2 * math.sin(i + k)), 3 + k, (110, 220, 60))
        sup.append(f)
    ranged = [ex.tf(cv, sp, rot=[0, -4, -8, 4, 6, 3, 0, 0, 0][i], dx=[0, -2, -3, 2, 3, 1, 0, 0, 0][i]) for i in range(9)]
    custom = dict(super=sup, ranged=ranged, melee=ranged[:8])
    return finish('drunky', cv, sp, spec, custom, dict(super=(14, 5, 0)), (30, 40), flying=True)

def portraits():
    for name, rel, frac in [('aunty', 'aunty/aunty_walk_step3.jpg', 0.36), ('cyberbum', 'cyberbum/cyberbum_idle.jpg', 0.34), ('domina', 'domina/mistress_idle.jpg', 0.22), ('nerd', 'nerd/nerd/nerd_idle.jpg', 0.30), ('refreezer', 'refreezer/refreezer/refreezer_move.jpeg', 0.95)]:
        a = ac.cut(rel); h = int(a.shape[0] * frac); a = a[:h]
        ys, xs = np.nonzero(a[..., 3]); a = a[:, xs.min():xs.max() + 1]
        w = a.shape[1]; S = max(w, h)
        bg = Image.new('RGBA', (S, S), (30, 38, 58, 255)); t = Image.fromarray(a, 'RGBA'); bg.paste(t, ((S - w) // 2, 0), t)
        rgb = np.array(bg.convert('RGB').resize((32, 32), Image.BOX))
        rgb[rgb.sum(-1) < 6] = (1, 1, 1)
        Image.fromarray(rgb).save(os.path.join(OUT, 'portrait_%s.bmp' % name))

OUT = 'assets'
def main():
    global OUT
    OUT = sys.argv[1] if len(sys.argv) > 1 else 'assets'
    os.makedirs(OUT, exist_ok=True)
    only = sys.argv[3:] if len(sys.argv) > 3 else None
    res = []
    for fn in [aunty, cyberbum, domina, rocketpunk, dumbpunk, deliver, ironfem, monoman, firewalker, megadog, capsulo, superdrom, ramrod, nerd, refreezer, drunky]:
        if only and fn.__name__ not in only: continue
        res.append(fn()); print('baked', fn.__name__)
    if not only: portraits()
    if len(sys.argv) > 2 and sys.argv[2] != '-':
        strip(res, sys.argv[2])

def strip(res, path):
    rows = []
    for name, cv, fr in res:
        cells = []
        for a in ['idle', 'walk', 'run', 'melee', 'ranged', 'super', 'activate']:
            cells += fr[a][:8]
        rows.append((cv, cells))
    cols = max(len(c) for _, c in rows)
    cw = max(cv.cw for cv, _ in rows); ch = max(cv.ch for cv, _ in rows)
    img = Image.new('RGB', (cols * cw, len(rows) * ch), (110, 150, 190))
    for r, (cv, cells) in enumerate(rows):
        for k, f in enumerate(cells):
            t = Image.fromarray(f, 'RGBA'); img.paste(t, (k * cw, r * ch), t)
    img.save(path)

if __name__ == '__main__': main()
