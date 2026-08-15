// Windows half of the platform shim. See nwn_platform.h for the strategy.
#ifdef _WIN32

#include "nwn_platform.h"
#include <cstdio>
#include <cstring>

// Redirect stderr BEFORE anything else in this DLL runs.
// mingw runs C++ constructors during DLL_PROCESS_ATTACH *before* DllMain, so
// the injector's own constructor logged its entire startup report -- symbol
// resolution, hook installation -- while stderr still pointed nowhere. Worse,
// DllMain then reopened the log with "w" and truncated whatever survived.
// A constructor with an explicit low priority runs ahead of the injector's
// (which uses the default priority), so the log captures everything.
__attribute__((constructor(101)))
static void nwn_win_early_log_init() {
    // OPT-IN on Windows. This is the shipping build: it should leave nothing
    // beside the user's nwmain.exe unless someone is debugging. Set
    // NWN_SHADOWMAP_LOG=1 to get the log back. (Linux always logs -- it is the
    // development build and its launchers already redirect to a file.)
    //
    // GetEnvironmentVariableA, not getenv: this runs from an early constructor,
    // and the CRT environment is not reliably usable that early -- seeding it
    // with _putenv_s here once killed startup outright (see win/README.md).
    // SHIPPING DEFAULT: no log unless NWN_SHADOWMAP_LOG=1. Set this to 1 for a
    // debug DLL when something needs diagnosing on a machine that cannot set
    // environment variables easily.
    #define NWN_SHADOWMAP_WIN_LOG_DEFAULT 0   // SHIPPING. 1 = debug DLL.
    char want[8] = {};
    const DWORD got = GetEnvironmentVariableA("NWN_SHADOWMAP_LOG", want, sizeof(want));
    const bool wantLog = (got == 0) ? (NWN_SHADOWMAP_WIN_LOG_DEFAULT != 0)
                                    : (want[0] != '0');
    if (!wantLog) return;           // no redirect: stderr goes nowhere, no file

    char path[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (char* slash = strrchr(path, '\\')) slash[1] = '\0';
    strncat(path, "nwn_shadowmap_win.log", sizeof(path) - strlen(path) - 1);
    freopen(path, "w", stderr);
    // UNBUFFERED, not line-buffered. With line buffering a crash mid-line
    // leaves a truncated entry and it is ambiguous whether the fprintf itself
    // faulted or something after it did -- that ambiguity cost several test
    // runs. Unbuffered means the last line in the log is exactly the last
    // statement that completed.
    setvbuf(stderr, nullptr, _IONBF, 0);
    // PID and build stamp on the very first line. NWN's own exolog writes to
    // this same stderr, and more than one process can attach the proxy, so
    // without these it is impossible to tell "the injector stopped early" from
    // "a second process truncated the file" or "this log is from the previous
    // build" -- all three happened while bringing this up.
    fprintf(stderr, "[shadowmap][win] log opened (early constructor) pid=%lu build=%s %s\n",
            (unsigned long)GetCurrentProcessId(), __DATE__, __TIME__);
}

// ---------------------------------------------------------------------------
//  dlsym shim
// ---------------------------------------------------------------------------
void* nwn_win_dlsym(void* /*handle*/, const char* name) {
    if (!name) return nullptr;
    // GL first: opengl32 exports the 1.1 set, wglGetProcAddress everything
    // newer. Order matters -- wglGetProcAddress returns null for 1.1 names on
    // some drivers, and opengl32 has no entry for the modern ones.
    static HMODULE gl = nullptr;
    if (!gl) gl = GetModuleHandleA("opengl32.dll");
    if (!gl) gl = LoadLibraryA("opengl32.dll");
    if (gl) {
        if (void* p = (void*)GetProcAddress(gl, name)) return p;
        using wglGetProcAddress_t = PROC (WINAPI*)(LPCSTR);
        static wglGetProcAddress_t wgl =
            // Via void*: GetProcAddress returns FARPROC, and casting a
            // function pointer straight to another signature trips
            // -Wcast-function-type. The two-step cast is the documented
            // Windows idiom and keeps the build warning-clean, so a real
            // warning is never lost in the noise of an expected one.
            (wglGetProcAddress_t)(void*)GetProcAddress(gl, "wglGetProcAddress");
        // Only valid with a current context; returns null harmlessly otherwise.
        if (wgl) { if (void* p = (void*)wgl(name)) return p; }
    }
    // Engine/SDL names live in the executable itself.
    if (void* p = (void*)GetProcAddress(GetModuleHandleW(nullptr), name)) return p;
    return nullptr;
}

void* nwn_win_resolve(const char* winMangledName) {
    return (void*)GetProcAddress(GetModuleHandleW(nullptr), winMangledName);
}

// ---------------------------------------------------------------------------
//  mprotect / sysconf
// ---------------------------------------------------------------------------
int nwn_win_mprotect(void* addr, size_t len, int prot) {
    DWORD want = PAGE_READONLY;
    if (prot & PROT_WRITE) want = PAGE_READWRITE;
    else if (prot & PROT_READ) want = PAGE_READONLY;
    DWORD old = 0;
    return VirtualProtect(addr, len, want, &old) ? 0 : -1;
}

long nwn_win_sysconf(int /*name*/) {
    SYSTEM_INFO si; GetSystemInfo(&si);
    return (long)si.dwPageSize;
}

// ---------------------------------------------------------------------------
//  IAT patching
// ---------------------------------------------------------------------------
bool nwn_win_patch_iat(const char* dllName, const char* funcName,
                       void* replacement, void** originalOut) {
    auto base = (BYTE*)GetModuleHandleW(nullptr);
    auto dos  = (IMAGE_DOS_HEADER*)base;
    if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return false;

    for (auto imp = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress);
         imp->Name; ++imp) {
        if (_stricmp((const char*)(base + imp->Name), dllName) != 0) continue;
        auto thunk = (IMAGE_THUNK_DATA*)(base + imp->FirstThunk);
        auto orig  = (IMAGE_THUNK_DATA*)(base + (imp->OriginalFirstThunk
                                                 ? imp->OriginalFirstThunk
                                                 : imp->FirstThunk));
        for (; orig->u1.AddressOfData; ++orig, ++thunk) {
            if (IMAGE_SNAP_BY_ORDINAL(orig->u1.Ordinal)) continue;
            auto ibn = (IMAGE_IMPORT_BY_NAME*)(base + orig->u1.AddressOfData);
            if (strcmp((const char*)ibn->Name, funcName) != 0) continue;
            DWORD old = 0;
            if (!VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_READWRITE, &old))
                return false;
            if (originalOut) *originalOut = (void*)thunk->u1.Function;
            thunk->u1.Function = (ULONGLONG)replacement;
            VirtualProtect(&thunk->u1.Function, sizeof(void*), old, &old);
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
//  wglGetProcAddress hook -- the Windows stand-in for the Linux __glew* patch.
//  nwmain.exe exports no __glew* pointers, so there is nothing to patch after
//  the fact; instead we hand the engine our wrapper AT RESOLVE TIME and keep
//  the driver's real pointer for ourselves.
// ---------------------------------------------------------------------------
namespace {
struct GlWrapper { const char* name; void* wrapper; void** realSlot; };
GlWrapper g_wrappers[32];
int       g_wrapperCount = 0;

using wglGetProcAddress_t = PROC (WINAPI*)(LPCSTR);
wglGetProcAddress_t g_realWgl = nullptr;
bool g_hooksInstalled = false;
}

void nwn_win_register_gl_wrapper(const char* name, void* wrapper, void** realSlot) {
    if (g_wrapperCount >= (int)(sizeof(g_wrappers)/sizeof(g_wrappers[0]))) return;
    g_wrappers[g_wrapperCount++] = { name, wrapper, realSlot };
}

extern "C" PROC WINAPI nwn_win_wglGetProcAddress(LPCSTR name) {
    PROC real = g_realWgl ? g_realWgl(name) : nullptr;
    if (!name) return real;
    for (int i = 0; i < g_wrapperCount; ++i) {
        if (strcmp(name, g_wrappers[i].name) != 0) continue;
        if (g_wrappers[i].realSlot) *g_wrappers[i].realSlot = (void*)real;
        // Hand the engine our wrapper. It caches this pointer for the whole
        // run, which is exactly the interception the Linux build gets from
        // patching the GLEW pointer.
        return (PROC)g_wrappers[i].wrapper;
    }
    return real;
}

void nwn_win_install_gl_hooks() {
    if (g_hooksInstalled) return;
    g_hooksInstalled = true;
    if (nwn_win_patch_iat("OPENGL32.dll", "wglGetProcAddress",
                          (void*)nwn_win_wglGetProcAddress, (void**)&g_realWgl))
        fprintf(stderr, "[shadowmap][win] wglGetProcAddress IAT hook installed "
                        "(%d GL wrappers registered)\n", g_wrapperCount);
    else
        fprintf(stderr, "[shadowmap][win] WARNING: wglGetProcAddress IAT patch FAILED; "
                        "modern GL interception is inactive\n");
}

#endif // _WIN32


// ---------------------------------------------------------------------------
//  Overlay input capture (window subclass)
// ---------------------------------------------------------------------------
// Linux patches SDL_PollEvent's jump thunk; that route is refused here, so the
// panel's clicks also reached the game. Subclassing the window is the standard
// Windows answer and touches no SDL internals: messages the panel is using are
// swallowed before the game's own WndProc ever sees them.
//
// Mouse POSITION still comes from polling SDL_GetMouseState (unchanged) -- this
// only blocks delivery, it does not feed ImGui.
#include "nwn_overlay.h"

static WNDPROC  g_origWndProc = nullptr;

// Feed ImGui, THEN decide whether the game may see the message. Feeding first
// is the whole point: a swallowed message never reaches SDL, so the polling
// path the Linux build relies on reports nothing and the panel becomes
// unclickable. This mirrors imgui_impl_win32's WndProcHandler.
static LRESULT CALLBACK nwn_win_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto clientX = [&]{ return (float)(short)LOWORD(lp); };
    auto clientY = [&]{ return (float)(short)HIWORD(lp); };
    switch (msg) {
        case WM_MOUSEMOVE:
            nwn_overlay_add_mouse_pos(clientX(), clientY());
            break;
        case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK:
            nwn_overlay_add_mouse_pos(clientX(), clientY());
            nwn_overlay_add_mouse_button(0, true);  break;
        case WM_LBUTTONUP:
            nwn_overlay_add_mouse_button(0, false); break;
        case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK:
            nwn_overlay_add_mouse_pos(clientX(), clientY());
            nwn_overlay_add_mouse_button(1, true);  break;
        case WM_RBUTTONUP:
            nwn_overlay_add_mouse_button(1, false); break;
        case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK:
            nwn_overlay_add_mouse_button(2, true);  break;
        case WM_MBUTTONUP:
            nwn_overlay_add_mouse_button(2, false); break;
        default: break;
    }
    switch (msg) {
        case WM_MOUSEWHEEL:
            // ImGui cannot recover the wheel by polling, so forward it even
            // when the panel is not capturing -- it is discarded there.
            nwn_overlay_add_mouse_wheel(0.0f,
                (float)GET_WHEEL_DELTA_WPARAM(wp) / (float)WHEEL_DELTA);
            if (nwn_overlay_wants_mouse()) return 0;
            break;
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN: case WM_LBUTTONUP:   case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN: case WM_RBUTTONUP:   case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN: case WM_MBUTTONUP:   case WM_MBUTTONDBLCLK:
            // ImGui has already been told about it above; only the GAME is
            // denied the message.
            if (nwn_overlay_wants_mouse()) return 0;
            break;
        case WM_KEYDOWN: case WM_KEYUP: case WM_CHAR: case WM_SYSCHAR:
            // NOT the hotkey itself: Ctrl+Shift+F11 is polled elsewhere, and
            // swallowing it here would make the panel impossible to close.
            if (nwn_overlay_wants_keyboard() && wp != VK_F11) return 0;
            break;
        default: break;
    }
    return CallWindowProcW(g_origWndProc, hwnd, msg, wp, lp);
}

static BOOL CALLBACK nwn_win_find_window(HWND hwnd, LPARAM param) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(hwnd)) return TRUE;
    RECT r = {};
    GetClientRect(hwnd, &r);
    // Skip tool/splash windows: the game's own window is the big one.
    if ((r.right - r.left) < 320 || (r.bottom - r.top) < 200) return TRUE;
    *(HWND*)param = hwnd;
    return FALSE;
}

void nwn_win_install_input_capture() {
    if (g_origWndProc) return;                      // already subclassed
    if (!nwn_overlay_ready()) return;               // nothing to capture for yet
    HWND hwnd = nullptr;
    EnumWindows(nwn_win_find_window, (LPARAM)&hwnd);
    if (!hwnd) return;                              // retry next frame
    g_origWndProc = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC,
                                               (LONG_PTR)nwn_win_wndproc);
    if (!g_origWndProc) {
        fprintf(stderr, "[shadowmap][win] input capture: SetWindowLongPtrW failed (%lu)\n",
                (unsigned long)GetLastError());
        return;
    }
    fprintf(stderr, "[shadowmap][win] overlay input capture installed on hwnd=%p\n",
            (void*)hwnd);
}
