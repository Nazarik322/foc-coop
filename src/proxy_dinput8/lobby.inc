// lobby.inc (owner 2026-10-20): a lobby in F10 without loading a map. Everybody waits in the game's main menu; the host
// starts the mission from the game's OWN menu as in single player, and everybody in the lobby follows into it.
//  host:  F10 > Host > "Create lobby": FoCRelay host 0 ... (Steam lobby: public / friends / private, password hash).
//         The game's own mission start is caught in the engine's console Exec (vtable slot hook): an `open <story map>`
//         without ?listen gets ?listen?MaxPlayers=N. If the menu loads the map another way, the host reopens the loaded
//         map with ?listen (fallback, one extra load). Once the listen port is bound: relay "port N", then "go <map>".
//  guest: F10 > Join > "Join lobby" (password asked when the lobby has one): FoCRelay join <lobby> 7790 while the game
//         stays in the main menu. When the relay reports go=<seq>|<map> the game opens 127.0.0.1:7790 (also on a late
//         join: the newcomer arrives with a fresh character, as on any join). waitguest holds the host's intro until the
//         guests are in, so the cutscenes start for everybody.
static int g_lobbyRole = 0;            // 0 none, 1 host, 2 guest
static int g_lobbyType = 1;            // 0 public, 1 friends only, 2 private (invite only)
static char g_lobbyPw[32] = "";
static int g_lobbyMax = 4;
static char g_lobbyGoDone[128] = "";   // guest: the go value already acted on
static char g_lobbyJoinId[24] = "", g_lobbyJoinHash[24] = "";
static char g_lobbyStatus[160] = "";
static void LobbyHash(const char* pw, char* out, size_t n) {   // FNV-1a, 8 hex digits ("-" = no password)
    if (!pw[0]) { strncpy_s(out, n, "-", _TRUNCATE); return; }
    unsigned h = 2166136261u; for (const char* c = pw; *c; c++) { h ^= (unsigned char)*c; h *= 16777619u; }
    _snprintf_s(out, n, _TRUNCATE, "%08x", h);
}
static void LobbyStopRelays() {   // the join relay of an earlier try kept port 7790 busy (2026-10-20: 13 relays)
    RelayCommand("quit"); ListRelayStop();
    for (int i = 0; i < 30; i++) { Sleep(100); ReadRelayStatus(); if (!strcmp(g_relayState, "stopped") || !g_relayState[0]) break; }
    // the old status must not be read as the new lobby (a test took a lobby id from the previous session)
    wchar_t dir[MAX_PATH]; RelayDir(dir); wchar_t st[MAX_PATH]; _snwprintf_s(st, _TRUNCATE, L"%s\\relay.status", dir); DeleteFileW(st);
    g_relayState[0] = 0; g_relayLobby = 0;
}
static DWORD WINAPI LobbyCreateThread(LPVOID) {
    LobbyStopRelays();
    char hash[24]; LobbyHash(g_lobbyPw, hash, sizeof(hash));
    static const char* types[] = {"public", "friends", "private"};
    char a[200]; _snprintf_s(a, _TRUNCATE, "host 0 %d Story - %s %s", g_lobbyMax, types[g_lobbyType], hash);
    g_relayState[0] = 0; g_relayGo[0] = 0;
    if (StartRelay(a)) { g_lobbyRole = 1; strcpy_s(g_lobbyStatus, "lobby open: start the mission from the game's menu"); Log("lobby: created (%s, max %d, %s)", types[g_lobbyType], g_lobbyMax, g_lobbyPw[0] ? "password" : "no password"); }
    return 0;
}
static DWORD WINAPI LobbyJoinThread(LPVOID) {
    LobbyStopRelays();
    char a[96]; _snprintf_s(a, _TRUNCATE, "join %s %d", g_lobbyJoinId, kRelayLocalPort);
    g_relayState[0] = 0; g_relayGo[0] = 0; g_lobbyGoDone[0] = 0;
    if (StartRelay(a)) { g_lobbyRole = 2; strcpy_s(g_lobbyStatus, "in the lobby: waiting for the host to start the mission"); Log("lobby: joining %s", g_lobbyJoinId); }
    return 0;
}
static void LobbyCreate() { g_hosting = 0; CreateThread(nullptr, 0, LobbyCreateThread, nullptr, 0, nullptr); }
static bool LobbyJoin(const char* id, const char* hash, const char* pw) {
    if (hash && hash[0] && strcmp(hash, "-") && strcmp(hash, "0")) {
        char h[24]; LobbyHash(pw ? pw : "", h, sizeof(h));
        if (strcmp(h, hash)) { strcpy_s(g_lobbyStatus, "wrong password"); return false; }
    }
    strncpy_s(g_lobbyJoinId, id, _TRUNCATE);
    CreateThread(nullptr, 0, LobbyJoinThread, nullptr, 0, nullptr);
    return true;
}
// The game's disconnect goes first and the relay quits 3 s later: the close message to the host travels through the
// relay; with both at once the host only noticed the guest's leave by timeout and kept its controller (B24, 2026-10-21).
static DWORD WINAPI LobbyRelayQuitLater(LPVOID) { Sleep(3000); RelayCommand("quit"); return 0; }
static void LobbyLeave() {
    Log("lobby: left (role %d)", g_lobbyRole);
    g_lobbyRole = 0; g_lobbyStatus[0] = 0;
    char map[96]; MapName(g_to, map, sizeof(map));
    if ((map[0] == 'L' && map[1] >= '0' && map[1] <= '9') || !_strnicmp(map, "ESC_", 4)) QueueCommand("disconnect");
    CreateThread(nullptr, 0, LobbyRelayQuitLater, nullptr, 0, nullptr);
}

// ---------------------------------------------------------------- engine console Exec hook (host: mission start)
static PFN_Exec g_execOrig = nullptr;
static bool LobbyStoryOpen(const wchar_t* cmd) {   // "open L0..." / "start L0..." without ?listen
    const wchar_t* p = cmd; while (*p == L' ') p++;
    if (_wcsnicmp(p, L"open ", 5) && _wcsnicmp(p, L"start ", 6)) return false;
    p = wcschr(p, L' '); while (p && *p == L' ') p++;
    return p && (p[0] == L'L' || p[0] == L'l') && p[1] >= L'0' && p[1] <= L'9' && !wcsstr(cmd, L"?listen") && !wcsstr(cmd, L"?Listen");
}
static BOOL __fastcall h_EngineExec(void* self, void* edx, const wchar_t* cmd, void* ar) {
    (void)edx;
    if (cmd) {
        static volatile LONG n = 0;
        if (InterlockedIncrement(&n) <= 400 && wcsncmp(cmd, L"stat", 4)) Log("engine exec: %.300ls", cmd);
        if (g_lobbyRole == 1 && LobbyStoryOpen(cmd)) {
            static wchar_t buf[1024];
            _snwprintf_s(buf, _TRUNCATE, L"%s?listen?MaxPlayers=%d", cmd, g_lobbyMax);
            g_hostBoundPort = 0; g_captureBind = 1; g_hosting = 1;
            Log("lobby: mission start from the game's menu -> %ls", buf);
            { char nb[600]; WideCharToMultiByte(CP_ACP, 0, buf, -1, nb, sizeof(nb), nullptr, nullptr); NoteTravel(nb); }
            return CallExecSafe((void*)(g_execOrig ? g_execOrig : g_exec), self, buf, ar);
        }
    }
    return CallExecSafe((void*)(g_execOrig ? g_execOrig : g_exec), self, cmd, ar);
}
static void LobbyHookExec() {   // after FindExec: the FExec vtable slot of GEngine (ghidra 0x1522ba4)
    static bool done = false; if (done || !g_exec || !g_execHook) return;
    BYTE* slot = (BYTE*)GetModuleHandleW(nullptr) + (0x1522ba4 - 0x400000);
    if (*(void**)slot != (void*)g_exec) { Log("lobby: Exec slot holds %p, not %p: no hook", *(void**)slot, g_exec); done = true; return; }
    g_execOrig = g_exec;
    DWORD old; VirtualProtect(slot, 4, PAGE_READWRITE, &old); *(void**)slot = (void*)h_EngineExec; VirtualProtect(slot, 4, old, &old);
    done = true; Log("lobby: engine Exec hooked (mission start from the game's menu)");
}

// the loaded world's URL options (FURL Op, a TArray<FString> at +0x28 from Protocol, gametick.inc g_urlOff), except
// Game=, listen and MaxPlayers: "Continue" loads TnCampaignGameSolo with its own options (save point), keep those
static void LobbyWorldOptions(const TickObjs& t, char* out, size_t n) {
    out[0] = 0; if (g_urlOff < 0 || t.nuworld < 1) return;
    for (int w = 0; w < t.nuworld; w++) {
        BYTE* world = t.uworld[w]; if (!Rd(world + g_urlOff, 0x40) || !WideIs(world + g_urlOff, L"unreal")) continue;
        TArr* op = (TArr*)(world + g_urlOff + 0x28);
        if (op->Num < 0 || op->Num > 40 || !Rd(op->Data, op->Num * 12)) return;
        size_t k = 0;
        for (int i = 0; i < op->Num; i++) {
            char o[200]; FStrAscii((BYTE*)op->Data + i * 12, o, sizeof(o));
            if (!o[0] || !_strnicmp(o, "Game=", 5) || !_stricmp(o, "listen") || !_strnicmp(o, "MaxPlayers=", 11)) continue;
            k += _snprintf_s(out + k, n - k, _TRUNCATE, "?%s", o);
        }
        return;
    }
}

// ---------------------------------------------------------------- per tick
static void TickWindowSlot();
static void TickLobby(const TickObjs& t) {
    TickWindowSlot();
    if (!g_exec) FindExec();
    LobbyHookExec();
    if (!g_lobbyRole) return;
    char map[96]; MapName(t, map, sizeof(map));
    bool story = map[0] == 'L' && map[1] >= '0' && map[1] <= '9';
    DWORD now = GetTickCount();
    if (g_lobbyRole == 1) {
        // keyed on the GameInfo object (a new one per load), not on the map name: the map name was not reliable after a
        // travel inside the running game (B29). Solo game (the menu's CHAPTER / CONTINUE) -> reopen as a session; the
        // session game with its listen port -> "port", then "go".
        static BYTE* game = nullptr; static DWORD since = 0; static int stage = 0; static DWORD stageAt = 0; static bool reopened = false;
        BYTE* g = t.ngame ? t.game[0] : nullptr;
        if (g != game) { game = g; since = now; stage = 0; reopened = false; }
        if (!g || !story) return;
        char gcls[80] = ""; ClassNameOf(g, gcls, sizeof(gcls));
        bool solo = strstr(gcls, "Solo") != nullptr;
        if (solo && !reopened && now - since > 3000) {   // the menu loaded the map as single player: reopen it as a session
            reopened = true;
            char opts[300]; LobbyWorldOptions(t, opts, sizeof(opts));
            char cn[80] = ""; if (t.ngame) ClassNameOf(t.game[0], cn, sizeof(cn));
            Log("lobby: loaded %s with game %s options '%s'", map, cn, opts);
            char cmd[600]; _snprintf_s(cmd, _TRUNCATE, "open %s?Game=TransContent.TnCampaignGame%s?listen?MaxPlayers=%d", map, opts, g_lobbyMax);
            g_hostBoundPort = 0; g_captureBind = 1; g_hosting = 1;
            Log("lobby: %s is not listening, reopening it as a session: %s", map, cmd);
            QueueCommand(cmd); return;
        }
        if (solo || !g_hostBoundPort) return;
        if (stage == 0) { char c[32]; _snprintf_s(c, _TRUNCATE, "port %ld", g_hostBoundPort); RelayCommand(c); stage = 1; stageAt = now; Log("lobby: relay port %ld", g_hostBoundPort); }
        else if (stage == 1 && now - stageAt > 1600) { char c[128]; _snprintf_s(c, _TRUNCATE, "go %s", map); RelayCommand(c); stage = 2; strcpy_s(g_lobbyStatus, "mission started: friends are joining"); Log("lobby: go %s", map); }
    } else if (g_lobbyRole == 2) {
        // a guest that started a mission itself (single player, it owns a GameInfo) also goes to the host; a guest that
        // is already a client of the host stays (2026-10-21: the guest window got clicks during a test and loaded L01 solo)
        bool own = story && t.ngame > 0;
        if ((story && !own) || !g_relayGo[0] || !strcmp(g_relayGo, g_lobbyGoDone)) return;
        if (strncmp(g_relayState, "joined", 6) && strncmp(g_relayState, "connected", 9)) return;
        strncpy_s(g_lobbyGoDone, g_relayGo, _TRUNCATE);
        char name[64]; strncpy_s(name, g_relayPersona[0] ? g_relayPersona : g_playerName, _TRUNCATE);
        for (char* c = name; *c; c++) if (*c == '?' || *c == '=' || *c == ' ' || *c == '"' || *c == ',' || *c == '#' || (unsigned char)*c < 0x20) *c = '_';
        char cmd[160]; _snprintf_s(cmd, _TRUNCATE, "open 127.0.0.1:%d?Name=%s", kRelayLocalPort, name);
        strcpy_s(g_lobbyStatus, "the host started the mission: joining");
        Log("lobby: host started (%s) -> %s", g_relayGo, cmd);
        QueueCommand(cmd);
    }
}

// ---------------------------------------------------------------- F10 parts
static void DrawLobbyHostPart() {
    ImGui::Spacing(); ImGui::Separator();
    ImGui::TextUnformatted("LOBBY - friends join first, then you start the mission from the game's own menu");
    if (g_lobbyRole == 1) {
        ImGui::TextColored(ImVec4(0.5f, 1.f, 0.5f, 1.f), "%s", g_lobbyStatus);
        ImGui::Text("Steam: %s   players in lobby: %d", g_relayState, g_relayPeers + 1);
        if (ImGui::Button("Close lobby")) LobbyLeave();
        return;
    }
    ImGui::RadioButton("Friends only", &g_lobbyType, 1); ImGui::SameLine();
    ImGui::RadioButton("Public", &g_lobbyType, 0); ImGui::SameLine();
    ImGui::RadioButton("Private (invite)", &g_lobbyType, 2);
    ImGui::SetNextItemWidth(180); ImGui::InputText("Password (optional)", g_lobbyPw, sizeof(g_lobbyPw), ImGuiInputTextFlags_Password);
    ImGui::SetNextItemWidth(180); ImGui::SliderInt("Players", &g_lobbyMax, 2, 5);
    if (ImGui::Button("Create lobby")) { LobbyCreate(); g_menuOpen = 0; }
    ImGui::SameLine(); ImGui::TextDisabled("(no map loads; the game is not restarted)");
}

// test commands (foc_cmd.txt): `lobby create [max]`, `lobby join <lobbyId>`, `lobby leave`, `lobby menustart <map>`
// (the last one goes through the hooked console Exec like the game's own menu would)
static void LobbyCommand(const char* args) {
    char verb[32] = "", arg[128] = ""; sscanf_s(args, "%31s %127s", verb, (unsigned)sizeof(verb), arg, (unsigned)sizeof(arg));
    if (!strcmp(verb, "create")) { if (arg[0]) g_lobbyMax = atoi(arg); LobbyCreate(); }
    else if (!strcmp(verb, "join")) LobbyJoin(arg, "-", "");
    else if (!strcmp(verb, "leave")) LobbyLeave();
    else if (!strcmp(verb, "menustart") && g_execSelf) {
        wchar_t w[300]; _snwprintf_s(w, _TRUNCATE, L"open %hs?Game=TransContent.TnCampaignGame", arg);
        h_EngineExec(g_execSelf, nullptr, w, g_outDev);
    } else Log("lobby: usage lobby create [max] | join <id> | leave | menustart <map>");
}

// ---------------------------------------------------------------- test copies: keep the window in its slot
// Owner 2026-10-20: the two test windows ended on top of each other again (the game moves its window when a map loads).
// Only for E:\FoC-coop\<letter>\ copies: A at the left, B to the right of it, C/D below; checked every 2 s.
static void TickWindowSlot() {
    static int slot = -2; static DWORD last = 0;
    if (slot == -1 || !g_gameWnd || GetTickCount() - last < 2000) return;
    last = GetTickCount();
    if (slot == -2) {
        wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
        const wchar_t* p = wcsstr(exe, L"\\FoC-coop\\");
        slot = (p && p[10] >= L'A' && p[10] <= L'D' && p[11] == L'\\') ? p[10] - L'A' : -1;
        if (slot < 0) return;
    }
    RECT r; if (!GetWindowRect(g_gameWnd, &r)) return;
    int x = (slot % 2) * 980, y = (slot / 2) * 590;
    if (r.left != x || r.top != y) SetWindowPos(g_gameWnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// `pcprobe` (test command): the local controller's header words +0x00..+0x3c as object pointers and as GObjects
// indexes (also the low/high 16 bits and >>13), with the names they lead to, twice up the chain - to find the Outer.
static void PcProbeName(DWORD w, char* out, size_t n) {
    TArr* go = GObj(); out[0] = 0; char a[64];
    if (IsObjFast((BYTE*)(UINT_PTR)w) && ObjNameStr((BYTE*)(UINT_PTR)w, a, sizeof(a))) { _snprintf_s(out, n, _TRUNCATE, "ptr:%s", a); return; }
    DWORD cand[4] = {w, w & 0xffff, w >> 16, w >> 13};
    for (int c = 0; c < 4; c++) if (cand[c] > 0 && (int)cand[c] < go->Num && go->Data[cand[c]] && ObjNameStr((BYTE*)go->Data[cand[c]], a, sizeof(a))) { _snprintf_s(out, n, _TRUNCATE, "idx%d:%s", c, a); return; }
}
static void PcProbeCommand() {
    BYTE* pc = nullptr; for (int a = 0; a < g_to.npc; a++) if (IsObjFast(g_to.pc[a]) && IsLocalPC(g_to.pc[a])) pc = g_to.pc[a];
    if (!pc) { Log("pcprobe: no local controller"); return; }
    for (int off = 0; off <= 0x3c; off += 4) {
        DWORD w = *(DWORD*)(pc + off); char nm[96]; PcProbeName(w, nm, sizeof(nm));
        Log("pcprobe: +%02x %08lx %s", off, w, nm);
    }
}
