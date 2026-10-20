// story_title.inc - B16: chapter title ("CHAPTER 10 THE FINAL COUNTDOWN") on both screens.
// Included after sync.inc (uses ShowChapterLocal, CallScript, the sync snapshot helpers).

// `objscan <Class> [first-index]` (foc_cmd.txt, game thread): log the object pointers found in the first instance of
// a class object (the class object itself, offsets 0x20..0x140) with the names they point to. Used to find
// UStruct.SuperStruct and the like.
static void ObjScanCommand(const char* args) {
    char cls[80] = ""; sscanf_s(args, "%79s", cls, (unsigned)sizeof(cls));
    unsigned cid = ClassIdByName(cls);
    BYTE* c = cid && cid < 8192 ? g_classObjById[cid] : nullptr;
    if (!strncmp(cls, "fn:", 3)) {   // the first Function object with that name
        unsigned fid = ClassIdByName("Function"); TArr* go = GObj(); c = nullptr;
        for (int i = 0; i < go->Num && !c; i++) {
            BYTE* o = (BYTE*)go->Data[i]; char n2[96];
            if (o && (*(unsigned*)(o + 0x14) & 0x1fff) == fid && ObjNameStr(o, n2, sizeof(n2)) && !strcmp(n2, cls + 3)) c = o;
        }
    }
    if (!IsObjFast(c)) { Log("objscan: no class object for '%s' (cid %u)", cls, cid); return; }
    char nm[96]; ObjLabel(c, nm, sizeof(nm)); Log("objscan: %s at %p cid %u", nm, c, cid);
    {   // header words, also read as GObjects indexes (outer / archetype / class links may be stored that way)
        TArr* go = GObj();
        for (int k = 0; k < 0x30; k += 4) {
            unsigned w = *(unsigned*)(c + k); char r[200] = "";
            unsigned cand[3] = {w & 0x3ffff, (w >> 14) & 0x3ffff, w >> 13};
            for (int j = 0; j < 3; j++) {
                for (int d = 0; d < 2; d++) {
                    unsigned ix = cand[j] - d;
                    if (!ix || ix >= (unsigned)go->Num || !IsObjFast((BYTE*)go->Data[ix])) continue;
                    char n3[96]; ObjLabel((BYTE*)go->Data[ix], n3, sizeof(n3));
                    size_t l = strlen(r); _snprintf_s(r + l, sizeof(r) - l, _TRUNCATE, " [%d-%d:%s]", j, d, n3);
                }
            }
            Log("objscan: word +0x%x = %08x%s", k, w, r);
        }
    }
    if (!strncmp(cls, "fn:", 3)) for (int k = 0x28; k < 0x90; k += 16) Log("objscan: raw +0x%x: %08x %08x %08x %08x", k, *(unsigned*)(c + k), *(unsigned*)(c + k + 4), *(unsigned*)(c + k + 8), *(unsigned*)(c + k + 12));
    for (int k = 0x8; k < 0x140; k += 4) {
        BYTE* p = *(BYTE**)(c + k);
        if (!IsObjFast(p)) continue;
        char pn[96]; ObjLabel(p, pn, sizeof(pn));
        Log("objscan: +0x%x -> %s", k, pn);
    }
}

// The Function object "Activated" exists once per class that overrides the event (SequenceOp, TnSeqAct_SetChapter, ...).
// Its owner class is the word at +0x28 (same as FindScriptFunction reads). Pick the one owned by TnSeqAct_SetChapter;
// g_chapFnPick >= 0 (test command `chapter <n> fn<k>`) forces the k-th candidate instead. All candidates are logged once.
static int g_chapFnPick = -1;
static BYTE* ActivatedFunction() {
    unsigned fid = ClassIdByName("Function"), cid = ClassIdByName("TnSeqAct_SetChapter"); TArr* go = GObj();
    BYTE* own = cid && cid < 8192 ? g_classObjById[cid] : nullptr; BYTE* best = nullptr; BYTE* forced = nullptr; int k = 0;
    static bool logged = false;
    for (int i = 0; i < go->Num; i++) {
        BYTE* o = (BYTE*)go->Data[i]; char n[96];
        if (!o || (*(unsigned*)(o + 0x14) & 0x1fff) != fid || !ObjNameStr(o, n, sizeof(n)) || strcmp(n, "Activated")) continue;
        BYTE* w28 = *(BYTE**)(o + 0x28); char ow[96] = "?"; if (IsObjFast(w28)) ObjLabel(w28, ow, sizeof(ow));
        if (!logged) Log("chapter: Activated candidate %d at %p index %d owner %p %s parmsSize %u", k, o, i, w28, ow, (unsigned)*(unsigned short*)(o + 0x5e));
        if (w28 == own && !best) best = o;
        if (k == g_chapFnPick) forced = o;
        k++;
    }
    logged = true;
    if (!best && !forced && k == 1) { Log("chapter: only one Activated Function exists (owner word at +0x28 is null): using it; SetChapter's own override has no Function object"); best = (BYTE*)0; for (int i = 0; i < go->Num && !best; i++) { BYTE* o = (BYTE*)go->Data[i]; char n[96]; if (o && (*(unsigned*)(o + 0x14) & 0x1fff) == fid && ObjNameStr(o, n, sizeof(n)) && !strcmp(n, "Activated")) best = o; } }
    return forced ? forced : best;
}
static const int kChapterTargets = 0x80;
// all TnSeqAct_SetChapter objects of the level (instances, not Default__); returns the count
static int ChapterActions(BYTE** out, int cap) {
    unsigned cid = ClassIdByName("TnSeqAct_SetChapter"); if (!cid) return 0;
    TArr* go = GObj(); int n = 0;
    for (int i = 0; i < go->Num && n < cap; i++) {
        BYTE* o = (BYTE*)go->Data[i];
        if (!o || (*(unsigned*)(o + 0x14) & 0x1fff) != cid) continue;
        char nm[96]; if (!ObjNameStr(o, nm, sizeof(nm)) || !strncmp(nm, "Default__", 9)) continue;
        out[n++] = o;
    }
    return n;
}
// Runs the level's own TnSeqAct_SetChapter (ChapterNumber == number) for the local controller only:
// the action's Targets TArray (+0x80 in TnSeqAct_SetChapter, checked on the host: Data +0x80, Num +0x84, ChapterNumber +0x8c) is pointed at the local PC for the call.
static bool ShowChapter2(const TickObjs& t, float number, bool pcTarget = false, bool alt = false) {
    BYTE* pc = nullptr; for (int a = 0; a < t.npc; a++) if (IsLocalPC(t.pc[a])) pc = t.pc[a];
    if (!pc) { Log("chapter: no local controller"); return false; }
    BYTE* acts[16]; int n = ChapterActions(acts, 16); BYTE* act = nullptr;
    for (int i = alt ? n - 1 : 0; i >= 0 && i < n && (!act || alt); i += alt ? -1 : 1) if (*(float*)(acts[i] + 0x8c) == number) { act = acts[i]; break; }
    if (!act) { Log("chapter: no TnSeqAct_SetChapter with number %.1f (%d actions in the level)", number, n); return false; }
    BYTE* f = ActivatedFunction();
    if (f) Log("chapter: using Activated function %p", f);
    if (!f) { Log("chapter: no Activated function object"); return false; }
    BYTE* pawn = PawnOf(pc);
    static void* target[1]; target[0] = pcTarget || !pawn ? pc : pawn;   // the host's own action targets the player's pawn
    TArr* tg = (TArr*)(act + kChapterTargets); TArr saved = *tg;
    tg->Data = target; tg->Num = 1; tg->Max = 1;
    static DWORD parms[8]; memset(parms, 0, sizeof(parms));
    bool ok = true;
    __try { CallThis3((BYTE*)GetModuleHandleW(nullptr) + (0x728c60 - 0x400000), act, (DWORD)(UINT_PTR)f, (DWORD)(UINT_PTR)parms, 0); }
    __except (EXCEPTION_EXECUTE_HANDLER) { Log("chapter: Activated raised %08lx", GetExceptionCode()); ok = false; }
    *tg = saved;
    char an[96]; ObjLabel(act, an, sizeof(an));
    Log("chapter: %s (%.1f).Activated for the local player %s", an, number, ok ? "ok" : "failed");
    return ok;
}
// `chapters` (foc_cmd.txt, game thread): log every TnSeqAct_SetChapter of the level: ChapterNumber (+0x8c),
// SequenceOp.ActivateCount (+0x70), Targets
static void ChaptersCommand() {
    BYTE* acts[16]; int n = ChapterActions(acts, 16);
    for (int i = 0; i < n; i++) {
        char an[96]; ObjLabel(acts[i], an, sizeof(an)); BYTE* o = acts[i];
        TArr* tg = (TArr*)(o + kChapterTargets); char t0[96] = "", t1[96] = "";
        if (tg->Num > 0 && tg->Num < 16 && Rd(tg->Data, 8)) { ObjLabel((BYTE*)tg->Data[0], t0, sizeof(t0)); if (tg->Num > 1) ObjLabel((BYTE*)tg->Data[1], t1, sizeof(t1)); }
        Log("chapters: %s number=%.1f activateCount=%d targets=%d [%s] [%s]", an, *(float*)(o + 0x8c), *(int*)(o + 0x70), tg->Num, t0, t1);
    }
    Log("chapters: %d actions", n);
}

// `vtdiff <ClassA> <ClassB>` (foc_cmd.txt, game thread): compare the vtables of the two classes' default objects
// (class object +0x9c) and log the slots that differ, as ghidra addresses. Finds a class's native overrides.
static void VtDiffCommand(const char* args) {
    char a[80] = "", b[80] = ""; sscanf_s(args, "%79s %79s", a, (unsigned)sizeof(a), b, (unsigned)sizeof(b));
    BYTE* cl[2]; const char* nm[2] = {a, b}; BYTE** vt[2]; BYTE* base = (BYTE*)GetModuleHandleW(nullptr);
    for (int i = 0; i < 2; i++) {
        unsigned cid = ClassIdByName(nm[i]); cl[i] = cid && cid < 8192 ? g_classObjById[cid] : nullptr;
        BYTE* def = IsObjFast(cl[i]) ? *(BYTE**)(cl[i] + 0x9c) : nullptr;
        if (!IsObjFast(def)) { Log("vtdiff: no default object for '%s'", nm[i]); return; }
        vt[i] = *(BYTE***)def;
    }
    int n = 0;
    for (int k = 0; k < 200 && Rd(vt[0] + k, 4) && Rd(vt[1] + k, 4); k++) {
        if (vt[0][k] == vt[1][k]) continue;
        Log("vtdiff: slot %d (+0x%x): %s %lx, %s %lx", k, k * 4, a, (unsigned long)(0x400000 + (vt[0][k] - base)), b, (unsigned long)(0x400000 + (vt[1][k] - base))); n++;
    }
    Log("vtdiff: %d slots differ (vtables %p %p)", n, vt[0], vt[1]);
}

// ---------------------------------------------------------------- host -> guest (chaptersync)
// Host: poll SequenceOp.ActivateCount (+0x70) of the level's TnSeqAct_SetChapter actions; a rise means the Kismet ran it
// (the host's HUD shows the title). The number and a counter go into the snapshot (chap=, chapseq=); the guest runs
// ShowChapter2 once per new chapseq.
static int g_chapSeq = 0; static int g_chapNum = 0;
static int ChapterSnap(const TickObjs& t, char* out, size_t n) {
    if (!g_chapterSync) return 0;
    static BYTE* seen[16]; static int cnt[16]; static int ns = 0;
    BYTE* acts[16]; int na = ChapterActions(acts, 16);
    for (int i = 0; i < na; i++) {
        int k = 0; while (k < ns && seen[k] != acts[i]) k++;
        int c = *(int*)(acts[i] + 0x70);
        if (k == ns && ns < 16) { seen[ns] = acts[i]; cnt[ns] = c; ns++; continue; }   // first sight: baseline
        if (k < ns && c > cnt[k]) {
            cnt[k] = c; g_chapNum = (int)*(float*)(acts[i] + 0x8c); g_chapSeq++;
            Log("title: host chapter %d (activateCount %d)", g_chapNum, c);
        } else if (k < ns) cnt[k] = c;
    }
    if (!g_chapSeq) return 0;
    return _snprintf_s(out, n, _TRUNCATE, "chap=%d\nchapseq=%d\n", g_chapNum, g_chapSeq);
}
static void ChapterApply(const TickObjs& t, const char* body) {
    if (!g_chapterSync) return;
    static int applied = 0; static DWORD retryAt = 0;
    int seq = SyncGetInt(body, "chapseq", 0); if (seq == applied || !seq) return;
    if (GetTickCount() < retryAt) return;
    int num = SyncGetInt(body, "chap", -1);
    if (ShowChapter2(t, (float)num)) { applied = seq; Log("title: guest shows chapter %d (seq %d)", num, seq); }
    else retryAt = GetTickCount() + 2000;   // no local controller / action yet
}

// ---------------------------------------------------------------- GFx probes (B16: Scaleform route)
static bool IContains(const char* h, const char* nd) { size_t l = strlen(nd); for (; *h; h++) if (_strnicmp(h, nd, l) == 0) return true; return false; }
static bool IsPropObj(BYTE* p) {
    if (!IsObjFast(p)) return false; char cn[80] = "?"; ClassNameOf(p, cn, sizeof(cn)); return strstr(cn, "Property") != nullptr;
}
// `fnlist <substr> [substr..]` (game thread): every Function object whose name contains one of the substrings
// (case-insensitive): pointer, ParmsSize (+0x5e), and the parameter properties reachable from it (the first
// property-class pointer in words +0x20..+0x80, then each property's own next pointer in words +0x20..+0x40) with offset
// ((word +0x30) >> 20), class and name.
static void FnListCommand(const char* args) {
    char want[6][48]; int nw = 0;
    for (const char* p = args; *p && nw < 6;) { while (*p == ' ') p++; int k = 0; while (*p && *p != ' ' && k < 47) want[nw][k++] = *p++; want[nw][k] = 0; if (k) nw++; }
    unsigned fid = ClassIdByName("Function"); TArr* go = GObj(); int found = 0;
    for (int i = 0; i < go->Num && found < 120; i++) {
        BYTE* o = (BYTE*)go->Data[i]; char n[96];
        if (!o || (*(unsigned*)(o + 0x14) & 0x1fff) != fid || !ObjNameStr(o, n, sizeof(n))) continue;
        bool hit = false; for (int w = 0; w < nw; w++) if (IContains(n, want[w])) hit = true;
        if (!hit) continue;
        char line[900]; size_t k = _snprintf_s(line, _TRUNCATE, "fnlist: %s %p idx %d parmsSize %u flags %08x:", n, o, i, (unsigned)*(unsigned short*)(o + 0x5e), *(unsigned*)(o + 0x58));
        BYTE* pr = nullptr;
        for (int off = 0x20; off < 0x80 && !pr; off += 4) { BYTE* c = *(BYTE**)(o + off); if (IsPropObj(c)) pr = c; }
        for (int s = 0; s < 12 && pr; s++) {
            char pn[64] = "?", cn[64] = "?"; ObjNameStr(pr, pn, sizeof(pn)); ClassNameOf(pr, cn, sizeof(cn));
            k += _snprintf_s(line + k, sizeof(line) - k, _TRUNCATE, " [%s %s @%x]", cn, pn, *(unsigned*)(pr + 0x30) >> 20);
            BYTE* nx = nullptr; for (int off = 0x20; off < 0x40 && !nx; off += 4) { BYTE* c = *(BYTE**)(pr + off); if (c != pr && IsPropObj(c)) nx = c; }
            pr = nx;
        }
        Log("%s", line); found++;
    }
    Log("fnlist: %d function(s)", found);
}
// `hudscan` (game thread): the local controller's myHUD (+0x3b4): class, and every object pointer in its first 0x300
// bytes whose class name contains "Movie" or "GFx", with the offset
static void HudScanCommand() {
    BYTE* pc = nullptr; for (int a = 0; a < g_to.npc; a++) if (IsLocalPC(g_to.pc[a])) pc = g_to.pc[a];
    if (!pc) { Log("hudscan: no local controller"); return; }
    BYTE* h = *(BYTE**)(pc + 0x3b4); if (!IsObjFast(h)) { Log("hudscan: myHUD null"); return; }
    char hn[96]; ObjLabel(h, hn, sizeof(hn)); Log("hudscan: myHUD %p %s", h, hn);
    for (int off = 0x20; off < 0x400; off += 4) {
        BYTE* c = *(BYTE**)(h + off); if (!IsObjFast(c)) continue;
        char cn[80] = "?"; ClassNameOf(c, cn, sizeof(cn));
        if (!strstr(cn, "Movie") && !strstr(cn, "GFx")) continue;
        char nm[96]; ObjLabel(c, nm, sizeof(nm)); Log("hudscan: +0x%x -> %p %s (class %s)", off, c, nm, cn);
    }
}
