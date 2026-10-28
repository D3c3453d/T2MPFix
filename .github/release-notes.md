Four separate fixes for Thief 2 multiplayer (NewDark 1.29), each with its own switch in `T2MPFix.ini`:

| Fix | Switch | What it fixes |
|---|---|---|
| 1. Smooth remote players | `send_fix=1` and `recv_fix=1` | Other players' avatars lagging, sticking in walls and on door frames, floating or vanishing. `send_fix` sends your position 20×/s while you move; `recv_fix` shows the others on their real path |
| 2. Stock steering bug | `steer_fix=1` | Players without T2MPFix drifting several units behind: the engine's steering tests uninitialized memory |
| 3. Player limit | `max_players=8` | The last player to join drops others, because the engine's cap is off by one. Example: a 4th player joining kicks two others. Allows up to 8 (DirectPlay's limit) |
| 4. Environmental damage | `damage_fix=1` | Hazards each game runs locally doing no damage in multiplayer. Example: lava |

Set a switch to 0 to turn that fix off; the others keep working. `T2MPFix.log` lists which fixes are on.

## Install

Everyone in the session installs it (a player without it is still shown the stock way).

1. Install Thief 2 + NewDark 1.29 with the multiplayer beta: [T2Fix 1.29](https://github.com/Xanfre/T2Fix/releases),
   tick "Thief 2 Multiplayer Beta". Everyone must use the same version.
2. Download `T2MPFix-@VERSION@.zip` below and copy `dinput.dll` and `T2MPFix.ini` from its `T2MPFix`
   folder next to `Thief2MP.exe`. When updating, you can keep your own `T2MPFix.ini`.
3. Start `Thief2MP.exe`. The game folder gets a `T2MPFix.log` whose first line should say
   `T2MPFix @VERSION@ on Thief2MP 1.29`.

Uninstall: delete `dinput.dll` and `T2MPFix.ini`. Settings and known issues: [README](https://github.com/@REPO@/blob/v@VERSION@/README.md).
