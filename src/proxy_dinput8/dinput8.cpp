// dinput8.dll proxy for Transformers: Fall of Cybertron (32-bit, UE3 / High Moon).
// Phase 1 goal: see what the game does (log file next to the exe) and let several copies run on one PC.
//
// What it does:
//  - forwards DirectInput8Create to the real system dinput8.dll
//  - writes foc_coop.log next to TFOC.exe
//  - hooks (by patching the exe's import table) OutputDebugStringA/W, CreateMutexA/W, sendto, recvfrom
//  - CreateMutex: if the mutex name starts with "UnrealEngine3_" and a suffix is configured
//    (foc_coop.ini: instance=<text>, or env FOC_INSTANCE) the suffix is appended, so more than
//    one copy can run
//  - logs a vectored exception (crash address and module)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cwchar>
#include <cstdlib>
#include <cmath>
#include <initializer_list>

static HMODULE g_real = nullptr;
static FILE* g_log = nullptr;
static CRITICAL_SECTION g_cs;
static char g_suffix[64] = {0};
static DWORD g_t0 = 0;
static LONG g_netLines = 0;
static bool g_tcpDriver = false;
static bool g_dump = false;
static bool g_scan = false;
static bool g_trace = false;
static bool g_patchCap = false;
static bool g_cfg = false;
static bool g_natives = false;
static bool g_lpFlag = false;
struct PokeDef { char cls[48]; unsigned off, val, mask; unsigned id; bool used; };
static PokeDef g_pokes[8];
static int g_traceLenFlag = 0;
static int g_maxPlayers = 0;
static bool g_forceCharFlag = false;
static bool g_skipTut = false;
static bool g_ctrlShare = false;
static bool g_tutGuard = false;
static bool g_hostFirst = false;
static volatile LONG g_playerCount = 0;   // player controllers seen by the host (shown in the lobby list)
static bool g_holdSpawn = false;
static bool g_rescue = false;
static int g_spawnAfter = 0;
static volatile LONG g_releaseSpawn = 0;
static bool g_ctrlDiag = false;
static char g_ctrlSkipMaskBuf[64] = {0};
static const char* g_ctrlSkipMask = nullptr;
static char g_findCls[64] = "";
static bool g_engInfo = false;
static int g_snap1 = 0, g_snap2 = 0;
static bool g_charFixFlag = false;
static char g_instClass[64] = "";

static void Log(const char* fmt, ...) {
    if (!g_log) return;
    EnterCriticalSection(&g_cs);
    fprintf(g_log, "[%7lu ms t%04lu] ", GetTickCount() - g_t0, GetCurrentThreadId());
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
    LeaveCriticalSection(&g_cs);
}

// ---------------------------------------------------------------- hooks
typedef void(WINAPI* PFN_ODSA)(LPCSTR);
typedef void(WINAPI* PFN_ODSW)(LPCWSTR);
typedef HANDLE(WINAPI* PFN_CMA)(LPSECURITY_ATTRIBUTES, BOOL, LPCSTR);
typedef HANDLE(WINAPI* PFN_CMW)(LPSECURITY_ATTRIBUTES, BOOL, LPCWSTR);
typedef int(WSAAPI* PFN_SENDTO)(SOCKET, const char*, int, int, const sockaddr*, int);
typedef int(WSAAPI* PFN_RECVFROM)(SOCKET, char*, int, int, sockaddr*, int*);

static PFN_ODSA o_ODSA;
static PFN_ODSW o_ODSW;
static PFN_CMA o_CMA;
static PFN_CMW o_CMW;
static PFN_SENDTO o_sendto;
static PFN_RECVFROM o_recvfrom;
typedef int(WSAAPI* PFN_BIND)(SOCKET, const sockaddr*, int);
typedef SOCKET(WSAAPI* PFN_SOCKET)(int, int, int);
typedef int(WSAAPI* PFN_CLOSE)(SOCKET);
typedef int(WSAAPI* PFN_RECV)(SOCKET, char*, int, int);
typedef int(WSAAPI* PFN_SEND)(SOCKET, const char*, int, int);
static PFN_BIND o_bind;
static PFN_SOCKET o_socket;
static PFN_CLOSE o_close;
static PFN_RECV o_recv;
static PFN_SEND o_send;
typedef int(WSAAPI* PFN_CONNECT)(SOCKET, const sockaddr*, int);
static PFN_CONNECT o_connect;
static LONG g_recvfromCalls = 0, g_recvfromErr = 0, g_recvfromOk = 0;

static void trimEol(char* s) {
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r')) s[--n] = 0;
}

static void WINAPI h_ODSA(LPCSTR s) {
    if (s) {
        char buf[1024];
        strncpy_s(buf, s, _TRUNCATE);
        trimEol(buf);
        Log("DBG: %s", buf);
    }
    if (o_ODSA) o_ODSA(s);
}

static void WINAPI h_ODSW(LPCWSTR s) {
    if (s) {
        char buf[1024];
        WideCharToMultiByte(CP_UTF8, 0, s, -1, buf, sizeof(buf), nullptr, nullptr);
        buf[sizeof(buf) - 1] = 0;
        trimEol(buf);
        Log("DBGW: %s", buf);
    }
    if (o_ODSW) o_ODSW(s);
}

static HANDLE WINAPI h_CMA(LPSECURITY_ATTRIBUTES a, BOOL b, LPCSTR name) {
    if (name && g_suffix[0] && strncmp(name, "UnrealEngine3_", 14) == 0) {
        char n2[256];
        _snprintf_s(n2, _TRUNCATE, "%s%s", name, g_suffix);
        Log("CreateMutexA '%s' -> '%s'", name, n2);
        return o_CMA(a, b, n2);
    }
    Log("CreateMutexA '%s'", name ? name : "(null)");
    return o_CMA(a, b, name);
}

static HANDLE WINAPI h_CMW(LPSECURITY_ATTRIBUTES a, BOOL b, LPCWSTR name) {
    if (name && g_suffix[0] && wcsncmp(name, L"UnrealEngine3_", 14) == 0) {
        wchar_t n2[256];
        _snwprintf_s(n2, _TRUNCATE, L"%s%S", name, g_suffix);
        Log("CreateMutexW '%ls' -> '%ls'", name, n2);
        return o_CMW(a, b, n2);
    }
    char nb[256] = "(null)";
    if (name) WideCharToMultiByte(CP_UTF8, 0, name, -1, nb, sizeof(nb), nullptr, nullptr);
    Log("CreateMutexW '%s'", nb);
    return o_CMW(a, b, name);
}


// ---------------------------------------------------------------- caller discovery for a sent packet
#define g_traceLen g_traceLenFlag
static void LogCallers(const char* tag) {
    BYTE* base = (BYTE*)GetModuleHandleW(nullptr);
    BYTE* textLo = base + 0x1000; BYTE* textHi = base + 0x1000 + 0xf5fc71;
    DWORD esp_val = 0;
    __asm { mov esp_val, esp }
    DWORD* sp = (DWORD*)esp_val;
    char line[3000]; size_t k = _snprintf_s(line, _TRUNCATE, "callers: %s:", tag);
    int n = 0;
    __try {
        for (int i = 0; i < 2048 && n < 60; i++) {
            BYTE* v = (BYTE*)sp[i];
            if (v < textLo || v >= textHi) continue;
            // plausible return address: preceded by a CALL (E8 rel32 or FF /2)
            bool call = (v[-5] == 0xE8) || (v[-2] == 0xFF && (v[-1] & 0x38) == 0x10) || (v[-3] == 0xFF && (v[-2] & 0x38) == 0x10) || (v[-6] == 0xFF && (v[-5] & 0x38) == 0x10);
            if (!call) continue;
            if (k + 20 < sizeof(line)) k += _snprintf_s(line + k, sizeof(line) - k, _TRUNCATE, " %lx", (unsigned long)(0x470000 + (v - base)));
            n++;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    Log("%s", line);
}

static void hexdump(const char* p, int n, char* out, size_t outsz) {
    size_t o = 0;
    for (int i = 0; i < n && o + 3 < outsz; i++) o += _snprintf_s(out + o, outsz - o, _TRUNCATE, "%02x", (unsigned char)p[i]);
    out[o] = 0;
}

static int WSAAPI h_sendto(SOCKET s, const char* buf, int len, int flags, const sockaddr* to, int tolen) {
    int r = o_sendto(s, buf, len, flags, to, tolen);
    if (g_traceLen && len == g_traceLen) { static volatile LONG nTr = 0; if (InterlockedIncrement(&nTr) <= 3) LogCallers("sendto len match"); }
    if (InterlockedIncrement(&g_netLines) < 4000) {
        char hx[200];
        hexdump(buf, len < 80 ? len : 80, hx, sizeof(hx));
        unsigned port = 0;
        char ip[32] = "?";
        if (to && to->sa_family == AF_INET) {
            const sockaddr_in* in = (const sockaddr_in*)to;
            port = ntohs(in->sin_port);
            _snprintf_s(ip, _TRUNCATE, "%u.%u.%u.%u", in->sin_addr.S_un.S_un_b.s_b1, in->sin_addr.S_un.S_un_b.s_b2, in->sin_addr.S_un.S_un_b.s_b3, in->sin_addr.S_un.S_un_b.s_b4);
        }
        Log("NET sendto %s:%u len=%d ret=%d %s", ip, port, len, r, hx);
    }
    return r;
}

static int WSAAPI h_recvfrom(SOCKET s, char* buf, int len, int flags, sockaddr* from, int* fromlen) {
    int r = o_recvfrom(s, buf, len, flags, from, fromlen);
    InterlockedIncrement(&g_recvfromCalls);
    if (r < 0) {
        int e = WSAGetLastError();
        if (e != WSAEWOULDBLOCK && InterlockedIncrement(&g_recvfromErr) < 200) Log("NET recvfrom sock=%u error %d", (unsigned)s, e);
        WSASetLastError(e);
    } else {
        InterlockedIncrement(&g_recvfromOk);
    }
    if (r > 0 && InterlockedIncrement(&g_netLines) < 4000) {
        char hx[200];
        hexdump(buf, r < 80 ? r : 80, hx, sizeof(hx));
        unsigned port = 0;
        char ip[32] = "?";
        if (from && from->sa_family == AF_INET) {
            const sockaddr_in* in = (const sockaddr_in*)from;
            port = ntohs(in->sin_port);
            _snprintf_s(ip, _TRUNCATE, "%u.%u.%u.%u", in->sin_addr.S_un.S_un_b.s_b1, in->sin_addr.S_un.S_un_b.s_b2, in->sin_addr.S_un.S_un_b.s_b3, in->sin_addr.S_un.S_un_b.s_b4);
        }
        Log("NET recvfrom %s:%u len=%d %s", ip, port, r, hx);
    }
    return r;
}

// Replace every pointer in the main module's import address table that equals `orig` with `hook`.
static int PatchIat(void* orig, void* hook) {
    if (!orig) return 0;
    BYTE* base = (BYTE*)GetModuleHandleW(nullptr);
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    IMAGE_DATA_DIRECTORY dd = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dd.VirtualAddress) return 0;
    int n = 0;
    IMAGE_IMPORT_DESCRIPTOR* d = (IMAGE_IMPORT_DESCRIPTOR*)(base + dd.VirtualAddress);
    for (; d->Name; d++) {
        void** thunk = (void**)(base + d->FirstThunk);
        for (; *thunk; thunk++) {
            if (*thunk == orig) {
                DWORD old;
                if (VirtualProtect(thunk, sizeof(void*), PAGE_READWRITE, &old)) {
                    *thunk = hook;
                    VirtualProtect(thunk, sizeof(void*), old, &old);
                    n++;
                }
            }
        }
    }
    return n;
}

static void* Resolve(const char* dll, const char* fn) {
    HMODULE m = GetModuleHandleA(dll);
    if (!m) m = LoadLibraryA(dll);
    return m ? (void*)GetProcAddress(m, fn) : nullptr;
}


static void fmtAddr(const sockaddr* a, char* out, size_t n) {
    if (a && a->sa_family == AF_INET) {
        const sockaddr_in* in = (const sockaddr_in*)a;
        _snprintf_s(out, n, _TRUNCATE, "%u.%u.%u.%u:%u", in->sin_addr.S_un.S_un_b.s_b1, in->sin_addr.S_un.S_un_b.s_b2, in->sin_addr.S_un.S_un_b.s_b3, in->sin_addr.S_un.S_un_b.s_b4, ntohs(in->sin_port));
    } else {
        _snprintf_s(out, n, _TRUNCATE, "family=%d", a ? a->sa_family : -1);
    }
}

static volatile LONG g_captureBind = 0, g_hostBoundPort = 0;   // port the game really listens on once hosting started
static int WSAAPI h_bind(SOCKET s, const sockaddr* a, int len) {
    int r = o_bind(s, a, len);
    int e = WSAGetLastError();
    if (r == 0 && g_captureBind && a && a->sa_family == AF_INET) {
        int port = ntohs(((const sockaddr_in*)a)->sin_port);
        if (port >= 7777 && port <= 7800) g_hostBoundPort = port;
    }
    char ad[64];
    fmtAddr(a, ad, sizeof(ad));
    Log("NET bind sock=%u %s ret=%d err=%d", (unsigned)s, ad, r, r ? e : 0);
    WSASetLastError(e);
    return r;
}

static SOCKET WSAAPI h_socket(int af, int type, int proto) {
    SOCKET s = o_socket(af, type, proto);
    int e = WSAGetLastError();
    Log("NET socket af=%d type=%d proto=%d -> %u", af, type, proto, (unsigned)s);
    WSASetLastError(e);
    return s;
}

static int WSAAPI h_closesocket(SOCKET s) {
    Log("NET closesocket %u", (unsigned)s);
    return o_close(s);
}

static int WSAAPI h_recv(SOCKET s, char* buf, int len, int flags) {
    int r = o_recv(s, buf, len, flags);
    if (r > 0 && InterlockedIncrement(&g_netLines) < 4000) {
        char hx[40];
        hexdump(buf, r < 16 ? r : 16, hx, sizeof(hx));
        Log("NET recv sock=%u len=%d %s", (unsigned)s, r, hx);
    }
    return r;
}

static bool g_blockDw = true;   // Demonware (port 3074) is dead; failing its TCP connects at once keeps the online layer calm
static int WSAAPI h_connect(SOCKET s, const sockaddr* a, int len) {
    if (g_blockDw && a && a->sa_family == AF_INET && ntohs(((const sockaddr_in*)a)->sin_port) == 3074) {
        char ad0[64]; fmtAddr(a, ad0, sizeof(ad0));
        Log("NET connect sock=%u %s blocked (Demonware)", (unsigned)s, ad0);
        WSASetLastError(WSAENETUNREACH);
        return SOCKET_ERROR;
    }
    int r = o_connect(s, a, len);
    int e = WSAGetLastError();
    char ad[64]; fmtAddr(a, ad, sizeof(ad));
    Log("NET connect sock=%u %s ret=%d err=%d", (unsigned)s, ad, r, r ? e : 0);
    WSASetLastError(e);
    return r;
}

static int WSAAPI h_send(SOCKET s, const char* buf, int len, int flags) {
    int r = o_send(s, buf, len, flags);
    if (InterlockedIncrement(&g_netLines) < 4000) {
        char hx[40];
        hexdump(buf, len < 16 ? len : 16, hx, sizeof(hx));
        Log("NET send sock=%u len=%d ret=%d %s", (unsigned)s, len, r, hx);
    }
    return r;
}

static DWORD WINAPI StatsThread(LPVOID) {
    for (;;) {
        Sleep(5000);
        Log("STAT recvfrom calls=%ld ok=%ld err=%ld", g_recvfromCalls, g_recvfromOk, g_recvfromErr);
    }
    return 0;
}

// Patch the import table entries for a function exported by both wsock32 and ws2_32.
static int PatchBoth(const char* fn, void** origOut, void* hook) {
    void* w2 = Resolve("ws2_32.dll", fn);
    void* w1 = Resolve("wsock32.dll", fn);
    *origOut = w2 ? w2 : w1;
    int n = 0;
    if (w2) n += PatchIat(w2, hook);
    if (w1 && w1 != w2) n += PatchIat(w1, hook);
    return n;
}


// Replace a wide string literal in the main module's data sections (same length only).
static int PatchWideString(const wchar_t* from, const wchar_t* to) {
    size_t n = wcslen(from);
    if (wcslen(to) != n) return -1;
    BYTE* base = (BYTE*)GetModuleHandleW(nullptr);
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    int count = 0;
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
        if (sec->Characteristics & IMAGE_SCN_MEM_EXECUTE) continue;
        BYTE* p = base + sec->VirtualAddress;
        size_t sz = sec->Misc.VirtualSize;
        size_t bytes = n * sizeof(wchar_t);
        for (size_t o = 0; o + bytes + 2 <= sz; o += 2) {
            if (memcmp(p + o, from, bytes) == 0 && *(wchar_t*)(p + o + bytes) == 0) {
                DWORD old;
                if (VirtualProtect(p + o, bytes, PAGE_READWRITE, &old)) {
                    memcpy(p + o, to, bytes);
                    VirtualProtect(p + o, bytes, old, &old);
                    count++;
                }
            }
        }
    }
    return count;
}


// The game reads [Engine.Engine] NetworkDevice from its obfuscated Coalesced.ini into heap strings.
// Replace "IpDrv.TDwNetDriver" (Demonware, needs the dead Demonware servers) with "IpDrv.TcpNetDriver"
// (plain UE3 UDP) in every private read/write heap region. Same length, so it is done in place.
static int PatchHeapWide(const wchar_t* from, const wchar_t* to) {
    size_t bytes = wcslen(from) * sizeof(wchar_t);
    if (wcslen(to) * sizeof(wchar_t) != bytes) return -1;
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    BYTE* p = (BYTE*)si.lpMinimumApplicationAddress;
    BYTE* end = (BYTE*)si.lpMaximumApplicationAddress;
    int count = 0;
    MEMORY_BASIC_INFORMATION mbi;
    while (p < end && VirtualQuery(p, &mbi, sizeof(mbi))) {
        BYTE* rb = (BYTE*)mbi.BaseAddress;
        size_t rs = mbi.RegionSize;
        if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE && mbi.Protect == PAGE_READWRITE && rs >= bytes + 2) {
            __try {
                for (size_t o = 0; o + bytes + 2 <= rs; o += 2) {
                    if (rb[o] == (BYTE)from[0] && memcmp(rb + o, from, bytes) == 0 && *(wchar_t*)(rb + o + bytes) == 0) {
                        memcpy(rb + o, to, bytes);
                        count++;
                    }
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
        p = rb + rs;
    }
    return count;
}

static DWORD WINAPI DelayedNetDriverPatch(LPVOID) {
    int total = 0;
    for (int i = 0; i < 240; i++) {
        Sleep(500);
        int n = PatchHeapWide(L"IpDrv.TDwNetDriver", L"IpDrv.TcpNetDriver");
        if (n > 0) {
            total += n;
            Log("heap: NetworkDevice patched to IpDrv.TcpNetDriver in %d place(s) (total %d)", n, total);
        }
    }
    return 0;
}


// Dump the running exe's memory image (after the Steam DRM stub decrypted the code) for Ghidra.
static DWORD WINAPI DumpThread(LPVOID) {
    Sleep(12000);
    BYTE* base = (BYTE*)GetModuleHandleW(nullptr);
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    DWORD size = nt->OptionalHeader.SizeOfImage;
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    wchar_t* sl = wcsrchr(exe, L'\\');
    if (sl) *sl = 0;
    wchar_t path[MAX_PATH];
    _snwprintf_s(path, _TRUNCATE, L"%s\\foc_dump.bin", exe);
    FILE* f = nullptr;
    _wfopen_s(&f, path, L"wb");
    if (!f) { Log("dump: cannot open output"); return 0; }
    static BYTE zero[4096];
    DWORD done = 0, missing = 0;
    for (DWORD o = 0; o < size; o += 4096) {
        MEMORY_BASIC_INFORMATION mbi;
        bool ok = VirtualQuery(base + o, &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD));
        if (ok) { fwrite(base + o, 1, 4096, f); done += 4096; }
        else { fwrite(zero, 1, 4096, f); missing += 4096; }
    }
    fclose(f);
    Log("dump: base=%p size=0x%lx written=0x%lx unreadable=0x%lx entry_rva=0x%lx imagebase_in_header=0x%lx", base, size, done, missing, nt->OptionalHeader.AddressOfEntryPoint, nt->OptionalHeader.ImageBase);
    return 0;
}


// ---------------------------------------------------------------- UE3 runtime discovery (GNames / GObjObjects)
static bool Rd(const void* p, size_t n) {
    if (!p) return false;
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    BYTE* end = (BYTE*)mbi.BaseAddress + mbi.RegionSize;
    return (BYTE*)p + n <= end;
}

struct TArr { void** Data; int Num; int Max; };

static int g_scanDelayMs = 20000;
static DWORD WINAPI ScanThread(LPVOID) {
    Sleep(g_scanDelayMs);
    BYTE* base = (BYTE*)GetModuleHandleW(nullptr);
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    BYTE* dataStart = nullptr; size_t dataSize = 0;
    BYTE* rdataStart = nullptr; size_t rdataSize = 0;
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        if (memcmp(sec[i].Name, ".data", 5) == 0) { dataStart = base + sec[i].VirtualAddress; dataSize = sec[i].Misc.VirtualSize; }
        if (memcmp(sec[i].Name, ".rdata", 6) == 0) { rdataStart = base + sec[i].VirtualAddress; rdataSize = sec[i].Misc.VirtualSize; }
    }
    Log("scan: .data %p+0x%zx .rdata %p+0x%zx", dataStart, dataSize, rdataStart, rdataSize);
    // 1) GNames candidates: TArray whose element 0 is an FNameEntry containing "None"
    TArr* gnames = nullptr; int nameOff = -1;
    for (size_t o = 0; o + sizeof(TArr) <= dataSize; o += 4) {
        TArr* t = (TArr*)(dataStart + o);
        if (t->Num < 20000 || t->Num > 3000000 || t->Max < t->Num || t->Max > 6000000) continue;
        if (!Rd(t->Data, 16)) continue;
        BYTE* e0 = (BYTE*)t->Data[0];
        if (!Rd(e0, 0x20)) continue;
        for (int so = 4; so <= 0x1c; so += 4) {
            if (memcmp(e0 + so, "None", 5) == 0) {
                void* e1 = t->Data[1];
                if (!Rd(e1, 0x20)) continue;
                Log("scan: GNames candidate at %p (rva 0x%lx) Num=%d Max=%d Data=%p string offset 0x%x, entry1 = '%.20s'", t, (unsigned long)((BYTE*)t - base), t->Num, t->Max, t->Data, so, (char*)e1 + so);
                if (!gnames) { gnames = t; nameOff = so; }
            }
        }
    }
    if (!gnames) { Log("scan: GNames not found"); return 0; }
    auto nameOf = [&](int idx, char* out, size_t n) -> bool {
        out[0] = 0;
        if (idx < 0 || idx >= gnames->Num) return false;
        BYTE* e = (BYTE*)gnames->Data[idx];
        if (!Rd(e, nameOff + 4)) return false;
        const char* s = (const char*)e + nameOff;
        size_t k = 0;
        while (k + 1 < n && Rd(s + k, 1) && s[k]) { out[k] = s[k]; k++; }
        out[k] = 0;
        return k > 0;
    };
    // 2) GObjObjects candidates: TArray<UObject*>; objects have a vtable pointer into the exe's .rdata
    int found = 0;
    for (size_t o = 0; o + sizeof(TArr) <= dataSize && found < 6; o += 4) {
        TArr* t = (TArr*)(dataStart + o);
        if (t == gnames) continue;
        if (t->Num < 20000 || t->Num > 3000000 || t->Max < t->Num || t->Max > 6000000) continue;
        if (!Rd(t->Data, 4 * 64)) continue;
        int good = 0;
        for (int i = 0; i < 64; i++) {
            BYTE* obj = (BYTE*)t->Data[i];
            if (!obj) continue;
            if (!Rd(obj, 0x60)) { good = -1; break; }
            BYTE* vt = *(BYTE**)obj;
            if (vt >= rdataStart && vt < rdataStart + rdataSize) good++;
        }
        if (good < 20) continue;
        found++;
        Log("scan: GObjObjects candidate at %p (rva 0x%lx) Num=%d Max=%d Data=%p (objects with exe vtable among first 64: %d)", t, (unsigned long)((BYTE*)t - base), t->Num, t->Max, t->Data, good);
        // print first objects: find which dword offset resolves to names
        for (int i = 0; i < 6; i++) {
            BYTE* obj = (BYTE*)t->Data[i];
            if (!obj || !Rd(obj, 0x60)) continue;
            char line[600];
            size_t ln = _snprintf_s(line, _TRUNCATE, "scan:   obj[%d]=%p vtable=%p rva 0x%lx:", i, obj, *(void**)obj, (unsigned long)(*(BYTE**)obj - base));
            for (int off = 0x08; off < 0x50 && ln + 40 < sizeof(line); off += 4) {
                char nm[40];
                int idx = *(int*)(obj + off);
                if (nameOf(idx, nm, sizeof(nm)) && strlen(nm) >= 3) ln += _snprintf_s(line + ln, sizeof(line) - ln, _TRUNCATE, " [+0x%x]=%s", off, nm);
            }
            Log("%s", line);
        }

        // 2b) raw layout dump of a few well-known objects
        {
            static const char* names2[] = {"GameInfo", "Default__GameInfo", "PostBeginPlay", "Actor", "Default__Actor", "Class", "Function"};
            for (int i = 0; i < t->Num; i++) {
                BYTE* obj = (BYTE*)t->Data[i];
                if (!obj || !Rd(obj, 0x80)) continue;
                char nm[80];
                if (!nameOf(*(int*)(obj + 0x18), nm, sizeof(nm))) continue;
                bool want = false;
                for (const char* w : names2) if (strcmp(nm, w) == 0) want = true;
                if (!want) continue;
                static int perName[8] = {0};
                int wi = 0; for (int q = 0; q < 7; q++) if (strcmp(nm, names2[q]) == 0) wi = q;
                if (perName[wi]++ >= 2) continue;
                char line[1400];
                size_t ln = _snprintf_s(line, _TRUNCATE, "scan: RAW %s obj=%p idx=%d:", nm, obj, i);
                for (int off = 0; off < 0x70 && ln + 40 < sizeof(line); off += 4) {
                    unsigned v = *(unsigned*)(obj + off);
                    char tn[40]; tn[0] = 0;
                    BYTE* tp = (BYTE*)(UINT_PTR)v;
                    if (v > 0x10000 && Rd(tp, 0x20)) nameOf(*(int*)(tp + 0x18), tn, sizeof(tn));
                    if (tn[0]) ln += _snprintf_s(line + ln, sizeof(line) - ln, _TRUNCATE, " +%x:%x(%s)", off, v, tn);
                    else ln += _snprintf_s(line + ln, sizeof(line) - ln, _TRUNCATE, " +%x:%x", off, v);
                }
                Log("%s", line);
            }
        }
        // 2d) list objects named *WorldInfo* / *PlayerController* / *GameInfo*
        {
            int shown = 0;
            for (int i = 0; i < t->Num && shown < 60; i++) {
                BYTE* obj = (BYTE*)t->Data[i];
                if (!obj || !Rd(obj, 0x30)) continue;
                char nm[80];
                if (!nameOf(*(int*)(obj + 0x18), nm, sizeof(nm))) continue;
                if (!(strstr(nm, "WorldInfo") || strstr(nm, "PlayerController") || strcmp(nm, "GameInfo") == 0)) continue;
                Log("scan: OBJ %s idx=%d obj=%p vtable rva=0x%lx +1c=%d", nm, i, obj, (unsigned long)(*(BYTE**)obj - base), *(int*)(obj + 0x1c));
                shown++;
            }
        }
        // 2c) live game instances: objects named Tn*Game*_<n> (not Default__); dump words that look like MaxPlayers pairs
        for (int i = 0; i < t->Num; i++) {
            BYTE* obj = (BYTE*)t->Data[i];
            if (!obj || !Rd(obj, 0x40)) continue;
            char nm[80];
            if (!nameOf(*(int*)(obj + 0x18), nm, sizeof(nm))) continue;
            size_t L = strlen(nm);
            if (strncmp(nm, "Tn", 2) != 0 || !strstr(nm, "Game") || strncmp(nm, "Default__", 9) == 0) continue;
            if (L < 3 || nm[L - 1] < '0' || nm[L - 1] > '9' || !strchr(nm, '_')) continue;
            if (strstr(nm, "Message") || strstr(nm, "Replication")) continue;
            Log("scan: GAMEINSTANCE %s obj=%p idx=%d", nm, obj, i);
            // search the first 0x800 bytes for adjacent int pairs (MaxPlayers, MaxPlayersAllowed): small positive ints, second >= first
            if (Rd(obj, 0x800)) {
                char line[1500]; size_t ln = _snprintf_s(line, _TRUNCATE, "scan:   pairs:");
                for (int off = 0x40; off < 0x7f8 && ln + 30 < sizeof(line); off += 4) {
                    int a = *(int*)(obj + off), b = *(int*)(obj + off + 4);
                    if (a >= 1 && a <= 32 && b >= a && b <= 64 && (b == 32 || b == 1 || b == 2 || b == 4 || b == 5 || b == 8 || b == 12 || b == 16)) ln += _snprintf_s(line + ln, sizeof(line) - ln, _TRUNCATE, " +%x:(%d,%d)", off, a, b);
                }
                Log("%s", line);
            }
        }
        // 3) vtables of a few default objects (UObject / Actor / GameInfo) to locate ProcessEvent
        static const char* wanted[] = {"Default__Object", "Default__Actor", "Default__GameInfo", "Default__Class", "PostBeginPlay", "Tick", "Touch"};
        for (int i = 0; i < t->Num; i++) {
            BYTE* obj = (BYTE*)t->Data[i];
            if (!obj || !Rd(obj, 0x30)) continue;
            char nm[80];
            if (!nameOf(*(int*)(obj + 0x18), nm, sizeof(nm))) continue;
            for (const char* w : wanted) {
                if (strcmp(nm, w) != 0) continue;
                BYTE** vt = *(BYTE***)obj;
                if (!Rd(vt, 4 * 120)) continue;
                char line[2200];
                size_t ln = _snprintf_s(line, _TRUNCATE, "scan: %s obj=%p vtable=%p rva=0x%lx slots(rva):", nm, obj, vt, (unsigned long)((BYTE*)vt - base));
                for (int k = 0; k < 110 && ln + 12 < sizeof(line); k++) ln += _snprintf_s(line + ln, sizeof(line) - ln, _TRUNCATE, " %lx", (unsigned long)(vt[k] - base));
                Log("%s", line);
                // class pointer candidates
                for (int off = 0x1c; off <= 0x60; off += 4) {
                    BYTE* c = *(BYTE**)(obj + off);
                    char cn[64];
                    if (c && Rd(c, 0x30) && nameOf(*(int*)(c + 0x18), cn, sizeof(cn))) Log("scan:   %s [+0x%x] -> %p name '%s'", nm, off, c, cn);
                }
            }
        }
    }
    Log("scan: done");
    return 0;
}


// ---------------------------------------------------------------- slot tracer (find UObject::ProcessEvent)
#include "slot_targets.inc"
static const unsigned long kGNamesRva = 0x15b1d20;
static const unsigned long kGObjRva = 0x15b1d5c;
static const int kNameFieldOff = 0x18;
static void* g_slotOrig[128];
static volatile LONG g_slotHits[128];
static char g_slotSample[128][3][48];
static volatile LONG g_slotSampleN[128];

static bool NameAt(int idx, char* out, size_t n) {
    out[0] = 0;
    BYTE* base = (BYTE*)GetModuleHandleW(nullptr);
    TArr* gn = (TArr*)(base + kGNamesRva);
    if (idx < 0 || idx >= gn->Num) return false;
    BYTE* e = (BYTE*)gn->Data[idx];
    if (!Rd(e, 8 + 4)) return false;
    const char* s = (const char*)e + 8;
    size_t k = 0;
    while (k + 1 < n && Rd(s + k, 1) && s[k]) { out[k] = s[k]; k++; }
    out[k] = 0;
    return k > 0;
}

extern "C" void __cdecl OnSlotHit(int n, void* self, void* a0, void* a1, void* a2) {
    __try {
        if (!a0 || (UINT_PTR)a0 < 0x10000) return;
        BYTE* o = (BYTE*)a0;
        if (!Rd(o, 0x40)) return;
        char nm[48];
        if (!NameAt(*(int*)(o + kNameFieldOff), nm, sizeof(nm))) return;
        if (!(nm[0] >= 'A' && nm[0] <= 'Z')) return;
        // a Function object has a vtable in the exe and a plausible name; ProcessEvent's first arg is such an object
        LONG h = InterlockedIncrement(&g_slotHits[n]);
        LONG s = g_slotSampleN[n];
        if (s < 3 && h < 5000) { strncpy_s(g_slotSample[n][s], nm, _TRUNCATE); g_slotSampleN[n] = s + 1; }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// Trampolines are generated at runtime (MSVC does not substitute template arguments inside __asm).
static BYTE* MakeTramp(int n, void* orig, void* handler = nullptr) {
    static BYTE* pool = nullptr;
    static size_t used = 0;
    if (!pool) pool = (BYTE*)VirtualAlloc(nullptr, 64 * 128, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    BYTE* p = pool + used;
    BYTE* w = p;
    auto b = [&](std::initializer_list<int> v) { for (int x : v) *w++ = (BYTE)x; };
    auto d = [&](unsigned long v) { memcpy(w, &v, 4); w += 4; };
    b({0x60});                    // pushad
    b({0x89, 0xE5});              // mov ebp, esp
    b({0xFF, 0x75, 0x2C});        // push [ebp+2c]  arg2
    b({0xFF, 0x75, 0x28});        // push [ebp+28]  arg1
    b({0xFF, 0x75, 0x24});        // push [ebp+24]  arg0
    b({0xFF, 0x75, 0x18});        // push [ebp+18]  saved ecx (this)
    b({0x68}); d((unsigned long)n);                          // push n
    b({0xB8}); d((unsigned long)(UINT_PTR)(handler ? handler : (void*)&OnSlotHit));       // mov eax, handler
    b({0xFF, 0xD0});              // call eax
    b({0x83, 0xC4, 0x14});        // add esp, 14h
    b({0xB8}); d((unsigned long)(UINT_PTR)orig);             // mov eax, orig
    b({0x89, 0x45, 0x1C});        // mov [ebp+1c], eax (saved eax slot)
    b({0x61});                    // popad
    b({0xFF, 0xE0});              // jmp eax
    used += 64;
    return p;
}

static DWORD WINAPI TraceThread(LPVOID) {
    Sleep(1500);
    BYTE* base = (BYTE*)GetModuleHandleW(nullptr);
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    BYTE* rd = nullptr; size_t rdSize = 0;
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++)
        if (memcmp(sec[i].Name, ".rdata", 6) == 0) { rd = base + sec[i].VirtualAddress; rdSize = sec[i].Misc.VirtualSize; }
    Log("trace: start, rdata=%p size=0x%zx", rd, rdSize);
    static void* tramps[128];
    for (int k = 0; k < kSlotCount && k < 110; k++) tramps[k] = MakeTramp(k, base + kSlotRva[k]);
    Log("trace: trampolines built, t0=%p t108=%p", tramps[0], tramps[108]);
    int patched = 0;
    for (int k = 0; k < kSlotCount && k < 110; k++) {
        g_slotOrig[k] = base + kSlotRva[k];
        for (size_t o = 0; o + 4 <= rdSize; o += 4) {
            void** p = (void**)(rd + o);
            if (*p == g_slotOrig[k]) {
                DWORD old;
                if (VirtualProtect(p, 4, PAGE_READWRITE, &old)) { *p = tramps[k]; VirtualProtect(p, 4, old, &old); patched++; }
            }
        }
    }
    Log("trace: patched %d vtable entries for %d slot functions", patched, kSlotCount);
    for (int round = 1; round <= 4; round++) {
        Sleep(10000);
        char line[1800]; size_t ln = _snprintf_s(line, _TRUNCATE, "trace: round %d hits:", round);
        for (int k = 0; k < kSlotCount && ln + 60 < sizeof(line); k++) {
            if (g_slotHits[k] > 0) ln += _snprintf_s(line + ln, sizeof(line) - ln, _TRUNCATE, " [%d rva=%lx n=%ld %s,%s,%s]", k, kSlotRva[k], g_slotHits[k], g_slotSample[k][0], g_slotSample[k][1], g_slotSample[k][2]);
        }
        Log("%s", line);
    }
    return 0;
}


// ---------------------------------------------------------------- UnrealScript bytecode patches
// A UFunction keeps its bytecode in a TArray<BYTE> (Data, Num, Max) inside the function object.
// "return false" is bytes 04 28 (EX_Return, EX_False): writing it over the first bytes of a bool function
// makes it return false immediately.
static int FindNameIndex(const char* name) {
    BYTE* base = (BYTE*)GetModuleHandleW(nullptr);
    TArr* gn = (TArr*)(base + kGNamesRva);
    if (gn->Num < 20000 || gn->Num > 3000000 || !gn->Data || !Rd(gn->Data, 16)) return -1;
    for (int i = 0; i < gn->Num; i++) {
        BYTE* e = (BYTE*)gn->Data[i];
        if (!Rd(e, 8 + 4)) continue;
        if (strcmp((const char*)e + 8, name) == 0) return i;
    }
    return -1;
}

// Patch every function object called `fname` whose bytecode length is within [minLen, maxLen] and starts with `firstByte`.
static const char* g_patchName = "AtCapacity";
static int PatchFunctionStart(int nameIdx, int minLen, int maxLen, BYTE firstByte, const BYTE* repl, int replLen) {
    BYTE* base = (BYTE*)GetModuleHandleW(nullptr);
    TArr* go = (TArr*)(base + kGObjRva);
    int patched = 0;
    if (go->Num < 1000 || go->Num > 3000000 || !go->Data || !Rd(go->Data, 16)) return 0;
    for (int i = 0; i < go->Num; i++) {
        if (!Rd(&go->Data[i], 4)) break;
        BYTE* obj = (BYTE*)go->Data[i];
        if (!obj) continue;
        if (!Rd(obj, 0x30)) continue;
        if (*(int*)(obj + 0x18) != nameIdx) continue;
        if (!Rd(obj, 0x160)) continue;
        {
            static int dbg = 0;
            if (dbg++ < 6) {
                char line[1200]; size_t ln = _snprintf_s(line, _TRUNCATE, "scriptpatch: debug obj %p idx %d arrays:", obj, i);
                for (int off = 0x20; off < 0x150 && ln + 40 < sizeof(line); off += 4) {
                    TArr* a = (TArr*)(obj + off);
                    if (a->Num > 0 && a->Num < 100000 && a->Max >= a->Num && a->Max < 200000 && Rd(a->Data, 4)) ln += _snprintf_s(line + ln, sizeof(line) - ln, _TRUNCATE, " +%x:(%d,%d,b0=%02x)", off, a->Num, a->Max, *(BYTE*)a->Data);
                }
                Log("%s", line);
            }
        }
        for (int off = 0x20; off < 0x150; off += 4) {
            TArr* a = (TArr*)(obj + off);
            if (a->Num < minLen || a->Num > maxLen || a->Max < a->Num || a->Max > a->Num + 256) continue;
            if (!Rd(a->Data, a->Num)) continue;
            BYTE* b = (BYTE*)a->Data;
            if (b[0] == firstByte) {
                DWORD old;
                if (VirtualProtect(b, replLen, PAGE_READWRITE, &old)) {
                    memcpy(b, repl, replLen);
                    VirtualProtect(b, replLen, old, &old);
                    Log("scriptpatch: obj %p (idx %d) bytecode at %p len %d patched", obj, i, b, a->Num);
                    patched++;
                }
                break;
            }
        }
    }
    return patched;
}

static DWORD WINAPI ScriptPatchThread(LPVOID) {
    int atCap = -1;
    int total = 0;
    for (int round = 0; round < 240; round++) {
        Sleep(1000);
        if (round == 12 || round == 45) {
            __try {
                static const char* probe[] = {"get_CustomCharactersLoaded", "get_CharactersLoaded", "UpdateStatus", "InitializeCharacters", "CustomCharacterReadComplete", "OnKeyArchiveReadComplete", "ReadCustomizationFile", "ReadInFile", "CheckCustomCharacterDataLoaded", "ShouldDelayGameInvites", "AtCapacity", "Login", "PostLogin", "HasSelectedCharacter", "OnStorageDeviceChange"};
                const int NP = 15;
                int idxs[NP]; int counts[NP] = {0};
                for (int q = 0; q < NP; q++) idxs[q] = FindNameIndex(probe[q]);
                BYTE* base = (BYTE*)GetModuleHandleW(nullptr);
                TArr* go = (TArr*)(base + kGObjRva);
                for (int i = 0; i < go->Num; i++) {
                    BYTE* obj = (BYTE*)go->Data[i];
                    if (!obj) continue;
                    int nidx = *(int*)(obj + 0x18);
                    for (int q = 0; q < NP; q++) if (nidx == idxs[q]) counts[q]++;
                }
                char line[900]; size_t ln = _snprintf_s(line, _TRUNCATE, "scriptpatch: presence round %d (Num=%d):", round, go->Num);
                for (int q = 0; q < NP; q++) ln += _snprintf_s(line + ln, sizeof(line) - ln, _TRUNCATE, " %s[%d]=%d", probe[q], idxs[q], counts[q]);
                Log("%s", line);
            } __except (EXCEPTION_EXECUTE_HANDLER) { Log("scriptpatch: presence probe faulted"); }
        }
        int n = 0;
        __try {
            if (atCap < 0) { atCap = FindNameIndex("AtCapacity"); if (atCap >= 0) Log("scriptpatch: name index of AtCapacity = %d", atCap); }
            if (atCap < 0) continue;
            static const BYTE retFalse[2] = {0x04, 0x28};
            n = PatchFunctionStart(atCap, 100, 160, 0x07, retFalse, 2);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            n = 0;
        }
        if (n > 0) { total += n; Log("scriptpatch: AtCapacity -> return false (%d object(s), total %d, round %d)", n, total, round); }
    }
    return 0;
}


// ---------------------------------------------------------------- config (Coalesced) values in the heap
// The game keeps [section] key=value pairs as FString pairs. For a key string, find the FString header
// {Data, Num, Max} that points at it, then read the FString that follows (the value) and, optionally, rewrite it.
struct FStr { wchar_t* Data; int Num; int Max; };

static int FindWideStrings(const wchar_t* needle, void (*cb)(wchar_t* where, void* ctx), void* ctx) {
    size_t bytes = wcslen(needle) * sizeof(wchar_t);
    SYSTEM_INFO si; GetSystemInfo(&si);
    BYTE* p = (BYTE*)si.lpMinimumApplicationAddress; BYTE* end = (BYTE*)si.lpMaximumApplicationAddress;
    int count = 0;
    MEMORY_BASIC_INFORMATION mbi;
    while (p < end && VirtualQuery(p, &mbi, sizeof(mbi))) {
        BYTE* rb = (BYTE*)mbi.BaseAddress; size_t rs = mbi.RegionSize;
        if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE && mbi.Protect == PAGE_READWRITE && rs >= bytes + 2) {
            __try {
                for (size_t o = 0; o + bytes + 2 <= rs; o += 2) {
                    if (rb[o] == (BYTE)needle[0] && memcmp(rb + o, needle, bytes) == 0 && *(wchar_t*)(rb + o + bytes) == 0) { cb((wchar_t*)(rb + o), ctx); count++; }
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        p = rb + rs;
    }
    return count;
}

struct CfgCtx { const wchar_t* key; const wchar_t* newValue; int found; int changed; };

static void CfgCb(wchar_t* where, void* vctx) {
    CfgCtx* c = (CfgCtx*)vctx;
    // find FString headers {Data==where, Num==len+1} in the heap near the string (scan all private RW memory again is too slow;
    // instead look at the 256 bytes around where for a header pointing at it)
    BYTE* lo = (BYTE*)where - 4096; BYTE* hi = (BYTE*)where + 4096;
    for (BYTE* q = lo; q < hi; q += 4) {
        if (!Rd(q, sizeof(FStr) * 2)) continue;
        FStr* k = (FStr*)q;
        if (k->Data != where || k->Num != (int)wcslen(c->key) + 1) continue;
        FStr* v = k + 1;
        if (!Rd(v->Data, 4) || v->Num < 1 || v->Num > 64) continue;
        wchar_t cur[70] = {0};
        wcsncpy_s(cur, v->Data, 64);
        Log("cfg: %ls -> '%ls' (key str %p, header %p, value %p num %d max %d)", c->key, cur, where, k, v->Data, v->Num, v->Max);
        c->found++;
        if (c->newValue && v->Max >= (int)wcslen(c->newValue) + 1) {
            wcscpy_s(v->Data, v->Max, c->newValue);
            v->Num = (int)wcslen(c->newValue) + 1;
            c->changed++;
        }
    }
}

static DWORD WINAPI CfgThread(LPVOID) {
    static const wchar_t* keys[] = {L"MaxPlayers", L"MaxPlayersAllowed", L"NetworkDevice", L"ConnectionTimeout", L"InitialConnectTimeout"};
    Sleep(1200);
    for (const wchar_t* k : keys) {
        CfgCtx c = {k, nullptr, 0, 0};
        int n = FindWideStrings(k, CfgCb, &c);
        Log("cfg: key %ls: %d string occurrence(s), %d header match(es)", k, n, c.found);
    }
    return 0;
}


// ---------------------------------------------------------------- GNatives hooks (script opcode handlers)
static const unsigned long kGNativesRva = 0x154c968;
static volatile LONG g_natLines = 0;
static volatile LONG g_natCount[256];

static bool ObjName(void* p, char* out, size_t n) {
    out[0] = 0;
    if ((UINT_PTR)p < 0x10000 || !Rd(p, 0x30)) return false;
    return NameAt(*(int*)((BYTE*)p + kNameFieldOff), out, n);
}

static int g_wantNames[8] = {-1,-1,-1,-1,-1,-1,-1,-1};
static const char* g_wantList[8] = {"SetAButton", "SetBButton", "SetText", "GetPlayerController", "UpdateStatus", "UpdateText", "Close", "get_CustomCharactersLoaded"};
static volatile LONG g_wantLog = 0;

extern "C" void __cdecl OnNative(int op, void* self, void* stack, void* result, void*) {
    InterlockedIncrement(&g_natCount[op & 255]);
    if (g_wantLog > 60) return;
    __try {
        if (!Rd(stack, 0x40)) return;
        BYTE* code = *(BYTE**)((BYTE*)stack + 0xC);
        if (!code || !Rd(code, 64)) return;
        int nameIdx = -1;
        if (op == 0x1B || op == 0x1D) nameIdx = *(int*)code;
        else {
            BYTE* fp = *(BYTE**)code;
            if ((UINT_PTR)fp > 0x10000 && Rd(fp, 0x30)) nameIdx = *(int*)(fp + kNameFieldOff);
        }
        char cn[64] = "";
        if (nameIdx < 0 || !NameAt(nameIdx, cn, sizeof(cn))) return;
        for (int q = 0; q < 8; q++) {
            if (strcmp(cn, g_wantList[q]) == 0) {
                LONG n = InterlockedIncrement(&g_wantLog);
                if (n > 60) return;
                char hx[200]; hexdump((const char*)code - 1, 44, hx, sizeof(hx));
                Log("native: op=%02x call '%s' this=%p frame code=%p bytes(-1..43): %s", op, g_wantList[q], self, code, hx);
                break;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

extern "C" void __cdecl OnCount(int op, void* self, void* stack, void* result, void*) {
    InterlockedIncrement(&g_natCount[op & 255]);
}

static DWORD WINAPI NativesThread(LPVOID) {
    Sleep(15000);
    BYTE* base = (BYTE*)GetModuleHandleW(nullptr);
    void** tab = (void**)(base + kGNativesRva);
    for (int q = 0; q < 8; q++) g_wantNames[q] = FindNameIndex(g_wantList[q]);
    int hooked = 0;
    for (int op = 0; op < 256; op++) {
        void* orig = tab[op];
        if (orig == (void*)(base + 0x2af10 + 0x470000 - 0x470000)) {}
        if ((BYTE*)orig == base + 0x2af10 - 0x0) {}
        BYTE* tr = MakeTramp(op, orig, (op == 0x1B || op == 0x1C) ? (void*)&OnNative : (void*)&OnCount);
        DWORD old;
        if (VirtualProtect(&tab[op], 4, PAGE_READWRITE, &old)) { tab[op] = tr; VirtualProtect(&tab[op], 4, old, &old); hooked++; }
    }
    Log("natives: hooked %d opcode slots", hooked);
    LONG prev[256] = {0};
    for (int i = 0; i < 6; i++) {
        Sleep(10000);
        char line[1500]; size_t k = _snprintf_s(line, _TRUNCATE, "natives: per-10s counts:");
        LONG total = 0;
        for (int op = 0; op < 256; op++) { LONG c = g_natCount[op]; LONG dlt = c - prev[op]; prev[op] = c; total += dlt; if (dlt > 2000 && k + 24 < sizeof(line)) k += _snprintf_s(line + k, sizeof(line) - k, _TRUNCATE, " %02x=%ld", op, dlt); }
        Log("%s total=%ld", line, total);
    }
    return 0;
}


// ---------------------------------------------------------------- TnLocalPlayer discovery
#define g_lp g_lpFlag

static void DumpRaw(const char* tag, BYTE* obj, int bytes) {
    char line[1800]; size_t k = _snprintf_s(line, _TRUNCATE, "lp: %s obj=%p:", tag, obj);
    for (int off = 0; off < bytes && k + 14 < sizeof(line); off += 4) k += _snprintf_s(line + k, sizeof(line) - k, _TRUNCATE, " %x", *(unsigned*)(obj + off));
    Log("%s", line);
}

static DWORD WINAPI LocalPlayerThread(LPVOID) {
    Sleep(g_scanDelayMs);
    BYTE* base = (BYTE*)GetModuleHandleW(nullptr);
    TArr* go = (TArr*)(base + kGObjRva);
    __try {
        static const char* props[] = {"_HasBypassedIISOnce", "_CharactersLoaded", "_CorruptCustomChatacterDlgUp", "_CopiedCustomChatacterDlgUp", "_HasShownAutoSaveWarning", "_WaitingForConfirmResetDLCCharacterParts", "_XpManager", "_PlayerFileHandler"};
        int pidx[8]; for (int q = 0; q < 8; q++) pidx[q] = FindNameIndex(props[q]);
        int defIdx = FindNameIndex("Default__TnLocalPlayer");
        Log("lp: name indices: %d %d %d %d %d %d, Default__TnLocalPlayer=%d", pidx[0], pidx[1], pidx[2], pidx[3], pidx[4], pidx[5], defIdx);
        BYTE* defobj = nullptr; void* vt = nullptr;
        for (int i = 0; i < go->Num; i++) {
            BYTE* obj = (BYTE*)go->Data[i];
            if (!obj || !Rd(obj, 0x80)) continue;
            int n = *(int*)(obj + kNameFieldOff);
            for (int q = 0; q < 8; q++) if (n == pidx[q]) { char t[64]; _snprintf_s(t, _TRUNCATE, "prop %s idx=%d", props[q], i); DumpRaw(t, obj, 0x80); }
            if (n == defIdx && !defobj) { defobj = obj; vt = *(void**)obj; DumpRaw("Default__TnLocalPlayer", obj, 0x60); }
        }
        {
            // every name containing LocalPlayer and the objects that carry it
            TArr* gn = (TArr*)(base + kGNamesRva);
            for (int i = 0; i < gn->Num; i++) {
                char nm[80];
                if (!NameAt(i, nm, sizeof(nm))) continue;
                bool inst = (strncmp(nm, "TnWorldInfo_", 12) == 0 || strncmp(nm, "TnLocalPlayer_", 14) == 0 || strncmp(nm, "TnSurvivalGame_", 15) == 0 || strncmp(nm, "TnGameEngine", 12) == 0 || strncmp(nm, "TnSurvivalGame", 14) == 0 || strncmp(nm, "TnPlayerController_", 19) == 0);
                if (!inst && !strstr(nm, "LocalPlayer")) continue;
                if (!inst) continue;
                int cnt = 0; BYTE* first = nullptr;
                for (int j = 0; j < go->Num; j++) { BYTE* obj = (BYTE*)go->Data[j]; if (obj && Rd(obj, 0x30) && *(int*)(obj + kNameFieldOff) == i) { cnt++; if (!first) first = obj; } }
                if (cnt) Log("lp: name '%s' idx %d carried by %d object(s), first %p vtable rva 0x%lx", nm, i, cnt, first, (unsigned long)(*(BYTE**)first - base));
            }
        }
        if (vt) {
            Log("lp: Default__TnLocalPlayer vtable rva 0x%lx", (unsigned long)((BYTE*)vt - base));
            for (int i = 0; i < go->Num; i++) {
                BYTE* obj = (BYTE*)go->Data[i];
                if (!obj || obj == defobj || !Rd(obj, 0x40)) continue;
                if (*(void**)obj != vt) continue;
                char t[64]; _snprintf_s(t, _TRUNCATE, "instance idx=%d", i);
                DumpRaw(t, obj, 0x60);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { Log("lp: faulted"); }
    Log("lp: done");
    return 0;
}


// ---------------------------------------------------------------- instances of a native class (found through its vtable)

static BYTE* FindClassObject(const char* name) {
    BYTE* base = (BYTE*)GetModuleHandleW(nullptr);
    TArr* go = (TArr*)(base + kGObjRva);
    int idx = FindNameIndex(name);
    if (idx < 0) return nullptr;
    for (int i = 0; i < go->Num; i++) {
        BYTE* obj = (BYTE*)go->Data[i];
        if (obj && Rd(obj, 0x30) && *(int*)(obj + kNameFieldOff) == idx) return obj;
    }
    return nullptr;
}

static DWORD WINAPI InstThread(LPVOID) {
    Sleep(g_scanDelayMs);
    BYTE* base = (BYTE*)GetModuleHandleW(nullptr);
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    BYTE* textLo = base + 0x1000; BYTE* textHi = base + 0x1000 + 0xf5fc71;
    __try {
        BYTE* cls = FindClassObject(g_instClass);
        Log("inst: class '%s' object %p", g_instClass, cls);
        if (!cls || !Rd(cls, 0x400)) return 0;
        {
            char line[3000]; size_t k = _snprintf_s(line, _TRUNCATE, "inst: class words:");
            for (int off = 0; off < 0x200; off += 4) k += _snprintf_s(line + k, sizeof(line) - k, _TRUNCATE, " %x", *(unsigned*)(cls + off));
            Log("%s", line);
            for (int off = 0; off < 0x3f0; off += 4) {
                BYTE* f = *(BYTE**)(cls + off);
                if (f >= textLo && f < textHi && Rd(f, 24)) { char hx[80]; hexdump((const char*)f, 20, hx, sizeof(hx)); Log("inst:   class+0x%x -> text %p: %s", off, f, hx); }
            }
        }
        // candidate vtables: function pointers inside the class object whose first bytes are  C7 01 imm32  (mov [ecx], vtable)
        void* vtables[16]; int nv = 0;
        for (int off = 0; off < 0x3f0; off += 4) {
            BYTE* f = *(BYTE**)(cls + off);
            if (f < textLo || f >= textHi || !Rd(f, 64)) continue;
            for (int k = 0; k < 40; k++) {
                if (f[k] == 0xC7 && f[k + 1] == 0x01) {
                    void* vt = *(void**)(f + k + 2);
                    bool dup = false; for (int q = 0; q < nv; q++) if (vtables[q] == vt) dup = true;
                    if (!dup && (BYTE*)vt >= base && nv < 16) { vtables[nv++] = vt; Log("inst:   class+0x%x fn %p sets vtable %p (rva 0x%lx)", off, f, vt, (unsigned long)((BYTE*)vt - base)); }
                    break;
                }
            }
        }
        {
            // follow pointer fields of the stub
            for (int off = 0; off < 0x40; off += 4) {
                BYTE* t2 = *(BYTE**)(cls + off);
                if ((UINT_PTR)t2 < 0x1000000 || t2 == cls || !Rd(t2, 0x100)) continue;
                char line[2200]; size_t k = _snprintf_s(line, _TRUNCATE, "inst: stub+0x%x -> %p words:", off, t2);
                for (int o2 = 0; o2 < 0xE0; o2 += 4) k += _snprintf_s(line + k, sizeof(line) - k, _TRUNCATE, " %x", *(unsigned*)(t2 + o2));
                Log("%s", line);
            }
        }
        if (nv == 0) { Log("inst: no vtable found in class object"); return 0; }
        // sweep the heap for objects starting with one of these vtables
        SYSTEM_INFO si; GetSystemInfo(&si);
        BYTE* p = (BYTE*)si.lpMinimumApplicationAddress; BYTE* end = (BYTE*)si.lpMaximumApplicationAddress;
        int found = 0;
        MEMORY_BASIC_INFORMATION mbi;
        while (p < end && VirtualQuery(p, &mbi, sizeof(mbi))) {
            BYTE* rb = (BYTE*)mbi.BaseAddress; size_t rs = mbi.RegionSize;
            if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE && (mbi.Protect == PAGE_READWRITE) && rs >= 0x100) {
                for (size_t o = 0; o + 0x600 <= rs; o += 4) {
                    void* v = *(void**)(rb + o);
                    for (int q = 0; q < nv; q++) {
                        if (v == vtables[q] && found < 6) {
                            found++;
                            char line[2600]; size_t k = _snprintf_s(line, _TRUNCATE, "inst: INSTANCE %p (vtable #%d) words:", rb + o, q);
                            int printed = 0;
                            for (int off = 4; off < 0x600 && k + 24 < sizeof(line) && printed < 180; off += 4) {
                                unsigned w = *(unsigned*)(rb + o + off);
                                if (w) { k += _snprintf_s(line + k, sizeof(line) - k, _TRUNCATE, " %x:%x", off, w); printed++; }
                            }
                            Log("%s", line);
                        }
                    }
                }
            }
            p = rb + rs;
        }
        Log("inst: found %d instance(s)", found);
    } __except (EXCEPTION_EXECUTE_HANDLER) { Log("inst: faulted"); }
    return 0;
}


// ---------------------------------------------------------------- bytecode skeleton patches
// Search the private heap for a bytecode skeleton (0xFF in `mask` = must match, 0x00 = wildcard, used for the
// 4-byte object references) and return all matches.
static bool g_anyMem = false;
static int FindSkeleton(const BYTE* pat, const BYTE* mask, int len, BYTE** out, int maxOut) {
    SYSTEM_INFO si; GetSystemInfo(&si);
    BYTE* p = (BYTE*)si.lpMinimumApplicationAddress; BYTE* end = (BYTE*)si.lpMaximumApplicationAddress;
    int n = 0;
    MEMORY_BASIC_INFORMATION mbi;
    while (p < end && VirtualQuery(p, &mbi, sizeof(mbi))) {
        BYTE* rb = (BYTE*)mbi.BaseAddress; size_t rs = mbi.RegionSize;
        bool okReg = mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) && (g_anyMem ? true : (mbi.Type == MEM_PRIVATE && mbi.Protect == PAGE_READWRITE));
        if (okReg && rs >= (size_t)len) {
            __try {
                for (size_t o = 0; o + len <= rs; o++) {
                    if (rb[o] != pat[0]) continue;
                    bool ok = true;
                    for (int k = 1; k < len; k++) if (mask[k] && rb[o + k] != pat[k]) { ok = false; break; }
                    if (ok && n < maxOut) out[n++] = rb + o;
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        p = rb + rs;
    }
    return n;
}

#define g_charFix g_charFixFlag
static DWORD WINAPI CharFixThread(LPVOID) {
    // get_CustomCharactersLoaded: 04 19 2e <cls4> 01 <prop4> 0a 00 04 00 2d 01 <prop4> 04 3a <prop4> 53
    static const BYTE p1[29] = {0x04,0x19,0x2e,0,0,0,0,0x01,0,0,0,0,0x0a,0x00,0x04,0x00,0x2d,0x01,0,0,0,0,0x04,0x3a,0,0,0,0,0x53};
    static const BYTE m1[29] = {0xff,0xff,0xff,0,0,0,0,0xff,0,0,0,0,0xff,0xff,0xff,0xff,0xff,0xff,0,0,0,0,0xff,0xff,0,0,0,0,0xff};
    // get_CharactersLoaded (and similar bool getters): 04 2d 01 <prop4> 04 3a <prop4> 53
    static const BYTE p2[14] = {0x04,0x2d,0x01,0,0,0,0,0x04,0x3a,0,0,0,0,0x53};
    static const BYTE m2[14] = {0xff,0xff,0xff,0,0,0,0,0xff,0xff,0,0,0,0,0xff};
    int charsIdx = -1;
    int patched1 = 0, patched2 = 0;
    for (int round = 0; round < 90; round++) {
        Sleep(2000);
        BYTE* hits[64];
        int n = FindSkeleton(p1, m1, sizeof(p1), hits, 64);
        for (int i = 0; i < n; i++) {
            if (hits[i][1] == 0x27) continue;
            DWORD old;
            if (VirtualProtect(hits[i], 2, PAGE_READWRITE, &old)) { hits[i][1] = 0x27; VirtualProtect(hits[i], 2, old, &old); patched1++; Log("charfix: get_CustomCharactersLoaded at %p -> return true (round %d)", hits[i], round); }
        }
        __try {
            if (charsIdx < 0) charsIdx = FindNameIndex("_CharactersLoaded");
            BYTE* h2[256];
            int n2 = FindSkeleton(p2, m2, sizeof(p2), h2, 256);
            for (int i = 0; i < n2; i++) {
                BYTE* prop = *(BYTE**)(h2[i] + 3);
                if ((UINT_PTR)prop < 0x10000 || !Rd(prop, 0x30)) continue;
                if (*(int*)(prop + kNameFieldOff) != charsIdx) continue;
                if (h2[i][1] == 0x27) continue;
                DWORD old;
                // replace "2d 01 <prop>" by "27" + padding nops: bytes: 04 27 0b 0b 0b 0b 0b ... (0b = EX_Nothing) keep length
                if (VirtualProtect(h2[i], 8, PAGE_READWRITE, &old)) {
                    h2[i][1] = 0x27;
                    for (int k = 2; k < 8; k++) h2[i][k] = 0x0b;
                    VirtualProtect(h2[i], 8, old, &old);
                    patched2++;
                    Log("charfix: get_CharactersLoaded at %p -> return true (round %d)", h2[i], round);
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        if (round % 10 == 0) Log("charfix: round %d, patched1=%d patched2=%d matches p1=%d", round, patched1, patched2, n);
        if (round == 10) {
            // UpdateStatus of the Loading Characters popup: two SetXButton("") calls then more
            static const BYTE pu[30] = {0x1b,0,0,0,0,0,0,0,0,0x1f,0x00,0x4a,0x4a,0x4a,0x16,0x1b,0,0,0,0,0,0,0,0,0x1f,0x00,0x4a,0x4a,0x4a,0x16};
            static const BYTE mu[30] = {0xff,0,0,0,0,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0,0,0,0,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff};
            BYTE* hh[16];
            g_anyMem = true;
            int au = FindSkeleton(pu, mu, sizeof(pu), hh, 16);
            g_anyMem = false;
            Log("charfix: UpdateStatus skeleton hits (any memory): %d", au);
            for (int i = 0; i < au && i < 4; i++) {
                MEMORY_BASIC_INFORMATION mbi; VirtualQuery(hh[i], &mbi, sizeof(mbi));
                char hx[500]; hexdump((const char*)hh[i], 120, hx, sizeof(hx));
                Log("charfix:   hit %p type=%lx protect=%lx: %s", hh[i], mbi.Type, mbi.Protect, hx);
            }
        }
        if (round == 4) {
            g_anyMem = true;
            BYTE* hh[16];
            int a1 = FindSkeleton(p1, m1, sizeof(p1), hh, 16);
            for (int i = 0; i < a1 && i < 4; i++) { MEMORY_BASIC_INFORMATION mbi; VirtualQuery(hh[i], &mbi, sizeof(mbi)); Log("charfix: any-memory skeleton p1 hit %p type=%lx protect=%lx bytes=%02x%02x%02x%02x %02x%02x%02x%02x%02x", hh[i], mbi.Type, mbi.Protect, hh[i][3], hh[i][4], hh[i][5], hh[i][6], hh[i][7], hh[i][8], hh[i][9], hh[i][10], hh[i][11]); }
            int a2 = FindSkeleton(p2, m2, sizeof(p2), hh, 16);
            Log("charfix: any-memory matches p1=%d p2(first16)=%d", a1, a2);
            g_anyMem = false;
        }
    }
    return 0;
}


// ---------------------------------------------------------------- heap snapshots (for offline diffing)

static void WriteSnapshot(const wchar_t* name) {
    wchar_t dir[MAX_PATH]; GetModuleFileNameW(nullptr, dir, MAX_PATH);
    wchar_t* sl = wcsrchr(dir, L'\\'); if (sl) *sl = 0;
    wchar_t pbin[MAX_PATH], pidx[MAX_PATH];
    _snwprintf_s(pbin, _TRUNCATE, L"%s\\%s.bin", dir, name);
    _snwprintf_s(pidx, _TRUNCATE, L"%s\\%s.idx", dir, name);
    FILE* fb = nullptr; FILE* fi = nullptr;
    _wfopen_s(&fb, pbin, L"wb"); _wfopen_s(&fi, pidx, L"w");
    if (!fb || !fi) { Log("snap: cannot open output"); return; }
    SYSTEM_INFO si; GetSystemInfo(&si);
    BYTE* p = (BYTE*)si.lpMinimumApplicationAddress; BYTE* end = (BYTE*)si.lpMaximumApplicationAddress;
    MEMORY_BASIC_INFORMATION mbi;
    unsigned long long off = 0; int regions = 0;
    while (p < end && VirtualQuery(p, &mbi, sizeof(mbi))) {
        BYTE* rb = (BYTE*)mbi.BaseAddress; size_t rs = mbi.RegionSize;
        if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE && mbi.Protect == PAGE_READWRITE && rs <= (96u << 20)) {
            __try {
                fwrite(rb, 1, rs, fb);
                fprintf(fi, "%p %zx %llx\n", rb, rs, off);
                off += rs; regions++;
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        p = rb + rs;
    }
    fclose(fb); fclose(fi);
    Log("snap: wrote %ls: %d regions, %llu bytes", name, regions, off);
}

static DWORD WINAPI SnapThread(LPVOID) {
    DWORD t0 = GetTickCount();
    if (g_snap1 > 0) { Sleep(g_snap1); WriteSnapshot(L"foc_snap1"); }
    if (g_snap2 > 0) { DWORD el = GetTickCount() - t0; if ((DWORD)g_snap2 > el) Sleep(g_snap2 - el); WriteSnapshot(L"foc_snap2"); }
    return 0;
}


// ---------------------------------------------------------------- GEngine -> GamePlayers (LocalPlayer instances)
static const unsigned long kGEngineRva = 0x1561fdc;

static void LogObjWords(const char* tag, BYTE* obj, int bytes) {
    char line[3800]; size_t k = _snprintf_s(line, _TRUNCATE, "eng: %s %p:", tag, obj);
    int n = 0;
    for (int off = 0; off < bytes && k + 22 < sizeof(line); off += 4) { unsigned w = *(unsigned*)(obj + off); if (w) { k += _snprintf_s(line + k, sizeof(line) - k, _TRUNCATE, " %x:%x", off, w); n++; } }
    Log("%s", line);
}

static DWORD WINAPI EngineThread(LPVOID) {
    BYTE* base = (BYTE*)GetModuleHandleW(nullptr);
    BYTE* rdLo = base + 0xf61000; BYTE* rdHi = base + 0xf61000 + 0x56da46;
    BYTE* lp = nullptr;
    for (int round = 0; round < 12; round++) {
        Sleep(round == 0 ? 6000 : 5000);
        __try {
            BYTE* eng = *(BYTE**)(base + kGEngineRva);
            if (round == 0) Log("eng: GEngine=%p vtable=%p", eng, eng ? *(void**)eng : nullptr);
            int engSize = 0;
            for (int sz = 0x1400; sz >= 0x100; sz -= 0x100) if (Rd(eng, sz)) { engSize = sz; break; }
            if (!eng || engSize == 0) continue;
            if (round == 0) Log("eng: readable engine bytes: 0x%x", engSize);
            if (!lp) {
                for (int off = 0x20; off < engSize - 0x10; off += 4) {
                    TArr* a = (TArr*)(eng + off);
                    if (a->Num < 1 || a->Num > 4 || a->Max < a->Num || a->Max > 8 || !Rd(a->Data, 4)) continue;
                    BYTE* o = (BYTE*)a->Data[0];
                    if ((UINT_PTR)o < 0x10000 || !Rd(o, 0x600)) continue;
                    BYTE* vt = *(BYTE**)o;
                    if (vt < rdLo || vt >= rdHi) continue;
                    Log("eng: GEngine+0x%x TArray Num=%d -> %p vtable rva 0x%lx", off, a->Num, o, (unsigned long)(vt - base));
                    if (!lp) lp = o;   // first plausible; log all and pick later
                }
            }
            if (lp && Rd(lp, 0x600)) { char tag[40]; _snprintf_s(tag, _TRUNCATE, "LP words t=%d", (round + 1) * 5); LogObjWords(tag, lp, 0x600); }
        } __except (EXCEPTION_EXECUTE_HANDLER) { Log("eng: faulted"); }
    }
    return 0;
}


// ---------------------------------------------------------------- class id based instance finder
static const unsigned long kClassByIdRva = 0x19465c4 - 0x470000;   // table of class pointers indexed by class id (4 bytes each)

static bool ObjNameStr(BYTE* obj, char* out, size_t n, unsigned* number = nullptr) {
    out[0] = 0;
    if (!obj || !Rd(obj, 0x20)) return false;
    unsigned packed = *(unsigned*)(obj + 4);
    if (number) *number = packed & 0x3fff;
    return NameAt((int)(packed >> 14), out, n);
}

static DWORD WINAPI FindClsThread(LPVOID) {
    Sleep(g_scanDelayMs);
    BYTE* base = (BYTE*)GetModuleHandleW(nullptr);
    TArr* go = (TArr*)(base + kGObjRva);
    BYTE** classById = (BYTE**)(base + kClassByIdRva);
    __try {
        Log("fcls: GObjects Num=%d, looking for class '%s'", go->Num, g_findCls);
        int cnt = 0;
        unsigned wantId = 0; BYTE* classObj = nullptr;
        for (int i = 0; i < go->Num && cnt < 12; i++) {
            BYTE* obj = (BYTE*)go->Data[i];
            if (!obj) continue;
            char nm[80]; unsigned num = 0;
            if (!ObjNameStr(obj, nm, sizeof(nm), &num) || strcmp(nm, g_findCls) != 0) continue;
            cnt++;
            unsigned cid = Rd(obj, 0x88) ? *(unsigned*)(obj + 0x14) & 0x1fff : 0;
            unsigned own = Rd(obj, 0x88) ? *(unsigned*)(obj + 0x84) : 0;
            BYTE* tab = (own > 0 && own < 8192) ? classById[own] : nullptr;
            Log("fcls: obj %p idx=%d name '%s' num=%u classId(+0x14)=%u own(+0x84)=%u ClassById[own]=%p readable0x88=%d", obj, i, nm, num, cid, own, tab, Rd(obj, 0x88) ? 1 : 0);
            if (tab == obj || tab == obj + 0x10) { wantId = own; classObj = obj; }
        }
        // instance class id = class object's +0x84 plus one
        {
            // pick the class object: classId(+0x14) == 3 ('Class') and +0x84 plausible
            for (int i = 0; i < go->Num; i++) {
                BYTE* obj = (BYTE*)go->Data[i];
                if (!obj || !Rd(obj, 0x88)) continue;
                char nm[80]; unsigned num = 0;
                if (!ObjNameStr(obj, nm, sizeof(nm), &num) || strcmp(nm, g_findCls) != 0 || num != 0) continue;
                if ((*(unsigned*)(obj + 0x14) & 0x1fff) != 3) continue;
                wantId = *(unsigned*)(obj + 0x84) + 1; classObj = obj; break;
            }
        }
        if (!wantId) { Log("fcls: class object not found"); return 0; }
        Log("fcls: class object %p -> instance class id %u", classObj, wantId);
        int found = 0;
        for (int i = 0; i < go->Num; i++) {
            BYTE* obj = (BYTE*)go->Data[i];
            if (!obj || !Rd(obj, 0x30)) continue;
            if ((*(unsigned*)(obj + 0x14) & 0x1fff) != wantId) continue;
            char nm[80]; unsigned num = 0; ObjNameStr(obj, nm, sizeof(nm), &num);
            found++;
            if (found <= 6) {
                Log("fcls: INSTANCE idx=%d %p name '%s' num=%u vtable rva 0x%lx", i, obj, nm, num, (unsigned long)(*(BYTE**)obj - base));
                for (int t = 0; t < 3; t++) {
                    if (Rd(obj, 0x700)) { char tag[48]; _snprintf_s(tag, _TRUNCATE, "INST words t+%ds", t * 10); LogObjWords(tag, obj, 0x700); }
                    if (t < 2) Sleep(10000);
                }
            }
        }
        Log("fcls: %d instance(s) of class id %u", found, wantId);
    } __except (EXCEPTION_EXECUTE_HANDLER) { Log("fcls: faulted"); }
    return 0;
}


// ---------------------------------------------------------------- force TnLocalPlayer._CharactersLoaded
// Layout facts (found by diffing the instance against Default__TnLocalPlayer and from the script var order):
//   TnLocalPlayer + 0x2c0 : CharacterCustomizationFilename (FString)
//   + 0x2cc,0x2d0,0x2d4   : _CustomCharacterData, _PlayerFileHandler, _XpManager (pointers)
//   + 0x2d8               : bool bit field: bit0 _HasBypassedIISOnce, bit1 _CharactersLoaded, ...
//   + 0x2dc               : CharacterLoadCompleteFunctionName (FString, 0x26 chars incl. terminator)
static DWORD WINAPI ForceCharThread(LPVOID) {
    BYTE* base = (BYTE*)GetModuleHandleW(nullptr);
    unsigned wantId = 0;
    int applied = 0;
    for (int round = 0; round < 600; round++) {
        Sleep(1000);
        __try {
            TArr* go = (TArr*)(base + kGObjRva);
            if (go->Num < 1000 || !go->Data) continue;
            if (!wantId) {
                for (int i = 0; i < go->Num; i++) {
                    BYTE* obj = (BYTE*)go->Data[i];
                    if (!obj || !Rd(obj, 0x88)) continue;
                    char nm[80]; unsigned num = 0;
                    if (!ObjNameStr(obj, nm, sizeof(nm), &num) || strcmp(nm, "TnLocalPlayer") != 0 || num != 0) continue;
                    if ((*(unsigned*)(obj + 0x14) & 0x1fff) != 3) continue;
                    wantId = *(unsigned*)(obj + 0x84) + 1;
                    Log("forcechar: TnLocalPlayer class id %u", wantId);
                    break;
                }
                if (!wantId) continue;
            }
            for (int i = 0; i < go->Num; i++) {
                BYTE* obj = (BYTE*)go->Data[i];
                if (!obj || !Rd(obj, 0x30)) continue;
                if ((*(unsigned*)(obj + 0x14) & 0x1fff) != wantId) continue;
                char nm[80]; unsigned num = 0;
                if (!ObjNameStr(obj, nm, sizeof(nm), &num) || num == 0) continue;   // skip Default__ (name differs) and class objects
                if (!Rd(obj, 0x300)) continue;
                if (*(int*)(obj + 0x2e0) != 0x26 || *(int*)(obj + 0x2e4) != 0x26) continue;   // layout sanity check
                DWORD* flags = (DWORD*)(obj + 0x2d8);
                if (!(*flags & 2)) {
                    *flags |= 2;
                    applied++;
                    Log("forcechar: instance %p name '%s'_%u: _CharactersLoaded set (flags now %x), round %d", obj, nm, num, *flags, round);
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    return 0;
}


// ---------------------------------------------------------------- GameInfo.MaxPlayers poke
// GameInfo instance layout in this build (from diffing instances against Default__ objects):
//   +0x230 MaxPlayers, +0x234 MaxPlayersAllowed (1/1 for TnCampaignGame, 4/4 for TnSurvivalGame), +0x238 NumPlayers,
//   +0x244 CurrentID. (+0x224/+0x228 are MaxSpectators/MaxSpectatorsAllowed.)
// FUN_006e00f0 (ghidra) = TnInputOverrideModuleTutorial refresh; its class lookup returns null in a coop campaign
// session and the host crashes (this=0x10). Turn it into a plain return.
static DWORD WINAPI SkipTutThread(LPVOID) {
    Sleep(3000);
    BYTE* fn = (BYTE*)GetModuleHandleW(nullptr) + 0x2e00f0;
    __try {
        if (fn[0] == 0x56 && fn[1] == 0x57 && fn[2] == 0x68) {
            DWORD old;
            if (VirtualProtect(fn, 1, PAGE_EXECUTE_READWRITE, &old)) { fn[0] = 0xC3; VirtualProtect(fn, 1, old, &old); Log("skiptut: patched %p", fn); }
        } else Log("skiptut: unexpected bytes %02x %02x %02x at %p", fn[0], fn[1], fn[2], fn);
    } __except (EXCEPTION_EXECUTE_HANDLER) { Log("skiptut: faulted"); }
    return 0;
}

// Remote PlayerControllers on the host never get their locally-created manager objects (input override, profile,
// achievements, ...). Native campaign code dereferences them (null+0x10 "this"). Copy valid UObject pointers from the
// local controller into null slots of every other TnPlayerController* instance.
static DWORD WINAPI CtrlShareThread(LPVOID) {
    BYTE* base = (BYTE*)GetModuleHandleW(nullptr);
    static BYTE* fixed[64]; static int nFixed = 0;
    for (int round = 0; round < 3000; round++) {
        Sleep(500);
        __try {
            TArr* go = (TArr*)(base + kGObjRva);
            if (go->Num < 1000 || !go->Data) continue;
            BYTE* pcs[16]; int npc = 0;
            for (int i = 0; i < go->Num && npc < 16; i++) {
                BYTE* obj = (BYTE*)go->Data[i];
                if (!obj || !Rd(obj, 0x30)) continue;
                char nm[80]; unsigned num = 0;
                if (!ObjNameStr(obj, nm, sizeof(nm), &num) || num == 0) continue;
                if (strncmp(nm, "TnPlayerController", 18) != 0) continue;
                if (!Rd(obj, 0x740)) continue;
                pcs[npc++] = obj;
            }
            g_playerCount = npc;
            { static int lastN = -1; if (npc != lastN) { lastN = npc; Log("pcdiag: controller count now %d", npc); } }
            if (npc < 1 || (npc < 2 && !g_ctrlDiag)) continue;
            static DWORD lastDiag = 0; if (g_ctrlDiag && GetTickCount() - lastDiag > 4000 && (lastDiag = GetTickCount(), true)) {
                Log("pcdiag: %d controllers", npc);
                for (int a = 0; a < npc; a++) {
                    char nm[80]; unsigned num = 0; ObjNameStr(pcs[a], nm, sizeof(nm), &num);
                    char line[1200]; size_t k = _snprintf_s(line, _TRUNCATE, "pcdiag: PC %s_%u %p refs:", nm, num - 1, pcs[a]);
                    for (int off = 0x100; off < 0x740; off += 4) {
                        BYTE* o = *(BYTE**)(pcs[a] + off);
                        if (!o || !Rd(o, 0x30)) continue;
                        unsigned idx = *(unsigned*)(o + 0x18) & 0x3ffff;
                        if (!(idx >= 1 && idx <= (unsigned)go->Num && (BYTE*)go->Data[idx - 1] == o)) continue;
                        char n2[80]; unsigned nn = 0; if (!ObjNameStr(o, n2, sizeof(n2), &nn)) continue;
                        if (strstr(n2, "Pawn") || strstr(n2, "Character") || strstr(n2, "Player") || strstr(n2, "Connection") || strstr(n2, "Hero")) {
                            if (k + 60 < sizeof(line)) k += _snprintf_s(line + k, sizeof(line) - k, _TRUNCATE, " %x=%s_%u", off, n2, nn - 1);
                        }
                    }
                    Log("%s", line);
                }
                if (npc == 2) {
                    // word diff between the local-player controller (host) and the remote one
                    int L = -1;
                    for (int a = 0; a < npc; a++) { BYTE* o = *(BYTE**)(pcs[a] + 0x344); char n2[80]; unsigned nn; if (o && Rd(o, 0x30) && ObjNameStr(o, n2, sizeof(n2), &nn) && !strncmp(n2, "TnLocalPlayer", 13)) L = a; }
                    if (L >= 0) {
                        BYTE* h = pcs[L]; BYTE* r = pcs[1 - L];
                        char line[1500]; size_t k = 0; int cnt = 0;
                        for (int off = 0x1d0; off < 0x740; off += 4) {
                            unsigned hv = *(unsigned*)(h + off), rv = *(unsigned*)(r + off);
                            if (hv == rv) continue;
                            bool hp = hv >= 0x01000000 && hv < 0x3e000000 && (hv & 3) == 0, rp = rv >= 0x01000000 && rv < 0x3e000000 && (rv & 3) == 0;
                            if (hp && rp) continue;   // both look like heap pointers
                            if (k + 30 > sizeof(line) - 40) { Log("pcdiff: host-vs-remote:%s", line); k = 0; line[0] = 0; }
                            k += _snprintf_s(line + k, sizeof(line) - k, _TRUNCATE, " %x:%x/%x", off, hv, rv); cnt++;
                        }
                        if (k) Log("pcdiff: host-vs-remote:%s", line);
                    }
                }
                for (int a = 0; a < npc; a++) {
                    for (int off = 0x100; off < 0x740; off += 4) {
                        BYTE* cam = *(BYTE**)(pcs[a] + off);
                        if (!cam || !Rd(cam, 0x30)) continue;
                        unsigned ci = *(unsigned*)(cam + 0x18) & 0x3ffff;
                        if (!(ci >= 1 && ci <= (unsigned)go->Num && (BYTE*)go->Data[ci - 1] == cam)) continue;
                        char cn[80]; unsigned cnum = 0; if (!ObjNameStr(cam, cn, sizeof(cn), &cnum) || cnum == 0) continue;
                        if (!strstr(cn, "PlayerCamera") && !strstr(cn, "CameraActor")) continue;
                        if (!Rd(cam, 0x600)) continue;
                        char line[1300]; size_t k = _snprintf_s(line, _TRUNCATE, "camdiag: PC%d +%x %s_%u %p refs:", a, off, cn, cnum - 1, cam);
                        for (int o2 = 0x100; o2 < 0x600; o2 += 4) {
                            BYTE* t = *(BYTE**)(cam + o2);
                            if (!t || !Rd(t, 0x30)) continue;
                            unsigned ti = *(unsigned*)(t + 0x18) & 0x3ffff;
                            if (!(ti >= 1 && ti <= (unsigned)go->Num && (BYTE*)go->Data[ti - 1] == t)) continue;
                            char tn[80]; unsigned tnum = 0; if (!ObjNameStr(t, tn, sizeof(tn), &tnum)) continue;
                            if (!strncmp(tn, "TnPlayer", 8) || strstr(tn, "Camera")) { if (k + 60 < sizeof(line)) k += _snprintf_s(line + k, sizeof(line) - k, _TRUNCATE, " %x=%s_%u", o2, tn, tnum - 1); }
                        }
                        Log("%s", line);
                    }
                }
                for (int a = 0; a < npc; a++) {
                    BYTE* pri = *(BYTE**)(pcs[a] + 0x1d4);
                    if (!pri || !Rd(pri, 0x900)) continue;
                    char line[1400]; size_t k = _snprintf_s(line, _TRUNCATE, "pridiag: PC%d pri %p strings:", a, pri);
                    for (int off = 0x200; off < 0x900; off += 4) {
                        wchar_t* d = *(wchar_t**)(pri + off); int num = *(int*)(pri + off + 4), mx = *(int*)(pri + off + 8);
                        if (num < 2 || num > 40 || mx < num || mx > 4096 || !d || !Rd(d, num * 2)) continue;
                        bool ok = d[num - 1] == 0; for (int q = 0; q < num - 1 && ok; q++) if (d[q] < 32 || d[q] > 126) ok = false;
                        if (!ok) continue;
                        char t[48]; int tn = 0; for (; tn < num - 1 && tn < 40; tn++) t[tn] = (char)d[tn]; t[tn] = 0;
                        if (k + 70 < sizeof(line)) k += _snprintf_s(line + k, sizeof(line) - k, _TRUNCATE, " %x='%s'(%d)", off, t, num);
                    }
                    Log("%s", line);
                }
                for (int i = 0; i < go->Num; i++) {
                    BYTE* obj = (BYTE*)go->Data[i];
                    if (!obj || !Rd(obj, 0x30)) continue;
                    char nm[80]; unsigned num = 0;
                    if (!ObjNameStr(obj, nm, sizeof(nm), &num) || num == 0 || strncmp(nm, "TnPlayerPawn", 12) != 0 || !Rd(obj, 0x400)) continue;
                    char line[1500]; size_t k = _snprintf_s(line, _TRUNCATE, "pawnloc: %s_%u %p:", nm, num - 1, obj);
                    for (int off = 0x60; off < 0x300; off += 4) {
                        float* f = (float*)(obj + off);
                        bool ok = true; for (int q = 0; q < 3; q++) if (!(f[q] > -2e6f && f[q] < 2e6f) || (f[q] != f[q])) ok = false;
                        if (!ok) continue;
                        if (fabsf(f[0]) < 1.0f && fabsf(f[1]) < 1.0f && fabsf(f[2]) < 1.0f) continue;
                        if (fabsf(f[0]) < 1e-3f && fabsf(f[1]) < 1e-3f) continue;
                        if (k + 70 < sizeof(line)) k += _snprintf_s(line + k, sizeof(line) - k, _TRUNCATE, " %x=(%.0f,%.0f,%.0f)", off, f[0], f[1], f[2]);
                    }
                    Log("%s", line);
                }
                // all pawn-like instances and their controller-ish refs
                int shown = 0;
                for (int i = 0; i < go->Num && shown < 12; i++) {
                    BYTE* obj = (BYTE*)go->Data[i];
                    if (!obj || !Rd(obj, 0x30)) continue;
                    char nm[80]; unsigned num = 0;
                    if (!ObjNameStr(obj, nm, sizeof(nm), &num) || num == 0) continue;
                    if (!strstr(nm, "Pawn") || strstr(nm, "Default__")) continue;
                    if (!strncmp(nm, "TnPawn", 6) == 0 && !strstr(nm, "Transformer") && !strstr(nm, "Pawn_")) {}
                    if (!Rd(obj, 0x600)) continue;
                    char line[900]; size_t k = _snprintf_s(line, _TRUNCATE, "pcdiag: PAWN %s_%u %p ctl-refs:", nm, num - 1, obj);
                    for (int off = 0x100; off < 0x600; off += 4) {
                        BYTE* o = *(BYTE**)(obj + off);
                        for (int a = 0; a < npc; a++) if (o == pcs[a] && k + 24 < sizeof(line)) k += _snprintf_s(line + k, sizeof(line) - k, _TRUNCATE, " %x=PC%d", off, a);
                    }
                    Log("%s", line); shown++;
                }
            }
            auto isObj = [&](BYTE* o) -> bool {
                if (!o || !Rd(o, 0x30)) return false;
                unsigned idx = *(unsigned*)(o + 0x18) & 0x3ffff;
                return idx >= 1 && idx <= (unsigned)go->Num && (BYTE*)go->Data[idx - 1] == o;
            };
            auto isObj0 = [&](BYTE* o) -> bool {
                if (!o || !Rd(o, 0x200)) return false;
                unsigned idx = *(unsigned*)(o + 0x18) & 0x3ffff;
                return idx >= 1 && idx <= (unsigned)go->Num && (BYTE*)go->Data[idx - 1] == o;
            };
            if (g_hostFirst && npc >= 2) {
                // Kismet "Player" = first PlayerController in WorldInfo.ControllerList, which is the newest one. Put the
                // local-player controller (host) at the head: list = host -> others -> none.
                int L = -1;
                for (int a = 0; a < npc; a++) { BYTE* o = *(BYTE**)(pcs[a] + 0x344); char n2[80]; unsigned nn; if (o && Rd(o, 0x30) && ObjNameStr(o, n2, sizeof(n2), &nn) && !strncmp(n2, "TnLocalPlayer", 13)) L = a; }
                { static int lastL = -9; if (L != lastL) { lastL = L; Log("hostfirst: local-player controller index %d of %d", L, npc); } }
                if (L >= 0) {
                    BYTE* host = pcs[L];
                    BYTE* wi = nullptr; int wiOff = -1;
                    for (int i = 0; i < go->Num && !wi; i++) {
                        BYTE* obj = (BYTE*)go->Data[i];
                        if (!obj || !Rd(obj, 0x30)) continue;
                        char n2[80]; unsigned nn = 0;
                        if ((*(unsigned*)(obj + 0x14) & 0x1fff) == 3 || !ObjNameStr(obj, n2, sizeof(n2), &nn) || !strstr(n2, "WorldInfo") || strstr(n2, "Default__") || !Rd(obj, 0xc00)) continue;
                        for (int off = 0x100; off < 0xc00 && !wi; off += 4) {
                            BYTE* v = *(BYTE**)(obj + off);
                            if (!isObj0(v)) continue;
                            int g2 = 0; for (BYTE* c = v; c && g2 < 3000 && isObj0(c); c = *(BYTE**)(c + 0x1dc), g2++) if (c == host) { wi = obj; wiOff = off; break; }
                        }
                    }
                    { static int lastSt = -9; int st = !wi ? -1 : (*(BYTE**)(wi + wiOff) == host ? 2 : 1); if (st != lastSt) { lastSt = st; Log("hostfirst: state %d (wi=%p off=%x)", st, wi, wiOff); } }
                    if (wi && *(BYTE**)(wi + wiOff) != host) {
                        // splice the host controller out of the chain and put it at the head
                        BYTE* head = *(BYTE**)(wi + wiOff);
                        BYTE* pred = nullptr; int guardN = 0;
                        for (BYTE* c = head; c && guardN < 4096 && Rd(c, 0x200); c = *(BYTE**)(c + 0x1dc), guardN++) if (*(BYTE**)(c + 0x1dc) == host) { pred = c; break; }
                        if (pred) {
                            Log("hostfirst: WorldInfo %p +%x head %p -> host %p", wi, wiOff, head, host);
                            *(BYTE**)(pred + 0x1dc) = *(BYTE**)(host + 0x1dc);
                            *(BYTE**)(host + 0x1dc) = head;
                            *(BYTE**)(wi + wiOff) = host;
                        }
                    }
                }
            }
            // reference = controller with the most valid object pointers in the range
            int best = -1, bestN = -1;
            for (int a = 0; a < npc; a++) { int n = 0; for (int off = 0x500; off < 0x740; off += 4) if (isObj(*(BYTE**)(pcs[a] + off))) n++; if (n > bestN) { bestN = n; best = a; } }
            for (int a = 0; a < npc; a++) {
                if (a == best) continue;
                int copied = 0;
                for (int off = 0x500; off < 0x740; off += 4) {
                    BYTE** dst = (BYTE**)(pcs[a] + off); BYTE* src = *(BYTE**)(pcs[best] + off);
                    if (*dst == nullptr && isObj(src)) {
                        char n2[80]; unsigned nn = 0; ObjNameStr(src, n2, sizeof(n2), &nn);
                        if (g_ctrlSkipMask && strstr(n2, g_ctrlSkipMask)) { static volatile LONG ns = 0; if (InterlockedIncrement(&ns) <= 6) Log("ctrlshare: skipping +%x %s_%u", off, n2, nn - 1); continue; }
                        static volatile LONG nc = 0; if (InterlockedIncrement(&nc) <= 40) Log("ctrlshare: +%x <- %s_%u", off, n2, nn - 1);
                        *dst = src; copied++;
                    }
                }
                if (copied) {
                    bool seen = false; for (int k = 0; k < nFixed; k++) if (fixed[k] == pcs[a]) seen = true;
                    if (!seen && nFixed < 64) fixed[nFixed++] = pcs[a];
                    static volatile LONG nl = 0; if (InterlockedIncrement(&nl) <= 20) Log("ctrlshare: %p <- %p copied %d null object slots", pcs[a], pcs[best], copied);
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) { static volatile LONG nx = 0; if (InterlockedIncrement(&nx) <= 3) Log("ctrlshare: faulted (round %d)", round); }
    }
    return 0;
}

// holdspawn: keep every remote controller's PRI _SelectedCharacter.CharacterName (PRI+0x34c FString) empty so the campaign
// spawn helper (CheckReadySpawn -> HasSelectedCharacter) does not spawn the guest yet; the guest keeps watching the host.
// Release = F8 in the host window, a file "foc_coop.release" next to the exe, or spawnafter=<s> seconds after the guest joined.
static DWORD WINAPI HoldSpawnThread(LPVOID) {
    BYTE* base = (BYTE*)GetModuleHandleW(nullptr);
    struct Held { BYTE* pri; int num; };
    static Held held[16]; static int nHeld = 0;
    wchar_t flag[MAX_PATH]; GetModuleFileNameW(nullptr, flag, MAX_PATH);
    wchar_t* sl = wcsrchr(flag, L'\\'); if (sl) wcscpy_s(sl + 1, 32, L"foc_coop.release");
    DWORD joined = 0; bool released = false;
    for (;;) {
        Sleep(5);
        __try {
            if (!released) {
                if ((GetAsyncKeyState(VK_F8) & 0x8000)) { Log("holdspawn: F8"); released = true; }
                else if (GetFileAttributesW(flag) != INVALID_FILE_ATTRIBUTES) { Log("holdspawn: release file"); DeleteFileW(flag); released = true; }
                else if (g_spawnAfter > 0 && joined && GetTickCount() - joined > (DWORD)g_spawnAfter * 1000) { Log("holdspawn: timeout"); released = true; }
            }
            // find controllers cheaply from the cached PRI pointers via a scan every 250 ms
            static DWORD lastScan = 0;
            if (GetTickCount() - lastScan > 250) {
                lastScan = GetTickCount();
                TArr* go = (TArr*)(base + kGObjRva);
                if (go->Num > 1000 && go->Data) {
                    for (int i = 0; i < go->Num; i++) {
                        BYTE* obj = (BYTE*)go->Data[i];
                        if (!obj || !Rd(obj, 0x30)) continue;
                        char nm[80]; unsigned num = 0;
                        if (!ObjNameStr(obj, nm, sizeof(nm), &num) || num == 0 || strncmp(nm, "TnPlayerController", 18) != 0 || !Rd(obj, 0x740)) continue;
                        BYTE* o = *(BYTE**)(obj + 0x344); char n2[80]; unsigned nn;
                        if (o && Rd(o, 0x30) && ObjNameStr(o, n2, sizeof(n2), &nn) && !strncmp(n2, "TnLocalPlayer", 13)) continue;   // the host itself
                        BYTE* pri = *(BYTE**)(obj + 0x1d4);
                        if (!pri || !Rd(pri, 0x900)) continue;
                        bool known = false; for (int k = 0; k < nHeld; k++) if (held[k].pri == pri) known = true;
                        if (!known && nHeld < 16) { held[nHeld].pri = pri; held[nHeld].num = 0; nHeld++; if (!joined) joined = GetTickCount(); Log("holdspawn: tracking guest PRI %p", pri); }
                    }
                }
            }
            for (int k = 0; k < nHeld; k++) {
                int* num = (int*)(held[k].pri + 0x350);
                if (!released) { if (*num > 1 && *num < 64) { held[k].num = *num; *num = 0; static volatile LONG nl = 0; if (InterlockedIncrement(&nl) <= 5) Log("holdspawn: blanked CharacterName of %p (was %d chars)", held[k].pri, held[k].num); } }
                else if (held[k].num > 1 && *num == 0) { *num = held[k].num; Log("holdspawn: restored CharacterName of %p", held[k].pri); held[k].num = 0; }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    return 0;
}

// rescue: when a player pawn has fallen far below another player's pawn (its streamed-out area no longer exists on the
// host), teleport it next to the other pawn. Pawn = PlayerController+0x1d0, Actor.Location = pawn+0xe4 (x,y,z floats).
static DWORD WINAPI RescueThread(LPVOID) {
    BYTE* base = (BYTE*)GetModuleHandleW(nullptr);
    BYTE* pcs[16]; int npc = 0; DWORD lastScan = 0, lastStat = 0;
    Log("rescue: thread started");
    for (;;) {
        Sleep(100);
        __try {
            TArr* go = (TArr*)(base + kGObjRva);
            if (go->Num < 1000 || !go->Data) continue;
            if (GetTickCount() - lastScan > 1000) {
                lastScan = GetTickCount(); npc = 0;
                for (int i = 0; i < go->Num && npc < 16; i++) {
                    BYTE* obj = (BYTE*)go->Data[i];
                    if (!obj || !Rd(obj, 0x30)) continue;
                    char nm[80]; unsigned num = 0;
                    if (!ObjNameStr(obj, nm, sizeof(nm), &num) || num == 0 || strncmp(nm, "TnPlayerController", 18) != 0 || !Rd(obj, 0x740)) continue;
                    pcs[npc++] = obj;
                }
            }
            if (npc < 2) continue;
            BYTE* pawns[16]; int np = 0;
            for (int a = 0; a < npc; a++) {
                if (!Rd(pcs[a], 0x1e0)) continue;
                BYTE* pw = *(BYTE**)(pcs[a] + 0x1d0);
                if (!pw || !Rd(pw, 0x400)) continue;
                unsigned idx = *(unsigned*)(pw + 0x18) & 0x3ffff;
                if (!(idx >= 1 && idx <= (unsigned)go->Num && (BYTE*)go->Data[idx - 1] == pw)) continue;
                pawns[np++] = pw;
            }
            if (GetTickCount() - lastStat > 5000) { lastStat = GetTickCount(); Log("rescue: stat npc=%d np=%d z0=%.0f z1=%.0f", npc, np, np > 0 ? ((float*)(pawns[0] + 0xe4))[2] : 0.f, np > 1 ? ((float*)(pawns[1] + 0xe4))[2] : 0.f); }
            if (np < 2) continue;
            int hi = 0; for (int a = 1; a < np; a++) if (((float*)(pawns[a] + 0xe4))[2] > ((float*)(pawns[hi] + 0xe4))[2]) hi = a;
            float* ref = (float*)(pawns[hi] + 0xe4);
            for (int a = 0; a < np; a++) {
                if (a == hi) continue;
                float* loc = (float*)(pawns[a] + 0xe4);
                if (ref[2] - loc[2] > 4000.0f && ref[2] > -20000.0f) {
                    static volatile LONG nl = 0; if (InterlockedIncrement(&nl) <= 20) Log("rescue: pawn %p fell to z=%.0f, moving next to %p (%.0f,%.0f,%.0f)", pawns[a], loc[2], pawns[hi], ref[0], ref[1], ref[2]);
                    float nx = ref[0] + 250.0f * (a + 1), ny = ref[1] + 60.0f, nz = ref[2] + 120.0f;
                    loc[0] = nx; loc[1] = ny; loc[2] = nz;
                    float* loc2 = (float*)(pawns[a] + 0x134); loc2[0] = nx; loc2[1] = ny; loc2[2] = nz;
                    // clear a plausible Velocity so the fall does not continue: scan the pawn for the old fall speed is not possible; leave it.
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    return 0;
}

static DWORD WINAPI MaxPlayersThread(LPVOID) {
    BYTE* base = (BYTE*)GetModuleHandleW(nullptr);
    static const char* classes[] = {"TnCampaignGame", "TnCampaignGameSolo", "TnSurvivalGame", "TnTeamGame", "TnMultiplayerGame", "TnFreeForAllGame", "TnLobbyGame"};
    unsigned ids[8] = {0};
    for (int round = 0; round < 900; round++) {
        Sleep(1000);
        __try {
            TArr* go = (TArr*)(base + kGObjRva);
            if (go->Num < 1000 || !go->Data) continue;
            bool need = false;
            for (int q = 0; q < 7; q++) if (!ids[q]) need = true;
            if (need && (round % 5 == 0)) {
                for (int i = 0; i < go->Num; i++) {
                    BYTE* obj = (BYTE*)go->Data[i];
                    if (!obj || !Rd(obj, 0x88)) continue;
                    if ((*(unsigned*)(obj + 0x14) & 0x1fff) != 3) continue;
                    char nm[80]; unsigned num = 0;
                    if (!ObjNameStr(obj, nm, sizeof(nm), &num) || num != 0) continue;
                    for (int q = 0; q < 7; q++) if (!ids[q] && strcmp(nm, classes[q]) == 0) { unsigned own = *(unsigned*)(obj + 0x84); if (own > 0 && own < 8191) ids[q] = own + 1; }
                }
            }
            for (int i = 0; i < go->Num; i++) {
                BYTE* obj = (BYTE*)go->Data[i];
                if (!obj || !Rd(obj, 0x30)) continue;
                unsigned cid = *(unsigned*)(obj + 0x14) & 0x1fff;
                int q = -1; for (int k = 0; k < 7; k++) if (ids[k] && ids[k] == cid) q = k;
                if (q < 0) continue;
                char nm[80]; unsigned num = 0;
                if (!ObjNameStr(obj, nm, sizeof(nm), &num) || num == 0) continue;       // skip class + Default__ objects
                if (!Rd(obj, 0x300)) continue;
                int* mp = (int*)(obj + 0x230); int* mpa = (int*)(obj + 0x234);
                if (*mpa < 1 || *mpa > 64 || *mp < 0 || *mp > 64) continue;           // sanity
                if (*mpa < g_maxPlayers && *mp >= g_maxPlayers) continue;
                if (*mp < g_maxPlayers) {
                    Log("maxplayers: %s_%u at %p: MaxPlayers %d (allowed %d) -> %d", nm, num - 1, obj, *mp, *mpa, g_maxPlayers);
                    *mp = g_maxPlayers;
                    if (*mpa < g_maxPlayers) *mpa = g_maxPlayers;
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    return 0;
}


// ---------------------------------------------------------------- generic pokes: poke1..poke8 = "Class,offsetHex,valueHex,maskHex"
static DWORD WINAPI PokeThread(LPVOID) {
    BYTE* base = (BYTE*)GetModuleHandleW(nullptr);
    for (int round = 0; round < 1200; round++) {
        Sleep(1000);
        __try {
            TArr* go = (TArr*)(base + kGObjRva);
            if (go->Num < 1000 || !go->Data) continue;
            bool need = false;
            for (auto& p : g_pokes) if (p.used && !p.id) need = true;
            if (need && round % 5 == 0) {
                for (int i = 0; i < go->Num; i++) {
                    BYTE* obj = (BYTE*)go->Data[i];
                    if (!obj || !Rd(obj, 0x88) || (*(unsigned*)(obj + 0x14) & 0x1fff) != 3) continue;
                    char nm[80]; unsigned num = 0;
                    if (!ObjNameStr(obj, nm, sizeof(nm), &num) || num != 0) continue;
                    for (auto& p : g_pokes) if (p.used && !p.id && strcmp(nm, p.cls) == 0) { unsigned own = *(unsigned*)(obj + 0x84); if (own > 0 && own < 8191) p.id = own + 1; }
                }
            }
            for (int i = 0; i < go->Num; i++) {
                BYTE* obj = (BYTE*)go->Data[i];
                if (!obj || !Rd(obj, 0x30)) continue;
                unsigned cid = *(unsigned*)(obj + 0x14) & 0x1fff;
                for (auto& p : g_pokes) {
                    if (!p.used || p.id != cid) continue;
                    char nm[80]; unsigned num = 0;
                    if (!ObjNameStr(obj, nm, sizeof(nm), &num) || num == 0) continue;
                    if (!Rd(obj, p.off + 4)) continue;
                    unsigned* w = (unsigned*)(obj + p.off);
                    unsigned nv = (*w & ~p.mask) | (p.val & p.mask);
                    if (nv != *w) { Log("poke: %s_%u +0x%x: %x -> %x", nm, num - 1, p.off, *w, nv); *w = nv; }
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    return 0;
}

static void InstallHooks() {
    o_ODSA = (PFN_ODSA)Resolve("kernel32.dll", "OutputDebugStringA");
    o_ODSW = (PFN_ODSW)Resolve("kernel32.dll", "OutputDebugStringW");
    o_CMA = (PFN_CMA)Resolve("kernel32.dll", "CreateMutexA");
    o_CMW = (PFN_CMW)Resolve("kernel32.dll", "CreateMutexW");
    o_sendto = (PFN_SENDTO)Resolve("ws2_32.dll", "sendto");
    o_recvfrom = (PFN_RECVFROM)Resolve("ws2_32.dll", "recvfrom");
    int a = PatchIat((void*)o_ODSA, (void*)h_ODSA);
    int b = PatchIat((void*)o_ODSW, (void*)h_ODSW);
    int c = PatchIat((void*)o_CMA, (void*)h_CMA);
    int d = PatchIat((void*)o_CMW, (void*)h_CMW);
    // wsock32 forwards sendto/recvfrom to ws2_32, so the exe's wsock32 entries may point at the same addresses
    int e = PatchIat((void*)o_sendto, (void*)h_sendto);
    int f = PatchIat((void*)o_recvfrom, (void*)h_recvfrom);
    void* ws32_sendto = Resolve("wsock32.dll", "sendto");
    void* ws32_recvfrom = Resolve("wsock32.dll", "recvfrom");
    int g = 0, h = 0;
    if (ws32_sendto && ws32_sendto != (void*)o_sendto) { g = PatchIat(ws32_sendto, (void*)h_sendto); }
    if (ws32_recvfrom && ws32_recvfrom != (void*)o_recvfrom) { h = PatchIat(ws32_recvfrom, (void*)h_recvfrom); }
    int hb = PatchBoth("bind", (void**)&o_bind, (void*)h_bind);
    int hs = PatchBoth("socket", (void**)&o_socket, (void*)h_socket);
    int hc = PatchBoth("closesocket", (void**)&o_close, (void*)h_closesocket);
    int hr = PatchBoth("recv", (void**)&o_recv, (void*)h_recv);
    int hn = PatchBoth("send", (void**)&o_send, (void*)h_send);
    PatchBoth("connect", (void**)&o_connect, (void*)h_connect);
    Log("IAT patched more: bind=%d socket=%d closesocket=%d recv=%d send=%d", hb, hs, hc, hr, hn);
    CreateThread(nullptr, 0, StatsThread, nullptr, 0, nullptr);
    if (g_tcpDriver) CreateThread(nullptr, 0, DelayedNetDriverPatch, nullptr, 0, nullptr);
    if (g_dump) CreateThread(nullptr, 0, DumpThread, nullptr, 0, nullptr);
    if (g_scan) CreateThread(nullptr, 0, ScanThread, nullptr, 0, nullptr);
    if (g_trace) CreateThread(nullptr, 0, TraceThread, nullptr, 0, nullptr);
    if (g_patchCap) CreateThread(nullptr, 0, ScriptPatchThread, nullptr, 0, nullptr);
    if (g_cfg) CreateThread(nullptr, 0, CfgThread, nullptr, 0, nullptr);
    if (g_natives) CreateThread(nullptr, 0, NativesThread, nullptr, 0, nullptr);
    if (g_lp) CreateThread(nullptr, 0, LocalPlayerThread, nullptr, 0, nullptr);
    if (g_instClass[0]) CreateThread(nullptr, 0, InstThread, nullptr, 0, nullptr);
    if (g_charFix) CreateThread(nullptr, 0, CharFixThread, nullptr, 0, nullptr);
    if (g_engInfo) CreateThread(nullptr, 0, EngineThread, nullptr, 0, nullptr);
    if (g_findCls[0]) CreateThread(nullptr, 0, FindClsThread, nullptr, 0, nullptr);
    if (g_forceCharFlag) CreateThread(nullptr, 0, ForceCharThread, nullptr, 0, nullptr);
    if (g_maxPlayers > 0) CreateThread(nullptr, 0, MaxPlayersThread, nullptr, 0, nullptr);
    if (g_skipTut) CreateThread(nullptr, 0, SkipTutThread, nullptr, 0, nullptr);
    if (g_ctrlShare) CreateThread(nullptr, 0, CtrlShareThread, nullptr, 0, nullptr);
    if (g_holdSpawn) CreateThread(nullptr, 0, HoldSpawnThread, nullptr, 0, nullptr);
    if (g_rescue) CreateThread(nullptr, 0, RescueThread, nullptr, 0, nullptr);
    { bool any = false; for (auto& p : g_pokes) if (p.used) any = true; if (any) CreateThread(nullptr, 0, PokeThread, nullptr, 0, nullptr); }
    if (g_snap1 > 0 || g_snap2 > 0) CreateThread(nullptr, 0, SnapThread, nullptr, 0, nullptr);
    Log("IAT patched: ODSA=%d ODSW=%d CMA=%d CMW=%d sendto=%d recvfrom=%d wsock32 sendto=%d recvfrom=%d", a, b, c, d, e, f, g, h);
}

static LONG CALLBACK VehHandler(EXCEPTION_POINTERS* ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    // null-this guard: ghidra FUN_00c2b010 (input-binding lookup) is called on a null object in a coop campaign.
    // Fault is at +6 after 4 pushes (ebx,ebp,esi,edi); emulate "xor eax,eax; pop x4; ret 8".
    if ((g_skipTut || g_tutGuard) && code == EXCEPTION_ACCESS_VIOLATION && ep->ContextRecord && ep->ContextRecord->Ecx == 0 &&
        (BYTE*)ep->ExceptionRecord->ExceptionAddress == (BYTE*)GetModuleHandleW(nullptr) + 0x82b016) {
        CONTEXT* c = ep->ContextRecord;
        DWORD* sp = (DWORD*)c->Esp;
        c->Edi = sp[0]; c->Esi = sp[1]; c->Ebp = sp[2]; c->Ebx = sp[3];
        c->Eip = sp[4]; c->Esp += 16 + 4 + 8; c->Eax = 0;
        static volatile LONG nG = 0; if (InterlockedIncrement(&nG) <= 3) Log("guard: null-this in input lookup bypassed");
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    // FUN_0093ca70 (ghidra; GetDefaultObject-like, returns 0x10 == "none") called with this=0x10: emulate returning 0x10.
    // Fault is at +6 after "sub esp,18; push esi"; stack: [esp]=esi, [esp+0x1c]=ret, [esp+0x20]=arg (ret 4).
    if (g_tutGuard && code == EXCEPTION_ACCESS_VIOLATION && ep->ContextRecord && ep->ContextRecord->Esi == 0x10 &&
        (BYTE*)ep->ExceptionRecord->ExceptionAddress == (BYTE*)GetModuleHandleW(nullptr) + 0x53ca76) {
        CONTEXT* c = ep->ContextRecord;
        DWORD* sp = (DWORD*)c->Esp;
        c->Esi = sp[0]; c->Eip = sp[7]; c->Esp += 0x1c + 4 + 4; c->Eax = 0x10;
        static volatile LONG nG2 = 0; if (InterlockedIncrement(&nG2) <= 3) Log("guard: getdefault(null) returned none");
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    // log only serious exceptions
    if (code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_ILLEGAL_INSTRUCTION || code == EXCEPTION_STACK_OVERFLOW || code == EXCEPTION_INT_DIVIDE_BY_ZERO) {
        void* addr = ep->ExceptionRecord->ExceptionAddress;
        HMODULE m = nullptr;
        char name[MAX_PATH] = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)addr, &m)) GetModuleFileNameA(m, name, MAX_PATH);
        Log("EXCEPTION %08lx at %p in %s (base %p) info0=%p info1=%p", code, addr, name, m, (void*)ep->ExceptionRecord->ExceptionInformation[0], (void*)ep->ExceptionRecord->ExceptionInformation[1]);
        if (code == EXCEPTION_ACCESS_VIOLATION && ep->ContextRecord) {
            CONTEXT* c = ep->ContextRecord;
            Log("REGS eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx ebp=%08lx esp=%08lx", c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
            BYTE* base = (BYTE*)GetModuleHandleW(nullptr);
            DWORD* sp = (DWORD*)c->Esp;
            char line[3000]; size_t k = _snprintf_s(line, _TRUNCATE, "CRASHSTACK:");
            int n = 0;
            __try {
                for (int i = 0; i < 4096 && n < 50; i++) {
                    BYTE* v = (BYTE*)sp[i];
                    if (v < base + 0x1000 || v >= base + 0x1000 + 0xf5fc71) continue;
                    bool call = (v[-5] == 0xE8) || (v[-2] == 0xFF && (v[-1] & 0x38) == 0x10) || (v[-3] == 0xFF && (v[-2] & 0x38) == 0x10) || (v[-6] == 0xFF && (v[-5] & 0x38) == 0x10);
                    if (!call) continue;
                    if (k + 24 < sizeof(line)) k += _snprintf_s(line + k, sizeof(line) - k, _TRUNCATE, " %d:%lx", i, (unsigned long)(0x470000 + (v - base)));
                    n++;
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
            Log("%s", line);
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static void LoadConfig(const wchar_t* dir) {
    char suffix[64] = {0};
    if (GetEnvironmentVariableA("FOC_INSTANCE", suffix, sizeof(suffix)) == 0) {
        wchar_t ini[MAX_PATH];
        _snwprintf_s(ini, _TRUNCATE, L"%s\\foc_coop.ini", dir);
        wchar_t w[64] = {0};
        GetPrivateProfileStringW(L"foc_coop", L"instance", L"", w, 64, ini);
        WideCharToMultiByte(CP_ACP, 0, w, -1, suffix, sizeof(suffix), nullptr, nullptr);
    }
    strncpy_s(g_suffix, suffix, _TRUNCATE);
    {
        wchar_t ini2[MAX_PATH];
        _snwprintf_s(ini2, _TRUNCATE, L"%s\\foc_coop.ini", dir);
        g_tcpDriver = GetPrivateProfileIntW(L"foc_coop", L"tcpdriver", 0, ini2) != 0;
        g_dump = GetPrivateProfileIntW(L"foc_coop", L"dump", 0, ini2) != 0;
        g_scan = GetPrivateProfileIntW(L"foc_coop", L"scan", 0, ini2) != 0;
        g_scanDelayMs = GetPrivateProfileIntW(L"foc_coop", L"scandelay", 20000, ini2);
        g_trace = GetPrivateProfileIntW(L"foc_coop", L"trace", 0, ini2) != 0;
        g_patchCap = GetPrivateProfileIntW(L"foc_coop", L"patchcap", 0, ini2) != 0;
        g_cfg = GetPrivateProfileIntW(L"foc_coop", L"cfg", 0, ini2) != 0;
        g_natives = GetPrivateProfileIntW(L"foc_coop", L"natives", 0, ini2) != 0;
        g_lpFlag = GetPrivateProfileIntW(L"foc_coop", L"lp", 0, ini2) != 0;
        for (int q = 0; q < 8; q++) {
            wchar_t key[16], w[200] = {0}; _snwprintf_s(key, _TRUNCATE, L"poke%d", q + 1);
            GetPrivateProfileStringW(L"foc_coop", key, L"", w, 200, ini2);
            if (!w[0]) continue;
            char a[200]; WideCharToMultiByte(CP_ACP, 0, w, -1, a, sizeof(a), nullptr, nullptr);
            char* c1 = strchr(a, ','); if (!c1) continue; *c1++ = 0;
            char* c2 = strchr(c1, ','); if (!c2) continue; *c2++ = 0;
            char* c3 = strchr(c2, ','); if (!c3) continue; *c3++ = 0;
            strncpy_s(g_pokes[q].cls, a, _TRUNCATE);
            g_pokes[q].off = strtoul(c1, nullptr, 16); g_pokes[q].val = strtoul(c2, nullptr, 16); g_pokes[q].mask = strtoul(c3, nullptr, 16);
            g_pokes[q].used = true; g_pokes[q].id = 0;
        }
        g_traceLenFlag = GetPrivateProfileIntW(L"foc_coop", L"tracelen", 0, ini2);
        g_maxPlayers = GetPrivateProfileIntW(L"foc_coop", L"maxplayers", 0, ini2);
        g_forceCharFlag = GetPrivateProfileIntW(L"foc_coop", L"forcechar", 0, ini2) != 0;
        g_skipTut = GetPrivateProfileIntW(L"foc_coop", L"skiptut", 0, ini2) != 0;
        g_ctrlShare = GetPrivateProfileIntW(L"foc_coop", L"ctrlshare", 0, ini2) != 0;
        g_tutGuard = GetPrivateProfileIntW(L"foc_coop", L"tutguard", 0, ini2) != 0;
        g_hostFirst = GetPrivateProfileIntW(L"foc_coop", L"hostfirst", 0, ini2) != 0;
        g_holdSpawn = GetPrivateProfileIntW(L"foc_coop", L"holdspawn", 0, ini2) != 0;
        g_rescue = GetPrivateProfileIntW(L"foc_coop", L"rescue", 0, ini2) != 0;
        g_spawnAfter = GetPrivateProfileIntW(L"foc_coop", L"spawnafter", 0, ini2);
        g_ctrlDiag = GetPrivateProfileIntW(L"foc_coop", L"ctrldiag", 0, ini2) != 0;
        { wchar_t w[64] = {0}; GetPrivateProfileStringW(L"foc_coop", L"ctrlskip", L"", w, 64, ini2); WideCharToMultiByte(CP_ACP, 0, w, -1, g_ctrlSkipMaskBuf, sizeof(g_ctrlSkipMaskBuf), nullptr, nullptr); if (g_ctrlSkipMaskBuf[0]) g_ctrlSkipMask = g_ctrlSkipMaskBuf; }
        { wchar_t w[64] = {0}; GetPrivateProfileStringW(L"foc_coop", L"findcls", L"", w, 64, ini2); WideCharToMultiByte(CP_ACP, 0, w, -1, g_findCls, sizeof(g_findCls), nullptr, nullptr); }
        g_engInfo = GetPrivateProfileIntW(L"foc_coop", L"enginfo", 0, ini2) != 0;
        g_snap1 = GetPrivateProfileIntW(L"foc_coop", L"snap1", 0, ini2);
        g_snap2 = GetPrivateProfileIntW(L"foc_coop", L"snap2", 0, ini2);
        g_charFixFlag = GetPrivateProfileIntW(L"foc_coop", L"charfix", 0, ini2) != 0;
        if (GetFileAttributesW(ini2) == INVALID_FILE_ATTRIBUTES) {   // no ini: sensible co-op defaults for players
            g_tcpDriver = g_forceCharFlag = g_tutGuard = g_ctrlShare = g_hostFirst = g_rescue = true;
            g_maxPlayers = 5;
        }
        { wchar_t w[64] = {0}; GetPrivateProfileStringW(L"foc_coop", L"instclass", L"", w, 64, ini2); WideCharToMultiByte(CP_ACP, 0, w, -1, g_instClass, sizeof(g_instClass), nullptr, nullptr); }
    }
}

#include "overlay.inc"

// ---------------------------------------------------------------- proxy export
typedef HRESULT(WINAPI* PFN_DI8Create)(HINSTANCE, DWORD, REFIID, LPVOID*, LPVOID);

extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE h, DWORD ver, REFIID riid, LPVOID* out, LPVOID unk) {
    static PFN_DI8Create real = nullptr;
    if (!real && g_real) real = (PFN_DI8Create)GetProcAddress(g_real, "DirectInput8Create");
    if (!real) return E_FAIL;
    HRESULT hr = real(h, ver, riid, out, unk);
    if (SUCCEEDED(hr) && out && *out) HookDirectInput(*out);
    return hr;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        InitializeCriticalSection(&g_cs);
        g_t0 = GetTickCount();
        wchar_t sys[MAX_PATH];
        GetSystemDirectoryW(sys, MAX_PATH);
        wcscat_s(sys, L"\\dinput8.dll");
        g_real = LoadLibraryW(sys);

        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        wchar_t* slash = wcsrchr(exe, L'\\');
        if (slash) *slash = 0;
        LoadConfig(exe);
        wchar_t logp[MAX_PATH];
        _snwprintf_s(logp, _TRUNCATE, L"%s\\foc_coop.log", exe);
        g_log = _wfsopen(logp, L"w", _SH_DENYNO);
        Log("foc_coop proxy attached, pid=%lu, instance suffix='%s', real dinput8=%p", GetCurrentProcessId(), g_suffix, g_real);
        Log("cmdline: %ls", GetCommandLineW());
        AddVectoredExceptionHandler(1, VehHandler);
        InstallHooks();
        OverlayStart();
    }
    return TRUE;
}
