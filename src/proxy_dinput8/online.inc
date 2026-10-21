// online.inc (owner 2026-10-21): Escalation through the game's own main menu, with Steam (Spacewar 480 relay) in place
// of the dead Demonware service. Step 1 maps the online subsystem: the scripts are compiled to C++, so the menu calls
// the subsystem through its interface vtables (OnlineSubsystem.PlayerInterface etc.), not through Function objects.

// `osprobe [entries]` (test command): the OnlineSubsystemDemonware object, and for each interface variable of
// OnlineSubsystem the object and interface pointer it holds plus the first entries of that interface's vtable (exe VAs,
// for Ghidra). The method order of a native interface follows its declaration order in the .uc file.
static const char* kOsIfaces[] = { "AccountInterface", "PlayerInterface", "PlayerInterfaceEx", "SystemInterface", "GameInterface",
    "ContentInterface", "VoiceInterface", "StatsInterface", "NewsInterface", "ReservationInterface", "SteamInterface" };
static MirrorProp g_mpOsIface[sizeof(kOsIfaces) / sizeof(kOsIfaces[0])] = {};
static BYTE* OnlineSubObject() {
    static unsigned cid = 0; if (!cid) cid = ClassIdByName("OnlineSubsystemDemonware");
    if (!cid) return nullptr;
    TArr* go = GObj();
    for (int i = 0; i < go->Num; i++) {
        BYTE* o = (BYTE*)go->Data[i];
        if (!o || (*(unsigned*)(o + 0x14) & 0x1fff) != cid) continue;
        char nm[80]; if (!ObjNameStr(o, nm, sizeof(nm)) || !strncmp(nm, "Default__", 9)) continue;
        return o;
    }
    return nullptr;
}
static DWORD ExeVa(const void* p) {
    BYTE* base = (BYTE*)GetModuleHandleW(nullptr);
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
    if ((BYTE*)p < base || (BYTE*)p >= base + nt->OptionalHeader.SizeOfImage) return 0;
    return (DWORD)((BYTE*)p - base) + 0x400000;
}
static void OsProbeCommand(const char* args) {
    int entries = atoi(args); if (entries <= 0) entries = 64;
    BYTE* os = OnlineSubObject();
    if (!os) { Log("osprobe: no OnlineSubsystemDemonware object"); return; }
    Log("osprobe: subsystem %p vtable %lx", os, ExeVa(*(void**)os));
    for (int off = 4; off < 0x200; off += 4) {   // secondary vtables (multiple inheritance) inside the object
        void* w = nullptr; __try { w = *(void**)(os + off); } __except (EXCEPTION_EXECUTE_HANDLER) { break; }
        DWORD va = ExeVa(w);
        if (va >= 0x1361000 && va < 0x18cf000) Log("osprobe:   +%03x -> exe:%lx", off, va);   // .rdata pointers (vtables)
    }
    for (size_t k = 0; k < sizeof(kOsIfaces) / sizeof(kOsIfaces[0]); k++) {
        if (!MirrorPropFind("OnlineSubsystem", kOsIfaces[k], g_mpOsIface[k])) { Log("osprobe: %s not found", kOsIfaces[k]); continue; }
        BYTE* obj = *(BYTE**)(os + g_mpOsIface[k].off); BYTE* itf = *(BYTE**)(os + g_mpOsIface[k].off + 4);
        char on[80] = "-", cn[80] = "-";
        if (IsObjFast(obj)) { ObjNameStr(obj, on, sizeof(on)); ClassNameOf(obj, cn, sizeof(cn)); }
        Log("osprobe: %s +0x%x obj=%p %s (%s) iface=%p (obj+0x%x)", kOsIfaces[k], g_mpOsIface[k].off, obj, on, cn, itf, (unsigned)(itf - obj));
        void** vt = nullptr; __try { vt = itf ? *(void***)itf : nullptr; } __except (EXCEPTION_EXECUTE_HANDLER) { vt = nullptr; }
        if (!vt) continue;
        char line[1200]; size_t n = _snprintf_s(line, _TRUNCATE, "osprobe:   vt %lx:", ExeVa(vt));
        for (int i = 0; i < entries; i++) {
            void* f = nullptr; __try { f = vt[i]; } __except (EXCEPTION_EXECUTE_HANDLER) { break; }
            DWORD va = ExeVa(f); if (va < 0x401000 || va >= 0x1361000) break;   // past the end of the table (not .text)
            n += _snprintf_s(line + n, sizeof(line) - n, _TRUNCATE, " %d=%lx", i, va);
            if (n > sizeof(line) - 40) { Log("%s", line); n = _snprintf_s(line, _TRUNCATE, "osprobe:   ..."); }
        }
        Log("%s", line);
    }
}

// esclogin (ini, default 1): the front-end game (TransContent.TnFrontEndGame) opens ESCALATION only in state
// LoginSequenceCompleted; its login chain goes through Demonware and ends in LoginSequenceFailed ("Sign in / Failed to
// sign in"). Any login state is sent straight to LoginSequenceCompleted (its BeginState tells the menu the login
// sequence succeeded). GotoState = ghidra 0x70df30, thiscall on the object: (packed state name, label 0 = Begin,
// bForceEvents 0, 0); the packed name is the State object's own name word (+4). Current state: StateFrame (object
// +0x28) +0x38 = the State object (found 2026-10-21).
static const char* kLoginStates[] = { "WaitingForNetwork", "WaitingForSteam", "ReadingAccountNames", "CreatingAccount",
    "LoggingInPC", "ReadingKeyArchive", "WaitingForNatDiscovery", "LoginSequenceFailed" };
static DWORD StateNameWord(const char* state) {
    static unsigned cid = 0; if (!cid) cid = ClassIdByName("State");
    if (!cid) return 0;
    TArr* go = GObj();
    for (int i = 0; i < go->Num; i++) {
        BYTE* o = (BYTE*)go->Data[i];
        if (!o || (*(unsigned*)(o + 0x14) & 0x1fff) != cid) continue;
        char nm[80]; unsigned num = 0;
        if (ObjNameStr(o, nm, sizeof(nm), &num) && num == 0 && !strcmp(nm, state)) return *(DWORD*)(o + 4);
    }
    return 0;
}
static bool CurrentStateName(BYTE* obj, char* out, size_t n) {
    out[0] = 0;
    __try {
        BYTE* frame = *(BYTE**)(obj + 0x28); if (!frame) return false;
        BYTE* st = *(BYTE**)(frame + 0x38); if (!IsObjFast(st)) return false;
        return ObjNameStr(st, out, n);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
typedef void(__thiscall* GotoStateFn)(BYTE* self, DWORD stateName, DWORD label, DWORD forceEvents, DWORD unk);
static bool GotoStateNative(BYTE* obj, DWORD stateName) {
    GotoStateFn fn = (GotoStateFn)((BYTE*)GetModuleHandleW(nullptr) + (0x70df30 - 0x400000));
    __try { fn(obj, stateName, 0, 0, 0); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static void InstallOnlinePatches();
static void TickSteamChat(const TickObjs& t); static void TickLanSessions(const TickObjs& t); static void EscJoinCommand(const char* args); static void SessionsCommand(); static void EscInviteCommand(const char* args);
static void TickEscLogin(const TickObjs& t) {
    InstallOnlinePatches();
    TickSteamChat(t);
    TickLanSessions(t);
    if (!g_escLogin) return;
    for (int g = 0; g < t.ngame; g++) {
        BYTE* game = t.game[g]; char cn[80] = "";
        if (!IsObjFast(game) || !ClassNameOf(game, cn, sizeof(cn))) continue;
        char st[80]; bool got = CurrentStateName(game, st, sizeof(st));
        // state changes per class (several GameInfo-like objects are listed)
        static char seenCls[8][64], seenSt[8][64]; static int nSeen = 0;
        int k = 0; while (k < nSeen && strcmp(seenCls[k], cn)) k++;
        if (k == nSeen && nSeen < 8) { strcpy_s(seenCls[nSeen], cn); seenSt[nSeen][0] = 1; seenSt[nSeen][1] = 0; nSeen++; }
        if (k < nSeen && strcmp(seenSt[k], got ? st : "(no state)")) { strcpy_s(seenSt[k], got ? st : "(no state)"); Log("esclogin: game %s %s", cn, seenSt[k]); }
        if (strcmp(cn, "TnFrontEndGame") || !got) continue;
        bool login = false;
        for (const char* s : kLoginStates) if (!strcmp(st, s)) login = true;
        if (!login) continue;
        static DWORD done = 0; if (!done) done = StateNameWord("LoginSequenceCompleted");
        if (!done) { Log("esclogin: no State LoginSequenceCompleted"); return; }
        bool ok = GotoStateNative(game, done);
        char now[80]; CurrentStateName(game, now, sizeof(now));
        Log("esclogin: front end in %s -> LoginSequenceCompleted (%s, now %s)", st, ok ? "ok" : "fault", now);
    }
}

// `peek <hex address> [count] [deref]` (test command): count dwords from a raw address, each also as a ghidra VA when it
// points into the exe. With deref=1 the address is read first (peek the table a pointer points at).
static void PeekCommand(const char* args) {
    unsigned long a = 0; int count = 16, deref = 0;
    if (sscanf_s(args, "%lx %i %i", &a, &count, &deref) < 1) { Log("peek: usage peek <hex> [count] [deref]"); return; }
    BYTE* p = (BYTE*)(UINT_PTR)a;
    __try { if (deref) p = *(BYTE**)p; } __except (EXCEPTION_EXECUTE_HANDLER) { Log("peek: fault"); return; }
    for (int i = 0; i < count; i++) {
        DWORD w = 0; __try { w = *(DWORD*)(p + 4 * i); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("peek: fault at %p", p + 4 * i); return; }
        DWORD va = ExeVa((void*)(UINT_PTR)w); char on[80] = "";
        if (IsObjFast((BYTE*)(UINT_PTR)w)) ObjNameStr((BYTE*)(UINT_PTR)w, on, sizeof(on));
        char vs[24] = ""; if (va) _snprintf_s(vs, _TRUNCATE, " ghidra:%lx", va);
        Log("peek: %p [%d] %08lx%s%s%s", p + 4 * i, i, w, vs, on[0] ? " obj:" : "", on);
    }
}

// esconline (ini, default 1): the online subsystem answers "signed in" without Demonware. Patched in the static
// vtables (.rdata) of OnlineSubsystemDemonware's interfaces, found 2026-10-21 (docs/dev/ESCALATION-MENU.md):
// a vtable slot = position of the function or delegate in the interface's declaration (decompiled .uc order;
// PlayerInterface: line - 2), checked against the decompiled slot bodies. All are thiscall; the replacements are
// fastcall (ecx = this, edx unused) with the same stack arguments, so the callee pops them like the original.
// Only outside the story (g_onlineCtx, set by TickLanSessions from the map name): on a story map every wrapper calls
// the original Demonware function again - signed in during co-op story left the chapter 2 guest without a pawn
// (must-pass 2026-10-21).
static volatile bool g_onlineCtx = true;
static void* g_origOnline[8];   // originals, index = VtPatch.origPlus1 - 1
typedef BYTE(__thiscall* OrigB1)(void*, DWORD); typedef DWORD(__thiscall* OrigD0)(void*); typedef DWORD(__thiscall* OrigD1)(void*, DWORD);
#define ON_LOGIN(N) static BYTE __fastcall OnLogin##N(void* s, void*, DWORD a) { return g_onlineCtx ? 2 : ((OrigB1)g_origOnline[N])(s, a); }   // LS_LoggedIn
ON_LOGIN(0) ON_LOGIN(1) ON_LOGIN(2)
static DWORD __fastcall OnLink3(void* s, void*) { return g_onlineCtx ? 1 : ((OrigD0)g_origOnline[3])(s); }
static DWORD __fastcall OnNatOk4(void* s, void*) { return g_onlineCtx ? 1 : ((OrigD0)g_origOnline[4])(s); }
static DWORD __fastcall OnNatFail5(void* s, void*) { return g_onlineCtx ? 0 : ((OrigD0)g_origOnline[5])(s); }
static DWORD __fastcall OnKeyArchive6(void* s, void*, DWORD id) { return g_onlineCtx ? 1 : ((OrigD1)g_origOnline[6])(s, id); }
struct VtPatch { const char* what; DWORD vt; int slot; DWORD expect; void* fn; int origPlus1; };
// OnlineGameInterfaceDemonware primary vtable (ghidra 0x17b1018): some natives are sent to the stock UE3 LAN version of
// OnlineGameInterfaceImpl (primary vtable 0x1796870) - sessions are LAN matches (esclan), and the Demonware versions
// wait for Demonware tasks that never come (DestroyOnlineGame fired no completion, 2026-10-21). base = ghidra VA of the
// base function, resolved at patch time.
struct VtBase { const char* what; DWORD vt; int slot; DWORD expect; DWORD base; };
static const VtBase kGameBasePatches[] = {
    // DestroyOnlineGame (primary 78) and its listener list (interface table 0x152ca18 slots 9/10 = Add/Clear
    // DestroyOnlineGameCompleteDelegate): the Demonware Destroy fires no completion and its Add keeps a list of its own;
    // the base trio uses one list (object +0x4c). CreateOnlineGame stays Demonware (its LAN path works with the
    // Demonware create listeners). Redirecting the whole primary 77..91 range crashed (NULLCALL, 2026-10-21).
    { "Game.DestroyOnlineGame (LAN base)", 0x17b1018, 78, 0xb537b0, 0x4c6a50 },
    { "Game.AddDestroyComplete (LAN base)", 0x152ca18, 9, 0x9c6760, 0xb15d00 },
    { "Game.ClearDestroyComplete (LAN base)", 0x152ca18, 10, 0x9d6940, 0xa30940 },
    // GetResolvedConnectString (interface slot 24 -> primary 83): Demonware resolves the host from its own session
    // data; a member got the host's "travel to game" and never connected (2026-10-22). The LAN base uses the LAN
    // session info the member received in the reservation reply.
    { "Game.GetResolvedConnectString (LAN base)", 0x17b1018, 83, 0x874fe0, 0x44c680 },
};
static const VtPatch kOnlinePatches[] = {
    { "Player.GetLoginStatus",         0x168ec38, 15, 0xa3b7f0, (void*)OnLogin0, 1 },
    { "Player.GetPreviousLoginStatus", 0x168ec38, 16, 0xa3b7f0, (void*)OnLogin1, 2 },
    { "Player.GetSecondaryLoginStatus",0x168ec38, 19, 0xc2b230, (void*)OnLogin2, 3 },
    { "System.HasLinkConnection",      0x14c0d70,  1, 0x8a7d90, (void*)OnLink3, 4 },
    { "System.NatDiscoverySucceeded",  0x14c0d70,  2, 0x929b80, (void*)OnNatOk4, 5 },
    { "System.NatDiscoveryFailed",     0x14c0d70,  3, 0xa39940, (void*)OnNatFail5, 6 },
    { "StatsEx.HasReadKeyArchiveFor",  0x164f150, 12, 0xce9c10, (void*)OnKeyArchive6, 7 },
};
static void ApplyVtPatches(const VtPatch* list, size_t n);
extern const VtPatch* const g_chatPatchList; extern const size_t g_chatPatchCount;
static void InstallOnlinePatches() {
    static bool done = false; if (done || !g_escOnline) return; done = true;
    ApplyVtPatches(kOnlinePatches, sizeof(kOnlinePatches) / sizeof(kOnlinePatches[0]));
    ApplyVtPatches(g_chatPatchList, g_chatPatchCount);
    BYTE* base = (BYTE*)GetModuleHandleW(nullptr);
    for (const VtBase& b : kGameBasePatches) {
        VtPatch p = { b.what, b.vt, b.slot, b.expect, base + (b.base - 0x400000), 0 };
        ApplyVtPatches(&p, 1);
    }
}
static void ApplyVtPatches(const VtPatch* list, size_t n) {
    BYTE* base = (BYTE*)GetModuleHandleW(nullptr);
    for (size_t i = 0; i < n; i++) { const VtPatch& p = list[i];
        void** slot = (void**)(base + (p.vt - 0x400000)) + p.slot;
        DWORD cur = ExeVa(*slot);
        if (cur != p.expect) { Log("esconline: %s slot holds %lx, expected %lx - not patched", p.what, cur, p.expect); continue; }
        DWORD old = 0;
        if (!VirtualProtect(slot, 4, PAGE_READWRITE, &old)) { Log("esconline: %s VirtualProtect failed", p.what); continue; }
        if (p.origPlus1 > 0) g_origOnline[p.origPlus1 - 1] = *slot;
        *slot = p.fn; VirtualProtect(slot, 4, old, &old);
        Log("esconline: %s patched", p.what);
    }
}

// esclan (ini, default 1): the Demonware game interface has two paths in CreateOnlineGame (ghidra 0x6c6a20):
// internet (vtable +0x188, needs Demonware) and LAN (0x54b1f0, local). Outside the story every OnlineGameSettings
// object - the class defaults too, so new settings start with it - gets bIsLanMatch, so party and game sessions are
// created locally; Steam does the rest (SteamChat, relay). On a story map (L01..L13) the flag is cleared again:
// signed in + LAN sessions left the guest without a pawn in chapter 2 (must-pass L02a/L02b FAIL 2026-10-21, PASS with
// esclan=0 or esconline=0; excluding only TnOnlineGameSettingsCPSolo did not help). Checked once a second.
static MirrorProp g_mpLan = {}, g_mpBestHost = {};
static void TickLanSessions(const TickObjs& t) {
    char map[96]; MapName(t, map, sizeof(map));
    bool story = (map[0] == 'L' || map[0] == 'l') && map[1] >= '0' && map[1] <= '9';
    if (g_onlineCtx == story) { g_onlineCtx = !story; Log("esconline: %s on %s", story ? "story map - original online functions" : "signed-in online functions", map); }
    if (!g_escLan) return;
    static DWORD next = 0; DWORD now = GetTickCount();
    if ((LONG)(now - next) < 0) return; next = now + 1000;
    if (!MirrorPropFind("OnlineGameSettings", "bIsLanMatch", g_mpLan) || !g_mpLan.mask) return;
    MirrorPropFind("OnlineGameSettings", "AllowLobbyBestHostDeterminationHostMigration", g_mpBestHost);
    static signed char isSettings[8192];   // per class id: 1 = an OnlineGameSettings class, -1 = not
    TArr* go = GObj(); int changed = 0;
    for (int i = 0; i < go->Num; i++) {
        BYTE* o = (BYTE*)go->Data[i]; if (!o) continue;
        unsigned cid = *(unsigned*)(o + 0x14) & 0x1fff;
        if (!isSettings[cid]) { char c[80] = ""; ClassNameOf(o, c, sizeof(c)); isSettings[cid] = c[0] ? (strstr(c, "OnlineGameSettings") ? 1 : -1) : 0; }
        if (isSettings[cid] != 1) continue;
        // no "best host" migration: the host picked a member as the better host, stopped its heartbeats while the
        // migration was pending and every member lost the host at the countdown (3+ players, 2026-10-22)
        if (!story && g_mpBestHost.off > 0 && g_mpBestHost.mask) { DWORD* bw = (DWORD*)(o + g_mpBestHost.off); if (*bw & g_mpBestHost.mask) *bw &= ~g_mpBestHost.mask; }
        DWORD* w = (DWORD*)(o + g_mpLan.off);
        bool has = (*w & g_mpLan.mask) != 0;
        if (has == !story) continue;
        if (story) *w &= ~g_mpLan.mask; else *w |= g_mpLan.mask;
        if (++changed <= 12) { char nm[80] = ""; ObjNameStr(o, nm, sizeof(nm)); Log("esclan: bIsLanMatch %s on %s (%s)", story ? "cleared" : "set", nm, map); }
    }
}

// `vttrace <ghidra vtable VA> <slots> <tag> [first]` (test command): wraps slots first(1)..slots of a vtable in stubs that log
// "vttrace: <tag>[slot] this=..." (first 6 calls of each slot) and then jump to the original function - to see which
// interface functions a menu flow calls, in order. Stub: pushad; pushfd; push id; push ecx; call VtTraceHit;
// add esp,8; popfd; popad; jmp original.
static char g_vtTag[8][16]; static int g_vtTags;
static volatile LONG g_vtHits[8][128];
static void __cdecl VtTraceHit(DWORD self, DWORD id) {
    int t = (id >> 16) & 7, s = id & 0x7f;
    LONG n = InterlockedIncrement(&g_vtHits[t][s]);
    if (n <= 6) Log("vttrace: %s[%d] this=%08lx (call %ld)", g_vtTag[t], s, self, n);
}
static void VtTraceCommand(const char* args) {
    unsigned long vt = 0; int slots = 0, first = 1; char tag[16] = "";
    if (sscanf_s(args, "%lx %i %15s %i", &vt, &slots, tag, (unsigned)sizeof(tag), &first) < 3 || slots < 1 || slots > 127 || first < 1 || g_vtTags >= 8) { Log("vttrace: usage vttrace <vtVA> <slots> <tag> [first]"); return; }
    int t = g_vtTags++; strcpy_s(g_vtTag[t], tag);
    BYTE* base = (BYTE*)GetModuleHandleW(nullptr);
    void** table = (void**)(base + (vt - 0x400000));
    BYTE* code = (BYTE*)VirtualAlloc(nullptr, 32 * (slots + 1), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!code) { Log("vttrace: no memory"); return; }
    DWORD old = 0; VirtualProtect(table, 4 * (slots + 1), PAGE_READWRITE, &old);
    for (int s = first; s <= slots; s++) {
        BYTE* p = code + 32 * s; BYTE* orig = (BYTE*)table[s];
        DWORD id = ((DWORD)t << 16) | (DWORD)s;
        int k = 0;
        p[k++] = 0x60; p[k++] = 0x9c;                                            // pushad; pushfd
        p[k++] = 0x68; memcpy(p + k, &id, 4); k += 4;                            // push id
        p[k++] = 0x51;                                                           // push ecx (this)
        p[k++] = 0xe8; LONG rel = (LONG)((BYTE*)VtTraceHit - (p + k + 4)); memcpy(p + k, &rel, 4); k += 4;   // call
        p[k++] = 0x83; p[k++] = 0xc4; p[k++] = 0x08;                             // add esp, 8
        p[k++] = 0x9d; p[k++] = 0x61;                                            // popfd; popad
        p[k++] = 0xe9; rel = (LONG)(orig - (p + k + 4)); memcpy(p + k, &rel, 4); k += 4;                     // jmp orig
        table[s] = p;
    }
    VirtualProtect(table, 4 * (slots + 1), old, &old);
    Log("vttrace: %s = vtable %lx, %d slots wrapped", tag, vt, slots);
}
