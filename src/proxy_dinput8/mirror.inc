// mirror.inc (owner 2026-10-20): the guest repeats what the story script does to the host; only the fight itself
// (health, ammo, damage) stays separate. ini `mirror` (default 1).
// 1) allplayers: every Kismet "Player" variable (SeqVar_Player) gets bAllPlayers, so each action the level script runs
//    on player 0 (the host) - teleport, matinee, input lock, hide, animation, cinematic mode - runs on every player.
//    Done on host and guest (on the guest only its own controller is in the list, so a matinee replicated to it binds
//    its own pawn as well).
// 2) matanim: TnPawn._MatineeAnimationEnabled is not replicated (B17: BeginAnimControl exits while it is false). The
//    host sends its own pawn's value in the sync snapshot (`matanim=`) and sets it on the guests' pawns; the guest sets
//    it on every player pawn it has.
// g_mirror is declared in dinput8.cpp
struct MirrorProp { int off; unsigned mask; bool done; };
static MirrorProp g_mpAll = {}, g_mpMatAnim = {};
// UProperty by name: offset = word +0x30 >> 20, BoolProperty bit mask = word +0x40 (NOTES "Script variable offsets").
// Several classes (and function locals) can have a variable of the same name: the property's Outer must be the class
// `owner`. The Outer field is found by looking for a header word (+0x10..+0x3c) that points at an object named `owner`.
// Without such a word the candidate with the largest offset wins (a class member lies behind the object header; a
// function local of the same name has a small frame offset, e.g. +0x38). Every candidate is logged.
static bool MirrorOuterIs(BYTE* o, const char* owner) {
    for (int off = 0x10; off <= 0x3c; off += 4) {
        BYTE* q = nullptr; __try { q = *(BYTE**)(o + off); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        if (!IsObjFast(q)) continue;
        char on[80]; unsigned num = 0;
        if (ObjNameStr(q, on, sizeof(on), &num) && num == 0 && !strcmp(on, owner)) return true;
    }
    return false;
}
// `hdrprobe <name>` (test command): for every UProperty with this name, every header word +0x00..+0x3c read as an
// object pointer and as a GObjects index, with the names they lead to (to find where the Outer is kept).
static void HdrProbeCommand(const char* name) {
    TArr* go = GObj(); int n = 0;
    for (int i = 0; i < go->Num && n < 6; i++) {
        BYTE* o = (BYTE*)go->Data[i];
        if (!o) continue;
        char nm[80]; unsigned num = 0;
        if (!ObjNameStr(o, nm, sizeof(nm), &num) || num != 0 || strcmp(nm, name)) continue;
        char cn[80] = ""; ClassNameOf(o, cn, sizeof(cn));
        Log("hdrprobe: %s %s %p index %d", nm, cn, o, i); n++;
        for (int off = 0; off <= 0x3c; off += 4) {
            DWORD w = 0; __try { w = *(DWORD*)(o + off); } __except (EXCEPTION_EXECUTE_HANDLER) { break; }
            char a[80] = "-", b[80] = "-";
            if (IsObjFast((BYTE*)(UINT_PTR)w)) { if (!ObjNameStr((BYTE*)(UINT_PTR)w, a, sizeof(a))) strcpy_s(a, "?"); }
            if (w > 0 && (int)w < go->Num && go->Data[w]) { if (!ObjNameStr((BYTE*)go->Data[w], b, sizeof(b))) strcpy_s(b, "?"); }
            Log("hdrprobe:   +%02x %08lx ptr=%s idx=%s", off, w, a, b);
        }
    }
    Log("hdrprobe: %d object(s) named %s", n, name);
}
static bool MirrorPropFind(const char* owner, const char* name, MirrorProp& p) {
    if (p.done) return p.off > 0;
    p.done = true;
    TArr* go = GObj();
    int bestOff = 0; unsigned bestMask = 0; bool bestOuter = false;
    static signed char isProp[8192];   // per class id: a *Property class (cheap check before the name lookup)
    for (int i = 0; i < go->Num; i++) {
        BYTE* o = (BYTE*)go->Data[i];
        if (!o) continue;
        unsigned cid = *(unsigned*)(o + 0x14) & 0x1fff;
        if (!isProp[cid]) { char c[80] = ""; ClassNameOf(o, c, sizeof(c)); isProp[cid] = c[0] ? (strstr(c, "Property") ? 1 : -1) : 0; }
        if (isProp[cid] != 1) continue;
        char nm[80]; unsigned num = 0;
        if (!ObjNameStr(o, nm, sizeof(nm), &num) || num != 0 || strcmp(nm, name)) continue;
        char cn[80] = ""; ClassNameOf(o, cn, sizeof(cn));
        if (!strstr(cn, "Property")) continue;
        int off = (int)(*(unsigned*)(o + 0x30) >> 20);
        unsigned mask = strcmp(cn, "BoolProperty") ? 0 : *(unsigned*)(o + 0x40);
        bool outer = MirrorOuterIs(o, owner);
        Log("mirror: candidate %s.%s %s at +0x%x mask 0x%x outer-match %d", owner, name, cn, off, mask, (int)outer);
        if (outer && !bestOuter) { bestOuter = true; bestOff = off; bestMask = mask; }
        else if (outer == bestOuter && off > bestOff) { bestOff = off; bestMask = mask; }
    }
    p.off = bestOff; p.mask = bestMask;
    Log("mirror: %s.%s -> +0x%x mask 0x%x (%s)", owner, name, p.off, p.mask, bestOuter ? "outer match" : p.off ? "largest offset" : "not found");
    return p.off > 0;
}

static void TickAllPlayers(const TickObjs& t) {
    static unsigned cid = 0; static DWORD cidRetry = 0; static int scanPos = 0, total = 0; static char lastMap[96] = "";
    DWORD now = GetTickCount();
    if (!cid) { if ((LONG)(now - cidRetry) < 0) return; cidRetry = now + 5000; cid = ClassIdByName("SeqVar_Player"); if (!cid) return; }
    if (!MirrorPropFind("SeqVar_Player", "bAllPlayers", g_mpAll) || !g_mpAll.mask) return;
    TArr* go = GObj();
    char map[96]; MapName(t, map, sizeof(map));
    int from = scanPos, to = scanPos + 20000;
    if (strcmp(map, lastMap)) { strncpy_s(lastMap, map, _TRUNCATE); from = 0; to = go->Num; total = 0; }   // new map: full pass
    if (to > go->Num) to = go->Num;
    static int seen = 0, already = 0;
    if (from == 0) { seen = 0; already = 0; }
    for (int i = from; i < to; i++) {
        BYTE* o = (BYTE*)go->Data[i];
        if (!o || (*(unsigned*)(o + 0x14) & 0x1fff) != cid) continue;
        seen++;
        DWORD* w = (DWORD*)(o + g_mpAll.off);
        if (*w & g_mpAll.mask) { already++; continue; }
        char nm[80]; if (!ObjNameStr(o, nm, sizeof(nm)) || !strncmp(nm, "Default__", 9)) continue;
        *w |= g_mpAll.mask; total++;
        if (total <= 5 || total % 50 == 0) Log("mirror: allplayers on %s (%d so far on %s)", nm, total, map);
    }
    static int lastSeen = -1, lastAlready = -1;
    if (to >= go->Num && (seen != lastSeen || already != lastAlready)) Log("mirror: allplayers pass on %s: %d Player variables, %d had bAllPlayers already, %d set so far", map, seen, already, total);
    if (to >= go->Num) { lastSeen = seen; lastAlready = already; }
    scanPos = to >= go->Num ? 0 : to;
}

static int g_mirrorMatAnim = -1;   // host: its own pawn's _MatineeAnimationEnabled (sent as matanim=)
static void MirrorSetMatAnim(BYTE* pawn, bool on) {
    if (!IsObjFast(pawn) || g_mpMatAnim.off <= 0 || !g_mpMatAnim.mask) return;
    DWORD* w = (DWORD*)(pawn + g_mpMatAnim.off);
    bool cur = (*w & g_mpMatAnim.mask) != 0;
    if (cur == on) return;
    if (on) *w |= g_mpMatAnim.mask; else *w &= ~g_mpMatAnim.mask;
    static volatile LONG nl = 0; if (InterlockedIncrement(&nl) <= 40) Log("mirror: matanim %d on pawn %p", (int)on, pawn);
}
static void TickMatAnim(const TickObjs& t) {
    if (!MirrorPropFind("TnPawn", "_MatineeAnimationEnabled", g_mpMatAnim) || !g_mpMatAnim.mask) return;
    if (t.ngame < 1) return;   // the guest applies what the host sends (MirrorApply)
    BYTE* host = nullptr;
    for (int a = 0; a < t.npc; a++) if (IsObjFast(t.pc[a]) && IsLocalPC(t.pc[a])) host = PawnOf(t.pc[a]);
    if (!IsObjFast(host)) { g_mirrorMatAnim = -1; return; }
    bool on = (*(DWORD*)(host + g_mpMatAnim.off) & g_mpMatAnim.mask) != 0;
    g_mirrorMatAnim = on;
    for (int a = 0; a < t.npc; a++) if (IsObjFast(t.pc[a]) && !IsLocalPC(t.pc[a])) MirrorSetMatAnim(PawnOf(t.pc[a]), on);
}
static void TickCamView(const TickObjs& t);
static void TickFakeLogin(const TickObjs& t);
static void TickMirror(const TickObjs& t) {
    TickFakeLogin(t);
    TickCamView(t);
    if (!g_mirror) return;
    TickAllPlayers(t);
    TickMatAnim(t);
}
static int MirrorSnap(const TickObjs& t, char* out, size_t n) {
    if (!g_mirror || g_mirrorMatAnim < 0) return 0;
    return _snprintf_s(out, n, _TRUNCATE, "matanim=%d\n", g_mirrorMatAnim);
}
static void MirrorApply(const TickObjs& t, const char* body) {
    if (!g_mirror) return;
    int v = SyncGetInt(body, "matanim", -1);
    if (v < 0 || !MirrorPropFind("TnPawn", "_MatineeAnimationEnabled", g_mpMatAnim)) return;
    for (int a = 0; a < t.npawn; a++) MirrorSetMatAnim(t.pawn[a], v != 0);
}

// ---------------------------------------------------------------- camview (host): the guest sees the host's scene camera
// In the chapter 1 intro the host is driven by matinee (controller state AnimControl) and looks through the scene's
// CameraActors (view target PlayerController +0x378), the guest kept its own camera and missed the scene. While the
// host is in cinematic mode or AnimControl and its view target is neither its pawn nor its controller, every guest
// gets the same view target (client RPC ClientSetViewTarget(Actor, ViewTargetTransitionParams)); afterwards its own
// pawn again. ini `camview` (default 1).
static void MirrorSetView(BYTE* pc, BYTE* target, const char* why) {
    DWORD parms[16] = {0};
    parms[0] = (DWORD)(UINT_PTR)target;   // A
    float blend = 0.0f, exp = 2.0f;        // TransitionParams: BlendTime, BlendFunction (byte), BlendExp, bLockOutgoing
    memcpy(&parms[1], &blend, 4); parms[2] = 0; memcpy(&parms[3], &exp, 4); parms[4] = 0;
    bool ok = CallScript(pc, "ClientSetViewTarget", parms, sizeof(parms));
    char pn[96], tn[96]; ObjLabel(pc, pn, sizeof(pn)); ObjLabel(target, tn, sizeof(tn));
    Log("camview: %s %s.ClientSetViewTarget(%s) %s", why, pn, tn, ok ? "ok" : "failed");
}
static void TickCamView(const TickObjs& t) {
    static BYTE* lastWant = (BYTE*)1;
    if (!g_camView || t.ngame < 1) { lastWant = (BYTE*)1; return; }
    BYTE* host = nullptr; int remotes = 0;
    for (int a = 0; a < t.npc; a++) { if (IsObjFast(t.pc[a]) && IsLocalPC(t.pc[a])) host = t.pc[a]; else remotes++; }
    if (!host || !remotes) { lastWant = (BYTE*)1; return; }
    BYTE* hp = PawnOf(host); BYTE* view = *(BYTE**)(host + 0x378);
    char st[48] = ""; StateName(host, st, sizeof(st));
    bool scene = ((*(unsigned*)(host + 0x360) & 0x20000) || !strcmp(st, "AnimControl")) && IsObjFast(view) && view != hp && view != host;
    BYTE* want = scene ? view : nullptr;   // nullptr = each guest's own pawn
    if (want == lastWant) return;
    lastWant = want;
    for (int a = 0; a < t.npc; a++) {
        BYTE* pc = t.pc[a]; if (!IsObjFast(pc) || IsLocalPC(pc)) continue;
        BYTE* gp = PawnOf(pc);
        if (want) MirrorSetView(pc, want, "host scene camera ->");
        else if (gp) MirrorSetView(pc, gp, "scene over ->");
    }
}

// `fnprobe <name>` (test command): every Function object with this name, its outer chain guess (header words that are
// objects) and the header words that point into the exe's code (the native thunk, UFunction::Func).
static void FnProbeCommand(const char* name) {
    BYTE* base = (BYTE*)GetModuleHandleW(nullptr);
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
    BYTE* lo = base, *hi = base + nt->OptionalHeader.SizeOfImage;
    TArr* go = GObj(); int n = 0;
    for (int i = 0; i < go->Num && n < 8; i++) {
        BYTE* o = (BYTE*)go->Data[i]; if (!o) continue;
        char nm[80]; unsigned num = 0;
        if (!ObjNameStr(o, nm, sizeof(nm), &num) || num != 0 || strcmp(nm, name)) continue;
        char cn[80] = ""; ClassNameOf(o, cn, sizeof(cn));
        char line[900]; size_t k = _snprintf_s(line, _TRUNCATE, "fnprobe: %s %s %p:", nm, cn, o);
        for (int off = 0; off < 0xc0; off += 4) {
            DWORD w = 0; __try { w = *(DWORD*)(o + off); } __except (EXCEPTION_EXECUTE_HANDLER) { break; }
            if ((BYTE*)(UINT_PTR)w >= lo && (BYTE*)(UINT_PTR)w < hi) k += _snprintf_s(line + k, sizeof(line) - k, _TRUNCATE, " +%x=exe:%lx", off, (DWORD)((BYTE*)(UINT_PTR)w - base) + 0x400000);
            else if (IsObjFast((BYTE*)(UINT_PTR)w)) { char on[64] = ""; ObjNameStr((BYTE*)(UINT_PTR)w, on, sizeof(on)); k += _snprintf_s(line + k, sizeof(line) - k, _TRUNCATE, " +%x=obj:%s", off, on); }
        }
        Log("%s", line); n++;
    }
    Log("fnprobe: %d object(s) named %s", n, name);
}

// fakelogin (ini, default 0 until tested): the official Demonware service is gone, so the game is never signed in, and
// TnPlayerController.PlayerTick drops any network game back to the front end once: "if (!HasCheckedForLogin && NetMode
// != Standalone) { if (GetLoginStatus != LS_LoggedIn) NotifyLoginChange(...); HasCheckedForLogin = true; }" ->
// "You must be signed in to play online" (B4: the reason Host/Join restart the game). The scripts are compiled to C++
// and call GetLoginStatus directly (replacing the UFunction thunk did nothing), so instead every local controller gets
// HasCheckedForLogin = true before its first tick.
static MirrorProp g_mpChecked = {};
static void TickFakeLogin(const TickObjs& t) {
    if (!g_fakeLogin) return;
    if (!MirrorPropFind("TnPlayerController", "HasCheckedForLogin", g_mpChecked) || !g_mpChecked.mask) return;
    // class defaults first: a new controller is made from them, so it has the flag before its first tick (our tick runs
    // only every 200 ms)
    // once per map (a full pass over the object names every 5 s made the host hitch, 2026-10-20)
    static char lastMap[96] = "\x01"; char curMap[96]; MapName(t, curMap, sizeof(curMap));
    if (strcmp(curMap, lastMap)) {
        strncpy_s(lastMap, curMap, _TRUNCATE);
        TArr* go = GObj(); int n = 0;
        for (int i = 0; i < go->Num; i++) {
            BYTE* o = (BYTE*)go->Data[i]; if (!o) continue;
            // class first (cheap): a name lookup for every object took 2-4 s per map (slow: TickMirror, 2026-10-20)
            unsigned cid = *(unsigned*)(o + 0x14) & 0x1fff;
            static signed char isPc[8192];
            if (!isPc[cid]) { char cn[96] = ""; ClassNameOf(o, cn, sizeof(cn)); isPc[cid] = cn[0] ? (strstr(cn, "PlayerController") ? 1 : -1) : 0; }
            if (isPc[cid] != 1) continue;
            char nm[96]; if (!ObjNameStr(o, nm, sizeof(nm)) || strncmp(nm, "Default__Tn", 11)) continue;
            if (!Rd(o + g_mpChecked.off, 4)) continue;
            DWORD* w = (DWORD*)(o + g_mpChecked.off); if (*w & g_mpChecked.mask) continue;
            *w |= g_mpChecked.mask; n++;
        }
        if (n) Log("fakelogin: HasCheckedForLogin set on %d controller class default(s)", n);
    }
    for (int a = 0; a < t.npc; a++) {
        BYTE* pc = t.pc[a]; if (!IsObjFast(pc) || !IsLocalPC(pc)) continue;
        DWORD* w = (DWORD*)(pc + g_mpChecked.off);
        if (*w & g_mpChecked.mask) continue;
        *w |= g_mpChecked.mask;
        char pn[96]; ObjLabel(pc, pn, sizeof(pn)); Log("fakelogin: %s HasCheckedForLogin set (no sign-in check)", pn);
    }
}
