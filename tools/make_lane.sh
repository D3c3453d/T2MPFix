#!/usr/bin/env bash
# Build a test lane: clean Steam copy + NewDark 1.29 + MP package, in $T2_TEST_DIR/<lane>.
# Usage: tools/make_lane.sh <lane-name> [player-name]
# Never touches the owner's Steam install; works from the workspace's local/vanilla (a copy of it) and downloads/nd129
# (new_dark.zip, contrib.zip, mp.zip from t2_v129.zip). Workspace = $T2_WORKSPACE, default two levels above the repo.
set -euo pipefail
lane="$1"; pname="${2:-$1}"
root=$(cd "$(dirname "$0")/.." && pwd)
ws=$(cd "${T2_WORKSPACE:-$root/../..}" && pwd)
test="${T2_TEST_DIR:-/mnt/d/thief_coop_test}"   # Windows folder for the test lanes, as a WSL path
dst="$test/$lane"
mkdir -p "$test"
win=$(wslpath -w "$test")\\$lane
nd="$ws/downloads/nd129"

[ -e "$dst" ] && { echo "lane exists: $dst" >&2; exit 1; }
mkdir -p "$dst"
cp -r "$ws/local/vanilla/." "$dst/"
python3 -I - "$nd" "$dst" <<'EOF'
import sys, zipfile
nd, dst = sys.argv[1], sys.argv[2]
for z in ("new_dark.zip", "contrib.zip", "mp.zip"):
    zipfile.ZipFile(f"{nd}/{z}").extractall(dst)
EOF
# NewDark resolves resources via darkinst.cfg; point it at this lane.
cat > "$dst/DARKINST.CFG" <<EOF
cd_path \\
install_path $win
language english
resname_base $win\\RES
load_path $win
script_module_path $win
movie_path $win\\MOVIES
EOF
# Windowed, small, skip intro: several instances on one desktop.
cat >> "$dst/cam_ext.cfg" <<EOF

; --- thief_coop test lane ---
skip_intro
force_windowed
EOF
sed -i "s/^;charname_default .*/charname_default $pname/; s/^;hostaddr_default .*/hostaddr_default 127.0.0.1/" "$dst/dark_net.cfg"
echo "lane ready: $win"
