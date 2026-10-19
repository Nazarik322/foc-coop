<div align="center">

# foc-coop

**Unofficial co-op mod for Transformers: Fall of Cybertron**

English | [Русский](README.ru.md)

![status](https://img.shields.io/badge/status-early%20development-orange)
![platform](https://img.shields.io/badge/platform-Windows%20x86-blue)
![game](https://img.shields.io/badge/game-Steam%20build%2012831-lightgrey)

</div>

---

> **Early development build.** This is a proof of concept, not a finished mod. Expect crashes, visual glitches
> and missing features. The repository contains no game files; you need your own copy of the game.
> Not affiliated with Hasbro, High Moon Studios or Activision.

The Fall of Cybertron campaign is single-player and its official online services are gone. This project adds
co-op on top of the engine's own Unreal Engine 3 replication, using plain TCP with a direct IP address.
It is a `dinput8.dll` proxy that is loaded by the game, plus a small launcher.

Supported game build: Steam, `TFOC.exe` SHA-256
`c1dab177f81503ecb831d63a3d799f928dd9823242ca3689482c684396acfedc`.

## Status

| Feature | State |
|---|---|
| Escalation, 2 players (Ark map tested) | Works |
| Campaign chapter 1 "The Exodus", 2 players | Works, with a rescue teleport for the guest |
| Direct-IP connection (no online services) | Works |
| Launcher (install, host, join) | Works |
| Several game copies on one PC | Works |
| Campaign chapters 2-9 | Untested |
| 3-5 players | Untested |
| Play over the internet | Not solved (LAN, localhost or VPN only) |
| Versus modes other than Escalation | Not attempted |
| Installer | Not available |

### Known issues

- Characters look like dark silhouettes or have dark textures on the guest's screen.
- The guest starts the campaign "downed" and relies on the rescue teleport.
- Cutscenes and scripted sequences are only partly synchronised.
- Only the Steam build with the hash above is supported; patches and offsets are build-specific.
- Windows only (the game is 32-bit).

## Quick start

You need two copies of the game folder, one for the host and one for the guest. Do not patch your Steam
installation; work on copies.

1. Run `launcher/FoCCoop.exe` and point it at each game copy. It checks the exe hash, installs `dinput8.dll`
   and writes `foc_coop.ini`.
2. **Host:** choose a campaign chapter or an Escalation map and press *Host*.
3. **Guest:** enter the host IP (`127.0.0.1` on the same PC) and port `7778`, then press *Join*.

Manual equivalent: the host opens
`ESC_Ark_BASE_m?Game=TransContent.TnSurvivalGame?listen?MaxPlayers=5` and the guest connects to `127.0.0.1:7778`.

## Configuration

Settings are read from `foc_coop.ini` next to the game exe, section `[foc_coop]`.

| Key | Effect |
|---|---|
| `instance=B` | Suffix for the single-instance mutex, so several copies can run on one PC |
| `tcpdriver=1` | Use `IpDrv.TcpNetDriver` instead of the Demonware driver |
| `forcechar=1` | Force "characters loaded" (needed to leave the Escalation loading screen) |
| `maxplayers=N` | Player limit on the host |
| `tutguard=1`, `skiptut=1` | Guards and patches for the tutorial logic that crashes the campaign host |
| `ctrlshare=1`, `hostfirst=1` | Controller sharing and host-first controller order (campaign) |
| `rescue=1` | Teleport a fallen pawn back next to another player |
| `holdspawn=1`, `spawnafter=N` | Delay the guest's spawn |
| `ctrldiag`, `scan`, `dump`, `trace`, `lp` | Diagnostics and experiments that write extra logs |

All activity is logged to `foc_coop.log` next to the exe (network calls, exceptions, crash registers and stack).

## How it works

The proxy `dinput8.dll` sits next to `TFOC.exe` and is loaded instead of the system DLL. At runtime it:

- locates the engine's name and object tables and the classes it needs,
- switches the net driver to TCP in the loaded config strings,
- raises the player limit and forces `TnLocalPlayer._CharactersLoaded`,
- patches the tutorial logic that crashes the campaign host,
- shares and orders player controllers so that the guest can spawn.

## Building

- Proxy DLL: run `build.bat` (Visual Studio 2022 Build Tools, x86). Source: `src/proxy_dinput8/`.
- Launcher: a single C# file, `launcher/FoCCoop.cs`, compiled with the .NET Framework `csc.exe` that ships
  with Windows (C# 5 syntax).
- Prebuilt `launcher/FoCCoop.exe` and `launcher/payload/dinput8.dll` are included.
- `lpdiff.py` and `snapdiff.py` are helper scripts for diffing memory snapshots of the running game.

## Roadmap

1. Fix the guest's visuals and the downed start.
2. Play through chapters 2-9.
3. Support 3-5 players.
4. Internet play (Steam relay or a VPN guide).
5. Installer.
6. More Escalation maps and versus modes.

## Disclaimer

This is interoperability research for personal use with a legitimately owned copy of the game. Use it at your
own risk. It is not intended for use with any official online services.
