#!/usr/bin/env bash
# A/B test: one moving player (bot), one observer; same session flow for every mode.
#   run_ab.sh <seconds> <mode>...       modes: stock fixed send_only recv_only
# Results: tools/qa/results/<stamp>_<mode>/{host,client1}_tel.csv and summary.json
set -uo pipefail
test=${T2_TEST_DIR:-/mnt/d/thief_coop_test}
cd "$(dirname "$0")"
secs=$1; shift
mover=${MOVER:-client1}   # which lane runs the walker bot
stamp=$(date +%Y%m%d_%H%M%S)
for mode in "$@"; do
  out=results/${stamp}_${mode}_${mover}${NETSIM:+_net${NETSIM//,/-}}
  mkdir -p "$out"
  python3 -I lanes.py kill
  if [ "$mover" = host ]; then bots="host:walker client1:idle"; else bots="host:idle client1:walker"; fi
  python3 -I lanes.py setup "$mode" $bots >/dev/null
  python3 -I lanes.py launch host client1 >/dev/null
  python3 -I lanes.py start_session host client1 >/dev/null
  if ! python3 -I lanes.py wait_ingame host client1 60; then echo "$mode: session did not start"; continue; fi
  python3 -I - "$secs" <<'EOF'
import sys, time
time.sleep(float(sys.argv[1]))
EOF
  python3 -I lanes.py shot host "${stamp}_${mode}_host" >/dev/null
  python3 -I lanes.py kill
  for l in host client1; do
    cp $test/$l/T2MPFix_tel.csv "$out/${l}_tel.csv"
    cp $test/$l/T2MPFix.log "$out/${l}_T2MPFix.log"
    [ -f $test/$l/crash.dmp ] && echo "$mode: $l CRASHED" && cp $test/$l/crash.dmp "$out/${l}_crash.dmp"
  done
  if [ "$mover" = host ]; then snd=host; rcv=client1; else snd=client1; rcv=host; fi
  echo "== $mode: $rcv's view of $snd (moving)"
  python3 -I analyze.py "$out/${snd}_tel.csv" "$out/${rcv}_tel.csv" --json "$out/summary.json"
done
