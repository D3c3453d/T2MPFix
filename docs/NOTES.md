# Thief II: The Metal Age: co-op notes

Working notes, written as I go. Observations, derived facts, guesses and unknowns are kept apart.

## Game identity (rule 5)

| Field | Value |
|---|---|
| Store | Steam, app 211740, buildid 1970 |
| Stock exe | `thief2.exe` (OldDark 1.18 + ddfix), SHA-256 `f21cfc6d5fa73404637621e06cb20cade31a733508692fa8f7296a078dc74386` |
| Engine | Dark Engine (Looking Glass), patched by NewDark (unofficial, closed source) |
| MP exe targeted | NewDark 1.29 `Thief2MP.exe`, SHA-256 `e86b0645295e24f85747607c106beb9e94e4c21314f09156d4813c931d521fe0` (5,615,616 bytes) |
| MP exe 1.27 (reference) | SHA-256 `a22b3d0e6a254fe5247019bbd5a4d93bcfec234b51e67adf898825a442d45927`, PE timestamp 2019-06-15 |
| OS | Windows (32-bit PE, i386); tested from WSL2 on Windows |

## Downloads (pinned)

| File | SHA-256 | Source |
|---|---|---|
| `t2_v127.zip` (NewDark 1.27) | `33caca92a14acdb366211b464fbaa73113e028e9d024c9636e5a74890157ce26` | TTLG t=146448 mirror (MediaFire, via bit.ly/2KV5oDU) |
| `t2_v129.zip` (NewDark 1.29) | `c2de1addf3e61b81ec55de6b4df0154d42a174b0c2e9fce4cc3c768855183952` | Google Drive id pinned in T2Fix `fetchresources.sh`; MD5 `991b0ad2…` matches the TTLG t=153456 post |
| `T2Fix_1.29-2026-09-16.exe` | `4982266e7777683285dcf88311377257fe6be8880010458b3c6255bf2c78730f` | github.com/Xanfre/T2Fix releases (not needed in the end: Inno 6.4.3, innoextract master can't parse it) |

## Sources

- Video guide (WungusDasPungus, "How to Make Thief 2 Multiplayer Actually Work"): https://www.youtube.com/watch?v=aRiVJqx9cnQ
- NewDark 1.27: https://www.ttlg.com/forums/showthread.php?t=146448 ("added multiplayer beta (separate executable)")
- NewDark 1.29: https://www.ttlg.com/forums/showthread.php?t=153456
- T2Fix: https://www.ttlg.com/forums/showthread.php?t=149669, https://github.com/Xanfre/T2Fix
- Figgis, SS2/T2 MP improvements: https://www.systemshock.org/index.php?topic=12190.0
- Global server: https://github.com/sungaila/NewDarkGlobalServer
- Costume add-on: https://github.com/vfig/t2mpfun

## Observations

- The "Thief 2 Multiplayer" in the hub directory (2007 fan mod for OldDark 1.18) is a different project. Since NewDark 1.27
  (2019-06-15) the NewDark team ships its own MP beta as `mp.zip` → `Thief2MP.exe`. The video and T2Fix use that one.
- 1.28 and 1.29 release notes list MP fixes for server-list and readables crashes and centered name labels. Nothing about
  avatar/ghost movement (release_notes.txt in t2_v129.zip).
- The MP exe uses the original Looking Glass "ghost" system: RTTI `cGhostLocalHash`, `cGhostRemoteHash`; net message names
  `GhostFullHB`, `GhostNormHB`, `GhostMiniHB`, `GhostHBTerse`, `GhostRotHB`, `GhostObjHB`, `GhostAIHB`. The only config
  vars are `ghost_local_frame_rate`, `ghost_local_process_ms`, `ghost_spew`, so no knob for remote smoothing.
- Figgis (2023) lists as unsolved: "Sometimes a remote player can get stuck on geometry and then warp to the proper
  position after a while." This matches the owner's bug report.
- Figgis's player-cap byte "offset 0x2F440 in Thief2MP.exe": in the 1.27 exe from t2_v127.zip that file offset is a function
  prologue (`6a ff 68 …`), not `04`. **Contradiction**, not resolved yet. The Google Drive patched exe from the video
  description returns 404 (checked 2026-10-26).
- YouTube transcript points: 4th player joining kicks two others (Figgis's patch fixes it); server browser crash in T2Fix
  1.27 (fixed in 1.29 per notes); teleports in FMs break sessions; items picked up by a client can vanish.

## Derived (from the Ghidra 12.2 decompile of the 1.29 MP exe; addresses are preferred VAs, base 0x400000)

Ghost (network object movement) pipeline:

| Address | Role |
|---|---|
| `0x5516e0` | per-frame ghost tick (dt ms): each local ghost `0x5556a0`, then each remote ghost `0x554980` |
| `0x5556a0` | local ghost: run the receiver's predictor on our own state, ask `0x554fe0` whether to send, send with `0x554d40` |
| `0x554fe0` | send decision. Builds the heartbeat (pos from phys model +0x34), then sends only if the receiver's prediction is off by more than 4 units on an axis (`0x81a108` = 4.0 × per-ghost scale), velocity off by 20, heading off, mode change, or 30 s keep-alive. Minimum interval by ghost type table `0x889f1c` = {0, 500, 250, 100} ms; type 3 = player |
| `0x5539d0` | receive heartbeat (all message handlers `0x552620..0x552800` funnel here). Copies into the remote ghost's info block (+0x0c); adds the `AvatarZOffset` property to z |
| `0x553bd0` → `0x553520` | receiver prediction: gravity if not grounded, blend velocity, pos += vel·dt, then a raycast (`0x54a880`) that stops the prediction at walls |
| `0x553dc0` | steer the avatar toward the prediction: error d = pred − physics position. If \|d\|² > 144 (`0x81a9a0`) or flag 0x200000: warp via `0x5534c0` (which fails if the physics spheres don't fit there, `0x547220`). Else set a physics velocity d·2.0 (`0x819aa0`) or predicted velocity + d, so the avatar is moved by local physics, with collisions |
| `0x516c90` | object → physics model (`+0x34` position) |
| `0x41a940` | engine command line (EAX = string) |

Conclusion: the remote avatar is a locally simulated physics body chasing a coarse guess. It may be up to 12 units away
before the engine snaps it, collides with door frames and stairs that the real player passed, follows a gravity-and-raycast
prediction (sinks or floats until the next update), and the sender only corrects when the guess is 4+ units off. That
matches all reported symptoms. There is no config var for any of these thresholds.

UI and input facts (for test automation):
- The window procedure (`0x6999d0`) handles `WM_ACTIVATEAPP(FALSE)` by calling a pause method (vtable +0x5c), so an
  inactive window freezes the whole game loop (also what Figgis saw: saves stall until everyone alt-tabs back).
- Mouse events go to the Looking Glass mouse queue `0x689480` ({x, y, type, time, buttons}).
- `screen_dump` captures the 3D back buffer only, which is empty in menus.
- With the Windows session locked, D3D can't present and nothing can be tested live.

## Fix: T2MPFix (mod/, dinput.dll proxy)

- Send: for the local player ghost, `0x554fe0` is forced to "send" every 50 ms while moving/turning (plus one
  "stopped" heartbeat). Measured 16.3 heartbeats/s while moving vs 1.3/s stock.
- Receive (only for senders proven to run T2MPFix: ≥ 12 heartbeats/s while moving, which stock can't reach):
  per remote player, buffer received states (receive time, pos, vel, heading) and show the state at
  now − delay (delay = 1.5 × mean gap + 2 × jitter, clamped 60..300 ms; no gravity in extrapolation, capped 100 ms).
  The interpolated state is written into the ghost's prediction block and flag 0x200000 makes the stock steer code warp
  there. If the warp refuses (body doesn't fit), `0x559c30` (ObjPosUpdate) places it anyway. Never needed in tests so far.

## Test results (2026-10-26, NewDark 1.29 MP, Steam build 1970, 2 instances on one PC, mission 1)

Harness: `tools/qa/run_ab.sh` (A/B: identical session and bot, only the ini differs), metric `tools/qa/analyze.py`.
One player runs `tools/qa/bots/walker.txt` (walk, run, jump, strafe, turn, crouch, back, circle; 30 s loop), the
other stands still; 65 s measured. path_dev = distance of the shown avatar from the true path (lag excluded).

| Run | path_dev p95 | path_dev max | frames >1 unit off path | lag p50 | heartbeats/s |
|---|---|---|---|---|---|
| stock, client moving, localhost | 2.371 | 3.031 | 2338 / 5926 | 0.982 | 1.3 |
| T2MPFix, client moving, localhost | 0.002 | 0.077 | 0 / 5926 | 0.314 | 16.3 |
| stock, host moving, localhost | 2.367 | 3.041 (lag max 14.3, 1 warp) | 2360 | 1.053 | 1.3 |
| T2MPFix, host moving, localhost | 0.002 | 0.066 | 0 | 0.307 | 16.3 |
| stock, 60±30 ms, 2% loss | 2.428 | 3.38 | 2171 | 1.054 | 1.3 |
| T2MPFix, 60±30 ms, 2% loss | 0.004 | 0.094 | 0 | 0.648 | 16.3 |
| stock, 150±75 ms, 10% loss | 2.651 | 7.13 | 2328 | 1.291 | 1.3 |
| T2MPFix, 150±75 ms, 10% loss | 0.011 | 0.249 | 0 | 1.059 | 16.3 |

Network impairment is applied in the DLL in front of the heartbeat receiver (QA only; the exe imports no
sendto/recvfrom/bind, so the transport is likely DirectPlay and the hub's UDP chaos relay doesn't fit). Visual check: screenshot of the host's view
shows the client's avatar on the ground at the telemetry position (tools/qa shots, not committed).

- Player cap: `cNetManager` field +0x136 (default 4 written at `0x438d39`), `SetMaxPlayers` = vtable slot 40
  (`0x43a230`, stdcall). Stock lobby with 4 instances: Client3 joining kicked Client1 and Client2 (reproduced).
  With the cap set to 5: all 4 stay and play.

## Lava does no damage in multiplayer (fixed by `damage_fix`)

Observed: Soulforge (mission 15, `miss16`): the lava trough next to the start does nothing in `Thief2MP.exe`, kills in
`thief2.exe`; the ladle (a physical object) hurts in both. Reproduced with all T2MPFix hooks off and `max_players=0`.

Traced with a throwaway logging build, step by step (each step logged on the live game):
- The lava is a water room whose cells carry flow group 2. Object 1739 (`Water Flow Group`, `FlowGroup` = 2) has
  stim sources FireStim and WaterStim. On a medium change the physics media callback (`0x486d00`) calls the flow
  propagator (`0xab9a70`, a `cPeriodicPropagator`), which links 1739 -> player and sends FireStim (intensity 14) now
  and every 1000 ms. This all works in MP.
- The player's receptrons: `M-GarrettDiffNormal` Amplify 0.8 (-> 11.2), then `FireDamaged` `damage` (effect 21,
  handler `0x596fc0`), which calls `IDamageModel::DamageObject` (`0x5a4da0`) with victim = player, culprit = 1739,
  amount 11-12.
- `DamageObject` runs the damage filters (`0x5963f0`, array at model+0x20). Filter `0x44bce0` (MP only, `__stdcall`
  (victim, culprit, msg, data)) zeroes the amount and returns 1, so `DamageObject` returns 1 ("status quo").
- Its logic: allow if not networking or if the victim is local-only; if the culprit is hosted here and the victim
  is a proxy, send the damage to the owner (`0x44c080`) and zero it locally; if the culprit is neither hosted here
  nor a proxy, zero it unless it has `LocalCopy` (the out value of that property query reuses an argument slot, so
  a missing property also blocks). Measured: player f1c 0 / f20 1 / f24 0, lava 1739 f1c 0 / f20 0 / f24 1
  (`IObjectNetworking` at `0xab83ac`, slots +0x1c proxy, +0x20 hosted here, +0x24 local only).
- Result: any damage whose culprit is a Local Only object is dropped on every machine. `dark_net.dml` makes
  `physical` objects Hosted, which is why the ladle works.

Fix: hook the filter; if the culprit is local-only and the victim is hosted here (not a proxy), return 0 (no
opinion) and let the damage through. Each machine applies it to the objects it owns (its own player; AI on the
host); proxies still go through the stock filter and stay blocked, so nothing is applied twice.
Verified: host and client each die within ~2 s in the Soulforge lava; the other player keeps full health.

## Lessons (candidates for playbook/lessons.md)

- `0x516c90` is `__stdcall` (`ret 4`) although Ghidra shows a plain one-argument function. Calling it as cdecl drifted
  ESP by 4 per call and crashed both games on the first in-mission frame. Check `ret N` of every engine function you
  call, not only the decompiler's signature.
- Hooks called from MSVC code get a 4-byte-aligned stack; clang/gcc for i686 assume 16 and emit `movapd` to the stack.
  Build hook DLLs with `-mstackrealign`. The fault shows up as an access violation at address 0xffffffff.
- Thief2MP pauses its whole loop on `WM_ACTIVATEAPP(FALSE)`, and only accepts injected mouse events while its window
  is *not* the foreground window. For two instances on one desktop: subclass the window, swallow deactivation, send a
  synthetic activate, and keep both windows in the background. Mouse coordinates are in a space scaled 1.28 from the
  800x600 client (menus are 640x480-based).
- When a stimulus "does nothing", log each hop (propagator -> sensor -> receptron -> effect -> damage model ->
  filters) on the live game before reading more decompile: here the decompile of the stimulator had "unreachable"
  blocks removed, and the real cause was a damage filter three layers further down.
- A locked Windows session makes every visual or input test fail silently (D3D can't present); check `LogonUI` first.

## Unknowns

- The game transport: no sendto/recvfrom/bind imports, so likely DirectPlay (not verified).
- Remote players on elevators and moving platforms (heartbeats relative to an object fall back to stock steering),
  ladders, rope arrows, swimming, saves/loads, mission changes, fan missions: not tested.

Resolved: player heartbeat rate and warp threshold (see "Derived"); several `Thief2MP.exe` instances run on one PC
(tested with 4).
