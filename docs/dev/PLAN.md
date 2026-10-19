# Plan

**Start of every session: read this file, then [BUGS.md](BUGS.md).** Update it when the plan changes.
Approved by the owner on 2026-10-19 (full text with reasoning: `%USERPROFILE%\.claude\plans\sequential-scribbling-cascade.md`).

## Next step (where to resume)
**Before every DLL commit run the "must pass" set in [TESTS.md](TESTS.md).**
1. Verify `startfix` (B1) on chapters 1, 2a, 4c, 5, 8b, 9 with `-Look`, then B2 in chapter 5; then the must-pass set.
2. Injected input via window messages (stage 6) so movement tests need no focus.
3. Stage 3: ProcessEvent looks unusable (script mostly native, see NOTES); go for native calls found in Ghidra.

## Goals in order
1. **R1: all 13 story chapters start for 2 players.** Automated check: both have a pawn within 90 s of the guest joining,
   both alive for 5+ min, both pawns move, no crash.
2. **Friends over a VPN** right after R1 (owner decision), with what works at that point.
3. **R2: chapter 1 start to finish for 2 players** (combat, revive, checkpoint, chapter transition).
4. **R3: 3-5 players.**
5. The whole campaign. Escalation is a parallel quick track.

Owner decisions (2026-10-19): gameplay tests run **inside the game** (state file, injected input, in-game screenshots);
the F10 menu test keeps real mouse and keyboard. Friends join over a VPN after R1.

## Stages
Link the bug each item addresses ([BUGS.md](BUGS.md)); move finished and verified items to [DONE.md](DONE.md).

### Stage 1. Foundation (B7, B8)
1. **Game-thread tick.** A timer thread posts `WM_APP+0x4F1` to the game window every 100-250 ms; `GetMsgProc`
   (`overlay.inc`) calls `GameTick()`. One pass over GObjects by class id (no `VirtualQuery` per object) fills a cache
   (controllers, pawns, Game, WorldInfo). The work of `CtrlShareThread`, hostfirst, `RescueThread`, `MaxPlayersThread`
   and `ForceCharThread` moves into the tick (same ini flags). Background threads keep only what is needed before the
   window exists (net driver heap patch, mutex).
2. **`foc_state.json`** (host and guest), written once per second (tmp file + rename): map, GameInfo class, role,
   per controller: local/remote, pawn, pawn class, xyz, spawn-helper state; guard and exception counters.
3. **`tools/matrix.ps1` asserts state** instead of relying on screenshots: polls both state files with timeouts, checks
   the mode first (map, game class, role), prints `RESULT: PASS/FAIL <map> <reason>`; a missing file or process is FAIL.
4. **Minidump** of the first unhandled access violation in TFOC.exe (`foc_crash_<pid>.dmp`, dbghelp), `crashtest` command.
5. **Old probes read the name from +0x18** (it is the packed FName at +4, `ObjNameStr`). Fix them and mark the two
   conclusions based on it in NOTES.md as unverified.
- Check: matrix PASS on `ESC_Ark_BASE_m`, `L01_TheExodus_BASE_m`, `L06_Megatron_BASE_m`; stat every 1-3 s (B8);
  10 starts of copy B without a crash (B7); `crashtest` writes a dump.

### Stage 2. B1 diagnosis from memory
Find `SpawnHelper` in the live Game object and its `Players` array (`{PC, GameState}`, 8 bytes each). Write each
player's state to the state file. Run 2b (bad), 6 (control), 1. State 0 = `CheckReadySpawn` fails, 1 = no spawn point,
2 = native `TrySpawnPlayer` fails, 3 = pawn spawned and lost. Record the result in BUGS.md.

### Stage 3. Script calls (gate G-A)
Find `ProcessEvent` two ways: the slot tracer with the correct name field (arg0 must be a `Function` object), and Ghidra
(xrefs of the ASCII `RestartPlayer` name → `eventXxx` wrappers → `FindFunctionChecked` + `[vtbl+N]`). Add
`FindFunction`, a game-thread `CallScript` wrapper (like `CallExecSafe`), the `call` test command, a smoke test
(`GetNumPlayers`), and property offsets by name (calibrate `UProperty::Offset` with known offsets).
Gate: no ProcessEvent after 2 sessions → `ULocalPlayer::Exec`, direct native calls, or memory fixes.

### Stage 4. Fix B1
`Game.RestartPlayer(guestPC)` when the guest has no pawn for N s; else `SpawnDefaultPawnFor` + `Possess` near the host;
else possess a spare pool pawn + teleport. Ini `guestspawn=1`. Check 2a, 2b, 3b, 4c, 8b, 9; twin `guestspawn=0` must FAIL.

### Stage 5. B2 by root cause
Rerun 2a, 5, 2b after B1. If still crashing: find the replicated actor/class at ghidra `0x857202` (dump + register
object names), the null static `UProperty*` and why; fix by cause. Revisit `ctrlshare` and remove guards that no
longer fire. Check: host and guest alive 10+ min in 2a, 5, 2b.

### Stage 6. In-game input and screenshots
`shot <file>` from the D3D9 backbuffer (replaces `CopyFromScreen`); `input hold <key> <ms>` through the DirectInput hook
for both windows at once (spike: focus spoof in test mode if needed); `move.ps1` uses it; twins in the suite.

### Stage 7. R1 sweep
Matrix over all 13 `L*_BASE_m` maps, per-chapter table in DONE/BUGS, gameplay after the opening scene in 3a/4a/4b.
Gate G2 (owner): who the guest plays per chapter, policy for special chapters (Grimlock, flying, driving, Bruticus).

### Stage 8. Friends over a VPN (right after R1)
VPN chosen by the owner (install only with OK), firewall rule, README guide, release zip (publish only after OK),
a session with a friend; findings go to BUGS.

### Stage 9. R2: chapter 1 end to end
Combat, revive (`ServerGiveRevive`, check B6), triggers/objectives from any player, death and checkpoint restart,
chapter transition (guest follows or auto-rejoins), only the host saves, bot with teleports/`BugItX/Y/Z`.

### Stage 10. 3-5 players
Copies `E:\FoC-coop\C`..., N guests in `dev.ps1`/`matrix.ps1`, N+1 controllers with pawns (Escalation, chapters 1 and 6).

### Stage 11. Escalation (when story work waits)
Waves 1-3 and match end on 4 maps, guest character choice, 4-5 players, B3.

### Later / not now (owner)
Whole campaign (M6-M8), coop-anything write-up and PR (only with OK), B5, B6, Steam relay without VPN, installer.
