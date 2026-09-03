// ---------------------------------------------------------------------------
// Phase 5b: small injector-owned immediate overlay.  It deliberately avoids a
// second UI toolkit in NWN's process: a compact bitmap atlas and one GL program
// provide the stable first window, while SDL keyboard-state polling supplies a
// no-event-queue-mutation control path.
// ---------------------------------------------------------------------------
using SDLGetKeyboardState_t = const unsigned char* (*)(int*);
// Last value the receiver computed, for readouts. The helper is time-stepped,
// so calling it a second time in the same frame would advance the fade twice.
static float g_localDeselectFadeShown = 1.0f;

static SDLGetKeyboardState_t g_sdlGetKeyboardState = nullptr;

static int overlay_glyph_index(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= '0' && c <= '9') return 26 + (c - '0');
    switch (c) { case '.': return 36; case ':': return 37; case '-': return 38;
                 case '+': return 39; case '/': return 40; default: return -1; }
}

static void ensure_shadow_overlay_font() {
    if (g_shadowOverlayFontTex) return;
    // 5x7 uppercase/digit glyphs. Bits are stored left-to-right in the low
    // five bits; 8x8 atlas cells leave a quiet one-pixel border.
    static const unsigned char glyphs[41][7] = {
        {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},
        {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
        {14,17,16,23,17,17,14},{17,17,17,31,17,17,17},{31,4,4,4,4,4,31},
        {7,2,2,2,2,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
        {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
        {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
        {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
        {17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
        {17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
        {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
        {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
        {14,17,17,15,1,1,14},{0,0,0,0,0,12,12},{0,12,12,0,12,12,0},
        {0,0,0,31,0,0,0},{0,4,4,31,4,4,0},{1,2,4,8,16,0,0}
    };
    constexpr int cols = 8, cell = 8, width = cols * cell, height = 6 * cell;
    unsigned char pixels[width * height] = {};
    for (int glyph = 0; glyph < 41; ++glyph) {
        const int ox = (glyph % cols) * cell, oy = (glyph / cols) * cell;
        for (int row = 0; row < 7; ++row)
            for (int col = 0; col < 5; ++col)
                if (glyphs[glyph][row] & (1u << (4 - col)))
                    pixels[(oy + row) * width + ox + col] = 255;
    }
    GLint oldActive=GL_TEXTURE0, oldTex=0;
    gl::GetIntegerv(GL_ACTIVE_TEXTURE,&oldActive);
    gl::GetIntegerv(GL_TEXTURE_BINDING_2D,&oldTex);
    // The game can leave an unrelated GL error pending.  Do not mistake that
    // for a failure to create our atlas (the old code could silently prevent
    // the entire overlay from drawing in that case).
    while (gl::GetError() != GL_NO_ERROR) {}
    gl::GenTextures(1, &g_shadowOverlayFontTex);
    if (!g_shadowOverlayFontTex) {
        fprintf(stderr, "[shadowmap][overlay] font atlas allocation failed\n");
        return;
    }
    gl::BindTexture(GL_TEXTURE_2D, g_shadowOverlayFontTex);
    gl::TexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0,
                   GL_RED, GL_UNSIGNED_BYTE, pixels);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (gl::GetError() != GL_NO_ERROR) {
        fprintf(stderr, "[shadowmap][overlay] font atlas creation failed\n");
        gl::DeleteTextures(1, &g_shadowOverlayFontTex);
        g_shadowOverlayFontTex = 0;
    }
    gl::BindTexture(GL_TEXTURE_2D,(GLuint)oldTex);
    gl::ActiveTexture((GLenum)oldActive);
    if (g_shadowOverlayFontTex)
        fprintf(stderr, "[shadowmap][overlay] font atlas ready (texture=%u)\n",
                g_shadowOverlayFontTex);
}

static void build_shadow_overlay_program() {
    if (g_shadowOverlayProgram ) return;  // GLEW-null checks removed: see glew_or_dlsym
    auto create     = (CreateShader_t) glew_or_dlsym(eng::GlewCreateShader,  "glCreateShader");
    auto compile    = (CompileShader_t)glew_or_dlsym(eng::GlewCompileShader, "glCompileShader");
    auto getShader  = (GetShaderiv_t)  glew_or_dlsym(eng::GlewGetShaderiv,   "glGetShaderiv");
    auto createProg = (CreateProgram_t)glew_or_dlsym(eng::GlewCreateProgram, "glCreateProgram");
    auto attach     = (AttachShader_t) glew_or_dlsym(eng::GlewAttachShader,  "glAttachShader");
    auto link       = (LinkProgram_t)  glew_or_dlsym(eng::GlewLinkProgram,   "glLinkProgram");
    auto getProg    = (GetProgramiv_t) glew_or_dlsym(eng::GlewGetProgramiv,  "glGetProgramiv");
    ShaderSource_t source = g_realShaderSource ? g_realShaderSource :
                            (ShaderSource_t)glew_or_dlsym(eng::GlewShaderSource, "glShaderSource");
    if (!create || !compile || !getShader || !createProg || !attach || !link ||
        !getProg || !source) return;
    static const char* vs =
        "#version 330 compatibility\n"
        "uniform vec2 nwnOverlayScreen; uniform vec4 nwnOverlayRect,nwnOverlayGlyph;\n"
        "out vec2 nwnOverlayUV;\n"
        "void main(){ vec2 q; int i=gl_VertexID%6;"
        "if(i==0||i==3)q=vec2(0);else if(i==1)q=vec2(1,0);else q=vec2(0,1);"
        "if(i==2||i==4)q=vec2(1);"
        "vec2 p=mix(nwnOverlayRect.xy,nwnOverlayRect.zw,q);"
        "gl_Position=vec4(p.x/nwnOverlayScreen.x*2.0-1.0,1.0-p.y/nwnOverlayScreen.y*2.0,0,1);"
        "nwnOverlayUV=mix(nwnOverlayGlyph.xy,nwnOverlayGlyph.zw,q); }\n";
    static const char* fs =
        "#version 330 compatibility\n"
        "uniform sampler2D nwnOverlayFont; uniform vec4 nwnOverlayColor; uniform int nwnOverlayText;\n"
        "in vec2 nwnOverlayUV; void main(){ vec4 c=nwnOverlayColor;"
        "if(nwnOverlayText!=0){ c.a*=texture2D(nwnOverlayFont,nwnOverlayUV).r; if(c.a<0.01)discard;}"
        "gl_FragColor=c; }\n";
    GLuint v=create(GL_VERTEX_SHADER), f=create(GL_FRAGMENT_SHADER);
    const GLint vl=(GLint)strlen(vs), fl=(GLint)strlen(fs);
    source(v,1,&vs,&vl); source(f,1,&fs,&fl); compile(v); compile(f);
    GLint vok=0,fok=0; getShader(v,GL_COMPILE_STATUS,&vok); getShader(f,GL_COMPILE_STATUS,&fok);
    if (!vok || !fok) { fprintf(stderr,"[shadowmap][overlay] shader compile FAILED\n"); return; }
    GLuint p=createProg(); attach(p,v); attach(p,f); link(p);
    GLint pok=0; getProg(p,GL_LINK_STATUS,&pok);
    if (!pok) { fprintf(stderr,"[shadowmap][overlay] program link FAILED\n"); return; }
    g_shadowOverlayProgram=p;
    fprintf(stderr,"[shadowmap][overlay] settings UI compiled (program=%u)\n",p);
}

static void update_shadow_overlay_input() {
    if (!g_sdlGetKeyboardState)
        g_sdlGetKeyboardState = (SDLGetKeyboardState_t)dlsym(RTLD_DEFAULT, "SDL_GetKeyboardState");
    if (!g_sdlGetKeyboardState) return;
    int count = 0;
    const unsigned char* keys = g_sdlGetKeyboardState(&count);
    if (!keys || count <= 0) return;
    auto down = [&](int sc) { return sc >= 0 && sc < count && keys[sc] != 0; };
    auto edge = [&](int sc) {
        const bool now = down(sc), fired = now && !g_shadowOverlayPreviousKeys[sc];
        g_shadowOverlayPreviousKeys[sc] = now ? 1 : 0;
        return fired;
    };
    constexpr int kF11=68, kEscape=41, kEnter=40, kUp=82, kDown=81, kLeft=80, kRight=79;
    constexpr int kLCtrl=224, kRCtrl=228, kLShift=225, kRShift=229;
    const bool modifier = (down(kLCtrl) || down(kRCtrl)) && (down(kLShift) || down(kRShift));
    const bool f11 = edge(kF11), escape = edge(kEscape), enter = edge(kEnter);
    const bool up = edge(kUp), downKey = edge(kDown), left = edge(kLeft), right = edge(kRight);
    if (modifier && f11 && !g_shadowOverlayToggleLatch) {
        g_shadowOverlayVisible = !g_shadowOverlayVisible;
        g_shadowOverlayToggleLatch = true;
        fprintf(stderr,"[shadowmap][overlay] %s (Ctrl+Shift+F11)\n",
                g_shadowOverlayVisible ? "opened" : "closed");
    }
    if (!down(kF11)) g_shadowOverlayToggleLatch = false;
    if (!g_shadowOverlayVisible) return;
    if (escape) { g_shadowOverlayVisible=false; fprintf(stderr,"[shadowmap][overlay] closed (Escape)\n"); return; }
    if (!modifier) return;
    if (up) g_shadowOverlaySelection = (g_shadowOverlaySelection + 3) % 4;
    if (downKey) g_shadowOverlaySelection = (g_shadowOverlaySelection + 1) % 4;
    const int direction = right ? 1 : (left ? -1 : 0);
    if (g_shadowOverlaySelection == 0 && (enter || direction != 0))
        g_cascadeCompositeShadows = !g_cascadeCompositeShadows;
    else if (direction != 0 && g_shadowOverlaySelection == 1)
        g_cascadeCompositeStrength = std::max(0.0f, std::min(1.0f,
            g_cascadeCompositeStrength + direction * 0.02f));
    else if (direction != 0 && g_shadowOverlaySelection == 2)
        g_cascadeReceiverBias = std::max(0.0f, std::min(0.05f,
            g_cascadeReceiverBias + direction * 0.00025f));
    else if (direction != 0 && g_shadowOverlaySelection == 3)
        g_cascadeLambda = std::max(0.0f, std::min(1.0f,
            g_cascadeLambda + direction * 0.02f));
    if (!g_shadowOverlayInputLogged) {
        g_shadowOverlayInputLogged = true;
        fprintf(stderr,"[shadowmap][overlay] controls: Ctrl+Shift+Up/Down selects; "
                       "Ctrl+Shift+Left/Right edits; Ctrl+Shift+Enter toggles output\n");
    }
}

struct ShadowOverlayDraw {
    GLint screen=-1, rect=-1, glyph=-1, color=-1, text=-1, font=-1;
    int width=0, height=0;
};

static void shadow_overlay_rect(const ShadowOverlayDraw& d, float x, float y,
                                float w, float h, float r, float g, float b, float a) {
    gl::Uniform4f(d.rect,x,y,x+w,y+h); gl::Uniform4f(d.color,r,g,b,a);
    gl::Uniform1i(d.text,0); gl::Uniform4f(d.glyph,0,0,1,1);
    const bool prior=g_inOurPass; g_inOurPass=true;
    if (g_nextDrawArrays) g_nextDrawArrays(0x0004,0,6);
    g_inOurPass=prior;
}

static void shadow_overlay_text(const ShadowOverlayDraw& d, float x, float y, float scale,
                                const char* text, float r, float g, float b, float a) {
    constexpr float invW=1.0f/64.0f, invH=1.0f/48.0f;
    for (const char* p=text; *p; ++p) {
        if (*p == ' ') { x += 4.0f * scale; continue; }
        const int glyph=overlay_glyph_index(*p);
        if (glyph < 0) { x += 6.0f * scale; continue; }
        const float u=(float)((glyph%8)*8)*invW, v=(float)((glyph/8)*8)*invH;
        gl::Uniform4f(d.rect,x,y,x+5.0f*scale,y+7.0f*scale);
        gl::Uniform4f(d.glyph,u,v,u+5.0f*invW,v+7.0f*invH);
        gl::Uniform4f(d.color,r,g,b,a); gl::Uniform1i(d.text,1);
        const bool prior=g_inOurPass; g_inOurPass=true;
        if (g_nextDrawArrays) g_nextDrawArrays(0x0004,0,6);
        g_inOurPass=prior;
        x += 6.0f * scale;
    }
}

// ImGui panel. Replaces the hand-rolled renderer below, which compiled and
// toggled but never became visible (Phase 5b, deferred). Set
// NWN_SHADOWMAP_OVERLAY_LEGACY=1 to fall back to that one for A/B.
// Is the published local map still usable this frame?
//
// This used to be `serial - g_localLightVPFrame <= 1`, which was correct only
// while the capture ran EVERY frame. The cadence control republishes every N
// frames, and a frame-count rule cannot tell a throttled capture from a stopped
// one -- so the receiver dropped the local term on every frame between
// captures and the shadow flickered at exactly the cadence rate.
//
// Age in SECONDS against the configured interval keeps the original guarantee
// (a capture that genuinely stopped stops being composited, within a fraction
// of a second) while a deliberately slow cadence stays rock solid. The cube
// path has always measured freshness this way; this is the same rule.
//
// The multiplier tolerates a capture opportunity or two being missed -- a frame
// where the dynamic stage did not complete, or a hitch -- without blinking the
// shadow off. The floor keeps fast cadences from becoming hair-triggered.
static bool local_map_still_fresh() {
    if (!g_localLightVPFrame) return false;
    if (g_localLightLastCaptureTime < 0.0) return false;
    const double age = now_seconds() - g_localLightLastCaptureTime;
    const double maxAge = std::max(0.25, g_localCubeUpdateSeconds * 3.0);
    return age <= maxAge;
}

static void draw_shadow_overlay_imgui(void* scene) {
    if (scene != g_areaScene) return;
    // Hand the panel the live tunables so edits apply on the next frame: the
    // receiver re-reads every one of these when it uploads its uniforms.
    NwnOverlayState st;
    st.csmStrength   = &g_cascadeCompositeStrength;
    st.csmBias       = &g_cascadeReceiverBias;
    st.csmBlend      = &g_cascadeBlendWidth;
    st.csmPcf        = &g_cascadePcfRadius;
    st.areaShadowFadeSeconds = &g_areaShadowFadeSeconds;
    st.csmComposite  = &g_cascadeCompositeShadows;
    st.localEnabled  = &g_localLightReceiver;
    st.localStrength = &g_localLightStrength;
    st.localBias     = &g_localLightBias;
    st.localLift     = &g_localLightLift;
    st.localDynOnly  = &g_localLightDynamicOnly;
    st.localEdgeFade = &g_localLightEdgeFade;
    st.localFalloff  = &g_localLightFalloff;
    st.localFov      = &g_localLightFovDeg;
    st.localHeight   = &g_localLightHeight;
    st.localSoft     = &g_localLightSoft;
    st.localSlope    = &g_localLightSlopeBias;
    st.localNormalBias= &g_localLightNormalBias;
    st.localAlphaCast = &g_localLightAlphaCasters;
    st.localMinSep    = &g_localLightMinSep;
    st.localEmitCast  = &g_localLightEmittersCast;
    st.localMapSize  = &g_pendingLocalSize;
    st.localMapLive  = g_localLightCaptureSize;
    st.localCubeQuality = &g_localCubeQuality;
    st.localCubeSources = &g_localCubeSourceBudget;
    // The regular local depth array and receiver are already slot-based. Show
    // the number that actually survived NWN's priority list/radius validation,
    // not the old diagnostic cube probe count.
    st.localCubeActiveSources = g_localLightSlotCount;
    st.localShadowCandidates = g_engineShadowLightCount;
    // Shadow-source budget, not the independent lift/census count. This keeps
    // the panel's local-map estimate aligned with the chosen tier's light count.
    st.localSlots = (unsigned)std::max(1, std::min(local_source_lights(),
                                                   kLocalLightFaces));
    // Straight from the census, NOT from a counter the capture writes. The
    // capture only runs when the local-light path is fully armed, so routing
    // this through it made the panel report "0 lights" while the lift was
    // happily using twelve -- the count was reporting on the CAPTURE, not on
    // the lights.
    st.localPickCount = g_lampLightCount;
    st.liftThreshold = &g_liftThreshold;
    st.lampLift      = &g_lampLiftEnabled;
    st.dynamicCasters = &g_dynamicCastersEnabled;
    st.staticCasters  = &g_staticCastersEnabled;
    st.hideEngineShadows = &g_hideEngineShadows;
    st.resetDefaults = &g_settingsResetRequested;
    st.receiverDebug = &g_receiverDebug;

    if (g_pendingCascadeSize == 0) g_pendingCascadeSize = g_size;
    if (g_pendingWorldSize   == 0) g_pendingWorldSize   = g_staticWorldSize;
    st.cascadeCount       = &g_cascadeActiveCount;
    st.dynamicLayers      = &g_cascadeDynamicLayers;
    st.receiverEnabled    = &g_receiverEnabled;
    st.fullBspEnabled     = &g_fullBspEnabled;
    st.staticNearCascades = &g_staticNearCascades;
    st.fogFade            = &g_fogFade;
    st.fogStart           = &g_fogStart;
    st.fogEnd             = &g_fogEnd;
    st.fogStartLive       = g_fogStartLive;
    st.fogEndLive         = g_fogEndLive;
    st.fogFromEngine      = g_fogFromEngine;
    st.cascadeDistance    = &g_cascadeMaxDistance;
    st.staticCache        = &g_cascadeStaticCache;
    st.cacheMove          = &g_cascadeCacheMove;
    st.worldEnabled       = &g_staticWorldEnabled;
    if (g_pendingWorldExtent <= 0.0f) g_pendingWorldExtent = g_staticWorldExtent;
    st.worldExtent        = &g_pendingWorldExtent;   // staged; Apply commits it
    st.worldExtentLive    = g_staticWorldExtent;
    st.pendingCascadeSize = &g_pendingCascadeSize;
    st.pendingWorldSize   = &g_pendingWorldSize;
    st.applyResolution    = &g_resolutionApplyRequested;
    st.msReceiver     = g_msReceiverShown;
    st.msWorldMap     = g_msWorldMapShown;
    st.msReplay       = g_msReplayShown;
    st.msSceneCopy    = g_msSceneCopyShown;
    st.engineDraws    = g_frameDrawCallsShown;
    st.replayCalls    = g_replayCallsShown;
    st.replayDraws    = g_replayDrawsShown;
    // Running totals, not per-frame: "0 hit / 4823 refit" reads as a verdict at
    // a glance, where a per-frame 0/1 does not.
    st.fitCacheHits   = g_cascadeCacheHits;
    st.fitRefits      = g_cascadeRefitMove + g_cascadeRefitTurn + g_cascadeRefitSun +
                        g_cascadeRefitScene + g_cascadeRefitDisabled;
    st.fullBspSkipped = g_fullBspSkipped;
    st.cascadeSizeLive    = g_size;
    st.worldSizeLive      = g_staticWorldSize;
    st.worldValid         = g_staticWorldValid;
    st.worldRenders       = g_staticWorldRenders;
    st.injectorTargetMiB  = injector_target_mib();
    overlay_driver_vram(st.driverVramKnown, st.driverVramTotalMiB,
                        st.driverVramAvailableMiB);

    st.bucketReplay = g_cascadeBucketReplay;
    // Same freshness rule the receiver uses, or the panel reports "not ready"
    // while the shadow is on screen.
    st.localReady   = g_haveLocalLightVP && g_localLightTargetUsable &&
                      g_localDeselectFadeShown > 0.0005f;
    // Never let the debug/readout path overwrite the engine-selected light
    // with a camera-ranked census entry.  LightManager::GetShadowLights is
    // NWN's authority for both the capture and the displayed selection.
    st.localPos[0]  = g_localLightWorldPos[0];
    st.localPos[1]  = g_localLightWorldPos[1];
    st.localPos[2]  = g_localLightWorldPos[2];
    st.localRadius  = g_localLightWorldRadius;
    st.localRgb[0]  = g_localLightSelectedInfo.color[0];
    st.localRgb[1]  = g_localLightSelectedInfo.color[1];
    st.localRgb[2]  = g_localLightSelectedInfo.color[2];
    st.localLights  = (g_localLightLastCount == UINT32_MAX) ? 0u : g_localLightLastCount;
    for (int i = 0; i < kCascadeCount; ++i) {
        st.clipFar[i] = g_cascadeMath.clipFar[i];
        st.staticDraws  += g_cascadeStaticCaptureDrawsLayer[i];
        st.dynamicDraws += g_cascadeDynamicCaptureDrawsLayer[i];
    }
    if (g_haveTraceAreaLight) {
        st.sunDir[0] = g_traceAreaLightDir[0];
        st.sunDir[1] = g_traceAreaLightDir[1];
        st.sunDir[2] = g_traceAreaLightDir[2];
    }

    GLint vp[4] = {0,0,0,0};
    if (gl::GetIntegerv) gl::GetIntegerv(GL_VIEWPORT, vp);
    if (vp[2] <= 0 || vp[3] <= 0) return;

    // ImGui's GL3 backend saves/restores GL state itself, but this engine is
    // fussy enough that the injector adds its own outer save/restore, exactly
    // as every other private pass here does.
    GLint oldProg = 0, oldFbo = 0, oldActive = GL_TEXTURE0;
    GLboolean oldDepth = gl::IsEnabled(GL_DEPTH_TEST);
    GLboolean oldScissorEnabled = gl::IsEnabled(GL_SCISSOR_TEST);
    GLboolean oldMask = GL_TRUE, oldColor[4] = {GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE};
    gl::GetIntegerv(GL_CURRENT_PROGRAM, &oldProg);
    gl::GetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFbo);
    gl::GetIntegerv(GL_ACTIVE_TEXTURE, &oldActive);
    gl::GetBooleanv(GL_DEPTH_WRITEMASK, &oldMask);
    gl::GetBooleanv(GL_COLOR_WRITEMASK, oldColor);
    if (oldDepth) gl::Disable(GL_DEPTH_TEST);
    if (oldScissorEnabled) gl::Disable(GL_SCISSOR_TEST);
    gl::ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl::DepthMask(GL_FALSE);

    // The overlay's own draws must not be mistaken for engine geometry by the
    // interposed draw wrappers -- same rule as every other injector pass. Note
    // this uses g_overlayPassActive, NOT g_inOurPass; see that flag's comment.
    const bool priorOverlay = g_overlayPassActive;
    g_overlayPassActive = true;
#ifdef _WIN32
    // Idempotent; the window may not exist on the first frames.
    nwn_win_install_input_capture();
#endif
    nwn_overlay_render(vp[2], vp[3], st);
    g_overlayPassActive = priorOverlay;

    if (oldDepth) gl::Enable(GL_DEPTH_TEST);
    if (oldScissorEnabled) gl::Enable(GL_SCISSOR_TEST);
    gl::DepthMask(oldMask);
    gl::ColorMask(oldColor[0], oldColor[1], oldColor[2], oldColor[3]);
    gl::ActiveTexture((GLenum)oldActive);
    gl::BindFramebuffer(GL_FRAMEBUFFER, (GLuint)oldFbo);
    if (g_realUseProgram) g_realUseProgram((GLuint)oldProg);
    else if (eng::GlewUseProgram && *eng::GlewUseProgram)
        ((UseProgram_t)*eng::GlewUseProgram)((GLuint)oldProg);
}

static void draw_shadow_overlay(void* scene) {
    update_shadow_overlay_input();
    nwn_overlay_set_visible(g_shadowOverlayVisible);
    if (!g_overlayLegacy) { draw_shadow_overlay_imgui(scene); return; }
    if (!g_shadowOverlayVisible || !g_cascadeCsmStaticReceiver || scene != g_areaScene ||
        (!g_shadowOverlayProgram && !eng::GlewCreateShader)) return;
    UseProgram_t useProgram = g_realUseProgram;
    if (!useProgram && eng::GlewUseProgram && *eng::GlewUseProgram)
        useProgram = (UseProgram_t)*eng::GlewUseProgram;
    if (!useProgram) return;
    build_shadow_overlay_program(); ensure_shadow_overlay_font();
    if (!g_shadowOverlayProgram || !g_nextDrawArrays) {
        static bool loggedMissing = false;
        if (!loggedMissing) {
            loggedMissing = true;
            fprintf(stderr,"[shadowmap][overlay] waiting for renderer program=%u draw=%s\n",
                    g_shadowOverlayProgram, g_nextDrawArrays ? "ready" : "missing");
        }
        return;
    }
    GLint vp[4]={0,0,0,0}, oldProg=0, oldActive=GL_TEXTURE0, oldFbo=0, oldFont=0;
    GLint oldBlendSrcRGB=GL_ONE,oldBlendDstRGB=GL_ZERO,oldBlendSrcAlpha=GL_ONE,oldBlendDstAlpha=GL_ZERO;
    GLint oldBlendEquationRGB=GL_FUNC_ADD,oldBlendEquationAlpha=GL_FUNC_ADD,maxUnits=0;
    GLboolean oldDepth=gl::IsEnabled(GL_DEPTH_TEST),oldBlend=gl::IsEnabled(GL_BLEND),oldMask=GL_TRUE;
    GLboolean oldColor[4]={GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE};
    const bool oldScissor=gl::IsEnabled(GL_SCISSOR_TEST);
    gl::GetIntegerv(GL_VIEWPORT,vp); gl::GetIntegerv(GL_CURRENT_PROGRAM,&oldProg);
    gl::GetIntegerv(GL_ACTIVE_TEXTURE,&oldActive); gl::GetIntegerv(GL_FRAMEBUFFER_BINDING,&oldFbo);
    gl::GetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS,&maxUnits);
    if (oldFbo != 0 || vp[2] <= 0 || vp[3] <= 0 || maxUnits <= 28) {
        static bool loggedState = false;
        if (!loggedState) {
            loggedState = true;
            fprintf(stderr,"[shadowmap][overlay] waiting for area framebuffer fbo=%d viewport=%dx%d units=%d\n",
                    oldFbo,vp[2],vp[3],maxUnits);
        }
        return;
    }
    gl::GetIntegerv(GL_BLEND_SRC_RGB,&oldBlendSrcRGB); gl::GetIntegerv(GL_BLEND_DST_RGB,&oldBlendDstRGB);
    gl::GetIntegerv(GL_BLEND_SRC_ALPHA,&oldBlendSrcAlpha); gl::GetIntegerv(GL_BLEND_DST_ALPHA,&oldBlendDstAlpha);
    gl::GetIntegerv(GL_BLEND_EQUATION_RGB,&oldBlendEquationRGB); gl::GetIntegerv(GL_BLEND_EQUATION_ALPHA,&oldBlendEquationAlpha);
    gl::GetBooleanv(GL_DEPTH_WRITEMASK,&oldMask); gl::GetBooleanv(GL_COLOR_WRITEMASK,oldColor);
    gl::ActiveTexture(GL_TEXTURE0+28); gl::GetIntegerv(GL_TEXTURE_BINDING_2D,&oldFont);
    gl::BindTexture(GL_TEXTURE_2D,g_shadowOverlayFontTex);
    if (oldDepth) gl::Disable(GL_DEPTH_TEST);
    if (oldScissor) gl::Disable(GL_SCISSOR_TEST);
    gl::Enable(GL_BLEND); gl::BlendEquationSeparate(GL_FUNC_ADD,GL_FUNC_ADD);
    gl::BlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
    gl::DepthMask(GL_FALSE); gl::ColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    useProgram(g_shadowOverlayProgram);
    ShadowOverlayDraw d;
    d.screen=gl::GetUniformLocation(g_shadowOverlayProgram,"nwnOverlayScreen");
    d.rect=gl::GetUniformLocation(g_shadowOverlayProgram,"nwnOverlayRect");
    d.glyph=gl::GetUniformLocation(g_shadowOverlayProgram,"nwnOverlayGlyph");
    d.color=gl::GetUniformLocation(g_shadowOverlayProgram,"nwnOverlayColor");
    d.text=gl::GetUniformLocation(g_shadowOverlayProgram,"nwnOverlayText");
    d.font=gl::GetUniformLocation(g_shadowOverlayProgram,"nwnOverlayFont");
    d.width=vp[2]; d.height=vp[3];
    if (d.screen >= 0) gl::Uniform2f(d.screen,(float)d.width,(float)d.height);
    if (d.font >= 0) gl::Uniform1i(d.font,28);
    const float x=48.0f, y=62.0f, w=408.0f, h=224.0f, textScale=2.0f;
    shadow_overlay_rect(d,x,y,w,h,0.025f,0.035f,0.055f,0.93f);
    shadow_overlay_rect(d,x,y,w,28,0.10f,0.17f,0.25f,0.98f);
    if (g_shadowOverlayFontTex)
        shadow_overlay_text(d,x+14,y+7,textScale,"NWN SHADOWMAP",0.88f,0.94f,1.0f,1.0f);
    const char* labels[4]={"OUTPUT","STRENGTH","BIAS","SPLIT LAMBDA"};
    char values[4][32];
    std::snprintf(values[0],sizeof(values[0]),"%s",g_cascadeCompositeShadows?"DARK":"RED");
    std::snprintf(values[1],sizeof(values[1]),"%.2f",g_cascadeCompositeStrength);
    std::snprintf(values[2],sizeof(values[2]),"%.5f",g_cascadeReceiverBias);
    std::snprintf(values[3],sizeof(values[3]),"%.2f",g_cascadeLambda);
    for (int row=0; row<4; ++row) {
        const float ry=y+42.0f+row*34.0f;
        if (row==g_shadowOverlaySelection)
            shadow_overlay_rect(d,x+9,ry-3,w-18,26,0.12f,0.25f,0.38f,0.66f);
        if (g_shadowOverlayFontTex) {
            shadow_overlay_text(d,x+18,ry,textScale,labels[row],0.82f,0.88f,0.94f,1.0f);
            shadow_overlay_text(d,x+253,ry,textScale,values[row],
                                row==g_shadowOverlaySelection?0.95f:0.70f,
                                row==g_shadowOverlaySelection?0.85f:0.82f,0.45f,1.0f);
        }
        if (row > 0) {
            const float amount = row==1 ? g_cascadeCompositeStrength :
                                 row==2 ? g_cascadeReceiverBias/0.01f : g_cascadeLambda;
            shadow_overlay_rect(d,x+252,ry+18,122,4,0.18f,0.22f,0.28f,1.0f);
            shadow_overlay_rect(d,x+252,ry+18,122*std::min(1.0f,amount),4,0.85f,0.62f,0.22f,1.0f);
        }
    }
    if (g_shadowOverlayFontTex) {
        shadow_overlay_text(d,x+18,y+187,1.0f,"CTRL SHIFT ARROWS SELECT EDIT",0.62f,0.72f,0.84f,1.0f);
        shadow_overlay_text(d,x+18,y+202,1.0f,"CTRL SHIFT ENTER TOGGLE  ESC CLOSE",0.62f,0.72f,0.84f,1.0f);
    }
    static bool loggedDraw = false;
    if (!loggedDraw) {
        loggedDraw = true;
        fprintf(stderr,"[shadowmap][overlay] draw live viewport=%dx%d font=%s\n",
                d.width,d.height,g_shadowOverlayFontTex ? "ready" : "unavailable");
    }
    useProgram((GLuint)oldProg); gl::ActiveTexture(GL_TEXTURE0+28);
    gl::BindTexture(GL_TEXTURE_2D,(GLuint)oldFont); gl::ActiveTexture((GLenum)oldActive);
    if(oldDepth) gl::Enable(GL_DEPTH_TEST); else gl::Disable(GL_DEPTH_TEST);
    if(oldBlend) gl::Enable(GL_BLEND); else gl::Disable(GL_BLEND);
    if(oldScissor) gl::Enable(GL_SCISSOR_TEST); else gl::Disable(GL_SCISSOR_TEST);
    gl::BlendEquationSeparate((GLenum)oldBlendEquationRGB,(GLenum)oldBlendEquationAlpha);
    gl::BlendFuncSeparate((GLenum)oldBlendSrcRGB,(GLenum)oldBlendDstRGB,
                          (GLenum)oldBlendSrcAlpha,(GLenum)oldBlendDstAlpha);
    gl::DepthMask(oldMask); gl::ColorMask(oldColor[0],oldColor[1],oldColor[2],oldColor[3]);
}

// Why the receiver refuses to draw, reported AT THE RETURN.
//
// The first version recorded the reason and printed it from the top of the
// next call. That produced a line claiming REASON=noStaticTex next to
// staticTex=1095 in the same report -- a reason from some earlier call, next
// to a value from this one. A report that can disagree with itself is worse
// than none: it sent two rounds chasing the wrong gate. Print where it
// happens, rate-limited, with the values that decided it.
static const char* g_recvBlock = "not evaluated yet";
static unsigned g_recvBlockSeen = 0;

static void recv_block(const char* why, long a = -1, long b = -1, long c = -1) {
    const bool changed = (why != g_recvBlock);
    g_recvBlock = why;
    if (changed || (++g_recvBlockSeen % 600) == 0)
        fprintf(stderr, "[shadowmap][gate] receiver BLOCKED at %s (a=%ld b=%ld c=%ld) "
                        "staticTex=%u dynTex=%u nearCascades=%d cascades=%d dynLayers=%d "
                        "worldValid=%d serial=%u\n",
                why, a, b, c, g_cascadeStaticTex, g_cascadeDynamicTex,
                g_staticNearCascades, g_cascadeActiveCount, g_cascadeDynamicLayers,
                (int)g_staticWorldValid, g_shadowFrameContext.serial);
}

// Short fade-out when a local light stops being refreshed.
//
// Everything else about the local term is engine-driven, but this one cannot
// be: once NWN deselects a light there is no engine value left to follow -- the
// light is simply gone from GetShadowLights(). Without this the shadow survives
// until the map goes stale and then cuts in a single frame.
//
// Safe because it is COMPOSITE-ONLY and one-directional: it scales the
// receiver's local strength on the way out and never clears, rebuilds or
// invalidates a map. The last published generation stays bound, and the light's
// view/projection is world-space, so the shadow keeps its correct shape and
// only loses opacity.
//
// Short on purpose. This is a softener for a hard cut, not the day/night fade
// (that one follows the engine, see update_engine_shadow_fade).
static constexpr double kLocalDeselectFadeSeconds = 0.35;

static float local_deselect_fade(bool fresh) {
    static float  level = 1.0f;
    static double last  = 0.0;
    const double now = now_seconds();
    if (last <= 0.0) { last = now; level = fresh ? 1.0f : 0.0f; return level; }
    const double dt = now - last;
    last = now;
    if (fresh) { level = 1.0f; return level; }   // refreshed: instantly current
    if (dt <= 0.0) return level;
    level -= (float)(dt / kLocalDeselectFadeSeconds);
    if (level < 0.0f) level = 0.0f;
    return level;
}

static void draw_static_receiver(void* scene) {
    if (!g_receiverEnabled) return;   // master A/B, not a fault
    // Bounded "why did I not draw" report. The receiver has ~10 independent
    // gates and returns silently from all of them, which on Windows left us
    // with cascades demonstrably full and nothing on screen. Same debug-ladder
    // discipline that pinned the SDL recursion: make the code say it.
    {
        // Only report for the SELECTED AREA scene. The first version burned
        // its whole budget on menu/loading scenes (ctxSerial=0, no area yet)
        // and was silent by the time the game was actually running.
        static unsigned gateReports = 0, gateSeen = 0;
        if (g_staticReceiverDraws == 0 && g_areaScene && scene == g_areaScene &&
            (gateReports < 6 || (++gateSeen % 300) == 0)) {
            ++gateReports;
            unsigned staleStatic = 0, staleDynamic = 0;
            for (int i = 0; i < kCascadeCount; ++i) {
                // Same rule as the real gate below: the stamp alone.
                if (g_cascadeStaticCaptureFrameLayer[i] != g_shadowFrameContext.serial) ++staleStatic;
                if (g_cascadeDynamicCaptureFrameLayer[i] != g_shadowFrameContext.serial) ++staleDynamic;
            }
            fprintf(stderr, "[shadowmap][gate] receiver-not-drawn: multiLayer=%d csmStatic=%d "
                    "staticRecv=%d areaScene=%d(ctx=%d) ctxValid=%d mathValid=%d "
                    "mathFrame=%u ctxSerial=%u staticTex=%u dynTex=%u dynRecv=%d "
                    "staleStaticLayers=%u staleDynLayers=%u useProgram=%p\n",
                    (int)g_cascadeMultiLayerCapture, (int)g_cascadeCsmStaticReceiver,
                    (int)g_cascadeStaticReceiver, (int)(scene == g_areaScene),
                    (int)(g_shadowFrameContext.scene == scene),
                    (int)g_shadowFrameContext.valid, (int)g_cascadeMath.valid,
                    g_cascadeMath.sourceFrame, g_shadowFrameContext.serial,
                    g_cascadeStaticTex, g_cascadeDynamicTex,
                    (int)g_cascadeDynamicReceiver, staleStatic, staleDynamic,
                    (void*)g_realUseProgram);
        }
    }
    const char* blocked = nullptr;
    if (g_cascadeMultiLayerCapture && !g_cascadeCsmStaticReceiver) blocked = "multiLayerWithoutCsmStatic";
    else if (!g_cascadeStaticReceiver)                             blocked = "staticReceiverOff";
    else if (scene != g_areaScene) {
        // Do NOT record this one. draw_static_receiver runs for every scene,
        // menus included, so recording it overwrote the area scene's real
        // reason every frame -- the first version of this report said
        // "notAreaScene" and told us nothing.
        return;
    }
    else if (!g_shadowFrameContext.valid)                          blocked = "ctxInvalid";
    else if (g_shadowFrameContext.scene != scene)                  blocked = "ctxSceneMismatch";
    else if (!g_cascadeMath.valid)                                 blocked = "mathInvalid";
    else if (g_cascadeMath.sourceFrame != g_shadowFrameContext.serial) blocked = "mathFrameStale";
    else if (!g_cascadeStaticTex)                                  blocked = "noStaticTex";
    else if (g_cascadeDynamicReceiver && !g_cascadeDynamicTex)     blocked = "noDynamicTex";
    else if (g_staticWorldEnabled && !g_staticWorldValid)          blocked = "worldMapInvalid";
    if (blocked) { recv_block(blocked); return; }
    if (g_cascadeCsmStaticReceiver) {
        const int activeLayers = (g_cascadeActiveCount < 1) ? 1
                               : (g_cascadeActiveCount > kCascadeCount ? kCascadeCount
                                                                       : g_cascadeActiveCount);
        for (int layer = 0; layer < activeLayers; ++layer) {
            // With the world-anchored map the static cascade layers are never
            // written, so their per-layer freshness is meaningless; the map's
            // own validity is the gate instead.
            const bool needStaticLayer = !g_staticWorldEnabled ||
                                         layer < g_staticNearCascades;
            // FRESHNESS IS THE FRAME STAMP, NOT THE DRAW COUNT. The stamp is
            // written in the same branch that issues ClearDepth+Clear for the
            // layer, so frame == serial already proves the capture pass ran and
            // wiped last frame's depth. A layer that then took ZERO draws is
            // legitimately EMPTY -- nothing in that slice casts -- and sampling
            // it correctly reports "not occluded".
            //
            // Requiring draws > 0 conflated "empty" with "did not run" and
            // suppressed the ENTIRE receiver, static and world shadows
            // included, whenever one slice happened to have no casters. That
            // is what broke native Windows while Proton (where the dynamic
            // bucket always had geometry) looked fine:
            //   BLOCKED at dynLayer0Stale (a=0 b=8924 c=8924)
            // draws=0, but capture frame == context serial.
            if (needStaticLayer &&
                g_cascadeStaticCaptureFrameLayer[layer] != g_shadowFrameContext.serial) {
                static const char* kNames[4] = {"staticLayer0Stale","staticLayer1Stale",
                                                "staticLayer2Stale","staticLayer3Stale"};
                recv_block(kNames[layer & 3],
                           (long)g_cascadeStaticCaptureDrawsLayer[layer],
                           (long)g_cascadeStaticCaptureFrameLayer[layer],
                           (long)g_shadowFrameContext.serial);
                return;
            }
            // The CSM dynamic extension may only composite a frame in which
            // bucket 2 has populated every matching dynamic layer.  This is
            // deliberately stricter than retaining prior-frame character
            // depth: a missing body pass must suppress the diagnostic, never
            // make a stale player shadow appear to follow the camera.
            if (g_cascadeCsmDynamicReceiver && g_cascadeDynamicLayers > 0 &&
                layer < g_cascadeDynamicLayers &&
                g_cascadeDynamicCaptureFrameLayer[layer] != g_shadowFrameContext.serial) {
                static const char* kNames[4] = {"dynLayer0Stale","dynLayer1Stale",
                                                "dynLayer2Stale","dynLayer3Stale"};
                recv_block(kNames[layer & 3],
                           (long)g_cascadeDynamicCaptureDrawsLayer[layer],
                           (long)g_cascadeDynamicCaptureFrameLayer[layer],
                           (long)g_shadowFrameContext.serial);
                return;
            }
        }
    } else if (g_cascadeStaticCaptureFrame != g_shadowFrameContext.serial ||
               !g_cascadeStaticCaptureDraws ||
               (g_cascadeDynamicReceiver &&
                (g_cascadeDynamicCaptureFrame != g_shadowFrameContext.serial ||
                 !g_cascadeDynamicCaptureDraws))) {
        recv_block("legacyCaptureStale");
        return;
    }
    // Trace mode deliberately does not patch __glewUseProgram, but the engine
    // has already resolved its live dispatcher.  Use that exact unmodified
    // function directly; installing another global hook here would broaden
    // this static-only diagnostic's scope for no benefit.
    UseProgram_t useProgram = g_realUseProgram;
    if (!useProgram && eng::GlewUseProgram && *eng::GlewUseProgram)
        useProgram = (UseProgram_t)*eng::GlewUseProgram;
    if (!useProgram) {
        static bool logged = false;
        if (!logged) {
            logged = true;
            fprintf(stderr,"[shadowmap][static] receiver waiting for glUseProgram dispatcher\n");
        }
        recv_block("noUseProgram");
        return;
    }
    build_static_receiver_program();
    if (!g_staticReceiverProgram) { recv_block("noReceiverProgram"); return; }
    if (!gl::CopyTexSubImage2D)    { recv_block("noCopyTexSubImage2D"); return; }

    GLint vp[4]={0,0,0,0}, oldProg=0, oldActive=GL_TEXTURE0, oldFbo=0;
    GLint oldStaticTex=0, oldDynamicTex=0, oldSceneTex=0;
    GLint oldBlendSrcRGB=GL_ONE, oldBlendDstRGB=GL_ZERO;
    GLint oldBlendSrcAlpha=GL_ONE, oldBlendDstAlpha=GL_ZERO;
    GLint oldBlendEquationRGB=GL_FUNC_ADD, oldBlendEquationAlpha=GL_FUNC_ADD;
    GLboolean oldDepth=gl::IsEnabled(GL_DEPTH_TEST), oldBlend=gl::IsEnabled(GL_BLEND);
    GLboolean oldMask=GL_TRUE, oldColor[4]={GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE};
    const bool oldScissor=gl::IsEnabled(GL_SCISSOR_TEST);
    gl::GetIntegerv(GL_VIEWPORT,vp); gl::GetIntegerv(GL_CURRENT_PROGRAM,&oldProg);
    gl::GetIntegerv(GL_ACTIVE_TEXTURE,&oldActive);
    gl::GetIntegerv(GL_FRAMEBUFFER_BINDING,&oldFbo);
    gl::GetIntegerv(GL_BLEND_SRC_RGB,&oldBlendSrcRGB);
    gl::GetIntegerv(GL_BLEND_DST_RGB,&oldBlendDstRGB);
    gl::GetIntegerv(GL_BLEND_SRC_ALPHA,&oldBlendSrcAlpha);
    gl::GetIntegerv(GL_BLEND_DST_ALPHA,&oldBlendDstAlpha);
    gl::GetIntegerv(GL_BLEND_EQUATION_RGB,&oldBlendEquationRGB);
    gl::GetIntegerv(GL_BLEND_EQUATION_ALPHA,&oldBlendEquationAlpha);
    gl::GetBooleanv(GL_DEPTH_WRITEMASK,&oldMask); gl::GetBooleanv(GL_COLOR_WRITEMASK,oldColor);
    // The private copy is only defined for the completed area framebuffer.
    // Refuse instead of sampling a UI/transition FBO and recreating the leak.
    if (oldFbo != 0 || vp[2] <= 0 || vp[3] <= 0) {
        // NOT the default framebuffer, or no viewport. Worth naming precisely:
        // the receiver composites onto the finished scene, so an engine that is
        // still rendering into its own FBO at this point (post-processing,
        // upscaling) parks the whole feature silently.
        recv_block((oldFbo != 0) ? "notDefaultFramebuffer" : "emptyViewport",
                   oldFbo, vp[2], vp[3]);
        return;
    }

    // See g_shadowSamplerDummyTex: the local-light unit must always hold a
    // comparison-mode depth texture, even when no local light is active.
    if (!g_shadowSamplerDummyTex) {
        GLint prevTex = 0;
        gl::GetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
        gl::GenTextures(1, &g_shadowSamplerDummyTex);
        gl::BindTexture(GL_TEXTURE_2D, g_shadowSamplerDummyTex);
        const GLuint farDepth = 0xFFFFFFFFu;   // nothing in front = always lit
        gl::TexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, 1, 1, 0,
                       GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, &farDepth);
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LESS);
        gl::BindTexture(GL_TEXTURE_2D, (GLuint)prevTex);
        fprintf(stderr, "[shadowmap][static] shadow-sampler placeholder tex=%u created\n",
                g_shadowSamplerDummyTex);
    }
    constexpr int kColorUnit = 26, kWorldUnit = 27, kLocalUnit = 28, kDynamicUnit = 29, kStaticUnit = 30, kSceneUnit = 31;
    GLint maxUnits=0; gl::GetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS,&maxUnits);
    if (maxUnits <= kSceneUnit) { recv_block("tooFewTextureUnits", maxUnits); return; }
    // The local-light capture deliberately runs AFTER this receiver (see the
    // ordering comment in SceneRender_detour), so its map is one frame old by
    // construction. The light's view/projection is world-space, so an old map
    // is still correctly ALIGNED -- only its contents lag. Freshness is
    // therefore judged in seconds against the configured cadence
    // (local_map_still_fresh), not in frames; see that function for why the
    // old one-frame rule flickered as soon as the cadence control worked.
    // Why is the local term off? The capture is demonstrably working on Windows
    // (map with casters, buckets drawing) yet the receiver never enables it,
    // and every readout that would say why lives in the panel -- which the
    // shipping Windows build hides. Report the breakdown once.
    {
        static bool loggedLocalGate = false;
        if (!loggedLocalGate && g_localLightCapture) {
            loggedLocalGate = true;
            fprintf(stderr, "[shadowmap][local-light] receiver gate: recv=%d haveVP=%d "
                            "targetUsable=%d tex=%u vpFrame=%u serial=%u age=%d\n",
                    (int)g_localLightReceiver, (int)g_haveLocalLightVP,
                    (int)g_localLightTargetUsable, g_localLightDepthTex,
                    g_localLightVPFrame, g_shadowFrameContext.serial,
                    (int)(g_shadowFrameContext.serial - g_localLightVPFrame));
        }
    }
    // Freshness no longer gates the term directly: it starts the fade instead,
    // and the term stays enabled until that fade reaches zero.
    const float deselectFade = local_deselect_fade(local_map_still_fresh());
    g_localDeselectFadeShown = deselectFade;
    // g_haveLocalLightVP goes FALSE while a new generation is staged and only
    // returns when the next dynamic stage refills it. That is correct for the
    // ACTIVE slots -- their layers really are cleared -- but it also switched
    // off the retirement slot, whose depth was copied aside precisely so it
    // would survive. The result was a visible break the instant a light left,
    // and only then the fade: exactly the reported "breaks before going into
    // the 4th bucket and then fading out".
    //
    // The retirement layer is not part of the staged generation, so it stays
    // valid across that gap and may keep drawing on its own.
    const bool localReady = g_localLightReceiver && g_localLightTargetUsable &&
                            g_localLightDepthTex && deselectFade > 0.0005f &&
                            g_haveLocalLightVP;
    GLint oldLocalTex = 0, oldWorldTex = 0;
    gl::ActiveTexture(GL_TEXTURE0+kSceneUnit);
    gl::GetIntegerv(GL_TEXTURE_BINDING_2D,&oldSceneTex);
    // DRAIN the GL error flag before doing our own work. glGetError reports
    // the ACCUMULATED flag, so an error raised by the ENGINE (or by anything
    // else in the process) is still latched when we look -- and the draw below
    // is gated on GetError()==GL_NO_ERROR. On native Windows that gate was
    // permanently closed by somebody else's error while Proton, whose driver
    // did not raise it, sailed through: same binary, shadows on one, silence
    // on the other. Only errors WE cause should be able to stop our pass.
    {
        unsigned drained = 0;
        while (gl::GetError() != GL_NO_ERROR && ++drained < 32) {}
        static bool loggedDrain = false;
        if (drained && !loggedDrain) {
            loggedDrain = true;
            fprintf(stderr, "[shadowmap][static] cleared %u pre-existing GL error(s) "
                            "raised before the receiver ran (not ours)\n", drained);
        }
    }
    const bool copied=ensure_scene_depth_texture(vp[2],vp[3]);
    const double tCopy0 = now_seconds();
    bool depthOk = copied;
    if (copied) depthOk = capture_scene_depth(vp);
    // Same moment as the depth grab: the engine's frame is finished and our
    // composite has not run yet.
    gl::ActiveTexture(GL_TEXTURE0+kColorUnit);
    const bool colorOk = capture_scene_color(vp);
    if (depthOk && colorOk && g_sceneColorCapturedNow)
        weather_runtime::publish_framebuffer_capture(scene, g_sceneColorTex,
                                                     g_sceneDepthTex);
    // Finish on the reporting frame only, for the same reason recv does: with
    // MSAA on, this "copy" is a multisample RESOLVE of a full-screen depth
    // buffer, and submission time cannot see it. Unfinished, its GPU cost was
    // billed to whichever call blocked next -- which is recv, immediately
    // below, so the two numbers were not separable at all.
    if (g_costFinishThisFrame && gl::Finish) gl::Finish();
    g_msSceneCopy += (now_seconds() - tCopy0) * 1000.0;
    // Debug 3 proved this pass reaches the framebuffer while debug 1/2 (which
    // sit after the background test) showed nothing -- i.e. the copied scene
    // depth reads as all-far and discards every pixel. Measure it instead of
    // inferring: read the texture back and report its range. Bounded, and only
    // when a debug mode is on, because this is a full-screen CPU readback.
    // Debug-mode only: this is a full-screen CPU readback. It was briefly
    // unconditional to diagnose the blit path ("only magenta shows" => every
    // pixel discarded by the d>=0.999999 test => the capture read all-far); the
    // answer is in, so it goes back to being opt-in.
    if (copied && g_receiverDebug && gl::GetTexImage) {
        static unsigned probes = 0;
        if (probes < 3) {
            ++probes;
            const GLenum copyErr = gl::GetError();
            std::vector<uint32_t> px((size_t)vp[2]*(size_t)vp[3]);
            gl::GetTexImage(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT,GL_UNSIGNED_INT,px.data());
            uint32_t lo=0xFFFFFFFFu, hi=0; size_t nearer=0;
            for (uint32_t v : px) { if(v<lo)lo=v; if(v>hi)hi=v; if(v!=0xFFFFFFFFu)++nearer; }
            fprintf(stderr,"[shadowmap][static] scene-depth copy probe %u: %dx%d "
                           "range=%u..%u non-far=%zu/%zu copyErr=0x%x\n",
                    probes, vp[2], vp[3], lo, hi, nearer, px.size(), copyErr);
        }
    }
    gl::ActiveTexture(GL_TEXTURE0+kStaticUnit);
    gl::GetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY,&oldStaticTex);
    gl::BindTexture(GL_TEXTURE_2D_ARRAY,g_cascadeStaticTex);
    gl::ActiveTexture(GL_TEXTURE0+kDynamicUnit);
    gl::GetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY,&oldDynamicTex);
    gl::BindTexture(GL_TEXTURE_2D_ARRAY,g_cascadeDynamicTex);
    // Always bind SOMETHING valid here -- the placeholder when there is no
    // local-light map -- or the shadow sampler invalidates the program and the
    // driver drops the draw entirely (see g_shadowSamplerDummyTex).
    gl::ActiveTexture(GL_TEXTURE0+kColorUnit);
    gl::BindTexture(GL_TEXTURE_2D, colorOk ? g_sceneColorTex : g_sceneDepthTex);
    gl::ActiveTexture(GL_TEXTURE0+kLocalUnit);
    gl::GetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY,&oldLocalTex);
    // 2D ARRAY (one layer per light). The fallback is the STATIC CASCADE array,
    // which is always allocated and is a depth array of the right type -- the
    // 1x1 dummy is a 2D texture and binding it here would leave the sampler
    // incomplete, which makes the driver discard the whole draw.
    gl::BindTexture(GL_TEXTURE_2D_ARRAY, localReady ? g_localLightDepthTex
                                                    : g_cascadeStaticTex);
    // Same rule as the local-light sampler: a sampler2DShadow must ALWAYS have
    // a comparison-mode depth texture bound or the driver discards the draw.
    gl::ActiveTexture(GL_TEXTURE0+kWorldUnit);
    gl::GetIntegerv(GL_TEXTURE_BINDING_2D,&oldWorldTex);
    gl::BindTexture(GL_TEXTURE_2D, (g_staticWorldEnabled && g_staticWorldValid)
                                   ? g_staticWorldTex : g_shadowSamplerDummyTex);
    gl::ActiveTexture(GL_TEXTURE0+kSceneUnit);
    gl::BindTexture(GL_TEXTURE_2D,g_sceneDepthTex);
    // This was the last silent gate: when it was false the whole draw block --
    // uniforms, glDrawArrays, the draw counter -- was skipped with no log at
    // all, which is why the gate report kept saying "receiver-not-drawn" while
    // every reason it knew about was green.
    const GLenum preDrawErr = gl::GetError();
    if (!depthOk || preDrawErr != GL_NO_ERROR)
        recv_block(!depthOk ? "sceneDepthCaptureFailed" : "glErrorBeforeDraw",
                   (long)preDrawErr, (long)vp[2], (long)vp[3]);
    if (depthOk && preDrawErr==GL_NO_ERROR) {
        if (oldDepth) gl::Disable(GL_DEPTH_TEST);
        if (g_cascadeCompositeShadows) {
            gl::Enable(GL_BLEND);
            gl::BlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
            gl::BlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                                  GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        } else if (oldBlend) gl::Disable(GL_BLEND);
        if (oldScissor) gl::Disable(GL_SCISSOR_TEST);
        gl::DepthMask(GL_FALSE); gl::ColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
        useProgram(g_staticReceiverProgram);
        const GLint a=gl::GetUniformLocation(g_staticReceiverProgram,"nwnStaticSceneDepth");
        const GLint b=gl::GetUniformLocation(g_staticReceiverProgram,"nwnStaticLightDepth");
        const GLint c=gl::GetUniformLocation(g_staticReceiverProgram,"nwnDynamicLightDepth");
        const GLint d=gl::GetUniformLocation(g_staticReceiverProgram,"nwnStaticCameraVPInv");
        const GLint e=gl::GetUniformLocation(g_staticReceiverProgram,"nwnStaticCameraView");
        const GLint f=gl::GetUniformLocation(g_staticReceiverProgram,"nwnStaticViewportOrigin");
        const GLint g=gl::GetUniformLocation(g_staticReceiverProgram,"nwnStaticLayer");
        const GLint h=gl::GetUniformLocation(g_staticReceiverProgram,"nwnStaticDynamicEnabled");
        const GLint i=gl::GetUniformLocation(g_staticReceiverProgram,"nwnStaticCSMEnabled");
        const GLint j=gl::GetUniformLocation(g_staticReceiverProgram,"nwnStaticLightVP[0]");
        const GLint k=gl::GetUniformLocation(g_staticReceiverProgram,"nwnStaticClipFar0");
        const GLint l=gl::GetUniformLocation(g_staticReceiverProgram,"nwnStaticClipFar1");
        const GLint m=gl::GetUniformLocation(g_staticReceiverProgram,"nwnStaticClipFar2");
        const GLint n=gl::GetUniformLocation(g_staticReceiverProgram,"nwnStaticClipFar3");
        const GLint o=gl::GetUniformLocation(g_staticReceiverProgram,"nwnStaticComposite");
        const GLint p=gl::GetUniformLocation(g_staticReceiverProgram,"nwnStaticCompositeStrength");
        const GLint q=gl::GetUniformLocation(g_staticReceiverProgram,"nwnStaticReceiverBias");
        const GLint r=gl::GetUniformLocation(g_staticReceiverProgram,"nwnStaticCascadeBlend");
        const GLint s=gl::GetUniformLocation(g_staticReceiverProgram,"nwnStaticPCFRadius");
        if(a>=0) gl::Uniform1i(a,kSceneUnit);
        if(b>=0) gl::Uniform1i(b,kStaticUnit);
        if(c>=0) gl::Uniform1i(c,kDynamicUnit);
        if(d>=0) gl::UniformMatrix4fv(d,1,GL_FALSE,g_shadowFrameContext.viewProjectionInverse);
        if(e>=0) gl::UniformMatrix4fv(e,1,GL_FALSE,g_shadowFrameContext.view);
        if(f>=0 && g_realUniform3f)
            g_realUniform3f(f,(GLfloat)vp[0],(GLfloat)vp[1],0.0f);
        if(g>=0) gl::Uniform1i(g,g_cascadeLightCaptureLayer);
        if(h>=0) gl::Uniform1i(h,g_cascadeDynamicReceiver ? 1 : 0);
        if(i>=0) gl::Uniform1i(i,g_cascadeCsmStaticReceiver ? 1 : 0);
        if(j>=0) gl::UniformMatrix4fv(j,kCascadeCount,GL_FALSE,&g_cascadeMath.lightVP[0][0]);
        if(k>=0) gl::Uniform1f(k,g_cascadeMath.clipFar[0]);
        if(l>=0) gl::Uniform1f(l,g_cascadeMath.clipFar[1]);
        if(m>=0) gl::Uniform1f(m,g_cascadeMath.clipFar[2]);
        if(n>=0) gl::Uniform1f(n,g_cascadeMath.clipFar[3]);
        if(o>=0) gl::Uniform1i(o,g_cascadeCompositeShadows ? 1 : 0);
        // Area policy gates/attenuates the directional sun only.  Local-light
        // shadowing deliberately remains available in indoor/no-sun areas.
        if(p>=0) gl::Uniform1f(p,effective_area_sun_strength());
        if(q>=0) gl::Uniform1f(q,g_cascadeReceiverBias);
        if(r>=0) gl::Uniform1f(r,g_cascadeBlendWidth);
        if(s>=0) gl::Uniform1f(s,g_cascadePcfRadius);
        {   // World-anchored static map.
            poll_engine_fog();
            {   // Fog. poll_engine_fog() reads NWN's own shader fog uniform;
                // the manual sliders are only a fallback for when it finds none.
                float start = g_fogStart, end = g_fogEnd;
                bool have = (end > start);
                g_fogFromEngine = false;
                if (g_haveEngineFog) {
                    start = g_fogStartEngine; end = g_fogEndEngine;
                    have = true; g_fogFromEngine = true;
                }
                g_fogStartLive = start;
                g_fogEndLive   = end;
                const GLint fa=gl::GetUniformLocation(g_staticReceiverProgram,"nwnFogEnabled");
                const GLint fb=gl::GetUniformLocation(g_staticReceiverProgram,"nwnFogStart");
                const GLint fc=gl::GetUniformLocation(g_staticReceiverProgram,"nwnFogEnd");
                if(fa>=0) gl::Uniform1i(fa,(g_fogFade && have) ? 1 : 0);
                if(fb>=0) gl::Uniform1f(fb,start);
                if(fc>=0) gl::Uniform1f(fc,end);
                static bool logged = false;
                if (!logged && g_fogFade) {
                    logged = true;
                    fprintf(stderr, "[shadowmap][fog] shadow fog fade %s: start=%.1f end=%.1f "
                                    "(%s)\n", have ? "ON" : "off (no usable range)",
                            start, end,
                            g_fogFromEngine ? "read from the engine's fogParams uniform"
                                            : "manual; no engine fog found");
                }
            }
            const GLint dl=gl::GetUniformLocation(g_staticReceiverProgram,"nwnDynamicLayers");
            if(dl>=0) gl::Uniform1i(dl,g_cascadeDynamicLayers);
            const GLint nl=gl::GetUniformLocation(g_staticReceiverProgram,"nwnStaticNearLayers");
            if(nl>=0) gl::Uniform1i(nl,g_staticWorldEnabled ? g_staticNearCascades : kCascadeCount);
            const GLint wa=gl::GetUniformLocation(g_staticReceiverProgram,"nwnStaticWorldDepth");
            const GLint wb=gl::GetUniformLocation(g_staticReceiverProgram,"nwnStaticWorldVP");
            const GLint wc=gl::GetUniformLocation(g_staticReceiverProgram,"nwnStaticWorldEnabled");
            if(wa>=0) gl::Uniform1i(wa,kWorldUnit);
            if(wb>=0) gl::UniformMatrix4fv(wb,1,GL_FALSE,g_staticWorldVP);
            if(wc>=0) gl::Uniform1i(wc,(g_staticWorldEnabled && g_staticWorldValid) ? 1 : 0);
        }
        {   // Phase 6d local-light term.
            const GLint la=gl::GetUniformLocation(g_staticReceiverProgram,"nwnLocalLightDepth");
            const GLint lb=gl::GetUniformLocation(g_staticReceiverProgram,"nwnLocalLightVP");
            const GLint lc=gl::GetUniformLocation(g_staticReceiverProgram,"nwnLocalLightEnabled");
            const GLint ld=gl::GetUniformLocation(g_staticReceiverProgram,"nwnLocalLightPos");
            const GLint le=gl::GetUniformLocation(g_staticReceiverProgram,"nwnLocalLightRadius");
            const GLint lf=gl::GetUniformLocation(g_staticReceiverProgram,"nwnLocalLightStrength");
            const GLint lg=gl::GetUniformLocation(g_staticReceiverProgram,"nwnLocalLightBias");
            const GLint li=gl::GetUniformLocation(g_staticReceiverProgram,"nwnLocalLightLift");
            const GLint lp=gl::GetUniformLocation(g_staticReceiverProgram,"nwnLamps");
            const GLint lr=gl::GetUniformLocation(g_staticReceiverProgram,"nwnLampCount");
            const GLint lfo=gl::GetUniformLocation(g_staticReceiverProgram,"nwnLampFalloff");
            if(la>=0) gl::Uniform1i(la,kLocalUnit);
            if(lb>=0) gl::UniformMatrix4fv(lb,kLocalLightFaces,GL_FALSE,&g_localLightFaceVP[0][0]);
            {
                const GLint sp=gl::GetUniformLocation(g_staticReceiverProgram,"nwnLocalSlotPos");
                const GLint sr=gl::GetUniformLocation(g_staticReceiverProgram,"nwnLocalSlotRadius");
                const GLint sc=gl::GetUniformLocation(g_staticReceiverProgram,"nwnLocalSlots");
                const GLint sf=gl::GetUniformLocation(g_staticReceiverProgram,"nwnLocalSlotFade");
                float pos4[kLocalLightFaces*4] = {}; float rad[kLocalLightFaces] = {};
                float fade[kLocalLightFaces] = {};
                for (unsigned i = 0; i < (unsigned)kLocalLightFaces; ++i) {
                    pos4[i*4+0] = g_localLightSlotPos[i][0];
                    pos4[i*4+1] = g_localLightSlotPos[i][1];
                    pos4[i*4+2] = g_localLightSlotPos[i][2];
                    pos4[i*4+3] = 1.0f;
                    rad[i]      = g_localLightSlotRadius[i];
                    // Default 1.0: a slot that never received a fade behaves
                    // exactly as it did before this existed.
                    fade[i]     = (g_localLightSlotFade[i] > 0.0f &&
                                   g_localLightSlotFade[i] <= 1.0f)
                                ? g_localLightSlotFade[i] : 1.0f;
                    // No join multiply here: the slot's fade already carries
                    // NWN's per-light fade TIMES our level, applied where the
                    // slot list is built. Multiplying again would square it.
                }
                // Fading lights are ordinary slots now (see the join table in
                // shadow_local_lights.inc), so there is no separate retirement
                // slot to publish here.
                const unsigned slotUpper = g_localLightSlotCount;
                if (sf >= 0) {
                    if (gl::Uniform1fv) gl::Uniform1fv(sf,kLocalLightFaces,fade);
                    else if (gl::Uniform1f)
                        for (unsigned i = 0; i < (unsigned)kLocalLightFaces; ++i)
                            gl::Uniform1f(sf + (GLint)i, fade[i]);
                }
                if(sp>=0 && gl::Uniform4fv) gl::Uniform4fv(sp,kLocalLightFaces,pos4);
                if (sr >= 0) {
                    if (gl::Uniform1fv) gl::Uniform1fv(sr,kLocalLightFaces,rad);
                    else if (gl::Uniform1f) {
                        // Fallback: upload the array one element at a time.
                        // glUniform1fv may be missing where the modern entry
                        // points come from wglGetProcAddress.
                        for (int i = 0; i < kLocalLightFaces; ++i) {
                            char nm[40];
                            std::snprintf(nm, sizeof(nm), "nwnLocalSlotRadius[%d]", i);
                            const GLint li2 = gl::GetUniformLocation(g_staticReceiverProgram, nm);
                            if (li2 >= 0) gl::Uniform1f(li2, rad[i]);
                        }
                    }
                }
                if(sc>=0) gl::Uniform1i(sc,(int)slotUpper);
                const GLint se=gl::GetUniformLocation(g_staticReceiverProgram,"nwnLocalEdgeFade");
                const GLint ss=gl::GetUniformLocation(g_staticReceiverProgram,"nwnLocalSoft");
                if(se>=0) gl::Uniform1f(se,g_localLightEdgeFade);
                const GLint sfo=gl::GetUniformLocation(g_staticReceiverProgram,"nwnLocalFalloff");
                if(sfo>=0) gl::Uniform1f(sfo,g_localLightFalloff);
                const GLint slf=gl::GetUniformLocation(g_staticReceiverProgram,"nwnLocalLift");
                if(slf>=0) gl::Uniform1f(slf,g_localLightHeight);
                if(ss>=0) gl::Uniform1f(ss,g_localLightSoft);
                const GLint sn=gl::GetUniformLocation(g_staticReceiverProgram,"nwnLocalNormalBias");
                if(sn>=0) gl::Uniform1f(sn,g_localLightNormalBias);
                const GLint sm=gl::GetUniformLocation(g_staticReceiverProgram,"nwnLocalMinSep");
                if(sm>=0) gl::Uniform1f(sm,g_localLightMinSep);
                const GLint st2=gl::GetUniformLocation(g_staticReceiverProgram,"nwnLocalTanHalfFov");
                // THE FOV ACTUALLY USED, which is per-FACE. g_localLightFovDeg is
                // the ONE-FACE value (170); four-face mode captures at 110. Feeding
                // the one-face number in 4-face mode overestimates texel size by
                // ~8x (tan 85 vs tan 55), which inflates the normal offset and bias
                // and wrecks character self-shadowing.
                if(st2>=0) gl::Uniform1f(st2,(float)std::tan(
                        local_face_fov_deg(local_source_faces()) * 3.14159265f / 360.0f));
                // Did the per-slot uniforms actually ARRIVE? A -1 location or a
                // zero count makes the receiver's loop run zero times and
                // return "not shadowed" regardless of what the map holds --
                // indistinguishable from a shadowing bug, and the map is known
                // good here (a caster at 0.54 units).
                static bool loggedSlots = false;
                if (!loggedSlots && localReady) {
                    loggedSlots = true;
                    // The FUNCTION POINTERS matter as much as the locations:
                    // Uniform1fv/4fv resolve through wglGetProcAddress on
                    // Windows, and a null one silently uploads nothing -- every
                    // slot radius would read 0 and be skipped, which looks
                    // exactly like "local shadows do not work" with every other
                    // readout healthy.
                    fprintf(stderr, "[shadowmap][local-light] receiver uniforms: pos=%d radius=%d "
                                    "count=%d -> slots=%u  VP[0] loc=%d  depthUnit=%d\n"
                                    "[shadowmap][local-light]   fn: Uniform4fv=%p Uniform1fv=%p "
                                    "Uniform1i=%p UniformMatrix4fv=%p Uniform3f=%p\n",
                            sp, sr, sc, g_localLightSlotCount, lb, kLocalUnit,
                            (void*)gl::Uniform4fv, (void*)gl::Uniform1fv,
                            (void*)gl::Uniform1i, (void*)gl::UniformMatrix4fv,
                            (void*)g_realUniform3f);
                    for (unsigned i = 0; i < g_localLightSlotCount && i < 4; ++i)
                        fprintf(stderr, "[shadowmap][local-light]   slot %u pos=(%.2f,%.2f,%.2f) r=%.2f\n",
                                i, g_localLightSlotPos[i][0], g_localLightSlotPos[i][1],
                                g_localLightSlotPos[i][2], g_localLightSlotRadius[i]);
                }
            }
            if(lc>=0) gl::Uniform1i(lc,localReady ? 1 : 0);
            if(ld>=0 && g_realUniform3f)
                g_realUniform3f(ld,g_localLightWorldPos[0],g_localLightWorldPos[1],
                                g_localLightWorldPos[2]);
            if(le>=0) gl::Uniform1f(le,g_localLightWorldRadius);
            // NOT scaled by the engine's `shadowalpha` global. That was tried
            // on 2026-08-13 and removed the same day: multiplying by it killed
            // local shadows outright, because the value is NOT a stable
            // per-frame 0..1 opacity. Sampled once per Scene::Render it reads
            // 0.5 at startup, 1.0 sometimes, and 0.025 while two lights are
            // active -- so it is scratch state the stencil renderer writes
            // during its own pass, not a frame-constant the receiver can read.
            // Reading it at an arbitrary point in the frame samples whatever
            // the last shadow draw happened to leave there.
            // Engine-driven fade-out (see update_engine_shadow_fade). Holds at
            // 1.0 outside a transition, so nothing changes in normal play.
            if(lf>=0) gl::Uniform1f(lf,g_localLightStrength * g_engineFadeLevel * deselectFade);
            if(lg>=0) gl::Uniform1f(lg,g_localLightBias);
            // THE off switch for "Lights lift sun shadow". Zeroing the lift
            // amount is what actually disables it: the shader's whole lift
            // block is gated on `nwnLocalLightLift > 0.0`, so 0 stops it dead
            // regardless of anything else.
            //
            // Gating the LAMP COUNT instead (the obvious-looking fix) worked on
            // Linux and did nothing on Windows, so it is not the reliable lever
            // -- this is. The count is still zeroed below as well; both are
            // cheap and the shader needs only one of them to be right.
            {
                const float liftNow = g_lampLiftEnabled ? g_localLightLift : 0.0f;
                if(li>=0) gl::Uniform1f(li,liftNow);
                // Diagnosing the Windows toggle. Prints only on change, and
                // reports the LOCATION too: a -1 there means the uniform was
                // optimised out or the lookup failed, in which case the upload
                // never happens and the checkbox cannot possibly work.
                static int   lastLoc = -2;
                static float lastVal = -1.0f;
                static bool  lastEn  = false;
                if (li != lastLoc || std::fabs(liftNow - lastVal) > 1e-6f ||
                    g_lampLiftEnabled != lastEn) {
                    lastLoc = li; lastVal = liftNow; lastEn = g_lampLiftEnabled;
                    fprintf(stderr, "[shadowmap][lift] checkbox=%d slider=%.3f "
                                    "uploaded=%.3f loc=%d\n",
                            g_lampLiftEnabled ? 1 : 0, g_localLightLift, liftNow, li);
                }
            }
            {
                // A bounded lamp set; w carries range SQUARED.
                unsigned order[kLampPool];
                for (unsigned i = 0; i < g_lampLightCount; ++i) order[i] = i;
                Vec3f eye = g_shadowFrameContext.eye;
#ifdef _WIN32
                // The Scene::Render eye argument is not the world-camera eye
                // on Windows.  The frozen inverse view is authoritative and
                // already drives the working cascade fit; its translation is
                // the actual camera position.  Sun lift follows the nearest
                // ordinary SetLightGL census entries, never the unrelated
                // GetShadowLights(mode) shadow-caster lists.
                eye.x = g_shadowFrameContext.viewInverse[12];
                eye.y = g_shadowFrameContext.viewInverse[13];
                eye.z = g_shadowFrameContext.viewInverse[14];
#endif
                auto d2c = [&](unsigned i) {
                    const float dx = g_lampLights[i].pos[0]-eye.x;
                    const float dy = g_lampLights[i].pos[1]-eye.y;
                    const float dz = g_lampLights[i].pos[2]-eye.z;
                    return dx*dx+dy*dy+dz*dz;
                };
                for (unsigned a2 = 0; a2 + 1 < g_lampLightCount; ++a2)
                    for (unsigned b2 = a2 + 1; b2 < g_lampLightCount; ++b2) {
                        if (d2c(order[b2]) < d2c(order[a2]))
                            std::swap(order[a2], order[b2]);
                    }
                float packed[kMaxLampLights*4] = {};
                const unsigned cap = (unsigned)std::max(1, std::min(lamp_upload_max(),
                                                                    (int)kMaxLampLights));
                const unsigned n = std::min(g_lampLightCount, cap);
                for (unsigned i = 0; i < n; ++i) {
                    const LampEntry& e = g_lampLights[order[i]];
                    packed[i*4+0] = e.pos[0];
                    packed[i*4+1] = e.pos[1];
                    packed[i*4+2] = e.pos[2];
                    packed[i*4+3] = e.radius * e.radius;
                }
                static unsigned lastN = 0xFFFFFFFFu;
                if (n != lastN) {
                    lastN = n;
                    fprintf(stderr, "[shadowmap][lamps] %u light(s) feeding the sun-shadow "
                                    "lift (cap %u)\n", n, cap);
                }
                if(lp>=0 && gl::Uniform4fv) gl::Uniform4fv(lp,(GLsizei)kMaxLampLights,packed);
                // Zero lamps IS the off switch: the shader's lift loop
                // simply never runs. No branch added to the hot path.
                if(lr>=0) gl::Uniform1i(lr,(int)n);
                if(lfo>=0) gl::Uniform2f(lfo,g_lightMaxIntensityInv,g_lightFalloffFactor);
            }

            // SELF-ILLUMINATION GUARD. kColorUnit already holds the engine's
            // finished colour; nothing sampled it until now. g_liftThreshold was
            // exposed in the panel and read by NOTHING, so it becomes this
            // control's value rather than adding a fourth slider.
            const GLint sic=gl::GetUniformLocation(g_staticReceiverProgram,"nwnStaticSceneColor");
            const GLint sig=gl::GetUniformLocation(g_staticReceiverProgram,"nwnSelfIllumGuard");
            if(sic>=0) gl::Uniform1i(sic,kColorUnit);
            // 1.0 disables it: the shader skips the whole block, and a scene with
            // no colour capture cannot then darken wrongly.
            if(sig>=0) gl::Uniform1f(sig,colorOk ? g_liftThreshold : 1.0f);
            const GLint lh=gl::GetUniformLocation(g_staticReceiverProgram,"nwnReceiverDebug");
            if(lh>=0) gl::Uniform1i(lh,g_receiverDebug);
        }
        if (!g_nextDrawArrays)
            g_nextDrawArrays=(void(*)(GLenum,GLint,GLsizei))dlsym(RTLD_NEXT,"glDrawArrays");
        const bool priorOurPass=g_inOurPass; g_inOurPass=true;
        if (g_nextDrawArrays) g_nextDrawArrays(0x0004,0,3);
        g_inOurPass=priorOurPass;
        // "receiver drew" previously meant only "we called glDrawArrays", which
        // is NOT the same as "anything rendered": an invalid sampler binding
        // makes the driver discard the draw and raise GL_INVALID_OPERATION.
        // That cost a lot of debugging, so check it and say so.
        {
            const GLenum drawErr = gl::GetError();
            static unsigned errReports = 0;
            if (drawErr != GL_NO_ERROR && errReports < 8) {
                ++errReports;
                fprintf(stderr,"[shadowmap][static] receiver draw raised GL error 0x%x "
                               "-- the draw was DISCARDED, nothing rendered\n", drawErr);
            }
        }
        g_staticReceiverFrame=g_shadowFrameContext.serial;
        ++g_staticReceiverDraws;
        if (g_costReport &&
            (g_staticReceiverDraws == 1 || (g_staticReceiverDraws % 300) == 0)) {
            unsigned reportedStaticDraws = g_cascadeStaticCaptureDraws;
            unsigned reportedDynamicDraws = g_cascadeDynamicCaptureDraws;
            unsigned reportedDynamicBodyDraws = 0;
            unsigned reportedDynamicAlphaDraws = 0;
            if (g_cascadeCsmStaticReceiver) {
                reportedStaticDraws = 0;
                reportedDynamicDraws = 0;
                for (int layer = 0; layer < kCascadeCount; ++layer) {
                    reportedStaticDraws += g_cascadeStaticCaptureDrawsLayer[layer];
                    reportedDynamicDraws += g_cascadeDynamicCaptureDrawsLayer[layer];
                    reportedDynamicBodyDraws += g_cascadeDynamicBodyCaptureDrawsLayer[layer];
                    reportedDynamicAlphaDraws += g_cascadeDynamicAlphaCaptureDrawsLayer[layer];
                }
            }
            char buckets[64];
            std::snprintf(buckets, sizeof(buckets), "%d", g_cascadeLightCaptureBucket);
            if (g_cascadeDynamicReceiver)
                std::snprintf(buckets + strlen(buckets), sizeof(buckets) - strlen(buckets),
                              "+%d", g_cascadeDynamicBucket);
            if (g_cascadeAlphaReceiver)
                std::snprintf(buckets + strlen(buckets), sizeof(buckets) - strlen(buckets),
                              "+%d", g_cascadeAlphaBucket);
            if (g_cascadeStaticAlphaReceiver)
                std::snprintf(buckets + strlen(buckets), sizeof(buckets) - strlen(buckets),
                              "+static-alpha:%d", g_cascadeStaticAlphaBucket);
            fprintf(stderr,"[shadowmap][static] receiver drew frame=%u static-draws=%u dynamic-draws=%u "
                           "(body=%u alpha=%u) | ms recv=%.2f world=%.2f replay=%.2f copy=%.2f "
                           "enginedraws=%u worldmap=%d^2 cascademap=%d^2 "
                           "| %s output=%s strength=%.2f bias=%.5f blend=%.3f pcf=%.2f buckets=%s\n",
                    g_staticReceiverFrame,reportedStaticDraws,
                    reportedDynamicDraws, reportedDynamicBodyDraws,
                    reportedDynamicAlphaDraws,
                    g_msReceiverShown, g_msWorldMapShown, g_msReplayShown,
                    g_msSceneCopyShown, g_frameDrawCallsShown,
                    g_staticWorldSize, g_size,
                    g_cascadeCsmStaticReceiver ? "csm=depth-selected" : "csm=off",
                    g_cascadeCompositeShadows ? "dark" : "red",
                    g_cascadeCompositeStrength,
                    g_cascadeReceiverBias,
                    g_cascadeBlendWidth,
                    g_cascadePcfRadius,
                    buckets);
        }
    }
    useProgram((GLuint)oldProg);
    gl::ActiveTexture(GL_TEXTURE0+kDynamicUnit);
    gl::BindTexture(GL_TEXTURE_2D_ARRAY,(GLuint)oldDynamicTex);
    gl::ActiveTexture(GL_TEXTURE0+kStaticUnit);
    gl::BindTexture(GL_TEXTURE_2D_ARRAY,(GLuint)oldStaticTex);
    gl::ActiveTexture(GL_TEXTURE0+kLocalUnit);
    gl::BindTexture(GL_TEXTURE_2D_ARRAY,(GLuint)oldLocalTex);   // array now, see the bind above
    gl::ActiveTexture(GL_TEXTURE0+kWorldUnit);
    gl::BindTexture(GL_TEXTURE_2D,(GLuint)oldWorldTex);
    gl::ActiveTexture(GL_TEXTURE0+kSceneUnit);
    gl::BindTexture(GL_TEXTURE_2D,(GLuint)oldSceneTex);
    gl::ActiveTexture((GLenum)oldActive);
    if(oldDepth) gl::Enable(GL_DEPTH_TEST); else gl::Disable(GL_DEPTH_TEST);
    if(oldBlend) gl::Enable(GL_BLEND); else gl::Disable(GL_BLEND);
    gl::BlendEquationSeparate((GLenum)oldBlendEquationRGB, (GLenum)oldBlendEquationAlpha);
    gl::BlendFuncSeparate((GLenum)oldBlendSrcRGB, (GLenum)oldBlendDstRGB,
                          (GLenum)oldBlendSrcAlpha, (GLenum)oldBlendDstAlpha);
    if(oldScissor) gl::Enable(GL_SCISSOR_TEST); else gl::Disable(GL_SCISSOR_TEST);
    gl::DepthMask(oldMask); gl::ColorMask(oldColor[0],oldColor[1],oldColor[2],oldColor[3]);
}

// GLEW resolves its pointers during init, well after our constructor runs, so
// patch lazily on the first frame instead.
// ===========================================================================
//  Overlay input capture: swallow the events ImGui is using
// ===========================================================================
// SDL_PollEvent in this binary is a thunk:
//     push rbp; mov rbp,rsp; pop rbp; jmp QWORD PTR [rip+rel32]
// so the real implementation is reached through a writable function pointer.
// Patching that pointer is the same technique the GLEW entry points use, and
// is strictly better than detouring the thunk: nothing is relocated, the
// original is a plain pointer we keep, and undoing it is one store.
//
// SDL's event structs are not needed. Every SDL_Event begins with a Uint32
// `type` at offset 0 (stable ABI), and SDL_MouseWheelEvent keeps x/y at
// offsets 16/20, which is all this hook reads.
using SdlPollEvent_t = int (*)(void*);
static SdlPollEvent_t g_realSdlPollEvent = nullptr;
static void**         g_sdlPollEventSlot = nullptr;
static bool           g_overlayInputCapture = true;   // NWN_SHADOWMAP_OVERLAY_NO_INPUT_CAPTURE=1 disables
static unsigned long  g_overlaySwallowedEvents = 0;

enum : uint32_t {
    kSdlKeyDown = 0x300, kSdlKeyUp = 0x301, kSdlTextEditing = 0x302, kSdlTextInput = 0x303,
    kSdlMouseMotion = 0x400, kSdlMouseButtonDown = 0x401,
    kSdlMouseButtonUp = 0x402, kSdlMouseWheel = 0x403,
};

extern "C" int nwn_sdl_poll_event(void* event) {
    if (!g_realSdlPollEvent) return 0;
    for (;;) {
        const int got = g_realSdlPollEvent(event);
        if (!got || !event) return got;

        uint32_t type = 0;
        std::memcpy(&type, event, sizeof(type));

        // Proves the game actually pumps events through THIS entry point --
        // it could have used SDL_PeepEvents/SDL_WaitEvent instead, in which
        // case the patch installs cleanly and silently never fires.
        static bool sawFirst = false;
        if (!sawFirst) {
            sawFirst = true;
            fprintf(stderr, "[shadowmap][overlay] SDL_PollEvent hook is live "
                            "(first event type=0x%x)\n", type);
        }

        const bool isMouse = (type == kSdlMouseMotion || type == kSdlMouseButtonDown ||
                              type == kSdlMouseButtonUp || type == kSdlMouseWheel);
        const bool isKey   = (type == kSdlKeyDown || type == kSdlKeyUp ||
                              type == kSdlTextInput || type == kSdlTextEditing);

        // The wheel cannot be recovered by polling SDL_GetMouseState, so hand
        // it to ImGui here regardless of whether we go on to swallow it.
        if (type == kSdlMouseWheel) {
            int32_t wx = 0, wy = 0;
            std::memcpy(&wx, (const char*)event + 16, sizeof(wx));
            std::memcpy(&wy, (const char*)event + 20, sizeof(wy));
            nwn_overlay_add_mouse_wheel((float)wx, (float)wy);
        }

        if (g_overlayInputCapture) {
            if ((isMouse && nwn_overlay_wants_mouse()) ||
                (isKey && nwn_overlay_wants_keyboard())) {
                if (++g_overlaySwallowedEvents == 1)
                    fprintf(stderr, "[shadowmap][overlay] input capture active: first event "
                                    "(type=0x%x) consumed by the panel\n", type);
                continue;      // consume it; the game never sees this event
            }
        }
        return got;
    }
}

// The slot lives in writable data, but take the RELRO case seriously rather
// than assuming: make the page writable before storing, and verify the thunk
// really has the expected shape before trusting the decoded address.
#ifdef _WIN32
static subhook_t g_hookSdlPoll = nullptr;   // Windows detours the real export
#endif
static void install_sdl_pollevent_patch() {
    static bool tried = false;
    if (tried) return;
    tried = true;
#ifdef _WIN32
    // On Windows SDL_PollEvent is a REAL exported function, not the five-byte
    // jump thunk the Linux build patches, so there is no pointer to swap --
    // detour it instead.
    if (!eng::SdlPollEventThunk) {
        fprintf(stderr, "[shadowmap][overlay] SDL_PollEvent not resolved; input capture off\n");
        return;
    }
    g_realSdlPollEvent = (SdlPollEvent_t)eng::SdlPollEventThunk;
    g_hookSdlPoll = subhook_new(eng::SdlPollEventThunk,
                                (void*)nwn_sdl_poll_event, SUBHOOK_64BIT_OFFSET);
    if (!g_hookSdlPoll || subhook_install(g_hookSdlPoll) != 0) {
        fprintf(stderr, "[shadowmap][overlay] SDL_PollEvent detour FAILED; input capture off\n");
        g_realSdlPollEvent = nullptr;
        return;
    }
    // A NULL trampoline is fatal here, not merely degraded. Falling back to the
    // ORIGINAL address (as the code briefly did) means calling a function whose
    // prologue we just patched, so it jumps straight back into this detour:
    // unbounded recursion -> stack overflow -> crash on the first event pump.
    // That is exactly the subhook hazard AGENTS.md documents; the Linux side
    // avoids it because SDL_PollEvent there is a jump thunk we patch by pointer
    // instead of detouring. Refuse the hook rather than ship a crash: the only
    // loss is overlay input capture, and the panel still works.
    void* tramp = subhook_get_trampoline(g_hookSdlPoll);
    if (!tramp) {
        fprintf(stderr, "[shadowmap][overlay] SDL_PollEvent has no trampoline; "
                        "input capture DISABLED (calling the original would recurse "
                        "into this detour and overflow the stack)\n");
        subhook_remove(g_hookSdlPoll);
        subhook_free(g_hookSdlPoll);
        g_hookSdlPoll = nullptr;
        g_realSdlPollEvent = nullptr;
        return;
    }
    g_realSdlPollEvent = (SdlPollEvent_t)tramp;
    fprintf(stderr, "[shadowmap][overlay] SDL_PollEvent detoured (trampoline=%p)\n",
            (void*)g_realSdlPollEvent);
    return;
#else
    if (!eng::SdlPollEventThunk) {
        fprintf(stderr, "[shadowmap][overlay] SDL_PollEvent not resolved; "
                        "input capture unavailable (panel still works)\n");
        return;
    }
    const uint8_t* fn = (const uint8_t*)eng::SdlPollEventThunk;
    // push rbp(55) mov rbp,rsp(48 89 e5) pop rbp(5d) then ff 25 <rel32>
    if (!(fn[0] == 0x55 && fn[1] == 0x48 && fn[2] == 0x89 && fn[3] == 0xe5 &&
          fn[4] == 0x5d && fn[5] == 0xff && fn[6] == 0x25)) {
        fprintf(stderr, "[shadowmap][overlay] SDL_PollEvent is not the expected jump thunk "
                        "(%02x %02x %02x %02x %02x %02x %02x); not patching\n",
                fn[0], fn[1], fn[2], fn[3], fn[4], fn[5], fn[6]);
        return;
    }
    int32_t rel = 0;
    std::memcpy(&rel, fn + 7, sizeof(rel));
    void** slot = (void**)(fn + 11 + rel);       // RIP is the next instruction

    const long pageSize = sysconf(_SC_PAGESIZE);
    void* page = (void*)((uintptr_t)slot & ~(uintptr_t)(pageSize - 1));
    if (mprotect(page, (size_t)pageSize, PROT_READ | PROT_WRITE) != 0) {
        fprintf(stderr, "[shadowmap][overlay] mprotect failed for SDL_PollEvent slot %p\n",
                (void*)slot);
        return;
    }
    if (!*slot) { tried = false; return; }        // SDL not wired up yet; retry later

    g_sdlPollEventSlot = slot;
    g_realSdlPollEvent = (SdlPollEvent_t)*slot;
    *slot = (void*)nwn_sdl_poll_event;
    fprintf(stderr, "[shadowmap][overlay] SDL_PollEvent patched (slot=%p real=%p) -- "
                    "the panel now consumes the input it uses\n",
            (void*)slot, (void*)g_realSdlPollEvent);
#endif
}

#ifdef _WIN32
// Windows replacement for the Linux __glew* pointer patch: nwmain.exe exports
// no GLEW pointers, so instead we register our wrappers with the
// wglGetProcAddress hook and the engine receives them as it resolves GL. The
// two GL 1.1 draw calls are imported from OPENGL32.dll and are IAT-patched.
// Both must happen before the engine resolves anything, which the version.dll
// proxy guarantees (it loads at process start).
static void install_windows_gl_interposition() {
    static bool done = false;
    if (done) return;
    done = true;

    nwn_win_register_gl_wrapper("glUseProgram",        (void*)my_use_program,        (void**)&g_realUseProgram);
    nwn_win_register_gl_wrapper("glUniformMatrix4fv",  (void*)my_uniform_matrix4fv,  (void**)&g_realUniformMatrix4fv);
    nwn_win_register_gl_wrapper("glUniform3f",         (void*)my_uniform3f,          (void**)&g_realUniform3f);
    nwn_win_register_gl_wrapper("glUniform3fv",        (void*)my_uniform3fv,         (void**)&g_realUniform3fv);
    nwn_win_register_gl_wrapper("glShaderSource",      (void*)my_shader_source,      (void**)&g_realShaderSource);
    nwn_win_register_gl_wrapper("glDrawRangeElements", (void*)my_draw_range_elements,(void**)&g_realDrawRangeElements);
    nwn_win_register_gl_wrapper("glMultiDrawArrays",   (void*)my_multi_draw_arrays,  (void**)&g_realMultiDrawArrays);
    nwn_win_register_gl_wrapper("glMultiDrawElements", (void*)my_multi_draw_elements,(void**)&g_realMultiDrawElements);
    nwn_win_register_gl_wrapper("glDrawElementsBaseVertex",
                                (void*)my_draw_elements_base_vertex, (void**)&g_realDrawElementsBaseVertex);
    nwn_win_register_gl_wrapper("glDrawRangeElementsBaseVertex",
                                (void*)my_draw_range_elements_base_vertex, (void**)&g_realDrawRangeElementsBaseVertex);
    nwn_win_register_gl_wrapper("glDrawElementsInstanced",
                                (void*)my_draw_elements_instanced, (void**)&g_realDrawElementsInstanced);
    nwn_win_register_gl_wrapper("glDrawElementsInstancedBaseVertex",
                                (void*)my_draw_elements_instanced_base_vertex,
                                (void**)&g_realDrawElementsInstancedBaseVertex);
    nwn_win_install_gl_hooks();

    if (nwn_win_patch_iat("OPENGL32.dll", "glDrawElements",
                          (void*)nwn_glDrawElements, (void**)&g_nextDrawElements))
        fprintf(stderr, "[shadowmap][win] glDrawElements IAT hook installed\n");
    else
        fprintf(stderr, "[shadowmap][win] WARNING: glDrawElements IAT patch failed\n");
    if (nwn_win_patch_iat("OPENGL32.dll", "glDrawArrays",
                          (void*)nwn_glDrawArrays, (void**)&g_nextDrawArrays))
        fprintf(stderr, "[shadowmap][win] glDrawArrays IAT hook installed\n");
    else
        fprintf(stderr, "[shadowmap][win] WARNING: glDrawArrays IAT patch failed\n");
}
#endif

static void install_useprogram_patch() {
    static bool tried = false;
    if (tried || !eng::GlewUseProgram) return;
    tried = true;
    void* cur = *eng::GlewUseProgram;
    if (!cur) { tried = false; return; }          // GLEW not initialised yet
    fprintf(stderr, "[shadowmap] __glewUseProgram resolved at %p -> %p\n",
            (void*)eng::GlewUseProgram, cur);
    g_realUseProgram   = (UseProgram_t)cur;
    *eng::GlewUseProgram = (void*)my_use_program;

    GLint maxUnits = 0;
    if (gl::GetIntegerv) gl::GetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &maxUnits);
    fprintf(stderr, "[shadowmap] glUseProgram patched (real=%p). "
                    "GL_MAX_TEXTURE_IMAGE_UNITS=%d, using unit %d.\n",
            cur, maxUnits, SHADOW_UNIT);
    if (maxUnits <= SHADOW_UNIT)
        fprintf(stderr, "[shadowmap] WARNING: unit %d exceeds this GPU's limit!\n",
                SHADOW_UNIT);
}

static void install_uniform_matrix_patch() {
    static bool tried = false;
    if (tried || !eng::GlewUniformMatrix4fv) return;
    tried = true;
    void* cur = *eng::GlewUniformMatrix4fv;
    if (!cur) { tried = false; return; }          // GLEW not initialised yet
    g_realUniformMatrix4fv = (UniformMatrix4fv_t)cur;
    *eng::GlewUniformMatrix4fv = (void*)my_uniform_matrix4fv;
    if (g_traceEnabled)
        fprintf(stderr, "[shadowmap][mat] hooked glUniformMatrix4fv (read-only)\n");
    else
        fprintf(stderr, "[shadowmap] glUniformMatrix4fv patched (real=%p): "
                        "light replay will replace stale camera transforms\n", cur);
}

// Trace-only light-direction discovery.  Both scalar and vector variants are
// used by OpenGL programs, so observe both GLEW indirections.  The detours only
// forward values after a bounded log entry.
static void install_light_vector_trace_patch() {
    if (!g_lightVectorTrace) return;
    static bool tried3f = false, tried3fv = false;
    if (!tried3f && eng::GlewUniform3f) {
        tried3f = true;
        void* cur = *eng::GlewUniform3f;
        if (!cur) tried3f = false;
        else {
            g_realUniform3f = (Uniform3f_t)cur;
            *eng::GlewUniform3f = (void*)my_uniform3f;
            fprintf(stderr, "[shadowmap][lightvec] hooked glUniform3f (read-only)\n");
        }
    }
    if (!tried3fv && eng::GlewUniform3fv) {
        tried3fv = true;
        void* cur = *eng::GlewUniform3fv;
        if (!cur) tried3fv = false;
        else {
            g_realUniform3fv = (Uniform3fv_t)cur;
            *eng::GlewUniform3fv = (void*)my_uniform3fv;
            fprintf(stderr, "[shadowmap][lightvec] hooked glUniform3fv (read-only)\n");
        }
    }
}

// Unlike glUseProgram, these are optional GLEW draw-dispatch slots. Patch only
// the populated ones and retry on later startup frames until GLEW is ready.
// Every wrapper forwards the original call verbatim after recording it.
static void install_geometry_trace_patch() {
    if (!g_cascadeGeometryTrace && !nwn_oit_needs_draw_observer() &&
        !weather_needs_draw_observer()) return;
    static bool rangeDone = false, arraysDone = false, elementsDone = false;
    auto install = [](void** slot, void* replacement, void** real,
                      bool& done, const char* label) {
        if (done || !slot) return;
        void* current = *slot;
        if (!current) return;
        *real = current;
        *slot = replacement;
        done = true;
        fprintf(stderr, "[shadowmap][geom] hooked %s through GLEW (read-only)\n", label);
    };
    install(eng::GlewDrawRangeElements, (void*)my_draw_range_elements,
            (void**)&g_realDrawRangeElements, rangeDone, "glDrawRangeElements");
    install(eng::GlewMultiDrawArrays, (void*)my_multi_draw_arrays,
            (void**)&g_realMultiDrawArrays, arraysDone, "glMultiDrawArrays");
    install(eng::GlewMultiDrawElements, (void*)my_multi_draw_elements,
            (void**)&g_realMultiDrawElements, elementsDone, "glMultiDrawElements");
}

static void install_shadersource_patch() {
    static bool tried = false;
    if (tried || !eng::GlewShaderSource) return;
    tried = true;
    void* cur = *eng::GlewShaderSource;
    if (!cur) { tried = false; return; }
    g_realShaderSource = (ShaderSource_t)cur;
    *eng::GlewShaderSource = (void*)my_shader_source;
    fprintf(stderr, "[shadowmap] glShaderSource patched for runtime injection%s\n",
            g_injectTest ? " (marker test enabled)" : "");
}

// Bind the depth texture where every shader can reach it, restoring the
// engine's active texture unit afterwards.
// tex=0 detaches. The depth texture MUST NOT be left bound to a sampler unit
// while it is also the depth attachment of the framebuffer we are rendering
// into -- that is a framebuffer feedback loop, undefined per spec, and NVIDIA
// resolves it by handing back the clear value. Symptom: the shader samples
// 1.0 everywhere even though the sampler unit and binding are both verified
// correct and glGetTexImage on the same object shows real geometry.
static void bind_shadow_unit(GLuint tex) {
    if (!g_glBound) return;   // gl:: not resolved yet (Windows: no context at attach)
    if (!gl::ActiveTexture) return;
    GLint prevActive = GL_TEXTURE0;
    gl::GetIntegerv(GL_ACTIVE_TEXTURE, &prevActive);
    gl::ActiveTexture(GL_TEXTURE0 + SHADOW_UNIT);

    // Report what was ALREADY sitting on our unit. If this is 0 every frame,
    // something is unbinding it between frames and the shader samples nothing --
    // which is exactly the symptom (dRef reads 1.0 while glGetTexImage on the
    // same texture object shows real geometry).
    gl::BindTexture(GL_TEXTURE_2D, tex);
    gl::ActiveTexture((GLenum)prevActive);
}

static void bind_shadow_texture() {
    if (!g_glBound) return;   // gl:: not resolved yet (Windows: no context at attach)
    if (use_cpu_bridge()) bind_shadow_unit(g_depthMirrorTex);
    else if (use_gpu_copy_bridge()) bind_shadow_unit(g_gpuStepTex);
    else if (shadow_getenv("NWN_SHADOWMAP_COLORCAST_TEST")) bind_shadow_unit(g_lightColorTex);
    else bind_shadow_unit(shadow_getenv("NWN_SHADOWMAP_GPU_TEST") ? g_gpuMirrorTex : g_depthMirrorTex);
}

static float* mtx_entry(int idx) {
    if (!eng::MtxStack || idx < 0 || idx >= MTX_ENTRIES) return nullptr;
    return (float*)((uint8_t*)eng::MtxStack + (size_t)idx * MTX_STRIDE + MTX_MAT_OFF);
}

// Standard OpenGL lookAt, column-major -- the exact form the engine itself uses.
static void build_lookat(const Vec3f& eye, const float f[3], float m[16]) {
    float up[3] = { 0.0f, 0.0f, 1.0f };                 // NWN world is Z-up
    if (std::fabs(f[2]) > 0.999f) { up[0] = 0.0f; up[1] = 1.0f; up[2] = 0.0f; }

    float s3[3] = { f[1]*up[2] - f[2]*up[1],
                    f[2]*up[0] - f[0]*up[2],
                    f[0]*up[1] - f[1]*up[0] };
    v_normalize(s3);
    float u3[3] = { s3[1]*f[2] - s3[2]*f[1],
                    s3[2]*f[0] - s3[0]*f[2],
                    s3[0]*f[1] - s3[1]*f[0] };

    m[0]=s3[0]; m[1]=u3[0]; m[2]=-f[0]; m[3]=0.0f;
    m[4]=s3[1]; m[5]=u3[1]; m[6]=-f[1]; m[7]=0.0f;
    m[8]=s3[2]; m[9]=u3[2]; m[10]=-f[2]; m[11]=0.0f;
    m[12] = -(s3[0]*eye.x + s3[1]*eye.y + s3[2]*eye.z);
    m[13] = -(u3[0]*eye.x + u3[1]*eye.y + u3[2]*eye.z);
    m[14] =  (f[0]*eye.x + f[1]*eye.y + f[2]*eye.z);
    m[15] = 1.0f;
}

// The true world camera position, recovered from the view matrix the engine is
// actually using: V = R*T(-eye) with rows of R = (s, u, -f), so eye = -R^T * t.
// This replaces the captured SetViewTransform argument, which is unreliable --
// that function fires several times a frame (skybox / world / UI) and latching
// "the last one" sometimes yields nonsense like (0,0,767).
// Verified against a live dump: recovers (25.440, 12.389, 14.389) exactly.
static bool eye_from_view(Vec3f& out) {
    const float* m = mtx_entry(MTX_VIEW);
    if (!m) return false;
    const float t0 = m[12], t1 = m[13], t2 = m[14];
    out.x = -(m[0] * t0 + m[1] * t1 + m[2]  * t2);
    out.y = -(m[4] * t0 + m[5] * t1 + m[6]  * t2);
    out.z = -(m[8] * t0 + m[9] * t1 + m[10] * t2);
    out.w = g_camPos.w;
    return true;
}
