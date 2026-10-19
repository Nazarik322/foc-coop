# Technical notes

Facts about the game and the tools, collected while working. Update them when something turns out to be wrong.
Related: [BUGS.md](BUGS.md), [PLAN.md](PLAN.md), [DONE.md](DONE.md).

## Binary and addresses
- Steam `TFOC.exe` build 12831, SHA-256 `c1dab177...acfedc`. 32-bit UE3.
- Ghidra image base is 0x400000, and the usual runtime base is 0x470000. **Runtime = ghidra + (base − 0x400000)**, and the
  base-relative offset = ghidra VA − 0x400000. The base can differ (ASLR, for example 0xDC0000), so always compute from
  `GetModuleHandle(nullptr)`.
- `GEngine` global: ghidra VA `0x19d1fdc`. `UGameEngine::Exec` is ghidra `0x77ba80` (thiscall, this = GEngine+0x28,
  args `(const wchar_t* Cmd, FOutputDevice*)`). It must run on the game thread (WH_GETMESSAGE hook) through the
  `CallExecSafe` asm wrapper.
- `GMalloc`: ghidra global `0x19117b8`. `appRealloc(p, size, align)` is vtable slot 3 (+0xc). Wrapper: ghidra `0xaf5ff0`.
- GObjects array: `kGObjRva` in `dinput8.cpp`.

## Object layout (offsets that were confirmed)
- PlayerController: `Pawn` +0x1d0, `PlayerReplicationInfo` +0x1d4, `Player` +0x344
  (a `TnLocalPlayer_*` means the host's own controller; otherwise it is a remote guest).
- Actor `Location`: +0xe4 (x, y, z floats).
- TnPlayerReplicationInfo `_SelectedCharacter.CharacterName`: FString {Data, Num, Max} at +0x34c.
- Live spawn helper object is `TnPlayerSpawnHelperCampaign_<n>`. Objects ending in `_0` are templates.
  The `Players` array offset is **unknown** (+0x108 is wrong on the live object).

## Script facts (decompiled UnrealScript in `coop-anything/local/script`)
- Spawn flow: `TnPlayerSpawnHelper.Tick` runs per player: state 0 `CheckReadySpawn` (CanRestartPlayer + HasSelectedCharacter)
  → 1 `FindSpawnPoint` → 2 `TrySpawn` (`TnGame.TrySpawnPlayer`, native) → 3 in game.
- Story chapters spawn the hero from Kismet (`SeqAct_TnPawnFactory`). The level keeps a pool of `TnPlayerPawn_*`.

## Crash guards (VEH, `dinput8.cpp`, enabled by `tutguard`)
| ghidra | condition | emulation |
|---|---|---|
| 0x6cb310 (+8 fault) | esi == 0 | pop edi; pop esi; ret 4 |
| 0x660130 (+9 fault) | esi == 0 | pop edi, esi, ebx; ret |
| 0x4e0ca0 (entry) | eax == 0 | eax = 0; ret |

## Testing
- Copies: `E:\FoC-coop\A` (host) and `B` (guest, `instance=B`). Never touch the Steam install in Program Files.
- Build and deploy: `build.bat`, then copy `build\dinput8.dll` to both `Binaries` (scratchpad `bd.ps1` does both).
  Run `build.bat` through `cmd.exe /c "<full path>"`, otherwise PowerShell cannot find it.
- `tools/matrix.ps1 -Maps <map> [-Keep]`: host, then guest on 127.0.0.1:7778, screenshots in `%TEMP%\foc_matrix`.
- Keys for DirectInput must be sent as **scan codes** (`HoldKey`, flag KEYEVENTF_SCANCODE): W 0x11, A 0x1E, S 0x1F, D 0x20.
  The VK-only `[W]::Key` works for menus (Enter, arrows).
- Host log `E:\FoC-coop\A\Binaries\foc_coop.log`: `rescue: stat` (pawns of both), `pawns in world`, `EXCEPTION`, `guard:`.
- Ghidra headless: `analyzeHeadless.bat <proj> FoC -process TFOC.exe -noanalysis -scriptPath ghidra_scripts
  -postScript DecompAt.java <out> <ghidraVA...>` (project `coop-anything/local/ghidra_proj`).
