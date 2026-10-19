# Bugs and open problems

Working list. Add a bug when it is found, update it when something new is learned, and move it to
[DONE.md](DONE.md) when it is fixed and verified. Technical facts behind these entries are in [NOTES.md](NOTES.md).

Status values: **open** (not worked on yet), **investigating**, **blocked** (needs something first), **workaround**.

## Critical

### B1. Story: the guest gets no pawn — fixed in 1, 2a, 3b, 4c, 5, 8b, 9 (`spawnshift` + guard 0x641e10)
- Cause: `GameInfo.RestartPlayer` (native, ghidra `0x7c2ee0`) spawns the guest's pawn right next to the host (the spawn
  location follows the host, not the StartSpot actor) and the spawn fails on collision every tick; the 47 "orphan"
  pawns are those failed spawns waiting for GC.
- Fix `spawnshift` (default on): a hardware breakpoint (DR1, game thread) on the SpawnActor wrapper ghidra `0x54a8f0`;
  while a guest is stuck in spawn state 2 the VEH moves the Location argument of player-pawn spawns 400-1000 uu away.
  Verified 2026-10-19: chapters 1, 2a, 3b, 4c, 5, 9 PASS (guest pawn, 120 s alive, screenshots differ per player).
- **8b (Grimlock Smash)**: with spawnshift the guest's Possess runs and then crashed the host in ghidra `FUN_00641e10`
  (checkpoint data, `this` = none) called from the tail of Possess. New VEH guard (counter 5) emulates its `ret 8`.
  Verified: guest plays Grimlock, separate camera, 120 s alive; the guest barely moved on W (check by hand).
- Chapter 9: the guest's W press moved it only 48 uu (probably a wall in the start corridor); check by hand.
- Chapter 1: the guest screen still shows the tutorial camera with dark silhouettes (B5/B6 territory).
- Tried and ruled out: `startfix` (moving the StartSpot actor does not change the spawn location), spectator bit
  (`specfix`), script state Dead (`statefix`), hand-wired possession (`possessfix`, crashes). All off by default.

### B2. Story: the host crashes about 50 s after the guest joins — not reproduced since 2026-10-19
- Not seen in 2a (180 s) and 5 (120 s, guest with pawn) since engine writes moved to the game thread. Keep watching.
- Old analysis: crash at ghidra `0x857202` in pawn property replication (null cached `UProperty*`).

## Major

### B3. Escalation: the default character (Megatron) is locked — workaround
- On the Choose Character screen, Enter on Megatron does nothing ("Default" with a lock icon).
  Press Right arrow, then Enter (Shockwave works).
- `tools/matrix.ps1` now presses Right, then Enter for the host. Players need to know about it (it is in the README).
- Not checked: which characters are unlocked without save data, and whether the guest has the same problem
  (the guest got Cliffjumper automatically).

### B4. Opening a session without a restart is rejected — workaround
- In-process `open <map>?listen` from a focused front end ends with "You must be signed in to play online"
  (Demonware session code, TCP 3074). Blocking 3074 and spoofing foreground did not help.
- Workaround: the menu relaunches the game with the URL on the command line (20–30 s).

### B5. Guest visuals: dark silhouettes in cutscenes — open (owner said: not now)

### B6. Guest starts "downed" in chapter 1 — workaround (rescue teleport)

### B10. Host camera follows the guest's pawn — fixed 2026-10-19 (see DONE, TESTS)

## Minor

### B7. A copy crashes at startup (3-8 s) at ghidra `0x64d247` — workaround (environment)
- Cause found 2026-10-19: the render thread runs an RHI init function (ghidra `FUN_0064d1f0`: BeginScene + 16×
  SetSamplerState(MAXANISOTROPY) on `this+0xc`, the `IDirect3DDevice9`) while the device is still null; the game never
  called `CreateDevice` (no `menu: game CreateDevice` line).
- It happens while the remote desktop session is not displayed (no foreground window, LogonUI on the console):
  5 of 5 starts crashed **with the old and the new DLL**; once the session was shown again, 7 of 7 starts worked.
  D3D9 still reports the adapter (`menu: D3D9 adapters=1 'NVIDIA GeForce RTX 3080' mode=1920x1080`).
- Workaround: keep the remote desktop window open (not minimized) while tests run. Not caused by the DLL.

### B9. `tools/matrix.ps1` screenshots fail when the guest window does not exist (Win32Exception) — open
- Harmless, but it pollutes the output.

## Not verified yet
- 3–5 players (needs a third game copy).
- Gameplay after the opening cutscene in chapters 3a, 4a, 4b.
- Movement in story chapters (chapter 6: both pawns exist, movement not tested).
- Escalation waves and match end, Escalation on Gladiator, Iacon and Temple beyond "both in match".
