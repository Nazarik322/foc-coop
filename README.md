# foc-coop

**Co-op for Transformers: Fall of Cybertron** — play the story and Escalation with friends (up to 5).
Unofficial, early development. English | [Русский](README.ru.md)

## Install

1. Run **`FoCCoopSetup.exe`**.
2. Next → Next → the game folder is found automatically (or press Browse) → **Install**.

That's it. To remove the mod, run the installer again and tick "Remove the mod".

**Extra mods (optional).** The installer can also put in these Nexus mods if you downloaded them (it finds the archives
in your Downloads folder): High Quality Audio Logs, FMV Remaster, No-Intro Splash Fix, TFOC Advanced Configurator (F1
menu; runs together with the co-op mod). ReShade 6.8 with a colour/sharpness preset is bundled (on by default, Home opens
its menu; colour effects only, so it looks the same online). Replaced game files are backed up and restored on removal.
Ticking a mod you have not downloaded opens its Nexus page in your browser, and the installer waits until the download
is in Downloads. All players should pick the same mods. The texture pack (Authentic High-Quality Textures) needs the separate TFC
Installer and is not handled yet.
Works with the Steam version of the game (build 12831). Every player needs their own copy.

## Play with friends

Steam must be running for everyone, and you must be Steam friends.

- **Host:** start the game, press **F10** → Host a game → pick a chapter or Escalation map → Start hosting.
- **Friend:** start the game, press **F10** → Join a game → pick the game in the list (friends are on top) → **Join**.

No IP addresses, no port forwarding: traffic goes through Steam. While the mod is installed, Steam shows the game as
"Spacewar" (the free Steam app the co-op connection uses); `spacewar=0` in `Binariesoc_coop.ini` turns that off. On one network you can also join from the lobby list
or by IP.

## What works

- Escalation for 2 players on all 4 maps, with score, kills, money and the weapon store as in the original.
- Story chapters 1, 2a, 2b, 3b, 4c, 5, 6, 8a, 8b, 9: both players spawn, each with their own camera and HUD.
- Joining through Steam (tested on one PC; a test with two real Steam accounts is still to come).
- Story: the host waits until the friend has loaded the level, then it starts for both; the current objective and the
  objective marker show on both screens (tested in chapter 6).
- A downed player stays down until the other one revives them; one player dying no longer ends the mission. Only when
  everybody is down: MISSION FAILED, restart from the checkpoint.

## Known problems

- 3–5 players are not tested yet.
- Cutscenes and scripted events are only partly in sync; the guest can see dark silhouettes in cutscenes.
- If the friend really dies (e.g. falls into a pit), they do not come back until a checkpoint restart.
- Reaching some markers loads the next area only around that player: stay together.
- The chapter title only shows on the host.
- Hosting or joining restarts the game once (about 20–30 s).

Bugs and progress: [docs/dev](docs/dev/README.md).

## Build

`installer\build_installer.bat` (Visual Studio 2022 Build Tools, x86) builds the DLL, the Steam relay and
`dist\FoCCoopSetup.exe`.

---
Not affiliated with Hasbro, High Moon Studios or Activision. No game files are included.
