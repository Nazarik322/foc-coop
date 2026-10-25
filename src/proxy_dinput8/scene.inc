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
// A player in a scripted moment: cinematic mode, matinee control (AnimControl) or the script walking it (InputDisabled:
// the chapter 2 "walk through the fire" - TnSeqEvent_WalkToLocation - had no cinematic mode, real test 2026-10-25).
static bool SceneScripted(BYTE* pc) {
    if (!IsObjFast(pc)) return false;
    if (*(unsigned*)(pc + 0x360) & 0x20000) return true;
    char st[48]; StateName(pc, st, sizeof(st));
    return !strcmp(st, "AnimControl") || !strcmp(st, "InputDisabled");
}

// ---------------------------------------------------------------- protection (real test 2026-10-25)
// The host walked through the fire under script control (no damage), the guest walked through it by itself, burned,
// went down and died (environment damage kills a downed pawn: TnPawn.TakeDamageHealthBelowZero), and every death
// restarted the checkpoint - three times. Now (host, server side) Controller.bGodMode (GameInfo.ReduceDamage zeroes the
// damage) is on for: every downed player while somebody is still up (a downed player waits for a revive forever; the
// game ends only when everybody is down - TickDowned g_allDown), and every other player while one player is in a
// scripted moment. Only the flags set here are cleared again. Ini scenetp (with the gathering).
static MirrorProp g_spGod = {};
static void TickProtect(const TickObjs& t) {
    static BYTE* godPc[8]; static int ngod = 0;
    static bool told = false;
    if (g_spGod.off <= 0) MirrorPropFind("Controller", "_bGodMode", g_spGod);
    if (!told) { told = true; Log("protect: Controller._bGodMode +0x%x mask %x", g_spGod.off, g_spGod.mask); }
    if (g_spGod.off <= 0 || !g_spGod.mask) return;
    BYTE* scripted = nullptr; for (int i = 0; i < t.npc; i++) if (PawnOf(t.pc[i]) && SceneScripted(t.pc[i])) scripted = t.pc[i];
    for (int i = 0; i < t.npc; i++) {
        BYTE* pc = t.pc[i]; BYTE* p = PawnOf(pc); if (!p || !Rd(pc + g_spGod.off, 4)) continue;
        bool downed = PawnDowned(p) && !g_allDown;
        bool shield = scripted && pc != scripted && !SceneScripted(pc);
        bool want = downed || shield;
        unsigned* w = (unsigned*)(pc + g_spGod.off);
        int k = -1; for (int j = 0; j < ngod; j++) if (godPc[j] == pc) k = j;
        if (want && k < 0) {
            if (*w & g_spGod.mask) continue;   // god mode from elsewhere (cheat): not ours
            *w |= g_spGod.mask; if (ngod < 8) godPc[ngod++] = pc;
            char pn[64]; ObjLabel(pc, pn, sizeof(pn)); Log("protect: %s invulnerable (%s)", pn, downed ? "downed, waiting for a revive" : "another player's scripted moment");
        } else if (!want && k >= 0) {
            *w &= ~g_spGod.mask; godPc[k] = godPc[--ngod];
            char pn[64]; ObjLabel(pc, pn, sizeof(pn)); Log("protect: %s vulnerable again", pn);
        }
    }
    for (int j = ngod - 1; j >= 0; j--) { bool there = false; for (int i = 0; i < t.npc; i++) if (t.pc[i] == godPc[j]) there = true; if (!there) godPc[j] = godPc[--ngod]; }
}

// ---------------------------------------------------------------- abilities (real test 2026-10-25)
// TnSeqAct_TogglePlayerActions (transform, shoot, jump, melee, abilities, weapon) changes only Targets[0] - the host's
// pawn (hostfirst) - so a guest could transform before the host was allowed and could not shoot or transform after the
// host got the weapon. The flags are replicated TnPawn bools: the host copies every change of its own flags to each
// guest pawn (and all of them to a new guest pawn) and marks the pawn for a network update. Not while the host is down.
static const char* kAbilityFlags[] = { "_TransformingEnabled", "_JumpingEnabled", "_MeleeEnabled", "_ShootingEnabled",
    "_Ability0Enabled", "_Ability1Enabled", "_WeaponEquipEnabled", "_HandednessEnabled" };
static const int kNAbility = sizeof(kAbilityFlags) / sizeof(kAbilityFlags[0]);
static MirrorProp g_spAbility[8], g_spNetDirty = {}, g_spForceNet = {};
static void TickAbilities(const TickObjs& t) {
    static bool ok = false, tried = false; static int last[8]; static BYTE* lastHostPawn = nullptr; static BYTE* known[8]; static int nknown = 0;
    if (!tried) {
        tried = true; ok = true;
        for (int i = 0; i < kNAbility; i++) if (!MirrorPropFind("TnPawn", kAbilityFlags[i], g_spAbility[i]) || !g_spAbility[i].mask) { ok = false; Log("abilities: TnPawn.%s not found", kAbilityFlags[i]); }
        MirrorPropFind("Actor", "bNetDirty", g_spNetDirty); MirrorPropFind("Actor", "bForceNetUpdate", g_spForceNet);
        Log("abilities: flags %s, bNetDirty +0x%x/%x, bForceNetUpdate +0x%x/%x", ok ? "found" : "missing", g_spNetDirty.off, g_spNetDirty.mask, g_spForceNet.off, g_spForceNet.mask);
    }
    if (!ok) return;
    BYTE* hp = nullptr; for (int i = 0; i < t.npc; i++) if (IsLocalPC(t.pc[i])) hp = PawnOf(t.pc[i]);
    if (!hp || PawnDowned(hp)) return;
    int cur[8]; for (int i = 0; i < kNAbility; i++) cur[i] = (*(unsigned*)(hp + g_spAbility[i].off) & g_spAbility[i].mask) ? 1 : 0;
    bool hostNew = hp != lastHostPawn;
    if (hostNew) { lastHostPawn = hp; for (int i = 0; i < kNAbility; i++) last[i] = cur[i]; nknown = 0; }
    char changed[200] = ""; size_t ck = 0;
    for (int i = 0; i < kNAbility; i++) if (cur[i] != last[i]) ck += _snprintf_s(changed + ck, sizeof(changed) - ck, _TRUNCATE, " %s=%d", kAbilityFlags[i] + 1, cur[i]);
    for (int a = 0; a < t.npc; a++) {
        BYTE* pc = t.pc[a]; if (!IsObjFast(pc) || IsLocalPC(pc)) continue;
        BYTE* gp = PawnOf(pc); if (!gp || PawnDowned(gp)) continue;
        bool isNew = true; for (int j = 0; j < nknown; j++) if (known[j] == gp) isNew = false;
        if (!isNew && !changed[0]) continue;
        int set = 0;
        for (int i = 0; i < kNAbility; i++) {
            if (!isNew && cur[i] == last[i]) continue;   // only what the host's script changed
            unsigned* w = (unsigned*)(gp + g_spAbility[i].off);
            bool have = (*w & g_spAbility[i].mask) != 0;
            if (have != (cur[i] != 0)) { if (cur[i]) *w |= g_spAbility[i].mask; else *w &= ~g_spAbility[i].mask; set++; }
        }
        if (set) {
            if (g_spForceNet.off > 0 && g_spForceNet.mask) *(unsigned*)(gp + g_spForceNet.off) |= g_spForceNet.mask;
        }
        if (isNew && nknown < 8) known[nknown++] = gp;
        char gn[64]; ObjLabel(gp, gn, sizeof(gn));
        if (set) Log("abilities: %s %d flag(s) copied from the host (%s)", gn, set, isNew ? "new guest pawn" : changed);
    }
    for (int i = 0; i < kNAbility; i++) last[i] = cur[i];
}

static void TickScene(const TickObjs& t) {
    static int lastCine = -1; static BYTE* sceneAct = nullptr; static DWORD endAt = 0; static BYTE* world = nullptr;
    if (!g_sceneTp || t.ngame < 1 || t.npc < 2 || t.nuworld < 1) { lastCine = -1; return; }
    if (world != t.uworld[0]) { world = t.uworld[0]; lastCine = -1; sceneAct = nullptr; endAt = 0; g_sceneActPc = nullptr; }
    bool local = false; for (int i = 0; i < t.npc; i++) if (IsLocalPC(t.pc[i])) local = true;
    if (!local) return;   // a guest: the host decides
    if (StoryMapNow(t)) { TickProtect(t); TickAbilities(t); }
    int cine = 0; for (int i = 0; i < t.npc; i++) if (PawnOf(t.pc[i]) && SceneScripted(t.pc[i])) cine = 1;
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

// `pawnvis` (test command, 2026-10-25: guests see the host's character not loaded - only hands - in every chapter):
// for every player pawn the replicated character data, the character applier, the forms and the Mesh component with
// its SkeletalMesh and hidden flags, to compare the host's copy on a guest with the guest's own pawn.
static void PawnVisCommand(const TickObjs& t) {
    static MirrorProp cc = {}, ap = {}, rf = {}, cf = {}, mesh = {}, skm = {}, hid = {}, own = {};
    MirrorPropFind("TnPawn", "_CurrentCharacterCompressed", cc); MirrorPropFind("TnPawn", "_CharacterApplier", ap);
    MirrorPropFind("TnPawn", "_RobotForm", rf); MirrorPropFind("TnPawn", "_CurrentForm", cf);
    MirrorPropFind("Pawn", "Mesh", mesh); MirrorPropFind("SkeletalMeshComponent", "SkeletalMesh", skm);
    MirrorPropFind("PrimitiveComponent", "HiddenGame", hid); MirrorPropFind("PrimitiveComponent", "bOwnerNoSee", own);
    for (int i = 0; i < t.npawn; i++) {
        BYTE* p = t.pawn[i]; char pn[64]; ObjLabel(p, pn, sizeof(pn));
        char line[700]; size_t k = _snprintf_s(line, _TRUNCATE, "pawnvis: %s", pn);
        if (cc.off > 0 && Rd(p + cc.off, 32)) { k += _snprintf_s(line + k, sizeof(line) - k, _TRUNCATE, " char="); for (int b = 0; b < 32; b += 4) k += _snprintf_s(line + k, sizeof(line) - k, _TRUNCATE, "%08lx", *(DWORD*)(p + cc.off + b)); }
        BYTE* obj[3] = { ap.off > 0 ? *(BYTE**)(p + ap.off) : nullptr, rf.off > 0 ? *(BYTE**)(p + rf.off) : nullptr, cf.off > 0 ? *(BYTE**)(p + cf.off) : nullptr };
        const char* on[3] = { "applier", "robotform", "curform" };
        for (int j = 0; j < 3; j++) { char n2[64] = "-"; if (IsObjFast(obj[j])) ObjLabel(obj[j], n2, sizeof(n2)); k += _snprintf_s(line + k, sizeof(line) - k, _TRUNCATE, " %s=%s", on[j], n2); }
        BYTE* m = mesh.off > 0 ? *(BYTE**)(p + mesh.off) : nullptr;
        if (IsObjFast(m)) {
            char mn[64]; ObjLabel(m, mn, sizeof(mn)); BYTE* sk = skm.off > 0 ? *(BYTE**)(m + skm.off) : nullptr; char sn[64] = "-"; if (IsObjFast(sk)) ObjLabel(sk, sn, sizeof(sn));
            int h = hid.off > 0 && hid.mask ? ((*(unsigned*)(m + hid.off) & hid.mask) ? 1 : 0) : -1, o = own.off > 0 && own.mask ? ((*(unsigned*)(m + own.off) & own.mask) ? 1 : 0) : -1;
            k += _snprintf_s(line + k, sizeof(line) - k, _TRUNCATE, " mesh=%s skel=%s hiddenGame=%d ownerNoSee=%d", mn, sn, h, o);
            // +0x224: a TArray on the mesh (2 entries on the guest's own pawn, 0 on the host's copy - 2026-10-25 diff);
            // every object pointer inside its first 0x80 bytes
            TArr* at = (TArr*)(m + 0x224);
            if (Rd(at, 12) && at->Num > 0 && at->Num < 32 && Rd(at->Data, 0x80)) {
                k += _snprintf_s(line + k, sizeof(line) - k, _TRUNCATE, " arr224[%d]:", at->Num);
                for (int b = 0; b < 0x80; b += 4) { BYTE* q = *(BYTE**)((BYTE*)at->Data + b); if (IsObjFast(q)) { char qn[64], qc[64] = ""; ObjLabel(q, qn, sizeof(qn)); ClassNameOf(q, qc, sizeof(qc)); BYTE* qs = skm.off > 0 && strstr(qc, "SkeletalMesh") ? *(BYTE**)(q + skm.off) : nullptr; char qsn[64] = ""; if (IsObjFast(qs)) ObjLabel(qs, qsn, sizeof(qsn)); k += _snprintf_s(line + k, sizeof(line) - k, _TRUNCATE, " +%x=%s(%s)%s", b, qn, qc, qsn); } }
            } else k += _snprintf_s(line + k, sizeof(line) - k, _TRUNCATE, " arr224[%d]", Rd(at, 12) ? at->Num : -1);
        } else k += _snprintf_s(line + k, sizeof(line) - k, _TRUNCATE, " mesh=-");
        Log("%s", line);
        // every component of the pawn and of its robot form: class, mesh, hidden flags
        static MirrorProp comps = { 0x4c, 0, true }, smm = {}; MirrorPropFind("StaticMeshComponent", "StaticMesh", smm);   // Actor.Components +0x4c (TArray in the pawn dump; the name search picks +0xac of another class)
        BYTE* owners[2] = { p, obj[1] };
        for (int q = 0; q < 2; q++) {
            BYTE* a = owners[q]; if (!IsObjFast(a) || comps.off <= 0) continue;
            TArr* arr = (TArr*)(a + comps.off); if (!Rd(arr, 12) || arr->Num < 0 || arr->Num > 64) continue;
            for (int c = 0; c < arr->Num; c++) {
                BYTE* co = (BYTE*)arr->Data[c]; if (!IsObjFast(co)) continue;
                char cn2[64] = ""; ClassNameOf(co, cn2, sizeof(cn2)); char con[64]; ObjLabel(co, con, sizeof(con));
                char what[64] = "";
                if (strstr(cn2, "SkeletalMesh") && skm.off > 0) { BYTE* sk = *(BYTE**)(co + skm.off); if (IsObjFast(sk)) ObjLabel(sk, what, sizeof(what)); }
                else if (strstr(cn2, "StaticMesh") && smm.off > 0) { BYTE* sm = *(BYTE**)(co + smm.off); if (IsObjFast(sm)) ObjLabel(sm, what, sizeof(what)); }
                int h = -1, o = -1; if (strstr(cn2, "Component") && hid.off > 0 && Rd(co + hid.off, 4)) { h = (*(unsigned*)(co + hid.off) & hid.mask) ? 1 : 0; o = (*(unsigned*)(co + own.off) & own.mask) ? 1 : 0; }
                Log("pawnvis:   %s %s comp %s %s %s hidden=%d ownerNoSee=%d", pn, q ? "form" : "pawn", con, cn2, what, h, o);
            }
        }
    }
}

// `animfix` (test command, 2026-10-25): on a guest the host's pawn mesh has no animation nodes (+0x224 empty) and only
// the hands/weapon show. Re-apply the mesh's AnimTreeTemplate (or the local pawn's) with SetAnimTreeTemplate on every
// player pawn mesh whose node list is empty, and log the node count before and after.
static void AnimFixCommand(const TickObjs& t) {
    static MirrorProp mesh = {}, tmpl = {};
    MirrorPropFind("Pawn", "Mesh", mesh); MirrorPropFind("SkeletalMeshComponent", "AnimTreeTemplate", tmpl);
    if (mesh.off <= 0 || tmpl.off <= 0) { Log("animfix: offsets missing (Mesh %x AnimTreeTemplate %x)", mesh.off, tmpl.off); return; }
    BYTE* ownTmpl = nullptr;
    for (int i = 0; i < t.npc; i++) if (IsLocalPC(t.pc[i]) && PawnOf(t.pc[i])) { BYTE* m = *(BYTE**)(PawnOf(t.pc[i]) + mesh.off); if (IsObjFast(m)) ownTmpl = *(BYTE**)(m + tmpl.off); }
    for (int i = 0; i < t.npawn; i++) {
        BYTE* p = t.pawn[i]; BYTE* m = *(BYTE**)(p + mesh.off); if (!IsObjFast(m)) continue;
        TArr* at = (TArr*)(m + 0x224); int before = Rd(at, 12) ? at->Num : -1;
        BYTE* tp = *(BYTE**)(m + tmpl.off);
        char pn[64], tn[64] = "-", on[64] = "-"; ObjLabel(p, pn, sizeof(pn)); if (IsObjFast(tp)) ObjLabel(tp, tn, sizeof(tn)); if (IsObjFast(ownTmpl)) ObjLabel(ownTmpl, on, sizeof(on));
        // skeleton update flags (UE3: a mesh that was never rendered and has bUpdateSkelWhenNotRendered=0 never builds
        // its pose, so it is never drawn - the host's copy on a guest): log them for every pawn, force them on empty ones
        static MirrorProp upd = {}, noskel = {}, ign = {};
        MirrorPropFind("SkeletalMeshComponent", "bUpdateSkelWhenNotRendered", upd); MirrorPropFind("SkeletalMeshComponent", "bNoSkeletonUpdate", noskel);
        MirrorPropFind("SkeletalMeshComponent", "bIgnoreControllersWhenNotRendered", ign);
        auto bit = [&](MirrorProp& q) { return q.off > 0 && q.mask ? ((*(unsigned*)(m + q.off) & q.mask) ? 1 : 0) : -1; };
        Log("animfix: %s flags bUpdateSkelWhenNotRendered=%d bNoSkeletonUpdate=%d bIgnoreControllersWhenNotRendered=%d", pn, bit(upd), bit(noskel), bit(ign));
        if (before != 0) { Log("animfix: %s nodes %d template %s - left alone", pn, before, tn); continue; }
        if (upd.off > 0 && upd.mask) *(unsigned*)(m + upd.off) |= upd.mask;
        if (noskel.off > 0 && noskel.mask) *(unsigned*)(m + noskel.off) &= ~noskel.mask;
        if (ign.off > 0 && ign.mask) *(unsigned*)(m + ign.off) &= ~ign.mask;
        // no script functions for SetAnimTreeTemplate / InitAnimTree in this build: drop the tree instance and let the
        // engine reattach the component (ActorComponent.bNeedsReattach): Attach builds the tree from the template again
        static MirrorProp anims = {}, reat = {};
        MirrorPropFind("SkeletalMeshComponent", "Animations", anims); MirrorPropFind("ActorComponent", "bNeedsReattach", reat);
        if (!IsObjFast(tp) && IsObjFast(ownTmpl)) *(BYTE**)(m + tmpl.off) = ownTmpl;
        BYTE* old = anims.off > 0 ? *(BYTE**)(m + anims.off) : nullptr; char an[64] = "-"; if (IsObjFast(old)) ObjLabel(old, an, sizeof(an));
        // ghidra 0x9c1e90 (thiscall on the mesh, no args): "if (!Animations && AnimTreeTemplate) { Animations =
        // AnimTreeTemplate->CopyAnimTree(this); init (0xc1b420, 1); UpdateSkelPose }" - the tree setup of the mesh's
        // attach. Animations (+0x214) is cleared first so it builds a fresh tree.
        if (anims.off != 0x214 || tmpl.off != 0x210) { Log("animfix: unexpected offsets Animations %x AnimTreeTemplate %x - not called", anims.off, tmpl.off); continue; }
        *(BYTE**)(m + 0x214) = nullptr;
        static BYTE* base = (BYTE*)GetModuleHandleW(nullptr);
        __try { CallThis3(base + (0x9c1e90 - 0x400000), m, 0, 0, 0); }
        __except (EXCEPTION_EXECUTE_HANDLER) { Log("animfix: tree setup raised %08lx", GetExceptionCode()); }
        BYTE* nt = *(BYTE**)(m + 0x214); char ntn[64] = "-"; if (IsObjFast(nt)) ObjLabel(nt, ntn, sizeof(ntn));
        Log("animfix: %s nodes 0, template %s (own %s), old tree %s -> new tree %s, nodes now %d", pn, tn, on, an, ntn, Rd(at, 12) ? at->Num : -1);
        (void)reat;
    }
}

// `meshnodes` (test command, 2026-10-25): every SkeletalMeshComponent with an owner: owner class/name, the animation
// node list (+0x224) count and the SkeletalMesh - do enemies (simulated proxies like the host's copy) have nodes?
static void MeshNodesCommand() {
    static MirrorProp owner = { 0x3c, 0, true }, skm = {};   // ActorComponent.Owner right after the object header (mesh dump)
    MirrorPropFind("SkeletalMeshComponent", "SkeletalMesh", skm);
    unsigned cid = ClassIdByName("SkeletalMeshComponent"); if (!cid || owner.off <= 0) { Log("meshnodes: no class/offset (Owner %x)", owner.off); return; }
    TArr* go = GObj(); int n = 0, zero = 0, some = 0;
    for (int i = 0; i < go->Num && n < 80; i++) {
        BYTE* o = (BYTE*)go->Data[i]; if (!o || (*(unsigned*)(o + 0x14) & 0x1fff) != cid) continue;
        BYTE* ow = *(BYTE**)(o + owner.off); if (!IsObjFast(ow)) continue;
        char oc[64] = ""; ClassNameOf(ow, oc, sizeof(oc)); if (!strstr(oc, "Pawn")) continue;
        TArr* at = (TArr*)(o + 0x224); int nodes = Rd(at, 12) ? at->Num : -1; if (nodes == 0) zero++; else some++;
        char on[64], sn[64] = "-"; ObjLabel(ow, on, sizeof(on)); BYTE* sk = skm.off > 0 ? *(BYTE**)(o + skm.off) : nullptr; if (IsObjFast(sk)) ObjLabel(sk, sn, sizeof(sn));
        static MirrorProp par = {}, hid = {}, att = {}; MirrorPropFind("SkeletalMeshComponent", "ParentAnimComponent", par); MirrorPropFind("PrimitiveComponent", "HiddenGame", hid);
        BYTE* pa = par.off > 0 ? *(BYTE**)(o + par.off) : nullptr; char pn[64] = "-"; if (IsObjFast(pa)) ObjLabel(pa, pn, sizeof(pn));
        int h = hid.off > 0 && hid.mask ? ((*(unsigned*)(o + hid.off) & hid.mask) ? 1 : 0) : -1;
        char cn[64]; ObjLabel(o, cn, sizeof(cn));
        Log("meshnodes: %s (%s) %s nodes %d mesh %s parent %s hidden %d", on, oc, cn, nodes, sn, pn, h); n++;
    }
    Log("meshnodes: pawn meshes with nodes %d, without %d", some, zero);
}

// `partshow` (test command, 2026-10-25): the host's copy on a guest has most body part meshes (RB_<Char>_CHARSKEL_Chest,
// Head, Legs, CapL/R ...) HiddenGame=1 while the guest's own pawn shows them - only the arms and the gun draw. For every
// player pawn that is not ours: each *_CHARSKEL_<part> mesh takes the visibility of our own pawn's mesh with the same
// <part> (weapons and other meshes stay as they are). Tries PrimitiveComponent.SetHidden, else the bit + reattach flag.
static int PartShowPass(const TickObjs& t, bool verbose) {
    static MirrorProp owner = { 0x3c, 0, true }, skm = {}, hid = {}, reat = {};
    MirrorPropFind("SkeletalMeshComponent", "SkeletalMesh", skm); MirrorPropFind("PrimitiveComponent", "HiddenGame", hid);
    MirrorPropFind("ActorComponent", "bNeedsReattach", reat);
    unsigned cid = ClassIdByName("SkeletalMeshComponent"); if (!cid || skm.off <= 0 || hid.off <= 0 || !hid.mask) return 0;
    BYTE* own = nullptr; for (int i = 0; i < t.npc; i++) if (IsLocalPC(t.pc[i])) own = PawnOf(t.pc[i]);
    if (!own) return 0;
    struct Part { BYTE* comp; BYTE* pawn; char part[32]; int hidden; };
    static Part parts[256]; int np = 0;
    TArr* go = GObj();
    for (int i = 0; i < go->Num && np < 256; i++) {
        BYTE* o = (BYTE*)go->Data[i]; if (!o || (*(unsigned*)(o + 0x14) & 0x1fff) != cid) continue;
        BYTE* ow = *(BYTE**)(o + owner.off); if (!IsObjFast(ow)) continue;
        bool player = false; for (int k = 0; k < t.npc; k++) if (PawnOf(t.pc[k]) == ow) player = true;
        if (!player && ow != own) { char oc[64] = ""; ClassNameOf(ow, oc, sizeof(oc)); if (!strstr(oc, "TnPlayerPawn")) continue; }
        BYTE* sk = *(BYTE**)(o + skm.off); if (!IsObjFast(sk)) continue;
        char sn[96]; ObjNameStr(sk, sn, sizeof(sn)); const char* p = strstr(sn, "_CHARSKEL_"); if (!p) continue;
        Part& q = parts[np++]; q.comp = o; q.pawn = ow; strncpy_s(q.part, p + 10, _TRUNCATE); q.hidden = (*(unsigned*)(o + hid.off) & hid.mask) ? 1 : 0;
    }
    int changed = 0;
    static MirrorProp par = {}; MirrorPropFind("SkeletalMeshComponent", "ParentAnimComponent", par);
    for (int i = 0; i < np; i++) {
        if (parts[i].pawn == own) continue;
        // body parts of another player's pawn: shown, except the arms (the game hides the arm a weapon replaces, right
        // on every copy) and while the robot mesh itself is hidden (vehicle form)
        if (!_strnicmp(parts[i].part, "Arm", 3)) continue;
        BYTE* pa = par.off > 0 ? *(BYTE**)(parts[i].comp + par.off) : nullptr;
        if (IsObjFast(pa) && (*(unsigned*)(pa + hid.off) & hid.mask)) continue;
        int want = 0;
        if (want == parts[i].hidden) continue;
        DWORD parms[4] = { (DWORD)want, 0, 0, 0 };
        bool viaScript = CallScript(parts[i].comp, "SetHidden", parms, sizeof(parms));
        if (!viaScript) {
            unsigned* w = (unsigned*)(parts[i].comp + hid.off); if (want) *w |= hid.mask; else *w &= ~hid.mask;
            if (reat.off > 0 && reat.mask) *(unsigned*)(parts[i].comp + reat.off) |= reat.mask;
        }
        changed++;
        if (verbose) { char pn[64]; ObjLabel(parts[i].pawn, pn, sizeof(pn)); Log("partshow: %s %s hidden %d -> %d (%s)", pn, parts[i].part, parts[i].hidden, want, viaScript ? "SetHidden" : "bit + reattach"); }
    }
    return changed;
}
static void PartShowCommand(const TickObjs& t) { int n = PartShowPass(t, true); Log("partshow: %d part(s) changed", n); }

// B63 fix (2026-10-25): every 2 s on a story map, on host and guests, body parts of other players' pawns are shown
// (PartShowPass). On a guest the host's copy had Chest, Head, Legs, CapL/R hidden - only the arms and the gun drew.
static void TickPartShow(const TickObjs& t) {
    static DWORD next = 0; DWORD now = GetTickCount();
    if ((LONG)(now - next) < 0 || !g_partShow) return; next = now + 2000;
    if (!StoryMapNow(t)) return;
    int n = PartShowPass(t, false);
    static volatile LONG logged = 0; if (n && InterlockedIncrement(&logged) <= 20) Log("partshow: %d hidden body part(s) of other players shown", n);
}

// `meshinfo <owner class part>` (test command, 2026-10-25, B5: NPC robots black on guests in scenes): every
// SkeletalMeshComponent whose owner's class contains the text: mesh, hidden, its LightEnvironment (class, bEnabled),
// bAcceptsLights / bAcceptsDynamicLights, LightingChannels word, CastShadow - to diff host and guest.
static void MeshInfoCommand(const char* want) {
    static MirrorProp owner = { 0x3c, 0, true }, skm = {}, hid = {}, le = {}, leOn = {}, acc = {}, accDyn = {}, chan = {}, shad = {}, mats = {};
    MirrorPropFind("SkeletalMeshComponent", "SkeletalMesh", skm); MirrorPropFind("PrimitiveComponent", "HiddenGame", hid);
    MirrorPropFind("PrimitiveComponent", "LightEnvironment", le); MirrorPropFind("LightEnvironmentComponent", "bEnabled", leOn);
    MirrorPropFind("PrimitiveComponent", "bAcceptsLights", acc); MirrorPropFind("PrimitiveComponent", "bAcceptsDynamicLights", accDyn);
    MirrorPropFind("PrimitiveComponent", "LightingChannels", chan); MirrorPropFind("PrimitiveComponent", "CastShadow", shad);
    MirrorPropFind("MeshComponent", "Materials", mats);
    unsigned cid = ClassIdByName("SkeletalMeshComponent"); if (!cid) return;
    TArr* go = GObj(); int n = 0;
    auto bit = [](BYTE* o, MirrorProp& q) { return q.off > 0 && q.mask && Rd(o + q.off, 4) ? ((*(unsigned*)(o + q.off) & q.mask) ? 1 : 0) : -1; };
    for (int i = 0; i < go->Num && n < 60; i++) {
        BYTE* o = (BYTE*)go->Data[i]; if (!o || (*(unsigned*)(o + 0x14) & 0x1fff) != cid) continue;
        BYTE* ow = *(BYTE**)(o + owner.off); if (!IsObjFast(ow)) continue;
        char oc[64] = ""; ClassNameOf(ow, oc, sizeof(oc)); if (strcmp(want, "*") && !strstr(oc, want)) continue;
        if (!strcmp(want, "*")) { static char seen[64][64]; static int ns = 0; bool k = false; for (int j = 0; j < ns; j++) if (!strcmp(seen[j], oc)) k = true; if (k) continue; if (ns < 64) strcpy_s(seen[ns++], oc); }
        char on[64], sn[64] = "-"; ObjLabel(ow, on, sizeof(on)); BYTE* sk = skm.off > 0 ? *(BYTE**)(o + skm.off) : nullptr; if (IsObjFast(sk)) ObjLabel(sk, sn, sizeof(sn));
        // the light environment by content: the first pointer in the mesh to a *LightEnvironmentComponent
        BYTE* l = nullptr; int loff = 0;
        for (int w = 0x40; w < 0x400 && !l; w += 4) { BYTE* q = nullptr; __try { q = *(BYTE**)(o + w); } __except (EXCEPTION_EXECUTE_HANDLER) { break; } if (IsObjFast(q)) { char qc[64] = ""; ClassNameOf(q, qc, sizeof(qc)); if (strstr(qc, "LightEnvironmentComponent")) { l = q; loff = w; } } }
        char ln[64] = "-", lc[64] = ""; if (IsObjFast(l)) { ObjLabel(l, ln, sizeof(ln)); _snprintf_s(lc, _TRUNCATE, "+%x ", loff); size_t z = strlen(lc); ClassNameOf(l, lc + z, sizeof(lc) - z); }
        int lon = IsObjFast(l) ? bit(l, leOn) : -1;
        unsigned ch = chan.off > 0 && Rd(o + chan.off, 4) ? *(unsigned*)(o + chan.off) : 0;
        TArr* ma = mats.off > 0 ? (TArr*)(o + mats.off) : nullptr; int nm = ma && Rd(ma, 12) ? ma->Num : -1;
        char m0[64] = "-"; if (nm > 0 && Rd(ma->Data, 4) && IsObjFast((BYTE*)ma->Data[0])) ObjLabel((BYTE*)ma->Data[0], m0, sizeof(m0));
        Log("meshinfo: %s (%s) %s hid=%d le=%s(%s) leOn=%d acc=%d accDyn=%d chan=%08x shadow=%d mats=%d %s", on, oc, sn, bit(o, hid), ln, lc, lon, bit(o, acc), bit(o, accDyn), ch, bit(o, shad), nm, m0);
        n++;
    }
    Log("meshinfo: %d mesh(es) for '%s'", n, want);
}

// B5 fix (2026-10-25): NPC robots in scenes (TnMeshSetActorMAT) and player pawns are a master SkeletalMeshComponent
// (RB_Iconic_SKEL / RB_NpcAi_SKEL) plus part meshes (*_CHARSKEL_*) with ParentAnimComponent = the master. On the host
// every part uses the master's LightEnvironment; on a guest the parts had another light environment (never lit) ->
// black robots with only their emissive bits showing. Every 2 s on story maps (host and guests): a part whose
// LightEnvironment (+0x100, checked to be a *LightEnvironmentComponent) differs from its master's gets the master's,
// and is reattached (ActorComponent.bNeedsReattach) so the render proxy uses it. Ini lightfix (default 1).
static int LightFixPass(bool verbose) {
    static MirrorProp par = {}, reat = {};
    MirrorPropFind("SkeletalMeshComponent", "ParentAnimComponent", par); MirrorPropFind("ActorComponent", "bNeedsReattach", reat);
    unsigned cid = ClassIdByName("SkeletalMeshComponent"); if (!cid || par.off <= 0) return 0;
    auto isLE = [](BYTE* q) { if (!IsObjFast(q)) return false; char qc[64] = ""; ClassNameOf(q, qc, sizeof(qc)); return strstr(qc, "LightEnvironmentComponent") != nullptr; };
    TArr* go = GObj(); int n = 0;
    for (int i = 0; i < go->Num; i++) {
        BYTE* o = (BYTE*)go->Data[i]; if (!o || (*(unsigned*)(o + 0x14) & 0x1fff) != cid) continue;
        BYTE* pa = *(BYTE**)(o + par.off); if (!IsObjFast(pa) || pa == o) continue;
        BYTE* mine = *(BYTE**)(o + 0x100); BYTE* theirs = *(BYTE**)(pa + 0x100);
        if (mine == theirs || !isLE(theirs) || (mine && !isLE(mine))) continue;
        *(BYTE**)(o + 0x100) = theirs;
        if (reat.off > 0 && reat.mask) *(unsigned*)(o + reat.off) |= reat.mask;
        n++;
        if (verbose) { char on[64], ln[64], tn[64] = "-"; ObjLabel(o, on, sizeof(on)); ObjLabel(theirs, ln, sizeof(ln)); if (IsObjFast(mine)) ObjLabel(mine, tn, sizeof(tn)); Log("lightfix: %s light environment %s -> master's %s", on, tn, ln); }
    }
    return n;
}
static void TickLightFix(const TickObjs& t) {
    static DWORD next = 0; DWORD now = GetTickCount();
    if ((LONG)(now - next) < 0 || !g_lightFix) return; next = now + 2000;
    if (!StoryMapNow(t)) return;
    static volatile LONG logged = 0; bool verbose = logged < 3;
    int n = LightFixPass(verbose);
    if (n && InterlockedIncrement(&logged) <= 30) Log("lightfix: %d part mesh(es) given their master's light environment", n);
}
static void LightFixCommand() { int n = LightFixPass(true); Log("lightfix: %d part mesh(es) fixed (command)", n); }

// `soundinfo <file>` (2026-10-25, Quality Audio Logs mod: audio logs ignore the volume sliders): every SoundCue with its
// package, SoundClass (FName) and VolumeMultiplier, one line each, to diff the original TransGame.xxx against the mod's.
static void SoundInfoCommand(const char* file) {
    static MirrorProp cls = {}, vol = {};
    MirrorPropFind("SoundCue", "SoundClass", cls); MirrorPropFind("SoundCue", "VolumeMultiplier", vol);
    unsigned cid = ClassIdByName("SoundCue"); if (!cid) { Log("soundinfo: no SoundCue class"); return; }
    FILE* f = nullptr; fopen_s(&f, file, "w"); if (!f) { Log("soundinfo: cannot write %s", file); return; }
    TArr* go = GObj(); int n = 0;
    for (int i = 0; i < go->Num; i++) {
        BYTE* o = (BYTE*)go->Data[i]; if (!o || (*(unsigned*)(o + 0x14) & 0x1fff) != cid) continue;
        char nm[96] = "", pk[96] = "?", cn[96] = "-";
        ObjLabel(o, nm, sizeof(nm));
        for (int h = 0x10; h <= 0x3c; h += 4) { BYTE* q = *(BYTE**)(o + h); if (!IsObjFast(q) || q == o) continue; char qc[64] = ""; ClassNameOf(q, qc, sizeof(qc)); if (!strcmp(qc, "Package")) { ObjNameStr(q, pk, sizeof(pk)); break; } }
        if (cls.off > 0) { unsigned packed = *(unsigned*)(o + cls.off); NameAt((int)(packed >> 14), cn, sizeof(cn)); }
        float v = vol.off > 0 ? *(float*)(o + vol.off) : -1.f;
        fprintf(f, "%s|%s|%s|%.3f\n", pk, nm, cn, v); n++;
    }
    fclose(f); Log("soundinfo: %d SoundCue(s) -> %s (SoundClass +0x%x, VolumeMultiplier +0x%x)", n, file, cls.off, vol.off);
}
// `namefind <substring> <file>`: every live object whose name contains the substring (case-insensitive), with its class
// and address; for finding what a mod package changed (soundinfo above).
static void NameFindCommand(const char* args) {
    char want[64] = "", file[MAX_PATH] = ""; if (sscanf_s(args, "%63s %259s", want, (unsigned)sizeof(want), file, (unsigned)sizeof(file)) < 2) return;
    _strlwr_s(want);
    FILE* f = nullptr; fopen_s(&f, file, "w"); if (!f) return;
    TArr* go = GObj(); int n = 0;
    for (int i = 0; i < go->Num; i++) {
        BYTE* o = (BYTE*)go->Data[i]; if (!IsObjFast(o)) continue;
        char nm[128] = "", lw[128]; ObjLabel(o, nm, sizeof(nm)); strcpy_s(lw, nm); _strlwr_s(lw);
        if (!strstr(lw, want)) continue;
        char cn[64] = ""; ClassNameOf(o, cn, sizeof(cn)); fprintf(f, "%s|%s\n", cn, nm); n++;
    }
    fclose(f); Log("namefind: %d object(s) with '%s' -> %s", n, want, file);
}

// `lightdump <file>` (2026-10-25, owner: "some parts are still black, animations only half fixed"): every mesh component
// (skeletal and static) of characters - TnMeshSetActorMAT and pawns - with its parent (ParentAnimComponent / attach
// parent), light environment, its bEnabled, HiddenGame, mesh and first material; host and guest dumps are diffed by
// owner class + mesh name.
static void LightDumpCommand(const char* file) {
    static MirrorProp par = {}, leOn = {}, hid = {}, skm = {}, stm = {}, mats = {}, ap = {};
    MirrorPropFind("SkeletalMeshComponent", "ParentAnimComponent", par); MirrorPropFind("LightEnvironmentComponent", "bEnabled", leOn);
    MirrorPropFind("PrimitiveComponent", "HiddenGame", hid); MirrorPropFind("SkeletalMeshComponent", "SkeletalMesh", skm);
    MirrorPropFind("StaticMeshComponent", "StaticMesh", stm); MirrorPropFind("MeshComponent", "Materials", mats);
    MirrorPropFind("SkeletalMeshComponent", "AttachedComponents", ap);
    unsigned c1 = ClassIdByName("SkeletalMeshComponent"), c2 = ClassIdByName("StaticMeshComponent");
    FILE* f = nullptr; fopen_s(&f, file, "w"); if (!f) return;
    auto isLE = [](BYTE* q) { if (!IsObjFast(q)) return false; char qc[64] = ""; ClassNameOf(q, qc, sizeof(qc)); return strstr(qc, "LightEnvironmentComponent") != nullptr; };
    TArr* go = GObj(); int n = 0;
    for (int i = 0; i < go->Num; i++) {
        BYTE* o = (BYTE*)go->Data[i]; if (!o) continue; unsigned cid = *(unsigned*)(o + 0x14) & 0x1fff;
        if (cid != c1 && cid != c2) continue;
        BYTE* ow = *(BYTE**)(o + 0x3c); if (!IsObjFast(ow)) continue;
        char oc[64] = ""; ClassNameOf(ow, oc, sizeof(oc));
        if (!strstr(oc, "MeshSetActor") && !strstr(oc, "Pawn")) continue;
        char on[64] = "", mn[96] = "-", pn[64] = "-", ln[64] = "-", m0[96] = "-";
        ObjLabel(ow, on, sizeof(on));
        MirrorProp& mp = cid == c1 ? skm : stm; BYTE* me = mp.off > 0 ? *(BYTE**)(o + mp.off) : nullptr; if (IsObjFast(me)) ObjLabel(me, mn, sizeof(mn));
        BYTE* pa = (cid == c1 && par.off > 0) ? *(BYTE**)(o + par.off) : nullptr; if (IsObjFast(pa)) { BYTE* pm = skm.off > 0 ? *(BYTE**)(pa + skm.off) : nullptr; if (IsObjFast(pm)) ObjLabel(pm, pn, sizeof(pn)); else ObjLabel(pa, pn, sizeof(pn)); }
        BYTE* l = *(BYTE**)(o + 0x100); int lon = -1;
        char lo[64] = "-";
        if (isLE(l)) { ObjLabel(l, ln, sizeof(ln)); if (leOn.off > 0 && leOn.mask) lon = (*(unsigned*)(l + leOn.off) & leOn.mask) ? 1 : 0; BYTE* low = *(BYTE**)(l + 0x3c); if (IsObjFast(low)) ObjLabel(low, lo, sizeof(lo)); } else if (l) strcpy_s(ln, "?");
        int h = (hid.off > 0 && hid.mask) ? ((*(unsigned*)(o + hid.off) & hid.mask) ? 1 : 0) : -1;
        TArr* ma = mats.off > 0 ? (TArr*)(o + mats.off) : nullptr; if (ma && Rd(ma, 12) && ma->Num > 0 && Rd(ma->Data, 4) && IsObjFast((BYTE*)ma->Data[0])) ObjLabel((BYTE*)ma->Data[0], m0, sizeof(m0));
        char cn[48] = ""; ClassNameOf(o, cn, sizeof(cn));
        fprintf(f, "%s|%s|%s|%s|parent=%s|le=%s|leOn=%d|hid=%d|mat=%s|leOwner=%s\n", oc, on, cn, mn, pn, ln, lon, h, m0, lo); n++;
    }
    fclose(f); Log("lightdump: %d mesh(es) -> %s", n, file);
}

// `lightsdump <file>` (2026-10-25, B5 leftovers: guests' robots look flatter): every light component (point, spot,
// directional, sky, dominant...) with its owner actor, bEnabled, Brightness, LightColor and lighting channels, to diff
// host and guest (level actors have the same names on both).
static void LightsDumpCommand(const char* file) {
    static MirrorProp en = {}, br = {}, col = {}, ch = {}, cast = {};
    MirrorPropFind("LightComponent", "bEnabled", en); MirrorPropFind("LightComponent", "Brightness", br);
    MirrorPropFind("LightComponent", "LightColor", col); MirrorPropFind("LightComponent", "LightingChannels", ch);
    MirrorPropFind("LightComponent", "CastShadows", cast);
    if (cast.off > 0) { en.off = cast.off; en.mask = 1; }   // the "largest offset" guess for bEnabled was outside the object; it sits next to CastShadows
    FILE* f = nullptr; fopen_s(&f, file, "w"); if (!f) return;
    TArr* go = GObj(); int n = 0;
    for (int i = 0; i < go->Num; i++) {
        BYTE* o = (BYTE*)go->Data[i]; if (!IsObjFast(o)) continue;
        __try {
        char cn[64] = ""; ClassNameOf(o, cn, sizeof(cn));
        size_t l = strlen(cn); if (l < 14 || strcmp(cn + l - 14, "LightComponent") || !strncmp(cn, "Default__", 9)) continue;
        char nm[64] = ""; ObjLabel(o, nm, sizeof(nm)); if (!strncmp(nm, "Default__", 9)) continue;
        BYTE* ow = *(BYTE**)(o + 0x3c); char on[64] = "-", oc[64] = "";
        if (IsObjFast(ow)) { ObjLabel(ow, on, sizeof(on)); ClassNameOf(ow, oc, sizeof(oc)); }
        int e = (en.off > 0 && en.mask) ? ((*(unsigned*)(o + en.off) & en.mask) ? 1 : 0) : -1;
        float b = br.off > 0 ? *(float*)(o + br.off) : -1.f;
        unsigned c = col.off > 0 ? *(unsigned*)(o + col.off) : 0, chv = ch.off > 0 ? *(unsigned*)(o + ch.off) : 0;
        int cs = (cast.off > 0 && cast.mask) ? ((*(unsigned*)(o + cast.off) & cast.mask) ? 1 : 0) : -1;
        fprintf(f, "%s|%s|%s|%s|en=%d|br=%.3f|col=%08x|ch=%08x|cast=%d\n", cn, oc, on, nm, e, b, c, chv, cs); n++;
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    fclose(f); Log("lightsdump: %d light(s) -> %s (bEnabled +0x%x/%x Brightness +0x%x)", n, file, en.off, en.mask, br.off);
}
// `classprops <Class> <file>`: the script variables declared by a class (property objects whose Outer is that class),
// with type, offset (+0x30 >> 20) and bool mask, and for every live instance (up to 40) their values as words.
static void ClassPropsCommand(const char* args) {
    char cls[80] = "", file[MAX_PATH] = ""; if (sscanf_s(args, "%79s %259s", cls, (unsigned)sizeof(cls), file, (unsigned)sizeof(file)) < 2) return;
    FILE* f = nullptr; fopen_s(&f, file, "w"); if (!f) return;
    TArr* go = GObj(); struct P { char n[64]; char t[40]; int off; unsigned mask; }; std::vector<P> ps;
    for (int i = 0; i < go->Num; i++) {
        BYTE* o = (BYTE*)go->Data[i]; if (!IsObjFast(o)) continue;
        char cn[48] = ""; ClassNameOf(o, cn, sizeof(cn)); size_t l = strlen(cn);
        if (l < 8 || strcmp(cn + l - 8, "Property")) continue;
        if (!MirrorOuterIs(o, cls)) continue;
        P p = {}; ObjNameStr(o, p.n, sizeof(p.n)); strcpy_s(p.t, cn); p.off = (int)(*(unsigned*)(o + 0x30) >> 20);
        p.mask = strcmp(cn, "BoolProperty") ? 0 : *(unsigned*)(o + 0x40); ps.push_back(p);
        fprintf(f, "PROP %s %s +0x%x mask %x\n", p.n, p.t, p.off, p.mask);
    }
    unsigned cid = ClassIdByName(cls); int k = 0;
    for (int i = 0; i < go->Num && cid && k < 40; i++) {
        BYTE* o = (BYTE*)go->Data[i]; if (!o || (*(unsigned*)(o + 0x14) & 0x1fff) != cid) continue;
        char on[64] = ""; ObjLabel(o, on, sizeof(on)); fprintf(f, "OBJ %s\n", on); k++;
        for (auto& p : ps) {
            if (!Rd(o + p.off, 4)) continue;
            unsigned w = *(unsigned*)(o + p.off);
            char ref[64] = ""; if (IsObjFast((BYTE*)(UINT_PTR)w)) ObjLabel((BYTE*)(UINT_PTR)w, ref, sizeof(ref));
            fprintf(f, "  %s = %08x %s%s\n", p.n, p.mask ? ((w & p.mask) ? 1 : 0) : w, ref, !strcmp(p.t, "FloatProperty") ? "" : "");
            if (!strcmp(p.t, "FloatProperty")) fprintf(f, "    (float %.3f)\n", *(float*)(o + p.off));
        }
    }
    fclose(f); Log("classprops: %d prop(s), %d object(s) of %s -> %s", (int)ps.size(), k, cls, file);
}
// `hideowner <owner class substring> <1|0>`: HiddenGame on/off for every mesh component whose owner's class contains
// the substring (B5 leftovers: which actor draws the black stretched polygons on the guest).
static void HideOwnerCommand(const char* args) {
    char want[64] = ""; int on = 1; sscanf_s(args, "%63s %d", want, (unsigned)sizeof(want), &on);
    static MirrorProp hid = {}; MirrorPropFind("PrimitiveComponent", "HiddenGame", hid); if (hid.off <= 0 || !hid.mask) return;
    static MirrorProp reat = {}; MirrorPropFind("ActorComponent", "bNeedsReattach", reat);
    unsigned c1 = ClassIdByName("SkeletalMeshComponent"), c2 = ClassIdByName("StaticMeshComponent");
    TArr* go = GObj(); int n = 0;
    for (int i = 0; i < go->Num; i++) {
        BYTE* o = (BYTE*)go->Data[i]; if (!o) continue; unsigned cid = *(unsigned*)(o + 0x14) & 0x1fff; if (cid != c1 && cid != c2) continue;
        BYTE* ow = *(BYTE**)(o + 0x3c); if (!IsObjFast(ow)) continue;
        char oc[64] = ""; ClassNameOf(ow, oc, sizeof(oc)); if (!strstr(oc, want)) continue;
        unsigned* w = (unsigned*)(o + hid.off); if (on) *w |= hid.mask; else *w &= ~hid.mask; n++;
        if (reat.off > 0 && reat.mask) *(unsigned*)(o + reat.off) |= reat.mask;
    }
    Log("hideowner: %s %d -> %d mesh(es)", want, on, n);
}
// `micdump <owner class substring> <file>` (B5 leftovers, owner: "metallic / reflection missing on the guest"): for every
// mesh of matching owners, the materials of its SkeletalMesh asset (or the component's own) with their parent and
// parameter values (scalar / vector / texture), names resolved, to diff host and guest. FName is 4 bytes here
// (packed index << 14 | number), so the elements are 24 (scalar, texture) and 36 (vector) bytes.
static void MicDumpValues(FILE* f, BYTE* mi, int depth) {
    static MirrorProp par = {}, sc = {}, ve = {}, tx = {};
    MirrorPropFind("MaterialInstance", "Parent", par); MirrorPropFind("MaterialInstanceConstant", "ScalarParameterValues", sc);
    MirrorPropFind("MaterialInstanceConstant", "VectorParameterValues", ve); MirrorPropFind("MaterialInstanceConstant", "TextureParameterValues", tx);
    char mn[96] = "-", cn[48] = ""; ObjLabel(mi, mn, sizeof(mn)); ClassNameOf(mi, cn, sizeof(cn));
    fprintf(f, "%*sMAT %s (%s)\n", depth * 2, "", mn, cn);
    if (!strstr(cn, "MaterialInstance") || depth > 4) return;
    struct { MirrorProp* p; int sz; const char* k; } arr[] = { { &sc, 28, "S" }, { &ve, 40, "V" }, { &tx, 28, "T" } };   // name 8 bytes (packed + 0), value, GUID
    for (auto& a : arr) {
        if (a.p->off <= 0 || !Rd(mi + a.p->off, 12)) continue;
        TArr* t = (TArr*)(mi + a.p->off); if (t->Num < 0 || t->Num > 64 || (t->Num && !Rd(t->Data, t->Num * a.sz))) continue;
        { static int raw = 0; if (raw < 6 && t->Num >= 2 && Rd(t->Data, 64)) { raw++; fprintf(f, "%*s  RAW %s n=%d:", depth * 2, "", a.k, t->Num); for (int w = 0; w < 16; w++) fprintf(f, " %08x", ((unsigned*)t->Data)[w]); fprintf(f, "\n"); } }
        for (int i = 0; i < t->Num; i++) {
            BYTE* e = (BYTE*)t->Data + i * a.sz; char pn[64] = "?"; NameAt((int)(*(unsigned*)e >> 14), pn, sizeof(pn));
            if (a.k[0] == 'S') fprintf(f, "%*s  S %s = %.3f\n", depth * 2, "", pn, *(float*)(e + 8));
            else if (a.k[0] == 'V') fprintf(f, "%*s  V %s = %.3f %.3f %.3f %.3f\n", depth * 2, "", pn, *(float*)(e + 8), *(float*)(e + 12), *(float*)(e + 16), *(float*)(e + 20));
            else { BYTE* tex = *(BYTE**)(e + 8); char tn[96] = "none"; if (IsObjFast(tex)) ObjLabel(tex, tn, sizeof(tn)); fprintf(f, "%*s  T %s = %s\n", depth * 2, "", pn, tn); }
        }
    }
    BYTE* p = par.off > 0 ? *(BYTE**)(mi + par.off) : nullptr;
    if (IsObjFast(p)) MicDumpValues(f, p, depth + 1);
}
static void MicDumpCommand(const char* args) {
    char want[64] = "", file[MAX_PATH] = ""; if (sscanf_s(args, "%63s %259s", want, (unsigned)sizeof(want), file, (unsigned)sizeof(file)) < 2) return;
    static MirrorProp skm = {}, mats = {}, smats = {};
    MirrorPropFind("SkeletalMeshComponent", "SkeletalMesh", skm); MirrorPropFind("MeshComponent", "Materials", mats); MirrorPropFind("SkeletalMesh", "Materials", smats);
    FILE* f = nullptr; fopen_s(&f, file, "w"); if (!f) return;
    fprintf(f, "offsets: SkeletalMesh.Materials +0x%x MeshComponent.Materials +0x%x\n", smats.off, mats.off);
    unsigned c1 = ClassIdByName("SkeletalMeshComponent"); TArr* go = GObj(); int n = 0;
    for (int i = 0; i < go->Num; i++) {
        BYTE* o = (BYTE*)go->Data[i]; if (!o || (*(unsigned*)(o + 0x14) & 0x1fff) != c1) continue;
        BYTE* ow = *(BYTE**)(o + 0x3c); if (!IsObjFast(ow)) continue;
        char oc[64] = ""; ClassNameOf(ow, oc, sizeof(oc)); if (!strstr(oc, want)) continue;
        char on[64] = "", sn[96] = "-"; ObjLabel(ow, on, sizeof(on));
        BYTE* sk = skm.off > 0 ? *(BYTE**)(o + skm.off) : nullptr; if (IsObjFast(sk)) ObjLabel(sk, sn, sizeof(sn));
        fprintf(f, "MESH %s %s\n", on, sn); n++;
        TArr* own = mats.off > 0 ? (TArr*)(o + mats.off) : nullptr;
        TArr* use = (own && Rd(own, 12) && own->Num > 0) ? own : nullptr;
        // SkeletalMesh.Materials by content (the name lookup picked the component's offset): a TArray whose first
        // element is a material object
        if (!use && IsObjFast(sk))
            for (int w = 0x30; w < 0x300 && !use; w += 4) {
                TArr* t = (TArr*)(sk + w); if (!Rd(t, 12) || t->Num <= 0 || t->Num > 16 || t->Max < t->Num || !Rd(t->Data, 4)) continue;
                BYTE* m0 = (BYTE*)t->Data[0]; if (!IsObjFast(m0)) continue;
                char mc[48] = ""; ClassNameOf(m0, mc, sizeof(mc)); if (strstr(mc, "Material")) { use = t; if (n < 2) fprintf(f, "  (SkeletalMesh materials at +0x%x)\n", w); }
            }
        if (!use || !Rd(use, 12) || use->Num <= 0 || use->Num > 16 || !Rd(use->Data, use->Num * 4)) continue;
        for (int m = 0; m < use->Num; m++) { BYTE* mi = (BYTE*)use->Data[m]; if (IsObjFast(mi)) MicDumpValues(f, mi, 1); }
    }
    fclose(f); Log("micdump: %d mesh(es) of '%s' -> %s", n, want, file);
}
// `forcemips <owner class substring> <1|0>` (B5 leftovers): PrimitiveComponent.bForceMipStreaming on the meshes of
// matching owners - the host prestreams character textures before scenes (Kismet "Stream In Textures", server only),
// a guest may render them with low mips (flat, no metal detail).
static int ForceMipsPass(const char* want, int on) {
    static MirrorProp fm = {}; MirrorPropFind("PrimitiveComponent", "bForceMipStreaming", fm); if (fm.off <= 0 || !fm.mask) return -1;
    unsigned c1 = ClassIdByName("SkeletalMeshComponent"), c2 = ClassIdByName("StaticMeshComponent");
    TArr* go = GObj(); int n = 0;
    for (int i = 0; i < go->Num; i++) {
        BYTE* o = (BYTE*)go->Data[i]; if (!o) continue; unsigned cid = *(unsigned*)(o + 0x14) & 0x1fff; if (cid != c1 && cid != c2) continue;
        BYTE* ow = *(BYTE**)(o + 0x3c); if (!IsObjFast(ow)) continue;
        char oc[64] = ""; ClassNameOf(ow, oc, sizeof(oc)); if (!strstr(oc, want)) continue;
        unsigned* w = (unsigned*)(o + fm.off); bool was = (*w & fm.mask) != 0;
        if (on && !was) { *w |= fm.mask; n++; } else if (!on && was) { *w &= ~fm.mask; n++; }
    }
    return n;
}
static void ForceMipsCommand(const char* args) {
    char want[64] = ""; int on = 1; sscanf_s(args, "%63s %d", want, (unsigned)sizeof(want), &on);
    Log("forcemips: %s %d -> %d mesh(es) changed", want, on, ForceMipsPass(want, on));
}
// `propsat <offset>`: every script property object with that offset (+0x30 >> 20), its class, bool mask and owner class
static void PropsAtOffsetCommand(const char* args) {
    int want = (int)strtol(args, nullptr, 0); TArr* go = GObj(); int n = 0;
    for (int i = 0; i < go->Num && n < 80; i++) {
        BYTE* o = (BYTE*)go->Data[i]; if (!IsObjFast(o)) continue;
        char cn[48] = ""; ClassNameOf(o, cn, sizeof(cn)); size_t l = strlen(cn); if (l < 8 || strcmp(cn + l - 8, "Property")) continue;
        if ((int)(*(unsigned*)(o + 0x30) >> 20) != want) continue;
        char nm[64] = "", ow[64] = "?";
        ObjNameStr(o, nm, sizeof(nm));
        for (int h = 0x10; h <= 0x3c; h += 4) { BYTE* q = *(BYTE**)(o + h); if (IsObjFast(q) && q != o) { char qc[48] = ""; ClassNameOf(q, qc, sizeof(qc)); if (!strcmp(qc, "Class") || !strcmp(qc, "ScriptStruct")) { ObjNameStr(q, ow, sizeof(ow)); break; } } }
        unsigned mask = strcmp(cn, "BoolProperty") ? 0 : *(unsigned*)(o + 0x40);
        Log("propsat: +0x%x %s.%s %s mask %08x", want, ow, nm, cn, mask); n++;
    }
    Log("propsat: %d property(ies) at +0x%x", n, want);
}
// `reattach <component class> <owner class substring|*>` (B5 leftovers): ActorComponent.bNeedsReattach on matching
// components. On guests the lights' AffectedLightEnvironments lists (+0x104) held 1-3 fewer entries than on the host
// (the robots' light environments were not registered with the lights): reattaching rebuilds those links.
static int ReattachPass(const char* cls, const char* want) {
    static MirrorProp reat = {}; MirrorPropFind("ActorComponent", "bNeedsReattach", reat); if (reat.off <= 0 || !reat.mask) return -1;
    unsigned cid = ClassIdByName(cls); if (!cid) return -2;
    TArr* go = GObj(); int n = 0;
    for (int i = 0; i < go->Num; i++) {
        BYTE* o = (BYTE*)go->Data[i]; if (!o || (*(unsigned*)(o + 0x14) & 0x1fff) != cid) continue;
        BYTE* ow = *(BYTE**)(o + 0x3c); if (!IsObjFast(ow)) continue;
        if (strcmp(want, "*")) { char oc[64] = ""; ClassNameOf(ow, oc, sizeof(oc)); if (!strstr(oc, want)) continue; }
        *(unsigned*)(o + reat.off) |= reat.mask; n++;
    }
    return n;
}
static void ReattachCommand(const char* args) {
    char cls[64] = "", want[64] = "*"; sscanf_s(args, "%63s %63s", cls, (unsigned)sizeof(cls), want, (unsigned)sizeof(want));
    Log("reattach: %s of %s -> %d", cls, want, ReattachPass(cls, want));
}
// `lecount <file>`: AffectedLightEnvironments.Num (+0x108) of every light component, to compare host and guest
static void LeCountCommand(const char* file) {
    FILE* f = nullptr; fopen_s(&f, file, "w"); if (!f) return; TArr* go = GObj(); int n = 0, tot = 0;
    for (int i = 0; i < go->Num; i++) {
        BYTE* o = (BYTE*)go->Data[i]; if (!IsObjFast(o)) continue;
        char cn[64] = ""; ClassNameOf(o, cn, sizeof(cn)); size_t l = strlen(cn); if (l < 14 || strcmp(cn + l - 14, "LightComponent")) continue;
        char nm[64] = ""; ObjLabel(o, nm, sizeof(nm)); int c = *(int*)(o + 0x108); fprintf(f, "%s %s %d", cn, nm, c); n++; tot += c;
        BYTE** arr = *(BYTE***)(o + 0x104);
        if (c > 0 && c < 400 && Rd(arr, c * 4)) for (int k = 0; k < c; k++) { BYTE* le = arr[k]; char ow[64] = "?"; if (IsObjFast(le)) { BYTE* lo = *(BYTE**)(le + 0x3c); if (IsObjFast(lo)) ObjLabel(lo, ow, sizeof(ow)); else ObjLabel(le, ow, sizeof(ow)); } fprintf(f, " %s", ow); }
        fprintf(f, "\n");
    }
    fclose(f); Log("lecount: %d light(s), %d light environment links -> %s", n, tot, file);
}
// `hidemesh <mesh name substring|*> <1|0>`: HiddenGame (+ reattach) on every skeletal/static mesh component of scene
// robots and pawns whose mesh asset name contains the substring (B5 leftovers: which part draws the black polygons)
static void HideMeshCommand(const char* args) {
    char want[64] = ""; int on = 1; sscanf_s(args, "%63s %d", want, (unsigned)sizeof(want), &on);
    static MirrorProp hid = {}, reat = {}, skm = {}, stm = {};
    MirrorPropFind("PrimitiveComponent", "HiddenGame", hid); MirrorPropFind("ActorComponent", "bNeedsReattach", reat);
    MirrorPropFind("SkeletalMeshComponent", "SkeletalMesh", skm); MirrorPropFind("StaticMeshComponent", "StaticMesh", stm);
    unsigned c1 = ClassIdByName("SkeletalMeshComponent"), c2 = ClassIdByName("StaticMeshComponent"); TArr* go = GObj(); int n = 0;
    for (int i = 0; i < go->Num; i++) {
        BYTE* o = (BYTE*)go->Data[i]; if (!o) continue; unsigned cid = *(unsigned*)(o + 0x14) & 0x1fff; if (cid != c1 && cid != c2) continue;
        BYTE* ow = *(BYTE**)(o + 0x3c); if (!IsObjFast(ow)) continue;
        char oc[64] = ""; ClassNameOf(ow, oc, sizeof(oc)); if (!strstr(oc, "MeshSetActor") && !strstr(oc, "Pawn")) continue;
        MirrorProp& mp = cid == c1 ? skm : stm; BYTE* me = mp.off > 0 ? *(BYTE**)(o + mp.off) : nullptr; char mn[96] = "";
        if (IsObjFast(me)) ObjLabel(me, mn, sizeof(mn)); if (strcmp(want, "*") && !strstr(mn, want)) continue;
        unsigned* w = (unsigned*)(o + hid.off); if (on) *w |= hid.mask; else *w &= ~hid.mask;
        if (reat.off > 0) *(unsigned*)(o + reat.off) |= reat.mask; n++;
    }
    Log("hidemesh: %s %d -> %d", want, on, n);
}
