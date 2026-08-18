// ===========================================================================
//  Depth readback -- used only for the final PGM, not for detection.
// ===========================================================================
static std::vector<uint32_t> g_readback;
struct DepthStats { uint32_t lo, hi; size_t written; };

static DepthStats read_depth() {
    g_readback.resize((size_t)g_size * g_size);
    GLint prevTex = 0;
    gl::GetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    gl::BindTexture(GL_TEXTURE_2D, g_depthTex);
    gl::GetTexImage(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT,
                    g_readback.data());
    gl::BindTexture(GL_TEXTURE_2D, (GLuint)prevTex);

    DepthStats s{0xFFFFFFFFu, 0u, 0};
    for (uint32_t v : g_readback) {
        if (v < s.lo) s.lo = v;
        if (v > s.hi) s.hi = v;
        if (v != 0xFFFFFFFFu) ++s.written;
    }
    return s;
}

static bool update_depth_mirror() {
    // Last known-good diagnostic bridge: mirror once a second. Its matrix
    // advances on the same upload, keeping depth and receiver coherent.
    if (!shadow_getenv("NWN_SHADOWMAP_INJECT_TEST") || !g_depthMirrorTex) return false;
    static double nextMirrorUpload = 0.0;
    const double now = now_seconds();
    if (now < nextMirrorUpload) return false;
    nextMirrorUpload = now + 1.0;
    DepthStats mirrorStats = read_depth();
    static int mirrorReports = 0;
    if ((++mirrorReports % 120) == 1)
        fprintf(stderr, "[shadowmap] mirror frame: depth=%u..%u written=%zu/%zu\n",
                mirrorStats.lo, mirrorStats.hi, mirrorStats.written,
                g_readback.size());
    GLint prevTex = 0;
    gl::GetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    gl::BindTexture(GL_TEXTURE_2D, g_depthMirrorTex);
    gl::TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, g_size, g_size,
                      GL_RED_INTEGER, GL_UNSIGNED_INT, g_readback.data());
    gl::BindTexture(GL_TEXTURE_2D, (GLuint)prevTex);
    return true;
}

// Every PGM dump goes through this so diagnostic output lands in a chosen
// directory instead of the process CWD. That matters because a launcher may
// legitimately need to `cd` elsewhere before exec (nwmain-linux infers its
// base data directory from CWD and refuses to start otherwise), and without
// this the dumps from one working copy silently overwrite another's.
// Unset = plain relative name = previous behaviour, so parent launchers are
// unaffected.
static FILE* open_dump_file(const char* name) {
    const char* dir = shadow_getenv("NWN_SHADOWMAP_OUT_DIR");
    if (!dir || !*dir) return fopen(name, "wb");
    char path[1024];
    std::snprintf(path, sizeof(path), "%s/%s", dir, name);
    return fopen(path, "wb");
}

// ---------------------------------------------------------------------------
//  Panel settings persistence
// ---------------------------------------------------------------------------
// Everything the ImGui panel edits lives in a plain key=value file beside the
// log, so the choices survive a restart. Without this every session started
// from the launcher's defaults and the maintainer re-dialled the same sliders.
//
// PRECEDENCE: the file is loaded AFTER the environment, so the panel wins. That
// is deliberate -- the launchers set almost every variable, so env-wins would
// make the file useless. NWN_SHADOWMAP_NO_SETTINGS=1 skips loading it entirely,
// which is the escape hatch when a saved value makes the game unusable.
//
// The receiver debug mode is NOT saved: it is a diagnostic, and restarting into
// solid magenta with no memory of why would be its own bug report.
struct SettingRef { const char* key; float* f; int* i; bool* b; };

// Bump when a DEFAULT changes, so an .ini written by an older build cannot
// pin the retuned values. Without this, anyone who ran a previous build keeps
// e.g. local_normal_bias=2.0 forever and never sees the tuned 0.5 -- the file
// wins over the environment by design, and it would win over a better default
// too. On an older file the keys listed in settings_retuned() are ignored and
// the file is rewritten at the new version; everything else the user set is
// preserved.
static constexpr int kSettingsVersion = 5;

// Keys whose defaults changed in a local-light tuning pass.  Version 5 also
// makes these controls live on the cube receiver; reset the old 1-unit
// separation default rather than letting it detach every point-light shadow.
static bool settings_retuned(const char* key) {
    static const char* kV2[] = {
        "local_min_sep", "local_normal_bias", "local_soft",
    };
    for (const char* k : kV2) if (std::strcmp(k, key) == 0) return true;
    return false;
}

static bool  g_settingsLoaded = false;
static bool  g_settingsResetRequested = false;   // panel's "Restore defaults"
static double g_settingsSaveAt = 0.0;   // debounce; 0 = nothing pending

static unsigned settings_table(SettingRef* out, unsigned max) {
    unsigned n = 0;
    auto addf = [&](const char* k, float* v) { if (n<max) out[n++] = {k,v,nullptr,nullptr}; };
    auto addi = [&](const char* k, int*   v) { if (n<max) out[n++] = {k,nullptr,v,nullptr}; };
    auto addb = [&](const char* k, bool*  v) { if (n<max) out[n++] = {k,nullptr,nullptr,v}; };
    // --- reachable from the panel on BOTH platforms ------------------------
    addi("cascades",           &g_cascadeActiveCount);
    addi("dynamic_cascades",   &g_cascadeDynamicLayers);   // merged into "Cascades"
    addi("cascade_distance",   &g_staticNearCascades);
    addi("cascade_map",        &g_size);
    addi("world_map",          &g_staticWorldSize);
    addf("world_extent",       &g_staticWorldExtent);
    addb("local_emit_cast",    &g_localLightEmittersCast); // "Lights casting shadows"
    addi("local_map",          &g_pendingLocalSize);       // "Light casted shadow"
    // "Light supported" removed: the count follows the engine. Persisting a
    // duplicate would let a saved value fight it.
    //addi("max_lamps",        &g_lampUploadMax);          // "Light supported"
    // "Local shadow update" is gone: the sun-path fill refills every frame
    // from the engine's bucket pass, so a cadence could only throttle slot
    // re-computation, which is trivial.
    //addi("local_cube_quality", &g_localCubeQuality);
    addi("local_cube_sources", &g_localCubeSourceBudget);
    addf("selfillum_guard",    &g_liftThreshold);          // "Bright surfaces keep light"
    addb("lamp_lift",          &g_lampLiftEnabled);        // "Lights lift sun shadow"

#if !NWN_SHIP
    // --- DEV BUILD ONLY: everything a SHIPPING panel HIDES ---------------------
    // A setting that no control on this platform can change must not be
    // persisted here, because a saved value can then only ever FIGHT the
    // shipped default with no way to undo it. That is not a theory: an .ini
    // carrying local_enabled=0 silently disabled light-cast shadows on Windows
    // and cost a whole test round to find (see win/README.md).
    //
    // The rule was written down then but applied to that ONE key. Everything
    // below is equally unreachable on Windows -- the Sun shadows section, the
    // Local light section and "Refit after" are all #ifndef _WIN32 in
    // nwn_overlay_imgui.cpp -- so the same reasoning covers all of it. On
    // Windows these are owned by settings_reset_defaults() and kWinDefaultEnv.
    //
    // cache_move is the one with teeth: it is the distance the camera may move
    // before the cascades REFIT, and a refit re-renders every static caster
    // into every near layer. A small saved value there is a permanent,
    // invisible framerate cost on the shipping build.
    // The three diagnostic A/B switches. Their widgets are #ifndef _WIN32 in
    // nwn_overlay_imgui.cpp, so by the rule above they must not be persisted
    // on Windows either: each one REMOVES shadows, and a saved value there
    // could disable the whole effect with no control able to turn it back on.
    addb("dynamic_casters",    &g_dynamicCastersEnabled);  // "Moving casters"
    addb("static_casters",     &g_staticCastersEnabled);   // "Fixed casters"
    addb("receiver_enabled",   &g_receiverEnabled);        // "Sun shadows" master
    addf("csm_strength",       &g_cascadeCompositeStrength);
    addf("csm_bias",           &g_cascadeReceiverBias);
    addf("csm_blend",          &g_cascadeBlendWidth);
    addf("csm_pcf",            &g_cascadePcfRadius);
    addb("csm_composite",      &g_cascadeCompositeShadows);
    addf("cache_move",         &g_cascadeCacheMove);
    // Hidden on Windows (see nwn_overlay_imgui.cpp), so by the rule above it
    // is not persisted there either -- a saved hide_engine_shadows=0 would
    // put NWN's shadows back under everything with no control able to undo it.
    addb("hide_engine_shadows",&g_hideEngineShadows);
    addb("local_enabled",      &g_localLightReceiver);
    addf("local_strength",     &g_localLightStrength);
    addf("local_bias",         &g_localLightBias);
    addf("local_lift",         &g_localLightLift);
    addf("local_edge_fade",    &g_localLightEdgeFade);
    addf("local_falloff",      &g_localLightFalloff);
    addf("local_fov",          &g_localLightFovDeg);
    addf("local_height",       &g_localLightHeight);
    addf("local_soft",         &g_localLightSoft);
    addf("local_slope",        &g_localLightSlopeBias);
    addf("local_normal_bias",  &g_localLightNormalBias);
    addb("local_alpha_cast",   &g_localLightAlphaCasters);
    addf("local_min_sep",      &g_localLightMinSep);
    addb("local_dyn_only",     &g_localLightDynamicOnly);
#endif
    return n;
}

// The built-in defaults, in one place. These are the maintainer's measured
// preferences, not neutral values: 3+3 cascades looked BETTER than 4+4 as well
// as costing less, extent 128 kept small objects' shadows that 256 lost, and
// "Lights for lift" 32 matches NWN's own default video setting.
static void settings_reset_defaults() {
    g_cascadeCompositeStrength = 0.42f;
    g_cascadeReceiverBias      = 0.0025f;
    g_cascadeBlendWidth        = 0.75f;
    g_cascadePcfRadius         = 0.75f;
    g_cascadeCompositeShadows  = true;
    g_cascadeActiveCount       = 3;
    g_cascadeDynamicLayers     = 3;
    g_staticNearCascades       = 4;      // "Ultra"
    g_cascadeCacheMove         = 2.0f;
    g_dynamicCastersEnabled    = true;   // moving things cast by default
    g_staticCastersEnabled     = true;
    g_hideEngineShadows        = true;   // confirmed in game 2026-08-12
    g_receiverEnabled          = true;
#if NWN_SHIP
    g_localLightReceiver       = true;   // shipped on; the panel hides it
#else
    g_localLightReceiver       = false;  // opt-in on the development build
#endif
    g_localLightStrength       = 0.50f;
    g_localLightBias           = 0.0f;
    g_localLightLift           = 1.0f;
    g_localLightEdgeFade       = 0.0f;
    g_localLightFalloff        = 0.50f;
    g_localLightFovDeg         = 170.0f;
    g_localLightHeight         = 0.0f;
    g_localLightSoft           = 0.51f;
    g_localLightSlopeBias      = 1.8f;
    g_localLightNormalBias     = 0.0f;
    g_localLightAlphaCasters   = false;
    g_localLightMinSep         = 0.30f;
    g_localLightEmittersCast   = true;
    g_pendingLocalSize         = 256;   // Low
    g_localLightDynamicOnly    = true;
    g_localCubeQuality         = kLocalCubeQualityMax;   // every frame
#if NWN_SHIP
    g_localCubeSourceBudget    = 3;   // shipping: always three; NWN gates it
#else
    g_localCubeSourceBudget    = 1;   // dev: start cheap
#endif
    g_lampLiftEnabled          = true;
    g_liftThreshold            = 0.85f;  // only genuinely glowing surfaces
    g_localCubeUpdateSeconds   = kLocalCubeCadenceSeconds[g_localCubeQuality];
#if NWN_SHIP
    // Match the shipping default (kWinDefaultEnv MAX_LAMPS=8): on Windows each
    // of these also casts a shadow, so Restore defaults must not quietly hand
    // someone 32 per-light captures a frame.
    g_lampUploadMax            = 0;   // follow the engine
#else
    g_lampUploadMax            = 0;   // follow the engine
#endif
    // Resolutions own GL textures, so they go through the same staged Apply the
    // panel uses rather than being written straight to the live values.
    g_pendingCascadeSize  = 2048;
    g_pendingWorldSize    = 8192;
    g_pendingWorldExtent  = 128.0f;
    g_resolutionApplyRequested = true;
}

static bool settings_path(char* buf, size_t n) {
    const char* dir = shadow_getenv("NWN_SHADOWMAP_OUT_DIR");
    if (dir && *dir) std::snprintf(buf, n, "%s/nwn_shadowmap_settings.ini", dir);
    else             std::snprintf(buf, n, "nwn_shadowmap_settings.ini");
    return true;
}

static void save_settings();   // defined below; load rewrites a stale file

// local_cube_quality is the persisted/UI-facing value; the capture scheduler
// consumes g_localCubeUpdateSeconds.  Keep that derived runtime state in one
// place and apply it immediately after loading settings, rather than waiting
// for the first Scene::Render tick.  The old split initialization could leave
// the panel saying "High" while the scheduler still used an environment/default
// cadence until a live combo-box edit happened.
static const char* local_cube_quality_name(int quality) {
    static const char* names[] = {"Low", "Medium", "High", "Extreme", "Ultra"};
    static_assert(sizeof(names)/sizeof(names[0]) == kLocalCubeQualityCount,
                  "quality names must match the cadence table");
    quality = std::max(0, std::min(kLocalCubeQualityMax, quality));
    return names[quality];
}

static void apply_local_cube_runtime_settings(bool startup, const char* source) {
    static int appliedQuality = -1;
    static int appliedSources = -1;
    g_localCubeQuality = std::max(0, std::min(kLocalCubeQualityMax, g_localCubeQuality));
    g_localCubeSourceBudget =
        std::max(1, std::min(kLocalSourceIndexMax, g_localCubeSourceBudget));
#if NWN_SHIP
    // SHIPPING PINS THIS TO THREE, overriding anything persisted.
    //
    // The "Local shadow sources" control is #if !NWN_SHIP, so a shipping build
    // cannot change it -- and a saved value of 1 from an earlier dev session
    // then sticks forever with no way out. Observed on Windows 2026-08-15: the
    // log read "candidates=3 cap=1 -> capturing=1", two torches in view and
    // only one casting, which looked like a selection bug and was a stale ini.
    //
    // NWN's own "Shadow Casting Lights" video setting is what should gate this,
    // and it already does -- GetShadowLights() simply offers fewer candidates.
    // Taking all it offers is the correct behaviour when we have no control.
    g_localCubeSourceBudget = 3;
#endif
    // The "Off" rungs were removed from Cascades and Cascade distance. A config
    // written before that can still hold them, and the panel would then show
    // "Low" while the stored value stayed off -- a state the user cannot see or
    // escape without touching the control. Clamp them out on load.
    if (g_staticNearCascades < 1) g_staticNearCascades = 1;
    if (g_cascadeDynamicLayers < 1) g_cascadeDynamicLayers = 1;
    if (g_cascadeActiveCount < 2) g_cascadeActiveCount = 2;

    const double cadence = kLocalCubeCadenceSeconds[g_localCubeQuality];
    const bool cadenceChanged = std::fabs(g_localCubeUpdateSeconds - cadence) > 1e-9;
    const bool capturePolicyChanged = startup ||
        appliedQuality != g_localCubeQuality ||
        appliedSources != g_localCubeSourceBudget ||
        false;
    g_localCubeUpdateSeconds = cadence;

    // Current local depth storage is single-buffered. Quality remains a saved
    // UI preference for the future ping-pong implementation; capture itself
    // stays per-frame so a cleared staging map cannot become visible as flicker.
    // Source/range changes still advance the epoch for diagnostics.
    if (capturePolicyChanged) {
        ++g_localLightCapturePolicyEpoch;
        g_localLightLastCaptureTime = -1.0;
        fprintf(stderr,
                "[shadowmap][local-light] policy: %d light(s) update=%s (%.0f ms)%s%s\n",
                local_source_lights(),
                local_cube_quality_name(g_localCubeQuality),
                cadence * 1000.0,
                source && *source ? " from " : "",
                source && *source ? source : "");
    }
    appliedQuality = g_localCubeQuality;
    appliedSources = g_localCubeSourceBudget;

    if (startup || cadenceChanged) {
        fprintf(stderr,
                "[shadowmap][settings] local shadow update %s (%.0f ms) applied%s%s\n",
                local_cube_quality_name(g_localCubeQuality), cadence * 1000.0,
                source && *source ? " from " : "",
                source && *source ? source : "");
    }
}

// local_map is deliberately a staged panel value during play: changing the
// combo does not destroy/recreate a live GL target until the user presses
// Apply.  Startup is different.  load_settings() runs before the first area
// render and therefore before the local-light target exists, so the saved
// staged value must become the live allocation size immediately.  Previously
// only g_pendingLocalSize was loaded; the panel could say High/Ultra while the
// first target was still allocated at the launcher's/default Low size (256).
static int sanitize_local_shadow_map_size(int requested) {
    static const int sizes[5] = {256, 512, 1024, 2048, 4096};
    int best = sizes[0];
    int bestDelta = requested > best ? requested - best : best - requested;
    for (int size : sizes) {
        const int delta = requested > size ? requested - size : size - requested;
        if (delta < bestDelta) {
            best = size;
            bestDelta = delta;
        }
    }
    return best;
}

static void apply_local_map_runtime_settings(bool startup, const char* source) {
    g_pendingLocalSize = sanitize_local_shadow_map_size(g_pendingLocalSize);

    const int oldLiveSize = g_localLightCaptureSize;
    if (startup && !g_localLightFbo && !g_localLightDepthTex) {
        // No GL resource exists yet: make capture #1 allocate the saved size.
        g_localLightCaptureSize = g_pendingLocalSize;
        g_localSizeCommit = false;
    } else if (g_pendingLocalSize != g_localLightCaptureSize) {
        // Defensive late-load path.  The normal in-game resolution workflow
        // still goes through Performance -> Apply and apply_resolution_change().
        g_localSizeCommit = true;
    }

    if (startup || oldLiveSize != g_localLightCaptureSize) {
        fprintf(stderr,
                "[shadowmap][settings] local shadow map %d -> %d applied%s%s\n",
                oldLiveSize, g_pendingLocalSize,
                source && *source ? " from " : "",
                source && *source ? source : "");
    }
}

static void load_settings() {
    g_settingsLoaded = true;
    if (shadow_getenv("NWN_SHADOWMAP_NO_SETTINGS")) {
        fprintf(stderr, "[shadowmap][settings] NWN_SHADOWMAP_NO_SETTINGS set; using defaults\n");
        apply_local_cube_runtime_settings(true, "environment/defaults");
        apply_local_map_runtime_settings(true, "environment/defaults");
        return;
    }
    char path[1024];
    settings_path(path, sizeof(path));
    FILE* f = fopen(path, "rb");
    if (!f) {
        apply_local_cube_runtime_settings(true, "environment/defaults (no settings file)");
        apply_local_map_runtime_settings(true, "environment/defaults (no settings file)");
        return;
    }
    SettingRef table[32];
    const unsigned count = settings_table(table, 32);
    // Two passes: the version is needed before deciding what to honour.
    int fileVersion = 1;
    {
        char probe[256];
        while (fgets(probe, sizeof(probe), f)) {
            int v = 0;
            if (std::sscanf(probe, "version=%d", &v) == 1) { fileVersion = v; break; }
        }
        std::rewind(f);
    }
    const bool stale = (fileVersion < kSettingsVersion);

    char line[256];
    unsigned applied = 0, skipped = 0;
    while (fgets(line, sizeof(line), f)) {
        char key[64] = {}; double value = 0.0;
        if (std::sscanf(line, "%63[^=]=%lf", key, &value) != 2) continue;
        if (stale && settings_retuned(key)) { ++skipped; continue; }
        for (unsigned i = 0; i < count; ++i) {
            if (std::strcmp(table[i].key, key) != 0) continue;
            if (!std::isfinite(value)) break;
            if (table[i].f) *table[i].f = (float)value;
            else if (table[i].i) *table[i].i = (int)value;
            else if (table[i].b) *table[i].b = (value != 0.0);
            ++applied;
            break;
        }
    }
    fclose(f);
    apply_local_cube_runtime_settings(true, "saved settings");
    apply_local_map_runtime_settings(true, "saved settings");
    // The staged resolution boxes must agree with what was just loaded, or the
    // panel shows "pending" against values that are already live.
    g_pendingCascadeSize = g_size;
    g_pendingWorldSize   = g_staticWorldSize;
    g_pendingWorldExtent = g_staticWorldExtent;
    fprintf(stderr, "[shadowmap][settings] loaded %u value(s) from %s (file v%d)\n",
            applied, path, fileVersion);
    if (stale) {
        // Write the new defaults straight back so this only happens once.
        fprintf(stderr, "[shadowmap][settings] older settings file: %u retuned value(s) "
                        "ignored, rewriting at v%d\n", skipped, kSettingsVersion);
        save_settings();
    }
}

static void save_settings() {
    char path[1024];
    settings_path(path, sizeof(path));
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "# NWN shadow map panel settings. Delete this file to reset,\n"
               "# or run with NWN_SHADOWMAP_NO_SETTINGS=1 to ignore it.\n");
    fprintf(f, "version=%d\n", kSettingsVersion);
    SettingRef table[32];
    const unsigned count = settings_table(table, 32);
    for (unsigned i = 0; i < count; ++i) {
        if (table[i].f)      fprintf(f, "%s=%.6g\n", table[i].key, (double)*table[i].f);
        else if (table[i].i) fprintf(f, "%s=%d\n",   table[i].key, *table[i].i);
        else if (table[i].b) fprintf(f, "%s=%d\n",   table[i].key, *table[i].b ? 1 : 0);
    }
    fclose(f);
}

// Called every frame: notices any change the panel made and writes once the
// edits stop. Comparing values rather than hooking each widget keeps the panel
// code free of persistence concerns -- and a slider drag becomes ONE write
// instead of one per frame.
static void settings_tick() {
    if (!g_settingsLoaded) return;
    if (g_settingsResetRequested) {
        g_settingsResetRequested = false;
        settings_reset_defaults();
        save_settings();      // write immediately: a reset must survive a crash
        fprintf(stderr, "[shadowmap][settings] restored defaults\n");
    }
    // Reconcile a live combo-box edit immediately and reset the old capture
    // deadline when its cadence changes.
    apply_local_cube_runtime_settings(false, "panel");
    static double last[32] = {};
    static bool primed = false;
    SettingRef table[32];
    const unsigned count = settings_table(table, 32);
    bool changed = false;
    for (unsigned i = 0; i < count; ++i) {
        const double v = table[i].f ? (double)*table[i].f
                       : table[i].i ? (double)*table[i].i
                       : (*table[i].b ? 1.0 : 0.0);
        if (primed && v != last[i]) changed = true;
        last[i] = v;
    }
    primed = true;
    const double now = now_seconds();
    if (changed) g_settingsSaveAt = now + 0.75;      // debounce a drag
    if (g_settingsSaveAt > 0.0 && now >= g_settingsSaveAt) {
        g_settingsSaveAt = 0.0;
        save_settings();
    }
}

// Normalised so the scene is actually visible: raw depth clusters near 1.0 and
// a straight dump looks blank white.
static void dump_depth(const char* path) {
    DepthStats s = read_depth();
    FILE* f = open_dump_file(path);
    if (!f) { fprintf(stderr, "[shadowmap] dump: cannot open %s\n", path); return; }
    fprintf(f, "P5\n%d %d\n255\n", g_size, g_size);
    const double span = (s.hi > s.lo) ? double(s.hi) - double(s.lo) : 1.0;
    for (uint32_t v : g_readback)
        fputc((int)(255.0 * (double(v) - double(s.lo)) / span), f);
    fclose(f);
    fprintf(stderr, "[shadowmap] dumped %s  range %u..%u  texels-with-geometry %zu/%zu%s\n",
            path, s.lo, s.hi, s.written, g_readback.size(),
            (s.lo == s.hi) ? "  (uniform - nothing rendered)" : "");
}
