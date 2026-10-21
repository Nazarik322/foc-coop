// invite.inc (2026-10-21): joining a friend's Escalation party the way the game joins an accepted invite.
// PlayerController.OnGameInviteAccepted(settings) -> GetOnlineGameSearchResultForGameInvite -> OnlineSessionJoiner:
// reservation over the party channel (SteamChat), the host answers with its session info, JoinOnlineGame (LAN),
// travel. We provide the two Demonware pieces: the invite-accepted listeners (game interface table 0x152ca18
// slot 42 add / 43 clear, 16-byte records like the chat ones) and the search result (slot 44, returns
// {GameSettings, PlatformData}). The settings object is built here: class TnOnlineGameSettingsPartyPersistentSV
// (the host's party session), ReservationChannel = the host's channel, OwningPlayerId = its owner, open slots.
// StaticConstructObject = ghidra 0x77ce70 (cdecl: Class, Outer, Name, Flags lo, Flags hi, Template, Error,
// SubobjectRoot, InstanceGraph).
struct InvRec { DWORD w[4]; };
static InvRec g_invL[8]; static int g_invN;
static DWORD g_invSettings = 0;   // object for slot 44 (0: the Demonware answer)
static void __fastcall InvAdd(void*, void*, DWORD, const DWORD* d) {
    for (int i = 0; i < g_invN; i++) if (!memcmp(g_invL[i].w, d, 16)) return;
    if (g_invN < 8) { memcpy(g_invL[g_invN++].w, d, 16); Log("invite: listener added (handler %lx)", ExeVa((void*)(UINT_PTR)d[3])); }
}
static void __fastcall InvClear(void*, void*, DWORD, const DWORD* d) {
    for (int i = 0; i < g_invN; i++) if (!memcmp(g_invL[i].w, d, 16)) { g_invL[i] = g_invL[--g_invN]; return; }
}
typedef DWORD*(__thiscall* InvResultFn)(void* self, DWORD* ret, DWORD num);
static void* g_origInvResult = nullptr;
static DWORD* __fastcall InvResult(void* self, void*, DWORD* ret, DWORD num) {
    if (g_invSettings && ret) { ret[0] = g_invSettings; ret[1] = 0; Log("invite: search result for the invite -> settings %08lx", g_invSettings); return ret; }
    return g_origInvResult ? ((InvResultFn)g_origInvResult)(self, ret, num) : ret;
}
static void InstallGameSpy(); static void InstallPeerPatch();
static void InstallFriendPatches();
static void InstallInvitePatches() {
    static bool done = false; if (done || !g_escOnline) return; done = true;
    InstallGameSpy(); InstallPeerPatch(); InstallFriendPatches();
    VtPatch list[] = {
        { "Game.AddGameInviteAccepted",   0x152ca18, 42, 0xa07f60, (void*)InvAdd, 0 },
        { "Game.ClearGameInviteAccepted", 0x152ca18, 43, 0x5d7550, (void*)InvClear, 0 },
    };
    ApplyVtPatches(list, 2);
    void** slot = (void**)((BYTE*)GetModuleHandleW(nullptr) + (0x152ca18 - 0x400000)) + 44;
    if (ExeVa(*slot) != 0xb74f60) { Log("invite: search result slot holds %lx - not patched", ExeVa(*slot)); return; }
    g_origInvResult = *slot;
    DWORD old = 0; VirtualProtect(slot, 4, PAGE_READWRITE, &old); *slot = (void*)InvResult; VirtualProtect(slot, 4, old, &old);
    Log("invite: Game.GetOnlineGameSearchResultForGameInvite patched");
}

typedef BYTE*(__cdecl* ConstructFn)(BYTE* cls, BYTE* outer, DWORD name, DWORD flagsLo, DWORD flagsHi, BYTE* tmpl, void* err, BYTE* root, void* graph);
static BYTE* ConstructByClassName(const char* cls) {
    unsigned cid = ClassIdByName(cls);
    BYTE* c = cid ? g_classObjById[cid] : nullptr;
    if (!c) { Log("invite: class %s not loaded", cls); return nullptr; }
    ConstructFn fn = (ConstructFn)((BYTE*)GetModuleHandleW(nullptr) + (0x77ce70 - 0x400000));
    BYTE* o = nullptr;
    __try { o = fn(c, nullptr, 0, 0, 0, nullptr, nullptr, nullptr, nullptr); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("invite: StaticConstructObject fault"); return nullptr; }
    char nm[80] = "?", cn[80] = "?"; if (IsObjFast(o)) { ObjNameStr(o, nm, sizeof(nm)); ClassNameOf(o, cn, sizeof(cn)); }
    Log("invite: constructed %s (%s) at %p", nm, cn, o);
    return IsObjFast(o) ? o : nullptr;
}
static MirrorProp g_mpResChan = {}, g_mpOwnerId = {}, g_mpNumPub = {}, g_mpNumPriv = {}, g_mpOpenPub = {}, g_mpOpenPriv = {};
typedef void(__thiscall* InvFireFn)(DWORD self, DWORD settings);
// Accept an invite into the party whose channel is `ch` (owner = ChanOwner(ch)).
static bool InviteJoinChannel(unsigned long long ch) {
    unsigned long long owner = ChanOwner(ch);
    if (!g_invN) { Log("invite: nobody listens for accepted invites (not in the front end / lobby?)"); return false; }
    BYTE* s = ConstructByClassName("TnOnlineGameSettingsPartyPersistentSV");
    if (!s) return false;
    if (MirrorPropFind("OnlineGameSettings", "ReservationChannel", g_mpResChan)) *(unsigned long long*)(s + g_mpResChan.off) = ch;
    if (MirrorPropFind("OnlineGameSettings", "OwningPlayerId", g_mpOwnerId)) *(unsigned long long*)(s + g_mpOwnerId.off) = owner;
    if (MirrorPropFind("OnlineGameSettings", "NumPublicConnections", g_mpNumPub)) *(int*)(s + g_mpNumPub.off) = 4;
    if (MirrorPropFind("OnlineGameSettings", "NumPrivateConnections", g_mpNumPriv)) *(int*)(s + g_mpNumPriv.off) = 4;
    if (MirrorPropFind("OnlineGameSettings", "NumOpenPublicConnections", g_mpOpenPub)) *(int*)(s + g_mpOpenPub.off) = 3;
    if (MirrorPropFind("OnlineGameSettings", "NumOpenPrivateConnections", g_mpOpenPriv)) *(int*)(s + g_mpOpenPriv.off) = 3;
    if (g_mpLan.off && g_mpLan.mask) *(DWORD*)(s + g_mpLan.off) |= g_mpLan.mask;
    g_invSettings = (DWORD)(UINT_PTR)s;
    Log("invite: accepting party %016llx of %llu, %d listener(s)", ch, owner, g_invN);
    InvRec l[8]; int n = g_invN; memcpy(l, g_invL, sizeof(InvRec) * n);
    for (int i = 0; i < n; i++) {
        // object arguments of interface and delegate calls are native pointers (object + 0x10; the game's own
        // CreateOnlineGame wrapper subtracts 0x10 before the primary vtable call)
        __try { ((InvFireFn)(UINT_PTR)l[i].w[3])(l[i].w[2], g_invSettings + 0x10); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("invite: listener fault"); }
    }
    return true;
}
// `escjoin <channel hex>` (test command): accept an invite into that party channel.
static void EscJoinCommand(const char* args) {
    unsigned long long ch = _strtoui64(args, nullptr, 16);
    if (!ch) { Log("escjoin: usage escjoin <channel hex>"); return; }
    InviteJoinChannel(ch);
}

// Diagnostics (2026-10-21): GetGameSettings (slot 6, thiscall(name) -> settings native pointer or 0x10) and
// JoinOnlineGame (slot 20, thiscall(num, name, result*) -> primary slot 82) log their session names and settings classes.
typedef DWORD(__thiscall* GetSettingsFn)(void* self, DWORD name);
typedef DWORD(__thiscall* JoinFn)(void* self, DWORD num, DWORD name, DWORD* result);
static void* g_origGetSettings = nullptr; static void* g_origJoin = nullptr;
static void SpyName(DWORD packed, char* out, size_t n) { if (!NameAt((int)(packed >> 14), out, n)) _snprintf_s(out, n, _TRUNCATE, "?%lx", packed); }
static void SpyObj(DWORD native, char* out, size_t n) {
    BYTE* o = native > 0x10 ? (BYTE*)(UINT_PTR)(native - 0x10) : nullptr; char nm[64] = "-", cn[64] = "";
    if (IsObjFast(o)) { ObjNameStr(o, nm, sizeof(nm)); ClassNameOf(o, cn, sizeof(cn)); }
    _snprintf_s(out, n, _TRUNCATE, "%s (%s)", nm, cn);
}
static DWORD __fastcall SpyGetSettings(void* self, void*, DWORD name) {
    DWORD r = ((GetSettingsFn)g_origGetSettings)(self, name);
    static DWORD lastName, lastR; static DWORD lastAt;
    if (name != lastName || r != lastR || GetTickCount() - lastAt > 10000) {
        lastName = name; lastR = r; lastAt = GetTickCount();
        char nm[64], ob[160]; SpyName(name, nm, sizeof(nm)); SpyObj(r, ob, sizeof(ob));
        Log("gamespy: GetGameSettings(%s) -> %s", nm, ob);
    }
    return r;
}
static DWORD __fastcall SpyJoin(void* self, void*, DWORD num, DWORD name, DWORD* result) {
    char nm[64], ob[160] = "?"; SpyName(name, nm, sizeof(nm));
    if (result) { BYTE* o = (BYTE*)(UINT_PTR)result[0]; char a[64] = "-", c[64] = ""; if (IsObjFast(o)) { ObjNameStr(o, a, sizeof(a)); ClassNameOf(o, c, sizeof(c)); } _snprintf_s(ob, _TRUNCATE, "%s (%s) platform %08lx", a, c, result[1]); }
    Log("gamespy: JoinOnlineGame(%lu, %s, %s)", num, nm, ob);
    // Joining a party from an invite: the session joiner rebuilds the result from the host's reply with the class of
    // its own search (TnOnlineGameSettingsBase), and the lobby then takes the game-lobby path and crashes
    // (GetGameSettings('Game') none, 2026-10-21). The party needs TnOnlineGameSettingsPartyPersistent: use the object
    // made for the invite, keep the host's session info (result[1]).
    if (result && g_invSettings && !strcmp(nm, "Party")) {
        char c[64] = ""; BYTE* o = (BYTE*)(UINT_PTR)result[0]; if (IsObjFast(o)) ClassNameOf(o, c, sizeof(c));
        if (!strstr(c, "PartyPersistent")) { result[0] = g_invSettings; Log("invite: party join uses the invite settings %08lx instead of %s", g_invSettings, c); }
    }
    DWORD r = ((JoinFn)g_origJoin)(self, num, name, result);
    Log("gamespy: JoinOnlineGame -> %lu", r);
    return r;
}
typedef DWORD(__thiscall* RegFn)(void* self, DWORD name, DWORD* reg, DWORD invited, DWORD peer);
static void* g_origReg = nullptr;
// RegisterPlayer (interface slot 25): Demonware adds the registrant to the named session only when 0x4dba20 does not
// find it in another list (the beacon's peers already hold it), so the host's Party session stayed empty and the
// lobby showed nobody (2026-10-21). After the original: a member missing from the session is appended like the game
// does (TArray add 0x689fc0 (count, element size) on the registrants array, copy-construct 0x6af210 (src)).
typedef int(__thiscall* ArrAddFn)(void* arr, int n, int elemSize);
typedef void(__thiscall* RegCopyFn)(void* dst, const void* src);
static void EnsureRegistrant(DWORD name, const DWORD* reg) {
    BYTE* os = OnlineSubObject(); if (!os || !reg) return;
    unsigned long long id = *(const unsigned long long*)reg; if (!id) return;
    BYTE* base = (BYTE*)GetModuleHandleW(nullptr);
    __try {
        BYTE* arr = *(BYTE**)(os + 0xec); int n = *(int*)(os + 0xf0);
        for (int i = 0; i < n; i++) {
            BYTE* e = arr + 0x24 * i; if (*(DWORD*)e != name) continue;
            BYTE* regs = *(BYTE**)(e + 0xc); int nr = *(int*)(e + 0x10);
            for (int r = 0; r < nr; r++) if (*(unsigned long long*)(regs + 0x60 * r) == id) { Log("invite: %llu already in the session (%d member(s))", id, nr); return; }
            int idx = ((ArrAddFn)(base + (0x689fc0 - 0x400000)))(e + 0xc, 1, 0x60);
            BYTE* slot = *(BYTE**)(e + 0xc) + 0x60 * idx;
            ((RegCopyFn)(base + (0x6af210 - 0x400000)))(slot, reg);
            char nm[64]; SpyName(name, nm, sizeof(nm));
            Log("invite: %llu added to session %s (%d member(s) now)", id, nm, *(int*)(e + 0x10));
            return;
        }
        Log("invite: no session for the RegisterPlayer name (%d session(s))", n);
    } __except (EXCEPTION_EXECUTE_HANDLER) { Log("invite: adding a session member faulted"); }
}
static DWORD __fastcall SpyRegister(void* self, void*, DWORD name, DWORD* reg, DWORD invited, DWORD peer) {
    char nm[64]; SpyName(name, nm, sizeof(nm));
    unsigned long long id = reg ? *(unsigned long long*)reg : 0;
    DWORD r = ((RegFn)g_origReg)(self, name, reg, invited, peer);
    Log("gamespy: RegisterPlayer(%s, %llu, invited %lu, peer %lu) -> %lu", nm, id, invited, peer, r);
    if (g_onlineCtx) EnsureRegistrant(name, reg);
    return r;
}
// CreateOnlineGame (interface slot 1, thiscall(num, name, settings native pointer)): Demonware fills the new session's
// OwningPlayerId and the host's own registrant from its login, 0 for us; the lobby then found a member a "better
// host" than the owner 0, migrated, and the member was dropped (2026-10-21). After the original, sessions we create
// carry our Steam ID as owner and for any registrant with id 0.
typedef DWORD(__thiscall* CreateFn)(void* self, DWORD num, DWORD name, DWORD settings);
static void* g_origCreate = nullptr;
static MirrorProp g_mpOwner2 = {};
static DWORD __fastcall SnCreateGame(void* self, void*, DWORD num, DWORD name, DWORD settings) {
    DWORD r = ((CreateFn)g_origCreate)(self, num, name, settings);
    if (!g_onlineCtx || !g_snMe) return r;
    char nm[64]; SpyName(name, nm, sizeof(nm));
    BYTE* so = settings > 0x10 ? (BYTE*)(UINT_PTR)(settings - 0x10) : nullptr;
    if (IsObjFast(so) && MirrorPropFind("OnlineGameSettings", "OwningPlayerId", g_mpOwner2)) {
        unsigned long long* o = (unsigned long long*)(so + g_mpOwner2.off);
        if (*o == 0) { *o = g_snMe; Log("invite: session %s owner 0 -> %llu", nm, g_snMe); }
    }
    BYTE* os = OnlineSubObject();
    __try {
        BYTE* arr = *(BYTE**)(os + 0xec); int n = *(int*)(os + 0xf0);
        for (int i = 0; i < n; i++) {
            BYTE* e = arr + 0x24 * i; if (*(DWORD*)e != name) continue;
            BYTE* regs = *(BYTE**)(e + 0xc); int nr = *(int*)(e + 0x10);
            for (int k = 0; k < nr; k++) { unsigned long long* id = (unsigned long long*)(regs + 0x60 * k); if (*id == 0) { *id = g_snMe; Log("invite: session %s registrant 0 -> %llu", nm, g_snMe); } }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return r;
}
typedef DWORD(__thiscall* ResolveFn)(void* self, DWORD name, FStr* out);
static void* g_origResolve = nullptr;
static DWORD __fastcall SpyResolve(void* self, void*, DWORD name, FStr* out) {
    DWORD r = ((ResolveFn)g_origResolve)(self, name, out);
    g_resolvedAt = GetTickCount();
    char nm[64]; SpyName(name, nm, sizeof(nm));
    Log("gamespy: GetResolvedConnectString(%s) -> %lu '%ls'", nm, r, (out && out->Num > 0 && out->Data) ? out->Data : L"");
    return r;
}
static void InstallGameSpy() {
    static bool done = false; if (done) return; done = true;
    void** t = (void**)((BYTE*)GetModuleHandleW(nullptr) + (0x152ca18 - 0x400000));
    DWORD old = 0; VirtualProtect(t, 4 * 32, PAGE_READWRITE, &old);
    if (ExeVa(t[6]) == 0xd36020) { g_origGetSettings = t[6]; t[6] = (void*)SpyGetSettings; }
    if (ExeVa(t[20]) == 0xb9ed90) { g_origJoin = t[20]; t[20] = (void*)SpyJoin; }
    if (ExeVa(t[25]) == 0x8c2750) { g_origReg = t[25]; t[25] = (void*)SpyRegister; }
    if (ExeVa(t[24]) == 0x8084b0) { g_origResolve = t[24]; t[24] = (void*)SpyResolve; }
    if (ExeVa(t[1]) == 0x676400) {
        DWORD o1 = 0; VirtualProtect(t + 1, 4, PAGE_READWRITE, &o1); g_origCreate = t[1]; t[1] = (void*)SnCreateGame; VirtualProtect(t + 1, 4, o1, &o1);
    }
    VirtualProtect(t, 4 * 32, old, &old);
    Log("gamespy: GetGameSettings %s, JoinOnlineGame %s", g_origGetSettings ? "wrapped" : "not wrapped", g_origJoin ? "wrapped" : "not wrapped");
}

// Host crash when a member joins the party (2026-10-21): the beacon hands the member's registrant data to the
// subsystem (primary vtable 0x17b11c8 slot 85 = 0xa1a0d0, thiscall(a, registrant*)), which first does the generic
// part (0xd259b0, same arguments) and then copies the member's Demonware network address (registrant +0x24, null for
// us) -> access violation in a rep movsd. Replacement: the generic part only.
typedef void(__thiscall* PeerFn)(void* self, DWORD a, DWORD reg);
static void __fastcall SafeRegisterPeer(void* self, void*, DWORD a, DWORD reg) {
    PeerFn generic = (PeerFn)((BYTE*)GetModuleHandleW(nullptr) + (0xd259b0 - 0x400000));
    generic(self, a, reg);
}
static void InstallPeerPatch() {
    static bool done = false; if (done || !g_escOnline) return; done = true;
    VtPatch p = { "Subsystem.RegisterPeer (no Demonware address)", 0x17b11c8, 85, 0xa1a0d0, (void*)SafeRegisterPeer, 0 };
    ApplyVtPatches(&p, 1);
}

// `sessions` (test command): the subsystem's named sessions (array at subsystem +0xec/+0xf0, 0x24 bytes each:
// name, session info, settings, registrants data/num at +0xc/+0x10, 0x60 bytes each, PlayerNetId first).
static void SessionsCommand() {
    BYTE* os = OnlineSubObject(); if (!os) { Log("sessions: no subsystem"); return; }
    __try {
        BYTE* arr = *(BYTE**)(os + 0xec); int n = *(int*)(os + 0xf0);
        Log("sessions: %d session(s)", n);
        for (int i = 0; i < n && i < 8; i++) {
            BYTE* e = arr + 0x24 * i; char nm[64]; SpyName(*(DWORD*)e, nm, sizeof(nm));
            BYTE* gs = *(BYTE**)(e + 8); char cn[64] = "-"; if (IsObjFast(gs)) ClassNameOf(gs, cn, sizeof(cn));
            BYTE* regs = *(BYTE**)(e + 0xc); int nr = *(int*)(e + 0x10);
            char line[600]; size_t k = _snprintf_s(line, _TRUNCATE, "sessions: %s settings %s info %p, %d member(s):", nm, cn, *(void**)(e + 4), nr);
            for (int r = 0; r < nr && r < 8; r++) k += _snprintf_s(line + k, sizeof(line) - k, _TRUNCATE, " %llu", *(unsigned long long*)(regs + 0x60 * r));
            Log("%s", line);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { Log("sessions: fault"); }
    for (int i = 0; i < g_to.npc; i++) {
        BYTE* pc = g_to.pc[i]; if (!IsObjFast(pc)) continue;
        BYTE* pri = *(BYTE**)(pc + 0x1d4); char pn[64] = "-", rn[64] = "-";
        ObjNameStr(pc, pn, sizeof(pn)); if (IsObjFast(pri)) ObjNameStr(pri, rn, sizeof(rn));
        Log("sessions: pc %s local=%d pri %s id %llu", pn, (int)IsLocalPC(pc), rn, IsObjFast(pri) ? *(unsigned long long*)(pri + 0x230) : 0ull);
    }
}

// `escinvite <steam id>` (test command): a Steam lobby invite to our party (steamnet.inc SnPartyChannel lobby).
static void EscInviteCommand(const char* args) {
    unsigned long long id = _strtoui64(args, nullptr, 10);
    if (!id || !g_steamLobby) { Log("escinvite: need a Steam id and our party lobby (lobby %llu)", g_steamLobby); return; }
    char c[64]; _snprintf_s(c, _TRUNCATE, "invite %llu", id); SnCmd(c); Log("escinvite: invited %llu to lobby %llu", id, g_steamLobby);
}

// ---------------------------------------------------------------- Steam friends in the game's ONLINE FRIENDS screen
// The screen shows UIDataProvider_OnlineFriends.FriendsList (TArray<OnlineFriend>, 0x3c bytes each: UniqueId +0,
// SessionId +8, UniqueName +0x10, DisplayName +0x1c, PresenceInfo +0x28 (FStrings), FriendState +0x34, bits +0x38:
// 1 online, 2 playing, 4 playing this game, 8 joinable, 0x40 has invited you). Without Demonware it stays empty: it
// is filled from the relay's Steam friends ("friends" -> "friend <id> <state> <lobby> <name>" ... "friendsend").
struct SnFriend { unsigned long long id, lobby; int state; char name[64]; };
static SnFriend g_friends[64], g_friendsNew[64]; static int g_nFriends = 0, g_nFriendsNew = 0;
static MirrorProp g_mpFriendsList = {};
static void FriendsEvent(const char* e) {
    unsigned long long id = 0, lobby = 0; int st = 0; char name[64] = "";
    if (sscanf_s(e, "friend %llu %d %llu %63[^\n]", &id, &st, &lobby, name, (unsigned)sizeof(name)) >= 3) {
        if (g_nFriendsNew < 64) { SnFriend& f = g_friendsNew[g_nFriendsNew++]; f.id = id; f.lobby = lobby; f.state = st; strncpy_s(f.name, name, _TRUNCATE); }
    } else if (!strcmp(e, "friendsend")) {
        memcpy(g_friends, g_friendsNew, sizeof(SnFriend) * g_nFriendsNew); g_nFriends = g_nFriendsNew; g_nFriendsNew = 0;
    }
}
struct FriendArr { BYTE* Data; int Num; int Max; };
static void FillFriendArray(FriendArr* a) {
    int n = g_nFriends;
    if (a->Max < n) { void* d = GameRealloc(a->Data, n * 0x3c); if (!d) return; memset((BYTE*)d + a->Max * 0x3c, 0, (n - a->Max) * 0x3c); a->Data = (BYTE*)d; a->Max = n; }
    for (int k = 0; k < n; k++) {
        BYTE* e = a->Data + 0x3c * k; const SnFriend& f = g_friends[k];
        *(unsigned long long*)e = f.id; *(unsigned long long*)(e + 8) = 0;
        wchar_t w[64]; _snwprintf_s(w, _TRUNCATE, L"%llu", f.id); SetFStr(e + 0x10, w);
        MultiByteToWideChar(CP_UTF8, 0, f.name, -1, w, 64); SetFStr(e + 0x1c, w);
        bool inGame = f.lobby != 0, invited = false;
        for (int j = 0; j < g_nInvitedBy; j++) if (g_invitedBy[j] == f.id) invited = true;
        SetFStr(e + 0x28, inGame ? L"Fall of Cybertron co-op" : (f.state ? L"Online" : L"Offline"));
        *(BYTE*)(e + 0x34) = (BYTE)(f.state ? 1 : 0);
        *(DWORD*)(e + 0x38) = (f.state ? 1 : 0) | (inGame ? (2 | 4 | 8) : 0) | (invited ? 0x40 : 0);
    }
    a->Num = n;
}
// PlayerInterface slot 51 GetFriendsList(byte, out array<OnlineFriend>, count, start) -> EOnlineEnumerationReadState
// (2 = done): the menus look friends up here before Join Game (Demonware: empty -> "Join Game Failed").
typedef BYTE(__thiscall* GetFriendsFn)(void* self, DWORD num, FriendArr* out, DWORD count, DWORD start);
static void* g_origGetFriends = nullptr;
static BYTE __fastcall SnGetFriendsList(void* self, void*, DWORD num, FriendArr* out, DWORD count, DWORD start) {
    if (!g_onlineCtx || !out) return ((GetFriendsFn)g_origGetFriends)(self, num, out, count, start);
    FillFriendArray(out);
    return 2;
}
static void FillFriendsProvider() {
    static unsigned cid = 0; if (!cid) cid = ClassIdByName("UIDataProvider_OnlineFriends");
    if (!cid || !MirrorPropFind("UIDataProvider_OnlineFriends", "FriendsList", g_mpFriendsList)) return;
    TArr* go = GObj();
    for (int i = 0; i < go->Num; i++) {
        BYTE* o = (BYTE*)go->Data[i];
        if (!o || (*(unsigned*)(o + 0x14) & 0x1fff) != cid) continue;
        char nm[80]; if (!ObjNameStr(o, nm, sizeof(nm)) || !strncmp(nm, "Default__", 9)) continue;
        FillFriendArray((FriendArr*)(o + g_mpFriendsList.off));
    }
}
static void TickFriends() {
    static DWORD next = 0; DWORD now = GetTickCount();
    if (!g_onlineCtx || !g_snPort || (LONG)(now - next) < 0) return;
    next = now + 15000;
    SnCmd("friends");
    FillFriendsProvider();
}

// PlayerInterface slot 70 JoinFriendGame(byte, UniqueNetId* friend) -> bool (Demonware 0xa36d80 behind a this-adjust
// thunk 0x515e00): the friend's Steam lobby (friends list "lobby") is joined through the relay; the lobby data names
// the party channel and the join continues like an accepted invite (steamnet.inc SnSteamLobbyEvent).
typedef DWORD(__thiscall* JoinFriendFn)(void* self, DWORD num, const unsigned long long* id);
static void* g_origJoinFriend = nullptr;
static DWORD __fastcall SnJoinFriendGame(void* self, void*, DWORD num, const unsigned long long* id) {
    if (!g_onlineCtx || !id) return ((JoinFriendFn)g_origJoinFriend)(self, num, id);
    for (int k = 0; k < g_nFriends; k++) if (g_friends[k].id == *id) {
        unsigned long long lobby = g_friends[k].lobby;
        if (lobby <= 1) { Log("invite: friend %llu is not in a party lobby", *id); return 0; }
        g_joinViaLobby = true; char c[64]; _snprintf_s(c, _TRUNCATE, "join %llu", lobby); SnCmd(c);
        Log("invite: joining friend %llu through Steam lobby %llu", *id, lobby);
        return 1;
    }
    Log("invite: JoinFriendGame: %llu is not in the friends list", *id);
    return 0;
}
static void InstallFriendPatches() {
    {
        void** t = (void**)((BYTE*)GetModuleHandleW(nullptr) + (0x168ec38 - 0x400000));
        if (ExeVa(t[70]) == 0x515e00) {
            DWORD o = 0; VirtualProtect(t + 70, 4, PAGE_READWRITE, &o); g_origJoinFriend = t[70]; t[70] = (void*)SnJoinFriendGame; VirtualProtect(t + 70, 4, o, &o);
            Log("invite: Player.JoinFriendGame patched (Steam lobby)");
        }
    }
    void** t = (void**)((BYTE*)GetModuleHandleW(nullptr) + (0x168ec38 - 0x400000));
    if (ExeVa(t[51]) != 0x9ab810) { Log("invite: GetFriendsList slot holds %lx - not patched", ExeVa(t[51])); return; }
    DWORD old = 0; VirtualProtect(t + 51, 4, PAGE_READWRITE, &old); g_origGetFriends = t[51]; t[51] = (void*)SnGetFriendsList; VirtualProtect(t + 51, 4, old, &old);
    Log("invite: Player.GetFriendsList patched (Steam friends)");
}
