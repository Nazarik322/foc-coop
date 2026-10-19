# foc-coop

**Co-op for Transformers: Fall of Cybertron** — play the story and Escalation with friends (up to 5).
Unofficial, early development. English | [Русский](README.ru.md)

## Install

1. Run **`FoCCoopSetup.exe`**.
2. Next → Next → the game folder is found automatically (or press Browse) → **Install**.

That's it. To remove the mod, run the installer again and tick "Remove the mod".
Works with the Steam version of the game (build 12831). Every player needs their own copy.

## Play with friends

Steam must be running for everyone, and you must be Steam friends.

- **Host:** start the game, press **F10** → Host a game → pick a chapter or Escalation map → Start hosting.
- **Friend:** start the game, press **F10** → Join a game → **Join a friend through Steam**.

No IP addresses, no port forwarding: traffic goes through Steam. On one network you can also join from the lobby list
or by IP.

## What works

- Escalation for 2 players on all 4 maps.
- Story chapters 1, 2a, 2b, 3b, 4c, 5, 6, 8a, 8b, 9: both players spawn, each with their own camera and HUD.
- Joining through Steam (tested on one PC; a test with two real Steam accounts is still to come).

## Known problems

- 3–5 players are not tested yet.
- Cutscenes and scripted events are only partly in sync; the guest can see dark silhouettes in cutscenes.
- Hosting or joining restarts the game once (about 20–30 s).

Bugs and progress: [docs/dev](docs/dev/README.md).

## Build

`installer\build_installer.bat` (Visual Studio 2022 Build Tools, x86) builds the DLL, the Steam relay and
`dist\FoCCoopSetup.exe`.

---
Not affiliated with Hasbro, High Moon Studios or Activision. No game files are included.
