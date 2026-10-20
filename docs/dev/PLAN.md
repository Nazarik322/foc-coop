# Plan

**Start of every session: read this file, then [BUGS.md](BUGS.md).** Update it when the plan changes.
Approved by the owner on 2026-10-19 (full text with reasoning: `%USERPROFILE%\.claude\plans\sequential-scribbling-cascade.md`).

## Team plan (2026-10-20)
Work runs in team mode, see [TEAM.md](TEAM.md): lead + 3 agents. Wave 1: re (B15/B14/B16/B17 recipes), infra (slot
lease, worktree builds), title (B16). Wave 2: cine, tp, input. Wave 3: merge, must-pass, R1 sweep, release on OK.

**Resume point (2026-10-20 evening, owner stopped the team for release v0.6.0-dev):**
- MERGED and verified: infra (490c313), tp B15 (b9c29c7, 27d8c61), input stage 6 (289671b), title B16 (051719d).
- re lane reports: `coop-anything\local\lanes\re\REPORT.md` (round 1) and the round 2 natives in NOTES.md
  (chapter title, GFx invoke, texture loader).
- STOPPED, unmerged: `lane/cine` 4892f3e (B14 `cinehide`, B17 `matineeanim` probe; role offset 0x6e; the form call
  faults on role-1 pawns; no PASS yet) and `lane/gfx` 220d6c2 (B25: gfxtest shots/metrics, `guestprofile` and
  `charloader` default off, no results yet). Resume each from its branch with a new agent (Sonnet, medium).
- Todo: rerun `inputtest.ps1 -Twin` on main (it hung once and SKIPped); B25 remaining checks (BUGS); B17; switch the
  ESC_Ark character pick and move/look tests to `input tap/hold` (infra files); then R1 sweep.

## Next step (where to resume)
**2026-10-20 (stopped by the owner here): story bugs.** Done: B14 part (guest HUD/movement follow the host's
cutscenes, `cinesync`, commit 4eaa654, local, not pushed). Resume with:
1. B16: on L06 `-Keep`, `chapter 10` on the guest (B) + `shot`; if the title shows, wire host -> guest over sync.
2. B14 rest: hide the guest's pawn in cutscenes (needs native AActor::SetHidden from ghidra); chapter 1 opening
   "look around": guest view stuck on TnLookAroundCameraActor's cutscene shot (with B17).
3. B17 intro animation, revive test both ways, B15 teleport of the guest, then release (owner approved "в прод"
   for releases in this phase; ask if unsure).
Also in the last commit: `hud` target for call/oexec, `chapter <n>` command (builds, matrix L06 PASS, not tried).

**2026-10-19 owner: Escalation first, released v0.4.0.** Open for Escalation: B19 weapon slots (buy at an armory),
match end after the last team life, Armory Recreators / T.E.C.H. (not checked). Open for the story (later, owner said
so): B11 automatic checkpoint restart and guest respawn after a real death, B14 cutscene camera, B15 streaming
trigger teleport, B16 chapter title on one side, B17 intro animation, revive test both ways.
Test commands now include `oexec <pc|pawn|rpc|rpawn|game|pri|rpri|obj:Name> <exec function>` (ScriptConsoleExec ->
ProcessEvent 0x728c60) and `poke`; the developer exec FakeScoreKill crashes the server (do not use).

**Owner priority 2026-10-19: chapter 6 (L06_Megatron) host and guest identical, then the same fix for every chapter.**
Order: UI → cutscene sync → camera in scripted scenes → streaming triggers → deaths. Full list: B12-B16 in BUGS.md.
- Root cause of most of it: Kismet runs only on the host; objectives, chapter title, markers, matinee and streaming are
  host-local. Plan: a **sync channel** between the two DLLs (host → guest events: objective, chapter, markers by actor
  name, cutscene start/stop, teleports, "wait for the guest"), applied on the guest by memory writes (the objective
  manager shows a new objective when `DeferredAddObjective` is set, no script call needed).
- Deaths (B11): downed players never bleed out (`downedsec`, done); still to do: revive test both ways, automatic
  checkpoint restart when everybody is down (now the MISSION FAILED menu), guest respawn after a real death (KillZ).
0. Real Steam test with a friend (owner): both need the mod with `Binaries\foc_relay` (real steam_api.dll); host F10 -> Host
   (Through Steam), friend F10 -> Join -> "Join a friend through Steam".
**Before every DLL commit run the "must pass" set in [TESTS.md](TESTS.md).**
1. All 13 chapters together (stage 7, R1) with `-Look`; rerun the must-pass set
   with the desktop shown (last run had SKIPs).
2. Injected input via window messages (stage 6) so movement tests need no focus.
3. Stage 3: ProcessEvent looks unusable (script mostly native, see NOTES); go for native calls found in Ghidra.

## Owner request 2026-10-19: Nexus mods in the installer + settings menu (go given 2026-10-19, archives in Downloads)
- Done (not yet tested in game): installer page "Extra mods" (`installer/Mods.cs`) finds the archives in Downloads,
  extracts them with Windows tar.exe, backs up replaced files to `<game>\foc_backup\`; uninstall restores them.
  Audio logs (TransGame.xxx), FMV remaster (Movies\*.bik), intro skip (after FMV), Advanced Configurator (its
  dinput8.dll becomes `Binaries\TFOCAdvancedConfigurator.dll`, chain-loaded by our dinput8.dll).
- ReShade 6.8.0 bundled (installer/reshade: d3d9.dll from the official setup, preset "good color preset" by fpc without
  Clarity, SweetFX/bacondither shaders, licenses). No depth effects (normal ReShade blocks depth while network traffic).
- Checked 2026-10-19 in chapter 6 with host + guest (LAN and Steam mock): audio logs, FMV, intro skip, ReShade, then
  + Advanced Configurator: PASS, sync and waitguest work, F10 menu works. Configurator's DirectInput hooks fail next to
  ours (its F1 menu polls input itself; not checked by screenshot because it draws after our capture). Default off.
- Texture pack (Authentic HQ, mod 62) is a UPK Explorer texture pack: needs **TFC Installer** (nexusmods.com/site/mods/587,
  owner has to download it). Not wired yet.
- Next: install all into A and B, play chapter 6 with both, check F1 (configurator) + F10 menus and FPS cap vs network.
- Mods: nexusmods.com/transformersfallofcybertron/mods/17, /12, /9, /120, /62. Texture mods and audio-log mods: always
  installed; intro skip: a choice in the installer; other mods: decide per mod. All of them tested together with co-op
  (online, host + guest, matrix + playbot).
- Blocker: Nexus returns 403 to automated fetches and needs a login to download. The owner downloads the archives into
  `E:\FoC-coop\mods_incoming\`; check each mod's permission section before bundling it into a public installer (if
  redistribution is not allowed: the installer installs from archives the player downloaded, or ask the author).
- In-game settings: an F10 "Settings" page for the mod options (and the mod-pack options that can change at run time);
  options that need a restart say so.

## Goals in order
1. **R1: all 13 story chapters start for 2 players.** Automated check: both have a pawn within 90 s of the guest joining,
   both alive for 5+ min, both pawns move, no crash.
2. **Friends over a VPN** right after R1 (owner decision), with what works at that point.
3. **R2: chapter 1 start to finish for 2 players** (combat, revive, checkpoint, chapter transition).
4. **R3: 3-5 players.**
5. The whole campaign. Escalation is a parallel quick track.

Owner decisions (2026-10-19): gameplay tests run **inside the game** (state file, injected input, in-game screenshots);
the F10 menu test keeps real mouse and keyboard. Friends join over a VPN after R1.

## Stages
Link the bug each item addresses ([BUGS.md](BUGS.md)); move finished and verified items to [DONE.md](DONE.md).

### Stage 1. Foundation (B7, B8)
1. **Game-thread tick.** A timer thread posts `WM_APP+0x4F1` to the game window every 100-250 ms; `GetMsgProc`
   (`overlay.inc`) calls `GameTick()`. One pass over GObjects by class id (no `VirtualQuery` per object) fills a cache
   (controllers, pawns, Game, WorldInfo). The work of `CtrlShareThread`, hostfirst, `RescueThread`, `MaxPlayersThread`
   and `ForceCharThread` moves into the tick (same ini flags). Background threads keep only what is needed before the
   window exists (net driver heap patch, mutex).
2. **`foc_state.json`** (host and guest), written once per second (tmp file + rename): map, GameInfo class, role,
   per controller: local/remote, pawn, pawn class, xyz, spawn-helper state; guard and exception counters.
3. **`tools/matrix.ps1` asserts state** instead of relying on screenshots: polls both state files with timeouts, checks
   the mode first (map, game class, role), prints `RESULT: PASS/FAIL <map> <reason>`; a missing file or process is FAIL.
4. **Minidump** of the first unhandled access violation in TFOC.exe (`foc_crash_<pid>.dmp`, dbghelp), `crashtest` command.
5. **Old probes read the name from +0x18** (it is the packed FName at +4, `ObjNameStr`). Fix them and mark the two
   conclusions based on it in NOTES.md as unverified.
- Check: matrix PASS on `ESC_Ark_BASE_m`, `L01_TheExodus_BASE_m`, `L06_Megatron_BASE_m`; stat every 1-3 s (B8);
  10 starts of copy B without a crash (B7); `crashtest` writes a dump.

### Stage 2. B1 diagnosis from memory
Find `SpawnHelper` in the live Game object and its `Players` array (`{PC, GameState}`, 8 bytes each). Write each
player's state to the state file. Run 2b (bad), 6 (control), 1. State 0 = `CheckReadySpawn` fails, 1 = no spawn point,
2 = native `TrySpawnPlayer` fails, 3 = pawn spawned and lost. Record the result in BUGS.md.

### Stage 3. Script calls (gate G-A)
Find `ProcessEvent` two ways: the slot tracer with the correct name field (arg0 must be a `Function` object), and Ghidra
(xrefs of the ASCII `RestartPlayer` name → `eventXxx` wrappers → `FindFunctionChecked` + `[vtbl+N]`). Add
`FindFunction`, a game-thread `CallScript` wrapper (like `CallExecSafe`), the `call` test command, a smoke test
(`GetNumPlayers`), and property offsets by name (calibrate `UProperty::Offset` with known offsets).
Gate: no ProcessEvent after 2 sessions → `ULocalPlayer::Exec`, direct native calls, or memory fixes.

### Stage 4. Fix B1
`Game.RestartPlayer(guestPC)` when the guest has no pawn for N s; else `SpawnDefaultPawnFor` + `Possess` near the host;
else possess a spare pool pawn + teleport. Ini `guestspawn=1`. Check 2a, 2b, 3b, 4c, 8b, 9; twin `guestspawn=0` must FAIL.

### Stage 5. B2 by root cause
Rerun 2a, 5, 2b after B1. If still crashing: find the replicated actor/class at ghidra `0x857202` (dump + register
object names), the null static `UProperty*` and why; fix by cause. Revisit `ctrlshare` and remove guards that no
longer fire. Check: host and guest alive 10+ min in 2a, 5, 2b.

### Stage 6. In-game input and screenshots
`shot <file>` from the D3D9 backbuffer (replaces `CopyFromScreen`); `input hold <key> <ms>` through the DirectInput hook
for both windows at once (spike: focus spoof in test mode if needed); `move.ps1` uses it; twins in the suite.

### Stage 7. R1 sweep
Matrix over all 13 `L*_BASE_m` maps, per-chapter table in DONE/BUGS, gameplay after the opening scene in 3a/4a/4b.
Gate G2 (owner): who the guest plays per chapter, policy for special chapters (Grimlock, flying, driving, Bruticus).

### Stage 8. Friends over a VPN (right after R1)
VPN chosen by the owner (install only with OK), firewall rule, README guide, release zip (publish only after OK),
a session with a friend; findings go to BUGS.

### Stage 9. R2: chapter 1 end to end
Combat, revive (`ServerGiveRevive`, check B6), triggers/objectives from any player, death and checkpoint restart,
chapter transition (guest follows or auto-rejoins), only the host saves, bot with teleports/`BugItX/Y/Z`.

### Stage 10. 3-5 players
Copies `E:\FoC-coop\C`..., N guests in `dev.ps1`/`matrix.ps1`, N+1 controllers with pawns (Escalation, chapters 1 and 6).

### Stage 11. Escalation (when story work waits)
Waves 1-3 and match end on 4 maps, guest character choice, 4-5 players, B3.

### Later / not now (owner)
Whole campaign (M6-M8), coop-anything write-up and PR (only with OK), B5, B6, Steam relay without VPN, installer.
