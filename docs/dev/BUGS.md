# Bugs and open problems

Working list. Add a bug when it is found, update it when something new is learned, and move it to
[DONE.md](DONE.md) when it is fixed and verified. Technical facts behind these entries are in [NOTES.md](NOTES.md).

Status values: **open** (not worked on yet), **investigating**, **blocked** (needs something first), **workaround**.

## Critical

### B1. Story: the guest gets no pawn — investigating (now chapters 1, 3b, 4c, 5, 8b, 9)
- 2026-10-19 afternoon: with all engine writes on the game thread (gametick.inc) the guest gets a pawn in **2b** (4/4)
  and **2a in 2 of 3 runs** (third: spawn state 2) — so B1 is not fixed, the timing only got better. Still failing: 1 (until the host finishes the tutorial), 3b, 4c, 5, 8b, 9.
- In the failing chapters the guest stays in spawn state **2**: `TnGame.TrySpawnPlayer` (native) fails every tick and
  leaves a new unpossessed `TnPlayerPawn` at the start spot each time (47 of them after 2 min in chapter 1).
- Next: find why TrySpawnPlayer fails (Ghidra on the native, or `Possess` one of those pawns via script calls).

### B2. Story: the host crashes about 50 s after the guest joins — retest pending
- Not seen in 2a with the game-thread tick (180 s alive). Chapter 5 not checked yet (guest gets no pawn there, B1).
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
