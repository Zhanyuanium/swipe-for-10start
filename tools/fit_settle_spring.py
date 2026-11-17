"""Fit the mod's spring (y0 = 0) to the measured Quick Settings motion.

Vectorised: the measured curve is resampled on a 1 ms grid, the spring is
evaluated on that grid for each parameter pair, and the area residual is the
sum of |y - d|.
"""
import math
import numpy as np

DY_HIDDEN = 1156.0
DIST = 1440.0

OPEN = [(29.71, 527), (37.49, 458), (45.73, 402), (55.91, 355), (72.08, 278),
        (89.82, 218), (104.69, 170), (120.43, 130), (129.17, 114),
        (137.60, 98), (145.28, 85), (153.90, 72), (162.47, 61), (171.40, 51),
        (179.08, 42), (187.16, 34), (195.41, 28), (204.07, 22), (212.14, 16),
        (220.12, 12), (228.81, 8), (236.96, 5), (245.20, 3), (253.03, 1),
        (261.64, 0)]

CLOSE = [(88.41, 17), (96.84, 29), (105.41, 44), (122.45, 84), (139.05, 140),
         (154.96, 216), (163.33, 263), (171.65, 319), (179.44, 384),
         (187.21, 463), (195.26, 559), (203.22, 681), (210.74, 853)]

def grid_of(data, sign, t_end):
    """d(t) on a 1 ms grid; linear between samples, 0 before the first."""
    ts = np.array([p[0] for p in data], dtype=float)
    ds = np.array([(1.0 - p[1] / DY_HIDDEN) if sign > 0 else (p[1] / DY_HIDDEN)
                   for p in data], dtype=float)
    g = np.arange(0.0, t_end + 1e-9, 1.0)
    d = np.zeros_like(g)
    ok = g >= ts[0]
    d[ok] = np.interp(g[ok], ts, ds)
    return g, d

def y_spring(t, period, zeta):
    """Vectorised spring step response, clamped to 0..1, t in ms."""
    t = np.maximum(t, 0.0)
    w = 2.0 * math.pi / period
    if zeta < 1.0:
        wd = w * math.sqrt(1.0 - zeta * zeta)
        v = 1.0 - np.exp(-zeta * w * t) * (
            np.cos(wd * t) + (zeta * w / wd) * np.sin(wd * t))
    elif abs(zeta - 1.0) < 1e-9:
        v = 1.0 - np.exp(-w * t) * (1.0 + w * t)
    else:
        wd = w * math.sqrt(zeta * zeta - 1.0)
        v = 1.0 - np.exp(-zeta * w * t) * (
            np.cosh(wd * t) + (zeta * w / wd) * np.sinh(wd * t))
    return np.clip(v, 0.0, 1.0)

def fit(g, d, start_lo, start_hi, step=1.0):
    best = None
    for period in np.arange(80, 701, 2.0):
        for zeta in np.arange(0.60, 2.51, 0.02):
            for start in np.arange(start_lo, start_hi + 1e-9, step):
                y = y_spring(g - start, period, zeta)
                a = np.abs(y - d).sum() * 1.0  # 1 ms bins
                if best is None or a < best[0]:
                    best = (a, period, zeta, start)
    return best

def report(name, data, sign, t_end, best):
    a, period, zeta, start = best
    g, d = grid_of(data, sign, t_end)
    y = y_spring(g - start, period, zeta)
    dur = 5.0 * period / (2 * math.pi * zeta)
    print(f'\n== {name}')
    print(f'   area = {a:.1f} ms of displacement; mean |y-d| = {a/len(g):.4f}; '
          f'rms = {np.sqrt(((y-d)**2).mean()):.4f}')
    print(f'   period = {period:.0f} ms, zeta = {zeta:.2f}, start = {start:+.0f} ms '
          f'relative to the measured t=0')
    print(f'   settle duration 5/(zeta*omega) = {dur:.0f} ms')
    print(f'   {"t":>7} {"y":>6} {"d":>6} {"err":>7}')
    for t, dy in data:
        dd = (1.0 - dy / DY_HIDDEN) if sign > 0 else (dy / DY_HIDDEN)
        yy = float(y_spring(np.array([t - start]), period, zeta)[0])
        print(f'   {t:7.1f} {yy:6.3f} {dd:6.3f} {yy - dd:+7.3f}')
    return period, zeta, start, a

g_open, d_open = grid_of(OPEN, +1, 306.0)
f1 = fit(g_open, d_open, 0, 0)
p1 = report('OPEN, y0=0, start pinned at the visibility flip (2 parameters)',
            OPEN, +1, 306.0, f1)

f2 = fit(g_open, d_open, -60, 30)
p2 = report('OPEN, y0=0, start free (3 parameters)', OPEN, +1, 306.0, f2)

g_close, d_close = grid_of(CLOSE, -1, 215.0)
f3 = fit(g_close, d_close, 60, 115)
p3 = report('CLOSE, y0=0, start free (3 parameters)', CLOSE, -1, 215.0, f3)

# What does the native open curve look like as a bezier? 2-parameter family
# with the classic Fluent shape ((x1,y1),(0.5,1)).
def bez_y(x1, y1, n=401):
    u = np.linspace(0, 1, n)
    x = 3 * u * (1 - u) ** 2 * x1 + 3 * u * u * (1 - u) * 0.5 + u ** 3
    y = 3 * u * (1 - u) ** 2 * y1 + 3 * u * u * (1 - u) * 1.0 + u ** 3
    return x, y

best = None
for T in np.arange(240, 341, 10.0):
    for x1 in np.arange(0.04, 0.61, 0.02):
        for y1 in np.arange(0.20, 1.61, 0.05):
            x, y = bez_y(x1, y1)
            model = np.interp(g_open / T, x, y)
            a = np.abs(model - d_open).sum()
            if best is None or a < best[0]:
                best = (a, T, x1, y1)
a, T, x1, y1 = best
print(f'\n== OPEN as cubic-bezier(({x1:.2f},{y1:.2f}),(0.5,1)) over {T:.0f} ms: '
      f'area {a:.0f} ms, mean {a/307:.3f}, initial slope {y1/x1:.2f}x average')

print('\n== initial slope at a half-open release (Distance 1440 DIP, finger 0.4 DIP/ms)')
print('   Win10 spring, and the fitted Win11 spring, y0 = v/remaining = '
      f'{0.4/(0.5*DIST)*1000:.3f} 1/s -> 0.40 DIP/ms, 4.0 DIP in the first 10 ms')
print(f'   old Win11 bezier(0.1,0.9,0.2,1)/300 ms -> 9 x average = '
      f'{9*(0.5*DIST)/300:.2f} DIP/ms, {9*(0.5*DIST)/300*10:.0f} DIP in the first 10 ms')
