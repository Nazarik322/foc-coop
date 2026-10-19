# foc-coop — co-op for Transformers: Fall of Cybertron (DEV / work in progress)

**Languages:** **English** | [Русский](README.ru.md)

Unofficial co-op layer for **Transformers: Fall of Cybertron** (Steam app 213120, build 12831,
`TFOC.exe` SHA-256 `c1dab177f81503ecb831d63a3d799f928dd9823242ca3689482c684396acfedc`).
The campaign is single-player and the official online services are dead, so this project builds
co-op on top of the engine's own Unreal Engine 3 replication, using plain TCP (direct IP).

> **Status: early development build.** It is a proof of concept, not a finished mod. Expect crashes,
> visual glitches and missing features. No game files are included; you need your own copy of the game.
> Not affiliated with Hasbro, High Moon Studios or Activision.

(Кратко по-русски: ранняя dev-версия кооператива для Fall of Cybertron по прямому IP. Работают
Escalation на двоих и первая глава кампании на двоих; остальное не проверено. Нужна своя копия игры.)

## What works

- **Two-player Escalation** (Ark map tested) between two game copies on one PC: host and client both
  get into the match and play.
- **Campaign chapter 1 (The Exodus), two players**: the host starts the map, the second player joins by
  direct IP and loads in. A *rescue teleport* brings a fallen/stuck pawn back to the host.
- **Direct-IP networking** through `IpDrv.TcpNetDriver` (the Demonware driver is switched off).
- **Launcher** (`launcher/FoCCoop.exe`, WinForms): installs the proxy DLL into a game copy and starts a
  host or guest session; chapter / Escalation map pickers, player count, port, windowed mode.
- **Several game copies on one PC** (`instance=` renames the single-instance mutex) for testing.

## What does not work / known issues

- **Guest visuals**: on the client some characters appear as dark silhouettes / with dark textures.
- **Guest starts downed** in the campaign and relies on the rescue teleport.
- **Chapters 2–9 are untested** (they are in the launcher list but may crash or soft-lock).
- **3–5 players are untested**; only 2 players were verified.
- **Internet play is not solved**: only LAN / localhost / VPN with a reachable IP. No Steam relay yet.
- Cutscenes and scripted sequences are only partly synchronised.
- Versus multiplayer modes other than Escalation: not attempted.
- No installer; the launcher needs a copy of the game that you point it at (it checks the exe hash).
- Only the Steam build with the SHA-256 above is supported (offsets/patches are build-specific).
- Windows only, 32-bit game.

## How it works

`src/proxy_dinput8/dinput8.cpp` builds a 32-bit `dinput8.dll` proxy that is dropped next to `TFOC.exe`.
The game loads it instead of the system DLL; it then
- finds engine structures (GNames / GObjObjects) and needed classes at runtime,
- swaps the net driver to TCP in the loaded config strings,
- raises the player cap, forces `TnLocalPlayer._CharactersLoaded`, patches the tutorial logic that
  crashes the campaign host, and shares/orders player controllers so the guest can spawn,
- logs everything to `foc_coop.log` (network calls, exceptions, crash registers/stack).

Settings live in `foc_coop.ini` next to the exe, section `[foc_coop]`:

| key | effect |
|---|---|
| `instance=B` | appended to the single-instance mutex name so several copies run on one PC |
| `tcpdriver=1` | use `IpDrv.TcpNetDriver` instead of Demonware |
| `forcechar=1` | force the characters-loaded flag (needed to leave the Escalation loading screen) |
| `maxplayers=N` | player cap for the host |
| `tutguard`, `skiptut` | guards/patches for the tutorial script that crashes the campaign host |
| `ctrlshare`, `hostfirst` | controller sharing and host-first controller order (campaign) |
| `rescue=1` | rescue teleport for fallen pawns |
| `holdspawn`, `spawnafter` | delay the guest spawn |
| `ctrldiag`, `scan`, `dump`, `trace`, `lp`, ... | diagnostics and experiments, write extra logs |

## Quick start (2 players, one PC or LAN)

1. Make **two copies** of your game folder (never patch the Steam install itself).
2. Run `launcher/FoCCoop.exe`, point it at each copy; it installs `dinput8.dll` and writes `foc_coop.ini`.
3. Host: pick a chapter (or an Escalation map) and press Host.
   Guest: enter the host IP (`127.0.0.1` on the same PC), port `7778`, press Join.
4. Manual equivalent: the host opens `ESC_Ark_BASE_m?Game=TransContent.TnSurvivalGame?listen?MaxPlayers=5`,
   the guest opens `127.0.0.1:7778`.

## Build

`build.bat` (Visual Studio 2022 Build Tools, x86) builds the proxy DLL. The launcher is a single C# file
compiled with the .NET Framework `csc.exe` that ships with Windows (C# 5 syntax). Prebuilt
`launcher/FoCCoop.exe` and `launcher/payload/dinput8.dll` are checked in for convenience.
`lpdiff.py` / `snapdiff.py` are helpers for diffing memory snapshots of the running game.

## Roadmap

1. Fix guest visuals and the downed start. 2. Walk through chapters 2–9. 3. 3–5 players.
4. Internet play (Steam relay / VPN guide). 5. Installer. 6. More Escalation maps and versus modes.

## Disclaimer

Interoperability research for personal use with a legitimately owned copy of the game. Use at your own
risk. Not intended for use with any official online services.
