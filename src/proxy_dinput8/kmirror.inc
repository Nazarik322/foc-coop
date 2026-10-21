// kmirror.inc (owner 2026-10-20, "mirror the script"): the guest runs the same Kismet ops the host runs, found by
// object name in its own copy of the level. Step 1: `kimpulse <OpName_N> [input]` queues an op the way UE3's
// USequence::QueueSequenceOp does: InputLinks[input].bHasImpulse = 1 and the op appended to its parent sequence's
// ActiveSequenceOps; the engine runs it on its next Kismet update. The log tells whether ActivateCount (+0x70) grew.
// Offsets come from reflection (mirror.inc MirrorPropFind); the input link stride is measured from the LinkDesc FStrings.
static MirrorProp g_kpInputs = {}, g_kpParent = {}, g_kpActive = {};
static int g_kpStride = 0;
static bool KpFStr(BYTE* p, char* out, size_t n) {   // FString {wchar_t* Data, int Num, int Max} -> ascii
    out[0] = 0;
    if (!Rd(p, 12)) return false;
    wchar_t* d = *(wchar_t**)p; int num = *(int*)(p + 4), mx = *(int*)(p + 8);
    if (num < 1 || num > 64 || mx < num || !d || !Rd(d, num * 2)) return false;
    size_t k = 0; for (int i = 0; i < num - 1 && k + 1 < n; i++) { if (d[i] < 32 || d[i] > 126) return false; out[k++] = (char)d[i]; }
    out[k] = 0; return d[num - 1] == 0;
}
static bool KpSetup() {
    MirrorPropFind("SequenceOp", "InputLinks", g_kpInputs);
    MirrorPropFind("SequenceObject", "ParentSequence", g_kpParent);
    MirrorPropFind("Sequence", "ActiveSequenceOps", g_kpActive);
    return g_kpInputs.off > 0 && g_kpParent.off > 0 && g_kpActive.off > 0;
}
// stride of SeqOpInputLink: the smallest step at which every entry starts with a readable FString
static int KpStride(TArr* in) {
    if (g_kpStride) return g_kpStride;
    if (in->Num < 2) return 0;
    for (int s = 0x10; s <= 0x60; s += 4) {
        bool ok = true; char t[64];
        for (int i = 0; i < in->Num && ok; i++) ok = KpFStr((BYTE*)in->Data + i * s, t, sizeof(t));
        if (ok) { g_kpStride = s; Log("kmirror: SeqOpInputLink stride 0x%x", s); return s; }
    }
    return 0;
}
static BYTE* KpFindOp(const char* label) {   // "SeqAct_Interp_5373" = name + number
    char want[96]; strncpy_s(want, label, _TRUNCATE);
    char* us = strrchr(want, '_'); if (!us) return nullptr;
    unsigned wantNum = (unsigned)atoi(us + 1) + 1; *us = 0;   // the label shows the internal number - 1 (ObjLabel)
    TArr* go = GObj();
    for (int i = 0; i < go->Num; i++) {
        BYTE* o = (BYTE*)go->Data[i]; if (!o) continue;
        char nm[96]; unsigned num = 0;
        if (ObjNameStr(o, nm, sizeof(nm), &num) && num == wantNum && !strcmp(nm, want)) return o;
    }
    return nullptr;
}
// queue `op` with an impulse on input `idx`; returns false when the layout does not check out
static bool KpImpulse(BYTE* op, int idx, const char* why) {
    if (!KpSetup()) { Log("kmirror: offsets not found (InputLinks %d ParentSequence %d ActiveSequenceOps %d)", g_kpInputs.off, g_kpParent.off, g_kpActive.off); return false; }
    TArr* in = (TArr*)(op + g_kpInputs.off);
    if (!Rd(in, 12) || in->Num < 1 || in->Num > 16 || idx < 0 || idx >= in->Num) { Log("kmirror: %s input %d of %d invalid", why, idx, in->Num); return false; }
    int s = in->Num >= 2 ? KpStride(in) : (g_kpStride ? g_kpStride : 0);
    char desc[64] = "?";
    if (s) KpFStr((BYTE*)in->Data + idx * s, desc, sizeof(desc));
    else if (!KpFStr((BYTE*)in->Data, desc, sizeof(desc))) { Log("kmirror: %s no input stride yet", why); return false; }
    BYTE* link = (BYTE*)in->Data + idx * (s ? s : 0);
    BYTE* parent = *(BYTE**)(op + g_kpParent.off);
    if (!IsObjFast(parent)) { Log("kmirror: %s has no parent sequence", why); return false; }
    TArr* act = (TArr*)(parent + g_kpActive.off);
    if (!Rd(act, 12) || act->Num < 0 || act->Num > 4096) { Log("kmirror: %s parent active list invalid", why); return false; }
    *(DWORD*)(link + 0x0c) |= 1;   // bHasImpulse: first bit after the 12-byte LinkDesc
    bool queued = false; for (int i = 0; i < act->Num; i++) if (act->Data[i] == op) queued = true;
    if (!queued) {
        if (act->Num >= act->Max) { void* d = GameRealloc(act->Data, (act->Max + 8) * 4); if (!d) return false; act->Data = (void**)d; act->Max += 8; }
        act->Data[act->Num++] = op;
    }
    char on[96]; ObjLabel(op, on, sizeof(on));
    Log("kmirror: %s impulse %s input %d '%s' (count %d, %s)", why, on, idx, desc, *(int*)(op + 0x70), queued ? "already queued" : "queued");
    return true;
}
static BYTE* g_kpWatch = nullptr; static int g_kpWatchCount = 0; static DWORD g_kpWatchAt = 0;
static void KImpulseCommand(const char* args) {
    char name[96] = ""; int idx = 0;
    if (sscanf_s(args, "%95s %d", name, (unsigned)sizeof(name), &idx) < 1) { Log("kimpulse: usage kimpulse <OpName_N> [input]"); return; }
    BYTE* op = KpFindOp(name);
    if (!op) { Log("kimpulse: no object %s", name); return; }
    g_kpWatch = op; g_kpWatchCount = *(int*)(op + 0x70); g_kpWatchAt = GetTickCount();
    KpImpulse(op, idx, "kimpulse");
}
static void TickKMirrorGuest(const TickObjs& t);
static void TickKMirror(const TickObjs& t) {
    TickKMirrorGuest(t);
    if (g_kpWatch && GetTickCount() - g_kpWatchAt > 1000) {
        char on[96]; ObjLabel(g_kpWatch, on, sizeof(on));
        int c = IsObjFast(g_kpWatch) ? *(int*)(g_kpWatch + 0x70) : -1;
        Log("kimpulse: %s ActivateCount %d -> %d (%s)", on, g_kpWatchCount, c, c > g_kpWatchCount ? "RAN" : "did not run");
        g_kpWatch = nullptr;
    }
}

// ---------------------------------------------------------------- the mirror (ini kmirror, default 1)
// Host: every op activation the Kismet tracker sees (trace.inc) is offered to KMirrorHostOp; ops of a whitelisted class
// with a known input are sent to the guests at once ("FOCK1\n<OpLabel> <input>\n"). The input that fired is not kept by
// the engine, so it is inferred: ToggleHidden from the first target's bHidden after the op (hidden = "Hide" 0, else
// "Show" 1); single-input classes use 0. Guest: queued by the network thread, run on the game thread with KpImpulse
// on the op of the same name in its own copy of the level (map label -> op built once per map).
// Not mirrored (logged once per class on the host): everything else, e.g. matinee (already replicated), spawns,
// damage, objectives (server state).
#include <string>
#include <unordered_map>
static int KMirrorInput(BYTE* op, const char* cls) {
    // ForceActorMipsResident: the level keeps the textures of scripted characters fully loaded before a scene; on the
    // server only, so the guest saw them low-res (owner 2026-10-20)
    if (!strcmp(cls, "SeqAct_SetMaterial") || !strcmp(cls, "SeqAct_CameraShake") || !strcmp(cls, "SeqAct_ForceActorMipsResident")) return 0;
    if (!strcmp(cls, "SeqAct_ToggleHidden")) {
        TArr* tg = (TArr*)(op + 0x80);   // SequenceAction.Targets
        if (!Rd(tg, 12) || tg->Num < 1 || tg->Num > 256) return -1;
        BYTE* a = (BYTE*)tg->Data[0]; if (!IsObjFast(a)) return -1;
        return (*(unsigned*)(a + 0x84) & 4) ? 0 : 1;   // Actor.bHidden
    }
    return -1;
}
static void KMirrorHostOp(BYTE* op, const char* cls) {   // trace.inc TickKismetLog, host, game thread
    if (strstr(cls, "Streaming")) TpStreamGuard(cls);   // SeqAct_SetStreamingState, SeqAct_LevelStreaming, ...
    if (!g_kMirror || g_syncSock == INVALID_SOCKET) return;
    int in = KMirrorInput(op, cls);
    if (in < 0) {
        static char seen[64][48]; static int ns = 0; bool known = false;
        for (int i = 0; i < ns && !known; i++) known = !strcmp(seen[i], cls);
        if (!known && ns < 64) { strncpy_s(seen[ns++], cls, _TRUNCATE); Log("kmirror: host op class %s not mirrored", cls); }
        return;
    }
    char lab[96]; ObjLabel(op, lab, sizeof(lab));
    char msg[160]; int n = _snprintf_s(msg, _TRUNCATE, "FOCK1\n%s %d\n", lab, in);
    EnterCriticalSection(&g_syncCs);
    for (int i = 0; i < g_syncNPeers; i++) if (GetTickCount() - g_syncPeers[i].seen < 10000) o_sendto(g_syncSock, msg, n, 0, (const sockaddr*)&g_syncPeers[i].addr, sizeof(sockaddr_in));
    LeaveCriticalSection(&g_syncCs);
    Log("kmirror: host sent %s input %d", lab, in);
}
static char g_kmRx[256][128]; static volatile LONG g_kmHead = 0, g_kmTail = 0;
static void KMirrorQueue(const char* buf, int len) {   // network thread
    LONG h = g_kmHead; if (h - g_kmTail >= 256) return;
    int n = len < 127 ? len : 127; memcpy(g_kmRx[h % 256], buf, n); g_kmRx[h % 256][n] = 0;
    InterlockedIncrement(&g_kmHead);
}
static void TickKMirrorGuest(const TickObjs& t) {
    static std::unordered_map<std::string, BYTE*> ops; static char opsMap[96] = ""; static DWORD indexedAt = 0;
    if (!g_kMirror || t.ngame > 0 || g_kmTail == g_kmHead) return;
    char map[96]; MapName(t, map, sizeof(map));
    // only in the story map with our own pawn: ops that arrive while the guest is still loading wait in the queue
    // (running them during the join hung the guest, 2026-10-20)
    BYTE* own = nullptr; for (int a = 0; a < t.npc; a++) if (IsObjFast(t.pc[a]) && IsLocalPC(t.pc[a])) own = PawnOf(t.pc[a]);
    if (!own || !StoryMapNow(t)) { return; }
    // (re)index on a new map, and again (at most every 5 s) when an op is missing: streamed sublevels bring their own
    // Kismet later (2026-10-20: "guest has no op SeqAct_ToggleHidden_3081")
    bool missing = false;
    for (LONG q = g_kmTail; q != g_kmHead && !missing; q++) { char lab[96] = ""; sscanf_s(g_kmRx[q % 256] + 6, "%95s", lab, (unsigned)sizeof(lab)); missing = ops.find(lab) == ops.end(); }
    if (strcmp(map, opsMap) || ops.empty() || (missing && GetTickCount() - indexedAt > 5000)) {
        ops.clear(); strncpy_s(opsMap, map, _TRUNCATE); indexedAt = GetTickCount();
        TArr* go = GObj();
        for (int i = 0; i < go->Num; i++) { BYTE* o = (BYTE*)go->Data[i]; if (!o || !TraceIsSeqOp(o)) continue; char lab[96]; ObjLabel(o, lab, sizeof(lab)); if (lab[0]) ops[lab] = o; }
        Log("kmirror: guest indexed %d Kismet ops on %s", (int)ops.size(), map);
    }
    while (g_kmTail != g_kmHead) {
        char lab[96] = ""; int in = 0;
        sscanf_s(g_kmRx[g_kmTail % 256] + 6, "%95s %d", lab, (unsigned)sizeof(lab), &in);
        InterlockedIncrement(&g_kmTail);
        auto it = ops.find(lab);
        if (it == ops.end() || !IsObjFast(it->second)) { Log("kmirror: guest has no op %s", lab); continue; }
        KpImpulse(it->second, in, "guest mirror");
    }
}
