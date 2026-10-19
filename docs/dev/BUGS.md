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

### B4. Opening a session without a restart is rejected — workaround
- In-process `open <map>?listen` from a focused front end ends with "You must be signed in to play online"
  (Demonware session code, TCP 3074). Blocking 3074 and spoofing foreground did not help.
- Workaround: the menu relaunches the game with the URL on the command line (20–30 s).

### B5. Guest visuals: dark silhouettes in cutscenes — open (owner said: not now)

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
- Open: (a) a really dead guest (fall into a pit, KillZ) gets no new pawn: its spawn helper entry goes back to state 2
  but native TrySpawnPlayer (ghidra 0x8a35b0) is never called for it (the host's own respawn works); (b) owner wants an
  automatic checkpoint restart when everybody is down (now the menu); (c) revive test both ways (owner's order:
  down host -> guest revives, down guest -> host revives, both down -> restart).
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

### B14. Scripted scenes: the guest sees the host's cutscene from its own position — open
- Owner wants: guest camera switched to the main character or the cutscene played for both, then both placed next to
  each other without collision pushing them apart.

### B15. Streaming trigger: one player reaches a marker, the other's area unloads and he falls — open
- Owner wants: teleport the other player next to him, or keep the area loaded and hold the first player (input lock or
  invisible wall) until the other one reaches the marker.
- Note: writing Actor.Location (+0xe4/+0x134) does not move a standing pawn (physics puts it back); a real teleport
  needs a native move (FarMoveActor / SetLocation) or a console command through the local player.

### B16. Chapter title shown to only one player — open (see B12)
- Owner 2026-10-19: "Chapter ..." at the start shows in the UI of only one player (which one: check).

### B17. Intro animation: one player has no character animation — open (owner report 2026-10-19)
- The intro now plays on host and guest at the same time, but on one of them (probably the host) the player
  character's animation is missing. Check with screenshots of both at the level start.

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
  the menu (code kept). Relay logs to foc_relay
elay.log; mod logs of the last 10 runs in Binariesoc_logs.

### B21. Guest frozen on the host after using an interactive object, then dropped — fixed 2026-10-19 (`ctrlskip`)
- Owner's Steam session (chapter 6): the guest used something at ~63 s; ctrlshare copied the host's TnInteractStore /
  TnContextualCommandUseInteract (and later TnTankForm) into the guest's controller every 0.2 s; the guest's pawn froze
  on the host (client kept moving: 25000 uu apart) and the guest fell back to the menu ~60 s later.
- Fix: ctrlskip is a comma list, default `Camera,Interact,ContextualCommand,Form`.

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
