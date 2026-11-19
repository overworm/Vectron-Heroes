"""Вырезание поз из пользовательских кадров tools/art/<имя>/*. Фон белый, тень-эллипс и маркеры-капсулы убираются."""
import os, sys, numpy as np
from scipy import ndimage as ndi
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import sprite_extract as se
ART = os.path.join(HERE, 'art')

def cut(path, lum=200, sat=40, clean=False, shadow=True, flip=False, keep_n=1, min_frac=0.0, enclosed=40, dark=False):
    a = se.load(path if os.path.isabs(path) else os.path.join(ART, path))
    fg = se.remove_bg(a, lum, sat)
    sat0 = a.max(-1) - a.min(-1); lum0 = (a[..., 0] * 299 + a[..., 1] * 587 + a[..., 2] * 114) // 1000
    flat = (sat0 <= 4) & (lum0 >= 58) & (lum0 <= 78)               # плоская серая тень-эллипс
    lab0, n0 = ndi.label(flat)
    if n0:
        sz0 = ndi.sum(flat, lab0, range(1, n0 + 1))
        big0 = [k + 1 for k, v in enumerate(sz0) if v >= 120]
        flat = np.isin(lab0, big0)
        fg &= ~ndi.binary_dilation(flat, iterations=1)
    if enclosed:
        cand = se.bg_like(a, lum, sat)
        lab, n = ndi.label(cand)
        sizes = ndi.sum(cand, lab, range(1, n + 1))
        ys0, xs0 = np.nonzero(fg); yb = ys0.max(); hh = yb - ys0.min()
        sat_ = a.max(-1) - a.min(-1); lum_ = (a[..., 0] * 299 + a[..., 1] * 587 + a[..., 2] * 114) // 1000
        shd = np.zeros_like(fg); band = slice(max(0, yb - int(hh * 0.15)), yb + 3)
        shd[band] = (sat_[band] < 26) & (lum_[band] < 215) & (lum_[band] > 70)
        shd = ndi.binary_opening(shd, structure=np.ones((3, 3))) | ndi.binary_dilation(flat, iterations=2)
        rm = []
        for k, sl in enumerate(ndi.find_objects(lab), 1):
            if sizes[k - 1] < (12 if dark else enclosed): continue
            r = lab == k
            if dark or ((ndi.binary_dilation(r, iterations=3) & shd).any() and sl[0].start > ys0.min() + hh * 0.35): rm.append(k)
        fg &= ~np.isin(lab, rm)
    if keep_n == 1: m = se.largest(fg)
    else: m = fg
    if clean: m = se.clean(m)
    if shadow: m = unshadow2(a, m)
    m = fill_small(m)
    im = se.crop(se.rgba(a, m))
    if flip: im = im[:, ::-1].copy()
    return im

def fill_small(m, frac=0.0006):
    holes = ~m
    lab, n = ndi.label(holes)
    lim = max(30, int(m.size * frac))
    border = set(np.unique(np.concatenate([lab[0], lab[-1], lab[:, 0], lab[:, -1]])))
    out = m.copy()
    for k in range(1, n + 1):
        if k in border: continue
        r = lab == k
        if r.sum() < lim: out |= r
    return out

def unshadow2(a, m):
    """тень: серые малонасыщенные пиксели в нижних 12% (шире, чем в sprite_extract)"""
    ys, xs = np.nonzero(m)
    y1 = ys.max(); H = y1 - ys.min(); band = int(H * 0.12)
    sat = a.max(-1) - a.min(-1); lum = (a[..., 0] * 299 + a[..., 1] * 587 + a[..., 2] * 114) // 1000
    bad = np.zeros_like(m)
    sl = slice(max(0, y1 - band), y1 + 1)
    bad[sl] = (sat[sl] < 26) & (lum[sl] < 215) & (lum[sl] > 70)
    # серая тень - большая связная область: убираем только то, что связано с нижней границей и широкое
    lab, n = ndi.label(bad)
    out = m.copy()
    for k in range(1, n + 1):
        r = lab == k
        if r.sum() > 12: out &= ~r
    blue = np.zeros_like(m)                       # маркеры-капсулы и голубая тень у ног
    blue[sl] = (a[sl][..., 2] > a[sl][..., 0] + 28) & (a[sl][..., 2] > a[sl][..., 1] + 5) & (lum[sl] > 110)
    out &= ~ndi.binary_dilation(blue, iterations=1)
    out = ndi.binary_opening(out, structure=np.ones((3, 3)))
    return se.largest(out)

def split_sheet(path, lum=215, sat=30, min_area=600):
    """лист с несколькими кадрами: возвращает список вырезок слева-направо по строкам"""
    a = se.load(os.path.join(ART, path))
    fg = se.remove_bg(a, lum, sat)
    fg = ndi.binary_closing(fg, iterations=2)
    lab, n = ndi.label(fg, structure=np.ones((3, 3)))
    boxes = []
    for k, sl in enumerate(ndi.find_objects(lab), 1):
        area = (lab[sl] == k).sum()
        if area < min_area: continue
        boxes.append((sl, k))
    out = []
    for sl, k in boxes:
        m = lab == k
        out.append((sl[0].start, sl[1].start, se.crop(se.rgba(a, ndi.binary_fill_holes(m)))))
    return out
