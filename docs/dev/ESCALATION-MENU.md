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
- 2026-10-22 - B35 "Ability: undefined" analysed (exported TnCharacterScriptBinding etc. to
  coop-anything/local/script/CharUI): GetCharacterAbilities uses the iconic Escalation character only when
  `UseSurvivalCharacter()` = `TnGame.GetGameTypeAsGameTeamStatus() == 2`. That reads the 'Game' session settings
  first; on a member they are a plain TnOnlineGameSettingsBase with TeamType 0, so the card looked among custom
  characters (none) -> undefined. Host: TnOnlineGameSettingsSVPrivate, TeamType 2. Fix tried: TickTeamType
  (invite.inc) copies the replicated GRI GameTeamStatus into the member's Game settings while TeamType is 0.
  Result: see next entry.
- 2026-10-22 - TickTeamType (once a second) result: member's TeamType became 2 and
  `call obj:TnCharacterScriptBinding_N GetCharacterAbilities s:Megatron` (new `s:` string args in `call`) returns
  "ShieldAbility,Sprint," on the member as on the host - but the card still said undefined, also after switching
  to Shockwave. On the map the member gets a new settings object with TeamType 0 (second "0 -> 2" log line right
  at the map load); the character movie reads its data at load, before the 1 s check. Also seen: the first version
  changed the member's own party presence settings (TnOnlineGameSettingsPartyPresenceSV) - now own sessions
  (OwningPlayerId = us) are skipped. Fix tried: check every tick and set the class default
  Default__TnOnlineGameSettingsBase TeamType as well while we are a member of a joined game (restored on story
  maps). Result: see next entry.
- 2026-10-22 - every-tick check + class default (`esc2_ability3`): **B35 fixed** - the member's card shows
  "Ability: Flak Shield" with its description like the host. Log: class default and the Game settings set to 2 at
  once in the game lobby; no second fix needed at the map load any more.
- 2026-10-22 - 4-copy regression with the B35 fix (`esc4_ability`, `-LobbyWait 80`): connection PASS 4/4, but only
  B had the fix logged and a correct card; **C and D still showed "Ability: undefined"** and never logged a
  TeamType change. Suspect the OwningPlayerId skip (members' joined settings may carry their own id). Changed: the
  joined game is recognised by its class (exactly TnOnlineGameSettingsBase) instead; diagnostic log lines
  "teamtype gri ... / teamtype check ..." (8 max, every 15 s). Result: see next entry.
- 2026-10-22 - **wrong conclusion corrected**: C and D were not a code problem - that regression run used
  `-NoBuild`, and the previous build had been deployed to A and B only (Build-Deploy deploys to the `-Copies` of
  that run). Rerun with a build for all four (`esc4_ability2`): B, C and D all log "TeamType 0 -> 2"; joined
  settings have OwningPlayerId 0. Rule: after a build for some copies, never run other copies with `-NoBuild`.
- 2026-10-22 - owner: compare every player's screen, enter the game, check names. New `tools/shotgrid.py`
  (2x2 grid per step, difference of each copy from the others, ODD flag) on `esc4_ability2`: members' game lobby
  lacks the "ESCALATION / SURVIVE WAVES OF ATTACKS" title and the "DECEPTICONS" team list (they show the party-style
  list); members' party lobby splits the list into groups with a chain icon and orders it differently than the
  host. Reading: the member's Game settings are the plain TnOnlineGameSettingsBase (no mode defaults). Fix tried:
  SpyJoin builds TnOnlineGameSettingsSVPrivate for a 'Game' join with plain base settings and copies the session
  fields (ReservationChannel, OwningPlayerId, connection counts, LAN). Party-lobby grouping not addressed yet.
  `esc4menu.ps1 -Play [-Rounds n]`: characters by RIGHT x index + ENTER, rounds of game and TAB screenshots, names
  from foc_state.json of every copy, shotgrid at the end. Result: see next entry.
- 2026-10-22 - `esc4_play1` (4 copies, `-Play -Rounds 3`): connection PASS 4/4; SVPrivate join settings work -
  members' game lobby now has the ESCALATION title and the DECEPTICONS list. **TAB in the game: identical on all
  four** (DECEPTICONS, Bumblebee, Ironhide, Optimus, Ratchet, 375 each). Idle players get killed in wave 1 ("WAVE 1
  FAILED, RESTARTING WAVE") - normal, nobody plays. Remaining visual differences: (1) party lobby on members: the
  list is split into groups, some with a chain icon, different per member; host: one plain list; (2) game lobby on
  members: their own name in a separate block under the team list. `sessions` shows every member's Party and Game
  sessions hold all four ids plus an id-0 entry -> the grouping is not about who is in the session; next: dump
  TeamNumber/PartyId of the registrants (`sessions` now prints them; esc4menu calls `sessions` after steps 2 and 3).
  Names from foc_state.json: clients only list themselves (state file limitation), the TAB shots are the check.
- 2026-10-22 - registrant dump (`esc3_party1`): PartyId 255 and TeamNumber equal everywhere - **not** the grouping
  cause. **Correction of an earlier reading**: the id-0 registrant is the member itself (bandwidth tier 1 = our member
  value), not a second host entry; the joined game's OwningPlayerId was 0 as well, which is why member heartbeats
  went to "0" and the beacon timed the 0 entry out after ~50 s. In the game lobby the 0 entry has team 255 -> the
  own name in a separate block. Fix tried (replaces the receive-time copy): TickZeroRegistrant removes the 0 entry
  when our real-id entry exists (else gives it our id); SpyJoin sets the joined game's OwningPlayerId to the owner of
  the announced game channel. Result: see next entry.
- 2026-10-22 - result `esc4_play2` (zero-registrant removal + real owner): the 70 s lobby wait held, but **FAIL at
  the start**: with the real owner the members' GetResolvedConnectString returned 1 with '0.0.0.0:0', the travel
  code took it as "the game resolved the host itself", the game sent its connect packets to 0.0.0.0 and the members
  fell back to their own party after a minute. Fix: SpyResolve treats 0.0.0.0 / port 0 as a failure (the relay path
  connects). Also **AUTOBOTS vs DECEPTICONS** in the game lobby (owner's "factions" remark): the host's
  RMT_TeamAssignments (0x04) listed 5 entries for 4 players - the host twice; its second entry got team 0 (AUTOBOTS),
  everybody else team 1. The duplicate comes from our creation fix (local 0 entry -> our id) plus the party
  registration of every member. Fix: TickZeroRegistrant (host and members) removes a second entry of the same id.
  Party lobby grouping on members: still open. Result: see next entry.
- 2026-10-22 - result `esc4_play3` (4 copies, 70 s lobby, `-Play -Rounds 3`): **PASS 4/4**; members joined the match
  through the relay after "empty host address"; host removed its second registrant. Game lobby: one DECEPTICONS
  list with all four on every copy (each copy lists itself first - looks like the game's own order). Character
  cards: Flak Shield on all. TAB in the game: identical on all four. Still different: party lobby on members - the
  list is split, one group with ring icons: on each member that group is itself, the host and those who joined the
  party after it; the plain group are members that were in the party before it joined. Ring icons also appear in the
  game lobby on varying names. Registrant PartyId/TeamNumber equal; BeaconConnectionStatus of the host on members'
  Party session 0. Open.
- Test pitfall: copies started 25 s apart still sometimes die at D3D start (C had no log after 5 s, 2026-10-22);
  check every copy has "event hello" before reading the result.
- 2026-10-23 - **first real test on two PCs** (owner hosts, friend joins through Friends List; Steam invite never
  arrived - B39). Friend reached party and game lobby, got "travel to game", opened 127.0.0.1:7802 and sent connect
  packets for minutes without an answer. Host log: no "hosting the match" line, host relay log: no "game port".
  Reading: the class table gave a stale "Game" kind to TnObjectiveMarkerTypeTransformerSurvival on ESC_Gladiator and
  never recognised TnSurvivalGame (pointer + id reuse after three front end <-> lobby trips) - B38. All mock tests ran
  on Ark (the default map) with one lobby trip, so this never showed. Fix: class registration checks the name index;
  hosting also = "was GameHost + ESC_ map". New `esc4menu.ps1 -MapSteps n` (Select Map RIGHT x n) to test every
  map. Result: Ark (`esc2_port1`) PASS, Gladiator (`esc2_map1`) PASS; others: see next entry.
- 2026-10-23 - map runs `esc2_map2/3/4`: Temple, Iacon, Iacon (RIGHT x4 wraps or stops at Iacon) - all PASS with the
  "hosting the match" line. So all four maps (Ark, Gladiator, Temple, Iacon) work on the mock.
- 2026-10-23 - B39 in-game Send Invite: PlayerInterface slot 66 (0x55a020) = SendGameInviteToFriend, found by slot
  order (63 OnFriendInviteReceived, 64/65 add/clear, 66 send, 67 OnReceivedGameInvite, 68/69 add/clear, 70
  JoinFriendGame) and decompiling 0x8d8d50. Slot 65 (0x9ba6b0) was a wrong first guess (a clear-delegate body).
  Patched -> Steam lobby invite. `-MenuInvite` (game's SendGameInvite through `call cls:`) PASS.
- 2026-10-23 - B40 host spinner = member status 0 on the game channel (see BUGS). Member replays its last non-zero
  status 3 s after subscribing -> spinner gone (esc3_spin1). Tried also: members fire the statuses they received
  again 3 s after subscribing, hoping to clear the members' icons (B37) -> **no change**: those icons are chain
  links (party grouping), not spinners. Reverted.
- 2026-10-23 - `esc4_final` (4 copies, -LobbyWait 80 -MenuInvite -MapSteps 3, Iacon): PASS 4/4, no host spinner.
- 2026-10-23 - owner: "the others still have it" (spinners, colours) -> screenshots of every copy at each lobby step.
  The members' "chain/ring" icons are the same spinner in other rotation frames (**earlier reading "chain links,
  party grouping" was wrong**). Steps: (a) status 0 whispers replaced by our broadcast -> Party statuses 1 everywhere,
  Game: members still 0 for each other; (b) owner forwards whispered member statuses + gives a new subscriber all of
  them -> all 1 (`esc4_spin5`); (c) `-Dump` (PRI bytes, tools/esc4menu.ps1) showed one extra PRI with id 0 and the
  member's own name on every member -> destroyed (Actor.Destroy has no Function object; DestroyActor works) -> the
  party lobby list no longer splits (`esc4_phantom2`), but a spinner stayed at the host: the party host had never
  broadcast a status (no 0x16 before its party beacon existed) -> "16 01 00000000" fallback -> `esc4_phantom4`: party
  lobby identical on all four, game lobby without spinners. Remaining: B41.
- Tried, did not work: `call game SyncPlaceholderPRIsWithSessions` (no Function object in this build).
- Test pitfall: B once never got "event hello" (state TnFrontendStatisticsCollection) - start flake, rerun.
- 2026-10-24 - **4 copies, keyboard only, joined through Friends List** (owner: "test 4 players, mouse and keyboard,
  join through friends", then "keyboard only"); build e4b8323 = v0.8.1-dev, escautoaccept=0 on all, no test
  commands (no escinvite/call). Steps: main menu DOWN/ENTER -> ESCALATION on all; B/C/D: Friends List (DOWN x5,
  ENTER) -> host row (mock_85533) -> ENTER -> "Join game?" ENTER -> PartyClient; ESC back to the party lobby; A:
  Private Match -> Create Game -> Start Game; each picks a character (RIGHT x n, ENTER). Result: **PASS** - party
  lobby identical on all four with green bars; game lobby all four on every copy; all on Oblivion, host npc=4
  pawns=4, TAB board identical on all four (sessions/esc4human, grid_*.png, logs foc_coop_A..D.log).
  Still seen: B41 (host's game-lobby ping bars grey, members show Difficulty EASY while host has HARD). After
  "Join game" the member stays on ONLINE FRIENDS (needs ESC) - same as the original game, probably fine.
  Note: Read-FocState on members listed only the member itself (players[]), although TAB showed all four - the
  state file's player list on clients is not a reliable names check.
- 2026-10-24 - mouse tried first and dropped (owner switched to keyboard only): `input click` and posted
  WM_MOUSEMOVE/LBUTTONDOWN (Post and SendMessage, with the GetCursorPos spoof) do not move the menu highlight. The
  log shows no GetDeviceState/GetDeviceData at all and CreateDevice only with device instance GUIDs (9f9d6dc0..,
  c2a1dd80.., 7112..), never GUID_SysMouse, so g_mouseDev stays null. Reading: the game reads the DirectInput mouse
  only while its window is active (Acquire fails in background copies). Not fixed; idea for later: hook Acquire
  (slot 7) and classify devices by GetCapabilities dwDevType (0x12 mouse, 0x13 keyboard).
- 2026-10-24 - **real 2-PC test with v0.8.1-dev** (owner hosts, friend; owner's log
  sessions/real_20261024/host_escalation_pid6052.log): the in-game Send Invite worked (friend joined the party at
  68 s), game lobby -> ESC_Gladiator "hosting the match on port 7778", the friend played the whole match (~29 min,
  both pawns respawning) up to MatchOver -> **B38 and B39 confirmed fixed on real Steam**. Owner: "everything works
  great, only the statistics do not show" (B47). After the match: back to the lobby, main menu, then a crash when
  the old online game was destroyed (B45). The owner then tried MULTIPLAYER (versus): crash 4 s after Send Invite
  (B46). Friend's log not received.
