# Done

What is implemented and verified, newest first. Every entry says how it was verified.
Open problems are in [BUGS.md](BUGS.md), and planned work is in [PLAN.md](PLAN.md).

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
