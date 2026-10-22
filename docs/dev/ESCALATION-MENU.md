# Escalation through the game's own menu (design, 2026-10-21)

Owner request (2026-10-21): main menu → ESCALATION works like the original game: a party lobby you
enter like a server, Steam friends, invites, the host starts the game and picks the map. Everything over
Steam (Spacewar 480 relay), no outside server (ReEnergized/Warehouse impersonates Demonware on its own server;
we do not use it).

## What the game does (mapped 2026-10-21, scripts exported to coop-anything/local/script/TransGameOnline,
IpDrvOnline, TransContentFE)

1. Main menu ESCALATION → `TnAccountActionScriptBinding.OpenPartyLobbyBlockOnLogin("GTS_SingleTeamGame")`.
   Opens only if the front-end game `TnFrontEndGame` is in state `LoginSequenceCompleted`; otherwise it restarts
   the login sequence and shows "Sign in / Failed to sign in" (screenshot sessions/escmenu/p3.png).
2. Login sequence (`TnFrontEndGame`): skipped completely when `NeedsLogIn()` is false, i.e.
   `PlayerInterface.GetLoginStatus(c) == LS_LoggedIn (2)` and `GetSecondaryLoginStatus(c) == 2` (PC build).
   Otherwise WaitingForNetwork → WaitingForSteam → ReadingAccountNames (Demonware) → CreatingAccount /
   LoggingInPC (Demonware auth at 185.34.107.28:3074, blocked by our DLL, dead anyway) → ReadingKeyArchive →
   WaitingForNatDiscovery → LoginSequenceCompleted.
3. Flash `_global.invokeEscalation` → `OpenPartyLobby` → `ClientTravelToMap(TnOnlineGameSettingsSV.BuildPartyLobbyURL())`:
   every player loads a local lobby map with `TnLobbyGameBase` (not a network game!).
4. `TnLobbyGameBase` states: CreatingPartySessions (`TnPartySessionCreator`: ReservationInterface.CreateReservationHost
   → beacon channel id → GameInterface.CreateOnlineGame for the "Party" presence and persistent sessions) → PartyHost /
   PartyClient → Matchmaking / HostingGame / JoiningGame (`GameInterface.JoinOnlineGame`, then
   `WorldInfo.ClientTravelToOnlineGame` → `GetResolvedConnectString`) → GameHost / GameClient (map vote, countdown,
   teams) → travel to the Escalation map.
5. Party members talk only through the reservation beacon (`TnOnlineReservationInterfaceImpl` → `TnReservationBeacon`):
   lobby status, map votes, join session, prepare/travel to game, connection status. The beacon's transport is a
   separate class `ReservationBeaconConnectionDemonware` (abstract base `ReservationBeaconConnection`, 7 methods:
   CreateChannel, ConnectToChannel, Send(recipient), Broadcast, Recv, OnMemberJoined/LeftChannel, GetConnectionChannelId).
6. Sessions: `OnlineGameInterfaceDemonware` (subclass of IpDrv `OnlineGameInterfaceImpl`, which has the stock UE3 LAN
   code): natives CreateOnlineGame, DestroyOnlineGame, StartOnlineGame, FindOnlineGames, JoinOnlineGame, RegisterPlayer,
   UnregisterPlayer, GetResolvedConnectString, AcceptGameInvite, ClearGameInvite, CheckGameInvites,
   GetOnlineGameSearchResultForGameInvite, Read/BindPlatformSpecificSessionInfo.
7. Friends / invites: `OnlineSubsystemDemonware` natives ReadFriendsList, GetFriendsList, SendGameInviteToFriend(s),
   JoinFriendGame, InviteSteamFriends, SteamPersonaName; Flash binding `TnOnlineActionScriptBinding` (SendGameInvite,
   JoinFriendGame, ShowInviteUI, RespondToFriendInvite ...).
8. Playlists (the map list): `TnLobbyGameBase.InitGame` → `PlaylistManager.DownloadPlaylist()` (Demonware storage).

Scripts are compiled to C++ in this build: replacing UFunction thunks does nothing (B4 note). Hooks must patch the
native C++ functions (inline detour) or the vtable slots. No symbols: addresses come from the runtime vtables
(`osprobe` command, online.inc) + Ghidra (coop-anything/local/lanes/re/g.ps1 DecompAt).

## Design: answer the game's online calls from Steam (relay), keep the game's own lobby logic

| Game piece | Steam replacement (relay process, app 480) |
|---|---|
| Login status / account / NAT discovery | always LoggedIn; UniqueNetId = Steam ID; nickname = Steam persona (test copies: ini playername) |
| Reservation channel (party) | a Steam lobby; channel id = lobby id; Send/Broadcast/Recv = lobby messages through the relay; member joined/left = lobby member changes |
| Sessions (party presence/persistent, game) | kept locally in the DLL; session search = lobby list; join = join lobby; GetResolvedConnectString = host's game port through the relay (127.0.0.1:7790 on guests, as now) |
| Friends list | Steam friends (persona, online state, "in our game" = running the 480 relay) |
| Invites | ISteamMatchmaking::InviteUserToLobby; received invites (LobbyInvite_t / GameLobbyJoinRequested_t) → game's invite delegates |
| Playlists / maps | local playlist built in the DLL (the 4 Escalation maps), if the Demonware download is required |

Steps (each tested with 2-4 copies through the real menus, no player actions):
1. Login: GetLoginStatus / GetSecondaryLoginStatus → 2 (and what else the lobby asks). ESCALATION must open the
   party lobby screen.
2. Party sessions + channel on one machine (host alone): CreatingPartySessions succeeds, lobby screen shows the host.
3. Second copy joins through the Steam lobby (invite or friend's game) → both in the party lobby screen.
4. Host starts → map vote/selection → everybody travels to the Escalation map through the relay.
5. Friends list + invites from the in-game screens.
6. Remove the F10 lobby (F10 stays a quick test launcher). MULTIPLAYER (versus) menu last.

## Reverse-engineering notes (2026-10-21, ghidra VAs; runtime = base + VA - 0x400000)

- Script-native methods: native `this` = object + 0x10; the table at object + 0x10 holds the class's script natives in
  .uc order after 16 common entries (ReservationBeaconConnection: 16 DestroyConnection, 17 CreateChannel,
  18 ConnectToChannel, 19 Send, 20 Broadcast, 21 Recv, 22 GetConnectionChannelId, 23 StopListening). GotoState
  (0x70df30) takes the object itself; the state frame is at object + 0x28, its State object at frame + 0x38.
- OnlineSubsystemDemonware interface tables (object + offset → vtable): PlayerInterface +0x14c (0x168ec38),
  AccountInterface +0x144 (0x152503c), StatsInterfaceEx +0x154 (0x164f150), SystemInterface +0x158 (0x14c0d70),
  PlayerInterfaceEx +0x160 (0x1529df8), ChatInterface +0x168 (0x1511fc0). Slot 0 is common; slot n = n-th
  function/delegate of the decompiled .uc (PlayerInterface: line - 2). Verified slots: Player 15 GetLoginStatus,
  16 GetPreviousLoginStatus, 19 GetSecondaryLoginStatus, 26 IsGuestLogin (returns 0), 28 CanPlayOnline (returns 2);
  System 1 HasLinkConnection, 2 NatDiscoverySucceeded, 3 NatDiscoveryFailed; StatsEx 12 HasReadKeyArchiveFor(id*);
  Chat 1 CreateChatChannel(), 2/3 Add/ClearChatCreate, 4 SubscribeToChatChannel(id*), 5/6, 7 Unsubscribe(id*), 8/9,
  10 SendChatMessage(channel*, data, size), 11 SendChatWhisperMessage(channel*, user*, data, size), 12/13 send
  complete, 14/15 chat change, 16/17 chat message.
- The party channel ReservationBeaconConnectionDemonware is a thin wrapper over the ChatInterface (CreateChannel =
  CreateChatChannel, ConnectToChannel = Subscribe, Broadcast = SendChatMessage, Send = whisper, Recv = its buffer).
  It registers listeners (0xa13470) as 16-byte records {object, function name, native this, native handler}:
  OnChatCreate 0x99c170 (bool ok, id* channel), OnChatSubscribe 0xa10090 (bool ok, id* channel), OnChatUnsubscribe
  0x6a4b70 (bool ok, id* channel), OnChatSendComplete 0x803d20 (empty), OnChatChange 0x8a2050 (id* channel,
  UniqueNetId member by value, byte 1 joined / 0 left), OnChatMessage 0xba53e0 (id* channel, id* sender, ?, data,
  size; drops its own messages: sender == connection +0x70). Connection +0x68 = channel id, +0x58 state
  (1 ok, 2 error), +0x70 = local player id.
- Done so far (branch wip/escalation-menu): `esclogin` (front end → LoginSequenceCompleted) and `esconline` (vtable
  patches above). Result: ESCALATION loads the party lobby (TnLobbyGame), which then fails in CreatingPartySessions
  ("TRANSFORMERS: FoC server is not available") because the chat channel and the sessions still go to Demonware.
- Next: ChatInterface over a Steam lobby (relay: CreateLobby/JoinLobby, lobby chat messages with a recipient header,
  member changes → OnChatChange), UniqueNetId = Steam ID, then GameInterface sessions (object + 0x28 interface table).

## Tried and result (keep this log; read it before trying something new)
Format: date - what was tried -> result. Wrong conclusions are struck out with the correction, not deleted.

- 2026-10-21 - drop beacon message 0x17 in ChatSend, believing it was "migrate to best host" -> 2 players reached the
  map. **Wrong reading**: 0x17 = 23 = `RMT_LostConnection <member id>` (enum `EReservationMessageType` in
  IpDrvOnline/ReservationBeacon.uc; 0x07 is MigrateToBestHost, 0x14 Kick, 0x15 Heartbeat, 0x16 ConnectionStatus).
  Dropping it only hides the symptom; with 3-4 players the members still fell back to their own party.
- 2026-10-21 - clear `AllowLobbyBestHostDeterminationHostMigration` on all settings -> no change (private match never
  migrates: `ShouldMigrateToBetterHost` is not the cause). Clearing the AllowLobby*HostMigration flags earlier also
  crashed both lobbies on entry.
- 2026-10-21 - patch HostMigrator.BeginHostMigration (0x1597c40 slot 25) to refuse -> never called; no change.
- 2026-10-22 - System.GetBandwidthTier (slot 40) returns 4 for the party owner, 1 for members -> no change (same
  reason: there is no migration).
- 2026-10-22 - owner's idea "factions differ" checked in script: a private Escalation match calls
  `TeamHandler.ForceToTeam` (TnLobbyGameBase.AssignTeams) - everyone is put on one team. Not the cause.
- 2026-10-22 - **root cause 1**: our chat "member joined/left" call into the beacon (OnChatChange 0x8a2050) faulted
  every time ("change listener fault"). Logged the fault: c0000005 at 0xadf957. The beacon handler takes the member
  id **by pointer**, we passed it by value. Fixed (pointer).
- 2026-10-22 - with the pointer fix, members left the party right after the reservation reply (165-byte reply without
  the member). **Root cause 2**: the last argument means *left* (1 -> delegate +0x38 -> 0xadf950 = member-left: adds to
  PlayersThatHaveLeftTheChannel, drops the pending registrant), 0 = joined. We sent 1 for joins. Fixed (inverted).
  After both fixes: party join OK (200-byte reply), member reaches the game lobby.
- 2026-10-22 - still: in the game lobby neither side sends heartbeats (0x15) any more (party channel did, every 5 s);
  host sends RMT_LostConnection about 10 s after the members join -> members drop back to their own party at the
  countdown. Probing the session registrants (`sessions` now prints BeaconConnectionStatus and the heartbeat
  times at registrant +0x50/+0x58) with `tools/esc4menu.ps1 -Probe`.
- 2026-10-22 - `-Probe` result (2 copies, PASS 2 of 2): in the game session the member keeps the host at
  BeaconConnectionStatus 0 (party: 1) and its "sent" time never moves = it sends nothing; the host's "recv" for the
  member is stuck. The host broadcasts its 0x16 ConnectionStatus when it creates the game channel, before anyone is
  subscribed, so members never get it. Fix tried: the channel owner remembers its last broadcast 0x16 and replays it
  to every member that subscribes later (steamnet.inc 'S'). Result: see next entry.
  Also seen: host Game session lists the host twice (EnsureRegistrant duplicate?); member sessions have a registrant
  with id 0. Not yet known if harmful.
- 2026-10-22 - status replay (3 copies): **PASS 3 of 3** reached the map. But the host still tried 2x
  RMT_LostConnection (dropped by us), and members' host registrant stays at status 0. The dump shows the member's
  game session has a registrant with id **0** whose "sent" time moves: member heartbeats are whispered to id 0, and
  ChatWhisper threw away whispers to 0. Fix tried: whisper to 0 -> channel owner. Result: see next entry.
- 2026-10-22 - whisper to id 0 -> channel owner (4 copies, `esc4_w0`): **PASS 4 of 4**, all four at CHOOSE
  CHARACTER on the map; no RMT_LostConnection at all, the host gets member heartbeats on the game channel (27).
  Then the 0x17 hold-back was removed (forwarded again, only logged). Rerun: see next entry.
- 2026-10-22 - rerun with 0x17 forwarded (`esc4_fwd`): FAIL 1 of 4, but no 0x17 at all. Heartbeats fine both ways.
  B quit to its party ~50 s after entering the game lobby (C, D followed when B left). Same ~50 s in the very first
  failing runs: **the PASS runs only passed because the countdown finished before 50 s.** Reading: the member's
  session has the host twice (id 0 and real id); only the real entry's receive time (+0x50) moves, the beacon times
  out entry 0 -> OnReservationError type 2 -> QuitToPartyLobby ("host quit"). Fix tried: TickZeroRegistrant
  (invite.inc) copies the channel owner's receive time into entry 0. Test with `-LobbyWait 80` so the lobby lasts
  longer than the timeout. Result: see next entry.
- 2026-10-22 - TickZeroRegistrant with `-LobbyWait 80` (`esc4_zero`): B and C stayed 80 s in the game lobby and
  reached the map (host sees 3) - **the 50 s quit is fixed**. D never joined: A's relay sent the invite, D's mock
  never raised it. Probable cause (test environment only): mock InviteUserToLobby wrote users\<id>.invite in place
  and the reader deleted it even when it read nothing. Mock fixed (temp file + rename, keep unreadable file).
  Result: see next entry.
- 2026-10-22 - all fixes + mock invite fix, `-LobbyWait 80` (`esc4_zero2`): **PASS 4 of 4**. All four in the game
  lobby (team DECEPTICONS: Bumblebee, Ironhide, Optimus, Ratchet; map Oblivion), 80 s wait, all four at CHOOSE
  CHARACTER on the map. Members still show "Ability: undefined" on the character card (open).
- 2026-10-22 - observations still open after PASS 4/4: members show "Ability: undefined" on CHOOSE CHARACTER
  (host: Flak Shield) - B35; in-game Send Invite button reacts to mouse/gamepad only, and injected mouse clicks do
  not move the menu cursor (input.inc `input click`), so tests use keys + Steam invites; Find Match, friend adding
  and Select Map are not done/checked yet. The background export of HostMigrator/TnHostMigrator (export_classes.ps1)
  finished; not needed any more (there is no host migration in a private match).
- Test pitfall: copies started 25 s apart still sometimes die at D3D start (C had no log after 5 s, 2026-10-22);
  check every copy has "event hello" before reading the result.
