// ---------------------------------------------------------------------------
// Selectable quarter-, half-, or full-resolution volumetric sun shafts.
//
// Each pixel reconstructs its world-space camera ray from the completed scene
// depth, marches that ray through the directional cascade volume, and gathers
// only samples visible to the sun.  The cascade maps already contain opaque,
// dynamic, and alpha-tested casters, so foliage holes shape the lit volume in
// world space.  Projected sun position is diagnostic-only; normal rendering
// never clamps a source to a screen edge or performs a radial image blur.
// ---------------------------------------------------------------------------
static GLuint g_godrayMaskProgram = 0;
static GLuint g_godrayScatterProgram = 0;
static GLuint g_godrayBlurProgram = 0;
static GLuint g_godrayCompositeProgram = 0;
static GLuint g_godrayMaskTexture = 0;
static GLuint g_godrayScatterTexture = 0;
static GLuint g_godrayFilteredTexture = 0;
static GLuint g_godrayFbo = 0;
static int g_godrayWidth = 0;
static int g_godrayHeight = 0;

static void godrays_blocked(const char* reason) {
    // Scene::Render is also called for UI/portrait scenes.  A one-entry
    // dedupe alternates between their reason and the area reason every frame,
    // flooding the pipe and making the game's UI appear to hang.  Remember
    // each distinct gate once instead.
    static char seen[8][96] = {};
    static unsigned seenCount = 0;
    for (unsigned i = 0; i < seenCount; ++i)
        if (std::strncmp(seen[i], reason, sizeof(seen[i]) - 1) == 0) return;
    if (seenCount < 8) std::snprintf(seen[seenCount++], sizeof(seen[0]), "%s", reason);
    fprintf(stderr, "[shadowmap][godrays] blocked: %s\n", reason);
}

// This pass allocates targets lazily, so state ownership has to begin before
// ensure_godray_targets(), not merely before the first draw.  In particular,
// texture allocation binds GL_TEXTURE_2D and the completeness check binds our
// FBO.  A scoped owner makes every return below restore the engine exactly.
struct GodrayGlState {
    UseProgram_t useProgram = nullptr;
    GLint viewport[4] = {0, 0, 0, 0};
    GLint framebuffer = 0;
    GLint program = 0;
    GLint activeTexture = GL_TEXTURE0;
    GLint texture28 = 0;
    GLint texture29 = 0;
    GLint texture29Array = 0;
    GLint texture30Array = 0;
    GLint srcRgb = GL_ONE, dstRgb = GL_ZERO;
    GLint srcAlpha = GL_ONE, dstAlpha = GL_ZERO;
    GLint eqRgb = GL_FUNC_ADD, eqAlpha = GL_FUNC_ADD;
    GLboolean depthMask = GL_TRUE;
    GLboolean colorMask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
    GLboolean depth = GL_FALSE, blend = GL_FALSE, scissor = GL_FALSE;

    explicit GodrayGlState(UseProgram_t dispatcher) : useProgram(dispatcher) {
        gl::GetIntegerv(GL_VIEWPORT, viewport);
        gl::GetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
        gl::GetIntegerv(GL_CURRENT_PROGRAM, &program);
        gl::GetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
        gl::ActiveTexture(GL_TEXTURE0 + 28);
        gl::GetIntegerv(GL_TEXTURE_BINDING_2D, &texture28);
        gl::ActiveTexture(GL_TEXTURE0 + 29);
        gl::GetIntegerv(GL_TEXTURE_BINDING_2D, &texture29);
        gl::GetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &texture29Array);
        gl::ActiveTexture(GL_TEXTURE0 + 30);
        gl::GetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &texture30Array);
        gl::GetIntegerv(GL_BLEND_SRC_RGB, &srcRgb);
        gl::GetIntegerv(GL_BLEND_DST_RGB, &dstRgb);
        gl::GetIntegerv(GL_BLEND_SRC_ALPHA, &srcAlpha);
        gl::GetIntegerv(GL_BLEND_DST_ALPHA, &dstAlpha);
        gl::GetIntegerv(GL_BLEND_EQUATION_RGB, &eqRgb);
        gl::GetIntegerv(GL_BLEND_EQUATION_ALPHA, &eqAlpha);
        gl::GetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
        gl::GetBooleanv(GL_COLOR_WRITEMASK, colorMask);
        depth = gl::IsEnabled(GL_DEPTH_TEST);
        blend = gl::IsEnabled(GL_BLEND);
        scissor = gl::IsEnabled(GL_SCISSOR_TEST);
    }

    ~GodrayGlState() {
        gl::BindFramebuffer(GL_FRAMEBUFFER, (GLuint)framebuffer);
        gl::Viewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        useProgram((GLuint)program);
        gl::ActiveTexture(GL_TEXTURE0 + 28);
        gl::BindTexture(GL_TEXTURE_2D, (GLuint)texture28);
        gl::ActiveTexture(GL_TEXTURE0 + 29);
        gl::BindTexture(GL_TEXTURE_2D, (GLuint)texture29);
        gl::BindTexture(GL_TEXTURE_2D_ARRAY, (GLuint)texture29Array);
        gl::ActiveTexture(GL_TEXTURE0 + 30);
        gl::BindTexture(GL_TEXTURE_2D_ARRAY, (GLuint)texture30Array);
        gl::ActiveTexture((GLenum)activeTexture);
        gl::BlendFuncSeparate((GLenum)srcRgb, (GLenum)dstRgb,
                              (GLenum)srcAlpha, (GLenum)dstAlpha);
        gl::BlendEquationSeparate((GLenum)eqRgb, (GLenum)eqAlpha);
        if (depth) gl::Enable(GL_DEPTH_TEST); else gl::Disable(GL_DEPTH_TEST);
        if (blend) gl::Enable(GL_BLEND); else gl::Disable(GL_BLEND);
        if (scissor) gl::Enable(GL_SCISSOR_TEST); else gl::Disable(GL_SCISSOR_TEST);
        gl::DepthMask(depthMask);
        gl::ColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
    }

    GodrayGlState(const GodrayGlState&) = delete;
    GodrayGlState& operator=(const GodrayGlState&) = delete;
};

static GLuint godrays_make_program(const char* fragment, const char* label) {
    auto create = (CreateShader_t)glew_or_dlsym(eng::GlewCreateShader, "glCreateShader");
    auto compile = (CompileShader_t)glew_or_dlsym(eng::GlewCompileShader, "glCompileShader");
    auto getShader = (GetShaderiv_t)glew_or_dlsym(eng::GlewGetShaderiv, "glGetShaderiv");
    auto createProgram = (CreateProgram_t)glew_or_dlsym(eng::GlewCreateProgram, "glCreateProgram");
    auto attach = (AttachShader_t)glew_or_dlsym(eng::GlewAttachShader, "glAttachShader");
    auto link = (LinkProgram_t)glew_or_dlsym(eng::GlewLinkProgram, "glLinkProgram");
    auto getProgram = (GetProgramiv_t)glew_or_dlsym(eng::GlewGetProgramiv, "glGetProgramiv");
    ShaderSource_t source = g_realShaderSource ? g_realShaderSource :
        (ShaderSource_t)glew_or_dlsym(eng::GlewShaderSource, "glShaderSource");
    if (!create || !compile || !getShader || !createProgram || !attach || !link ||
        !getProgram || !source)
        return 0;
    static const char* vertex =
        "#version 330 core\n"
        "void main(){ vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);"
        "gl_Position=vec4(p*2.0-1.0,0.0,1.0); }\n";
    const GLint vertexLength = (GLint)std::strlen(vertex);
    const GLint fragmentLength = (GLint)std::strlen(fragment);
    const GLuint vs = create(GL_VERTEX_SHADER), fs = create(GL_FRAGMENT_SHADER);
    if (!vs || !fs) return 0;
    source(vs, 1, &vertex, &vertexLength);
    source(fs, 1, &fragment, &fragmentLength);
    compile(vs); compile(fs);
    GLint vertexOk = 0, fragmentOk = 0;
    getShader(vs, GL_COMPILE_STATUS, &vertexOk);
    getShader(fs, GL_COMPILE_STATUS, &fragmentOk);
    if (!vertexOk || !fragmentOk) {
        fprintf(stderr, "[shadowmap][godrays] %s shader compile failed\n", label);
        return 0;
    }
    const GLuint program = createProgram();
    if (!program) return 0;
    attach(program, vs); attach(program, fs); link(program);
    GLint linked = 0;
    getProgram(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        fprintf(stderr, "[shadowmap][godrays] %s shader link failed\n", label);
        return 0;
    }
    return program;
}

static void build_godray_programs() {
    if (g_godrayMaskProgram && g_godrayScatterProgram && g_godrayBlurProgram &&
        g_godrayCompositeProgram)
        return;
    static bool attempted = false;
    if (attempted) return;
    attempted = true;
    static const char* maskFragment =
        "#version 330 core\n"
        "out vec4 nwnFragColor; uniform sampler2D nwnSceneDepth;"
        "uniform vec2 nwnTargetSize;"
        "void main(){ vec2 uv=gl_FragCoord.xy/nwnTargetSize;"
        "float d=texture(nwnSceneDepth,uv).r;"
        "nwnFragColor=vec4(vec3(d),1.0); }\n";
    static const char* scatterFragment =
        "#version 330 core\n"
        "out vec4 nwnFragColor; uniform sampler2D nwnSceneDepthQuarter;"
        "uniform sampler2DArrayShadow nwnStaticLightDepth;"
        "uniform sampler2DArrayShadow nwnDynamicLightDepth;"
        "uniform vec2 nwnTargetSize,nwnSunUV,nwnFogRange;"
        "uniform vec4 nwnSunTint,nwnFogColor;"
        "uniform mat4 nwnCameraVPInv,nwnCameraView,nwnLightVP[4];"
        "uniform vec4 nwnClipFar,nwnEye,nwnLightDir;"
        "uniform int nwnSamples,nwnDebug,nwnDynamicLayers,nwnFogActive;"
        "uniform float nwnStrength,nwnBias,nwnClearDensity;"
        "float nwnLit(vec3 world){"
        "float z=-(nwnCameraView*vec4(world,1)).z;int layer=0;"
        "if(z>nwnClipFar.x)layer=1;if(z>nwnClipFar.y)layer=2;"
        "if(z>nwnClipFar.z)layer=3;"
        "vec4 lp=nwnLightVP[layer]*vec4(world,1);if(lp.w<=0.0)return 1.0;"
        "vec3 s=lp.xyz/lp.w*.5+.5;"
        "if(any(lessThanEqual(s,vec3(0)))||any(greaterThanEqual(s,vec3(1))))return 1.0;"
        "float a=texture(nwnStaticLightDepth,vec4(s.xy,float(layer),s.z-nwnBias));"
        "float b=layer<nwnDynamicLayers?texture(nwnDynamicLightDepth,"
        "vec4(s.xy,float(layer),s.z-nwnBias)):1.0;return min(a,b);}\n"
        "void main(){ vec2 uv=gl_FragCoord.xy/nwnTargetSize;"
        "if(nwnDebug==1){nwnFragColor=vec4(1,0,1,1);return;}"
        "float depth=texture(nwnSceneDepthQuarter,uv).r;"
        "if(nwnDebug==2){float sky=depth>=.999999?1.0:0.0;"
        "nwnFragColor=vec4(vec3(sky),1);return;}"
        "if(nwnDebug==4){ float a=1.0-smoothstep(.010,.030,length(uv-nwnSunUV));"
        "nwnFragColor=vec4(vec3(1.0,.75,.05)*a,1);return;}"
        "vec4 endH=nwnCameraVPInv*vec4(uv*2.0-1.0,depth*2.0-1.0,1);"
        "if(abs(endH.w)<1e-6){nwnFragColor=vec4(0);return;}"
        "vec3 endWorld=endH.xyz/endH.w;vec3 delta=endWorld-nwnEye.xyz;"
        "float rayLength=length(delta);if(rayLength<1e-4){nwnFragColor=vec4(0);return;}"
        "vec3 ray=delta/rayLength;float marchLength=min(rayLength,nwnClipFar.w);"
        "float stepLength=marchLength/float(nwnSamples);"
        // Stable midpoint sampling is preferable here. Per-pixel random
        // jitter without a temporal resolve turned the shadow volume into a
        // crawling grey stipple instead of coherent shafts.
        "float jitter=.5;"
        "float litCount=0.0;float mediumCount=0.0;"
        "for(int i=0;i<64;i++){if(i>=nwnSamples)break;"
        "float dist=(float(i)+.25+.5*jitter)*stepLength;"
        // Atmospheric density is a near-camera medium in EVERY area. Authored
        // fog can make the medium denser with distance, but no longer disables
        // the user's atmosphere control merely because fogParams is present.
        "float authoredMedium=nwnFogActive!=0?clamp((dist-nwnFogRange.x)/"
        "max(nwnFogRange.y-nwnFogRange.x,.001),0.0,1.0):0.0;"
        "float medium=max(authoredMedium,nwnClearDensity);"
        "litCount+=nwnLit(nwnEye.xyz+ray*dist)*medium;mediumCount+=medium;}"
        "float raw=litCount/max(mediumCount,.0001);"
        "float authoredAmount=nwnFogActive!=0?clamp((marchLength-nwnFogRange.x)/"
        "max(nwnFogRange.y-nwnFogRange.x,.001),0.0,1.0):0.0;"
        "float atmosphereAmount=nwnClearDensity*smoothstep(.02,.35,"
        "clamp(marchLength/max(nwnClipFar.w,.001),0.0,1.0));"
        "float fogAmount=max(authoredAmount,atmosphereAmount);"
        "float g=.45;float ct=clamp(dot(-ray,normalize(nwnLightDir.xyz)),-1.0,1.0);"
        // Require the CAMERA, not merely a ray at the top edge, to face the
        // sun. Without this center-ray gate an offscreen sun could leave a
        // serrated crescent along one edge while the player looked elsewhere.
        "vec4 centerH=nwnCameraVPInv*vec4(0,0,1,1);"
        "vec3 centerRay=normalize(centerH.xyz/centerH.w-nwnEye.xyz);"
        "float cameraSun=smoothstep(.55,.82,"
        "clamp(dot(-centerRay,normalize(nwnLightDir.xyz)),-1.0,1.0));"
        // The per-ray term softly focuses the effect around the source once
        // the camera-center requirement has admitted the frame.
        "float viewSun=smoothstep(.30,.75,ct)*cameraSun;"
        "float phase=(1.0-g*g)/pow(max(.001,1.0+g*g-2.0*g*ct),1.5)*.16*viewSun;"
        "if(nwnDebug==3){nwnFragColor=vec4(clamp((1.0-raw)*4.0,0.0,1.0),"
        "clamp(phase,0.0,1.0),clamp(raw,0.0,1.0),1);return;}"
        // Preserve integrated sunlight here.  Shaft extraction happens after
        // spatial filtering; mapping partial visibility directly to brightness
        // projects translucent copies of every caster into the final image.
        "float energy=clamp(raw*phase*nwnStrength*fogAmount*.52,0.0,.50);"
        "vec3 warm=mix(clamp(nwnFogColor.rgb,vec3(0),vec3(1.5)),"
        "clamp(nwnSunTint.rgb,vec3(0),vec3(1.5)),.58);"
        "nwnFragColor=vec4(warm*energy,1.0); }\n";
    static const char* compositeFragment =
        "#version 330 core\n"
        "out vec4 nwnFragColor;uniform sampler2D nwnGodrayFine,nwnGodrayBase;"
        "uniform vec2 nwnScreenSize;uniform int nwnDebug;"
        "void main(){vec2 uv=gl_FragCoord.xy/nwnScreenSize;"
        "vec3 fine=texture(nwnGodrayFine,uv).rgb;"
        "if(nwnDebug!=0){nwnFragColor=vec4(fine,1);return;}"
        // A shaft is positive local illumination contrast, not the shadow
        // field itself.  Removing the broad component prevents foliage and
        // cascade silhouettes from appearing as a translucent overlay.
        "vec3 base=texture(nwnGodrayBase,uv).rgb;"
        // Retain a small, smooth body of the volume as well as its positive
        // local contrast.  A pure high-pass disappears when a shaft is wider
        // than the baseline kernel; the 8% residual keeps such shafts visible
        // without restoring the original full-strength caster overlay.
        "vec3 shaft=max(fine-base*.92,vec3(0))*3.25;"
        "nwnFragColor=vec4(min(shaft,vec3(.38)),1.0);}\n";
    static const char* blurFragment =
        "#version 330 core\n"
        "out vec4 nwnFragColor;uniform sampler2D nwnVolume,nwnSceneDepth;"
        "uniform vec2 nwnTargetSize,nwnAxis;uniform float nwnSigma;"
        "void main(){vec2 uv=gl_FragCoord.xy/nwnTargetSize;"
        "vec2 texel=nwnAxis/nwnTargetSize;float cd=texture(nwnSceneDepth,uv).r;"
        "vec4 sum=vec4(0);float total=0.0;"
        "for(int i=-8;i<=8;++i){float x=float(i);vec2 q=clamp(uv+texel*x,vec2(0),vec2(1));"
        "float d=texture(nwnSceneDepth,q).r;float spatial=exp(-.5*x*x/(nwnSigma*nwnSigma));"
        "float edge=exp(-abs(d-cd)*600.0);float w=spatial*edge;"
        "sum+=texture(nwnVolume,q)*w;total+=w;}"
        "nwnFragColor=sum/max(total,.0001);}\n";
    g_godrayMaskProgram = godrays_make_program(maskFragment, "mask");
    g_godrayScatterProgram = godrays_make_program(scatterFragment, "scatter");
    g_godrayBlurProgram = godrays_make_program(blurFragment, "bilateral-blur");
    g_godrayCompositeProgram = godrays_make_program(compositeFragment, "composite");
    if (g_godrayMaskProgram && g_godrayScatterProgram && g_godrayBlurProgram &&
        g_godrayCompositeProgram)
        fprintf(stderr, "[shadowmap][godrays] volumetric shaft programs compiled\n");
}

static bool ensure_godray_targets(int width, int height) {
    if (width <= 0 || height <= 0 || !gl::GenTextures || !gl::BindTexture ||
        !gl::TexImage2D || !gl::TexParameteri || !gl::GenFramebuffers ||
        !gl::BindFramebuffer || !gl::FramebufferTexture2D ||
        !gl::CheckFramebufferStatus)
        return false;
    if (!g_godrayMaskTexture) gl::GenTextures(1, &g_godrayMaskTexture);
    if (!g_godrayScatterTexture) gl::GenTextures(1, &g_godrayScatterTexture);
    if (!g_godrayFilteredTexture) gl::GenTextures(1, &g_godrayFilteredTexture);
    if (!g_godrayFbo) gl::GenFramebuffers(1, &g_godrayFbo);
    if (!g_godrayMaskTexture || !g_godrayScatterTexture ||
        !g_godrayFilteredTexture || !g_godrayFbo) return false;
    if (width != g_godrayWidth || height != g_godrayHeight) {
        const GLuint textures[3] = {g_godrayMaskTexture, g_godrayScatterTexture,
                                    g_godrayFilteredTexture};
        for (GLuint texture : textures) {
            gl::BindTexture(GL_TEXTURE_2D, texture);
            gl::TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0,
                           GL_RGBA, GL_FLOAT, nullptr);
            gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
        g_godrayWidth = width;
        g_godrayHeight = height;
    }
    gl::BindFramebuffer(GL_FRAMEBUFFER, g_godrayFbo);
    gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                             g_godrayScatterTexture, 0);
    return gl::CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
}

static bool godrays_sun_direction(float out[3]) {
    const float* direction = nullptr;
    if (!g_dirOverride && g_haveTraceAreaLight) direction = g_traceAreaLightDir;
    else if (eng::SunDir) direction = eng::SunDir;
    if (!direction) return false;
    float dx = direction[0], dy = direction[1], dz = direction[2];
    const float length = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!std::isfinite(length) || length < 1.0e-5f) return false;
    out[0] = dx / length; out[1] = dy / length; out[2] = dz / length;
    return true;
}

static bool godrays_project_sun(float& u, float& v) {
    float direction[3];
    if (!godrays_sun_direction(direction)) return false;
    // g_traceAreaLightDir is the recorded light->scene direction.  The sun is
    // therefore located in the opposite direction from the camera.
    const float x = g_shadowFrameContext.eye.x - direction[0] * 1000.0f;
    const float y = g_shadowFrameContext.eye.y - direction[1] * 1000.0f;
    const float z = g_shadowFrameContext.eye.z - direction[2] * 1000.0f;
    float vp[16];
    mat4_mul(g_shadowFrameContext.projection, g_shadowFrameContext.view, vp);
    const float clipX = vp[0] * x + vp[4] * y + vp[8] * z + vp[12];
    const float clipY = vp[1] * x + vp[5] * y + vp[9] * z + vp[13];
    const float clipW = vp[3] * x + vp[7] * y + vp[11] * z + vp[15];
    if (!std::isfinite(clipW) || std::fabs(clipW) < 1.0e-5f || clipW <= 0.0f)
        return false;
    u = clipX / clipW * 0.5f + 0.5f;
    v = clipY / clipW * 0.5f + 0.5f;
    return std::isfinite(u) && std::isfinite(v);
}

static void godrays_draw_fullscreen() {
    NWN_GL_INTERPOSE(glDrawArrays)(0x0004 /* GL_TRIANGLES */, 0, 3);
}

static void draw_godrays(void* scene) {
    if constexpr (!kGodraysFeatureAvailable) return;
    if (!g_godraysEnabled) return;
    // Trace mode owns the selected-area context but does not update the
    // normal-run g_isWorldScene flag.  The selected scene plus its frozen
    // context is the authoritative gate in both launch paths.
    if (scene != g_areaScene) {
        godrays_blocked("not-current-area-scene"); return;
    }
    if (!g_shadowFrameContext.valid || g_shadowFrameContext.scene != scene) {
        godrays_blocked("no-current-area-context"); return;
    }
    // This is the shared authority used by the native-shadow fog fade too.
    // Polling is internally throttled and reads uniforms only; it never alters
    // NWN's shader state.
    poll_engine_fog();
    if (!gl::GetIntegerv || !gl::GetBooleanv || !gl::IsEnabled || !gl::Enable ||
        !gl::Disable || !gl::BlendFuncSeparate || !gl::BlendEquationSeparate ||
        !gl::Viewport || !gl::ActiveTexture || !gl::BindTexture ||
        !gl::BindFramebuffer || !gl::FramebufferTexture2D || !gl::DepthMask ||
        !gl::ColorMask || !gl::GetUniformLocation ||
        !gl::Uniform1i || !gl::Uniform1f || !gl::Uniform2f || !gl::Uniform4f ||
        !gl::UniformMatrix4fv) {
        godrays_blocked("missing-gl-entry"); return;
    }
    UseProgram_t useProgram = g_realUseProgram ? g_realUseProgram :
        (UseProgram_t)glew_or_dlsym(eng::GlewUseProgram, "glUseProgram");
    if (!useProgram) { godrays_blocked("no-glUseProgram-dispatcher"); return; }
    GLint entryViewport[4] = {0, 0, 0, 0}, entryFbo = 0;
    gl::GetIntegerv(GL_VIEWPORT, entryViewport);
    gl::GetIntegerv(GL_FRAMEBUFFER_BINDING, &entryFbo);
    if (entryFbo != 0 || entryViewport[2] <= 0 || entryViewport[3] <= 0) {
        godrays_blocked(entryFbo ? "not-default-framebuffer" : "empty-viewport"); return;
    }

    // Cross-module ownership is the required interposition guard.  g_inOurPass
    // is a caster-replay flag and can colour-mask an unrecognised fullscreen
    // program, including this pass and ImGui.
    nwn_core::OwnedPass ownedPass;
    GodrayGlState state(useProgram);
    gl::ActiveTexture(GL_TEXTURE0 + 28);

    const int resolution = std::max(0, std::min(2, g_godraysResolution));
    const int divisor = resolution == 0 ? 4 : (resolution == 1 ? 2 : 1);
    const int targetW = (state.viewport[2] + divisor - 1) / divisor;
    const int targetH = (state.viewport[3] + divisor - 1) / divisor;
    build_godray_programs();
    if (!g_godrayMaskProgram || !g_godrayScatterProgram || !g_godrayBlurProgram ||
        !g_godrayCompositeProgram) {
        godrays_blocked("shader-build-failed"); return;
    }
    if (!ensure_godray_targets(targetW, targetH)) {
        godrays_blocked("target-allocation-failed"); return;
    }
    float sunU = 0.5f, sunV = 0.5f;
    float lightDirection[3] = {0.0f, 0.0f, -1.0f};
    const bool needsVolume = g_godraysDebug == 0 || g_godraysDebug == 3;
    const bool needsSunPosition = g_godraysDebug == 4;
    if (needsVolume && !godrays_sun_direction(lightDirection)) {
        godrays_blocked("no-sun-direction"); return;
    }
    if (needsSunPosition && !godrays_project_sun(sunU, sunV)) {
        godrays_blocked("no-projectable-sun-direction"); return;
    }
    const float projectedSunU = sunU;
    const float projectedSunV = sunV;
    const bool sunOffscreen = needsSunPosition &&
        (sunU < -0.10f || sunU > 1.10f || sunV < -0.10f || sunV > 1.10f);
    // A source just outside the viewport is a normal and important shaft case:
    // the sun can be hidden above the camera while its rays still cross the
    // visible canopy.  Keep normal mode's origin slightly outside the edge;
    // only the marker view pins it inside so the diagnostic remains visible.
    if (sunOffscreen && g_godraysDebug == 4) {
        sunU = std::max(0.02f, std::min(0.98f, sunU));
        sunV = std::max(0.02f, std::min(0.98f, sunV));
    } else if (sunOffscreen) {
        sunU = std::max(-0.08f, std::min(1.08f, sunU));
        sunV = std::max(-0.08f, std::min(1.08f, sunV));
    }
    if (g_godraysDebug == 0 && effective_area_sun_strength() <= 0.001f) {
        godrays_blocked("area-sun-disabled"); return;
    }
    if (needsVolume && (!g_cascadeTargetsUsable || !g_cascadeStaticTex ||
                        !g_cascadeDynamicTex || !g_cascadeMath.valid ||
                        g_cascadeMath.sourceFrame != g_shadowFrameContext.serial)) {
        godrays_blocked("directional-volume-unavailable"); return;
    }
    if (g_godraysDebug != 1 && g_godraysDebug != 4 && !g_sceneDepthTex) {
        godrays_blocked("no-scene-depth-copy"); return;
    }

    gl::Disable(GL_DEPTH_TEST); gl::Disable(GL_BLEND); gl::Disable(GL_SCISSOR_TEST);
    gl::DepthMask(GL_FALSE); gl::ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl::BindFramebuffer(GL_FRAMEBUFFER, g_godrayFbo);
    gl::Viewport(0, 0, targetW, targetH);

    if (g_godraysDebug != 1 && g_godraysDebug != 4) {
        gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                 g_godrayMaskTexture, 0);
        gl::BindTexture(GL_TEXTURE_2D, g_sceneDepthTex);
        useProgram(g_godrayMaskProgram);
        GLint location = gl::GetUniformLocation(g_godrayMaskProgram, "nwnSceneDepth");
        if (location >= 0) gl::Uniform1i(location, 28);
        location = gl::GetUniformLocation(g_godrayMaskProgram, "nwnTargetSize");
        if (location >= 0) gl::Uniform2f(location, (float)targetW, (float)targetH);
        godrays_draw_fullscreen();
    }

    gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                             g_godrayScatterTexture, 0);
    gl::ActiveTexture(GL_TEXTURE0 + 29);
    gl::BindTexture(GL_TEXTURE_2D_ARRAY, g_cascadeStaticTex);
    gl::ActiveTexture(GL_TEXTURE0 + 30);
    gl::BindTexture(GL_TEXTURE_2D_ARRAY, g_cascadeDynamicTex);
    gl::ActiveTexture(GL_TEXTURE0 + 28);
    gl::BindTexture(GL_TEXTURE_2D, g_godrayMaskTexture);
    useProgram(g_godrayScatterProgram);
    GLint location = gl::GetUniformLocation(g_godrayScatterProgram, "nwnSceneDepthQuarter");
    if (location >= 0) gl::Uniform1i(location, 28);
    location = gl::GetUniformLocation(g_godrayScatterProgram, "nwnStaticLightDepth");
    if (location >= 0) gl::Uniform1i(location, 29);
    location = gl::GetUniformLocation(g_godrayScatterProgram, "nwnDynamicLightDepth");
    if (location >= 0) gl::Uniform1i(location, 30);
    location = gl::GetUniformLocation(g_godrayScatterProgram, "nwnTargetSize");
    if (location >= 0) gl::Uniform2f(location, (float)targetW, (float)targetH);
    location = gl::GetUniformLocation(g_godrayScatterProgram, "nwnSunUV");
    if (location >= 0) gl::Uniform2f(location, sunU, sunV);
    location = gl::GetUniformLocation(g_godrayScatterProgram, "nwnSamples");
    if (location >= 0) gl::Uniform1i(location, std::max(8, std::min(64, g_godraysSamples)));
    location = gl::GetUniformLocation(g_godrayScatterProgram, "nwnDebug");
    if (location >= 0) gl::Uniform1i(location, g_godraysDebug);
    float tintR = (eng::SunDiffuse && std::isfinite(eng::SunDiffuse[0])) ? std::max(0.15f, eng::SunDiffuse[0]) : 1.0f;
    float tintG = (eng::SunDiffuse && std::isfinite(eng::SunDiffuse[1])) ? std::max(0.12f, eng::SunDiffuse[1]) : 0.88f;
    float tintB = (eng::SunDiffuse && std::isfinite(eng::SunDiffuse[2])) ? std::max(0.08f, eng::SunDiffuse[2]) : 0.62f;
    // SunDiffuse is an LDR engine-light value, not HDR solar radiance. Keep its
    // hue but normalize its peak so the in-scattering can balance attenuation.
    const float tintPeak = std::max(tintR, std::max(tintG, tintB));
    if (tintPeak > 1.0e-4f) {
        tintR /= tintPeak; tintG /= tintPeak; tintB /= tintPeak;
    }
    location = gl::GetUniformLocation(g_godrayScatterProgram, "nwnSunTint");
    if (location >= 0) gl::Uniform4f(location, tintR, tintG, tintB, 1.0f);
    const bool authoredFog = g_haveEngineFog &&
        g_fogEndEngine - g_fogStartEngine >= 5.0f;
    g_fogFromEngine = authoredFog;
    g_fogStartLive = authoredFog ? g_fogStartEngine : 0.0f;
    g_fogEndLive = authoredFog ? g_fogEndEngine : 0.0f;
    location = gl::GetUniformLocation(g_godrayScatterProgram, "nwnFogRange");
    if (location >= 0) gl::Uniform2f(location,
        authoredFog ? g_fogStartEngine : 0.0f,
        authoredFog ? g_fogEndEngine : g_cascadeMath.clipFar[3]);
    location = gl::GetUniformLocation(g_godrayScatterProgram, "nwnFogColor");
    if (location >= 0) gl::Uniform4f(location,
        g_haveEngineFogColor ? g_fogColorEngine[0] : 0.62f,
        g_haveEngineFogColor ? g_fogColorEngine[1] : 0.68f,
        g_haveEngineFogColor ? g_fogColorEngine[2] : 0.72f, 1.0f);
    location = gl::GetUniformLocation(g_godrayScatterProgram, "nwnFogActive");
    if (location >= 0) gl::Uniform1i(location, authoredFog ? 1 : 0);
    location = gl::GetUniformLocation(g_godrayScatterProgram, "nwnClearDensity");
    if (location >= 0) gl::Uniform1f(location, g_atmosphereDensity);
    location = gl::GetUniformLocation(g_godrayScatterProgram, "nwnStrength");
    if (location >= 0) gl::Uniform1f(location, g_godraysStrength);
    location = gl::GetUniformLocation(g_godrayScatterProgram, "nwnCameraVPInv");
    if (location >= 0) gl::UniformMatrix4fv(location, 1, GL_FALSE,
                                             g_shadowFrameContext.viewProjectionInverse);
    location = gl::GetUniformLocation(g_godrayScatterProgram, "nwnCameraView");
    if (location >= 0) gl::UniformMatrix4fv(location, 1, GL_FALSE,
                                             g_shadowFrameContext.view);
    location = gl::GetUniformLocation(g_godrayScatterProgram, "nwnLightVP[0]");
    if (location >= 0) gl::UniformMatrix4fv(location, kCascadeCount, GL_FALSE,
                                             &g_cascadeMath.lightVP[0][0]);
    location = gl::GetUniformLocation(g_godrayScatterProgram, "nwnClipFar");
    if (location >= 0) gl::Uniform4f(location, g_cascadeMath.clipFar[0],
                                     g_cascadeMath.clipFar[1],
                                     g_cascadeMath.clipFar[2],
                                     g_cascadeMath.clipFar[3]);
    location = gl::GetUniformLocation(g_godrayScatterProgram, "nwnEye");
    if (location >= 0) gl::Uniform4f(location, g_shadowFrameContext.eye.x,
                                     g_shadowFrameContext.eye.y,
                                     g_shadowFrameContext.eye.z, 1.0f);
    location = gl::GetUniformLocation(g_godrayScatterProgram, "nwnLightDir");
    if (location >= 0) gl::Uniform4f(location, lightDirection[0], lightDirection[1],
                                     lightDirection[2], 0.0f);
    location = gl::GetUniformLocation(g_godrayScatterProgram, "nwnDynamicLayers");
    if (location >= 0) gl::Uniform1i(location,
        g_cascadeCsmDynamicReceiver ? g_cascadeDynamicLayers : 0);
    location = gl::GetUniformLocation(g_godrayScatterProgram, "nwnBias");
    if (location >= 0) gl::Uniform1f(location, g_cascadeReceiverBias);
    godrays_draw_fullscreen();

    if (g_godraysDebug == 0) {
        // First produce a softly resolved illumination field, then a much
        // broader local baseline.  The composite keeps only illumination above
        // that baseline, so caster silhouettes cannot survive as grey overlays.
        useProgram(g_godrayBlurProgram);
        location = gl::GetUniformLocation(g_godrayBlurProgram, "nwnVolume");
        if (location >= 0) gl::Uniform1i(location, 28);
        location = gl::GetUniformLocation(g_godrayBlurProgram, "nwnSceneDepth");
        if (location >= 0) gl::Uniform1i(location, 29);
        location = gl::GetUniformLocation(g_godrayBlurProgram, "nwnTargetSize");
        if (location >= 0) gl::Uniform2f(location, (float)targetW, (float)targetH);
        const GLint axisLocation =
            gl::GetUniformLocation(g_godrayBlurProgram, "nwnAxis");
        const GLint sigmaLocation =
            gl::GetUniformLocation(g_godrayBlurProgram, "nwnSigma");
        gl::ActiveTexture(GL_TEXTURE0 + 29);
        gl::BindTexture(GL_TEXTURE_2D, g_sceneDepthTex);

        // Fine resolve: enough to remove quarter-resolution stair steps while
        // retaining coherent bands of illumination.
        gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                 g_godrayMaskTexture, 0);
        gl::ActiveTexture(GL_TEXTURE0 + 28);
        gl::BindTexture(GL_TEXTURE_2D, g_godrayScatterTexture);
        // Keep the blur footprint in SCREEN pixels. A one-target-pixel radius
        // made Full four times sharper than Quarter and exposed ray steps.
        const float screenScale = 4.0f / (float)divisor;
        if (axisLocation >= 0) gl::Uniform2f(axisLocation, screenScale, 0.0f);
        if (sigmaLocation >= 0) gl::Uniform1f(sigmaLocation, 2.6f);
        godrays_draw_fullscreen();

        gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                 g_godrayFilteredTexture, 0);
        gl::BindTexture(GL_TEXTURE_2D, g_godrayMaskTexture);
        if (axisLocation >= 0) gl::Uniform2f(axisLocation, 0.0f, screenScale);
        godrays_draw_fullscreen();

        // Broad resolve used only as the local daylight baseline.
        gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                 g_godrayMaskTexture, 0);
        gl::BindTexture(GL_TEXTURE_2D, g_godrayFilteredTexture);
        if (axisLocation >= 0) gl::Uniform2f(axisLocation, screenScale, 0.0f);
        if (sigmaLocation >= 0) gl::Uniform1f(sigmaLocation, 5.0f);
        godrays_draw_fullscreen();

        gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                 g_godrayScatterTexture, 0);
        gl::BindTexture(GL_TEXTURE_2D, g_godrayMaskTexture);
        if (axisLocation >= 0) gl::Uniform2f(axisLocation, 0.0f, screenScale);
        godrays_draw_fullscreen();
    }

    gl::BindFramebuffer(GL_FRAMEBUFFER, (GLuint)state.framebuffer);
    gl::Viewport(state.viewport[0], state.viewport[1], state.viewport[2], state.viewport[3]);
    gl::ActiveTexture(GL_TEXTURE0 + 28);
    gl::BindTexture(GL_TEXTURE_2D, g_godraysDebug == 0 ?
                    g_godrayFilteredTexture : g_godrayScatterTexture);
    gl::ActiveTexture(GL_TEXTURE0 + 29);
    gl::BindTexture(GL_TEXTURE_2D, g_godrayScatterTexture);
    useProgram(g_godrayCompositeProgram);
    location = gl::GetUniformLocation(g_godrayCompositeProgram, "nwnGodrayFine");
    if (location >= 0) gl::Uniform1i(location, 28);
    location = gl::GetUniformLocation(g_godrayCompositeProgram, "nwnGodrayBase");
    if (location >= 0) gl::Uniform1i(location, 29);
    location = gl::GetUniformLocation(g_godrayCompositeProgram, "nwnDebug");
    if (location >= 0) gl::Uniform1i(location, g_godraysDebug);
    location = gl::GetUniformLocation(g_godrayCompositeProgram, "nwnScreenSize");
    if (location >= 0) gl::Uniform2f(location, (float)state.viewport[2], (float)state.viewport[3]);
    gl::Enable(GL_BLEND);
    gl::BlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    // Normal mode implements the volume equation in fixed-function blending:
    //   result = in-scattering + scene * transmittance
    // Debug modes replace RGB so their channels cannot saturate against the
    // underlying scene. Framebuffer alpha belongs to NWN in both cases.
    if (g_godraysDebug == 0)
        gl::BlendFuncSeparate(GL_ONE, GL_SRC_ALPHA, GL_ZERO, GL_ONE);
    else
        gl::BlendFuncSeparate(GL_ONE, GL_ZERO, GL_ZERO, GL_ONE);
    gl::ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
    godrays_draw_fullscreen();

    // Log transitions including the return to normal mode.  This is bounded
    // and leaves mode 0 diagnosable without turning on a full debug view.
    static int lastMode = -1;
    if (lastMode != g_godraysDebug) {
        lastMode = g_godraysDebug;
        fprintf(stderr, "[shadowmap][godrays] draw mode=%d target=%dx%d sunUv=(%.3f,%.3f) projected=(%.3f,%.3f) strength=%.2f fog=%s %.1f..%.1f color=%s\n",
                g_godraysDebug, targetW, targetH, sunU, sunV,
                projectedSunU, projectedSunV, g_godraysStrength,
                authoredFog ? "area" : "clear", g_fogStartLive, g_fogEndLive,
                g_haveEngineFogColor ? "area" : "fallback");
    }
}
