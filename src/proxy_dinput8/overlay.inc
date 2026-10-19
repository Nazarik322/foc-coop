// In-game co-op menu: Direct3D9 overlay (Dear ImGui) + engine console command + LAN lobby beacon.
// Included at the end of dinput8.cpp (shares Log / Rd / PatchIat).
#include <d3d9.h>
#include <ws2tcpip.h>
#include <vector>
#include <algorithm>
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

// `crashtest` (foc_cmd.txt): fault inside this DLL on purpose; the VEH writes a test minidump and our handler recovers.
static void CrashTest() {
    g_crashTest = 1;
    __try { *(volatile int*)(UINT_PTR)8 = 1; } __except (EXCEPTION_EXECUTE_HANDLER) {}
    Log("crashtest: done (dump requested: %s)", g_crashTest ? "not written" : "written");
}

// `dump <Class> <file> [bytes]` (foc_cmd.txt, game thread): raw bytes of every live instance of a class, for diffing
// an object before and after an event. Per object: "OBJ <name> <address>" and hex words, 8 per line.
static void DumpClass(const char* args) {
    char cls[80] = "", file[MAX_PATH] = ""; int size = 0x800;
    if (sscanf_s(args, "%79s %259s %i", cls, (unsigned)sizeof(cls), file, (unsigned)sizeof(file), &size) < 2) { Log("dump: usage dump <Class> <file> [bytes]"); return; }
    unsigned cid = ClassIdByName(cls);
    if (!cid) { Log("dump: no class %s", cls); return; }
    FILE* f = nullptr; fopen_s(&f, file, "w");
    if (!f) { Log("dump: cannot write %s", file); return; }
    TArr* go = (TArr*)((BYTE*)GetModuleHandleW(nullptr) + kGObjRva);
    int n = 0;
    for (int i = 0; i < go->Num; i++) {
        BYTE* o = (BYTE*)go->Data[i];
        if (!o || (*(unsigned*)(o + 0x14) & 0x1fff) != cid || !Rd(o, size)) continue;
        char nm[80] = ""; unsigned num = 0; ObjNameStr(o, nm, sizeof(nm), &num);
        fprintf(f, "OBJ %s_%u %p\n", nm, num, o);
        for (int w = 0; w < size / 4; w++) fprintf(f, "%08x%s", ((unsigned*)o)[w], (w % 8 == 7) ? "\n" : " ");
        n++;
    }
    fclose(f);
    Log("dump: %d object(s) of %s -> %s", n, cls, file);
}

// `fall <local|remote>` / `tp <local|remote> <x> <y> <z>` (foc_cmd.txt, host, game thread): test deaths and teleports.
// `fall` drops the pawn far below the level so the game kills it (KillZ), like a fall into a pit.
static void MovePawnCommand(const char* args, bool fall) {
    char who[16] = ""; float p[3] = {0, 0, 0};
    int got = sscanf_s(args, "%15s %f %f %f", who, (unsigned)sizeof(who), &p[0], &p[1], &p[2]);
    if (got < 1 || (!fall && got < 4)) { Log("tp/fall: bad arguments '%s'", args); return; }
    bool local = !strcmp(who, "local");
    for (int a = 0; a < g_to.npc; a++) {
        BYTE* pc = g_to.pc[a];
        if (!IsObjFast(pc) || IsLocalPC(pc) != local) continue;
        BYTE* pawn = PawnOf(pc);
        if (!pawn) { Log("tp/fall: %s player has no pawn", who); continue; }
        float* loc = LocOf(pawn);
        float n[3] = {loc[0], loc[1], loc[2] - 1000000.0f};
        if (!fall) memcpy(n, p, sizeof(n));
        Log("%s: %s pawn (%.0f,%.0f,%.0f) -> (%.0f,%.0f,%.0f)", fall ? "fall" : "tp", who, loc[0], loc[1], loc[2], n[0], n[1], n[2]);
        memcpy(loc, n, 12); memcpy(pawn + 0x134, n, 12);
    }
}

// `props <name> [name...]` (foc_cmd.txt, game thread): log every object with one of these names (UProperty objects
// of script variables) with its class and first 0x60 bytes, to find UProperty::Offset and the variables' offsets.
static void PropsCommand(const char* args) {
    char want[8][64]; int nw = 0;
    for (const char* p = args; *p && nw < 8;) {
        while (*p == ' ') p++;
        int k = 0; while (*p && *p != ' ' && k < 63) want[nw][k++] = *p++;
        want[nw][k] = 0; if (k) nw++;
    }
    TArr* go = (TArr*)((BYTE*)GetModuleHandleW(nullptr) + kGObjRva);
    int found = 0;
    for (int i = 0; i < go->Num && found < 200; i++) {
        BYTE* o = (BYTE*)go->Data[i];
        if (!o || !Rd(o, 0x60)) continue;
        char nm[80]; unsigned num = 0;
        if (!ObjNameStr(o, nm, sizeof(nm), &num) || num != 0) continue;
        bool hit = false; for (int w = 0; w < nw; w++) if (!strcmp(nm, want[w])) hit = true;
        if (!hit) continue;
        char cn[80] = "?"; ClassNameOf(o, cn, sizeof(cn));
        if (!strstr(cn, "Property")) continue;
        char line[700]; size_t k = _snprintf_s(line, _TRUNCATE, "props: %s %s %p:", nm, cn, o);
        for (int w = 0; w < 0x60 / 4; w++) k += _snprintf_s(line + k, sizeof(line) - k, _TRUNCATE, " %x", ((unsigned*)o)[w]);
        Log("%s", line); found++;
    }
    Log("props: %d object(s)", found);
}

// `hp <local|remote> <value>` (foc_cmd.txt, host): set a player pawn's Health (TnPawn float +0x350; HealthMax +0x354)
static void HpCommand(const char* args) {
    char who[16] = ""; float v = 0;
    if (sscanf_s(args, "%15s %f", who, (unsigned)sizeof(who), &v) < 2) { Log("hp: bad arguments '%s'", args); return; }
    bool local = !strcmp(who, "local");
    for (int a = 0; a < g_to.npc; a++) {
        BYTE* pc = g_to.pc[a];
        if (!IsObjFast(pc) || IsLocalPC(pc) != local) continue;
        BYTE* pawn = PawnOf(pc);
        if (!pawn) { Log("hp: %s player has no pawn", who); continue; }
        float* h = (float*)(pawn + 0x350);
        Log("hp: %s pawn health %.0f/%.0f -> %.0f", who, h[0], h[1], v);
        h[0] = v;
    }
}

// `lpexec <console command>` (foc_cmd.txt, game thread): run a console command through the local player
// (ULocalPlayer::Exec, the FExec subobject of TnLocalPlayer: the first word after the object header that points at a
// vtable whose first slot is code). Unlike the engine Exec this reaches the controller's, the pawn's and the cheat
// manager's exec functions.
static void LpExecCommand(const char* cmd) {
    if (g_to.nlp < 1) { Log("lpexec: no local player"); return; }
    BYTE* lp = g_to.lp[0];
    BYTE* base = (BYTE*)GetModuleHandleW(nullptr);
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
    BYTE* end = base + nt->OptionalHeader.SizeOfImage;
    int want = -1; const char* sp = strstr(cmd, "@"); if (sp) { want = strtol(sp + 1, nullptr, 16); }   // "cmd @off" picks the subobject
    for (int off = 0; off < 0x100; off += 4) {   // candidates, for the log
        BYTE* vt = *(BYTE**)(lp + off);
        if (vt < base || vt >= end || !Rd(vt, 16)) continue;
        Log("lpexec: candidate +0x%x vtable ghidra %lx slots %lx %lx %lx %lx", off, (unsigned long)(0x400000 + (vt - base)),
            (unsigned long)(0x400000 + (((BYTE**)vt)[0] - base)), (unsigned long)(0x400000 + (((BYTE**)vt)[1] - base)), (unsigned long)(0x400000 + (((BYTE**)vt)[2] - base)), (unsigned long)(0x400000 + (((BYTE**)vt)[3] - base)));
    }
    if (want < 0) { Log("lpexec: add @<offset> to choose the subobject"); return; }
    char c2[400]; strncpy_s(c2, cmd, _TRUNCATE); char* at = strstr(c2, " @"); if (at) *at = 0; cmd = c2;
    for (int off = want; off == want; off += 4) {
        BYTE* vt = *(BYTE**)(lp + off);
        if (vt < base || vt >= end || !Rd(vt, 4)) continue;
        BYTE* fn = *(BYTE**)vt;
        if (fn < base + 0x1000 || fn >= base + 0x1000 + 0xf5fc71) continue;
        wchar_t w[512]; MultiByteToWideChar(CP_ACP, 0, cmd, -1, w, 512);
        Log("lpexec: '%s' via TnLocalPlayer %p +0x%x fn ghidra %lx", cmd, lp, off, (unsigned long)(0x400000 + (fn - base)));
        BOOL r = FALSE;
        __try { r = CallExecSafe(fn, lp + off, w, g_outDev); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("lpexec: exception %08lx", GetExceptionCode()); return; }
        Log("lpexec: returned %d", (int)r);
        return;
    }
    Log("lpexec: no FExec subobject found");
}

// `oexec <target> <exec function and args>` (foc_cmd.txt, game thread): run a script exec function on an object through
// UObject::ScriptConsoleExec (ghidra 0xbf1f70, thiscall: const TCHAR* Str, FOutputDevice& Ar, UObject* Executor; it finds
// the function by name, checks FUNC_Exec, parses the arguments and calls ProcessEvent = ghidra 0x728c60).
// Targets: pc / pawn (local player), rpc / rpawn (first remote player, host only), game, gri, obj:<Name_N>.
typedef BOOL(__fastcall* PFN_ScriptConsoleExec)(void* self, void* edx, const wchar_t* str, void* ar, void* executor);
static BYTE* OexecTarget(const char* t) {
    BYTE* lpc = nullptr; BYTE* rpc = nullptr;
    for (int a = 0; a < g_to.npc; a++) { if (!IsObjFast(g_to.pc[a])) continue; if (IsLocalPC(g_to.pc[a])) lpc = g_to.pc[a]; else if (!rpc) rpc = g_to.pc[a]; }
    if (!strcmp(t, "pc")) return lpc;
    if (!strcmp(t, "pawn")) return lpc ? PawnOf(lpc) : nullptr;
    if (!strcmp(t, "rpc")) return rpc;
    if (!strcmp(t, "rpawn")) return rpc ? PawnOf(rpc) : nullptr;
    if (!strcmp(t, "game")) return g_to.ngame ? g_to.game[0] : nullptr;
    if (!strcmp(t, "pri")) return lpc ? *(BYTE**)(lpc + 0x1d4) : nullptr;
    if (!strcmp(t, "rpri")) return rpc ? *(BYTE**)(rpc + 0x1d4) : nullptr;
    if (!strncmp(t, "obj:", 4)) return FindObjByLabel(t + 4);
    return nullptr;
}
static void OexecCommand(const char* args) {
    char target[96] = ""; int n = 0; sscanf_s(args, "%95s%n", target, (unsigned)sizeof(target), &n);
    const char* rest = args + n; while (*rest == ' ') rest++;
    BYTE* obj = OexecTarget(target);
    if (!obj || !*rest) { Log("oexec: no target '%s' or no command", target); return; }
    wchar_t w[512]; MultiByteToWideChar(CP_ACP, 0, rest, -1, w, 512);
    PFN_ScriptConsoleExec fn = (PFN_ScriptConsoleExec)((BYTE*)GetModuleHandleW(nullptr) + (0xbf1f70 - 0x400000));
    char on[96]; ObjLabel(obj, on, sizeof(on));
    BOOL r = FALSE;
    __try { r = fn(obj, nullptr, w, g_outDev, nullptr); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("oexec: %s '%s' raised %08lx", on, rest, GetExceptionCode()); return; }
    Log("oexec: %s '%s' -> %s", on, rest, r ? "handled" : "not an exec function of this object");
}

// `poke <target> <hex offset> <int>` (foc_cmd.txt, game thread): write an int into an object (targets as oexec, plus
// pri / rpri); e.g. Escalation money TnPlayerReplicationInfoSurvival._SurvivalMoney (offset from `props`)
static void PokeCommand(const char* args) {
    char target[96] = ""; unsigned off = 0; int v = 0;
    if (sscanf_s(args, "%95s %x %d", target, (unsigned)sizeof(target), &off, &v) < 3) { Log("poke: bad arguments '%s'", args); return; }
    BYTE* obj = OexecTarget(target);
    if (!IsObjFast(obj) || off > 0x2000 || !Rd(obj + off, 4)) { Log("poke: no target '%s'", target); return; }
    int old = *(int*)(obj + off); *(int*)(obj + off) = v;
    char on[96]; ObjLabel(obj, on, sizeof(on)); Log("poke: %s +0x%x %d -> %d", on, off, old, v);
}

// `call <target> <Function> [i:int f:float b:0|1 o:target ...]` (foc_cmd.txt, game thread): ProcessEvent on a script
// function (sync.inc CallScript). Arguments are 4-byte slots in order (a vector = three f:); the parameter block
// after the call is logged (return values and out parameters).
static void CallCommand(const char* args) {
    char target[96] = "", fn[96] = ""; int n = 0;
    if (sscanf_s(args, "%95s %95s%n", target, (unsigned)sizeof(target), fn, (unsigned)sizeof(fn), &n) < 2) { Log("call: usage call <target> <Function> [args]"); return; }
    BYTE* obj = OexecTarget(target);
    if (!IsObjFast(obj)) { Log("call: no target '%s'", target); return; }
    static BYTE parms[512]; memset(parms, 0, sizeof(parms)); int k = 0;
    for (const char* p = args + n; *p && k < 120;) {
        while (*p == ' ') p++;
        if (!*p) break;
        char tok[96]; int i = 0; while (p[i] && p[i] != ' ' && i < 95) { tok[i] = p[i]; i++; } tok[i] = 0; p += i;
        DWORD v = 0;
        if (!strncmp(tok, "f:", 2)) { float f = (float)atof(tok + 2); memcpy(&v, &f, 4); }
        else if (!strncmp(tok, "o:", 2)) v = (DWORD)(UINT_PTR)OexecTarget(tok + 2);
        else v = (DWORD)strtol(tok + 2, nullptr, 0);   // i: or b:
        ((DWORD*)parms)[k++] = v;
    }
    char on[96]; ObjLabel(obj, on, sizeof(on));
    bool ok = CallScript(obj, fn, parms, sizeof(parms));
    char line[300]; size_t l = _snprintf_s(line, _TRUNCATE, "call: %s.%s -> %s; parms:", on, fn, ok ? "done" : "failed");
    for (int i = 0; i < 12; i++) l += _snprintf_s(line + l, sizeof(line) - l, _TRUNCATE, " %08lx", ((DWORD*)parms)[i]);
    Log("%s", line);
}

// `funcs <part> [part...]` (foc_cmd.txt): log the Function objects whose name contains one of the parts, with their
// outer (class / state) and ParmsSize, to see what can be called
static void FuncsCommand(const char* args) {
    unsigned fid = ClassIdByName("Function");
    TArr* go = GObj(); int n = 0;
    for (int i = 0; i < go->Num && n < 150; i++) {
        BYTE* o = (BYTE*)go->Data[i];
        if (!o || (*(unsigned*)(o + 0x14) & 0x1fff) != fid) continue;
        char nm[96]; if (!ObjNameStr(o, nm, sizeof(nm))) continue;
        bool hit = false; char buf[200]; strncpy_s(buf, args, _TRUNCATE); char* ctx = nullptr;
        for (char* t = strtok_s(buf, " ", &ctx); t; t = strtok_s(nullptr, " ", &ctx)) if (strstr(nm, t)) hit = true;
        if (!hit) continue;
        char outer[96] = "?"; BYTE* ou = *(BYTE**)(o + 0x28); if (IsObjFast(ou)) ObjNameStr(ou, outer, sizeof(outer));
        Log("funcs: %s.%s parms=%u flags=%08lx", outer, nm, *(unsigned short*)(o + 0x5e), *(unsigned*)(o + 0x54)); n++;
    }
    Log("funcs: %d found", n);
}

// `key <vk>` (foc_cmd.txt): post a key press (WM_KEYDOWN + WM_KEYUP) to the game window; the game's own menus read
// window messages, so this works without focus (e.g. 13 = Enter on the MISSION FAILED menu)
static void PostKey(UINT vk) {
    if (!g_gameWnd) return;
    UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    PostMessageW(g_gameWnd, WM_KEYDOWN, vk, 1 | (sc << 16));
    PostMessageW(g_gameWnd, WM_KEYUP, vk, 1 | (sc << 16) | (1u << 30) | (1u << 31));
}

static void RunCommandNow(const std::string& cmd) {
    if (cmd.rfind("restartplayer ", 0) == 0) {   // `restartplayer <pc|rpc>`: base GameInfo::RestartPlayer (ghidra 0x7c2ee0) on the host
        BYTE* pc = OexecTarget(cmd.c_str() + 14);
        if (!pc || !g_to.ngame) { Log("restartplayer: no controller or not the host"); return; }
        bool ok = NativeRestartPlayer(g_to.game[0], pc);
        char pn[96]; ObjLabel(PawnOf(pc), pn, sizeof(pn)); Log("restartplayer: %s -> pawn %s", ok ? "called" : "raised", pn); return;
    }
    if (cmd.rfind("vtfind ", 0) == 0) {   // `vtfind <target> <ghidra hex>`: vtable of the object (ghidra VA) and the slots holding that address
        char t[96] = ""; unsigned long want = 0; sscanf_s(cmd.c_str() + 7, "%95s %lx", t, (unsigned)sizeof(t), &want);
        BYTE* o = OexecTarget(t); if (!o) { Log("vtfind: no target"); return; }
        BYTE* base = (BYTE*)GetModuleHandleW(nullptr); BYTE** vt = *(BYTE***)o;
        Log("vtfind: %s vtable ghidra %lx", t, (unsigned long)(0x400000 + ((BYTE*)vt - base)));
        for (int i = 0; i < 400 && Rd(vt + i, 4); i++) if ((unsigned long)(0x400000 + (vt[i] - base)) == want) Log("vtfind: slot %d (+0x%x)", i, i * 4);
        return;
    }
    if (cmd.rfind("gotostate ", 0) == 0) {
        char t[96] = "", st[64] = ""; sscanf_s(cmd.c_str() + 10, "%95s %63s", t, (unsigned)sizeof(t), st, (unsigned)sizeof(st));
        BYTE* o = OexecTarget(t); char s0[64] = "", s1[64] = "";
        if (o) StateName(o, s0, sizeof(s0));
        bool ok = GotoStateNamed(o, st);
        if (o) StateName(o, s1, sizeof(s1));
        Log("gotostate: %s %s -> %s (%s)", t, s0, s1, ok ? "called" : "failed"); return;
    }
    if (cmd.rfind("key ", 0) == 0) { PostKey((UINT)strtol(cmd.c_str() + 4, nullptr, 0)); Log("key: posted %s", cmd.c_str() + 4); return; }
    if (cmd.rfind("funcs ", 0) == 0) { FuncsCommand(cmd.c_str() + 6); return; }
    if (cmd.rfind("call ", 0) == 0) { CallCommand(cmd.c_str() + 5); return; }
    if (cmd.rfind("oexec ", 0) == 0) { OexecCommand(cmd.c_str() + 6); return; }
    if (cmd.rfind("poke ", 0) == 0) { PokeCommand(cmd.c_str() + 5); return; }
    if (cmd.rfind("lpexec ", 0) == 0) { LpExecCommand(cmd.c_str() + 7); return; }
    if (cmd.rfind("hp ", 0) == 0) { HpCommand(cmd.c_str() + 3); return; }
    if (cmd.rfind("tpmarker", 0) == 0) { TpMarkerCommand(cmd.size() > 9 ? cmd.c_str() + 9 : ""); return; }
    if (cmd.rfind("props ", 0) == 0) { PropsCommand(cmd.c_str() + 6); return; }
    if (cmd == "crashtest") { CrashTest(); return; }
    if (cmd.rfind("dump ", 0) == 0) { DumpClass(cmd.c_str() + 5); return; }
    if (cmd.rfind("fall ", 0) == 0) { MovePawnCommand(cmd.c_str() + 5, true); return; }
    if (cmd.rfind("tp ", 0) == 0) { MovePawnCommand(cmd.c_str() + 3, false); return; }
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

static void InjectCommand(const char* args);
static void JoinViaSteam();
static volatile LONG g_shotReq = 0;      // set by `shot`, served by the next final EndScene (SaveBackBuffer)
static volatile LONG g_shotUiReq = 0;    // `shotui`: the same, but taken after the co-op menu was drawn
static volatile LONG g_menuReq = -1;     // `menu <page>` / `menu off`: open the F10 menu on a page (tests)
static wchar_t g_shotPath[MAX_PATH];
static bool HandleLocalCommand(const std::string& c) {   // test commands that do not need the game thread
    bool ui = c.rfind("shotui", 0) == 0;
    if (ui || c == "shot" || c.rfind("shot ", 0) == 0) {
        size_t skip = ui ? 7 : 5;
        wchar_t dir[MAX_PATH]; GetModuleFileNameW(nullptr, dir, MAX_PATH);
        wchar_t* sl = wcsrchr(dir, L'\\'); if (sl) *sl = 0;
        if (c.size() > skip) MultiByteToWideChar(CP_ACP, 0, c.c_str() + skip, -1, g_shotPath, MAX_PATH);
        else _snwprintf_s(g_shotPath, _TRUNCATE, L"%s\\foc_shot.bmp", dir);
        if (ui) g_shotUiReq = 1; else g_shotReq = 1;
        return true;
    }
    if (c.rfind("menu ", 0) == 0) { g_menuReq = c.substr(5) == "off" ? 99 : atoi(c.c_str() + 5); return true; }
    if (c.rfind("input ", 0) == 0) { InjectCommand(c.c_str() + 6); return true; }
    if (c == "steamjoin") { JoinViaSteam(); return true; }   // same as the Join page's "Join a friend through Steam"
    return false;
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
        if (!c.empty() && HandleLocalCommand(c)) continue;
        if (!c.empty()) {
            if (g_gameWnd && IsWindow(g_gameWnd)) {   // run on the game thread (window procedure)
                EnterCriticalSection(&g_cmdCs); g_cmdForGame = c; LeaveCriticalSection(&g_cmdCs);
                PostMessageW(g_gameWnd, WM_APP + 0x4F0, 0, 0);
            } else Log("menu: no game window yet, command dropped: %s", c.c_str());
        }
    }
}

static HHOOK g_msgHook = nullptr;
static const UINT kTickMsg = WM_APP + 0x4F1;   // posted by TickThread; runs GameTickSafe() on the game thread
static volatile LONG g_tickPosted = 0;
static volatile DWORD g_tickPostedAt = 0;
static volatile LONG g_menuOpen = 0;
static LRESULT CALLBACK GetMsgProc(int code, WPARAM wp, LPARAM lp) {
    if (code >= 0 && lp) {
        MSG* m = (MSG*)lp;
        // F10 menu open: keyboard, mouse and raw input never reach the game (its own menus read window messages; the
        // DirectInput hooks already return "nothing pressed"). Our menu reads keys with GetAsyncKeyState.
        if (g_menuOpen && ((m->message >= WM_KEYFIRST && m->message <= WM_KEYLAST) || (m->message >= WM_MOUSEFIRST && m->message <= WM_MOUSELAST) || m->message == WM_INPUT))
            m->message = WM_NULL;
        if (m->message == WM_APP + 0x4F0) {
            std::string c; EnterCriticalSection(&g_cmdCs); c.swap(g_cmdForGame); LeaveCriticalSection(&g_cmdCs);
            if (!c.empty()) RunCommandNow(c);
            m->message = WM_NULL;
        } else if (m->message == kTickMsg && wp == PM_REMOVE) {
            g_tickPosted = 0;
            GameTickSafe();
            m->message = WM_NULL;
        }
    }
    return CallNextHookEx(g_msgHook, code, wp, lp);
}

// Posts kTickMsg every g_tickMs while the game thread keeps up (one message in flight at a time; a message lost
// for 5 s, for example while a map loads without pumping messages, is re-posted).
static DWORD WINAPI TickThread(LPVOID) {
    for (;;) {
        Sleep(g_tickMs);
        HWND w = g_gameWnd;
        if (!w || !g_msgHook || !IsWindow(w)) continue;
        if (g_tickPosted && GetTickCount() - g_tickPostedAt < 5000) continue;
        g_tickPosted = 1; g_tickPostedAt = GetTickCount();
        PostMessageW(w, kTickMsg, 0, 0);
    }
}
// Commands must run on the game thread (the thread that owns the window); a message hook on that thread does it.
static void SubclassGameWindow(HWND w) {
    if (g_msgHook || !w) return;
    DWORD tid = GetWindowThreadProcessId(w, nullptr);
    g_msgHook = SetWindowsHookExW(WH_GETMESSAGE, GetMsgProc, nullptr, tid);
    Log("menu: message hook on game thread %lu: %p (err %lu)", tid, g_msgHook, g_msgHook ? 0 : GetLastError());
}

// ---------------------------------------------------------------- menu state

struct MapEntry { const char* title; const char* map; };
static const char kEscalationOptions[] = "?GameModeTag=SV?GameTeamStatus=2?GameRules=TransGame.TnGameRules_CompetitiveScoring";
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

static int g_page = 0;            // 0 main, 1 host, 2 join, 3 about, 4 settings
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

static int CurrentPlayerCount() { return g_playerCount > 0 ? (int)g_playerCount : 1; }

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
static void RelaunchWithUrl(const char* url, const char* extra = "") {
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
        if (!_wcsicmp(a, L"-steamrelay")) continue;                       // set again below when wanted
        cmd += L" " + std::wstring(a);
    }
    if (extra && extra[0]) { wchar_t we[128]; MultiByteToWideChar(CP_ACP, 0, extra, -1, we, 128); cmd += L" " + std::wstring(we); }
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

// ---------------------------------------------------------------- Steam relay (FoCRelay.exe in Binaries\foc_relay)
// Friends join through Steam (AppID 480 "Spacewar") without IPs: the host runs `FoCRelay host <gamePort>`, the guest runs
// `FoCRelay join <lobby|auto> 7790` and its game connects to 127.0.0.1:7790. The relay's own steam_api.dll lives in
// foc_relay so it never replaces the game's Steam_api.dll.
static const int kRelayLocalPort = 7790;
static bool g_useSteam = true;            // Host page checkbox
static char g_joinLobby[32] = "";         // Join page: lobby id, empty = first friend who hosts
static char g_relayState[160] = "";
static unsigned long long g_relayLobby = 0; static int g_relayPeers = 0; static char g_relayRole[16] = "";
static void RelayDir(wchar_t* out) { GetModuleFileNameW(nullptr, out, MAX_PATH); wchar_t* sl = wcsrchr(out, L'\\'); if (sl) wcscpy_s(sl, 32, L"\\foc_relay"); }
static bool StartRelay(const char* args) {
    wchar_t dir[MAX_PATH]; RelayDir(dir);
    wchar_t st[MAX_PATH]; _snwprintf_s(st, _TRUNCATE, L"%s\\relay.status", dir); DeleteFileW(st);
    wchar_t wa[128]; MultiByteToWideChar(CP_ACP, 0, args, -1, wa, 128);
    wchar_t cmd[MAX_PATH + 160]; _snwprintf_s(cmd, _TRUNCATE, L"\"%s\\FoCRelay.exe\" %s", dir, wa);
    STARTUPINFOW si = {sizeof(si)}; PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, dir, &si, &pi)) {
        Log("relay: cannot start %ls (%lu)", cmd, GetLastError()); strcpy_s(g_relayState, "FoCRelay.exe not found in Binaries\\foc_relay"); return false;
    }
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    Log("relay: started %ls", cmd); return true;
}
static void ReadRelayStatus() {   // relay.status: key=value lines
    wchar_t dir[MAX_PATH]; RelayDir(dir); wchar_t p[MAX_PATH]; _snwprintf_s(p, _TRUNCATE, L"%s\\relay.status", dir);
    FILE* f = nullptr; _wfopen_s(&f, p, L"r"); if (!f) return;
    char line[200];
    while (fgets(line, sizeof(line), f)) {
        size_t l = strlen(line); while (l && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = 0;
        if (!strncmp(line, "state=", 6)) strncpy_s(g_relayState, line + 6, _TRUNCATE);
        else if (!strncmp(line, "lobby=", 6)) g_relayLobby = _strtoui64(line + 6, nullptr, 10);
        else if (!strncmp(line, "peers=", 6)) g_relayPeers = atoi(line + 6);
        else if (!strncmp(line, "role=", 5)) strncpy_s(g_relayRole, line + 5, _TRUNCATE);
        else if (!strncmp(line, "persona=", 8)) strncpy_s(g_relayPersona, line + 8, _TRUNCATE);
    }
    fclose(f);
}
static void RelayCommand(const char* c) {
    wchar_t dir[MAX_PATH]; RelayDir(dir); wchar_t p[MAX_PATH]; _snwprintf_s(p, _TRUNCATE, L"%s\\relay.cmd", dir);
    FILE* f = nullptr; _wfopen_s(&f, p, L"w"); if (f) { fputs(c, f); fclose(f); }
}
// host started with -steamrelay: once the game's real listen port is known, start the host relay
static DWORD WINAPI HostRelayThread(LPVOID) {
    // the first bind in 7777..7800 is an early LAN socket, the listen socket comes when the map loads: wait for the map
    for (int i = 0; i < 600 && !(g_hostBoundPort && g_to.ngame > 0 && g_to.npc > 0); i++) Sleep(500);
    Sleep(2000);
    if (!g_hostBoundPort) { Log("relay: no listen port, host relay not started"); return 0; }
    // lobby data for the Join page: max players, mode and map from our command line URL
    const char* url = CmdlineUrl();
    char map[96] = ""; int i = 0; while (url[i] && url[i] != '?' && i < 95) { map[i] = url[i]; i++; } map[i] = 0;
    const char* mp = strstr(url, "MaxPlayers="); int maxp = mp ? atoi(mp + 11) : 5;
    const char* mode = strstr(url, "TnSurvivalGame") ? "Escalation" : "Story";
    char a[200]; _snprintf_s(a, _TRUNCATE, "host %ld %d %s %s", g_hostBoundPort, maxp, mode, map[0] ? map : "-");
    StartRelay(a);
    return 0;
}
// guest: start the join relay, wait until it is in the lobby, then restart the game into 127.0.0.1:7790
static void ListRelayStop();
static DWORD WINAPI JoinSteamThread(LPVOID) {
    ListRelayStop();
    char a[96]; _snprintf_s(a, _TRUNCATE, "join %s %d", g_joinLobby[0] ? g_joinLobby : "auto", kRelayLocalPort);
    g_relayState[0] = 0;
    if (!StartRelay(a)) return 0;
    for (int i = 0; i < 120; i++) {
        Sleep(500); ReadRelayStatus();
        if (!strncmp(g_relayState, "joined", 6) || !strncmp(g_relayState, "connected", 9)) {
            // ?Name=: without it the guest's player name is the profile default ("Default" on the scoreboard)
            char name[64]; strncpy_s(name, g_relayPersona[0] ? g_relayPersona : g_playerName, _TRUNCATE);
            for (char* c = name; *c; c++) if (*c == '?' || *c == '=' || *c == ' ' || *c == '"' || *c == ',' || *c == '#' || (unsigned char)*c < 0x20) *c = '_';
            char url[128]; _snprintf_s(url, _TRUNCATE, "127.0.0.1:%d?Name=%s", kRelayLocalPort, name);
            RelaunchWithUrl(url);
            return 0;
        }
        if (strstr(g_relayState, "failed") || strstr(g_relayState, "not a ") || strstr(g_relayState, "missing")) return 0;
    }
    strcpy_s(g_relayState, "no friend hosting found within 60 s");
    RelayCommand("quit");
    return 0;
}
static void JoinViaSteam() { CreateThread(nullptr, 0, JoinSteamThread, nullptr, 0, nullptr); }

// Join page: a "FoCRelay list" process searches Steam for foc-coop lobbies (public ones, plus friends' lobbies) and
// writes lobbies.txt every 3 s: lobby|owner|name|mode|map|members|max|friend.
struct SteamLobby { char id[24]; char name[64]; char mode[24]; char map[96]; int members, maxp; bool isFriend; };
static std::vector<SteamLobby> g_steamLobbies;
static DWORD g_listStarted = 0, g_listRead = 0;
static void ListRelayStop() { wchar_t dir[MAX_PATH]; RelayDir(dir); wchar_t p[MAX_PATH]; _snwprintf_s(p, _TRUNCATE, L"%s\\list.cmd", dir); FILE* f = nullptr; _wfopen_s(&f, p, L"w"); if (f) { fputs("quit", f); fclose(f); } g_listStarted = 0; }
static const char* MapTitle(const char* map) {
    for (auto& m : kChapters) if (!_stricmp(m.map, map)) return m.title;
    for (auto& m : kEscalation) if (!_stricmp(m.map, map)) return m.title;
    return map;
}
static void ListRelayTick() {   // called while the Join page is open
    DWORD now = GetTickCount();
    if (!g_listStarted) {   // start the search once per visit of the page (stopped by Back / Join; it also ends 60 s after the game)
        wchar_t dir[MAX_PATH]; RelayDir(dir); wchar_t p[MAX_PATH]; _snwprintf_s(p, _TRUNCATE, L"%s\\list.cmd", dir); DeleteFileW(p);
        _snwprintf_s(p, _TRUNCATE, L"%s\\lobbies.txt", dir); DeleteFileW(p); g_steamLobbies.clear();
        {
            wchar_t cmd[MAX_PATH + 40]; _snwprintf_s(cmd, _TRUNCATE, L"\"%s\\FoCRelay.exe\" list", dir);
            STARTUPINFOW si = {sizeof(si)}; PROCESS_INFORMATION pi = {};
            if (CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, dir, &si, &pi)) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); Log("relay: lobby search started"); }
        }
        g_listStarted = now;
    }
    if (now - g_listRead < 1000) return;
    g_listRead = now;
    wchar_t dir[MAX_PATH]; RelayDir(dir); wchar_t p[MAX_PATH]; _snwprintf_s(p, _TRUNCATE, L"%s\\lobbies.txt", dir);
    FILE* f = nullptr; _wfopen_s(&f, p, L"r"); if (!f) return;
    std::vector<SteamLobby> v; char line[400];
    while (fgets(line, sizeof(line), f)) {
        char* fld[8] = {0}; int n = 0; char* c = line; fld[n++] = c;
        for (; *c && n < 8; c++) if (*c == '|') { *c = 0; fld[n++] = c + 1; }
        if (n < 8) continue;
        char* e = fld[7]; while (*e && *e != '\n' && *e != '\r') e++; *e = 0;
        SteamLobby l = {}; strncpy_s(l.id, fld[0], _TRUNCATE); strncpy_s(l.name, fld[2][0] ? fld[2] : "?", _TRUNCATE);
        strncpy_s(l.mode, fld[3], _TRUNCATE); strncpy_s(l.map, MapTitle(fld[4]), _TRUNCATE);
        l.members = atoi(fld[5]); l.maxp = atoi(fld[6]); l.isFriend = fld[7][0] == '1';
        v.push_back(l);
    }
    fclose(f);
    std::stable_sort(v.begin(), v.end(), [](const SteamLobby& a, const SteamLobby& b) { return a.isFriend && !b.isFriend; });
    g_steamLobbies.swap(v);
}

static void StartHosting() {
    const MapEntry& m = g_hostMode == 0 ? kChapters[g_hostMapStory] : kEscalation[g_hostMapEsc];
    char url[400], cmd[420];
    // Escalation needs the options the game's own lobby adds (TnOnlineGameSettingsSV): its GameRules give points and
    // money for kills (TnGameRules_CompetitiveScoring) and the TAB scoreboard; without them nothing is scored.
    // The lobby also adds TnGameRules_TrackKills ("A,B"), but any comma in a command-line URL sends the game back to
    // the front end (also with our GetCommandLine hook restoring %2C), and the in-game "open" bounces (B4): one rule.
    _snprintf_s(url, _TRUNCATE, "%s?Game=TransContent.%s%s?listen?MaxPlayers=%d", m.map,
                g_hostMode == 0 ? "TnCampaignGame" : "TnSurvivalGame", g_hostMode == 0 ? "" : kEscalationOptions, g_hostPlayers);
    _snprintf_s(cmd, _TRUNCATE, "open %s", url);
    strncpy_s(g_hostTitle, m.title, _TRUNCATE); strcpy_s(g_hostModeName, g_hostMode == 0 ? "Story" : "Escalation");
    g_hostMax = g_hostPlayers; g_hostBoundPort = 0; g_captureBind = 1; g_hosting = 1;
    g_menuOpen = 0;
    if (g_useSteam && !g_fastTravel) { RelaunchWithUrl(url, "-steamrelay"); return; }
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
    ImGui::Spacing();
    ImGui::TextDisabled("%s", g_hostMode == 0 ? "Story co-op: chapters 1, 2a, 2b, 3b, 4c, 5, 6, 8a, 8b, 9 tested." : "Escalation: wave survival, up to 5 players.");
    ImGui::TextWrapped("Your game appears in your friends' Join list through Steam (no IP addresses, no port forwarding). Steam must be running.");
    ImGui::Spacing();
    if (ImGui::Button("Start hosting", ImVec2(ImGui::GetFontSize() * 11.f, 0))) StartHosting();
    ImGui::SameLine(); ImGui::TextDisabled("(the game restarts into the session)");
    ImGui::SameLine();
    if (ImGui::Button("Back")) g_page = 0;
}

static void DrawJoinPage() {
    ImGui::TextUnformatted("JOIN A GAME");
    ImGui::Separator();
    ListRelayTick();
    ImGui::TextUnformatted("Games on Steam (friends first, refreshed every few seconds):");
    ImGui::BeginChild("lobbies", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 9.5f), true);
    if (ImGui::BeginTable("lt", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Host"); ImGui::TableSetupColumn("Mode"); ImGui::TableSetupColumn("Map / chapter");
        ImGui::TableSetupColumn("Players"); ImGui::TableSetupColumn("");
        ImGui::TableHeadersRow();
        for (int i = 0; i < (int)g_steamLobbies.size(); i++) {
            const SteamLobby& l = g_steamLobbies[i];
            ImGui::TableNextRow(); ImGui::TableNextColumn();
            if (l.isFriend) ImGui::TextColored(ImVec4(0.5f, 1.f, 0.5f, 1.f), "%s (friend)", l.name); else ImGui::TextUnformatted(l.name);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(l.mode);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(l.map);
            ImGui::TableNextColumn(); if (l.maxp) ImGui::Text("%d / %d", l.members, l.maxp); else ImGui::TextUnformatted("?");
            ImGui::TableNextColumn();
            char b[48]; _snprintf_s(b, _TRUNCATE, "Join##%s", l.id);
            if (ImGui::SmallButton(b)) { strncpy_s(g_joinLobby, l.id, _TRUNCATE); JoinViaSteam(); }
        }
        if (g_steamLobbies.empty()) { ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextDisabled(g_listStarted && GetTickCount() - g_listStarted > 8000 ? "no games found yet - ask your friend to host" : "searching..."); }
        ImGui::EndTable();
    }
    ImGui::EndChild();
    if (g_relayState[0] && !strcmp(g_relayRole, "join")) ImGui::TextDisabled("Steam: %s", g_relayState);
    ImGui::TextDisabled("Joining restarts the game once into the session.");
    ImGui::Spacing();
    if (ImGui::Button("Back")) { ListRelayStop(); g_page = 0; }
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
    if (g_relayState[0]) {
        ImGui::Text("Steam: %s", g_relayState);
        if (g_relayLobby) { ImGui::SameLine(); ImGui::TextDisabled("(lobby %llu, friends %d)", g_relayLobby, g_relayPeers); }
        // no Steam invite button: the invite dialog only opens for the process that owns the lobby (the relay, which has
        // no window), so it only showed the overlay; friends find the game in their Join list instead
        if (!strcmp(g_relayRole, "host")) ImGui::TextDisabled("Friends see your game in F10 > Join a game.");
    }
    if (ImGui::Button(g_fastTravel ? "Disconnect / stop" : "Leave to main menu (restarts the game)", bs)) { g_hosting = 0; g_menuOpen = 0; if (g_fastTravel) QueueCommand("disconnect"); else RelaunchWithUrl(""); }
    if (ImGui::Button("Settings", bs)) g_page = 4;
    Tooltip("Co-op options; most apply at once, also during a game");
    if (ImGui::Button("About / status", bs)) g_page = 3;
    ImGui::Spacing();
    ImGui::SetNextItemWidth(200); ImGui::InputText("Your name", g_playerName, sizeof(g_playerName));
}

// ---------------------------------------------------------------- settings page (F10 -> Settings)
// Writes foc_coop.ini next to the exe. The whole player set is written, because an ini that exists but lacks a key
// turns that feature off (LoadConfig only fills the co-op defaults when there is no ini at all).
static void SaveSettings() {
    wchar_t dir[MAX_PATH]; GetModuleFileNameW(nullptr, dir, MAX_PATH); wchar_t* sl = wcsrchr(dir, L'\\'); if (sl) *sl = 0;
    wchar_t ini[MAX_PATH]; _snwprintf_s(ini, _TRUNCATE, L"%s\\foc_coop.ini", dir);
    struct { const wchar_t* k; int v; } kv[] = {
        {L"tcpdriver", g_tcpDriver}, {L"forcechar", g_forceCharFlag}, {L"maxplayers", g_maxPlayers}, {L"tutguard", g_tutGuard},
        {L"ctrlshare", g_ctrlShare}, {L"hostfirst", g_hostFirst}, {L"rescue", g_rescue}, {L"viewfix", g_viewFix},
        {L"spawnshift", g_spawnShiftFlag}, {L"ctrldiag", g_ctrlDiag}, {L"ctrlsharedelay", g_ctrlShareDelay}, {L"steamhost", g_useSteam},
    };
    for (auto& e : kv) { wchar_t v[16]; _snwprintf_s(v, _TRUNCATE, L"%d", e.v); WritePrivateProfileStringW(L"foc_coop", e.k, v, ini); }
    if (g_suffix[0]) { wchar_t v[64]; MultiByteToWideChar(CP_ACP, 0, g_suffix, -1, v, 64); WritePrivateProfileStringW(L"foc_coop", L"instance", v, ini); }
    Log("settings: saved to %ls", ini);
}
static void DrawSettingsPage() {
    ImGui::TextUnformatted("SETTINGS");
    ImGui::Separator();
    bool changed = false;
    ImGui::TextDisabled("Apply at once (also while playing):");
    changed |= ImGui::Checkbox("Rescue a player who falls out of the level", &g_rescue);
    changed |= ImGui::Checkbox("Spawn the guest next to the host when the start spot is blocked", &g_spawnShiftFlag);
    changed |= ImGui::Checkbox("Guest camera follows its own character", &g_viewFix);
    changed |= ImGui::Checkbox("Share host objects with guests (fixes host crashes in story)", &g_ctrlShare);
    changed |= ImGui::Checkbox("Host first in the player list (story scripts target the host)", &g_hostFirst);
    changed |= ImGui::Checkbox("Detailed diagnostics in foc_coop.log", &g_ctrlDiag);
    ImGui::Spacing();
    ImGui::TextDisabled("Used when you host next time:");
    changed |= ImGui::Checkbox("Host through Steam by default", &g_useSteam);
    changed |= ImGui::SliderInt("Player limit", &g_maxPlayers, 2, 5);
    ImGui::Spacing();
    if (changed) SaveSettings();
    ImGui::TextDisabled("Saved to foc_coop.ini next to TFOC.exe.");
    if (ImGui::Button("Back")) g_page = 0;
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
    switch (g_page) { case 1: DrawHostPage(); break; case 2: DrawJoinPage(); break; case 3: DrawAboutPage(); break; case 4: DrawSettingsPage(); break; default: DrawMainPage(); }
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
    { static DWORD lastRelay = 0; if (GetTickCount() - lastRelay > 1000) { lastRelay = GetTickCount(); ReadRelayStatus(); } }
    { LONG r = InterlockedExchange(&g_menuReq, -1); if (r == 99) g_menuOpen = 0; else if (r >= 0) { g_menuOpen = 1; g_page = r; } }   // `menu` test command
    // toggle on F10 (edge)
    static bool f10prev = false; bool f10 = KeyDown(VK_F10);
    if (f10 && !f10prev) { g_menuOpen = !g_menuOpen; if (g_menuOpen) { g_page = 0; ClipCursor(nullptr); } }
    f10prev = f10;
    if (g_menuOpen && KeyDown(VK_ESCAPE)) { g_menuOpen = 0; }
    FeedInput(io);
    ImGui_ImplDX9_NewFrame();
    ImGui::NewFrame();
    if (g_menuOpen) {
        // dark backdrop over the whole game while the menu is open (the game gets no input then, see GetMsgProc)
        ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, 150));
        DrawMenu(io);
        // software cursor
        ImGui::GetForegroundDrawList()->AddTriangleFilled(io.MousePos, ImVec2(io.MousePos.x + 14 * io.FontGlobalScale, io.MousePos.y + 10 * io.FontGlobalScale), ImVec2(io.MousePos.x + 5 * io.FontGlobalScale, io.MousePos.y + 17 * io.FontGlobalScale), IM_COL32(255, 255, 255, 255));
    } else if (GetTickCount() - g_menuT0 < 25000 || g_hosting) {
        char t[160];
        if (g_hosting) _snprintf_s(t, _TRUNCATE, "co-op: hosting %s - %s   [F10 menu]", g_hostModeName, g_hostTitle);
        else strcpy_s(t, "Co-op menu: F10");
        ImGui::GetForegroundDrawList()->AddText(ImVec2(10, io.DisplaySize.y - 24 * (io.DisplaySize.y > 900 ? 1.5f : 1.f)), IM_COL32(255, 255, 255, 170), t);
    }
    if (g_waitingGuest > 0 && !g_menuOpen) {   // sync.inc: the host's story map is paused until a friend has loaded it
        char t[160]; _snprintf_s(t, _TRUNCATE, "Waiting for your friend to load the level... (%ld s)", g_waitingGuest);
        ImVec2 sz = ImGui::CalcTextSize(t);
        ImGui::GetForegroundDrawList()->AddText(ImVec2((io.DisplaySize.x - sz.x) / 2, io.DisplaySize.y * 0.45f), IM_COL32(255, 255, 255, 230), t);
    }
    ImGui::EndFrame();
    ImGui::Render();
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
}

// ---------------------------------------------------------------- in-game screenshot (`shot [file]` in foc_cmd.txt)
// Copies the back buffer of the next final frame to a 32-bit BMP: only the game's own image, no desktop capture.
static void SaveBackBuffer(IDirect3DDevice9* dev) {
    IDirect3DSurface9 *bb = nullptr, *res = nullptr, *sys = nullptr;
    D3DSURFACE_DESC d = {};
    HRESULT hr = dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb);
    if (SUCCEEDED(hr)) hr = bb->GetDesc(&d);
    IDirect3DSurface9* src = bb;
    if (SUCCEEDED(hr) && d.MultiSampleType != D3DMULTISAMPLE_NONE) {   // resolve MSAA first
        hr = dev->CreateRenderTarget(d.Width, d.Height, d.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &res, nullptr);
        if (SUCCEEDED(hr)) hr = dev->StretchRect(bb, nullptr, res, nullptr, D3DTEXF_NONE);
        src = res;
    }
    if (SUCCEEDED(hr)) hr = dev->CreateOffscreenPlainSurface(d.Width, d.Height, d.Format, D3DPOOL_SYSTEMMEM, &sys, nullptr);
    if (SUCCEEDED(hr)) hr = dev->GetRenderTargetData(src, sys);
    D3DLOCKED_RECT lr = {};
    if (SUCCEEDED(hr) && (d.Format == D3DFMT_X8R8G8B8 || d.Format == D3DFMT_A8R8G8B8)) hr = sys->LockRect(&lr, nullptr, D3DLOCK_READONLY);
    else if (SUCCEEDED(hr)) hr = E_NOTIMPL;
    if (SUCCEEDED(hr)) {
        FILE* f = nullptr; _wfopen_s(&f, g_shotPath, L"wb");
        if (f) {
            BITMAPFILEHEADER fh = {}; BITMAPINFOHEADER ih = {};
            DWORD row = d.Width * 4, img = row * d.Height;
            fh.bfType = 0x4D42; fh.bfOffBits = sizeof(fh) + sizeof(ih); fh.bfSize = fh.bfOffBits + img;
            ih.biSize = sizeof(ih); ih.biWidth = d.Width; ih.biHeight = -(LONG)d.Height; ih.biPlanes = 1; ih.biBitCount = 32; ih.biCompression = BI_RGB; ih.biSizeImage = img;
            fwrite(&fh, sizeof(fh), 1, f); fwrite(&ih, sizeof(ih), 1, f);
            for (UINT y = 0; y < d.Height; y++) fwrite((BYTE*)lr.pBits + y * lr.Pitch, 1, row, f);
            fclose(f);
        } else hr = E_ACCESSDENIED;
        sys->UnlockRect();
    }
    Log("shot: %ls %ux%u format %d -> hr %08lx", g_shotPath, d.Width, d.Height, (int)d.Format, hr);
    if (sys) sys->Release();
    if (res) res->Release();
    if (bb) bb->Release();
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
    if (isFinal && InterlockedExchange(&g_shotReq, 0)) { __try { SaveBackBuffer(dev); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("shot: exception"); } }
    if (isFinal) { __try { RenderOverlay(dev); } __except (EXCEPTION_EXECUTE_HANDLER) { static int n = 0; if (n++ < 3) Log("menu: exception in overlay render"); } }
    if (isFinal && InterlockedExchange(&g_shotUiReq, 0)) { __try { SaveBackBuffer(dev); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("shot: exception"); } }
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
        // B7: without a usable display (remote desktop window minimized, session locked) the game gets no device and
        // its render thread crashes at ghidra 0x64d247. Log what D3D9 sees so that case is recognisable in the log.
        UINT n = d9->GetAdapterCount();
        D3DADAPTER_IDENTIFIER9 id = {}; D3DDISPLAYMODE dm = {};
        HRESULT hi = n ? d9->GetAdapterIdentifier(0, 0, &id) : E_FAIL;
        HRESULT hm = n ? d9->GetAdapterDisplayMode(0, &dm) : E_FAIL;
        Log("menu: D3D9 adapters=%u [0] '%s' mode=%ux%u (hr %08lx/%08lx)", n, SUCCEEDED(hi) ? id.Description : "?", dm.Width, dm.Height, hi, hm);
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

// Injected input for tests (`input key <scancode hex> <ms>`, `input mouse <dx> <dy> <ms>` in foc_cmd.txt): keys read
// as held and the mouse moves inside the game's own DirectInput reads, so tests need no focus and no real input.
static void* g_kbDev = nullptr; static void* g_mouseDev = nullptr;   // from CreateDevice: GUID_SysKeyboard / GUID_SysMouse
static volatile DWORD g_injKeyUntil[256];
static volatile LONG g_injKeyEdge[256];      // 1 = key-down event pending, 2 = key-up event pending (buffered reads)
static volatile LONG g_injMouseDx = 0, g_injMouseDy = 0;      // motion still to deliver
static volatile DWORD g_injMouseUntil = 0, g_injMouseLast = 0;
static bool InjectActive() {
    DWORD now = GetTickCount();
    for (int k = 0; k < 256; k++) if ((LONG)(g_injKeyUntil[k] - now) > 0 || g_injKeyEdge[k]) return true;
    return g_injMouseDx || g_injMouseDy;
}
static void InjectMouseStep(LONG* dx, LONG* dy) {   // the part of the remaining motion that is due by now
    DWORD now = GetTickCount();
    LONG rx = g_injMouseDx, ry = g_injMouseDy;
    if (!rx && !ry) { *dx = *dy = 0; return; }
    LONG left = (LONG)(g_injMouseUntil - now), dt = (LONG)(now - g_injMouseLast);
    g_injMouseLast = now;
    if (left <= 0 || dt >= left) { *dx = rx; *dy = ry; }
    else { *dx = (LONG)((long long)rx * dt / (dt + left)); *dy = (LONG)((long long)ry * dt / (dt + left)); }
    InterlockedExchangeAdd(&g_injMouseDx, -*dx); InterlockedExchangeAdd(&g_injMouseDy, -*dy);
}
static HRESULT WINAPI h_GetDeviceState(void* self, DWORD cb, void* data) {
    DiHook* h = FindDiHook(self);
    HRESULT r = ((HRESULT(WINAPI*)(void*, DWORD, void*))h->getState)(self, cb, data);
    { static void* seen[4]; static int ns = 0; bool k = false; for (int i = 0; i < ns; i++) if (seen[i] == self) k = true; if (!k && ns < 4) { seen[ns++] = self; Log("input: device %p (%s) GetDeviceState cb=%lu hr=%08lx", self, self == g_kbDev ? "keyboard" : self == g_mouseDev ? "mouse" : "?", cb, r); } }
    if (g_menuOpen && SUCCEEDED(r) && data) memset(data, 0, cb);
    if (data && !g_menuOpen && InjectActive()) {
        if (FAILED(r)) { memset(data, 0, cb); r = S_OK; }   // not acquired without focus: report the injected state anyway
        if (cb == 256) {
            DWORD now = GetTickCount();
            for (int k = 0; k < 256; k++) if ((LONG)(g_injKeyUntil[k] - now) > 0) ((BYTE*)data)[k] = 0x80;
        } else if (cb == 16 || cb == 20) {   // DIMOUSESTATE / DIMOUSESTATE2: lX, lY first
            LONG dx, dy; InjectMouseStep(&dx, &dy); ((LONG*)data)[0] += dx; ((LONG*)data)[1] += dy;
        }
    }
    return r;
}
static HRESULT WINAPI h_GetDeviceData(void* self, DWORD cb, void* data, DWORD* inout, DWORD flags) {
    DiHook* h = FindDiHook(self);
    DWORD room = inout ? *inout : 0;
    HRESULT r = ((HRESULT(WINAPI*)(void*, DWORD, void*, DWORD*, DWORD))h->getData)(self, cb, data, inout, flags);
    { static void* seen[4]; static int ns = 0; bool k = false; for (int i = 0; i < ns; i++) if (seen[i] == self) k = true; if (!k && ns < 4) { seen[ns++] = self; Log("input: device %p (%s) GetDeviceData cbObject=%lu room=%lu hr=%08lx", self, self == g_kbDev ? "keyboard" : self == g_mouseDev ? "mouse" : "?", cb, room, r); } }
    if (g_menuOpen && SUCCEEDED(r) && inout) *inout = 0;
    if (!g_menuOpen && data && inout && cb >= 16 && !(flags & 1 /*DIGDD_PEEK*/) && InjectActive()) {
        if (FAILED(r)) { *inout = 0; r = S_OK; }
        DWORD n = *inout;
        auto add = [&](DWORD ofs, DWORD val) { if (n < room) { BYTE* e = (BYTE*)data + n * cb; memset(e, 0, cb); ((DWORD*)e)[0] = ofs; ((DWORD*)e)[1] = val; ((DWORD*)e)[2] = GetTickCount(); n++; } };
        if (self == g_kbDev) {
            DWORD now = GetTickCount();
            for (int k = 0; k < 256; k++) {
                if (g_injKeyEdge[k] == 1) { add(k, 0x80); g_injKeyEdge[k] = 0; }
                else if (!g_injKeyEdge[k] && g_injKeyUntil[k] && (LONG)(g_injKeyUntil[k] - now) <= 0) { add(k, 0); g_injKeyUntil[k] = 0; }
            }
        } else if (self == g_mouseDev) {
            LONG dx, dy; InjectMouseStep(&dx, &dy);
            if (dx) add(0 /*DIMOFS_X*/, (DWORD)dx);
            if (dy) add(4 /*DIMOFS_Y*/, (DWORD)dy);
        }
        *inout = n;
    }
    return r;
}
// `input key <sc hex> <ms>` / `input mouse <dx> <dy> <ms>`
static void InjectCommand(const char* args) {
    char kind[16] = ""; unsigned a = 0; int b = 0, c = 0;
    if (sscanf_s(args, "%15s", kind, (unsigned)sizeof(kind)) != 1) return;
    if (!strcmp(kind, "key") && sscanf_s(args, "%*s %x %d", &a, &b) == 2 && a < 256) {
        g_injKeyUntil[a] = GetTickCount() + (DWORD)b; g_injKeyEdge[a] = 1;
        // The game keeps only a DirectInput mouse; the keyboard comes through window messages, so post those too.
        UINT vk = MapVirtualKeyW(a, MAPVK_VSC_TO_VK);
        struct KeyUp { HWND w; UINT vk, sc; DWORD ms; };
        KeyUp* ku = new KeyUp{g_gameWnd, vk, a, (DWORD)b};
        if (g_gameWnd) PostMessageW(g_gameWnd, WM_KEYDOWN, vk, 1 | (a << 16));
        CreateThread(nullptr, 0, [](LPVOID p) -> DWORD {
            KeyUp* k = (KeyUp*)p; DWORD end = GetTickCount() + k->ms;
            while ((LONG)(end - GetTickCount()) > 0) { Sleep(100); if (k->w && (LONG)(end - GetTickCount()) > 0) PostMessageW(k->w, WM_KEYDOWN, k->vk, 1 | (k->sc << 16) | (1u << 30)); }
            if (k->w) PostMessageW(k->w, WM_KEYUP, k->vk, 1 | (k->sc << 16) | (1u << 30) | (1u << 31));
            delete k; return 0; }, ku, 0, nullptr);
        Log("input: key 0x%02x (vk 0x%02x) held for %d ms", a, vk, b);
    } else if (!strcmp(kind, "mouse") && sscanf_s(args, "%*s %d %d %d", &b, &c, (int*)&a) == 3) {
        g_injMouseLast = GetTickCount(); g_injMouseUntil = g_injMouseLast + a; g_injMouseDx = b; g_injMouseDy = c;
        Log("input: mouse %d,%d over %u ms", b, c, a);
    } else Log("input: bad command '%s'", args);
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
    if (SUCCEEDED(r) && out && *out) {
        HookDiDevice(*out);
        Log("input: CreateDevice guid %08lx-%04x -> %p", g.Data1, g.Data2, *out);
        if (g.Data1 == 0x6F1D2B61 && g.Data2 == 0xD5A0) g_kbDev = *out;          // GUID_SysKeyboard
        else if (g.Data1 == 0x6F1D2B60 && g.Data2 == 0xD5A0) g_mouseDev = *out;  // GUID_SysMouse
    }
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
    if (strstr(cl, "-steamrelay")) { Log("menu: hosting through Steam (relay)"); CreateThread(nullptr, 0, HostRelayThread, nullptr, 0, nullptr); }
}

static void OverlayStart() {
    {   // Settings page value: host through Steam by default
        wchar_t dir[MAX_PATH]; GetModuleFileNameW(nullptr, dir, MAX_PATH); wchar_t* sl = wcsrchr(dir, L'\\'); if (sl) *sl = 0;
        wchar_t ini[MAX_PATH]; _snwprintf_s(ini, _TRUNCATE, L"%s\\foc_coop.ini", dir);
        g_useSteam = GetPrivateProfileIntW(L"foc_coop", L"steamhost", 1, ini) != 0;
    }
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
    CreateThread(nullptr, 0, TickThread, nullptr, 0, nullptr);
    CreateThread(nullptr, 0, BeaconThread, nullptr, 0, nullptr);
}
