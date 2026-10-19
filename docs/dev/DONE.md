# Done

What is implemented and verified, newest first. Every entry says how it was verified.
Open problems are in [BUGS.md](BUGS.md), and planned work is in [PLAN.md](PLAN.md).

## 2026-10-19 (afternoon, stage 1 of the new plan)

- **Game-thread tick** (`src/proxy_dinput8/gametick.inc`): forcechar, maxplayers, ctrlshare, hostfirst, rescue and ctrldiag
  now run on the game thread (posted message every 200 ms, one GObjects pass by class id) instead of background
  threads that wrote engine objects. Verified in chapter 6 with host + guest: tick 0.5-1.5 ms (first one ~130 ms),
  both players have a pawn, spawn state 3, guest sees its own and the host's pawn. `stat:` line every 3 s (B8 closed).
  Found on the way: the old ctrldiag code on the game thread froze the host for 21 s per tick (full GObjects walk with
  VirtualQuery); it now uses the tick's object list.
- **`foc_state.json`** on host and guest (format in NOTES.md), incl. the real map name (UWorld.URL) and the host port.
- **Spawn-helper state from memory**: TnCampaignGame+0x378 -> spawn helper, Players at +0x28 (campaign and Escalation).
- **Crash dumps**: `crashtest` wrote `foc_crash_<pid>_test0.dmp` (633 KB) and the game kept running; B7 crashes wrote
  real dumps with CRASHSTACK in ghidra VAs.
- **B7 explained** (environment: undisplayed remote desktop session), see BUGS.md.
- `tools/matrix.ps1` rewritten: checks the state files, prints `RESULT: PASS/FAIL/SKIP`, copies logs and dumps;
  `Stop-Foc` only stops test copies. **Not yet verified end to end**: the remote desktop session stopped being displayed
  during the run (all chapter starts hit B7), so the matrix PASS for Ark / chapter 1 / chapter 6 is still open.
- Written but not run yet: `shot [file]` (back-buffer BMP) and `input key <sc> <ms>` / `input mouse <dx> <dy> <ms>`
  (injected DirectInput) commands in `foc_cmd.txt` (stage 6).

## 2026-10-19

- **Movement of both players verified in Escalation Ark.** Host as Shockwave, guest as Cliffjumper.
  W/D/S held through scan codes (`tools/move.ps1`). Both pawns change position in the host log
  (`rescue: stat ... HOST pawn (...) | GUEST pawn (...)`).
- **Host-side pawn diagnostics.** The rescue stat now prints HOST/GUEST, pawn or no pawn, and xyz for every controller,
  plus a list of all `TnPlayerPawn_*` objects in the world.
- **Guard for ghidra `0x4e0ca0`** (validity check with `this` in EAX equal to null) on the host in chapter 2b.
- **Test tools:** `tools/move.ps1` (hold W/D/S in both windows), `tools/keys.ps1` (send keys, then a screenshot),
  `tools/shot.ps1`, `tools/host_pick.ps1`, `HoldKey` in `dev.ps1` (DirectInput needs scan codes, not VK codes),
  and `matrix.ps1 -Keep` (leave both games running after the run).
- **Pushed to GitHub:** `841e8ef..b5f9652` (in-game menu) and `b5f9652..31f466e` (guard, diagnostics, tools, README).
- **In-game F10 menu** (Direct3D9 overlay with Dear ImGui): pages Host, Join and About; LAN lobby list over UDP 47799;
  host name, mode, map and players in the list; unique player names per instance; the real listen port captured
  from `bind()`. Verified end to end: host from the menu on copy A, join from the lobby list on copy B.
- **Host and join restart the game into the session** (workaround for B4).
- **Escalation, 2 players:** Ark, Gladiator, Iacon and Temple. Both are in the match, and the host stays alive for 40 s and more.
- **Story, 2 players:** chapter 1 works (tutorial, rescue teleport). In chapters 6 and 8a both players are in gameplay.
  In chapters 3a, 4a and 4b both players are in the opening cutscene.
- **Guards for host crashes:** ghidra `0x6cb310` (setter with null `this`) and `0x660130` (array clear with null `this`).
- **README.md and README.ru.md** rewritten (English and Russian, with a language switch).

## Earlier (before 2026-10-19)
- dinput8 proxy: TCP net driver switch, player limit, forced `_CharactersLoaded`, tutorial guards, controller sharing,
  host-first controller order, rescue teleport. Dev launcher (`launcher/FoCCoop.exe`). Chapter 1 works for 2 players.
