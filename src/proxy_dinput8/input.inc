// input.inc - test input without window focus (stage 6). Included after overlay.inc.
// Commands in foc_cmd.txt:  input hold <key> <ms> | input tap <key> | input mouse <dx> <dy> [ms] | input mode <bits>
//                           input key <scancode hex> <ms> (old form, overlay.inc InjectCommand)
// A held key is made visible to the game by up to four paths (ini inputmode bits, default 6):
//   1 DirectInput GetDeviceState/GetDeviceData (hooks in overlay.inc, state g_injKey*)
//   2 window messages WM_KEYDOWN (repeated) / WM_KEYUP posted to the game window
//   4 GetAsyncKeyState / GetKeyState imports of TFOC.exe report the key as down
//   8 GetForegroundWindow / GetFocus / GetActiveWindow report the game window while a key is held
// ini inputinject=0 turns every injection off (twin of the test).
// g_inputInject / g_inputMode are declared in dinput8.cpp.

struct InKey { const char* name; BYTE sc; BYTE vk; };
static const InKey kInKeys[] = {
    {"W", 0x11, 'W'}, {"A", 0x1E, 'A'}, {"S", 0x1F, 'S'}, {"D", 0x20, 'D'}, {"E", 0x12, 'E'}, {"Q", 0x10, 'Q'},
    {"F", 0x21, 'F'}, {"R", 0x13, 'R'}, {"C", 0x2E, 'C'}, {"V", 0x2F, 'V'}, {"X", 0x2D, 'X'}, {"Z", 0x2C, 'Z'},
    {"G", 0x22, 'G'}, {"T", 0x14, 'T'}, {"H", 0x23, 'H'}, {"B", 0x30, 'B'}, {"N", 0x31, 'N'}, {"M", 0x32, 'M'},
    {"SPACE", 0x39, VK_SPACE}, {"ENTER", 0x1C, VK_RETURN}, {"ESC", 0x01, VK_ESCAPE}, {"TAB", 0x0F, VK_TAB},
    {"LSHIFT", 0x2A, VK_LSHIFT}, {"LCTRL", 0x1D, VK_LCONTROL}, {"LALT", 0x38, VK_LMENU},
    {"1", 0x02, '1'}, {"2", 0x03, '2'}, {"3", 0x04, '3'}, {"4", 0x05, '4'}, {"5", 0x06, '5'},
    {"UP", 0xC8, VK_UP}, {"DOWN", 0xD0, VK_DOWN}, {"LEFT", 0xCB, VK_LEFT}, {"RIGHT", 0xCD, VK_RIGHT},
    {"F1", 0x3B, VK_F1}, {"F2", 0x3C, VK_F2}, {"F3", 0x3D, VK_F3}, {"F4", 0x3E, VK_F4},
};
static bool InLookup(const char* s, BYTE* sc, BYTE* vk) {
    for (auto& k : kInKeys) if (!_stricmp(k.name, s)) { *sc = k.sc; *vk = k.vk; return true; }
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {   // raw DIK scancode
        unsigned v = (unsigned)strtoul(s, nullptr, 16);
        if (v > 0 && v < 256) { *sc = (BYTE)v; *vk = (BYTE)MapVirtualKeyW(v, MAPVK_VSC_TO_VK); return true; }
    }
    return false;
}

static volatile DWORD g_inVkUntil[256];   // GetAsyncKeyState / GetKeyState view, by virtual key
static volatile DWORD g_inFocusUntil = 0;
static bool InVkDown(int vk) {
    DWORD now = GetTickCount();
    auto d = [&](int v) { return v >= 0 && v < 256 && (LONG)(g_inVkUntil[v] - now) > 0; };
    if (vk == VK_SHIFT) return d(VK_LSHIFT) || d(VK_RSHIFT) || d(VK_SHIFT);
    if (vk == VK_CONTROL) return d(VK_LCONTROL) || d(VK_RCONTROL) || d(VK_CONTROL);
    if (vk == VK_MENU) return d(VK_LMENU) || d(VK_RMENU) || d(VK_MENU);
    return d(vk);
}
typedef SHORT(WINAPI* PFN_GetKeyS)(int);
static PFN_GetKeyS o_GetAsyncKeyState, o_GetKeyState;
static SHORT WINAPI h_InGetAsyncKeyState(int vk) {
    SHORT r = o_GetAsyncKeyState(vk);
    if ((g_inputMode & 4) && InVkDown(vk)) r = (SHORT)0x8001;
    return r;
}
static SHORT WINAPI h_InGetKeyState(int vk) {
    SHORT r = o_GetKeyState(vk);
    if ((g_inputMode & 4) && InVkDown(vk)) r = (SHORT)((r & 1) | 0x8000);
    return r;
}
static bool InFocusSpoof() { return (g_inputMode & 8) && g_gameWnd && (LONG)(g_inFocusUntil - GetTickCount()) > 0; }
typedef HWND(WINAPI* PFN_GetWnd)();
static PFN_GetWnd o_InFG, o_GetFocus, o_GetActiveWindow;
static HWND WINAPI h_InGetForegroundWindow() { if (InFocusSpoof()) return g_gameWnd; return o_InFG(); }
static HWND WINAPI h_InGetFocus() { if (InFocusSpoof()) return g_gameWnd; return o_GetFocus(); }
static HWND WINAPI h_InGetActiveWindow() { if (InFocusSpoof()) return g_gameWnd; return o_GetActiveWindow(); }

// GetCursorPos: during a test click (input click) the game sees the cursor at the click point; the menus read the
// cursor position when a mouse message arrives (a moved-only message put the cursor in the corner, 2026-10-22).
typedef BOOL(WINAPI* PFN_GetCursorPos)(LPPOINT);
static PFN_GetCursorPos o_GetCursorPos;
static volatile LONG g_clickX = 0, g_clickY = 0; static volatile DWORD g_clickUntil = 0;
static BOOL WINAPI h_InGetCursorPos(LPPOINT p) {
    if (p && g_gameWnd && (LONG)(g_clickUntil - GetTickCount()) > 0) { POINT q = { g_clickX, g_clickY }; ClientToScreen(g_gameWnd, &q); *p = q; return TRUE; }
    return o_GetCursorPos(p);
}
static void InputStart() {   // called at the end of OverlayStart (after the GetForegroundWindow import was patched there)
    o_GetAsyncKeyState = (PFN_GetKeyS)Resolve("user32.dll", "GetAsyncKeyState");
    o_GetKeyState = (PFN_GetKeyS)Resolve("user32.dll", "GetKeyState");
    o_GetFocus = (PFN_GetWnd)Resolve("user32.dll", "GetFocus");
    o_GetActiveWindow = (PFN_GetWnd)Resolve("user32.dll", "GetActiveWindow");
    o_InFG = (PFN_GetWnd)h_GetForegroundWindow;   // chain after the menu's own hook
    int a = PatchIat((void*)o_GetAsyncKeyState, (void*)h_InGetAsyncKeyState);
    int b = PatchIat((void*)o_GetKeyState, (void*)h_InGetKeyState);
    int c = PatchIat((void*)o_GetFocus, (void*)h_InGetFocus);
    int d = PatchIat((void*)o_GetActiveWindow, (void*)h_InGetActiveWindow);
    int e = PatchIat((void*)h_GetForegroundWindow, (void*)h_InGetForegroundWindow);
    o_GetCursorPos = (PFN_GetCursorPos)Resolve("user32.dll", "GetCursorPos");
    int f = PatchIat((void*)o_GetCursorPos, (void*)h_InGetCursorPos);
    Log("input: GetCursorPos import patched=%d", f);
    Log("input: injection %s mode %d; imports patched GetAsyncKeyState=%d GetKeyState=%d GetFocus=%d GetActiveWindow=%d GetForegroundWindow=%d",
        g_inputInject ? "on" : "OFF (inputinject=0)", g_inputMode, a, b, c, d, e);
}

struct InPump { HWND w; UINT vk, sc; DWORD ms; };
static DWORD WINAPI InKeyMsgThread(LPVOID p) {   // window-message path: key down, repeats, key up
    InPump* k = (InPump*)p;
    LPARAM base = 1 | ((LPARAM)k->sc << 16);
    if (k->sc >= 0xC8 || k->sc == 0xCB || k->sc == 0xCD || k->sc == 0xD0) base |= (1 << 24);   // extended (arrows)
    DWORD end = GetTickCount() + k->ms;
    PostMessageW(k->w, WM_KEYDOWN, k->vk, base);
    while ((LONG)(end - GetTickCount()) > 0) { Sleep(50); if ((LONG)(end - GetTickCount()) > 0) PostMessageW(k->w, WM_KEYDOWN, k->vk, base | (1u << 30)); }
    PostMessageW(k->w, WM_KEYUP, k->vk, base | (1u << 30) | (1u << 31));
    delete k; return 0;
}

static void InputHold(const char* name, int ms) {
    BYTE sc = 0, vk = 0;
    if (!InLookup(name, &sc, &vk)) { Log("input: unknown key '%s'", name); return; }
    if (!g_inputInject) { Log("input: hold %s ignored (inputinject=0)", name); return; }
    if (ms < 30) ms = 30;
    DWORD until = GetTickCount() + (DWORD)ms;
    if (g_inputMode & 1) { g_injKeyUntil[sc] = until; g_injKeyEdge[sc] = 1; }
    if (g_inputMode & 4) g_inVkUntil[vk] = until;
    if ((LONG)(until + 300 - g_inFocusUntil) > 0) g_inFocusUntil = until + 300;
    if ((g_inputMode & 2) && g_gameWnd) CreateThread(nullptr, 0, InKeyMsgThread, new InPump{g_gameWnd, vk, sc, (DWORD)ms}, 0, nullptr);
    Log("input: hold %s (sc 0x%02x vk 0x%02x) %d ms, mode %d, window %p", name, sc, vk, ms, g_inputMode, g_gameWnd);
}

// `input click <x> <y>` (2026-10-22, menu tests with the mouse): a left click at x,y given in 960x540 screenshot
// coordinates, scaled to the window's client area; sent as window messages (move, button down, button up) - the
// Scaleform menus follow the window's mouse messages. The focus spoof covers the click.
// The menu cursor follows DirectInput mouse deltas (window messages put it in the corner): the click first pushes the
// cursor into the top-left corner, then moves it by x,y times the scale (ini clickscale, default 100 = 1 delta per
// screenshot pixel; calibrate with `input clickmove`), then presses the DirectInput left button.
static int g_clickScale = 100;
struct InClick { HWND w; int x, y; bool press; };
static DWORD WINAPI InClickThread(LPVOID p) {
    InClick c = *(InClick*)p; delete (InClick*)p;
    InjectCommand("mouse -8000 -8000 250"); Sleep(500);
    char m[64]; _snprintf_s(m, _TRUNCATE, "mouse %d %d 250", c.x * g_clickScale / 100, c.y * g_clickScale / 100); InjectCommand(m); Sleep(500);
    if (c.press) InjectCommand("button 150");
    return 0;
}
static void InjectCommand(const char* args);   // overlay.inc
static void InputClick(int x, int y, bool press = true) {
    if (!g_gameWnd) { Log("input: click ignored (no window)"); return; }
    RECT r; GetClientRect(g_gameWnd, &r);
    int cx = (int)((long long)x * (r.right - r.left) / 960), cy = (int)((long long)y * (r.bottom - r.top) / 540);
    DWORD until = GetTickCount() + 1500; if ((LONG)(until - g_inFocusUntil) > 0) g_inFocusUntil = until;
    g_clickX = cx; g_clickY = cy; g_clickUntil = until;
    CreateThread(nullptr, 0, InClickThread, new InClick{g_gameWnd, x, y, press}, 0, nullptr);
    Log("input: click %d,%d -> client %d,%d (%ldx%ld)", x, y, cx, cy, r.right - r.left, r.bottom - r.top);
}
static void InputCommand(const char* args) {
    char kind[16] = "", name[24] = ""; int a = 0, b = 0, c = 0;
    if (sscanf_s(args, "%15s", kind, (unsigned)sizeof(kind)) != 1) return;
    if (!strcmp(kind, "hold") && sscanf_s(args, "%*s %23s %d", name, (unsigned)sizeof(name), &a) == 2) InputHold(name, a);
    else if (!strcmp(kind, "tap") && sscanf_s(args, "%*s %23s", name, (unsigned)sizeof(name)) == 1) InputHold(name, 120);
    else if (!strcmp(kind, "mode") && sscanf_s(args, "%*s %d", &a) == 1) { g_inputMode = a; Log("input: mode now %d", a); }
    else if (!strcmp(kind, "click") && sscanf_s(args, "%*s %d %d", &a, &b) == 2) InputClick(a, b);
    else if (!strcmp(kind, "clickmove") && sscanf_s(args, "%*s %d %d", &a, &b) == 2) InputClick(a, b, false);
    else if (!strcmp(kind, "clickscale") && sscanf_s(args, "%*s %d", &a) == 1) { g_clickScale = a; Log("input: click scale %d", a); }
    else if (!strcmp(kind, "mouse")) {
        int n = sscanf_s(args, "%*s %d %d %d", &a, &b, &c);
        if (n < 2) { Log("input: bad command '%s'", args); return; }
        if (!g_inputInject) { Log("input: mouse ignored (inputinject=0)"); return; }
        char buf[96]; _snprintf_s(buf, _TRUNCATE, "mouse %d %d %d", a, b, n >= 3 ? c : 500); InjectCommand(buf);
    } else if (!g_inputInject) Log("input: '%s' ignored (inputinject=0)", args);
    else InjectCommand(args);   // `key <sc> <ms>` (old form; DirectInput + messages)
}
