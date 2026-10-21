// steamnet.inc (2026-10-21): Escalation through the game's own menus over Steam (Spacewar 480).
// 1) Relay client: the DLL starts Binaries\foc_relay\FoCRelay.exe svc once and talks to it over a local UDP control
//    socket (relay_svc.status "svcport="). The relay is a pipe: P2P messages to any Steam ID, Steam lobby, friends and
//    invite commands, and the game traffic tunnel (relay/focrelay.c, svc mode).
// 2) SteamChat: the subsystem's OnlineChatInterface (ghidra vtable 0x1511fc0), which the party channel
//    (ReservationBeaconConnectionDemonware) is built on, is replaced by channels kept here. A channel belongs to the
//    player who created it; its id is the owner's Steam ID with the top byte 0x80|seq (owner = top byte 0x01, real
//    and mock Steam IDs both start with 0x01). Members talk to the owner; the owner forwards channel messages.
//    Messages (relay 'M' payloads): 'S' ch = subscribe, 'A' ch ok members... = subscribe answer, 'X' ch = leave,
//    'J' ch id / 'Q' ch id = member joined / left, 'C' ch from data = channel message, 'W' ch from data = whisper.
// 3) The local player's UniqueNetId is the Steam ID of the relay (PlayerInterface slot 22 GetUniquePlayerId), so
//    chat members, registrants and PRI ids all match.
// Listeners are the game's 16-byte records {object, function name, native this, native handler}, called on the
// game thread (thiscall on record[2]), see ESCALATION-MENU.md.
static SOCKET g_sn = INVALID_SOCKET; static int g_snPort = 0;
static unsigned long long g_snMe = 0; static char g_snPersona[64] = "";
static DWORD g_snStartAt = 0, g_snPingAt = 0, g_snStatusAt = 0;

static void SnStatusRead() {
    wchar_t dir[MAX_PATH]; RelayDir(dir); wchar_t p[MAX_PATH]; _snwprintf_s(p, _TRUNCATE, L"%s\\relay_svc.status", dir);
    FILE* f = nullptr; _wfopen_s(&f, p, L"r"); if (!f) return;
    char line[200]; int port = 0; DWORD pid = 0;
    while (fgets(line, sizeof(line), f)) {
        if (!strncmp(line, "svcport=", 8)) port = atoi(line + 8);
        else if (!strncmp(line, "pid=", 4)) pid = strtoul(line + 4, nullptr, 10);
    }
    fclose(f);
    if (port && pid) {   // a status left by a relay that is gone does not count
        HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid); DWORD w = h ? WaitForSingleObject(h, 0) : WAIT_OBJECT_0; if (h) CloseHandle(h);
        if (w != WAIT_TIMEOUT) port = 0;
    }
    if (port != g_snPort) { Log("steamnet: relay control port %d", port); g_snPort = port; g_snMe = 0; }
}
static void SnStart() {
    wchar_t dir[MAX_PATH]; RelayDir(dir);
    wchar_t p[MAX_PATH]; _snwprintf_s(p, _TRUNCATE, L"%s\\relay_svc.status", dir); DeleteFileW(p);
    _snwprintf_s(p, _TRUNCATE, L"%s\\relay_svc.cmd", dir); DeleteFileW(p);
    wchar_t cmd[MAX_PATH + 40]; _snwprintf_s(cmd, _TRUNCATE, L"\"%s\\FoCRelay.exe\" svc", dir);
    STARTUPINFOW si = {sizeof(si)}; PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, dir, &si, &pi)) {
        Log("steamnet: cannot start %ls (%lu)", cmd, GetLastError()); return;
    }
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess); Log("steamnet: started %ls", cmd);
}
static void SnSendRaw(const void* d, int n) {
    if (g_sn == INVALID_SOCKET || !g_snPort) return;
    sockaddr_in a = {}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = htons((u_short)g_snPort);
    sendto(g_sn, (const char*)d, n, 0, (sockaddr*)&a, sizeof(a));
}
static void SnCmd(const char* text) { char b[600]; b[0] = 'L'; strncpy_s(b + 1, sizeof(b) - 1, text, _TRUNCATE); SnSendRaw(b, 1 + (int)strlen(b + 1)); }
static void SnSendTo(unsigned long long to, const void* d, int n, bool reliable) {
    if (n < 0 || n > 1380) { Log("steamnet: message of %d bytes dropped", n); return; }
    char b[1400]; b[0] = 'P'; b[1] = reliable ? 1 : 0; memcpy(b + 2, &to, 8); memcpy(b + 10, d, n); SnSendRaw(b, 10 + n);
}

// ---------------------------------------------------------------- SteamChat listeners
struct ChatListener { DWORD w[4]; };
enum { CL_CREATE, CL_SUBSCRIBE, CL_UNSUBSCRIBE, CL_SENDDONE, CL_CHANGE, CL_MESSAGE, CL_KINDS };
static ChatListener g_chatL[CL_KINDS][16]; static int g_chatN[CL_KINDS];
static void ChatAdd(int k, const DWORD* d) {
    for (int i = 0; i < g_chatN[k]; i++) if (!memcmp(g_chatL[k][i].w, d, 16)) return;
    if (g_chatN[k] < 16) memcpy(g_chatL[k][g_chatN[k]++].w, d, 16);
}
static void ChatClear(int k, const DWORD* d) {
    for (int i = 0; i < g_chatN[k]; i++) if (!memcmp(g_chatL[k][i].w, d, 16)) { g_chatL[k][i] = g_chatL[k][--g_chatN[k]]; return; }
}
#define CHAT_ADDCLEAR(K) \
    static void __fastcall ChatAdd##K(void*, void*, const DWORD* d) { ChatAdd(K, d); } \
    static void __fastcall ChatClear##K(void*, void*, const DWORD* d) { ChatClear(K, d); }
CHAT_ADDCLEAR(CL_CREATE) CHAT_ADDCLEAR(CL_SUBSCRIBE) CHAT_ADDCLEAR(CL_UNSUBSCRIBE) CHAT_ADDCLEAR(CL_SENDDONE)
CHAT_ADDCLEAR(CL_CHANGE) CHAT_ADDCLEAR(CL_MESSAGE)
typedef void(__thiscall* ChatOkFn)(DWORD self, DWORD ok, const DWORD* channel);
typedef void(__thiscall* ChatChangeFn)(DWORD self, const DWORD* channel, DWORD memberLo, DWORD memberHi, BYTE joined);
typedef void(__thiscall* ChatMsgFn)(DWORD self, const DWORD* channel, const DWORD* sender, DWORD unk, const void* data, int size);
// handlers may add or clear listeners while we call them: work on a copy
static int ChatCopy(int k, ChatListener* out) { int n = g_chatN[k]; memcpy(out, g_chatL[k], sizeof(ChatListener) * n); return n; }
static void ChatFireOk(int k, bool ok, unsigned long long ch) {
    DWORD id[2] = { (DWORD)ch, (DWORD)(ch >> 32) }; ChatListener l[16]; int n = ChatCopy(k, l);
    for (int i = 0; i < n; i++) {
        __try { ((ChatOkFn)(UINT_PTR)l[i].w[3])(l[i].w[2], ok ? 1 : 0, id); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("steamchat: listener %d fault", k); }
    }
}
static void ChatFireChange(unsigned long long ch, unsigned long long member, bool joined) {
    DWORD id[2] = { (DWORD)ch, (DWORD)(ch >> 32) }; ChatListener l[16]; int n = ChatCopy(CL_CHANGE, l);
    Log("steamchat: %016llx member %llu %s", ch, member, joined ? "joined" : "left");
    for (int i = 0; i < n; i++) {
        __try { ((ChatChangeFn)(UINT_PTR)l[i].w[3])(l[i].w[2], id, (DWORD)member, (DWORD)(member >> 32), joined ? 1 : 0); }
        __except (EXCEPTION_EXECUTE_HANDLER) { Log("steamchat: change listener fault"); }
    }
}
static void ChatFireMsg(unsigned long long ch, unsigned long long from, const void* data, int size) {
    DWORD id[2] = { (DWORD)ch, (DWORD)(ch >> 32) }, sid[2] = { (DWORD)from, (DWORD)(from >> 32) };
    ChatListener l[16]; int n = ChatCopy(CL_MESSAGE, l);
    for (int i = 0; i < n; i++) {
        __try { ((ChatMsgFn)(UINT_PTR)l[i].w[3])(l[i].w[2], id, sid, 0, data, size); }
        __except (EXCEPTION_EXECUTE_HANDLER) { Log("steamchat: message listener fault"); }
    }
}

// ---------------------------------------------------------------- SteamChat channels
// id = the channel on the wire; local = the id the game uses for it (the same, except a game channel that reached a
// member as 0: Demonware carried the game session's channel in its own session data, we do not; the owner announces
// its newest channel to its members ('N' old new) and a subscribe to 0 goes there).
struct SnChan { unsigned long long id, local, owner, mem[16]; int n; int state; DWORD asked; };   // state 1 subscribing, 2 in
static unsigned long long g_announcedCh = 0;   // newest channel announced by the owner of a channel we are in
static SnChan g_chans[8]; static int g_nchans; static unsigned g_chanSeq = 0;
static int g_chatPendingCreate = 0; static DWORD g_chatCreateAt = 0;
struct ChatEvt { int kind; bool ok; unsigned long long ch; };   // completions fired from the tick, not inside the call
static ChatEvt g_chatEvts[16]; static int g_nChatEvts;
static unsigned long long ChanOwner(unsigned long long ch) { return (ch & 0x00ffffffffffffffull) | 0x0100000000000000ull; }
static SnChan* ChanFind(unsigned long long ch) { for (int i = 0; i < g_nchans; i++) if (g_chans[i].id == ch) return &g_chans[i]; return nullptr; }
static SnChan* ChanFindLocal(unsigned long long ch) { for (int i = 0; i < g_nchans; i++) if (g_chans[i].local == ch) return &g_chans[i]; return nullptr; }
static SnChan* ChanAdd(unsigned long long ch) {
    SnChan* c = ChanFind(ch); if (c) return c;
    if (g_nchans >= 8) { Log("steamchat: too many channels"); return nullptr; }
    c = &g_chans[g_nchans++]; memset(c, 0, sizeof(*c)); c->id = ch; c->local = ch; c->owner = ChanOwner(ch); return c;
}
static void ChanRemove(unsigned long long ch) { for (int i = 0; i < g_nchans; i++) if (g_chans[i].id == ch) { g_chans[i] = g_chans[--g_nchans]; return; } }
static bool ChanHas(SnChan* c, unsigned long long m) { for (int i = 0; i < c->n; i++) if (c->mem[i] == m) return true; return false; }
static void ChanAddMember(SnChan* c, unsigned long long m) { if (!ChanHas(c, m) && c->n < 16) c->mem[c->n++] = m; }
static void ChanDelMember(SnChan* c, unsigned long long m) { for (int i = 0; i < c->n; i++) if (c->mem[i] == m) { c->mem[i] = c->mem[--c->n]; return; } }
static void ChatQueue(int kind, bool ok, unsigned long long ch) { if (g_nChatEvts < 16) g_chatEvts[g_nChatEvts++] = { kind, ok, ch }; }
static void SnPkt(unsigned long long to, char type, unsigned long long ch, const unsigned long long* extra, int nextra, const void* data, int size) {
    char b[1400]; int k = 0; b[k++] = type; memcpy(b + k, &ch, 8); k += 8;
    for (int i = 0; i < nextra; i++) { memcpy(b + k, &extra[i], 8); k += 8; }
    if (size > 0) { if (k + size > (int)sizeof(b)) return; memcpy(b + k, data, size); k += size; }
    SnSendTo(to, b, k, true);
}

static DWORD __fastcall ChatCreate(void*, void*) { g_chatPendingCreate = 1; g_chatCreateAt = GetTickCount(); Log("steamchat: CreateChatChannel"); return 1; }
static DWORD __fastcall ChatSubscribe(void*, void*, const DWORD* id) {
    unsigned long long local = id ? ((unsigned long long)id[1] << 32 | id[0]) : 0, ch = local;
    if (!ch && g_announcedCh) { ch = g_announcedCh; Log("steamchat: subscribe to channel 0 -> announced %016llx", ch); }
    SnChan* c = ChanAdd(ch); if (!c) { ChatQueue(CL_SUBSCRIBE, false, local); return 1; }
    c->local = local;
    Log("steamchat: SubscribeToChatChannel %016llx (owner %llu)", ch, c->owner);
    if (c->owner == g_snMe) { c->state = 2; ChanAddMember(c, g_snMe); ChatQueue(CL_SUBSCRIBE, true, local); }
    else { c->state = 1; c->asked = GetTickCount(); SnPkt(c->owner, 'S', ch, nullptr, 0, nullptr, 0); }
    return 1;
}
static DWORD __fastcall ChatUnsubscribe(void*, void*, const DWORD* id) {
    unsigned long long ch = id ? ((unsigned long long)id[1] << 32 | id[0]) : 0;
    Log("steamchat: UnsubscribeFromChatChannel %016llx", ch);
    if (SnChan* c = ChanFindLocal(ch)) {
        unsigned long long w = c->id;
        if (c->owner == g_snMe) { for (int i = 0; i < c->n; i++) if (c->mem[i] != g_snMe) SnPkt(c->mem[i], 'Q', w, &g_snMe, 1, nullptr, 0); }
        else SnPkt(c->owner, 'X', w, nullptr, 0, nullptr, 0);
        ChanRemove(w);
    }
    ChatQueue(CL_UNSUBSCRIBE, true, ch);
    return 1;
}
static LONG g_chatLogN = 0;   // first channel messages are logged (type, size, first bytes)
static void ChatLogMsg(const char* what, unsigned long long ch, unsigned long long peer, const void* data, int size) {
    if (InterlockedIncrement(&g_chatLogN) > 200) return;
    char hex[64] = ""; const BYTE* b = (const BYTE*)data;
    for (int i = 0; i < size && i < 16; i++) _snprintf_s(hex + 2 * i, sizeof(hex) - 2 * i, _TRUNCATE, "%02x", b[i]);
    Log("steamchat: %s %016llx %llu %d bytes %s", what, ch, peer, size, hex);
}
static DWORD __fastcall ChatSend(void*, void*, const DWORD* chp, const void* data, int size) {
    unsigned long long ch = chp ? ((unsigned long long)chp[1] << 32 | chp[0]) : 0;
    ChatLogMsg("send", ch, 0, data, size);
    // beacon message 0x17 = "migrate to the best host": the party host named a member as the better host and that
    // member left with a fresh party of its own (2026-10-21). The host stays the host: not forwarded. (Clearing the
    // AllowLobby*HostMigration settings flags instead crashed both lobbies on entry.)
    if (size >= 1 && ((const BYTE*)data)[0] == 0x17) { Log("steamchat: host migration message not sent"); return 1; }
    SnChan* c = ChanFindLocal(ch); if (!c || c->state != 2) return 0;
    if (c->owner == g_snMe) { for (int i = 0; i < c->n; i++) if (c->mem[i] != g_snMe) SnPkt(c->mem[i], 'C', c->id, &g_snMe, 1, data, size); }
    else SnPkt(c->owner, 'C', c->id, &g_snMe, 1, data, size);
    return 1;
}
static DWORD __fastcall ChatWhisper(void*, void*, const DWORD* chp, const DWORD* user, const void* data, int size) {
    unsigned long long ch = chp ? ((unsigned long long)chp[1] << 32 | chp[0]) : 0, to = user ? ((unsigned long long)user[1] << 32 | user[0]) : 0;
    if (!to || to == g_snMe) return 0;
    if (SnChan* c = ChanFindLocal(ch)) ch = c->id;
    ChatLogMsg("whisper to", ch, to, data, size);
    SnPkt(to, 'W', ch, &g_snMe, 1, data, size);
    return 1;
}
static const VtPatch kChatPatches[] = {
    { "Chat.CreateChatChannel",      0x1511fc0,  1, 0xc4ff70, (void*)ChatCreate },
    { "Chat.AddChatCreate",          0x1511fc0,  2, 0x6bc780, (void*)ChatAddCL_CREATE },
    { "Chat.ClearChatCreate",        0x1511fc0,  3, 0x64b700, (void*)ChatClearCL_CREATE },
    { "Chat.Subscribe",              0x1511fc0,  4, 0xd14fc0, (void*)ChatSubscribe },
    { "Chat.AddChatSubscribe",       0x1511fc0,  5, 0xaf6420, (void*)ChatAddCL_SUBSCRIBE },
    { "Chat.ClearChatSubscribe",     0x1511fc0,  6, 0x7d0aa0, (void*)ChatClearCL_SUBSCRIBE },
    { "Chat.Unsubscribe",            0x1511fc0,  7, 0x4d46a0, (void*)ChatUnsubscribe },
    { "Chat.AddChatUnsubscribe",     0x1511fc0,  8, 0x42c310, (void*)ChatAddCL_UNSUBSCRIBE },
    { "Chat.ClearChatUnsubscribe",   0x1511fc0,  9, 0xc3c500, (void*)ChatClearCL_UNSUBSCRIBE },
    { "Chat.SendChatMessage",        0x1511fc0, 10, 0xbd42d0, (void*)ChatSend },
    { "Chat.SendChatWhisperMessage", 0x1511fc0, 11, 0xc79ba0, (void*)ChatWhisper },
    { "Chat.AddChatSendComplete",    0x1511fc0, 12, 0x694ba0, (void*)ChatAddCL_SENDDONE },
    { "Chat.ClearChatSendComplete",  0x1511fc0, 13, 0xb73760, (void*)ChatClearCL_SENDDONE },
    { "Chat.AddChatChange",          0x1511fc0, 14, 0xc80c90, (void*)ChatAddCL_CHANGE },
    { "Chat.ClearChatChange",        0x1511fc0, 15, 0xa60760, (void*)ChatClearCL_CHANGE },
    { "Chat.AddChatMessage",         0x1511fc0, 16, 0xbad600, (void*)ChatAddCL_MESSAGE },
    { "Chat.ClearChatMessage",       0x1511fc0, 17, 0xb37890, (void*)ChatClearCL_MESSAGE },
};
const VtPatch* const g_chatPatchList = kChatPatches; const size_t g_chatPatchCount = sizeof(kChatPatches) / sizeof(kChatPatches[0]);

// PlayerInterface slot 22 GetUniquePlayerId(byte LocalUserNum, out UniqueNetId) -> the relay's Steam ID
typedef DWORD(__thiscall* GetUidFn)(void* self, DWORD num, DWORD* out);
static GetUidFn g_origGetUid = nullptr;
static DWORD __fastcall SnGetUid(void* self, void*, DWORD num, DWORD* out) {
    if (g_snMe && out && g_onlineCtx) { out[0] = (DWORD)g_snMe; out[1] = (DWORD)(g_snMe >> 32); return 1; }
    return g_origGetUid ? g_origGetUid(self, num, out) : 0;
}
// PlayerInterface slot 23 GetPlayerNickname(byte) -> FString by value (thiscall(FString* ret, byte num), returns ret):
// the name members see in the lobby lists (registrant NickName). Outside the story: ini playername (test copies) or
// the relay's Steam persona; else the original (Demonware: the game's Steam name, the same in every test copy).
typedef FStr*(__thiscall* NickFn)(void* self, FStr* ret, DWORD num);
static NickFn g_origNick = nullptr;
static FStr* __fastcall SnNickname(void* self, void*, FStr* ret, DWORD num) {
    const char* want = g_pendingPlayerName[0] ? g_pendingPlayerName : g_snPersona;
    if (!g_onlineCtx || !want[0] || !ret) return g_origNick(self, ret, num);
    wchar_t w[64]; int n = MultiByteToWideChar(CP_UTF8, 0, want, -1, w, 64); if (n <= 0) return g_origNick(self, ret, num);
    ret->Data = nullptr; ret->Num = 0; ret->Max = 0;
    void* d = GameRealloc(nullptr, n * 2); if (!d) return g_origNick(self, ret, num);
    memcpy(d, w, n * 2); ret->Data = (wchar_t*)d; ret->Num = n; ret->Max = n;
    return ret;
}
static void SnInstallUid() {
    static bool done = false; if (done) return; done = true;
    void** slot = (void**)((BYTE*)GetModuleHandleW(nullptr) + (0x168ec38 - 0x400000)) + 22;
    if (ExeVa(*slot) != 0x7bfd40) { Log("steamnet: GetUniquePlayerId slot holds %lx - not patched", ExeVa(*slot)); return; }
    g_origGetUid = (GetUidFn)*slot;
    DWORD old = 0; VirtualProtect(slot, 4, PAGE_READWRITE, &old); *slot = (void*)SnGetUid; VirtualProtect(slot, 4, old, &old);
    Log("steamnet: Player.GetUniquePlayerId patched (Steam ID)");
    void** ns = (void**)((BYTE*)GetModuleHandleW(nullptr) + (0x168ec38 - 0x400000)) + 23;
    if (ExeVa(*ns) == 0x9101b0) {
        g_origNick = (NickFn)*ns;
        VirtualProtect(ns, 4, PAGE_READWRITE, &old); *ns = (void*)SnNickname; VirtualProtect(ns, 4, old, &old);
        Log("steamnet: Player.GetPlayerNickname patched");
    }
}

// ---------------------------------------------------------------- incoming
static void SnEvent(const char* e) {
    Log("steamnet: event %s", e);
    if (!strncmp(e, "hello ", 6)) {
        char name[64] = ""; unsigned long long me = 0;
        if (sscanf_s(e + 6, "%llu %63[^\n]", &me, name, (unsigned)sizeof(name)) >= 1) { g_snMe = me; strncpy_s(g_snPersona, name, _TRUNCATE); }
    }
}
static unsigned long long Rd64(const char* p) { unsigned long long v; memcpy(&v, p, 8); return v; }
// Travel to the match (2026-10-22): the member got the host's "travel to game" (0x0c) but the game never resolved the
// host address (the handler is native; Demonware session data). The DLL connects instead, over the relay: the host's
// relay forwards its game port ("gameport", the newest bind in 7777..7800 while hosting a TnSurvivalGame), the member's
// relay opens a local port ("connect <host> <port>") and the game opens 127.0.0.1:<port>.
static unsigned long long g_travelHost = 0; static DWORD g_travelAt = 0, g_resolvedAt = 0;
static void SnChatMsg(unsigned long long from, const char* d, int n) {
    if (n < 9) return;
    char t = d[0]; unsigned long long ch = Rd64(d + 1); SnChan* c = ChanFind(ch);
    switch (t) {
    case 'S': {   // subscribe request to the channel we own
        bool ok = c && c->owner == g_snMe && c->state == 2;
        unsigned long long reply[17]; int nr = 0;
        if (ok) { ChanAddMember(c, from); for (int i = 0; i < c->n && nr < 16; i++) reply[nr++] = c->mem[i]; }
        char b[1 + 8 + 1 + 8 * 16]; int k = 0; b[k++] = 'A'; memcpy(b + k, &ch, 8); k += 8; b[k++] = ok ? 1 : 0;
        memcpy(b + k, reply, 8 * nr); k += 8 * nr; SnSendTo(from, b, k, true);
        Log("steamchat: %llu subscribes to %016llx: %s", from, ch, ok ? "ok" : "refused");
        if (ok) { for (int i = 0; i < c->n; i++) if (c->mem[i] != g_snMe && c->mem[i] != from) SnPkt(c->mem[i], 'J', ch, &from, 1, nullptr, 0); ChatFireChange(c->local, from, true); }
        break; }
    case 'A': {
        if (!c || c->state != 1 || n < 10) break;
        bool ok = d[9] != 0; unsigned long long local = c->local;
        if (ok) { c->state = 2; c->n = 0; for (int k = 10; k + 8 <= n; k += 8) ChanAddMember(c, Rd64(d + k)); ChanAddMember(c, g_snMe); }
        else ChanRemove(ch);
        Log("steamchat: subscribed to %016llx: %s", ch, ok ? "ok" : "refused");
        ChatFireOk(CL_SUBSCRIBE, ok, local);
        if (ok && (c = ChanFind(ch)) != nullptr) { unsigned long long m[16]; int nm = c->n; memcpy(m, c->mem, 8 * nm); for (int i = 0; i < nm; i++) if (m[i] != g_snMe) ChatFireChange(local, m[i], true); }
        break; }
    case 'X':
        if (c && c->owner == g_snMe) { ChanDelMember(c, from); for (int i = 0; i < c->n; i++) if (c->mem[i] != g_snMe) SnPkt(c->mem[i], 'Q', ch, &from, 1, nullptr, 0); ChatFireChange(c->local, from, false); }
        break;
    case 'J': if (c && n >= 17) { unsigned long long m = Rd64(d + 9); ChanAddMember(c, m); ChatFireChange(c->local, m, true); } break;
    case 'Q': if (c && n >= 17) { unsigned long long m = Rd64(d + 9); ChanDelMember(c, m); ChatFireChange(c->local, m, false); } break;
    case 'N': if (n >= 17) { g_announcedCh = Rd64(d + 9); Log("steamchat: %llu announces channel %016llx", from, g_announcedCh); } break;
    case 'C':
        if (c && c->state == 2 && n >= 17) {
            unsigned long long src = Rd64(d + 9);
            if (c->owner == g_snMe) for (int i = 0; i < c->n; i++) if (c->mem[i] != g_snMe && c->mem[i] != src) SnPkt(c->mem[i], 'C', ch, &src, 1, d + 17, n - 17);
            ChatLogMsg("received", ch, src, d + 17, n - 17);
            if (n - 17 == 1 && (BYTE)d[17] == 0x0c && c->owner != g_snMe) { g_travelHost = c->owner; g_travelAt = GetTickCount(); Log("steamnet: host %llu says travel to game", c->owner); }
            ChatFireMsg(c->local, src, d + 17, n - 17);
        }
        break;
    case 'W': if (n >= 17) { ChatLogMsg("whisper from", ch, Rd64(d + 9), d + 17, n - 17); ChatFireMsg(c ? c->local : ch, Rd64(d + 9), d + 17, n - 17); } break;
    }
}
static void SnPoll() {
    char buf[1500]; sockaddr_in from; int fl = sizeof(from), n;
    while ((n = recvfrom(g_sn, buf, sizeof(buf) - 1, 0, (sockaddr*)&from, &fl)) > 0) {
        fl = sizeof(from);
        if (buf[0] == 'E') { buf[n] = 0; SnEvent(buf + 1); }
        else if (buf[0] == 'M' && n >= 9) SnChatMsg(Rd64(buf + 1), buf + 9, n - 9);
    }
}

// ---------------------------------------------------------------- tick (game thread, via TickEscLogin)
static void InstallInvitePatches();   // invite.inc
// The local player's PRI got its UniqueId at login in the front end, before the relay told us the Steam ID: 0. The
// party then registered members with id 0 (host's Party session empty, 2026-10-21). Outside the story every local
// PRI carries the Steam ID (PRI.UniqueId +0x230).
static void SnFixLocalIds(const TickObjs& t) {
    if (!g_snMe || !g_onlineCtx) return;
    for (int i = 0; i < t.npc; i++) {
        BYTE* pc = t.pc[i]; if (!IsObjFast(pc) || !IsLocalPC(pc)) continue;
        BYTE* pri = *(BYTE**)(pc + 0x1d4); if (!IsObjFast(pri) || !Rd(pri + 0x230, 8)) continue;
        unsigned long long* id = (unsigned long long*)(pri + 0x230);
        if (*id == g_snMe) continue;
        char nm[64] = ""; ObjNameStr(pri, nm, sizeof(nm));
        Log("steamnet: %s UniqueId %llu -> Steam ID %llu", nm, *id, g_snMe);
        *id = g_snMe;
    }
}
// Demonware's account service makes every UniqueNetId (registrants, session owner) from the Steam ID string it logged
// in with (ghidra 0x45e210, thiscall(byte num, out UniqueNetId*), RET 8; also called directly, not only through the
// vtable). Without the Demonware login that string is empty -> id 0, the party registered members with id 0 (host's
// Party session empty, 2026-10-21; writing the string did not stick). Inline hook: outside the story the relay's
// Steam ID, else the original (first 7 bytes push -1 / push imm32 moved to a trampoline).
typedef void(__thiscall* AccIdFn)(void* self, DWORD num, unsigned long long* out);
static AccIdFn g_accIdOrig = nullptr;
static void __fastcall SnAccountId(void* self, void*, DWORD num, unsigned long long* out) {
    if (g_snMe && g_onlineCtx && out) { *out = g_snMe; return; }
    g_accIdOrig(self, num, out);
}
static void SnHookAccountId() {
    static bool done = false; if (done) return; done = true;
    BYTE* f = (BYTE*)GetModuleHandleW(nullptr) + (0x45e210 - 0x400000);
    static const BYTE want[3] = { 0x6a, 0xff, 0x68 };
    if (memcmp(f, want, 3)) { Log("steamnet: account id function has unexpected bytes - not hooked"); return; }
    BYTE* tr = (BYTE*)VirtualAlloc(nullptr, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tr) return;
    memcpy(tr, f, 7); tr[7] = 0xe9; LONG rel = (LONG)((f + 7) - (tr + 12)); memcpy(tr + 8, &rel, 4);
    g_accIdOrig = (AccIdFn)tr;
    DWORD old = 0; VirtualProtect(f, 7, PAGE_EXECUTE_READWRITE, &old);
    f[0] = 0xe9; rel = (LONG)((BYTE*)SnAccountId - (f + 5)); memcpy(f + 1, &rel, 4); f[5] = 0x90; f[6] = 0x90;
    VirtualProtect(f, 7, old, &old); FlushInstructionCache(GetCurrentProcess(), f, 7);
    Log("steamnet: account id function hooked (Steam ID outside the story)");
}
static void TickSteamTravel(const TickObjs& t) {
    DWORD now = GetTickCount();
    // host: our relay forwards game traffic to the port the match listens on
    static LONG sentPort = 0;
    bool hosting = false;
    for (int g = 0; g < t.ngame; g++) { char cn[80] = ""; if (IsObjFast(t.game[g]) && ClassNameOf(t.game[g], cn, sizeof(cn)) && !strcmp(cn, "TnSurvivalGame")) hosting = true; }
    if (hosting && g_lastGameBind && g_lastGameBind != sentPort && g_snPort) {
        sentPort = g_lastGameBind; char c[32]; _snprintf_s(c, _TRUNCATE, "gameport %ld", sentPort); SnCmd(c);
        Log("steamnet: hosting the match on port %ld (relay forwards it)", sentPort);
    }
    // member: the host started the match and the game did not connect by itself
    if (g_travelHost && now - g_travelAt > 1500) {
        unsigned long long host = g_travelHost; g_travelHost = 0;
        if ((LONG)(g_resolvedAt - g_travelAt) >= 0) { Log("steamnet: the game resolved the host itself"); return; }
        int port = 7790 + (int)(GetCurrentProcessId() % 40);
        char c[64]; _snprintf_s(c, _TRUNCATE, "connect %llu %d", host, port); SnCmd(c);
        char o[96]; _snprintf_s(o, _TRUNCATE, "open 127.0.0.1:%d", port);
        Log("steamnet: joining the match of %llu through the relay (%s)", host, o);
        QueueCommand(o);
    }
}
static void TickSteamChat(const TickObjs& t) {
    if (!g_escOnline) return;
    if (g_onlineCtx) TickSteamTravel(t);
    SnInstallUid();
    InstallInvitePatches();
    SnFixLocalIds(t);
    SnHookAccountId();
    DWORD now = GetTickCount();
    if (g_sn == INVALID_SOCKET) {
        g_sn = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        sockaddr_in a = {}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = 0;
        bind(g_sn, (sockaddr*)&a, sizeof(a)); u_long nb = 1; ioctlsocket(g_sn, FIONBIO, &nb);
    }
    if ((LONG)(now - g_snStatusAt) >= 0) { g_snStatusAt = now + (g_snPort ? 5000 : 1000); SnStatusRead(); }
    if (!g_snPort && (!g_snStartAt || now - g_snStartAt > 20000)) { g_snStartAt = now; SnStart(); }
    if (g_snPort && !g_snMe && (LONG)(now - g_snPingAt) >= 0) { g_snPingAt = now + 1000; SnCmd("ping"); }
    if (g_snPort) SnPoll();
    // completions queued by the interface calls
    ChatEvt ev[16]; int ne = g_nChatEvts; memcpy(ev, g_chatEvts, sizeof(ChatEvt) * ne); g_nChatEvts = 0;
    for (int i = 0; i < ne; i++) ChatFireOk(ev[i].kind, ev[i].ok, ev[i].ch);
    if (g_chatPendingCreate && g_snMe) {
        g_chatPendingCreate = 0;
        unsigned long long ch = (g_snMe & 0x00ffffffffffffffull) | ((unsigned long long)(0x80 | (++g_chanSeq & 0x7f)) << 56);
        SnChan* c = ChanAdd(ch);
        if (c) { c->state = 2; c->n = 0; ChanAddMember(c, g_snMe); }
        Log("steamchat: channel %016llx created", ch);
        for (int i = 0; i < g_nchans; i++) {   // members of our other channels (the party) learn the new one
            SnChan* o = &g_chans[i]; if (o->id == ch || o->owner != g_snMe) continue;
            for (int k = 0; k < o->n; k++) if (o->mem[k] != g_snMe) SnPkt(o->mem[k], 'N', o->id, &ch, 1, nullptr, 0);
        }
        ChatFireOk(CL_CREATE, c != nullptr, ch);
    } else if (g_chatPendingCreate && now - g_chatCreateAt > 15000) {
        g_chatPendingCreate = 0; Log("steamchat: no Steam relay - channel not created"); ChatFireOk(CL_CREATE, false, 0);
    }
    for (int i = g_nchans - 1; i >= 0; i--)   // owner did not answer the subscribe
        if (g_chans[i].state == 1 && now - g_chans[i].asked > 10000) { unsigned long long ch = g_chans[i].id, local = g_chans[i].local; ChanRemove(ch); Log("steamchat: subscribe to %016llx timed out", ch); ChatFireOk(CL_SUBSCRIBE, false, local); }
}
