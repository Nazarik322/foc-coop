// scene.inc (owner 2026-10-24): "if one player activates something - an animation, a cutscene or anything else - it
// counts for everybody, and after the scene everybody moves to the one who activated it". The host is the server: its
// Kismet runs every trigger, so whatever a guest touches or uses already counts for all; what was missing is the
// gathering after the scene (players stood wherever they were, often in another streamed area).
// Host only. Activator: the player whose pawn (or controller) is the Instigator of the last Kismet event (Touch, Used,
// trigger volume, ...) that fired within 15 s before a cutscene started. A cutscene = any player controller in
// cinematic mode (PC +0x360 bit 0x20000, set by the scene's Kismet; cinesync mirrors it to the guests). 1.5 s after the
// last controller left cinematic mode, every other player's pawn is moved next to the activator's pawn (tp.inc
// TpGuestNear: native SetLocation with free spots around the target). Ini `scenetp` (default 1).
// Test commands: `sceneact <n>` = the n-th player controller is the activator of the next scene (0 = host),
// `scenegather` = gather everybody at the activator now.
static MirrorProp g_spInstigator = {}, g_spController = {};
static MirrorProp g_tpTrigCount = {};   // SequenceEvent.TriggerCount (trace.inc counts events with it)
static BYTE* g_sceneActPc = nullptr; static DWORD g_sceneActAt = 0; static char g_sceneActWhy[96] = "";
static int g_sceneForced = -1;   // sceneact: controller index for the next scene

// PlayerReplicationInfo.PlayerID of a controller (replicated: the same number on the host and on that guest); -1 unknown.
// sync.inc puts it into the guest's position reports so the host keeps them per guest.
static int PriPlayerId(BYTE* pc) {
    static MirrorProp pid = {};
    if (!IsObjFast(pc)) return -1;
    BYTE* pri = *(BYTE**)(pc + 0x1d4);
    if (!IsObjFast(pri)) return -1;
    if (pid.off <= 0 && !MirrorPropFind("PlayerReplicationInfo", "PlayerID", pid)) return -1;
    return Rd(pri + pid.off, 4) ? *(int*)(pri + pid.off) : -1;
}
static BYTE* ScenePcOf(const TickObjs& t, BYTE* a) {   // the player controller behind an actor (pawn, form, controller)
    if (!IsObjFast(a)) return nullptr;
    if (g_spController.off <= 0) MirrorPropFind("Pawn", "Controller", g_spController);
    for (int i = 0; i < t.npc; i++) {
        BYTE* pc = t.pc[i]; if (!IsObjFast(pc)) continue;
        if (pc == a || PawnOf(pc) == a) return pc;
    }
    if (g_spController.off > 0 && Rd(a + g_spController.off, 4)) {   // a vehicle form or another pawn the player drives
        BYTE* c = *(BYTE**)(a + g_spController.off);
        for (int i = 0; i < t.npc; i++) if (t.pc[i] == c) return c;
    }
    return nullptr;
}
// trace.inc TickKismetLog: every op activation the tracker sees (host, game thread)
static void SceneNoteOp(const TickObjs& t, BYTE* op, const char* cls) {
    if (!strstr(cls, "SeqEvent") && !strstr(cls, "SeqEvt")) return;
    if (strstr(cls, "PlayerSpawned")) return;   // a (re)spawn is no action: the intro after it would gather at the last one spawned
    // "Instigator" exists in many classes and the reflection search picked another class' offset (always null). In
    // SequenceEvent.uc the fields are Originator, Instigator, ActivationTime, TriggerCount: Instigator = TriggerCount - 8
    // (TriggerCount +0x84 found exactly by trace.inc; candidate +0x7c was in the list, 2026-10-24).
    if (g_spInstigator.off <= 0) {
        if (g_tpTrigCount.off <= 0 && !MirrorPropFind("SequenceEvent", "TriggerCount", g_tpTrigCount)) return;
        g_spInstigator.off = g_tpTrigCount.off - 8;
        Log("scene: SequenceEvent.Instigator at +0x%x (TriggerCount +0x%x)", g_spInstigator.off, g_tpTrigCount.off);
    }
    if (!Rd(op + g_spInstigator.off, 4)) return;
    BYTE* inst = *(BYTE**)(op + g_spInstigator.off);
    BYTE* pc = ScenePcOf(t, inst);
    if (!pc) {
        static volatile LONG nu = 0;
        if (InterlockedIncrement(&nu) <= 40) { char on[96]; ObjLabel(op, on, sizeof(on)); char in[96] = "none"; if (IsObjFast(inst)) ObjLabel(inst, in, sizeof(in)); Log("scene: event %s (%s) instigator %s - not a player", on, cls, in); }
        return;
    }
    g_sceneActPc = pc; g_sceneActAt = GetTickCount();
    char on[96]; ObjLabel(op, on, sizeof(on)); char pn[64]; ObjLabel(pc, pn, sizeof(pn));
    _snprintf_s(g_sceneActWhy, _TRUNCATE, "%s by %s", on, pn);
    static volatile LONG nl = 0; if (InterlockedIncrement(&nl) <= 300) Log("scene: event %s (%s) by %s", on, cls, pn);
}
// a guest's pawn was moved on the server: move its own copy too (ClientSetLocation RPC), else the guest's next move
// undid it and reconcile put the server copy back (4 copies, 2026-10-24); no reconcile for 6 s meanwhile
static void SceneClientMove(BYTE* pc, BYTE* p) {
    g_reconcileHold = GetTickCount() + 6000;
    if (IsLocalPC(pc)) return;
    DWORD parms[16] = {0}; memcpy(parms, LocOf(p), 12); memcpy(parms + 3, p + 0xf0, 12);   // NewLocation, NewRotation
    bool ok = CallScript(pc, "ClientSetLocation", parms, sizeof(parms));
    char pn[64]; ObjLabel(pc, pn, sizeof(pn)); Log("scene: %s.ClientSetLocation (%.0f,%.0f,%.0f) %s", pn, LocOf(p)[0], LocOf(p)[1], LocOf(p)[2], ok ? "ok" : "failed");
}
// where the player sees its pawn: a guest's own report when there is one, else the server copy
static const float* SceneViewPos(BYTE* pc, BYTE* p, GuestPos* tmp) {
    if (!IsLocalPC(pc) && TpGuestReport(p, tmp)) return tmp->p;
    return LocOf(p);
}
static void SceneGather(const TickObjs& t, BYTE* actPc, const char* why) {
    BYTE* ap = actPc ? PawnOf(actPc) : nullptr;
    if (!ap) { Log("scene: %s - the activator has no pawn, nobody moved", why); return; }
    GuestPos ar; const float* at = SceneViewPos(actPc, ap, &ar);
    int moved = 0, nearby = 0;
    for (int i = 0; i < t.npc; i++) {
        BYTE* pc = t.pc[i]; if (!IsObjFast(pc) || pc == actPc) continue;
        BYTE* p = PawnOf(pc); if (!p) continue;
        GuestPos r; const float* v = SceneViewPos(pc, p, &r);
        if (TpDist(LocOf(p), LocOf(ap)) < 1200.0f && TpDist(v, at) < 1200.0f) { nearby++; continue; }
        if (!TpGuestNear(ap, p, nullptr, IsLocalPC(pc) ? "scene host" : "scene guest")) continue;
        moved++;
        SceneClientMove(pc, p);
    }
    char an[64]; ObjLabel(actPc, an, sizeof(an));
    Log("scene: %s - gathered at %s: %d moved, %d already near", why, an, moved, nearby);
}
static void TickScene(const TickObjs& t) {
    static int lastCine = -1; static BYTE* sceneAct = nullptr; static DWORD endAt = 0; static BYTE* world = nullptr;
    if (!g_sceneTp || t.ngame < 1 || t.npc < 2 || t.nuworld < 1) { lastCine = -1; return; }
    if (world != t.uworld[0]) { world = t.uworld[0]; lastCine = -1; sceneAct = nullptr; endAt = 0; g_sceneActPc = nullptr; }
    bool local = false; for (int i = 0; i < t.npc; i++) if (IsLocalPC(t.pc[i])) local = true;
    if (!local) return;   // a guest: the host decides
    int cine = 0; for (int i = 0; i < t.npc; i++) if (IsObjFast(t.pc[i]) && (*(unsigned*)(t.pc[i] + 0x360) & 0x20000)) cine = 1;
    DWORD now = GetTickCount();
    if (cine != lastCine) {
        if (cine && lastCine == 0) {
            if (g_sceneForced >= 0 && g_sceneForced < t.npc) { sceneAct = t.pc[g_sceneForced]; Log("scene: cutscene started, activator forced to controller %d", g_sceneForced); g_sceneForced = -1; }
            else if (g_sceneActPc && now - g_sceneActAt < 15000) { sceneAct = g_sceneActPc; Log("scene: cutscene started, activator %s (%lu ms before)", g_sceneActWhy, now - g_sceneActAt); }
            else { sceneAct = nullptr; Log("scene: cutscene started, no player event before it (level script) - nobody will be moved"); }
            endAt = 0;
        } else if (!cine && lastCine == 1) { endAt = now + 1500; Log("scene: cutscene ended"); }
        lastCine = cine;
    }
    if (endAt && (LONG)(now - endAt) >= 0 && !cine) {
        endAt = 0;
        bool alive = false; for (int i = 0; i < t.npc; i++) if (t.pc[i] == sceneAct) alive = true;
        if (sceneAct && alive) SceneGather(t, sceneAct, "after the cutscene");
        sceneAct = nullptr;
    }
}
static void SceneCommand(const TickObjs& t, const char* verb, const char* args) {
    if (!strcmp(verb, "sceneact")) { g_sceneForced = atoi(args); Log("scene: next activator = controller %d", g_sceneForced); }
    else if (!strcmp(verb, "scenemove")) {   // `scenemove <n> dx dy dz` (tests): move controller n's pawn on the server
        int n = -1; float o[3]; if (sscanf_s(args, "%d %f %f %f", &n, &o[0], &o[1], &o[2]) != 4 || n < 0 || n >= t.npc) { Log("scene: usage scenemove <n> dx dy dz"); return; }
        BYTE* p = PawnOf(t.pc[n]); if (!p) { Log("scene: controller %d has no pawn", n); return; }
        float* l = LocOf(p); float d[3] = {l[0] + o[0], l[1] + o[1], l[2] + o[2]};
        bool ok = TpNativeSetLocation(p, d);
        if (ok) SceneClientMove(t.pc[n], p);
        Log("scene: scenemove %d -> (%.0f,%.0f,%.0f) ret=%d now (%.0f,%.0f,%.0f)", n, d[0], d[1], d[2], (int)ok, LocOf(p)[0], LocOf(p)[1], LocOf(p)[2]);
    }
    else if (!strcmp(verb, "scenegather")) {
        int n = args && args[0] ? atoi(args) : -1;
        BYTE* pc = (n >= 0 && n < t.npc) ? t.pc[n] : g_sceneActPc;
        SceneGather(t, pc, "scenegather");
    }
}
