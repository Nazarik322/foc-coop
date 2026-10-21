// tp.inc (lane tp, B15): teleport a guest's pawn next to the host pawn with the native AActor::SetLocation.
// ghidra 0x93c9d0 = AActor::SetLocation(FVector*) (thiscall, ecx = the pawn UObject itself, ret 4) -> FarMoveActor with
// collision checks, so it can return 0 when the destination is blocked: a few offsets above/around the target are tried.
// Runs on the game thread only (TickRescue and the `tpg` test command). Ini key `tpsync`.
// g_tpSync (ini tpsync) is declared in dinput8.cpp
static int g_tpVelOff = 0xfc;   // Actor.Velocity (props Velocity: StructProperty offset 0xfc, after Location 0xe4 and Rotation 0xf0)

static bool TpAnyCinematic() {
    for (int a = 0; a < g_to.npc; a++) {
        BYTE* pc = g_to.pc[a];
        if (IsObjFast(pc) && Rd(pc + 0x360, 4) && (*(DWORD*)(pc + 0x360) & 0x20000)) return true;
    }
    return false;
}
// Native SetLocation on `pawn`; true when the call returned nonzero. Never called with a null / invalid pawn.
static bool TpNativeSetLocation(BYTE* pawn, float* dest) {
    if (!IsObjFast(pawn)) return false;
    static BYTE* base = (BYTE*)GetModuleHandleW(nullptr);
    float v[3] = {dest[0], dest[1], dest[2]};
    DWORD r = 0;
    __try { r = CallThis3(base + (0x93c9d0 - 0x400000), pawn, (DWORD)(UINT_PTR)v, 0, 0); }
    __except (EXCEPTION_EXECUTE_HANDLER) { Log("tp: SetLocation raised %08lx", GetExceptionCode()); return false; }
    return (r & 0xff) != 0;
}
static float TpDist(const float* a, const float* b) { float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2]; return sqrtf(dx * dx + dy * dy + dz * dz); }
// Move `guest` next to `host`. off = explicit offset from the host pawn (nullptr = try a list). Returns true when the
// call returned nonzero and the guest stands within 700 uu of the destination (destinations are >= 400 uu from the host).
static bool TpGuestNear(BYTE* host, BYTE* guest, const float* off, const char* why) {
    if (!IsObjFast(host) || !IsObjFast(guest) || host == guest) return false;
    if (TpAnyCinematic()) { Log("tp: %s skipped, a cutscene is running", why); return false; }
    float h[3]; memcpy(h, LocOf(host), 12);
    float before[3]; memcpy(before, LocOf(guest), 12);
    static const float list[][3] = { {400,0,90},{-400,0,90},{0,400,90},{0,-400,90},{600,0,90},{-600,0,90},{0,600,90},{0,-600,90},{500,500,150},{-500,-500,150},{0,0,700},{0,0,1200} };   // <= 280 uu from the host pawn is blocked by its cylinder
    int n = off ? 1 : (int)(sizeof(list) / sizeof(list[0]));
    bool ok = false;
    for (int i = 0; i < n && !ok; i++) {
        const float* o = off ? off : list[i];
        float d[3] = {h[0] + o[0], h[1] + o[1], h[2] + o[2]};
        bool ret = TpNativeSetLocation(guest, d);
        float* now = LocOf(guest);
        float err = TpDist(now, d);
        Log("tp: %s try %d dest (%.0f,%.0f,%.0f) ret=%d now (%.0f,%.0f,%.0f) err=%.0f", why, i, d[0], d[1], d[2], (int)ret, now[0], now[1], now[2], err);
        if (ret && err < 700.0f) ok = true;
    }
    if (ok && g_tpVelOff > 0 && Rd(guest + g_tpVelOff, 12)) memset(guest + g_tpVelOff, 0, 12);
    Log("tp: %s guest %p (%.0f,%.0f,%.0f) -> (%.0f,%.0f,%.0f), host %p (%.0f,%.0f,%.0f) dist %.0f %s", why, guest, before[0], before[1], before[2],
        LocOf(guest)[0], LocOf(guest)[1], LocOf(guest)[2], host, h[0], h[1], h[2], TpDist(LocOf(guest), h), ok ? "OK" : "FAILED");
    return ok;
}
// Host: distance between the guest pawn on the server and where the guest sees it (FOCP1, sync.inc); -1 = unknown.
static float TpGuestDesync(BYTE* gp) {
    if (!IsObjFast(gp) || !g_gposAt || GetTickCount() - g_gposAt > 2000) return -1.0f;
    return TpDist(LocOf(gp), g_gpos);
}
// Teleports of a guest only while server and guest agree on where it is: moving a pawn the guest sees 40000 uu away
// dropped the guest to the main menu (owner 2026-10-20). reconcile fixes the disagreement first.
static bool TpGuestConsistent(BYTE* gp, const char* why) {
    float d = TpGuestDesync(gp);
    if (d > 1500.0f) { static DWORD lastLog = 0; if (GetTickCount() - lastLog > 3000) { lastLog = GetTickCount(); Log("tp: %s skipped, the guest sees its pawn %.0f uu from the server copy", why, d); } return false; }
    return true;
}
// Host pawn = the local controller's pawn (host side only).
static BYTE* TpHostPawn() {
    for (int a = 0; a < g_to.npc; a++) if (IsObjFast(g_to.pc[a]) && IsLocalPC(g_to.pc[a])) return PawnOf(g_to.pc[a]);
    return nullptr;
}
// `tpg [dx dy dz]` (foc_cmd.txt, host, game thread): teleport every guest pawn next to the host pawn.
static void TpGuestCommand(const char* args) {
    if (!g_tpSync) { Log("tpg: tpsync is off"); return; }
    BYTE* host = TpHostPawn();
    if (!host) { Log("tpg: host has no pawn"); return; }
    float o[3]; bool have = sscanf_s(args, "%f %f %f", &o[0], &o[1], &o[2]) == 3;
    int moved = 0;
    for (int a = 0; a < g_to.npc; a++) {
        BYTE* pc = g_to.pc[a];
        if (!IsObjFast(pc) || IsLocalPC(pc)) continue;
        BYTE* pawn = PawnOf(pc);
        if (!pawn) { Log("tpg: guest has no pawn"); continue; }
        if (!TpGuestConsistent(pawn, "tpg")) continue;
        if (TpGuestNear(host, pawn, have ? o : nullptr, "tpg")) moved++;
    }
    Log("tpg: %d guest(s) moved", moved);
}
// `tph dx dy dz` (host, test command): move the host's own pawn by an offset (rescue test for a falling host).
static void TpHostCommand(const char* args) {
    BYTE* host = TpHostPawn();
    if (!host) { Log("tph: host has no pawn"); return; }
    float o[3]; if (sscanf_s(args, "%f %f %f", &o[0], &o[1], &o[2]) != 3) { Log("tph: usage tph dx dy dz"); return; }
    float* l = LocOf(host); float d[3] = {l[0] + o[0], l[1] + o[1], l[2] + o[2]};
    bool ok = TpNativeSetLocation(host, d);
    Log("tph: host pawn -> (%.0f,%.0f,%.0f) ret=%d now (%.0f,%.0f,%.0f)", d[0], d[1], d[2], (int)ok, LocOf(host)[0], LocOf(host)[1], LocOf(host)[2]);
}

// tpfollow (ini, default on, host): a scripted move of the host (Kismet teleport, scene change) left the guest behind
// with no way to follow (owner 2026-10-20). A jump of the host pawn by more than kFollowJump uu between two ticks
// (~200 ms; walking, driving and flying stay far below) marks a scripted move. One second later, when the host stands
// there (not falling) and no cutscene runs, every guest farther than kFollowDist uu is teleported next to the host.
// A rescue move of the host lands next to a guest, so nothing follows then.
static const float kFollowJump = 2500.0f, kFollowDist = 3000.0f;
static void TickTpFollow(const TickObjs& t) {
    static BYTE* lastPawn = nullptr; static float last[3]; static float jumpZ = 0; static DWORD pendingAt = 0, pendingSince = 0;
    if (!g_tpFollow || !g_tpSync || t.npc < 2) { lastPawn = nullptr; pendingAt = 0; return; }
    BYTE* host = nullptr;
    for (int a = 0; a < t.npc; a++) if (IsObjFast(t.pc[a]) && IsLocalPC(t.pc[a])) host = PawnOf(t.pc[a]);
    if (!IsObjFast(host)) { lastPawn = nullptr; pendingAt = 0; return; }
    float* l = LocOf(host);
    if (host != lastPawn) { lastPawn = host; memcpy(last, l, 12); pendingAt = 0; return; }   // new pawn (respawn, checkpoint)
    float jump = TpDist(l, last); memcpy(last, l, 12);
    DWORD now = GetTickCount();
    if (jump > kFollowJump) {
        Log("tpfollow: host pawn jumped %.0f uu to (%.0f,%.0f,%.0f)", jump, l[0], l[1], l[2]);
        pendingAt = now + 3000; pendingSince = now; jumpZ = l[2];   // 3 s on the new spot: a scene can end over a void (2026-10-20)
    }
    if (!pendingAt || (LONG)(now - pendingAt) < 0) return;
    if (jumpZ - l[2] > 400.0f) { Log("tpfollow: host is falling after the jump, guests stay"); pendingAt = 0; return; }
    if (TpAnyCinematic() && now - pendingSince < 120000) return;   // follow after the cutscene
    pendingAt = 0;
    for (int a = 0; a < t.npc; a++) {
        BYTE* pc = t.pc[a];
        if (!IsObjFast(pc) || IsLocalPC(pc)) continue;
        BYTE* pawn = PawnOf(pc);
        if (!IsObjFast(pawn)) continue;
        float d = TpDist(LocOf(pawn), l);
        if (d > kFollowDist && TpGuestConsistent(pawn, "tpfollow")) { Log("tpfollow: guest pawn %p is %.0f uu away, moving it next to the host", pawn, d); TpGuestNear(host, pawn, nullptr, "tpfollow"); }
    }
}

// leash (ini, default on, host): owner 2026-10-20 - in chapter 1 a scripted door closed behind the host and the guest
// was locked out with no other way through. A guest farther than kLeashDist uu from the host that has not moved more
// than 300 uu for kLeashSec seconds (stuck, not just exploring) while no cutscene runs is teleported next to the host.
static const float kLeashDist = 4000.0f;
static const DWORD kLeashSec = 10;
static void TickLeash(const TickObjs& t) {
    static BYTE* lp[16]; static float lpos[16][3]; static DWORD lsince[16];
    if (!g_leash || !g_tpSync || t.npc < 2 || TpAnyCinematic()) return;
    BYTE* host = nullptr;
    for (int a = 0; a < t.npc; a++) if (IsObjFast(t.pc[a]) && IsLocalPC(t.pc[a])) host = PawnOf(t.pc[a]);
    if (!IsObjFast(host)) return;
    DWORD now = GetTickCount();
    for (int a = 0; a < t.npc && a < 16; a++) {
        BYTE* pc = t.pc[a]; if (!IsObjFast(pc) || IsLocalPC(pc)) continue;
        BYTE* gp = PawnOf(pc); if (!IsObjFast(gp)) { lp[a] = nullptr; continue; }
        float* g = LocOf(gp);
        if (lp[a] != gp || TpDist(g, lpos[a]) > 300.0f) { lp[a] = gp; memcpy(lpos[a], g, 12); lsince[a] = now; continue; }
        float d = TpDist(g, LocOf(host));
        if (d < kLeashDist) { lsince[a] = now; continue; }
        if (now - lsince[a] < kLeashSec * 1000) continue;
        if (!TpGuestConsistent(gp, "leash")) continue;
        Log("leash: guest pawn %p stuck %.0f uu from the host for %lu s, moving it next to the host", gp, d, kLeashSec);
        TpGuestNear(host, gp, nullptr, "leash");
        memcpy(lpos[a], LocOf(gp), 12); lsince[a] = now;
    }
}

// reconcile (ini, default on, host): when the guest sees its pawn more than 1500 uu from the server copy for 2 s and
// neither side is in a scene (the guest played a matinee only locally), the server copy is put where the guest is.
static void TickReconcile(const TickObjs& t) {
    static DWORD since = 0;
    if (!g_reconcile || t.npc < 2) return;
    BYTE* gp = nullptr; BYTE* hostPc = nullptr;
    for (int a = 0; a < t.npc; a++) { BYTE* pc = t.pc[a]; if (!IsObjFast(pc)) continue; if (IsLocalPC(pc)) hostPc = pc; else if (!gp) gp = PawnOf(pc); }
    float d = TpGuestDesync(gp);
    bool scene = g_gposCine || !strcmp(g_gposState, "AnimControl") || (hostPc && (*(unsigned*)(hostPc + 0x360) & 0x20000));
    if (d < 1500.0f || scene) { since = 0; return; }
    DWORD now = GetTickCount();
    if (!since) { since = now; return; }
    if (now - since < 2000) return;
    since = 0;
    float dest[3] = {g_gpos[0], g_gpos[1], g_gpos[2] + 20.0f};
    bool ok = TpNativeSetLocation(gp, dest);
    Log("reconcile: guest pawn %p server (%.0f,%.0f,%.0f) vs guest view (%.0f,%.0f,%.0f), %.0f uu -> server copy moved %s", gp,
        LocOf(gp)[0], LocOf(gp)[1], LocOf(gp)[2], g_gpos[0], g_gpos[1], g_gpos[2], d, ok ? "ok" : "FAILED");
}

// streamguard (ini, default on, host): when the level script changes the streamed sublevels while the players are far
// apart, the player behind is moved to the one ahead before its floor unloads (2026-10-20: the guest ran ahead, the
// host's area unloaded, the host fell, the rescue put it next to the guest and the next scene - which moves the pawn
// relative to where the script expects it - ended over a void). "Ahead" = closer to the first objective marker; without
// a marker the host is the leader. Called by the Kismet tracker (kmirror.inc KMirrorHostOp) on the host.
static void TpStreamGuard(const char* opClass) {
    if (!g_streamGuard || !g_tpSync) return;
    TickObjs& t = g_to;
    BYTE* pawns[16]; BYTE* pcs[16]; int np = 0;
    for (int a = 0; a < t.npc && np < 16; a++) { BYTE* pw = PawnOf(t.pc[a]); if (IsObjFast(pw)) { pcs[np] = t.pc[a]; pawns[np++] = pw; } }
    if (np < 2) return;
    float maxd = 0; for (int a = 1; a < np; a++) { float d = TpDist(LocOf(pawns[0]), LocOf(pawns[a])); if (d > maxd) maxd = d; }
    if (maxd < 3000.0f) return;
    BYTE* target = nullptr;   // first objective marker's actor
    BYTE* m = ObjectiveManager(t);
    if (m) { TArr* a = (TArr*)(m + 0x28); for (int i = 0; i < a->Num && i < 64 && !target; i++) { BYTE* e = (BYTE*)a->Data + i * 0x1c; BYTE* b = *(BYTE**)(e + 0xc); if (IsObjFast(b) && *(int*)(e + 0x10) < 1000) target = b; } }
    int lead = 0;
    for (int a = 0; a < np; a++) if (IsLocalPC(pcs[a])) lead = a;   // default: the host
    if (target) { float best = 1e30f; for (int a = 0; a < np; a++) { float d = TpDist(LocOf(pawns[a]), LocOf(target)); if (d < best) { best = d; lead = a; } } }
    Log("streamguard: %s while players are %.0f uu apart -> everybody to %s (%s)", opClass, maxd, IsLocalPC(pcs[lead]) ? "the host" : "a guest", target ? "closer to the objective marker" : "no marker");
    for (int a = 0; a < np; a++) {
        if (a == lead || TpDist(LocOf(pawns[a]), LocOf(pawns[lead])) < 3000.0f) continue;
        if (!IsLocalPC(pcs[a]) && !TpGuestConsistent(pawns[a], "streamguard")) continue;
        TpGuestNear(pawns[lead], pawns[a], nullptr, "streamguard");
    }
}
