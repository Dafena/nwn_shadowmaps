// nwn_hooks_core.h -- the (deliberately tiny) contract between injector modules.
//
// WHY THIS EXISTS
// ---------------
// The injector is growing a second feature (order-independent transparency)
// alongside the cascade shadow maps. They are independent renderer features and
// each lives in its own translation unit, but they CANNOT be independent
// LD_PRELOAD libraries. Two preloads do load and chain fine -- every interposer
// here resolves through dlsym(RTLD_NEXT, ...), so `LD_PRELOAD=A:B` gives a
// working A -> B -> libGL chain, and the GLEW slot patches compose because each
// captures the current pointer before overwriting it. What does NOT work is
// OWNERSHIP:
//
//   * Each module replays geometry. Those replay draws travel through the other
//     module's per-draw hook, which sees them as ordinary scene draws. The
//     shadow pass would feed its cascade replay into the transparency
//     accumulation; the transparency resolve would land in shadow_before_draw().
//     The existing fix for exactly this class of bug is a re-entrancy flag --
//     and a `static bool` cannot be shared across two separately dlopen'd
//     libraries.
//   * Both would subhook Scene::Render at one address, where the
//     remove -> call real -> reinstall pattern used for capture hooks writes
//     back bytes the other library owns.
//   * Frame-phase ordering would be decided by LD_PRELOAD string order rather
//     than by intent.
//
// So: separate FILES, separate env vars, separate launchers, separate panel
// sections, separate toggles -- one .so. This header is the only thing they
// share, and it is intentionally about COORDINATION, not plumbing. Each module
// resolves its own GL entry points (nwn_oit.cpp has its own small table); the
// `gl::` table stays private to nwn_shadowmap.cpp. Duplicating a dozen function
// pointers is a much better trade than coupling the two modules' GL state.
//
// This header must compile under both g++ (the .so) and mingw-w64 (version.dll).
#pragma once

namespace nwn_core {

// Set while ANY injector module is issuing its own draw calls. Every module's
// per-draw hook must early-out on it, or one module's pass gets counted,
// muted, or accumulated by another.
//
// Defined in nwn_shadowmap.cpp, next to the older per-feature flags it
// generalises (g_cascadeReplayActive / g_localLightPassActive /
// g_overlayPassActive). It lives there rather than in a core .cpp because the
// shadow module is always linked; when a real nwn_core.cpp exists, move it.
extern bool g_ownedPass;

// RAII form. Nests correctly -- restores the previous value rather than
// clearing, because an injector pass can legitimately run inside another one.
struct OwnedPass {
    bool prev;
    OwnedPass() : prev(g_ownedPass) { g_ownedPass = true; }
    ~OwnedPass() { g_ownedPass = prev; }
    OwnedPass(const OwnedPass&) = delete;
    OwnedPass& operator=(const OwnedPass&) = delete;
};

// Index of the draw bucket the engine is currently rendering, or -1 outside
// Scene::RenderDrawBucket / outside the selected area scene. Set by the shadow
// module's bucket detour because that is where the hook already exists; it
// describes the ENGINE's state, not that module's, which is why it lives here.
extern int g_currentBucket;

// Per-draw observation of the ENGINE's own draws. Null unless a module opts in,
// and the call site tests it before doing anything, so the cost when unset is
// one predictable branch on a path that runs ~15k times a frame.
//
// Invoked from trace_normal_geometry_draw() in nwn_shadowmap.cpp -- the one
// function every draw wrapper already funnels through (glDrawElements,
// glDrawArrays, glDrawRangeElements, both glMultiDraw*, and the BaseVertex /
// Instanced variants; the engine was observed using glDrawRangeElements and
// glMultiDraw* in practice). Note that shadow_before_draw() would NOT have been
// a valid hook point: it is reached only from the glDrawArrays/glDrawElements
// interposers, which is not where this engine's geometry goes.
//
// Observers must not issue GL commands that alter state the engine is relying
// on mid-bucket, and are not called for injector-owned passes.
extern void (*g_drawObserver)();

}   // namespace nwn_core

// ---------------------------------------------------------------------------
//  OIT module (nwn_oit.cpp) -- entry points called from the shadow module's
//  frame sequence. All are no-ops unless NWN_OIT=1.
// ---------------------------------------------------------------------------

// Runs once per frame, after the engine's Scene::Render has completed. Owns its
// own lazy init, its own GL resolution, and full save/restore of every GL state
// it touches.
void nwn_oit_frame(void);

// Release GL objects. Safe to call when never initialised.
void nwn_oit_shutdown(void);

// True when the module is enabled AND live (for the settings panel / logging).
bool nwn_oit_active(void);
