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
static void TexSyncQueue(const char* buf, int len);   // gfxsync.inc
static void KMirrorQueue(const char* buf, int len);   // kmirror.inc
// host: where the guest sees its own pawn (FOCP1 datagram, 4 per second): owner 2026-10-20, the guest's pawn on the
// server and on the guest drifted 40000 uu apart after a scene the guest played only locally.
static volatile DWORD g_gposAt = 0; static float g_gpos[3]; static char g_gposState[48] = ""; static int g_gposCine = 0;

static void SyncInit() { InitializeCriticalSection(&g_syncCs); }

// sendto hook: remember the game's socket
static void SyncNoteOut(SOCKET s) { if (g_syncOn) g_syncSock = s; }

// recvfrom hook: true = our snapshot (consumed, the game must not see it). Other datagrams register their sender as a
// peer (on the host these are the guests).
static bool SyncFilterIn(const char* buf, int len, const sockaddr* from) {
    if (!g_syncOn) return false;
    if (len >= 6 && !memcmp(buf, "FOCR1\n", 6)) { g_guestReadyAt = GetTickCount(); return true; }   // guest -> host "ready"
    if (len >= 6 && !memcmp(buf, "FOCT1\n", 6)) { TexSyncQueue(buf, len); return true; }   // host -> guest: forced texture names (gfxsync.inc)
    if (len >= 6 && !memcmp(buf, "FOCK1\n", 6)) { KMirrorQueue(buf, len); return true; }   // host -> guest: Kismet op to run (kmirror.inc)
    if (len >= 6 && len < 200 && !memcmp(buf, "FOCP1\n", 6)) {   // guest -> host: its own pawn position
        char tmp[200]; memcpy(tmp, buf, len); tmp[len] = 0; float x, y, z; char st[48] = ""; int c = 0;
        if (sscanf_s(tmp + 6, "%f %f %f %47s %d", &x, &y, &z, st, (unsigned)sizeof(st), &c) >= 4) {
            g_gpos[0] = x; g_gpos[1] = y; g_gpos[2] = z; strncpy_s(g_gposState, st, _TRUNCATE); g_gposCine = c; g_gposAt = GetTickCount(); }
        return true;
    }
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
    TArr* a = (TArr*)(m + 0x28);
    if (a->Num < 0 || a->Num > 64) return;
    // owner 2026-10-20 (battery marker stayed on the guest after the host took it): the guest keeps only its own
    // teammate markers; every other marker is the host's list. The guest's level adds markers of its own (pickups),
    // so they are removed whenever they show up, not only when the host's list changes.
    auto ownKeep = [](BYTE* e) { BYTE* type = *(BYTE**)(e + 0x18); char tc[80] = ""; if (IsObjFast(type)) ClassNameOf(type, tc, sizeof(tc)); return !strcmp(tc, "TnObjectiveMarkerTypeTransformerCampaign") || !strcmp(tc, "TnObjectiveMarkerTypeDownedAlly"); };
    bool stray = false;
    for (int i = 0; i < a->Num; i++) { BYTE* e = (BYTE*)a->Data + i * 0x1c; if (*(int*)(e + 0x10) < 1000 && !ownKeep(e)) stray = true; }
    if (!strcmp(want, applied)) {
        // same host list: only drop the stray own markers in place (no object lookups: the guest's level re-adds them
        // every tick, and resolving the host markers each time froze the guest at ~1 s per tick, 2026-10-20)
        if (!stray) return;
        int k = 0;
        for (int i = 0; i < a->Num; i++) { BYTE* e = (BYTE*)a->Data + i * 0x1c; if (*(int*)(e + 0x10) < 1000 && !ownKeep(e)) continue; if (k != i) memcpy((BYTE*)a->Data + k * 0x1c, e, 0x1c); k++; }
        static DWORD lastLog = 0; if (GetTickCount() - lastLog > 10000) { lastLog = GetTickCount(); Log("sync: markers -> dropped %d own non-teammate marker(s) again", a->Num - k); }
        a->Num = k;
        return;
    }
    strcpy_s(applied, want);
    static BYTE tmp[64 * 0x1c]; int n = 0, dropped = 0;
    for (int i = 0; i < a->Num; i++) { BYTE* e = (BYTE*)a->Data + i * 0x1c; if (*(int*)(e + 0x10) < 1000) { if (ownKeep(e)) memcpy(tmp + 0x1c * n++, e, 0x1c); else dropped++; } }   // the guest's own teammate markers
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
    Log("sync: markers -> %d mirrored from the host, %d in total, %d own non-teammate dropped", mirrored, n, dropped);
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
// guest -> host: "FOCP1\nx y z controllerState cine" four times a second while the guest has a pawn (host: reconcile)
static void SendPos(const TickObjs& t) {
    static DWORD last = 0; if (t.ngame > 0 || GetTickCount() - last < 250 || g_syncSock == INVALID_SOCKET) return;
    BYTE* pc = nullptr; for (int a = 0; a < t.npc; a++) if (IsObjFast(t.pc[a]) && IsLocalPC(t.pc[a])) pc = t.pc[a];
    BYTE* pw = pc ? PawnOf(pc) : nullptr; if (!pw) return;
    last = GetTickCount();
    char st[48] = "-"; StateName(pc, st, sizeof(st)); if (!st[0]) strcpy_s(st, "-");
    float* l = LocOf(pw); char msg[160];
    int n = _snprintf_s(msg, _TRUNCATE, "FOCP1\n%.0f %.0f %.0f %s %d\n", l[0], l[1], l[2], st, (*(unsigned*)(pc + 0x360) & 0x20000) ? 1 : 0);
    EnterCriticalSection(&g_syncCs);
    for (int i = 0; i < g_syncNPeers; i++) if (GetTickCount() - g_syncPeers[i].seen < 10000) o_sendto(g_syncSock, msg, n, 0, (const sockaddr*)&g_syncPeers[i].addr, sizeof(sockaddr_in));
    LeaveCriticalSection(&g_syncCs);
}
static void SendReady(const TickObjs& t) {
    static DWORD last = 0; if (GetTickCount() - last < 250 || g_syncSock == INVALID_SOCKET || !GuestReadyNow(t)) return;
    last = GetTickCount();
    EnterCriticalSection(&g_syncCs);
    for (int i = 0; i < g_syncNPeers; i++) if (GetTickCount() - g_syncPeers[i].seen < 10000) o_sendto(g_syncSock, kReadyMagic, sizeof(kReadyMagic) - 1, 0, (const sockaddr*)&g_syncPeers[i].addr, sizeof(sockaddr_in));
    LeaveCriticalSection(&g_syncCs);
}

// ---------------------------------------------------------------- Escalation: the second scoring rule (host)
// The game's lobby starts Escalation with GameRules "TnGameRules_CompetitiveScoring,TnGameRules_TrackKills", but a
// comma in a command-line URL breaks the map load, so the URL carries only CompetitiveScoring (points, money). This adds
// TnGameRules_TrackKills (kills on the TAB board, kill streaks) once: native spawn through the Actor spawn wrapper
// ghidra 0x54a8f0 (thiscall on the spawning actor: Class, Owner, int* tag, &Location, &Rotation, Template,
// bNoCollisionFail, FString* out; the game's own callers, e.g. ghidra 0x41de60, pass 0, 0, &0, loc, rot, 0, 0, &{0,0,0})
// with the game as spawner, then appended to GameInfo+0x280 GameRulesModifiers ->
// GameRules+0x1d0 NextGameRules (offsets from `props`). ini killrules=0 turns it off.
typedef BYTE* (__fastcall* PFN_SpawnWrap)(void* self, void* edx, BYTE* cls, BYTE* owner, int* tag, float* loc, int* rot, BYTE* tmpl, int noCollFail, int* outStr);
static void TickEscalationRules(const TickObjs& t) {
    static BYTE* doneFor = nullptr;
    if (!g_killRules || t.ngame < 1 || t.nuworld < 1 || doneFor == t.uworld[0]) return;
    BYTE* game = t.game[0];
    char gc[80] = ""; ClassNameOf(game, gc, sizeof(gc));
    if (strcmp(gc, "TnSurvivalGame") != 0) return;
    BYTE* first = *(BYTE**)(game + 0x280);
    if (!IsObjFast(first)) return;   // rules not there yet (or none: an old URL)
    unsigned tkId = ClassIdByName("TnGameRules_TrackKills");
    BYTE* tkCls = tkId ? g_classObjById[tkId] : nullptr;
    if (!tkCls) return;
    doneFor = t.uworld[0];
    BYTE* last = first;
    for (int i = 0; i < 16; i++) {
        if ((*(unsigned*)(last + 0x14) & 0x1fff) == tkId) { Log("killrules: TnGameRules_TrackKills already in the chain"); return; }
        BYTE* nx = *(BYTE**)(last + 0x1d0);
        if (!IsObjFast(nx)) break;
        last = nx;
    }
    int tag = 0; int rot[3] = {0, 0, 0}; float loc[3]; memcpy(loc, LocOf(game), 12);
    PFN_SpawnWrap spawn = (PFN_SpawnWrap)((BYTE*)GetModuleHandleW(nullptr) + (0x54a8f0 - 0x400000));
    BYTE* r = nullptr;
    int outStr[3] = {0, 0, 0};
    __try { r = spawn(game, nullptr, tkCls, nullptr, &tag, loc, rot, nullptr, 0, outStr); }
    __except (EXCEPTION_EXECUTE_HANDLER) { Log("killrules: spawn raised %08lx", GetExceptionCode()); return; }
    if (!IsObjFast(r)) { Log("killrules: spawn returned %p", r); return; }
    *(BYTE**)(last + 0x1d0) = r;
    char rn[96]; ObjLabel(r, rn, sizeof(rn)); char ln[96]; ObjLabel(last, ln, sizeof(ln));
    Log("killrules: spawned %s, chained after %s", rn, ln);
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
    bool guestPawns = t.npc >= 2;   // every guest controller already has its pawn on the host (the intro matinee binds pawns at its start)
    for (int a = 0; a < t.npc; a++) if (IsObjFast(t.pc[a]) && !IsLocalPC(t.pc[a]) && !PawnOf(t.pc[a])) guestPawns = false;
    static DWORD readySince = 0;
    bool ready = t.npc >= 2 && GetTickCount() - g_guestReadyAt < 2000 && !MoviePlaying() && GetTickCount() - g_movieEnd > 2000;
    if (!ready) readySince = 0; else if (!readySince) readySince = GetTickCount();
    if (ready && !guestPawns && GetTickCount() - readySince > 3000) { guestPawns = true; Log("waitguest: guest ready 3 s but its pawn is not on the host yet -> going on"); }
    bool guest = ready && guestPawns;
    bool timeout = GetTickCount() - since > (DWORD)g_waitGuestSec * 1000;
    for (int i = 0; i < t.nworld; i++) {
        BYTE** pauser = (BYTE**)(t.world[i] + 0x394);
        if (guest || timeout) { if (*pauser == pri) *pauser = nullptr; }
        else if (*pauser == nullptr) *pauser = pri;
    }
    if (guest || timeout) { active = false; g_waitingGuest = 0; Log("waitguest: %s -> unpaused", guest ? "guest ready, no movie playing" : "timeout"); }
    else g_waitingGuest = (LONG)(g_waitGuestSec - (GetTickCount() - since) / 1000);
}

// ---------------------------------------------------------------- calling script functions (game thread only)
// Found through UObject::ScriptConsoleExec (ghidra 0xbf1f70): FName from a string = ghidra 0xcc13e0 (thiscall on the
// 4-byte packed FName, args: name, 0, 1), FindFunction = object vtable +0xC8 (thiscall: FName, global=0),
// ProcessEvent = ghidra 0x728c60 (thiscall: UFunction*, void* Parms, void* Result). UFunction +0x5e = ParmsSize.
// The calls run with every register and the stack pointer saved around them (the conventions are taken from the
// decompiler, not verified).
static DWORD __declspec(noinline) CallThis3(void* fn, void* self, DWORD a1, DWORD a2, DWORD a3) {
    DWORD r = 0, savedEsp = 0;
    __asm {
        pushad
        mov savedEsp, esp
        mov ecx, self
        push a3
        push a2
        push a1
        call fn
        mov esp, savedEsp
        mov r, eax
        popad
    }
    return r;
}
// UStruct::SuperStruct offset, calibrated on the pair Pawn -> Actor
static int SuperOffset() {
    static int off = -1;
    if (off >= 0) return off;
    unsigned pid = ClassIdByName("Pawn"), aid = ClassIdByName("Actor");
    BYTE* p = pid && pid < 8192 ? g_classObjById[pid] : nullptr; BYTE* a = aid && aid < 8192 ? g_classObjById[aid] : nullptr;
    if (!IsObjFast(p) || !IsObjFast(a)) return -1;
    // Pawn -> HmActor -> ... -> Actor in this build: walk the chain along each candidate offset (SuperStruct is +0x28)
    for (int k = 0x28; k < 0x100 && off < 0; k += 4) {
        BYTE* c = p;
        for (int i = 0; i < 16 && IsObjFast(c); i++) { if (c == a) { off = k; break; } c = *(BYTE**)(c + k); }
    }
    Log("call: SuperStruct at +0x%x", off);
    return off;
}
static bool ClassIsA(BYTE* cls, BYTE* want) {
    int off = SuperOffset();
    for (int i = 0; i < 64 && IsObjFast(cls); i++) {
        if (cls == want) return true;
        if (off < 0) return false;
        cls = *(BYTE**)(cls + off);
    }
    return false;
}
static BYTE* FindScriptFunction(BYTE* obj, const char* name) {
    if (!IsObjFast(obj)) return nullptr;
    BYTE* base = (BYTE*)GetModuleHandleW(nullptr);
    wchar_t w[96]; MultiByteToWideChar(CP_ACP, 0, name, -1, w, 96);
    DWORD fname = 0;
    DWORD ret = CallThis3(base + (0xcc13e0 - 0x400000), &fname, (DWORD)(UINT_PTR)w, 0, 1);
    void* findFn = (*(void***)obj)[0xC8 / 4];
    BYTE* f = fname ? (BYTE*)(UINT_PTR)CallThis3(findFn, obj, fname, 0, 0) : nullptr;
    (void)ret;
    if (!IsObjFast(f)) {
        // FindFunction finds nothing on the native-only game class (TnCampaignGame): take the Function object with that
        // name from GObjects (the one whose +0x28 word is not another Function, i.e. not a nested copy) whose class (or
        // state's class) is the object's class or one of its superclasses: another class's function of the same name
        // (TnWeapon.SetHidden for a pawn) must not run on this object
        unsigned fid = ClassIdByName("Function"); TArr* go = GObj();
        BYTE* own = g_classObjById[*(unsigned*)(obj + 0x14) & 0x1fff];
        for (int i = 0; i < go->Num && !f; i++) {
            BYTE* o = (BYTE*)go->Data[i];
            if (!o || (*(unsigned*)(o + 0x14) & 0x1fff) != fid) continue;
            char nm[96]; if (!ObjNameStr(o, nm, sizeof(nm)) || strcmp(nm, name)) continue;
            BYTE* w28 = *(BYTE**)(o + 0x28);
            if (IsObjFast(w28) && (*(unsigned*)(w28 + 0x14) & 0x1fff) == fid) continue;
            if (!ClassIsA(own, w28) && !(IsObjFast(w28) && ClassIsA(own, *(BYTE**)(w28 + 0x28)))) continue;
            f = o;
        }
        if (f) Log("call: %s taken from GObjects (FindFunction found nothing)", name);
    }
    return IsObjFast(f) ? f : nullptr;
}
// UObject::GotoState (ghidra 0x70df30, thiscall: FName State, FName Label, bForceEvents, bKeepStack); returns false
// when the state name is unknown to the name table
static bool GotoStateNamed(BYTE* obj, const char* state) {
    if (!IsObjFast(obj)) return false;
    BYTE* base = (BYTE*)GetModuleHandleW(nullptr);
    wchar_t w[64]; MultiByteToWideChar(CP_ACP, 0, state, -1, w, 64);
    DWORD fname = 0; CallThis3(base + (0xcc13e0 - 0x400000), &fname, (DWORD)(UINT_PTR)w, 0, 1);
    if (!fname) return false;
    __try { CallThis3(base + (0x70df30 - 0x400000), obj, fname, 0, 0); }
    __except (EXCEPTION_EXECUTE_HANDLER) { Log("gotostate: raised %08lx", GetExceptionCode()); return false; }
    return true;
}
// The campaign game's RestartPlayer (vtable +0x2b0 of TnCampaignGame) is an empty function (ghidra 0x803d20, ret 8),
// so a dead player is never spawned again. The base GameInfo::RestartPlayer is ghidra 0x7c2ee0 (thiscall on the game,
// arg: the controller's +0x10 subobject); it spawns only when Controller.Pawn is null.
static bool NativeRestartPlayer(BYTE* game, BYTE* pc) {
    BYTE* base = (BYTE*)GetModuleHandleW(nullptr);
    __try { CallThis3(base + (0x7c2ee0 - 0x400000), game, (DWORD)(UINT_PTR)(pc + 0x10), 0, 0); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { Log("restartplayer: raised %08lx", GetExceptionCode()); return false; }
}
// parms: buffer of at least the function's ParmsSize (zero-filled by the caller); returns false when not found
static bool CallScript(BYTE* obj, const char* name, void* parms, int parmsCap) {
    BYTE* f = FindScriptFunction(obj, name);
    if (!f) { Log("call: %s not found on the object", name); return false; }
    int size = *(unsigned short*)(f + 0x5e);
    if (size > parmsCap) { Log("call: %s needs %d bytes of parameters", name, size); return false; }
    BYTE* base = (BYTE*)GetModuleHandleW(nullptr);
    __try { CallThis3(base + (0x728c60 - 0x400000), obj, (DWORD)(UINT_PTR)f, (DWORD)(UINT_PTR)parms, 0); }
    __except (EXCEPTION_EXECUTE_HANDLER) { Log("call: %s raised %08lx", name, GetExceptionCode()); return false; }
    return true;
}

// ---------------------------------------------------------------- cutscenes (B14, host, ini cinesync)
// Kismet puts only the host's controller into cinematic mode, so during a cutscene the guest kept its HUD and could
// move. When the host's PlayerController.bCinematicMode (+0x360 bit 0x20000) changes, the host calls the client RPC
// ClientSetCinematicMode(bInCinematicMode, bAffectsMovement, bAffectsTurning, bAffectsHUD, bAffectsButtons) on every
// remote controller (PlayerController.SetCinematicMode has no Function object in this build). The affects flags stay 1
// so that "off" brings the HUD back. Tested (cine3): guest cine=1 during the host's cutscene, HUD gone, back after.
// Not done: hiding the guest's pawn. Actor.SetHidden / TnPawn.SetHidden have no Function objects either, and writing
// Actor.bHidden (+0x84 bit 4) does not change what is drawn (the components are not updated).
static void TickCinematic(const TickObjs& t) {
    static int last = -1; static int lastRemotes = 0;
    if (!g_cineSync || t.ngame < 1) { last = -1; return; }
    BYTE* host = nullptr; int remotes = 0;
    for (int a = 0; a < t.npc; a++) { if (IsLocalPC(t.pc[a])) host = t.pc[a]; else remotes++; }
    if (!host || !remotes) { last = -1; return; }
    int cine = (*(unsigned*)(host + 0x360) & 0x20000) ? 1 : 0;
    if (cine == last && remotes == lastRemotes) return;
    if (last < 0 && !cine) { last = 0; lastRemotes = remotes; return; }   // start of the map, nothing to undo
    last = cine; lastRemotes = remotes;
    for (int a = 0; a < t.npc; a++) {
        BYTE* pc = t.pc[a]; if (IsLocalPC(pc) || !IsObjFast(pc)) continue;
        DWORD parms[16] = {(DWORD)cine, 1, 1, 1, 1, 0};   // affects flags stay 1, else "off" leaves the HUD hidden
        bool ok = CallScript(pc, "ClientSetCinematicMode", parms, sizeof(parms));
        char pn[96]; ObjLabel(pc, pn, sizeof(pn));
        Log("cinesync: host cinematic mode %s -> %s.ClientSetCinematicMode(%d) %s", cine ? "on" : "off", pn, cine, ok ? "ok" : "failed");
    }
}

// ---------------------------------------------------------------- player names
// Each game takes the player name from its own profile ("Default" when Demonware is offline), so both players could
// show the same name. Once the local pawn exists, the local controller runs `SetName <name>` (PlayerController exec ->
// ServerChangeName, so the host's copy is renamed too) through UObject::ScriptConsoleExec (ghidra 0xbf1f70). Name: the
// ?Name= option of our URL (the guest's join URL carries the Steam name), else the Steam name from relay.status.
static char g_relayPersona[64] = "";   // the player's Steam name (relay.status persona=, read by overlay.inc)
typedef BOOL(__fastcall* PFN_SCE)(void* self, void* edx, const wchar_t* str, void* ar, void* executor);
static void* g_sceOut[2];   // an FOutputDevice whose vtable entries do nothing (set up in TickNames)
static void __fastcall SceOutStub(void*, void*, const wchar_t*, int) {}
static void* g_sceVt[32];
static void TickNames(const TickObjs& t) {
    static BYTE* world = nullptr; static int tries = 0; static DWORD last = 0;
    if (t.nuworld < 1) return;
    if (world != t.uworld[0]) { world = t.uworld[0]; tries = 0; }
    if (tries >= 3 || GetTickCount() - last < 5000) return;
    char want[64] = "";
    const char* url = CmdlineUrl(); const char* n = strstr(url, "?Name=");
    if (n) { n += 6; size_t i = 0; while (n[i] && n[i] != '?' && i + 1 < sizeof(want)) { want[i] = n[i]; i++; } want[i] = 0; }
    else strncpy_s(want, g_relayPersona, _TRUNCATE);
    for (char* c = want; *c; c++) if (*c == ' ' || *c == '"' || (unsigned char)*c < 0x20) *c = '_';
    if (!want[0]) return;
    BYTE* pc = nullptr; for (int a = 0; a < t.npc; a++) if (IsLocalPC(t.pc[a])) pc = t.pc[a];
    if (!pc || !PawnOf(pc)) return;
    BYTE* pri = *(BYTE**)(pc + 0x1d4); if (!IsObjFast(pri)) return;
    char have[64] = ""; FStrAscii(pri + 0x1e0, have, sizeof(have));
    if (!strcmp(have, want)) return;
    last = GetTickCount(); tries++;
    if (!g_sceVt[0]) { for (int i = 0; i < 32; i++) g_sceVt[i] = (void*)SceOutStub; g_sceOut[0] = g_sceVt; }
    wchar_t cmd[96]; _snwprintf_s(cmd, _TRUNCATE, L"SetName %hs", want);
    PFN_SCE fn = (PFN_SCE)((BYTE*)GetModuleHandleW(nullptr) + (0xbf1f70 - 0x400000));
    BOOL r = FALSE;
    __try { r = fn(pc, nullptr, cmd, g_sceOut, nullptr); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("names: SetName raised %08lx", GetExceptionCode()); tries = 3; return; }
    Log("names: '%s' -> SetName %s (%s)", have, want, r ? "ok" : "not handled");
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

static void PostKey(UINT vk);   // overlay.inc
static int ChapterSnap(const TickObjs& t, char* out, size_t n);   // story_title.inc (host: chap= lines)
static void ChapterApply(const TickObjs& t, const char* body);     // story_title.inc (guest)
static int MirrorSnap(const TickObjs& t, char* out, size_t n);    // mirror.inc (host: matanim=)
static void MirrorApply(const TickObjs& t, const char* body);      // mirror.inc (guest)
static void TickSync(const TickObjs& t) {
    TickWaitGuest(t);
    TickEscalationRules(t);
    TickCinematic(t);
    // B11: everybody is down and the host shows MISSION FAILED: its first item is "Restart From Last Checkpoint";
    // press Enter for the host after 4 s (window message, no focus needed). Tested: the level restarts, the guest stays.
    // A second Enter 1.5 s later confirms "Load Checkpoint - all unsaved progress will be lost" (Continue is selected).
    static DWORD confirmAt = 0;
    if (g_autoRestart && g_gameOverAt && GetTickCount() - g_gameOverAt > 4000) { g_gameOverAt = 0; PostKey(VK_RETURN); confirmAt = GetTickCount() + 1500; Log("autorestart: Enter on the game-over menu (restart from the last checkpoint)"); }
    if (confirmAt && (LONG)(GetTickCount() - confirmAt) >= 0) { confirmAt = 0; PostKey(VK_RETURN); Log("autorestart: Enter on the Load Checkpoint confirmation"); }
    if (g_setNames) TickNames(t);
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
        n += ChapterSnap(t, snap + n, sizeof(snap) - n);
        n += MirrorSnap(t, snap + n, sizeof(snap) - n);
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
    SendPos(t);
    char snap[1200];
    EnterCriticalSection(&g_syncCs); memcpy(snap, g_syncRx, sizeof(snap)); LeaveCriticalSection(&g_syncCs);
    if (!snap[0]) return;
    char hmap[96]; SyncGetStr(snap + sizeof(kSyncMagic) - 2, "map", hmap, sizeof(hmap));
    if (hmap[0] && t.ngame == 0) { strncpy_s(g_hostMapHint, hmap, _TRUNCATE); g_hostMapAt = GetTickCount(); }   // client: the host's map is ours (MapName)
    if (_stricmp(hmap, map) != 0) return;   // still loading, or another map
    const char* body = snap + sizeof(kSyncMagic) - 2;   // starts at the '\n' that ends the magic
    int obj = SyncGetInt(body, "obj", -2);
    BYTE* m = ObjectiveManager(t);
    if (m && obj >= -1) ApplyObjective(m, obj);
    if (m) ApplyMarkers(m, body);
    ChapterApply(t, body);
    MirrorApply(t, body);
}
