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

### B5. Guest visuals: dark silhouettes in cutscenes — FIXED 2026-10-25 (mock, `lightfix`)
- Cause (owner's lead "only characters in animations are dark"): scene robots (TnMeshSetActorMAT) and player pawns are
  a master SkeletalMeshComponent (RB_Iconic_SKEL) plus part meshes (RB_<Char>_CHARSKEL_<Part>, ParentAnimComponent =
  master). `meshinfo TnMeshSetActorMAT` (L01 intro): on the host every part uses the master's LightEnvironmentComponent
  (mesh +0x100); on the guest the parts of most robots pointed at one other light environment (e.g. master _691, parts
  _15655) that nothing lights -> black robots with only the emissive bits (eyes, Autobot logo). Bumblebee's parts shared
  the master's on both and were lit.
- Fix (scene.inc TickLightFix, ini `lightfix`, default 1): every 2 s on story maps a part whose light environment differs
  from its master's gets the master's + bNeedsReattach. Guest L01: 107 parts at the start, 36 at the next scene; host 0.
  Test command `lightfix`.
- Verified: `tools/lighttest.ps1` PASS (guest 3-7 % black pixels, host 0.5-2.7 %), twin `-Twin` FAIL (guest 37-67 %);
  screenshots L01 (4 copies through f10visual, all lit), L03a, L08a (sessions/charvis: lfgrid, c3_grid, c8_grid).
  Leftover: a few small black rectangles in one L01 guest shot (not robots; not investigated).
- Owner 2026-10-25 after the fix: "some parts are still black, different from the original, animations only half fixed".
  Checked on L01 (lightdump command): the guest's mesh components, parents, light environments (owner = the actor,
  same state words as on the host), material counts and the Tn* character helpers (ramp lights, material managers,
  overriders, texture loaders) are now the same as on the host; the guest's robots still look flatter. Open - next:
  per-material texture parameters (MIC) of the parts and the ramp light targets.
- 2026-10-25 afternoon (owner: "as if some lighting / metallic / reflection is missing"), L01 intro, host A vs guest B:
  - Same on both: scene lights (lightsdump: state, brightness, colour), every material parameter of the robots'
    materials (micdump; scalar MetallicOffset/Energon..., vectors; no texture parameters - textures come from the base
    materials), mesh parents and light environments, character helper objects; TnRampLightManager = death / cloak
    effect lights only. Forcing bForceMipStreaming on the guest (forcemips) changed nothing visible.
  - Different: (1) the host's robots get a warm golden key light, the guest's look neutral grey-blue (crop
    sessions/fm1/crop.png); (2) lights' AffectedLightEnvironments (+0x104/+0x108): on the guest one player pawn's
    light environment was missing from 10-14 lights during the intro, registered again later (transient);
    reattaching all light environments (reattach) made it worse (the dominant directional light lost all 83 links);
    (3) black stretched polygons on the guest = a CHARSKEL Chest part of a scene robot (hidemesh Chest removes them),
    most likely the Bumblebee that the host shows complete and lit while the guest shows only its black chest -
    TnMeshSetActorMAT part visibility / mesh set differs during the scene (guest actors are SimulatedProxy).
  - Next: compare part HiddenGame and bone transforms of the Bumblebee TnMeshSetActorMATs at the same matinee time
    (pause both on one frame), and find what switches their parts on the host (Kismet/matinee event, server only).
  New test commands: lightdump, lightsdump, classprops, hideowner, hidemesh, micdump, forcemips, propsat, reattach, lecount.
- 2026-10-20: also in gameplay (L03a) and only on NPC robots; follows the guest role. Details in B25.
- Owner 2026-10-20: switching off blur/post effects in the Advanced Configurator (F1) did NOT fix it (first report
  withdrawn).
- 2026-10-20 gfxdiag (new: both sides write Binaries\foc_gfx.txt every 30 s + `gfxdiag` command; tools/gfxdiff.ps1
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
- 2026-10-21 lobby late join: after the guest left and joined again the host listed 3 players (lobbycontinue -Late).
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
  CAMPAIGN > chapter I > overwrite warning Continue): PASS 2026-10-21 - guest in the mission 20 s after the warning
  was confirmed. A lobby guest that starts a mission itself (clicks in its window) also follows the host on "go".
- CONTINUE loads TnCampaignGameSolo with ?LoadCheckpoint, not through the console Exec, so the lobby reopens the map
  as a session; after that MapName returns the old "UI_FrontEnd_m" (many UWorld objects linger) and the relay never
  gets "port"/"go". Details and next steps: PLAN.md "Resume point 2026-10-20 late night". Test: tools/lobbycontinue.ps1.

### B34. Escalation menu: with 3-4 players the members fell back to their own party at the countdown - fixed 2026-10-22
- Members joined the party and the game lobby, then quit (~50 s after entering the game lobby, or after the host's
  RMT_LostConnection). Four causes in our Steam chat layer, all fixed; the full trail with dead ends (host migration,
  bandwidth tier, factions - none were the cause) is in ESCALATION-MENU.md "Tried and result".
  Test: `tools/esc4menu.ps1 -LobbyWait 80` PASS 4/4.

### B35. Escalation menu: members see "Ability: undefined" on the character card - fixed 2026-10-22 (4/4 in esc4_play3)
- Seen on every member at CHOOSE CHARACTER (host: Flak Shield). Cause: the member's 'Game' settings are a plain
  TnOnlineGameSettingsBase with TeamType 0, so TnCharacterScriptBinding.UseSurvivalCharacter() was false and the
  abilities were looked up among custom characters. Fix: invite.inc TickTeamType copies the replicated GRI
  GameTeamStatus into the joined game's settings and the class default (every tick; restored on story maps).
  Test: `tools/esc4menu.ps1 -Copies A,B`, member screenshot 4_map_B shows Flak Shield.

### B36. Escalation menu: host on AUTOBOTS, members on DECEPTICONS in the game lobby - fixed 2026-10-22
- The host's Game session listed the host twice; the team assignment gave the second entry team 0. invite.inc
  TickZeroRegistrant removes duplicate ids. esc4_play3: one DECEPTICONS list on all four.

### B37. Escalation menu: members' party lobby list split into two groups (ring icons) - fixed 2026-10-23
- Host shows one plain list. See ESCALATION-MENU.md "Tried and result" 2026-10-22/23.
- Two causes. (1) The "rings" were the lobby's connecting spinner (TnPlayerReplicationInfo.IsConnecting, captured in
  different rotation frames), see B40. (2) The split list: every member had a fifth lobby PRI - UniqueId 0, its own
  name - the placeholder made from the id-0 registrant before TickZeroRegistrant replaced it. invite.inc
  TickPhantomPri destroys it (DestroyActor) once our real PRI is in GRI.PRIArray. `esc4_phantom4`: the party lobby
  list is the same on all four copies (one list, same order, no icons).

### B38. Escalation over real Steam (2 PCs): the friend never loads the map - fixed 2026-10-23 (mock, all 4 maps; real 2-PC retest pending)
- First real test (owner + a friend, 2026-10-23, logs: owner foc_coop.log in the Steam folder, friend's copy on the
  desktop). Party, game lobby and travel work; the friend's game sends its connect packets to 127.0.0.1:7802 (its
  relay) and gets no answer for minutes. The host log has no "steamnet: hosting the match on port" line, the host
  relay never got "gameport", so it had nowhere to forward the friend's packets.
- Cause: on ESC_Gladiator the host's tick list held TnObjectiveMarkerTypeTransformerSurvival as "the game" and never
  TnSurvivalGame (games=0 during the load, then the marker object). The class table kept a stale kind: after map
  changes (the host went front end <-> lobby three times) a freed class's pointer and id were reused by another class,
  and RegisterClassObj returned early on the same pointer. Also seen: host bind 7777 failed with 10013 (port reserved
  on the owner's PC), the game used 7778 - handled, the relay takes the real port.
- Fix: RegisterClassObj also compares the class name index; TickSteamTravel also treats "was TnLobbyGame GameHost and
  an ESC_ map is loaded" as hosting. Test: `esc4menu.ps1 -Copies A,B -MapSteps n` for every map. Ark (0), Gladiator (1),
  Temple (2), Iacon (3) PASS, each host logs "hosting the match on port"; 4 copies `esc4_final` (Iacon) PASS 4/4.

### B39. Escalation: in-game invite gives the friend no Steam notification - fixed 2026-10-23 (mock; real Steam accept pending)
- Real test 2026-10-23: the friend saw the host in ONLINE FRIENDS, the invite never arrived; the friend joined
  through Friends List instead (that works). The in-game invite (SendGameInviteToFriend / ShowInviteUI) is not
  routed to the relay's Steam lobby invite yet; only the test command `escinvite <id>` sends one.
- Fix: invite.inc patches PlayerInterface slot 66 SendGameInviteToFriend (0x55a020, verified by decompiling
  0x8d8d50: one-id array -> SendGameInviteToFriends 0x816640) -> relay "invite <id>" = InviteUserToLobby to our
  party lobby. The friend's Steam shows the invite; its accept button raises GameLobbyJoinRequested in the friend's
  running relay (svc) -> "joinrequest" -> join (steamnet.inc). The friend must have the game running (the invite is
  for app 480; with the game closed Steam would try to start Spacewar). Test: `esc4menu.ps1 -MenuInvite` calls the
  game's own TnOnlineActionScriptBinding.SendGameInvite (`call cls:<Class> ...`, new target) - PASS 2/2, 3/3, 4/4.

### B40. Escalation: host sees a loading spinner at a member's name in the game lobby - fixed 2026-10-23
- Owner's report after the real test; also on the mock (spinner at Bumblebee in esc2_port1 3_game_lobby_A). The icon
  is TnPlayerReplicationInfo.IsConnecting = the member's registrant BeaconConnectionStatus 0 (EBCS_Traveling). The
  member broadcast status 1 before it was in the game channel; right after subscribing its new beacon whispered
  status 0 ("1600") and nothing else. Fix: steamnet.inc NoteMyStatus/TickStatusReplay - a status 0 sent within 3 s
  of subscribing is followed by our last non-zero status 3 s after the subscription. esc3_spin1, esc4_final: no
  spinner on the host. Not the same as B37 (members' chain icons, static) - those stay.
- **Correction** (same day): the members' icons were spinners as well. Statuses in `sessions` (registrant +0x4d):
  the host and every member whispered "16 00" (status 0) to each new channel member - a beacon created after the
  status broadcast answers with 0; the party host never broadcast at all (its status 1 went out before its beacon
  existed). Final fix (steamnet.inc): a whispered "16 00" is sent as our last broadcast status (or "16 01 00000000"
  if there was none); the channel owner keeps each member's last status, passes whispered ones on to the other
  members and gives a new subscriber everybody's; statuses received on a channel are fired again 3 and 8 s later
  (they arrive before the joined session has the registrants). The earlier "replay 3 s after subscribing" is
  replaced. `esc4_spin5`/`esc4_phantom4`: every registrant status 1 on every copy (self entries aside), no spinner
  in the party or game lobby on any copy.
- Found on the way: copies on one PC picked the same local relay port (7790 + pid % 40: B and C on 7822, FAIL 3/4
  in `esc4_spin5`); TickSteamTravel now takes the first free port from there.

### B41. Escalation game lobby: small differences between copies - open
- `esc4_phantom4` game lobby: the host and B show grey ping bars, C and D green; B draws the team list and title in
  blue (others purple - B's profile colour setting? not checked); members show Difficulty EASY while the host has
  HARD (the member builds its own SVPrivate settings, the host's settings are not sent; display only - the host runs
  the match). Map picture says OBLIVION after Select Map on every copy.
- Also seen (open, minor): members show Difficulty EASY while the host shows HARD; the map picture says OBLIVION
  after Select Map on every copy (the loaded map was the selected one).
- 2026-10-24 keyboard-only run through Friends List (sessions/esc4human, grid_5.png) - **still there, fix later**
  (owner): game lobby - host shows Difficulty **HARD**, all three members **EASY**; host's ping bars grey, members'
  green. Party lobby was identical (all green). To fix: send the host's difficulty (game settings) to members, and
  find why the host's own lobby bars stay grey.

### B42. Escalation: test tools cannot drive the menus with the mouse - open (fix later, owner 2026-10-24)
- `input click` and posted WM_MOUSEMOVE/LBUTTONDOWN (also SendMessage, with the GetCursorPos spoof) do not move the
  menu highlight in background copies. The log has no GetDeviceState/GetDeviceData calls; CreateDevice gets device
  instance GUIDs, never GUID_SysMouse, so g_mouseDev stays null. Probably the DirectInput mouse is read only while
  the window is active. Idea: hook Acquire (vtable slot 7), classify devices by GetCapabilities dwDevType
  (0x12 mouse, 0x13 keyboard). Keyboard works for every menu step; tests use keys meanwhile.

### B43. Escalation: foc_state.json on members lists only the member itself - open (fix later)
- 2026-10-24, in the match: host's players[] had all four names, each member only its own, while TAB on every
  member showed all four. The esc4menu `-Play` names check reads this file, so it can give a false FAIL. Fix the
  client state writer (PRIs of the other players) or check names from TAB screenshots.

### B44. Escalation: after "Join game" in ONLINE FRIENDS the member stays on that screen - minor, check
- 2026-10-24: the member is already in the party (host lists it) but its screen stays on ONLINE FRIENDS until ESC.
  Check whether the original game returns to the party lobby by itself.

### B45. Escalation: crash after a finished match, when the online game is destroyed - open (real 2-PC, 2026-10-24)
- Owner's log (v0.8.1-dev, host, sessions/real_20261024/host_escalation_pid6052.log + foc_crash_6052_crash1.dmp):
  MatchOver at 1876 s -> UI_Lobby_ESC_m at 1893 s -> UI_FrontEnd_m at 1898 s (left to the main menu) -> at 1931 s
  "UnsubscribeFromChatChannel" of the game channel, `GetGameSettings(Game) -> -` (null), then EXCEPTION c0000005 at
  ghidra 0x5e0aff (read of 0x11c), CRASHOBJ OnDestroyOnlineGameComplete / OnlineDisconnector /
  OnlineGameInterfaceDemonware. Owner says he then tried MULTIPLAYER - the crash is likely that click (the
  disconnector destroys the old Escalation sessions). Reading: the destroy-complete handler gets a session whose
  settings we already dropped. Fix later: keep the settings until OnDestroyOnlineGameComplete or answer the destroy
  for a missing session cleanly. Test: finish (or quit) an Escalation match, back to the main menu, open MULTIPLAYER.

### B46. MULTIPLAYER (versus) lobby: crash a few seconds after Send Invite - open (versus not supported yet)
- Owner's log (sessions/real_20261024/host_multiplayer_pid7068.log): MULTIPLAYER loads UI_Lobby_m (TnLobbyGame
  PartyHost, our Steam party lobby is created like in Escalation), Send Invite -> Steam lobby invite to the friend
  at 45 s; the friend never joined (no member+); at 49 s null call from ghidra 0xc1b467 (stack 8bd33a, b6e252,
  574b0b ...), no dump. Versus is the last item in the plan (ESCALATION-MENU.md step 6); this is the first real try.
  Fix later: find what 0xc1b467 calls (null vtable/delegate in the versus party lobby), or keep the Escalation
  hooks out of UI_Lobby_m until versus is done.

### B47. Escalation: Leaderboards empty - must be GLOBAL leaderboards - open (owner 2026-10-24)
- Owner: "Leaderboards are not shown, they must be global". So the screen is Leaderboards (party lobby and game
  lobby item), and the owner wants one shared table for all players of the mod, not a local/friends-only list.
- Log: no stats lines; MatchOver -> lobby 17 s later. Cause (probable): the game reads/writes stats and
  leaderboards through Demonware (StatsInterfaceEx), which is dead; only HasReadKeyArchiveFor is answered.
- Idea (no outside server, owner rule): Steam leaderboards of our app id 480 (ISteamUserStats::FindOrCreateLeaderboard,
  UploadLeaderboardScore, DownloadLeaderboardEntries GLOBAL / FRIENDS) through the relay; the DLL answers the game's
  stats read/write calls (StatsInterfaceEx slots, Write/Flush/ReadOnlineStats*) from them. Check: Spacewar leaderboards
  are shared by everyone using app 480 - prefix our board names (e.g. "foccoop_esc_<map>_<column>") and accept that
  anyone can write there. Map first which columns the Leaderboards screen shows (TnLeaderboard* scripts).
- Also check the post-match XP/level the same way (same stats interface).

### B48. Campaign over the F10 lobby (real 2-PC, v0.8.1-dev): the friend never gets into the host's game - FIXED 2026-10-24 (mock), real 2-PC retest pending
- 2026-10-24 20:13-20:19, after the Escalation session. Logs: sessions/real_20261024/host_campaign_L09_pid12244.log,
  host_campaign_L01_pid8320.log, host_relay.log, friend.log (friend's last run only).
- Host relay.log: four lobbies in a row (L09 ...021042, L09 ...787697, L01 ...788003, L01 ...789935); every time
  "session request from <friend>: accepted" / "peer ... connected, local socket -> 127.0.0.1:7778", so the Steam
  part works. But the host game never saw a guest: L09 run "waitguest: pausing until a guest joins" with no join,
  then F10 disconnect after ~80 s; the owner restarted with L01 several times. Last try (pid 8320): lobby created,
  L01 loaded, "lobby: go L01" sent at 81 s, no guest, F10 disconnect at 109 s.
- Friend (last run): F10 lobby search -> "relay: started ... join 109775242139789935 7790" -> "lobby: joining", then
  8 s later "relay: lobby search started" again; the friend's game stayed in the main menu: no "go", no open of
  127.0.0.1:7790, no travel. So the friend's side lost the join (relay join process gone / "go" never read).
- Not known yet: friend's relay.log (friend PC, Binaries\foc_relay\relay.log) and the friend's earlier runs
  (Binaries\foc_logs). Ask the owner for them. Also relay.log earlier: "no game running for 60 s, exiting".
- Story co-op is the main goal (owner requirements) - high priority once the logs are in.
- **Cause (2026-10-24):** two relay processes per player under one Steam user and app 480: the Escalation relay
  (`FoCRelay svc`, started at game start since v0.8.x) and the story relay (`host`/`join`). Steam delivered the
  friend's P2P messages to the svc process - the host's relay_svc.log has "session request from <friend>: accepted"
  at the times of the L09 and L01 tries - and svc drops story traffic. Host relay: no peer; guest relay: "host silent
  for 20 s", so the guest's TickLobby (needs state joined/connected) never opened 127.0.0.1:7790 although go= arrived.
  Mock Steam shows the same (lobbytest twin: "go=1|L01 state=host silent for 20 s").
- **Fix:** StartRelay (overlay.inc) stops the svc relay before a host/join relay starts (SnStopSvc: quit, kill after
  3 s, only if the status pid really is our FoCRelay.exe) and keeps the story relay's process handle; TickSteamChat
  (steamnet.inc) does not start svc while that relay lives and starts it again when it is gone. Both players need the
  new build. Test: tools/lobbytest.ps1 (TESTS.md).

### B49. F10 lobby on real Steam after v0.8.2-dev: still no game together - FIXED 2026-10-24 (mock), real retest pending
- Owner + friend, v0.8.2-dev, 21:27-21:33 (sessions/real_20261024b: host_run1/2.log, host_relay.log, friend.log):
  1. The svc relay was killed (not quit within 3 s, often while it was still starting) and restarted around every
     F10 step on both PCs; the host relay got the friend's session request but never a packet ("peer connected"
     missing), the friend heard nothing from the host.
  2. The host pressed Join on its OWN lobby in the list while hosting: its host relay quit, Steam made the friend the
     lobby owner; every later join went to the friend, who hosts nothing -> empty screen ("host silent for 20 s").
     The stale go= in the lobby made each join "follow" the old start at once.
  3. The friend (guest) pressed CONTINUE himself: his solo L01 paused "until a guest joins" -> his waiting screen.
- Fix: svc relay only from the ESCALATION / MULTIPLAYER menus and their maps (owner: "only when the Escalation button
  is pressed, kill it when you go back to the menu"), clean quit (10 s) before a kill; Join list (relay list mode)
  drops our own lobby, Escalation party lobbies (focesc) and lobbies whose host left (owner data != Steam owner or
  closed=1); Join disabled while we host; the host relay sets go="" and closed=1 when it ends; a guest relay leaves
  when the host left / the lobby is closed or gone ("stopped: host left the lobby") and the DLL drops the lobby role;
  a lobby guest never pauses its own map for guests (sync.inc g_lobbyGuest).
- Also (owner): lobby banner top left in the menus (role, players with Steam names, what to do next); the F10 menu
  font is a Windows TTF with Cyrillic (Russian Steam names showed as "?"), build.bat compiles with /utf-8.
- Test: tools/lobbyfriend.ps1 (TESTS.md). Escalation itself not re-tested (owner: parked); its relay now starts only
  when the Escalation lobby map loads - first party creation waits for the relay's Steam ID (up to 15 s).

### B50. Story co-op, 3+ players: guest position reports mixed up between guests - FIXED 2026-10-24 (4 copies)
- One FOCP1 slot (g_gpos) for all guests: TpGuestConsistent / TickReconcile compared guest 1's server pawn with the
  report of whichever guest sent last ("the guest sees its pawn 8707 uu from the server copy"); reconcile looked only at
  the first guest and could put its pawn on another guest's spot. Fix: FOCP1 carries PlayerReplicationInfo.PlayerID,
  the host keeps one report per guest (sync.inc g_gp, tp.inc TpGuestReport), reconcile runs per guest.
- Also: a server-side move of a guest pawn (scene gather, scenemove) was undone by the guest's next move and then by
  reconcile; now the host also calls ClientSetLocation on that guest and holds reconcile for 6 s (scene.inc).

### B51. After a scene everybody should stand with the player who triggered it - DONE 2026-10-24 (4 copies, L02a)
- Owner 2026-10-24 (choice: the game's network stays - host = server, enemies/damage shared; scenes for everybody;
  actions count for all; afterwards all move to the activator). scene.inc: the activator is the Instigator of the last
  Kismet event a player caused (Touch, Used, ...; not PlayerSpawned) within 15 s before a cutscene (any controller in
  cinematic mode); 1.5 s after the scene everybody else (the host too) is moved next to it, guests also through
  ClientSetLocation. Level-script scenes (intro) move nobody. Ini scenetp (default 1).
- Found on the way: the Kismet tracker never saw events (they count in SequenceEvent.TriggerCount +0x84, not in
  ActivateCount +0x70); the reflection search for "Instigator" picked another class' field - Instigator = +0x7c.
- Test: tools/scene4.ps1 (TESTS.md). Not tested with a natural trigger -> scene chain on a real map (the test fakes the
  scene with the level's SeqAct_ToggleCinematicMode and a forced activator); a guest's touch counting on the server is
  the engine's own behaviour, not separately checked. Open from the screenshots: B5 dark silhouettes on guests in the
  L01 opening (unchanged), "Hold E to Revive" prompt on guests there.

### R1. Installer: optional Russian translation, all mods unticked - DONE 2026-10-24
- Owner: bundle the Russian translation (Steam guide 2983257437 "РАБОЧИЙ МУЛЬТИПЛЕЕР + НОВАЯ РУСИФИКАЦИЯ TFOC + DLC" by
  Zerberus, archive TFOC.7z: Coalesced.RUS, a replacement Coalesced.ini, two _RUS.bik, a .reg with Language=RUS) as a
  choice, and every mod unticked by default. The guide gives no permission to redistribute (said to the owner, his call).
- Installer: first list entry "Русский язык", resource rus.zip (build_installer.bat, files from E:\FoC-coop\downloads\rus,
  not in git); copies TransGame\ files with backups and writes foc_coop.ini language=rus; uninstall restores the files and
  removes the key. DLL: ini language=<x> appends -languageforcooking=<x> to the command line (the guide's launch option).
- Tested on copy D: files + language=rus -> Russian main menu; without the key English (registry stayed ENU, so the
  .reg is not needed and HKLM is not touched); installer /install /mods rus and /uninstall on copy D exit 0, files and
  key put back (Coalesced.ini hash as before).
- DLL fix on the way: an ini without the co-op keys (e.g. only language=rus) turned co-op features off; the defaults now
  apply when the ini has no tcpdriver key.

### B52. 4 players: a guest briefly carries another guest's name on the host - open (flaky)
- 2026-10-25 run `fix25b_4`: B and C swap names every 5 s (B's log "names: 'Ironhide' -> SetName Bumblebee", C's
  "names: 'Bumblebee' -> SetName Ironhide") - one guest's rename lands on the other's PRI. Frequent with 4 copies.
- 2026-10-25 release run `rel_4`: copy D (Ratchet) showed as "Ironhide" (C's name) on the host; D's log: "names:
  'Ironhide' -> SetName Ratchet (ok)", so D got C's name and renamed itself back. scene4 found no Ratchet and failed;
  the rerun `rel_4b` PASS. Check the name path (B32 code, TickNames / ServerChangeName) with 4 copies.

- 2026-10-25 night: scene4 failed 2 of 4 runs ("no player named Ratchet"; two players named Bumblebee and the host
  2236 uu from the activator after the scene); the other 2 passed. The host's player list said Optimus, Default, Default, Default every time.

### Real test 2026-10-25 (v0.9.0-dev, owner hosts + friend, chapters 1-2) - list and state
Logs: sessions/real_20261025/host.log (host session from 00:37), friend_restart.log (only the friend's restart after his
crash - the crash run is in the friend's Binaries\foc_logs, not received yet), host_relay.log.

### B53. A guest triggers a scripted moment ("Shift" walk in chapter 1) and the host is left behind a door - mitigated
- Owner: "the friend triggered the Shift moment, everything broke and we stayed behind the door"; he asks that only the
  host can activate triggers. The level script runs the moment for its Instigator (the guest).
- Done: a scripted moment now also means a controller in InputDisabled or AnimControl (TnSeqEvent_WalkToLocation runs
  without cinematic mode); after it everybody is moved to its activator (scene.inc), so the host is brought to the guest.
- Open: host-only triggers. Needs USequenceEvent::CheckActivate (or the Touch path) to refuse guest instigators; it has
  no Function object (`fnprobe CheckActivate`: 0) and no exec name strings in TFOC.exe (ghidra NativeTable / StrRefs
  empty), so the native must be found another way (callers of TriggerEventClass, SeqEvent_Touch vtable).

### B54. Abilities (transform, shoot, weapon) only for the host - FIXED 2026-10-25 (mock)
- Owner: the friend could transform at the start before the host was allowed; after the weapon pickup in chapter 2 the
  host could shoot and transform, the friend could not.
- Cause: TnSeqAct_TogglePlayerActions.Activated uses only Targets[0] (the host's pawn, hostfirst). Its flags are
  replicated TnPawn bools (_TransformingEnabled, _JumpingEnabled, _MeleeEnabled, _ShootingEnabled, _Ability0/1Enabled,
  _WeaponEquipEnabled, _HandednessEnabled). Fix (scene.inc TickAbilities, host): every change of the host pawn's flags
  is copied to each guest pawn (a new guest pawn gets all of them), bForceNetUpdate set. Not while the host is downed.
- Verified: L01 "6 flag(s) copied from the host (TransformingEnabled=0 ...)", L02a "2 flag(s) copied (new guest pawn)".
  Not verified on the guest's screen (transform/shoot allowed at the same moments as the host): real test.
- Found on the way: MirrorPropFind("Actor","bNetDirty") gives +0x30 (inside the UObject header - another class' field):
  not used.

### B55. Walk through the fire (chapter 2): the guest burns and dies, the checkpoint restarts - FIXED 2026-10-25 (mock)
- Host log 806-815 s: TnSeqEvent_WalkToLocation by the host (InputDisabled, no damage), the guest walked through the
  fire by itself (hp 200 -> 118 -> downed -> -344 -> dead); the game went InProgressOver and restarted the checkpoint
  5 s later (also at 852 s and 883 s). coopdeath never saw it (no "game over requested": another path than 0x7eb060).
- Fix (scene.inc TickProtect, host): Controller._bGodMode (+0x1e0 mask 2; GameInfo.ReduceDamage zeroes damage when
  InGodMode, TnPlayerPawn.TakeDamageHealthBelowZero returns) for every other player while one player is in a scripted
  moment. Verified: "invulnerable (another player's scripted moment)" during the host's intro, "vulnerable again" after.

### B56. A downed player who is not revived quickly ends the game - FIXED 2026-10-25 (mock)
- Owner: the game must end only when everybody is down; otherwise a downed player lies without a time limit.
- The bleed-out was already off (DownedHealth 1e9), but environment damage (TnDamageTypeEnvironmentNoDowned), instant
  kills and the "Health < -DownedHealth" check still killed a downed pawn, and each death restarted the checkpoint.
- Fix: TickProtect also sets _bGodMode on every downed player while somebody is still up (g_allDown from TickDowned:
  when everybody is down the protection goes and the existing bleed-out -> MISSION FAILED path runs). Verified on L02a:
  TestDowned -> "invulnerable (downed, waiting for a revive)", 30 s down, TestRevive -> "vulnerable again".
- Not checked: KillZ (falling out of the world) still kills (rescue moves players back first).

### B57. Picking up the heavy weapon removed the first weapon - open
- Owner: when the second (heavy) weapon was picked up the first one vanished, for host and guest alike. Check whether it is
  the original game (one heavy slot replaces the carried weapon) or the co-op (ctrlshare / inventory).

### B58. Host does not see some prompts (button hints, what to do) - open
- Owner: in chapters 1 and 2 many prompts were missing on the host. TnSeqAct_DisplayMessage registers the tutorial module
  on every target's controller (no Targets[0] problem). Next: compare host and guest screenshots at a tutorial prompt; check
  the TnInputOverrideManager on the host (ctrlshare copies interact/command objects between controllers).

### B59. End-of-chapter video: the guest stops watching and starts loading long before the host - open
- Owner: after chapter 1 the friend's video ended early and his loading started much earlier. Check movie sync (Bink hooks,
  movie: lines) on both sides with logs from both.

### B60. Weapon pickup sound plays at the guest's position - open
- Owner: the host picked up the weapon, the sound came from the friend's side. A sound action probably targets the
  Instigator / the first player; check the Kismet ops around the pickup.

### B61. Guest crash in chapter 2 - open, needs the friend's log
- The friend crashed with an error after the weapon pickup. The crashed run is in the friend's Binaries\foc_logs (and a
  foc_crash_*.dmp next to the exe); ask the owner for them.

### B62. Guest sees periodic lags (host capped at 60 fps) - partly fixed 2026-10-25
- Host log: "slow: TickTraceAll took 74-85 ms" 33 times - our Kismet tracker compared every op it found with all collected
  ops (up to 12000) one by one; the host froze for that long and the guest saw it as lag. Now a hash set (trace.inc).
- Still to check with the friend's log: his own long frames and the relay's receive gaps.

### B63. Guests do not see the host's character (only the hands / weapon) - every chapter - FIXED 2026-10-25 (mock, `partshow`)
- Fix: the host copy's body part meshes (CHARSKEL parts) were HiddenGame on the guest; scene.inc TickPartShow (ini
  `partshow`, default 1) unhides every non-"Arm" CHARSKEL part of other players' pawns unless the master is hidden, every
  2 s on story maps. Verified by screenshots on L03b and L01 (sessions/charvis ps*_B, auto1_*) and L08a (c8_grid: the
  guest sees the host's whole Decepticon, lit). The empty anim node list below was not the cause.
- Owner 2026-10-25: "chapter 3: at the start the client sees only the host's hands; the host sees everything ... same in
  chapter 8, in every chapter". Reproduced with 2 copies on L03b (sessions/charvis: near_*, xm_B, fix0_B): from 900 uu
  behind the host the guest sees only the host's gun floating where its hand is; the host sees the guest fully.
- What is the same on the guest for its own pawn and the host's copy (`pawnvis`, scene.inc): replicated character data
  (_CurrentCharacterCompressed +0x6b8), TnCharacterApplier, TnRobotForm, one SkeletalMeshComponent with
  RB_Iconic_SKEL, AnimTreeTemplate Robot_ANIMTREE, HiddenGame/bOwnerNoSee 0, the same Actor.Components list, bHidden 0.
- The only difference found in the mesh object: the list at SkeletalMeshComponent +0x224 (animation nodes:
  TnAnimNodeWeaponSlot, AnimNodeSequence) has 2 entries on every pawn on the host and on the guest's own pawn, 0 on the
  host's copy on the guest -> the host copy's animation tree never got its nodes. Theory: the characters share one
  skeleton and the tree hides the parts of other characters, so without it only the unhidden bits (hands) draw.
- Tried (`animfix` test command): SetAnimTreeTemplate - no Function object (also none for InitAnimTree,
  ForceSkelUpdate, UpdateAnimations, SetSkeletalMesh); dropping the tree instance (+0x214 Animations) + ActorComponent
  bNeedsReattach (+0x38/2) -> no change, still 0 nodes and invisible.
- Next: find the native USkeletalMeshComponent::InitAnimTree (ghidra: callers of the AnimTreeTemplate copy, or the
  SkeletalMeshComponent vtable) and call it for remote player meshes with 0 nodes; check whether B5 (dark silhouettes on
  guests) is the same missing-tree problem. New test commands: `pawnvis`, `propat <off> [mask]`, `animfix`.

### B64. F10 menu cut off and guests "host silent" after Join - FIXED 2026-10-25 (mock, visual test)
- The menu laid out in window pixels while the game renders into a smaller back buffer stretched onto the window
  (1920x1071 window, 800x450 buffer): everything below the buffer height was cut - "Create lobby" invisible. Now the
  menu uses back-buffer pixels (mouse scaled), its height is capped (scrolls), and the lobby part is first on Host.
- The Join page's browser relay (FoCRelay list) was still alive when the join relay started: one more process of the
  same Steam user, the host's messages did not reach the join relay ("host silent for 20 s"). StartRelay now waits
  for the browser to quit (killed after 4 s). Probably also hit real friends who joined through the Join page.

### B32. Escalation: names (Default, then three guests with one name) - fixed 2026-10-21
- Owner: TAB showed his name and "Default", later mock_*; with 4 players the three guests counted as one. Cause:
  every guest logs in with UniqueNetId 0 (no online service), so the game applied one guest's name change to all
  PRIs with that id (4-copy test: "Bumblebee" x3). Fix: `uniqueids` (host gives each PRI UniqueId +0x230 its own
  Steam-form id), names set as soon as the PRI exists and re-set every 5 s, source ini `playername` > Steam name >
  ?Name=. Test: tools/esc4.ps1 (A-D, invented names Optimus/Bumblebee/Ironhide/Ratchet): PASS, TAB shows all four.

### B33. Escalation: a player's death banner on another player's screen - fixed 2026-10-21
- ctrlshare gave guests the host's UIController; in Escalation it is not shared any more (story keeps it).
  4-copy run: the host died (spectating), the three guests showed the normal wave countdown.

### B31. Host: Esc (pause menu) with a guest connected called address 0 - fixed 2026-10-21 (`exechook` default 0)
- The engine console Exec hook (lobby.inc, vtable slot ghidra 0x1522ba4) broke "onrelease showmenu" while a guest was
  connected: EXCEPTION at 00000000, [esp]=0 (looks like a corrupted return), the host process died once. Without a
  guest, or with exechook=0, the pause menu is fine. The hook is not needed: the menu's CHAPTER / CONTINUE never use
  Exec (the lobby reopens solo games). Test: tools/pausetest.ps1 -Guest (PASS with exechook=0, FAIL with 1).
  NULLCALL lines in the log now show [esp] slots with their module for calls to address 0.

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

### B65. High Quality Audio Logs: audio logs ignore the volume sliders (always full volume) - FIXED 2026-10-25 (installer)
- Owner 2026-10-25: with the mod installed the audio logs play at maximum volume whatever the sliders say.
- Cause (unpacked both TransGame.xxx - UE3, zlib chunks - and diffed): the mod changes only one thing, the
  CategoryName of the 76 audio log sound events, DX_AUDIO_LOG (the lo-fi audio log effect) -> None. Without a category
  the volume sliders do not reach them. In memory the HmSoundEvent objects are the same; `soundinfo`/`namefind` test
  commands added on the way (SoundCue has no SoundClass here: the game's own Hm audio).
- Fix (installer/Mods.cs AudioLogFix): after copying the mod's TransGame.xxx the installer sets CategoryName to
  DX_DRY_RADIO (the only other dialogue category in the package: dialogue volume, no reverb) and packs the file again
  (same chunks, zlib). Verified: the C# result is byte-equal to the Python reference (tools in the job scratch), the game
  loads L01 with it. Not verified by ear yet whether DX_DRY_RADIO adds a radio filter - owner to listen.

