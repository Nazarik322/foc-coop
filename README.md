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
co-op on top of the engine's own Unreal Engine 3 replication, using direct IP connections. It is a single
`dinput8.dll` that the game loads; it adds an **in-game menu** (press **F10**) for hosting and joining.

Supported game build: Steam, `TFOC.exe` SHA-256
`c1dab177f81503ecb831d63a3d799f928dd9823242ca3689482c684396acfedc`.

## Install

**Installer:** run `FoCCoopSetup.exe` → Next → Next → game folder (found automatically through your Steam libraries) →
Install. It puts `dinput8.dll` and the `foc_relay` folder into the game's `Binaries` (a foreign `dinput8.dll` is kept as
`dinput8.dll.foccoop-backup`), checks the `TFOC.exe` version and can remove the mod again (checkbox on the folder step).
A folder under Program Files asks for administrator rights.

**Playing with friends through Steam:** Steam running for everyone, you are Steam friends. Host: F10 → Host a game
("Through Steam" is on) → pick a chapter → Start hosting. Friend: F10 → Join a game → **Join a friend through Steam**.
No IP addresses or port forwarding (traffic goes over Steam's network as the Spacewar app, AppID 480).

By hand: `dinput8.dll` into `Binaries`, `FoCRelay.exe` and `steam_api.dll` into `Binaries\foc_relay`.
With no `foc_coop.ini` present, the co-op defaults are used.

## Using the in-game menu

Press **F10** in the main menu or in a match; **Esc** or **F10** closes it. The game ignores the keyboard and mouse
while the menu is open.

| Page | What it does |
|---|---|
| **Host a game** | Choose *Story* (a chapter) or *Escalation* (a map), set max players (2-5), press *Start hosting*. |
| **Join a game** | A list of lobbies found on your network (host name, mode, map, players, address) and a manual IP / port field. |
| **Leave to main menu** | Stops hosting / leaves the session. |
| **About / status** | Engine state, last command, instance name, the experimental *fast travel* switch. |

Hosting and joining **restart the game** straight into the session (about 20-30 seconds). Opening a map from the
already running main menu is rejected by the game's dead online layer ("You must be signed in to play online"),
while a map given on the command line works, so the menu relaunches the game with the right arguments.

Lobbies are announced with a UDP broadcast on your local network. For play over the internet use a VPN (or a
forwarded port) and type the host's address in the *Join* page. Real internet matchmaking does not exist.

## What works (tested)

Tested with two game copies on one PC, host and guest, through the menu:

| Mode | Result |
|---|---|
| **Escalation**: Ark, Gladiator, Iacon, Temple | Works for 2 players: both enter the match, pick characters and play. |
| **Story, chapter 1 (The Exodus)** | Works for 2 players (intro cutscene, tutorial, rescue teleport for the guest). |
| **Story, chapters 6 (Megatron) and 8a (Starscream)** | Both players are in gameplay with their own camera and HUD. |
| **Story, chapters 3a and 4a** | Both players are in the opening cutscene; gameplay after it not verified. |
| In-game menu, lobby list, host and join, restart into session | Works. |

## What does not work / known issues

- **Story chapters 2a, 2b, 3b, 4c, 8b, 9: the guest connects but gets no character** (stares at the sky, no HUD).
  Checked in 2b: the guest is not falling out of the map, it simply has no pawn. The host's controller list shows
  a pawn for the host only, and the level holds a pool of pre-spawned pawns that nobody possesses for the guest.
  The guest's character name is already set, so the spawn helper is not blocked on that. Fix needs a native `Possess` call.
- **Chapters 2a and 5: the host crashes about 50 seconds after the guest joins** (when the chapter's opening scene ends and the level script touches the guest's controller). Two null-object guards were added for it; the crashes then move to other places.
- **Chapter 4b** was shown as a cutscene for both players; gameplay not verified.
- **Guest visuals**: characters appear as dark silhouettes on the guest's screen in cutscenes.
- **The guest starts "downed" in chapter 1** and relies on the rescue teleport.
- **3-5 players** are not verified (only 2 players were tested).
- **Opening a session without a restart** is rejected by the game; the menu relaunches the game instead.
- Only the Steam build with the hash above is supported. Windows only (the game is 32-bit).
- Cutscenes and scripted events are only partly synchronised. Versus multiplayer modes are not attempted.

## Configuration

Optional `foc_coop.ini` next to the exe, section `[foc_coop]`. **Without this file all co-op features are on.**

| Key | Effect |
|---|---|
| `instance=B` | Suffix for the single-instance mutex and the player name, so several copies run on one PC |
| `tcpdriver=1` | Use `IpDrv.TcpNetDriver` instead of the Demonware driver |
| `forcechar=1` | Force "characters loaded" (needed to leave the Escalation loading screen) |
| `maxplayers=N` | Player limit on the host |
| `tutguard=1`, `skiptut=1` | Guards and patches for the tutorial logic that crashes the campaign host |
| `ctrlshare=1`, `hostfirst=1` | Controller sharing and host-first controller order (campaign) |
| `rescue=1` | Teleport a fallen pawn back next to another player |
| `ctrldiag`, `scan`, `dump`, `trace`, `lp` | Diagnostics and experiments that write extra logs |

All activity is logged to `foc_coop.log` next to the exe (network calls, menu actions, exceptions).

## How it works

The proxy `dinput8.dll` sits next to `TFOC.exe` and is loaded instead of the system DLL. It:

- hooks Direct3D9 (`IDirect3D9::CreateDevice` then the device's `EndScene`) and draws the menu with Dear ImGui,
  and zeroes DirectInput state while the menu is open;
- switches the net driver to TCP/IP in the loaded config strings, raises the player limit and forces
  `TnLocalPlayer._CharactersLoaded`;
- patches the tutorial logic that crashes the campaign host, shares and orders player controllers so a guest can spawn;
- announces hosted games with a UDP broadcast (port 47799) and lists them in the join page;
- can call the engine's `Exec` (`open <url>`) on the game thread (the experimental *fast travel*).

## Building

- `build.bat` (Visual Studio 2022 Build Tools, x86) builds `build\dinput8.dll` from `src/proxy_dinput8/`
  (`dinput8.cpp` plus `overlay.inc`; Dear ImGui is vendored in `third_party/imgui`, MIT license).
- `launcher/FoCCoop.exe` is a small **developer** helper that installs the DLL into test copies and starts sessions
  from the command line; players do not need it. It is a single C# file compiled with the .NET Framework `csc.exe`.
- `tools/dev.ps1`, `tools/e2e_menu.ps1`, `tools/matrix.ps1` drive two game copies for tests (real key / mouse
  events, screenshots, a map matrix). `lpdiff.py` and `snapdiff.py` diff memory snapshots.

## Roadmap

1. Spawn a character for the guest in every story chapter; fix the chapter 5 host crash.
2. Guest visuals (dark silhouettes) and the downed start.
3. 3-5 players.
4. Internet play without a VPN (relay).
5. Installer.

## Disclaimer

This is interoperability research for personal use with a legitimately owned copy of the game. Use it at your
own risk. It is not intended for use with any official online services.
