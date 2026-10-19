# Plan

**Start of every session: read this file, then [BUGS.md](BUGS.md).** Update it when the plan changes.

## Next step (where to resume)
Item 1 below: find `UObject::ProcessEvent` in ghidra and add a game-thread wrapper to call script functions.

## Plan in priority order
Planned work. Link the bug it addresses ([BUGS.md](BUGS.md)), and move items to [DONE.md](DONE.md)
when they are finished and verified.

1. **Native script calls on the game thread** (needed for B1, and useful everywhere).
   Find `UObject::ProcessEvent` (vtable slot of UObject, or by xrefs to the "ProcessEvent" / "Script call stack" strings)
   and `UObject::FindFunction`. Then wrap them like `CallExecSafe` and run them through the WH_GETMESSAGE game-thread queue.
2. **B1: give the guest a pawn in story chapters.** With item 1, try `GameInfo.RestartPlayer(guestPC)`; otherwise
   `guestPC.Possess(sparePoolPawn, false)`. Verify in 2b, then 2a, 3b, 4c, 8b and 9 (`tools/matrix.ps1`), and check
   that the guest pawn moves (`tools/move.ps1`).
3. **B2: host crash after the guest joins.** Check whether it disappears once B1 is fixed. If not, find which actor's
   replication hits the null `UProperty` at ghidra `0x857202`.
4. **Movement checks in story chapters** that already work (1, 6, 8a) and gameplay after the cutscene in 3a, 4a and 4b.
5. **Escalation:** which characters are unlocked, guest character choice, waves and match end on all 4 maps.
6. **3–5 players:** create copy C (`E:\FoC-coop\C`, `instance=C`) and extend `matrix.ps1`.
7. Later (owner: not now): guest visuals (B5), the downed start (B6), internet play without a VPN, an installer.
