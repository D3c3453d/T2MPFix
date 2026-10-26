"""Compare a player's true path (L lines on their machine) with how the other machine showed them
(R lines for the ghost of that player). Both machines run on one PC, so the QPC timestamps share a clock.

  python3 -I analyze.py <sender ghostfix_tel.csv> <receiver ghostfix_tel.csv> [--json out.json]

Metrics (units are Dark units, roughly 1 ft):
  path_dev   distance from the displayed avatar to the nearest point of the true path in the
             window [t - 1500 ms, t + 100 ms]. Lag alone does not count; passing through walls,
             floating, sinking and being stuck off the path does.
  lag_err    distance to where the player really was at the same moment (lag included).
  jumps      frames where the displayed avatar moved more than 3 units at once (visible warps).
A constant offset (the engine's AvatarZOffset between the player's physics origin and the avatar
model) is estimated as the median displacement and removed before measuring."""
import bisect, json, math, statistics, sys


def load(path):
    L, R, H, S, M = [], [], [], [], []
    with open(path, newline='') as f:
        for line in f:
            p = line.rstrip('\n').split(',')
            k = p[0]
            try:
                if k == 'L':
                    L.append((float(p[1]), float(p[4]), float(p[5]), float(p[6])))
                elif k == 'R':
                    R.append(dict(t=float(p[1]), obj=int(p[3]), flags=int(p[4], 16),
                                  before=tuple(map(float, p[5:8])), after=tuple(map(float, p[8:11])),
                                  target=tuple(map(float, p[11:14])), fixed=int(p[14]), mode=int(p[15]),
                                  fallback=int(p[17]) if len(p) > 17 else 0))
                elif k == 'H':
                    H.append((float(p[1]), int(p[4]), int(p[3])))
                elif k == 'S':
                    S.append((float(p[1]), int(p[4])))
                elif k == 'M':
                    M.append((float(p[1]), ','.join(p[3:])))
            except (ValueError, IndexError):
                pass  # a line cut off when the game was killed
    return L, R, H, S, M


def dist(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))


def truth_at(L, Lt, t):
    i = bisect.bisect_left(Lt, t)
    if i <= 0:
        return L[0][1:]
    if i >= len(L):
        return L[-1][1:]
    a, b = L[i - 1], L[i]
    u = (t - a[0]) / (b[0] - a[0]) if b[0] > a[0] else 0
    return tuple(a[k] + (b[k] - a[k]) * u for k in (1, 2, 3))


def seg_dist(p, a, b):
    ab = [b[k] - a[k] for k in range(3)]
    ap = [p[k] - a[k] for k in range(3)]
    den = sum(x * x for x in ab)
    u = 0 if den == 0 else max(0.0, min(1.0, sum(x * y for x, y in zip(ap, ab)) / den))
    return dist(p, [a[k] + ab[k] * u for k in range(3)])


def pct(xs, q):
    if not xs:
        return float('nan')
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(q * len(xs)))]


def analyze(sender, receiver, t_from=None, t_to=None):
    L, _, _, S, M = load(sender)
    _, R, H, _, _ = load(receiver)
    if not L or not R:
        return {'error': f'no data: {len(L)} L lines, {len(R)} R lines'}
    Lt = [x[0] for x in L]
    t0, t1 = max(L[0][0], R[0]['t']) + 2000, min(L[-1][0], R[-1]['t'])
    if t_from is not None: t0 = max(t0, t_from)
    if t_to is not None: t1 = min(t1, t_to)
    rows = [r for r in R if t0 <= r['t'] <= t1]
    if not rows:
        return {'error': 'no overlapping time range'}
    # several remote players: object ids differ per machine, so take the ghost whose track is
    # closest to the sender's true path (a 1.4-unit AvatarZOffset is small next to player spacing)
    objs = sorted({r['obj'] for r in rows})
    if len(objs) > 1:
        def score(o):
            sample = [r for r in rows if r['obj'] == o][::50]
            return statistics.median(dist(r['after'], truth_at(L, Lt, r['t'])) for r in sample)
        best_obj = min(objs, key=score)
        rows = [r for r in rows if r['obj'] == best_obj]
    H = [h for h in H if not objs or len(objs) == 1 or h[2] == rows[0]['obj']]
    # constant offset: median of displayed - true, using the best lag per frame (path matching)
    offs = []
    for r in rows[:: max(1, len(rows) // 400)]:
        best = min((truth_at(L, Lt, r['t'] - lag) for lag in range(0, 1500, 25)), key=lambda q: dist(q, r['after']))
        offs.append([r['after'][k] - best[k] for k in range(3)])
    off = [statistics.median(o[k] for o in offs) for k in range(3)]

    path_dev, lag_err, jumps = [], [], 0
    prev = None
    for r in rows:
        shown = tuple(r['after'][k] - off[k] for k in range(3))
        i0 = bisect.bisect_left(Lt, r['t'] - 1500)
        i1 = bisect.bisect_right(Lt, r['t'] + 100)
        seg = L[max(0, i0 - 1):min(len(L), i1 + 1)]
        d = min((seg_dist(shown, a[1:], b[1:]) for a, b in zip(seg, seg[1:])), default=float('nan'))
        path_dev.append(d)
        lag_err.append(dist(shown, truth_at(L, Lt, r['t'])))
        if prev is not None and dist(prev, r['after']) > 3.0:
            jumps += 1
        prev = r['after']
    dur = (t1 - t0) / 1000
    Lw = [x for x in L if t0 <= x[0] <= t1]
    travelled = sum(dist(a[1:], b[1:]) for a, b in zip(Lw, Lw[1:]))
    sends = [s for s in S if t0 <= s[0] <= t1]
    hbs = [h for h in H if t0 <= h[0] <= t1]
    return {
        'seconds': round(dur, 1), 'frames': len(rows), 'offset': [round(x, 2) for x in off],
        'sender_travelled': round(travelled, 1),
        'warning': 'sender barely moved: this run proves nothing' if travelled < dur else None,
        'path_dev_p50': round(pct(path_dev, 0.5), 3), 'path_dev_p95': round(pct(path_dev, 0.95), 3),
        'path_dev_max': round(max(path_dev), 3),
        'frames_off_path_gt1': sum(d > 1 for d in path_dev), 'frames_off_path_gt3': sum(d > 3 for d in path_dev),
        'lag_err_p50': round(pct(lag_err, 0.5), 3), 'lag_err_p95': round(pct(lag_err, 0.95), 3),
        'lag_err_max': round(max(lag_err), 3), 'jumps_gt3': jumps,
        'sends_per_s': round(len(sends) / dur, 1) if dur else 0, 'heartbeats_per_s': round(len(hbs) / dur, 1) if dur else 0,
        'fixed_frames': sum(r['fixed'] for r in rows), 'fallback_frames': sum(r['fallback'] for r in rows),
    }


if __name__ == '__main__':
    out = analyze(sys.argv[1], sys.argv[2])
    print(json.dumps(out, indent=1))
    if '--json' in sys.argv:
        json.dump(out, open(sys.argv[sys.argv.index('--json') + 1], 'w'), indent=1)
