"""1D tracker for the narrow-strip captures: correlate the row profile of the
strip against the settled frame with a vertical offset dy."""
import sys, re, glob, os
import numpy as np
from PIL import Image

Y0 = 96
CARD_TOP = 520
CARD_BOT = 1252
MIN_ROWS = 260

def prof(path):
    a = np.asarray(Image.open(path).convert('L'), dtype=np.float32)
    return a.mean(axis=1)

def fit(p, ref, dy_max=1300):
    best = None
    for dy in range(0, dy_max + 1):
        r0 = max(CARD_TOP, Y0 + dy)
        r1 = CARD_BOT
        if r1 - r0 < MIN_ROWS:
            break
        f = p[r0 - dy - Y0:r1 - dy - Y0]
        s = ref[r0 - Y0:r1 - Y0]
        f0 = f - f.mean(); s0 = s - s.mean()
        den = float(np.sqrt((f0 * f0).sum() * (s0 * s0).sum()))
        if den <= 1e-6:
            continue
        r = float((f0 * s0).sum()) / den
        if best is None or r > best[0]:
            best = (r, dy)
    return best

def load_times(path):
    times, vis = {}, None
    for line in open(path, encoding='utf-8-sig'):
        line = line.strip()
        m = re.match(r'F (\d+) t=([\d.]+)', line)
        if m:
            times[int(m.group(1))] = float(m.group(2))
        if line.startswith('VIS '):
            vis = float(re.search(r't=([\d.]+)', line).group(1))
    return times, vis

prefix, tpath = sys.argv[1], sys.argv[2]
ref_index = int(sys.argv[3]) if len(sys.argv) > 3 else None
files = sorted(glob.glob(prefix + '-*.bmp'))
times, vis = load_times(tpath)
ref = prof(files[-1] if ref_index is None else files[ref_index])
print(f'{prefix}: vis={vis}')
print(f'{"idx":>3} {"t":>7} {"dt":>7} {"dy":>5} {"r":>6}')
for i, f in enumerate(files):
    p = prof(f)
    got = fit(p, ref)
    t = times.get(i)
    if got is None:
        print(f'{i:3d} {t:7.2f}   --')
        continue
    r, dy = got
    print(f'{i:3d} {t:7.2f} {(t-vis) if t and vis else 0:7.2f} {dy:5d} {r:6.3f}')
