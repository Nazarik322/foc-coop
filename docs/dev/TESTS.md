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
| B14 guest keeps HUD/moves in cutscenes | `cinesync` (sync.inc TickCinematic, ClientSetCinematicMode on remote PCs) | `tools/cinetest.ps1` (L01): guest cine=1 while the host is in a cutscene, 0 after, shots | `cinetest.ps1 -Twin` (cinesync=0): guest cine stays 0, FAIL | 2026-10-20: PASS, twin FAIL |
| B18 Escalation scoring | URL options + `killrules` | matrix `-Steam` ESC_Ark `-Keep`, both shoot 2 min, TAB screenshot: score, kills, money > 0 on both | URL without GameRules: all 0 (owner's session) | 2026-10-19 PASS (kills 2/1, money 707/640) |
| B21 guest frozen after interacting | `ctrlskip` default Camera,Interact,ContextualCommand,Form | playbot story with "interact" actions over Steam: guest desync < 400 | `ctrlskip=Camera`: TnInteractStore copied (log) | 2026-10-19: log check only |
| B13 intro starts before the guest loaded | `waitguest` (Pauser until guest ready, movies over) | matrix L06: host log "waitguest: guest ready, no movie playing -> unpaused" after the guest's "movie: closed" | `waitguest=0` | 2026-10-19: PASS (twin not run yet) |
| B15 teleport instead of falling | `tpsync` (tp.inc) + rescue | `tools/tptest.ps1 -Auto` (L06): tpg away/back, guest within 800 uu of the host in both views, 5000 uu drop rescued | `-Twin` (tpsync=0): FAIL (tpg does nothing; the -Auto drop also needs tpg, so its rescue phase says nothing) | 2026-10-20 lead rerun on 27d8c61: PASS 13:37:29 (rescued after 4 s), twin FAIL 13:39:12 |
| Stage 6 unfocused input | `inputinject` (input.inc) | `tools/inputtest.ps1` (L06 -Keep, focus on another window, `input hold W 2000` on guest then host: own pawn >= 150 uu, other <= 40 uu) | `-Twin` (inputinject=0): FAIL (0 uu) | 2026-10-20 input lane: PASS [B=2904/A=0 A=2855/B=0], twin FAIL |
| B16 chapter title on the guest | `chaptersync` (story_title.inc) | `tools/titletest.ps1` (L06): white pixels of the title text x 785-880, y 470-498 (1920 wide) >= 100 on host AND guest | `-Twin` (chaptersync=0): FAIL | 2026-10-20 lane: PASS 296/303, twin FAIL |
| B4 host/join without a restart | `fakelogin` (HasCheckedForLogin set) | `tools/b4test.ps1` (L01): host and guest reach the map from a focused main menu and stay 40 s | `-Twin` (fakelogin=0): FAIL | 2026-10-20 PASS, twin FAIL |
| Lobby: guest follows the host (no restart) | lobby.inc (`lobby create/join`, Exec hook) + relay port/go | `tools/lobbytest.ps1` (menustart through the hooked Exec) | - | 2026-10-20 PASS |
| B29 lobby: the game's own CONTINUE / CHAPTER | lobby.inc solo -> session reopen, NoteTravel | `tools/lobbycontinue.ps1` (keys Up+Enter) and `-Keys "ENTER,ENTER,ENTER"` (chapter I); do not touch the windows during the run | - | 2026-10-21 PASS both (chapter: warning confirmed by hand once) |
| B31 pause menu with a guest | `exechook` default 0 | `tools/pausetest.ps1 -Guest` (host A in L01 as a session, guest B joined, Esc twice: alive, no call to address 0) | `-Ini exechook=1`: FAIL (2 null calls) | 2026-10-21 PASS / twin FAIL |
| B32/B33 Escalation names, death banner | `uniqueids`, names, UIController not shared in Escalation | `tools/esc4.ps1` (4 copies, IP join, names in the host state + TAB shot; no player actions) | - | 2026-10-21 PASS (4 names in TAB) |
| Escalation through the game's menus, 4 players over Steam (mock) | steamnet.inc chat (member id by pointer, left flag, status replay, whisper 0 -> owner), invite.inc TickZeroRegistrant | `tools/esc4menu.ps1 -LobbyWait 80` (A hosts, B-D invited, Private Match > Create Game, 80 s in the game lobby, Start; host npc >= 4; `-Probe` dumps registrants) | without TickZeroRegistrant: members quit ~50 s after entering the game lobby | 2026-10-22 PASS 4/4 (`esc4_zero2`) |
| Escalation: every map + in-game invite + no host spinner (B38, B39, B40) | gametick.inc RegisterClassObj name check, steamnet.inc TickSteamTravel lobby-host sign + TickStatusReplay, invite.inc slot 66 | `tools/esc4menu.ps1 -Copies A,B -MenuInvite -MapSteps n` for n = 0..3 (Ark, Gladiator, Temple, Iacon); host log "hosting the match on port", screenshot 3_game_lobby_A without spinner | twin: real 2-PC test (owner + friend) - friend accepts the Steam invite and loads the map | 2026-10-23 PASS all maps (mock), `esc4_final` 4/4 |
| Escalation lobby lists the same on every copy (B37, B40) | steamnet.inc status whisper/forward/replay, invite.inc TickPhantomPri, free relay port | `tools/esc4menu.ps1 -MenuInvite -LobbyWait 30` (4 copies), compare 2_party_together_* and 3_game_lobby_* of all copies (no spinner, one list, same order); `-Dump` writes PRI bytes, `sessions` lines show registrant status at +0x4d | twin: real 2-PC test | 2026-10-23 PASS 4/4 `esc4_phantom4` |
| Slot lease (team mode) | `Enter-FocSlot` etc. in dev.ps1 | `tools/slottest.ps1` (no game): 5/5 PASS | - | 2026-10-20: 5/5 PASS (lead rerun) |

## Known flaky
- ESC_Ark: the host's character pick uses real key presses; one run of 3 failed (host spawn=0). Needs in-game input.
- Any run while the remote desktop is not shown: SKIP (B7).
- ESC_Ark, second mode (2026-10-20, build 490c313): the guest connected and got the package list but stayed on
  TnPlayerControllerFrontEnd without a pawn (host spawn=3, 1 player). The rerun right after passed. Watch for repeats.
- Must-pass 2026-10-20 on 27d8c61 (tp merged, lead): all 5 PASS, Steam mock L03b PASS.
- Must-pass 2026-10-20 on 490c313 (lead): L02a, L02b, L04c, L06 PASS; ESC_Ark FAIL (above) then PASS; Steam mock
  L03b PASS (relay connected).

- Sweep 2026-10-20 (SWEEP-0.6.md): the matrix `-Look` B10 check ("host picture changed although only the guest
  moved") fails on maps that start with a cutscene (L01, L04a, sometimes L03a): both screens play the cutscene. Not
  B10; the check should skip while the host is in cinematic mode (state `cine`).
- deathtest: the "local player: no respawn without game over" expectation predates v0.5 (a real death now goes
  through the checkpoint restart). Update the test before using it as a gate.
- inputtest hung ~8 min and FAILED with guest position -1 (2026-10-20 sweep), like the earlier twin run.
- Guest barely moves on W at the start of L03a, L05, L08b, L09 (tutorial prompts / start corridor), so the
  "guest did not move" FAIL there is not a regression by itself; L08b and L09 passed in reruns.

## Known open (expected FAIL now)
- Chapter 8b: no longer "guest spawn state 0": both get a pawn in every 2026-10-20 run. Over Steam a player can
  fall through the world after the first cutscene (B27) and the host crashed once (B28).
- Chapter 9 over Steam: host crash 1 of 3 runs (B28).
