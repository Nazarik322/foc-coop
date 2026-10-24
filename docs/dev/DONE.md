# Done

What is implemented and verified, newest first. Every entry says how it was verified.
Open problems are in [BUGS.md](BUGS.md), and planned work is in [PLAN.md](PLAN.md).

## 2026-10-23: first real 2-PC test fixes (released as v0.8.1-dev, commit 1e9ba70, main fast-forwarded)
- B38 match port on every ESC map, B39 in-game Send Invite -> Steam lobby invite, B40/B37 lobby spinners and the
  split party list, free local relay port. Verified on the mock: all 4 maps (esc2_map*), 4 copies esc4_phantom4.
  Installer SHA-256 b8ac66f817c598c1cbf6487d1bf511b7ac718eb1d6546d70b936ba5aefc2a5bd (downloaded back, same).
  Not run: story must-pass (owner: skip singleplayer), real 2-PC retest.

## 2026-10-22: Escalation through the game's own menus, 4 players (released as v0.8.0-dev, commit b51f5f5)
- ESCALATION from the main menu over Steam (Spacewar relay): party lobby, Steam invites / Friends List join,
  Private Match > Create Game, game lobby with all players on one team, Start Game, everybody loads the map.
- Fixed on the way (details and dead ends in ESCALATION-MENU.md "Tried and result"): chat member-change call
  (id by pointer, 1 = left), host connection status replayed to late subscribers, member whispers to id 0 go to the
  host, the member's id-0 host registrant follows the host's receive time (members quit after ~50 s before).
- Verified: `tools/esc4menu.ps1 -LobbyWait 80` PASS 4/4 (mock Steam, 4 copies on one machine).

## 2026-10-21: lobby without restarts (branch wip/lobby-b4, not released)
- B4 fixed (fakelogin): host and join inside the running game; F10 Host/Join/Leave without a restart.
- F10 lobby: Create lobby (friends / public / private, password), Join lobby from the main menu; the host starts the
  mission from the game's own menu (CONTINUE or CAMPAIGN > chapter), everybody in the lobby follows; late join works.
- Also from 2026-10-20: v0.6 map sweep (SWEEP-0.6.md), trace/Kismet logs, Kismet op mirror for the guest, camview,
  tpfollow, 7 s rescue for any player, leash, reconcile, streamguard, netspeed, gfxdiag, B26 host hitch fix, B31.
- Verified: lobbytest, lobbycontinue (-Late, chapter), pausetest -Guest, b4test (twin FAIL); must-pass d970658: 5/5 PASS,
  Steam mock L03b PASS. Only the Steam mock on one PC so far.

## 2026-10-20: B16 chapter title on both screens (lane title, merged 051719d)
- story_title.inc: host detection (ActivateCount), sync keys chap/chapseq, guest native GFx invoke. titletest
  (screen based) PASS host/guest 296/303 in the lane, twin FAIL; screenshots E:\FoC-coop\sessions\title\t2.
- Released as v0.6.0-dev together with B15 teleport and the B14 cutscene part (cinesync).
- Release check on 051719d: must-pass 5/5 PASS (an earlier run on the same commit had L02a/L02b/L04c FAIL,
  flaky), Steam L03b PASS, titletest PASS 301/302, twin FAIL. README/README.ru updated for 0.6.

## 2026-10-20: stage 6 unfocused input (lane input, merged 289671b)
- input.inc: `input hold/tap/mouse/mode`; key messages + GetAsyncKeyState/GetKeyState hooks move the pawn of either
  window without focus. inputtest PASS, twin FAIL. The old DirectInput-only `input key` never moved the pawn (the game
  reads no DirectInput keyboard). Next: use it for the ESC_Ark character pick and move/look tests (infra files).

## 2026-10-20: B15 native teleport (lane tp, merged b9c29c7 + 27d8c61)
- tp.inc: host calls AActor::SetLocation (0x93c9d0) on the guest pawn; `tpg` test command; `rescue` uses it
  (`tpsync`, default on). tptest PASS, twin FAIL; must-pass all PASS. Real streaming unload still to try.

## 2026-10-20: team tooling (lane infra, merged 490c313)
- Slot lease for the shared test slot, `Build-Deploy -Repo` for lane worktrees, `foc_build.txt`, copy parameters.
  `tools/slottest.ps1` 5/5 PASS (rerun by the lead); must-pass set on the merged main: see TESTS.md.
- B9 fixed: window screenshot / focus with no window is a note, not an exception (`Test-FocWin`).

## 2026-10-20: story cutscenes (B14, part)
- Guest follows the host's cinematic mode: no HUD and no movement during cutscenes, back afterwards (`cinesync`).
  Test `tools/cinetest.ps1` (chapter 1): PASS; twin `-Twin` (cinesync=0): FAIL as it should.
- `poke <target> <off> |bits / ~bits`; foc_state.json players[].hidden (Actor.bHidden).
- FindScriptFunction fallback limited to the object's class chain.

## 2026-10-20: Escalation session fixes (v0.4.2)
- Player names from Steam on both sides (B22); ctrlshare allowlist (B23); names in foc_state.json.

## 2026-10-19 (night 2): Escalation scoring, Steam lobby list, guest freeze
- Escalation: scoring rules (B18) - TAB now counts score, kills, deaths, revives, money (verified, 2 copies, Steam mock).
- Steam join: public lobbies with name/mode/map, Join page lists games found on Steam (friends first) with Join
  buttons; IP/LAN removed from the menu (B20). Guest joins with ?Name=<Steam name>.
- ctrlshare no longer shares interaction objects and vehicle forms (B21, the guest drop in the owner's session).
- Logs: last 10 runs in Binaries\foc_logs; relay logs relay.log / relay_list.log.

## 2026-10-19 (late night): Steam shows Spacewar, installer opens mod pages
- Owner: Steam showed "Transformers" instead of Spacewar. dinput8.dll now restarts a game started with SteamAppId != 480
  as SteamAppId=480 (verified: Steam RunningAppID 480, game runs; a Steam-like start with 213120 switches to 480).
  ini `spacewar` (default 1). tools/dev.ps1 starts test copies with SteamAppId=480 (otherwise the tracked pid is gone).
  Chapter 6 LAN + Steam mock PASS after the change.
- Installer: ticking a mod without its archive opens its Nexus page (default browser); a "Waiting for the downloads"
  page polls Downloads every 2 s (ignores .crdownload/.part, waits for a stable size) and installs by itself.

## 2026-10-19 (night): story co-op identity, deaths, mods
- Play bot over all chapters (`tools/botall.ps1`): 2a, 2b no problems; 1: guest fell into the pit and was rescued;
  3a: guest fell through the level and died (B11 open item); 3b onwards SKIP (remote desktop minimized).
- B11: `coopdeath` (no MISSION FAILED while a player is up, host and guest), `downedsec` (downed players wait for a
  revive). Tests `tools/deathtest.ps1`, `tools/downtest.ps1`.
- B12/B13: sync channel host -> guest inside the game's UDP connection (`sync.inc`): objective + markers mirrored on the
  guest; host waits with a paused level until the guest is in and both intro movies are over.
- Installer: "Extra mods" page (`installer/Mods.cs`) for the Nexus archives in Downloads (audio logs, FMV remaster,
  intro skip, Advanced Configurator chain-loaded by our dinput8.dll); backups in `<game>\foc_backup`.
- Test commands: `dump`, `props`, `fall`, `tp`, `hp`, `tpmarker`, `lpexec`.
- ReShade bundled in the installer; all extra mods installed into A and B with the installer (silent mode) and played
  in chapter 6 on LAN and through the Steam relay mock: PASS (see PLAN, mods section).

## 2026-10-19 (evening): F10 Settings page

- F10 -> Settings: rescue, spawnshift, guest camera fix, ctrlshare, hostfirst, diagnostics apply at once (also in a
  game); "host through Steam" and the player limit apply next time you host. Saved to foc_coop.ini (the full player
  set, so a partial ini never turns features off). Test commands `menu <page>|off` and `shotui <file>` (screenshot
  with the menu). Verified: page captured in the front end; 2b and Ark still PASS.

## 2026-10-19 (evening): owner playtest fixes

- **"Teleported back" (host and guest)**: the rescue teleport fired when one player simply walked down the level
  (2b drops 6000 uu). Now it needs a real fall: 4 one-second samples in a row dropping > 800 uu each.
- **Host crash in 2b (owner playtest)**: native ServerUpdatePing (ghidra 0xb4e5a0) wrote the ping into a null PRI of
  the guest. New VEH guard (counter 6) skips the update.
- **Guest dropped to the main menu right after loading (~1 in 6 runs)**: ctrlshare touched the guest controller
  during its login. Now it waits 8 s after a controller appears (`ctrlsharedelay`). 2a/2b 6/6 PASS.
- **`tools/playbot.ps1`**: host and guest walk through a chapter (real keys/mouse), checks desync between what the
  host and the guest see, teleports, crashes, screenshots every step. 2b, 3 min: no crash, positions in sync
  (0-25 uu while moving), guard 6 fired once (the owner's crash).
- **`tools/record.ps1`**: records a play session (in-game screenshots + state every 20 s).

## 2026-10-19 (evening): installer

- **`installer/Setup.cs` -> `dist/FoCCoopSetup.exe`** (`installer/build_installer.bat` builds DLL, relay and embeds them
  with the real steam_api.dll): wizard Welcome -> Before you install -> Game folder (found from the Steam registry path
  and every library in libraryfolders.vdf) -> Install/Remove; Russian or English by Windows language; TFOC.exe hash
  check; backup of a foreign dinput8.dll; self-elevates (UAC) when the folder needs admin rights.
  Verified: detection finds `C:\Program Files (x86)\Steam\steamapps\common\Transformers Fall of Cybertron` (read only);
  `/install E:\FoC-coop\A /silent` -> exit 0, all three files byte-identical to the build; `/uninstall` removes them;
  wizard pages captured (window only). Not tested: the UAC path (needs a click).

## 2026-10-19 (evening): Steam (Spacewar 480) relay

- **`relay/focrelay.c` -> `FoCRelay.exe`** (32-bit, flat Steam API loaded at run time, playbook HXCoop pattern):
  `host <gamePort>` opens a friends-only lobby and gives each friend a local UDP socket to the game;
  `join <lobby|auto> 7790` finds a friend who hosts and carries the guest game's packets. Lives in
  `Binaries\foc_relay` with its own `steam_api.dll` (Steamworks.NET 2025.164.1 x86, SHA-256 fdafb7c6...eb65da).
- **In-game menu**: Host has "Through Steam" (on by default; the host restarts with `-steamrelay` and the DLL starts the
  relay once the map listens), Join has "Join a friend through Steam" (+ optional lobby id), main page shows the
  relay state, lobby, friends and an "Invite friends (Steam overlay)" button. Test command `steamjoin`.
- **Mock `steam_api.dll`** (`relay/mock`) for two players on one PC. Found on the way: delivering messages out of order
  (swap-remove) stalls the UE3 handshake; fixed (FIFO).
- Verified: relay round trip without the game 3/3; **chapter 2b through the relay (mock): PASS** (guest joined from the
  menu, pawn, both moved, 60 s alive, host relay tx 182 KB / rx 86 KB). **Real Steam, host side**: InitFlat OK while
  TFOC.exe runs on the same account, real friends-only lobby created, relay aimed at game port 7778.
  Not tested: a real second Steam account (needs a friend). Also PASS through the relay: Escalation Ark, chapter 3b.
- **Guest sometimes dropped right after loading (2a)**: the host-only fixes (ctrlshare, hostfirst, rescue) also ran on
  the client and copied objects of the old front-end controller into the new one. Now host only; 2a 3/3 PASS.

## 2026-10-19 (afternoon, stage 1 of the new plan)

- **B1 fixed in chapters 1, 2a, 3b, 4c, 5, 9** (`spawnshift`): cause found with a hardware breakpoint on the object
  allocator plus Ghidra (RestartPlayer spawns the guest's pawn into the host and the spawn fails). Matrix with
  screenshots, 120 s alive: all six PASS; without the fix all six FAIL (guest spawn state 2).
- **8b**: guest gets Grimlock after a new guard for the crash in the tail of Possess (ghidra 0x641e10); 120 s alive.

- **B10 fixed: the host's camera followed the guest's pawn** (reported by the owner). Cause: ctrlshare copied the
  host's `TnCameraStrategyManager` (PC+0x694) into the guest's controller, so both controllers drove one camera.
  Fix: `ctrlskip` defaults to `Camera`. Verified with in-game screenshots (matrix `-Look`, only the guest walks):
  fix off -> host picture diff 27, FAIL; fix on -> diff 4, PASS (chapter 2b).
- **Matrix runs after the move to the game thread**: Ark, 2a, 2b, 6 PASS (2a/2b: guest has a pawn for the first time;
  2a 180 s without the old B2 crash). 1, 3b, 4c, 5, 8b, 9: guest stuck in spawn state 2 (B1).
- **In-game screenshots** (`shot`) work on host and guest (1920x1071 BMP from the back buffer). Injected keys do not
  move the pawn yet (the game keeps no DirectInput keyboard; window messages are tried next) — the matrix uses real W.
- `docs/dev/TESTS.md`: regression list (each fix with its test and twin), run before every DLL commit.

- **Game-thread tick** (`src/proxy_dinput8/gametick.inc`): forcechar, maxplayers, ctrlshare, hostfirst, rescue and ctrldiag
  now run on the game thread (posted message every 200 ms, one GObjects pass by class id) instead of background
  threads that wrote engine objects. Verified in chapter 6 with host + guest: tick 0.5-1.5 ms (first one ~130 ms),
  both players have a pawn, spawn state 3, guest sees its own and the host's pawn. `stat:` line every 3 s (B8 closed).
  Found on the way: the old ctrldiag code on the game thread froze the host for 21 s per tick (full GObjects walk with
  VirtualQuery); it now uses the tick's object list.
- **`foc_state.json`** on host and guest (format in NOTES.md), incl. the real map name (UWorld.URL) and the host port.
- **Spawn-helper state from memory**: TnCampaignGame+0x378 -> spawn helper, Players at +0x28 (campaign and Escalation).
- **Crash dumps**: `crashtest` wrote `foc_crash_<pid>_test0.dmp` (633 KB) and the game kept running; B7 crashes wrote
  real dumps with CRASHSTACK in ghidra VAs.
- **B7 explained** (environment: undisplayed remote desktop session), see BUGS.md.
- `tools/matrix.ps1` rewritten: checks the state files, prints `RESULT: PASS/FAIL/SKIP`, copies logs and dumps;
  `Stop-Foc` only stops test copies. **Not yet verified end to end**: the remote desktop session stopped being displayed
  during the run (all chapter starts hit B7), so the matrix PASS for Ark / chapter 1 / chapter 6 is still open.
- Written but not run yet: `shot [file]` (back-buffer BMP) and `input key <sc> <ms>` / `input mouse <dx> <dy> <ms>`
  (injected DirectInput) commands in `foc_cmd.txt` (stage 6).

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
