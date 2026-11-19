#!/usr/bin/env python3
"""Предметы уровня из картинок: мусорный бак -> assets/crate.bmp (24 бит, чёрный 0,0,0 = прозрачно).
Использование: python3 -I tools/make_props.py <картинка бака> <assets> [ширина_px=48]"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from PIL import Image
import sprite_extract as se
import make_sprites as ms

def main():
    src, out = sys.argv[1], sys.argv[2]
    width = int(sys.argv[3]) if len(sys.argv) > 3 else 48
    a = se.load(src)
    m = se.unshadow(a, se.clean(se.largest(se.remove_bg(a, 150, 24))))
    im = se.crop(se.rgba(a, m))
    sc = width / im.shape[1]
    sp = ms.downscale(im, sc, colors=32)
    sp = ms.crop_alpha(sp)
    os.makedirs(out, exist_ok=True)
    Image.fromarray(ms.to_bgr_key(sp)).save(os.path.join(out, 'crate.bmp'))
    print('crate.bmp', sp.shape[1], 'x', sp.shape[0])

if __name__ == '__main__':
    main()
