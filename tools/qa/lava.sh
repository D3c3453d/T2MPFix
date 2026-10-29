#!/usr/bin/env bash
# damage_fix check: the host alone on Soulforge (mission 15) is placed in the lava trough next to the start.
#   lava.sh <damage_fix 0|1>
# Only damage_fix differs from stock (send_fix/recv_fix off, max_players=0), so this checks that fix on its own.
# Precondition for both: telemetry puts the player in the trough at the end (else FAIL as a setup error).
# PASS with 1: T2MPFix.log shows local-only damage applied (and the screenshot shows the player dying).
# PASS with 0: the fix is logged off, no damage is applied (stock: lava does nothing in multiplayer).
# Temporarily sets starting_mission 15 in the host lane's USER.CFG and restores it afterwards.
set -uo pipefail
test=${T2_TEST_DIR:-/mnt/d/thief_coop_test}
cd "$(dirname "$0")"
python3 -I lanes.py check_unlocked || exit 1
dmg=${1:?usage: lava.sh <damage_fix 0|1>}
H=$test/host
python3 -I lanes.py kill
cp "$H/USER.CFG" "$H/USER.CFG.lava_bak"
trap 'python3 -I lanes.py kill; mv -f "$H/USER.CFG.lava_bak" "$H/USER.CFG"' EXIT
sed -i -E 's/^;?starting_mission ([0-9]+)/;starting_mission \1/; s/^;(starting_mission 15\r?)$/\1/' "$H/USER.CFG"
in_lava() {  # the player's last true position (telemetry L line) is down in the trough, around (80, 29, -4.8)
  python3 -I - "$H/T2MPFix_tel.csv" <<'PY'
import sys
last = [l.split(',') for l in open(sys.argv[1]) if l.startswith('L,')][-1:]
x, y, z = map(float, last[0][4:7]) if last else (0, 0, 99)
inside = abs(x - 80) < 4 and abs(y - 29) < 4 and z < -3
print(f'player at {x:.1f} {y:.1f} {z:.1f}: {"in the lava" if inside else "NOT in the lava"}')
sys.exit(0 if inside else 1)
PY
}
DAMAGE=$dmg MAXP=0 python3 -I lanes.py setup stock host:idle >/dev/null
python3 -I lanes.py launch host >/dev/null
python3 -I lanes.py unfocus >/dev/null
# multiplayer, next x3, second option, start game, play mission
python3 -I lanes.py qa host "click 312 233; wait 1500; click 457 428; wait 1500; click 457 428; wait 2500; click 457 428; wait 2000; click 310 114; wait 2000; click 329 286; wait 3000; click 533 457; wait 3000; status" >/dev/null
for _ in $(seq 1 30); do
  s=$(python3 -I lanes.py qa host "status" | awk '/^sim/ {print $2}')
  [ "${s:-0}" -gt 3000 ] 2>/dev/null && break
  sleep 2
done
python3 -I lanes.py qa host "place 80 29 -4; wait 5000; status" >/dev/null   # into the trough next to the start
python3 -I lanes.py shot host "lava_damage_fix$dmg"
in_lava || { echo "FAIL lava damage_fix=$dmg: the player is not in the lava (test setup, not the fix)"; exit 1; }
grep -E "damage_fix|active:" "$H/T2MPFix.log" | cut -c14-
n=$(grep -c "local-only damage to obj" "$H/T2MPFix.log")
if [ "$dmg" = 1 ]; then ok=$([ "$n" -gt 0 ] && echo 1 || echo 0); else ok=$([ "$n" -eq 0 ] && grep -q "damage_fix: off" "$H/T2MPFix.log" && echo 1 || echo 0); fi
[ "$ok" = 1 ] && echo "PASS lava damage_fix=$dmg (applied $n)" || { echo "FAIL lava damage_fix=$dmg (applied $n)"; exit 1; }
