// version.dll proxy: the Windows equivalent of LD_PRELOAD.
//
// nwmain.exe imports from VERSION.dll, so dropping this DLL next to the exe
// gets us loaded at process start -- before any GL setup -- and we forward to
// the real DLL in System32.
//
// FORWARD EVERY EXPORT, NOT JUST THE GAME'S.
// The first version exported only the three functions nwmain.exe itself
// imports (GetFileVersionInfoA / GetFileVersionInfoSizeA / VerQueryValueA).
// That crashed on launch with "entry point GetFileVersionInfoW not found in
// nvoglv64.dll": a proxy replaces version.dll for the WHOLE PROCESS, and
// NVIDIA's OpenGL driver imports it too. Once our module is loaded under the
// name "version.dll" the loader hands it to every other module that asks, so
// anything we fail to export is a hard load failure for them.
//
// The forwarders are deliberately generic. Every version.dll entry point takes
// only integer/pointer arguments and returns an integer/BOOL, so under the
// single Microsoft x64 calling convention one six-argument passthrough is ABI-
// correct for all of them: the first four arrive in RCX/RDX/R8/R9, any extras
// sit in caller-allocated stack slots, and the return comes back in RAX.
// Passing more arguments than the real function reads is harmless. This avoids
// hand-writing 17 exact signatures -- including GetFileVersionInfoByHandle,
// which Microsoft does not document.
//
// The injector proper lives in nwn_shadowmap.cpp and starts from its
// __attribute__((constructor)), which mingw runs on DLL_PROCESS_ATTACH exactly
// as the .so's runs on load. Keeping work out of DllMain also keeps us off the
// loader lock.

#include <windows.h>
#include <cstdio>
#include <cstring>

namespace {

using GenericFn = UINT_PTR (WINAPI*)(UINT_PTR, UINT_PTR, UINT_PTR,
                                     UINT_PTR, UINT_PTR, UINT_PTR);

// Order must match the FORWARD() list below.
const char* const kExportNames[] = {
    "GetFileVersionInfoA",
    "GetFileVersionInfoByHandle",
    "GetFileVersionInfoExA",
    "GetFileVersionInfoExW",
    "GetFileVersionInfoSizeA",
    "GetFileVersionInfoSizeExA",
    "GetFileVersionInfoSizeExW",
    "GetFileVersionInfoSizeW",
    "GetFileVersionInfoW",
    "VerFindFileA",
    "VerFindFileW",
    "VerInstallFileA",
    "VerInstallFileW",
    "VerLanguageNameA",
    "VerLanguageNameW",
    "VerQueryValueA",
    "VerQueryValueW",
};
constexpr int kExportCount = (int)(sizeof(kExportNames) / sizeof(kExportNames[0]));

GenericFn g_real[kExportCount] = {};
HMODULE   g_realVersion = nullptr;

void load_real_version() {
    char sys[MAX_PATH] = {};
    GetSystemDirectoryA(sys, MAX_PATH);
    strncat(sys, "\\version.dll", sizeof(sys) - strlen(sys) - 1);
    g_realVersion = LoadLibraryA(sys);
    if (!g_realVersion) return;
    for (int i = 0; i < kExportCount; ++i)
        g_real[i] = (GenericFn)(void*)GetProcAddress(g_realVersion, kExportNames[i]);
}

// NOTE: stderr redirection deliberately lives in nwn_platform_win.cpp as an
// early-priority constructor. Doing it here would be too late -- mingw runs
// constructors before DllMain, so the injector's startup report would already
// have been written to a dead stderr, and reopening with "w" here truncated
// what did survive.

} // namespace

// Internal names: windows.h already declares the real ones, so we define
// nwn_fwd_N here and let version.def export each under its true name. That
// also keeps the generic six-argument signature legal.
#define FORWARD(idx)                                                           \
    extern "C" UINT_PTR WINAPI nwn_fwd_##idx(UINT_PTR a, UINT_PTR b, UINT_PTR c, \
                                             UINT_PTR d, UINT_PTR e, UINT_PTR f) { \
        return g_real[idx] ? g_real[idx](a, b, c, d, e, f) : 0;                \
    }

FORWARD(0)   // GetFileVersionInfoA
FORWARD(1)   // GetFileVersionInfoByHandle
FORWARD(2)   // GetFileVersionInfoExA
FORWARD(3)   // GetFileVersionInfoExW
FORWARD(4)   // GetFileVersionInfoSizeA
FORWARD(5)   // GetFileVersionInfoSizeExA
FORWARD(6)   // GetFileVersionInfoSizeExW
FORWARD(7)   // GetFileVersionInfoSizeW
FORWARD(8)   // GetFileVersionInfoW
FORWARD(9)   // VerFindFileA
FORWARD(10)   // VerFindFileW
FORWARD(11)   // VerInstallFileA
FORWARD(12)   // VerInstallFileW
FORWARD(13)   // VerLanguageNameA
FORWARD(14)   // VerLanguageNameW
FORWARD(15)   // VerQueryValueA
FORWARD(16)   // VerQueryValueW

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        load_real_version();          // must be ready before anyone calls through
        int bound = 0;
        for (int i = 0; i < kExportCount; ++i) if (g_real[i]) ++bound;
        fprintf(stderr, "[shadowmap][win] version.dll proxy attached; "
                        "forwarded %d/%d exports; injector constructor takes over\n",
                bound, kExportCount);
        if (bound != kExportCount)
            for (int i = 0; i < kExportCount; ++i)
                if (!g_real[i])
                    fprintf(stderr, "[shadowmap][win] NOTE: real version.dll has no %s "
                                    "(calls to it will return 0)\n", kExportNames[i]);
    }
    return TRUE;
}
