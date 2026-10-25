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
