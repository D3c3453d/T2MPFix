Fixes other players' avatars in Thief 2 multiplayer (NewDark 1.29) lagging, sticking in walls,
floating or vanishing, and the "4th player joining kicks two others" bug.

## Install

Everyone in the session installs it (a player without it is still shown the stock way).

1. Install Thief 2 + NewDark 1.29 with the multiplayer beta: [T2Fix 1.29](https://github.com/Xanfre/T2Fix/releases),
   tick "Thief 2 Multiplayer Beta". Everyone must use the same version.
2. Download `thief2-ghostfix-@VERSION@.zip` below and copy `dinput.dll` and `ghostfix.ini` from its `ghostfix`
   folder next to `Thief2MP.exe`. When updating, you can keep your own `ghostfix.ini`.
3. Start `Thief2MP.exe`. The game folder gets a `ghostfix.log` whose first line should say
   `ghostfix @VERSION@ on Thief2MP 1.29`.

Uninstall: delete `dinput.dll` and `ghostfix.ini`. Settings and known issues: [README](https://github.com/@REPO@/blob/v@VERSION@/README.md).
