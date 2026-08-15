// ===========================================================================
//  Matrix-stack diagnostic (READ ONLY)
//  Four rounds of inferring SetViewTransform's semantics from its arguments
//  produced a wrong answer every time. The engine's real matrices are sitting
//  in a static symbol, so read them instead: with the camera in a known place,
//  the view matrix tells us the handedness, the forward axis and the storage
//  order outright, and the projection entry identifies which index is which.
// ===========================================================================
static void dump_matrix_stack(const char* when) {
    if (!eng::MtxStack || !eng::CurMtxIdx) {
        fprintf(stderr, "[shadowmap] matrix stack symbols unresolved\n");
        return;
    }
    int cur = *eng::CurMtxIdx;
    fprintf(stderr, "\n[shadowmap] ===== MATRIX STACK (%s) current=%d =====\n",
            when, cur);
    for (int i = 0; i < MTX_ENTRIES; ++i) {
        const uint8_t* e = (const uint8_t*)eng::MtxStack + (size_t)i * MTX_STRIDE;
        const float*   f = (const float*)e;
        fprintf(stderr, "[shadowmap] entry %d%s\n", i, i == cur ? "  <-- current" : "");
        for (int r = 0; r < 4; ++r)
            fprintf(stderr, "[shadowmap]   % 10.4f % 10.4f % 10.4f % 10.4f\n",
                    f[r*4+0], f[r*4+1], f[r*4+2], f[r*4+3]);
        fprintf(stderr, "[shadowmap]   tail:");
        for (int t = 16; t < 22; ++t) fprintf(stderr, " %.4f", f[t]);
        fprintf(stderr, "\n");
    }
    if (eng::SunDir)
        fprintf(stderr, "[shadowmap] SUN dir  =(%.4f %.4f %.4f %.4f)  |len|=%.4f\n",
                eng::SunDir[0], eng::SunDir[1], eng::SunDir[2], eng::SunDir[3],
                std::sqrt(eng::SunDir[0]*eng::SunDir[0] +
                          eng::SunDir[1]*eng::SunDir[1] +
                          eng::SunDir[2]*eng::SunDir[2]));
    if (eng::SunDiffuse)
        fprintf(stderr, "[shadowmap] SUN diff =(%.4f %.4f %.4f %.4f)\n",
                eng::SunDiffuse[0], eng::SunDiffuse[1],
                eng::SunDiffuse[2], eng::SunDiffuse[3]);
    if (eng::SunAmbient)
        fprintf(stderr, "[shadowmap] SUN amb  =(%.4f %.4f %.4f %.4f)\n",
                eng::SunAmbient[0], eng::SunAmbient[1],
                eng::SunAmbient[2], eng::SunAmbient[3]);
    fprintf(stderr, "[shadowmap] last captured view arg pos=(%.3f %.3f %.3f %.3f)"
                    "  quat[wxyz]=(%.4f %.4f %.4f %.4f)\n"
                    "[shadowmap] ===== END MATRIX STACK =====\n\n",
            g_camPos.x, g_camPos.y, g_camPos.z, g_camPos.w,
            g_camQuat.a, g_camQuat.b, g_camQuat.c, g_camQuat.d);
}

// ===========================================================================
//  Crash guard for the probe call.
// ===========================================================================
#ifndef _WIN32
static sigjmp_buf g_jmp;
static volatile sig_atomic_t g_guardArmed = 0;

static void fault_handler(int sig) {
    if (g_guardArmed) { g_guardArmed = 0; siglongjmp(g_jmp, sig); }
    signal(sig, SIG_DFL);
    raise(sig);
}
#endif

static const char* g_faultLabel = "PROBE";

static bool guarded_render_bucket(void* scene, int bucket) {
#ifdef _WIN32
    // No sigaction/sigsetjmp here. The Linux guard exists because a bad
    // standalone bucket call once faulted; the in-sequence replay this build
    // uses has been stable, so Windows runs it unguarded rather than shipping
    // a half-working SEH translation. If it ever faults on Windows the crash
    // will be honest instead of silently disabled.
    eng::SceneRenderDrawBucket(scene, bucket);
    return true;
#else
    struct sigaction sa {}, oldSegv {}, oldBus {}, oldFpe {};
    sa.sa_handler = fault_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_NODEFER;          // required to longjmp out of the handler
    sigaction(SIGSEGV, &sa, &oldSegv);
    sigaction(SIGBUS,  &sa, &oldBus);
    sigaction(SIGFPE,  &sa, &oldFpe);

    volatile bool ok;
    int sig = sigsetjmp(g_jmp, 1);
    if (sig == 0) {
        g_guardArmed = 1;
        eng::SceneRenderDrawBucket(scene, bucket);
        ok = true;
    } else {
        fprintf(stderr,
            "[shadowmap] %s FAULTED (signal %d) in bucket %d.\n"
            "[shadowmap] Disabling. RESTART THE GAME -- engine state is undefined\n"
            "[shadowmap] after a caught fault (models can go missing until reloaded).\n",
            g_faultLabel, sig, bucket);
        ok = false;
    }
    g_guardArmed = 0;

    sigaction(SIGSEGV, &oldSegv, nullptr);
    sigaction(SIGBUS,  &oldBus,  nullptr);
    sigaction(SIGFPE,  &oldFpe,  nullptr);
    return ok;
#endif
}

// ===========================================================================
//  Target bind / restore
// ===========================================================================
struct SavedState {
    GLint     fbo = 0, vp[4] = {0,0,0,0};
    GLboolean depthMask = GL_TRUE, scissor = GL_FALSE, depthTest = GL_TRUE;
    GLboolean colorMask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
};

// forceDepth: also enable GL_DEPTH_TEST. The probe needs it -- if the engine
// left depth testing off, geometry would draw but write no depth and the
// target would look empty for entirely the wrong reason.
static SavedState bind_target(bool forceDepth) {
    SavedState st;
    gl::GetIntegerv(GL_FRAMEBUFFER_BINDING, &st.fbo);
    gl::GetIntegerv(GL_VIEWPORT, st.vp);
    gl::GetBooleanv(GL_DEPTH_WRITEMASK, &st.depthMask);
    gl::GetBooleanv(GL_COLOR_WRITEMASK, st.colorMask);
    st.scissor   = gl::IsEnabled(GL_SCISSOR_TEST);
    st.depthTest = gl::IsEnabled(GL_DEPTH_TEST);

    gl::BindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    gl::Viewport(0, 0, g_size, g_size);
    if (st.scissor)     gl::Disable(GL_SCISSOR_TEST);   // would clip the clear
    if (!st.depthMask)  gl::DepthMask(GL_TRUE);         // or the clear is a no-op
    gl::ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);  // and so is color clear
    if (forceDepth && !st.depthTest) gl::Enable(GL_DEPTH_TEST);
    return st;
}

static void restore_state(const SavedState& st) {
    if (!st.depthTest) gl::Disable(GL_DEPTH_TEST);
    if (!st.depthMask) gl::DepthMask(GL_FALSE);
    if (st.scissor)    gl::Enable(GL_SCISSOR_TEST);
    gl::ColorMask(st.colorMask[0], st.colorMask[1], st.colorMask[2], st.colorMask[3]);
    gl::Viewport(st.vp[0], st.vp[1], st.vp[2], st.vp[3]);
    gl::BindFramebuffer(GL_FRAMEBUFFER, (GLuint)st.fbo);
}

// Phase 3d: prove that every private cascade layer can be written without
// disturbing NWN.  This is intentionally a one-shot clear/readback only: no
// engine geometry, no bucket replay, no shader mutation and no receiver.
static void validate_cascade_target_clear_once() {
    static bool done = false;
    if (done || !g_cascadeTargetValidate || !g_cascadeTargetsUsable ||
        !g_cascadeStaticFbo || !g_cascadeDynamicFbo || !gl::GetTexImage) return;
    done = true;

    SavedState st;
    gl::GetIntegerv(GL_FRAMEBUFFER_BINDING, &st.fbo);
    gl::GetIntegerv(GL_VIEWPORT, st.vp);
    gl::GetBooleanv(GL_DEPTH_WRITEMASK, &st.depthMask);
    gl::GetBooleanv(GL_COLOR_WRITEMASK, st.colorMask);
    st.scissor = gl::IsEnabled(GL_SCISSOR_TEST);
    st.depthTest = gl::IsEnabled(GL_DEPTH_TEST);
    GLint oldArray = 0;
    gl::GetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &oldArray);

    auto clear_layers = [&](GLuint fbo, GLuint tex, const char* label) {
        gl::BindFramebuffer(GL_FRAMEBUFFER, fbo);
        gl::Viewport(0, 0, g_size, g_size);
        if (st.scissor) gl::Disable(GL_SCISSOR_TEST);
        if (!st.depthMask) gl::DepthMask(GL_TRUE);
        for (int layer = 0; layer < kCascadeCount; ++layer) {
            gl::FramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                        tex, 0, layer);
            gl::ClearDepth(1.0);
            gl::Clear(GL_DEPTH_BUFFER_BIT);
        }
        gl::Finish();

        const size_t pixels = (size_t)g_size * (size_t)g_size * kCascadeCount;
        std::vector<uint32_t> values(pixels);
        gl::BindTexture(GL_TEXTURE_2D_ARRAY, tex);
        gl::GetTexImage(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT,
                        GL_UNSIGNED_INT, values.data());
        uint32_t lo = UINT32_MAX, hi = 0;
        size_t nonClear = 0;
        for (uint32_t value : values) {
            lo = std::min(lo, value);
            hi = std::max(hi, value);
            if (value != UINT32_MAX) ++nonClear;
        }
        fprintf(stderr, "[shadowmap][csm] target clear %s: non-clear=%zu/%zu "
                        "range=%u..%u\n",
                label, nonClear, pixels, lo, hi);
        return nonClear == 0;
    };

    const bool staticClear = clear_layers(g_cascadeStaticFbo, g_cascadeStaticTex,
                                           "static");
    const bool dynamicClear = clear_layers(g_cascadeDynamicFbo, g_cascadeDynamicTex,
                                            "dynamic");
    const GLenum err = gl::GetError();
    gl::BindTexture(GL_TEXTURE_2D_ARRAY, (GLuint)oldArray);
    restore_state(st);
    fprintf(stderr, "[shadowmap][csm] target clear validation: %s (GL=0x%x; "
                    "no engine draws)\n",
            staticClear && dynamicClear && err == GL_NO_ERROR ? "READY" : "FAILED", err);
}

// Phase 3f: a deliberately narrow in-sequence bridge.  The native draw is
// first allowed to complete normally; the wrapper then repeats that exact
// driver call into private dynamic layer 0 while its program, VAO and uniforms
// are still live.  It never edits the camera, matrix stack, shader, material,
// or displayed framebuffer.  Matching the normal-pass viewport aspect keeps
// this first camera-depth PGM meaningful instead of stretching it into the
// square shadow-map allocation.
static bool begin_cascade_camera_capture(SavedState& st) {
    if (!g_cascadeCameraCapture || !g_cascadeCameraCaptureActive ||
        !g_cascadeTargetsUsable || !g_cascadeDynamicFbo ||
        !g_cascadeDynamicTex || !g_shadowFrameContext.valid ||
        !gl::FramebufferTextureLayer)
        return false;

    st.fbo = 0;
    gl::GetIntegerv(GL_FRAMEBUFFER_BINDING, &st.fbo);
    gl::GetIntegerv(GL_VIEWPORT, st.vp);
    gl::GetBooleanv(GL_DEPTH_WRITEMASK, &st.depthMask);
    gl::GetBooleanv(GL_COLOR_WRITEMASK, st.colorMask);
    st.scissor = gl::IsEnabled(GL_SCISSOR_TEST);
    st.depthTest = gl::IsEnabled(GL_DEPTH_TEST);

    gl::BindFramebuffer(GL_FRAMEBUFFER, g_cascadeDynamicFbo);
    gl::FramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                g_cascadeDynamicTex, 0, 0);
    const int srcW = std::max(1, g_shadowFrameContext.viewport[2]);
    const int srcH = std::max(1, g_shadowFrameContext.viewport[3]);
    const int dstH = std::max(1, std::min(g_size, (g_size * srcH) / srcW));
    gl::Viewport(0, 0, g_size, dstH);
    if (st.scissor) gl::Disable(GL_SCISSOR_TEST);
    if (!st.depthMask) gl::DepthMask(GL_TRUE);
    if (!st.depthTest) gl::Enable(GL_DEPTH_TEST);

    if (g_cascadeCameraCaptureFrame != g_shadowFrameContext.serial) {
        g_cascadeCameraCaptureFrame = g_shadowFrameContext.serial;
        g_cascadeCameraCaptureDraws = 0;
        gl::ClearDepth(1.0);
        gl::Clear(GL_DEPTH_BUFFER_BIT);
    }
    return true;
}

static void end_cascade_camera_capture(const SavedState& st) {
    restore_state(st);
}

static bool cascade_light_capture_bucket_selected(int bucket) {
    if (g_cascadeLightCaptureBuckets.empty())
        return bucket == g_cascadeLightCaptureBucket;
    return std::find(g_cascadeLightCaptureBuckets.begin(),
                     g_cascadeLightCaptureBuckets.end(), bucket) !=
           g_cascadeLightCaptureBuckets.end();
}

// Phase 3h uses the same private array as the camera proof, but the cascade
// fit is square.  The original normal draw is always completed first; this
// helper only binds the private depth layer for the duplicate that follows.
static bool begin_cascade_light_capture(SavedState& st) {
    const bool staticTarget = g_cascadeStaticReceiver && !g_cascadeDynamicTargetActive;
    const int captureLayer = g_cascadeMultiLayerCapture ? g_cascadeReplayLayer
                                                         : g_cascadeLightCaptureLayer;
    const GLuint captureFbo = staticTarget ? g_cascadeStaticFbo : g_cascadeDynamicFbo;
    const GLuint captureTex = staticTarget ? g_cascadeStaticTex : g_cascadeDynamicTex;
    if (!g_cascadeLightCapture || !g_cascadeLightCaptureActive ||
        !g_cascadeTargetsUsable || !captureFbo || !captureTex ||
        !g_shadowFrameContext.valid ||
        !g_cascadeMath.valid ||
        g_cascadeMath.sourceFrame != g_shadowFrameContext.serial ||
        !gl::FramebufferTextureLayer)
        return false;

    st.fbo = 0;
    gl::GetIntegerv(GL_FRAMEBUFFER_BINDING, &st.fbo);
    gl::GetIntegerv(GL_VIEWPORT, st.vp);
    gl::GetBooleanv(GL_DEPTH_WRITEMASK, &st.depthMask);
    gl::GetBooleanv(GL_COLOR_WRITEMASK, st.colorMask);
    st.scissor = gl::IsEnabled(GL_SCISSOR_TEST);
    st.depthTest = gl::IsEnabled(GL_DEPTH_TEST);

    gl::BindFramebuffer(GL_FRAMEBUFFER, captureFbo);
    gl::FramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                captureTex, 0,
                                captureLayer);
    gl::Viewport(0, 0, g_size, g_size);
    if (st.scissor) gl::Disable(GL_SCISSOR_TEST);
    if (!st.depthMask) gl::DepthMask(GL_TRUE);
    if (!st.depthTest) gl::Enable(GL_DEPTH_TEST);

    // Static and dynamic depth arrays are independent images.  Clear each
    // once per frozen Scene::Render serial, not once globally: bucket 0 is
    // encountered before bucket 2 and otherwise leaves dynamic layer 2 full
    // of last-frame depth.
    unsigned& targetFrame = g_cascadeMultiLayerCapture
        ? (staticTarget ? g_cascadeStaticCaptureFrameLayer[captureLayer]
                        : g_cascadeDynamicCaptureFrameLayer[captureLayer])
        : (staticTarget ? g_cascadeStaticCaptureFrame : g_cascadeDynamicCaptureFrame);
    unsigned& targetDraws = g_cascadeMultiLayerCapture
        ? (staticTarget ? g_cascadeStaticCaptureDrawsLayer[captureLayer]
                        : g_cascadeDynamicCaptureDrawsLayer[captureLayer])
        : (staticTarget ? g_cascadeStaticCaptureDraws : g_cascadeDynamicCaptureDraws);
    if (targetFrame != g_shadowFrameContext.serial) {
        targetFrame = g_shadowFrameContext.serial;
        targetDraws = 0;
        if (g_cascadeMultiLayerCapture && !staticTarget) {
            g_cascadeDynamicBodyCaptureDrawsLayer[captureLayer] = 0;
            g_cascadeDynamicAlphaCaptureDrawsLayer[captureLayer] = 0;
        }
        gl::ClearDepth(1.0);
        gl::Clear(GL_DEPTH_BUFFER_BIT);
    }
    // Keep the legacy aggregate counters for the PGM diagnostics.  They do
    // not decide whether either receiver layer is fresh.
    if (g_cascadeLightCaptureFrame != g_shadowFrameContext.serial) {
        g_cascadeLightCaptureFrame = g_shadowFrameContext.serial;
        g_cascadeLightCaptureDraws = 0;
        g_cascadeLightCaptureSkipped = 0;
        g_cascadeLightCaptureAttempts = 0;
        g_cascadeLightCaptureNotReady = 0;
    }
    return true;
}

static inline void note_cascade_light_draws(unsigned count) {
    g_cascadeLightCaptureDraws += count;
    if (g_cascadeMultiLayerCapture) {
        if (g_cascadeDynamicTargetActive) {
            g_cascadeDynamicCaptureDrawsLayer[g_cascadeReplayLayer] += count;
            if (g_cascadeDynamicAlphaTargetActive)
                g_cascadeDynamicAlphaCaptureDrawsLayer[g_cascadeReplayLayer] += count;
            else
                g_cascadeDynamicBodyCaptureDrawsLayer[g_cascadeReplayLayer] += count;
        } else
            g_cascadeStaticCaptureDrawsLayer[g_cascadeReplayLayer] += count;
    } else if (g_cascadeDynamicTargetActive) {
        g_cascadeDynamicCaptureDraws += count;
    } else {
        g_cascadeStaticCaptureDraws += count;
    }
}

// Phase 6c bucket replay: counts draws for replay_bucket_into_cascade_layers()
// instead (see g_cascadeReplayDrawCount). A no-op unless a replay is active,
// so it is safe to call unconditionally from every draw wrapper below.
static inline void note_local_light_draws(unsigned count) {
    if (!g_localLightPassActive) return;
    if (g_localLightCurrentBucket >= 0 && g_localLightCurrentBucket < 4)
        g_localLightBucketDraws[g_localLightCurrentBucket] += count;
}

// Per-bucket split of both sides of the ledger. NWN renders buckets
// 0,1,2,3,4,6,7,8; the cascade replay only ever visits 0,1,2,3, so 4/6/7/8 are
// already excluded from casting. Which bucket holds the emitter particles
// decides whether "exclude the emitters" is a change at all -- and a total
// draw count cannot answer that. Area scene only (nwn_core::g_currentBucket is
// left alone for UI/portrait scenes).
static unsigned g_engineBucketDraws[kBucketCount] = {};
static unsigned g_replayBucketDraws[kBucketCount] = {};
static unsigned g_engineBucketIdx[kBucketCount]   = {};
static unsigned g_replayBucketIdx[kBucketCount]   = {};
static int      g_cascadeReplayBucket = -1;

static inline void note_cascade_replay_draws(unsigned count) {
    if (g_cascadeReplayActive) {
        g_cascadeReplayDrawCount += count;
        if (g_cascadeReplayBucket >= 0 && g_cascadeReplayBucket < kBucketCount)
            g_replayBucketDraws[g_cascadeReplayBucket] += count;
    } else {
        g_frameDrawCalls += count;   // draws the ENGINE itself is issuing
        const int b = nwn_core::g_currentBucket;
        if (b >= 0 && b < kBucketCount) g_engineBucketDraws[b] += count;
    }
}

static void end_cascade_light_capture(const SavedState& st) {
    restore_state(st);
}

// Phase 6c bucket-level replay (see the flag comment above g_cascadeBucketReplay).
// Binds the target cascade array/layer ONCE per layer -- not once per draw --
// then replays the WHOLE bucket via guarded_render_bucket(), the same
// re-entrant call already proven safe by render_from_light() and the
// local-light probe.
//
// HOW THE LIGHT TRANSFORM IS APPLIED, and why the obvious approach failed:
// the first version substituted each object's light-space m_mv/m_proj by
// intercepting glUniformMatrix4fv during the replay. Measured result
// (`[shadowmap][replay] ... sub-mv=` counters): STATIC buckets reported
// sub-mv=0 on every layer INCLUDING layer 0, while dynamic buckets reported
// a full count. The engine dirty-checks its uniform uploads -- a static
// object's model matrix is unchanged when we re-draw it, so NWN uploads
// nothing and there is nothing to intercept; skinned/animated geometry
// recomputes every draw, so it always uploads. Static geometry therefore
// rendered with the CAMERA transform into all four layers (byte-identical
// layers, no usable shadows).
// The fix is the mechanism this project already validated in Phase 3 for
// render_from_light(): write the light view/projection DIRECTLY into the
// engine's matrix stack and let NWN derive and upload each object's matrices
// itself. Changing the view also makes those uploads genuinely dirty, so the
// per-object values arrive correctly for static and dynamic alike -- which is
// exactly why no uniform interception is needed (or wanted: doing both would
// double-transform).
// Create the world-anchored static target. Separate from the cascade arrays:
// it is one 2D depth map, sized by the user, and is written once per area.
// These live further down beside the other matrix/vector helpers; forward
// declare rather than reorder a 7000-line file.
static void build_lookat(const Vec3f& eye, const float f[3], float m[16]);
static void dump_local_light_depth(const char* path);
static float* mtx_entry(int idx);

static void create_static_world_target() {
    if (g_staticWorldFbo || !g_staticWorldEnabled) return;
    GLint prevFbo = 0, prevTex = 0;
    gl::GetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    gl::GetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    while (gl::GetError() != GL_NO_ERROR) {}

    gl::GenTextures(1, &g_staticWorldTex);
    gl::BindTexture(GL_TEXTURE_2D, g_staticWorldTex);
    gl::TexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24,
                   g_staticWorldSize, g_staticWorldSize, 0,
                   GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LESS);
    const GLenum errTex = gl::GetError();

    gl::GenFramebuffers(1, &g_staticWorldFbo);
    gl::BindFramebuffer(GL_FRAMEBUFFER, g_staticWorldFbo);
    gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                             GL_TEXTURE_2D, g_staticWorldTex, 0);
    gl::DrawBuffer(GL_NONE);
    gl::ReadBuffer(GL_NONE);
    const GLenum status = gl::CheckFramebufferStatus(GL_FRAMEBUFFER);
    g_staticWorldUsable = (status == GL_FRAMEBUFFER_COMPLETE) &&
                          errTex == GL_NO_ERROR && gl::GetError() == GL_NO_ERROR;
    gl::BindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
    gl::BindTexture(GL_TEXTURE_2D, (GLuint)prevTex);
    fprintf(stderr, "[shadowmap][world] static world target %dx%d extent=%.0f "
                    "(%.1f cm/texel) status=%s usable=%s\n",
            g_staticWorldSize, g_staticWorldSize, g_staticWorldExtent,
            (2.0f * g_staticWorldExtent / (float)g_staticWorldSize) * 100.0f,
            fbo_status_str(status), g_staticWorldUsable ? "yes" : "no");
    if (!g_staticWorldUsable)
        fprintf(stderr, "[shadowmap][world] allocation failed -- most likely the size is "
                        "beyond this GPU's texture limit or VRAM; try a smaller "
                        "NWN_SHADOWMAP_STATIC_WORLD_SIZE\n");
}

// Apply a resolution change staged by the overlay. Textures are deleted and
// the create_* helpers rebuild them at the new size on next use; the cascade
// freshness stamps are reset so the receiver suppresses itself for a frame
// rather than sampling a target that no longer exists.
static void apply_resolution_change() {
    if (!g_resolutionApplyRequested) return;
    g_resolutionApplyRequested = false;
    // The local-light array is owned by the capture, which reallocates it at a
    // point where nothing is bound; just hand the request over.
    if (g_pendingLocalSize != g_localLightCaptureSize) g_localSizeCommit = true;

    if (g_pendingCascadeSize > 0 && g_pendingCascadeSize != g_size) {
        fprintf(stderr, "[shadowmap][csm] cascade map %d^2 -> %d^2 (reallocating)\n",
                g_size, g_pendingCascadeSize);
        if (g_cascadeStaticFbo)  gl::DeleteFramebuffers(1, &g_cascadeStaticFbo);
        if (g_cascadeDynamicFbo) gl::DeleteFramebuffers(1, &g_cascadeDynamicFbo);
        if (g_cascadeStaticTex)  gl::DeleteTextures(1, &g_cascadeStaticTex);
        if (g_cascadeDynamicTex) gl::DeleteTextures(1, &g_cascadeDynamicTex);
        g_cascadeStaticFbo = g_cascadeDynamicFbo = 0;
        g_cascadeStaticTex = g_cascadeDynamicTex = 0;
        g_cascadeTargetsUsable = false;
        for (int i = 0; i < kCascadeCount; ++i) {
            g_cascadeStaticCaptureFrameLayer[i]  = 0;
            g_cascadeStaticCaptureDrawsLayer[i]  = 0;
            g_cascadeDynamicCaptureFrameLayer[i] = 0;
            g_cascadeDynamicCaptureDrawsLayer[i] = 0;
            for (int b = 0; b < 16; ++b) g_cascadeStaticGeneration[b][i] = 0;
        }
        g_size = g_pendingCascadeSize;
        create_cascade_targets();
    }

    if (g_pendingWorldExtent > 0.0f && g_pendingWorldExtent != g_staticWorldExtent) {
        fprintf(stderr, "[shadowmap][world] extent %.0f -> %.0f (rebuilding in place; "
                        "no area reload needed)\n",
                g_staticWorldExtent, g_pendingWorldExtent);
        g_staticWorldExtent = g_pendingWorldExtent;
        g_staticWorldValid  = false;
        g_staticWorldDirty  = true;
        g_fullBspWanted     = true;   // the rebuild needs the complete caster set
    }
    if (g_pendingWorldSize > 0 && g_pendingWorldSize != g_staticWorldSize) {
        fprintf(stderr, "[shadowmap][world] static world map %d^2 -> %d^2 (reallocating)\n",
                g_staticWorldSize, g_pendingWorldSize);
        if (g_staticWorldFbo) gl::DeleteFramebuffers(1, &g_staticWorldFbo);
        if (g_staticWorldTex) gl::DeleteTextures(1, &g_staticWorldTex);
        g_staticWorldFbo = 0;
        g_staticWorldTex = 0;
        g_staticWorldUsable = false;
        g_staticWorldValid  = false;
        g_staticWorldDirty  = true;      // force a re-render at the new size
        g_staticWorldSize = g_pendingWorldSize;
    }
}

// Re-render only when the map genuinely cannot describe the scene any more.
static bool static_world_needs_refresh(const ShadowFrameContext& ctx) {
    if (!g_staticWorldValid) return true;
    // NEW AREA -- and the Scene pointer alone CANNOT tell you that.
    //
    // The engine REUSES the Scene across areas: walking out of an area and back
    // in yields the same pointer. This check therefore never fired, and the
    // world-anchored map kept the PREVIOUS area's geometry -- reported
    // 2026-08-15 as shadows that appear after leaving an area and returning,
    // most visible when zooming out and panning. They are real shadows from the
    // wrong area, which is why it looked like a fog bug and was not one.
    //
    // The area's own CAurObject IS per-area (it is [area+0x250], the object the
    // shadow toggles are called with), and re-keying on it is exactly the fix
    // the area-policy memory needed the same day for the same reason.
    if (g_staticWorldScene != ctx.scene) return true;
    if (g_areaToggleObject && g_staticWorldArea != g_areaToggleObject) return true;
    // Compare against the SAME direction the cascades fit to, not eng::SunDir.
    //
    // The normal-pass vector is the same direction the regular CSM fit uses;
    // the legacy exported global is not always populated by a loaded area.
    // The static map remains stable for this area-light snapshot.
    {
        const float* sun = (!g_dirOverride && g_haveTraceAreaLight) ? g_traceAreaLightDir
                         : (eng::SunDir ? eng::SunDir : nullptr);
        // HYSTERESIS + RATE LIMIT. A 1e-3 threshold is finer than a day/night
        // cycle's per-frame step, so the map rebuilt EVERY FRAME once the sun
        // started moving -- at Mega High that is a 16384^2 render per frame and
        // the frame rate collapses. The map is a coarse area-wide approximation;
        // it does not need to track a degree of sun movement, and it must never
        // rebuild faster than it costs.
        if (sun) {
            float moved = 0.0f;
            for (int i = 0; i < 3; ++i)
                moved = std::max(moved, std::fabs(sun[i] - g_staticWorldSun[i]));
            if (moved > 0.05f) {                    // ~3 degrees
                const double now = now_seconds();
                if (now >= g_staticWorldSunRebuildAt) {
                    g_staticWorldSunRebuildAt = now + 2.0;   // at most every 2 s
                    return true;
                }
            }
        }
    }
    // Walked far enough that the box no longer safely contains the view.
    const float dx = ctx.eye.x - g_staticWorldCentre[0];
    const float dy = ctx.eye.y - g_staticWorldCentre[1];
    const float limit = g_staticWorldExtent * 0.6f;
    return (dx*dx + dy*dy) > (limit * limit);
}

// Render the world-anchored static map. Extracted from the replay so the
// HYBRID path can call it and then still populate the near cascade layers;
// previously this block ended in a bare return and owned the whole static
// path, which made near-field crispness impossible.
static void render_static_world_map(void* scene, int bucket) {
// World-anchored static: render the whole static set ONCE into a
// world-space map instead of once per cascade layer per frame. Everything
// below (the cascade fan-out) is then skipped for static buckets entirely.
    if (!g_staticWorldFbo) create_static_world_target();
    if (!g_staticWorldUsable) return;
    const ShadowFrameContext& ctx = g_shadowFrameContext;
    // Rebuild ONLY on a frame where the full caster set was actually
    // submitted, otherwise the map would be built from the camera-culled
    // set and lose every off-screen caster. If a rebuild is due but the set
    // is not present, request it and wait one frame.
    const bool wantRebuild = (g_staticWorldDirty || static_world_needs_refresh(ctx)) &&
                             g_staticWorldRefreshSerial != ctx.serial;
    if (wantRebuild && g_fullBspEnabled && !g_fullBspSubmittedFrame) {
        g_fullBspWanted = true;      // granted next frame; costs one frame of full draws
        return;
    }
    const bool startRefresh = wantRebuild;
    const bool inRefresh = startRefresh || g_staticWorldRefreshSerial == ctx.serial;
    if (!inRefresh) return;      // map still valid: nothing to do this frame

    float* vm = mtx_entry(MTX_VIEW);
    float* pm = mtx_entry(MTX_PROJ);
    if (!vm || !pm) return;

    float dir[3] = { 0.0f, 0.0f, -1.0f };
    if (eng::SunDir) {
        const float l = std::sqrt(eng::SunDir[0]*eng::SunDir[0] +
                                  eng::SunDir[1]*eng::SunDir[1] +
                                  eng::SunDir[2]*eng::SunDir[2]);
        if (l > 1e-4f)
            for (int i = 0; i < 3; ++i) dir[i] = eng::SunDir[i] * g_dirSign;
    }
    v_normalize(dir);

    float worldView[16], worldProj[16];
    if (startRefresh) {
        // Re-centre on the camera and snap to the texel grid: without the
        // snap, re-centring would shimmer every edge in the map.
        const float texel = (2.0f * g_staticWorldExtent) / (float)g_staticWorldSize;
        g_staticWorldCentre[0] = std::round(ctx.eye.x / texel) * texel;
        g_staticWorldCentre[1] = std::round(ctx.eye.y / texel) * texel;
        g_staticWorldCentre[2] = ctx.eye.z;
        // Cache what we actually fitted to, or the comparison above can never
        // settle (it would compare a trace vector against an engine one).
        {
            const float* sun = (!g_dirOverride && g_haveTraceAreaLight) ? g_traceAreaLightDir
                             : (eng::SunDir ? eng::SunDir : nullptr);
            if (sun) for (int i = 0; i < 3; ++i) g_staticWorldSun[i] = sun[i];
        }
        g_staticWorldScene = ctx.scene;
        g_staticWorldArea  = g_areaToggleObject;
        g_staticWorldRefreshSerial = ctx.serial;
    }
    const float dist = g_staticWorldExtent * 2.0f;
    Vec3f eye{ g_staticWorldCentre[0] - dir[0] * dist,
               g_staticWorldCentre[1] - dir[1] * dist,
               g_staticWorldCentre[2] - dir[2] * dist, 1.0f };
    build_lookat(eye, dir, worldView);
    build_ortho(g_staticWorldExtent, 1.0f, dist * 2.0f + g_staticWorldExtent * 2.0f, worldProj);
    mat4_mul(worldProj, worldView, g_staticWorldVP);

    float savedView[16], savedProj[16];
    std::memcpy(savedView, vm, sizeof(savedView));
    std::memcpy(savedProj, pm, sizeof(savedProj));
    const int oldMode = eng::CurMtxIdx ? *eng::CurMtxIdx : 0;
    std::memcpy(vm, worldView, sizeof(worldView));
    std::memcpy(pm, worldProj, sizeof(worldProj));
    if (eng::SetMatrixMode) eng::SetMatrixMode(MTX_VIEW);
    if (eng::FlagDirty)     eng::FlagDirty();
    if (eng::SetMatrixMode) eng::SetMatrixMode(MTX_PROJ);
    if (eng::FlagDirty)     eng::FlagDirty();
    if (eng::SetMatrixMode) eng::SetMatrixMode(oldMode);

    SavedState ws;
    gl::GetIntegerv(GL_FRAMEBUFFER_BINDING, &ws.fbo);
    gl::GetIntegerv(GL_VIEWPORT, ws.vp);
    gl::GetBooleanv(GL_DEPTH_WRITEMASK, &ws.depthMask);
    gl::GetBooleanv(GL_COLOR_WRITEMASK, ws.colorMask);
    ws.scissor = gl::IsEnabled(GL_SCISSOR_TEST);
    ws.depthTest = gl::IsEnabled(GL_DEPTH_TEST);
    gl::BindFramebuffer(GL_FRAMEBUFFER, g_staticWorldFbo);
    gl::Viewport(0, 0, g_staticWorldSize, g_staticWorldSize);
    if (ws.scissor)    gl::Disable(GL_SCISSOR_TEST);
    if (!ws.depthMask) gl::DepthMask(GL_TRUE);
    if (!ws.depthTest) gl::Enable(GL_DEPTH_TEST);
    // Clear only when the refresh starts; later static buckets in the same
    // frame accumulate into the same map.
    if (startRefresh) { gl::ClearDepth(1.0); gl::Clear(GL_DEPTH_BUFFER_BIT); }

    g_cascadeReplayDrawCount = 0;
    g_cascadeReplayActive = true;
    g_cascadeReplayLayer = 0;
    const double tWorld0 = now_seconds();
    guarded_render_bucket(scene, bucket);
    g_msWorldMap += (now_seconds() - tWorld0) * 1000.0;
    g_cascadeReplayActive = false;

    restore_state(ws);
    std::memcpy(vm, savedView, sizeof(savedView));
    std::memcpy(pm, savedProj, sizeof(savedProj));
    if (eng::SetMatrixMode) eng::SetMatrixMode(MTX_VIEW);
    if (eng::FlagDirty)     eng::FlagDirty();
    if (eng::SetMatrixMode) eng::SetMatrixMode(MTX_PROJ);
    if (eng::FlagDirty)     eng::FlagDirty();
    if (eng::SetMatrixMode) eng::SetMatrixMode(oldMode);

    g_staticWorldValid = true;
    g_staticWorldDirty = false;
    g_fullBspWanted = false;   // complete set consumed; stop inflating the engine's draws
    if (startRefresh && ++g_staticWorldRenders <= 12)
        fprintf(stderr, "[shadowmap][world] static map rendered #%u at "
                        "centre=(%.1f %.1f) draws=%u -- reused until the area, sun "
                        "or a %.0f-unit move invalidates it\n",
                g_staticWorldRenders, g_staticWorldCentre[0], g_staticWorldCentre[1],
                g_cascadeReplayDrawCount, g_staticWorldExtent * 0.6f);
    return;
}


// ---------------------------------------------------------------------------
//  SUN-PATH local light fill
// ---------------------------------------------------------------------------
// The cascade replay's shape, aimed at the local light layers instead. Called
// from the Scene::RenderDrawBucket hook right after CALL_ORIGINAL, so it rides
// the engine's OWN visible camera traversal.
//
// That timing is the entire point. The standalone replay in
// capture_local_light_shadow() re-invokes the bucket render outside the
// engine's sequence and therefore picks up whatever is staged at that moment --
// which is where the stencil proxies come from, and why it needs the
// "render 0/1 first, then clear" warm-up to make buckets 2/3 draw at all.
// Here the engine has just rendered this bucket for the camera, so the casters
// are exactly what is visible and no warm-up is required.
//
// Everything else is unchanged from the other tiers: the same per-light
// view/projection computed in capture_local_light_shadow(), the same layer per
// light, the same receiver.
static void replay_bucket_into_local_light_layers(void* scene, int bucket) {
    // MASTER SWITCH. "Draw shadows at all" used to gate only the receiver draw,
    // so switching it off stopped the shadows appearing while every bucket was
    // still replayed into every layer -- the expensive half kept running and the
    // frame rate did not recover. It has to be checked HERE, where the work is.
    if (!g_receiverEnabled) return;
    // "Moving casters only". Buckets 0/1 are the static world, 2/3 the dynamic
    // geometry (2 = bodies, 3 = the alpha/dither cards).
    //
    // Under the OLD standalone replay this could not be done by selection --
    // the engine refused to draw 2/3 cold, so it had to render the static
    // buckets first and wipe the depth between, and the flag drove that. That
    // code went with the tier removal and the flag has done NOTHING since.
    //
    // Riding the engine's own pass makes it trivial: it is already drawing
    // every bucket for the camera, so we simply decline to replay the static
    // ones. No warm-up, no clear-between.
    if (g_localLightDynamicOnly && bucket < 2) return;
    if (!g_localInlinePrepared || !g_localLightTargetUsable ||
        !g_localLightDepthTex || !gl::FramebufferTextureLayer ||
        !g_shadowFrameContext.valid)
        return;
    if (g_localInlinePolicyEpoch != g_localLightCapturePolicyEpoch) return;
    const unsigned slots = g_localInlineSlotCount;
    if (slots == 0) return;

    // The light transform goes in through the engine's matrix stack, exactly as
    // the cascades do it. Without both entries we cannot build a light view at
    // all, so bail rather than fill the map with camera-space depth.
    float* vm = mtx_entry(MTX_VIEW);
    float* pm = mtx_entry(MTX_PROJ);
    if (!vm || !pm) return;

    // Query the outer (real scene) state ONCE for the whole bucket, not per
    // layer, and restore it once at the end.
    SavedState outer;
    gl::GetIntegerv(GL_FRAMEBUFFER_BINDING, &outer.fbo);
    gl::GetIntegerv(GL_VIEWPORT, outer.vp);
    gl::GetBooleanv(GL_DEPTH_WRITEMASK, &outer.depthMask);
    gl::GetBooleanv(GL_COLOR_WRITEMASK, outer.colorMask);
    outer.scissor   = gl::IsEnabled(GL_SCISSOR_TEST);
    outer.depthTest = gl::IsEnabled(GL_DEPTH_TEST);

    gl::BindFramebuffer(GL_FRAMEBUFFER, g_localLightFbo);
    gl::Viewport(0, 0, g_localLightCaptureSize, g_localLightCaptureSize);
    if (outer.scissor)    gl::Disable(GL_SCISSOR_TEST);
    if (!outer.depthMask) gl::DepthMask(GL_TRUE);
    gl::ColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);   // depth-only target
    if (!outer.depthTest) gl::Enable(GL_DEPTH_TEST);

    // BYTE-EXACT save/restore of the camera matrices. Reconstructing them
    // afterwards is what caused the old camera-corruption bugs; this is the
    // project's standing rule for this path.
    float savedView[16], savedProj[16];
    std::memcpy(savedView, vm, sizeof(savedView));
    std::memcpy(savedProj, pm, sizeof(savedProj));
    const int oldMode = eng::CurMtxIdx ? *eng::CurMtxIdx : 0;

    // Without this the re-entered bucket detour treats each replay below as an
    // ordinary engine draw and launches a whole cascade replay for it.
    const bool priorPass = g_localLightPassActive;
    g_localLightPassActive = true;

    for (unsigned layer = 0; layer < slots && layer < (unsigned)kLocalLightFaces; ++layer) {
        gl::FramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                    g_localLightDepthTex, 0, (int)layer);
        // Clear ONCE per frame, on the first bucket that touches this layer --
        // clearing per bucket would erase the previous bucket's casters.
        //
        // STRUCTURAL NOTE, not a known bug: this clears in-band and refills as
        // the engine walks its buckets, so a frame whose refill does not
        // complete would leave the layer empty for that frame. The old cadence
        // path could not do that -- it published a whole generation or nothing.
        // Investigated 2026-08-15 after a chop report; the maintainer confirmed
        // the chop predates all of this, so nothing here is known to misbehave.
        // If per-frame dropouts ever DO appear, this is the first suspect and
        // the fix is to hold the previous generation until the refill lands.
        if (g_localInlineLayerFrame[layer] != g_shadowFrameContext.serial) {
            g_localInlineLayerFrame[layer] = g_shadowFrameContext.serial;
            g_localInlineDraws[layer] = 0;
            gl::ClearDepth(1.0);
            gl::Clear(GL_DEPTH_BUFFER_BIT);
        }
        std::memcpy(vm, g_localVisibleView[layer], sizeof(float) * 16);
        std::memcpy(pm, g_localVisibleProj[layer], sizeof(float) * 16);
        // Flag BOTH stacks dirty so NWN recomputes each object's derived
        // matrices, then put the mode BACK before drawing: leaving it on the
        // view stack makes per-object model transforms land there instead.
        if (eng::SetMatrixMode) eng::SetMatrixMode(MTX_VIEW);
        if (eng::FlagDirty)     eng::FlagDirty();
        if (eng::SetMatrixMode) eng::SetMatrixMode(MTX_PROJ);
        if (eng::FlagDirty)     eng::FlagDirty();
        if (eng::SetMatrixMode) eng::SetMatrixMode(oldMode);

        g_localLightCurrentBucket = bucket;
        if (guarded_render_bucket(scene, bucket)) ++g_localInlineDraws[layer];
        g_localLightCurrentBucket = -1;
    }

    g_localLightPassActive = priorPass;

    if (eng::SetMatrixMode) eng::SetMatrixMode(MTX_VIEW);
    std::memcpy(vm, savedView, sizeof(savedView));
    if (eng::FlagDirty)     eng::FlagDirty();
    if (eng::SetMatrixMode) eng::SetMatrixMode(MTX_PROJ);
    std::memcpy(pm, savedProj, sizeof(savedProj));
    if (eng::FlagDirty)     eng::FlagDirty();
    if (eng::SetMatrixMode) eng::SetMatrixMode(oldMode);

    restore_state(outer);
}

static void replay_bucket_into_cascade_layers(void* scene, int bucket, bool staticTarget) {
    // MASTER SWITCH, same reason as the local replay: with the receiver off
    // nothing samples these layers, so filling them is pure cost. This is the
    // sun's four-layer replay -- measured at 1036 draws -- and leaving it
    // running was most of why "Draw shadows at all" did not restore the frame
    // rate.
    if (!g_receiverEnabled) return;
    if (!g_cascadeTargetsUsable || !gl::FramebufferTextureLayer ||
        !g_shadowFrameContext.valid || !g_cascadeMath.valid ||
        g_cascadeMath.sourceFrame != g_shadowFrameContext.serial)
        return;

    if (g_staticWorldEnabled && staticTarget) {
        render_static_world_map(scene, bucket);
        // HYBRID: fall through to populate the NEAR cascade layers too, using
        // the engine's normal camera-culled set (near casters are on screen, so
        // culling costs nothing here and the expensive full-BSP submission is
        // not needed). 0 = world map only, the previous behaviour.
        if (g_staticNearCascades <= 0) return;
    }

    const GLuint fbo = staticTarget ? g_cascadeStaticFbo : g_cascadeDynamicFbo;
    const GLuint tex = staticTarget ? g_cascadeStaticTex : g_cascadeDynamicTex;
    if (!fbo || !tex) return;

    // The light transform is applied through the matrix stack (see the header
    // comment). Without both entries we cannot render a correct light view at
    // all, so bail rather than silently filling the cascades with camera-space
    // depth again.
    float* vm = mtx_entry(MTX_VIEW);
    float* pm = mtx_entry(MTX_PROJ);
    if (!vm || !pm) return;

    if (g_cascadeLightCaptureFrame != g_shadowFrameContext.serial) {
        g_cascadeLightCaptureFrame = g_shadowFrameContext.serial;
        g_cascadeLightCaptureDraws = 0;
        g_cascadeLightCaptureSkipped = 0;
        g_cascadeLightCaptureAttempts = 0;
        g_cascadeLightCaptureNotReady = 0;
    }

    // Query the "outer" (real scene) state ONCE for this whole bucket replay
    // -- not once per draw per layer -- and restore it once at the end.
    SavedState outer;
    gl::GetIntegerv(GL_FRAMEBUFFER_BINDING, &outer.fbo);
    gl::GetIntegerv(GL_VIEWPORT, outer.vp);
    gl::GetBooleanv(GL_DEPTH_WRITEMASK, &outer.depthMask);
    gl::GetBooleanv(GL_COLOR_WRITEMASK, outer.colorMask);
    outer.scissor   = gl::IsEnabled(GL_SCISSOR_TEST);
    outer.depthTest = gl::IsEnabled(GL_DEPTH_TEST);

    gl::BindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl::Viewport(0, 0, g_size, g_size);
    if (outer.scissor)    gl::Disable(GL_SCISSOR_TEST);
    if (!outer.depthMask) gl::DepthMask(GL_TRUE);
    if (!outer.depthTest) gl::Enable(GL_DEPTH_TEST);

    // Byte-exact save of the real camera matrices, restored after the last
    // layer. Byte-exact save/restore is the project's established rule here:
    // reconstructing the camera afterwards is what caused the old
    // camera-corruption bugs.
    float savedView[16], savedProj[16];
    std::memcpy(savedView, vm, sizeof(savedView));
    std::memcpy(savedProj, pm, sizeof(savedProj));
    const int oldMode = eng::CurMtxIdx ? *eng::CurMtxIdx : 0;

    int activeLayers = (g_cascadeActiveCount < 1) ? 1
                     : (g_cascadeActiveCount > kCascadeCount ? kCascadeCount
                                                             : g_cascadeActiveCount);
    if (!staticTarget) {
        // "Moving casters" off: skip the dynamic buckets entirely. In an area
        // whose geometry ROTATES, NWN classifies nearly everything as dynamic
        // (measured: 1827 draws in bucket 2 against 215 in static bucket 0), so
        // the static cache has almost nothing to cache and this is the only
        // lever left. Costs the shadows of anything that moves, characters
        // included -- it is a real trade, not a free win.
        if (!g_dynamicCastersEnabled) return;
        // Dynamic casters only populate the nearest layers (see
        // g_cascadeDynamicLayers). 0 means they cast nothing at all.
        const int dyn = g_cascadeDynamicLayers < 0 ? 0 : g_cascadeDynamicLayers;
        if (dyn == 0) return;
        if (dyn < activeLayers) activeLayers = dyn;
    } else {
        // "Fixed casters" off. Together with "Moving casters" off this takes
        // the replay to nothing at all, which is the clean A/B: if the frame
        // rate does not move with ZERO replay draws, the replay was never the
        // cost and the search belongs somewhere else entirely.
        if (!g_staticCastersEnabled) return;
        if (g_staticWorldEnabled) {
            // Hybrid: only the near layers get camera-fitted static casters; the
            // world map covers everything beyond them.
            if (g_staticNearCascades < activeLayers) activeLayers = g_staticNearCascades;
        }
    }
    for (int layer = 0; layer < activeLayers; ++layer) {
        gl::FramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, tex, 0, layer);

        // This cascade's light view/projection, straight into the stack.
        std::memcpy(vm, g_cascadeMath.lightView[layer], sizeof(float) * 16);
        std::memcpy(pm, g_cascadeMath.lightProjection[layer], sizeof(float) * 16);
        // Flag BOTH stacks dirty so NWN recomputes every object's derived
        // matrices, then put the current mode BACK before drawing: leaving it
        // on the view stack makes per-object model transforms land there
        // instead (the Phase 3 bug that produced sliver depth maps).
        if (eng::SetMatrixMode) eng::SetMatrixMode(MTX_VIEW);
        if (eng::FlagDirty)     eng::FlagDirty();
        if (eng::SetMatrixMode) eng::SetMatrixMode(MTX_PROJ);
        if (eng::FlagDirty)     eng::FlagDirty();
        if (eng::SetMatrixMode) eng::SetMatrixMode(oldMode);

        unsigned& targetFrame = staticTarget ? g_cascadeStaticCaptureFrameLayer[layer]
                                              : g_cascadeDynamicCaptureFrameLayer[layer];
        unsigned& targetDraws = staticTarget ? g_cascadeStaticCaptureDrawsLayer[layer]
                                              : g_cascadeDynamicCaptureDrawsLayer[layer];
        // STATIC CACHE: this layer already holds depth rendered with the
        // current cascade fit, and static casters cannot have moved. Re-stamp
        // it as fresh and skip the entire replay -- this is where the draw
        // calls are actually saved. Dynamic layers always re-render.
        if (g_cascadeStaticCache && staticTarget &&
            bucket >= 0 && bucket < 16 &&
            g_cascadeStaticGeneration[bucket][layer] == g_cascadeMathGeneration &&
            g_cascadeMathGeneration != 0 && targetDraws > 0) {
            targetFrame = g_shadowFrameContext.serial;
            continue;
        }
        if (targetFrame != g_shadowFrameContext.serial) {
            targetFrame = g_shadowFrameContext.serial;
            targetDraws = 0;
            if (!staticTarget) {
                g_cascadeDynamicBodyCaptureDrawsLayer[layer] = 0;
                g_cascadeDynamicAlphaCaptureDrawsLayer[layer] = 0;
            }
            gl::ClearDepth(1.0);
            gl::Clear(GL_DEPTH_BUFFER_BIT);
        }

        g_cascadeReplayDrawCount = 0;
        g_cascadeReplayActive = true;
        g_cascadeReplayLayer  = layer;
        ++g_cascadeLightCaptureAttempts;
        // How MANY replays a frame runs, and how many engine draws they issue.
        // `replay` is a sum over an unknown number of calls, so a platform
        // difference in the total says nothing about whether each call got
        // slower or there were simply more of them -- the two have completely
        // different causes (a fit-cache miss vs. per-draw overhead).
        ++g_replayCallsFrame;
        g_cascadeReplayBucket = bucket;
        const double tReplay0 = now_seconds();
        guarded_render_bucket(scene, bucket);
        if (g_costFinishThisFrame && gl::Finish) gl::Finish();
        g_msReplay += (now_seconds() - tReplay0) * 1000.0;
        g_cascadeReplayActive = false;
        g_cascadeReplayBucket = -1;
        g_replayDrawsFrame += g_cascadeReplayDrawCount;

        targetDraws += g_cascadeReplayDrawCount;
        g_cascadeLightCaptureDraws += g_cascadeReplayDrawCount;
        if (staticTarget && bucket >= 0 && bucket < 16)
            g_cascadeStaticGeneration[bucket][layer] = g_cascadeMathGeneration;
        if (!staticTarget) {
            if (bucket == g_cascadeAlphaBucket)
                g_cascadeDynamicAlphaCaptureDrawsLayer[layer] += g_cascadeReplayDrawCount;
            else
                g_cascadeDynamicBodyCaptureDrawsLayer[layer] += g_cascadeReplayDrawCount;
        }
    }

    // Byte-exact restore of the real camera matrices, then flag dirty so the
    // engine's next ordinary draw recomputes from them rather than from the
    // last cascade's light transform.
    std::memcpy(vm, savedView, sizeof(savedView));
    std::memcpy(pm, savedProj, sizeof(savedProj));
    if (eng::SetMatrixMode) eng::SetMatrixMode(MTX_VIEW);
    if (eng::FlagDirty)     eng::FlagDirty();
    if (eng::SetMatrixMode) eng::SetMatrixMode(MTX_PROJ);
    if (eng::FlagDirty)     eng::FlagDirty();
    if (eng::SetMatrixMode) eng::SetMatrixMode(oldMode);

    if (!outer.depthTest) gl::Disable(GL_DEPTH_TEST);
    if (!outer.depthMask) gl::DepthMask(GL_FALSE);
    if (outer.scissor)    gl::Enable(GL_SCISSOR_TEST);
    gl::ColorMask(outer.colorMask[0], outer.colorMask[1], outer.colorMask[2], outer.colorMask[3]);
    gl::Viewport(outer.vp[0], outer.vp[1], outer.vp[2], outer.vp[3]);
    gl::BindFramebuffer(GL_FRAMEBUFFER, (GLuint)outer.fbo);
}

static void dump_cascade_camera_capture_once() {
    if (!g_dumpCapturePgm) return;
    if (g_cascadeCameraCaptureDumped || !g_cascadeCameraCapture ||
        !g_cascadeCameraCaptureDraws || !g_cascadeDynamicTex ||
        !gl::GetTexImage) return;
    g_cascadeCameraCaptureDumped = true;

    GLint oldArray = 0;
    gl::GetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &oldArray);
    const size_t layerPixels = (size_t)g_size * (size_t)g_size;
    std::vector<uint32_t> layers(layerPixels * kCascadeCount);
    if (gl::Finish) gl::Finish();
    gl::BindTexture(GL_TEXTURE_2D_ARRAY, g_cascadeDynamicTex);
    gl::GetTexImage(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT,
                    GL_UNSIGNED_INT, layers.data());
    gl::BindTexture(GL_TEXTURE_2D_ARRAY, (GLuint)oldArray);

    const uint32_t* values = layers.data(); // dynamic cascade layer 0
    DepthStats stats{UINT32_MAX, 0, 0};
    for (size_t i = 0; i < layerPixels; ++i) {
        const uint32_t value = values[i];
        stats.lo = std::min(stats.lo, value);
        stats.hi = std::max(stats.hi, value);
        if (value != UINT32_MAX) ++stats.written;
    }
    FILE* f = open_dump_file("shadowmap_cascade_camera.pgm");
    if (!f) {
        fprintf(stderr, "[shadowmap][capture] cannot open shadowmap_cascade_camera.pgm\n");
        return;
    }
    fprintf(f, "P5\n%d %d\n255\n", g_size, g_size);
    const double span = stats.hi > stats.lo ? double(stats.hi) - stats.lo : 1.0;
    for (size_t i = 0; i < layerPixels; ++i)
        fputc((int)(255.0 * (double(values[i]) - stats.lo) / span), f);
    fclose(f);
    fprintf(stderr,
            "[shadowmap][capture] dumped shadowmap_cascade_camera.pgm frame=%u "
            "bucket=%d duplicate-draws=%u range=%u..%u written=%zu/%zu%s\n",
            g_cascadeCameraCaptureFrame, g_cascadeCameraCaptureBucket,
            g_cascadeCameraCaptureDraws, stats.lo, stats.hi, stats.written,
            layerPixels, stats.lo == stats.hi ? " (uniform - no depth)" : "");
}

static void dump_cascade_light_capture_once() {
    if (!g_dumpCapturePgm) return;
    if (g_cascadeMultiLayerCapture) {
        if (g_cascadeLightCaptureDumped || !g_cascadeLightCapture ||
            !g_cascadeLightCaptureDraws || !g_cascadeStaticTex ||
            !g_cascadeDynamicTex || !gl::GetTexImage)
            return;
        g_cascadeLightCaptureDumped = true;
        GLint oldArray = 0;
        gl::GetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &oldArray);
        const size_t layerPixels = (size_t)g_size * (size_t)g_size;
        auto dumpTarget = [&](GLuint tex, const char* name,
                              const unsigned* frames, const unsigned* draws) {
            std::vector<uint32_t> layers(layerPixels * kCascadeCount);
            gl::BindTexture(GL_TEXTURE_2D_ARRAY, tex);
            gl::GetTexImage(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT,
                            GL_UNSIGNED_INT, layers.data());
            for (int layer = 0; layer < kCascadeCount; ++layer) {
                const uint32_t* values = layers.data() + layerPixels * (size_t)layer;
                DepthStats stats{UINT32_MAX, 0, 0};
                for (size_t i = 0; i < layerPixels; ++i) {
                    const uint32_t value = values[i];
                    stats.lo = std::min(stats.lo, value);
                    stats.hi = std::max(stats.hi, value);
                    if (value != UINT32_MAX) ++stats.written;
                }
                char filename[96];
                std::snprintf(filename, sizeof(filename),
                              "shadowmap_cascade_%s_c%d.pgm", name, layer);
                FILE* f = open_dump_file(filename);
                if (f) {
                    fprintf(f, "P5\n%d %d\n255\n", g_size, g_size);
                    const double span = stats.hi > stats.lo ?
                        double(stats.hi) - stats.lo : 1.0;
                    for (size_t i = 0; i < layerPixels; ++i)
                        fputc((int)(255.0 * (double(values[i]) - stats.lo) / span), f);
                    fclose(f);
                }
                fprintf(stderr, "[shadowmap][csm] multi capture target=%s layer=%d "
                                "frame=%u draws=%u range=%u..%u written=%zu/%zu file=%s\n",
                        name, layer, frames[layer], draws[layer], stats.lo, stats.hi,
                        stats.written, layerPixels, f ? filename : "<open-failed>");
            }
        };
        if (gl::Finish) gl::Finish();
        dumpTarget(g_cascadeStaticTex, "static", g_cascadeStaticCaptureFrameLayer,
                   g_cascadeStaticCaptureDrawsLayer);
        dumpTarget(g_cascadeDynamicTex, "dynamic", g_cascadeDynamicCaptureFrameLayer,
                   g_cascadeDynamicCaptureDrawsLayer);
        gl::BindTexture(GL_TEXTURE_2D_ARRAY, (GLuint)oldArray);
        return;
    }
    const bool staticTarget = g_cascadeStaticReceiver && !g_cascadeDynamicTargetActive;
    const GLuint captureTex = staticTarget ? g_cascadeStaticTex : g_cascadeDynamicTex;
    if (g_cascadeLightCaptureDumped || !g_cascadeLightCapture ||
        !g_cascadeLightCaptureDraws || !captureTex ||
        !gl::GetTexImage) return;
    g_cascadeLightCaptureDumped = true;

    GLint oldArray = 0;
    gl::GetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &oldArray);
    const size_t layerPixels = (size_t)g_size * (size_t)g_size;
    std::vector<uint32_t> layers(layerPixels * kCascadeCount);
    if (gl::Finish) gl::Finish();
    gl::BindTexture(GL_TEXTURE_2D_ARRAY, captureTex);
    gl::GetTexImage(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT,
                    GL_UNSIGNED_INT, layers.data());
    gl::BindTexture(GL_TEXTURE_2D_ARRAY, (GLuint)oldArray);

    const uint32_t* values = layers.data() +
        layerPixels * (size_t)g_cascadeLightCaptureLayer;
    DepthStats stats{UINT32_MAX, 0, 0};
    for (size_t i = 0; i < layerPixels; ++i) {
        const uint32_t value = values[i];
        stats.lo = std::min(stats.lo, value);
        stats.hi = std::max(stats.hi, value);
        if (value != UINT32_MAX) ++stats.written;
    }
    FILE* f = open_dump_file("shadowmap_cascade_light.pgm");
    if (!f) {
        fprintf(stderr, "[shadowmap][lightcap] cannot open shadowmap_cascade_light.pgm\n");
        return;
    }
    fprintf(f, "P5\n%d %d\n255\n", g_size, g_size);
    const double span = stats.hi > stats.lo ? double(stats.hi) - stats.lo : 1.0;
    for (size_t i = 0; i < layerPixels; ++i)
        fputc((int)(255.0 * (double(values[i]) - stats.lo) / span), f);
    fclose(f);
    const bool alphaCrop = !staticTarget && stats.written &&
                           (g_cascadeAlphaCardCapture || g_cascadeStaticAlphaCapture);
    if (alphaCrop) {
        int minX = g_size, minY = g_size, maxX = -1, maxY = -1;
        for (int y = 0; y < g_size; ++y) for (int x = 0; x < g_size; ++x) {
            if (values[(size_t)y * (size_t)g_size + (size_t)x] == UINT32_MAX) continue;
            minX = std::min(minX, x); minY = std::min(minY, y);
            maxX = std::max(maxX, x); maxY = std::max(maxY, y);
        }
        constexpr int kMargin = 12;
        minX = std::max(0, minX - kMargin); minY = std::max(0, minY - kMargin);
        maxX = std::min(g_size - 1, maxX + kMargin);
        maxY = std::min(g_size - 1, maxY + kMargin);
        const int cropW = maxX - minX + 1, cropH = maxY - minY + 1;
        const int longSide = std::max(cropW, cropH);
        const int scale = std::max(1, std::min(16, (512 + longSide - 1) / longSide));
        const char* cropName = g_cascadeStaticAlphaCapture
            ? "shadowmap_static_alpha_cards.pgm" : "shadowmap_alpha_cards.pgm";
        FILE* crop = open_dump_file(cropName);
        if (!crop) {
            fprintf(stderr, "[shadowmap][alpha] cannot open %s\n", cropName);
        } else {
            fprintf(crop, "P5\n%d %d\n255\n", cropW * scale, cropH * scale);
            for (int y = minY; y <= maxY; ++y) {
                for (int sy = 0; sy < scale; ++sy) {
                    for (int x = minX; x <= maxX; ++x) {
                        const uint32_t value = values[(size_t)y * (size_t)g_size + (size_t)x];
                        const int shade = value == UINT32_MAX ? 255 :
                            (int)(255.0 * (double(value) - stats.lo) / span);
                        for (int sx = 0; sx < scale; ++sx) fputc(shade, crop);
                    }
                }
            }
            fclose(crop);
            fprintf(stderr,
                    "[shadowmap][alpha] dumped %s crop=%dx%d "
                    "scale=%dx bounds=(%d,%d)-(%d,%d) written=%zu\n",
                    cropName, cropW, cropH, scale, minX, minY, maxX, maxY, stats.written);
        }
    }
    fprintf(stderr,
            "[shadowmap][lightcap] dumped shadowmap_cascade_light.pgm frame=%u "
            "layer=%d target=%s buckets=%u dynamic-scopes=%u attempts=%u duplicate-draws=%u skipped=%u not-ready=%u "
            "range=%u..%u written=%zu/%zu%s\n",
            g_cascadeLightCaptureFrame, g_cascadeLightCaptureLayer,
            staticTarget ? "static" : "dynamic",
            g_cascadeLightCaptureBucketsVisited, g_cascadeLightCaptureDynamicScopes,
            g_cascadeLightCaptureAttempts,
            g_cascadeLightCaptureDraws, g_cascadeLightCaptureSkipped,
            g_cascadeLightCaptureNotReady,
            stats.lo, stats.hi, stats.written, layerPixels,
            stats.lo == stats.hi ? " (uniform - no depth)" : "");
}

static void clear_target() {
    gl::ClearColor(1.0f, 1.0f, 1.0f, 1.0f);  // no caster = far depth = lit
    gl::ClearDepth(1.0);
    gl::Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

