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

static void hexdump(const char* p, int n, char* out, size_t outsz) {
    size_t o = 0;
    for (int i = 0; i < n && o + 3 < outsz; i++) o += _snprintf_s(out + o, outsz - o, _TRUNCATE, "%02x", (unsigned char)p[i]);
    out[o] = 0;
}

static int WSAAPI h_sendto(SOCKET s, const char* buf, int len, int flags, const sockaddr* to, int tolen) {
    int r = o_sendto(s, buf, len, flags, to, tolen);
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

static int WSAAPI h_bind(SOCKET s, const sockaddr* a, int len) {
    int r = o_bind(s, a, len);
    int e = WSAGetLastError();
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
static BYTE* MakeTramp(int n, void* orig) {
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
    b({0xB8}); d((unsigned long)(UINT_PTR)&OnSlotHit);       // mov eax, OnSlotHit
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
    Log("IAT patched more: bind=%d socket=%d closesocket=%d recv=%d send=%d", hb, hs, hc, hr, hn);
    CreateThread(nullptr, 0, StatsThread, nullptr, 0, nullptr);
    if (g_tcpDriver) CreateThread(nullptr, 0, DelayedNetDriverPatch, nullptr, 0, nullptr);
    if (g_dump) CreateThread(nullptr, 0, DumpThread, nullptr, 0, nullptr);
    if (g_scan) CreateThread(nullptr, 0, ScanThread, nullptr, 0, nullptr);
    if (g_trace) CreateThread(nullptr, 0, TraceThread, nullptr, 0, nullptr);
    if (g_patchCap) CreateThread(nullptr, 0, ScriptPatchThread, nullptr, 0, nullptr);
    if (g_cfg) CreateThread(nullptr, 0, CfgThread, nullptr, 0, nullptr);
    Log("IAT patched: ODSA=%d ODSW=%d CMA=%d CMW=%d sendto=%d recvfrom=%d wsock32 sendto=%d recvfrom=%d", a, b, c, d, e, f, g, h);
}

static LONG CALLBACK VehHandler(EXCEPTION_POINTERS* ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    // log only serious exceptions
    if (code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_ILLEGAL_INSTRUCTION || code == EXCEPTION_STACK_OVERFLOW || code == EXCEPTION_INT_DIVIDE_BY_ZERO) {
        void* addr = ep->ExceptionRecord->ExceptionAddress;
        HMODULE m = nullptr;
        char name[MAX_PATH] = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)addr, &m)) GetModuleFileNameA(m, name, MAX_PATH);
        Log("EXCEPTION %08lx at %p in %s (base %p) info0=%p info1=%p", code, addr, name, m, (void*)ep->ExceptionRecord->ExceptionInformation[0], (void*)ep->ExceptionRecord->ExceptionInformation[1]);
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
    }
}

// ---------------------------------------------------------------- proxy export
typedef HRESULT(WINAPI* PFN_DI8Create)(HINSTANCE, DWORD, REFIID, LPVOID*, LPVOID);

extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE h, DWORD ver, REFIID riid, LPVOID* out, LPVOID unk) {
    static PFN_DI8Create real = nullptr;
    if (!real && g_real) real = (PFN_DI8Create)GetProcAddress(g_real, "DirectInput8Create");
    if (!real) return E_FAIL;
    return real(h, ver, riid, out, unk);
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
    }
    return TRUE;
}
