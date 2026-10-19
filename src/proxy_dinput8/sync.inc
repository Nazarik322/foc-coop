// Co-op sync channel (host -> guest). Included from dinput8.cpp after gametick.inc.
//
// Kismet runs only on the host, so everything it drives is host-local: the current objective, objective markers,
// chapter title, cutscenes, streaming. The host's DLL sends a small state snapshot to every guest four times a second
// and the guest's DLL applies it with memory writes. Transport: the game's own UDP socket. Snapshot datagrams start with
// the magic "FOCS1\n" and are taken out in recvfrom before the game sees them, so the channel works wherever the game
// connection works (LAN, VPN, the Steam relay). A snapshot holds the full state (no deltas), so a lost one is harmless.
//
// Snapshot: lines "key=value". v1 keys: seq, map, obj (host TnObjectiveManager.CurrentObjective, -1 = none).

static const char kSyncMagic[] = "FOCS1\n";
static CRITICAL_SECTION g_syncCs;
static SOCKET g_syncSock = INVALID_SOCKET;   // the game's UDP socket (last one used by sendto)
struct SyncPeer { sockaddr_in addr; DWORD seen; };
static SyncPeer g_syncPeers[8]; static int g_syncNPeers = 0;
static char g_syncRx[1200]; static volatile LONG g_syncRxSeq = 0;   // last snapshot received (guest)
static volatile LONG g_syncTx = 0, g_syncRxCount = 0;
static volatile DWORD g_guestReadyAt = 0;   // host: last "ready" datagram from a guest

static void SyncInit() { InitializeCriticalSection(&g_syncCs); }

// sendto hook: remember the game's socket
static void SyncNoteOut(SOCKET s) { if (g_syncOn) g_syncSock = s; }

// recvfrom hook: true = our snapshot (consumed, the game must not see it). Other datagrams register their sender as a
// peer (on the host these are the guests).
static bool SyncFilterIn(const char* buf, int len, const sockaddr* from) {
    if (!g_syncOn) return false;
    if (len >= 6 && !memcmp(buf, "FOCR1\n", 6)) { g_guestReadyAt = GetTickCount(); return true; }   // guest -> host "ready"
    if (len >= (int)sizeof(kSyncMagic) - 1 && !memcmp(buf, kSyncMagic, sizeof(kSyncMagic) - 1)) {
        int n = len < (int)sizeof(g_syncRx) - 1 ? len : (int)sizeof(g_syncRx) - 1;
        EnterCriticalSection(&g_syncCs); memcpy(g_syncRx, buf, n); g_syncRx[n] = 0; LeaveCriticalSection(&g_syncCs);
        InterlockedIncrement(&g_syncRxCount);
        return true;
    }
    if (from && from->sa_family == AF_INET) {
        const sockaddr_in* in = (const sockaddr_in*)from;
        EnterCriticalSection(&g_syncCs);
        int i = 0;
        for (; i < g_syncNPeers; i++) if (g_syncPeers[i].addr.sin_addr.s_addr == in->sin_addr.s_addr && g_syncPeers[i].addr.sin_port == in->sin_port) break;
        if (i == g_syncNPeers && g_syncNPeers < 8) { g_syncPeers[i].addr = *in; g_syncNPeers++; }
        if (i < 8) g_syncPeers[i].seen = GetTickCount();
        LeaveCriticalSection(&g_syncCs);
    }
    return false;
}

// ---------------------------------------------------------------- engine strings (FString) through the game allocator
// GMalloc (ghidra global 0x19117b8), vtable slot 3 = Realloc(ptr, size, alignment) (thiscall; called as fastcall with
// a dummy edx). Strings the game later frees or reassigns must come from this allocator.
typedef void* (__fastcall* PFN_GRealloc)(void* self, void* edx, void* p, DWORD size, DWORD align);
static void* GameRealloc(void* p, DWORD size) {
    void* gm = *(void**)((BYTE*)GetModuleHandleW(nullptr) + (0x19117b8 - 0x400000));
    if (!gm || !Rd(gm, 4)) return nullptr;
    PFN_GRealloc fn = (PFN_GRealloc)(*(void***)gm)[3];
    return fn(gm, nullptr, p, size, 8);
}
static bool SetFStr(BYTE* at, const wchar_t* s) {
    FStr* f = (FStr*)at;
    int n = s && *s ? (int)wcslen(s) + 1 : 0;
    if (n == 0) { f->Num = 0; return true; }
    if (f->Max < n) {
        void* d = GameRealloc(f->Data, n * 2);
        if (!d) return false;
        f->Data = (wchar_t*)d; f->Max = n;
    }
    memcpy(f->Data, s, n * 2); f->Num = n;
    return true;
}

// ---------------------------------------------------------------- objectives
// TnWorldInfo +0x5d0 _ObjectiveManager (TnObjectiveManager): +0x3c CurrentObjective, +0x40 LastCompletedObjective,
// +0x44 LastObjective, +0x48 ObjectiveBank (TArray<FString>), +0x54 DeferredAddObjective, +0x60 DeferredCompleteObjective.
// The HUD calls Manager.Update every frame, which shows DeferredAddObjective as NEW OBJECTIVE and clears it.
static BYTE* ObjectiveManager(const TickObjs& t) {
    for (int i = 0; i < t.nworld; i++) {
        BYTE* w = t.world[i];
        if (!Rd(w + 0x5d0, 4)) continue;
        BYTE* m = *(BYTE**)(w + 0x5d0);
        if (!IsObjFast(m) || !Rd(m + 0x70, 4)) continue;
        TArr* bank = (TArr*)(m + 0x48);
        if (bank->Num > 0 && bank->Num < 200) return m;
    }
    return nullptr;
}
static const wchar_t* BankStr(BYTE* m, int i) {
    TArr* bank = (TArr*)(m + 0x48);
    if (i < 0 || i >= bank->Num) return nullptr;
    FStr* f = (FStr*)((BYTE*)bank->Data + i * 12);
    return f->Num > 0 && f->Data ? f->Data : nullptr;
}
// the guest-side copy of TnObjectiveManager.ClientSetCurrentObjective
static void ApplyObjective(BYTE* m, int cur) {
    int* curObj = (int*)(m + 0x3c); int* lastDone = (int*)(m + 0x40); int* last = (int*)(m + 0x44);
    if (*curObj == cur) return;
    TArr* bank = (TArr*)(m + 0x48);
    if (cur >= bank->Num) return;
    const wchar_t* add = cur >= 0 ? BankStr(m, cur) : nullptr;
    const wchar_t* prev = *last >= 0 ? BankStr(m, *last) : nullptr;
    if (prev && (!add || wcscmp(prev, add) != 0)) SetFStr(m + 0x60, prev);
    SetFStr(m + 0x54, add);
    *lastDone = cur >= 0 ? cur - 1 : *last;
    *last = cur; *curObj = cur;
    char a8[200] = ""; if (add) WideCharToMultiByte(CP_UTF8, 0, add, -1, a8, sizeof(a8), nullptr, nullptr);
    Log("sync: objective -> %d '%s'", cur, a8);
}

// ---------------------------------------------------------------- objective markers
// TnObjectiveManager +0x28 LocalObjectiveMarkers: TArray<ObjectiveMarkerData>, 0x1c bytes each: +0 Description
// (FString), +0xc MarkerBase (Actor), +0x10 MarkerId, +0x14 MarkerTypeClass, +0x18 MarkerType (an instance of that
// class; the HUD only calls its functions). Host sends "mk=<base actor>,<type class>,<id>" per marker; level actors
// have the same names on both sides. The guest keeps its own markers (ids < 1000) and replaces the mirrored ones
// (id 1000 + host id).
static BYTE* FindObjByLabel(const char* label) {
    TArr* go = GObj();
    for (int i = 0; i < go->Num; i++) {
        BYTE* o = (BYTE*)go->Data[i];
        if (!o) continue;
        char l[96]; ObjLabel(o, l, sizeof(l));
        if (!strcmp(l, label)) return o;
    }
    return nullptr;
}
static BYTE* FindInstanceOfClass(const char* cls, BYTE** clsObj) {
    unsigned cid = ClassIdByName(cls);
    *clsObj = cid ? g_classObjById[cid] : nullptr;
    if (!cid) return nullptr;
    TArr* go = GObj(); BYTE* def = nullptr;
    for (int i = 0; i < go->Num; i++) {
        BYTE* o = (BYTE*)go->Data[i];
        if (!o || (*(unsigned*)(o + 0x14) & 0x1fff) != cid) continue;
        if (*(unsigned*)(o + 4) & 0x3fff) return o;   // a live instance
        def = o;                                        // Default__ object
    }
    return def;
}
static int HostMarkers(BYTE* m, char* out, size_t n) {
    TArr* a = (TArr*)(m + 0x28); int k = 0;
    if (a->Num < 0 || a->Num > 64) return 0;
    for (int i = 0; i < a->Num; i++) {
        BYTE* e = (BYTE*)a->Data + i * 0x1c;
        BYTE* base = *(BYTE**)(e + 0xc); BYTE* type = *(BYTE**)(e + 0x18);
        if (!IsObjFast(base) || !IsObjFast(type)) continue;
        char bl[96], tc[80]; ObjLabel(base, bl, sizeof(bl)); ClassNameOf(type, tc, sizeof(tc));
        if (!strcmp(tc, "TnObjectiveMarkerTypeTransformerCampaign")) continue;   // teammate markers: each side has its own
        k += _snprintf_s(out + k, n - k, _TRUNCATE, "mk=%s,%s,%d\n", bl, tc, *(int*)(e + 0x10));
    }
    return k;
}
static void ApplyMarkers(BYTE* m, const char* body) {
    static char applied[1000] = "\x01";
    char want[1000] = ""; size_t w = 0;
    for (const char* p = strstr(body, "\nmk="); p; p = strstr(p + 1, "\nmk=")) {
        const char* e = strchr(p + 1, '\n'); size_t L = e ? (size_t)(e - p) : strlen(p);
        if (w + L + 1 < sizeof(want)) { memcpy(want + w, p, L); w += L; want[w] = 0; }
    }
    if (!strcmp(want, applied)) return;
    strcpy_s(applied, want);
    TArr* a = (TArr*)(m + 0x28);
    if (a->Num < 0 || a->Num > 64) return;
    static BYTE tmp[64 * 0x1c]; int n = 0;
    for (int i = 0; i < a->Num; i++) { BYTE* e = (BYTE*)a->Data + i * 0x1c; if (*(int*)(e + 0x10) < 1000) memcpy(tmp + 0x1c * n++, e, 0x1c); }   // the guest's own
    int mirrored = 0;
    for (const char* p = strstr(want, "\nmk="); p && n < 64; p = strstr(p + 1, "\nmk=")) {
        char base[96] = "", cls[80] = ""; int id = 0;
        if (sscanf_s(p + 4, "%95[^,],%79[^,],%d", base, (unsigned)sizeof(base), cls, (unsigned)sizeof(cls), &id) != 3) continue;
        BYTE* actor = FindObjByLabel(base); BYTE* clsObj = nullptr; BYTE* type = FindInstanceOfClass(cls, &clsObj);
        if (!actor || !type || !clsObj) { Log("sync: marker %s (%s) not mirrored: actor %p type %p", base, cls, actor, type); continue; }
        BYTE* e = tmp + 0x1c * n++; memset(e, 0, 0x1c);
        *(BYTE**)(e + 0xc) = actor; *(int*)(e + 0x10) = 1000 + id; *(BYTE**)(e + 0x14) = clsObj; *(BYTE**)(e + 0x18) = type;
        mirrored++;
    }
    if (n > a->Max) { void* d = GameRealloc(a->Data, n * 0x1c); if (!d) return; a->Data = (void**)d; a->Max = n; }
    if (n) memcpy(a->Data, tmp, n * 0x1c);
    a->Num = n;
    Log("sync: markers -> %d mirrored from the host, %d in total", mirrored, n);
}

// `tpmarker [dist]` (foc_cmd.txt, either side, game thread): put the local pawn `dist` uu (default 300) short of the
// first objective marker (tests of objective triggers and level streaming).
static void TpMarkerCommand(const char* args) {
    float dist = 300; if (args && *args) dist = (float)atof(args);
    TickObjs& t = g_to;
    BYTE* m = ObjectiveManager(t); BYTE* pawn = nullptr;
    for (int a = 0; a < t.npc; a++) if (IsObjFast(t.pc[a]) && IsLocalPC(t.pc[a])) pawn = PawnOf(t.pc[a]);
    if (!m || !pawn) { Log("tpmarker: no objective manager or no pawn"); return; }
    TArr* a = (TArr*)(m + 0x28);
    for (int i = 0; i < a->Num; i++) {
        BYTE* e = (BYTE*)a->Data + i * 0x1c; BYTE* base = *(BYTE**)(e + 0xc); BYTE* type = *(BYTE**)(e + 0x18);
        if (!IsObjFast(base) || !IsObjFast(type)) continue;
        char tc[80]; ClassNameOf(type, tc, sizeof(tc));
        if (!strcmp(tc, "TnObjectiveMarkerTypeTransformerCampaign")) continue;
        float* to = LocOf(base); float* p = LocOf(pawn);
        float dx = p[0] - to[0], dy = p[1] - to[1], d = sqrtf(dx * dx + dy * dy); if (d < 1) d = 1;
        float n[3] = {to[0] + dx / d * dist, to[1] + dy / d * dist, to[2] + 150};
        char bl[96]; ObjLabel(base, bl, sizeof(bl));
        Log("tpmarker: pawn (%.0f,%.0f,%.0f) -> (%.0f,%.0f,%.0f), %0.f uu from marker %s", p[0], p[1], p[2], n[0], n[1], n[2], dist, bl);
        memcpy(p, n, 12); memcpy(pawn + 0x134, n, 12);
        return;
    }
    Log("tpmarker: no objective marker");
}

// ---------------------------------------------------------------- full-screen movies (Bink)
// The game plays its intro and loading videos (TransGame\Movies\*.bik) through binkw32.dll. Hooking BinkOpen and
// BinkClose in the import table tells whether such a movie is playing; in-level Bink textures come from packages, not
// from the Movies folder, and are ignored.
typedef void* (__stdcall* PFN_BinkOpen)(const char* name, unsigned flags);
typedef void(__stdcall* PFN_BinkClose)(void* bink);
static PFN_BinkOpen o_BinkOpen; static PFN_BinkClose o_BinkClose;
static void* volatile g_movies[8]; static volatile DWORD g_movieEnd = 0;
static bool MoviePlaying() { for (int i = 0; i < 8; i++) if (g_movies[i]) return true; return false; }
static void* __stdcall h_BinkOpen(const char* name, unsigned flags) {
    void* b = o_BinkOpen(name, flags);
    if (b && name && (strstr(name, "Movies\\") || strstr(name, "Movies/") || strstr(name, ".bik"))) {
        for (int i = 0; i < 8; i++) if (!g_movies[i]) { g_movies[i] = b; break; }
        Log("movie: open %s", name);
    }
    return b;
}
static void __stdcall h_BinkClose(void* b) {
    for (int i = 0; i < 8; i++) if (g_movies[i] == b) { g_movies[i] = nullptr; g_movieEnd = GetTickCount(); Log("movie: closed"); }
    o_BinkClose(b);
}
static void SyncInstallBink() {
    o_BinkOpen = (PFN_BinkOpen)Resolve("binkw32.dll", "_BinkOpen@8");
    o_BinkClose = (PFN_BinkClose)Resolve("binkw32.dll", "_BinkClose@4");
    int a = PatchIat((void*)o_BinkOpen, (void*)h_BinkOpen), b = PatchIat((void*)o_BinkClose, (void*)h_BinkClose);
    Log("movie: Bink hooks open=%d close=%d", a, b);
}

// ---------------------------------------------------------------- guest -> host "ready"
// The guest is ready when it is in the story map and no movie has played for 2 s. It sends "FOCR1\n" datagrams to the host
// (every address the game talks to) four times a second; the host notes the time of the last one.
static const char kReadyMagic[] = "FOCR1\n";
// (not "has a pawn": the paused host spawns nobody, so that would wait forever)
static bool GuestReadyNow(const TickObjs& t) {
    bool pc = false; for (int a = 0; a < t.npc; a++) if (IsLocalPC(t.pc[a])) pc = true;
    char map[96]; MapName(t, map, sizeof(map));
    bool story = map[0] == 'L' && map[1] >= '0' && map[1] <= '9';
    return pc && story && !MoviePlaying() && GetTickCount() - g_movieEnd > 2000;
}
static void SendReady(const TickObjs& t) {
    static DWORD last = 0; if (GetTickCount() - last < 250 || g_syncSock == INVALID_SOCKET || !GuestReadyNow(t)) return;
    last = GetTickCount();
    EnterCriticalSection(&g_syncCs);
    for (int i = 0; i < g_syncNPeers; i++) if (GetTickCount() - g_syncPeers[i].seen < 10000) o_sendto(g_syncSock, kReadyMagic, sizeof(kReadyMagic) - 1, 0, (const sockaddr*)&g_syncPeers[i].addr, sizeof(sockaddr_in));
    LeaveCriticalSection(&g_syncCs);
}

// ---------------------------------------------------------------- wait for the guest (host, ini waitguest seconds)
// A story map starts its intro (Kismet, matinee) as soon as it is loaded, long before a guest has loaded it. The host
// keeps its world paused (WorldInfo +0x394 Pauser = the host's PlayerReplicationInfo) after a story map loaded until a
// guest controller exists (the guest has loaded the map and logged in) or waitguest seconds passed. Network keeps
// running while paused.
static volatile LONG g_waitingGuest = 0;   // shown by the overlay
static void TickWaitGuest(const TickObjs& t) {
    static BYTE* worldSeen = nullptr; static DWORD since = 0; static bool active = false;
    if (g_waitGuestSec <= 0 || t.ngame < 1 || t.nuworld < 1) { g_waitingGuest = 0; return; }
    char map[96]; MapName(t, map, sizeof(map));
    bool story = map[0] == 'L' && map[1] >= '0' && map[1] <= '9';
    BYTE* host = nullptr; for (int a = 0; a < t.npc; a++) if (IsLocalPC(t.pc[a])) host = t.pc[a];
    BYTE* pri = host ? *(BYTE**)(host + 0x1d4) : nullptr;
    if (!story || !IsObjFast(pri)) { g_waitingGuest = 0; return; }
    BYTE* w = t.uworld[0];
    if (w != worldSeen) { worldSeen = w; since = GetTickCount(); active = true; Log("waitguest: %s loaded, pausing until a guest joins (max %d s)", map, g_waitGuestSec); }
    if (!active) return;
    // the guest has logged in, reports ready (pawn, its movie over) and the host's own movie is over
    bool guest = t.npc >= 2 && GetTickCount() - g_guestReadyAt < 2000 && !MoviePlaying() && GetTickCount() - g_movieEnd > 2000;
    bool timeout = GetTickCount() - since > (DWORD)g_waitGuestSec * 1000;
    for (int i = 0; i < t.nworld; i++) {
        BYTE** pauser = (BYTE**)(t.world[i] + 0x394);
        if (guest || timeout) { if (*pauser == pri) *pauser = nullptr; }
        else if (*pauser == nullptr) *pauser = pri;
    }
    if (guest || timeout) { active = false; g_waitingGuest = 0; Log("waitguest: %s -> unpaused", guest ? "guest ready, no movie playing" : "timeout"); }
    else g_waitingGuest = (LONG)(g_waitGuestSec - (GetTickCount() - since) / 1000);
}

// ---------------------------------------------------------------- tick (game thread)
static int SyncGetInt(const char* snap, const char* key, int def) {
    char k[40]; _snprintf_s(k, _TRUNCATE, "\n%s=", key);
    const char* p = strstr(snap, k);
    return p ? atoi(p + strlen(k)) : def;
}
static void SyncGetStr(const char* snap, const char* key, char* out, size_t n) {
    char k[40]; _snprintf_s(k, _TRUNCATE, "\n%s=", key); out[0] = 0;
    const char* p = strstr(snap, k); if (!p) return;
    p += strlen(k); size_t i = 0; while (p[i] && p[i] != '\n' && i + 1 < n) { out[i] = p[i]; i++; } out[i] = 0;
}

static void TickSync(const TickObjs& t) {
    TickWaitGuest(t);
    if (!g_syncOn) return;
    static DWORD last = 0, lastLog = 0; DWORD now = GetTickCount();
    char map[96]; MapName(t, map, sizeof(map));
    if (now - lastLog > 10000) { lastLog = now; Log("sync: %s sent=%ld received=%ld peers=%d", t.ngame ? "host" : "guest", g_syncTx, g_syncRxCount, g_syncNPeers); }
    if (t.ngame >= 1) {   // host: send the snapshot (only in a game with guests, not in the front end)
        if (now - last < 250 || g_syncSock == INVALID_SOCKET || t.npc < 2) return;
        last = now;
        BYTE* m = ObjectiveManager(t);
        char snap[1000]; int n = 0;
        static LONG seq = 0;
        n += _snprintf_s(snap + n, sizeof(snap) - n, _TRUNCATE, "%sseq=%ld\nmap=%s\nobj=%d\n", kSyncMagic, ++seq, map, m ? *(int*)(m + 0x3c) : -2);
        if (m) n += HostMarkers(m, snap + n, sizeof(snap) - n);
        EnterCriticalSection(&g_syncCs);
        for (int i = 0; i < g_syncNPeers; i++) {
            if (now - g_syncPeers[i].seen > 10000) continue;
            o_sendto(g_syncSock, snap, n, 0, (const sockaddr*)&g_syncPeers[i].addr, sizeof(sockaddr_in));
            InterlockedIncrement(&g_syncTx);
        }
        LeaveCriticalSection(&g_syncCs);
        return;
    }
    // guest: tell the host when ready, apply the latest snapshot
    SendReady(t);
    char snap[1200];
    EnterCriticalSection(&g_syncCs); memcpy(snap, g_syncRx, sizeof(snap)); LeaveCriticalSection(&g_syncCs);
    if (!snap[0]) return;
    char hmap[96]; SyncGetStr(snap + sizeof(kSyncMagic) - 2, "map", hmap, sizeof(hmap));
    if (_stricmp(hmap, map) != 0) return;   // still loading, or another map
    const char* body = snap + sizeof(kSyncMagic) - 2;   // starts at the '\n' that ends the magic
    int obj = SyncGetInt(body, "obj", -2);
    BYTE* m = ObjectiveManager(t);
    if (m && obj >= -1) ApplyObjective(m, obj);
    if (m) ApplyMarkers(m, body);
}
