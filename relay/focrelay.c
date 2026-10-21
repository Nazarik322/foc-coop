/* FoCRelay: moves the game's UDP packets over Steam (AppID 480 "Spacewar"), so friends join without IPs or port
 * forwarding. Pattern from coop-anything playbook/networking-steam-spacewar.md (HXCoop).
 *
 *   FoCRelay.exe host <gamePort|0> [max] [mode] [map] [public|friends|private] [pwhash]
 *                                                create a lobby tagged foccoop=1 (name, mode, map in the
 *                                                lobby data); every player in it gets a local UDP socket connected
 *                                                to 127.0.0.1:<gamePort> (the hosting game)
 *   FoCRelay.exe list                            lobby browser for the Join page: every 3 s writes lobbies.txt
 *                                                (lobby|owner|name|mode|map|members|max|friend); stops on "quit" in
 *                                                list.cmd or when no game runs
 *   FoCRelay.exe join <lobbyId|auto> <localPort> join a lobby (auto = a friend who hosts foc-coop); the guest game
 *                                                connects to 127.0.0.1:<localPort>
 *
 * steam_api.dll (32-bit, flat API) and steam_appid.txt (480) live next to this exe, never next to TFOC.exe (the game has
 * its own Steam_api.dll). Status for the in-game menu and tests: relay.status next to the exe, rewritten every second.
 * The relay exits when no TFOC.exe has run from the parent folder for 60 s, on "quit" in relay.cmd, or on Ctrl+C.
 * Lobby without a map (owner 2026-10-20): the host relay may start with game port 0; relay.cmd "port <n>" sets the game
 * port later (peer sockets are re-pointed), "go <map>" tells the lobby the mission started (lobby data go=<seq>|<map>),
 * the join relay reports it in relay.status (go=...) and the guest game connects then. A password is kept as a hash
 * in the lobby data "pw" (checked by the game before it joins). */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>

typedef int bool8;
#pragma pack(push, 8)
typedef struct { int32_t hSteamUser; int32_t callback; uint8_t* param; int32_t param_size; } CallbackMsg_t;
typedef struct { uint64_t call; int32_t callback; uint32_t param_size; } SteamAPICallCompleted_t;
typedef struct { int32_t result; uint64_t lobby; } LobbyCreated_t;
typedef struct { uint64_t lobby; uint32_t perms; uint8_t locked; uint32_t response; } LobbyEnter_t;
typedef struct { uint64_t lobby; uint64_t user_changed; uint64_t making_change; uint32_t state_change; } LobbyChatUpdate_t;
typedef struct { uint64_t lobby; uint64_t friend_id; } GameLobbyJoinRequested_t;
typedef struct { uint64_t gameID; uint32_t ip; uint16_t port, qport; uint64_t lobby; } FriendGameInfo_t;
typedef struct { uint32_t count; } LobbyMatchList_t;
#pragma pack(pop)
typedef struct { uint8_t raw[160]; } NetIdentity;   /* SteamNetworkingIdentity is 136 bytes; opaque */

/* flat API */
static HMODULE g_api;
#define F(ret, name, args) typedef ret (__cdecl* t_##name) args; static t_##name name;
F(int, SteamAPI_InitFlat, (char*))
F(bool8, SteamAPI_IsSteamRunning, (void))
F(void, SteamAPI_Shutdown, (void))
F(int32_t, SteamAPI_GetHSteamPipe, (void))
F(void, SteamAPI_ManualDispatch_Init, (void))
F(void, SteamAPI_ManualDispatch_RunFrame, (int32_t))
F(bool8, SteamAPI_ManualDispatch_GetNextCallback, (int32_t, CallbackMsg_t*))
F(void, SteamAPI_ManualDispatch_FreeLastCallback, (int32_t))
F(bool8, SteamAPI_ManualDispatch_GetAPICallResult, (int32_t, uint64_t, void*, int, int, bool8*))
F(void*, SteamAPI_SteamUser_v023, (void))
F(uint64_t, SteamAPI_ISteamUser_GetSteamID, (void*))
F(void*, SteamAPI_SteamMatchmaking_v009, (void))
F(uint64_t, SteamAPI_ISteamMatchmaking_CreateLobby, (void*, int, int))
F(uint64_t, SteamAPI_ISteamMatchmaking_JoinLobby, (void*, uint64_t))
F(void, SteamAPI_ISteamMatchmaking_LeaveLobby, (void*, uint64_t))
F(bool8, SteamAPI_ISteamMatchmaking_SetLobbyData, (void*, uint64_t, const char*, const char*))
F(const char*, SteamAPI_ISteamMatchmaking_GetLobbyData, (void*, uint64_t, const char*))
F(uint64_t, SteamAPI_ISteamMatchmaking_GetLobbyOwner, (void*, uint64_t))
F(int, SteamAPI_ISteamMatchmaking_GetNumLobbyMembers, (void*, uint64_t))
F(uint64_t, SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex, (void*, uint64_t, int))
F(void, SteamAPI_ISteamMatchmaking_AddRequestLobbyListStringFilter, (void*, const char*, const char*, int))
F(void, SteamAPI_ISteamMatchmaking_AddRequestLobbyListDistanceFilter, (void*, int))
F(void, SteamAPI_ISteamMatchmaking_AddRequestLobbyListResultCountFilter, (void*, int))
F(uint64_t, SteamAPI_ISteamMatchmaking_RequestLobbyList, (void*))
F(uint64_t, SteamAPI_ISteamMatchmaking_GetLobbyByIndex, (void*, int))
F(void*, SteamAPI_SteamFriends_v018, (void))
F(bool8, SteamAPI_ISteamFriends_SetRichPresence, (void*, const char*, const char*))
F(void, SteamAPI_ISteamFriends_ClearRichPresence, (void*))
F(void, SteamAPI_ISteamFriends_ActivateGameOverlayInviteDialog, (void*, uint64_t))
F(int, SteamAPI_ISteamFriends_GetFriendCount, (void*, int))
F(uint64_t, SteamAPI_ISteamFriends_GetFriendByIndex, (void*, int, int))
F(bool8, SteamAPI_ISteamFriends_GetFriendGamePlayed, (void*, uint64_t, FriendGameInfo_t*))
F(const char*, SteamAPI_ISteamFriends_GetFriendPersonaName, (void*, uint64_t))
F(const char*, SteamAPI_ISteamFriends_GetPersonaName, (void*))
F(void*, SteamAPI_SteamNetworkingMessages_SteamAPI_v002, (void))
F(int, SteamAPI_ISteamNetworkingMessages_SendMessageToUser, (void*, const NetIdentity*, const void*, uint32_t, int, int))
F(int, SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel, (void*, int, void**, int))
F(bool8, SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser, (void*, const NetIdentity*))
F(bool8, SteamAPI_ISteamNetworkingMessages_CloseSessionWithUser, (void*, const NetIdentity*))
F(void, SteamAPI_SteamNetworkingIdentity_SetSteamID64, (NetIdentity*, uint64_t))
F(uint64_t, SteamAPI_SteamNetworkingIdentity_GetSteamID64, (NetIdentity*))
F(void, SteamAPI_SteamNetworkingMessage_t_Release, (void*))
F(void*, SteamAPI_SteamNetworkingUtils_SteamAPI_v004, (void))
F(bool8, SteamAPI_ISteamNetworkingUtils_SetGlobalConfigValueInt32, (void*, int, int32_t))
F(void, SteamAPI_ISteamNetworkingUtils_InitRelayNetworkAccess, (void*))
/* optional (svc mode; missing in old builds of the mock) */
F(bool8, SteamAPI_ISteamMatchmaking_InviteUserToLobby, (void*, uint64_t, uint64_t))
F(int, SteamAPI_ISteamFriends_GetFriendPersonaState, (void*, uint64_t))
#undef F

static int LoadApi(void) {
    char p[MAX_PATH]; GetModuleFileNameA(NULL, p, MAX_PATH);
    char* s = strrchr(p, '\\'); if (s) strcpy(s + 1, "steam_api.dll");
    g_api = LoadLibraryA(p);
    if (!g_api) return 0;
    int ok = 1;
#define L(name) if (!(name = (t_##name)GetProcAddress(g_api, #name))) { printf("missing export %s\n", #name); ok = 0; }
    L(SteamAPI_InitFlat) L(SteamAPI_IsSteamRunning) L(SteamAPI_Shutdown) L(SteamAPI_GetHSteamPipe)
    L(SteamAPI_ManualDispatch_Init) L(SteamAPI_ManualDispatch_RunFrame) L(SteamAPI_ManualDispatch_GetNextCallback)
    L(SteamAPI_ManualDispatch_FreeLastCallback) L(SteamAPI_ManualDispatch_GetAPICallResult)
    L(SteamAPI_SteamUser_v023) L(SteamAPI_ISteamUser_GetSteamID) L(SteamAPI_SteamMatchmaking_v009)
    L(SteamAPI_ISteamMatchmaking_CreateLobby) L(SteamAPI_ISteamMatchmaking_JoinLobby) L(SteamAPI_ISteamMatchmaking_LeaveLobby)
    L(SteamAPI_ISteamMatchmaking_SetLobbyData) L(SteamAPI_ISteamMatchmaking_GetLobbyData) L(SteamAPI_ISteamMatchmaking_GetLobbyOwner)
    L(SteamAPI_ISteamMatchmaking_GetNumLobbyMembers) L(SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex)
    L(SteamAPI_ISteamMatchmaking_AddRequestLobbyListStringFilter) L(SteamAPI_ISteamMatchmaking_AddRequestLobbyListDistanceFilter)
    L(SteamAPI_ISteamMatchmaking_AddRequestLobbyListResultCountFilter) L(SteamAPI_ISteamMatchmaking_RequestLobbyList)
    L(SteamAPI_ISteamMatchmaking_GetLobbyByIndex)
    L(SteamAPI_SteamFriends_v018) L(SteamAPI_ISteamFriends_SetRichPresence) L(SteamAPI_ISteamFriends_ClearRichPresence)
    L(SteamAPI_ISteamFriends_ActivateGameOverlayInviteDialog) L(SteamAPI_ISteamFriends_GetFriendCount)
    L(SteamAPI_ISteamFriends_GetFriendByIndex) L(SteamAPI_ISteamFriends_GetFriendGamePlayed)
    L(SteamAPI_ISteamFriends_GetFriendPersonaName) L(SteamAPI_ISteamFriends_GetPersonaName)
    L(SteamAPI_SteamNetworkingMessages_SteamAPI_v002) L(SteamAPI_ISteamNetworkingMessages_SendMessageToUser)
    L(SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel) L(SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser)
    L(SteamAPI_ISteamNetworkingMessages_CloseSessionWithUser) L(SteamAPI_SteamNetworkingIdentity_SetSteamID64)
    L(SteamAPI_SteamNetworkingIdentity_GetSteamID64) L(SteamAPI_SteamNetworkingMessage_t_Release)
    L(SteamAPI_SteamNetworkingUtils_SteamAPI_v004) L(SteamAPI_ISteamNetworkingUtils_SetGlobalConfigValueInt32)
    L(SteamAPI_ISteamNetworkingUtils_InitRelayNetworkAccess)
#undef L
#define O(name) name = (t_##name)GetProcAddress(g_api, #name);
    O(SteamAPI_ISteamMatchmaking_InviteUserToLobby) O(SteamAPI_ISteamFriends_GetFriendPersonaState)
#undef O
    return ok;
}

/* ------------------------------------------------------------------ state */
enum { CH_GAME = 0, CH_CTRL = 1, CH_SVC = 2 };
enum { SEND_UNRELIABLE_NONAGLE = 1, SEND_RELIABLE_NONAGLE = 9 };
#define MAXPEERS 8
typedef struct { uint64_t id; SOCKET s; DWORD seen; unsigned long long tx, rx; } Peer;
static Peer g_peers[MAXPEERS]; static int g_npeers;
static void *g_user, *g_mm, *g_fr, *g_nm, *g_nu;
static int32_t g_pipe;
static uint64_t g_me, g_lobby, g_hostId;
static int g_isHost, g_isList, g_gamePort, g_localPort, g_inLobby, g_maxPlayers = 5;
static int g_lobbyType = 2;   /* 2 public, 1 friends only, 0 private (invite only) */
static char g_pwHash[24] = "", g_go[128] = ""; static int g_goSeq;
static char g_mode[32] = "", g_map[96] = "";
static int g_listPending; static DWORD g_listAsked;
static char g_state[128] = "starting";
static char g_dir[MAX_PATH];
static SOCKET g_cs = INVALID_SOCKET;          /* join: socket the guest game talks to */
static struct sockaddr_in g_gameAddr; static int g_haveGame;
static DWORD g_hostSeen; static unsigned long long g_tx, g_rx;
static volatile LONG g_quit;
/* svc mode (2026-10-21, Escalation through the game's menus): one relay per game, started by the DLL. A local UDP
 * control socket (port in relay.status "svcport=") talks to the DLL:
 *   DLL -> relay  'P' flags(1) to(8) payload   message to a Steam user (flags bit 0: reliable), channel CH_SVC
 *                 'L' text                      command: create <type> <max> | join <lobby> | leave | data <k> <v> |
 *                                               list | invite <id> | overlay | friends | gameport <n> |
 *                                               connect <hostId> <localPort> | disconnect | quit
 *   relay -> DLL  'M' from(8) payload           message from a Steam user
 *                 'E' text                      event: hello <me> <persona> | created <lobby> | joined <lobby> <owner> |
 *                                               joinfail | member+ <id> | member- <id> | lobby <id> <owner> <n>
 *                                               <max> <name> | listend | friend <id> <state> <lobby> <name> |
 *                                               friendsend | invite <from> <lobby> | joinrequest <lobby> <from>
 * Game traffic keeps the host/join tunnel: "gameport" makes this relay forward CH_GAME packets from anybody to the
 * hosting game; "connect" opens the local port the joining game connects to. */
static int g_isSvc; static SOCKET g_svc = INVALID_SOCKET; static int g_svcPort;
static struct sockaddr_in g_dllAddr; static int g_haveDll;
static uint64_t g_members[16]; static int g_nmembers;

static void Status(void) {
    if (g_isList) return;   /* the lobby browser must not overwrite the session relay's status */
    char p[MAX_PATH], t[MAX_PATH]; sprintf(p, "%s\\%s", g_dir, g_isSvc ? "relay_svc.status" : "relay.status"); sprintf(t, "%s.tmp", p);
    FILE* f = fopen(t, "w"); if (!f) return;
    fprintf(f, "role=%s\nstate=%s\nme=%llu\nlobby=%llu\nhost=%llu\npeers=%d\ntx=%llu\nrx=%llu\ngamePort=%d\nlocalPort=%d\npid=%lu\n",
            g_isSvc ? "svc" : g_isHost ? "host" : "join", g_state, g_me, g_lobby, g_hostId, g_isHost ? g_npeers : (g_hostId ? 1 : 0), g_tx, g_rx,
            g_gamePort, g_localPort, GetCurrentProcessId());
    fprintf(f, "go=%s\nlobbytype=%d\n", g_go, g_lobbyType);
    if (g_isSvc) fprintf(f, "svcport=%d\n", g_svcPort);
    for (int i = 0; i < g_npeers; i++) fprintf(f, "peer=%llu tx=%llu rx=%llu\n", g_peers[i].id, g_peers[i].tx, g_peers[i].rx);
    if (g_fr) { const char* pn = SteamAPI_ISteamFriends_GetPersonaName(g_fr); fprintf(f, "persona=%s\n", pn ? pn : ""); }
    fclose(f);
    MoveFileExA(t, p, MOVEFILE_REPLACE_EXISTING);
}
static void SetState(const char* s) { if (strcmp(s, g_state)) { strncpy(g_state, s, sizeof(g_state) - 1); printf("state: %s\n", s); Status(); } }

static void Ident(NetIdentity* id, uint64_t sid) { memset(id, 0, sizeof(*id)); SteamAPI_SteamNetworkingIdentity_SetSteamID64(id, sid); }
static void SendTo(uint64_t sid, const void* d, int n, int flags, int ch) {
    NetIdentity id; Ident(&id, sid);
    for (int tries = 0; tries < 5; tries++) {
        int r = SteamAPI_ISteamNetworkingMessages_SendMessageToUser(g_nm, &id, d, (uint32_t)n, flags, ch);
        if (r == 1 /* k_EResultOK */ || !(flags & 8)) return;
        Sleep(5);
    }
}
static int IsLobbyMember(uint64_t sid) {
    int n = SteamAPI_ISteamMatchmaking_GetNumLobbyMembers(g_mm, g_lobby);
    for (int i = 0; i < n; i++) if (SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex(g_mm, g_lobby, i) == sid) return 1;
    return 0;
}
static Peer* FindPeer(uint64_t sid) { for (int i = 0; i < g_npeers; i++) if (g_peers[i].id == sid) return &g_peers[i]; return NULL; }
static Peer* AddPeer(uint64_t sid) {
    if (g_npeers >= MAXPEERS) return NULL;
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in a = {0}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = 0;
    bind(s, (struct sockaddr*)&a, sizeof(a));
    a.sin_port = htons((u_short)g_gamePort); connect(s, (struct sockaddr*)&a, sizeof(a));
    u_long nb = 1; ioctlsocket(s, FIONBIO, &nb);
    Peer* p = &g_peers[g_npeers++]; memset(p, 0, sizeof(*p)); p->id = sid; p->s = s; p->seen = GetTickCount();
    printf("peer %llu (%s) connected, local socket -> 127.0.0.1:%d\n", sid, SteamAPI_ISteamFriends_GetFriendPersonaName(g_fr, sid), g_gamePort);
    return p;
}
static void DropPeer(int i, const char* why) {
    printf("peer %llu dropped: %s\n", g_peers[i].id, why);
    NetIdentity id; Ident(&id, g_peers[i].id); SteamAPI_ISteamNetworkingMessages_CloseSessionWithUser(g_nm, &id);
    closesocket(g_peers[i].s); g_peers[i] = g_peers[--g_npeers];
}

/* ------------------------------------------------------------------ lobby browser (list mode) */
static int IsFriend(uint64_t id) {
    int n = SteamAPI_ISteamFriends_GetFriendCount(g_fr, 4);
    for (int i = 0; i < n; i++) if (SteamAPI_ISteamFriends_GetFriendByIndex(g_fr, i, 4) == id) return 1;
    return 0;
}
static void Clean(char* s) { for (; *s; s++) if (*s == '|' || *s == '\n' || *s == '\r') *s = ' '; }
static void CopyData(char* out, int n, uint64_t l, const char* key) {
    const char* v = SteamAPI_ISteamMatchmaking_GetLobbyData(g_mm, l, key); strncpy(out, v ? v : "", n - 1); out[n - 1] = 0; Clean(out);
}
static void WriteLobbyList(uint32_t count) {
    char p[MAX_PATH], t[MAX_PATH]; sprintf(p, "%s\\lobbies.txt", g_dir); sprintf(t, "%s.tmp", p);
    FILE* f = fopen(t, "w"); if (!f) return;
    uint64_t seen[64]; int ns = 0;
    for (uint32_t i = 0; i < count && ns < 64; i++) {
        uint64_t l = SteamAPI_ISteamMatchmaking_GetLobbyByIndex(g_mm, (int)i); if (!l) continue;
        char name[64], mode[32], map[96], own[32], max[16];
        CopyData(name, 64, l, "name"); CopyData(mode, 32, l, "mode"); CopyData(map, 96, l, "map"); CopyData(own, 32, l, "owner"); CopyData(max, 16, l, "max");
        uint64_t owner = _strtoui64(own, NULL, 10);
        char pw[24], go[128]; CopyData(pw, 24, l, "pw"); CopyData(go, 128, l, "go");
        fprintf(f, "%llu|%llu|%s|%s|%s|%d|%d|%d|%s|%d\n", l, owner, name, mode, map, SteamAPI_ISteamMatchmaking_GetNumLobbyMembers(g_mm, l), atoi(max), owner && IsFriend(owner), pw[0] ? pw : "-", go[0] != 0);
        seen[ns++] = l;
    }
    /* friends in a foc-coop lobby the search did not return (an older host: friends-only lobby) */
    int n = SteamAPI_ISteamFriends_GetFriendCount(g_fr, 4);
    for (int i = 0; i < n; i++) {
        uint64_t fid = SteamAPI_ISteamFriends_GetFriendByIndex(g_fr, i, 4); FriendGameInfo_t gi; memset(&gi, 0, sizeof(gi));
        if (!SteamAPI_ISteamFriends_GetFriendGamePlayed(g_fr, fid, &gi) || (gi.gameID & 0xffffff) != 480 || !gi.lobby) continue;
        int dup = 0; for (int k = 0; k < ns; k++) if (seen[k] == gi.lobby) dup = 1;
        if (dup) continue;
        char name[64]; const char* fn = SteamAPI_ISteamFriends_GetFriendPersonaName(g_fr, fid); strncpy(name, fn ? fn : "?", 63); name[63] = 0; Clean(name);
        fprintf(f, "%llu|%llu|%s|?|?|0|0|1|0|0\n", gi.lobby, fid, name);
    }
    fclose(f);
    MoveFileExA(t, p, MOVEFILE_REPLACE_EXISTING);
}
static void RequestList(void) {
    SteamAPI_ISteamMatchmaking_AddRequestLobbyListStringFilter(g_mm, "foccoop", "1", 0 /* equal */);
    SteamAPI_ISteamMatchmaking_AddRequestLobbyListDistanceFilter(g_mm, 3 /* worldwide */);
    SteamAPI_ISteamMatchmaking_AddRequestLobbyListResultCountFilter(g_mm, 50);
    SteamAPI_ISteamMatchmaking_RequestLobbyList(g_mm);
    g_listPending = 1; g_listAsked = GetTickCount();
}

/* ------------------------------------------------------------------ svc mode: DLL control socket */
static void SvcRaw(char type, uint64_t id, const void* d, int n) {
    if (!g_haveDll || g_svc == INVALID_SOCKET || n < 0 || n > 1400) return;
    char buf[1500]; int k = 0; buf[k++] = type;
    if (type == 'M') { memcpy(buf + k, &id, 8); k += 8; }
    memcpy(buf + k, d, n); k += n;
    sendto(g_svc, buf, k, 0, (struct sockaddr*)&g_dllAddr, sizeof(g_dllAddr));
}
static void SvcEvent(const char* fmt, ...) {
    char t[1024]; va_list a; va_start(a, fmt); vsnprintf(t, sizeof(t), fmt, a); va_end(a);
    printf("event: %s\n", t); SvcRaw('E', 0, t, (int)strlen(t));
}
static void SvcMembers(void) {   /* lobby member changes -> member+/member- (polled: the mock sends no LobbyChatUpdate) */
    uint64_t now[16]; int n = 0;
    if (g_inLobby && g_lobby) { int c = SteamAPI_ISteamMatchmaking_GetNumLobbyMembers(g_mm, g_lobby); for (int i = 0; i < c && n < 16; i++) now[n++] = SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex(g_mm, g_lobby, i); }
    for (int i = 0; i < n; i++) { int had = 0; for (int k = 0; k < g_nmembers; k++) if (g_members[k] == now[i]) had = 1; if (!had && now[i] != g_me) SvcEvent("member+ %llu", now[i]); }
    for (int k = 0; k < g_nmembers; k++) { int has = 0; for (int i = 0; i < n; i++) if (g_members[k] == now[i]) has = 1; if (!has && g_members[k] != g_me) SvcEvent("member- %llu", g_members[k]); }
    memcpy(g_members, now, sizeof(uint64_t) * n); g_nmembers = n;
}
static void SvcFriends(void) {
    int n = SteamAPI_ISteamFriends_GetFriendCount(g_fr, 4);
    for (int i = 0; i < n; i++) {
        uint64_t f = SteamAPI_ISteamFriends_GetFriendByIndex(g_fr, i, 4); FriendGameInfo_t gi; memset(&gi, 0, sizeof(gi));
        int st = SteamAPI_ISteamFriends_GetFriendPersonaState ? SteamAPI_ISteamFriends_GetFriendPersonaState(g_fr, f) : 1;
        uint64_t lobby = (SteamAPI_ISteamFriends_GetFriendGamePlayed(g_fr, f, &gi) && (gi.gameID & 0xffffff) == 480) ? (gi.lobby ? gi.lobby : 1) : 0;
        const char* nm = SteamAPI_ISteamFriends_GetFriendPersonaName(g_fr, f); char name[64]; strncpy(name, nm ? nm : "?", 63); name[63] = 0; Clean(name);
        SvcEvent("friend %llu %d %llu %s", f, st, lobby, name);
    }
    SvcEvent("friendsend");
}
static void SvcLeave(void) {
    if (g_lobby) SteamAPI_ISteamMatchmaking_LeaveLobby(g_mm, g_lobby);
    g_lobby = 0; g_inLobby = 0; g_nmembers = 0; g_isHost = 0;
}
static void SvcCommand(const char* c) {
    printf("svc: %s\n", c);
    if (!strncmp(c, "create ", 7)) {
        int type = 2, max = 4; sscanf(c + 7, "%d %d", &type, &max);
        SvcLeave(); g_lobbyType = type; g_maxPlayers = max; g_isHost = 1;
        SteamAPI_ISteamMatchmaking_CreateLobby(g_mm, type, max);
    } else if (!strncmp(c, "join ", 5)) {
        SvcLeave(); SteamAPI_ISteamMatchmaking_JoinLobby(g_mm, _strtoui64(c + 5, NULL, 10));
    } else if (!strcmp(c, "leave")) SvcLeave();
    else if (!strncmp(c, "data ", 5) && g_lobby && g_isHost) {
        char k[64] = "", v[256] = ""; if (sscanf(c + 5, "%63s %255[^\n]", k, v) >= 1) SteamAPI_ISteamMatchmaking_SetLobbyData(g_mm, g_lobby, k, v);
    } else if (!strcmp(c, "list")) {
        SteamAPI_ISteamMatchmaking_AddRequestLobbyListStringFilter(g_mm, "focesc", "1", 0);
        SteamAPI_ISteamMatchmaking_AddRequestLobbyListDistanceFilter(g_mm, 3);
        SteamAPI_ISteamMatchmaking_AddRequestLobbyListResultCountFilter(g_mm, 50);
        SteamAPI_ISteamMatchmaking_RequestLobbyList(g_mm); g_listPending = 1; g_listAsked = GetTickCount();
    } else if (!strncmp(c, "invite ", 7) && g_lobby && SteamAPI_ISteamMatchmaking_InviteUserToLobby) {
        SteamAPI_ISteamMatchmaking_InviteUserToLobby(g_mm, g_lobby, _strtoui64(c + 7, NULL, 10));
    } else if (!strcmp(c, "overlay") && g_lobby) SteamAPI_ISteamFriends_ActivateGameOverlayInviteDialog(g_fr, g_lobby);
    else if (!strcmp(c, "friends")) SvcFriends();
    else if (!strncmp(c, "gameport ", 9)) {
        g_gamePort = atoi(c + 9);
        struct sockaddr_in a = {0}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = htons((u_short)g_gamePort);
        for (int i = 0; i < g_npeers; i++) connect(g_peers[i].s, (struct sockaddr*)&a, sizeof(a));
        Status();
    } else if (!strncmp(c, "connect ", 8)) {
        uint64_t h = 0; int port = 7790; sscanf(c + 8, "%llu %d", &h, &port);
        if (g_cs == INVALID_SOCKET || port != g_localPort) {
            if (g_cs != INVALID_SOCKET) closesocket(g_cs);
            g_cs = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            struct sockaddr_in a = {0}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = htons((u_short)port);
            if (bind(g_cs, (struct sockaddr*)&a, sizeof(a))) { SvcEvent("connectfail port %d busy", port); closesocket(g_cs); g_cs = INVALID_SOCKET; return; }
            u_long nb = 1; ioctlsocket(g_cs, FIONBIO, &nb);
        }
        g_hostId = h; g_localPort = port; g_haveGame = 0; g_hostSeen = GetTickCount();
        SendTo(g_hostId, "H", 1, SEND_RELIABLE_NONAGLE, CH_CTRL);
        SvcEvent("connecting %llu %d", h, port); Status();
    } else if (!strcmp(c, "disconnect")) {
        if (g_hostId) SendTo(g_hostId, "B", 1, SEND_RELIABLE_NONAGLE, CH_CTRL);
        g_hostId = 0; g_haveGame = 0;
    } else if (!strcmp(c, "ping")) SvcEvent("hello %llu %s", g_me, SteamAPI_ISteamFriends_GetPersonaName(g_fr));
    else if (!strcmp(c, "quit")) g_quit = 1;
}
static void PumpSvc(void) {
    char buf[1500]; struct sockaddr_in from; int fl = sizeof(from), n;
    while ((n = recvfrom(g_svc, buf, sizeof(buf), 0, (struct sockaddr*)&from, &fl)) > 0) {
        fl = sizeof(from);
        int first = !g_haveDll; g_dllAddr = from; g_haveDll = 1;
        if (first) SvcEvent("hello %llu %s", g_me, SteamAPI_ISteamFriends_GetPersonaName(g_fr));
        if (buf[0] == 'P' && n >= 10) {
            uint64_t to; memcpy(&to, buf + 2, 8);
            SendTo(to, buf + 10, n - 10, (buf[1] & 1) ? SEND_RELIABLE_NONAGLE : SEND_UNRELIABLE_NONAGLE, CH_SVC);
        } else if (buf[0] == 'L') { buf[n < (int)sizeof(buf) ? n : (int)sizeof(buf) - 1] = 0; SvcCommand(buf + 1); }
    }
}

/* ------------------------------------------------------------------ callbacks */
static void OnLobbyEntered(uint64_t lobby, uint32_t response) {
    if (response != 1) { SetState("joining the lobby failed"); return; }
    g_lobby = lobby; g_inLobby = 1;
    if (g_isHost) {
        SteamAPI_ISteamMatchmaking_SetLobbyData(g_mm, lobby, "foccoop", "1");
        char v[64]; sprintf(v, "%d", g_gamePort); SteamAPI_ISteamMatchmaking_SetLobbyData(g_mm, lobby, "port", v);
        sprintf(v, "%llu", g_me); SteamAPI_ISteamMatchmaking_SetLobbyData(g_mm, lobby, "owner", v);
        SteamAPI_ISteamMatchmaking_SetLobbyData(g_mm, lobby, "name", SteamAPI_ISteamFriends_GetPersonaName(g_fr));
        SteamAPI_ISteamMatchmaking_SetLobbyData(g_mm, lobby, "mode", g_mode);
        SteamAPI_ISteamMatchmaking_SetLobbyData(g_mm, lobby, "map", g_map);
        sprintf(v, "%d", g_maxPlayers); SteamAPI_ISteamMatchmaking_SetLobbyData(g_mm, lobby, "max", v);
        SteamAPI_ISteamMatchmaking_SetLobbyData(g_mm, lobby, "pw", g_pwHash);
        SteamAPI_ISteamMatchmaking_SetLobbyData(g_mm, lobby, "go", "");
        char c[64]; sprintf(c, "+connect_lobby %llu", lobby);
        SteamAPI_ISteamFriends_SetRichPresence(g_fr, "connect", c);
        SteamAPI_ISteamFriends_SetRichPresence(g_fr, "status", "Hosting Fall of Cybertron co-op");
        g_hostId = g_me;
        SetState("hosting: lobby open, waiting for friends");
    } else {
        const char* tag = SteamAPI_ISteamMatchmaking_GetLobbyData(g_mm, lobby, "foccoop");
        if (!tag || strcmp(tag, "1")) { SetState("that lobby is not a Fall of Cybertron co-op game"); return; }
        g_hostId = SteamAPI_ISteamMatchmaking_GetLobbyOwner(g_mm, lobby);
        g_hostSeen = GetTickCount();
        SendTo(g_hostId, "H", 1, SEND_RELIABLE_NONAGLE, CH_CTRL);   /* hello: lets the host accept our session */
        SetState("joined: waiting for the game to connect");
    }
    printf("lobby %llu, host %llu\n", g_lobby, g_hostId);
}

static void SvcDispatch(int cb, uint8_t* data) {
    if (cb == 513) {                                         /* LobbyCreated (twice on real Steam) */
        LobbyCreated_t* lc = (LobbyCreated_t*)data;
        if (g_inLobby) return;
        if (lc->result != 1) { SvcEvent("createfail %d", lc->result); g_isHost = 0; return; }
        g_lobby = lc->lobby; g_inLobby = 1; g_hostId = 0;
        char v[64];
        SteamAPI_ISteamMatchmaking_SetLobbyData(g_mm, g_lobby, "foccoop", "1");
        SteamAPI_ISteamMatchmaking_SetLobbyData(g_mm, g_lobby, "focesc", "1");
        sprintf(v, "%llu", g_me); SteamAPI_ISteamMatchmaking_SetLobbyData(g_mm, g_lobby, "owner", v);
        SteamAPI_ISteamMatchmaking_SetLobbyData(g_mm, g_lobby, "name", SteamAPI_ISteamFriends_GetPersonaName(g_fr));
        sprintf(v, "%d", g_maxPlayers); SteamAPI_ISteamMatchmaking_SetLobbyData(g_mm, g_lobby, "max", v);
        sprintf(v, "+connect_lobby %llu", g_lobby); SteamAPI_ISteamFriends_SetRichPresence(g_fr, "connect", v);
        SteamAPI_ISteamFriends_SetRichPresence(g_fr, "status", "Fall of Cybertron Escalation");
        SetState("svc: lobby open"); SvcEvent("created %llu", g_lobby); SvcMembers();
    } else if (cb == 504) {                                  /* LobbyEnter */
        LobbyEnter_t* le = (LobbyEnter_t*)data;
        if (g_isHost || g_inLobby) return;
        if (le->response != 1) { SvcEvent("joinfail %u", le->response); return; }
        g_lobby = le->lobby; g_inLobby = 1;
        SetState("svc: in a lobby");
        { char chan[32]; CopyData(chan, 32, g_lobby, "chan");   /* the owner's party channel (DLL steamnet.inc) */
          SvcEvent("joined %llu %llu %s", g_lobby, SteamAPI_ISteamMatchmaking_GetLobbyOwner(g_mm, g_lobby), chan[0] ? chan : "0"); }
        SvcMembers();
    } else if (cb == 1251) {                                 /* SessionRequest: anybody may talk to us (public matchmaking) */
        NetIdentity* id = (NetIdentity*)data; printf("session request from %llu: accepted\n", SteamAPI_SteamNetworkingIdentity_GetSteamID64(id));
        SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser(g_nm, id);
    } else if (cb == 510) {                                  /* LobbyMatchList */
        uint32_t count = ((LobbyMatchList_t*)data)->count; g_listPending = 0;
        for (uint32_t i = 0; i < count && i < 50; i++) {
            uint64_t l = SteamAPI_ISteamMatchmaking_GetLobbyByIndex(g_mm, (int)i); if (!l) continue;
            char name[64], own[32], max[16]; CopyData(name, 64, l, "name"); CopyData(own, 32, l, "owner"); CopyData(max, 16, l, "max");
            SvcEvent("lobby %llu %s %d %s %s", l, own[0] ? own : "0", SteamAPI_ISteamMatchmaking_GetNumLobbyMembers(g_mm, l), max[0] ? max : "0", name);
        }
        SvcEvent("listend");
    } else if (cb == 503) {                                  /* LobbyInvite: user, lobby, game id */
        uint64_t* v = (uint64_t*)data; SvcEvent("invite %llu %llu", v[0], v[1]);
    } else if (cb == 333) {                                  /* GameLobbyJoinRequested (accepted in the overlay) */
        GameLobbyJoinRequested_t* j = (GameLobbyJoinRequested_t*)data; SvcEvent("joinrequest %llu %llu", j->lobby, j->friend_id);
    }
}
static void Dispatch(void) {
    CallbackMsg_t m;
    SteamAPI_ManualDispatch_RunFrame(g_pipe);
    while (SteamAPI_ManualDispatch_GetNextCallback(g_pipe, &m)) {
        int cb = m.callback; uint8_t* data = m.param; uint8_t* buf = NULL;
        if (cb == 703) {
            SteamAPICallCompleted_t* d = (SteamAPICallCompleted_t*)m.param; bool8 failed = 0;
            buf = (uint8_t*)malloc(d->param_size + 16);
            if (SteamAPI_ManualDispatch_GetAPICallResult(g_pipe, d->call, buf, d->param_size, d->callback, &failed) && !failed) { cb = d->callback; data = buf; }
            else cb = 0;
        }
        if (g_isSvc) SvcDispatch(cb, data);
        else if (cb == 513 && !g_inLobby) {                  /* LobbyCreated (arrives twice on real Steam) */
            LobbyCreated_t* lc = (LobbyCreated_t*)data;
            if (lc->result != 1) { printf("CreateLobby failed: %d\n", lc->result); SetState("creating the lobby failed"); }
            else OnLobbyEntered(lc->lobby, 1);
        } else if (cb == 504 && !g_isHost && !g_inLobby) {   /* LobbyEnter */
            LobbyEnter_t* le = (LobbyEnter_t*)data; OnLobbyEntered(le->lobby, le->response);
        } else if (cb == 1251) {                             /* SessionRequest: identity first */
            NetIdentity* id = (NetIdentity*)data; uint64_t sid = SteamAPI_SteamNetworkingIdentity_GetSteamID64(id);
            int ok = g_isHost ? IsLobbyMember(sid) : (sid == g_hostId);
            printf("session request from %llu: %s\n", sid, ok ? "accepted" : "refused (not in the lobby)");
            if (ok) SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser(g_nm, id);
        } else if (cb == 510 && g_isList) {                  /* LobbyMatchList */
            WriteLobbyList(((LobbyMatchList_t*)data)->count); g_listPending = 0;
        } else if (cb == 506 && g_isHost) {                  /* LobbyChatUpdate: someone left */
            LobbyChatUpdate_t* u = (LobbyChatUpdate_t*)data;
            if (u->state_change & (2 | 4 | 8 | 16)) for (int i = 0; i < g_npeers; i++) if (g_peers[i].id == u->user_changed) { DropPeer(i, "left the lobby"); break; }
        } else if (cb == 333 && !g_isHost && !g_inLobby) {   /* invite accepted while running */
            GameLobbyJoinRequested_t* j = (GameLobbyJoinRequested_t*)data; SteamAPI_ISteamMatchmaking_JoinLobby(g_mm, j->lobby);
        }
        if (buf) free(buf);
        SteamAPI_ManualDispatch_FreeLastCallback(g_pipe);
    }
}

/* ------------------------------------------------------------------ packet pumps */
static void PumpSteam(void) {
    void* msgs[64];
    for (int ch = 0; ch < (g_isSvc ? 3 : 2); ch++) {
        int n = SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel(g_nm, ch, msgs, 64);
        for (int i = 0; i < n; i++) {
            uint8_t* m = (uint8_t*)msgs[i];
            const char* data = *(const char**)m; int size = *(int*)(m + 4);
            uint64_t sid = SteamAPI_SteamNetworkingIdentity_GetSteamID64((NetIdentity*)(m + 16));
            if (!sid) sid = SteamAPI_SteamNetworkingIdentity_GetSteamID64((NetIdentity*)(m + 12));
            /* svc: CH_SVC goes to the DLL; game traffic from our host goes to the joining game, from anybody else to
               the hosting game (when it has a port) */
            int hosting = g_isSvc ? (g_gamePort > 0 && sid != g_hostId) : g_isHost;
            if (ch == CH_SVC) { SvcRaw('M', sid, data, size); }
            else if (hosting) {
                Peer* p = FindPeer(sid);
                if (!p && (g_isSvc || IsLobbyMember(sid))) p = AddPeer(sid);
                if (p) {
                    p->seen = GetTickCount();
                    if (ch == CH_GAME) { send(p->s, data, size, 0); p->rx += size; g_rx += size; }
                    else if (size >= 1 && data[0] == 'B') { for (int k = 0; k < g_npeers; k++) if (&g_peers[k] == p) { DropPeer(k, "said goodbye"); break; } }
                }
            } else if (sid == g_hostId) {
                g_hostSeen = GetTickCount();
                if (ch == CH_GAME && g_haveGame) { sendto(g_cs, data, size, 0, (struct sockaddr*)&g_gameAddr, sizeof(g_gameAddr)); g_rx += size; }
            }
            SteamAPI_SteamNetworkingMessage_t_Release(m);
        }
    }
}
static void PumpLocal(void) {
    char buf[2048];
    if (g_isHost || g_isSvc) {
        for (int i = 0; i < g_npeers; i++) {
            int n;
            while ((n = recv(g_peers[i].s, buf, sizeof(buf), 0)) > 0) { SendTo(g_peers[i].id, buf, n, SEND_UNRELIABLE_NONAGLE, CH_GAME); g_peers[i].tx += n; g_tx += n; }
        }
    }
    if ((!g_isHost || g_isSvc) && g_cs != INVALID_SOCKET) {
        struct sockaddr_in from; int fl = sizeof(from), n;
        while ((n = recvfrom(g_cs, buf, sizeof(buf), 0, (struct sockaddr*)&from, &fl)) > 0) {
            if (!g_haveGame || from.sin_port != g_gameAddr.sin_port) { g_gameAddr = from; g_haveGame = 1; SetState("connected: game traffic flows through Steam"); }
            if (g_hostId) { SendTo(g_hostId, buf, n, SEND_UNRELIABLE_NONAGLE, CH_GAME); g_tx += n; }
            fl = sizeof(from);
        }
    }
}

/* the relay belongs to the game copy in the parent folder; stop when no TFOC.exe from there runs any more */
static int GameRunning(void) {
    char parent[MAX_PATH]; strcpy(parent, g_dir); char* s = strrchr(parent, '\\'); if (s) *s = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0); PROCESSENTRY32 pe = {sizeof(pe)}; int found = 0;
    if (Process32First(snap, &pe)) do {
        if (_stricmp(pe.szExeFile, "TFOC.exe")) continue;
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID); char path[MAX_PATH]; DWORD n = MAX_PATH;
        if (h && QueryFullProcessImageNameA(h, 0, path, &n)) { char* q = strrchr(path, '\\'); if (q) *q = 0; if (!_stricmp(path, parent)) found = 1; }
        if (h) CloseHandle(h);
    } while (!found && Process32Next(snap, &pe));
    CloseHandle(snap); return found;
}
static BOOL WINAPI OnCtrl(DWORD t) { (void)t; g_quit = 1; return TRUE; }

static uint64_t FindHostingFriend(void) {
    int n = SteamAPI_ISteamFriends_GetFriendCount(g_fr, 4 /* immediate */);
    for (int i = 0; i < n; i++) {
        uint64_t f = SteamAPI_ISteamFriends_GetFriendByIndex(g_fr, i, 4); FriendGameInfo_t gi; memset(&gi, 0, sizeof(gi));
        if (SteamAPI_ISteamFriends_GetFriendGamePlayed(g_fr, f, &gi) && (gi.gameID & 0xffffff) == 480 && gi.lobby) {
            printf("friend %s hosts lobby %llu\n", SteamAPI_ISteamFriends_GetFriendPersonaName(g_fr, f), gi.lobby);
            return gi.lobby;
        }
    }
    return 0;
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    GetModuleFileNameA(NULL, g_dir, MAX_PATH); { char* s = strrchr(g_dir, '\\'); if (s) *s = 0; }
    g_isList = argc >= 2 && !strcmp(argv[1], "list");
    g_isSvc = argc >= 2 && !strcmp(argv[1], "svc");
    {   /* log next to the exe (relay.log, relay_list.log), started fresh above 2 MB */
        char lp[MAX_PATH]; sprintf(lp, "%s\\%s", g_dir, g_isList ? "relay_list.log" : g_isSvc ? "relay_svc.log" : "relay.log");
        WIN32_FILE_ATTRIBUTE_DATA fa; if (GetFileAttributesExA(lp, GetFileExInfoStandard, &fa) && fa.nFileSizeLow > 2000000) DeleteFileA(lp);
        if (freopen(lp, "a", stdout)) setvbuf(stdout, NULL, _IONBF, 0);
        SYSTEMTIME st; GetLocalTime(&st); printf("\n==== %04d-%02d-%02d %02d:%02d:%02d FoCRelay", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        for (int i = 1; i < argc; i++) printf(" %s", argv[i]);
        printf("\n");
    }
    if (!g_isList && !g_isSvc && (argc < 3 || (strcmp(argv[1], "host") && strcmp(argv[1], "join")))) { printf("usage: FoCRelay host <gamePort> [max] [mode] [map] | join <lobbyId|auto> <localPort> | list | svc\n"); return 2; }
    g_isHost = !strcmp(argv[1], "host");
    if (g_isHost) {
        g_gamePort = atoi(argv[2]);
        if (argc > 3) g_maxPlayers = atoi(argv[3]);
        if (argc > 4) strncpy(g_mode, argv[4], sizeof(g_mode) - 1);
        if (argc > 5) strncpy(g_map, argv[5], sizeof(g_map) - 1);
        if (argc > 6) g_lobbyType = !strcmp(argv[6], "friends") ? 1 : !strcmp(argv[6], "private") ? 0 : 2;
        if (argc > 7 && strcmp(argv[7], "-")) strncpy(g_pwHash, argv[7], sizeof(g_pwHash) - 1);
        if (g_maxPlayers < 2 || g_maxPlayers > 8) g_maxPlayers = 5;
    } else if (!g_isList && !g_isSvc) g_localPort = argc > 3 ? atoi(argv[3]) : 7790;
    SetConsoleCtrlHandler(OnCtrl, TRUE);
    { char p[MAX_PATH]; sprintf(p, "%s\\steam_appid.txt", g_dir); FILE* f = fopen(p, "w"); if (f) { fputs("480", f); fclose(f); } }
    SetCurrentDirectoryA(g_dir);
    WSADATA wd; WSAStartup(MAKEWORD(2, 2), &wd);
    if (!LoadApi()) { SetState("steam_api.dll missing or too old (needs the flat API)"); return 3; }
    char err[1024] = "";
    int ir = SteamAPI_InitFlat(err);
    if (ir) { char s[200]; sprintf(s, "Steam init failed (%d: %s) - is Steam running and logged in?", ir, err); SetState(s); return 4; }
    SteamAPI_ManualDispatch_Init(); g_pipe = SteamAPI_GetHSteamPipe();
    g_user = SteamAPI_SteamUser_v023(); g_mm = SteamAPI_SteamMatchmaking_v009(); g_fr = SteamAPI_SteamFriends_v018();
    g_nm = SteamAPI_SteamNetworkingMessages_SteamAPI_v002(); g_nu = SteamAPI_SteamNetworkingUtils_SteamAPI_v004();
    g_me = SteamAPI_ISteamUser_GetSteamID(g_user);
    SteamAPI_ISteamNetworkingUtils_InitRelayNetworkAccess(g_nu);
    printf("Steam OK as %s (%llu)\n", SteamAPI_ISteamFriends_GetPersonaName(g_fr), g_me);
    if (g_isSvc) {
        g_svc = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        struct sockaddr_in a = {0}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = 0;
        bind(g_svc, (struct sockaddr*)&a, sizeof(a)); int al = sizeof(a); getsockname(g_svc, (struct sockaddr*)&a, &al);
        g_svcPort = ntohs(a.sin_port); u_long nb = 1; ioctlsocket(g_svc, FIONBIO, &nb);
        printf("svc control port %d\n", g_svcPort); SetState("svc: ready"); Status();
    } else if (g_isHost) {
        SteamAPI_ISteamMatchmaking_CreateLobby(g_mm, g_lobbyType /* 2 public, 1 friends only, 0 private */, g_maxPlayers);
        SetState("creating lobby");
    } else if (g_isList) {
        RequestList();
    } else {
        g_cs = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        struct sockaddr_in a = {0}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = htons((u_short)g_localPort);
        if (bind(g_cs, (struct sockaddr*)&a, sizeof(a))) { SetState("local port busy"); return 5; }
        u_long nb = 1; ioctlsocket(g_cs, FIONBIO, &nb);
        uint64_t lobby = strcmp(argv[2], "auto") ? _strtoui64(argv[2], NULL, 10) : 0;
        SetState(lobby ? "joining lobby" : "looking for a friend who hosts");
        if (lobby) SteamAPI_ISteamMatchmaking_JoinLobby(g_mm, lobby);
    }
    DWORD lastStatus = 0, lastKeep = 0, lastSearch = 0, lastGame = GetTickCount();
    while (!g_quit) {
        Dispatch(); if (!g_isList) { PumpSteam(); PumpLocal(); }
        if (g_isSvc) PumpSvc();
        DWORD now = GetTickCount();
        if (g_isList && now - g_listAsked > (g_listPending ? 15000u : 3000u)) RequestList();
        if (!g_isHost && !g_isList && !g_isSvc && !g_inLobby && !strcmp(argv[2], "auto") && now - lastSearch > 2000) {   /* list mode has no argv[2] */
            lastSearch = now; uint64_t l = FindHostingFriend(); if (l) { SetState("joining lobby"); SteamAPI_ISteamMatchmaking_JoinLobby(g_mm, l); }
        }
        if (g_isSvc && now - lastKeep > 500) {      /* svc: lobby member changes, keepalives for the game tunnel */
            lastKeep = now; SvcMembers();
            static DWORD lastK; if (now - lastK > 1000) {
                lastK = now;
                for (int i = 0; i < g_npeers; i++) SendTo(g_peers[i].id, "K", 1, SEND_RELIABLE_NONAGLE, CH_CTRL);
                for (int i = g_npeers - 1; i >= 0; i--) if (now - g_peers[i].seen > 20000) DropPeer(i, "silent for 20 s");
                if (g_hostId) SendTo(g_hostId, "K", 1, SEND_RELIABLE_NONAGLE, CH_CTRL);
            }
        } else if (!g_isSvc && now - lastKeep > 1000 && g_inLobby) {
            lastKeep = now;
            if (g_isHost) {
                for (int i = 0; i < g_npeers; i++) SendTo(g_peers[i].id, "K", 1, SEND_RELIABLE_NONAGLE, CH_CTRL);
                for (int i = g_npeers - 1; i >= 0; i--) if (now - g_peers[i].seen > 20000) DropPeer(i, "silent for 20 s");
                SetState(g_npeers ? "hosting: friends connected" : "hosting: lobby open, waiting for friends");
            } else if (g_hostId) {
                SendTo(g_hostId, "K", 1, SEND_RELIABLE_NONAGLE, CH_CTRL);
                const char* go = SteamAPI_ISteamMatchmaking_GetLobbyData(g_mm, g_lobby, "go");   /* host started a mission */
                if (go && strcmp(go, g_go)) { strncpy(g_go, go, sizeof(g_go) - 1); printf("go: %s\n", g_go); Status(); }
                if (now - g_hostSeen > 20000) SetState("host silent for 20 s");
            }
        }
        if (now - lastStatus > 1000) {
            lastStatus = now; Status();
            if (GameRunning()) lastGame = now; else if (now - lastGame > 60000) { printf("no game running for 60 s, exiting\n"); break; }
            char cp[MAX_PATH]; sprintf(cp, "%s\\%s", g_dir, g_isList ? "list.cmd" : g_isSvc ? "relay_svc.cmd" : "relay.cmd"); FILE* f = fopen(cp, "r");
            if (f) {
                char c[160] = ""; fgets(c, sizeof(c), f); fclose(f); DeleteFileA(cp);
                c[strcspn(c, "\r\n")] = 0;
                if (!strncmp(c, "quit", 4)) break;
                if (!strncmp(c, "invite", 6) && g_lobby) SteamAPI_ISteamFriends_ActivateGameOverlayInviteDialog(g_fr, g_lobby);
                if (!strncmp(c, "port ", 5) && g_isHost) {   /* the hosting game opened its listen socket */
                    g_gamePort = atoi(c + 5);
                    char v[16]; sprintf(v, "%d", g_gamePort); if (g_lobby) SteamAPI_ISteamMatchmaking_SetLobbyData(g_mm, g_lobby, "port", v);
                    struct sockaddr_in a = {0}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = htons((u_short)g_gamePort);
                    for (int i = 0; i < g_npeers; i++) connect(g_peers[i].s, (struct sockaddr*)&a, sizeof(a));
                    printf("game port %d\n", g_gamePort); Status();
                }
                if (!strncmp(c, "go ", 3) && g_isHost && g_lobby) {   /* the host started a mission: guests follow */
                    strncpy(g_map, c + 3, sizeof(g_map) - 1); SteamAPI_ISteamMatchmaking_SetLobbyData(g_mm, g_lobby, "map", g_map);
                    sprintf(g_go, "%d|%s", ++g_goSeq, g_map); SteamAPI_ISteamMatchmaking_SetLobbyData(g_mm, g_lobby, "go", g_go);
                    printf("go: %s\n", g_go); Status();
                }
            }
        }
        Sleep(1);
    }
    if (g_isHost) for (int i = 0; i < g_npeers; i++) SendTo(g_peers[i].id, "B", 1, SEND_RELIABLE_NONAGLE, CH_CTRL);
    else if (g_hostId) SendTo(g_hostId, "B", 1, SEND_RELIABLE_NONAGLE, CH_CTRL);
    if (g_lobby) SteamAPI_ISteamMatchmaking_LeaveLobby(g_mm, g_lobby);
    SteamAPI_ISteamFriends_ClearRichPresence(g_fr);
    SetState("stopped");
    SteamAPI_Shutdown();
    return 0;
}
