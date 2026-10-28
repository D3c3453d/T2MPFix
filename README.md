# T2MPFix: fixes for Thief 2 multiplayer (NewDark 1.29)

Four separate fixes for Thief II multiplayer, each with its own switch in `T2MPFix.ini`:

1. **Smooth remote players** (`send_fix` + `recv_fix`): the other players' avatars no longer show up in
   the wrong place (lagging behind, sticking on door frames and stairs, walking through walls, floating,
   vanishing).
2. **Stock steering bug** (`steer_fix`): players that fix 1 doesn't cover (those without T2MPFix) no longer
   drift several units behind, caused by an uninitialized read in the engine's steering code.
3. **4-player cap** (`max_players`): a 4th player joining no longer kicks two others.
4. **Lava damage** (`damage_fix`): lava and other local-only hazards hurt players again.

**Everyone in the session should install it.** The avatar fix changes how often your own position is sent as
well as how the others are shown. A player without it is still shown by the stock code (plus fix 2 on the
machines that have T2MPFix), but only players who have it are shown smoothly. It only works with the NewDark
1.29 multiplayer exe (`Thief2MP.exe` from `t2_v129.zip` / T2Fix 1.29 with the multiplayer component). With any
other exe it does nothing.

## Install

1. Have a working Thief 2 + NewDark 1.29 + multiplayer install (T2Fix 1.29 from
   https://github.com/Xanfre/T2Fix, tick "Thief 2 Multiplayer Beta"). Everyone must use the same version.
2. Copy `dinput.dll` and `T2MPFix.ini` from the release zip into the game folder (next to `Thief2MP.exe`).
3. Start `Thief2MP.exe` as usual. A `T2MPFix.log` file appears in the game folder; its first line
   should say `T2MPFix 0.2 on Thief2MP 1.29`.

Hosting and joining are unchanged: the host forwards port 5198 (5199 too for the global server) or
everyone uses a LAN VPN.

Uninstall: delete `dinput.dll` and `T2MPFix.ini`. Single-player `thief2.exe` is not affected.

## The four fixes

Each fix is its own module (`mod/src/fix_*.c`) with its own key in `T2MPFix.ini`. A fix that is off, or
whose engine code doesn't match, hooks nothing and leaves that part of the game stock; the others are not
affected.

### 1. Smooth remote players: `send_fix` and `recv_fix`

The stock engine sends a player's position only when the receiver's dead-reckoned guess is 4+ units
off, moves the remote avatar as a local physics body chasing that guess (colliding with the world on
the way), and only snaps it into place when it is 12 units off. The fix has two halves, one on each end:

| Key | Default | Half | What it does |
|---|---|---|---|
| `send_fix` | 1 | sending (`fix_send.c`) | While you move or turn, send your position 20 times a second (stock: about 1.3/s) |
| `recv_fix` | 1 | receiving (`fix_recv.c`) | Buffer the received positions and place each player on their interpolated real path, about 0.1 s behind, with the engine's own teleport, instead of steering their body through local physics |
| `send_interval_ms` | 50 | sending | Send interval while moving (receivers also use it as the expected gap) |
| `delay_min_ms` / `delay_max_ms` | 60 / 300 | receiving | Bounds for the display delay (it adapts to your connection's jitter) |

The halves can be switched independently, but only work together. `recv_fix` only interpolates a player
whose heartbeat rate proves they run `send_fix`; anyone else keeps the stock steering (see fix 2). `send_fix`
alone doesn't help either: a receiver without `recv_fix` gets ~16 updates a second instead of ~1.3, but its stock
steering still lags and sticks (measured: worse than stock, see Results).

### 2. Stock steering bug: `steer_fix`

| Key | Default | What it does |
|---|---|---|
| `steer_fix` | 1 | For a moving remote player, the engine's steering code decides whether to pull the avatar toward its predicted position by testing three stack values it never wrote. In the 1.29 MP exe the test fails, so the avatar only follows the predicted velocity and drifts behind (several units) until the 12-unit snap. This 6-byte patch (`fix_steer.c`) always pulls it toward the prediction. 0 = stock |

It only matters for players that `recv_fix` doesn't interpolate: players without T2MPFix, or everyone if you
turn `recv_fix` off. Such a player's path deviation: p95 6.5 / max 7.7 units with the stock code, 2.4 / 3.0 with
`steer_fix`.

### 3. 4-player cap: `max_players`

| Key | Default | What it does |
|---|---|---|
| `max_players` | 4 (0 in code) | The host sets the engine's player cap to N+1 (Figgis's finding), so N players can join; stock keeps 4, so the 4th joiner gets two others kicked. 0 = leave stock. Tested with all four players set to 4 |

### 4. Lava damage: `damage_fix`

| Key | Default | What it does |
|---|---|---|
| `damage_fix` | 1 | Lava (and other damage from local-only objects) hurts players again. Stock multiplayer zeroes it on every machine; with this, each player's own game applies it to them. 0 = stock |

### Other settings

| Key | Default | Meaning |
|---|---|---|
| `telemetry` | 0 | Write `T2MPFix_tel.csv` for debugging |

## Turning a fix off

If a NewDark update ever fixes one of these problems itself, set that key to 0 (for the avatar fix,
everyone sets `send_fix=0` and `recv_fix=0`). `T2MPFix.log` lists each fix on startup:

```
send_fix: on
recv_fix: off in T2MPFix.ini
steer_fix: on
max_players: on
damage_fix: on
active: send_fix steer_fix max_players damage_fix
```

With all of them off it hooks nothing and is a plain `dinput.dll` pass-through. Note that a new NewDark exe
also has new addresses: T2MPFix recognises only the 1.29 `Thief2MP.exe` and does nothing on any other
exe until it is ported. To remove a fix from the code, delete its `mod/src/fix_*.c`, its line in `fixes[]` in
`mod/src/main.c` and its entry in `tools/build.sh` and `mod/Makefile`.

Addresses, the derivation and all measurements: [docs/NOTES.md](docs/NOTES.md).

## Results

Two real game instances on one PC on mission 1, one player driven by a movement bot (walk, run, jump, strafe,
crouch, turn), the same session with each set of fixes (`tools/qa/regress.sh`, build `04d806c9…`, which differs
from the 0.2 release only in the version string). path_dev =
distance in Dark units of the shown avatar from the player's true path. "stock" is the real game: the steer
function is not hooked (telemetry reads the ghost table instead).

```
PASS stock localhost (control)    travelled  260.3  path_dev p95 6.481 max 7.652  off>1  3896  jumps 1  lag p50 3.04  hb/s 1.3  fixed 0/6519
PASS fixed localhost              travelled  260.1  path_dev p95 0.003 max 0.127  off>1     0  jumps 0  lag p50 0.30  hb/s 16.5  fixed 6209/6527
PASS fixed 60+-30ms 2% loss       travelled  259.8  path_dev p95 0.004 max 0.125  off>1     0  jumps 0  lag p50 0.62  hb/s 15.7  fixed 6182/6517
PASS fixed 150+-75ms 10% loss     travelled  257.8  path_dev p95 0.013 max 0.236  off>1     0  jumps 0  lag p50 0.98  hb/s 11.6  fixed 6093/6503
PASS send_fix only                travelled  259.8  path_dev p95 9.177 max 12.000  off>1  3482  jumps 2  lag p50 1.68  hb/s 16.5  fixed 0/6519
PASS recv_fix only                travelled  259.7  path_dev p95 2.401 max 3.060  off>1  2668  jumps 0  lag p50 1.02  hb/s 1.3  fixed 0/6529
PASS steer_fix only               travelled  257.1  path_dev p95 2.378 max 3.006  off>1  2702  jumps 1  lag p50 1.09  hb/s 1.4  fixed 0/6514
PASS mixed (mover without fix)    travelled  257.3  path_dev p95 2.438 max 3.124  off>1  2667  jumps 1  lag p50 1.08  hb/s 1.3  fixed 0/6396
ALL PASS
```

- "fixed" = all fixes on. "mixed": the mover has no T2MPFix; the observer's `steer_fix` handles them.
- "send_fix only": a player with T2MPFix seen by a player without it is shown *worse* than stock (the stock
  steering bug with more frequent updates). Everyone in the session should install T2MPFix.
- "recv_fix only": `steer_fix` is off, but the result matches it: the stock steering's uninitialized read
  happened to go the good way behind this build's hook. `steer_fix` makes that deterministic.

Each fix on its own, with every other fix off:
- `max_players=4`: 4 instances all join and play; with `max_players=0` the 4th joiner drops the others
  ("Network Connection Lost!" while the host waits forever).
- `damage_fix=1`: on Soulforge (mission 15) the player dies in the lava; with `damage_fix=0` they stand in it at
  full health (`tools/qa/lava.sh`).
- With all fixes off (and QA off), nothing is hooked or patched; the game runs as stock.

## Known issues

- Not tested yet: elevators and moving platforms (positions relative to an object fall back to stock
  steering), ladders, rope arrows, mantling, swimming, saves/loads, mission changes, fan missions,
  a session over the Internet (network impairment was simulated in the DLL).
- AI and other networked objects still use the stock ghost code.
- Alt-tabbing still pauses that player's game, which stalls things like saving for everyone (stock behaviour).

## Build

Needs Docker (the compiler runs in a container; nothing is installed on the host).

```sh
tools/fetch_tools.sh        # pinned llvm-mingw 20261006 into tools_build/, SHA-256 checked
tools/build.sh              # -> mod/build/dinput.dll
tools/package.sh 0.2        # -> dist/T2MPFix-0.2.zip
```

The build is reproducible: the same source gives a byte-identical `dinput.dll` (no PE timestamp), so you
can rebuild a release and compare its hash with the one in the release description. The zip itself is not byte-identical (file times).

Releases are built by GitHub Actions (`.github/workflows/build.yml`) from source: every push and pull request
builds, tests and uploads the zip as a run artifact. To release, bump the version in `mod/src/main.c`
(`T2MPFix <version> on Thief2MP`), then `git tag v<version> && git push origin v<version>`. The workflow
creates a draft release with the zip and a description from `.github/release-notes.md` plus the hashes;
check it and publish it.

## Tests

- `make -C mod test`: unit tests for the interpolation, the damage rule and the hook selection (host gcc).
- `python3 -I tools/qa/test_analyze.py`: self-test of the path-deviation metric.
- `tools/qa/lava.sh <0|1>`: `damage_fix` on Soulforge's lava (host alone, needs the host lane).
- `tools/qa/regress.sh`: the A/B table above, with real games on one Windows PC driven from WSL
  (~25 min, needs an unlocked desktop). Set up the lanes first: put a copy of a clean Thief 2 install in
  `local/vanilla/` and the zips from `t2_v129.zip` in `downloads/nd129/`, then
  `tools/make_lane.sh host` and `tools/make_lane.sh client1` (lanes go to `$T2_TEST_DIR`, default
  `/mnt/d/thief_coop_test`). The DLL's QA mode (`[qa]` in the ini, see `mod/src/qa.c`) drives the menus
  and the bots; the game windows must not be the foreground window while it runs.
- `tools/decompile.sh <Thief2MP.exe>` (after `tools/fetch_tools.sh ghidra`): the Ghidra decompile the
  addresses in `mod/src/engine.h` come from. Keep its output local.

## Layout

| Path | What |
|---|---|
| `mod/src/main.c` | core: exe check, config, log, the ghost hooks the avatar fixes plug into, telemetry, bot, network impairment |
| `mod/src/fixes.h` | the fix module interface and which ghost hooks are needed (unit tested) |
| `mod/src/fix_send.c` | fix 1, sending half (`send_fix`): force a heartbeat every 50 ms while moving |
| `mod/src/fix_recv.c` | fix 1, receiving half (`recv_fix`): sender classification, interpolated placement |
| `mod/src/fix_steer.c` | fix 2 (`steer_fix`): patch for the stock steering's uninitialized read |
| `mod/src/fix_players.c` | fix 3 (`max_players`): player cap N+1 |
| `mod/src/fix_damage.c` | fix 4 (`damage_fix`): damage filter hook |
| `mod/src/damage.h` | when local-only damage (lava) is let through (unit tested) |
| `mod/src/interp.c` | snapshot buffer and interpolation (no engine dependencies, unit tested) |
| `mod/src/engine.h` | Thief2MP 1.29 addresses, struct offsets, byte signatures checked at startup |
| `mod/src/qa.c` | QA channel: injected mouse input, engine commands, window activation |
| `mod/src/proxy.c` | forwards the real `dinput.dll` exports |
| `mod/third_party/minhook` | MinHook v1.3.3 (BSD-2-Clause, see its LICENSE.txt) |
| `tools/qa/` | lane control, A/B runner, analyzer, bots |
| `tools/win/` | PowerShell helpers for window placement and screenshots |
| `tools/re/` | PE, minidump and command-table helpers |

## License

MIT, see [LICENSE](LICENSE). `mod/third_party/minhook` keeps its own BSD-2-Clause license.

## Credits

- NewDark team for the 1.29 multiplayer beta; Figgis (systemshock.org topic 12190) for the player-cap finding.
- MinHook by Tsuda Kageyu.
