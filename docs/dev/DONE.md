# Done

What is implemented and verified, newest first. Every entry says how it was verified.
Open problems are in [BUGS.md](BUGS.md), and planned work is in [PLAN.md](PLAN.md).

## 2026-10-19 (evening): Steam (Spacewar 480) relay

- **`relay/focrelay.c` -> `FoCRelay.exe`** (32-bit, flat Steam API loaded at run time, playbook HXCoop pattern):
  `host <gamePort>` opens a friends-only lobby and gives each friend a local UDP socket to the game;
  `join <lobby|auto> 7790` finds a friend who hosts and carries the guest game's packets. Lives in
  `Binaries\foc_relay` with its own `steam_api.dll` (Steamworks.NET 2025.164.1 x86, SHA-256 fdafb7c6...eb65da).
- **In-game menu**: Host has "Through Steam" (on by default; the host restarts with `-steamrelay` and the DLL starts the
  relay once the map listens), Join has "Join a friend through Steam" (+ optional lobby id), main page shows the
  relay state, lobby, friends and an "Invite friends (Steam overlay)" button. Test command `steamjoin`.
- **Mock `steam_api.dll`** (`relay/mock`) for two players on one PC. Found on the way: delivering messages out of order
  (swap-remove) stalls the UE3 handshake; fixed (FIFO).
- Verified: relay round trip without the game 3/3; **chapter 2b through the relay (mock): PASS** (guest joined from the
  menu, pawn, both moved, 60 s alive, host relay tx 182 KB / rx 86 KB). **Real Steam, host side**: InitFlat OK while
  TFOC.exe runs on the same account, real friends-only lobby created, relay aimed at game port 7778.
  Not tested: a real second Steam account (needs a friend). Also PASS through the relay: Escalation Ark, chapter 3b.
- **Guest sometimes dropped right after loading (2a)**: the host-only fixes (ctrlshare, hostfirst, rescue) also ran on
  the client and copied objects of the old front-end controller into the new one. Now host only; 2a 3/3 PASS.

## 2026-10-19 (afternoon, stage 1 of the new plan)

- **B1 fixed in chapters 1, 2a, 3b, 4c, 5, 9** (`spawnshift`): cause found with a hardware breakpoint on the object
  allocator plus Ghidra (RestartPlayer spawns the guest's pawn into the host and the spawn fails). Matrix with
  screenshots, 120 s alive: all six PASS; without the fix all six FAIL (guest spawn state 2).
- **8b**: guest gets Grimlock after a new guard for the crash in the tail of Possess (ghidra 0x641e10); 120 s alive.

- **B10 fixed: the host's camera followed the guest's pawn** (reported by the owner). Cause: ctrlshare copied the
  host's `TnCameraStrategyManager` (PC+0x694) into the guest's controller, so both controllers drove one camera.
  Fix: `ctrlskip` defaults to `Camera`. Verified with in-game screenshots (matrix `-Look`, only the guest walks):
  fix off -> host picture diff 27, FAIL; fix on -> diff 4, PASS (chapter 2b).
- **Matrix runs after the move to the game thread**: Ark, 2a, 2b, 6 PASS (2a/2b: guest has a pawn for the first time;
  2a 180 s without the old B2 crash). 1, 3b, 4c, 5, 8b, 9: guest stuck in spawn state 2 (B1).
- **In-game screenshots** (`shot`) work on host and guest (1920x1071 BMP from the back buffer). Injected keys do not
  move the pawn yet (the game keeps no DirectInput keyboard; window messages are tried next) — the matrix uses real W.
- `docs/dev/TESTS.md`: regression list (each fix with its test and twin), run before every DLL commit.

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
