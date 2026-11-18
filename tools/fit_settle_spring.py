"""Fit a model to the captured native Quick Settings motion.

What this is: an area comparison of a model against the samples in
artifacts/native-flyout-open.csv (or any CSV in that format). It fits a model to
captured data. It does not validate the shipping build's animation, and it does
not measure the mod: the numbers below describe how well a model reproduces the
native flyout curve, nothing more.

Two models, and the report names the one that produced each number:

  shipped (default)
      The settle this mod runs today, implemented exactly: the duration is the
      panel travel at 2.2 DIP/ms clamped to 160..520 ms; omega is picked so five
      time constants of the spring's slowest root land on that duration; every
      sample is the curve divided by its own value at T (endValue); the frame
      list is clamped to 0..1 and to the previous sample. The damping is the
      shipped Windows 11 value by default; --damping evaluates another and --fit
      searches for the area-optimal one. y0 = 0 everywhere: a hold release has no
      finger speed, while the native curve starts with the shell's own velocity.

  legacy (--model legacy)
      The fit this file used to report. A free period and damping ratio, with
      the settle duration read as 5/(zeta*omega). That is the wrong time
      constant for an overdamped spring, which the shipping style is, so the
      duration it printed was not the one the mod uses. Kept only for the
      historical comparison.

Residual: the model's frame list is interpolated on a 1 ms grid between the
first and the last sample, so `area` is ms of displacement (the metric the
README quotes) and `mean` is its average. The native curve's first 30 ms are
faster than any y0 = 0 spring at this duration, so that part is not what the fit
identifies; the report prints the residual after 90 ms next to the overall one.

The model's t = 0 is the CSV's t = 0, the window becoming visible. The start is
not free: moving it only lets the fit hide the early mismatch.

This file needs only the standard library:

    python tools/fit_settle_spring.py
    python tools/fit_settle_spring.py --damping 2.0
    python tools/fit_settle_spring.py --fit
    python tools/fit_settle_spring.py --model legacy

A close capture (artifacts/native-flyout-close.csv) counts its t from the window
hiding, so its samples are negative; it needs --time-offset with that capture's
key-down time, printed as KEY in its .txt. Without it the tool refuses rather
than fit a curve to samples the model never reaches.

[PROTOCOL]: Update this header on change, then check AGENTS.md
"""
import argparse
import math
import os
import sys

# The shipping settle's constants, copied from mods/start-menu-legacy-swipe.wh.cpp.
WIN11_DAMPING = 1.72   # kWin11SpringDamping, the default AnimationStyle
SPEED_DIP_PER_MS = 2.2
MIN_MS = 160
MAX_MS = 520
TIME_CONSTANTS = 5.0
SEGMENT_MS = 4.0
MIN_STEPS = 12
MAX_STEPS = 128
# The native flyout's own travel, the number in the CSV header and the one the
# mod's duration formula would use for this panel.
TRAVEL_DIP = 578.0
TRAVEL_PX = 1156.0
# The left taskbar's Start travel, used by the initial-slope comparison only.
DISTANCE_DIP = 1440.0


def shipped_duration(travel_dip):
    """T = clamp(remaining / 2.2 DIP/ms, 160, 520) ms, as the mod rounds it."""
    ms = math.floor(travel_dip / SPEED_DIP_PER_MS + 0.5)
    return min(max(ms, MIN_MS), MAX_MS)


def curve_at(t, zeta, omega, y0=0.0):
    """The mod's analytic spring, y(0) = 0, y(inf) = 1, y'(0) = y0, t in ms."""
    t = max(t, 0.0)
    decay = zeta * omega
    envelope = math.exp(-decay * t)
    if zeta > 1.0:
        wd = omega * math.sqrt(zeta * zeta - 1.0)
        return 1.0 - envelope * (
            math.cosh(wd * t) + ((decay - y0) / wd) * math.sinh(wd * t))
    if abs(zeta - 1.0) <= 1e-9:
        return 1.0 - envelope * (1.0 + (omega - y0) * t)
    wd = omega * math.sqrt(max(0.0, 1.0 - zeta * zeta))
    return 1.0 - envelope * (
        math.cos(wd * t) + ((decay - y0) / wd) * math.sin(wd * t))


def settle_frames(zeta, t_total, y0=0.0):
    """The mod's frame list: (times, values), start 0, target 1.

    Mirrors BeginGestureSettle: omega from the slowest root, endValue from the
    curve at T, each sample clamped to 0..1 and to the previous one, and the
    last frame the exact target.
    """
    slowest = zeta - math.sqrt(zeta * zeta - 1.0) if zeta > 1.0 else zeta
    omega = TIME_CONSTANTS / (slowest * t_total)
    end_value = min(max(curve_at(t_total, zeta, omega, y0), 0.5), 1.0)
    steps = min(max(int(math.ceil(t_total / SEGMENT_MS)), MIN_STEPS), MAX_STEPS)
    times = [0.0]
    values = [0.0]
    previous = 0.0
    for i in range(1, steps):
        t = t_total * i / steps
        y = min(max(curve_at(t, zeta, omega, y0) / end_value, 0.0), 1.0)
        previous = max(previous, y)
        times.append(t)
        values.append(previous)
    times.append(t_total)
    values.append(1.0)
    return times, values, omega, end_value, steps


def legacy_curve(t, zeta, period):
    """The historical model: a free period, no endValue normalisation."""
    t = max(t, 0.0)
    w = 2.0 * math.pi / period
    if zeta < 1.0:
        wd = w * math.sqrt(1.0 - zeta * zeta)
        v = 1.0 - math.exp(-zeta * w * t) * (
            math.cos(wd * t) + (zeta * w / wd) * math.sin(wd * t))
    elif abs(zeta - 1.0) < 1e-9:
        v = 1.0 - math.exp(-w * t) * (1.0 + w * t)
    else:
        wd = w * math.sqrt(zeta * zeta - 1.0)
        v = 1.0 - math.exp(-zeta * w * t) * (
            math.cosh(wd * t) + (zeta * w / wd) * math.sinh(wd * t))
    return min(max(v, 0.0), 1.0)


def load_csv(path, travel_px):
    """(t_ms, d) from the CSV; `d` is used when present, else dy/travel."""
    samples = []
    header = None
    with open(path, encoding='utf-8-sig') as handle:
        for line in handle:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            parts = [p.strip() for p in line.split(',')]
            if header is None:
                header = [p.lower() for p in parts]
                if not any(h in header for h in ('t_ms', 't')):
                    raise SystemExit('%s: no t_ms column' % path)
                continue
            row = dict(zip(header, parts))
            t = float(row.get('t_ms', row.get('t')))
            if 'd' in row and row['d'] != '':
                d = float(row['d'])
            elif 'dy_px' in row:
                d = 1.0 - float(row['dy_px']) / travel_px
            else:
                raise SystemExit('%s: no d or dy_px column' % path)
            samples.append((t, d))
    if len(samples) < 4:
        raise SystemExit('%s: too few samples' % path)
    samples.sort()
    return samples


def grid_of(samples, step=1.0):
    """d(t) on a 1 ms grid, linear between the samples."""
    t0 = samples[0][0]
    t1 = samples[-1][0]
    count = int(round((t1 - t0) / step)) + 1
    grid = [t0 + i * step for i in range(count)]
    values = []
    k = 0
    for g in grid:
        while k + 1 < len(samples) and samples[k + 1][0] <= g:
            k += 1
        if k + 1 >= len(samples):
            values.append(samples[-1][1])
        else:
            (ta, da), (tb, db) = samples[k], samples[k + 1]
            span = tb - ta
            values.append(da if span <= 0 else da + (db - da) * (g - ta) / span)
    return grid, values


def model_value(times, values, t):
    """The shipped frame list, interpolated the way the animation is."""
    if t <= times[0]:
        return values[0]
    if t >= times[-1]:
        return values[-1]
    lo, hi = 0, len(times) - 1
    while hi - lo > 1:
        mid = (lo + hi) // 2
        if times[mid] <= t:
            lo = mid
        else:
            hi = mid
    span = times[hi] - times[lo]
    if span <= 0:
        return values[hi]
    return values[lo] + (values[hi] - values[lo]) * ((t - times[lo]) / span)


def area_between(times, values, grid, measured):
    total = 0.0
    j = 0
    n = len(times)
    for k, g in enumerate(grid):
        if g <= times[0]:
            y = values[0]
        elif g >= times[-1]:
            y = values[-1]
        else:
            while j + 1 < n and times[j + 1] <= g:
                j += 1
            span = times[j + 1] - times[j]
            y = values[j] if span <= 0 else values[j] + (
                values[j + 1] - values[j]) * ((g - times[j]) / span)
        total += abs(y - measured[k])
    return total


def residual_after(samples, times, values, since_ms):
    late = [(t, d) for t, d in samples if t >= since_ms]
    if not late:
        return 0.0, 0.0
    errors = [abs(model_value(times, values, t) - d) for t, d in late]
    return sum(errors) / len(errors), max(errors)


def search_damping(t_total, grid, measured, lo=0.60, hi=6.00):
    """Coarse then fine area search for the shipped model's damping."""
    def area_at(zeta):
        times, values, _, _, _ = settle_frames(zeta, t_total)
        return area_between(times, values, grid, measured)

    best = None
    zeta = lo
    while zeta <= hi + 1e-9:
        area = area_at(zeta)
        if best is None or area < best[0]:
            best = (area, zeta)
        zeta = round(zeta + 0.05, 4)
    coarse = best[1]
    for i in range(-10, 11):
        zeta = round(coarse + 0.005 * i, 4)
        if zeta < lo or zeta > hi:
            continue
        area = area_at(zeta)
        if area < best[0]:
            best = (area, zeta)
    return best, lo, hi


def print_samples(path, samples, travel_dip):
    print('\n== samples')
    print('   file      %s' % path)
    print('   samples   %d, t %.1f .. %.1f ms, travel %.0f DIP'
          % (len(samples), samples[0][0], samples[-1][0], travel_dip))
    print('   read from the CSV: this is the captured curve, not the mod')


def print_table(samples, model_at):
    print('   %7s %6s %6s %7s' % ('t', 'y', 'd', 'err'))
    for t, d in samples:
        y = model_at(t)
        print('   %7.1f %6.3f %6.3f %+7.3f' % (t, y, d, y - d))


def report_shipped(args, samples, grid, measured, travel_dip):
    t_total = shipped_duration(travel_dip)
    zeta = args.damping if args.damping is not None else WIN11_DAMPING
    times, values, omega, end_value, steps = settle_frames(zeta, t_total)
    area = area_between(times, values, grid, measured)
    rms = math.sqrt(sum(
        (model_value(times, values, g) - measured[k]) ** 2
        for k, g in enumerate(grid)) / len(grid))
    late_mean, late_max = residual_after(samples, times, values, 90.0)
    slowest = zeta - math.sqrt(zeta * zeta - 1.0) if zeta > 1.0 else zeta

    print('\n== shipped model (the settle this mod runs)')
    print('   duration  T = %d ms (%.0f DIP / %.1f DIP/ms, clamped %d..%d)'
          % (t_total, travel_dip, SPEED_DIP_PER_MS, MIN_MS, MAX_MS))
    print('   omega     %.5f 1/ms, slowest root %.4f, %g time constants in T'
          % (omega, slowest, TIME_CONSTANTS))
    print('   endValue  curve(T) = %.4f, the shipped normalisation' % end_value)
    print('   frames    %d samples at %.0f ms per segment' % (steps + 1, SEGMENT_MS))
    print('   damping   %.2f%s, y0 = 0'
          % (zeta, ' (kWin11SpringDamping)' if args.damping is None else ' (--damping)'))
    print('   residual  area %.1f ms, mean |y-d| %.4f, rms %.4f'
          % (area, area / len(grid), rms))
    print('   after 90 ms: mean |y-d| %.4f, max %.4f' % (late_mean, late_max))
    if samples[0][1] > 0.30:
        print('   note      the native curve is faster than any y0 = 0 spring in its')
        print('             first 30 ms (%.3f of the travel at %.1f ms); the decay'
              % (samples[0][1], samples[0][0]))
        print('             after 90 ms is what this curve identifies.')

    if args.fit:
        (best_area, best_zeta), lo, hi = search_damping(t_total, grid, measured)
        print('\n== damping search (--fit, area over the observed window)')
        print('   best zeta %.3f, area %.1f ms (shipped %.2f: %.1f ms)'
              % (best_zeta, best_area, zeta, area))
        if best_zeta <= lo + 0.051 or best_zeta >= hi - 0.051:
            print('   the optimum sits on the search bound %.2f..%.2f: the'
                  % (lo, hi))
            print('   objective is monotone here, so this curve does not')
            print('   identify the damping. The shipped value comes from the')
            print('   free-period fit, which --model legacy repeats.')
        else:
            print('   the optimum is inside the search range; compare it with the')
            print('   shipped value above before changing anything.')

    print_table(samples, lambda t: model_value(times, values, t))
    return zeta, area


def legacy_grid(samples):
    """The historical domain: 0..last sample on a 1 ms grid, d = 0 before the
    first visible frame. The old file sampled this way (and stopped at 306 ms),
    which is why it reached period 102 ms / zeta 1.72. The shipped comparison
    above does not assume anything before the first frame."""
    ts = [p[0] for p in samples]
    ds = [p[1] for p in samples]
    grid = [float(i) for i in range(int(ts[-1]) + 1)]
    measured = []
    k = 0
    for g in grid:
        if g < ts[0]:
            measured.append(0.0)
            continue
        while k + 1 < len(ts) and ts[k + 1] <= g:
            k += 1
        if k + 1 >= len(ts):
            measured.append(ds[-1])
        else:
            span = ts[k + 1] - ts[k]
            measured.append(ds[k] if span <= 0 else
                            ds[k] + (ds[k + 1] - ds[k]) * (g - ts[k]) / span)
    return grid, measured


def report_legacy(samples):
    grid, measured = legacy_grid(samples)
    best = None
    for period in range(80, 701, 10):
        for zi in range(20):
            zeta = 0.60 + 0.10 * zi
            area = sum(abs(legacy_curve(g, zeta, period) - measured[k])
                       for k, g in enumerate(grid))
            if best is None or area < best[0]:
                best = (area, float(period), zeta)
    _, period0, zeta0 = best
    for period in range(int(period0) - 10, int(period0) + 11):
        for zi in range(-10, 11):
            zeta = round(zeta0 + 0.01 * zi, 4)
            if zeta <= 0:
                continue
            area = sum(abs(legacy_curve(g, zeta, period) - measured[k])
                       for k, g in enumerate(grid))
            if area < best[0]:
                best = (area, float(period), zeta)
    area, period, zeta = best
    omega = 2.0 * math.pi / period
    duration = TIME_CONSTANTS / (zeta * omega)
    rms = math.sqrt(sum(
        (legacy_curve(g, zeta, period) - measured[k]) ** 2
        for k, g in enumerate(grid)) / len(grid))
    late = [abs(legacy_curve(t, zeta, period) - d) for t, d in samples if t >= 90]

    print('\n== legacy model (historical only; the mod does not run this)')
    print('   domain    0..%d ms on a 1 ms grid, d = 0 before the first visible'
          % int(grid[-1]))
    print('             frame: the old file\'s domain (it stopped at 306 ms and')
    print('             printed 102 ms / 1.72)')
    print('   period    %.0f ms, zeta %.2f, start pinned at the visibility flip'
          % (period, zeta))
    print('   duration  5/(zeta*omega) = %.0f ms (the old, wrong time constant)'
          % duration)
    print('   residual  area %.1f ms, mean |y-d| %.4f, rms %.4f'
          % (area, area / len(grid), rms))
    if late:
        print('   after 90 ms: mean |y-d| %.4f, max %.4f'
              % (sum(late) / len(late), max(late)))
    print_table(samples, lambda t: legacy_curve(t, zeta, period))
    return zeta, area


def report_initial_slope():
    """The half-open release, with the tangent and the model kept apart.

    The old line printed `velocity * 10 ms` as the displacement of the first
    10 ms. That is the initial tangent, not a position on the curve: the spring
    starts at the finger's speed and accelerates away from it at once. This
    report evaluates the generated frame list, which is what the animation
    actually receives, and prints the first sampled frame and the 10 ms position
    next to the tangent that used to be labelled as one of them.
    """
    velocity = 0.4
    remaining = 0.5 * DISTANCE_DIP
    t_total = shipped_duration(remaining)
    y0 = velocity / remaining
    times, values, _, end_value, steps = settle_frames(
        WIN11_DAMPING, t_total, y0)
    first_frame_ms = times[1]
    first_frame_dip = values[1] * remaining
    at_10_ms_dip = model_value(times, values, 10.0) * remaining

    print('\n== shipping settle, half-open release (analytic, not measured)')
    print('   Distance %.0f DIP, remaining %.0f DIP, finger %.1f DIP/ms'
          % (DISTANCE_DIP, remaining, velocity))
    print('   y0 = v/remaining = %.6f 1/ms (%.3f 1/s), T = %d ms'
          % (y0, y0 * 1000.0, t_total))
    print('   tangent   initial slope x 10 ms = %.1f DIP: a straight-line'
          % (velocity * 10.0))
    print('             extrapolation, not the model position at 10 ms')
    print('   frames    %d segments of %.2f ms, endValue %.4f'
          % (steps, t_total / steps, end_value))
    print('   model     first sampled frame %.2f ms -> %.1f DIP'
          % (first_frame_ms, first_frame_dip))
    print('             at 10 ms -> %.1f DIP (frame list, interpolated)'
          % at_10_ms_dip)
    old = 9.0 * remaining / 300.0
    print('   old Windows 11 bezier(0.1,0.9,0.2,1)/300 ms: 9x average = '
          '%.2f DIP/ms,' % old)
    print('             %.0f DIP as a tangent (the curve is slower after 0)'
          % (old * 10.0))


def main(argv):
    here = os.path.dirname(os.path.abspath(__file__))
    parser = argparse.ArgumentParser(
        description='Fit a model to the captured native flyout curve. '
                    'This is a fit to data, not a validation of the shipping build.')
    parser.add_argument('--model', choices=('shipped', 'legacy'), default='shipped',
                        help='which model to report (default: shipped)')
    parser.add_argument('--csv', default=os.path.join(
        here, '..', 'artifacts', 'native-flyout-open.csv'),
        help='samples to fit (default: artifacts/native-flyout-open.csv)')
    parser.add_argument('--damping', type=float, default=None,
                        help='shipped model: evaluate this damping (default %.2f)'
                             % WIN11_DAMPING)
    parser.add_argument('--fit', action='store_true',
                        help='shipped model: also search the area-optimal damping')
    parser.add_argument('--travel-dip', type=float, default=TRAVEL_DIP,
                        help='panel travel in DIP for the duration formula (default: 578)')
    parser.add_argument('--travel-px', type=float, default=TRAVEL_PX,
                        help='panel travel in px for files without a d column (default: 1156)')
    parser.add_argument('--time-offset', type=float, default=0.0,
                        help='ms added to every sample t, for a capture whose t=0 is '
                             'not the animation start (the close CSV needs its KEY time)')
    args = parser.parse_args(argv)
    if args.model == 'legacy' and (args.fit or args.damping is not None):
        parser.error('--damping and --fit are shipped-model options')

    path = os.path.abspath(args.csv)
    samples = load_csv(path, args.travel_px)
    if args.time_offset:
        samples = [(t + args.time_offset, d) for t, d in samples]
    if samples[0][0] < 0:
        parser.error('%s: the first sample is at t=%.1f ms. The model starts at '
                     't=0, so the samples need the animation start as their '
                     'origin; use --time-offset for a capture counted from '
                     'something else.' % (path, samples[0][0]))

    print('model: %s - a fit to captured data, not a validation of the shipping build'
          % args.model)
    print_samples(path, samples, args.travel_dip)
    if args.model == 'shipped':
        grid, measured = grid_of(samples)
        report_shipped(args, samples, grid, measured, args.travel_dip)
    else:
        report_legacy(samples)
    report_initial_slope()
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
