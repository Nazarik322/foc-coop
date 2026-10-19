// Game-thread tick. Included from dinput8.cpp before overlay.inc (shares Log / Rd / TArr / ObjNameStr and the flags).
//
// GameTickSafe() runs on the game thread: TickThread (overlay.inc) posts kTickMsg to the game window and GetMsgProc calls
// it. Work that reads or writes engine objects belongs here, not in background threads: the engine changes these
// objects on its own thread (garbage collection, controller and actor lists), so access from other threads races it.
// One pass over GObjects per tick sorts the instances by class id; there is no VirtualQuery per object.

enum : BYTE { K_NONE = 0, K_PC, K_PAWN, K_GAME, K_WORLD, K_LOCALPLAYER, K_SPAWNHELPER, K_UWORLD };

struct TickObjs {
    BYTE* pc[16]; int npc;
    BYTE* pawn[48]; int npawn;      // TnPlayerPawn instances, possessed or not (story levels keep a pool of them)
    BYTE* game[4]; int ngame;       // GameInfo instances (host / standalone only)
    BYTE* world[16]; int nworld;
    BYTE* lp[4]; int nlp;           // TnLocalPlayer instances
    BYTE* helper[8]; int nhelper;   // TnPlayerSpawnHelper* instances
    BYTE* uworld[4]; int nuworld;   // UWorld objects ("TheWorld", name number 0); one has the map URL
};
static TickObjs g_to;
static volatile LONG g_tickCount = 0;
static double g_tickMsLast = 0, g_tickMsMax = 0;
static signed char g_spawnState[16];   // per g_to.pc[i]: spawn helper GameState 0..3, or -1 when unknown

static TArr* GObj() { static BYTE* base = (BYTE*)GetModuleHandleW(nullptr); return (TArr*)(base + kGObjRva); }

// A valid UObject pointer: its object index (+0x18) points back at it in GObjects.
static bool IsObjFast(const void* p) {
    BYTE* o = (BYTE*)p;
    if ((UINT_PTR)o < 0x10000 || ((UINT_PTR)o & 3)) return false;
    TArr* go = GObj();
    __try {
        unsigned idx = *(unsigned*)(o + 0x18) & 0x3ffff;
        return idx >= 1 && idx <= (unsigned)go->Num && (BYTE*)go->Data[idx - 1] == o;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static BYTE KindOf(BYTE* o) { return g_kindById[*(unsigned*)(o + 0x14) & 0x1fff]; }

static BYTE ClassKind(const char* n) {
    size_t L = strlen(n);
    if (!strncmp(n, "TnPlayerController", 18)) return K_PC;
    if (!strncmp(n, "TnPlayerPawn", 12)) return K_PAWN;
    if (!strncmp(n, "TnPlayerSpawnHelper", 19)) return K_SPAWNHELPER;
    if (!strcmp(n, "TnLocalPlayer")) return K_LOCALPLAYER;
    if (!strcmp(n, "TnWorldInfo") || !strcmp(n, "WorldInfo")) return K_WORLD;
    if (!strcmp(n, "World")) return K_UWORLD;
    if (!strncmp(n, "Tn", 2) && strcmp(n, "TnGame") != 0 &&
        ((L > 4 && !strcmp(n + L - 4, "Game")) || (L > 8 && !strcmp(n + L - 8, "GameSolo")))) return K_GAME;
    return K_NONE;
}

static void RegisterClassObj(BYTE* cls) {
    if (*(unsigned*)(cls + 4) & 0x3fff) return;   // class objects carry name number 0
    unsigned own = *(unsigned*)(cls + 0x84);
    if (own == 0 || own >= 8191 || g_classObjById[own + 1] == cls) return;
    char nm[80];
    if (!ObjNameStr(cls, nm, sizeof(nm))) return;
    g_classObjById[own + 1] = cls;
    g_kindById[own + 1] = ClassKind(nm);
}

static void TickCollect(TArr* go, TickObjs& t) {
    int n = go->Num;
    for (int i = 0; i < n; i++) {
        BYTE* obj = (BYTE*)go->Data[i];
        if (!obj) continue;
        unsigned cid = *(unsigned*)(obj + 0x14) & 0x1fff;
        if (cid == 3) { RegisterClassObj(obj); continue; }
        BYTE k = g_kindById[cid];
        if (!k) continue;
        if (k != K_UWORLD && !(*(unsigned*)(obj + 4) & 0x3fff)) continue;   // name number 0: Default__ objects, archetypes
        switch (k) {
        case K_PC: if (t.npc < 16) t.pc[t.npc++] = obj; break;
        case K_PAWN: if (t.npawn < 48) t.pawn[t.npawn++] = obj; break;
        case K_GAME: if (t.ngame < 4) t.game[t.ngame++] = obj; break;
        case K_WORLD: if (t.nworld < 16) t.world[t.nworld++] = obj; break;
        case K_LOCALPLAYER: if (t.nlp < 4) t.lp[t.nlp++] = obj; break;
        case K_SPAWNHELPER: if (t.nhelper < 8) t.helper[t.nhelper++] = obj; break;
        case K_UWORLD: if (t.nuworld < 4) t.uworld[t.nuworld++] = obj; break;
        }
    }
}

static bool InList(const BYTE* p, BYTE* const* list, int n) { for (int i = 0; i < n; i++) if (list[i] == p) return true; return false; }
// PlayerController: +0x1d0 Pawn, +0x1d4 PlayerReplicationInfo, +0x220 StartSpot, +0x344 Player (TnLocalPlayer = local).
static bool IsLocalPC(BYTE* pc) { BYTE* p = *(BYTE**)(pc + 0x344); return IsObjFast(p) && KindOf(p) == K_LOCALPLAYER; }
static BYTE* PawnOf(BYTE* pc) { BYTE* p = *(BYTE**)(pc + 0x1d0); return IsObjFast(p) ? p : nullptr; }
static float* LocOf(BYTE* actor) { return (float*)(actor + 0xe4); }   // Actor.Location (x, y, z)

// ---------------------------------------------------------------- forcechar: TnLocalPlayer._CharactersLoaded
// TnLocalPlayer layout (diffing the instance against Default__TnLocalPlayer and the script variable order):
//   +0x2c0 CharacterCustomizationFilename (FString), +0x2cc/0x2d0/0x2d4 _CustomCharacterData, _PlayerFileHandler,
//   _XpManager, +0x2d8 bool bit field (bit0 _HasBypassedIISOnce, bit1 _CharactersLoaded, ...),
//   +0x2dc CharacterLoadCompleteFunctionName (FString, 0x26 chars incl. terminator).
static void TickForceChar(const TickObjs& t) {
    if (!g_forceCharFlag) return;
    for (int i = 0; i < t.nlp; i++) {
        BYTE* obj = t.lp[i];
        if (*(int*)(obj + 0x2e0) != 0x26 || *(int*)(obj + 0x2e4) != 0x26) continue;   // layout sanity check
        DWORD* flags = (DWORD*)(obj + 0x2d8);
        if (!(*flags & 2)) { *flags |= 2; Log("forcechar: %p _CharactersLoaded set (flags now %x)", obj, *flags); }
    }
}

// ---------------------------------------------------------------- maxplayers: GameInfo +0x230 MaxPlayers, +0x234 MaxPlayersAllowed
static void TickMaxPlayers(const TickObjs& t) {
    if (g_maxPlayers <= 0) return;
    for (int i = 0; i < t.ngame; i++) {
        int* mp = (int*)(t.game[i] + 0x230); int* mpa = (int*)(t.game[i] + 0x234);
        if (*mpa < 1 || *mpa > 64 || *mp < 0 || *mp > 64) continue;   // sanity
        if (*mp >= g_maxPlayers) continue;
        char nm[80]; unsigned num = 0; ObjNameStr(t.game[i], nm, sizeof(nm), &num);
        Log("maxplayers: %s_%u at %p: MaxPlayers %d (allowed %d) -> %d", nm, num - 1, t.game[i], *mp, *mpa, g_maxPlayers);
        *mp = g_maxPlayers;
        if (*mpa < g_maxPlayers) *mpa = g_maxPlayers;
    }
}

// ---------------------------------------------------------------- ctrlshare
// Remote PlayerControllers on the host never get their locally-created manager objects (input override, profile,
// achievements, ...). Native campaign code dereferences them (null+0x10 "this"). Copy valid object pointers from the
// local controller into the null slots (+0x500..+0x740) of every other controller.
static void TickCtrlShare(const TickObjs& t) {
    if (!g_ctrlShare || t.npc < 2) return;
    int best = -1;
    for (int a = 0; a < t.npc && best < 0; a++) if (IsLocalPC(t.pc[a])) best = a;
    if (best < 0) {   // no local controller: take the one with the most object pointers
        int bestN = -1;
        for (int a = 0; a < t.npc; a++) { int n = 0; for (int off = 0x500; off < 0x740; off += 4) if (IsObjFast(*(BYTE**)(t.pc[a] + off))) n++; if (n > bestN) { bestN = n; best = a; } }
    }
    for (int a = 0; a < t.npc; a++) {
        if (a == best) continue;
        int copied = 0;
        for (int off = 0x500; off < 0x740; off += 4) {
            BYTE** dst = (BYTE**)(t.pc[a] + off); BYTE* src = *(BYTE**)(t.pc[best] + off);
            if (*dst != nullptr || !IsObjFast(src)) continue;
            char n2[80]; unsigned nn = 0; ObjNameStr(src, n2, sizeof(n2), &nn);
            if (g_ctrlSkipMask && strstr(n2, g_ctrlSkipMask)) { static volatile LONG ns = 0; if (InterlockedIncrement(&ns) <= 6) Log("ctrlshare: skipping +%x %s_%u", off, n2, nn - 1); continue; }
            static volatile LONG nc = 0; if (InterlockedIncrement(&nc) <= 40) Log("ctrlshare: +%x <- %s_%u", off, n2, nn - 1);
            *dst = src; copied++;
        }
        if (copied) { static volatile LONG nl = 0; if (InterlockedIncrement(&nl) <= 20) Log("ctrlshare: %p <- %p copied %d null object slots", t.pc[a], t.pc[best], copied); }
    }
}

// ---------------------------------------------------------------- hostfirst
// Kismet "Player" = first PlayerController in WorldInfo.ControllerList, which is the newest one (a guest). Put the
// local controller (host) at the head: list = host -> others. Controller.NextController is at +0x1dc.
static BYTE* g_hfWorld = nullptr; static int g_hfOff = -1;
static void TickHostFirst(const TickObjs& t) {
    if (!g_hostFirst || t.npc < 2) return;
    BYTE* host = nullptr;
    for (int a = 0; a < t.npc && !host; a++) if (IsLocalPC(t.pc[a])) host = t.pc[a];
    if (!host) return;
    if (g_hfWorld && !InList(g_hfWorld, t.world, t.nworld)) { g_hfWorld = nullptr; g_hfOff = -1; }
    if (!g_hfWorld) {
        static DWORD lastSearch = 0;
        if (GetTickCount() - lastSearch < 2000) return;
        lastSearch = GetTickCount();
        for (int w = 0; w < t.nworld && !g_hfWorld; w++) {
            BYTE* wi = t.world[w];
            if (!Rd(wi, 0xc00)) continue;
            for (int off = 0x100; off < 0xc00 && !g_hfWorld; off += 4) {
                BYTE* v = *(BYTE**)(wi + off);
                if (!IsObjFast(v) || KindOf(v) != K_PC) continue;
                int n = 0;
                for (BYTE* c = v; c && n < 64 && IsObjFast(c); c = *(BYTE**)(c + 0x1dc), n++) if (c == host) { g_hfWorld = wi; g_hfOff = off; break; }
            }
        }
        if (g_hfWorld) Log("hostfirst: ControllerList is WorldInfo %p +0x%x", g_hfWorld, g_hfOff);
        if (!g_hfWorld) return;
    }
    BYTE* head = *(BYTE**)(g_hfWorld + g_hfOff);
    if (head == host) return;
    BYTE* pred = nullptr; int n = 0;
    for (BYTE* c = head; c && n < 4096 && IsObjFast(c); c = *(BYTE**)(c + 0x1dc), n++) if (*(BYTE**)(c + 0x1dc) == host) { pred = c; break; }
    if (!pred) return;
    Log("hostfirst: WorldInfo %p +%x head %p -> host %p", g_hfWorld, g_hfOff, head, host);
    *(BYTE**)(pred + 0x1dc) = *(BYTE**)(host + 0x1dc);
    *(BYTE**)(host + 0x1dc) = head;
    *(BYTE**)(g_hfWorld + g_hfOff) = host;
}

// ---------------------------------------------------------------- rescue
// When a player pawn has fallen far below another player's pawn (its streamed-out area no longer exists on the host),
// teleport it next to the other pawn. Actor.Location is at +0xe4, with a copy at +0x134.
static void TickRescue(const TickObjs& t) {
    if (!g_rescue || t.npc < 2) return;
    BYTE* pawns[16]; int np = 0;
    for (int a = 0; a < t.npc; a++) { BYTE* pw = PawnOf(t.pc[a]); if (pw) pawns[np++] = pw; }
    if (np < 2) return;
    int hi = 0; for (int a = 1; a < np; a++) if (LocOf(pawns[a])[2] > LocOf(pawns[hi])[2]) hi = a;
    float* ref = LocOf(pawns[hi]);
    for (int a = 0; a < np; a++) {
        if (a == hi) continue;
        float* loc = LocOf(pawns[a]);
        if (!(ref[2] - loc[2] > 4000.0f && ref[2] > -20000.0f)) continue;
        static volatile LONG nl = 0; if (InterlockedIncrement(&nl) <= 20) Log("rescue: pawn %p fell to z=%.0f, moving next to %p (%.0f,%.0f,%.0f)", pawns[a], loc[2], pawns[hi], ref[0], ref[1], ref[2]);
        float nx = ref[0] + 250.0f * (a + 1), ny = ref[1] + 60.0f, nz = ref[2] + 120.0f;
        loc[0] = nx; loc[1] = ny; loc[2] = nz;
        float* loc2 = (float*)(pawns[a] + 0x134); loc2[0] = nx; loc2[1] = ny; loc2[2] = nz;
    }
}

// ---------------------------------------------------------------- spawn helper state (B1 diagnosis)
// TnGame.SpawnHelper -> TnPlayerSpawnHelper.Players: TArray<{TnPlayerController PC; byte GameState}>, 8 bytes per entry.
// GameState: 0 waiting for CheckReadySpawn, 1 no spawn point found, 2 TrySpawn failing, 3 in game.
// Offsets are found at run time: the Game field that points at a live helper, and the helper field holding an array
// whose entries are known controllers.
static int g_shGameOff = -1, g_shPlayersOff = -1;
static TArr* PlayersArrayAt(BYTE* helper, int off, const TickObjs& t) {
    TArr* a = (TArr*)(helper + off);
    if (a->Num < 1 || a->Num > 16 || a->Max < a->Num || a->Max > 64 || (UINT_PTR)a->Data < 0x10000 || ((UINT_PTR)a->Data & 3)) return nullptr;
    for (int i = 0; i < a->Num; i++) if (!InList(*(BYTE**)((BYTE*)a->Data + i * 8), t.pc, t.npc)) return nullptr;
    return a;
}
static void TickSpawnHelper(const TickObjs& t) {
    for (int i = 0; i < 16; i++) g_spawnState[i] = -1;
    if (t.ngame < 1 || t.nhelper < 1 || t.npc < 1) return;
    BYTE* game = t.game[0];
    BYTE* helper = nullptr;
    if (g_shGameOff >= 0 && InList(*(BYTE**)(game + g_shGameOff), t.helper, t.nhelper)) helper = *(BYTE**)(game + g_shGameOff);
    static DWORD lastSearch = 0;
    if (!helper && GetTickCount() - lastSearch > 2000) {
        lastSearch = GetTickCount();
        for (int off = 0x100; off < 0x1000 && !helper; off += 4) {
            if (!Rd(game + off, 4)) break;
            BYTE* h = *(BYTE**)(game + off);
            if (!InList(h, t.helper, t.nhelper)) continue;
            helper = h; g_shGameOff = off;
            char cn[80]; ClassNameOf(h, cn, sizeof(cn));
            Log("spawnhelper: Game %p +0x%x -> %p (%s)", game, off, h, cn);
        }
    }
    if (!helper) return;
    TArr* arr = g_shPlayersOff >= 0 ? PlayersArrayAt(helper, g_shPlayersOff, t) : nullptr;
    for (int off = 0x1c; off < 0x100 && !arr; off += 4) {
        arr = PlayersArrayAt(helper, off, t);
        if (arr && off != g_shPlayersOff) { g_shPlayersOff = off; Log("spawnhelper: Players array at helper+0x%x (%d entries)", off, arr->Num); }
    }
    if (!arr) return;
    for (int i = 0; i < arr->Num; i++) {
        BYTE* e = (BYTE*)arr->Data + i * 8;
        for (int a = 0; a < t.npc; a++) if (t.pc[a] == *(BYTE**)e) g_spawnState[a] = (signed char)e[4];
    }
}

static void SafeAscii(char* s) { for (; *s; s++) if (*s == '"' || *s == '\\' || (unsigned char)*s < 0x20 || (unsigned char)*s > 0x7e) *s = '_'; }
static void FStrAscii(const BYTE* fstr, char* out, size_t n) {   // FString {wchar_t* Data; int Num; int Max}
    out[0] = 0;
    const wchar_t* d = *(const wchar_t* const*)fstr; int num = *(const int*)(fstr + 4);
    if (!d || num < 2 || num > 128 || !Rd(d, num * 2)) return;
    size_t k = 0;
    for (int i = 0; i < num - 1 && k + 1 < n; i++) out[k++] = (d[i] >= 32 && d[i] < 127) ? (char)d[i] : '_';
    out[k] = 0;
    SafeAscii(out);
}
// ---------------------------------------------------------------- map name: UWorld.URL (FURL)
// FURL = {FString Protocol, FString Host, int Port, FString Map, TArray<FString> Op, FString Portal, int Valid}: find the
// FString "unreal" (Protocol) inside the World object; Map follows at +0x1c.
// (+0x14 bits 13..30 of an object are NOT its outer: following them gives the archetype chain Default__TnWorldInfo ->
// ... -> Default__Actor.)
static int g_urlOff = -1;
static bool WideIs(const BYTE* fstr, const wchar_t* text) {
    const wchar_t* d = *(const wchar_t* const*)fstr; int num = *(const int*)(fstr + 4);
    return d && num == (int)wcslen(text) + 1 && Rd(d, num * 2) && !wcscmp(d, text);
}
static void MapName(const TickObjs& t, char* out, size_t n) {
    out[0] = 0;
    for (int w = 0; w < t.nuworld && !out[0]; w++) {
        BYTE* world = t.uworld[w];
        if (!Rd(world, 0x800)) continue;
        if (g_urlOff >= 0 && WideIs(world + g_urlOff, L"unreal")) { FStrAscii(world + g_urlOff + 0x1c, out, n); continue; }
        for (int off = 0x1c; off < 0x7c0; off += 4) {
            if (!WideIs(world + off, L"unreal")) continue;
            if (off != g_urlOff) Log("tick: World %p URL at +0x%x", world, off);
            g_urlOff = off;
            FStrAscii(world + off + 0x1c, out, n);
            break;
        }
    }
}

// ---------------------------------------------------------------- foc_state.json (read by tools/matrix.ps1)
static void ObjLabel(BYTE* o, char* out, size_t n) {   // "Name_N" or ""
    out[0] = 0;
    char nm[80]; unsigned num = 0;
    if (!IsObjFast(o) || !ObjNameStr(o, nm, sizeof(nm), &num)) return;
    if (num) _snprintf_s(out, n, _TRUNCATE, "%s_%u", nm, num - 1); else strncpy_s(out, n, nm, _TRUNCATE);
    SafeAscii(out);
}
static const char* CmdlineUrl() {
    static char url[300] = "?";
    if (url[0] == '?') {
        url[0] = 0;
        int argc = 0; wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        for (int i = 1; argv && i < argc; i++) {
            if (argv[i][0] == L'-' || !_wcsnicmp(argv[i], L"ResX=", 5) || !_wcsnicmp(argv[i], L"ResY=", 5)) continue;
            WideCharToMultiByte(CP_ACP, 0, argv[i], -1, url, sizeof(url), nullptr, nullptr); break;
        }
        if (argv) LocalFree(argv);
        SafeAscii(url);
    }
    return url;
}

// The object's current script state: UObject keeps a pointer to its FStateFrame, whose first words include the UState
// node. Found by scanning the header for a pointer to a block that holds an object of class "State".
static int g_stateFrameOff = -1, g_stateNodeIdx = -1;
static unsigned g_stateClassId = 0;
static void StateName(BYTE* o, char* out, size_t n) {
    out[0] = 0;
    if (!g_stateClassId) { g_stateClassId = ClassIdByName("State"); if (!g_stateClassId) return; }
    for (int off = (g_stateFrameOff >= 0 ? g_stateFrameOff : 0x08); off < (g_stateFrameOff >= 0 ? g_stateFrameOff + 1 : 0x60); off += 4) {
        BYTE* f = *(BYTE**)(o + off);
        if ((UINT_PTR)f < 0x10000 || !Rd(f, 0x20)) continue;
        for (int j = (g_stateNodeIdx >= 0 ? g_stateNodeIdx : 0); j < (g_stateNodeIdx >= 0 ? g_stateNodeIdx + 1 : 8); j++) {
            BYTE* st = *(BYTE**)(f + j * 4);
            if (!IsObjFast(st) || (*(unsigned*)(st + 0x14) & 0x1fff) != g_stateClassId) continue;
            if (g_stateFrameOff < 0) { g_stateFrameOff = off; g_stateNodeIdx = j; Log("tick: state frame at +0x%x, node word %d", off, j); }
            ObjNameStr(st, out, n); SafeAscii(out); return;
        }
    }
}

static void WriteState(const TickObjs& t) {
    static char buf[16384];
    int n = 0;
#define J(...) n += _snprintf_s(buf + n, sizeof(buf) - n, _TRUNCATE, __VA_ARGS__)
    char map[96]; MapName(t, map, sizeof(map)); SafeAscii(map);
    char game[80] = ""; if (t.ngame) { ClassNameOf(t.game[0], game, sizeof(game)); SafeAscii(game); }
    J("{\"t\":%lu,\"pid\":%lu,\"instance\":\"%s\",\"tick\":%ld,\"tickMs\":%.2f,\"tickMsMax\":%.2f,", GetTickCount() - g_t0, GetCurrentProcessId(), g_suffix, g_tickCount, g_tickMsLast, g_tickMsMax);
    J("\"url\":\"%s\",\"map\":\"%s\",\"game\":\"%s\",\"role\":\"%s\",\"port\":%ld,\"players\":[", CmdlineUrl(), map, game, t.ngame ? "host" : "client", g_hostBoundPort);
    for (int a = 0; a < t.npc; a++) {
        BYTE* pc = t.pc[a]; BYTE* pw = PawnOf(pc);
        char pcn[96], pwn[96], start[96], view[96], chr[64] = "";
        ObjLabel(pc, pcn, sizeof(pcn)); ObjLabel(pw, pwn, sizeof(pwn)); ObjLabel(*(BYTE**)(pc + 0x220), start, sizeof(start));
        ObjLabel(*(BYTE**)(pc + 0x378), view, sizeof(view));   // camera view target (PlayerController+0x378)
        char st[64]; StateName(pc, st, sizeof(st));
        BYTE* pri = *(BYTE**)(pc + 0x1d4);
        DWORD prif = 0;
        if (IsObjFast(pri)) { FStrAscii(pri + 0x34c, chr, sizeof(chr)); prif = *(DWORD*)(pri + 0x210); }
        float* f = pw ? LocOf(pw) : nullptr;
        J("%s{\"local\":%d,\"pc\":\"%s\",\"pawn\":\"%s\",\"x\":%.0f,\"y\":%.0f,\"z\":%.0f,\"spawn\":%d,\"start\":\"%s\",\"view\":\"%s\",\"state\":\"%s\",\"prif\":%lu,\"char\":\"%s\"}",
          a ? "," : "", IsLocalPC(pc) ? 1 : 0, pcn, pwn, f ? f[0] : 0.f, f ? f[1] : 0.f, f ? f[2] : 0.f, g_spawnState[a], start, view, st, prif, chr);
    }
    J("],\"pawns\":[");
    for (int i = 0; i < t.npawn; i++) {
        bool owned = false; for (int a = 0; a < t.npc; a++) if (PawnOf(t.pc[a]) == t.pawn[i]) owned = true;
        char pwn[96]; ObjLabel(t.pawn[i], pwn, sizeof(pwn)); float* f = LocOf(t.pawn[i]);
        J("%s{\"name\":\"%s\",\"possessed\":%d,\"x\":%.0f,\"y\":%.0f,\"z\":%.0f}", i ? "," : "", pwn, owned ? 1 : 0, f[0], f[1], f[2]);
    }
    J("],\"guards\":[%ld,%ld,%ld,%ld,%ld,%ld],\"excExe\":%ld,\"excBenign\":%ld,\"excSelf\":%ld,\"dumps\":%ld}\n",
      g_guardHits[0], g_guardHits[1], g_guardHits[2], g_guardHits[3], g_guardHits[4], g_guardHits[5], g_excExe, g_excBenign, g_excSelf, g_dumps);
#undef J
    wchar_t dir[MAX_PATH]; GetModuleFileNameW(nullptr, dir, MAX_PATH);
    wchar_t* sl = wcsrchr(dir, L'\\'); if (sl) *sl = 0;
    wchar_t tmp[MAX_PATH], fin[MAX_PATH];
    _snwprintf_s(tmp, _TRUNCATE, L"%s\\foc_state.json.tmp", dir); _snwprintf_s(fin, _TRUNCATE, L"%s\\foc_state.json", dir);
    HANDLE h = CreateFileW(tmp, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD wr = 0; WriteFile(h, buf, (DWORD)n, &wr, nullptr); CloseHandle(h);
    MoveFileExW(tmp, fin, MOVEFILE_REPLACE_EXISTING);
}

static void LogStat(const TickObjs& t, bool withPawns) {
    char b[900]; int l = _snprintf_s(b, _TRUNCATE, "stat: npc=%d pawns=%d games=%d tick=%.1fms (max %.1f)", t.npc, t.npawn, t.ngame, g_tickMsLast, g_tickMsMax);
    for (int a = 0; a < t.npc; a++) {
        BYTE* pw = PawnOf(t.pc[a]); float* f = pw ? LocOf(pw) : nullptr;
        l += _snprintf_s(b + l, sizeof(b) - l, _TRUNCATE, " | %s %s (%.0f,%.0f,%.0f) spawn=%d", IsLocalPC(t.pc[a]) ? "LOCAL" : "REMOTE", pw ? "pawn" : "nopawn", f ? f[0] : 0.f, f ? f[1] : 0.f, f ? f[2] : 0.f, g_spawnState[a]);
    }
    Log("%s", b);
    if (!withPawns || !t.npawn) return;
    l = _snprintf_s(b, _TRUNCATE, "stat: player pawns in world:");
    for (int i = 0; i < t.npawn && i < 10; i++) {
        char pwn[96]; ObjLabel(t.pawn[i], pwn, sizeof(pwn)); float* f = LocOf(t.pawn[i]);
        bool owned = false; for (int a = 0; a < t.npc; a++) if (PawnOf(t.pc[a]) == t.pawn[i]) owned = true;
        l += _snprintf_s(b + l, sizeof(b) - l, _TRUNCATE, " %s%s (%.0f,%.0f,%.0f);", pwn, owned ? "*" : "", f[0], f[1], f[2]);
    }
    Log("%s", b);
}

// ---------------------------------------------------------------- ctrldiag
// ctrldiag=1 (every 4 s from the tick): what each controller references, a host-vs-remote word diff, camera refs,
// PRI strings and pawn locations.
static void CtrlDiag(BYTE** pcs, int npc, TArr* go) {
    Log("pcdiag: %d controllers", npc);
    // camera: every object the local controller references in 0x600..0x740, and which pawns those objects point at
    for (int a = 0; a < npc; a++) {
        if (!IsLocalPC(pcs[a])) continue;
        for (int off = 0x600; off < 0x740; off += 4) {
            BYTE* o = *(BYTE**)(pcs[a] + off);
            if (!IsObjFast(o) || !Rd(o, 0x400)) continue;
            char on[96], cn[80]; ObjLabel(o, on, sizeof(on)); ClassNameOf(o, cn, sizeof(cn));
            char line[900]; size_t k = _snprintf_s(line, _TRUNCATE, "camdiag2: PC+%x %s (%s) pawn refs:", off, on, cn);
            for (int o2 = 0x20; o2 < 0x400; o2 += 4) {
                BYTE* t = *(BYTE**)(o + o2);
                if (!IsObjFast(t) || KindOf(t) != K_PAWN) continue;
                char tn[96]; ObjLabel(t, tn, sizeof(tn));
                if (k + 60 < sizeof(line)) k += _snprintf_s(line + k, sizeof(line) - k, _TRUNCATE, " +%x=%s%s", o2, tn, t == PawnOf(pcs[a]) ? "(own)" : "(OTHER)");
            }
            Log("%s", line);
        }
    }
    for (int a = 0; a < npc; a++) {
        char nm[80]; unsigned num = 0; ObjNameStr(pcs[a], nm, sizeof(nm), &num);
        char line[1200]; size_t k = _snprintf_s(line, _TRUNCATE, "pcdiag: PC %s_%u %p refs:", nm, num - 1, pcs[a]);
        for (int off = 0x100; off < 0x740; off += 4) {
            BYTE* o = *(BYTE**)(pcs[a] + off);
            if (!o || !Rd(o, 0x30)) continue;
            unsigned idx = *(unsigned*)(o + 0x18) & 0x3ffff;
            if (!(idx >= 1 && idx <= (unsigned)go->Num && (BYTE*)go->Data[idx - 1] == o)) continue;
            char n2[80]; unsigned nn = 0; if (!ObjNameStr(o, n2, sizeof(n2), &nn)) continue;
            if (strstr(n2, "Pawn") || strstr(n2, "Character") || strstr(n2, "Player") || strstr(n2, "Connection") || strstr(n2, "Hero")) {
                if (k + 60 < sizeof(line)) k += _snprintf_s(line + k, sizeof(line) - k, _TRUNCATE, " %x=%s_%u", off, n2, nn - 1);
            }
        }
        Log("%s", line);
    }
    if (npc == 2) {
        // word diff between the local-player controller (host) and the remote one
        int L = -1;
        for (int a = 0; a < npc; a++) { BYTE* o = *(BYTE**)(pcs[a] + 0x344); char n2[80]; unsigned nn; if (o && Rd(o, 0x30) && ObjNameStr(o, n2, sizeof(n2), &nn) && !strncmp(n2, "TnLocalPlayer", 13)) L = a; }
        if (L >= 0) {
            BYTE* h = pcs[L]; BYTE* r = pcs[1 - L];
            char line[1500]; size_t k = 0; int cnt = 0;
            for (int off = 0x1d0; off < 0x740; off += 4) {
                unsigned hv = *(unsigned*)(h + off), rv = *(unsigned*)(r + off);
                if (hv == rv) continue;
                bool hp = hv >= 0x01000000 && hv < 0x3e000000 && (hv & 3) == 0, rp = rv >= 0x01000000 && rv < 0x3e000000 && (rv & 3) == 0;
                if (hp && rp) continue;   // both look like heap pointers
                if (k + 30 > sizeof(line) - 40) { Log("pcdiff: host-vs-remote:%s", line); k = 0; line[0] = 0; }
                k += _snprintf_s(line + k, sizeof(line) - k, _TRUNCATE, " %x:%x/%x", off, hv, rv); cnt++;
            }
            if (k) Log("pcdiff: host-vs-remote:%s", line);
        }
    }
    for (int a = 0; a < npc; a++) {
        for (int off = 0x100; off < 0x740; off += 4) {
            BYTE* cam = *(BYTE**)(pcs[a] + off);
            if (!cam || !Rd(cam, 0x30)) continue;
            unsigned ci = *(unsigned*)(cam + 0x18) & 0x3ffff;
            if (!(ci >= 1 && ci <= (unsigned)go->Num && (BYTE*)go->Data[ci - 1] == cam)) continue;
            char cn[80]; unsigned cnum = 0; if (!ObjNameStr(cam, cn, sizeof(cn), &cnum) || cnum == 0) continue;
            if (!strstr(cn, "PlayerCamera") && !strstr(cn, "CameraActor")) continue;
            if (!Rd(cam, 0x600)) continue;
            char line[1300]; size_t k = _snprintf_s(line, _TRUNCATE, "camdiag: PC%d +%x %s_%u %p refs:", a, off, cn, cnum - 1, cam);
            for (int o2 = 0x100; o2 < 0x600; o2 += 4) {
                BYTE* t = *(BYTE**)(cam + o2);
                if (!t || !Rd(t, 0x30)) continue;
                unsigned ti = *(unsigned*)(t + 0x18) & 0x3ffff;
                if (!(ti >= 1 && ti <= (unsigned)go->Num && (BYTE*)go->Data[ti - 1] == t)) continue;
                char tn[80]; unsigned tnum = 0; if (!ObjNameStr(t, tn, sizeof(tn), &tnum)) continue;
                if (!strncmp(tn, "TnPlayer", 8) || strstr(tn, "Camera")) { if (k + 60 < sizeof(line)) k += _snprintf_s(line + k, sizeof(line) - k, _TRUNCATE, " %x=%s_%u", o2, tn, tnum - 1); }
            }
            Log("%s", line);
        }
    }
    for (int a = 0; a < npc; a++) {
        BYTE* pri = *(BYTE**)(pcs[a] + 0x1d4);
        if (!pri || !Rd(pri, 0x900)) continue;
        char line[1400]; size_t k = _snprintf_s(line, _TRUNCATE, "pridiag: PC%d pri %p strings:", a, pri);
        for (int off = 0x200; off < 0x900; off += 4) {
            wchar_t* d = *(wchar_t**)(pri + off); int num = *(int*)(pri + off + 4), mx = *(int*)(pri + off + 8);
            if (num < 2 || num > 40 || mx < num || mx > 4096 || !d || !Rd(d, num * 2)) continue;
            bool ok = d[num - 1] == 0; for (int q = 0; q < num - 1 && ok; q++) if (d[q] < 32 || d[q] > 126) ok = false;
            if (!ok) continue;
            char t[48]; int tn = 0; for (; tn < num - 1 && tn < 40; tn++) t[tn] = (char)d[tn]; t[tn] = 0;
            if (k + 70 < sizeof(line)) k += _snprintf_s(line + k, sizeof(line) - k, _TRUNCATE, " %x='%s'(%d)", off, t, num);
        }
        Log("%s", line);
    }
    // player pawns from the tick's object list (a full GObjects walk with VirtualQuery here would stall the game thread)
    for (int i = 0; i < g_to.npawn; i++) {
        BYTE* obj = g_to.pawn[i];
        char nm[80]; unsigned num = 0;
        if (!ObjNameStr(obj, nm, sizeof(nm), &num) || num == 0 || !Rd(obj, 0x400)) continue;
        char line[1500]; size_t k = _snprintf_s(line, _TRUNCATE, "pawnloc: %s_%u %p:", nm, num - 1, obj);
        for (int off = 0x60; off < 0x300; off += 4) {
            float* f = (float*)(obj + off);
            bool ok = true; for (int q = 0; q < 3; q++) if (!(f[q] > -2e6f && f[q] < 2e6f) || (f[q] != f[q])) ok = false;
            if (!ok) continue;
            if (fabsf(f[0]) < 1.0f && fabsf(f[1]) < 1.0f && fabsf(f[2]) < 1.0f) continue;
            if (fabsf(f[0]) < 1e-3f && fabsf(f[1]) < 1e-3f) continue;
            if (k + 70 < sizeof(line)) k += _snprintf_s(line + k, sizeof(line) - k, _TRUNCATE, " %x=(%.0f,%.0f,%.0f)", off, f[0], f[1], f[2]);
        }
        Log("%s", line);
    }
    // player pawns and their controller-ish refs
    int shown = 0;
    for (int i = 0; i < g_to.npawn && shown < 12; i++) {
        BYTE* obj = g_to.pawn[i];
        char nm[80]; unsigned num = 0;
        if (!ObjNameStr(obj, nm, sizeof(nm), &num) || num == 0) continue;
        if (!Rd(obj, 0x600)) continue;
        char line[900]; size_t k = _snprintf_s(line, _TRUNCATE, "pcdiag: PAWN %s_%u %p ctl-refs:", nm, num - 1, obj);
        for (int off = 0x100; off < 0x600; off += 4) {
            BYTE* o = *(BYTE**)(obj + off);
            for (int a = 0; a < npc; a++) if (o == pcs[a] && k + 24 < sizeof(line)) k += _snprintf_s(line + k, sizeof(line) - k, _TRUNCATE, " %x=PC%d", off, a);
        }
        Log("%s", line); shown++;
    }
}

// ---------------------------------------------------------------- viewfix (B10)
// On a guest the local controller keeps itself as ViewTarget (PlayerController+0x378) after its pawn spawned, so its
// camera never follows its own pawn. Point the view target at the pawn.
static void TickViewFix(const TickObjs& t) {
    if (!g_viewFix) return;
    for (int a = 0; a < t.npc; a++) {
        BYTE* pc = t.pc[a];
        if (!IsLocalPC(pc)) continue;
        BYTE* pw = PawnOf(pc); BYTE** vt = (BYTE**)(pc + 0x378);
        if (!pw || *vt != pc) continue;
        *vt = pw;
        static volatile LONG n = 0; if (InterlockedIncrement(&n) <= 10) Log("viewfix: %p view target was the controller, now pawn %p", pc, pw);
    }
}

// ---------------------------------------------------------------- possessfix (B1 experiment, ini possessfix=1)
// In some chapters the spawn helper leaves a guest in GameState 2: TnGame.TrySpawnPlayer spawns a pawn at the start spot
// every tick but never possesses it. Wire one of those pawns to the guest by hand: copy the pattern of the host's own
// pawn (every pawn field that points at the host controller, i.e. Controller/Owner) and set PC.Pawn (+0x1d0, +0x374).
static int g_pawnCtlOffs[8]; static int g_nPawnCtlOffs = -1;
static TArr* g_playersArr = nullptr;
static void TickPossessFix(const TickObjs& t) {
    if (!g_possessFix || t.ngame < 1) return;
    if (g_nPawnCtlOffs < 0) {
        for (int a = 0; a < t.npc && g_nPawnCtlOffs < 0; a++) {
            BYTE* pw = PawnOf(t.pc[a]);
            if (!pw || !IsLocalPC(t.pc[a]) || !Rd(pw, 0x800)) continue;
            int n = 0; char line[300]; size_t k = _snprintf_s(line, _TRUNCATE, "possessfix: host pawn fields -> host PC:");
            for (int off = 0x60; off < 0x800 && n < 8; off += 4) if (*(BYTE**)(pw + off) == t.pc[a]) { g_pawnCtlOffs[n++] = off; k += _snprintf_s(line + k, sizeof(line) - k, _TRUNCATE, " +%x", off); }
            g_nPawnCtlOffs = n; Log("%s", line);
        }
        if (g_nPawnCtlOffs <= 0) { g_nPawnCtlOffs = -1; return; }
    }
    for (int a = 0; a < t.npc; a++) {
        BYTE* pc = t.pc[a];
        if (IsLocalPC(pc) || PawnOf(pc) || g_spawnState[a] != 2) continue;
        BYTE* best = nullptr;
        for (int i = 0; i < t.npawn; i++) {
            bool owned = false; for (int b = 0; b < t.npc; b++) if (PawnOf(t.pc[b]) == t.pawn[i]) owned = true;
            if (owned) continue;
            if (*(BYTE**)(t.pawn[i] + g_pawnCtlOffs[0])) continue;   // already has a controller
            best = t.pawn[i];   // the newest spawn is last in GObjects order
        }
        if (!best) continue;
        for (int k = 0; k < g_nPawnCtlOffs; k++) *(BYTE**)(best + g_pawnCtlOffs[k]) = pc;
        *(BYTE**)(pc + 0x1d0) = best; *(BYTE**)(pc + 0x374) = best;
        // stop the spawn helper from spawning more pawns: GameState 3
        if (g_shGameOff >= 0 && g_shPlayersOff >= 0) {
            BYTE* helper = *(BYTE**)(t.game[0] + g_shGameOff); TArr* arr = (TArr*)(helper + g_shPlayersOff);
            for (int i = 0; i < arr->Num && i < 16; i++) { BYTE* e = (BYTE*)arr->Data + i * 8; if (*(BYTE**)e == pc) e[4] = 3; }
        }
        Log("possessfix: guest PC %p <- pawn %p (%d fields)", pc, best, g_nPawnCtlOffs);
    }
}

// ---------------------------------------------------------------- pefind (ini pefind=1): locate ProcessEvent
// A script UFunction keeps its native entry (Func, normally UObject::ProcessInternal for script code) at a fixed
// offset. Find that offset (the code pointer most Function objects share), then point Func of every "PlayerTick" at a
// thunk that logs the return addresses once and jumps on. PlayerTick is called by the engine through ProcessEvent,
// so the first return address lands inside ProcessEvent.
static int g_funcOff = -1; static BYTE* g_funcOrig = nullptr; static volatile LONG g_peLogged = 0;
extern "C" void __cdecl PeLogStack(DWORD* sp) {
    if (InterlockedIncrement(&g_peLogged) > 3) return;
    BYTE* base = (BYTE*)GetModuleHandleW(nullptr);
    char line[1200]; size_t k = _snprintf_s(line, _TRUNCATE, "pefind: PlayerTick called, return addresses (ghidra VA):");
    int n = 0;
    for (int i = 0; i < 400 && n < 16; i++) {
        BYTE* v = (BYTE*)sp[i];
        if (v < base + 0x1000 || v >= base + 0x1000 + 0xf5fc71 || !Rd(v - 6, 6)) continue;
        bool call = (v[-5] == 0xE8) || (v[-2] == 0xFF && (v[-1] & 0x38) == 0x10) || (v[-3] == 0xFF && (v[-2] & 0x38) == 0x10) || (v[-6] == 0xFF && (v[-5] & 0x38) == 0x10);
        if (!call) continue;
        k += _snprintf_s(line + k, sizeof(line) - k, _TRUNCATE, " [%d]%lx", i, (unsigned long)(0x400000 + (v - base)));
        n++;
    }
    Log("%s", line);
}
static BYTE* g_peThunk = nullptr;
static void TickPeFind(TArr* go) {
    static bool done = false;
    if (!g_peFind || done || !g_functionClassId) { if (g_peFind && !g_functionClassId) g_functionClassId = ClassIdByName("Function"); return; }
    BYTE* base = (BYTE*)GetModuleHandleW(nullptr);
    BYTE* textLo = base + 0x1000; BYTE* textHi = base + 0x1000 + 0xf5fc71;
    // 1. Func offset: per offset, the most common code pointer over Function objects
    static int counts[96]; static BYTE* vals[96]; int sampled = 0;
    for (int i = 0; i < go->Num && sampled < 3000; i++) {
        BYTE* o = (BYTE*)go->Data[i];
        if (!o || (*(unsigned*)(o + 0x14) & 0x1fff) != g_functionClassId) continue;
        sampled++;
        for (int j = 0; j < 96; j++) { BYTE* v = *(BYTE**)(o + 0x20 + j * 4); if (v < textLo || v >= textHi) continue; if (!vals[j]) vals[j] = v; if (v == vals[j]) counts[j]++; }
    }
    int bj = -1; for (int j = 0; j < 96; j++) if (counts[j] > 0 && (bj < 0 || counts[j] > counts[bj])) bj = j;
    done = true;
    { char line[900]; size_t k = _snprintf_s(line, _TRUNCATE, "pefind: code pointers by offset (count/%d):", sampled);
      for (int j = 0; j < 96; j++) if (counts[j] > sampled / 20) k += _snprintf_s(line + k, sizeof(line) - k, _TRUNCATE, " +%x:%d(%lx)", 0x20 + j * 4, counts[j], (unsigned long)(0x400000 + (vals[j] - base)));
      Log("%s", line); }
    if (bj < 0 || counts[bj] < sampled / 5) { Log("pefind: no shared Func pointer found (%d functions)", sampled); return; }
    g_funcOff = 0x20 + bj * 4; g_funcOrig = vals[bj];
    Log("pefind: Func at UFunction+0x%x, shared value ghidra %lx (%d of %d functions)", g_funcOff, (unsigned long)(0x400000 + (g_funcOrig - base)), counts[bj], sampled);
    // 2. thunk: pushad; push esp+32 (caller frame); call PeLogStack; add esp,4; popad; jmp orig
    g_peThunk = (BYTE*)VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    BYTE* w = g_peThunk;
    *w++ = 0x60;                                   // pushad
    *w++ = 0x8D; *w++ = 0x44; *w++ = 0x24; *w++ = 0x20;   // lea eax,[esp+20h]
    *w++ = 0x50;                                   // push eax
    *w++ = 0xB8; *(void**)w = (void*)&PeLogStack; w += 4;  // mov eax, PeLogStack
    *w++ = 0xFF; *w++ = 0xD0;                       // call eax
    *w++ = 0x83; *w++ = 0xC4; *w++ = 0x04;          // add esp,4
    *w++ = 0x61;                                   // popad
    *w++ = 0xB8; *(void**)w = g_funcOrig; w += 4;  // mov eax, orig
    *w++ = 0xFF; *w++ = 0xE0;                       // jmp eax
    int idx = FindNameIndex("PlayerTick"), patched = 0;
    for (int i = 0; i < go->Num && idx >= 0; i++) {
        BYTE* o = (BYTE*)go->Data[i];
        if (!o || (*(unsigned*)(o + 0x14) & 0x1fff) != g_functionClassId || NameIdxOf(o) != idx) continue;
        if (*(BYTE**)(o + g_funcOff) == g_funcOrig) { *(BYTE**)(o + g_funcOff) = g_peThunk; patched++; }
    }
    Log("pefind: PlayerTick functions patched: %d", patched);
}

// ---------------------------------------------------------------- spawntrace (ini spawntrace=1, B1 diagnosis)
// Once a guest is stuck in spawn state 2, put a hardware breakpoint (DR0, game thread) on the object allocator
// (ghidra 0x759d80, StaticAllocateObject); the VEH logs the call stack of every allocation of the player pawn class.
struct HwbpArgs { DWORD tid; BYTE* addr; int slot; };
static DWORD WINAPI SetHwbpThread(LPVOID p) {
    HwbpArgs* a = (HwbpArgs*)p;
    HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, a->tid);
    if (!th) return 1;
    SuspendThread(th);
    CONTEXT c = {}; c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    GetThreadContext(th, &c);
    if (a->slot == 0) { c.Dr0 = (DWORD)(UINT_PTR)a->addr; c.Dr7 = (c.Dr7 & ~0x000F0003u) | 0x1; }   // L0, execute
    else { c.Dr1 = (DWORD)(UINT_PTR)a->addr; c.Dr7 = (c.Dr7 & ~0x00F0000Cu) | 0x4; }               // L1, execute
    BOOL ok = SetThreadContext(th, &c);
    ResumeThread(th); CloseHandle(th);
    Log("hwbp: DR%d=%p on thread %lu -> %d", a->slot, a->addr, a->tid, ok);
    return 0;
}
static void TickSpawnTrace(const TickObjs& t) {
    static bool armed = false;
    if (!g_spawnTrace || armed || t.npawn < 1) return;
    bool stuck = false; for (int a = 0; a < t.npc; a++) if (g_spawnState[a] == 2) stuck = true;
    if (!stuck) return;
    armed = true;
    g_hwbpClass = g_classObjById[*(unsigned*)(t.pawn[0] + 0x14) & 0x1fff];
    g_hwbpAddr = (BYTE*)GetModuleHandleW(nullptr) + (0x759d80 - 0x400000);
    char cn[80] = ""; if (g_hwbpClass) ObjNameStr(g_hwbpClass, cn, sizeof(cn));
    Log("spawntrace: armed for class %p (%s)", g_hwbpClass, cn);
    static HwbpArgs args; args.tid = GetCurrentThreadId(); args.addr = g_hwbpAddr; args.slot = 0;
    HANDLE h = CreateThread(nullptr, 0, SetHwbpThread, &args, 0, nullptr);
    if (h) CloseHandle(h);   // it suspends this (game) thread briefly once we return to the message loop
}

static void StateName(BYTE* o, char* out, size_t n);
// ---------------------------------------------------------------- statefix (B1 experiment, ini statefix=1)
// In chapters where the guest never gets a pawn its controller sits in script state Dead on the host, and RestartPlayer's
// Possess does nothing there. Switch it to the host controller's state node (normally PlayerWalking).
static void TickStateFix(const TickObjs& t) {
    {   // one-time: where RestartPlayer's virtual calls go for a remote controller (vtable of the +0x10 subobject)
        static bool logged = false;
        for (int a = 0; a < t.npc && !logged; a++) {
            if (IsLocalPC(t.pc[a])) continue;
            BYTE* base = (BYTE*)GetModuleHandleW(nullptr);
            BYTE** vt = *(BYTE***)(t.pc[a] + 0x10); BYTE** gvt = t.ngame ? *(BYTE***)t.game[0] : nullptr;
            if (!Rd(vt, 0x400)) continue;
            logged = true;
            Log("vtdiag: PC+0x10 vtable ghidra %lx: +0x210 %lx +0x278 (Possess) %lx +0x320 %lx; Game vtable %lx: +0x2b4 %lx +0x2c0 %lx +0x2f8 %lx",
                (unsigned long)(0x400000 + ((BYTE*)vt - base)), (unsigned long)(0x400000 + (vt[0x210 / 4] - base)), (unsigned long)(0x400000 + (vt[0x278 / 4] - base)), (unsigned long)(0x400000 + (vt[0x320 / 4] - base)),
                gvt ? (unsigned long)(0x400000 + ((BYTE*)gvt - base)) : 0, gvt ? (unsigned long)(0x400000 + (gvt[0x2b4 / 4] - base)) : 0, gvt ? (unsigned long)(0x400000 + (gvt[0x2c0 / 4] - base)) : 0, gvt ? (unsigned long)(0x400000 + (gvt[0x2f8 / 4] - base)) : 0);
        }
    }
    if (!g_stateFix || g_stateFrameOff < 0) return;
    BYTE* hostNode = nullptr;
    for (int a = 0; a < t.npc; a++) if (IsLocalPC(t.pc[a])) { BYTE* f = *(BYTE**)(t.pc[a] + g_stateFrameOff); if ((UINT_PTR)f > 0x10000) hostNode = *(BYTE**)(f + g_stateNodeIdx * 4); }
    if (!IsObjFast(hostNode)) return;
    for (int a = 0; a < t.npc; a++) {
        BYTE* pc = t.pc[a];
        if (IsLocalPC(pc) || PawnOf(pc) || g_spawnState[a] != 2) continue;
        char st[64]; StateName(pc, st, sizeof(st));
        if (strcmp(st, "Dead") != 0) continue;
        BYTE* f = *(BYTE**)(pc + g_stateFrameOff);
        *(BYTE**)(f + g_stateNodeIdx * 4) = hostNode;
        char hn[64]; ObjNameStr(hostNode, hn, sizeof(hn));
        static volatile LONG n = 0; if (InterlockedIncrement(&n) <= 10) Log("statefix: guest PC %p state Dead -> %s", pc, hn);
    }
}

// ---------------------------------------------------------------- specfix (B1, ini specfix, default on)
// Controller::Possess (ghidra 0xb279d0) only possesses when bit 1 of PlayerReplicationInfo+0x210 is clear
// (UE3: PlayerReplicationInfo.bOnlySpectator). In the chapters where the guest never gets a pawn that bit is set on the
// guest's PRI, so RestartPlayer spawns a pawn every tick and Possess skips it. Clear the bit for a guest stuck in
// spawn state 2.
static void TickSpecFix(const TickObjs& t) {
    if (!g_specFix) return;
    for (int a = 0; a < t.npc; a++) {
        BYTE* pc = t.pc[a];
        if (IsLocalPC(pc) || PawnOf(pc) || g_spawnState[a] != 2) continue;
        BYTE* pri = *(BYTE**)(pc + 0x1d4);
        if (!IsObjFast(pri)) continue;
        DWORD* f = (DWORD*)(pri + 0x210);
        if (!(*f & 2)) continue;
        Log("specfix: guest PC %p PRI %p flags %08lx -> %08lx (spectator bit cleared)", pc, pri, *f, *f & ~2u);
        *f &= ~2u;
    }
}

// ---------------------------------------------------------------- startfix (B1, ini startfix, default on)
// RestartPlayer stores SpawnDefaultPawnFor's result in PC.Pawn. For a guest stuck in spawn state 2 the spawn itself
// fails (the actor is created and destroyed again, those are the "orphan" pawns): the start spot is the one the host
// stands on, so the new pawn collides. While a guest is stuck, move its StartSpot (PC+0x220) around the original
// place (8 directions, 300 uu, one per tick); put it back once the guest has a pawn.
static BYTE* g_sfSpot = nullptr; static float g_sfOrig[3], g_sfOrig2[3]; static int g_sfStep = 0;
static void SfSet(BYTE* spot, const float* a, const float* b) { memcpy(LocOf(spot), a, 12); memcpy(spot + 0x134, b, 12); }
static void TickStartFix(const TickObjs& t) {
    if (!g_startFix) return;
    BYTE* stuckPc = nullptr;
    for (int a = 0; a < t.npc; a++) if (!IsLocalPC(t.pc[a]) && !PawnOf(t.pc[a]) && g_spawnState[a] == 2) stuckPc = t.pc[a];
    if (!stuckPc) {
        if (g_sfSpot) {   // done (or the guest left): restore
            if (IsObjFast(g_sfSpot)) SfSet(g_sfSpot, g_sfOrig, g_sfOrig2);
            Log("startfix: start spot %p restored after %d tries", g_sfSpot, g_sfStep);
            g_sfSpot = nullptr; g_sfStep = 0;
        }
        return;
    }
    BYTE* spot = *(BYTE**)(stuckPc + 0x220);
    if (!IsObjFast(spot)) return;
    if (spot != g_sfSpot) {
        if (g_sfSpot && IsObjFast(g_sfSpot)) SfSet(g_sfSpot, g_sfOrig, g_sfOrig2);
        g_sfSpot = spot; memcpy(g_sfOrig, LocOf(spot), 12); memcpy(g_sfOrig2, spot + 0x134, 12); g_sfStep = 0;
        char sn[96]; ObjLabel(spot, sn, sizeof(sn));
        Log("startfix: guest stuck in spawn state 2, moving its start spot %s (%.0f,%.0f,%.0f)", sn, g_sfOrig[0], g_sfOrig[1], g_sfOrig[2]);
    }
    static const float dx[8] = {1, 0, -1, 0, 0.7f, -0.7f, 0.7f, -0.7f}, dy[8] = {0, 1, 0, -1, 0.7f, 0.7f, -0.7f, -0.7f};
    int k = g_sfStep % 8; float r = 500.0f * (1 + (g_sfStep / 8) % 3);
    float n[3] = {g_sfOrig[0] + dx[k] * r, g_sfOrig[1] + dy[k] * r, g_sfOrig[2] + 40.0f};
    float n2[3] = {g_sfOrig2[0] + dx[k] * r, g_sfOrig2[1] + dy[k] * r, g_sfOrig2[2] + 40.0f};
    SfSet(spot, n, n2);
    g_sfStep++;
}

// ---------------------------------------------------------------- spawnshift (B1, ini spawnshift, default on)
// The guest's pawn is spawned right next to the host (both use the host's start) and the spawn fails on collision every
// tick. DR1 sits on the SpawnActor wrapper; the VEH moves the Location of player-pawn spawns while a guest is stuck.
static void TickSpawnShift(const TickObjs& t) {
    if (!g_spawnShiftFlag || t.ngame < 1) return;
    bool stuck = false;
    for (int a = 0; a < t.npc; a++) if (!IsLocalPC(t.pc[a]) && !PawnOf(t.pc[a]) && g_spawnState[a] == 2) stuck = true;
    g_ssActive = stuck;
    static bool armed = false;
    if (!stuck || armed || t.npawn < 1) return;
    armed = true;
    g_ssClass = g_classObjById[*(unsigned*)(t.pawn[0] + 0x14) & 0x1fff];
    g_ssAddr = (BYTE*)GetModuleHandleW(nullptr) + (0x54a8f0 - 0x400000);
    Log("spawnshift: guest stuck in spawn state 2, watching player pawn spawns (class %p)", g_ssClass);
    static HwbpArgs args; args.tid = GetCurrentThreadId(); args.addr = g_ssAddr; args.slot = 1;
    HANDLE h = CreateThread(nullptr, 0, SetHwbpThread, &args, 0, nullptr);
    if (h) CloseHandle(h);
}

// ---------------------------------------------------------------- chardiag (one-time): host vs guest PRI around
// _SelectedCharacter (+0x34c) when a guest waits in spawn state 0 (no character chosen, chapter 8b)
static void TickCharDiag(const TickObjs& t) {
    static DWORD since = 0; static bool done = false;
    if (done || t.npc < 2) return;
    BYTE *host = nullptr, *guest = nullptr;
    for (int a = 0; a < t.npc; a++) { if (IsLocalPC(t.pc[a])) host = t.pc[a]; else if (g_spawnState[a] == 0) guest = t.pc[a]; }
    if (!host || !guest) { since = 0; return; }
    if (!since) { since = GetTickCount(); return; }
    if (GetTickCount() - since < 10000) return;
    done = true;
    BYTE* hp = *(BYTE**)(host + 0x1d4); BYTE* gp = *(BYTE**)(guest + 0x1d4);
    if (!Rd(hp, 0x400) || !Rd(gp, 0x400)) return;
    char line[2400]; size_t k = _snprintf_s(line, _TRUNCATE, "chardiag: PRI words host/guest that differ (0x300..0x3c0):");
    for (int off = 0x300; off < 0x3c0; off += 4) {
        DWORD h = *(DWORD*)(hp + off), g = *(DWORD*)(gp + off);
        if (h != g) k += _snprintf_s(line + k, sizeof(line) - k, _TRUNCATE, " +%x=%lx/%lx", off, h, g);
    }
    Log("%s", line);
}

// ---------------------------------------------------------------- the tick
static void GameTick() {
    TArr* go = GObj();
    if (go->Num < 1000 || !go->Data) return;
    TickObjs t = {};
    TickCollect(go, t);
    g_to = t;
    g_playerCount = t.npc;
    TickForceChar(t);
    TickMaxPlayers(t);
    TickCtrlShare(t);
    TickHostFirst(t);
    TickRescue(t);
    TickSpawnHelper(t);
    TickViewFix(t);
    TickPossessFix(t);
    TickPeFind(go);
    TickSpawnTrace(t);
    TickStateFix(t);
    TickSpecFix(t);
    TickStartFix(t);
    TickSpawnShift(t);
    TickCharDiag(t);
    DWORD now = GetTickCount();
    static DWORD lastDiag = 0, lastStat = 0, lastPawns = 0, lastState = 0;
    if (g_ctrlDiag && t.npc >= 1 && now - lastDiag > 4000) { lastDiag = now; CtrlDiag(t.pc, t.npc, go); }
    if (t.npc >= 1 && now - lastStat > 3000) { lastStat = now; bool p = now - lastPawns > 15000; if (p) lastPawns = now; LogStat(t, p); }
    if (now - lastState > 1000) { lastState = now; WriteState(t); }
}

static void GameTickSafe() {
    LARGE_INTEGER f, a, b;
    QueryPerformanceFrequency(&f); QueryPerformanceCounter(&a);
    InterlockedIncrement(&g_tickCount);
    __try { GameTick(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { static volatile LONG n = 0; if (InterlockedIncrement(&n) <= 5) Log("tick: exception %08lx", GetExceptionCode()); }
    QueryPerformanceCounter(&b);
    g_tickMsLast = (double)(b.QuadPart - a.QuadPart) * 1000.0 / (double)f.QuadPart;
    if (g_tickMsLast > g_tickMsMax) g_tickMsMax = g_tickMsLast;
}
