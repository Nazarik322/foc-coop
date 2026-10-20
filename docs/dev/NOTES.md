# Technical notes

Facts about the game and the tools, collected while working. Update them when something turns out to be wrong.
Related: [BUGS.md](BUGS.md), [PLAN.md](PLAN.md), [DONE.md](DONE.md).

## Binary and addresses
- Steam `TFOC.exe` build 12831, SHA-256 `c1dab177...acfedc`. 32-bit UE3.
- Ghidra image base is 0x400000, and the usual runtime base is 0x470000. **Runtime = ghidra + (base − 0x400000)**, and the
  base-relative offset = ghidra VA − 0x400000. The base can differ (ASLR, for example 0xDC0000), so always compute from
  `GetModuleHandle(nullptr)`. The log now prints `ghidra=<VA>` for exceptions in TFOC.exe and CRASHSTACK in ghidra VAs.
- `GEngine` global: ghidra VA `0x19d1fdc`. `UGameEngine::Exec` is ghidra `0x77ba80` (thiscall, this = GEngine+0x28,
  args `(const wchar_t* Cmd, FOutputDevice*)`). It must run on the game thread (WH_GETMESSAGE hook) through the
  `CallExecSafe` asm wrapper.
- `GMalloc`: ghidra global `0x19117b8`. `appRealloc(p, size, align)` is vtable slot 3 (+0xc). Wrapper: ghidra `0xaf5ff0`.
- GObjects array: `kGObjRva` in `dinput8.cpp`. `FName::FName(const wchar_t*, ...)` = ghidra `0xcc13e0`; engine names are
  registered as UTF-16 (`TEXT()`) in per-file static initialisers (`push 1; push 1; push L"Name"; mov ecx, <global>;
  call 0xcc13e0`), about 40 copies per name. Those globals show no readers in Ghidra, so they do not lead to ProcessEvent.
- Known benign fault: the game writes to address 0 at ghidra `0x55432c` once at startup and handles it itself.

## Object header (this build)
- +0 vtable, +4 packed FName = (GNames index << 14) | number (instance suffix = number − 1; class objects, `Default__`
  objects and archetypes have number 0), +0x14 low 13 bits = class id, +0x18 low 18 bits = object index + 1.
- **+0x14 bits 13..30 are not the outer.** Following them from a TnWorldInfo instance gives the archetype chain
  `Default__TnWorldInfo / Default__HmWorldInfo / Default__WorldInfo / ... / Default__Actor`.
- A class object has class id 3 (`Class`) and keeps its own id at +0x84; its instances carry that id + 1 at +0x14.
- Early probes read the name from +0x18 (wrong). Two conclusions in coop-anything NOTES rest on that and are **unverified**:
  "many script functions have no function object in GObjects" and "the vtable slot tracer did not find ProcessEvent".

## Object layout (offsets that were confirmed)
- PlayerController: `Pawn` +0x1d0, `PlayerReplicationInfo` +0x1d4, `StartSpot` +0x220 (e.g. `TnCampaignPlayerStart_1062`),
  `Player` +0x344 (a `TnLocalPlayer_*` means the local controller; otherwise it is a remote guest), NextController +0x1dc.
- Actor `Location`: +0xe4 (x, y, z floats), copy at +0x134.
- TnPlayerReplicationInfo `_SelectedCharacter.CharacterName`: FString {Data, Num, Max} at +0x34c.
- TnLocalPlayer: +0x2d8 bit field (bit1 `_CharactersLoaded`), +0x2dc FString of 0x26 chars (layout check).
- GameInfo: +0x230 MaxPlayers, +0x234 MaxPlayersAllowed, +0x238 NumPlayers.
- **TnCampaignGame +0x378 = SpawnHelper** (`TnPlayerSpawnHelperCampaign`); **helper +0x28 = `Players`** TArray of
  8-byte entries {PC, GameState byte}. Found at run time by `TickSpawnHelper` (gametick.inc), 2026-10-19, chapter 6.
- UWorld: objects named `TheWorld` (class `World`, name number 0, several exist). **World +0x15c = URL** (FURL: Protocol
  "unreal", Host, Port, Map at +0x1c, ...), so the map name is the FString at World+0x178.

## ProcessEvent search (2026-10-19)
- Slot tracer with the right name field: hits mostly land in identical-code-folded stubs (`return 0`, `ret`, `movzx eax,
  word [ecx+0x34]`) shared by many vtables, so arg0 is noise. No ProcessEvent from it.
- Only **2673** objects of class `Function` exist at run time (chapter 6) and none of them holds a code pointer in
  +0x20..+0x1a0 (`pefind=1`). Likely most UnrealScript is compiled to C++ by High Moon; calling script functions through
  ProcessEvent may not be possible. Native calls (Ghidra) are the more promising route.

## Spawn path (native, found 2026-10-19 with `spawntrace=1`)
- `0x759d80` StaticAllocateObject <- `0x77ce70` <- `0xb93390` <- `0x88d4c0` SpawnActor <- `0x54a8f0` <- `0x5abb10`
  SpawnDefaultPawnFor (virtual) <- `0x7c2ee0` **RestartPlayer** (game vtable +0x2b4 is the spawn call; controller vtable
  of the PC+0x10 subobject: +0x278 Possess = `0x5eca60` -> `0xb279d0` Controller::Possess).
- `0xb560b0`: "role is not authority on a networked actor" check (WorldInfo +0x3e4 NetMode, actor +0x5e Role).
- Controller::Possess skips when PRI+0x210 bit1 (spectator) is set.
- Script state of an object: state frame pointer at +0x28, word 1 = UState node (e.g. PlayerWalking, Dead).
- Natively the pawn of a controller passed as `this+0x10` is at `this[0x70]` (= PC+0x1d0).

## Script facts (decompiled UnrealScript in `coop-anything/local/script`)
- Spawn flow: `TnGame.PostLogin` calls `SpawnHelper.Login(PC)` for remote controllers (`TnGame.uc:481`).
  `TnPlayerSpawnHelper.Tick` per player: state 0 `CheckReadySpawn` (CanRestartPlayer + HasSelectedCharacter) → 1
  `FindSpawnPoint` (`Game.FindInitialPlayerStart`) → 2 `TrySpawn` (`TnGame.TrySpawnPlayer`, native) → 3 in game.
- Coop spawn points: Kismet `TnSeqAct_SetCoopSpawnPointSet` sets the game's `SpawnPointArray` and `UseSpawnPointArray`.
- `TnGame.SpawnDefaultPawnFor` honours the URL options `BugItX/Y/Z/Yaw` (`TnGame.uc:1124`): a way to place spawns in tests.
- `TnPlayerController.ServerRestartPlayer` (dead state) → `TnGame.RequestRespawn` → `RespawnHelper.RequestRespawn`.
- Story chapters spawn the hero from Kismet (`SeqAct_TnPawnFactory`). The level keeps a pool of `TnPlayerPawn_*`.

## Crash guards (VEH, `dinput8.cpp`, enabled by `tutguard`)
| ghidra | condition | emulation | counter (`guards` in the state file) |
|---|---|---|---|
| 0xc2b010 (+6 fault) | ecx == 0 | xor eax,eax; pop x4; ret 8 | 0 |
| 0x93ca70 (+6 fault) | esi == 0x10 | return 0x10 | 1 |
| 0x6cb310 (+8 fault) | esi == 0 | pop edi; pop esi; ret 4 | 2 |
| 0x4e0ca0 (entry) | eax == 0 | eax = 0; ret | 3 |
| 0x660130 (+9 fault) | esi == 0 | pop edi, esi, ebx; ret | 4 |
| 0x641e10 (+7 fault) | ebp == 0x10 or 0 | pop ebp, ebx; ret 8 (8b, tail of Possess) | 5 |
| 0xb4e5a0 ServerUpdatePing (+0x70 fault) | eax == 0 (PRI) | pop edi, ebx; ret 4 | 6 |

## DLL structure (since 2026-10-19)
- **Game-thread tick** (`gametick.inc`): `TickThread` posts `WM_APP+0x4F1` every `tickms` (default 200) ms, `GetMsgProc`
  runs `GameTickSafe()` on the game thread. One pass over GObjects by class id collects controllers, player pawns, the
  game, WorldInfos, local players, spawn helpers and worlds. forcechar, maxplayers, ctrlshare, hostfirst, rescue,
  ctrldiag and the spawn-helper read all run there; no background thread writes engine objects any more.
  A tick costs about 1 ms (first one about 100-130 ms while classes are registered).
- **`foc_state.json`** next to the exe, rewritten once per second: `t`, `tick`, `tickMs`, `url`, `map`, `game`, `role`
  (`host` if a GameInfo exists, else `client`), `port` (host's bound port), `players[]` {local, pc, pawn, x, y, z, spawn,
  start, char}, `pawns[]` (all TnPlayerPawn objects, possessed or not), `guards[5]`, `excExe` (unguarded faults in
  TFOC.exe), `excBenign`, `excSelf` (our own handled probe faults), `dumps`.
- **Crash dumps**: the first two unguarded access violations in TFOC.exe write `foc_crash_<pid>_crashN.dmp` next to
  the exe (dbghelp `MiniDumpWriteDump`), plus a `CRASHOBJ:` log line with the engine objects in registers/stack.
  `crashtest` in `foc_cmd.txt` writes `foc_crash_<pid>_test0.dmp` without crashing.
- Log line `stat:` every 3 s (LOCAL/REMOTE, pawn, xyz, spawn state); player pawns every 15 s.

- `sync.inc`: sync channel host -> guest (objectives, markers, wait for the guest, movie hooks), test commands
  `tpmarker`; included after gametick.inc.
## Steam relay
- `Binaries\foc_relay\FoCRelay.exe` + `steam_api.dll` (real for players, mock in test copies via `Deploy-Relay`);
  `relay.status` (role, state, lobby, peers, tx, rx) and `relay.cmd` (`quit`, `invite`) next to it.
- The host's first bind in 7777..7800 is an early LAN socket (7777); the listen socket (7778) comes with the map. The
  host relay waits until the tick sees the game and a controller.
- Mock: `%TEMP%\foc_mock_steam` (users/, lobbies/), SteamID from the exe folder, messages on 127.0.0.1:41000+.
- Real Steam on this PC: relay and TFOC.exe (213120) run side by side on one account.

## Testing
- Copies: `E:\FoC-coop\A` (host) and `B` (guest, `instance=B`). Never touch the Steam install in Program Files.
  `Stop-Foc` (dev.ps1) stops only processes started from `E:\FoC-coop\*`.
- Build and deploy: `Build-Deploy` in `tools/dev.ps1` (runs `build.bat` through `cmd.exe /c`, copies to A and B).
- `tools/matrix.ps1 -Maps <map,...> [-Move] [-Keep] [-AliveSec N]`: host on A, guest on B at the host's real port; checks
  the state files (map and game class first, then guest joined, every player has a pawn, optional movement, both alive
  and updating, no unguarded exceptions) and prints `RESULT: PASS/FAIL <map> <reason>` and `SUMMARY`. Logs, state
  files and dumps go to `-Out` (default `%TEMP%\foc_matrix`).
- `Read-FocState` / `Wait-FocState` (dev.ps1) read and poll `foc_state.json`.
- Keys for DirectInput must be sent as **scan codes** (`HoldKey`, flag KEYEVENTF_SCANCODE): W 0x11, A 0x1E, S 0x1F, D 0x20.
  The VK-only `[W]::Key` works for menus (Enter, arrows).
- **Display needed**: in a remote desktop session the game only starts while the session is actually shown (an RDP
  window that is minimized or not displayed leaves the game without a D3D device and it crashes at start, see B7).
- Ghidra headless (project `coop-anything/local/ghidra_proj`, add `-readOnly`): `analyzeHeadless.bat <proj> FoC -process
  TFOC.exe -noanalysis -readOnly -scriptPath <ghidra_scripts> -postScript <Script>.java <out> <args...>`.
  Scripts: `DecompContaining.java <out> <VA...>` (function containing an address, instructions around it, callers,
  decompiled C; never creates functions), `NameEventCalls.java <out> <maxGlobals> <name...>` (FName registrations of a
  name), `CallPairs.java`, `DecompAt.java`, `XrefStrings.java`, `VtCalls.java`, `TopCallees.java`.

## Script variable offsets by reflection (2026-10-19 night)
- `props <name...>` (foc_cmd.txt) logs the UProperty objects with that name. **Offset = (word at +0x30) >> 20**
  (checked: Controller.Pawn 0x1d0, PlayerReplicationInfo 0x1d4, Actor.Location 0xe4). BoolProperty bit mask = word
  +0x40.
- TnPawn: +0x350 Health (float), +0x354 HealthMax, +0x5d0 bit 0x80000 `_IsDowned`, bit 0x20000 `_DrainHealthWhenDown`,
  bit 0x40000 `_CanHealWhenDown`, +0x84c DownedHealth (default 400), +0x850 DownedHealthDrainRate (40/s).
  Death while downed: Health <= -DownedHealth. Revive heals -Health + RevivalHealth. KillZ damage skips downed.
- WorldInfo: +0x37c TimeDilation, +0x394 Pauser (PlayerReplicationInfo; non-null pauses the world, net keeps running),
  +0x1e0 bit 0x80 bPlayersOnly. TnWorldInfo +0x5d0 `_ObjectiveManager`.
- TnObjectiveManager: +0x28 LocalObjectiveMarkers (TArray, 0x1c per entry: +0 Description FString, +0xc MarkerBase,
  +0x10 MarkerId, +0x14 MarkerTypeClass, +0x18 MarkerType instance), +0x34 ObjectiveMarkerCounter,
  +0x38 LocalObjectiveMarkerCounter, +0x3c CurrentObjective, +0x40 LastCompletedObjective, +0x44 LastObjective,
  +0x48 ObjectiveBank (TArray<FString>), +0x54 DeferredAddObjective, +0x60 DeferredCompleteObjective,
  +0x6c bit 1 bClearObjective. The HUD calls Update every frame: a non-empty DeferredAddObjective is shown as NEW
  OBJECTIVE and cleared.
- GMalloc (ghidra 0x19117b8) vtable slot 3 = Realloc(ptr, size, align), thiscall; `GameRealloc` in sync.inc calls it as
  fastcall with a dummy edx. Use it for FString/TArray memory the game may free.

## Death and game over (B11)
- Game over = GotoState('CampaignGameOver') through the thunk ghidra `0x7eb060` (`this` = object + 0x10, plain ret,
  no stack args); called on the UI controller (host: on any player's death; guest: on its own death).
- Native TrySpawnPlayer = ghidra `0x8a35b0`: role check, then RestartPlayer (game vtable +0x2b0 = `0x7c2ee0`), returns
  Controller.Pawn != null. RestartPlayer spawns only when Controller.Pawn (+0x1d0) is null.
- Spawn helper Tick (TnPlayerSpawnHelper.uc): state 2 calls TrySpawn every tick until it succeeds.

## Steam app id (Spacewar)
- TFOC has the old Steam DRM (Steam.dll, `TFOC.exe.<pid>.STEAMSTART` files) but runs fine started directly with the
  environment `SteamAppId=480`/`SteamGameId=480`: Steam then reports RunningAppID 480. DllMain does that restart.

## Sync channel host -> guest (sync.inc, ini `sync`, default on)
- Datagrams inside the game's own UDP socket: host -> guest "FOCS1\n" + `key=value` lines 4x/s (seq, map, obj,
  mk=<actor>,<marker class>,<id>), guest -> host "FOCR1\n" (= ready). h_recvfrom swallows them, so the game never sees
  them; works over LAN, VPN and the Steam relay (relay forwards datagrams).
- Peers = addresses that sent game datagrams in the last 10 s. Snapshots are full state: losing one is harmless.
- Movies: BinkOpen/BinkClose (binkw32.dll imports `_BinkOpen@8`, `_BinkClose@4`) hooked in the IAT; a file from the
  Movies folder open = a movie plays.

## Script calls (2026-10-20)
- FName from a string: ghidra 0xcc13e0 (thiscall on a 4-byte packed FName: name, 0, 1). FindFunction: object vtable
  +0xC8 (FName, 0) - finds nothing on the native-only TnCampaignGame; CallScript then takes the Function object from
  GObjects. ProcessEvent: ghidra 0x728c60 (thiscall: Function, Parms, Result); UFunction +0x5e ParmsSize, +0x54 flags.
- GotoState: ghidra 0x70df30 (thiscall: FName state, label 0, 0, 0). ScriptConsoleExec: 0xbf1f70.
- Not callable (no Function object): GetNumPlayers, SetLocation, NotifyChapterChanged, RestartFromActiveCheckpoint
  (native only), ClientTeleport. Callable: ClientSetLocation (moves the local pawn), TrySpawnPlayer (false for a dead
  player), TestDowned/TestRevive, exec functions.
- PlayerController: +0x360 bit 0x20000 bCinematicMode, +0x3b4 myHUD. PRI +0x1e0 PlayerName. TnSeqAct_SetChapter +0x8c
  ChapterNumber. GameInfo +0x280 GameRulesModifiers, GameRules +0x1d0 NextGameRules.
- 2026-10-20: only events and RPCs seem to keep Function objects (script mostly compiled to native). Callable:
  ClientSetCinematicMode (PC, 5 bools: on, movement, turning, HUD, buttons), SequenceOp `Activated` (event).
  Not callable: SetCinematicMode, Actor/TnPawn.SetHidden, TnHUD.NotifyChapterChanged; `funcs` finds only a handful.
- Actor: +0x84 bit 4 bHidden (writing it changes nothing on screen), +0x8c bit 0x80000 bForceNetUpdate.
  UStruct SuperStruct offset is calibrated at runtime (sync.inc SuperOffset, Pawn -> Actor).
- SequenceAction.Targets: TArray at +0x7c. Chapter 6 (L06) has TnSeqAct_SetChapter objects with numbers 0, 10, 10
  (on the guest too, the level's Kismet objects exist on the client).

## Test commands (foc_cmd.txt in Binaries, one line)
`shot <file>`, `shotui <file>`, `menu <page>|off`, `dump <Class> <file> [bytes]`, `props <names>`, `fall local|remote`,
`tp local|remote x y z` (only moves falling pawns; standing pawns are put back by physics), `hp local|remote <value>`,
`tpmarker [dist]` (same limit), `lpexec <cmd> @<offset>` (experiment, use oexec), `oexec <target> <exec cmd>`,
`call <target> <Function> [i: f: b: o:]`, `funcs <name parts>`, `gotostate <target> <State>`, `key <vk>`,
`vtfind <target> <ghidra hex>`, `restartplayer` (crashes the host, do not use), `poke`, `crashtest`, `steamjoin`;
targets: pc pawn rpc rpawn game pri rpri obj:Name_N. Any other text = engine console command.
