"""python3 -I tools/qa/test_analyze.py  -> ALL OK. Synthetic traces with a known answer."""
import math, os, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from analyze import analyze

def path(t):  # walk 10 u/s along x for 3 s, turn, walk along y; z constant
    s = t / 1000.0
    return (10 * s, 0.0, 5.0) if s < 3 else (30.0, 10 * (s - 3), 5.0)

def write(lines):
    f = tempfile.NamedTemporaryFile('w', suffix='.csv', delete=False)
    f.write('\n'.join(lines) + '\n'); f.close(); return f.name

def sender():
    return write([f"L,{t:.3f},0,1,{p[0]:.3f},{p[1]:.3f},{p[2]:.3f}" for t in range(0, 8000, 16) for p in [path(t)]])

def receiver(show):
    out = []
    for t in range(0, 8000, 16):
        p = show(t)
        out.append(f"R,{t:.3f},0,2,00000003,{p[0]},{p[1]},{p[2]},{p[0]},{p[1]},{p[2]},0,0,0,1,0,100")
    return write(out)

fails = 0
def check(c, msg):
    global fails
    if not c: fails += 1; print('FAIL', msg)

s = sender()
# 1. delayed by 150 ms and lifted by a constant 2 units (AvatarZOffset): on the path
good = analyze(s, receiver(lambda t: tuple(a + b for a, b in zip(path(max(0, t - 150)), (0, 0, 2)))))
check(good['path_dev_max'] < 0.05, f"delayed path flagged: {good}")
check(abs(good['offset'][2] - 2) < 0.05, f"offset not found: {good['offset']}")
check(1.0 < good['lag_err_p50'] < 2.0, f"lag error should be ~1.5: {good['lag_err_p50']}")
# 2. stuck on the corner for 1.5 s (cuts the corner off the path), then warps: off path and a jump
def stuck(t):
    if 3000 <= t < 4500: return (28.0, 2.0, 5.0)
    return path(max(0, t - 150))
bad = analyze(s, receiver(stuck))
check(bad['frames_off_path_gt1'] > 20, f"stuck avatar not flagged: {bad}")
check(bad['jumps_gt3'] >= 1, f"warp not counted: {bad}")
# 3. floating 4 units above the path for 1 s
def floating(t):
    p = path(max(0, t - 150))
    return (p[0], p[1], p[2] + (4 if 5000 <= t < 6000 else 0))
fl = analyze(s, receiver(floating))
check(fl['path_dev_max'] > 3.5, f"floating not flagged: {fl}")
# 4. a run where the sender stands still must be flagged
still = write([f"L,{t:.3f},0,1,1.0,2.0,3.0" for t in range(0, 8000, 16)])
st = analyze(still, receiver(lambda t: (1.0, 2.0, 3.0)))
check(st['warning'] is not None, f"standing-still run not flagged: {st}")
check(good['warning'] is None, f"moving run flagged: {good}")
print('ALL OK' if not fails else f'{fails} FAILED')
sys.exit(1 if fails else 0)
