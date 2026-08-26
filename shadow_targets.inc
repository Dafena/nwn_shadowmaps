// ===========================================================================
//  The depth target
// ===========================================================================
static GLuint g_fbo      = 0;
static GLuint g_depthTex = 0;
static GLuint g_lightColorTex = 0;
// PS4 reference layout: four directional cascade layers, split into static
// and dynamic depth arrays.  These remain opt-in until the Linux replay can
// classify a caster safely; target creation alone must not alter the known
// single-map diagnostic path.
static constexpr int kCascadeCount = 4;
static_assert(kCascadeCount == 4, "g_cascadeActiveCount defaults to 4; keep these in step");
// World-anchored static map (see g_staticWorldEnabled).
static GLuint g_staticWorldTex = 0;
static GLuint g_staticWorldFbo = 0;
static GLuint g_cascadeStaticTex = 0;
static GLuint g_cascadeDynamicTex = 0;
static GLuint g_cascadeStaticFbo = 0;
static GLuint g_cascadeDynamicFbo = 0;
static bool   g_cascadeTargetsUsable = false;
static GLuint g_gpuStepTex = 0;
// Ordinary integer colour texture mirroring g_depthTex. Mesa/NWN's shader
// path returned 1.0 for both raw and comparison reads of the depth attachment,
// even while GetTexImage proved its contents were valid. This is a temporary
// transport layer that makes the same 32-bit depth values sampleable.
static GLuint g_depthMirrorTex = 0;
static GLuint g_gpuMirrorTex = 0;
static GLuint g_gpuMirrorFbo = 0;
// A copy of the completed world depth buffer for the injector-owned fullscreen
// receiver.  This is intentionally separate from the light depth target.
static GLuint g_sceneDepthTex = 0;
static int    g_sceneDepthW = 0, g_sceneDepthH = 0;
static GLuint g_staticReceiverProgram = 0;
static unsigned g_staticReceiverFrame = 0;
static unsigned g_staticReceiverDraws = 0;
static GLuint g_query    = 0;
// False until bind_gl() has succeeded. On Windows that cannot happen until
// a GL context exists, so every GL-touching path must check it first.
static bool   g_glBound  = false;
static bool   g_created  = false;
static bool   g_usable   = false;
static long   g_frames   = 0;
// Latest inverse VP from the real world-camera material pass.  Kept here so
// the injector-owned fullscreen receiver can consume it later in the file.
static float  g_worldCameraVPInv[16] = {};
static bool   g_haveWorldCameraVPInv = false;

// Phase 6b: local-light single-perspective depth probe. Deliberately its own
// small FBO/texture, never the shared g_fbo/g_cascade* targets -- this is an
// additive, isolated probe (see SHADOWMAP_STATUS.md's local-light plan), not
// a replacement for or a participant in the accepted sun CSM path.
static GLuint g_localLightFbo          = 0;
// LOCAL CAPTURE COST, per frame. The [cost] line's recv/replay/copy time the
// receiver, the SUN cascade replay and the screen grab -- none of them cover the
// local-light duplication, which happens inside the engine's dynamic stage in
// the draw wrapper. Measured 1 source vs 3: those three numbers were identical,
// so they were structurally blind to the thing being optimised.
static double   g_localDupMs        = 0.0;
static unsigned g_localDupDraws     = 0;

// How long a local light takes to fade in when NWN selects it, and to fade out
// after NWN drops it. Both directions use the same table and the same slot
// machinery -- see shadow_local_lights.inc.
static constexpr double kLocalRetireFadeSeconds = 0.35;

// FADE STATE FOR LOCAL LIGHTS, keyed by light IDENTITY rather than slot index:
// NWN reorders its priority list constantly as the player moves, so an index
// says nothing about whether this is the same light as last frame. Getting that
// wrong published a light twice at once and restarted its fade every capture.
//
// ONE TABLE DRIVES BOTH DIRECTIONS. A light NWN has dropped keeps its entry
// with a decaying level and is still published as an ordinary active slot until
// it reaches zero -- so fading out is the same in-band mechanism as fading in.
// The earlier approach copied a dropped light's depth into a spare layer and
// published it as a special extra slot; that fade then had to survive staging,
// g_haveLocalLightVP going false, and the light rejoining, and it broke on each
// in turn. Keeping the light in the slot list removes the whole class.
struct LocalJoinFade {
    const void* key = nullptr;
    float level = 0.0f;
    bool  live  = false;        // still in NWN's selected set this capture
    float pos[3] = {0,0,0};
    float radius = 0.0f;
    float engineFade = 1.0f;    // NWN's own per-light fade (+0x90)
};
static LocalJoinFade g_localJoin[kLocalSlotsMax];
static double        g_localJoinLast = 0.0;
// Level for each published slot this capture, in slot order.
static float         g_localJoinLevel[kLocalSlotsMax] = {};

// Double-buffered local depth. Receivers sample only DepthTex (the last
// completed generation); in-band bucket replays fill WriteDepthTex. Publishing
// swaps the names after bucket 3, so A2C foliage never samples a texture that
// bucket 0 has just cleared and only partially refilled.
static GLuint g_localLightDepthTex      = 0;
static GLuint g_localLightWriteDepthTex = 0;
static bool   g_localLightTargetUsable = false;
// SUN-PATH state. capture_local_light_shadow() publishes the slots and sets
// these; replay_bucket_into_local_light_layers() consumes them from inside the
// engine's own bucket pass. Kept separate from the visible-capture staging
// flags so switching tiers can never leave one path holding the other's state.
// LAYERS ACTUALLY ALLOCATED. The array used to be created with kLocalLightFaces
// (32) layers no matter what, while at most lights*faces = 12 are ever used --
// so 4096 quietly cost 2.1 GB instead of 805 MB, and the panel's own estimate
// (which counts lights) was out by 10x or more. Allocating what is needed is
// what makes the larger sizes usable at all.
static int      g_localLightLayers       = 4;
static bool     g_localInlinePrepared    = false;
static unsigned g_localInlineSlotCount   = 0;
static unsigned g_localInlinePolicyEpoch = 0;
// Which frame each layer was last cleared for, so the clear happens once per
// frame on first touch rather than once per bucket. Same pattern the cascades
// use (g_cascadeDynamicCaptureFrameLayer).
static unsigned g_localInlineLayerFrame[kLocalSlotsMax] = {};
static unsigned g_localInlineDraws[kLocalSlotsMax] = {};
// The dynamic stage also contains submissions that exist only to support the
// engine's stencil shadows.  The local capture must follow the *visible* model,
// not those invisible shadow-proxy meshes.  Keep a small ledger so a capture
// log can prove which kind of submission we excluded.
static unsigned g_localCubeDynamicSkipNoColour = 0;
static unsigned g_localCubeDynamicSkipStencil = 0;
static unsigned g_localCubeDynamicSkipNoProgram = 0;
// Local-shadow update selector exposed by the shipping panel:
// Low=25ms, Medium=16ms, Ultra=every rendered frame.
// Low preserves the established shipping cost; Ultra intentionally matches
// the directional-shadow refresh cadence.
static int    g_localCubeQuality = 0;
// LOCAL SHADOW SOURCES -- how many lights get a shadow map, 1..3.
//
// There used to be three METHODS here (Legacy / Emitter / High) selected by the
// same control. They are gone: the sun-path fill replaced all of them on
// 2026-08-15 after the maintainer measured it as both better looking and much
// faster, and keeping three superseded fills alive to be picked wrongly is
// worse than having one.
//
// What survives is the method they were compared against, and it is the only
// one now: the light's layers are filled from INSIDE the engine's own
// Scene::RenderDrawBucket pass, the same way and at the same point as the sun's
// cascades (replay_bucket_into_local_light_layers). That timing is the whole
// trick -- the engine has just drawn that bucket for the camera, so the casters
// are exactly the visible geometry, the stencil proxies that plagued the old
// bucket replay never enter, and the "buckets 2/3 draw nothing cold" warm-up it
// needed is unnecessary.
//
// NWN exposes at most three shadow-casting lights, which is where the 3 comes
// from. Stored value IS the light count -- it was a combo index while the tiers
// existed, so a config written before 2026-08-15 will read as a different count
// and simply be re-picked.
static inline int lamp_upload_max() {
    // Follows NWN's own "Lighting Max Lights" video setting, live. There is no
    // injector-side control: a second copy of an engine setting can only ever
    // disagree with it.
    //
    // HISTORY: Windows temporarily retained an injector-side shipping cap of
    // 8 after the shared path had moved back to the engine value.  Zooming the
    // camera out then let nearby area lamps displace the carried torch from the
    // lift set.  The shadow-map capture budget is a separate three-light cap;
    // following the engine here only governs the cheap sun-lift loop.
    //
    // NWN_SHADOWMAP_MAX_LAMPS still overrides for A/B; 0 means follow the game.
    if (g_lampUploadMax > 0) return g_lampUploadMax;
    if (eng::LightManagerMaxLights) {
        const int v = *eng::LightManagerMaxLights;
        if (v >= 1 && v <= (int)kMaxLampLights) return v;
    }
    return 32;                       // engine not bound yet
}

// TWO SEPARATE KNOBS.
//
// Lights (1..3) is a TESTING control and is on borrowed time: NWN's own
// "Shadow Casting Lights" video setting already decides how many lights the
// engine nominates, so shipping our own count duplicates it. It stays for now
// because flipping it in our panel is quicker than restarting the game.
//
// Faces is the quality ladder, and it is the one that matters. A perspective
// cone spreads texels by ANGLE, not area: ground footprint per texel grows
// about 1/cos^3 off-axis, so one wide cone is sharp underneath the lamp and
// terrible at the rim. More faces means each covers less angle, so nothing is
// ever sampled far off a face axis.
//
//   faces  spacing  FOV   rim penalty   replays per light
//     1       --    170     ~1000x            1
//     3      120    140       ~25x            3
//     4       90    110        ~5x            4
//
// TWO FACES IS NOT AN OPTION and never will be: they would sit 180 degrees
// apart and need a FOV over 180 to meet. The hard limit is 175, so a 2-face
// layout leaves wedges of missing shadow between two lobes.
static constexpr int kLocalSourceIndexMax = 3;
static int    g_localCubeSourceBudget = 1;   // lights, 1..3
static inline int local_source_lights() {
    return g_localCubeSourceBudget < 1 ? 1
         : (g_localCubeSourceBudget > 3 ? 3 : g_localCubeSourceBudget);
}
// ONE FACE. The 1/3/4-face ladder was built and confirmed working on
// 2026-08-15 and then removed the same day: the maintainer settled on a single
// downward face at the full 170-degree FOV plus a high-resolution map (Mega
// High, 8192) as the best-looking option, which is cheaper than multiple faces
// and avoids their seams entirely.
//
// Kept as a function rather than inlined so the slot loop, the layer
// allocation and the VRAM readout all keep asking one place. If faces ever come
// back, this and local_face_dir are the two hooks -- see the savepoint
// 2026-08-15c-sunpath-only-and-face-ladder for the working implementation, the
// FOV table (1/170, 3/140, 4/110) and why 2 faces can never work.
static inline int local_source_faces() { return 1; }

// Straight down, camera-independent. A lamp shadows what is below it.
static inline void local_face_dir(int, int, float out[3]) {
    out[0] = 0.0f; out[1] = 0.0f; out[2] = -1.0f;
}
static inline float local_face_fov_deg(int) { return g_localLightFovDeg; }

// Minimum seconds between local-map rebuilds, per quality level. Index 4 is
// ZERO on purpose: the gate is `now - last < interval`, and `0 < 0` is false,
// so a zero interval rebuilds every frame. That is the pre-cadence behaviour,
// kept as a deliberate choice rather than an accident.
static constexpr double kLocalCubeCadenceSeconds[] = {0.025, 0.016, 0.0};
static constexpr int kLocalCubeQualityCount =
    (int)(sizeof(kLocalCubeCadenceSeconds) / sizeof(kLocalCubeCadenceSeconds[0]));
static constexpr int kLocalCubeQualityMax = kLocalCubeQualityCount - 1;
static double g_localCubeUpdateSeconds = kLocalCubeCadenceSeconds[0];
// Forget the engine's current shadow-light selection, so the next area starts
// from NWN's own answer rather than the previous area's.
static void reset_engine_shadow_light_anchor() {
    g_engineShadowLightKey = nullptr;
    g_haveEngineShadowLight = false;
    g_engineShadowLightCount = 0;
}
static float  g_localLightView[16] = {};
static float  g_localLightProj[16] = {};
// Phase 6d: what the receiver needs to consume the map above. Carries the
// frame serial so a stale local-light map can never be composited against a
// newer camera -- the same freshness rule the sun cascades already use.
// Peak-held light radius. NWN FLICKERS a torch by animating its radius, and
// this one was measured swinging between 0.09 and 13.30 on the same light
// pointer within a single run. The instantaneous value drove BOTH the shadow
// frustum (zf = radius*2, so 0.55 units for a light 4.3 above the floor -- the
// map came out completely empty, min=max=1.0) AND the receiver's attenuation
// (1 - dist/radius, which is 0 for any ground texel at that radius). Coverage
// is not attenuated, so blue stayed and green vanished: exactly the reported
// "cone reaches the floor but nothing is ever shadowed".
//
// A shadow must not blink with the flame, so hold the peak and decay slowly.
// Reset when the SELECTED light changes, or a big light's radius would carry
// over to a small one.
static float    g_localLightRadiusStable = 0.0f;
static const void* g_localLightRadiusOwner = nullptr;
// FOUR LIGHTS, one depth layer each -- not six cube faces for one light.
//
// The cube was the wrong shape for this problem. A point light needs six faces
// only if it must shadow in every direction; here the casters are DYNAMIC
// objects, which are few and clustered around the camera, so ONE perspective
// face per light aimed at them covers everything that matters -- and four
// lights then cost less than one cube did.
//
// Everything below mirrors the sun cascades, which are proven: a depth
// GL_TEXTURE_2D_ARRAY with one layer per slot, matrices installed through the
// engine's matrix stack, the bucket replay, and a receiver that projects with
// the stored VP and compares. The only differences are a perspective matrix
// instead of ortho and the attenuation cutoff.
// One depth layer per shadow-casting light. The COUNT follows the panel's
// "Lights for lift" so the two features agree on how many lights matter, capped
// here because the cost is real and per-light:
//   * one depth render of the dynamic buckets per light per frame, and
//   * one array layer of VRAM per light.
// At the default 512^2 that is 1 MB a light; 32 lights is 32 MB and 32 captures
// a frame. Raise NWN_SHADOWMAP_LOCAL_LIGHT_SIZE for sharper local shadows and
// pay for it in both.
static constexpr int kLocalLightFaces = kLocalSlotsMax;  // slots, one per light
// One VP per cube face, in the order the receiver indexes them:
//   0=+X 1=-X 2=+Y 3=-Y 4=+Z 5=-Z
static float    g_localLightFaceVP[kLocalLightFaces][16] = {};
static float    g_localLightVP[16] = {};
static bool     g_haveLocalLightVP = false;
static unsigned g_localLightVPFrame = 0;
static float    g_localLightWorldPos[3] = {};
static float    g_localLightWorldRadius = 0.0f;
// Regular local-light maps are now filled from the normal visible dynamic
// submission, exactly like the proven six-face cube diagnostic.  Keep this
// staging generation private until every selected layer has been populated;
// only then publish its matrices to the receiver.  This deliberately excludes
// render=0 stencil proxies while admitting both shadow=0/render=1 and
// shadow=1/render=1 geometry.
static bool     g_localVisibleDynamicCaptureActive = false;
static bool     g_localVisibleDynamicPrepared = false;
static bool     g_localVisibleFallbackNext = false;
static unsigned g_localVisiblePreparedFrame = 0;
static unsigned g_localVisibleCompletedFrame = 0;
static unsigned g_localVisibleSlotCount = 0;
// The regular visible-geometry local-light path has its own scheduling state.
// It deliberately shares the panel cadence with the old cube diagnostic, but
// does not share the cube's target or completion bookkeeping.
static double   g_localLightLastCaptureTime = -1.0;
static unsigned g_localLightCapturePolicyEpoch = 1;
static unsigned g_localLightCompletedPolicyEpoch = 0;
static unsigned g_localVisiblePolicyEpoch = 0;
static float    g_localVisibleView[kLocalLightFaces][16] = {};
static float    g_localVisibleProj[kLocalLightFaces][16] = {};
static float    g_localVisibleVP[kLocalLightFaces][16] = {};
static float    g_localVisibleSlotPos[kLocalLightFaces][3] = {};
static float    g_localVisibleSlotRadius[kLocalLightFaces] = {};
// NWN's per-light fade, staged with the slot it belongs to so the published
// generation and its fade always describe the same frame.
static float    g_localVisibleSlotFade[kLocalLightFaces] = {};
static unsigned g_localVisibleDynamicDraws[kLocalLightFaces] = {};
// A 1x1 depth texture with comparison sampling enabled, bound to the receiver's
// local-light unit whenever there is no real local-light map for this frame.
// This is NOT cosmetic: a sampler2DShadow whose texture unit holds no depth
// texture (or one without GL_TEXTURE_COMPARE_MODE) makes the program invalid
// at draw time, and the driver then silently DISCARDS THE WHOLE DRAW CALL --
// which killed the sun shadows too, in every launcher, while the injector's
// own logging still cheerfully reported "receiver drew".
static GLuint g_shadowSamplerDummyTex = 0;

// Exact accounting for injector-owned texture storage.  It deliberately does
// not claim to measure NWN's textures, attachments, allocator slack, or total
// process VRAM; standard OpenGL exposes no portable per-process counter.
static double injector_target_mib() {
    double bytes = 0.0;
    constexpr double depth24 = 3.0;
    if (g_cascadeStaticTex)  bytes += (double)g_size * g_size * kCascadeCount * depth24;
    if (g_cascadeDynamicTex) bytes += (double)g_size * g_size * kCascadeCount * depth24;
    if (g_staticWorldTex)    bytes += (double)g_staticWorldSize * g_staticWorldSize * depth24;
    if (g_sceneDepthTex)     bytes += (double)g_sceneDepthW * g_sceneDepthH * depth24;
    if (g_localLightDepthTex)
        bytes += (double)g_localLightCaptureSize * g_localLightCaptureSize *
                 g_localLightLayers * depth24;
    if (g_localLightWriteDepthTex)
        bytes += (double)g_localLightCaptureSize * g_localLightCaptureSize *
                 g_localLightLayers * depth24;
    return bytes / (1024.0 * 1024.0);
}

// NVIDIA's NVX extension is the only usable driver-wide VRAM query on this
// OpenGL path. It reports KiB for the driver's pool, not the NWN process.
static void overlay_driver_vram(bool& known, int& totalMiB, int& availableMiB) {
    known = false;
    totalMiB = availableMiB = 0;
    if (!gl::GetIntegerv || !gl::GetString) return;
    const unsigned char* vendor = gl::GetString(GL_VENDOR);
    if (!vendor || !std::strstr((const char*)vendor, "NVIDIA")) return;
    GLint totalKiB = 0, availableKiB = 0;
    while (gl::GetError && gl::GetError() != GL_NO_ERROR) {}
    gl::GetIntegerv(GL_GPU_MEMORY_INFO_DEDICATED_VIDMEM_NVX, &totalKiB);
    gl::GetIntegerv(GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX, &availableKiB);
    if (gl::GetError && gl::GetError() != GL_NO_ERROR) return;
    if (totalKiB <= 0 || availableKiB < 0) return;
    totalMiB = totalKiB / 1024;
    availableMiB = availableKiB / 1024;
    known = true;
}

static const char* fbo_status_str(GLenum s) {
    switch (s) {
        case GL_FRAMEBUFFER_COMPLETE: return "COMPLETE";
        case 0x8CD6: return "INCOMPLETE_ATTACHMENT";
        case 0x8CD7: return "INCOMPLETE_MISSING_ATTACHMENT";
        case 0x8CDB: return "INCOMPLETE_DRAW_BUFFER";
        case 0x8CDC: return "INCOMPLETE_READ_BUFFER";
        case 0x8CDD: return "UNSUPPORTED";
        case 0x8D56: return "INCOMPLETE_MULTISAMPLE";
        case 0x8DA8: return "INCOMPLETE_LAYER_TARGETS";
        case 0:      return "ZERO (no context?)";
        default:     return "UNKNOWN";
    }
}

static bool cascade_targets_requested() {
    return g_cascadeTargetValidate ||
           shadow_getenv("NWN_SHADOWMAP_CASCADE_TARGETS") != nullptr;
}

// Allocate the exact core layout declared by the PS4 technique: separate
// static/dynamic GL_TEXTURE_2D_ARRAY depth maps, four layers each, with
// hardware comparison sampling.  No engine draw is redirected here yet.  That
// separation needs a verified classification point, not a bucket heuristic.
static void create_cascade_targets() {
    if (!cascade_targets_requested() || g_cascadeTargetsUsable ||
        !gl::TexImage3D || !gl::FramebufferTextureLayer) return;

    GLint prevFBO = 0, prevTexArray = 0;
    gl::GetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    gl::GetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &prevTexArray);

    auto make_array = [&](GLuint& tex) {
        gl::GenTextures(1, &tex);
        gl::BindTexture(GL_TEXTURE_2D_ARRAY, tex);
        gl::TexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT24,
                       g_size, g_size, kCascadeCount, 0,
                       GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
        gl::TexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl::TexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        // PS4/GNM declares clamp-to-edge, not a lit border.  The cascade
        // matrix fitting must keep valid receivers inside the layer bounds;
        // edge clamping makes an out-of-range bug visible instead of hiding it.
        gl::TexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl::TexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gl::TexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE,
                          GL_COMPARE_REF_TO_TEXTURE);
        gl::TexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_FUNC, GL_LESS);
    };
    make_array(g_cascadeStaticTex);
    make_array(g_cascadeDynamicTex);

    auto make_fbo = [&](GLuint& fbo, GLuint tex, const char* label) -> bool {
        gl::GenFramebuffers(1, &fbo);
        gl::BindFramebuffer(GL_FRAMEBUFFER, fbo);
        gl::DrawBuffer(GL_NONE);
        gl::ReadBuffer(GL_NONE);
        bool allLayersOK = true;
        char states[4][32] = {};
        for (int layer = 0; layer < kCascadeCount; ++layer) {
            gl::FramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                        tex, 0, layer);
            const GLenum status = gl::CheckFramebufferStatus(GL_FRAMEBUFFER);
            std::snprintf(states[layer], sizeof(states[layer]), "%s",
                          fbo_status_str(status));
            allLayersOK = allLayersOK && status == GL_FRAMEBUFFER_COMPLETE;
        }
        fprintf(stderr, "[shadowmap] cascade %s target tex=%u fbo=%u layers=%d "
                        "size=%d layer-status=[%s,%s,%s,%s]\n",
                label, tex, fbo, kCascadeCount, g_size,
                states[0], states[1], states[2], states[3]);
        return allLayersOK;
    };
    const bool staticOK = make_fbo(g_cascadeStaticFbo, g_cascadeStaticTex, "static");
    const bool dynamicOK = make_fbo(g_cascadeDynamicFbo, g_cascadeDynamicTex, "dynamic");
    g_cascadeTargetsUsable = staticOK && dynamicOK && gl::GetError() == GL_NO_ERROR;

    gl::BindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFBO);
    gl::BindTexture(GL_TEXTURE_2D_ARRAY, (GLuint)prevTexArray);
    fprintf(stderr, "[shadowmap] PS4-style cascade targets: %s (%s)\n",
            g_cascadeTargetsUsable ? "READY" : "NOT USABLE",
            g_cascadeTargetValidate
                ? "trace validation only; no legacy target/replay/receiver"
                : "creation only; single-map replay remains active");
}

// Populate the first dynamic cascade from the established one-map replay.
// This is deliberately a GPU-side depth copy, not a second bucket replay: it
// proves the array layer receives exactly the known-good light-space depth
// without changing scene state, draw ordering, or the current receiver.
static bool copy_primary_depth_to_dynamic_cascade0() {
    if (!shadow_getenv("NWN_SHADOWMAP_CASCADE_COPY") || !g_cascadeTargetsUsable ||
        !g_cascadeDynamicTex || !gl::CopyTexSubImage3D) return false;
    GLint oldArray = 0;
    gl::GetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &oldArray);
    gl::BindTexture(GL_TEXTURE_2D_ARRAY, g_cascadeDynamicTex);
    // g_fbo is still bound by render_from_light; this copies its depth
    // attachment into array layer zero without a CPU readback.
    gl::CopyTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, 0,
                          0, 0, g_size, g_size);
    const GLenum err = gl::GetError();
    gl::BindTexture(GL_TEXTURE_2D_ARRAY, (GLuint)oldArray);
    static bool saidOK = false;
    if (err == GL_NO_ERROR && !saidOK) {
        saidOK = true;
        fprintf(stderr, "[shadowmap] dynamic cascade layer 0 copied from "
                        "validated primary light depth\n");
    }
    if (err != GL_NO_ERROR)
        fprintf(stderr, "[shadowmap] dynamic cascade layer 0 copy failed: "
                        "GL error 0x%x\n", err);
    return err == GL_NO_ERROR;
}

// One-shot, opt-in proof that CopyTexSubImage3D copied DEPTH rather than merely
// succeeding at API level.  A four-layer 2048² array is ~64 MiB, so never do
// this per frame; it is a validation instrument for the first cascade bridge.
static void verify_dynamic_cascade_layer0_once() {
    static bool done = false;
    if (done || !shadow_getenv("NWN_SHADOWMAP_CASCADE_VERIFY") ||
        !g_cascadeTargetsUsable || !gl::GetTexImage) return;
    done = true;
    const size_t pixels = (size_t)g_size * (size_t)g_size;
    std::vector<uint32_t> primary(pixels);
    std::vector<uint32_t> cascades(pixels * kCascadeCount);
    GLint old2D = 0, oldArray = 0;
    gl::GetIntegerv(GL_TEXTURE_BINDING_2D, &old2D);
    gl::GetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &oldArray);
    gl::BindTexture(GL_TEXTURE_2D, g_depthTex);
    gl::GetTexImage(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT,
                    primary.data());
    gl::BindTexture(GL_TEXTURE_2D_ARRAY, g_cascadeDynamicTex);
    gl::GetTexImage(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT,
                    cascades.data());
    gl::BindTexture(GL_TEXTURE_2D, (GLuint)old2D);
    gl::BindTexture(GL_TEXTURE_2D_ARRAY, (GLuint)oldArray);

    size_t mismatch = 0;
    uint32_t maxDelta = 0;
    for (size_t i = 0; i < pixels; ++i) {
        const uint32_t a = primary[i], b = cascades[i];
        if (a != b) {
            ++mismatch;
            const uint32_t d = a > b ? a - b : b - a;
            if (d > maxDelta) maxDelta = d;
        }
    }
    const GLenum err = gl::GetError();
    fprintf(stderr, "[shadowmap] dynamic cascade layer 0 verify: mismatch=%zu/%zu "
                    "max-delta=%u GL=0x%x\n",
            mismatch, pixels, maxDelta, err);
}

static void create_target() {
    g_created = true;

    GLint prevFBO = 0, prevTex = 0, prevTexArray = 0;
    gl::GetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    gl::GetIntegerv(GL_TEXTURE_BINDING_2D,  &prevTex);
    gl::GetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &prevTexArray);

    while (gl::GetError() != GL_NO_ERROR) {}   // drain pre-existing errors

    gl::GenTextures(1, &g_depthTex);
    gl::BindTexture(GL_TEXTURE_2D, g_depthTex);
    gl::TexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24,
                   g_size, g_size, 0,
                   GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    // LINEAR + compare mode gives free 2x2 hardware PCF on the shadow test.
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    // Border = 1.0 (max depth) so anything sampled outside the map reads as
    // "nothing in front of it" = lit, rather than shadowed.
    const GLfloat border[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    gl::TexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
    // A GL_DEPTH_COMPONENT texture read through a plain sampler2D returned the
    // clear value (1.0) on this driver no matter what -- sampler unit verified,
    // binding verified, feedback loop eliminated, glGetTexImage on the same
    // object showing real geometry. Depth textures are meant to be read through
    // a shadow sampler, where the hardware performs the comparison and returns
    // 0/1 directly. That path is well-trodden and also gives free PCF.
    // NOTE: glGetTexImage still works for the PGM dumps, so debugging is intact.
    // Depth textures are consumed through sampler2DShadow. On this driver raw
    // sampler2D reads returned 1.0 even when glGetTexImage showed real depth.
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE,
                      g_useCompare ? GL_COMPARE_REF_TO_TEXTURE : GL_NONE);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);

    GLenum errTex = gl::GetError();

    gl::GenTextures(1, &g_lightColorTex);
    gl::BindTexture(GL_TEXTURE_2D, g_lightColorTex);
    // The CPU fallback preserves the driver's 24-bit depth integer. RGBA8
    // here preserved only eight bits, enough to quantise the receiver compare
    // into "lit" everywhere. R32F keeps gl_FragCoord.z as a real depth value
    // while staying an ordinary, reliably sampleable colour texture.
    gl::TexImage2D(GL_TEXTURE_2D, 0, GL_R32F, g_size, g_size, 0,
                   GL_RED, GL_FLOAT, nullptr);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // GPU-only A/B snapshot. It has the same float layout as the live caster
    // texture, but is copied once per second so its matrix can be paired with
    // the same cadence as the known-good CPU bridge.
    gl::GenTextures(1, &g_gpuStepTex);
    gl::BindTexture(GL_TEXTURE_2D, g_gpuStepTex);
    gl::TexImage2D(GL_TEXTURE_2D, 0, GL_R32F, g_size, g_size, 0,
                   GL_RED, GL_FLOAT, nullptr);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    gl::GenTextures(1, &g_depthMirrorTex);
    gl::BindTexture(GL_TEXTURE_2D, g_depthMirrorTex);
    gl::TexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, g_size, g_size, 0,
                   GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // GPU conversion output: ordinary colour texture, sampleable by injected
    // fragment shaders without the driver's depth-texture read limitation.
    gl::GenTextures(1, &g_gpuMirrorTex);
    gl::BindTexture(GL_TEXTURE_2D, g_gpuMirrorTex);
    gl::TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, g_size, g_size, 0,
                   GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl::GenFramebuffers(1, &g_gpuMirrorFbo);
    gl::BindFramebuffer(GL_FRAMEBUFFER, g_gpuMirrorFbo);
    gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_TEXTURE_2D, g_gpuMirrorTex, 0);
    gl::DrawBuffer(GL_COLOR_ATTACHMENT0);
    gl::ReadBuffer(GL_COLOR_ATTACHMENT0);
    GLenum gpuStatus = gl::CheckFramebufferStatus(GL_FRAMEBUFFER);
    fprintf(stderr, "[shadowmap] GPU mirror target fbo=%u tex=%u status=%s\n",
            g_gpuMirrorFbo, g_gpuMirrorTex, fbo_status_str(gpuStatus));

    gl::GenFramebuffers(1, &g_fbo);
    gl::BindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                             GL_TEXTURE_2D, g_depthTex, 0);
    gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_TEXTURE_2D, g_lightColorTex, 0);
    gl::DrawBuffer(GL_COLOR_ATTACHMENT0);
    gl::ReadBuffer(GL_COLOR_ATTACHMENT0);

    GLenum status = gl::CheckFramebufferStatus(GL_FRAMEBUFFER);
    GLenum errFbo = gl::GetError();

    // Read the texture state back. If COMPARE_MODE does not report
    // COMPARE_REF_TO_TEXTURE (0x884E) then our TexParameteri never took, and a
    // sampler2DShadow reading a non-compare texture is undefined -- which on
    // this driver would return 1.0 unconditionally, exactly what we see.
    {
        GLint cmpMode = -1, cmpFunc = -1, minF = -1, magF = -1, ifmt = -1;
        gl::BindTexture(GL_TEXTURE_2D, g_depthTex);
        if (gl::GetTexParameteriv) {
            gl::GetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, &cmpMode);
            gl::GetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, &cmpFunc);
            gl::GetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &minF);
            gl::GetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &magF);
        }
        if (gl::GetTexLevelParameteriv)
            gl::GetTexLevelParameteriv(GL_TEXTURE_2D, 0, 0x1003 /*INTERNAL_FORMAT*/, &ifmt);
        fprintf(stderr, "[shadowmap] tex state: compareMode=0x%x (want 0x%x) "
                        "func=0x%x minF=0x%x magF=0x%x internalFormat=0x%x "
                        "(want 0x%x)\n",
                cmpMode, GL_COMPARE_REF_TO_TEXTURE, cmpFunc, minF, magF,
                ifmt, GL_DEPTH_COMPONENT24);
    }

    gl::GenQueries(1, &g_query);

    g_usable = (status == GL_FRAMEBUFFER_COMPLETE) &&
               errTex == GL_NO_ERROR && errFbo == GL_NO_ERROR;

    gl::BindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFBO);
    gl::BindTexture(GL_TEXTURE_2D, (GLuint)prevTex);
    gl::BindTexture(GL_TEXTURE_2D_ARRAY, (GLuint)prevTexArray);

    create_cascade_targets();

    const unsigned char* ver = gl::GetString(GL_VERSION);
    const unsigned char* ren = gl::GetString(GL_RENDERER);
    fprintf(stderr,
            "[shadowmap] GL %s | %s\n"
            "[shadowmap] depth target %dx%d  fbo=%u tex=%u query=%u  status=%s"
            "  errTex=0x%x errFbo=0x%x -> %s\n",
            ver ? (const char*)ver : "?", ren ? (const char*)ren : "?",
            g_size, g_size, g_fbo, g_depthTex, g_query, fbo_status_str(status),
            errTex, errFbo, g_usable ? "USABLE" : "NOT USABLE");
}
