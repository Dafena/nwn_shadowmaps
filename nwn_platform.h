// Platform shim so nwn_shadowmap.cpp builds for both the Linux .so and the
// Windows version.dll proxy.
//
// The strategy is to make Windows look enough like Linux that the ~7000 lines
// of engine logic compile UNCHANGED: dlsym/mprotect/sysconf are shimmed, so
// bind_gl() and the pointer patches work as written. Only four things are
// genuinely different and are #ifdef'd at their call sites:
//   1. symbol resolution  -- ELF .symtab walk vs GetProcAddress on the exe
//      (nwmain.exe is stripped but exports 18,245 named symbols, including the
//      static data ones the matrix-stack technique needs).
//   2. GL interposition   -- ELF symbol interposition does not exist on
//      Windows. GL 1.1 (glDrawElements/glDrawArrays) is IAT-hooked; modern GL
//      is intercepted by hooking wglGetProcAddress and handing the engine our
//      wrappers as it resolves them. This REPLACES the Linux __glew* pointer
//      patch, because nwmain.exe exports no __glew* symbols at all.
//   3. the fault guard    -- sigaction/sigsetjmp has no portable equivalent.
//   4. SDL_PollEvent      -- a jump thunk on Linux (patch the pointer), a real
//      exported function on Windows (subhook it).
#pragma once

// ---------------------------------------------------------------------------
//  SHIPPING POLICY vs PLATFORM MECHANICS
// ---------------------------------------------------------------------------
// These are two different questions and conflating them is a mistake this file
// made for a long time. "_WIN32" answers "how do I resolve a symbol" -- it must
// keep guarding dlsym/GetProcAddress, SDL_PollEvent/WndProc, sigsetjmp, and the
// GL interposition. It must NOT decide what a USER is allowed to see, which is
// what it had drifted into doing.
//
// NWN_SHIP answers "is this a build I hand to someone else". Windows is always
// one; Linux is one when built with -DNWN_SHADOWMAP_SHIPPING (`make deploy`).
// A shipping build:
//   - carries its own defaults, so it needs no launcher script
//   - hides every control that REMOVES shadows (they read as "it's broken")
//   - writes no .pgm dumps and no frame-cost instrumentation (which costs a
//     glFinish, so leaving it on is a real per-frame tax, not just noise)
//   - persists only the settings its panel can actually reach
#if defined(_WIN32) || defined(NWN_SHADOWMAP_SHIPPING)
#  define NWN_SHIP 1
#else
#  define NWN_SHIP 0
#endif

// THE PLATFORM RULE IS ABSOLUTE (see AGENTS.md): a Windows problem is fixed in
// a WINDOWS-ONLY path. Shared code -- C++ or GLSL -- is never modified to fix a
// symptom that only appears on Windows. This macro is the sanctioned mechanism;
// add to it rather than editing common code.
//
// NWN_WIN_LOCAL_FASTPATH answers a DIFFERENT question from NWN_SHIP: "is this
// the platform whose local-light capture needs the Windows-only cost cuts".
//
// THE RULE THIS ENCODES, and it is not negotiable: when a change helps one
// platform and harms the other, it gets a switch. It does NOT get shared and
// tuned until both are tolerable. Every regression on this path came from
// ignoring that -- the two renderers differ in how the engine reaches the
// driver (Windows sends ~4700 glUniformMatrix4fv per frame through our
// wrappers; Linux effectively none), so a "cheaper" path on one side can be
// pure risk with zero payoff on the other. Linux was paying the risk for
// savings it never collected.
//
// Behind this switch, and ONLY behind it:
//   - the per-light caster cull (skips casters outside a light's own reach)
//   - reading the current program from the tracked g_curProgram instead of
//     asking the driver
//   - publishing a local generation when ANY layer drew rather than all
//     (required BECAUSE the cull manufactures empty layers)
// Linux keeps the behaviour of the 2026-08-13 savepoint on all three.
#ifdef _WIN32
#  define NWN_WIN_LOCAL_FASTPATH 1
#else
#  define NWN_WIN_LOCAL_FASTPATH 0
#endif

#ifdef _WIN32

#include <windows.h>
#include <cstddef>

// --- dlsym ----------------------------------------------------------------
// GL names resolve from opengl32.dll first, then wglGetProcAddress (modern
// entry points live only there). Engine names resolve from the exe. That
// covers every dlsym() call the shared code makes.
#define RTLD_DEFAULT ((void*)0)
#define RTLD_NEXT    ((void*)-1)
void* nwn_win_dlsym(void* handle, const char* name);
#define dlsym nwn_win_dlsym

// --- mprotect / sysconf ---------------------------------------------------
#define PROT_READ     0x1
#define PROT_WRITE    0x2
#define _SC_PAGESIZE  1
int  nwn_win_mprotect(void* addr, size_t len, int prot);
long nwn_win_sysconf(int name);
#define mprotect nwn_win_mprotect
#define sysconf  nwn_win_sysconf

// --- GL interposition -----------------------------------------------------
// Register a wrapper to be handed to the engine when it resolves `name`; the
// driver's real pointer is stored into *realSlot. Call before GL init (we are
// loaded as version.dll at process start, so that is guaranteed).
void nwn_win_register_gl_wrapper(const char* name, void* wrapper, void** realSlot);
// IAT-patch one import of nwmain.exe. Used for the GL 1.1 draw calls.
bool nwn_win_patch_iat(const char* dll, const char* func, void* repl, void** origOut);
// Installs the wglGetProcAddress IAT hook. Idempotent.
void nwn_win_install_gl_hooks();
// Resolve an engine symbol by its MSVC-mangled export name.
void* nwn_win_resolve(const char* winMangledName);
// Overlay input capture. SDL_PollEvent cannot be hooked here -- it yields no
// trampoline and detouring it recursed into a stack overflow (see win/README)
// -- so Windows swallows input at the WINDOW level instead: the game's HWND is
// subclassed and mouse/keyboard messages are dropped while the panel wants
// them. Idempotent; safe to call every frame.
void nwn_win_install_input_capture();

// The interposed GL 1.1 entry points cannot keep their real names on Windows
// (that would define exports, not interpose), so they are renamed and hooked.
#define NWN_GL_INTERPOSE(name) nwn_##name

#else   // ---------------------------------------------------------------- Linux

#define NWN_GL_INTERPOSE(name) name

#endif
