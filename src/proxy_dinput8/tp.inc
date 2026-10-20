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
        if (TpGuestNear(host, pawn, have ? o : nullptr, "tpg")) moved++;
    }
    Log("tpg: %d guest(s) moved", moved);
}
