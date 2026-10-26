#!/usr/bin/env bash
# Download the pinned build and RE tools into tools_build/ (checked by SHA-256, nothing is run).
#   tools/fetch_tools.sh            llvm-mingw only (enough to build the DLL)
#   tools/fetch_tools.sh ghidra     also Ghidra 12.2 (only for tools/decompile.sh)
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
dst="$root/tools_build"
mkdir -p "$dst"

fetch() { # url sha256 file
  local f="$dst/$3"
  if [ ! -f "$f" ]; then
    curl -fL --proto '=https' -o "$f.part" "$1"
    echo "$2  $f.part" | sha256sum -c -
    mv "$f.part" "$f"
  fi
  echo "$2  $f" | sha256sum -c -
}

tc=llvm-mingw-20261006-msvcrt-ubuntu-22.04-x86_64
fetch "https://github.com/mstorsjo/llvm-mingw/releases/download/20261006/$tc.tar.xz" \
  9dd3bbff7f746937c99b38322c8b1fcab8054b7d8d31ec840b3457234dff7e52 "$tc.tar.xz"
[ -d "$dst/$tc" ] || tar -C "$dst" -xJf "$dst/$tc.tar.xz"

if [ "${1:-}" = ghidra ]; then
  fetch "https://github.com/NationalSecurityAgency/ghidra/releases/download/Ghidra_12.2_build/ghidra_12.2_PUBLIC_20261016.zip" \
    2c2cd776db3c1f6a097ba418922378ac3649cadd5e844a6f5f088ae628a7be67 ghidra.zip
  [ -d "$dst/ghidra/ghidra_12.2_PUBLIC" ] || python3 -I -m zipfile -e "$dst/ghidra.zip" "$dst/ghidra"
fi
