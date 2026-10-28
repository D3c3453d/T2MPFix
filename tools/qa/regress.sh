#!/usr/bin/env bash
# Full regression on the current build. Prints one summary line per scenario and PASS/FAIL against the bars below.
#   stock (control, the steer function not hooked): must show the bug    path_dev_max > 1.0
#   fixed (localhost, 60/30/2, 150/75/10):        path_dev_max < 0.5, no frames > 1 unit off path, no jumps
#   mixed (sender without T2MPFix; the observer's steer_fix):  path_dev_max < 4 (stock ~7.7)
#   send_only / recv_only (one half of the avatar fix on both machines): each half works on its own
#     send_only: sender at T2MPFix rate, receiver keeps stock steering    hb/s >= 10, fixed_frames == 0
#     recv_only: stock-rate sender is never interpolated                   hb/s < 3, fixed_frames == 0
#   steer_only: the stock sender's avatar is pulled toward the prediction   path_dev_max < 4, fixed_frames == 0
set -uo pipefail
test=${T2_TEST_DIR:-/mnt/d/thief_coop_test}
cd "$(dirname "$0")"
sha=$(sha256sum ../../mod/build/dinput.dll | cut -c1-16)
echo "build dinput.dll sha256 $sha"
fail=0
check() { # name json condition
  python3 -I - "$1" "$2" "$3" <<'EOF' || fail=1
import json, sys
name, path, cond = sys.argv[1:]
r = json.load(open(path))
ok = (r.get('warning') is None) and eval(cond, {}, r)
print(f"{'PASS' if ok else 'FAIL'} {name:28s} travelled {r['sender_travelled']:6.1f}  path_dev p95 {r['path_dev_p95']:.3f} max {r['path_dev_max']:.3f}"
      f"  off>1 {r['frames_off_path_gt1']:5d}  jumps {r['jumps_gt3']}  lag p50 {r['lag_err_p50']:.2f}  hb/s {r['heartbeats_per_s']}  fixed {r['fixed_frames']}/{r['frames']}")
sys.exit(0 if ok else 1)
EOF
}
latest() { ls -d results/*_"$1" | tail -1; }

./run_ab.sh 70 stock fixed >/dev/null 2>&1
check "stock localhost (control)" "$(latest stock_client1)/summary.json" "path_dev_max > 1.0"
check "fixed localhost" "$(latest fixed_client1)/summary.json" "path_dev_max < 0.5 and frames_off_path_gt1 == 0 and jumps_gt3 == 0"
NETSIM=60,30,2 ./run_ab.sh 70 fixed >/dev/null 2>&1
check "fixed 60+-30ms 2% loss" "$(latest fixed_client1_net60-30-2)/summary.json" "path_dev_max < 0.5 and frames_off_path_gt1 == 0 and jumps_gt3 == 0"
NETSIM=150,75,10,7 ./run_ab.sh 70 fixed >/dev/null 2>&1
check "fixed 150+-75ms 10% loss" "$(latest fixed_client1_net150-75-10-7)/summary.json" "path_dev_max < 0.5 and frames_off_path_gt1 == 0 and jumps_gt3 == 0"
./run_ab.sh 70 send_only recv_only steer_only >/dev/null 2>&1
check "send_fix only" "$(latest send_only_client1)/summary.json" "heartbeats_per_s >= 10 and fixed_frames == 0"
check "recv_fix only" "$(latest recv_only_client1)/summary.json" "heartbeats_per_s < 3 and fixed_frames == 0"
check "steer_fix only" "$(latest steer_only_client1)/summary.json" "path_dev_max < 4 and fixed_frames == 0"

# mixed: the moving player runs stock (no T2MPFix), the observer runs T2MPFix
python3 -I lanes.py kill
python3 -I lanes.py setup fixed host:idle client1:walker:stock >/dev/null
python3 -I lanes.py launch host client1 >/dev/null
python3 -I lanes.py start_session host client1 >/dev/null
python3 -I lanes.py wait_ingame host client1 60 >/dev/null || echo "mixed: session did not start"
python3 -I -c "import time; time.sleep(70)"
out=results/$(date +%Y%m%d_%H%M%S)_mixed; mkdir -p "$out"
for l in host client1; do cp $test/$l/T2MPFix_tel.csv "$out/${l}_tel.csv"; [ -f $test/$l/crash.dmp ] && echo "CRASH $l" && fail=1; done
python3 -I lanes.py kill
python3 -I analyze.py "$out/client1_tel.csv" "$out/host_tel.csv" --json "$out/summary.json" >/dev/null
check "mixed (mover without fix)" "$out/summary.json" "fixed_frames == 0 and path_dev_max < 4"
grep -l CRASH results/*/ 2>/dev/null
[ $fail = 0 ] && echo "ALL PASS" || echo "SOME FAILED"
