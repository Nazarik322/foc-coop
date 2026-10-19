# Bugs and open problems

Working list. Add a bug when it is found, update it when something new is learned, and move it to
[DONE.md](DONE.md) when it is fixed and verified. Technical facts behind these entries are in [NOTES.md](NOTES.md).

Status values: **open** (not worked on yet), **investigating**, **blocked** (needs something first), **workaround**.

## Critical

### B1. Story: the guest gets no pawn in chapters 2a, 2b, 3b, 4c, 8b, 9 — investigating
- Symptom: the guest connects and sees only sky (a mirrored cloud picture), with no HUD.
- Checked in 2b on 2026-10-19: this is **not** a fall out of the map. The host's rescue stat shows
  `HOST pawn (-242335,123189,23155) | GUEST nopawn`. The guest's controller has `Pawn` (PC+0x1d0) = null.
- The level holds a pool of pre-spawned `TnPlayerPawn_*` objects at almost the same spot (z≈23171), and none of them
  is possessed for the guest.
- Ruled out: the guest PRI `_SelectedCharacter.CharacterName` (PRI+0x34c) is **already set** ("Optimus Prime"),
  so `HasSelectedCharacter` is not the blocker.
- Unclear: whether `TnPlayerSpawnHelper` tracks the guest at all. The `Players` array offset was not found;
  +0x108 read 0 on the live helper even in chapter 6, where both players have pawns.
- Next step: call script functions natively on the game thread (find `UObject::ProcessEvent` and `FindFunction`),
  then try `Controller.Possess(pawn)` on a spare pool pawn, or `GameInfo.RestartPlayer(PC)`.

### B2. Story: the host crashes about 50 s after the guest joins (chapters 2a, 5, and sometimes 2b) — investigating
- It happens when the chapter's opening scene ends.
- Guards added (they emulate a return when `this` is null): ghidra `0x6cb310`, `0x660130`, `0x4e0ca0`.
  After each guard the crash moves somewhere else.
- Last seen crash: runtime `0x8C7202` (ghidra `0x857202`). This is pawn property replication (`GetOptimizedRepList`
  style: GroundSpeed / WaterSpeed / AirSpeed...). A cached `UProperty*` is null (`*(ushort*)(prop+0x36)`), then
  execution jumps to EIP=0. It is mid-function, so a simple return-guard is not possible.
- It probably comes from replicating a pawn the guest should not get, or a pawn with a different class.
  It is likely linked to B1.

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

## Minor

### B7. The guest copy sometimes crashes at startup (about 4 s) — open
- Seen once: `EXCEPTION at 0x24D247` (base-relative) and faults in the DINPUT8 scan threads. A rerun works.

### B8. RescueThread stat logs only every ~25–30 s instead of every 3 s — open
- The full GObjects scan is slow with two game instances on one PC. Diagnostics only.

### B9. `tools/matrix.ps1` screenshots fail when the guest window does not exist (Win32Exception) — open
- Harmless, but it pollutes the output.

## Not verified yet
- 3–5 players (needs a third game copy).
- Gameplay after the opening cutscene in chapters 3a, 4a, 4b.
- Movement in story chapters (chapter 6: both pawns exist, movement not tested).
- Escalation waves and match end, Escalation on Gladiator, Iacon and Temple beyond "both in match".
