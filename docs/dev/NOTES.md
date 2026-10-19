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
