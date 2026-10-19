# Regression tests

Every fixed bug gets a line here: the test that proves it works and its twin (the same test with the fix switched off
must FAIL). **Before every commit that changes the DLL, run the "must pass" set**; after a fix, add its line.
Needs a displayed desktop (BUGS B7). Results go to DONE.md.

## Must pass (about 25 minutes)
```powershell
. C:\GitRepo\foc-coop\tools\dev.ps1; Build-Deploy
C:\GitRepo\foc-coop\tools\matrix.ps1 -Look -AliveSec 60 -Maps ESC_Ark_BASE_m, L02a_DefendTheArk_BASE_m, L02b_DestroyAllMortars_BASE_m, L04c_FlyingFortress_BASE_m, L06_Megatron_BASE_m
```
Expected: 5 x `RESULT: PASS`. SKIP means the remote desktop window was not focused (B7) - rerun.

## Fixes and their tests
| Bug / feature | Fix (ini key) | Test | Twin (must FAIL) | Verified |
|---|---|---|---|---|
| B10 host camera follows the guest's pawn | ctrlshare skips camera objects (`ctrlskip`, default `Camera`) | matrix `-Look` on 2b: hostImgDiff small while only the guest walks | `ctrlskip=` (empty) in A's ini: FAIL "host camera follows the guest (B10)" | 2026-10-19: twin FAIL (diff 27), fix PASS (diff 4) |
| B1 guest without pawn in 2a, 2b | game-thread tick (all engine writes on the game thread) | matrix on 2a, 2b: guest spawn=3, pawn | none (old DLL: `launcher/payload/dinput8.dll` from 10:40) | 2026-10-19: 2b PASS 4/4; **2a PASS 2/3, one run FAIL (guest spawn=2): not fixed, flaky** |
| B1 guest without pawn (spawn collides with the host) | `spawnshift` (default on) | matrix on 1, 2a, 3b, 4c, 5, 9: guest spawn=3 | `spawnshift=0`: guest stays in spawn state 2 (all six chapters, run 14:14-14:23) | 2026-10-19: all six PASS, 4c twice |
| B2 host crash ~50 s after join in 2a | same as above | matrix 2a with `-AliveSec 180` | old DLL | 2026-10-19: PASS 180 s (chapter 5 not reached yet: guest has no pawn there) |
| Escalation join and spawn | tcpdriver, forcechar, maxplayers | matrix ESC_Ark | `forcechar=0`: stuck at "Loading Characters" | 2026-10-19 PASS |
| Chapter 6 both in game | ctrlshare, tutguard | matrix L06 | - | 2026-10-19 PASS |

## Known flaky
- ESC_Ark: the host's character pick uses real key presses; one run of 3 failed (host spawn=0). Needs in-game input.
- Any run while the remote desktop is not shown: SKIP (B7).

## Known open (expected FAIL now)
- Chapter 8b: guest spawn state 0.
