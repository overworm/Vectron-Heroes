"""Вырезает персонажей из эскизов PengiCore в RGBA-PNG полного разрешения (фон, тени и мелкие предметы убираются)."""
import sys, os
import numpy as np
from PIL import Image
from scipy import ndimage as ndi

def load(path):
    return np.asarray(Image.open(path).convert('RGB')).astype(np.int32)

def bg_like(a, lum_min=186, sat_max=26):
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    lum = (r * 299 + g * 587 + b * 114) // 1000
    sat = a.max(-1) - a.min(-1)
    return (lum >= lum_min) & (sat <= sat_max)

def remove_bg(a, lum_min=186, sat_max=26, keep='largest'):
    """Фон = светлые малонасыщенные пиксели, связанные с границей кадра. Остаётся самая большая связная область."""
    cand = bg_like(a, lum_min, sat_max)
    lab, n = ndi.label(cand)
    border = set(np.unique(np.concatenate([lab[0], lab[-1], lab[:, 0], lab[:, -1]]))) - {0}
    bg = np.isin(lab, list(border))
    fg = ~bg
    fg = ndi.binary_opening(fg, iterations=1)
    return fg

def largest(mask, connectivity=8):
    st = np.ones((3, 3)) if connectivity == 8 else None
    lab, n = ndi.label(mask, structure=st)
    if n == 0: return mask
    sizes = ndi.sum(mask, lab, range(1, n + 1))
    return lab == (1 + int(np.argmax(sizes)))

def clean(m):
    """убирает тонкие горизонтальные хвосты (тени, линии платформ) и оторванные предметы"""
    o = ndi.binary_opening(m, structure=np.ones((13, 1)))
    o = largest(o)
    o = ndi.binary_dilation(o, structure=np.ones((3, 3)), iterations=2) & m
    return largest(o)

def unshadow(a, m):
    """серая тень под ногами: малонасыщенные пиксели в нижних 8% высоты; оторванное после этого выбрасывается"""
    ys, xs = np.nonzero(m)
    y1 = ys.max(); band = int((y1 - ys.min()) * 0.08)
    sat = a.max(-1) - a.min(-1); lum = (a[..., 0] * 299 + a[..., 1] * 587 + a[..., 2] * 114) // 1000
    bad = np.zeros_like(m); bad[y1 - band:y1 + 1] = (sat[y1 - band:y1 + 1] < 24) & (lum[y1 - band:y1 + 1] < 228) & (lum[y1 - band:y1 + 1] > 90)
    m2 = m & ~bad
    m2 = ndi.binary_opening(m2, structure=np.ones((3, 3)))
    return largest(m2)

def rgba(a, mask):
    out = np.zeros(a.shape[:2] + (4,), np.uint8)
    out[..., :3] = a.clip(0, 255)
    out[..., 3] = (mask * 255).astype(np.uint8)
    return out

def crop(im):
    ys, xs = np.nonzero(im[..., 3])
    return im[ys.min():ys.max() + 1, xs.min():xs.max() + 1]

def save(name, im, out):
    Image.fromarray(crop(im)).save(os.path.join(out, name + '.png'))

def main(src, out):
    os.makedirs(out, exist_ok=True)
    P = lambda f: os.path.join(src, f)
    # --- обычные: фон белый
    simple = {
        'pengi_idle': 'pengi_idle.jpg', 'bum_idle': 'bum_idle.jpg', 'bum_attack': 'bum_attack.jpg',
        'door_attack': 'doorkeeper_attack.jpg', 'rocket': 'enemy_heavygunner.jpg', 'body': 'enemy_scout.jpg',
        'prof': 'enemy_professor.jpeg', 'mis_idle': 'enemy_mistress_idle.jpg', 'mis_saw': 'enemy_mistress_attack.jpg',
    }
    shadowy = {'door_attack', 'mis_idle', 'mis_saw', 'bum_idle', 'bum_attack', 'pengi_idle'}
    for k, f in simple.items():
        a = load(P(f))
        m = clean(largest(remove_bg(a, 150 if k in shadowy else 186, 24 if k in shadowy else 26)))
        if k in shadowy or k == 'rocket': m = unshadow(a, m)
        save(k, rgba(a, m), out)
    # --- пингвин с бластером: фон - горы, берём силуэт идла + ствол по цвету
    ai = load(P('pengi_idle.jpg')); aa = load(P('pengi_attack.jpg'))
    mi = largest(remove_bg(ai))
    ys, xs = np.nonzero(mi)
    # совмещение: по верху шапки и левой кромке (шапка не меняется)
    r, g, b = aa[..., 0], aa[..., 1], aa[..., 2]
    lum = (r * 299 + g * 587 + b * 114) // 1000
    mx = aa.max(-1) - aa.min(-1)
    gun = ((g - r) > 85) | ((lum < 100) & (mx < 70)) | (((r - b) > 15) & (lum < 150))
    box = np.zeros_like(gun); box[340:640, 440:1130] = True      # область ствола (без вспышки справа)
    gun &= box
    gun = ndi.binary_opening(gun, structure=np.ones((3, 3)), iterations=2)
    gun = ndi.binary_closing(gun, iterations=2)
    body = mi.copy()
    body = ndi.binary_dilation(body, iterations=1)
    m = (body | gun)
    m = clean(largest(ndi.binary_opening(m, iterations=1)))
    m = ndi.binary_fill_holes(m)
    save('pengi_gun', rgba(aa, m), out)
    print('ok')

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
