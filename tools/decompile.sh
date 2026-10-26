#!/usr/bin/env bash
# Decompile Thief2MP.exe with Ghidra 12.2 headless in a container into re/t2mp129/
# (decomp.c, functions.tsv, strings.tsv). Takes ~20 min. Output is derived from the game exe: keep it local.
#   tools/decompile.sh <path to Thief2MP.exe from t2_v129.zip>
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
exe=$1
mkdir -p "$root/re/t2mp129" "$root/re/proj"
cp "$exe" "$root/re/Thief2MP_129.exe"
docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp \
  -v "$root/tools_build/ghidra/ghidra_12.2_PUBLIC:/ghidra:ro" -v "$root/re:/re" -v "$root/tools/ghidra:/scripts:ro" \
  eclipse-temurin:25-jdk /ghidra/support/analyzeHeadless /re/proj t2mp129 -import /re/Thief2MP_129.exe -overwrite \
  -scriptPath /scripts -postScript DecompileAll.java /re/t2mp129 -max-cpu 8 > "$root/re/ghidra_t2mp129.log" 2>&1
tail -3 "$root/re/ghidra_t2mp129.log"
