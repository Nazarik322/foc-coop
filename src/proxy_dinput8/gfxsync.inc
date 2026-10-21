// gfxsync.inc (owner 2026-10-20: scripted characters without textures and jerky NPC animation on the guest only).
// netspeed (ini, default 1, both sides): UE3 gives a connection 10-20 KB/s by default, too little for a story level
// full of NPCs, so their moves arrive seldom. The net driver's MaxClientRate / MaxInternetClientRate and every
// connection's CurrentNetSpeed are raised to kNetSpeed (checked every 5 s).
// texsync (ini, default 1): on the host the character texture loaders keep the textures of scripted characters fully
// resident (Texture2D +0x7c bit 3, +0x88 = 0, NOTES "UpdateTransformer"); the guest has no such loaders for them. The
// host sends the names of its forced textures (FOCT1 datagrams, every 10 s) and the guest forces the same ones.
// g_netSpeed, g_texSync are declared in dinput8.cpp
static const int kNetSpeed = 100000;
static MirrorProp g_mpMaxClient = {}, g_mpMaxInet = {}, g_mpNetSpeed = {};
static void NetSpeedRaise(BYTE* o, MirrorProp& p, const char* what) {
    if (p.off <= 0 || p.off > 0x2000 || !Rd(o + p.off, 4)) return;
    int* v = (int*)(o + p.off);
    if (*v >= kNetSpeed || *v < 1000) return;   // only a plausible net rate (a wrong offset must not be written)
    char on[96]; ObjLabel(o, on, sizeof(on));
    Log("netspeed: %s %s %d -> %d", on, what, *v, kNetSpeed);
    *v = kNetSpeed;
}
static void TickNetSpeed(const TickObjs& t) {
    static DWORD last = 0; static unsigned cDrv = 0, cDrv2 = 0, cConn = 0;
    if (!g_netSpeed || GetTickCount() - last < 5000) return;
    last = GetTickCount();
    // a full pass only while something can still change: the first 60 s of a map and when the player count changes
    static char lastMap[96] = ""; static DWORD mapAt = 0; static int lastNpc = -1; char m[96]; MapName(t, m, sizeof(m));
    if (strcmp(m, lastMap)) { strncpy_s(lastMap, m, _TRUNCATE); mapAt = GetTickCount(); }
    if (GetTickCount() - mapAt > 60000 && t.npc == lastNpc) return;
    lastNpc = t.npc;
    if (!cDrv) { cDrv = ClassIdByName("TcpNetDriver"); cDrv2 = ClassIdByName("IpNetDriver"); cConn = ClassIdByName("TcpipConnection"); }
    MirrorPropFind("NetDriver", "MaxClientRate", g_mpMaxClient);
    MirrorPropFind("NetDriver", "MaxInternetClientRate", g_mpMaxInet);
    MirrorPropFind("NetConnection", "CurrentNetSpeed", g_mpNetSpeed);
    TArr* go = GObj();
    for (int i = 0; i < go->Num; i++) {
        BYTE* o = (BYTE*)go->Data[i]; if (!o) continue;
        unsigned c = *(unsigned*)(o + 0x14) & 0x1fff;
        if (c && (c == cDrv || c == cDrv2)) { NetSpeedRaise(o, g_mpMaxClient, "MaxClientRate"); NetSpeedRaise(o, g_mpMaxInet, "MaxInternetClientRate"); }
        else if (c && c == cConn) NetSpeedRaise(o, g_mpNetSpeed, "CurrentNetSpeed");
    }
}

// ---------------------------------------------------------------- texsync
static const int kTexForcedBit = 8;   // Texture2D +0x7c bit 3
static bool TexIsForced(BYTE* o) { return (*(unsigned*)(o + 0x7c) & kTexForcedBit) != 0; }
static void TexForce(BYTE* o) { *(unsigned*)(o + 0x7c) |= kTexForcedBit; *(DWORD*)(o + 0x88) = 0; }
// host: send the names ("Name_Number") of every forced Texture2D in datagrams of up to ~1000 bytes
static void TickTexSyncHost(const TickObjs& t) {
    static DWORD last = 0; static unsigned cTex = 0;
    if (!g_texSync || t.ngame < 1 || t.npc < 2 || g_syncSock == INVALID_SOCKET || GetTickCount() - last < 60000) return;
    last = GetTickCount();
    if (!cTex) cTex = ClassIdByName("Texture2D"); if (!cTex) return;
    TArr* go = GObj();
    char msg[1100]; int n = _snprintf_s(msg, _TRUNCATE, "FOCT1\n"); int total = 0, packets = 0;
    auto flush = [&]() {
        if (n <= 6) return;
        EnterCriticalSection(&g_syncCs);
        for (int i = 0; i < g_syncNPeers; i++) if (GetTickCount() - g_syncPeers[i].seen < 10000) o_sendto(g_syncSock, msg, n, 0, (const sockaddr*)&g_syncPeers[i].addr, sizeof(sockaddr_in));
        LeaveCriticalSection(&g_syncCs);
        packets++; n = _snprintf_s(msg, _TRUNCATE, "FOCT1\n");
    };
    for (int i = 0; i < go->Num; i++) {
        BYTE* o = (BYTE*)go->Data[i];
        if (!o || (*(unsigned*)(o + 0x14) & 0x1fff) != cTex || !Rd(o + 0x7c, 0x10) || !TexIsForced(o)) continue;
        char nm[96]; unsigned num = 0; if (!ObjNameStr(o, nm, sizeof(nm), &num)) continue;
        char e[112]; int L = _snprintf_s(e, _TRUNCATE, "%s %u\n", nm, num);
        if (n + L > 1000) flush();
        memcpy(msg + n, e, L); n += L; total++;
    }
    flush();
    static int lastTotal = -1; if (total != lastTotal) { lastTotal = total; Log("texsync: host has %d forced textures, sent in %d datagram(s)", total, packets); }
}
// guest: received names are queued by the network thread and applied on the game thread
static char g_texRx[16][1100]; static volatile LONG g_texRxHead = 0, g_texRxTail = 0;
static void TexSyncQueue(const char* buf, int len) {   // network thread (sync.inc SyncFilterIn)
    LONG h = g_texRxHead; if (h - g_texRxTail >= 16) return;
    int n = len < 1099 ? len : 1099; memcpy(g_texRx[h % 16], buf, n); g_texRx[h % 16][n] = 0;
    InterlockedIncrement(&g_texRxHead);
}
static void TickTexSyncGuest(const TickObjs& t) {
    static unsigned cTex = 0; static int forcedTotal = 0;
    if (!g_texSync || t.ngame > 0 || g_texRxTail == g_texRxHead) return;
    if (!cTex) cTex = ClassIdByName("Texture2D"); if (!cTex) return;
    // name -> textures: one pass over GObjects per batch of datagrams
    static char names[4096][72]; static unsigned nums[4096]; int nn = 0;
    while (g_texRxTail != g_texRxHead && nn < 4000) {
        char* p = g_texRx[g_texRxTail % 16] + 6;
        while (*p && nn < 4096) {
            char* e = strchr(p, '\n'); if (!e) break; *e = 0;
            if (sscanf_s(p, "%71s %u", names[nn], (unsigned)sizeof(names[nn]), &nums[nn]) == 2) nn++;
            p = e + 1;
        }
        InterlockedIncrement(&g_texRxTail);
    }
    if (!nn) return;
    TArr* go = GObj(); int newly = 0, missing = nn;
    for (int i = 0; i < go->Num; i++) {
        BYTE* o = (BYTE*)go->Data[i];
        if (!o || (*(unsigned*)(o + 0x14) & 0x1fff) != cTex || !Rd(o + 0x7c, 0x10)) continue;
        char nm[96]; unsigned num = 0; if (!ObjNameStr(o, nm, sizeof(nm), &num)) continue;
        for (int k = 0; k < nn; k++) {
            if (nums[k] != num || strcmp(names[k], nm)) continue;
            missing--;
            if (!TexIsForced(o)) { TexForce(o); newly++; }
            break;
        }
    }
    forcedTotal += newly;
    if (newly || missing) Log("texsync: guest got %d names, forced %d more (total %d), %d not loaded here", nn, newly, forcedTotal, missing);
}
static void TickGfxSync(const TickObjs& t) {
    TickNetSpeed(t);
    TickTexSyncHost(t);
    TickTexSyncGuest(t);
}
