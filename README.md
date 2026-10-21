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

**Story with a lobby (recommended):**
- **Host:** start the game, press **F10** → Host a game → at the bottom **Create lobby** (friends only, public or
  private, optional password). Nothing loads yet.
- **Friends:** press **F10** → Join a game → **Join lobby** (type the password if there is one) and wait in the main menu.
- **Host:** start the mission from the game's own menu as in single player (**Continue** or **Campaign** → chapter).
  Everybody in the lobby is taken into it; the intro waits until they have loaded. Joining a game that already runs
  works the same way (**Join game**), with a fresh character.

**Direct:** Host a game → pick a chapter or Escalation map → Start hosting; friends: Join a game → **Join**.
No game restart any more: hosting, joining and leaving happen inside the running game.

No IP addresses, no port forwarding: traffic goes through Steam. While the mod is installed, Steam shows the game as
"Spacewar" (the free Steam app the co-op connection uses); `spacewar=0` in `Binaries\foc_coop.ini` turns that off. On one network you can also join from the lobby list
or by IP.

## What works

- Escalation for 2 players on all 4 maps, with score, kills, money and the weapon store as in the original.
- Story chapters 1, 2a, 2b, 3b, 4c, 5, 6, 8a, 8b, 9: both players spawn, each with their own camera and HUD.
- Joining through Steam (tested on one PC; a test with two real Steam accounts is still to come).
- Story: the host waits until the friend has loaded the level, then it starts for both; the current objective and the
  objective marker show on both screens (tested in chapter 6).
- A downed player stays down until the other one revives them; one player dying no longer ends the mission. Only when
  everybody is down: MISSION FAILED, restart from the checkpoint. If a player really dies (e.g. falls into a pit),
  both restart at the last checkpoint after about 15 seconds.
- The chapter title shows on both screens.
- A player left behind (a scripted door closed, a scene moved the host) is brought to the host; scenes use the
  host's camera for everybody.
- In cutscenes the friend's HUD hides and they cannot walk away, as for the host.
- A player who falls out of the level is teleported next to the other one instead of falling forever.

## Known problems

- 3–5 players are not tested yet.
- Scripted events are only partly in sync; in cutscenes the guest can see dark silhouettes, and the other player's
  robot can stand in the scene.
- Reaching some markers loads the next area only around that player: stay together (the teleport is not yet tested
  on every such marker).
- Starting a mission from the lobby loads the chapter twice on the host (once as single player, then as the session).
- After a friend leaves and joins again the host shows one player too many for a while.

Bugs and progress: [docs/dev](docs/dev/README.md).

## Build

`installer\build_installer.bat` (Visual Studio 2022 Build Tools, x86) builds the DLL, the Steam relay and
`dist\FoCCoopSetup.exe`.

---
Not affiliated with Hasbro, High Moon Studios or Activision. No game files are included.
