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

### B4. Opening a session without a restart is rejected — fixed 2026-10-20 (`fakelogin`, default on)
- Cause: TnPlayerController.PlayerTick checks once per controller in any network game: "if (!HasCheckedForLogin &&
  NetMode != Standalone) { if (GetLoginStatus != LS_LoggedIn) NotifyLoginChange(); HasCheckedForLogin = true; }" ->
  back to the front end with "You must be signed in to play online" (Demonware is gone, never signed in). The same
  text replaces every kick reason in ClientWasKicked. Fix: HasCheckedForLogin is set on the controller class defaults
  and on local controllers (mirror.inc TickFakeLogin). Replacing the GetLoginStatus UFunction thunk did nothing (the
  scripts are compiled to C++). The F10 menu now hosts and joins inside the running game (InProcessTravel).
- Test: tools/b4test.ps1 (A hosts L01 from a focused main menu via `open ...?listen`, B joins via `open ip:port`):
  PASS 2026-10-20; -Twin (fakelogin=0): FAIL, the guest never reaches the map.
- History:
- In-process `open <map>?listen` from a focused front end ends with "You must be signed in to play online"
  (Demonware session code, TCP 3074). Blocking 3074 and spoofing foreground did not help.
- Workaround: the menu relaunches the game with the URL on the command line (20–30 s).

### B5. Guest visuals: dark silhouettes in cutscenes — open
- 2026-10-20: also in gameplay (L03a) and only on NPC robots; follows the guest role. Details in B25.
- Owner 2026-10-20: switching off blur/post effects in the Advanced Configurator (F1) did NOT fix it (first report
  withdrawn).
- 2026-10-20 gfxdiag (new: both sides write Binariesoc_gfx.txt every 30 s + `gfxdiag` command; tools/gfxdiff.ps1
  compares). L01 intro, both looking through CameraActor_6095, host vs guest: ShaderCache 95 vs 47 (the guest loaded
  half the shader caches), PointLightComponent 168 vs 151, LightEnvironmentComponent 287 vs 277, TnRampLightManager
  67 vs 51, DecalComponent 56 vs 6, and 8 fewer of each NPC-side helper (TnCharacterTextureLoader,
  TnCharacterMaterialManager, every Tn*MaterialOverrider). Leads: packages with shaders/lights loaded only on the
  server (spawned by Kismet / AI), light ramp managers of the NPC robots. Next: list which packages own the missing
  ShaderCache objects and lights (outer chain / package names) and load them on the guest.

### B6. Guest starts "downed" in chapter 1 — workaround (rescue teleport)

### B11. One player dies -> MISSION FAILED for both — partly fixed 2026-10-19 (`coopdeath`, `downedsec`)
- Found by the play bot in chapter 6: the host was downed, bled out (10 s) and both screens showed MISSION FAILED.
- Cause: single-player logic. On a death the game object (the host's `TnUIControllerCampaign`, and on the guest its own
  UI controller) goes to state `CampaignGameOver` through the thunk ghidra `0x7eb060` (GotoState 'CampaignGameOver').
- Fix `coopdeath` (default on): DR2 on that thunk on host and guest; while another player is up the VEH returns
  without the state change. Everybody down -> the game over goes through (MISSION FAILED, restart from checkpoint).
- Fix `downedsec` (default 0 = forever): a downed player never bleeds out and enemies cannot finish it (TnPawn
  DownedHealth 400 -> 1e9); it waits for a revive (or gets up by killing an enemy, game rule). With everybody down for
  5 s the mod lets them bleed out so the game shows MISSION FAILED.
- Verified (chapter 6, `tools/deathtest.ps1`, `tools/downtest.ps1`): no game over on host and guest when one dies;
  downed host survived 45 s; both down -> MISSION FAILED on both with "Restart From Last Checkpoint".
- Fixed 2026-10-20 (v0.5.0): (b) everybody down -> MISSION FAILED -> `autorestart` posts Enter (WM_KEYDOWN to the
  game window, no focus needed) on "Restart From Last Checkpoint" and again 1.5 s later on the "Load Checkpoint"
  confirmation; (a) a really dead player (fall/KillZ) cannot be spawned alone (the campaign game's RestartPlayer,
  vtable +0x2b0, is an empty function, ghidra 0x803d20; the native base 0x7c2ee0 called directly crashed the host), so
  15 s after the skipped game over the mod lets it through (GotoState 'CampaignGameOver' on the same UI controller) and
  the same auto restart follows. After a checkpoint restart the host's spawn collided with the guest (48 pawns in one
  place): `spawnshift` now covers the host too. Verified (chapter 6): both down -> restart, both up; guest falls ->
  restart after ~24 s, both up.
- Open: (c) revive test both ways by real play (commands: TestDowned / TestRevive work in Escalation).
- Also seen: the guest shows difficulty EASY while the host plays NORMAL (difficulty is per client).

### B12. Objectives, markers and the chapter title only on the host — mostly fixed 2026-10-19 (`sync`)
- Kismet runs only on the host; `SeqAct_TnSetCurrentObjective` and `SeqAct_TnObjectiveMarker` write the host's own
  TnObjectiveManager, nothing is replicated (`GRI.ActiveObjectives` is not replicated either).
- Fix: sync channel (`sync.inc`): the host sends the objective index and the marker list 4x per second inside the game's
  UDP connection; the guest writes them into its own objective manager. Verified in chapter 6: NEW OBJECTIVE panel and
  the objective marker on both screens.
- Open: chapter title ("CHAPTER 10 THE FINAL COUNTDOWN") is a direct Flash call on the host's HUD; markers on
  replicated actors (pawns) are not mirrored (names differ per side).

### B13. Intro starts on the host before the guest has loaded — fixed 2026-10-19 (`waitguest`, chapter 6)
- The host keeps a freshly loaded story map paused (`WorldInfo.Pauser`) until a guest is in the map, reports ready and
  no intro movie (Bink, `TransGame\Movies`) plays on either side; max `waitguest` seconds (default 300). The host sees
  "Waiting for your friend to load the level...".
- Open: movies that play during loading still run separately on each side (they are local), only the level start is
  synchronised.

### B14. Scripted scenes: the guest sees the host's cutscene from its own position — partly fixed 2026-10-20
- Fixed: Kismet put only the host's controller into cinematic mode, so during a cutscene the guest kept its HUD and
  could move. The host now mirrors its bCinematicMode (PC +0x360 bit 0x20000) to every guest with the client RPC
  ClientSetCinematicMode(on, 1, 1, 1, 1) (`cinesync`, sync.inc TickCinematic). Verified: chapter 1, guest cine=1
  during the host's cutscene with no HUD (sessions cine1/B2, cine2), back to 0 and HUD after; twin (cinesync=0) FAILs.
  First version passed the affects flags as 0 on "off" and left the guest without HUD (chapter 6, hide2) - fixed.
- Open: the guest's pawn still stands in the scene. Actor.SetHidden / TnPawn.SetHidden have no Function objects in this
  build, and writing Actor.bHidden (+0x84 bit 4; bForceNetUpdate +0x8c bit 0x80000) replicates but changes nothing on
  screen (components not updated). Needs the native AActor::SetHidden (ghidra) or another way.
- re lane 2026-10-20 (untested): call 0x54d950 (ecx = pawn+0x10, bool) for pawns with Role >= 2; for the host's
  pawn on the guest use 0x7dfef0 on the pawn and its _CurrentForm plus 0x652730 on the Mesh (NOTES.md native list).
- Open: chapter 1 opening "Use Mouse To Look Around": the guest's view is TnLookAroundCameraActor with the cutscene's
  silhouette shot, not the look-around view the host has (see B17).
- Safety fix found on the way: the GObjects fallback of FindScriptFunction took any Function of that name (chapter 6:
  a SetHidden of another class was run on the guest's pawn). It now requires the function's class (or its state's
  class) to be the object's class or a superclass (UStruct SuperStruct calibrated on Pawn -> Actor).
- Owner wants: guest camera switched to the main character or the cutscene played for both, then both placed next to
  each other without collision pushing them apart.

### B15. Streaming trigger: one player reaches a marker, the other's area unloads and he falls — mitigated
- The falling player is rescued next to the other (`rescue`) or, if he dies, the checkpoint restart (B11) brings both
  back. A clean teleport: `call pc ClientSetLocation` moves the host's own pawn; nothing tried so far moves the guest's
  pawn (ClientSetLocation on either side, ServerSetLocationAndRotation; ClientTeleport has no Function object).
- re lane 2026-10-20 (untested): host-side 0x93c9d0 AActor::SetLocation (ecx = guest pawn, FVector*), destination
  80-100 uu above the host pawn, then zero Velocity.
- 2026-10-20 tp lane (merged b9c29c7): the host's native AActor::SetLocation (0x93c9d0, ecx = the guest pawn UObject)
  moves the guest's pawn and it replicates (the guest's own state shows the new position, host/guest shots side by
  side). It returns 0 within ~280 uu of the host pawn (its cylinder), so tp.inc tries 400 uu around the host at +90,
  then 600, then above. `rescue` uses it when `tpsync=1` (default on), the old Location write stays as fallback.
  Not yet tried on a real streaming unload (only a simulated 5000 uu drop).
- Owner wants: teleport the other player next to him, or keep the area loaded and hold the first player (input lock or
  invisible wall) until the other one reaches the marker.
- Note: writing Actor.Location (+0xe4/+0x134) does not move a standing pawn (physics puts it back); a real teleport
  needs a native move (FarMoveActor / SetLocation) or a console command through the local player.

### B16. Chapter title shown to only one player — fixed 2026-10-20 (lane title, merged 051719d)
- FIX: host polls TnSeqAct_SetChapter ActivateCount +0x70 and sends chap/chapseq in the sync snapshot; the guest
  calls the HUD movie's native GFx invoke 0x452ed0 ("_global.NotifyChapterChanged", one number) - path B, because
  the guest HUD has role 0; path A (0xd558c0) is untested. ini `chaptersync` (default 1). Late joiners see the title
  once. History below.
- 2026-10-20 in progress: `chapter <n>` test command (sync.inc ShowChapterLocal) runs the guest's own
  TnSeqAct_SetChapter with that ChapterNumber via its Activated event, Targets (+0x80) temporarily = the guest's PC.
  Built, NOT yet tried with a real number (L06 has 0, 10, 10; try `chapter 10` on B and shot B). If it shows the title:
  host detects the activation (no ActivateCount property found; idea: watch for the host's own HUD title or hook
  ProcessEvent for this Function) and sends `chapter=N` over the sync channel; the guest calls ShowChapterLocal.
- Note 2026-10-20: HUD.NotifyChapterChanged has no Function object in this build (cannot be called). Next idea: run the
  level's TnSeqAct_SetChapter (ChapterNumber at +0x8c) on the guest with the guest's controller as target.
- re lane 2026-10-20: host detection by a ProcessEvent 0x728c60 detour filtering TnSeqAct_SetChapter.Activated
  (float +0x8c), or poll ActivateCount (`props ActivateCount PlayerIndex bActive`).
- 2026-10-20 title lane (not merged): host detection by polling ActivateCount +0x70 works (L06: _1591 fires with
  Targets[0] = TnPlayerPawn), `chap`/`chapseq` reach the guest, the guest's ProcessEvent of "Activated" returns ok but
  NO title appears (screenshots; host shows "CHAPTER 10 THE FINAL COUNTDOWN"). Likely the global "Activated" Function
  is SequenceOp's empty one, not SetChapter's script override: next try the object's FindFunction.
  Result: FindFunction on the instance returns null and only ONE Function "Activated" exists (owner word +0x28 = 0),
  so the script route is closed (lane/title 35f0e10, detection + sync kept). Next: guest calls
  GFxMoviePlayer.Invoke("_global.NotifyChapterChanged", [AS_Number N]) on its myHUD.HudMovie (what
  TnHUD.NotifyChapterChanged does), if Invoke has a Function object; else a native route from re.
  Result: no Function objects for Invoke / ActionScript* / SetVariable* / NotifyChapter* (`fnlist`). TnHUD.HudMovie
  +0x458 (props), guest myHUD valid. Blocked on a native GFx Invoke (re lane asked); lane/title 35f0e10 + 6f2cfda
  (`fnlist`, `hudscan` probes; titletest judges the screen: white pixels of the title, FAILs on the guest for now).
- Owner 2026-10-19: "Chapter ..." at the start shows in the UI of only one player (which one: check).

### B17. Intro animation: one player has no character animation — open (owner report 2026-10-19)
- The intro now plays on host and guest at the same time, but on one of them (probably the host) the player
  character's animation is missing. Check with screenshots of both at the level start.
- re lane 2026-10-20: TnPawn._MatineeAnimationEnabled is not replicated and BeginAnimControl exits if it is false;
  check it (props) on both sides during the intro, write 1 during the cutscene if false.

### B18. Escalation: TAB scoreboard does not count, no points/money — fixed 2026-10-19 (mock Steam, 2 copies)
- Owner report after a real Steam session. Cause: the game's own lobby starts Escalation with options from
  TnOnlineGameSettingsSV: `?GameModeTag=SV?GameTeamStatus=2?GameRules=TransGame.TnGameRules_CompetitiveScoring,
  TransGame.TnGameRules_TrackKills` (Rules array read from the package bytes). Our URL had only `?Game=...`, so
  GameInfo.InitGame added no rules: no kill scoring (CompetitiveScoring -> PRI score events -> AddScore -> money).
- A comma in a command-line map URL sends the game back to the front end (any option, also restored through a
  GetCommandLine hook from %2C); the in-game `open` bounces (B4). So the URL carries `?GameModeTag=SV?GameTeamStatus=2
  ?GameRules=TransGame.TnGameRules_CompetitiveScoring` (F10 Host kEscalationOptions, tools/matrix.ps1) and the host DLL
  spawns TnGameRules_TrackKills natively (`killrules`, sync.inc TickEscalationRules: spawn wrapper ghidra 0x54a8f0,
  chained after GameInfo+0x280 -> +0x1d0 NextGameRules).
- Verified: TAB shows score, kills, deaths, revives, money for host and guest; store purchase prompt works.

### B19. Escalation: a player can carry more weapons than 1 primary + 1 secondary/heavy — open, not reproduced yet
- tools/esctest.ps1 checks transform, ability, weapon switch, downed/revive, money, lives by commands, but not the
  weapon slots: that needs buying at an armory (Interact next to it). Next: buy two weapons with poke'd money and
  count the weapons in the inventory.
- Weapon slots are enforced by TnPickupManager.DropRobotWeapon on the server (tosses the weapon in the slot of the
  new pickup when TnSurvivalGame.RespectsWeaponTypes). Not reproduced yet; check after the B18 fix. Note: ctrlshare
  gives the guest the host's TnUIControllerSurvival, ProfileSettings and online objects (PickupManager is not shared).

### B20. Steam join was hard (restarts, friend did not see the lobby) — fixed 2026-10-19 (v0.4.1)
- v0.4.0 bug: `FoCRelay list` crashed on real Steam (strcmp on the missing argv[2] in the main loop; with the mock the
  search result arrived before that line). Fixed; checked with the owner's real Steam account in a scratch folder:
  the lobby list shows the hosted game. The Steam invite button only opened the overlay (the invite dialog belongs to
  the process that owns the lobby, the windowless relay): removed.
- F10 menu: the game gets no keyboard/mouse input while it is open (window messages swallowed in GetMsgProc, the
  DirectInput hooks already returned nothing) and the screen behind it is darkened.
- Lobbies were friends-only and the guest relay looked only for friends whose Steam "game played" was 480 with a
  lobby; the game itself showed as 213120 until v0.3.2.
- Fix: host lobby public, tagged foccoop=1 with name/mode/map/max; `FoCRelay list` searches Steam (string filter,
  worldwide) every 3 s and the F10 Join page lists the games (friends first) with a Join button; IP/LAN removed from
  the menu (code kept). Relay logs to foc_relay\relay.log; mod logs of the last 10 runs in Binaries\foc_logs.

### B21. Guest frozen on the host after using an interactive object, then dropped — fixed 2026-10-19 (`ctrlskip`)
- Owner's Steam session (chapter 6): the guest used something at ~63 s; ctrlshare copied the host's TnInteractStore /
  TnContextualCommandUseInteract (and later TnTankForm) into the guest's controller every 0.2 s; the guest's pawn froze
  on the host (client kept moving: 25000 uu apart) and the guest fell back to the menu ~60 s later.
- Fix: ctrlskip is a comma list, default `Camera,Interact,ContextualCommand,Form`.

### B22. Both players had the same name — fixed 2026-10-20 (`setnames`)
- Each game takes the player name from its own profile ("Default" without Demonware). Now the local controller runs
  `SetName <name>` (exec -> ServerChangeName) through ScriptConsoleExec: the guest uses the ?Name= of its join URL
  (Steam name), the host its Steam name from relay.status. Checked: host 'mock_85533', guest 'mock_87376' on both.

### B23. Escalation: the guest could not interact with many things — fix built 2026-10-20, check in a real session
- Owner's session log: ctrlshare also gave the guest the host's TnPlayerTargeting (+0x5f8, used to find what to
  interact with) and a TnPlayerPawnSurvival reference (+0x720). ctrlshare is now an allowlist (`ctrlallow`, default
  OnlineSubsystem,OnlinePlayerData,ProfileSettings,UIController). Buying at an armory could not be reproduced by
  commands (the pawn has to stand at the armory).

### B24. Rejoin leaves stale controllers on the host — open
- Owner's 41-minute Escalation session: npc=4 for 2 players at the end, two controllers shared one PRI
  (pridiag PC0 and PC2 -> the same PRI). Probably old controllers of a guest who reconnected. Check what the game does
  with them (scoreboard rows, spawn helper, sync peers).

### B25. Guest has blurry / broken textures, host fine — explained 2026-10-20
- The owner's game runs the Advanced Configurator (texture pool 1024 MB, TextureFix4K, AF 16x); the friend's did not
  (game defaults from 2012). The installer now ticks the configurator by default (it was checked with co-op in ch6 and
  Escalation). If the friend still sees broken textures with it, compare both ReShade/configurator settings.
- REOPENED 2026-10-20 (owner: "textures still a problem", suspects server -> client data / animations).
  Lead finding: TnPlayerPawn.ApplyCharacter (simulated) runs `_CharacterTextureLoader.ResetAll()` +
  `UpdateTransformer(_Blueprint, true)` (and HealthMaterialRamper.AddDefragMesh) ONLY `if (IsLocallyControlled())`.
  So (a) the other player's robot never gets the character texture loader on either side (single player never needed
  it), and (b) on the guest its own pawn may miss it too if ApplyCharacter ran before the Controller replicated.
  First look (tp shots L06): the host's own Megatron is sharp with red trim, the host's Megatron seen by the guest
  looks flatter/lower-res. Next: close-up shots of own and remote pawn on both sides, then run UpdateTransformer for
  every TnPlayerPawn on each side (Function object or native from re).
- 2026-10-20 owner: "on the client some shaders / reflections seem missing, textures dark". FOUND (test setup):
  copy B's `Binaries\reshade-shaders` had no `Shaders` folder, so ReShade on the guest window loaded no effects
  (A's ReShade.log compiles Curves/DPX/Vibrance/AdaptiveSharpen/Levels, B's compiles nothing): the guest picture had
  no sharpening, colour curves or vibrance. Fixed by copying A's Shaders to B. The Steam install and build\reshade.zip
  (installer) have the shaders. The "first look" above was taken with this difference, so it proves nothing.
  Checked equal on A/B: TransGame\Config\PC, TFOCAdvancedConfigurator.ini, FoCCoop_ReShade_Preset.ini.
  Still to check separately: (1) profile settings on the guest (exec TnPlayerController.UpdateLocalCacheOfProfileSettings
  applies PCSettingManager texture quality; does it run on a network client?), (2) the character texture loader for
  remote pawns (above), (3) host vs guest shots with ReShade off on both (pure engine difference).
- 2026-10-20 sweep (SWEEP-0.6.md): the guest sees **NPC robots** black (emissive parts glow) in L03a gameplay and the
  L04a cutscene, the host sees them lit. Role swap (host = B, guest = A) moves the black robots to A: it follows the
  network role, not the copy. Still black 45 s later. Black + emissive = no light / material parameter, not texture
  mips. Level Kismet does not use SeqAct_AddTextureStreamingViewInfo / SeqAct_FlushTextureStreaming (none of 458
  story packages), and NPCs (TnHtnAiPawn) have no character texture loader. Next: dump a dark NPC on both sides
  (light environment, lighting channels, material instance scalars) and diff. This is the same thing as B5.

### B26. Host frame hitches since v0.6.0 (owner: "very strong lags on the host") — fix built 2026-10-20
- story_title.inc ChapterSnap runs every 250 ms on the host (sync snapshot, also in Escalation) and did two full
  GObjects passes each time (ClassIdByName without a cache, then ChapterActions). DLL tick max rose from 70-140 ms
  (289671b) to 220-265 ms on every map after the title merge (051719d).
- Fix: class id cached, one full pass per map, then 20000 objects per call. Check: tickMsMax back to the old range,
  titletest PASS / twin FAIL (DONE).
- Measured by EndScene counts: host 41-43 FPS on the old build, 60 with the fix (L06, ESC_Ark). tickMsMax itself is
  a one-time load spike and did not change. The steady DLL tick was already small (avg 1.8 ms).
- Owner 2026-10-20: the lags he saw came from different FPS limits (the FPS-unlock mod; the test copies ran 200 FPS
  on one window): now 60 FPS everywhere. The 41-43 vs 60 FPS numbers above were taken with those mixed limits, so
  the gain from the fix is not proven; recheck host FPS with 60 on both.
- titletest on the fixed build (sweep060\lagfix\title): logs OK (host chapter 10, guest shows 10) but both
  screenshot scores 0 - not explained yet, rerun.

### B27. Chapter 8b: a player falls through the world after the first cutscene — open
- Steam sweep run 1: after the cutscene at 74-79 s the host's pawn fell (Z -63878 -> -109248), died; coopdeath
  cleared the pawn for a respawn; the test ended before the checkpoint restart. Rerun 1: the guest fell
  (Z -119282) and then the host crashed (B28). Rerun 2 and LAN: no fall. Likely B15 (streaming unload under one
  player); `rescue`/`tpsync` only move a falling *guest* to the host, a falling host is never rescued.

### B28. Host crashes in 8b and 9 (Steam sweep) — open, 1 of 3 runs each
- L09 Steam at 108 s: ghidra `0xae6773` reads null+0x20; object on the stack `TnInteractActionTogglePause`.
  Dump `sweep060\steam\L09_FinalFight_BASE_m_A_foc_crash_50056_crash1.dmp`. Two reruns (Steam) and LAN: no crash.
- L08b Steam rerun 1 at 75 s, right after the guest fell through the world (B27): ghidra `0xb560b0` (the HUD role
  gate, ecx = 0x10, object null); stack has TnGrabManager, TnRobotAnimator, both TnPlayerPawns.
  Dump `sweep060\rerun\l08b_1\..._crash1.dmp`.

### B29. Lobby: the guest does not follow when the host uses the game's own CONTINUE - fixed 2026-10-21 (aa61485)
- Fix: the lobby host watches the GameInfo object: a solo game (TnCampaignGameSolo, the menu's CONTINUE / CHAPTER) is
  reopened as a session with the same URL options (?LoadCheckpoint kept); a session game with its listen port sends
  "port" and "go". MapName knows the map of travels we start (NoteTravel) while that GameInfo lives.
- Test: tools/lobbycontinue.ps1 (key presses Up+Enter in the main menu): PASS 2026-10-21 - the guest followed within
  30 s (Steam mock relay, 2 players), Esc on the host keeps the game. The CHAPTER path (-Keys "ENTER,ENTER,ENTER":
  CAMPAIGN > chapter I > overwrite warning Continue) is not run yet: the games stopped drawing frames (remote desktop
  not shown) - rerun it.
- CONTINUE loads TnCampaignGameSolo with ?LoadCheckpoint, not through the console Exec, so the lobby reopens the map
  as a session; after that MapName returns the old "UI_FrontEnd_m" (many UWorld objects linger) and the relay never
  gets "port"/"go". Details and next steps: PLAN.md "Resume point 2026-10-20 late night". Test: tools/lobbycontinue.ps1.

### B30. Host hitches every few seconds with the 2026-10-20 trace/mirror build - mostly fixed
- Full GObjects passes with a name lookup per object (fakelogin class defaults every 5 s, netspeed every 5 s, texsync
  every 10 s, property lookups) froze the host 1-4 s. Now: class-filtered and once per map; "slow: <part> took N ms"
  logs every tick part over 25 ms. Remaining: ~1 s + 1.5 s once at startup.

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

## Not verified yet
- 3–5 players (needs a third game copy).
- Gameplay after the opening cutscene in chapters 3a, 4a, 4b.
- Movement in story chapters (chapter 6: both pawns exist, movement not tested).
- Escalation waves and match end, Escalation on Gladiator, Iacon and Temple beyond "both in match".
