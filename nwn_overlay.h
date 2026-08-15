// Injector-owned settings overlay (Dear ImGui).
//
// This replaces the deferred Phase 5b hand-rolled 5x7-bitmap overlay, which
// compiled and detected its hotkey but never became visible. ImGui is kept in
// its own translation unit so none of its headers reach nwn_shadowmap.cpp, and
// so the old overlay can stay in the file as an A/B reference.
//
// INPUT POLICY (unchanged from Phase 5b, and deliberate): NWN's SDL event
// queue is never touched. Mouse and keyboard are POLLED through dlsym'd
// SDL_GetMouseState / SDL_GetKeyboardState -- SDL2 is statically linked into
// nwmain-linux and exports those symbols -- and fed to ImGui as synthetic
// events. Consequence: while the panel is open the game still receives the
// same input, so clicks go to both. Blocking that needs real event
// interception and remains later work.
#pragma once

// Pointers reference the injector's live tunables, so edits take effect on the
// next frame with no plumbing: the receiver reads these globals each time it
// uploads uniforms. Any pointer may be null.
struct NwnOverlayState {
    float* csmStrength   = nullptr;
    float* csmBias       = nullptr;
    float* csmBlend      = nullptr;
    float* csmPcf        = nullptr;
    float* areaShadowFadeSeconds = nullptr;
    bool*  csmComposite  = nullptr;
    bool*  localEnabled  = nullptr;
    float* localStrength = nullptr;
    float* localBias     = nullptr;
    float* localLift     = nullptr;   // how much local light cancels the SUN shadow
    float* liftThreshold = nullptr;
    bool*  lampLift      = nullptr;   // local lights lift the sun's shadow   // brightness below which a pixel counts as unlit
    int*   maxLamps      = nullptr;   // how many lights feed the lift (engine allows 128)
    bool*  localDynOnly  = nullptr;   // local lights shadow only the dynamic buckets
    float* localEdgeFade = nullptr;   // fade band at the light frustum border
    float* localSoft     = nullptr;   // PCF radius for the local shadow
    float* localSlope    = nullptr;   // slope-scaled depth offset while filling
    float* localNormalBias= nullptr;  // normal-offset bias in texels
    bool*  localAlphaCast = nullptr;  // let dithered alpha cards cast
    float* localMinSep    = nullptr;  // world units a caster must be nearer to shadow
    bool*  localEmitCast  = nullptr;  // emitter lights may take caster slots
    bool*  dynamicCasters = nullptr;  // "Moving casters": replay the dynamic buckets
    bool*  staticCasters  = nullptr;  // "Fixed casters": replay the static buckets
    bool*  hideEngineShadows = nullptr; // skip NWN's own blob/stencil shadow pass
    int*   localMapSize  = nullptr;   // per-light shadow resolution (256..4096)
    // Local shadow-map controls. Deliberately separate from maxLamps: that is
    // NWN/lift lighting coverage, while these govern expensive shadow-map work
    // only. The "Cube" in the names is historical -- no path captures a cube.
    int*   localCubeQuality = nullptr; // cadence index into kLocalCubeCadenceSeconds
    int*   localCubeSources = nullptr; // (method, count) combo index; see LocalSourceMethod
    unsigned localCubeActiveSources = 0; // actually allocated by the live path
    unsigned localShadowCandidates = 0; // authoritative NWN priority-list entries this frame
    int    localMapLive  = 0;         // what is actually allocated
    unsigned localSlots  = 0;         // lights that get a layer
    unsigned localPickCount = 0;      // how many lights the census sees
    bool*  resetDefaults = nullptr;   // panel sets it; the injector restores and saves
    int*   receiverDebug = nullptr;

    // Performance controls. These are read by the injector every frame, so
    // edits apply immediately -- except the two RESOLUTIONS, which own GL
    // textures and therefore need a reallocation: the panel edits the pending
    // value and sets *applyResolution, and the injector rebuilds the targets
    // on the next frame.
    int*   cascadeCount    = nullptr;   // 1..4, linear cost saving
    int*   staticNearCascades = nullptr; // hybrid: cascade layers that also get crisp near static
    bool*  fogFade         = nullptr;   // fade shadows out with the engine's fog
    float* fogStart        = nullptr;   // manual fallback when the engine's fog state is unreadable
    float* fogEnd          = nullptr;
    bool*  fullBspEnabled  = nullptr;   // THE big cost: injects the uncalled static set into NWN's own draws
    bool*  receiverEnabled = nullptr;   // master A/B: skip the fullscreen pass
    int*   dynamicLayers   = nullptr;   // 0..4; dynamic casters are the only per-frame geometry left
    float* cascadeDistance = nullptr;   // 0 = use the camera far plane
    bool*  staticCache     = nullptr;
    float* cacheMove       = nullptr;
    bool*  worldEnabled    = nullptr;   // world-anchored static map
    float* worldExtent     = nullptr;   // STAGED value; committed by applyResolution
    int*   pendingCascadeSize = nullptr;   // needs Apply
    int*   pendingWorldSize   = nullptr;   // needs Apply
    bool*  applyResolution    = nullptr;   // panel sets this; injector clears it

    // Frame cost breakdown (CPU wall clock, ms) -- added because three rounds
    // of optimisation guesses failed to explain a 200 -> 27 fps drop.
    double   msReceiver   = 0.0;
    double   msWorldMap   = 0.0;
    double   msReplay     = 0.0;
    double   msSceneCopy  = 0.0;
    unsigned engineDraws  = 0;   // draws the GAME issues per frame
    // Cascade fit cache, per frame. hits==0 with a non-zero refit count means
    // every static caster is being re-replayed every frame -- the shape of the
    // bug found on Windows, and the reason this is readable from the panel
    // rather than only from a log.
    unsigned fitCacheHits = 0;
    unsigned fitRefits    = 0;
    unsigned replayCalls  = 0;
    unsigned replayDraws  = 0;
    bool     fullBspSkipped = false;

    // Read-only status for the performance section.
    int      cascadeSizeLive = 0;
    int      worldSizeLive   = 0;
    float    worldExtentLive = 0.0f;
    float    fogStartLive    = 0.0f;
    float    fogEndLive      = 0.0f;
    bool     fogFromEngine   = false;
    bool     worldValid      = false;
    unsigned worldRenders    = 0;
    // Memory is deliberately split in two: the injector estimate is exact for
    // the targets it allocates; the optional NVX readout is driver-global (not
    // a per-process figure), because OpenGL has no portable per-app VRAM API.
    double   injectorTargetMiB = 0.0;
    bool     driverVramKnown  = false;
    int      driverVramTotalMiB = 0;
    int      driverVramAvailableMiB = 0;

    // Read-only status, copied in by the caller each frame.
    bool     localReady    = false;
    bool     bucketReplay  = false;
    float    localPos[3]   = {0, 0, 0};
    float    localRadius   = 0.0f;
    float    localRgb[3]   = {0, 0, 0};
    unsigned staticDraws   = 0;
    unsigned dynamicDraws  = 0;
    unsigned localLights   = 0;
    float    clipFar[4]    = {0, 0, 0, 0};
    float    sunDir[3]     = {0, 0, 0};
};

// True once ImGui and its GL3 backend are live. Safe to call every frame.
bool nwn_overlay_ready();
// Creates the context/backend on first use; needs a current GL context.
bool nwn_overlay_init();
void nwn_overlay_shutdown();

bool nwn_overlay_visible();
void nwn_overlay_set_visible(bool visible);

// Builds and draws the panel. Does nothing unless visible and initialised.
// Saves and restores the GL state it touches, on top of ImGui's own backup.
void nwn_overlay_render(int viewportW, int viewportH, const NwnOverlayState& state);

// Input capture. True when the panel is open AND ImGui is actually using that
// device this frame -- e.g. the cursor is over the window or a widget is
// active. Clicks outside the panel deliberately still reach the game.
// The SDL_PollEvent hook uses these to decide which events to swallow.
bool nwn_overlay_wants_mouse();
bool nwn_overlay_wants_keyboard();

// Mouse wheel cannot be recovered by polling SDL_GetMouseState, so the event
// hook forwards it here.
void nwn_overlay_add_mouse_wheel(float x, float y);

// WINDOWS input path. Linux polls SDL_GetMouseState and swallows events in the
// SDL_PollEvent hook; Windows cannot hook that (no trampoline), so it subclasses
// the window instead -- and a message swallowed there never reaches SDL, so
// polling would report nothing and the panel could not be clicked at all. The
// WndProc therefore feeds ImGui directly, exactly as imgui_impl_win32 does.
void nwn_overlay_add_mouse_pos(float x, float y);
void nwn_overlay_add_mouse_button(int button, bool down);
