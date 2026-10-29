#!/usr/bin/env bash
# max_players check: N instances (host + client1..N-1) play mission 1 together; extra lanes try to join and must be
# refused without dropping anyone.
#   [MAXP=M] players.sh <N> [extra lane...]        e.g. players.sh 8 client8; MAXP=8 players.sh 4 (default M = N)
# Only max_players differs from stock (max_players=N, every other fix off), so this checks that fix on its own.
# PASS: in each of the N lanes' telemetry, every 1-s window of the last 60 s shows exactly N-1 remote players; the
# host log shows the cap set to N+1; every extra lane stays out of the mission (screenshot in shots/players<N>_<lane>).
# Needs lanes host, client1..client(N-1) and the extras (tools/make_lane.sh, about 1 GB each) and N+extras instances'
# worth of RAM.
set -uo pipefail
test=${T2_TEST_DIR:-/mnt/d/thief_coop_test}
cd "$(dirname "$0")"
python3 -I lanes.py check_unlocked || exit 1
n=${1:?usage: players.sh <N> [extra lane...]}; shift
extra=("$@")
maxp=${MAXP:-$n}
lanes=(host); for i in $(seq 1 $((n - 1))); do lanes+=("client$i"); done
python3 -I lanes.py kill
trap 'python3 -I lanes.py kill' EXIT
setup=(); for l in "${lanes[@]}" "${extra[@]}"; do setup+=("$l:idle"); done
free_mb() { powershell.exe -NoProfile -Command '[int]((Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory/1KB)' | tr -d '\r'; }
started=0
for try in 1 2; do   # injected menu clicks are occasionally dropped (more often when RAM is short); start over once
  python3 -I lanes.py kill
  MAXP=$maxp DAMAGE=0 python3 -I lanes.py setup stock "${setup[@]}" >/dev/null
  python3 -I lanes.py launch "${lanes[@]}" "${extra[@]}" >/dev/null
  mb=$(free_mb)
  echo "free RAM with $((n + ${#extra[@]})) instances: $mb MB"
  python3 -I lanes.py host_lobby host >/dev/null
  python3 -I lanes.py join "${lanes[@]:1}" >/dev/null
  for l in "${extra[@]}"; do
    python3 -I lanes.py join "$l" >/dev/null
    python3 -I lanes.py shot "$l" "players${n}_$l"
  done
  python3 -I lanes.py shot host "players${n}_lobby"
  sleep 2
  python3 -I lanes.py start host >/dev/null
  if python3 -I lanes.py wait_ingame "${lanes[@]}" 240; then started=1; break; fi
  python3 -I lanes.py shot host "players${n}_fail$try"
done
[ $started = 1 ] || { echo "FAIL players=$n: not all in game (free RAM after launch: $mb MB; each instance needs ~480 MB)"; exit 1; }
sleep 65
echo "free RAM in mission: $(free_mb) MB"
python3 -I lanes.py shot host "players${n}_mission"
for l in "${extra[@]}"; do echo "$l: $(python3 -I lanes.py qa "$l" status | grep local_player)"; done
python3 -I - "$test" "$n" "$maxp" "${lanes[@]}" -- "${extra[@]}" <<'PY'
import os, re, sys
sys.path.insert(0, os.getcwd())
from analyze import load
test, n, maxp, rest = sys.argv[1], int(sys.argv[2]), min(int(sys.argv[3]), 8), sys.argv[4:]
lanes, extra = rest[:rest.index('--')], rest[rest.index('--') + 1:]
ok = True
for lane in lanes:
    L, R, *_ = load(f'{test}/{lane}/T2MPFix_tel.csv')
    if not L:
        print(f'{lane}: no local player telemetry'); ok = False; continue
    end = L[-1][0]
    counts = [len({r['obj'] for r in R if end - 60000 + i * 1000 <= r['t'] < end - 60000 + (i + 1) * 1000}) for i in range(60)]
    bad = [(i, c) for i, c in enumerate(counts) if c != n - 1]
    print(f'{lane}: remote players per second min {min(counts)} max {max(counts)}, {len(bad)}/60 windows != {n - 1}'
          + (f' (first {bad[:5]})' if bad else ''))
    ok &= not bad
log = open(f'{test}/host/T2MPFix.log').read()
m = re.search(r'max_players: (\d+) players \(cap field (\d+) -> (\d+)\)', log)
print('host:', m.group(0) if m else 'no max_players line')
ok &= bool(m) and int(m.group(1)) == maxp and int(m.group(3)) == maxp + 1
for lane in extra:
    st = open(f'{test}/{lane}/qa_status.txt').read()
    inside = 'local_player 0' not in st
    print(f'{lane}: {"GOT IN" if inside else "kept out"}')
    ok &= not inside
print(('PASS' if ok else 'FAIL') + f' players={n} max_players={maxp} extra={",".join(extra) or "-"}')
sys.exit(0 if ok else 1)
PY
