#!/usr/bin/env bash
# Build mod/build/dinput.dll with llvm-mingw (pinned 20261006 msvcrt) inside a container.
# No PE timestamp, so the same source gives a byte-identical DLL (locally and in CI).
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
tc=llvm-mingw-20261006-msvcrt-ubuntu-22.04-x86_64
mh="third_party/minhook/src"
mkdir -p "$root/mod/build"
docker run --rm -u "$(id -u):$(id -g)" -v "$root/tools_build/$tc:/tc:ro" -v "$root/mod:/src" -w /src \
  eclipse-temurin:25-jdk /tc/bin/i686-w64-mingw32-clang -O2 -mstackrealign -Wall -Wextra -Wno-unused-parameter \
  -Isrc -Ithird_party/minhook/include -shared -o build/dinput.dll \
  src/main.c src/proxy.c src/interp.c src/qa.c src/fix_send.c src/fix_recv.c src/fix_steer.c src/fix_players.c src/fix_damage.c \
  $mh/hook.c $mh/buffer.c $mh/trampoline.c $mh/hde/hde32.c \
  src/dinput.def -s -static -Wl,--no-insert-timestamp
