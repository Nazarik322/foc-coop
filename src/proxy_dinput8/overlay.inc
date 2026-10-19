// In-game co-op menu: Direct3D9 overlay (Dear ImGui) + engine console command + LAN lobby beacon.
// Included at the end of dinput8.cpp (shares Log / Rd / PatchIat).
#include <d3d9.h>
#include <ws2tcpip.h>
#include <vector>
#include <shellapi.h>
#include <string>
#include "imgui.h"
#include "backends/imgui_impl_dx9.h"

// ---------------------------------------------------------------- engine: run a console command
// FUN_0077ba80 (ghidra) = UGameEngine::Exec(const TCHAR* Cmd, FOutputDevice& Ar), handles OPEN/START/SERVERTRAVEL.
// Its vtable slot lives at ghidra VA 0x1522ba4. Runtime address = ghidra VA + (module base - 0x400000).
typedef BOOL(__thiscall* PFN_Exec)(void* self, const wchar_t* cmd, void* ar);
static void __fastcall OutStub(void* self, void* edx, const wchar_t* v, int ev) { if (v) Log("engine says: %ls", v); }
static void* g_outVtbl[32];
static void* g_outDev[2] = {g_outVtbl, nullptr};
static PFN_Exec g_exec = nullptr;
static void* g_execSelf = nullptr;
// The engine routine is called with every register and the stack pointer saved around it (its exact convention is unverified).
static BOOL __declspec(noinline) CallExecSafe(void* fn, void* self, const wchar_t* cmd, void* ar) {
    BOOL r = 0; DWORD savedEsp = 0;
    __asm {
        pushad
        mov savedEsp, esp
        mov ecx, self
        push ar
        push cmd
        call fn
        mov esp, savedEsp
        mov r, eax
        popad
    }
    return r;
}
static char g_execStatus[160] = "engine not ready";

static bool FindExec() {
    BYTE* base = (BYTE*)GetModuleHandleW(nullptr);
    INT_PTR delta = (INT_PTR)base - 0x400000;
    BYTE* fn = (BYTE*)(0x77ba80 + delta);
    BYTE* slot = (BYTE*)(0x1522ba4 + delta);
    BYTE* eng = *(BYTE**)(0x19d1fdc + delta);   // DAT_019d1fdc = GEngine
    if (!eng || !Rd(eng, 0x100) || !Rd(slot, 4) || *(BYTE**)slot != fn) { strcpy_s(g_execStatus, "engine not ready"); return false; }
    for (int off = 0; off < 0x100; off += 4) {
        BYTE* vt = *(BYTE**)(eng + off);
        if ((UINT_PTR)vt < 0x10000) continue;
        for (int k = 0; k < 6; k++) {
            if (vt + 4 * k == slot) { g_execSelf = eng + off; g_exec = (PFN_Exec)fn; Log("menu: Exec found, GEngine=%p this=%p (off 0x%x slot %d)", eng, g_execSelf, off, k); return true; }
        }
    }
    Log("menu: Exec subobject not found; fn=%p slot=%p slotval=%p eng=%p engvt=%p", fn, slot, *(void**)slot, eng, *(void**)eng);
    { char line[2000]; size_t k = 0; for (int off = 0; off < 0x100 && k < 1900; off += 4) k += _snprintf_s(line + k, sizeof(line) - k, _TRUNCATE, " %x:%08x", off, *(unsigned*)(eng + off)); Log("menu: eng words%s", line); }
    strcpy_s(g_execStatus, "Exec subobject not found");
    return false;
}

// While a travel starts the game must not believe its window is focused (an active game processes its dead online
// session code and bails out to the menu with 'You must be signed in').
static volatile DWORD g_spoofUntil = 0;
typedef HWND(WINAPI* PFN_GetFG)();
static PFN_GetFG o_GetFG;
static HWND WINAPI h_GetForegroundWindow() { if ((LONG)(g_spoofUntil - GetTickCount()) > 0) return nullptr; return o_GetFG(); }
static CRITICAL_SECTION g_cmdCs;
static std::string g_cmdQueue;       // one pending command
static HANDLE g_cmdEvent;
static char g_lastCmd[400] = "";
static std::string g_cmdForGame;
static HWND g_gameWnd = nullptr;
static WNDPROC g_origWndProc = nullptr;

static void QueueCommand(const char* cmd) {
    EnterCriticalSection(&g_cmdCs);
    g_cmdQueue = cmd;
    LeaveCriticalSection(&g_cmdCs);
    SetEvent(g_cmdEvent);
}

static void RunCommandNow(const std::string& cmd) {
    if (!g_exec && !FindExec()) { Log("menu: cannot run '%s': %s", cmd.c_str(), g_execStatus); return; }
    wchar_t w[512]; MultiByteToWideChar(CP_ACP, 0, cmd.c_str(), -1, w, 512);
    Log("menu: exec '%s'", cmd.c_str());
    g_spoofUntil = GetTickCount() + 30000;
    strncpy_s(g_lastCmd, cmd.c_str(), _TRUNCATE);
    BOOL r = FALSE;
    __try { r = CallExecSafe((void*)g_exec, g_execSelf, w, g_outDev); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("menu: exec raised an exception"); strcpy_s(g_execStatus, "command crashed"); return; }
    Log("menu: exec returned %d", (int)r);
    _snprintf_s(g_execStatus, _TRUNCATE, r ? "ok: %s" : "engine did not handle: %s", cmd.c_str());
}

static DWORD WINAPI CmdThread(LPVOID) {
    for (int i = 0; i < 32; i++) g_outVtbl[i] = (void*)OutStub;
    wchar_t path[MAX_PATH]; GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* s = wcsrchr(path, L'\\'); if (s) *s = 0;
    wchar_t cmdFile[MAX_PATH]; _snwprintf_s(cmdFile, _TRUNCATE, L"%s\\foc_cmd.txt", path);
    for (;;) {
        WaitForSingleObject(g_cmdEvent, 500);
        std::string c;
        EnterCriticalSection(&g_cmdCs); c.swap(g_cmdQueue); LeaveCriticalSection(&g_cmdCs);
        if (c.empty()) {   // command file for scripted tests
            FILE* f = _wfopen(cmdFile, L"rb");
            if (f) { char b[512] = {0}; size_t n = fread(b, 1, sizeof(b) - 1, f); fclose(f); DeleteFileW(cmdFile); while (n && (b[n - 1] == '\r' || b[n - 1] == '\n' || b[n - 1] == ' ')) b[--n] = 0; c = b; }
        }
        if (!c.empty()) {
            if (g_gameWnd && IsWindow(g_gameWnd)) {   // run on the game thread (window procedure)
                EnterCriticalSection(&g_cmdCs); g_cmdForGame = c; LeaveCriticalSection(&g_cmdCs);
                PostMessageW(g_gameWnd, WM_APP + 0x4F0, 0, 0);
            } else Log("menu: no game window yet, command dropped: %s", c.c_str());
        }
    }
}

static HHOOK g_msgHook = nullptr;
static LRESULT CALLBACK GetMsgProc(int code, WPARAM wp, LPARAM lp) {
    if (code >= 0 && lp) {
        MSG* m = (MSG*)lp;
        if (m->message == WM_APP + 0x4F0) {
            std::string c; EnterCriticalSection(&g_cmdCs); c.swap(g_cmdForGame); LeaveCriticalSection(&g_cmdCs);
            if (!c.empty()) RunCommandNow(c);
            m->message = WM_NULL;
        }
    }
    return CallNextHookEx(g_msgHook, code, wp, lp);
}
// Commands must run on the game thread (the thread that owns the window); a message hook on that thread does it.
static void SubclassGameWindow(HWND w) {
    if (g_msgHook || !w) return;
    DWORD tid = GetWindowThreadProcessId(w, nullptr);
    g_msgHook = SetWindowsHookExW(WH_GETMESSAGE, GetMsgProc, nullptr, tid);
    Log("menu: message hook on game thread %lu: %p (err %lu)", tid, g_msgHook, g_msgHook ? 0 : GetLastError());
}

// ---------------------------------------------------------------- menu state
static volatile LONG g_menuOpen = 0;

struct MapEntry { const char* title; const char* map; };
static const MapEntry kChapters[] = {
    {"1  The Exodus", "L01_TheExodus_BASE_m"}, {"2a Defend the Ark", "L02a_DefendTheArk_BASE_m"},
    {"2b Destroy All Mortars", "L02b_DestroyAllMortars_BASE_m"}, {"3a Eye of the Storm", "L03a_EyeOfTheStorm_BASE_m"},
    {"3b Cut and Run", "L03b_CutAndRun_BASE_m"}, {"4a Bridge Assault", "L04a_BridgeAssault_BASE_m"},
    {"4b Stop the Transport", "L04b_StopTheTransport_BASE_m"}, {"4c Flying Fortress", "L04c_FlyingFortress_BASE_m"},
    {"5  Soundwave", "L05_Soundwave_BASE_m"}, {"6  Megatron", "L06_Megatron_BASE_m"},
    {"8a Starscream", "L08a_Starscream_BASE_m"}, {"8b Grimlock Smash", "L08b_GrimlockSmash_BASE_m"}, {"9  Final Fight", "L09_FinalFight_BASE_m"},
};
static const MapEntry kEscalation[] = {
    {"Ark", "ESC_Ark_BASE_m"}, {"Gladiator", "ESC_Gladiator_BASE_m"}, {"Iacon", "ESC_Iacon_BASE_m"}, {"Temple", "ESC_Temple_BASE_m"},
};

static int g_page = 0;            // 0 main, 1 host, 2 join, 3 about
static int g_hostMode = 0;        // 0 story, 1 escalation
static int g_hostMapStory = 0, g_hostMapEsc = 0;
static int g_hostPlayers = 4;
static int g_gamePort = 7778;
static char g_joinIp[64] = "127.0.0.1";
static char g_playerName[32] = "Player";
static int g_selLobby = -1;

// hosting state (announced on the LAN)
static volatile LONG g_hosting = 0;
static char g_hostTitle[96] = "", g_hostModeName[16] = "";
static int g_hostMax = 0;

// ---------------------------------------------------------------- LAN lobby beacon (UDP broadcast, port 47799)
static const int kBeaconPort = 47799;
struct Lobby { char host[32]; char mode[16]; char map[96]; int players, maxp, port; char ip[48]; DWORD seen; };
static std::vector<Lobby> g_lobbies;
static CRITICAL_SECTION g_lobCs;

static int CurrentPlayerCount() { return 0; }   // filled by the session watcher below when known

static DWORD WINAPI BeaconThread(LPVOID) {
    WSADATA wd; WSAStartup(MAKEWORD(2, 2), &wd);
    SOCKET rx = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    BOOL one = TRUE; setsockopt(rx, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof(one));
    sockaddr_in a = {}; a.sin_family = AF_INET; a.sin_port = htons(kBeaconPort); a.sin_addr.s_addr = htonl(INADDR_ANY);
    bool bound = bind(rx, (sockaddr*)&a, sizeof(a)) == 0;
    if (!bound) Log("menu: beacon bind failed (%d)", WSAGetLastError());
    u_long nb = 1; ioctlsocket(rx, FIONBIO, &nb);
    SOCKET tx = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    setsockopt(tx, SOL_SOCKET, SO_BROADCAST, (const char*)&one, sizeof(one));
    DWORD lastTx = 0;
    for (;;) {
        Sleep(100);
        if (g_hosting && GetTickCount() - lastTx > 1500) {
            lastTx = GetTickCount();
            char msg[400];
            _snprintf_s(msg, _TRUNCATE, "FOCCOOP1|%s|%s|%s|%d|%d|%d", g_playerName, g_hostModeName, g_hostTitle, CurrentPlayerCount(), g_hostMax, g_hostBoundPort ? (int)g_hostBoundPort : g_gamePort);
            sockaddr_in b = {}; b.sin_family = AF_INET; b.sin_port = htons(kBeaconPort);
            b.sin_addr.s_addr = htonl(INADDR_BROADCAST); sendto(tx, msg, (int)strlen(msg), 0, (sockaddr*)&b, sizeof(b));
            b.sin_addr.s_addr = htonl(INADDR_LOOPBACK); sendto(tx, msg, (int)strlen(msg), 0, (sockaddr*)&b, sizeof(b));
        }
        if (!bound) continue;
        for (int guard = 0; guard < 16; guard++) {
            char buf[512]; sockaddr_in from; int fl = sizeof(from);
            int n = recvfrom(rx, buf, sizeof(buf) - 1, 0, (sockaddr*)&from, &fl);
            if (n <= 0) break;
            buf[n] = 0;
            if (strncmp(buf, "FOCCOOP1|", 9) != 0) continue;
            Lobby l = {}; char* p = buf + 9; char* f[6]; int cnt = 0;
            while (cnt < 6) { f[cnt++] = p; char* q = strchr(p, '|'); if (!q) break; *q = 0; p = q + 1; }
            if (cnt < 6) continue;
            strncpy_s(l.host, f[0], _TRUNCATE); strncpy_s(l.mode, f[1], _TRUNCATE); strncpy_s(l.map, f[2], _TRUNCATE);
            l.players = atoi(f[3]); l.maxp = atoi(f[4]); l.port = atoi(f[5]);
            inet_ntop(AF_INET, &from.sin_addr, l.ip, sizeof(l.ip)); l.seen = GetTickCount();
            EnterCriticalSection(&g_lobCs);
            bool found = false;
            for (auto& e : g_lobbies) if (!strcmp(e.ip, l.ip) && e.port == l.port) { e = l; found = true; break; }
            if (!found) g_lobbies.push_back(l);
            LeaveCriticalSection(&g_lobCs);
        }
    }
}

static std::string MyLanIps() {
    char name[128]; std::string out;
    if (gethostname(name, sizeof(name)) != 0) return "?";
    addrinfo hints = {}, *res = nullptr; hints.ai_family = AF_INET;
    if (getaddrinfo(name, nullptr, &hints, &res) != 0) return "?";
    for (addrinfo* r = res; r; r = r->ai_next) {
        char ip[48]; inet_ntop(AF_INET, &((sockaddr_in*)r->ai_addr)->sin_addr, ip, sizeof(ip));
        if (!strncmp(ip, "127.", 4)) continue;
        if (!out.empty()) out += ", ";
        out += ip;
    }
    freeaddrinfo(res);
    return out.empty() ? "?" : out;
}

// ---------------------------------------------------------------- actions
// A session opened from the running front end is thrown back to the menu by the game's dead online layer, while a map
// given on the command line works. So Host/Join restart the game with the map URL (same arguments, new URL).
static bool g_fastTravel = false;   // experimental: run `open` inside the running game instead of restarting
static void RelaunchWithUrl(const char* url) {
    int argc = 0; wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring cmd = L"\"" + std::wstring(exe) + L"\"";
    wchar_t wurl[512]; MultiByteToWideChar(CP_ACP, 0, url, -1, wurl, 512);
    if (url[0]) cmd += L" \"" + std::wstring(wurl) + L"\"";
    bool skippedUrl = false;
    for (int i = 1; argv && i < argc; i++) {
        const wchar_t* a = argv[i];
        bool isOpt = a[0] == L'-' || !_wcsnicmp(a, L"ResX=", 5) || !_wcsnicmp(a, L"ResY=", 5);
        if (!isOpt && !skippedUrl) { skippedUrl = true; continue; }   // the old map URL
        cmd += L" " + std::wstring(a);
    }
    if (argv) LocalFree(argv);
    wchar_t dir[MAX_PATH]; wcscpy_s(dir, exe); wchar_t* sl = wcsrchr(dir, L'\\'); if (sl) *sl = 0;
    // start the new copy a few seconds later, after this process (and its single-instance mutex) is gone
    std::wstring full = L"cmd.exe /c ping -n 4 127.0.0.1 >nul & start \"\" " + cmd;
    Log("menu: relaunching: %ls", full.c_str());
    STARTUPINFOW si = {sizeof(si)}; si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    std::vector<wchar_t> buf(full.begin(), full.end()); buf.push_back(0);
    if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, dir, &si, &pi)) {
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        if (g_log) fflush(g_log);
        TerminateProcess(GetCurrentProcess(), 0);
    } else Log("menu: relaunch failed (%lu)", GetLastError());
}
static void RunSessionCommand(const char* url, const char* openCmd) {
    if (g_fastTravel) QueueCommand(openCmd);
    else RelaunchWithUrl(url);
}

static void StartHosting() {
    const MapEntry& m = g_hostMode == 0 ? kChapters[g_hostMapStory] : kEscalation[g_hostMapEsc];
    char url[400], cmd[420];
    _snprintf_s(url, _TRUNCATE, "%s?Game=TransContent.%s?listen?MaxPlayers=%d", m.map,
                g_hostMode == 0 ? "TnCampaignGame" : "TnSurvivalGame", g_hostPlayers);
    _snprintf_s(cmd, _TRUNCATE, "open %s", url);
    strncpy_s(g_hostTitle, m.title, _TRUNCATE); strcpy_s(g_hostModeName, g_hostMode == 0 ? "Story" : "Escalation");
    g_hostMax = g_hostPlayers; g_hostBoundPort = 0; g_captureBind = 1; g_hosting = 1;
    g_menuOpen = 0;
    RunSessionCommand(url, cmd);
}
static void JoinServer(const char* ip, int port) {
    char url[200], cmd[220]; _snprintf_s(url, _TRUNCATE, "%s:%d", ip, port);
    _snprintf_s(cmd, _TRUNCATE, "open %s", url);
    g_hosting = 0;
    g_menuOpen = 0;
    RunSessionCommand(url, cmd);
}

// ---------------------------------------------------------------- input helpers
static bool KeyDown(int vk) {
    if (!g_gameWnd || GetForegroundWindow() != g_gameWnd) return false;   // only react while this game window is focused
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

static void FeedInput(ImGuiIO& io) {
    RECT rc = {}; POINT pt = {};
    if (g_gameWnd && GetClientRect(g_gameWnd, &rc)) {
        io.DisplaySize = ImVec2((float)(rc.right - rc.left), (float)(rc.bottom - rc.top));
        GetCursorPos(&pt); ScreenToClient(g_gameWnd, &pt);
        io.AddMousePosEvent((float)pt.x, (float)pt.y);
    }
    io.AddMouseButtonEvent(0, KeyDown(VK_LBUTTON));
    io.AddMouseButtonEvent(1, KeyDown(VK_RBUTTON));
    static int wheel = 0; (void)wheel;
    static const struct { int vk; ImGuiKey k; } keys[] = {
        {VK_BACK, ImGuiKey_Backspace}, {VK_DELETE, ImGuiKey_Delete}, {VK_LEFT, ImGuiKey_LeftArrow}, {VK_RIGHT, ImGuiKey_RightArrow},
        {VK_HOME, ImGuiKey_Home}, {VK_END, ImGuiKey_End}, {VK_RETURN, ImGuiKey_Enter}, {VK_TAB, ImGuiKey_Tab},
        {VK_CONTROL, ImGuiMod_Ctrl}, {VK_SHIFT, ImGuiMod_Shift}, {'A', ImGuiKey_A}, {'C', ImGuiKey_C}, {'V', ImGuiKey_V}, {'X', ImGuiKey_X},
    };
    for (auto& k : keys) io.AddKeyEvent(k.k, KeyDown(k.vk));
    if (io.WantTextInput) {
        static bool prev[256];
        BYTE ks[256]; GetKeyboardState(ks);
        for (int vk = 0x20; vk < 0xFF; vk++) {
            bool d = KeyDown(vk);
            if (d && !prev[vk] && !KeyDown(VK_CONTROL)) {
                wchar_t ch[4] = {0};
                UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
                for (int j = 0; j < 256; j++) ks[j] = 0;
                ks[VK_SHIFT] = KeyDown(VK_SHIFT) ? 0x80 : 0;
                if (ToUnicode(vk, sc, ks, ch, 3, 0) == 1 && ch[0] >= 0x20) io.AddInputCharacter(ch[0]);
            }
            prev[vk] = d;
        }
    }
}

// ---------------------------------------------------------------- the menu UI
static void Tooltip(const char* t) { if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", t); }

static void DrawHostPage() {
    ImGui::TextUnformatted("HOST A GAME");
    ImGui::Separator();
    if (ImGui::RadioButton("Story (co-op campaign)", g_hostMode == 0)) g_hostMode = 0;
    ImGui::SameLine();
    if (ImGui::RadioButton("Escalation", g_hostMode == 1)) g_hostMode = 1;
    ImGui::Spacing();
    ImGui::TextUnformatted(g_hostMode == 0 ? "Chapter:" : "Map:");
    ImGui::BeginChild("maps", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 8.5f), true);
    if (g_hostMode == 0) {
        for (int i = 0; i < (int)(sizeof(kChapters) / sizeof(kChapters[0])); i++)
            if (ImGui::Selectable(kChapters[i].title, g_hostMapStory == i)) g_hostMapStory = i;
    } else {
        for (int i = 0; i < (int)(sizeof(kEscalation) / sizeof(kEscalation[0])); i++)
            if (ImGui::Selectable(kEscalation[i].title, g_hostMapEsc == i)) g_hostMapEsc = i;
    }
    ImGui::EndChild();
    ImGui::SliderInt("Max players", &g_hostPlayers, 2, 5);
    ImGui::InputInt("Game port", &g_gamePort, 0, 0);
    ImGui::Spacing();
    ImGui::TextDisabled("%s", g_hostMode == 0 ? "Story co-op is experimental: only chapter 1 has been tested." : "Escalation: wave survival, up to 5 players.");
    ImGui::Text("Your address for friends: %s : %d", MyLanIps().c_str(), g_gamePort);
    ImGui::TextDisabled("Friends on the same network see your lobby automatically; others type the IP.");
    ImGui::Spacing();
    if (ImGui::Button("Start hosting", ImVec2(ImGui::GetFontSize() * 11.f, 0))) StartHosting();
    ImGui::SameLine(); ImGui::TextDisabled("(the game restarts into the session)");
    ImGui::SameLine();
    if (ImGui::Button("Back")) g_page = 0;
}

static void DrawJoinPage() {
    ImGui::TextUnformatted("JOIN A GAME");
    ImGui::Separator();
    ImGui::TextUnformatted("Lobbies found on your network:");
    ImGui::BeginChild("lobbies", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 7.5f), true);
    if (ImGui::BeginTable("lt", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Host"); ImGui::TableSetupColumn("Mode"); ImGui::TableSetupColumn("Map / chapter");
        ImGui::TableSetupColumn("Players"); ImGui::TableSetupColumn("Address");
        ImGui::TableHeadersRow();
        EnterCriticalSection(&g_lobCs);
        DWORD now = GetTickCount();
        for (size_t i = 0; i < g_lobbies.size();) { if (now - g_lobbies[i].seen > 6000) g_lobbies.erase(g_lobbies.begin() + i); else i++; }
        for (int i = 0; i < (int)g_lobbies.size(); i++) {
            const Lobby& l = g_lobbies[i];
            ImGui::TableNextRow(); ImGui::TableNextColumn();
            char id[64]; _snprintf_s(id, _TRUNCATE, "%s##%d", l.host, i);
            if (ImGui::Selectable(id, g_selLobby == i, ImGuiSelectableFlags_SpanAllColumns)) { g_selLobby = i; strncpy_s(g_joinIp, l.ip, _TRUNCATE); g_gamePort = l.port; }
            ImGui::TableNextColumn(); ImGui::TextUnformatted(l.mode);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(l.map);
            ImGui::TableNextColumn(); ImGui::Text("%d / %d", l.players, l.maxp);
            ImGui::TableNextColumn(); ImGui::Text("%s:%d", l.ip, l.port);
        }
        if (g_lobbies.empty()) { ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextDisabled("searching..."); }
        LeaveCriticalSection(&g_lobCs);
        ImGui::EndTable();
    }
    ImGui::EndChild();
    ImGui::Spacing();
    ImGui::TextUnformatted("Or connect by address:");
    ImGui::SetNextItemWidth(260); ImGui::InputText("IP / host", g_joinIp, sizeof(g_joinIp));
    ImGui::SetNextItemWidth(120); ImGui::InputInt("Port", &g_gamePort, 0, 0);
    ImGui::Spacing();
    if (ImGui::Button("Join", ImVec2(ImGui::GetFontSize() * 11.f, 0))) JoinServer(g_joinIp, g_gamePort);
    ImGui::SameLine();
    if (ImGui::Button("Back")) g_page = 0;
}

static void DrawMainPage() {
    ImGui::TextUnformatted("TRANSFORMERS: FALL OF CYBERTRON  -  CO-OP");
    ImGui::Separator();
    ImGui::Spacing();
    ImVec2 bs(ImGui::GetContentRegionAvail().x, ImGui::GetTextLineHeight() * 2.4f);
    if (ImGui::Button("Host a game", bs)) g_page = 1;
    Tooltip("Pick story chapter or Escalation map and let friends join you");
    if (ImGui::Button("Join a game", bs)) g_page = 2;
    Tooltip("Choose a lobby from the list or type an IP address");
    if (g_hosting) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.5f, 1.f, 0.5f, 1.f), "Hosting: %s - %s (max %d)", g_hostModeName, g_hostTitle, g_hostMax);
    }
    if (ImGui::Button(g_fastTravel ? "Disconnect / stop" : "Leave to main menu (restarts the game)", bs)) { g_hosting = 0; g_menuOpen = 0; if (g_fastTravel) QueueCommand("disconnect"); else RelaunchWithUrl(""); }
    if (ImGui::Button("About / status", bs)) g_page = 3;
    ImGui::Spacing();
    ImGui::SetNextItemWidth(200); ImGui::InputText("Your name", g_playerName, sizeof(g_playerName));
}

static void DrawAboutPage() {
    ImGui::TextUnformatted("ABOUT");
    ImGui::Separator();
    ImGui::TextWrapped("foc-coop dev build. Unofficial co-op layer for Fall of Cybertron. Press F10 to open or close this menu.");
    ImGui::Spacing();
    ImGui::Text("Engine: %s", g_execStatus);
    ImGui::Text("Last command: %s", g_lastCmd[0] ? g_lastCmd : "(none)");
    ImGui::Text("Instance: %s", g_suffix[0] ? g_suffix : "(default)");
    ImGui::Checkbox("Fast travel without restart (experimental, usually fails)", &g_fastTravel);
    ImGui::Spacing();
    if (ImGui::Button("Back")) g_page = 0;
}

static void DrawMenu(ImGuiIO& io) {
    float s = io.DisplaySize.y / 720.f; if (s < 1.f) s = 1.f; if (s > 3.f) s = 3.f;
    io.FontGlobalScale = s;
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(560 * s, 0), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.92f);
    ImGui::Begin("foc-coop", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings);
    switch (g_page) { case 1: DrawHostPage(); break; case 2: DrawJoinPage(); break; case 3: DrawAboutPage(); break; default: DrawMainPage(); }
    ImGui::Spacing(); ImGui::Separator();
    ImGui::TextDisabled("%s", g_execStatus);
    ImGui::End();
}

// ---------------------------------------------------------------- Direct3D9 hook
typedef HRESULT(WINAPI* PFN_EndScene)(IDirect3DDevice9*);
typedef HRESULT(WINAPI* PFN_Reset)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
struct D3dVt { void** vt; PFN_EndScene end; PFN_Reset reset; };
static D3dVt g_d3dVts[4]; static int g_d3dVtN = 0;
static D3dVt* D3dFor(void* dev) { void** vt = *(void***)dev; for (int i = 0; i < g_d3dVtN; i++) if (g_d3dVts[i].vt == vt) return &g_d3dVts[i]; return nullptr; }
static IDirect3DDevice9* g_imDev = nullptr;
static DWORD g_menuT0 = 0;

static void RenderOverlay(IDirect3DDevice9* dev) {
    if (g_imDev != dev) {
        if (g_imDev) { ImGui_ImplDX9_Shutdown(); }
        else {
            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO(); io.IniFilename = nullptr; io.MouseDrawCursor = false;
            ImGui::StyleColorsDark();
            ImGuiStyle& st = ImGui::GetStyle(); st.WindowRounding = 6; st.FrameRounding = 4; st.WindowPadding = ImVec2(14, 12); st.ItemSpacing = ImVec2(8, 7);
            g_menuT0 = GetTickCount();
            DWORD n = sizeof(g_playerName); GetUserNameA(g_playerName, &n);
            if (g_suffix[0]) { size_t l = strlen(g_playerName); _snprintf_s(g_playerName + l, sizeof(g_playerName) - l, _TRUNCATE, "_%s", g_suffix); }   // second copy on one PC gets its own name
        }
        D3DDEVICE_CREATION_PARAMETERS cp; if (SUCCEEDED(dev->GetCreationParameters(&cp))) g_gameWnd = cp.hFocusWindow;
        ImGui_ImplDX9_Init(dev);
        g_imDev = dev;
        Log("menu: overlay initialised on device %p window %p", dev, g_gameWnd);
    }
    ImGuiIO& io = ImGui::GetIO();
    // toggle on F10 (edge)
    static bool f10prev = false; bool f10 = KeyDown(VK_F10);
    if (f10 && !f10prev) { g_menuOpen = !g_menuOpen; if (g_menuOpen) { g_page = 0; ClipCursor(nullptr); } }
    f10prev = f10;
    if (g_menuOpen && KeyDown(VK_ESCAPE)) { g_menuOpen = 0; }
    FeedInput(io);
    ImGui_ImplDX9_NewFrame();
    ImGui::NewFrame();
    if (g_menuOpen) {
        DrawMenu(io);
        // software cursor
        ImGui::GetForegroundDrawList()->AddTriangleFilled(io.MousePos, ImVec2(io.MousePos.x + 14 * io.FontGlobalScale, io.MousePos.y + 10 * io.FontGlobalScale), ImVec2(io.MousePos.x + 5 * io.FontGlobalScale, io.MousePos.y + 17 * io.FontGlobalScale), IM_COL32(255, 255, 255, 255));
    } else if (GetTickCount() - g_menuT0 < 25000 || g_hosting) {
        char t[160];
        if (g_hosting) _snprintf_s(t, _TRUNCATE, "co-op: hosting %s - %s   [F10 menu]", g_hostModeName, g_hostTitle);
        else strcpy_s(t, "Co-op menu: F10");
        ImGui::GetForegroundDrawList()->AddText(ImVec2(10, io.DisplaySize.y - 24 * (io.DisplaySize.y > 900 ? 1.5f : 1.f)), IM_COL32(255, 255, 255, 170), t);
    }
    ImGui::EndFrame();
    ImGui::Render();
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
}

static HRESULT WINAPI h_EndScene(IDirect3DDevice9* dev) {
    IDirect3DSurface9 *rt = nullptr, *bb = nullptr;
    bool isFinal = false;
    if (SUCCEEDED(dev->GetRenderTarget(0, &rt)) && SUCCEEDED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb))) isFinal = (rt == bb);
    static LONG nEnd = 0, nFinal = 0;
    if (isFinal) nFinal++;
    if ((++nEnd % 600) == 1) Log("menu: EndScene calls=%ld final=%ld rt=%p bb=%p", nEnd, nFinal, rt, bb);
    if (rt) rt->Release();
    if (bb) bb->Release();
    if (isFinal) { __try { RenderOverlay(dev); } __except (EXCEPTION_EXECUTE_HANDLER) { static int n = 0; if (n++ < 3) Log("menu: exception in overlay render"); } }
    return D3dFor(dev)->end(dev);
}

static HRESULT WINAPI h_Reset(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* pp) {
    if (g_imDev) ImGui_ImplDX9_InvalidateDeviceObjects();
    HRESULT r = D3dFor(dev)->reset(dev, pp);
    if (g_imDev && SUCCEEDED(r)) ImGui_ImplDX9_CreateDeviceObjects();
    return r;
}

static void PatchD3dVtable(IDirect3DDevice9* dev, const char* tag) {
    void** vt = *(void***)dev;
    for (int i = 0; i < g_d3dVtN; i++) if (g_d3dVts[i].vt == vt) return;
    if (g_d3dVtN >= 4) return;
    D3dVt& e = g_d3dVts[g_d3dVtN++];
    e.vt = vt; e.end = (PFN_EndScene)vt[42]; e.reset = (PFN_Reset)vt[16];
    DWORD old;
    VirtualProtect(&vt[42], 4, PAGE_READWRITE, &old); vt[42] = (void*)h_EndScene; VirtualProtect(&vt[42], 4, old, &old);
    VirtualProtect(&vt[16], 4, PAGE_READWRITE, &old); vt[16] = (void*)h_Reset; VirtualProtect(&vt[16], 4, old, &old);
    Log("menu: d3d9 vtable %p (%s) hooked", vt, tag);
}

// Hook IDirect3D9::CreateDevice (via the exe's Direct3DCreate9 import) so we patch the game's own device vtable.
typedef HRESULT(WINAPI* PFN_CreateDevice)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
static void* g_d3dCreateOrig[4]; static void** g_d3dCreateVts[4]; static int g_d3dCreateN = 0;
static HRESULT WINAPI h_CreateDevice(IDirect3D9* self, UINT ad, D3DDEVTYPE t, HWND w, DWORD fl, D3DPRESENT_PARAMETERS* pp, IDirect3DDevice9** out) {
    void** vt = *(void***)self; void* orig = nullptr;
    for (int i = 0; i < g_d3dCreateN; i++) if (g_d3dCreateVts[i] == vt) orig = g_d3dCreateOrig[i];
    HRESULT r = ((PFN_CreateDevice)orig)(self, ad, t, w, fl, pp, out);
    Log("menu: game CreateDevice hr=%08lx flags=%08lx window=%p", r, fl, w);
    if (SUCCEEDED(r) && out && *out) { g_gameWnd = w; SubclassGameWindow(w); PatchD3dVtable(*out, "game"); }
    return r;
}
typedef IDirect3D9*(WINAPI* PFN_Create9)(UINT);
static PFN_Create9 o_Create9;
static IDirect3D9* WINAPI h_Direct3DCreate9(UINT v) {
    IDirect3D9* d9 = o_Create9(v);
    if (d9) {
        void** vt = *(void***)d9; bool have = false;
        for (int i = 0; i < g_d3dCreateN; i++) if (g_d3dCreateVts[i] == vt) have = true;
        if (!have && g_d3dCreateN < 4) {
            g_d3dCreateVts[g_d3dCreateN] = vt; g_d3dCreateOrig[g_d3dCreateN] = vt[16]; g_d3dCreateN++;
            DWORD old; VirtualProtect(&vt[16], 4, PAGE_READWRITE, &old); vt[16] = (void*)h_CreateDevice; VirtualProtect(&vt[16], 4, old, &old);
            Log("menu: IDirect3D9::CreateDevice hooked");
        }
    }
    return d9;
}

// ---------------------------------------------------------------- block game input while the menu is open (DirectInput)
struct DiHook { void** vt; void* getState; void* getData; };
static DiHook g_diHooks[8];
static int g_diCount = 0;
static void* g_diCreateOrig = nullptr;
static void* g_diVtCreate[2] = {nullptr, nullptr};

static DiHook* FindDiHook(void* self) { void** vt = *(void***)self; for (int i = 0; i < g_diCount; i++) if (g_diHooks[i].vt == vt) return &g_diHooks[i]; return nullptr; }

static HRESULT WINAPI h_GetDeviceState(void* self, DWORD cb, void* data) {
    DiHook* h = FindDiHook(self);
    HRESULT r = ((HRESULT(WINAPI*)(void*, DWORD, void*))h->getState)(self, cb, data);
    if (g_menuOpen && SUCCEEDED(r) && data) memset(data, 0, cb);
    return r;
}
static HRESULT WINAPI h_GetDeviceData(void* self, DWORD cb, void* data, DWORD* inout, DWORD flags) {
    DiHook* h = FindDiHook(self);
    HRESULT r = ((HRESULT(WINAPI*)(void*, DWORD, void*, DWORD*, DWORD))h->getData)(self, cb, data, inout, flags);
    if (g_menuOpen && SUCCEEDED(r) && inout) *inout = 0;
    return r;
}
static void HookDiDevice(void* dev) {
    void** vt = *(void***)dev;
    if (FindDiHook(dev) || g_diCount >= 8) return;
    DiHook& h = g_diHooks[g_diCount++];
    h.vt = vt; h.getState = vt[9]; h.getData = vt[10];
    DWORD old; VirtualProtect(&vt[9], 8, PAGE_READWRITE, &old);
    vt[9] = (void*)h_GetDeviceState; vt[10] = (void*)h_GetDeviceData;
    VirtualProtect(&vt[9], 8, old, &old);
    Log("menu: input device vtable %p hooked", vt);
}
// CreateDevice(this, REFGUID, LPDIRECTINPUTDEVICE8*, LPUNKNOWN)
static void* g_diOrigCreate[2]; static void** g_diVts[2]; static int g_diVtN = 0;
static HRESULT WINAPI h_DiCreateDevice(void* self, REFGUID g, void** out, void* unk) {
    void** vt = *(void***)self; void* orig = nullptr;
    for (int i = 0; i < g_diVtN; i++) if (g_diVts[i] == vt) orig = g_diOrigCreate[i];
    HRESULT r = ((HRESULT(WINAPI*)(void*, REFGUID, void**, void*))orig)(self, g, out, unk);
    if (SUCCEEDED(r) && out && *out) HookDiDevice(*out);
    return r;
}
static void HookDirectInput(void* di) {
    void** vt = *(void***)di;
    for (int i = 0; i < g_diVtN; i++) if (g_diVts[i] == vt) return;
    if (g_diVtN >= 2) return;
    g_diVts[g_diVtN] = vt; g_diOrigCreate[g_diVtN] = vt[3]; g_diVtN++;
    DWORD old; VirtualProtect(&vt[3], 4, PAGE_READWRITE, &old); vt[3] = (void*)h_DiCreateDevice; VirtualProtect(&vt[3], 4, old, &old);
    Log("menu: DirectInput CreateDevice hooked");
}

// keep the cursor free while the menu is open
typedef BOOL(WINAPI* PFN_SetCursorPos)(int, int);
static PFN_SetCursorPos o_SetCursorPos;
static BOOL WINAPI h_SetCursorPos(int x, int y) { if (g_menuOpen) return TRUE; return o_SetCursorPos(x, y); }
typedef BOOL(WINAPI* PFN_ClipCursor)(const RECT*);
static PFN_ClipCursor o_ClipCursor;
static BOOL WINAPI h_ClipCursor(const RECT* r) { if (g_menuOpen) return o_ClipCursor(nullptr); return o_ClipCursor(r); }

// After a relaunch into a hosted map the process knows it is hosting only from its command line.
static void InitHostStateFromCmdline() {
    char cl[1024]; WideCharToMultiByte(CP_ACP, 0, GetCommandLineW(), -1, cl, sizeof(cl), nullptr, nullptr);
    char* l = strstr(cl, "?listen");
    if (!l) return;
    char* st = l; while (st > cl && st[-1] != '"' && st[-1] != ' ') st--;
    char map[96] = {0}; size_t n = 0; while (st + n < l && n < sizeof(map) - 1 && st[n] != '?') { map[n] = st[n]; n++; }
    int mode = -1; const char* title = map;
    for (auto& m : kChapters) if (!_stricmp(m.map, map)) { mode = 0; title = m.title; }
    for (auto& m : kEscalation) if (!_stricmp(m.map, map)) { mode = 1; title = m.title; }
    const char* mp = strstr(cl, "MaxPlayers="); int maxp = mp ? atoi(mp + 11) : 5;
    strncpy_s(g_hostTitle, title, _TRUNCATE); strcpy_s(g_hostModeName, mode == 1 ? "Escalation" : "Story");
    g_hostMax = maxp; g_hostBoundPort = 0; g_captureBind = 1; g_hosting = 1;
    Log("menu: started as host of '%s' (%s), max %d", title, g_hostModeName, maxp);
}

static void OverlayStart() {
    InitHostStateFromCmdline();
    InitializeCriticalSection(&g_cmdCs); InitializeCriticalSection(&g_lobCs);
    g_cmdEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    o_SetCursorPos = (PFN_SetCursorPos)Resolve("user32.dll", "SetCursorPos");
    o_ClipCursor = (PFN_ClipCursor)Resolve("user32.dll", "ClipCursor");
    o_GetFG = (PFN_GetFG)Resolve("user32.dll", "GetForegroundWindow");
    if (o_GetFG) PatchIat((void*)o_GetFG, (void*)h_GetForegroundWindow);
    if (o_SetCursorPos) PatchIat((void*)o_SetCursorPos, (void*)h_SetCursorPos);
    if (o_ClipCursor) PatchIat((void*)o_ClipCursor, (void*)h_ClipCursor);
    o_Create9 = (PFN_Create9)Resolve("d3d9.dll", "Direct3DCreate9");
    int n = o_Create9 ? PatchIat((void*)o_Create9, (void*)h_Direct3DCreate9) : 0;
    Log("menu: Direct3DCreate9 import patched (%d)", n);
    CreateThread(nullptr, 0, CmdThread, nullptr, 0, nullptr);
    CreateThread(nullptr, 0, BeaconThread, nullptr, 0, nullptr);
}
