// trace.inc (owner 2026-10-20: "log every sneeze", to find what the guest does differently from the host).
// ini `ptrace` (default 1). Both sides:
//  - `trace:` once per second per player: controller and pawn state, position, velocity, pawn rotation, view rotation
//    (controller rotation), view target, health, downed, hidden, cinematic mode, matinee-animation flag.
//  - `trace-chg:` as soon as a player's controller state, pawn state, downed, hidden, cinematic or view target changes.
// Host only:
//  - `kismet:` every activation of a Kismet op (SequenceOp.ActivateCount +0x70 grows): class and object name.
//  - scriptdown: when the level script downs / revives the host (TnSeqAct_DownRevive), the guests get the same
//    (exec TestDowned / TestRevive on their pawns); combat downs are not mirrored. ini `scriptdown` (default 1).
// g_ptrace, g_scriptDown are declared in dinput8.cpp

static void TraceRot(BYTE* a, int* r) { r[0] = r[1] = r[2] = 0; if (IsObjFast(a)) { r[0] = *(int*)(a + 0xf0) & 0xffff; r[1] = *(int*)(a + 0xf4) & 0xffff; r[2] = *(int*)(a + 0xf8) & 0xffff; } }
struct TraceLast { BYTE* pc; char pcSt[48]; char pwSt[48]; int downed, hidden, cine; BYTE* view; };
static TraceLast g_traceLast[16];

static void TickTrace(const TickObjs& t) {
    static DWORD last = 0; DWORD now = GetTickCount();
    bool full = now - last >= 1000; if (full) last = now;
    for (int a = 0; a < t.npc && a < 16; a++) {
        BYTE* pc = t.pc[a]; if (!IsObjFast(pc)) continue;
        BYTE* pw = PawnOf(pc);
        char pcSt[48] = "", pwSt[48] = "", pwn[64] = "-", vn[64] = "-";
        StateName(pc, pcSt, sizeof(pcSt)); if (pw) StateName(pw, pwSt, sizeof(pwSt));
        BYTE* view = *(BYTE**)(pc + 0x378);
        int downed = pw && (*(unsigned*)(pw + 0x5d0) & 0x80000) ? 1 : 0;
        int hidden = pw && (*(unsigned*)(pw + 0x84) & 4) ? 1 : 0;
        int cine = (*(unsigned*)(pc + 0x360) & 0x20000) ? 1 : 0;
        int mat = (pw && g_mpMatAnim.off > 0 && g_mpMatAnim.mask) ? ((*(DWORD*)(pw + g_mpMatAnim.off) & g_mpMatAnim.mask) ? 1 : 0) : -1;
        const char* who = IsLocalPC(pc) ? "local" : "remote";
        float ds = (!IsLocalPC(pc) && t.ngame > 0) ? TpGuestDesync(pw) : -1.0f;
        TraceLast& L = g_traceLast[a];
        if (L.pc != pc) { memset(&L, 0, sizeof(L)); L.pc = pc; L.downed = L.hidden = L.cine = -1; }
        bool chg = strcmp(L.pcSt, pcSt) || strcmp(L.pwSt, pwSt) || L.downed != downed || L.hidden != hidden || L.cine != cine || L.view != view;
        if (!full && !chg) continue;
        ObjLabel(pw, pwn, sizeof(pwn)); ObjLabel(view, vn, sizeof(vn));
        float* l = pw ? LocOf(pw) : nullptr; float* v = pw && Rd(pw + 0xfc, 12) ? (float*)(pw + 0xfc) : nullptr;
        int pr[3], cr[3]; TraceRot(pw, pr); TraceRot(pc, cr);
        Log("%s %s pc=%s pawn=%s(%s) at %.0f,%.0f,%.0f vel %.0f,%.0f,%.0f rot %d,%d view %d,%d -> %s hp=%.0f downed=%d hidden=%d cine=%d matanim=%d desync=%.0f",
            chg ? "trace-chg:" : "trace:", who, pcSt, pwn, pwSt, l ? l[0] : 0.f, l ? l[1] : 0.f, l ? l[2] : 0.f,
            v ? v[0] : 0.f, v ? v[1] : 0.f, v ? v[2] : 0.f, pr[1], pr[0], cr[1], cr[0], vn, pw ? *(float*)(pw + 0x350) : 0.f, downed, hidden, cine, mat, ds);
        strncpy_s(L.pcSt, pcSt, _TRUNCATE); strncpy_s(L.pwSt, pwSt, _TRUNCATE); L.downed = downed; L.hidden = hidden; L.cine = cine; L.view = view;
    }
}

static void KMirrorHostOp(BYTE* op, const char* cls);   // kmirror.inc
// ---------------------------------------------------------------- Kismet activity (host)
static bool g_isSeqClass[8192]; static bool g_seqClassKnown[8192]; static short g_seqCntOff[8192];   // g_tpTrigCount: scene.inc
static bool TraceIsSeqOp(BYTE* o) {
    unsigned cid = *(unsigned*)(o + 0x14) & 0x1fff;
    if (!g_seqClassKnown[cid]) {
        char cn[96] = ""; ClassNameOf(o, cn, sizeof(cn));
        if (cn[0]) {
            g_seqClassKnown[cid] = true;
            g_isSeqClass[cid] = strstr(cn, "SeqAct_") || strstr(cn, "SeqEvent_") || strstr(cn, "SeqCond_") || !strcmp(cn, "Sequence");
            // 2026-10-24: events count their firings in SequenceEvent.TriggerCount, not in ActivateCount (+0x70), so the
            // tracker never saw a Touch / Used event (the scene activator, scene.inc)
            g_seqCntOff[cid] = 0x70;
            if (strstr(cn, "SeqEvent_") && (g_tpTrigCount.off > 0 || MirrorPropFind("SequenceEvent", "TriggerCount", g_tpTrigCount)))
                g_seqCntOff[cid] = (short)g_tpTrigCount.off;
        }
    }
    return g_isSeqClass[cid];
}
static int TraceCount(BYTE* o) { unsigned cid = *(unsigned*)(o + 0x14) & 0x1fff; int off = g_seqCntOff[cid] ? g_seqCntOff[cid] : 0x70; return *(int*)(o + off); }
static void TickKismetLog(const TickObjs& t) {
    static BYTE* ops[12000]; static int cnt[12000]; static int nops = 0; static int scanPos = 0; static char lastMap[96] = "";
    static DWORD lastCheck = 0;
    if (t.ngame < 1) return;
    TArr* go = GObj();
    char map[96]; MapName(t, map, sizeof(map));
    if (strcmp(map, lastMap)) { strncpy_s(lastMap, map, _TRUNCATE); nops = 0; scanPos = 0; Log("kismet: new map %s, collecting ops", map); }
    // collect: 20000 objects per tick; drop entries whose slot was freed
    int to = scanPos + 20000; if (to > go->Num) to = go->Num;
    for (int i = scanPos; i < to && nops < 12000; i++) {
        BYTE* o = (BYTE*)go->Data[i];
        if (!o || !TraceIsSeqOp(o)) continue;
        bool have = false; for (int k = 0; k < nops && !have; k++) have = ops[k] == o;
        if (have) continue;
        char nm[80]; if (!ObjNameStr(o, nm, sizeof(nm)) || !strncmp(nm, "Default__", 9)) continue;
        ops[nops] = o; cnt[nops] = TraceCount(o); nops++;
    }
    scanPos = to >= go->Num ? 0 : to;
    DWORD now = GetTickCount(); if (now - lastCheck < 200) return; lastCheck = now;
    for (int k = 0; k < nops; k++) {
        BYTE* o = ops[k];
        if (!IsObjFast(o) || !TraceIsSeqOp(o)) { ops[k] = ops[--nops]; cnt[k] = cnt[nops]; k--; continue; }
        int c = TraceCount(o);
        if (c == cnt[k]) continue;
        int was = cnt[k]; cnt[k] = c;
        if (c < was) continue;
        char cn[96] = "", on[96] = ""; ClassNameOf(o, cn, sizeof(cn)); ObjLabel(o, on, sizeof(on));
        static volatile LONG nl = 0; if (g_ptrace && InterlockedIncrement(&nl) <= 5000) Log("kismet: %s %s activated (count %d)", cn, on, c);
        if (!strcmp(cn, "TnSeqAct_DownRevive")) g_scriptDownAt = now;
        KMirrorHostOp(o, cn);
        SceneNoteOp(t, o, cn);   // scene.inc: who triggered it (activator of the next cutscene)
        if (!strcmp(cn, "TnSeqAct_SetChapter") && g_chapterSync) {   // B16: the chapter title for the guests (ChapterSnap sends it)
            g_chapNum = (int)*(float*)(o + 0x8c); g_chapSeq++;
            Log("title: host chapter %d (activateCount %d)", g_chapNum, c);
        }
    }
}

// ---------------------------------------------------------------- scripted down / revive for every player (host)
static void TraceExec(BYTE* obj, const wchar_t* cmd) {
    PFN_ScriptConsoleExec fn = (PFN_ScriptConsoleExec)((BYTE*)GetModuleHandleW(nullptr) + (0xbf1f70 - 0x400000));
    char on[96]; ObjLabel(obj, on, sizeof(on));
    BOOL r = FALSE;
    __try { r = fn(obj, nullptr, cmd, g_outDev, nullptr); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("scriptdown: %s '%ls' raised %08lx", on, cmd, GetExceptionCode()); return; }
    Log("scriptdown: %s '%ls' -> %s", on, cmd, r ? "handled" : "not handled");
}
static void TickScriptDown(const TickObjs& t) {
    static bool scripted = false; static DWORD lastTry = 0;
    if (!g_scriptDown || t.ngame < 1) return;
    BYTE* host = nullptr;
    for (int a = 0; a < t.npc; a++) if (IsObjFast(t.pc[a]) && IsLocalPC(t.pc[a])) host = PawnOf(t.pc[a]);
    if (!IsObjFast(host)) return;
    bool hd = (*(unsigned*)(host + 0x5d0) & 0x80000) != 0;
    DWORD now = GetTickCount();
    bool recent = g_scriptDownAt && now - g_scriptDownAt < 4000;
    if (recent && hd && !scripted) { scripted = true; Log("scriptdown: the script downed the host -> guests follow"); }
    if (!scripted) return;
    if (now - lastTry < 1500) return; lastTry = now;
    for (int a = 0; a < t.npc; a++) {
        BYTE* pc = t.pc[a]; if (!IsObjFast(pc) || IsLocalPC(pc)) continue;
        BYTE* gp = PawnOf(pc); if (!IsObjFast(gp)) continue;
        bool gd = (*(unsigned*)(gp + 0x5d0) & 0x80000) != 0;
        if (hd && !gd) TraceExec(gp, L"TestDowned");
        else if (!hd && gd) TraceExec(gp, L"TestRevive");
    }
    if (!hd) { bool any = false; for (int a = 0; a < t.npc; a++) { BYTE* pc = t.pc[a]; if (IsObjFast(pc) && !IsLocalPC(pc)) { BYTE* gp = PawnOf(pc); if (gp && (*(unsigned*)(gp + 0x5d0) & 0x80000)) any = true; } }
        if (!any) { scripted = false; Log("scriptdown: host up again, guests up -> done"); } }
}

static void TickGfxDiag(const TickObjs& t);
static void TickTraceAll(const TickObjs& t) {
    if (g_ptrace) TickTrace(t);
    TickKismetLog(t);   // host: always (chapter title, scripted moments); logs only with ptrace
    TickScene(t);       // scene.inc: gather everybody at the activator after a cutscene
    TickScriptDown(t);
    TickGfxDiag(t);
}

// ---------------------------------------------------------------- gfxdiag (owner 2026-10-20: why shaders, light and
// effects fail on the guest). Both sides, every 30 s on a story map and on the command `gfxdiag`: Binaries\foc_gfx.txt
// gets the map, role, where the local player stands and looks, the D3D9 counts of the last frame (draw calls,
// primitives, texture and pixel shader sets, null ones) and a census of every object class with its instance count.
// tools/gfxdiff.ps1 compares the host's and the guest's files (classes the guest has fewer of or none).
static void GfxDiagWrite(const TickObjs& t, const char* why) {
    static int cnt[8192]; static BYTE* sample[8192];
    memset(cnt, 0, sizeof(cnt)); memset(sample, 0, sizeof(sample));
    TArr* go = GObj(); int total = 0;
    for (int i = 0; i < go->Num; i++) {
        BYTE* o = (BYTE*)go->Data[i]; if (!o) continue;
        unsigned cid = *(unsigned*)(o + 0x14) & 0x1fff;
        if (!cnt[cid]) sample[cid] = o;
        cnt[cid]++; total++;
    }
    wchar_t dir[MAX_PATH]; GetModuleFileNameW(nullptr, dir, MAX_PATH); wchar_t* sl = wcsrchr(dir, L'\\'); if (sl) *sl = 0;
    wchar_t path[MAX_PATH]; _snwprintf_s(path, _TRUNCATE, L"%s\\foc_gfx.txt", dir);
    FILE* f = nullptr; _wfopen_s(&f, path, L"w"); if (!f) return;
    char map[96]; MapName(t, map, sizeof(map));
    fprintf(f, "gfxdiag %s t=%lu map=%s role=%s objects=%d\n", why, GetTickCount() - g_t0, map, t.ngame ? "host" : "client", total);
    for (int a = 0; a < t.npc; a++) {
        BYTE* pc = t.pc[a]; if (!IsObjFast(pc) || !IsLocalPC(pc)) continue;
        BYTE* pw = PawnOf(pc); float* l = pw ? LocOf(pw) : nullptr; int cr[3]; TraceRot(pc, cr);
        char vn[96] = "-"; ObjLabel(*(BYTE**)(pc + 0x378), vn, sizeof(vn));
        fprintf(f, "player at %.0f,%.0f,%.0f view yaw %d pitch %d target %s\n", l ? l[0] : 0.f, l ? l[1] : 0.f, l ? l[2] : 0.f, cr[1], cr[0], vn);
    }
    fprintf(f, "d3d frame: draws %ld prims %ld texsets %ld nulltex %ld pssets %ld nullps %ld\n", g_gfxFrame[0], g_gfxFrame[1], g_gfxFrame[2], g_gfxFrame[3], g_gfxFrame[4], g_gfxFrame[5]);
    for (int c = 0; c < 8192; c++) {
        if (!cnt[c] || !sample[c]) continue;
        char cn[96] = ""; if (!ClassNameOf(sample[c], cn, sizeof(cn)) || !cn[0]) _snprintf_s(cn, _TRUNCATE, "class#%d", c);
        fprintf(f, "class %s %d\n", cn, cnt[c]);
    }
    fclose(f);
    Log("gfxdiag: %s map=%s objects=%d d3d draws %ld prims %ld texsets %ld nulltex %ld pssets %ld nullps %ld -> foc_gfx.txt", why, map, total,
        g_gfxFrame[0], g_gfxFrame[1], g_gfxFrame[2], g_gfxFrame[3], g_gfxFrame[4], g_gfxFrame[5]);
}
static void TickGfxDiag(const TickObjs& t) {
    static DWORD last = 0;
    bool req = InterlockedExchange(&g_gfxDiagReq, 0) != 0;
    if (!req && (!g_ptrace || !StoryMapNow(t) || GetTickCount() - last < 30000)) return;
    last = GetTickCount();
    GfxDiagWrite(t, req ? "command" : "auto");
}
