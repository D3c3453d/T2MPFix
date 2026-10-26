# ghostfix: smooth remote players for Thief 2 multiplayer (NewDark 1.29)

Fixes the other players' avatars in Thief II multiplayer showing up in the wrong place: lagging
behind, sticking on door frames and stairs, walking through walls, floating, or vanishing. Also
fixes "the 4th player joining kicks two others".

**Everyone in the session should install it.** It changes how often your own position is sent as
well as how the others are shown. A player without it is still shown the stock way (no worse than
before), but only players who have it are shown smoothly. It only works with the NewDark 1.29 multiplayer exe
(`Thief2MP.exe` from `t2_v129.zip` / T2Fix 1.29 with the multiplayer component). With any other
exe it does nothing.

## Install

1. Have a working Thief 2 + NewDark 1.29 + multiplayer install (T2Fix 1.29 from
   https://github.com/Xanfre/T2Fix, tick "Thief 2 Multiplayer Beta"). Everyone must use the same version.
2. Copy `dinput.dll` and `ghostfix.ini` from the release zip into the game folder (next to `Thief2MP.exe`).
3. Start `Thief2MP.exe` as usual. A `ghostfix.log` file appears in the game folder; its first line
   should say `ghostfix 0.1 on Thief2MP 1.29`.

Hosting and joining are unchanged: the host forwards port 5198 (5199 too for the global server) or
everyone uses a LAN VPN.

Uninstall: delete `dinput.dll` and `ghostfix.ini`. Single-player `thief2.exe` is not affected.

## Settings (`ghostfix.ini`)

| Key | Default | Meaning |
|---|---|---|
| `send_fix` | 1 | While you move or turn, send your position 20 times a second (stock: only when the others' guess is 4+ units off) |
| `recv_fix` | 1 | Show other players on their real path, about 0.1 s behind, instead of steering their body through local physics |
| `send_interval_ms` | 50 | Send interval while moving |
| `delay_min_ms` / `delay_max_ms` | 60 / 300 | Bounds for the display delay (it adapts to your connection's jitter) |
| `max_players` | 4 | The host's player cap is set to N+1 (Figgis's finding), so 4 players can join. 0 = leave stock. Tested with all four players set to 4 |
| `telemetry` | 0 | Write `ghostfix_tel.csv` for debugging |

## How it works

The stock engine sends a player's position only when the receiver's dead-reckoned guess is 4+ units
off, moves the remote avatar as a local physics body chasing that guess (colliding with the world on
the way), and only snaps it into place when it is 12 units off. ghostfix sends while moving, buffers
the received positions and places the avatar on the interpolated path every frame using the
engine's own teleport. Addresses, the derivation and all measurements: [docs/NOTES.md](docs/NOTES.md).

## Results

Two real game instances on one PC, one player driven by a movement bot (walk, run, jump, strafe,
crouch, turn), the same session with and without ghostfix (`tools/qa/regress.sh`, build `72bcefc0…`).
path_dev = distance in Dark units of the shown avatar from the player's true path.

```
PASS stock localhost (control)    travelled  239.0  path_dev p95 2.357 max 3.070  off>1  2352  jumps 1  lag p50 1.04  hb/s 1.3  fixed 0/5916
PASS fixed localhost              travelled  239.2  path_dev p95 0.003 max 0.125  off>1     0  jumps 0  lag p50 0.30  hb/s 16.2  fixed 5602/5923
PASS fixed 60+-30ms 2% loss       travelled  238.3  path_dev p95 0.004 max 0.137  off>1     0  jumps 0  lag p50 0.61  hb/s 15.6  fixed 5588/5919
PASS fixed 150+-75ms 10% loss     travelled  238.1  path_dev p95 0.014 max 0.218  off>1     0  jumps 0  lag p50 1.00  hb/s 11.3  fixed 5505/5905
PASS mixed (mover without fix)    travelled  232.9  path_dev p95 2.351 max 3.007  off>1  2346  jumps 1  lag p50 1.04  hb/s 1.3  fixed 0/5806
ALL PASS
```

4 players (2 moving): without `max_players` the 4th joiner kicks two others (reproduced); with it all
four stay, path_dev max ≤ 0.64.

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
tools/package.sh 0.1        # -> dist/thief2-ghostfix-0.1.zip
```

## Tests

- `make -C mod test`: interpolation unit tests (host gcc).
- `python3 -I tools/qa/test_analyze.py`: self-test of the path-deviation metric.
- `tools/qa/regress.sh`: the A/B table above, with real games on one Windows PC driven from WSL
  (~15 min, needs an unlocked desktop). Set up the lanes first: put a copy of a clean Thief 2 install in
  `local/vanilla/` and the zips from `t2_v129.zip` in `downloads/nd129/`, then
  `tools/make_lane.sh host` and `tools/make_lane.sh client1` (lanes go to `$T2_TEST_DIR`, default
  `/mnt/d/thief_coop_test`). The DLL's QA mode (`[qa]` in the ini, see `mod/src/qa.c`) drives the menus
  and the bots; the game windows must not be the foreground window while it runs.
- `tools/decompile.sh <Thief2MP.exe>` (after `tools/fetch_tools.sh ghidra`): the Ghidra decompile the
  addresses in `mod/src/engine.h` come from. Keep its output local.

## Layout

| Path | What |
|---|---|
| `mod/src/main.c` | hooks (send decision, steer, heartbeat receive), config, player cap, bot |
| `mod/src/interp.c` | snapshot buffer and interpolation (no engine dependencies, unit tested) |
| `mod/src/engine.h` | Thief2MP 1.29 addresses, struct offsets, byte signatures checked at startup |
| `mod/src/qa.c` | QA channel: injected mouse input, engine commands, window activation |
| `mod/src/proxy.c` | forwards the real `dinput.dll` exports |
| `mod/third_party/minhook` | MinHook v1.3.3 (BSD-2-Clause, see its LICENSE.txt) |
| `tools/qa/` | lane control, A/B runner, analyzer, bots |
| `tools/win/` | PowerShell helpers for window placement and screenshots |
| `tools/re/` | PE, minidump and command-table helpers |

## Credits

- NewDark team for the 1.29 multiplayer beta; Figgis (systemshock.org topic 12190) for the player-cap finding.
- MinHook by Tsuda Kageyu.
