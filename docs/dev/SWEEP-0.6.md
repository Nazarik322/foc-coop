# Sweep of v0.6.0-dev on every map (2026-10-20)

Build: main 051719d code (docs-only commit c3f0e9f on top), deployed to test copies A (host) and B (guest), mock
`steam_api` for the Steam rows. Tool: `tools/matrix.ps1 -Look -AliveSec 60` per map (both players get a pawn, both
games alive 60 s, then only the guest holds W for 3 s; screenshots before/after on both). Artifacts:
`E:\FoC-coop\sessions\sweep060\{lan,steam,title,cine,tp,death,down,input,esc}`, 2x2 shot grids in `grid_lan`.
Script: the job's `sweep.ps1` (Build-Deploy, 17 maps LAN, 17 maps Steam, then the feature tests).

## Map matrix

| Map | LAN | Steam (mock) | What the shots show |
|---|---|---|---|
| L01 The Exodus | FAIL "B10" | FAIL "B10" | Not B10: the opening cutscene runs on both, host and guest frames differ a little in time. Test artifact (the B10 check assumes gameplay). |
| L02a Defend the Ark | PASS | PASS | |
| L02b Destroy All Mortars | PASS | PASS | |
| L03a Eye of the Storm | FAIL guest 0 uu | FAIL "B10" | Gameplay ("Press Space To Jump" on the host). **Guest sees the allied NPC robots as black silhouettes** (only emissive parts glow), host sees them lit. See B25/B5 below. The guest did not move on W (tutorial spot / camera, check by hand). |
| L03b Cut and Run | PASS | PASS | |
| L04a Bridge Assault | FAIL "B10" | FAIL "B10" | Intro cutscene (Decepticons). Host lit, **guest sees the same robots as black silhouettes**; the two screens show different camera shots at the same moment (cutscene not in step). |
| L04b Stop the Transport | PASS | PASS | Both screens identical before and after (host hint "Press Q to use grapple"), although the guest moved ~4300 uu: the guest's camera does not follow its pawn here (check). |
| L04c Flying Fortress | PASS | PASS | |
| L05 Soundwave | FAIL guest 0 uu | PASS | LAN: tutorial ("Press F to Change Form"), guest view stuck behind a large robot, no movement. |
| L06 Megatron | PASS | PASS | |
| L08a Starscream | PASS | PASS | |
| L08b Grimlock Smash | FAIL guest 19 uu | FAIL host lost its pawn | LAN: both Grimlock, guest barely moves (known since B1). Steam: **after the 74-79 s cutscene the host's pawn falls through the world** (Z -63878 -> -109248), dies, `coopdeath` respawn pending; host not rescued (B27). |
| L09 Final Fight | FAIL guest 47 uu | FAIL **host crashed** at 108 s | Steam: **host crash** ghidra `0xae6773` (read of null+0x20), object on stack `TnInteractActionTogglePause` (B28). LAN: guest moves only 47 uu (wall in the start corridor, known). |
| ESC Ark | PASS | PASS | |
| ESC Gladiator | PASS | PASS | |
| ESC Iacon | PASS | PASS | |
| ESC Temple | PASS | PASS | |

LAN 11/17 PASS, Steam 12/17 PASS. Real problems: L08b host fall (Steam), L09 host crash (Steam), dark NPCs on the
guest (L03a, L04a). The other FAILs are test artifacts (cutscene at the start breaks the B10 image check) or the known
"guest barely moves at the start" spots.

Reruns (`sweep060\rerun`, AliveSec 90): L09 Steam FAIL (guest 0 uu, no crash) then PASS; L08b Steam: run 1 the
**guest** fell through the world (Z -119282) and the host crashed at ghidra `0xb560b0` (B28), run 2 FAIL (guest 66 uu,
no fall); LAN: L09 FAIL (guest 153 uu, tick max 674 ms), L08b PASS. So both crashes and the 8b fall are intermittent.

`tickMsMax` was 220-265 ms on **every** map in this sweep, against 70-140 ms on 289671b (before the title merge).
Cause found: B26 (host hitches every 250 ms).

## Feature tests (L06 unless noted)

| Test | Result | Note |
|---|---|---|
| titletest (B16) | PASS 296/304 | chapter title on host and guest |
| cinetest (B14, L01) | PASS | guest follows cinematic mode; guest pawn not hidden during the cutscene (B14 hide is not merged) |
| tptest -Auto (B15) | PASS | tpg away/back 402 uu, 5000 uu drop rescued at once |
| deathtest (B11) | FAIL "local player: no respawn without game over" | Remote death: skipped + respawn. Local (host) death: respawned (respawns=2) but via the game over path. The test predates v0.5 (real deaths now go through a checkpoint restart): expectation outdated, update the test. |
| downtest (B11) | PASS | downed host waits for a revive; all down -> check 3_all_down shots |
| inputtest (stage 6) | FAIL, hung ~8 min | guest position unreadable (-1) during the hold; same hang as the earlier twin run (TESTS Known flaky) |
| esctest (Escalation, Steam) | all PASS | transform, downed+revive host and guest, processes alive, no unguarded exceptions |

## Dark robots on the guest (B25 / B5): what the sweep proved

- Role swap test (L03a, host = copy B, guest = copy A, `sweep060\swap\grid.jpg`): the black silhouettes moved with
  the **guest role** to copy A. Not a copy/ReShade difference (ReShade shaders and preset are identical on A and B).
- 45 s later the same NPC is still black on the guest: not slow texture streaming.
- Black with glowing emissive parts means diffuse x light = 0. A texture that is not streamed in shows its lowest mip
  (a blurry average colour, red/grey for these robots), not black. So this is **lighting or a material parameter on
  network-simulated NPCs**, not textures.
- Texture-streaming hypothesis (owner): the exe has High Moon's own streaming manager
  (`HmAddViewInfoToTextureStreamingManager`, `HmUpdateTextureStreaming`) and Kismet actions
  `SeqAct_AddTextureStreamingViewInfo` / `SeqAct_FlushTextureStreaming` (server-only, would never reach the guest).
  Checked: none of the 458 story level packages uses these actions (decompressed, byte search), so level Kismet does
  not feed streaming points. The character texture loader (TnPlayerPawn.ApplyCharacter) exists only on player pawns;
  NPCs (TnHtnAiPawn) never use it. Blurry *player* robots can still be the loader (lane/gfx `charloader`), but the
  black NPCs are a different bug.
- Next: dump one dark NPC on host and guest (mesh component LightEnvironment `bEnabled`/lighting channels, material
  instance scalar parameters) and diff; first suspects: a spawn-in/dissolve material parameter set by server-side
  code, a DynamicLightEnvironment that a client never updates, cinematic lighting channels set by Kismet on the host.

## Chapter 1 mirror test (2026-10-20 evening, uncommitted build on c3f0e9f)

Owner goal: the guest repeats everything the script does to the host (animations, lying, scripted moves); only
walking/running/jumping, ammo, health and the combat camera stay separate. Test: `tools/l01test.ps1` (L01, 180 s,
host+guest shots every ~10 s, `sessions\mirror\l01b\grid_*.jpg`, host left / guest right). Windows now open side by
side (dev.ps1 `Set-FocWindowSlot`: A left, B right).

| Change | Result |
|---|---|
| `tpfollow` (host pawn jumps > 2500 uu by script -> guests follow after the cutscene) | **works**: at 101 s the script moved the host 4791 uu, the guest stayed; at 119 s (cutscene over) it was moved next to the host (410 uu). Twice, same result. |
| rescue for any player after 7 s of falling (no height limit) | built, not exercised in L01 (nobody fell) |
| `mirror` allplayers (SeqVar_Player.bAllPlayers on every Player variable) | **not applied**: 0 variables changed. The property lookup found only one `bAllPlayers` (+0x38) and could not confirm its owner class (Outer check fails for every property, also for TnPawn) - next: find UObject Outer offset / dump one SeqVar_Player with `dump SeqVar_Player`. |
| `mirror` matanim (host's _MatineeAnimationEnabled to guests) | offset +0x5f8 mask 0x2 picked (largest offset, unconfirmed); no visible effect in L01 |
| B26 title scan fix | no hitch lines; FPS check needs a rerun with 60 FPS on both (owner capped FPS) |

What the L01 shots show (grid_02): the host is lying and carried by the allies in the scripted intro ("Use Mouse To
Look Around"); the guest stands in normal gameplay with HUD and the prompt "Hold E to Revive" (it sees the host as a
downed player), the allied robots are black silhouettes on the guest (B5). Exactly the desync the owner describes.

Fixed today: guest left behind after a scripted host move (tpfollow); rescue switched off below z -20000 (8b).
New/confirmed: L01 intro state not mirrored (B6/B17 family), B5 still open (blur/post effects are not the cause,
owner), titletest screenshot score 0 on the fixed build (logs OK), L01 tick max 2.3 s at load (one-time).
