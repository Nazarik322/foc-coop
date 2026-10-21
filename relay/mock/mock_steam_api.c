/* Mock steam_api.dll (flat API subset used by FoCRelay) for testing two relays on one PC without two Steam accounts.
 * - SteamID: env FOC_MOCK_STEAMID, else derived from the folder of the loading exe (copies A and B differ).
 * - Lobbies, friends and rich presence: small text files in %TEMP%\foc_mock_steam\.
 * - Messages: localhost UDP, one port per SteamID; the first message from a stranger is held until AcceptSession,
 *   like real Steam (SessionRequest callback 1251).
 * - Call results (CreateLobby, JoinLobby) come back through ManualDispatch as SteamAPICallCompleted (703). */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define API __cdecl
typedef int bool8;
#pragma pack(push, 8)
typedef struct { int32_t hSteamUser; int32_t callback; uint8_t* param; int32_t param_size; } CallbackMsg_t;
typedef struct { uint64_t call; int32_t callback; uint32_t param_size; } SteamAPICallCompleted_t;
typedef struct { int32_t result; uint64_t lobby; } LobbyCreated_t;
typedef struct { uint64_t lobby; uint32_t perms; uint8_t locked; uint32_t response; } LobbyEnter_t;
typedef struct { uint64_t gameID; uint32_t ip; uint16_t port, qport; uint64_t lobby; } FriendGameInfo_t;
#pragma pack(pop)

static uint64_t g_me; static char g_name[64]; static char g_root[MAX_PATH];
static SOCKET g_sock = INVALID_SOCKET;
static CRITICAL_SECTION g_cs; static int g_init;

/* ---------------------------------------------------------------- files */
static void UserPath(uint64_t id, char* out) { sprintf(out, "%s\\users\\%llu.txt", g_root, id); }
static void LobbyPath(uint64_t id, char* out) { sprintf(out, "%s\\lobbies\\%llu.txt", g_root, id); }
static int ReadLines(const char* path, char lines[][160], int max) {
    FILE* f = fopen(path, "r"); if (!f) return -1; int n = 0;
    while (n < max && fgets(lines[n], 160, f)) { size_t l = strlen(lines[n]); while (l && (lines[n][l - 1] == '\n' || lines[n][l - 1] == '\r')) lines[n][--l] = 0; n++; }
    fclose(f); return n;
}
static void WriteLines(const char* path, char lines[][160], int n) { FILE* f = fopen(path, "w"); if (!f) return; for (int i = 0; i < n; i++) fprintf(f, "%s\n", lines[i]); fclose(f); }
static uint16_t PortOf(uint64_t id) { return (uint16_t)(41000 + (id % 9000)); }
static void WriteUser(uint64_t lobby) {
    char p[MAX_PATH]; UserPath(g_me, p); FILE* f = fopen(p, "w"); if (!f) return;
    fprintf(f, "name=%s\nlobby=%llu\n", g_name, lobby); fclose(f);
}

/* ---------------------------------------------------------------- callbacks */
typedef struct { int cb; uint8_t* param; int size; } Cb;
typedef struct { uint64_t call; int cb; uint8_t data[64]; int size; } CallResult;
static Cb g_cbs[256]; static int g_ncb; static Cb g_cur; static int g_haveCur;
static CallResult g_results[64]; static int g_nres; static uint64_t g_nextCall = 1000;
static void PushCb(int cb, const void* p, int size) { if (g_ncb >= 256) return; Cb* c = &g_cbs[g_ncb++]; c->cb = cb; c->size = size; c->param = (uint8_t*)malloc(size); memcpy(c->param, p, size); }
static uint64_t PushResult(int cb, const void* p, int size) {
    uint64_t call = g_nextCall++;
    if (g_nres < 64) { CallResult* r = &g_results[g_nres++]; r->call = call; r->cb = cb; r->size = size; memcpy(r->data, p, size); }
    SteamAPICallCompleted_t d = {call, cb, (uint32_t)size}; PushCb(703, &d, sizeof(d));
    return call;
}

/* ---------------------------------------------------------------- messages */
typedef struct { uint64_t from; int ch; int size; char* data; } Msg;
static Msg g_msgs[1024]; static int g_nmsg;
static uint64_t g_accepted[64]; static int g_nacc; static uint64_t g_requested[64]; static int g_nreq;
static int Accepted(uint64_t id) { for (int i = 0; i < g_nacc; i++) if (g_accepted[i] == id) return 1; return 0; }
static void Drain(void) {
    char buf[4096]; int n;
    while ((n = recv(g_sock, buf, sizeof(buf), 0)) > 9) {
        uint64_t from; memcpy(&from, buf, 8);
        if (!Accepted(from)) {   /* real Steam: SessionRequest, message held until accepted */
            int asked = 0; for (int i = 0; i < g_nreq; i++) if (g_requested[i] == from) asked = 1;
            if (!asked && g_nreq < 64) { g_requested[g_nreq++] = from; uint8_t id[160] = {0}; memcpy(id, &from, 8); PushCb(1251, id, 136); }
        }
        if (g_nmsg >= 1024) continue;
        Msg* m = &g_msgs[g_nmsg++]; m->from = from; m->ch = (uint8_t)buf[8]; m->size = n - 9; m->data = (char*)malloc(m->size); memcpy(m->data, buf + 9, m->size);
    }
}

/* ---------------------------------------------------------------- init */
__declspec(dllexport) bool8 API SteamAPI_IsSteamRunning(void) { return 1; }
__declspec(dllexport) int API SteamAPI_InitFlat(char* err) {
    if (g_init) return 0;
    InitializeCriticalSection(&g_cs);
    char v[64];
    if (GetEnvironmentVariableA("FOC_MOCK_STEAMID", v, sizeof(v))) g_me = _strtoui64(v, NULL, 10);
    else { char p[MAX_PATH]; GetModuleFileNameA(NULL, p, MAX_PATH); uint32_t h = 2166136261u; for (char* c = p; *c; c++) h = (h ^ (uint8_t)*c) * 16777619u; g_me = 76561190000000000ull + (h % 100000); }
    if (!GetEnvironmentVariableA("FOC_MOCK_NAME", g_name, sizeof(g_name))) sprintf(g_name, "mock_%llu", g_me % 100000);
    GetTempPathA(MAX_PATH, g_root); strcat(g_root, "foc_mock_steam");
    CreateDirectoryA(g_root, NULL); char d[MAX_PATH]; sprintf(d, "%s\\users", g_root); CreateDirectoryA(d, NULL); sprintf(d, "%s\\lobbies", g_root); CreateDirectoryA(d, NULL);
    WSADATA wd; WSAStartup(MAKEWORD(2, 2), &wd);
    g_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in a = {0}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = htons(PortOf(g_me));
    if (bind(g_sock, (struct sockaddr*)&a, sizeof(a))) {
        /* a second process of the same "user" (the lobby browser next to a session relay; real Steam allows it):
           any port; it can send but receives no messages, which the lobby browser does not need */
        a.sin_port = 0;
        if (bind(g_sock, (struct sockaddr*)&a, sizeof(a))) { if (err) strcpy(err, "mock: message port busy"); return 1; }
    }
    u_long nb = 1; ioctlsocket(g_sock, FIONBIO, &nb);
    WriteUser(0);
    g_init = 1; if (err) err[0] = 0;
    return 0;
}
__declspec(dllexport) void API SteamAPI_Shutdown(void) { WriteUser(0); }
__declspec(dllexport) int32_t API SteamAPI_GetHSteamPipe(void) { return 1; }
__declspec(dllexport) void API SteamAPI_ManualDispatch_Init(void) {}
__declspec(dllexport) void API SteamAPI_ManualDispatch_RunFrame(int32_t p) { (void)p; Drain(); }
__declspec(dllexport) bool8 API SteamAPI_ManualDispatch_GetNextCallback(int32_t p, CallbackMsg_t* m) {
    (void)p; if (!g_ncb) return 0;
    g_cur = g_cbs[0]; memmove(g_cbs, g_cbs + 1, (--g_ncb) * sizeof(Cb)); g_haveCur = 1;
    m->hSteamUser = 1; m->callback = g_cur.cb; m->param = g_cur.param; m->param_size = g_cur.size; return 1;
}
__declspec(dllexport) void API SteamAPI_ManualDispatch_FreeLastCallback(int32_t p) { (void)p; if (g_haveCur) { free(g_cur.param); g_haveCur = 0; } }
__declspec(dllexport) bool8 API SteamAPI_ManualDispatch_GetAPICallResult(int32_t p, uint64_t call, void* out, int size, int cb, bool8* failed) {
    (void)p; (void)cb;
    for (int i = 0; i < g_nres; i++) if (g_results[i].call == call) { memcpy(out, g_results[i].data, size < g_results[i].size ? size : g_results[i].size); *failed = 0; g_results[i] = g_results[--g_nres]; return 1; }
    *failed = 1; return 0;
}

/* ---------------------------------------------------------------- user / friends */
__declspec(dllexport) void* API SteamAPI_SteamUser_v023(void) { return (void*)1; }
__declspec(dllexport) uint64_t API SteamAPI_ISteamUser_GetSteamID(void* s) { (void)s; return g_me; }
__declspec(dllexport) void* API SteamAPI_SteamFriends_v018(void) { return (void*)2; }
__declspec(dllexport) const char* API SteamAPI_ISteamFriends_GetPersonaName(void* s) { (void)s; return g_name; }
__declspec(dllexport) bool8 API SteamAPI_ISteamFriends_SetRichPresence(void* s, const char* k, const char* v) { (void)s; (void)k; (void)v; return 1; }
__declspec(dllexport) void API SteamAPI_ISteamFriends_ClearRichPresence(void* s) { (void)s; }
__declspec(dllexport) void API SteamAPI_ISteamFriends_ActivateGameOverlayInviteDialog(void* s, uint64_t l) { (void)s; (void)l; }
static int ListUsers(uint64_t* out, int max) {
    char pat[MAX_PATH]; sprintf(pat, "%s\\users\\*.txt", g_root); WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pat, &fd); int n = 0;
    if (h == INVALID_HANDLE_VALUE) return 0;
    do { uint64_t id = _strtoui64(fd.cFileName, NULL, 10); if (id && id != g_me && n < max) out[n++] = id; } while (FindNextFileA(h, &fd));
    FindClose(h); return n;
}
__declspec(dllexport) int API SteamAPI_ISteamFriends_GetFriendCount(void* s, int f) { (void)s; (void)f; uint64_t ids[64]; return ListUsers(ids, 64); }
__declspec(dllexport) uint64_t API SteamAPI_ISteamFriends_GetFriendByIndex(void* s, int i, int f) { (void)s; (void)f; uint64_t ids[64]; int n = ListUsers(ids, 64); return i < n ? ids[i] : 0; }
__declspec(dllexport) bool8 API SteamAPI_ISteamFriends_GetFriendGamePlayed(void* s, uint64_t id, FriendGameInfo_t* gi) {
    (void)s; char p[MAX_PATH]; UserPath(id, p); char lines[8][160]; int n = ReadLines(p, lines, 8);
    for (int i = 0; i < n; i++) if (!strncmp(lines[i], "lobby=", 6)) { uint64_t l = _strtoui64(lines[i] + 6, NULL, 10); if (!l) return 0; memset(gi, 0, sizeof(*gi)); gi->gameID = 480; gi->lobby = l; return 1; }
    return 0;
}
__declspec(dllexport) const char* API SteamAPI_ISteamFriends_GetFriendPersonaName(void* s, uint64_t id) {
    (void)s; static char name[64]; char p[MAX_PATH]; UserPath(id, p); char lines[8][160]; int n = ReadLines(p, lines, 8);
    strcpy(name, "?"); for (int i = 0; i < n; i++) if (!strncmp(lines[i], "name=", 5)) strncpy(name, lines[i] + 5, 63);
    return name;
}

/* ---------------------------------------------------------------- matchmaking (lobby file: owner=, member=..., key=value) */
__declspec(dllexport) void* API SteamAPI_SteamMatchmaking_v009(void) { return (void*)3; }
__declspec(dllexport) uint64_t API SteamAPI_ISteamMatchmaking_CreateLobby(void* s, int type, int max) {
    (void)s; (void)type; (void)max;
    uint64_t lobby = 109775240000000000ull + (g_me % 100000) * 10 + (GetTickCount() % 10);
    char p[MAX_PATH]; LobbyPath(lobby, p); char lines[3][160]; sprintf(lines[0], "owner=%llu", g_me); sprintf(lines[1], "member=%llu", g_me);
    sprintf(lines[2], "pid=%lu", GetCurrentProcessId()); WriteLines(p, lines, 3);   /* a killed owner leaves no stale lobby (list) */
    WriteUser(lobby);
    LobbyCreated_t r = {1, lobby}; return PushResult(513, &r, sizeof(r));
}
__declspec(dllexport) uint64_t API SteamAPI_ISteamMatchmaking_JoinLobby(void* s, uint64_t lobby) {
    (void)s; char p[MAX_PATH]; LobbyPath(lobby, p); char lines[64][160]; int n = ReadLines(p, lines, 63);
    LobbyEnter_t r; memset(&r, 0, sizeof(r)); r.lobby = lobby; r.response = n < 0 ? 2 : 1;
    if (n >= 0) { sprintf(lines[n++], "member=%llu", g_me); WriteLines(p, lines, n); }
    return PushResult(504, &r, sizeof(r));
}
__declspec(dllexport) void API SteamAPI_ISteamMatchmaking_LeaveLobby(void* s, uint64_t lobby) {
    (void)s; char p[MAX_PATH]; LobbyPath(lobby, p); char lines[64][160]; int n = ReadLines(p, lines, 64), m = 0; char me[64]; sprintf(me, "member=%llu", g_me); char own[64]; sprintf(own, "owner=%llu", g_me);
    for (int i = 0; i < n; i++) { if (!strcmp(lines[i], own)) { DeleteFileA(p); WriteUser(0); return; } if (strcmp(lines[i], me)) strcpy(lines[m++], lines[i]); }
    if (n >= 0) WriteLines(p, lines, m);
}
__declspec(dllexport) bool8 API SteamAPI_ISteamMatchmaking_SetLobbyData(void* s, uint64_t lobby, const char* k, const char* v) {
    (void)s; char p[MAX_PATH]; LobbyPath(lobby, p); char lines[64][160]; int n = ReadLines(p, lines, 63), found = 0; if (n < 0) return 0;
    char key[80]; sprintf(key, "%s=", k);
    for (int i = 0; i < n; i++) if (!strncmp(lines[i], key, strlen(key)) && strncmp(lines[i], "member=", 7) && strncmp(lines[i], "owner=", 6)) { sprintf(lines[i], "%s=%s", k, v); found = 1; }
    if (!found) sprintf(lines[n++], "%s=%s", k, v);
    WriteLines(p, lines, n); return 1;
}
__declspec(dllexport) const char* API SteamAPI_ISteamMatchmaking_GetLobbyData(void* s, uint64_t lobby, const char* k) {
    (void)s; static char val[160]; char p[MAX_PATH]; LobbyPath(lobby, p); char lines[64][160]; int n = ReadLines(p, lines, 64); char key[80]; sprintf(key, "%s=", k);
    val[0] = 0; for (int i = 0; i < n; i++) if (!strncmp(lines[i], key, strlen(key))) strcpy(val, lines[i] + strlen(key));
    return val;
}
static int Members(uint64_t lobby, uint64_t* out, uint64_t* owner) {
    char p[MAX_PATH]; LobbyPath(lobby, p); char lines[64][160]; int n = ReadLines(p, lines, 64), m = 0;
    for (int i = 0; i < n; i++) { if (!strncmp(lines[i], "member=", 7) && m < 32) out[m++] = _strtoui64(lines[i] + 7, NULL, 10); if (owner && !strncmp(lines[i], "owner=", 6)) *owner = _strtoui64(lines[i] + 6, NULL, 10); }
    return m;
}
__declspec(dllexport) uint64_t API SteamAPI_ISteamMatchmaking_GetLobbyOwner(void* s, uint64_t lobby) { (void)s; uint64_t ids[32], o = 0; Members(lobby, ids, &o); return o; }
__declspec(dllexport) int API SteamAPI_ISteamMatchmaking_GetNumLobbyMembers(void* s, uint64_t lobby) { (void)s; uint64_t ids[32]; return Members(lobby, ids, NULL); }
__declspec(dllexport) uint64_t API SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex(void* s, uint64_t lobby, int i) { (void)s; uint64_t ids[32]; int n = Members(lobby, ids, NULL); return i < n ? ids[i] : 0; }

/* ---------------------------------------------------------------- networking messages (identity: SteamID at offset 0) */
__declspec(dllexport) void API SteamAPI_SteamNetworkingIdentity_SetSteamID64(uint8_t* id, uint64_t sid) { memset(id, 0, 136); memcpy(id, &sid, 8); }
__declspec(dllexport) uint64_t API SteamAPI_SteamNetworkingIdentity_GetSteamID64(uint8_t* id) { uint64_t sid; memcpy(&sid, id, 8); return sid; }
__declspec(dllexport) void* API SteamAPI_SteamNetworkingMessages_SteamAPI_v002(void) { return (void*)4; }
__declspec(dllexport) int API SteamAPI_ISteamNetworkingMessages_SendMessageToUser(void* s, const uint8_t* id, const void* d, uint32_t n, int flags, int ch) {
    (void)s; (void)flags; if (n > 4000) return 2;
    uint64_t to; memcpy(&to, id, 8); char buf[4096 + 9]; memcpy(buf, &g_me, 8); buf[8] = (char)ch; memcpy(buf + 9, d, n);
    struct sockaddr_in a = {0}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = htons(PortOf(to));
    sendto(g_sock, buf, n + 9, 0, (struct sockaddr*)&a, sizeof(a)); return 1;
}
__declspec(dllexport) int API SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel(void* s, int ch, void** out, int max) {
    (void)s; Drain(); int k = 0;
    for (int i = 0; i < g_nmsg && k < max;) {
        if (g_msgs[i].ch == ch && Accepted(g_msgs[i].from)) {
            uint8_t* m = (uint8_t*)calloc(1, 160); *(char**)m = g_msgs[i].data; *(int*)(m + 4) = g_msgs[i].size; memcpy(m + 16, &g_msgs[i].from, 8);
            out[k++] = m; memmove(&g_msgs[i], &g_msgs[i + 1], (--g_nmsg - i) * sizeof(Msg));   /* keep arrival order */
        } else i++;
    }
    return k;
}
__declspec(dllexport) bool8 API SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser(void* s, const uint8_t* id) { (void)s; uint64_t sid; memcpy(&sid, id, 8); if (!Accepted(sid) && g_nacc < 64) g_accepted[g_nacc++] = sid; return 1; }
__declspec(dllexport) bool8 API SteamAPI_ISteamNetworkingMessages_CloseSessionWithUser(void* s, const uint8_t* id) { (void)s; (void)id; return 1; }
__declspec(dllexport) void API SteamAPI_SteamNetworkingMessage_t_Release(uint8_t* m) { free(*(char**)m); free(m); }
__declspec(dllexport) void* API SteamAPI_SteamNetworkingUtils_SteamAPI_v004(void) { return (void*)5; }
__declspec(dllexport) bool8 API SteamAPI_ISteamNetworkingUtils_SetGlobalConfigValueInt32(void* s, int e, int32_t v) { (void)s; (void)e; (void)v; return 1; }
__declspec(dllexport) void API SteamAPI_ISteamNetworkingUtils_InitRelayNetworkAccess(void* s) { (void)s; }

/* ---------------------------------------------------------------- lobby search (RequestLobbyList -> LobbyMatchList 510) */
static char g_filtKey[64], g_filtVal[64]; static uint64_t g_found[64]; static int g_nfound;
__declspec(dllexport) void API SteamAPI_ISteamMatchmaking_AddRequestLobbyListStringFilter(void* s, const char* k, const char* v, int cmp) { (void)s; (void)cmp; strncpy(g_filtKey, k, 63); strncpy(g_filtVal, v, 63); }
__declspec(dllexport) void API SteamAPI_ISteamMatchmaking_AddRequestLobbyListDistanceFilter(void* s, int d) { (void)s; (void)d; }
__declspec(dllexport) void API SteamAPI_ISteamMatchmaking_AddRequestLobbyListResultCountFilter(void* s, int n) { (void)s; (void)n; }
__declspec(dllexport) uint64_t API SteamAPI_ISteamMatchmaking_RequestLobbyList(void* s) {
    char pat[MAX_PATH]; sprintf(pat, "%s\\lobbies\\*.txt", g_root); WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pat, &fd);
    g_nfound = 0;
    if (h != INVALID_HANDLE_VALUE) {
        do {
            uint64_t l = _strtoui64(fd.cFileName, NULL, 10); if (!l || g_nfound >= 64) continue;
            DWORD pid = (DWORD)strtoul(SteamAPI_ISteamMatchmaking_GetLobbyData(s, l, "pid"), NULL, 10);
            if (pid) { HANDLE ph = OpenProcess(SYNCHRONIZE, FALSE, pid); DWORD w = ph ? WaitForSingleObject(ph, 0) : WAIT_OBJECT_0; if (ph) CloseHandle(ph);
                       if (w != WAIT_TIMEOUT) { char lp[MAX_PATH]; LobbyPath(l, lp); DeleteFileA(lp); continue; } }   /* owner process gone */
            if (g_filtKey[0] && strcmp(SteamAPI_ISteamMatchmaking_GetLobbyData(s, l, g_filtKey), g_filtVal)) continue;
            g_found[g_nfound++] = l;
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    g_filtKey[0] = 0;
    uint32_t count = (uint32_t)g_nfound; return PushResult(510, &count, sizeof(count));
}
__declspec(dllexport) uint64_t API SteamAPI_ISteamMatchmaking_GetLobbyByIndex(void* s, int i) { (void)s; return i >= 0 && i < g_nfound ? g_found[i] : 0; }
