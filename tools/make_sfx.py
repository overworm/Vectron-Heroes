#!/usr/bin/env python3
"""Шаблонные звуки и музыка в духе SNES: 32 кГц, 8-битная зернистость, прямоугольные/треугольные волны и шум.
Использование:  python3 -I tools/make_sfx.py <корень проекта>      -> assets/sfx/*.wav и assets/bgm/*.mp3 (если есть ffmpeg, иначе .wav). Существующие mp3 музыки не трогает (--force - перезаписать)
Свои звуки: положите assets/sfx/<имя>.mp3 (имена в assets/sfx/README.txt) и assets/bgm/<имя>.mp3 (level1, boss, win) - mp3 загружается раньше wav."""
import os, subprocess, shutil, sys, wave
import numpy as np

SR = 32000
rng = np.random.default_rng(7)

def t_(d): return np.arange(int(SR * d)) / SR
def env(n, a=0.005, d=0.0, s=1.0, r=0.05, total=None):
    e = np.ones(n); na, nr = int(SR * a), int(SR * r)
    if na: e[:na] = np.linspace(0, 1, na)
    if nr: e[-nr:] *= np.linspace(1, 0, nr)
    return e
def expd(n, k): return np.exp(-np.linspace(0, k, n))
def osc(freq, d, wave='sq', duty=0.5):
    n = int(SR * d); f = np.broadcast_to(np.asarray(freq, float), (n,)) if np.ndim(freq) == 0 else np.interp(np.linspace(0, 1, n), np.linspace(0, 1, len(freq)), freq)
    ph = np.cumsum(f) / SR
    fr = ph % 1.0
    if wave == 'sq': return np.where(fr < duty, 1.0, -1.0)
    if wave == 'tri': return 4 * np.abs(fr - 0.5) - 1
    if wave == 'saw': return 2 * fr - 1
    return np.sin(2 * np.pi * ph)
def noise(d, lp=0.0):
    x = rng.uniform(-1, 1, int(SR * d))
    if lp > 0:
        y = np.zeros_like(x); a = 1 - lp
        for i in range(1, len(x)): y[i] = y[i - 1] + a * (x[i] - y[i - 1])
        x = y / max(1e-6, np.abs(y).max())
    return x
def sweep(f0, f1, d): return np.linspace(f0, f1, int(SR * d))
def mix(*parts):
    n = max(len(p) for p in parts); out = np.zeros(n)
    for p in parts: out[:len(p)] += p
    return out
def seq(*parts):
    return np.concatenate(parts)
def snes(x, bits=8, vol=0.8):
    x = x / max(1e-6, np.abs(x).max()) * vol
    q = 2 ** (bits - 1)
    return np.round(x * q) / q
def save_wav(path, x):
    pcm = (np.clip(x, -1, 1) * 32767).astype(np.int16)
    with wave.open(path, 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR); w.writeframes(pcm.tobytes())

NOTE = {n: 440 * 2 ** ((i - 9) / 12) for i, n in enumerate(['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'])}
def hz(name):
    n, o = name[:-1], int(name[-1]); return NOTE[n] * 2 ** (o - 4)

def build_sfx():
    S = {}
    d = 0.16; n = int(SR * d)
    S['jump'] = osc(sweep(260, 640, d), d, 'sq', 0.25) * env(n, 0.002, r=0.06)
    d = 0.10; n = int(SR * d); S['land'] = mix(noise(d, 0.6) * expd(n, 5), osc(sweep(120, 60, d), d, 'tri') * expd(n, 3))
    d = 0.22; n = int(SR * d); S['hurt'] = osc(sweep(700, 180, d), d, 'sq', 0.5) * env(n, 0.002, r=0.05) * 0.9
    d = 0.8; n = int(SR * d); w = 1 + 0.03 * np.sin(np.linspace(0, 40, n)); S['death'] = osc(sweep(520, 50, d) * w, d, 'saw') * expd(n, 2.2) * env(n, 0.002, r=0.1)
    d = 0.14; n = int(SR * d); S['swing'] = noise(d, 0.35) * np.sin(np.linspace(0, np.pi, n)) ** 1.5
    d = 0.12; n = int(SR * d); S['hit'] = mix(noise(d, 0.2) * expd(n, 7), osc(sweep(300, 90, d), d, 'sq') * expd(n, 6) * 0.7)
    d = 0.14; n = int(SR * d); S['shot'] = osc(sweep(1500, 380, d), d, 'sq', 0.125) * expd(n, 3.5)
    S['super'] = seq(*[osc(hz(x), 0.07, 'sq', 0.25) * env(int(SR * 0.07), 0.002, r=0.02) for x in ['C4', 'E4', 'G4', 'C5', 'E5', 'G5']],
                     mix(noise(0.25, 0.3) * expd(int(SR * 0.25), 4), osc(sweep(400, 120, 0.25), 0.25, 'saw') * expd(int(SR * 0.25), 4)))
    d = 0.16; n = int(SR * d); S['throw'] = noise(d, 0.5) * np.sin(np.linspace(0, np.pi, n)) * 0.8
    d = 0.4; n = int(SR * d); S['rocket'] = mix(noise(d, 0.75) * np.linspace(0.3, 1, n) * 0.9, osc(sweep(90, 210, d), d, 'sq') * 0.35) * env(n, 0.01, r=0.1)
    d = 0.7; n = int(SR * d); S['explode'] = mix(noise(d, 0.88) * expd(n, 4) * 1.2, osc(sweep(110, 35, d), d, 'tri') * expd(n, 3), noise(0.08, 0.1) * 0.5)
    S['button'] = seq(osc(1100, 0.04, 'sq') * 0.7, np.zeros(int(SR * 0.03)), osc(1500, 0.06, 'sq') * 0.7 * expd(int(SR * 0.06), 2))
    S['repair'] = seq(*[mix(noise(0.03, 0.2) * 0.8, osc(900 + 120 * i, 0.03, 'sq') * 0.5) * np.concatenate([np.ones(int(SR * 0.03)), np.zeros(int(SR * 0.03))])[:int(SR * 0.03)] for i in range(4)])
    d = 0.5; n = int(SR * d); S['lift'] = osc(80, d, 'sq', 0.3) * (0.6 + 0.4 * np.sin(np.linspace(0, 40, n))) * env(n, 0.02, r=0.1) * 0.8
    d = 0.5; n = int(SR * d); S['wall'] = mix(noise(d, 0.55) * expd(n, 5), osc(sweep(180, 50, d), d, 'saw') * expd(n, 5) * 0.6, noise(0.04) * 0.8)
    S['pickup'] = seq(*[osc(hz(x), 0.06, 'sq', 0.5) * env(int(SR * 0.06), 0.002, r=0.02) for x in ['E5', 'G5', 'B5', 'E6']])
    d = 0.4; n = int(SR * d); S['door'] = mix(osc(sweep(90, 55, d), d, 'tri') * expd(n, 3), osc(sweep(300, 180, d), d, 'saw') * 0.25 * np.sin(np.linspace(0, 3, n)))
    d = 0.09; n = int(SR * d); S['switch'] = osc(sweep(500, 1000, d), d, 'sq', 0.5) * env(n, 0.002, r=0.03)
    d = 0.2; n = int(SR * d); S['hook'] = mix(noise(0.12, 0.45) * np.sin(np.linspace(0, np.pi, int(SR * 0.12))), np.concatenate([np.zeros(int(SR * 0.1)), osc(2200, 0.1, 'sq', 0.25) * expd(int(SR * 0.1), 4)]))
    d = 0.5; n = int(SR * d); S['crash'] = mix(noise(d, 0.4) * expd(n, 6), osc(sweep(500, 60, d), d, 'sq') * expd(n, 5) * 0.7, noise(0.05) * 0.6)
    d = 0.7; n = int(SR * d); S['boss'] = mix(osc(sweep(120, 70, d), d, 'saw') * expd(n, 1.5), osc(sweep(123, 72, d), d, 'saw') * expd(n, 1.5), noise(d, 0.8) * 0.3) * env(n, 0.02, r=0.1)
    d = 0.30; n = int(SR * d); S['zap'] = mix(noise(d, 0.9) * expd(n, 3) * 0.7, osc(sweep(2400, 200, d), d, 'saw') * expd(n, 4) * 0.5)
    d = 0.35; n = int(SR * d); S['freeze'] = osc(sweep(1800, 2600, d), d, 'tri') * expd(n, 3) * 0.6 + noise(d, 0.3) * expd(n, 8) * 0.3
    d = 0.30; n = int(SR * d); S['splash'] = noise(d, 0.35) * np.sin(np.linspace(0, np.pi, n)) ** 0.5 * 0.8
    d = 0.20; n = int(SR * d); S['flame'] = noise(d, 0.55) * (0.7 + 0.3 * np.sin(np.linspace(0, 40, n))) * env(n, 0.01, r=0.05) * 0.7
    d = 0.25; n = int(SR * d); S['warn'] = osc(hz('A5'), d, 'sq', 0.5) * (np.sin(np.linspace(0, 60, n)) > 0) * env(n, 0.005, r=0.04) * 0.45
    d = 0.35; n = int(SR * d); S['spin'] = noise(d, 0.5) * (0.6 + 0.4 * np.sin(np.linspace(0, 60, n))) * np.sin(np.linspace(0, np.pi, n))
    S['win'] = seq(*[osc(hz(x), l, 'sq', 0.25) * env(int(SR * l), 0.004, r=0.03) for x, l in [('C5', .12), ('C5', .12), ('C5', .12), ('E5', .3), ('D5', .12), ('E5', .12), ('G5', .6)]])
    d = 0.05; n = int(SR * d); S['step'] = noise(d, 0.65) * expd(n, 6) * 0.5
    return S

def song(bpm, bars, chords, lead_pat, drums=True, lead_wave='sq'):
    """chords: список из (корень-басс, [ноты аккорда]) на такт; lead_pat: ноты мелодии по 8-м долям, '-' пауза"""
    step = 60 / bpm / 2                                  # 8-я
    total = int(SR * step * 8 * bars)
    out = np.zeros(total)
    def put(x, at):
        a = int(SR * at); b = min(total, a + len(x));
        if b > a: out[a:b] += x[:b - a]
    for b in range(bars):
        root, ch = chords[b % len(chords)]
        for k in range(8):
            t0 = (b * 8 + k) * step
            if k % 2 == 0: put(osc(hz(root), step * 1.6, 'tri') * env(int(SR * step * 1.6), 0.004, r=0.05) * 0.75, t0)
            nm = ch[k % len(ch)]
            put(osc(hz(nm), step * 0.9, 'sq', 0.125) * env(int(SR * step * 0.9), 0.004, r=0.04) * 0.18, t0)
            ln = lead_pat[(b * 8 + k) % len(lead_pat)]
            if ln != '-': put(osc(hz(ln), step * 0.95, lead_wave, 0.25) * env(int(SR * step * 0.95), 0.006, r=0.06) * 0.30, t0)
            if drums:
                if k % 4 == 0: put(osc(sweep(150, 45, 0.1), 0.1, 'tri') * expd(int(SR * 0.1), 4) * 0.8, t0)
                if k % 4 == 2: put(noise(0.09, 0.2) * expd(int(SR * 0.09), 6) * 0.35, t0)
                if k % 2 == 1: put(noise(0.03, 0.0) * expd(int(SR * 0.03), 5) * 0.12, t0)
    return out

def pitch(x, f):
    """сдвиг высоты ресемплингом (f>1 выше и короче) - из одной заготовки получаются разные герои"""
    n = max(8, int(len(x) / f))
    return np.interp(np.linspace(0, len(x) - 1, n), np.arange(len(x)), x)

# (имя персонажа: (высота, как звучит дальняя атака))
ACTORS = {'pengi': (1.30, 'shot'), 'cyberbum': (0.85, 'throw'), 'aunty': (1.00, 'throw'), 'domina': (1.50, 'spin'),
          'rocketpunk': (1.10, 'rocket'), 'megadog': (1.20, 'rocket'), 'capsulo': (0.90, 'boss'), 'dumbpunk': (0.80, 'throw'), 'ramrod': (0.60, 'hit'), 'professor': (0.70, 'boss'),
          'deliver': (1.25, 'throw'), 'firewalker': (0.55, 'rocket'), 'monoman': (0.95, 'shot'), 'ironfem': (1.35, 'hit'),
          'maestro': (1.05, 'boss'), 'bigpussy': (0.50, 'hit'), 'coach': (1.40, 'super'), 'hacker': (1.20, 'shot'),
          'watcher': (1.60, 'shot'), 'boot': (0.45, 'hit'), 'superdrom': (1.15, 'rocket'), 'drunky': (0.75, 'throw'), 'nerd': (0.90, 'shot'), 'refreezer': (0.65, 'hit')}

def main(root):
    sfx = os.path.join(root, 'assets', 'sfx'); bgm = os.path.join(root, 'assets', 'bgm')
    os.makedirs(sfx, exist_ok=True); os.makedirs(bgm, exist_ok=True)
    S = build_sfx()
    acts = {'step': 'step', 'jump': 'jump', 'land': 'land', 'melee': 'swing', 'ranged': 'shot', 'super': 'super', 'hurt': 'hurt', 'death': 'death'}
    def put(name, x): save_wav(os.path.join(sfx, name + '.wav'), snes(x))
    for a, src in acts.items(): put('generic_' + a, S[src])            # запасные: если у героя нет своего файла
    for c, src in {'pickup': 'pickup', 'door': 'door', 'panel': 'button', 'ui': 'switch', 'explode': 'explode', 'enemy_hit': 'hit', 'zap': 'zap', 'freeze': 'freeze', 'splash': 'splash', 'flame': 'flame', 'warn': 'warn'}.items(): put(c, S[src])
    for who, (f, rng_) in ACTORS.items():
        for a, src in acts.items():
            base = S[rng_] if a == 'ranged' else S[src]
            put('%s_%s' % (who, a), pitch(base, f))
    # музыка: уровень 1, босс, победа
    ch1 = [('A2', ['A3', 'C4', 'E4']), ('F2', ['F3', 'A3', 'C4']), ('C3', ['C4', 'E4', 'G4']), ('G2', ['G3', 'B3', 'D4'])]
    mel1 = ['E5', '-', 'A5', '-', 'G5', 'E5', '-', 'C5', 'F5', '-', 'A5', '-', 'G5', 'F5', 'E5', '-',
            'E5', '-', 'G5', '-', 'C6', 'B5', '-', 'G5', 'D5', '-', 'G5', 'B5', 'A5', '-', '-', '-']
    tracks = {'level1': song(104, 16, ch1, mel1),
              'boss': song(150, 16, [('E2', ['E3', 'G3', 'B3']), ('E2', ['E3', 'G3', 'B3']), ('C3', ['C4', 'E4', 'G4']), ('D3', ['D4', 'F#4', 'A4'])],
                           ['E5', 'E5', 'G5', 'E5', 'B5', '-', 'A5', 'G5', 'E5', 'E5', 'G5', 'E5', 'D6', '-', 'B5', 'A5'], True, 'saw'),
              'win': song(120, 4, [('C3', ['C4', 'E4', 'G4']), ('F3', ['F4', 'A4', 'C5']), ('G3', ['G4', 'B4', 'D5']), ('C3', ['C4', 'E4', 'G4'])],
                          ['C5', 'E5', 'G5', 'C6', 'G5', 'E5', 'G5', 'C6'], False)}
    have_ffmpeg = shutil.which('ffmpeg') is not None
    for name, x in tracks.items():
        if os.path.exists(os.path.join(bgm, name + '.mp3')) and '--force' not in sys.argv:
            print('пропускаю (уже есть ваш файл):', name + '.mp3'); continue
        x = snes(x, 10, 0.7)
        wp = os.path.join(bgm, name + '.wav'); save_wav(wp, x)
        if have_ffmpeg:
            r = subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', '-i', wp, '-codec:a', 'libmp3lame', '-b:a', '64k', os.path.join(bgm, name + '.mp3')])
            if r.returncode == 0: os.remove(wp)
    print('готово: звуки в', sfx, ', музыка в', bgm)

if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].startswith('--') else '.')
