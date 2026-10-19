# Regression tests

Every fixed bug gets a line here: the test that proves it works and its twin (the same test with the fix switched off
must FAIL). **Before every commit that changes the DLL, run the "must pass" set**; after a fix, add its line.
Needs a displayed desktop (BUGS B7). Results go to DONE.md.

## Must pass (about 25 minutes)
```powershell
. C:\GitRepo\foc-coop\tools\dev.ps1; Build-Deploy
C:\GitRepo\foc-coop\tools\matrix.ps1 -Look -AliveSec 60 -Maps ESC_Ark_BASE_m, L02a_DefendTheArk_BASE_m, L02b_DestroyAllMortars_BASE_m, L04c_FlyingFortress_BASE_m, L06_Megatron_BASE_m
```
Expected: 5 x `RESULT: PASS`. Plus the Steam relay: `matrix.ps1 -Steam -Look -Maps L03b_CutAndRun_BASE_m` (mock). SKIP means the remote desktop window was not focused (B7) - rerun.

## Fixes and their tests
| Bug / feature | Fix (ini key) | Test | Twin (must FAIL) | Verified |
|---|---|---|---|---|
| B10 host camera follows the guest's pawn | ctrlshare skips camera objects (`ctrlskip`, default `Camera`) | matrix `-Look` on 2b: hostImgDiff small while only the guest walks | `ctrlskip=` (empty) in A's ini: FAIL "host camera follows the guest (B10)" | 2026-10-19: twin FAIL (diff 27), fix PASS (diff 4) |
| B1 guest without pawn in 2a, 2b | game-thread tick (all engine writes on the game thread) | matrix on 2a, 2b: guest spawn=3, pawn | none (old DLL: `launcher/payload/dinput8.dll` from 10:40) | 2026-10-19: 2b PASS 4/4; **2a PASS 2/3, one run FAIL (guest spawn=2): not fixed, flaky** |
| B1 guest without pawn (spawn collides with the host) | `spawnshift` (default on) | matrix on 1, 2a, 3b, 4c, 5, 9: guest spawn=3 | `spawnshift=0`: guest stays in spawn state 2 (all six chapters, run 14:14-14:23) | 2026-10-19: all six PASS, 4c twice |
| B1 chapter 8b: host crash in the guest's Possess | guard 0x641e10 (`tutguard`) | matrix 8b `-AliveSec 120`: PASS except movement | `tutguard=0` or old build: host crash ~20 s (dump 2026-10-19 14:47) | 2026-10-19: guest pawn, 120 s alive |
| Steam relay join (mock) | FoCRelay + menu `steamjoin` | `matrix.ps1 -Steam -Look -Maps L02b_DestroyAllMortars_BASE_m` (Deploy-Relay puts the mock in A/B) | mock with swap-remove delivery: guest stuck loading | 2026-10-19 PASS |
| Guest dropped ~3 s after loading (2a, flaky) | ctrlshare/hostfirst/rescue run only on the host (`t.ngame > 0`) | matrix 2a x3 | old build: 2 of 6 runs FAIL (guest back in the main menu) | 2026-10-19: 3/3 PASS |
| B2 host crash ~50 s after join in 2a | same as above | matrix 2a with `-AliveSec 180` | old DLL | 2026-10-19: PASS 180 s (chapter 5 not reached yet: guest has no pawn there) |
| Escalation join and spawn | tcpdriver, forcechar, maxplayers | matrix ESC_Ark | `forcechar=0`: stuck at "Loading Characters" | 2026-10-19 PASS |
| Chapter 6 both in game | ctrlshare, tutguard | matrix L06 | - | 2026-10-19 PASS |
| B11 one death ends the mission | `coopdeath` (DR2 on game-over thunk 0x7eb060) | `tools/deathtest.ps1` (L06): log "coopdeath: ... SKIPPED", host screen keeps playing, guest screen no MISSION FAILED | `-Twin` (coopdeath=0): MISSION FAILED | 2026-10-19: skip works on host and guest; respawn of a really dead guest FAILS (open) |
| B11 downed players bleed out in 10 s | `downedsec` (DownedHealth 1e9) | `tools/downtest.ps1` (L06): downed host alive after 45 s; both down -> MISSION FAILED on both | `-Twin` | 2026-10-19: PASS by screenshots (twin not run yet) |
| B12 objective + marker only on the host | `sync` (sync.inc) | matrix L06 `-Keep`, `shot` both: NEW OBJECTIVE and marker on both; guest log "sync: objective -> 0" | `sync=0` in both inis: guest has no objective | 2026-10-19: PASS (twin not run yet) |
| Escalation by commands | oexec/poke test commands | `tools/esctest.ps1` (ESC_Ark, Steam mock): transform, ability, weapon switch, downed+revive host and guest, money, team lives / respawn wave; TAB screenshots | - | 2026-10-19: all PASS (weapon slots not covered) |
| B22 same player names | `setnames` | matrix `-Steam` ESC_Ark: state file `name` of host and guest differ and match their Steam names | `setnames=0`: guest 'Default' | 2026-10-20 PASS |
| B18 Escalation scoring | URL options + `killrules` | matrix `-Steam` ESC_Ark `-Keep`, both shoot 2 min, TAB screenshot: score, kills, money > 0 on both | URL without GameRules: all 0 (owner's session) | 2026-10-19 PASS (kills 2/1, money 707/640) |
| B21 guest frozen after interacting | `ctrlskip` default Camera,Interact,ContextualCommand,Form | playbot story with "interact" actions over Steam: guest desync < 400 | `ctrlskip=Camera`: TnInteractStore copied (log) | 2026-10-19: log check only |
| B13 intro starts before the guest loaded | `waitguest` (Pauser until guest ready, movies over) | matrix L06: host log "waitguest: guest ready, no movie playing -> unpaused" after the guest's "movie: closed" | `waitguest=0` | 2026-10-19: PASS (twin not run yet) |

## Known flaky
- ESC_Ark: the host's character pick uses real key presses; one run of 3 failed (host spawn=0). Needs in-game input.
- Any run while the remote desktop is not shown: SKIP (B7).

## Known open (expected FAIL now)
- Chapter 8b: guest spawn state 0.
