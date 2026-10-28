# The release DLL is built by tools/build.sh (llvm-mingw in a container); this rule is the same command
# for a host that has i686-w64-mingw32-clang on PATH. `make test` runs the interpolation, damage-filter and hook-selection tests with host gcc.
CC      = i686-w64-mingw32-clang
CFLAGS  = -O2 -mstackrealign -Wall -Wextra -Wno-unused-parameter -Isrc -Ithird_party/minhook/include
MH      = third_party/minhook/src/hook.c third_party/minhook/src/buffer.c third_party/minhook/src/trampoline.c \
          third_party/minhook/src/hde/hde32.c
SRC     = src/main.c src/proxy.c src/interp.c src/qa.c src/fix_send.c src/fix_recv.c src/fix_steer.c \
          src/fix_players.c src/fix_damage.c

build/dinput.dll: $(SRC) $(MH) src/common.h src/engine.h src/fixes.h src/interp.h src/damage.h \
                src/dinput.def
	mkdir -p build
	$(CC) $(CFLAGS) -shared -o $@ $(SRC) $(MH) src/dinput.def -s -static -Wl,--no-insert-timestamp

build/test_interp: tests/test_interp.c src/interp.c src/interp.h
	mkdir -p build
	gcc -O1 -Wall -Isrc -o $@ tests/test_interp.c src/interp.c -lm

build/test_damage: tests/test_damage.c src/damage.h
	mkdir -p build
	gcc -O1 -Wall -Isrc -o $@ tests/test_damage.c

build/test_fixes: tests/test_fixes.c src/fixes.h
	mkdir -p build
	gcc -O1 -Wall -Isrc -o $@ tests/test_fixes.c

test: build/test_interp build/test_damage build/test_fixes
	./build/test_interp
	./build/test_damage
	./build/test_fixes

.PHONY: test
