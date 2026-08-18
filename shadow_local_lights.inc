// ===========================================================================
//  Phase 6b: local-light single-perspective depth probe
//  Additive and isolated: its own FBO/texture, never g_fbo or g_cascade*, and
//  gated entirely behind NWN_SHADOWMAP_LOCAL_LIGHT_CAPTURE (default off). Per
//  SHADOWMAP_STATUS.md's local-light plan this is deliberately depth-only --
//  no receiver/composite -- until a PGM dump proves the depth map itself.
// ===========================================================================

// Mirrors create_target()'s depth-texture recipe (see there for the driver
// notes on COMPARE_MODE/border colour) but at its own, typically smaller,
// resolution and FBO: a single-light probe has no reason to pay full sun-CSM
// cost, and must never alias the sun's targets.
static void create_local_light_target() {
    if (g_localLightFbo) return;   // already created (or already attempted)

    GLint prevFBO = 0, prevTex = 0;
    gl::GetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    gl::GetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    while (gl::GetError() != GL_NO_ERROR) {}   // drain pre-existing errors

    // SIX FACES, as a depth 2D ARRAY. A point light needs the whole sphere:
    // one cone can hold the light or the things around it, never both, which
    // is what every measurement in this thread kept running into. Faces are
    // +X,-X,+Y,-Y,+Z,-Z at 90 degrees, and the receiver picks one from the
    // major axis of (fragment - light).
    //
    // An array rather than GL_TEXTURE_CUBE_MAP because the capture attaches one
    // LAYER per face with glFramebufferTextureLayer, exactly like the sun
    // cascades already do, and the receiver's face selection is then a plain
    // index instead of cube-face sampling rules.
    gl::GenTextures(1, &g_localLightDepthTex);
    gl::BindTexture(GL_TEXTURE_2D_ARRAY, g_localLightDepthTex);
    gl::TexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT24,
                   g_localLightCaptureSize, g_localLightCaptureSize,
                   g_localLightLayers, 0,
                   GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    gl::TexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl::TexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl::TexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl::TexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // Comparison sampling so the Phase 6d receiver can read this through a
    // sampler2DShadow and get hardware 2x2 PCF, matching the cascade arrays.
    // This does NOT affect the PGM dump: glGetTexImage returns raw depth
    // regardless of the comparison mode.
    // NONE, not COMPARE_REF_TO_TEXTURE: the receiver reads this through a plain
    // sampler2DArray and compares in the shader, which is what made the
    // comparison observable. The two must change together -- sampling a
    // compare-mode texture through a non-shadow sampler is undefined.
    gl::TexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE, 0 /*GL_NONE*/);

    const GLenum errTex = gl::GetError();

    gl::GenFramebuffers(1, &g_localLightFbo);
    gl::BindFramebuffer(GL_FRAMEBUFFER, g_localLightFbo);
    gl::FramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                g_localLightDepthTex, 0, 0);
    gl::DrawBuffer(GL_NONE);
    gl::ReadBuffer(GL_NONE);
    const GLenum status = gl::CheckFramebufferStatus(GL_FRAMEBUFFER);
    const GLenum errFbo = gl::GetError();

    g_localLightTargetUsable = (status == GL_FRAMEBUFFER_COMPLETE) &&
                               errTex == GL_NO_ERROR && errFbo == GL_NO_ERROR;

    gl::BindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFBO);
    gl::BindTexture(GL_TEXTURE_2D, (GLuint)prevTex);

    fprintf(stderr, "[shadowmap][local-light] target %dx%dx6 fbo=%u tex=%u status=%s usable=%s\n",
            g_localLightCaptureSize, g_localLightCaptureSize,
            g_localLightFbo, g_localLightDepthTex, fbo_status_str(status),
            g_localLightTargetUsable ? "yes" : "no");
}

// Same shape as bind_target()/restore_state() (SavedState is generic), but
// binds the private local-light FBO instead and never touches colour output.
static SavedState bind_local_light_target() {
    SavedState st;
    gl::GetIntegerv(GL_FRAMEBUFFER_BINDING, &st.fbo);
    gl::GetIntegerv(GL_VIEWPORT, st.vp);
    gl::GetBooleanv(GL_DEPTH_WRITEMASK, &st.depthMask);
    gl::GetBooleanv(GL_COLOR_WRITEMASK, st.colorMask);
    st.scissor   = gl::IsEnabled(GL_SCISSOR_TEST);
    st.depthTest = gl::IsEnabled(GL_DEPTH_TEST);

    gl::BindFramebuffer(GL_FRAMEBUFFER, g_localLightFbo);
    gl::Viewport(0, 0, g_localLightCaptureSize, g_localLightCaptureSize);
    if (st.scissor)    gl::Disable(GL_SCISSOR_TEST);
    if (!st.depthMask) gl::DepthMask(GL_TRUE);
    gl::ColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);  // depth-only target
    if (!st.depthTest) gl::Enable(GL_DEPTH_TEST);
    return st;
}


static unsigned g_localLightCaptureFrame = 0;
static unsigned g_localLightCaptureDraws = 0;


static void capture_local_light_shadow(void* scene) {
    // Do NOT gate on g_usable here. That flag belongs to the legacy
    // single-map target created by create_target(), which is only ever called
    // on the NON-trace path -- and every launcher that enables this probe runs
    // in trace mode, where SceneRender_detour returns before that call. So
    // g_usable is permanently false here and gating on it silently disabled
    // the entire capture (census logged fine, no target, no PGM, no clue).
    // This probe owns its own target and its own usability flag below.
    if (!g_localLightCapture) return;
    // Master switch, same reason as the replay: no point selecting lights,
    // building matrices and publishing slots for a pass that will not draw.
    if (!g_receiverEnabled) return;
    // Resolution changes own a GL texture, so the reallocation happens HERE --
    // at the top of the capture, outside any bound target -- but only once the
    // Performance page's Apply has asked for it (g_localSizeCommit). That keeps
    // all three resolutions on one button instead of this one changing under
    // the panel while the other two wait.
    // Layers needed right now. Changing lights or faces changes this, and the
    // texture has to be rebuilt for it exactly like a resolution change.
    {
        // +1 FOR THE FADE-OUT SLOT. A light NWN has dropped keeps its entry in
        // the join table with a decaying level and is published as an ORDINARY
        // EXTRA SLOT until it reaches zero -- that is how fading out works at
        // all (see the join table: "FADING SLOTS ARE CAPPED AT ONE").
        //
        // Sizing the array to lights*faces exactly left that slot with no layer,
        // so dropped lights popped out instead of fading. Regression introduced
        // and fixed the same day, 2026-08-15; the cap of one is why a single
        // spare is enough.
        const int want = std::max(1, std::min(local_source_lights() * local_source_faces() + 1,
                                              (int)kLocalLightFaces));
        if (want != g_localLightLayers && g_localLightFbo) {
            g_localLightLayers = want;
            gl::DeleteFramebuffers(1, &g_localLightFbo);
            if (g_localLightDepthTex) gl::DeleteTextures(1, &g_localLightDepthTex);
            g_localLightFbo = 0; g_localLightDepthTex = 0;
            g_localLightTargetUsable = false;
            g_haveLocalLightVP = false;
            g_localLightSlotCount = 0;
            g_localInlinePrepared = false;
        } else {
            g_localLightLayers = want;
        }
    }
    if (g_localSizeCommit && g_pendingLocalSize > 0 &&
        g_pendingLocalSize != g_localLightCaptureSize) {
        g_localSizeCommit = false;
        fprintf(stderr, "[shadowmap][local-light] resolution %d -> %d (%d lights, %.0f MB)\n",
                g_localLightCaptureSize, g_pendingLocalSize, kLocalLightFaces,
                (double)g_pendingLocalSize * g_pendingLocalSize * 4.0 *
                g_localLightLayers / (1024.0*1024.0));
        if (g_localLightFbo)      gl::DeleteFramebuffers(1, &g_localLightFbo);
        if (g_localLightDepthTex) gl::DeleteTextures(1, &g_localLightDepthTex);
        g_localLightFbo = 0;
        g_localLightDepthTex = 0;
        g_localLightTargetUsable = false;
        g_localVisibleDynamicPrepared = false;
        g_localVisibleDynamicCaptureActive = false;
        g_localVisibleFallbackNext = false;
        g_haveLocalLightVP = false;
        g_localLightSlotCount = 0;
        g_localLightLastCaptureTime = -1.0;
        g_localLightCompletedPolicyEpoch = 0;
        g_localLightCaptureSize = g_pendingLocalSize;
    }
    if (!g_localLightFbo) create_local_light_target();
    if (!g_localLightTargetUsable) return;
    // MASTER SWITCH for local-light shadows.
    //
    // This flag used to filter the SetLightGL census that chose which lights
    // cast. That selection is now NWN's GetShadowLights() priority list, and
    // the filter went with it -- leaving the control wired to NOTHING while the
    // panel still offered it. On Windows it is the only local-shadow switch the
    // user can see, so it now means what its label says: off = no local light
    // casts, and the capture is skipped entirely rather than drawn and hidden.
    if (!g_localLightEmittersCast) {
        g_haveLocalLightVP = false;
        g_localLightSlotCount = 0;
        g_localVisibleDynamicPrepared = false;
        g_localVisibleFallbackNext = false;
        return;
    }
    // The engine priority list is the only source-selection authority, for
    // every tier. The SetLightGL census is retained for the non-shadow sun-lift
    // term only.
    if (!g_haveEngineShadowLight || g_engineShadowLightCount == 0) return;
    // ONLY during a live area render. The gate this replaced
    // (g_localLightSelectedFrame != g_traceAreaFrame) was keyed to the dead
    // GetShadowLights path, but it was also the only thing keeping the capture
    // out of menu/loading/non-area frames -- and replaying buckets there
    // CRASHED the game (backtrace: SceneRender_detour -> this function).
    // Same restriction, expressed against data that is still alive.
    if (!g_traceAreaFrame) return;
    if (!g_shadowFrameContext.valid) return;
    if (g_areaScene && scene != g_areaScene) return;
    // This target is single-buffered, which is why the CLEAR is the thing that
    // must be scheduled carefully -- see the cadence gate below. NWN's
    // GetShadowLights ordering/count remain the sole source authority.

    // CADENCE -- this is what the panel's "Local shadow update" combo drives.
    //
    // Until this gate existed the combo moved g_localCubeUpdateSeconds and
    // nothing on the PRODUCTION path consumed it: the cadence only ever gated
    // the diagnostic cube (g_localCubeLastCaptureTime), so every quality
    // produced the same per-frame capture and the control did nothing.
    //
    // Gating the STAGE is what makes this safe on a single-buffered target.
    // The flicker this code previously feared comes from clearing the layers
    // the receiver samples and publishing nothing until the next dynamic stage
    // refills them. Skipping the stage clears NOTHING: the previously published
    // generation, its matrices and its slot data all stay live, and the
    // receiver keeps sampling a complete map. The map is then up to one
    // interval stale, which is exactly what the setting asks for.
    //
    // Only a published map may be held. Before the first successful capture,
    // and whenever the capture policy changed, fall through and stage
    // immediately -- apply_local_cube_runtime_settings() also resets
    // g_localLightLastCaptureTime to -1.0 so a new cadence takes effect on the
    // next frame instead of after the old deadline.
    if (g_haveLocalLightVP &&
        g_localLightLastCaptureTime >= 0.0 &&
        g_localLightCompletedPolicyEpoch == g_localLightCapturePolicyEpoch &&
        now_seconds() - g_localLightLastCaptureTime < g_localCubeUpdateSeconds)
        return;
    // CANDIDATES -- NWN's own list, in NWN's own order.
    //
    // GetShadowLights can be called more than once per Scene::Render and is
    // already priority ordered by the engine; consume that order verbatim.
    //
    // The Emitter tier briefly selected from the SetLightGL census by camera
    // distance instead, reproducing the 2026-08-10 build. That is gone by the
    // maintainer's decision, and with it the exception to the engine-authority
    // rule -- the census is read-only census again, for the sun lift only. What
    // makes Emitter the 08-10 path is its FILL (all four buckets, nothing
    // cleared between them), not how its lights were chosen.
    //
    // Consequence worth knowing: Emitter can no longer exceed what NWN offers,
    // so its 4th slot only ever fills if the engine hands over four lights. The
    // game's ceiling is three, and its own "Shadow Casting Lights" setting
    // governs how many of those arrive.
    EngineShadowLightEntry cand[kLocalSlotsMax];
    unsigned candCount = 0;
    for (unsigned i = 0; i < g_engineShadowLightCount && candCount < kLocalSlotsMax; ++i)
        cand[candCount++] = g_engineShadowLights[i];

#if NWN_WIN_LOCAL_FASTPATH
    // WINDOWS ONLY -- ORDER BY NWN'S OWN lightpriority.
    //
    // "lightpriority" is a real per-light property (a torch is 3), read from
    // PartLight +0x8c -- see kPartLightPriorityOff for how that was found. It is
    // STABLE, which NWN's list order is not: measured while walking, the same
    // three lights came back in a different order every few frames, evicting and
    // re-admitting each other and restarting their fades. That is the flicker.
    //
    // Sorting by priority replaces a churning order with one the ENGINE defines
    // and that does not change frame to frame. Both halves are still NWN's --
    // eligibility from GetShadowLights(), ranking from lightpriority -- so this
    // substitutes no injector heuristic.
    //
    // STABLE sort: equal priorities keep NWN's relative order, so nothing the
    // engine did not distinguish gets reshuffled. Insertion sort because
    // candCount is a handful.
    //
    // Linux is untouched (PLATFORM RULE, AGENTS.md): it does not show the churn.
    // DISABLED 2026-08-15: the priorities read on Windows are wrong. The log
    // shows "2 0 0" / "0 0 0" and NEVER the 3 a torch should carry, so
    // kPartLightPriorityOff (0x8c + kPartLightDelta) is not the field here.
    //
    // Worse than useless in that state: mostly all-zero, which makes the stable
    // sort a no-op, then an intermittent stray value reorders the roster --
    // churn, the very thing this was added to remove. That is also why it
    // LOOKED like it worked at first: a stable sort on equal keys changes
    // nothing, so "working" and "reading garbage" are indistinguishable.
    //
    // Re-enable only once the [prio-scan] output below identifies the real
    // offset on Windows.
    if (g_localPriorityOffset != 0) {
        for (unsigned i = 1; i < candCount; ++i) {
            const EngineShadowLightEntry key = cand[i];
            unsigned j = i;
            while (j > 0 && cand[j - 1].info.priority < key.info.priority) {
                cand[j] = cand[j - 1]; --j;
            }
            cand[j] = key;
        }
    }
    {   // One line when the ordering changes, so a wrong offset is obvious.
        static int lastTop = -999; static unsigned lastN = 0;
        if (candCount && (cand[0].info.priority != lastTop || candCount != lastN)) {
            lastTop = cand[0].info.priority; lastN = candCount;
            // FIELD SCAN. Dump a window of the first candidate's PartLight as
            // int32s: the torch's priority (3) should be sitting in one of
            // these slots, and its offset is what kPartLightPriorityOff should
            // be on this platform.
            if (candCount && cand[0].key) {
                const unsigned char* pl = (const unsigned char*)cand[0].key;
                fprintf(stderr, "[shadowmap][prio-scan] light=%p:", cand[0].key);
                for (size_t off = 0x70; off <= 0xA8; off += 4) {
                    int32_t v = 0; std::memcpy(&v, pl + off, sizeof(v));
                    if (v >= 0 && v <= 16) fprintf(stderr, " +0x%02zx=%d", off, v);
                }
                fprintf(stderr, "\n");
            }
            fprintf(stderr, "[shadowmap][prio] %u candidate(s), priorities:", candCount);
            for (unsigned i = 0; i < candCount && i < 6; ++i)
                fprintf(stderr, " %d", cand[i].info.priority);
            fprintf(stderr, "\n");
        }
    }
#endif
// SELECTION IS NWN'S ORDER, VERBATIM, ON LINUX.
    //
    // TRIED AND REVERTED 2026-08-15, do not re-attempt without new evidence:
    //
    //  * PINNING THE PLAYER'S CARRIED LIGHT to slot 0. Cannot work as written:
    //    AurSetDynamicProjectionLight hands back a CAurObject*, these keys are
    //    PartLight* -- different objects, never equal. The trace showed
    //    in-list=NO on every frame.
    //
    //  * HYSTERESIS (keep what we held while NWN still offers it). It did stop
    //    the reordering, and it regressed everything else: a newly lit torch
    //    had to wait for a slot to free before it could ever cast, stale lights
    //    survived an area change into interiors, and the roster held entries it
    //    should have dropped. Stability bought by ignoring the engine's current
    //    answer is not stability, it is staleness.
    //
    // The flicker is real and unfixed: NWN reorders its list every few frames,
    // and with a cap of 1..3 that evicts and re-admits lights whose fades then
    // restart. Whatever fixes it has to respect that the engine's list is the
    // live truth -- including when it changes because the player walked
    // through a door.
    if (candCount == 0) return;

    g_localLightSelected = (void*)cand[0].key;
    g_localLightSelectedInfo = cand[0].info;
    const PartLightInfo& info = g_localLightSelectedInfo;
    // AIM DOWN, not at the camera. The first version pointed the cone from the
    // light at the frozen camera eye, reasoning that this guarantees overlap
    // with what is on screen. Measured in game (receiver debug 2): the local
    // term was zero EVERYWHERE, in every area, including with a carried torch
    // as the only light. That aim is wrong precisely when the light is on or
    // near the player -- the axis then runs backwards from the player up to
    // the camera, so the cone covers the air between them while the ground
    // around the light, which is the only place a shadow would read, falls
    // outside the frustum and returns "not shadowed".
    // A point light really needs six faces; of the six, DOWN is the one worth
    // having first, because that is where the floor is.
    float dir[3] = { g_localLightDir[0], g_localLightDir[1], g_localLightDir[2] };
    if (!(std::fabs(dir[0]) + std::fabs(dir[1]) + std::fabs(dir[2]) > 1e-4f)) {
        dir[0] = 0.0f; dir[1] = 0.0f; dir[2] = -1.0f;   // NWN world is Z-up
    }
    v_normalize(dir);
    const Vec3f targetPos = g_shadowFrameContext.valid ? g_shadowFrameContext.eye : g_worldEye;
    const float dxToCam = targetPos.x - info.pos.x;
    const float dyToCam = targetPos.y - info.pos.y;
    const float dzToCam = targetPos.z - info.pos.z;
    const float distToTarget =
        std::sqrt(dxToCam*dxToCam + dyToCam*dyToCam + dzToCam*dzToCam);

    float* vm = mtx_entry(MTX_VIEW);
    float* pm = mtx_entry(MTX_PROJ);
    if (!vm || !pm) return;

    float savedView[16], savedProj[16];
    std::memcpy(savedView, vm, sizeof(savedView));
    std::memcpy(savedProj, pm, sizeof(savedProj));

    // Six faces at 90 degrees. Order matches the receiver's major-axis pick:
    //   0=+X 1=-X 2=+Y 3=-Y 4=+Z 5=-Z
    // One slot per light, aimed from that light at the camera's focus -- which
    // is where the dynamic casters are. (The cube aimed along the six axes and
    // the character landed in none of them.)
    struct LightSlot { float pos[3]; float radius; float fade; };
    // CASTERS ARE CAPPED SEPARATELY FROM THE LIFT.
    //
    // Every casting light costs a depth capture per frame, so the count must be
    // small and bounded -- an area full of emitters put dozens in the set and
    // the frame rate collapsed. But excluding emitters wholesale was the wrong
    // cure: the player's own torch IS an emitter, and it is exactly the light
    // whose shadow you want.
    //
    // Shadow-map ownership is intentionally independent from the cheap
    // light/lift census.  NWN already sorted g_engineShadowLights[] in its own
    // priority order; this setting only says how many entries we consume.
    // NWN exposes at most three shadow-casting lights.  Keep the injector at
    // that native ceiling even though the backing receiver arrays are larger.
    // Every method needs light selection; the tiers differ in how many slots
    // they may claim and WHERE the candidates come from (see the enum in
    // shadow_targets.inc). The Emitter tier reaches 4 because it selects from
    // the lamp census rather than NWN's 3-entry shadow set.
    const int slotBudget = std::max(1, std::min(local_source_lights(),
                                                kLocalLightFaces));
    LightSlot slots[kLocalLightFaces] = {};
    unsigned slotCount = 0;
    {
        // ONE TABLE, BOTH DIRECTIONS.
        //
        // Fading a light IN was easy because it stays in NWN's set: it has a
        // slot, its layer is captured every frame, and the level just scales
        // it. Fading OUT was hard only because the light LEFT -- its slot
        // vanished and its layer was reused, which is what the copy-aside
        // retirement existed to work around, and what kept breaking.
        //
        // So do not let it leave. A dropped light keeps its entry with a
        // decaying level and is still published as an ordinary slot until it
        // reaches zero. Its position and radius are the last ones NWN gave us,
        // and the capture treats it exactly like any other slot -- staged,
        // filled and published in band, so there is no gap to survive.
        //
        // Cost is one extra capture for at most the fade duration.
        const double now = now_seconds();
        const double dt = (g_localJoinLast > 0.0) ? (now - g_localJoinLast) : 0.0;
        g_localJoinLast = now;
        const float step = (dt > 0.0) ? (float)(dt / kLocalRetireFadeSeconds) : 0.0f;

        for (auto& j : g_localJoin) j.live = false;

        // 1. Everything this tier currently selects, in its own order (NWN's
        //    priority for Legacy/High, camera distance for Emitter).
        unsigned live = 0;
        for (unsigned i = 0; i < candCount && live < (unsigned)slotBudget; ++i) {
            const EngineShadowLightEntry& e = cand[i];
            if (e.info.radius <= 0.0f) continue;
            LocalJoinFade* slot = nullptr;
            for (auto& j : g_localJoin)
                if (j.key == e.key) { slot = &j; break; }
            if (!slot)
                for (auto& j : g_localJoin)
                    if (!j.key) { slot = &j; slot->key = e.key; slot->level = 0.0f; break; }
            if (!slot) continue;                 // table full; skip rather than evict
            slot->live = true;
            slot->pos[0] = e.info.pos.x;
            slot->pos[1] = e.info.pos.y;
            slot->pos[2] = e.info.pos.z;
            slot->radius = e.info.radius;
            slot->engineFade = e.info.fade;
            slot->level = std::min(1.0f, slot->level + step);
            ++live;
        }

        // 2. Anything NWN dropped, still fading. Same slot machinery.
        for (auto& j : g_localJoin) {
            if (!j.key || j.live) continue;
            j.level -= step;
            if (j.level <= 0.0f) { j = LocalJoinFade{}; continue; }
        }

        // 3. Publish live first (NWN's order is authoritative), then fading.
        //
        // FADING SLOTS ARE CAPPED AT ONE, and this is a cost bound, not a
        // nicety. Every published slot is a full capture of the visible dynamic
        // geometry. Standing still the set is stable and there are none; MOVING
        // churns NWN's priority list, so without a cap several lights can be
        // mid-fade at once and the capture count silently climbs above the
        // budget the user chose -- which is exactly the "fine standing still,
        // tanks when I move" behaviour.
        //
        // One fading slot bounds the worst case at budget + 1. The one with the
        // highest level is kept: it is the most recently dropped, so it is the
        // shadow whose disappearance would be most obvious.
        unsigned fadingPublished = 0;
        const LocalJoinFade* bestFading = nullptr;
        for (const auto& j : g_localJoin) {
            if (!j.key || j.live || j.radius <= 0.0f) continue;
            if (!bestFading || j.level > bestFading->level) bestFading = &j;
        }
        for (int pass = 0; pass < 2; ++pass) {
            for (auto& j : g_localJoin) {
                if (!j.key || j.radius <= 0.0f) continue;
                if ((pass == 0) != j.live) continue;
                if (pass == 1) {
                    if (&j != bestFading || fadingPublished) continue;
                    ++fadingPublished;
                }
                if (slotCount >= (unsigned)kLocalLightFaces) break;
                slots[slotCount].pos[0] = j.pos[0];
                slots[slotCount].pos[1] = j.pos[1];
                slots[slotCount].pos[2] = j.pos[2];
                slots[slotCount].radius = j.radius;
                // NWN's own per-light fade times ours.
                slots[slotCount].fade   = j.engineFade * j.level;
                g_localJoinLevel[slotCount] = j.level;
                ++slotCount;
            }
        }
    }
    // The copy-aside "retirement" that used to live here is gone. It preserved
    // a dropped light's depth in a spare layer and published it as an extra
    // slot, which meant the fade had to survive staging, g_haveLocalLightVP
    // going false, and the light rejoining -- three separate breaks, each fixed
    // and each followed by another. Keeping the light in the slot list instead
    // (see the table above) makes the whole class of problem disappear.
    if (!slotCount) {
        g_haveLocalLightVP = false;
        g_localLightSlotCount = 0;
        return;
    }
    // Say what the budget ACTUALLY did: how many lights the census holds, how
    // many were emitters, the cap, and how many slots ended up capturing. If
    // the cap or the emitter filter is not doing what the panel implies, this
    // line is where it shows.
    {
        static unsigned lastSig = 0xFFFFFFFFu;
        const unsigned sig = (slotCount << 24) ^
            (g_engineShadowLightCount << 8) ^ static_cast<unsigned>(slotBudget);
        if (sig != lastSig) {
            lastSig = sig;
            fprintf(stderr, "[shadowmap][local-light] NWN priority: candidates=%u cap=%d "
                            "-> capturing=%u slots (SetLightGL is lift-only)\n",
                    g_engineShadowLightCount, slotBudget, slotCount);
        }
    }
    build_lookat(info.pos, dir, g_localLightView);
    // A very wide FOV is deliberate: the frustum apex sits AT the light, so a
    // narrow cone covers almost nothing near it. At height h above the floor a
    // FOV of f covers a ground disc of radius h*tan(f/2).
    //
    // THIS CONE, NOT THE LIGHT'S RADIUS, IS USUALLY WHAT LIMITS HOW FAR A LOCAL
    // SHADOW REACHES. The receiver also stops at the light's radius, but the map
    // simply holds no data outside the cone, and a sample outside it reads as
    // lit. At 140 degrees a torch carried ~1.5 units up covers barely 4 units of
    // floor, which is the "radius is tiny" report from 2026-08-15.
    //
    // 170 IS THE DEFAULT FOR ONE-FACE MODE, and it is affordable only because
    // the sun-path fill replaced the old ones. Widening the cone costs frame
    // time by putting more geometry inside the light's frustum, and under the
    // standalone replay that was expensive enough that 160 had to be reverted
    // on 2026-08-15. Riding the engine's own bucket pass made it cheap, and the
    // maintainer confirmed 170 runs well on BOTH platforms.
    //
    //   140 -> 2.75x height     160 -> 5.67x
    //   170 -> 11.4x (default)  175 -> 22.9x (hard limit)
    //
    // WHAT IT COSTS INSTEAD IS SHARPNESS, and that is what four-face mode is
    // for. A perspective cone spreads texels by ANGLE, so ground footprint per
    // texel grows about 1/cos^3 off-axis: at 170 the rim is ~1000x coarser than
    // straight down. Four faces at 110 keep every sample within ~55 degrees of
    // a face axis (~5x) while covering more ground -- see local_face_fov_deg.
    // NWN_SHADOWMAP_LOCAL_LIGHT_FOV overrides the one-face value only.
    //
    // The receiver's texel-size estimate follows this value through the
    // nwnLocalTanHalfFov uniform; it used to be hardcoded for 140.
    // Near/far bound the LIGHT's radius, not the camera's -- past its radius
    // this light illuminates nothing, so it can shadow nothing.
    if (g_localLightRadiusOwner != g_localLightSelected) {
        g_localLightRadiusOwner = g_localLightSelected;
        g_localLightRadiusStable = 0.0f;
    }
    g_localLightRadiusStable = std::max(info.radius, g_localLightRadiusStable * 0.97f);
    const float stableRadius = g_localLightRadiusStable;

    // ONE perspective per slot, aimed from that light at the camera's focus.
    // Wide, because the apex sits at the light and the casters are close to it.
    const float fovY = g_localLightFovDeg * 3.14159265f / 180.0f;
    // ONE SLOT PER (LIGHT, FACE). A face shares its light's position, radius
    // and fade and differs only in aim, so the receiver's per-slot loop needs
    // no concept of faces: a fragment simply projects outside the faces that do
    // not contain it and they contribute nothing.
    const int faces = local_source_faces();
    const float faceFovY = local_face_fov_deg(faces) * 3.14159265f / 180.0f;
    unsigned slot = 0;
    for (unsigned i = 0; i < slotCount && slot < (unsigned)kLocalLightFaces; ++i) {
        const float r  = slots[i].radius;
        const float zni = std::max(0.05f, std::min(0.5f, r * 0.01f));
        // The lift extends the far plane too, or raising the light pushes the
        // floor out of the frustum it is being rendered into.
        const float zfi = std::max(kLocalLightMinRange,
                                   r * 2.0f + std::max(0.0f, g_localLightHeight));
        for (int f = 0; f < faces && slot < (unsigned)kLocalLightFaces; ++f, ++slot) {
            float projI[16], viewI[16];
            build_perspective(faceFovY, 1.0f, zni, zfi, projI);
            float aim[3];
            local_face_dir(f, faces, aim);
            v_normalize(aim);
            // Cast from a VIRTUAL position raised along world up (NWN is Z-up).
            // See g_localLightHeight: the light itself does not move, only the
            // apex of the shadow projection, which is what shortens the shadow.
            const Vec3f lp = { slots[i].pos[0], slots[i].pos[1],
                               slots[i].pos[2] + g_localLightHeight, 1.0f };
            build_lookat(lp, aim, viewI);
            mat4_mul(projI, viewI, g_localLightFaceVP[slot]);
            std::memcpy(g_localVisibleView[slot], viewI, sizeof(viewI));
            std::memcpy(g_localVisibleProj[slot], projI, sizeof(projI));
            std::memcpy(g_localVisibleVP[slot], g_localLightFaceVP[slot],
                        sizeof(g_localLightFaceVP[slot]));
            g_localLightSlotPos[slot][0] = slots[i].pos[0];
            g_localLightSlotPos[slot][1] = slots[i].pos[1];
            g_localLightSlotPos[slot][2] = slots[i].pos[2];
            g_localLightSlotRadius[slot] = r;
            g_localLightSlotFade[slot] = slots[i].fade;
            g_localVisibleSlotFade[slot] = slots[i].fade;
            std::memcpy(g_localVisibleSlotPos[slot], g_localLightSlotPos[slot],
                        sizeof(g_localLightSlotPos[slot]));
            g_localVisibleSlotRadius[slot] = r;
        }
    }
    // From here on "slot" is what the receiver and the replay both iterate.
    slotCount = slot;
    const float zn = std::max(0.05f, stableRadius * 0.01f);
    const float zf = std::max(zn + 0.5f, stableRadius * 2.0f);
    build_perspective(fovY, 1.0f, zn, zf, g_localLightProj);


    // ONE FILL, and it is the sun-path: capture_local_light_shadow() computes
    // and publishes the slots here, and the layers are filled during the
    // engine's OWN bucket pass by replay_bucket_into_local_light_layers().
    //
    // The staging dance for the visible-draw duplication and the standalone
    // bucket replay both lived here and are gone (2026-08-15). Nothing draws
    // in this function any more.
    g_localInlineSlotCount = slotCount;
    g_localInlinePolicyEpoch = g_localLightCapturePolicyEpoch;
    g_localInlinePrepared = true;

    // Publish what the receiver needs, tagged with this frame's serial. The
    // world position/radius drive a simple distance falloff so the shadow
    // fades out at the edge of the light's reach instead of ending abruptly
    // at the depth map's frustum boundary.
    mat4_mul(g_localLightProj, g_localLightView, g_localLightVP);
    g_localLightWorldPos[0] = info.pos.x;
    g_localLightWorldPos[1] = info.pos.y;
    g_localLightWorldPos[2] = info.pos.z;
    // The attenuation uses the SAME held radius as the frustum. Using the
    // flickering value here made the shadow fade in and out with the flame.
    g_localLightWorldRadius = stableRadius;
    g_haveLocalLightVP = true;
    g_localLightVPFrame = g_shadowFrameContext.serial;
    // PUBLISH THE SLOT COUNT. Only the visible-capture path used to set this,
    // because the bucket replay was a one-shot fallback that ran after a
    // successful publish had already set it. As a standing mode ("0 - legacy")
    // nothing sets it, the receiver uploads nwnLocalSlots = 0, and the shader's
    // per-slot loop never executes -- the map is filled correctly and then
    // never sampled.
    g_localLightSlotCount = slotCount;
    g_localLightCompletedPolicyEpoch = g_localLightCapturePolicyEpoch;
    g_localLightLastCaptureTime = now_seconds();

    // SUN-PATH is finished here: everything below is the standalone replay it
    // exists to avoid. Running both would draw every caster twice.
    ++g_localLightCaptureFrame;
    static double nextLocalLightStatus = 0.0;
    const double tnow = now_seconds();
    if (g_statusEvery > 0.0 && tnow >= nextLocalLightStatus) {
        nextLocalLightStatus = tnow + g_statusEvery;
        fprintf(stderr, "[shadowmap][local-light] capture frame=%u light=%p pos=(%.2f,%.2f,%.2f) "
                        "r=%.2f rgb=(%.2f,%.2f,%.2f) dir=(%.2f,%.2f,%.2f) dist=%.2f buckets-ok=%u/4\n",
                g_localLightCaptureFrame, g_localLightSelected,
                info.pos.x, info.pos.y, info.pos.z, info.radius,
                info.color[0], info.color[1], info.color[2],
                dir[0], dir[1], dir[2], distToTarget, g_localLightCaptureDraws);
    }
}

// Same normalisation approach as dump_depth() (raw depth clusters near 1.0,
// so a straight dump looks blank white) but reads the private local-light
// target instead of the shared g_depthTex/g_size globals.
static void dump_local_light_depth(const char* path) {
    if (!g_localLightTargetUsable) {
        fprintf(stderr, "[shadowmap][local-light] dump requested but target not usable\n");
        return;
    }
    std::vector<uint32_t> px((size_t)g_localLightCaptureSize * (size_t)g_localLightCaptureSize);
    GLint prevTex = 0;
    gl::GetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    gl::BindTexture(GL_TEXTURE_2D, g_localLightDepthTex);
    gl::GetTexImage(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, px.data());
    gl::BindTexture(GL_TEXTURE_2D, (GLuint)prevTex);

    uint32_t lo = 0xFFFFFFFFu, hi = 0u;
    size_t written = 0;
    for (uint32_t v : px) {
        if (v < lo) lo = v;
        if (v > hi) hi = v;
        if (v != 0xFFFFFFFFu) ++written;
    }
    FILE* f = open_dump_file(path);
    if (!f) { fprintf(stderr, "[shadowmap][local-light] dump: cannot open %s\n", path); return; }
    fprintf(f, "P5\n%d %d\n255\n", g_localLightCaptureSize, g_localLightCaptureSize);
    const double span = (hi > lo) ? double(hi) - double(lo) : 1.0;
    for (uint32_t v : px)
        fputc((int)(255.0 * (double(v) - double(lo)) / span), f);
    fclose(f);
    fprintf(stderr, "[shadowmap][local-light] dumped %s  range %u..%u  texels-with-geometry %zu/%zu%s\n",
            path, lo, hi, written, px.size(),
            (lo == hi) ? "  (uniform - nothing rendered)" : "");
}
