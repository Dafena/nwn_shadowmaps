// ===========================================================================
//  Phase 1: read-only render call graph
// ===========================================================================
// The hash is identity evidence only: it makes camera/projection changes easy
// to correlate in a log without dumping 16 floats for every pass. FNV-1a over
// the exact in-memory IEEE values is sufficient because it is never used as a
// transform or as a correctness decision.
static uint32_t trace_matrix_hash(int entry) {
    const float* m = mtx_entry(entry);
    if (!m) return 0;
    uint32_t h = 2166136261u;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(m);
    for (size_t i = 0; i < sizeof(float) * 16; ++i) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

static uint32_t trace_matrix_hash_data(const float* m) {
    if (!m) return 0;
    uint32_t h = 2166136261u;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(m);
    for (size_t i = 0; i < sizeof(float) * 16; ++i) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

static bool capture_shadow_frame_context(void* scene, void* camera,
                                         const GLint viewport[4], const Vec3f& eye) {
    const float* proj = mtx_entry(MTX_PROJ);
    const float* view = mtx_entry(MTX_VIEW);
    if (!proj || !view) return false;

    ShadowFrameContext& ctx = g_shadowFrameContext;
    ctx.valid = true;
    ++ctx.serial;
    ctx.scene = scene;
    ctx.camera = camera;
    std::memcpy(ctx.viewport, viewport, sizeof(ctx.viewport));
    ctx.matrixIndex = eng::CurMtxIdx ? *eng::CurMtxIdx : -1;
    ctx.eye = eye;
    std::memcpy(ctx.projection, proj, sizeof(ctx.projection));
    std::memcpy(ctx.view, view, sizeof(ctx.view));
    mat4_rigid_inverse(ctx.view, ctx.viewInverse);
    float viewProjection[16];
    mat4_mul(ctx.projection, ctx.view, viewProjection);
    if (!mat4_inverse(viewProjection, ctx.viewProjectionInverse)) {
        ctx.valid = false;
        fprintf(stderr, "[shadowmap][context] frozen camera VP is singular; static receiver skipped\n");
        return false;
    }
    ctx.projectionHash = trace_matrix_hash_data(ctx.projection);
    ctx.viewHash = trace_matrix_hash_data(ctx.view);
    return true;
}

// Phase 3a is deliberately mathematics only.  Its implementation lives below
// build_ortho(), alongside the existing matrix helpers; this declaration lets
// the exact Scene::Render entry boundary call it without moving those helpers.
static void update_cascade_math_from_context();

static bool trace_area_live() {
    return g_traceEnabled && g_traceFrameActive && g_traceCurrentScene &&
           g_traceCurrentScene == g_areaScene;
}

static bool trace_emit() {
    if (!trace_area_live() || !g_traceFrameLogged) return false;
    if (g_traceEvents >= g_traceEventsMax) {
        if (g_traceEvents == g_traceEventsMax)
            fprintf(stderr, "[shadowmap][trace] event limit %u reached; tracing paused\n",
                    g_traceEventsMax);
        ++g_traceEvents;
        return false;
    }
    ++g_traceEvents;
    ++g_traceSequence;
    return true;
}

static bool trace_camera_live(void* camera) {
    return g_traceEnabled && g_traceAreaCamera && camera == g_traceAreaCamera &&
           g_traceAreaFrame < g_traceFramesMax && g_traceEvents < g_traceEventsMax;
}

static bool trace_camera_emit(void* camera) {
    if (!trace_camera_live(camera)) return false;
    ++g_traceEvents;
    ++g_traceSequence;
    return true;
}

// `meshshadowbucket` and `staticshadowbucket` are CExoArrayList instances.
// ClearBuckets() in this exact binary stores zero at +8, so this is a direct
// read of the list count, never a guessed pointer walk. Keep a sanity bound:
// diagnostics must fail silent rather than turn a future layout change into a
// misleading report.
static int engine_list_count(const int* list) {
    if (!list) return -1;
    const int n = list[2];
    return (n >= 0 && n <= 10000000) ? n : -1;
}

static int engine_counter(const int* p) {
    if (!p) return -1;
    const int n = *p;
    return (n >= 0 && n <= 100000000) ? n : -1;
}

struct FullBspCensus {
    uint64_t nodes = 0;
    uint64_t dataNodes = 0;
    uint64_t meshParts = 0;
    uint64_t primaryParts = 0;
    uint64_t invalidData = 0;
};

extern "C" void FullBspCensusNode(void* node, void* user) {
    auto* census = static_cast<FullBspCensus*>(user);
    if (!census || !node) return;
    ++census->nodes;

    // The callback passed by ManageSceneBSP (at 0x481000) begins exactly with
    // `data = *(node + 0x70)`. Plain BSP nodes contain traversal information,
    // not a PartTriMesh array themselves.
    void* data = nullptr;
    std::memcpy(&data, static_cast<const uint8_t*>(node) + 0x70, sizeof(data));
    if (!data) return;
    ++census->dataNodes;

    void* parts = nullptr;
    void* primary = nullptr;
    int count = 0;
    // Do not give a possibly future-layout-mismatched field a typed alias.
    // These offsets are established by the actual nodedata callback and
    // ProcessTriMeshParts disassembly for this executable. memcpy keeps this
    // observation free of alignment or strict-aliasing assumptions.
    std::memcpy(&parts, static_cast<const uint8_t*>(data) + 0x20, sizeof(parts));
    std::memcpy(&count, static_cast<const uint8_t*>(data) + 0x28, sizeof(count));
    std::memcpy(&primary, static_cast<const uint8_t*>(data) + 0x90, sizeof(primary));
    if (count < 0 || count > 10000000 || (count != 0 && !parts)) {
        ++census->invalidData;
        return;
    }
    census->meshParts += (uint64_t)count;
    // The wrapper may submit this resident single part before its regular
    // nodedata array. Report it separately so a later caster route does not
    // silently omit that class.
    if (primary) ++census->primaryParts;
}

struct FullBspNativeSubmit {
    uint64_t nodes = 0;
    uint64_t dataNodes = 0;
    uint64_t regularParts = 0;
    uint64_t primaryParts = 0;
    uint64_t submitted = 0;
    uint64_t duplicates = 0;
    uint64_t invalidData = 0;
    std::unordered_set<void*> seen;
};

static void submit_full_bsp_part(FullBspNativeSubmit* submit, void* part,
                                 bool primary) {
    if (!submit || !part) return;
    if (!submit->seen.insert(part).second) {
        ++submit->duplicates;
        return;
    }
    if (primary) ++submit->primaryParts;
    else         ++submit->regularParts;
    // This is the engine helper called by its own BSP callback. It creates the
    // renderer's mesh-bucket record (including the part's material identity);
    // Scene::AddPartsToDrawBuckets below then applies the correct model matrix
    // and render ordering. No PartTriMesh vtable call is guessed here.
    eng::AddPartToMeshBuckets(part);
    ++submit->submitted;
}

extern "C" void FullBspNativeSubmitNode(void* node, void* user) {
    auto* submit = static_cast<FullBspNativeSubmit*>(user);
    if (!submit || !node) return;
    ++submit->nodes;

    void* data = nullptr;
    std::memcpy(&data, static_cast<const uint8_t*>(node) + 0x70, sizeof(data));
    if (!data) return;
    ++submit->dataNodes;

    void* parts = nullptr;
    void* primary = nullptr;
    int count = 0;
    std::memcpy(&parts, static_cast<const uint8_t*>(data) + 0x20, sizeof(parts));
    std::memcpy(&count, static_cast<const uint8_t*>(data) + 0x28, sizeof(count));
    std::memcpy(&primary, static_cast<const uint8_t*>(data) + 0x90, sizeof(primary));
    if (count < 0 || count > 10000000 || (count != 0 && !parts)) {
        ++submit->invalidData;
        return;
    }
    // ManageSceneBSP's wrapper submits this resident singleton before the
    // regular ProcessTriMeshParts list. Preserve that ordering for the first
    // native submission experiment.
    submit_full_bsp_part(submit, primary, true);
    for (int i = 0; i < count; ++i) {
        void* part = nullptr;
        std::memcpy(&part, static_cast<const uint8_t*>(parts) +
                    (size_t)i * sizeof(part), sizeof(part));
        submit_full_bsp_part(submit, part, false);
    }
}

// Phase 4d. Add the complete BSP source to NWN's *pending* mesh buckets after
// its normal camera-frustum BSP traversal, but before Scene::Render later calls
// Scene::AddPartsToDrawBuckets().  That ordering matters:
//
//   ManageSceneBSP()              -> fills the normal, camera-culled lists
//   [this hook adds the full list] -> same native pending-list representation
//   AddPartsToDrawBuckets()       -> NWN resets/builds its DrawBucketManager
//   RenderDrawBucket()            -> normal per-object matrix uploads, then
//                                    our proven adjacent light-space duplicate
//
// The first Phase 4d version called AddPartsToDrawBuckets() before the engine
// had completed its own per-frame renderer update.  It worked for one frame,
// then crashed in the engine on the next.  Do not reintroduce a manual call or
// manual RenderDrawBucket replay here: the engine owns both operations.
static void submit_full_bsp_native_static(void* scene) {
    // PERFORMANCE, and the big one. This appends the COMPLETE non-camera-culled
    // static BSP to NWN's pending mesh buckets, which means the ENGINE'S OWN
    // render draws every static object in the area with no frustum culling --
    // every frame, whether or not the shadow map needed it. That inflates the
    // base game's cost, not just ours.
    // With the world-anchored static map the full set is only needed on the
    // frames that actually rebuild it (once per area), so skip it entirely
    // while that map is valid and current.
    if (!g_fullBspEnabled) { g_fullBspSkipped = true; return; }
    // Grant the complete set only when the world map has asked for it.
    if (g_staticWorldEnabled && !g_fullBspWanted) {
        g_fullBspSkipped = true;
        return;
    }
    if (false) {
        static bool logged = false;
        if (!logged) {
            logged = true;
            fprintf(stderr, "[shadowmap][fullsubmit] world-anchored static map is current; "
                            "skipping the full-BSP submission -- NWN returns to its own "
                            "camera-culled draw set\n");
        }
        return;
    }
    if (!g_casterFullBspNativeSubmit || scene != g_areaScene ||
        !g_traceFrameActive || !g_shadowFrameContext.valid ||
        g_shadowFrameContext.scene != scene ||
        g_casterFullBspNativeLastFrame == g_shadowFrameContext.serial ||
        !eng::BSPTraverseAll || !eng::AddPartToMeshBuckets)
        return;

    void* root = nullptr;
    std::memcpy(&root, static_cast<const uint8_t*>(scene) + 0x188, sizeof(root));
    if (!root) return;

    FullBspNativeSubmit submit;
    const int beforeMeshShadow = engine_list_count(eng::MeshShadowBucket);
    eng::BSPTraverseAll(root, FullBspNativeSubmitNode, &submit);
    if (!submit.submitted) return;
    const int afterMeshShadow = engine_list_count(eng::MeshShadowBucket);

    g_casterFullBspNativeLastFrame = g_shadowFrameContext.serial;
    // Outside the bounded logging block: the world map must know the complete
    // set is present on EVERY submitting frame, not just the first eight.
    g_fullBspSubmittedFrame = true;
    if (g_casterFullBspNativeReports++ < 8 || g_verbose) {
        fprintf(stderr,
                "[shadowmap][fullsubmit] frame=%u nodes=%llu data-nodes=%llu "
                "regular=%llu primary=%llu submitted=%llu duplicates=%llu "
                "invalid=%llu pending-meshshadow=%d->%d; native bucket build pending\n",
                g_shadowFrameContext.serial,
                (unsigned long long)submit.nodes,
                (unsigned long long)submit.dataNodes,
                (unsigned long long)submit.regularParts,
                (unsigned long long)submit.primaryParts,
                (unsigned long long)submit.submitted,
                (unsigned long long)submit.duplicates,
                (unsigned long long)submit.invalidData,
                beforeMeshShadow, afterMeshShadow);
    }
}

// This runs after ManageSceneBSP has called ProcessTriMeshParts() with the
// CURRENT camera plane list.  It deliberately logs the lists before any
// material bucket is rendered, so camera movement can be compared to the
// source submission set rather than to the later private replay.
extern "C" void ManageSceneBSP_detour(void* self) {
    CALL_ORIGINAL(g_hookTraceManageSceneBSP, eng::ManageSceneBSP, self);
    g_fullBspSkipped = false;   // re-evaluated per frame; the old latch never reset
    g_fullBspSubmittedFrame = false;
    // The engine has just populated its per-frame mesh buckets.  Append the
    // world-static candidates now; Scene::Render will call its own
    // AddPartsToDrawBuckets() later, at the safe point shown above.
    submit_full_bsp_native_static(self);

    if ((!g_casterCullTrace && !g_casterFullBspTrace) || self != g_areaScene ||
        self != g_traceCurrentScene || !g_traceFrameActive)
        return;

    const int mesh = engine_list_count(eng::MeshShadowBucket);
    const int stat = engine_list_count(eng::StaticShadowBucket);
    const int culledPart = engine_counter(eng::CountCulledPart);
    const int culledShadow = engine_counter(eng::CountCulledShadows);
    const int backShadow = engine_counter(eng::CountBackShadows);
    const bool changed = mesh != g_casterCullLastMesh || stat != g_casterCullLastStatic ||
                         culledPart != g_casterCullLastCulledPart ||
                         culledShadow != g_casterCullLastCulledShadow ||
                         backShadow != g_casterCullLastBackShadow;
    // Keep the log useful for a real orbit test: one baseline is enough;
    // preserve the remaining budget for changes caused by camera motion.
    if (g_casterCullTraceReports < g_casterCullTraceMax &&
        (g_casterCullTraceReports == 0 || changed)) {
        ++g_casterCullTraceReports;
        fprintf(stderr,
                "[shadowmap][cull] frame=%u scene=%p meshshadow=%d staticvolume=%d "
                "culled-part=%d culled-shadow=%d back-shadow=%d%s\n",
                g_shadowFrameContext.serial, self, mesh, stat,
                culledPart, culledShadow, backShadow,
                changed ? "" : " (unchanged)");
    }
    g_casterCullLastMesh = mesh;
    g_casterCullLastStatic = stat;
    g_casterCullLastCulledPart = culledPart;
    g_casterCullLastCulledShadow = culledShadow;
    g_casterCullLastBackShadow = backShadow;

    // Sample once, then only when the normal camera list changes.  The full
    // walk is deliberately tied to the observation budget: it proves whether
    // an unculled candidate set exists, but it must never become per-frame
    // production work merely because the trace launcher stayed open.
    const bool fullChanged = g_casterFullBspLastCandidates == UINT64_MAX ||
                             changed;
    if (g_casterFullBspTrace && eng::BSPTraverseAll &&
        g_casterFullBspTraceReports < g_casterFullBspTraceMax && fullChanged) {
        void* root = nullptr;
        std::memcpy(&root, static_cast<const uint8_t*>(self) + 0x188, sizeof(root));
        FullBspCensus census;
        if (root) eng::BSPTraverseAll(root, FullBspCensusNode, &census);
        ++g_casterFullBspTraceReports;
        fprintf(stderr,
                "[shadowmap][fullbsp] frame=%u scene=%p nodes=%llu data-nodes=%llu "
                "tri-parts=%llu primary-parts=%llu tri-candidates=%llu "
                "normal-meshshadow=%d invalid-data=%llu%s\n",
                g_shadowFrameContext.serial, self,
                (unsigned long long)census.nodes,
                (unsigned long long)census.dataNodes,
                (unsigned long long)census.meshParts,
                (unsigned long long)census.primaryParts,
                (unsigned long long)(census.meshParts + census.primaryParts), mesh,
                (unsigned long long)census.invalidData,
                root ? "" : " (no root)");
        g_casterFullBspLastCandidates = census.meshParts + census.primaryParts;
    }
}

static void trace_scene_enter(void* scene) {
    g_traceCurrentScene = scene;
    g_traceFrameActive = false;
    g_traceFrameLogged = false;
    // Installed lazily from the render path: by the first area frame SDL is
    // certainly wired up, and it retries if the slot is not populated yet.
    if (!g_overlayLegacy) install_sdl_pollevent_patch();
#ifndef _WIN32
    // The shipping Linux trace/CSM path historically did not need the generic
    // program interposer. Rain classification does: it tracks program identity
    // and skinmesh uploads without a driver query on every draw.
    if (weather_needs_draw_observer()) install_useprogram_patch();
#endif
    install_weather_uniform_patch();
    install_light_vector_trace_patch();
    install_geometry_trace_patch();
    if (shadow_getenv("NWN_SHADOWMAP_DUMP_VERTEX") || shadow_getenv("NWN_SHADOWMAP_DUMP_FRAGMENT") ||
        g_cascadeStaticAlphaCapture || g_cascadeStaticAlphaReceiver ||
        nwn_oit_needs_shader_sources() || weather_needs_shader_sources())
        install_shadersource_patch();
    if (g_cascadeMatrixTrace || g_cascadeLightCapture) install_uniform_matrix_patch();

    GLint vp[4] = {0, 0, 0, 0};
    if (gl::GetIntegerv) gl::GetIntegerv(GL_VIEWPORT, vp);
    if (vp[2] > g_maxVpWidth) g_maxVpWidth = vp[2];
    const bool fullViewport = g_maxVpWidth > 0 && vp[2] >= (g_maxVpWidth * 9) / 10;
    const bool relaxedViewport = g_traceRelaxAreaViewport && g_maxVpWidth > 0 &&
                                 vp[2] * 2 >= g_maxVpWidth;

    Vec3f eye{};
    const bool haveEye = eye_from_view(eye);
    if ((fullViewport || relaxedViewport) && haveEye && std::fabs(eye.z) < 200.0f) {
        if (g_areaScene != scene) {
            g_areaScene = scene;
            reset_engine_shadow_light_anchor();
            g_traceAreaFrame = 0;
            g_traceEvents = 0;
            g_traceSequence = 0;
            fprintf(stderr,
                    "[shadowmap][trace] selected area scene=%p camera=%p viewport=%dx%d "
                    "eye=(%.2f %.2f %.2f)%s\n",
                    scene, g_traceCurrentCamera, vp[2], vp[3], eye.x, eye.y, eye.z,
                    relaxedViewport && !fullViewport ? " (relaxed viewport gate)" : "");
        }
    }
    if (scene != g_areaScene) return;
    weather_scene_begin(scene);
    // The normal trace remains bounded.  The visible static receiver is
    // different: its frame context and private depth must advance forever or
    // it would compare a new screen depth against an old camera/light frame.
    // The ordinary trace is intentionally bounded, but Phase 4b needs to stay
    // live after that bound so the tester can orbit a caster long after area
    // load. It emits only its compact change-driven census once tracing stops.
    if (g_traceAreaFrame >= g_traceFramesMax && !g_cascadeStaticReceiver &&
        !g_casterCullTrace && !g_casterFullBspTrace) return;
    g_traceFrameLogged = g_traceAreaFrame < g_traceFramesMax;
    if (g_traceFrameLogged) ++g_traceAreaFrame;
    g_traceFrameActive = true;
    if (g_traceCurrentCamera) g_traceAreaCamera = g_traceCurrentCamera;
    const bool capturedContext = capture_shadow_frame_context(
        scene, g_traceAreaCamera, vp, eye);
    if (capturedContext && g_cascadeMathTrace)
        update_cascade_math_from_context();
    if (!g_traceFrameLogged || !trace_emit()) return;
    fprintf(stderr,
            "[shadowmap][trace] #%u area-frame=%u Scene::Render enter scene=%p "
            "camera=%p viewport=%dx%d eye=(%.2f %.2f %.2f) mtx=%d proj=%08x view=%08x\n",
            g_traceSequence, g_traceAreaFrame, scene, g_traceAreaCamera,
            vp[2], vp[3], eye.x, eye.y, eye.z,
            eng::CurMtxIdx ? *eng::CurMtxIdx : -1,
            trace_matrix_hash(MTX_PROJ), trace_matrix_hash(MTX_VIEW));
    if (capturedContext) {
        const ShadowFrameContext& ctx = g_shadowFrameContext;
        fprintf(stderr,
                "[shadowmap][context] frame=%u scene=%p camera=%p vp=%d,%d %dx%d "
                "eye=(%.2f %.2f %.2f) mtx=%d proj=%08x view=%08x\n",
                ctx.serial, ctx.scene, ctx.camera,
                ctx.viewport[0], ctx.viewport[1], ctx.viewport[2], ctx.viewport[3],
                ctx.eye.x, ctx.eye.y, ctx.eye.z, ctx.matrixIndex,
                ctx.projectionHash, ctx.viewHash);
    }
}

static void trace_scene_exit(void* scene) {
    weather_scene_end(scene);
    // The area-light vector is discovered while the original normal pass is
    // running.  Allocate only after that pass, once the real direction and a
    // frozen cascade fit both exist.  This touches private GL objects only;
    // no engine render state, caster draw, clear, copy, or receiver is used.
    if (scene == g_areaScene && g_cascadeTargetValidate &&
        g_shadowFrameContext.valid && g_haveTraceAreaLight) {
        update_cascade_math_from_context();
        create_cascade_targets();
        validate_cascade_target_clear_once();
        if (!g_cascadeTargetValidationLogged && g_cascadeTargetsUsable) {
            g_cascadeTargetValidationLogged = true;
            fprintf(stderr,
                    "[shadowmap][csm] target validation ready: frame=%u "
                    "real-area-light fit, 4 static + 4 dynamic layers; no rendering\n",
                    g_shadowFrameContext.serial);
        }
    }
    // All selected native buckets in this one normal area frame contribute to
    // the same layer.  Dump only after Scene::Render has finished, rather than
    // after bucket 0, so terrain plus object buckets are visible together.
    if (scene == g_areaScene && g_cascadeLightCapture &&
        !g_cascadeLightCaptureDumped &&
        g_cascadeLightCaptureFrame == g_shadowFrameContext.serial &&
        g_cascadeLightCaptureDraws)
        dump_cascade_light_capture_once();
    if (scene == g_areaScene && trace_emit())
        fprintf(stderr, "[shadowmap][trace] #%u area-frame=%u Scene::Render exit scene=%p\n",
                g_traceSequence, g_traceAreaFrame, scene);
    if (scene == g_areaScene && g_shadowFrameContext.valid &&
        g_shadowFrameContext.scene == scene && trace_emit()) {
        const uint32_t projNow = trace_matrix_hash(MTX_PROJ);
        const uint32_t viewNow = trace_matrix_hash(MTX_VIEW);
        fprintf(stderr,
                "[shadowmap][context] frame=%u end proj=%08x%s view=%08x%s; "
                "future shadow work will use the frozen entry context\n",
                g_shadowFrameContext.serial, projNow,
                projNow == g_shadowFrameContext.projectionHash ? "" : " (changed)",
                viewNow, viewNow == g_shadowFrameContext.viewHash ? "" : " (changed)");
    }
    if (g_traceCurrentScene == scene) g_traceCurrentScene = nullptr;
    g_traceFrameActive = false;
    g_traceFrameLogged = false;
}

extern "C" void CameraRender_detour(void* self, bool renderChildren) {
    void* previous = g_traceCurrentCamera;
    g_traceCurrentCamera = self;
    ++g_traceCameraDepth;
    if (trace_camera_emit(self))
        fprintf(stderr, "[shadowmap][trace] #%u area-frame=%u camera-render enter "
                        "camera=%p children=%d depth=%d\n",
                g_traceSequence, g_traceAreaFrame, self, renderChildren ? 1 : 0,
                g_traceCameraDepth);
    CALL_ORIGINAL(g_hookTraceCameraRender, eng::CameraRender, self, renderChildren);
    if (trace_camera_emit(self))
        fprintf(stderr, "[shadowmap][trace] #%u area-frame=%u camera-render exit "
                        "camera=%p depth=%d\n",
                g_traceSequence, g_traceAreaFrame, self, g_traceCameraDepth);
    --g_traceCameraDepth;
    g_traceCurrentCamera = previous;
}

extern "C" void CameraRenderScene_detour(void* self) {
    void* previous = g_traceCurrentCamera;
    g_traceCurrentCamera = self;
    if (trace_camera_emit(self))
        fprintf(stderr, "[shadowmap][trace] #%u area-frame=%u camera-render-scene enter "
                        "camera=%p\n", g_traceSequence, g_traceAreaFrame, self);
    CALL_ORIGINAL(g_hookTraceCameraScene, eng::CameraRenderScene, self);
    if (trace_camera_emit(self))
        fprintf(stderr, "[shadowmap][trace] #%u area-frame=%u camera-render-scene exit "
                        "camera=%p\n", g_traceSequence, g_traceAreaFrame, self);
    g_traceCurrentCamera = previous;
}

extern "C" void SceneRenderSinglePass_detour(void* self) {
    if (self == g_traceCurrentScene && trace_emit())
        fprintf(stderr, "[shadowmap][trace] #%u area-frame=%u Scene::RenderSinglePass\n",
                g_traceSequence, g_traceAreaFrame);
    CALL_ORIGINAL(g_hookTraceSceneSingle, eng::SceneRenderSinglePass, self);
}

extern "C" void SceneRenderDynamicGeometry_detour(void* self) {
    if (self == g_traceCurrentScene && trace_emit())
        fprintf(stderr, "[shadowmap][trace] #%u area-frame=%u Scene::RenderDynamicGeometry\n",
                g_traceSequence, g_traceAreaFrame);
    // Characters and other animated objects are submitted through this
    // engine stage rather than a stable area bucket.  Keep the same native
    // duplicate bridge active only for this original call; no engine draw is
    // suppressed, and every duplicate still restores its exact normal state.
    const bool lightCapture = g_cascadeDynamicCharacterCapture && self == g_traceCurrentScene &&
                              trace_area_live() && g_cascadeTargetsUsable &&
                              g_shadowFrameContext.valid && g_cascadeMath.valid &&
                              g_cascadeMath.sourceFrame == g_shadowFrameContext.serial &&
                              !g_cascadeLightCaptureDumped;
    const bool localVisibleCapture = g_localLightCapture &&
                                     g_localVisibleDynamicPrepared &&
                                     self == g_traceCurrentScene && trace_area_live() &&
                                     g_shadowFrameContext.valid &&
                                     g_localVisiblePreparedFrame < g_shadowFrameContext.serial;
    const unsigned attemptsBefore = g_cascadeLightCaptureAttempts;
    const unsigned drawsBefore = g_cascadeLightCaptureDraws;
    if (lightCapture) {
        g_cascadeLightCaptureActive = true;
        g_cascadeDynamicTargetActive = true;
        g_cascadeDynamicAlphaTargetActive = false;
        ++g_cascadeLightCaptureDynamicScopes;
    }
    if (localVisibleCapture) {
        g_localVisibleDynamicCaptureActive = true;
        g_localCubeDynamicSkipNoColour = 0;
        g_localCubeDynamicSkipStencil = 0;
        g_localCubeDynamicSkipNoProgram = 0;
    }
    CALL_ORIGINAL(g_hookTraceSceneDynamic, eng::SceneRenderDynamicGeometry, self);
    if (localVisibleCapture) {
        g_localVisibleDynamicCaptureActive = false;
        g_localVisibleDynamicPrepared = false;
        unsigned visibleDraws = 0;
        bool allVisibleSlotsDrew = g_localVisibleSlotCount > 0;
        for (unsigned layer = 0; layer < g_localVisibleSlotCount; ++layer) {
            visibleDraws += g_localVisibleDynamicDraws[layer];
            allVisibleSlotsDrew = allVisibleSlotsDrew &&
                                  g_localVisibleDynamicDraws[layer] > 0;
        }
        // WHEN A STAGED GENERATION MAY BE PUBLISHED -- and the two platforms
        // genuinely need different answers here, so they get different answers.
        // See NWN_WIN_LOCAL_FASTPATH.
        //
        // Linux: EVERY selected layer must have drawn. This is the rule the
        // 2026-08-13 savepoint shipped and the maintainer confirmed in game, and
        // it is safe here precisely BECAUSE the per-light caster cull is off on
        // this platform: without culling, a selected light's layer going empty
        // means the stage really did not run, which is the case this guards.
        //
        // Windows: ANY layer having drawn is enough, because the cull is on and
        // an empty layer is then an ordinary outcome -- culling every caster of
        // a light is indistinguishable from that light having none. Demanding
        // all of them would let one empty layer discard the whole frame's
        // capture for every other light, dropping to the bucket-replay fallback
        // and back on alternating frames. The two fills do not share casters, so
        // that alternation is visible.
        //
        // Both keep the guarantee that matters: a staged map nothing submitted
        // to is never exposed. Windows enforces it on the total instead of
        // per layer.
        const bool publishGeneration = NWN_WIN_LOCAL_FASTPATH
            ? (g_localVisibleSlotCount > 0 && visibleDraws > 0)
            : allVisibleSlotsDrew;
        g_localVisibleCompletedFrame = g_shadowFrameContext.serial;
        if (publishGeneration) {
            g_localVisibleFallbackNext = false;
            g_localLightSlotCount = g_localVisibleSlotCount;
            for (unsigned layer = 0; layer < g_localVisibleSlotCount; ++layer) {
                std::memcpy(g_localLightFaceVP[layer], g_localVisibleVP[layer],
                            sizeof(g_localVisibleVP[layer]));
                std::memcpy(g_localLightSlotPos[layer], g_localVisibleSlotPos[layer],
                            sizeof(g_localVisibleSlotPos[layer]));
                g_localLightSlotRadius[layer] = g_localVisibleSlotRadius[layer];
                g_localLightSlotFade[layer]   = g_localVisibleSlotFade[layer];
            }
            if (g_localVisibleSlotCount) {
                std::memcpy(g_localLightVP, g_localVisibleVP[0], sizeof(g_localLightVP));
                std::memcpy(g_localLightWorldPos, g_localVisibleSlotPos[0],
                            sizeof(g_localLightWorldPos));
                g_localLightWorldRadius = g_localVisibleSlotRadius[0];
                g_haveLocalLightVP = true;
                g_localLightVPFrame = g_shadowFrameContext.serial;
                // A map is fresh only after every selected layer received the
                // normal visible dynamic submission.  This timestamp therefore
                // drives the production cadence, not the one-frame-earlier
                // staging call in capture_local_light_shadow().
                g_localLightLastCaptureTime = now_seconds();
                g_localLightCompletedPolicyEpoch = g_localVisiblePolicyEpoch;
            }
        } else {
            // A cleared staging map with no visible submissions must never be
            // exposed.  Ask the post-receiver capture to run the old bucket
            // replay once, keeping compatibility with unusual renderer paths.
            g_haveLocalLightVP = false;
            g_localVisibleFallbackNext = true;
        }
        static double nextVisibleStatus = 0.0;
        const double visibleNow = now_seconds();
        if (g_statusEvery > 0.0 && visibleNow >= nextVisibleStatus) {
            nextVisibleStatus = visibleNow + g_statusEvery;
            fprintf(stderr,
                    "[shadowmap][local-light] visible dynamic capture slots=%u draws=%u "
                    "fallback=%s reject(no-colour=%u stencil=%u no-program=%u)\n",
                    g_localVisibleSlotCount, visibleDraws,
                    publishGeneration ? "no" : "next", g_localCubeDynamicSkipNoColour,
                    g_localCubeDynamicSkipStencil, g_localCubeDynamicSkipNoProgram);
        }
    }
    if (lightCapture) {
        g_cascadeLightCaptureActive = false;
        g_cascadeDynamicTargetActive = false;
        g_cascadeDynamicAlphaTargetActive = false;
        if (!g_cascadeDynamicCharacterReported) {
            g_cascadeDynamicCharacterReported = true;
            fprintf(stderr,
                    "[shadowmap][dynamic] Scene::RenderDynamicGeometry direct capture: "
                    "attempts=%u draws=%u (delta attempts=%u draws=%u)\n",
                    g_cascadeLightCaptureAttempts, g_cascadeLightCaptureDraws,
                    g_cascadeLightCaptureAttempts - attemptsBefore,
                    g_cascadeLightCaptureDraws - drawsBefore);
        }
    }
}

extern "C" void SceneRenderDrawBucket_trace_detour(void* self, int bucket) {
    // CRITICAL: any injector pass that drives geometry through
    // guarded_render_bucket() calls eng::SceneRenderDrawBucket(), which is
    // THIS SAME hooked symbol -- so it re-enters this detour. Without this
    // guard the re-entrant call recomputes "should this bucket be replayed"
    // from the same global flags, decides yes again, and replays again:
    //  - for the cascade replay that fans out kCascadeCount-deeper per level
    //    until the stack overflows (this crashed the client outright rather
    //    than tripping the fault-guard, since the overflow can happen during
    //    signal delivery);
    //  - for the local-light pass it silently multiplies work instead, one
    //    full 4-layer cascade replay per local-light bucket.
    // Neither re-entrant call needs anything below: the light transform is
    // applied by the caller through the matrix stack. Just draw and return.
    if (g_cascadeReplayActive || g_localLightPassActive) {
        CALL_ORIGINAL(g_hookTraceBucket, eng::SceneRenderDrawBucket, self, bucket);
        return;
    }
    const bool census = g_cascadeGeometryTrace && self == g_traceCurrentScene &&
                        trace_area_live() &&
                        g_traceGeometryBuckets < g_traceGeometryMaxBuckets;
    // Capture is intentionally independent from the bounded census counter:
    // the text trace may stop at frame/event limits, while this one private
    // depth dump must still reach the first post-validation area frame.
    const bool capture = g_cascadeCameraCapture && self == g_traceCurrentScene &&
                         trace_area_live() && bucket == g_cascadeCameraCaptureBucket &&
                         g_cascadeTargetsUsable && g_shadowFrameContext.valid &&
                         !g_cascadeCameraCaptureDumped;
    const bool dynamicBucketCapture =
        (g_cascadeDynamicBucketCapture || g_cascadeDynamicReceiver) &&
        bucket == g_cascadeDynamicBucket;
    const bool alphaBucketCapture =
        g_cascadeAlphaReceiver && bucket == g_cascadeAlphaBucket;
    // Both post-dynamic buckets accumulate in the same target.  The target is
    // cleared by the first selected dynamic bucket of this frozen area frame;
    // later selected buckets add depth, rather than replacing bucket 2.
    const bool dynamicTargetBucketCapture = dynamicBucketCapture || alphaBucketCapture;
    const bool staticBucketCapture =
        g_cascadeStaticReceiver && bucket == g_cascadeLightCaptureBucket;
    const bool staticAlphaBucketCapture =
        (g_cascadeStaticAlphaCapture || g_cascadeStaticAlphaReceiver) &&
        bucket == g_cascadeStaticAlphaBucket;
    const bool receiverBucketCapture = staticBucketCapture || dynamicTargetBucketCapture ||
                                       staticAlphaBucketCapture;
    const bool lightCapture = g_cascadeLightCapture && !g_cascadeDynamicCharacterCapture &&
                              ((g_cascadeStaticReceiver || g_cascadeDynamicReceiver ||
                                g_cascadeAlphaReceiver || g_cascadeStaticAlphaReceiver)
                                   ? receiverBucketCapture
                                   : ((g_cascadeDynamicBucketCapture || g_cascadeStaticAlphaCapture)
                                      ? (dynamicBucketCapture || staticAlphaBucketCapture)
                                      : true)) &&
                              self == g_traceCurrentScene &&
                              trace_area_live() &&
                              // Phase 3k has already selected its one candidate
                              // above.  Its ordinary light-bucket list is empty
                              // by design, so applying that second filter would
                              // silently prevent every duplicate draw.
                              (g_cascadeDynamicBucketCapture ||
                               g_cascadeDynamicReceiver ||
                               g_cascadeAlphaReceiver ||
                               g_cascadeStaticAlphaCapture ||
                               g_cascadeStaticAlphaReceiver ||
                               g_cascadeLightCaptureAllBuckets ||
                               cascade_light_capture_bucket_selected(bucket)) &&
                              g_cascadeTargetsUsable && g_shadowFrameContext.valid &&
                              g_cascadeMath.valid &&
                              g_cascadeMath.sourceFrame == g_shadowFrameContext.serial &&
                              (g_cascadeStaticReceiver || !g_cascadeLightCaptureDumped);
    if (census) {
        g_traceGeometryActive = true;
        g_traceGeometryBucket = bucket;
        g_traceGeometryDraws = 0;
        g_traceGeometryElements = 0;
        g_traceGeometryArrays = 0;
        g_traceGeometryProgramCount = 0;
    }
    if (self == g_traceCurrentScene && trace_emit())
        fprintf(stderr, "[shadowmap][trace] #%u area-frame=%u Scene::RenderDrawBucket bucket=%d\n",
                g_traceSequence, g_traceAreaFrame, bucket);
    if (lightCapture) ++g_cascadeLightCaptureBucketsVisited;
    g_cascadeCameraCaptureActive = capture;
    // Phase 6c: when the bucket-replay perf path is on, this bucket's cascade
    // duplication happens ONCE after CALL_ORIGINAL via
    // replay_bucket_into_cascade_layers(), not per individual draw call
    // during it -- so the old per-draw flags below must stay off for the
    // real pass, or both mechanisms would duplicate the same geometry.
    const bool useBucketReplay = lightCapture && g_cascadeMultiLayerCapture && g_cascadeBucketReplay;
    g_cascadeLightCaptureActive = lightCapture && !useBucketReplay;
    g_cascadeDynamicTargetActive = g_cascadeLightCaptureActive &&
        (dynamicTargetBucketCapture ||
         (staticAlphaBucketCapture && g_cascadeStaticAlphaCapture));
    g_cascadeDynamicAlphaTargetActive = g_cascadeLightCaptureActive && alphaBucketCapture;
    g_cascadeStaticAlphaFilterActive = g_cascadeLightCaptureActive && staticAlphaBucketCapture;
    // Publish the engine's current bucket for other modules (nwn_hooks_core.h).
    // Area scene only: UI/portrait scenes render buckets too, and attributing
    // their draws to an area bucket index would be worse than reporting none.
    const int priorBucket = nwn_core::g_currentBucket;
    if (self == g_areaScene) nwn_core::g_currentBucket = bucket;
    if (self == g_areaScene) nwn_oit_bucket_begin(self, bucket);
    CALL_ORIGINAL(g_hookTraceBucket, eng::SceneRenderDrawBucket, self, bucket);
    nwn_core::g_currentBucket = priorBucket;
    g_cascadeCameraCaptureActive = false;
    g_cascadeLightCaptureActive = false;
    g_cascadeDynamicTargetActive = false;
    g_cascadeDynamicAlphaTargetActive = false;
    g_cascadeStaticAlphaFilterActive = false;
    if (self == g_areaScene) nwn_oit_bucket_complete(self, bucket);
    if (useBucketReplay) {
        const bool staticTarget = staticBucketCapture || staticAlphaBucketCapture;
        replay_bucket_into_cascade_layers(self, bucket, staticTarget);
    }
#ifndef _WIN32
    // Weather owns a separate straight-down static depth target. Its replay is
    // requested here, adjacent to the proven cascade bucket replay, and is a
    // Linux-only module until the weather system receives an explicit port.
    if (self == g_areaScene)
        weather_runtime::capture_precipitation_occlusion_bucket(self, bucket);
#endif
    // SUN-PATH local lights fill from here for the same reason the cascades do:
    // the engine has just rendered this bucket for the CAMERA, so replaying it
    // now captures exactly the visible casters. Area scene only, and never from
    // inside our own replay (that guard is g_localLightPassActive, checked in
    // the callee via the flag it sets around its own bucket calls).
    if (self == g_areaScene && !g_localLightPassActive)
        replay_bucket_into_local_light_layers(self, bucket);
    if (capture) dump_cascade_camera_capture_once();
    if (census) {
        g_traceGeometryActive = false;
        ++g_traceGeometryBuckets;
        char programs[192] = {};
        int at = 0;
        for (unsigned i = 0; i < g_traceGeometryProgramCount && at < (int)sizeof(programs); ++i)
            at += std::snprintf(programs + at, sizeof(programs) - (size_t)at,
                                "%s%u", i ? "," : "", g_traceGeometryPrograms[i]);
        fprintf(stderr, "[shadowmap][geom] frame=%u bucket=%d draws=%u indexed=%llu "
                        "arrays=%llu programs=%s\n",
                g_shadowFrameContext.serial, g_traceGeometryBucket,
                g_traceGeometryDraws, g_traceGeometryElements,
                g_traceGeometryArrays,
                g_traceGeometryProgramCount ? programs : "none");
    }
}

extern "C" void LightPrioritizeShadow_trace_detour(void* self) {
    if (trace_emit())
        fprintf(stderr, "[shadowmap][trace] #%u area-frame=%u LightManager::PrioritizeShadow self=%p\n",
                g_traceSequence, g_traceAreaFrame, self);
    CALL_ORIGINAL(g_hookTracePrioritizeShadow, eng::LightPrioritizeShadow, self);
}

// Read-only census of the lights the ENGINE actually shades with.
//
// GetShadowLights (below) returns the shadow-caster subset only: measured, it
// held the carried torch and nothing else while four wall lamps lit the scene,
// which is why the sun-shadow lift worked for the torch and for nothing else.
// SetLightGL is called by the engine for every light it enables, so collecting
// pointers here gives the real set. We never modify anything -- decode, record,
// call the original.
//
// The list is rebuilt per FRAME, not per call: entries are stamped with the
// current frame serial and stale ones are dropped when the serial moves, so a
// light that stops being enabled (torch put away) disappears on its own.
extern "C" void SetLightGL_detour(const void* light, int index, float f) {
    if (light && g_lampCensus) {
        PartLightInfo info;
        if (read_part_light(light, info) && !info.ambientOnly) {
            // Prefer the engine's accessor; the raw field is not the effective
            // radius here (all lamps read 0.1 through it).
            float radius = info.radius;
            float adjusted = -1.0f;
            if (eng::GetLightAdjustedRadius) {
                adjusted = eng::GetLightAdjustedRadius(light);
                if (std::isfinite(adjusted) && adjusted > 0.0f && adjusted < 1000.0f)
                    radius = adjusted;
            }
            // Settle the units question with data rather than argument: the raw
            // field reads 0.1 for every lamp, which is either a radius in some
            // other unit, a reciprocal, or simply not the radius. Print both.
            static unsigned unitReports = 0;
            if (unitReports < 6) {
                ++unitReports;
                fprintf(stderr, "[shadowmap][lamps] light %p raw=%.4f adjusted=%.4f "
                                "colour=(%.2f,%.2f,%.2f) pos=(%.1f,%.1f,%.1f)\n",
                        light, info.radius, adjusted,
                        info.color[0], info.color[1], info.color[2],
                        info.pos.x, info.pos.y, info.pos.z);
            }
            // EMITTER lights animate; a fixed lamp does not. An area full of
            // glowing water or flames produces dozens of them, and each one in
            // the shadow set is a whole depth capture every frame -- which is
            // what kills the frame rate there. They still LIGHT the scene (the
            // lift keeps them); they just stop casting.
            bool isEmitter = false;
            if (!eng::GetLightFadeSpeed) {
                // Without this accessor NOTHING is ever classified as an
                // emitter, so "Lights casting shadows" silently does nothing --
                // which is precisely how it shipped on Windows, where the
                // symbol had no MSVC mapping. Say so instead of pretending.
                static bool warned = false;
                if (!warned) {
                    warned = true;
                    fprintf(stderr, "[shadowmap][lamps] GetLightFadeSpeed unavailable: emitter "
                                    "lights cannot be identified, so \"Lights casting "
                                    "shadows\" has no effect\n");
                }
            }
            if (eng::GetLightFadeSpeed) {
                const float fade = eng::GetLightFadeSpeed(light);
                isEmitter = (std::isfinite(fade) && std::fabs(fade) > 1e-4f);
            }
            static unsigned fadeReports = 0;
            if (fadeReports < 8) {
                ++fadeReports;
                fprintf(stderr, "[shadowmap][lamps] light %p fade=%.4f r=%.2f -> %s\n",
                        light, eng::GetLightFadeSpeed ? eng::GetLightFadeSpeed(light) : -1.0f,
                        radius, isEmitter ? "EMITTER (no shadow)" : "static (casts)");
            }
            lamp_list_add(light, info.pos.x, info.pos.y, info.pos.z, radius,
                          isEmitter, info.color);
        }
    }
    if (g_hookSetLightGL) subhook_remove(g_hookSetLightGL);
    eng::SetLightGL(light, index, f);
    if (g_hookSetLightGL) subhook_install(g_hookSetLightGL);
}

extern "C" void* LightGetShadowLights_trace_detour(void* self, int mode) {
    if (trace_emit())
        fprintf(stderr, "[shadowmap][trace] #%u area-frame=%u LightManager::GetShadowLights enter self=%p mode=%d\n",
                g_traceSequence, g_traceAreaFrame, self, mode);
    if (g_hookTraceGetShadowLights) subhook_remove(g_hookTraceGetShadowLights);
    void* result = eng::LightGetShadowLights(self, mode);
    if (g_hookTraceGetShadowLights) subhook_install(g_hookTraceGetShadowLights);
    if (trace_emit())
        fprintf(stderr, "[shadowmap][trace] #%u area-frame=%u LightManager::GetShadowLights exit list=%p\n",
                g_traceSequence, g_traceAreaFrame, result);
    // CExoArrayList stores a data pointer at +0 and element count at +8 on
    // this 64-bit build (the same layout already observed for engine bucket
    // lists).  Phase 6b decodes each candidate's PartLight fields (see
    // read_part_light() above) and keeps the nearest non-ambient-only one as
    // the probe's selected light. Selection refreshes on EVERY call, not just
    // when the throttled log line below fires, so a local-light capture pass
    // always has a fresh candidate for this Scene::Render.
    // This hook is also the authoritative carried-torch priority channel for
    // the local cube.  Do not hide it behind the old trace flag: the actual
    // selected light is useful runtime data, whereas only the verbose list
    // printout is diagnostic.
    const bool areaRenderActive = g_traceAreaFrame ||
                                  (g_inSceneRender && g_renderingScene == g_areaScene);
    if (result && areaRenderActive) {
        void* data = nullptr;
        uint32_t count = 0;
        std::memcpy(&data, result, sizeof(data));
        std::memcpy(&count, (const char*)result + sizeof(void*), sizeof(count));
        const bool plausible = count <= 64 && data != nullptr;
        const bool shouldLog = plausible &&
            (result != g_localLightLastList || count != g_localLightLastCount ||
             mode != g_localLightLastMode || g_localLightReports < 2);

        if (g_localLightTrace && shouldLog) {
            g_localLightLastList = result;
            g_localLightLastCount = count;
            g_localLightLastMode = mode;
            ++g_localLightReports;
            fprintf(stderr,"[shadowmap][local-light] area-frame=%u mode=%d list=%p count=%u candidates=",
                    g_traceAreaFrame,mode,result,count);
        }
        if (plausible) {
            const uint32_t shown = std::min<uint32_t>(count, 8);
            for (uint32_t i = 0; i < count; ++i) {
                void* candidate = nullptr;
                std::memcpy(&candidate, (const char*)data + i*sizeof(void*), sizeof(candidate));
                PartLightInfo info;
                const bool decoded = read_part_light(candidate, info);
                if (g_localLightTrace && shouldLog && i < shown) {
                    if (decoded)
                        fprintf(stderr,"%s%p[pos=(%.2f,%.2f,%.2f) r=%.2f rgb=(%.2f,%.2f,%.2f)%s]",
                                i ? "," : "", candidate,
                                info.pos.x, info.pos.y, info.pos.z, info.radius,
                                info.color[0], info.color[1], info.color[2],
                                info.ambientOnly ? " ambient-only" : "");
                    else
                        fprintf(stderr,"%s%p[undecoded]", i ? "," : "", candidate);
                }
                if (decoded && !info.ambientOnly) {
#ifndef _WIN32
                    lamp_list_add(candidate, info.pos.x, info.pos.y, info.pos.z,
                                  info.radius, false, info.color, (int)i);
#endif
                }
            }
            if (g_localLightTrace && shouldLog)
                fprintf(stderr,"%s\n", count > shown ? ",..." : "");
        } else if (count > 64 && g_localLightReports++ == 0) {
            fprintf(stderr,"[shadowmap][local-light] refusing implausible count=%u from list=%p\n",
                    count,result);
        }

        // NWN returns its shadow candidates in priority order.  The first
        // usable entry is therefore the engine-selected source (normally a
        // carried torch), not the nearest light to the camera.
        if (plausible) {
            g_engineShadowLightCount = 0;
            for (uint32_t i = 0; i < count; ++i) {
                void* candidate = nullptr;
                std::memcpy(&candidate, (const char*)data + i*sizeof(void*), sizeof(candidate));
                PartLightInfo info;
                if (!read_part_light(candidate, info) || info.ambientOnly)
                    continue;
                // Preserve NWN's order.  GetLightAdjustedRadius only decodes
                // the range for the already engine-selected light; it never
                // ranks or substitutes candidates.
                if (eng::GetLightAdjustedRadius) {
                    const float adjusted = eng::GetLightAdjustedRadius(candidate);
                    if (std::isfinite(adjusted) && adjusted > 0.0f && adjusted < 1000.0f)
                        info.radius = adjusted;
                }
                if (g_engineShadowLightCount < kLocalSlotsMax)
                    g_engineShadowLights[g_engineShadowLightCount++] = { candidate, info };
            }
            if (g_engineShadowLightCount) {
                const EngineShadowLightEntry& preferred = g_engineShadowLights[0];
                const bool changed = preferred.key != g_engineShadowLightKey;
                g_engineShadowLightKey = preferred.key;
                g_haveEngineShadowLight = true;
                if (changed && g_localLightTrace)
                    fprintf(stderr, "[shadowmap][local-light] engine-priority shadow light=%p pos=(%.2f,%.2f,%.2f) r=%.2f\n",
                            preferred.key, preferred.info.pos.x, preferred.info.pos.y,
                            preferred.info.pos.z, preferred.info.radius);
                g_localLightSelected = (void*)preferred.key;
                g_localLightSelectedInfo = preferred.info;
            } else {
                g_haveEngineShadowLight = false;
            }
        } else {
            g_haveEngineShadowLight = false;
        }
    }
    return result;
}

// Construct the four camera-relative light matrices required by the recovered
// PS4/Switch receiver contract.  This must remain side-effect free: no FBO,
// texture, shader, renderer-stack, Camera, or GL call is allowed here.  It is
// a proof that the immutable Scene::Render-entry context contains sufficient
// data for stable cascade fitting before we redirect even one draw.
// The direction the cascade fit is built from, in ONE place. The fit, the fit
// cache's invalidation test and the cache's snapshot must all read the same
// vector or the cache is comparing something the matrices were not built from.
// Mirrors the selection in static_world_needs_refresh().
static const float* cascade_fit_sun() {
    if (!g_dirOverride && g_haveTraceAreaLight) return g_traceAreaLightDir;
    return eng::SunDir ? eng::SunDir : nullptr;
}

static void update_cascade_math_from_context() {
    const ShadowFrameContext& ctx = g_shadowFrameContext;
    if (!ctx.valid || ctx.viewport[2] <= 0 || ctx.viewport[3] <= 0) return;

    // Standard OpenGL perspective matrix.  The engine records near=0.1,
    // far=45 in this build; recover them from the frozen projection instead of
    // trusting an unrelated Camera object or the mutable matrix stack.
    const float p10 = ctx.projection[10];
    const float p14 = ctx.projection[14];
    const float nearClip = p14 / (p10 - 1.0f);
    float farClip  = p14 / (p10 + 1.0f);
    if (!std::isfinite(nearClip) || !std::isfinite(farClip) ||
        nearClip <= 0.0f || farClip <= nearClip + 0.01f) {
        if (g_cascadeMathLogCount < g_cascadeMathLogFrames) {
            fprintf(stderr, "[shadowmap][csm] invalid frozen projection near=%.7f far=%.7f "
                            "(p10=%.7f p14=%.7f)\n",
                    nearClip, farClip, p10, p14);
            ++g_cascadeMathLogCount;
        }
        return;
    }

    float lightDir[3] = { g_lightDir[0], g_lightDir[1], g_lightDir[2] };
    if (!g_dirOverride && g_haveTraceAreaLight) {
        lightDir[0] = g_traceAreaLightDir[0];
        lightDir[1] = g_traceAreaLightDir[1];
        lightDir[2] = g_traceAreaLightDir[2];
    } else if (!g_dirOverride && eng::SunDir) {
        const float engineLength = std::sqrt(eng::SunDir[0] * eng::SunDir[0] +
                                             eng::SunDir[1] * eng::SunDir[1] +
                                             eng::SunDir[2] * eng::SunDir[2]);
        // Some valid gameplay areas expose the symbol before their sun state
        // is populated.  Match the proven live-light rule: only replace the
        // explicit default when the engine vector is usable.
        if (std::isfinite(engineLength) && engineLength > 1.0e-4f) {
            lightDir[0] = eng::SunDir[0] * g_dirSign;
            lightDir[1] = eng::SunDir[1] * g_dirSign;
            lightDir[2] = eng::SunDir[2] * g_dirSign;
        }
    }
    const float lightLen = std::sqrt(lightDir[0] * lightDir[0] +
                                     lightDir[1] * lightDir[1] +
                                     lightDir[2] * lightDir[2]);
    if (!std::isfinite(lightLen) || lightLen < 1.0e-5f) return;
    lightDir[0] /= lightLen; lightDir[1] /= lightLen; lightDir[2] /= lightLen;

    // This matches build_lookat's basis.  Snap only in the light-image plane:
    // moving along the light direction must not perturb the fitted XY texels.
    float worldUp[3] = { 0.0f, 0.0f, 1.0f };
    if (std::fabs(lightDir[2]) > 0.999f) {
        worldUp[1] = 1.0f;
        worldUp[2] = 0.0f;
    }
    float side[3] = {
        lightDir[1] * worldUp[2] - lightDir[2] * worldUp[1],
        lightDir[2] * worldUp[0] - lightDir[0] * worldUp[2],
        lightDir[0] * worldUp[1] - lightDir[1] * worldUp[0]
    };
    v_normalize(side);
    const float lightUp[3] = {
        side[1] * lightDir[2] - side[2] * lightDir[1],
        side[2] * lightDir[0] - side[0] * lightDir[2],
        side[0] * lightDir[1] - side[1] * lightDir[0]
    };

    const float tanX = 1.0f / std::fabs(ctx.projection[0]);
    const float tanY = 1.0f / std::fabs(ctx.projection[5]);
    if (!std::isfinite(tanX) || !std::isfinite(tanY) || tanX <= 0.0f || tanY <= 0.0f)
        return;

    // Reuse the existing fit unless something that invalidates it changed.
    // Skipping the refit is what makes the static depth cache legal: the
    // cached layers were rendered with these exact matrices.
    if (g_cascadeStaticCache && g_cascadeMath.valid && g_haveCascadeLastFit) {
        const float fwd[3] = { -ctx.view[2], -ctx.view[6], -ctx.view[10] };
        const float dx = ctx.eye.x - g_cascadeLastFitEye[0];
        const float dy = ctx.eye.y - g_cascadeLastFitEye[1];
        const float dz = ctx.eye.z - g_cascadeLastFitEye[2];
        const float moved2 = dx*dx + dy*dy + dz*dz;
        const float turn = fwd[0]*g_cascadeLastFitFwd[0] + fwd[1]*g_cascadeLastFitFwd[1] +
                           fwd[2]*g_cascadeLastFitFwd[2];
        // SUN HYSTERESIS. The bare 1e-3 compare that used to be here meant a
        // moving sun invalidated the fit EVERY frame: measured on Windows,
        // 1682 sun refits, 79 move refits and *zero* cache hits over a whole
        // session, i.e. every static caster re-rendered into every near layer
        // on every main-scene render. The world map needed exactly this fix for
        // exactly this reason (a day/night cycle) and got 0.05 + a rate limit.
        // 0.01 on a normalised direction is ~0.6 degrees -- below the angle at
        // which a refit is visible as a pop, and enough that a drifting sun
        // refits every N frames instead of every one.
        // COMPARE THE VECTOR THE FIT ACTUALLY USES. The fit below prefers
        // g_traceAreaLightDir (observed from the engine's per-frame uniform
        // uploads) and only falls back to eng::SunDir, which is a global that
        // does not move at runtime. This check read eng::SunDir unconditionally,
        // so it was comparing a different quantity from the one the cached
        // matrices were built from -- the exact defect static_world_needs_refresh
        // already carries a comment about, fixed there and missed here.
        const float* fitSun = cascade_fit_sun();
        float sunDelta = 0.0f;
        if (fitSun)
            for (int i = 0; i < 3; ++i)
                sunDelta = std::max(sunDelta,
                                    std::fabs(fitSun[i] - g_cascadeLastFitSun[i]));
        const bool sunSame = (sunDelta <= kCascadeSunHysteresis);
        if (moved2 <= g_cascadeCacheMove * g_cascadeCacheMove &&
            turn >= g_cascadeCacheTurn && sunSame &&
            g_cascadeLastFitScene == ctx.scene) {
            // Same fit: keep every matrix, just advance the serial so the
            // freshness gates still see this frame as current.
            g_cascadeMath.sourceFrame = ctx.serial;
            ++g_cascadeCacheHits;
            return;
        }
        // WHICH condition rejected the fit, recorded AT THE POINT OF REJECTION.
        // A refit re-renders every static caster into every near layer, so a
        // cache that never hits costs the whole replay every frame -- and the
        // four causes want completely different fixes. Reporting a single
        // "refits" total cannot tell them apart, and this project has already
        // lost two test rounds to diagnostics that described the wrong thing.
        if (!(moved2 <= g_cascadeCacheMove * g_cascadeCacheMove)) ++g_cascadeRefitMove;
        else if (!(turn >= g_cascadeCacheTurn))                   ++g_cascadeRefitTurn;
        else if (!sunSame) {
            ++g_cascadeRefitSun;
            // The MAGNITUDE distinguishes the two causes, which want different
            // fixes: a small delta every time is a sun genuinely drifting (a
            // day/night cycle) and hysteresis is the answer; a large one is the
            // vector flipping between values, and no threshold would help.
            static unsigned reported = 0;
            if (g_costReport && (reported++ % 256) == 0 && fitSun)
                fprintf(stderr, "[shadowmap][csm] fit refit on SUN: delta=%.5f "
                                "(limit %.5f) dir=%.4f %.4f %.4f last=%.4f %.4f %.4f "
                                "src=%s\n",
                        sunDelta, kCascadeSunHysteresis,
                        fitSun[0], fitSun[1], fitSun[2],
                        g_cascadeLastFitSun[0], g_cascadeLastFitSun[1],
                        g_cascadeLastFitSun[2],
                        (!g_dirOverride && g_haveTraceAreaLight) ? "trace" : "engine");
        }
        else                                                      ++g_cascadeRefitScene;
    } else if (!g_cascadeStaticCache) ++g_cascadeRefitDisabled;

    CascadeMathState next{};
    next.valid = true;
    next.sourceFrame = ctx.serial;
    float previousClip = nearClip;
    const int   activeCascades = (g_cascadeActiveCount < 1) ? 1
                               : (g_cascadeActiveCount > kCascadeCount ? kCascadeCount
                                                                       : g_cascadeActiveCount);
    if (g_cascadeMaxDistance > 0.0f && g_cascadeMaxDistance < farClip)
        farClip = g_cascadeMaxDistance;
    for (int cascade = 0; cascade < activeCascades; ++cascade) {
        const float fraction = float(cascade + 1) / float(activeCascades);
        const float logarithmic = nearClip * std::pow(farClip / nearClip, fraction);
        const float uniform = nearClip + (farClip - nearClip) * fraction;
        const float split = (cascade == activeCascades - 1)
            ? farClip : (g_cascadeLambda * logarithmic + (1.0f - g_cascadeLambda) * uniform);

        Vec3f corners[8]{};
        int n = 0;
        for (int plane = 0; plane < 2; ++plane) {
            const float depth = plane ? split : previousClip;
            const float hx = depth * tanX;
            const float hy = depth * tanY;
            for (int y = -1; y <= 1; y += 2) {
                for (int x = -1; x <= 1; x += 2) {
                    const float vx = float(x) * hx;
                    const float vy = float(y) * hy;
                    const float vz = -depth;
                    const float* inv = ctx.viewInverse;
                    corners[n].x = inv[0]*vx + inv[4]*vy + inv[8]*vz + inv[12];
                    corners[n].y = inv[1]*vx + inv[5]*vy + inv[9]*vz + inv[13];
                    corners[n].z = inv[2]*vx + inv[6]*vy + inv[10]*vz + inv[14];
                    ++n;
                }
            }
        }

        Vec3f centre{};
        for (const Vec3f& corner : corners) {
            centre.x += corner.x; centre.y += corner.y; centre.z += corner.z;
        }
        centre.x *= 0.125f; centre.y *= 0.125f; centre.z *= 0.125f;

        float radius = 0.0f;
        for (const Vec3f& corner : corners) {
            const float dx = corner.x - centre.x;
            const float dy = corner.y - centre.y;
            const float dz = corner.z - centre.z;
            radius = std::max(radius, std::sqrt(dx*dx + dy*dy + dz*dz));
        }
        // Quantising radius itself prevents tiny floating-point fit changes from
        // changing the projected scale as the camera traverses a smooth orbit.
        const float padded = radius + (g_cascadeStaticCache ? g_cascadeCacheMove : 0.0f);
        const float extent = std::ceil(padded * 16.0f) * (1.0f / 16.0f);
        const float texelWorld = (2.0f * extent) / float(g_cascadeMathResolution);
        if (!std::isfinite(extent) || extent <= 0.0f || texelWorld <= 0.0f) return;

        const float sideCoord = centre.x*side[0] + centre.y*side[1] + centre.z*side[2];
        const float upCoord   = centre.x*lightUp[0] + centre.y*lightUp[1] + centre.z*lightUp[2];
        const float snappedSide = std::floor(sideCoord / texelWorld + 0.5f) * texelWorld;
        const float snappedUp   = std::floor(upCoord   / texelWorld + 0.5f) * texelWorld;
        centre.x += (snappedSide - sideCoord) * side[0] + (snappedUp - upCoord) * lightUp[0];
        centre.y += (snappedSide - sideCoord) * side[1] + (snappedUp - upCoord) * lightUp[1];
        centre.z += (snappedSide - sideCoord) * side[2] + (snappedUp - upCoord) * lightUp[2];

        const float lightDistance = extent + 4.0f;
        const Vec3f lightEye {
            centre.x - lightDir[0] * lightDistance,
            centre.y - lightDir[1] * lightDistance,
            centre.z - lightDir[2] * lightDistance,
            1.0f
        };
        float lightView[16], lightProjection[16];
        build_lookat(lightEye, lightDir, lightView);
        build_ortho(extent, 0.1f, lightDistance * 2.0f, lightProjection);
        std::memcpy(next.lightView[cascade], lightView, sizeof(lightView));
        std::memcpy(next.lightProjection[cascade], lightProjection,
                    sizeof(lightProjection));
        mat4_mul(lightProjection, lightView, next.lightVP[cascade]);
        next.clipFar[cascade] = split;
        next.extent[cascade] = extent;
        next.texelWorld[cascade] = texelWorld;
        next.centre[cascade] = centre;
        previousClip = split;
    }
    // Unused layers mirror the last active one. Two consequences, both wanted:
    // the receiver's existing "z > clipFar3 -> discard" becomes the shadow
    // distance cutoff, and its layer selection can never land on a stale slot,
    // so no shader change is needed to support fewer cascades.
    for (int cascade = activeCascades; cascade < kCascadeCount; ++cascade) {
        const int src = activeCascades - 1;
        next.clipFar[cascade]   = next.clipFar[src];
        next.extent[cascade]    = next.extent[src];
        next.texelWorld[cascade]= next.texelWorld[src];
        next.centre[cascade]    = next.centre[src];
        std::memcpy(next.lightView[cascade],       next.lightView[src],       sizeof(next.lightView[src]));
        std::memcpy(next.lightProjection[cascade], next.lightProjection[src], sizeof(next.lightProjection[src]));
        std::memcpy(next.lightVP[cascade],         next.lightVP[src],         sizeof(next.lightVP[src]));
    }
    g_cascadeMath = next;
    ++g_cascadeMathGeneration;
    ++g_cascadeCacheRefits;
    g_cascadeLastFitEye[0] = ctx.eye.x;
    g_cascadeLastFitEye[1] = ctx.eye.y;
    g_cascadeLastFitEye[2] = ctx.eye.z;
    g_cascadeLastFitFwd[0] = -ctx.view[2];
    g_cascadeLastFitFwd[1] = -ctx.view[6];
    g_cascadeLastFitFwd[2] = -ctx.view[10];
    // Snapshot the SAME vector the invalidation test reads (cascade_fit_sun),
    // not eng::SunDir. Writing one source and comparing another is why this
    // snapshot sat at (0,0,0) while the compare saw a unit vector, so the test
    // failed by a constant 0.758 forever and the cache never hit once.
    if (const float* fitSun = cascade_fit_sun())
        for (int i = 0; i < 3; ++i) g_cascadeLastFitSun[i] = fitSun[i];
    g_cascadeLastFitScene = ctx.scene;
    g_haveCascadeLastFit = true;

    if (g_cascadeMathLogCount < g_cascadeMathLogFrames) {
        fprintf(stderr, "[shadowmap][csm] frame=%u near=%.4f far=%.4f lambda=%.2f "
                        "dir=(%.4f %.4f %.4f) resolution=%d\n",
                ctx.serial, nearClip, farClip, g_cascadeLambda,
                lightDir[0], lightDir[1], lightDir[2], g_cascadeMathResolution);
        for (int cascade = 0; cascade < 4; ++cascade) {
            fprintf(stderr, "[shadowmap][csm] c%d clipFar=%.4f centre=(%.3f %.3f %.3f) "
                            "extent=%.4f texel=%.6f vp=%08x\n",
                    cascade, next.clipFar[cascade], next.centre[cascade].x,
                    next.centre[cascade].y, next.centre[cascade].z,
                    next.extent[cascade], next.texelWorld[cascade],
                    trace_matrix_hash_data(next.lightVP[cascade]));
        }
        ++g_cascadeMathLogCount;
    }
}

static void render_from_light(void* scene) {
    if (!g_usable || !eng::SceneRenderDrawBucket) return;
    // NWN renders many Scenes: the world, plus menu backdrops, the character
    // portrait and the minimap, each with its own camera (the eye=(0,0,742)
    // sightings). Only the world scene fills the window, so use viewport width
    // as the discriminator -- the small ones were burning a whole light pass to
    // produce samples=0.
    if (!g_isWorldScene) return;
    if (g_areaScene && scene != g_areaScene) {
        static int skippedForeignScene = 0;
        if (skippedForeignScene++ < 3)
            fprintf(stderr, "[shadowmap] skipped non-area Scene=%p (area=%p)\n",
                    scene, g_areaScene);
        return;
    }
    // Do not use camera height as a scene discriminator.  The area scene can
    // legitimately enter with the same z=742 UI-style camera while it is
    // loading, and rejecting it prevents the very first valid light pass.
    // Viewport classification above remains the non-destructive guard.
    if (now_seconds() < g_lightStart) return;      // let the module finish loading
    if (g_viewSub && !g_haveCam) return;

    g_faultLabel = "LIGHT PASS";

    float dir[3] = { g_lightDir[0], g_lightDir[1], g_lightDir[2] };
    // Prefer the engine's own sun over our placeholder, so the shadow map is
    // cast from the direction the engine is actually lighting from.
    if (!g_dirOverride && eng::SunDir) {
        float l = std::sqrt(eng::SunDir[0]*eng::SunDir[0] +
                            eng::SunDir[1]*eng::SunDir[1] +
                            eng::SunDir[2]*eng::SunDir[2]);
        if (l > 1e-4f) {
            dir[0] = eng::SunDir[0] * g_dirSign;
            dir[1] = eng::SunDir[1] * g_dirSign;
            dir[2] = eng::SunDir[2] * g_dirSign;
        }
    }
    v_normalize(dir);

    // Put the light back along -dir from the camera so the map follows the
    // player. A real implementation would fit this to the view frustum.
    // Centre the shadow box on what the CAMERA IS LOOKING AT, not on the camera
    // itself. NWN's camera sits behind and above the player looking forward and
    // down, so a box centred on the eye covers ground beside and behind it while
    // everything on screen is ahead -- the map came back half empty with the
    // visible trees outside it entirely. Forward is the third row of the view
    // rotation, negated: f = -(m[2], m[6], m[10]).
    // Use the eye latched at ENTRY to Scene::Render, not the one live now.
    // We run after orig(), and the engine leaves whatever view its last pass
    // used -- sometimes a skybox/far pass at z~761, which flings the light off
    // the world and yields samples=0. At entry the view is always the world
    // camera, because the engine sets it outside Scene::Render entirely.
    Vec3f  focus = g_haveWorldEye ? g_worldEye : g_camPos;
    if (g_haveCamView && g_focus > 0.0f) {
        const float fwd[3] = { -g_camView[2], -g_camView[6], -g_camView[10] };
        focus.x += fwd[0] * g_focus;
        focus.y += fwd[1] * g_focus;
        focus.z += fwd[2] * g_focus;
    }

    // Stabilise the orthographic shadow grid.  Without this, every sub-texel
    // pan/zoom changes the light-view translation, moving comparison edges
    // across the screen in visible jumps.  Quantise only the two axes
    // perpendicular to the light; movement along the ray must stay continuous
    // so the depth range remains correct.
    if (g_useOrtho && g_size > 0) {
        float up[3] = { 0.0f, 0.0f, 1.0f };
        if (std::fabs(dir[2]) > 0.999f) { up[0] = 0.0f; up[1] = 1.0f; up[2] = 0.0f; }
        float side[3] = { dir[1]*up[2] - dir[2]*up[1],
                          dir[2]*up[0] - dir[0]*up[2],
                          dir[0]*up[1] - dir[1]*up[0] };
        v_normalize(side);
        float lightUp[3] = { side[1]*dir[2] - side[2]*dir[1],
                             side[2]*dir[0] - side[0]*dir[2],
                             side[0]*dir[1] - side[1]*dir[0] };
        const float texel = (2.0f * g_extent) / (float)g_size;
        const float sx = focus.x*side[0] + focus.y*side[1] + focus.z*side[2];
        const float sy = focus.x*lightUp[0] + focus.y*lightUp[1] + focus.z*lightUp[2];
        const float dx = std::round(sx / texel) * texel - sx;
        const float dy = std::round(sy / texel) * texel - sy;
        focus.x += side[0]*dx + lightUp[0]*dy;
        focus.y += side[1]*dx + lightUp[1]*dy;
        focus.z += side[2]*dx + lightUp[2]*dy;
    }
    Vec3f lightPos = focus;
    lightPos.x -= dir[0] * g_dist;
    lightPos.y -= dir[1] * g_dist;
    lightPos.z -= dir[2] * g_dist;
    Quat4f lightRot = light_quat(dir, g_conv);

    bind_shadow_unit(0);            // break the feedback loop before rendering
    SavedState st = bind_target(true);
    clear_target();

    g_inOurPass = true;
    g_lightDrawCalls = g_lightCasterDrawCalls = g_lightMutedDrawCalls = 0;
    g_lightProgramBinds = g_lightCasterProgramBinds = 0;
    g_lightPreloadedCasters = set_known_caster_mode(true);
    // The replay frequently has no program-bind or PLT draw transition. The
    // preloaded caster shader therefore owns the attachment unconditionally.
    if (g_lightPreloadedCasters && gl::ColorMask)
        gl::ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    if (g_viewSub == 1 && eng::SetViewTransform)
        eng::SetViewTransform(&lightPos, &lightRot);
    else if (g_viewSub == 2)
        set_light_view_direct(lightPos, dir);
    else if (g_viewSub == 3 && eng::SetViewTransform)
        eng::SetViewTransform(&lightPos, &g_camQuat);   // moved eye, engine's
                                                        // own orientation

    // Orthographic projection, written straight into entry 0 (which the matrix
    // dump proved is the projection stack) with the same byte-exact save/restore
    // as the view. This replaces the old SetOrthoTransform call, which applied
    // to whatever the current matrix mode happened to be and corrupted the
    // frame. Ortho also removes the camera's 45-unit far plane, so the light no
    // longer has to sit uncomfortably close to the scene.
    float  lightView[16], lightProj[16];
    bool   haveLightView = false, haveLightProj = false;
    float  savedProj[16];
    float* pm = nullptr;
    if (g_useOrtho) {
        pm = mtx_entry(MTX_PROJ);
        if (pm) {
            std::memcpy(savedProj, pm, sizeof(savedProj));
            build_ortho(g_extent, 1.0f, g_dist * 2.0f + g_extent * 2.0f, lightProj);
            haveLightProj = true;
            std::memcpy(pm, lightProj, sizeof(lightProj));
        }
    }

    float  savedView[16];
    float* vm       = nullptr;
    int    oldMode  = 0;
    if (g_viewSub == 4) {
        vm = mtx_entry(MTX_VIEW);
        if (vm) {
            std::memcpy(savedView, vm, sizeof(savedView));   // byte-exact save
            build_lookat(lightPos, dir, lightView);
            haveLightView = true;
            std::memcpy(vm, lightView, sizeof(lightView));
            // CRITICAL: select mode 1 only long enough to flag the view dirty,
            // then put the current mode BACK before drawing. Run A (which
            // worked) rendered with the current mode left at 2; leaving it at 1
            // means RenderDrawBucket's per-object model transforms land on the
            // VIEW stack, which wrecks both the light view and the camera.
            oldMode = eng::CurMtxIdx ? *eng::CurMtxIdx : 0;
            if (eng::SetMatrixMode) eng::SetMatrixMode(MTX_VIEW);
            if (eng::FlagDirty)     eng::FlagDirty();
            if (pm && eng::SetMatrixMode) {          // projection too, if we
                eng::SetMatrixMode(MTX_PROJ);        // replaced it
                if (eng::FlagDirty) eng::FlagDirty();
            }
            if (eng::SetMatrixMode) eng::SetMatrixMode(oldMode);
        }
    }

    // Publish the exact matrices used by this private FBO pass before any
    // bucket material uploads begin.  The uniform hook consumes these only
    // while g_inOurPass is set, and they are cleared on the way out below.
    g_haveActiveLightMatrices = haveLightView && haveLightProj;
    if (g_haveActiveLightMatrices) {
        std::memcpy(g_activeLightView, lightView, sizeof(g_activeLightView));
        std::memcpy(g_activeLightProj, lightProj, sizeof(g_activeLightProj));
        mat4_mul(g_activeLightProj, g_activeLightView, g_activeLightWorldVP);
    }

    // NWN_SHADOWMAP_NOBUCKETS isolates "does the matrix manipulation alone
    // corrupt the camera?" from "does rendering with a substituted camera
    // corrupt it?". If the snap survives with no buckets drawn, the fault is in
    // the matrix handling; if it disappears, it is the drawing.
    const double tnow  = now_seconds();
    const bool   status = (g_statusEvery > 0.0 && tnow >= g_nextStatus);
    const bool gpuLiveDue = use_gpu_live_bridge() && g_lightPreloadedCasters;
    const bool gpuStepDue = use_gpu_step_bridge() && g_lightPreloadedCasters &&
                            tnow >= g_nextGpuStepCopy;
    const bool gpuCopyDue = gpuLiveDue || gpuStepDue;
    const bool cascadeCopyDue = shadow_getenv("NWN_SHADOWMAP_CASCADE_COPY") != nullptr &&
                                g_cascadeTargetsUsable;
    // The occlusion query is the validity fence for the detached receiver
    // texture: without it, an empty portrait/minimap render can overwrite a
    // good world map with the clear value.
    const bool querySamples = status || gpuCopyDue || cascadeCopyDue;
    if (querySamples) gl::BeginQuery(GL_SAMPLES_PASSED, g_query);

    if (!g_noBuckets)
        for (int i : g_lightBuckets)
            if (!guarded_render_bucket(scene, i)) { g_lightPass = false; break; }

    GLuint lightSamples = 0;
    if (querySamples) {
        gl::EndQuery(GL_SAMPLES_PASSED);
        gl::GetQueryObjectuiv(g_query, GL_QUERY_RESULT, &lightSamples);
    }
    bool gpuStepUpdated = false;
    if (gpuCopyDue) {
        // A full-size viewport may still invoke a Scene with no world geometry
        // (portrait/minimap/transition). Never replace a valid snapshot with
        // that clear map. Try again shortly rather than waiting another second.
        if (lightSamples > 1024) {
            gpuStepUpdated = update_gpu_step_bridge();
            if (gpuStepUpdated && gpuStepDue) g_nextGpuStepCopy = tnow + 1.0;
        } else {
            if (gpuStepDue) g_nextGpuStepCopy = tnow + 0.10;
            static int skipped = 0;
            if (skipped++ < 4)
                fprintf(stderr, "[shadowmap] GPU copy bridge skipped empty pass\n");
        }
    }
    // First PS4-style replay bridge: copy the proven primary depth target into
    // dynamic cascade layer zero only after the occlusion query confirms this
    // was a real world pass.  Other cascade layers and static classification
    // remain future stages.
    if (cascadeCopyDue && lightSamples > 1024) {
        if (copy_primary_depth_to_dynamic_cascade0())
            verify_dynamic_cascade_layer0_once();
    }
    diagnose_colour_caster_target_once();

    if (shadow_getenv("NWN_SHADOWMAP_COLORCAST_TEST")) {
        static int casterReports = 0;
        static bool reportedPreload = false;
        if (casterReports++ < 8 || (g_lightPreloadedCasters && !reportedPreload) || g_verbose) {
            if (g_lightPreloadedCasters) reportedPreload = true;
            fprintf(stderr, "[shadowmap] colour-caster replay: draws=%u injected=%u muted=%u "
                            "programs=%u caster-programs=%u preloaded=%u\n",
                    g_lightDrawCalls, g_lightCasterDrawCalls, g_lightMutedDrawCalls,
                    g_lightProgramBinds, g_lightCasterProgramBinds,
                    g_lightPreloadedCasters);
        }
    }

    if (status) {
        g_nextStatus = tnow + g_statusEvery;
        // Occlusion query, not a readback -- costs nothing, so it can run
        // forever. A healthy light pass keeps reporting millions of samples;
        // zero means the light has drifted off the world or the buckets emptied.
        fprintf(stderr, "[shadowmap] ok  samples=%u  eye=(%.1f %.1f %.1f)  "
                        "dir=(%.2f %.2f %.2f)  progs=%d/%ld  frame=%ld\n",
                lightSamples, lightPos.x, lightPos.y, lightPos.z,
                dir[0], dir[1], dir[2], g_progCount, g_progHits, g_frames);
    }

    if (g_viewSub == 4 && vm) {
        if (eng::SetMatrixMode) eng::SetMatrixMode(MTX_VIEW);
        std::memcpy(vm, savedView, sizeof(savedView));       // byte-exact restore
        if (eng::FlagDirty)     eng::FlagDirty();
        if (pm) {
            if (eng::SetMatrixMode) eng::SetMatrixMode(MTX_PROJ);
            std::memcpy(pm, savedProj, sizeof(savedProj));
            if (eng::FlagDirty) eng::FlagDirty();
        }
        if (eng::SetMatrixMode) eng::SetMatrixMode(oldMode);
    }

    // Do NOT replay SetViewTransform/SetPerspective here. The direct stack
    // copies above already restored the exact matrices active before our light
    // pass. Replaying captured arguments afterward overwrites that restoration
    // with a stale menu/skybox camera and makes the shader's m_view_inv disagree
    // with the scene that was actually rendered.
    set_known_caster_mode(false);
    g_inOurPass = false;
    g_haveActiveLightMatrices = false;

    // Opt-in live GPU conversion proof. It writes only the private GPU target;
    // receivers still use the known-good stepped mirror until this pass is
    // independently verified.
    if (shadow_getenv("NWN_SHADOWMAP_GPU_TEST")) run_gpu_depth_decode();

    // If we did not substitute the projection, the light pass still rendered
    // through whatever is in entry 0, so read that back -- the shadow matrix
    // must describe the projection actually used, not the one we intended.
    if (!haveLightProj) {
        const float* p0 = mtx_entry(MTX_PROJ);
        if (p0) { std::memcpy(lightProj, p0, sizeof(lightProj)); haveLightProj = true; }
    }

    float candidateShadowMat[16], candidateLightVP[16];
    bool haveCandidateShadowMat = false;
    if (haveLightView && haveLightProj && g_haveCamView) {
        float invCam[16];
        mat4_rigid_inverse(g_camView, invCam);
        mat4_mul(lightProj, lightView, candidateLightVP);
        mat4_mul(candidateLightVP, invCam, candidateShadowMat);
        haveCandidateShadowMat = true;
    }
    // The colour-caster target is produced synchronously by the bucket replay,
    // so unlike the old CPU mirror it is fresh every frame.
    const bool gpuUpdated = gpuStepUpdated || (!use_cpu_bridge() && !use_gpu_copy_bridge() &&
                            ((shadow_getenv("NWN_SHADOWMAP_GPU_TEST") && g_gpuDecodeProgram) ||
                             shadow_getenv("NWN_SHADOWMAP_COLORCAST_TEST")));
    if (haveCandidateShadowMat && (gpuUpdated || update_depth_mirror())) {
        std::memcpy(g_shadowMat, candidateShadowMat, sizeof(g_shadowMat));
        std::memcpy(g_shadowSourceView, g_camView, sizeof(g_shadowSourceView));
        std::memcpy(g_lightWorldVP, candidateLightVP, sizeof(g_lightWorldVP));
        g_haveShadowMat = true;
        g_haveLightWorldVP = true;
    }
    bind_shadow_texture();

    restore_state(st);

    static bool logged = false;
    if (!logged) {
        logged = true;
        fprintf(stderr,
            "[shadowmap] light pass running. view=%s dir=(%.2f %.2f %.2f) conv=%d "
            "extent=%.1f dist=%.1f ortho=%s\n"
            "[shadowmap] lightPos=(%.2f %.2f %.2f) quat=(%.4f %.4f %.4f %.4f)\n"
            "[shadowmap] engine sets the view inside Scene::Render(): %s\n",
            g_viewSub == 4 ? "LIGHT (direct matrix write)" :
            g_viewSub == 3 ? "LIGHT pos + ENGINE orientation" :
            g_viewSub == 2 ? "LIGHT (direct matrix)" :
            g_viewSub == 1 ? "LIGHT (SetViewTransform)" : "engine camera (unchanged)",
            dir[0], dir[1], dir[2], g_conv, g_extent, g_dist,
            g_useOrtho ? "on" : "off (keeping engine perspective)",
            lightPos.x, lightPos.y, lightPos.z,
            lightRot.a, lightRot.b, lightRot.c, lightRot.d,
            g_viewInsideRender ? "yes" : "no");
    }
}
