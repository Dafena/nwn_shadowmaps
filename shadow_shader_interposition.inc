// ===========================================================================
//  Phase 4: deliver the shadow map to EVERY shader
//
//  The uniforms cannot travel via MTR -- `parameter` only supports float and
//  int, so a mat4 is impossible. They have to be pushed with glUniform* at the
//  moment a program becomes current, which means intercepting glUseProgram.
//  The engine uses GLEW, so that is a writable function pointer rather than a
//  function: assign and done.
// ===========================================================================
// Chosen as "first index past everything documented as used": MTR texture<n>
// -> texUnit<n> for n=0..10, and 11-15 reserved for texFBColor / texFBDepth /
// texUnitEnvCube / texUnitEnv / texUnitNoise. GL_MAX_TEXTURE_IMAGE_UNITS is 32
// here so it is legal. NOTE THE ASSUMPTION: that MTR slot index equals GL
// texture unit index. That is unverified, so make it swappable.
static int SHADOW_UNIT = 16;
static const char* U_SHADOW_TEX = "texShadowMap";
static const char* U_SHADOW_MAT = "mShadowMatrix";
static const char* U_INJECT_TEST = "nwnShadowmapInjectTest";
static const char* U_VIEW_INV = "m_view_inv";
static const char* U_CASTER_PASS = "nwnCasterDepthPass";
static const char* U_CASTER_LIGHT_VP = "nwnCasterLightVP";
static const char* U_CASTER_CAMERA_VP_INV = "nwnCasterCameraVPInv";
static const char* U_MODEL = "m_m";
static const char* U_MODEL_VIEW = "m_mv";
static const char* U_MODEL_VIEW_PROJ = "m_mvp";
static const char* U_VIEW = "m_view";
static const char* U_PROJ = "m_proj";

// The complete fallback receiver transform, plus the camera-independent light
// VP. Terrain receivers reconstruct the former from their *current* m_view_inv
// each draw, so zoom/orbit cannot project a previous camera onto this frame.
static float g_shadowMat[16];
// The exact camera view used when g_shadowMat's light map was rendered.  This
// must not be confused with g_camView, which is overwritten by later camera
// and UI passes before a receiver draw occurs.
static float g_shadowSourceView[16];
static float g_lightWorldVP[16];
static bool  g_haveShadowMat = false;
static bool  g_haveLightWorldVP = false;
static float g_camView[16];          // world camera view, latched at entry
static bool  g_haveCamView   = false;
// Matrices that are live only while RenderDrawBucket is replaying into the
// private light target.  Unlike g_shadowMat these transform a caster vertex,
// not a camera-space receiver position.
static float g_activeLightView[16];
static float g_activeLightProj[16];
static float g_activeLightWorldVP[16];
static bool  g_haveActiveLightMatrices = false;

// Diagnostic A/B only: preserve the exact known-good receiver transport
// (GetTexImage -> R32UI once per second) while the new GPU caster pass stays
// enabled. This separates matrix/update cadence from GPU texture sampling.
static bool use_cpu_bridge() {
    return shadow_getenv("NWN_SHADOWMAP_CPU_BRIDGE") != nullptr;
}

static bool use_gpu_step_bridge() {
    return shadow_getenv("NWN_SHADOWMAP_GPU_STEP_BRIDGE") != nullptr;
}

// The live colour attachment is not a reliable receiver texture in NWN's GL
// state flow: it contains valid caster depths when read back, but receiver
// shaders see it as all 1.0.  Always sample a detached R32F copy instead.
// STEP keeps the old A/B cadence; the normal colour-caster launch copies every
// validated world light pass.
static bool use_gpu_live_bridge() {
    return !use_cpu_bridge() && !use_gpu_step_bridge() &&
           shadow_getenv("NWN_SHADOWMAP_COLORCAST_TEST") != nullptr;
}
static bool use_gpu_copy_bridge() {
    return use_gpu_step_bridge() || use_gpu_live_bridge();
}

static double g_nextGpuStepCopy = 0.0;
static double g_nextGpuBridgeLog = 0.0;

// GPU-only equivalent of the once-per-second CPU bridge. Copying one GPU
// texture into another has no CPU readback or upload, and lets us pair this
// exact color map with the matrix from the same replay.
static bool update_gpu_step_bridge() {
    if (!use_gpu_copy_bridge() || !g_gpuStepTex || !g_lightPreloadedCasters ||
        !gl::CopyTexSubImage2D || !gl::ActiveTexture) return false;
    GLint active = GL_TEXTURE0, oldTex = 0;
    gl::GetIntegerv(GL_ACTIVE_TEXTURE, &active);
    gl::ActiveTexture(GL_TEXTURE0 + SHADOW_UNIT);
    gl::GetIntegerv(GL_TEXTURE_BINDING_2D, &oldTex);
    gl::BindTexture(GL_TEXTURE_2D, g_gpuStepTex);
    gl::ReadBuffer(GL_COLOR_ATTACHMENT0);
    gl::CopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, g_size, g_size);
    gl::BindTexture(GL_TEXTURE_2D, (GLuint)oldTex);
    gl::ActiveTexture((GLenum)active);
    const GLenum err = gl::GetError();
    // This path may copy once per valid world pass.  A line per copy buries
    // the diagnostics we actually need; retain errors immediately and a
    // compact healthy heartbeat once every five seconds.
    const double now = now_seconds();
    if (err != GL_NO_ERROR || now >= g_nextGpuBridgeLog) {
        g_nextGpuBridgeLog = now + 5.0;
        fprintf(stderr, "[shadowmap] GPU color bridge %s\n",
                err == GL_NO_ERROR ? "ok" : "GL error");
    }
    return err == GL_NO_ERROR;
}

// One-shot evidence only for COLORCAST_TEST. It establishes whether the
// colour attachment contains real caster values after a replay that had an
// injected program preloaded. It never runs in the actual runtime path.
static void diagnose_colour_caster_target_once() {
    static bool done = false;
    if (done || !shadow_getenv("NWN_SHADOWMAP_COLORCAST_TEST") || !g_lightColorTex ||
        !g_lightPreloadedCasters || !gl::GetTexImage || !gl::ActiveTexture) return;
    done = true;
    GLint active = GL_TEXTURE0, oldTex = 0;
    gl::GetIntegerv(GL_ACTIVE_TEXTURE, &active);
    gl::ActiveTexture(GL_TEXTURE0 + SHADOW_UNIT);
    gl::GetIntegerv(GL_TEXTURE_BINDING_2D, &oldTex);
    gl::BindTexture(GL_TEXTURE_2D, g_lightColorTex);
    std::vector<float> px((size_t)g_size * (size_t)g_size);
    gl::GetTexImage(GL_TEXTURE_2D, 0, GL_RED, GL_FLOAT, px.data());
    gl::BindTexture(GL_TEXTURE_2D, g_depthTex);
    std::vector<uint32_t> hwDepth((size_t)g_size * (size_t)g_size);
    gl::GetTexImage(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT,
                    hwDepth.data());
    float lo = 1.0f, hi = 0.0f;
    size_t nonWhite = 0;
    size_t mismatched = 0;
    float maxDelta = 0.0f;
    for (size_t i = 0; i < px.size(); ++i) {
        const float r = px[i];
        if (r < lo) lo = r;
        if (r > hi) hi = r;
        if (r < 0.999999f) ++nonWhite;
        const float hw = float(hwDepth[i]) * (1.0f / 4294967295.0f);
        const float delta = std::fabs(r - hw);
        if (delta > maxDelta) maxDelta = delta;
        if (delta > 2.0e-6f) ++mismatched;
    }
    fprintf(stderr, "[shadowmap] colour-caster target: R=%.7f..%.7f nonwhite=%zu/%zu "
                    "vs-hw mismatch=%zu max-delta=%.7f\n",
            lo, hi, nonWhite, (size_t)g_size * (size_t)g_size,
            mismatched, maxDelta);
    gl::BindTexture(GL_TEXTURE_2D, (GLuint)oldTex);
    gl::ActiveTexture((GLenum)active);
}
// Per-program uniform locations. Looked up once; the matrix itself must be
// re-sent every time a program is bound, since it changes each frame.
struct ProgSlot {
    GLuint prog;
    GLint locTex, locMat, locTest, locViewInv, locVPInv;
    GLint locCaster, locCasterLightVP, locCasterCameraVPInv;
    // Normal NWN vertex shaders use one or more of these.  Stencil's
    // vs_shadowvol proved the intended convention: local -> m_m -> m_view ->
    // m_proj.  Cache the locations once, then replace only their light-pass
    // uploads through __glewUniformMatrix4fv.
    GLint locModel, locModelView, locModelViewProj, locView, locProj;
    float model[16];
    float cameraVPInv[16];
    bool haveModel;
    bool haveCameraVPInv;
    long frame;
};
static ProgSlot g_progs[128];
static int      g_progCount = 0;
static long     g_progHits  = 0;

typedef void (*UseProgram_t)(GLuint);
static UseProgram_t g_realUseProgram = nullptr;

using UniformMatrix4fv_t = void (*)(GLint, GLsizei, GLboolean, const GLfloat*);
static UniformMatrix4fv_t g_realUniformMatrix4fv = nullptr;

using Uniform3f_t = void (*)(GLint, GLfloat, GLfloat, GLfloat);
using Uniform3fv_t = void (*)(GLint, GLsizei, const GLfloat*);
static Uniform3f_t  g_realUniform3f = nullptr;
static Uniform3fv_t g_realUniform3fv = nullptr;

using ShaderSource_t = void (*)(GLuint shader, GLsizei count,
                                const char* const* string, const GLint* length);
static ShaderSource_t g_realShaderSource = nullptr;
static bool g_injectTest = false;

using CreateShader_t = GLuint (*)(GLenum);
using CompileShader_t = void (*)(GLuint);
using GetShaderiv_t = void (*)(GLuint, GLenum, GLint*);
using GetShaderInfoLog_t = void (*)(GLuint, GLsizei, GLsizei*, char*);
using CreateProgram_t = GLuint (*)();
using AttachShader_t = void (*)(GLuint, GLuint);
using LinkProgram_t = void (*)(GLuint);
using GetProgramiv_t = void (*)(GLuint, GLenum, GLint*);
using GetProgramInfoLog_t = void (*)(GLuint, GLsizei, GLsizei*, char*);
using DeleteShader_t = void (*)(GLuint);

static GLuint g_gpuDecodeProgram = 0;
extern "C" void NWN_GL_INTERPOSE(glDrawArrays)(GLenum mode, GLint first, GLsizei count);

// Render the depth decoder into the injector-owned colour target. The output
// is not consumed yet; this is the state-safe live-pass proof before switching
// receivers away from the known-good CPU mirror.
static bool run_gpu_depth_decode() {
    if (!g_gpuDecodeProgram || !g_gpuMirrorFbo || !g_realUseProgram) return false;
    GLint oldFbo=0, oldVp[4]={0,0,0,0}, oldProg=0, oldActive=GL_TEXTURE0, oldTex=0;
    GLboolean oldDepth=GL_TRUE, oldMask=GL_TRUE;
    gl::GetIntegerv(GL_FRAMEBUFFER_BINDING,&oldFbo);
    gl::GetIntegerv(GL_VIEWPORT,oldVp);
    gl::GetIntegerv(GL_CURRENT_PROGRAM,&oldProg);
    gl::GetIntegerv(GL_ACTIVE_TEXTURE,&oldActive);
    gl::GetBooleanv(GL_DEPTH_WRITEMASK,&oldMask);
    oldDepth=gl::IsEnabled(GL_DEPTH_TEST);
    gl::ActiveTexture(GL_TEXTURE0+SHADOW_UNIT);
    gl::GetIntegerv(GL_TEXTURE_BINDING_2D,&oldTex);
    gl::BindFramebuffer(GL_FRAMEBUFFER,g_gpuMirrorFbo);
    gl::Viewport(0,0,g_size,g_size);
    gl::Disable(GL_DEPTH_TEST); gl::DepthMask(GL_FALSE);
    gl::BindTexture(GL_TEXTURE_2D,g_depthTex);
    g_realUseProgram(g_gpuDecodeProgram);
    GLint t=gl::GetUniformLocation(g_gpuDecodeProgram,"nwnDepth");
    if(t>=0) gl::Uniform1i(t,SHADOW_UNIT);
    const bool priorOurPass=g_inOurPass; g_inOurPass=true;
    NWN_GL_INTERPOSE(glDrawArrays)(0x0004 /*GL_TRIANGLES*/,0,3);
    g_inOurPass=priorOurPass;
    g_realUseProgram((GLuint)oldProg);
    gl::BindTexture(GL_TEXTURE_2D,(GLuint)oldTex);
    gl::ActiveTexture((GLenum)oldActive);
    if(oldDepth) gl::Enable(GL_DEPTH_TEST); else gl::Disable(GL_DEPTH_TEST);
    gl::DepthMask(oldMask); gl::Viewport(oldVp[0],oldVp[1],oldVp[2],oldVp[3]);
    gl::BindFramebuffer(GL_FRAMEBUFFER,(GLuint)oldFbo);
    static bool logged=false; if(!logged){logged=true; fprintf(stderr,"[shadowmap] GPU depth decode draw executed\n");}
    return true;
}

// Phase G1: compile an injector-owned program. It is deliberately not drawn
// yet; compilation is the first observable proof that the GPU bridge can be
// self-contained and independent of NWN's on-disk shader resources.
static void build_gpu_decode_program() {
    static bool tried = false;
    if (tried || !eng::GlewCreateShader || !eng::GlewShaderSource ||
        !eng::GlewCompileShader || !eng::GlewGetShaderiv ||
        !eng::GlewCreateProgram || !eng::GlewAttachShader ||
        !eng::GlewLinkProgram || !eng::GlewGetProgramiv) return;
    void* cp = *eng::GlewCreateShader;
    void* sp = *eng::GlewShaderSource;
    void* xp = *eng::GlewCompileShader;
    void* gs = *eng::GlewGetShaderiv;
    void* pr = *eng::GlewCreateProgram;
    void* at = *eng::GlewAttachShader;
    void* lk = *eng::GlewLinkProgram;
    void* gp = *eng::GlewGetProgramiv;
    if (!cp || !sp || !xp || !gs || !pr || !at || !lk || !gp) return;
    tried = true;
    auto create = (CreateShader_t)cp;
    auto source = (ShaderSource_t)sp;
    auto compile = (CompileShader_t)xp;
    auto getShader = (GetShaderiv_t)gs;
    auto createProg = (CreateProgram_t)pr;
    auto attach = (AttachShader_t)at;
    auto link = (LinkProgram_t)lk;
    auto getProg = (GetProgramiv_t)gp;
    static const char* vs =
        "#version 330 compatibility\n"
        "void main(){ vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);"
        "gl_Position=vec4(p*2.0-1.0,0.0,1.0); }\n";
    static const char* fs =
        "#version 330 compatibility\n"
        "uniform sampler2DShadow nwnDepth;\n"
        "void main(){ vec2 uv=gl_FragCoord.xy/2048.0; float lo=0.0,hi=1.0;"
        "for(int i=0;i<8;++i){float m=(lo+hi)*0.5;"
        "if(texture(nwnDepth,vec3(uv,m))>0.5)lo=m;else hi=m;}"
        "gl_FragColor=vec4(lo,lo,lo,1.0); }\n";
    GLuint v = create(GL_VERTEX_SHADER), f = create(GL_FRAGMENT_SHADER);
    const GLint vl=(GLint)strlen(vs), fl=(GLint)strlen(fs);
    source(v,1,&vs,&vl); source(f,1,&fs,&fl); compile(v); compile(f);
    GLint okv=0, okf=0; getShader(v,GL_COMPILE_STATUS,&okv); getShader(f,GL_COMPILE_STATUS,&okf);
    if (!okv || !okf) { fprintf(stderr,"[shadowmap] GPU decode shader compile FAILED (vs=%d fs=%d)\n",okv,okf); return; }
    GLuint p=createProg(); attach(p,v); attach(p,f); link(p);
    GLint okp=0; getProg(p,GL_LINK_STATUS,&okp);
    if (!okp) { fprintf(stderr,"[shadowmap] GPU decode program link FAILED\n"); return; }
    g_gpuDecodeProgram=p;
    fprintf(stderr,"[shadowmap] GPU depth-decode helper compiled (program=%u)\n",p);
}

// The mapper terrain is the only receiver for now, because it exposes the
// required vPosView. Casters are broader: the original depth attachment got
// every world material for free, so the colour replacement must add a tiny
// depth-output branch to ordinary gl_FragColor scene shaders as well. The
// branch is inert in the normal camera pass and is enabled only while the
// replayed light pass is bound to our private FBO.
static bool inject_marker(std::string& source) {
    if (!g_injectTest) return false;
    const bool receiver = source.find("uniform float LightmapON") != std::string::npos;
    const bool casterOnly = !receiver && shadow_getenv("NWN_SHADOWMAP_COLORCAST_TEST") &&
                            source.find("gl_FragColor") != std::string::npos;
    // Reproject casters from the NORMAL camera matrices that the material
    // replay leaves in place.  m_vp_inv already maps the shader's vPosProj
    // back to world space, so this preserves skinning, per-part placement and
    // every stock vertex variant without guessing at a hidden m_m uniform.
    const bool vertexCaster = shadow_getenv("NWN_SHADOWMAP_VERTEX_REPROJECT") &&
                              source.find("gl_Position") != std::string::npos &&
                              source.find("m_vp_inv") != std::string::npos &&
                              source.find("void TransformMainCoordinates") != std::string::npos;
    if (!receiver && !casterOnly && !vertexCaster) return false;
    if (vertexCaster) {
        // vPosProj is local to TransformMainCoordinates(), not main().  The
        // first version appended to main and therefore broke every material
        // variant at compile time (magenta menus).  Patch the function which
        // owns the value instead; UI/postprocess shaders do not have it and
        // are excluded by the predicate above.
        const size_t fn = source.find("void TransformMainCoordinates");
        const size_t open = source.find('{', fn);
        if (open == std::string::npos) return false;
        int fnBraces = 0;
        size_t close = std::string::npos;
        for (size_t p = open; p < source.size(); ++p) {
            if (source[p] == '{') ++fnBraces;
            else if (source[p] == '}' && --fnBraces == 0) { close = p; break; }
        }
        if (close == std::string::npos) return false;
        static const char* kVertexDecl =
            "uniform float nwnCasterDepthPass;\n"
            "uniform mat4 nwnCasterLightVP;\n"
            "uniform mat4 nwnCasterCameraVPInv;\n";
        static const char* kVertexBody =
            "\n\t// light reprojection injected by libnwn_shadowmap.so\n"
            "\tif (nwnCasterDepthPass > 0.5) {\n"
            "\t\tvec4 nwnCasterWorld = nwnCasterCameraVPInv * vPosProj;\n"
            "\t\tgl_Position = nwnCasterLightVP * nwnCasterWorld;\n"
            "\t}\n";
        source.insert(fn, kVertexDecl);
        close += strlen(kVertexDecl);
        source.insert(close, kVertexBody);
        return true;
    }
    const size_t mainPos = source.rfind("void main");
    if (mainPos == std::string::npos) return false;
    const size_t open = source.find('{', mainPos);
    if (open == std::string::npos) return false;
    int braces = 0;
    for (size_t i = open; i < source.size(); ++i) {
        if (source[i] == '{') ++braces;
        else if (source[i] == '}' && --braces == 0) {
            if (casterOnly) {
                static const char* kCasterDecl =
                    "uniform float nwnCasterDepthPass;\n";
                static const char* kCasterBody =
                    "\n\t// caster branch injected by libnwn_shadowmap.so\n"
                    "\tif (nwnCasterDepthPass > 0.5) "
                    "gl_FragColor = vec4(vec3(gl_FragCoord.z), 1.0);\n";
                source.insert(mainPos, kCasterDecl);
                i += strlen(kCasterDecl);
                source.insert(i, kCasterBody);
                return true;
            }

            // Receiver declarations and comparison. COLORCAST_TEST reads the
            // ordinary colour attachment rather than the unsampleable depth
            // attachment; CPU fallback keeps the R32UI mirror intact.
            const bool gpu = !use_cpu_bridge() &&
                             (shadow_getenv("NWN_SHADOWMAP_GPU_TEST") != nullptr ||
                              shadow_getenv("NWN_SHADOWMAP_COLORCAST_TEST") != nullptr);
            const char* decl = gpu
                ? "uniform float nwnShadowmapInjectTest;\nuniform sampler2D texShadowMap;\nuniform mat4 mShadowMatrix;\nuniform float nwnCasterDepthPass;\n"
                : "uniform float nwnShadowmapInjectTest;\nuniform usampler2D texShadowMap;\nuniform mat4 mShadowMatrix;\nuniform float nwnCasterDepthPass;\n";
            source.insert(mainPos, decl);
            i += strlen(decl);
            std::string body = "\n\t// injected by libnwn_shadowmap.so\n"
                             "\tvec4 nwnLightClip = mShadowMatrix * vec4(vPosView, 1.0);\n"
                             "\tvec3 nwnDebug = vec3(1.0, 1.0, 0.0);\n" // yellow: w <= 0
                             "\tif (nwnLightClip.w > 0.0) {\n"
                             "\t\tvec3 nwnSC = nwnLightClip.xyz / nwnLightClip.w * 0.5 + 0.5;\n"
                             "\t\tif (nwnSC.z > 1.0) nwnDebug = vec3(1.0, 0.0, 1.0);\n" // magenta: past far
                             "\t\telse if (nwnSC.x < 0.0 || nwnSC.x > 1.0 || "
                             "nwnSC.y < 0.0 || nwnSC.y > 1.0) nwnDebug = vec3(0.0, 0.0, 1.0);\n" // blue: XY outside
                             "\t\telse {\n";
            body += gpu ? "\t\t\tfloat nwnDepth = texture2D(texShadowMap, nwnSC.xy).r;\n"
                        : "\t\t\tivec2 nwnTexel = ivec2(clamp(nwnSC.xy, 0.0, 0.999999) * 2048.0);\n\t\t\tfloat nwnDepth = float(texelFetch(texShadowMap, nwnTexel, 0).r) * (1.0 / 4294967295.0);\n";
            body +=
                             "\t\t\tnwnDebug = (nwnSC.z - 0.0025 > nwnDepth) "
                             "? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);\n"
                             "\t\t}\n"
                             "\t}\n";
            body += "\tif (nwnCasterDepthPass > 0.5) gl_FragColor = vec4(vec3(gl_FragCoord.z), 1.0);\n";
            // GPU colour-caster transport is now proven non-white by the
            // one-shot diagnostic. Show the same red/green receiver decision
            // as the known-good CPU mirror, but with a current-frame GPU map.
            body += gpu ? "\telse if (nwnShadowmapInjectTest > 0.5) gl_FragColor.rgb = nwnDebug;\n"
                        : "\telse gl_FragColor.rgb = nwnDebug;\n";
            source.insert(i, body);
            return true;
        }
    }
    return false;
}

// Add the accumulation side of the foliage experiment without activating it.
// The stock fragment template has one compatibility output and centralises
// alpha testing through ALPHA_DISCARD_VALUE, which gives us two stable source
// anchors. nwnOitPass defaults to zero in OpenGL, so every ordinary camera and
// shadow draw remains byte-for-byte equivalent until the future late replay
// explicitly sets the uniform.
static bool inject_oit_foliage_branch(std::string& source) {
    if (!nwn_oit_wants_foliage_shader_branch()) return false;
    std::string patched = source;
    const bool explicitContinuousAlpha =
        patched.find("#define NWN_CONTINUOUS_ALPHA_SHADER 1") !=
        std::string::npos;

    static const char* kOutput = "out vec4 compat_glFragColor;";
    static const char* kOutputs =
        "layout(location=0) out vec4 compat_glFragColor;\n"
        "layout(location=1) out vec4 nwnOitSum;\n"
        "layout(location=2) out vec4 nwnOitTranslucence;\n"
        "uniform int nwnOitPass;\n"
        "uniform int nwnOitFringeOnly;\n"
        "uniform float nwnOitCutoff;\n"
        "uniform float nwnOitAlphaGain;\n"
        "uniform int nwnOitNormalizeCutout;\n"
        "uniform float nwnOitNormalizePivot;\n"
        "uniform int nwnOitCoreResetPass;\n"
        "uniform int nwnOitDepthPass;\n"
        "uniform int nwnOitOpaqueCore;\n"
        "uniform int nwnA2cPass;\n"
        "uniform int nwnA2cEmitterPass;\n"
        "uniform sampler2DMS nwnA2cEmitterOpaqueDepth;\n"
        "uniform vec4 nwnA2cEmitterViewport;\n"
        "uniform float nwnA2cEmitterDepthBias;\n"
        "uniform int nwnA2cEmitterSamples;\n"
        "uniform sampler2DArrayShadow nwnA2cStaticDepth;\n"
        "uniform sampler2DArrayShadow nwnA2cDynamicDepth;\n"
        "uniform mat4 nwnA2cCameraVPInv;\n"
        "uniform mat4 nwnA2cCameraView;\n"
        "uniform mat4 nwnA2cLightVP[4];\n"
        "uniform vec4 nwnA2cClipFar;\n"
        "uniform vec4 nwnA2cViewport;\n"
        "uniform int nwnA2cDynamicLayers;\n"
        "uniform float nwnA2cShadowStrength;\n"
        "uniform float nwnA2cShadowBias;\n"
        "uniform float nwnA2cShadowBlend;\n"
        "uniform float nwnA2cShadowPcf;\n"
        "uniform float nwnA2cSelfIllumGuard;\n"
        "uniform vec4 nwnA2cLamps[128];\n"
        "uniform int nwnA2cLampCount;\n"
        "uniform float nwnA2cLampLift;\n"
        "uniform sampler2DArray nwnA2cLocalDepth;\n"
        "uniform mat4 nwnA2cLocalVP[32];\n"
        "uniform vec4 nwnA2cLocalPos[32];\n"
        "uniform float nwnA2cLocalRadius[32];\n"
        "uniform float nwnA2cLocalFade[32];\n"
        "uniform int nwnA2cLocalSlots;\n"
        "uniform float nwnA2cLocalStrength;\n"
        "uniform float nwnA2cLocalBias;\n"
        "uniform float nwnA2cLocalEdgeFade;\n"
        "uniform float nwnA2cLocalSoft;\n"
        "uniform float nwnA2cLocalNormalBias;\n"
        "uniform float nwnA2cLocalMinSep;\n"
        "uniform float nwnA2cLocalTanHalfFov;\n"
        "uniform float nwnA2cLocalLift;\n"
        "uniform float nwnA2cLocalFalloff;\n"
        "uniform vec2 nwnA2cLampFalloff;";
    const size_t outputAt = patched.find(kOutput);
    if (outputAt == std::string::npos) return false;
    patched.replace(outputAt, std::strlen(kOutput), kOutputs);

    // The template is concatenated from guarded includes and can contain the
    // same inactive definition more than once. Replace every exact definition;
    // the preprocessor will still select the same one it selected originally.
    static const char* kDiscard =
        "#define ALPHA_DISCARD_VALUE fAlphaDiscardValue";
    static const char* kOitDiscard =
        "#define ALPHA_DISCARD_VALUE (((nwnOitPass != 0) || (nwnA2cPass != 0)) ? 0.01 : "
        "((nwnOitDepthPass != 0) ? 0.25 : "
        "((nwnOitOpaqueCore != 0) ? nwnOitCutoff : fAlphaDiscardValue)))";
    size_t searchAt = 0;
    unsigned discardDefinitions = 0;
    while ((searchAt = patched.find(kDiscard, searchAt)) != std::string::npos) {
        patched.replace(searchAt, std::strlen(kDiscard), kOitDiscard);
        searchAt += std::strlen(kOitDiscard);
        ++discardDefinitions;
    }
    // Explicit custom shaders can compute coverage independently of
    // inc_standard's ALPHA_DISCARD_VALUE path. Their final gl_FragColor alpha
    // is authoritative, so the injected tail remains valid even when there is
    // no stock discard macro to rewrite.
    if (!discardDefinitions && !explicitContinuousAlpha) return false;

    const size_t mainAt = patched.rfind("void main");
    if (mainAt == std::string::npos) return false;
    const size_t open = patched.find('{', mainAt);
    if (open == std::string::npos) return false;
    int braces = 0;
    size_t close = std::string::npos;
    for (size_t i = open; i < patched.size(); ++i) {
        if (patched[i] == '{') ++braces;
        else if (patched[i] == '}' && --braces == 0) {
            close = i;
            break;
        }
    }
    if (close == std::string::npos) return false;

    static const char* kBody =
        "\n    // smooth-alpha MRT branch injected by libnwn_shadowmap.so\n"
        "    if (nwnA2cEmitterPass != 0) {\n"
        "        ivec2 nwnEmitterPixel=ivec2(gl_FragCoord.xy-nwnA2cEmitterViewport.xy);\n"
        "        float nwnOpaqueDepth=1.0;\n"
        "        for(int nwnSample=0;nwnSample<nwnA2cEmitterSamples&&nwnSample<16;++nwnSample)\n"
        "            nwnOpaqueDepth=min(nwnOpaqueDepth,texelFetch(nwnA2cEmitterOpaqueDepth,nwnEmitterPixel,nwnSample).r);\n"
        "        if(gl_FragCoord.z>nwnOpaqueDepth+nwnA2cEmitterDepthBias) discard;\n"
        "    } else if (nwnOitCoreResetPass != 0) {\n"
        "        gl_FragColor = vec4(0.0);\n"
        "        nwnOitSum = vec4(0.0);\n"
        "        nwnOitTranslucence = vec4(1.0, 0.0, 0.0, 0.0);\n"
        "    } else if (nwnOitPass != 0) {\n"
        "        float nwnOitAlpha = clamp(gl_FragColor.a, 0.0, 1.0);\n"
        "        if (nwnOitAlpha <= 0.01) discard;\n"
        "        if (nwnOitNormalizeCutout != 0)\n"
        "            nwnOitAlpha = clamp(nwnOitAlpha / max(nwnOitNormalizePivot, 0.01), 0.0, 1.0);\n"
        "        nwnOitAlpha = 1.0 - pow(1.0 - nwnOitAlpha, max(nwnOitAlphaGain, 0.01));\n"
        "        if (nwnOitFringeOnly != 0 && nwnOitAlpha >= nwnOitCutoff) discard;\n"
        "        gl_FragColor = vec4(gl_FragColor.rgb * nwnOitAlpha, 1.0);\n"
        "        nwnOitSum = vec4(nwnOitAlpha, 0.0, 0.0, 1.0);\n"
        "        nwnOitTranslucence = vec4(0.0, 0.0, 0.0, 1.0 - nwnOitAlpha);\n"
        "    } else if (nwnA2cPass != 0) {\n"
        "        float nwnDirLit = nwnA2cDirectionalShadow();\n"
        "        if (nwnA2cSelfIllumGuard < 1.0) {\n"
        "            float nwnLum = max(gl_FragColor.r, max(gl_FragColor.g, gl_FragColor.b));\n"
        "            float nwnKeepShadow = 1.0-smoothstep(nwnA2cSelfIllumGuard,1.0,nwnLum);\n"
        "            nwnDirLit = 1.0-(1.0-nwnDirLit)*nwnKeepShadow;\n"
        "        }\n"
        "        gl_FragColor.rgb *= min(nwnDirLit, nwnA2cLocalShadow());\n"
        "    } else if (nwnOitOpaqueCore != 0) {\n"
        "        // Stock shaders already test ALPHA_DISCARD_VALUE, but custom\n"
        "        // shaders may compute their real coverage only in the final\n"
        "        // output. Apply the same core cutoff to that final alpha.\n"
        "        if (clamp(gl_FragColor.a, 0.0, 1.0) < nwnOitCutoff) discard;\n"
        "        gl_FragColor.a = 1.0;\n"
        "    }\n";
    patched.insert(close, kBody);

    static const char* kA2cShadowHelpers =
        "float nwnA2cLayerLit(int layer, vec4 worldPos) {\n"
        "    vec4 lp = nwnA2cLightVP[layer] * worldPos;\n"
        "    if (lp.w <= 0.0) return 1.0;\n"
        "    vec3 s = lp.xyz / lp.w * 0.5 + 0.5;\n"
        "    if (s.x <= 0.0 || s.x >= 1.0 || s.y <= 0.0 || s.y >= 1.0 || s.z <= 0.0 || s.z >= 1.0) return 1.0;\n"
        "    vec2 texel = 1.0 / vec2(textureSize(nwnA2cStaticDepth, 0).xy);\n"
        "    float total = 0.0;\n"
        "    for (int y=-1; y<=1; ++y) for (int x=-1; x<=1; ++x) {\n"
        "        vec2 uv = s.xy + vec2(float(x),float(y)) * texel * nwnA2cShadowPcf;\n"
        "        float a = texture(nwnA2cStaticDepth, vec4(uv,float(layer),s.z-nwnA2cShadowBias));\n"
        "        float b = layer < nwnA2cDynamicLayers ? texture(nwnA2cDynamicDepth, vec4(uv,float(layer),s.z-nwnA2cShadowBias)) : 1.0;\n"
        "        total += min(a,b);\n"
        "    }\n"
        "    return total / 9.0;\n"
        "}\n"
        "float nwnA2cDirectionalShadow() {\n"
        "    if (nwnA2cShadowStrength <= 0.0) return 1.0;\n"
        "    vec2 uv = (gl_FragCoord.xy - nwnA2cViewport.xy) / nwnA2cViewport.zw;\n"
        "    vec4 w = nwnA2cCameraVPInv * vec4(uv*2.0-1.0, gl_FragCoord.z*2.0-1.0, 1.0);\n"
        "    if (abs(w.w) < 1e-6) return 1.0;\n"
        "    w /= w.w;\n"
        "    float z = -((nwnA2cCameraView*w).z);\n"
        "    if (z <= 0.0 || z > nwnA2cClipFar.w) return 1.0;\n"
        "    int layer = z <= nwnA2cClipFar.x ? 0 : (z <= nwnA2cClipFar.y ? 1 : (z <= nwnA2cClipFar.z ? 2 : 3));\n"
        "    float lit = nwnA2cLayerLit(layer,w);\n"
        "    if (nwnA2cShadowBlend > 0.0 && layer < 3) {\n"
        "        float edge = layer==0 ? nwnA2cClipFar.x : (layer==1 ? nwnA2cClipFar.y : nwnA2cClipFar.z);\n"
        "        float from = max(0.0, edge-nwnA2cShadowBlend);\n"
        "        if (z > from) lit = mix(lit,nwnA2cLayerLit(layer+1,w),smoothstep(from,edge,z));\n"
        "    }\n"
        "    float shade=nwnA2cShadowStrength*(1.0-lit);\n"
        "    float lampLit=0.0;\n"
        "    if(shade>0.0&&nwnA2cLampLift>0.0){\n"
        "        for(int li=0;li<nwnA2cLampCount&&li<128;++li){\n"
        "            float r2=nwnA2cLamps[li].w; if(r2<=0.0) continue;\n"
        "            vec3 off=w.xyz-nwnA2cLamps[li].xyz;\n"
        "            float d2=dot(off,off); if(d2>=r2) continue;\n"
        "            float f=d2/r2;\n"
        "            float att=(1.0-f)/max(1e-4,nwnA2cLampFalloff.x+nwnA2cLampFalloff.y*f);\n"
        "            lampLit=max(lampLit,clamp(att,0.0,1.0));\n"
        "        }\n"
        "    }\n"
        "    shade*=1.0-clamp(nwnA2cLampLift*lampLit,0.0,1.0);\n"
        "    return 1.0-shade;\n"
        "}\n"
        "float nwnA2cLocalShadow() {\n"
        "    if (nwnA2cLocalStrength <= 0.0 || nwnA2cLocalSlots <= 0) return 1.0;\n"
        "    vec2 uv = (gl_FragCoord.xy - nwnA2cViewport.xy) / nwnA2cViewport.zw;\n"
        "    vec4 w = nwnA2cCameraVPInv * vec4(uv*2.0-1.0, gl_FragCoord.z*2.0-1.0, 1.0);\n"
        "    if (abs(w.w) < 1e-6) return 1.0;\n"
        "    w /= w.w;\n"
        "    vec3 dxw=dFdx(w.xyz), dyw=dFdy(w.xyz);\n"
        "    vec3 nrm=cross(dxw,dyw);\n"
        "    float nl=length(nrm); nrm=nl>1e-6?nrm/nl:vec3(0.0);\n"
        "    float step2=max(dot(dxw,dxw),dot(dyw,dyw));\n"
        "    nrm*=1.0-smoothstep(0.02,0.09,step2);\n"
        "    float best=0.0;\n"
        "    for(int sl=0;sl<nwnA2cLocalSlots&&sl<32;++sl){\n"
        "        float rr=nwnA2cLocalRadius[sl]; if(rr<=0.0) continue;\n"
        "        vec3 off=w.xyz-nwnA2cLocalPos[sl].xyz;\n"
        "        float d2=dot(off,off); if(d2>=rr*rr) continue;\n"
        "        float dist=sqrt(max(d2,1e-8));\n"
        "        float texw=nwnA2cLocalTanHalfFov*dist/float(textureSize(nwnA2cLocalDepth,0).x);\n"
        "        vec3 nlight=nrm*(dot(nrm,nwnA2cLocalPos[sl].xyz-w.xyz)>=0.0?1.0:-1.0);\n"
        "        vec4 lp=nwnA2cLocalVP[sl]*vec4(w.xyz+nlight*texw*nwnA2cLocalNormalBias,1.0);\n"
        "        if(lp.w<=0.0) continue;\n"
        "        vec3 s=lp.xyz/lp.w*0.5+0.5;\n"
        "        if(s.x<=0.0||s.x>=1.0||s.y<=0.0||s.y>=1.0||s.z<=0.0||s.z>=1.0) continue;\n"
        "        vec2 eg=min(s.xy,vec2(1.0)-s.xy);\n"
        "        float edge=clamp(min(eg.x,eg.y)/max(1e-4,nwnA2cLocalEdgeFade),0.0,1.0);\n"
        "        edge*=clamp((1.0-s.z)/max(1e-4,nwnA2cLocalEdgeFade*0.5),0.0,1.0);\n"
        "        edge=edge*edge*(3.0-2.0*edge);\n"
        "        float zn=max(0.05,min(0.5,rr*0.01));\n"
        "        float zf=max(20.0,rr*2.0+max(0.0,nwnA2cLocalLift));\n"
        "        float dzdd=zn*zf/max(1e-6,dist*dist*(zf-zn));\n"
        "        float need=nwnA2cLocalBias+nwnA2cLocalMinSep*dzdd;\n"
        "        float lit=0.0;\n"
        "        if(nwnA2cLocalSoft<=0.0){\n"
        "            float st=texture(nwnA2cLocalDepth,vec3(s.xy,float(sl))).r;\n"
        "            lit=(s.z-need<=st)?1.0:0.0;\n"
        "        }else{\n"
        "            vec2 tx=1.0/vec2(textureSize(nwnA2cLocalDepth,0).xy);\n"
        "            for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){\n"
        "                float st=texture(nwnA2cLocalDepth,vec3(s.xy+vec2(float(x),float(y))*tx*nwnA2cLocalSoft,float(sl))).r;\n"
        "                lit+=(s.z-need<=st)?1.0:0.0;\n"
        "            }\n"
        "            lit/=9.0;\n"
        "        }\n"
        "        float f=d2/(rr*rr);\n"
        "        float att=clamp((1.0-f)/max(1e-4,nwnA2cLampFalloff.x+nwnA2cLampFalloff.y*f),0.0,1.0);\n"
        "        att=pow(att,max(0.05,nwnA2cLocalFalloff));\n"
        "        best=max(best,clamp(1.0-lit,0.0,1.0)*att*edge*nwnA2cLocalFade[sl]);\n"
        "    }\n"
        "    return 1.0-nwnA2cLocalStrength*best;\n"
        "}\n";
    patched.insert(mainAt, kA2cShadowHelpers);
    source.swap(patched);
    return true;
}

// Material-mode discovery is intentionally separate from the much larger OIT
// branch. NWN only uploads custom MTR parameters for active uniforms, so the
// reserved negative guard keeps NWN_ALPHA_MODE live without changing any
// documented mode (all valid modes are non-negative). The census never sets
// this uniform itself and never changes GL state.
static bool inject_material_mode_census(std::string& source) {
    if (!nwn_oit_wants_material_mode_census() ||
        source.find("NWN_ALPHA_MODE") != std::string::npos)
        return false;

    // The stock template contains several preprocessor-selected main
    // definitions. rfind("void main") can select an inactive branch, which
    // makes both the declaration and use disappear before link. Put the
    // declaration in unconditional global scope immediately after #version,
    // then instrument every candidate main; the preprocessor keeps exactly the
    // guard belonging to the active variant.
    const size_t versionAt = source.find("#version");
    if (versionAt == std::string::npos) return false;
    const size_t versionEnd = source.find('\n', versionAt);
    if (versionEnd == std::string::npos) return false;
    static const char* declaration = "uniform int NWN_ALPHA_MODE;\n";
    source.insert(versionEnd + 1, declaration);

    static const char* guard =
        "\n    // Negative values are reserved; valid census modes are inert.\n"
        "    if (NWN_ALPHA_MODE < 0) discard;\n";
    size_t searchAt = versionEnd + 1 + std::strlen(declaration);
    unsigned mains = 0;
    while ((searchAt = source.find("void main", searchAt)) != std::string::npos) {
        const size_t open = source.find('{', searchAt);
        if (open == std::string::npos) break;
        source.insert(open + 1, guard);
        searchAt = open + 1 + std::strlen(guard);
        ++mains;
    }
    return mains != 0;
}

// Particle emitters use a different stock fragment family from alpha-discard
// foliage. Give every ordinary scene-colour fragment a tiny, inert opaque-depth
// branch; live draw classification still requires the selected area's measured
// particle bucket/blend signature, so UI and non-particle materials never
// activate it.
static bool inject_a2c_emitter_branch(std::string& source) {
    if (!nwn_oit_wants_a2c_emitter_shader_branch() ||
        source.find("nwnA2cEmitterPass") != std::string::npos ||
        source.find("gl_FragColor") == std::string::npos)
        return false;
    const size_t mainAt = source.rfind("void main");
    if (mainAt == std::string::npos) return false;
    static const char* declarations =
        "uniform int nwnA2cEmitterPass;\n"
        "uniform sampler2DMS nwnA2cEmitterOpaqueDepth;\n"
        "uniform vec4 nwnA2cEmitterViewport;\n"
        "uniform float nwnA2cEmitterDepthBias;\n"
        "uniform int nwnA2cEmitterSamples;\n";
    source.insert(mainAt, declarations);
    const size_t shiftedMain = mainAt + std::strlen(declarations);
    const size_t open = source.find('{', shiftedMain);
    if (open == std::string::npos) return false;
    int braces = 0;
    size_t close = std::string::npos;
    for (size_t i = open; i < source.size(); ++i) {
        if (source[i] == '{') ++braces;
        else if (source[i] == '}' && --braces == 0) { close = i; break; }
    }
    if (close == std::string::npos) return false;
    static const char* body =
        "\nif(nwnA2cEmitterPass!=0){\n"
        " ivec2 p=ivec2(gl_FragCoord.xy-nwnA2cEmitterViewport.xy);\n"
        " float d=1.0;\n"
        " for(int s=0;s<nwnA2cEmitterSamples&&s<16;++s)\n"
        "  d=min(d,texelFetch(nwnA2cEmitterOpaqueDepth,p,s).r);\n"
        " if(gl_FragCoord.z>d+nwnA2cEmitterDepthBias) discard;\n"
        "}\n";
    source.insert(close, body);
    return true;
}

extern "C" void my_shader_source(GLuint shader, GLsizei count,
                                   const char* const* string, const GLint* length) {
    const bool dumpVertex = shadow_getenv("NWN_SHADOWMAP_DUMP_VERTEX") != nullptr;
    const bool dumpFragment = shadow_getenv("NWN_SHADOWMAP_DUMP_FRAGMENT") != nullptr;
    const bool staticAlphaProbe = g_cascadeStaticAlphaCapture ||
                                  g_cascadeStaticAlphaReceiver;
    const bool oitFoliageProbe = nwn_oit_needs_shader_sources();
    if (!g_realShaderSource || (!g_injectTest && !dumpVertex && !dumpFragment &&
                                !staticAlphaProbe && !oitFoliageProbe) ||
        count <= 0 || !string) {
        if (g_realShaderSource) g_realShaderSource(shader, count, string, length);
        return;
    }
    std::string combined;
    for (GLsizei i = 0; i < count; ++i) {
        if (!string[i]) continue;
        const GLint n = length ? length[i] : -1;
        if (n >= 0) combined.append(string[i], (size_t)n);
        else        combined.append(string[i]);
    }
    // `mzmap_006` has no custom shader; it selects NWN's compiled stock alpha
    // variant (`NO_DISCARD 0`) which exposes fAlphaDiscardValue. Persist only
    // the real fragment object identity; source and runtime shader stay intact.
    const bool stockAlphaVariant =
        combined.find("#define NO_DISCARD 0") != std::string::npos &&
        combined.find("fAlphaDiscardValue") != std::string::npos;
    const bool customContinuousAlphaVariant =
        combined.find("#define NWN_CONTINUOUS_ALPHA_SHADER 1") !=
        std::string::npos;
    bool oitFoliageInjected = false;
    bool materialModeInjected = false;
    bool a2cEmitterInjected = false;
    if (stockAlphaVariant || customContinuousAlphaVariant) {
        if (staticAlphaProbe) {
            bool seen = false;
            for (GLuint id : g_cascadeStaticAlphaFragments)
                if (id == shader) { seen = true; break; }
            if (!seen) {
                g_cascadeStaticAlphaFragments.push_back(shader);
                fprintf(stderr, "[shadowmap][alpha] classified static AlphaDiscard fragment=%u\n",
                        shader);
            }
        }
        // The shadow classifier historically needed only the two stock-source
        // markers above. Keep that accepted behaviour unchanged. OIT is more
        // specific: its future MRT transform needs the ordinary fragment
        // colour output, so do not register a vertex or non-colour stage that
        // happens to carry the same preprocessor text.
        if (oitFoliageProbe &&
            combined.find("gl_FragColor") != std::string::npos) {
            nwn_oit_note_foliage_fragment(shader);
            materialModeInjected = inject_material_mode_census(combined);
            if (materialModeInjected)
                fprintf(stderr, "[oit][material-mode] injected dormant "
                                "NWN_ALPHA_MODE census into fragment=%u\n", shader);
            oitFoliageInjected = inject_oit_foliage_branch(combined);
            if (oitFoliageInjected)
                fprintf(stderr, "[oit][foliage] injected inert MRT branch into "
                                "fragment=%u\n", shader);
        }
    }
    if (oitFoliageProbe && !oitFoliageInjected &&
        combined.find("gl_FragColor") != std::string::npos) {
        a2cEmitterInjected = inject_a2c_emitter_branch(combined);
        if (a2cEmitterInjected)
            nwn_oit_note_emitter_fragment(shader);
    }
    // Static casters drift because their vertex transform is still unknown.
    // Dump only vertex-stage sources on explicit request; this is read-only
    // diagnostics and lets us identify the exact model/MVP uniform rather
    // than inject a guessed transform into every NWN material.
    if (dumpVertex &&
        combined.find("gl_Position") != std::string::npos) {
        static std::vector<GLuint> dumped;
        bool seen = false;
        for (GLuint id : dumped) if (id == shader) { seen = true; break; }
        if (!seen) {
            dumped.push_back(shader);
            char path[96];
            snprintf(path, sizeof(path), "nwn_shadow_vertex_%u.glsl", shader);
            FILE* fp = open_dump_file(path);
            if (fp) {
                (void)fwrite(combined.data(), 1, combined.size(), fp);
                fclose(fp);
                fprintf(stderr, "[shadowmap] dumped vertex shader %u -> %s\n", shader, path);
            }
        }
    }
    // Alpha/card classification must come from the engine's real fragment
    // source, not from guessing a bucket number.  This branch is read-only:
    // it writes the source exactly as supplied and never calls inject_marker.
    if (dumpFragment &&
        combined.find("gl_FragColor") != std::string::npos) {
        static std::vector<GLuint> dumped;
        bool seen = false;
        for (GLuint id : dumped) if (id == shader) { seen = true; break; }
        if (!seen) {
            dumped.push_back(shader);
            char path[96];
            snprintf(path, sizeof(path), "nwn_shadow_fragment_%u.glsl", shader);
            FILE* fp = open_dump_file(path);
            if (fp) {
                (void)fwrite(combined.data(), 1, combined.size(), fp);
                fclose(fp);
                fprintf(stderr, "[shadowmap] dumped fragment shader %u -> %s\n", shader, path);
            }
        }
    }
    const bool markerInjected = inject_marker(combined);
    if (!materialModeInjected && !oitFoliageInjected &&
        !a2cEmitterInjected && !markerInjected) {
        g_realShaderSource(shader, count, string, length);
        return;
    }
    const char* patched = combined.c_str();
    const GLint patchedLen = (GLint)combined.size();
    g_realShaderSource(shader, 1, &patched, &patchedLen);
    static int logged = 0;
    if (logged++ < 4)
        fprintf(stderr, "[shadowmap] submitted patched runtime shader %u\n", shader);
}

// Uniform push only -- chaining is the caller's job, because we reach this from
// two different interception paths.
static void trace_receiver_camera(GLuint prog, const ProgSlot* slot) {
    if (!shadow_getenv("NWN_SHADOWMAP_RECEIVER_TRACE") || !slot ||
        slot->locViewInv < 0 || !g_haveCamView || !gl::GetUniformfv) return;
    static double next = 0.0;
    const double now = now_seconds();
    if (now < next) return;
    next = now + 0.50;
    float actual[16], expected[16];
    gl::GetUniformfv(prog, slot->locViewInv, actual);
    mat4_rigid_inverse(g_camView, expected);
    float delta = 0.0f;
    for (int i = 0; i < 16; ++i)
        delta = std::max(delta, std::fabs(actual[i] - expected[i]));
    fprintf(stderr, "[shadowmap] receiver camera delta prog=%u max=%.7f "
                    "liveT=(%.3f %.3f %.3f) latchedT=(%.3f %.3f %.3f)\n",
            prog, delta, actual[12], actual[13], actual[14],
            expected[12], expected[13], expected[14]);
}

static bool shadow_on_program(GLuint prog) {
    if (!prog || !g_haveShadowMat || !gl::GetUniformLocation) return false;

    ProgSlot* slot = nullptr;
    for (int i = 0; i < g_progCount; ++i)
        if (g_progs[i].prog == prog) { slot = &g_progs[i]; break; }
    if (!slot) {
        if (g_progCount >= (int)(sizeof(g_progs)/sizeof(g_progs[0]))) return false;
        slot = &g_progs[g_progCount++];
        slot->prog   = prog;
        slot->locTex = gl::GetUniformLocation(prog, U_SHADOW_TEX);
        slot->locMat = gl::GetUniformLocation(prog, U_SHADOW_MAT);
        slot->locTest = gl::GetUniformLocation(prog, U_INJECT_TEST);
        slot->locViewInv = gl::GetUniformLocation(prog, U_VIEW_INV);
        slot->locVPInv = gl::GetUniformLocation(prog, "m_vp_inv");
        slot->locCaster = gl::GetUniformLocation(prog, U_CASTER_PASS);
        slot->locCasterLightVP = gl::GetUniformLocation(prog, U_CASTER_LIGHT_VP);
        slot->locCasterCameraVPInv = gl::GetUniformLocation(prog, U_CASTER_CAMERA_VP_INV);
        slot->locModel = gl::GetUniformLocation(prog, U_MODEL);
        slot->locModelView = gl::GetUniformLocation(prog, U_MODEL_VIEW);
        slot->locModelViewProj = gl::GetUniformLocation(prog, U_MODEL_VIEW_PROJ);
        slot->locView = gl::GetUniformLocation(prog, U_VIEW);
        slot->locProj = gl::GetUniformLocation(prog, U_PROJ);
        slot->haveModel = false;
        slot->haveCameraVPInv = false;
        slot->frame  = -1;
        if (slot->locTex >= 0 || slot->locMat >= 0) ++g_progHits;
    }
    if (slot->locTex < 0 && slot->locMat < 0 && slot->locTest < 0 &&
        slot->locCaster < 0) return false;

    // The main camera render happens before our replay. Its program bind set
    // nwnCasterDepthPass=0 for this frame, so that ordinary once-per-frame
    // cache must never suppress the replay's 1. Push the only light-pass
    // switch per draw; the injector controls the color mask around it.
    if (g_inOurPass) {
        const bool vertexReproject = shadow_getenv("NWN_SHADOWMAP_VERTEX_REPROJECT") &&
                                     slot->locCasterCameraVPInv >= 0;
        // A newly linked material has not necessarily reached one normal
        // camera draw yet.  Never feed it the default zero camera inverse:
        // leave that one caster on the established engine path until captured.
        const bool casterEnabled = slot->locCaster >= 0 &&
                                   (!vertexReproject || slot->haveCameraVPInv);
        if (slot->locCaster >= 0 && gl::Uniform1f)
            gl::Uniform1f(slot->locCaster, casterEnabled ? 1.0f : 0.0f);
        if (slot->locCasterLightVP >= 0 && g_haveActiveLightMatrices &&
            gl::UniformMatrix4fv)
            gl::UniformMatrix4fv(slot->locCasterLightVP, 1, GL_FALSE,
                                 g_activeLightWorldVP);
        if (slot->locCasterCameraVPInv >= 0 && slot->haveCameraVPInv &&
            gl::UniformMatrix4fv)
            gl::UniformMatrix4fv(slot->locCasterCameraVPInv, 1, GL_FALSE,
                                 slot->cameraVPInv);
        return casterEnabled;
    }
    trace_receiver_camera(prog, slot);
    // A receiver's m_view_inv can change while the program remains bound.  Do
    // this per draw: the map was rendered against g_shadowSourceView, whereas
    // this terrain vertex is in the LIVE camera view of the current draw.
    // The old experiment used mutable g_camView for both sides; after a pan it
    // no longer described the camera that produced this map, causing shadows
    // to drift with the camera.
    if (slot->locTex >= 0 && gl::Uniform1i)
        gl::Uniform1i(slot->locTex, SHADOW_UNIT);
    if (slot->locMat >= 0 && gl::UniformMatrix4fv) {
        float receiverMat[16];
        const float* mat = g_shadowMat;
        if (slot->locViewInv >= 0 && g_haveLightWorldVP &&
            gl::GetUniformfv) {
            float currentViewInv[16];
            gl::GetUniformfv(prog, slot->locViewInv, currentViewInv);
            // Direct form: vPosView -> current world -> exact light clip.
            // This deliberately avoids the camera snapshot which proved to be
            // mutable across NWN's auxiliary render passes.
            mat4_mul(g_lightWorldVP, currentViewInv, receiverMat);
            mat = receiverMat;
        }
        gl::UniformMatrix4fv(slot->locMat, 1, GL_FALSE, mat);
    }
    if (slot->locTest >= 0 && gl::Uniform1f)
        gl::Uniform1f(slot->locTest, g_injectTest ? 1.0f : 0.0f);
    if (slot->locCaster >= 0 && gl::Uniform1f)
        gl::Uniform1f(slot->locCaster, 0.0f);

    static int testLogged = 0;
    if (g_injectTest && testLogged < 8) {
        ++testLogged;
        fprintf(stderr, "[shadowmap] inject uniform: program=%u loc=%d value=%s frame=%ld\n",
                prog, slot->locTest, g_injectTest ? "1" : "0", g_frames);
    }

    static bool matrixLogged = false;
    if (g_injectTest && !matrixLogged && slot->locViewInv >= 0 && gl::GetUniformfv) {
        matrixLogged = true;
        float vi[16];
        float ci[16];
        gl::GetUniformfv(prog, slot->locViewInv, vi);
        mat4_rigid_inverse(g_camView, ci);
        fprintf(stderr, "[shadowmap] terrain m_view_inv (rows):\n"
                        "[shadowmap]   % .4f % .4f % .4f % .4f\n"
                        "[shadowmap]   % .4f % .4f % .4f % .4f\n"
                        "[shadowmap]   % .4f % .4f % .4f % .4f\n"
                        "[shadowmap]   % .4f % .4f % .4f % .4f\n",
                vi[0],vi[4],vi[8],vi[12], vi[1],vi[5],vi[9],vi[13],
                vi[2],vi[6],vi[10],vi[14], vi[3],vi[7],vi[11],vi[15]);
        fprintf(stderr, "[shadowmap] latched camera inverse (rows):\n"
                        "[shadowmap]   % .4f % .4f % .4f % .4f\n"
                        "[shadowmap]   % .4f % .4f % .4f % .4f\n"
                        "[shadowmap]   % .4f % .4f % .4f % .4f\n"
                        "[shadowmap]   % .4f % .4f % .4f % .4f\n",
                ci[0],ci[4],ci[8],ci[12], ci[1],ci[5],ci[9],ci[13],
                ci[2],ci[6],ci[10],ci[14], ci[3],ci[7],ci[11],ci[15]);
    }

    // Read the sampler back. If it does not report SHADOW_UNIT, the write did
    // not land and the shader is sampling whatever unit it defaulted to --
    // which is 0, the diffuse texture.
    static int verified = 0;
    if (verified < 6 && slot->locTex >= 0 && gl::GetUniformiv) {
        ++verified;
        GLint readback = -999;
        gl::GetUniformiv(prog, slot->locTex, &readback);
        fprintf(stderr, "[shadowmap] prog %u: locTex=%d locMat=%d "
                        "sampler reads %d (want %d)%s\n",
                prog, slot->locTex, slot->locMat, readback, SHADOW_UNIT,
                readback == SHADOW_UNIT ? "" : "   *** MISMATCH ***");
    }
    return slot->locCaster >= 0;
}

// ONE-ENTRY CACHE, and it matters only on Windows -- where these wrappers are
// actually called. Measured: ~4700 glUniformMatrix4fv per frame there against
// ZERO on Linux (the engine reaches the driver by a different route). Every one
// of those did a linear scan of ~160 program slots, so roughly 750k comparisons
// per frame that Linux never pays.
//
// Consecutive uniform uploads are nearly always for the same program, so a
// single-entry cache hits almost every time. The index is re-validated against
// the array rather than trusted, so a slot being reused or the table growing
// cannot hand back the wrong program.
static int g_progSlotCacheIdx = -1;

static ProgSlot* find_prog_slot(GLuint prog) {
    if (g_progSlotCacheIdx >= 0 && g_progSlotCacheIdx < g_progCount &&
        g_progs[g_progSlotCacheIdx].prog == prog)
        return &g_progs[g_progSlotCacheIdx];
    for (int i = 0; i < g_progCount; ++i)
        if (g_progs[i].prog == prog) { g_progSlotCacheIdx = i; return &g_progs[i]; }
    return nullptr;
}

// The engine's normal material replay updates per-object uniforms but does not
// rebuild its camera-derived values after we substitute the matrix stack.
// vs_shadowvol establishes the correct relationship.  Preserve m_m as the
// engine supplied it, then make m_view/m_proj/m_mv/m_mvp coherent with the
// light.  This hook is active ONLY while g_inOurPass is true.
static void push_light_model_transforms(ProgSlot* slot) {
    if (!slot || !g_realUniformMatrix4fv || !g_haveActiveLightMatrices) return;
    if (slot->locView >= 0)
        g_realUniformMatrix4fv(slot->locView, 1, GL_FALSE, g_activeLightView);
    if (slot->locProj >= 0)
        g_realUniformMatrix4fv(slot->locProj, 1, GL_FALSE, g_activeLightProj);
    if (!slot->haveModel) return;
    float mv[16], mvp[16];
    mat4_mul(g_activeLightView, slot->model, mv);
    mat4_mul(g_activeLightProj, mv, mvp);
    if (slot->locModelView >= 0)
        g_realUniformMatrix4fv(slot->locModelView, 1, GL_FALSE, mv);
    if (slot->locModelViewProj >= 0)
        g_realUniformMatrix4fv(slot->locModelViewProj, 1, GL_FALSE, mvp);
}

// Capture the exact inverse camera VP which the material used in the NORMAL
// camera pass.  During the later light replay the engine may rebuild the live
// m_vp_inv while retaining an old m_mvp; mixing those two was the all-lit,
// invalid-depth failure from the first vertex-reprojection attempt.
static void capture_camera_vp_inv(GLint location, GLsizei count,
                                  GLboolean transpose, const GLfloat* value) {
    if (g_inOurPass || count != 1 || transpose != GL_FALSE || !value ||
        !gl::GetIntegerv) return;
    // We INTERCEPT glUseProgram, so the current program is already known and
    // asking the driver for it is pure waste -- on every matrix upload, which
    // is thousands of times a frame. shadow_before_draw() carries a comment
    // about exactly this ("glGetIntegerv is a driver-synchronising query and
    // this runs on EVERY draw call"); the lesson was applied to the draw path
    // and missed here. It costs more on Windows, where GL 1.1 entry points
    // dispatch through opengl32.dll's per-thread current-context lookup rather
    // than landing straight in the driver.
    GLint current = (GLint)g_curProgram;
    if (current <= 0) gl::GetIntegerv(GL_CURRENT_PROGRAM, &current);
    ProgSlot* slot = current > 0 ? find_prog_slot((GLuint)current) : nullptr;
    if (!slot || location != slot->locVPInv) return;
    std::memcpy(slot->cameraVPInv, value, sizeof(slot->cameraVPInv));
    // Only the selected area Scene owns the camera used by the shadow map.
    // Several full-size auxiliary scenes upload an equally valid m_vp_inv
    // later in the frame.  Accepting those made the world reconstruction jump
    // whenever orbit/pan/zoom caused their camera to diverge from the area.
    if (g_inSceneRender && g_renderingScene == g_areaScene) {
        std::memcpy(g_worldCameraVPInv, value, sizeof(g_worldCameraVPInv));
        g_haveWorldCameraVPInv = true;
    }
    const bool wasCaptured = slot->haveCameraVPInv;
    slot->haveCameraVPInv = true;
    static int logged = 0;
    if (!wasCaptured && logged++ < 8)
        fprintf(stderr, "[shadowmap] captured normal camera VP inverse for program %u\n",
                slot->prog);
}

// Diagnostic only.  The previous two reprojection attempts guessed how the
// engine pairs m_mvp with m_vp_inv during bucket replay.  Record the actual
// uploads in the normal camera pass and the light pass before attempting a
// third transform.  The cap keeps a single run readable.
static void trace_matrix_upload(GLint location, GLsizei count,
                                GLboolean transpose, const GLfloat* value) {
    // THE ENV LOOKUP MUST NOT BE ON THIS PATH. This runs on every
    // glUniformMatrix4fv the engine makes -- thousands per frame -- and
    // shadow_getenv() is dramatically more expensive on Windows than on Linux:
    // a getenv() miss there is followed by a linear strcmp scan of the 27-entry
    // kWinDefaultEnv table, and this variable is never set, so every upload
    // paid the full scan. Same source, same call, an order of magnitude apart
    // by platform -- which is exactly the shape of the symptom (a Windows-only
    // cost that no panel toggle could touch, because it is not a rendering
    // pass at all). Read once.
    static const bool enabled = shadow_getenv("NWN_SHADOWMAP_UNIFORM_TRACE") != nullptr;
    if (!enabled || count != 1 ||
        transpose != GL_FALSE || !value || !gl::GetIntegerv) return;
    GLint current = 0;
    gl::GetIntegerv(GL_CURRENT_PROGRAM, &current);
    ProgSlot* slot = current > 0 ? find_prog_slot((GLuint)current) : nullptr;
    if (!slot) return;
    const char* name = nullptr;
    if (location == slot->locModelViewProj) name = "m_mvp";
    else if (location == slot->locModelView) name = "m_mv";
    else if (location == slot->locVPInv) name = "m_vp_inv";
    else if (location == slot->locViewInv) name = "m_view_inv";
    else if (location == slot->locView) name = "m_view";
    else if (location == slot->locProj) name = "m_proj";
    if (!name) return;
    static unsigned normal = 0, light = 0;
    unsigned& n = g_inOurPass ? light : normal;
    if (n++ >= 24) return;
    fprintf(stderr, "[shadowmap] matrix %s prog=%u %s T=(%.3f %.3f %.3f) "
                    "diag=(%.3f %.3f %.3f %.3f)\n",
            g_inOurPass ? "LIGHT" : "CAMERA", (unsigned)current, name,
            value[12], value[13], value[14], value[0], value[5], value[10], value[15]);
}


// Read NWN's OWN fog range, so the shadow fade needs no manual tuning.
//
// NWN fogs in its SHADERS, not through GL fog state, so glGetFloatv(GL_FOG_*)
// reads nothing at all -- that is why this used to fall back to sliders. The
// values live in a vec4 uniform whose packing is documented by the shipped
// shader source (data/base_shaders.bif, inc_standard):
//
//     uniform vec4 fogParams;
//     #define fogEnabled           (fogParams.x)
//     #define fogStart             (fogParams.y)
//     #define fogEnd               (fogParams.z)
//     #define fogInvEndMinusStart  (fogParams.w)
//     GetFog(d) = (d - fogStart) * fogInvEndMinusStart,  d = -vPosView.z
//
// which is exactly the ramp the receiver shader applies, so matching the engine
// only needs the numbers. A uniform's current value reads back with
// glGetUniformfv -- no hook, no interception, no dirty-check problem.
//
// The programs are found by ENUMERATING GL program objects (glIsProgram over a
// bounded id range). g_nativeTransformSlots was tried first and found nothing:
// it only ever holds the handful of programs our own caster replay binds, and
// glUseProgram is not interposable here (see PATH B below), so the engine's
// fogged scene shaders never appear in it.
static GLuint   g_fogPrograms[16];
static unsigned g_fogProgramCount = 0;
static long     g_fogScanFrame    = -100000;

static void poll_engine_fog() {
    if (!gl::IsProgram || !gl::GetUniformLocation || !gl::GetUniformfv) return;
    // Rescan on a slow cadence: programs are compiled as areas load, and an
    // area with no fog leaves the cache legitimately empty.
    if (g_fogProgramCount == 0 || (g_frames - g_fogScanFrame) > 600) {
        g_fogScanFrame = g_frames;
        g_fogProgramCount = 0;
        for (GLuint id = 1; id <= 4096 && g_fogProgramCount <
                 sizeof(g_fogPrograms)/sizeof(g_fogPrograms[0]); ++id) {
            if (!gl::IsProgram(id)) continue;
            if (gl::GetUniformLocation(id, "fogParams") < 0) continue;
            g_fogPrograms[g_fogProgramCount++] = id;
        }
        static bool loggedScan = false;
        if (!loggedScan && g_fogProgramCount) {
            loggedScan = true;
            fprintf(stderr, "[shadowmap][fog] %u engine program(s) expose fogParams\n",
                    g_fogProgramCount);
        }
    }
    // CHOOSING among the programs, which the first version got wrong and cost a
    // Windows test round: it took the FIRST program reporting fogEnabled != 0,
    // and on Windows that was program 143 holding start=0.0 end=0.1. The shader
    // multiplies the shadow by 1-fog, so a 10 cm fog end erased every shadow in
    // the game. Two defences, both cheap:
    //   1. A degenerate range is REJECTED outright. Real NWN area fog spans
    //      tens of units; anything under kMinFogSpan is some other use of the
    //      uniform (or a leftover from a pass that is not the world), and
    //      trusting it makes the whole feature destructive rather than wrong-ish.
    //   2. Among what survives, the MAJORITY pair wins, so one odd program
    //      cannot outvote the scene shaders that all carry the real range.
    // The full census is logged once, so a future surprise is diagnosable from
    // the log instead of another guess.
    constexpr float kMinFogSpan = 5.0f;
    float  candStart[16], candEnd[16];
    unsigned candCount = 0;
    static bool loggedCensus = false;
    for (unsigned i = 0; i < g_fogProgramCount; ++i) {
        const GLint loc = gl::GetUniformLocation(g_fogPrograms[i], "fogParams");
        if (loc < 0) continue;
        GLfloat fp[4] = {0,0,0,0};
        gl::GetUniformfv(g_fogPrograms[i], loc, fp);
        if (!loggedCensus)
            fprintf(stderr, "[shadowmap][fog] census program %u: enabled=%.1f "
                            "start=%.2f end=%.2f inv=%.5f\n",
                    g_fogPrograms[i], fp[0], fp[1], fp[2], fp[3]);
        if (fp[0] == 0.0f) continue;                       // fogEnabled
        // A NEGATIVE FOG START IS LEGITIMATE, and rejecting it was a bug.
        //
        // The old test required start >= 0. A tester's area reported
        // "enabled=1.0 start=-8.00 end=100.00" -- a perfectly good 108-unit
        // range -- and it was thrown away, so the fade never switched on and
        // shadows got no fog at all. The log even blamed the SPAN, which was
        // fine; the start was what disqualified it.
        //
        // A start behind the camera simply means fog is already slightly
        // present at zero distance, and the shader's clamp handles that. What
        // actually has to be rejected is nonsense and degenerate ranges: the
        // 0.0 -> 0.1 program the note above warns about still fails on span.
        if (!std::isfinite(fp[1]) || !std::isfinite(fp[2])) continue;
        if (fp[2] <= 0.0f) continue;                       // fog ending at/behind us
        if (!(fp[2] - fp[1] >= kMinFogSpan)) continue;
        if (candCount < 16) { candStart[candCount] = fp[1]; candEnd[candCount] = fp[2]; ++candCount; }
    }

    unsigned bestVotes = 0;
    float bestStart = 0.0f, bestEnd = 0.0f;
    for (unsigned i = 0; i < candCount; ++i) {
        unsigned votes = 0;
        for (unsigned j = 0; j < candCount; ++j)
            if (candStart[j] == candStart[i] && candEnd[j] == candEnd[i]) ++votes;
        if (votes > bestVotes || (votes == bestVotes && candEnd[i] > bestEnd)) {
            bestVotes = votes; bestStart = candStart[i]; bestEnd = candEnd[i];
        }
    }

    // NWN's own attenuation constants, from ComputePointLightSource in
    // data/base_shaders.bif:
    //     f   = d^2 / range^2
    //     att = (1 - f) / (lightMaxIntensityInv + lightFalloffFactor * f)
    // Reading them lets the sun-shadow lift use the ENGINE's falloff instead of
    // a guessed one -- and NWN's point lights do no occlusion, so this
    // reproduces its lit pools exactly.
    for (unsigned i = 0; i < g_fogProgramCount; ++i) {
        const GLint la = gl::GetUniformLocation(g_fogPrograms[i], "lightMaxIntensityInv");
        const GLint lb = gl::GetUniformLocation(g_fogPrograms[i], "lightFalloffFactor");
        if (la < 0 || lb < 0) continue;
        GLfloat va = 0.0f, vb = 0.0f;
        gl::GetUniformfv(g_fogPrograms[i], la, &va);
        gl::GetUniformfv(g_fogPrograms[i], lb, &vb);
        if (std::isfinite(va) && std::isfinite(vb) && va > 0.0f) {
            g_lightMaxIntensityInv = va;
            g_lightFalloffFactor   = vb;
            static bool loggedFall = false;
            if (!loggedFall) {
                loggedFall = true;
                fprintf(stderr, "[shadowmap][lamps] engine falloff: maxIntensityInv=%.4f "
                                "falloffFactor=%.4f (program %u)\n", va, vb, g_fogPrograms[i]);
            }
            break;
        }
    }
    g_haveEngineFog = (bestVotes > 0);
    if (g_haveEngineFog) { g_fogStartEngine = bestStart; g_fogEndEngine = bestEnd; }
    if (!loggedCensus) {
        loggedCensus = true;
        if (g_haveEngineFog)
            fprintf(stderr, "[shadowmap][fog] chose start=%.2f end=%.2f (%u of %u "
                            "candidate programs agree)\n",
                    bestStart, bestEnd, bestVotes, candCount);
        else
            fprintf(stderr, "[shadowmap][fog] no usable range (no program reports fog "
                            "enabled with a span >= %.0f units); fade stays off\n",
                    kMinFogSpan);
    }
}

// Same one-entry cache, same reason: this is called per caster PER LIGHT during
// the local capture, so with three sources it is three scans per draw.
static unsigned g_nativeSlotCacheIdx = 0xFFFFFFFFu;

static NativeTransformSlot* native_transform_slot(GLuint program) {
    if (g_nativeSlotCacheIdx < g_nativeTransformSlotCount &&
        g_nativeTransformSlots[g_nativeSlotCacheIdx].prog == program)
        return &g_nativeTransformSlots[g_nativeSlotCacheIdx];
    for (unsigned i = 0; i < g_nativeTransformSlotCount; ++i)
        if (g_nativeTransformSlots[i].prog == program) {
            g_nativeSlotCacheIdx = i;
            return &g_nativeTransformSlots[i];
        }
    if (!program || g_nativeTransformSlotCount >= sizeof(g_nativeTransformSlots) /
                                                    sizeof(g_nativeTransformSlots[0]) ||
        !gl::GetUniformLocation) return nullptr;
    NativeTransformSlot& slot = g_nativeTransformSlots[g_nativeTransformSlotCount++];
    slot.prog = program;
    slot.model = gl::GetUniformLocation(program, U_MODEL);
    slot.modelView = gl::GetUniformLocation(program, U_MODEL_VIEW);
    slot.modelViewProj = gl::GetUniformLocation(program, U_MODEL_VIEW_PROJ);
    slot.view = gl::GetUniformLocation(program, U_VIEW);
    slot.proj = gl::GetUniformLocation(program, U_PROJ);
    slot.viewInv = gl::GetUniformLocation(program, U_VIEW_INV);
    slot.fogParams = gl::GetUniformLocation(program, "fogParams");
    if (g_cascadeMatrixTrace)
        fprintf(stderr, "[shadowmap][mat] program=%u named-locations "
                        "m_m=%d m_mv=%d m_mvp=%d m_view=%d m_proj=%d m_view_inv=%d\n",
                program, slot.model, slot.modelView, slot.modelViewProj,
                slot.view, slot.proj, slot.viewInv);
    return &slot;
}

// Record the exact normal-pass transforms that belong to the current native
// draw.  The draw wrappers run immediately after those uploads, so this is a
// THE CURRENT PROGRAM, resolved the way each platform actually needs.
//
// Windows: g_curProgram, tracked from the glUseProgram wrapper. The driver
// query it replaces was running per uniform upload inside the hottest loop the
// injector has, and on Windows that loop is enormous.
//
// Linux: ask the driver. The hook here is a patch of GLEW's function POINTER,
// not a symbol interposer, so g_curProgram is only as complete as the engine's
// use of that pointer -- and the same measurement that motivated the Windows
// change (~4700 matrix uploads a frame there, effectively none here) says the
// engine reaches the driver by another route on Linux at least some of the
// time. A stale program here does not degrade anything gracefully: it resolves
// the wrong transform slot, so casters land in the light's map with the wrong
// matrices. There is also nothing to win, since the query is not on a hot loop
// on this platform.
static inline GLint local_current_program() {
#if NWN_WIN_LOCAL_FASTPATH
    return (GLint)g_curProgram;
#else
    if (!gl::GetIntegerv) return 0;
    GLint current = 0;
    gl::GetIntegerv(GL_CURRENT_PROGRAM, &current);
    return current;
#endif
}

// far stronger association than a later scene-wide matrix-stack sample.
static void capture_native_transform(GLint location, GLsizei count,
                                     GLboolean transpose, const GLfloat* value) {
    const bool cascadeActive = g_cascadeLightCapture && g_cascadeLightCaptureActive;
    if ((!cascadeActive && !g_localVisibleDynamicCaptureActive) ||
        !g_traceFrameActive || g_traceCurrentScene != g_areaScene ||
        count != 1 || transpose != GL_FALSE || !value || !gl::GetIntegerv)
        return;

    const GLint current = local_current_program();
    if (current <= 0) return;
    NativeTransformSlot* slot = native_transform_slot((GLuint)current);
    if (!slot) return;
    if (location == slot->modelView) {
        std::memcpy(slot->normalModelView, value, sizeof(slot->normalModelView));
        slot->haveNormalModelView = true;
        // Associate the normal m_mv with the view that produced it, rather
        // than the Scene::Render-entry view.  The latter can be stale once
        // NWN has adjusted its camera matrices during an orbit/zoom frame;
        // mixing them made local point-light shadows detach with camera yaw.
        const float* normalView = mtx_entry(MTX_VIEW);
        if (normalView) {
            mat4_rigid_inverse(normalView, slot->normalViewInverse);
            slot->haveNormalViewInverse = true;
        } else {
            slot->haveNormalViewInverse = false;
        }
    } else if (location == slot->proj) {
        std::memcpy(slot->normalProjection, value, sizeof(slot->normalProjection));
        slot->haveNormalProjection = true;
    }
}

// m_mv is the normal camera view multiplied by an object transform.  Recover
// that object transform with the immutable entry inverse, then replace only
// m_mv/m_proj for the private duplicate.  The exact normal values are restored
// before the wrapper returns, so the engine never observes a light matrix.
static NativeTransformSlot* begin_cascade_light_transform(float savedMV[16],
                                                           float savedProj[16]) {
    if (!g_realUniformMatrix4fv || !g_cascadeMath.valid ||
        !g_shadowFrameContext.valid ||
        g_cascadeMath.sourceFrame != g_shadowFrameContext.serial ||
        !gl::GetIntegerv) return nullptr;
    const GLint current = local_current_program();
    if (current <= 0) return nullptr;
    NativeTransformSlot* slot = native_transform_slot((GLuint)current);
    if (!slot || slot->modelView < 0 || slot->proj < 0 ||
        !slot->haveNormalModelView || !slot->haveNormalProjection)
        return nullptr;

    std::memcpy(savedMV, slot->normalModelView, sizeof(slot->normalModelView));
    std::memcpy(savedProj, slot->normalProjection, sizeof(slot->normalProjection));
    float model[16], lightMV[16];
    mat4_mul(g_shadowFrameContext.viewInverse, savedMV, model);
    const int captureLayer = g_cascadeMultiLayerCapture ? g_cascadeReplayLayer
                                                         : g_cascadeLightCaptureLayer;
    mat4_mul(g_cascadeMath.lightView[captureLayer], model, lightMV);
    g_realUniformMatrix4fv(slot->modelView, 1, GL_FALSE, lightMV);
    g_realUniformMatrix4fv(slot->proj, 1, GL_FALSE,
                           g_cascadeMath.lightProjection[captureLayer]);

    static bool logged = false;
    if (!logged) {
        logged = true;
        fprintf(stderr, "[shadowmap][lightcap] live: program=%u m_mv=%d m_proj=%d; "
                        "using frozen camera inverse + cascade-%d light matrices\n",
                slot->prog, slot->modelView, slot->proj,
                captureLayer);
    }
    return slot;
}

static void end_cascade_light_transform(const NativeTransformSlot* slot,
                                        const float savedMV[16],
                                        const float savedProj[16]) {
    if (!slot || !g_realUniformMatrix4fv) return;
    g_realUniformMatrix4fv(slot->modelView, 1, GL_FALSE, savedMV);
    g_realUniformMatrix4fv(slot->proj, 1, GL_FALSE, savedProj);
}

// Cube target helpers are defined with the rest of the local-light target
// implementation later in this file.  The dynamic native-draw bridge above
// needs their declarations here.

static void end_local_cube_dynamic_transform(const NativeTransformSlot* slot,
                                             const float savedMV[16],
                                             const float savedProj[16]) {
    if (!slot || !g_realUniformMatrix4fv) return;
    g_realUniformMatrix4fv(slot->modelView, 1, GL_FALSE, savedMV);
    g_realUniformMatrix4fv(slot->proj, 1, GL_FALSE, savedProj);
}

static SavedState bind_local_light_target();

// PER-LIGHT CASTER CULL.
//
// Every visible dynamic draw was duplicated into EVERY selected light's layer.
// With three sources that is three times the dynamic geometry per frame, and on
// Windows -- where each draw costs more -- it stutters.
//
// A light's capture frustum reaches max(kLocalLightMinRange, radius*2). A caster
// beyond that cannot appear in that light's map no matter what, so skipping it
// removes work that produces nothing. This is not a quality setting and has no
// popping: it mirrors the frustum the capture already uses.
//
// The margin covers the object's own extent, since this tests its ORIGIN and a
// large model can straddle the boundary.
static constexpr float kLocalCullMarginUnits = 6.0f;

// No glGet here: g_curProgram is tracked from the glUseProgram wrapper. This
// runs per draw per light, which is precisely where a driver query must not be.
static bool local_visible_draw_world_pos(float out[3]) {
    const GLint cur = local_current_program();
    if (!g_shadowFrameContext.valid || cur <= 0) return false;
    NativeTransformSlot* slot = native_transform_slot((GLuint)cur);
    if (!slot || !slot->haveNormalModelView) return false;
    const float* vi = slot->haveNormalViewInverse ? slot->normalViewInverse
                                                  : g_shadowFrameContext.viewInverse;
    float model[16];
    mat4_mul(vi, slot->normalModelView, model);
    out[0] = model[12]; out[1] = model[13]; out[2] = model[14];
    return true;
}

// Unknown position or radius => never cull. A missing input must cost
// performance, never correctness.
static bool local_layer_accepts_caster(int layer, const float p[3], bool havePos) {
    // WINDOWS ONLY -- see NWN_WIN_LOCAL_FASTPATH. This exists to cut draw cost
    // on the platform where each duplicated draw is expensive; Linux never had
    // that problem and so gets nothing back for the risk. What it costs is
    // real: culling every caster of a light is indistinguishable from that
    // light having no casters, which turns a full layer into an empty one and
    // put the publish decision on a knife edge.
    if (!NWN_WIN_LOCAL_FASTPATH) return true;
    if (!havePos) return true;
    if (layer < 0 || layer >= (int)g_localVisibleSlotCount) return true;
    const float r = g_localVisibleSlotRadius[layer];
    if (!(r > 0.0f)) return true;
    // MIRRORS THE CAPTURE'S FAR PLANE, LIFT INCLUDED. shadow_local_lights.inc
    // builds zfi as max(kLocalLightMinRange, r*2 + g_localLightHeight): "Lamp
    // lift" raises the apex the shadow is cast from and extends the far plane
    // by the same amount. This cull is WINDOWS-ONLY, so it did not move with
    // that change and silently began rejecting casters the capture's frustum
    // still covers -- the same drift that broke the receiver's world-to-depth
    // conversion, in the one place the platform split hides it from Linux.
    const float reach = std::max(kLocalLightMinRange,
                                 r * 2.0f + std::max(0.0f, g_localLightHeight)) +
                        kLocalCullMarginUnits;
    const float dx = p[0] - g_localVisibleSlotPos[layer][0];
    const float dy = p[1] - g_localVisibleSlotPos[layer][1];
    const float dz = p[2] - g_localVisibleSlotPos[layer][2];
    return (dx*dx + dy*dy + dz*dz) <= reach * reach;
}

// SPLIT: resolve+save ONCE per draw, then only upload per layer.
//
// begin_local_visible_dynamic_transform() saves the object's matrices AND sets
// the light's, and end_..._transform() puts them back -- so a 3-source capture
// did 4 uniform uploads per layer (2 set, 2 restore) when the restore only
// needs to happen once, after the last layer. The object's own matrices are
// identical for every layer of the same draw.
//
// 3 layers: 12 uploads -> 8. The model matrix is also derived once instead of
// three times.
static bool local_visible_begin_draw(NativeTransformSlot*& slotOut,
                                     float savedMV[16], float savedProj[16],
                                     float modelOut[16]) {
    if (!g_localVisibleDynamicCaptureActive || !g_realUniformMatrix4fv ||
        !g_shadowFrameContext.valid)
        return false;
    const GLint current = local_current_program();
    if (current <= 0) return false;
    NativeTransformSlot* slot = native_transform_slot((GLuint)current);
    if (!slot || slot->modelView < 0 || slot->proj < 0 ||
        !slot->haveNormalModelView || !slot->haveNormalProjection)
        return false;
    std::memcpy(savedMV, slot->normalModelView, sizeof(slot->normalModelView));
    std::memcpy(savedProj, slot->normalProjection, sizeof(slot->normalProjection));
    const float* drawViewInverse = slot->haveNormalViewInverse
        ? slot->normalViewInverse : g_shadowFrameContext.viewInverse;
    mat4_mul(drawViewInverse, savedMV, modelOut);
    slotOut = slot;
    return true;
}

static void local_visible_set_layer_transform(const NativeTransformSlot* slot,
                                              const float model[16], int layer) {
    float lightMV[16];
    mat4_mul(g_localVisibleView[layer], model, lightMV);
    g_realUniformMatrix4fv(slot->modelView, 1, GL_FALSE, lightMV);
    g_realUniformMatrix4fv(slot->proj, 1, GL_FALSE, g_localVisibleProj[layer]);
}

// (begin_local_visible_dynamic_transform was removed: local_visible_begin_draw
// plus local_visible_set_layer_transform replace it, splitting the per-draw
// save from the per-layer upload.)

// SPLIT IN TWO, and the reason is performance, not tidiness.
//
// This used to bind the target AND attach the layer together, called once per
// caster PER LIGHT. bind_local_light_target() issues six driver state queries
// (GetIntegerv x2, GetBooleanv x2, IsEnabled x2) plus a framebuffer bind, and
// restore_state() undoes it -- all of it identical for every layer of the same
// draw. At 168 casters x 2 lights that is thousands of redundant state calls
// per capture, which is exactly the "no glGet on a per-draw path" rule this
// project already learned the hard way.
//
// The target binding is now hoisted to once per DRAW; only the layer
// attachment and the matrices change per light.
static bool begin_local_visible_dynamic_pass(SavedState& st) {
    if (!g_localVisibleDynamicCaptureActive || !g_localLightTargetUsable ||
        !g_localLightFbo || !g_localLightWriteDepthTex || !gl::FramebufferTextureLayer)
        return false;
    st = bind_local_light_target();
    return true;
}

static bool local_visible_attach_layer(int layer) {
    if (layer < 0 || layer >= (int)g_localVisibleSlotCount) return false;
    // MEASURED TWICE, WORSE BOTH TIMES -- do not try a third.
    //
    // The theory was sound: this re-attaches a layer per caster per light
    // (~510 calls a capture at 3 sources), and an attachment change forces the
    // driver to re-validate completeness. Replacing it with one pre-built FBO
    // per layer should have been cheaper.
    //
    // Attempt 1 omitted glDrawBuffer/glReadBuffer GL_NONE, so every FBO was
    // incomplete and every draw failed -- clearly worse. Attempt 2 built them
    // correctly, status-checked, and the frame rate still DROPPED (53-58 fps
    // fluctuating -> a flat 50). On this driver binding a different FBO per
    // draw costs more than re-attaching to one already bound.
    gl::FramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                g_localLightWriteDepthTex, 0, layer);
    return true;
}

// This intentionally avoids ProgSlot/shader discovery: trace mode must not
// require an injected material to learn the normal renderer's uniform layout.
// The frozen entry context gives exact reference values for the projection,
// view and their product. Every upload is forwarded unchanged.
static void trace_normal_matrix_upload(GLint location, GLsizei count,
                                       GLboolean transpose, const GLfloat* value) {
    if (!g_cascadeMatrixTrace || !g_traceFrameActive ||
        g_traceCurrentScene != g_areaScene ||
        g_cascadeMatrixTraceCount >= g_cascadeMatrixTraceMax ||
        count != 1 || transpose != GL_FALSE || !value || !gl::GetIntegerv)
        return;
    GLint program = 0;
    gl::GetIntegerv(GL_CURRENT_PROGRAM, &program);
    if (program <= 0) return;
    (void)native_transform_slot((GLuint)program);
    float vp[16];
    mat4_mul(g_shadowFrameContext.projection, g_shadowFrameContext.view, vp);
    const float projDelta = mat4_max_delta(value, g_shadowFrameContext.projection);
    const float viewDelta = mat4_max_delta(value, g_shadowFrameContext.view);
    const float vpDelta = mat4_max_delta(value, vp);
    const float invViewDelta = mat4_max_delta(value, g_shadowFrameContext.viewInverse);
    const char* kind = "object-or-other";
    float delta = std::min(std::min(projDelta, viewDelta), std::min(vpDelta, invViewDelta));
    if (projDelta < 1e-4f) kind = "camera-proj";
    else if (viewDelta < 1e-4f) kind = "camera-view";
    else if (vpDelta < 1e-4f) kind = "camera-vp";
    else if (invViewDelta < 1e-4f) kind = "camera-view-inv";
    ++g_cascadeMatrixTraceCount;
    fprintf(stderr,
            "[shadowmap][mat] frame=%u prog=%d loc=%d kind=%s delta=%.6g "
            "T=(%.3f %.3f %.3f) diag=(%.3f %.3f %.3f %.3f)\n",
            g_shadowFrameContext.serial, program, location, kind, delta,
            value[12], value[13], value[14], value[0], value[5], value[10], value[15]);
}

extern "C" void my_uniform_matrix4fv(GLint location, GLsizei count,
                                      GLboolean transpose, const GLfloat* value) {
    if (!g_realUniformMatrix4fv) return;
    ++g_uniformMat4Calls;
    trace_normal_matrix_upload(location, count, transpose, value);
    capture_native_transform(location, count, transpose, value);
    capture_camera_vp_inv(location, count, transpose, value);
    trace_matrix_upload(location, count, transpose, value);

    // NOTE: Phase 6c's bucket replay deliberately does NOT substitute anything
    // here. It applies the light transform through the engine's matrix stack
    // instead -- see the header comment on replay_bucket_into_cascade_layers()
    // for the measurement that forced that (uniform interception silently
    // missed 100% of static geometry, because unchanged model matrices are
    // never re-uploaded). Substituting in both places would double-transform.

    // The first transform reconstruction is an experiment, not a replacement
    // for the established replay matrix.  It produced sparse blobs on this
    // build because some stock variants do not expose a recoverable m_m.
    // Keep the hook inert by default; explicit opt-in preserves a clean A/B.
    if (!shadow_getenv("NWN_SHADOWMAP_LIGHT_CASTER_MATRICES") ||
        !g_inOurPass || !g_haveActiveLightMatrices || count != 1 ||
        transpose != GL_FALSE || !value || !gl::GetIntegerv) {
        g_realUniformMatrix4fv(location, count, transpose, value);
        return;
    }
    GLint current = 0;
    gl::GetIntegerv(GL_CURRENT_PROGRAM, &current);
    if (current <= 0) {
        g_realUniformMatrix4fv(location, count, transpose, value);
        return;
    }

    // This also populates the transform locations for newly seen injected
    // material programs.  It only touches our caster uniform, never the
    // engine's regular uniforms.
    shadow_on_program((GLuint)current);
    ProgSlot* slot = find_prog_slot((GLuint)current);
    if (!slot) {
        g_realUniformMatrix4fv(location, count, transpose, value);
        return;
    }

    bool replaced = false;
    if (location == slot->locModel) {
        std::memcpy(slot->model, value, sizeof(slot->model));
        slot->haveModel = true;
        g_realUniformMatrix4fv(location, count, transpose, value);
        push_light_model_transforms(slot);
        replaced = true;
    } else if (location == slot->locModelView) {
        // Some stock variants expose m_mv but optimise m_m away.  Recover the
        // local-to-world matrix from the known camera view at Scene entry.
        // That gives those variants the same stable light transform too.
        if (!slot->haveModel && g_haveCamView) {
            float invCam[16];
            mat4_rigid_inverse(g_camView, invCam);
            mat4_mul(invCam, value, slot->model);
            slot->haveModel = true;
        }
        if (slot->haveModel) {
            push_light_model_transforms(slot);
            replaced = true;
        }
    } else if (location == slot->locModelViewProj) {
        if (slot->haveModel) {
            push_light_model_transforms(slot);
            replaced = true;
        }
    } else if (location == slot->locView || location == slot->locProj) {
        push_light_model_transforms(slot);
        replaced = true;
    }
    if (!replaced)
        g_realUniformMatrix4fv(location, count, transpose, value);

    static int logged = 0;
    if (logged < 16 && (location == slot->locModel ||
                        location == slot->locModelView ||
                        location == slot->locModelViewProj ||
                        location == slot->locView || location == slot->locProj)) {
        ++logged;
        fprintf(stderr, "[shadowmap] light transform override: prog=%u loc=%d "
                        "m=%d mv=%d mvp=%d view=%d proj=%d model=%s\n",
                (unsigned)current, location, slot->locModel, slot->locModelView,
                slot->locModelViewProj, slot->locView, slot->locProj,
                slot->haveModel ? "yes" : "no");
    }
}

// The exported m_lightAreaDiffuseDirection is zero in the tested area, but
// ordinary material programs may receive the true sun through a uniform.
// Record only normalised-ish vec3 uploads while a bounded selected-area frame
// is rendering, then forward the call unchanged.
static void trace_light_vector_upload(GLint location, GLfloat x, GLfloat y, GLfloat z) {
    if (!g_lightVectorTrace || !g_traceFrameActive ||
        g_traceCurrentScene != g_areaScene ||
        g_lightVectorTraceCount >= g_lightVectorTraceMax || !gl::GetIntegerv)
        return;
    const float length = std::sqrt(x*x + y*y + z*z);
    if (!std::isfinite(length) || length < 0.5f || length > 1.5f) return;
    GLint program = 0;
    gl::GetIntegerv(GL_CURRENT_PROGRAM, &program);
    if (program <= 0) return;
    // The repeated candidate rotates whenever the player camera rotates.  Log
    // the equivalent world vector using the immutable entry inverse, without
    // assuming that every observed vec3 is a light direction.  A real
    // view-space sun will collapse to one stable world-space value here.
    const float* inv = g_shadowFrameContext.viewInverse;
    const float wx = inv[0]*x + inv[4]*y + inv[8]*z;
    const float wy = inv[1]*x + inv[5]*y + inv[9]*z;
    const float wz = inv[2]*x + inv[6]*y + inv[10]*z;

    // m_lightAreaDiffuseDirection points toward the light (confirmed by the
    // legacy light pass), while build_lookat expects rays travelling from the
    // light into the scene.  Do not promote a one-off normal/material vector:
    // require agreement from a second program in the normal area pass.
    if (length > 0.95f && length < 1.05f && !g_haveTraceAreaLight) {
        if (g_traceLightCandidateHits == 0) {
            g_traceLightCandidate[0] = wx;
            g_traceLightCandidate[1] = wy;
            g_traceLightCandidate[2] = wz;
            g_traceLightCandidateProgram = program;
            g_traceLightCandidateHits = 1;
        } else {
            const float dot = wx*g_traceLightCandidate[0] +
                              wy*g_traceLightCandidate[1] +
                              wz*g_traceLightCandidate[2];
            if (program != g_traceLightCandidateProgram && dot > 0.9999f) {
                ++g_traceLightCandidateHits;
                if (g_traceLightCandidateHits >= 2) {
                    g_haveTraceAreaLight = true;
                    g_traceAreaLightDir[0] = g_traceLightCandidate[0] * g_dirSign;
                    g_traceAreaLightDir[1] = g_traceLightCandidate[1] * g_dirSign;
                    g_traceAreaLightDir[2] = g_traceLightCandidate[2] * g_dirSign;
                    fprintf(stderr, "[shadowmap][lightvec] selected area direction "
                                    "light->scene=(%.5f %.5f %.5f) from %u programs\n",
                            g_traceAreaLightDir[0], g_traceAreaLightDir[1],
                            g_traceAreaLightDir[2], g_traceLightCandidateHits);
                }
            }
        }
    }
    ++g_lightVectorTraceCount;
    fprintf(stderr, "[shadowmap][lightvec] frame=%u prog=%d loc=%d "
                    "view=(%.5f %.5f %.5f) len=%.5f world=(%.5f %.5f %.5f)\n",
            g_shadowFrameContext.serial, program, location, x, y, z, length,
            wx, wy, wz);
}

extern "C" void my_uniform3f(GLint location, GLfloat x, GLfloat y, GLfloat z) {
    if (!g_realUniform3f) return;
    trace_light_vector_upload(location, x, y, z);
    g_realUniform3f(location, x, y, z);
}

extern "C" void my_uniform3fv(GLint location, GLsizei count, const GLfloat* value) {
    if (!g_realUniform3fv) return;
    if (count == 1 && value)
        trace_light_vector_upload(location, value[0], value[1], value[2]);
    g_realUniform3fv(location, count, value);
}

// RenderDrawBucket often reuses a program that was already current in the
// camera pass. In that case there is no glUseProgram call inside the replay to
// observe. Flip every discovered injected program explicitly before the
// buckets run, and restore it immediately afterwards so the camera pass can
// never inherit caster output.
static unsigned set_known_caster_mode(bool enabled) {
    if (!g_realUseProgram || !gl::Uniform1f || !gl::GetIntegerv) return 0;
    GLint saved = 0;
    gl::GetIntegerv(GL_CURRENT_PROGRAM, &saved);
    unsigned changed = 0;
    for (int i = 0; i < g_progCount; ++i) {
        const ProgSlot& slot = g_progs[i];
        if (slot.locCaster < 0) continue;
        g_realUseProgram(slot.prog);
        gl::Uniform1f(slot.locCaster, enabled ? 1.0f : 0.0f);
        ++changed;
    }
    g_realUseProgram((GLuint)saved);
    return changed;
}

// Defined below, after the GL texture-unit helper. The engine's material
// setup is free to mutate texture bindings between frames, so the confirmed
// GLEW program-bind path also reasserts our private sampler binding.
static void bind_shadow_texture();

// PATH A: GLEW's function pointer, if the engine dispatches through it.
extern "C" void my_use_program(GLuint prog) {
    if (g_realUseProgram) g_realUseProgram(prog);
    g_curProgram = prog;
    ++g_useProgramCalls;
    // On Windows this wrapper starts receiving calls before bind_gl() can
    // succeed (no GL context at DLL attach), so every gl:: pointer below is
    // still null. Forward and return until the render path binds them.
    if (!g_glBound) return;
    static bool said = false;
    if (!said) { said = true;
        fprintf(stderr, "[shadowmap] program binds arriving via GLEW pointer\n"); }
    if (g_inOurPass) {
        // RenderDrawBucket's draws bypass the executable's PLT draw imports,
        // but the material system does bind programs through this confirmed
        // GLEW slot. Set caster mode and colour masking at that boundary.
        const bool injectedCaster = shadow_on_program(prog);
        ++g_lightProgramBinds;
        if (injectedCaster) ++g_lightCasterProgramBinds;
        if (gl::ColorMask)
            gl::ColorMask(injectedCaster, injectedCaster,
                          injectedCaster, injectedCaster);
        return;
    }
    bind_shadow_texture();
    shadow_on_program(prog);
}

// PATH B: interpose the DRAW calls instead of the program bind.
//
// The executable imports only GL 1.1 through the PLT -- glBindTexture,
// glDrawElements, glDrawArrays, glViewport and friends. Everything modern
// (shaders, uniforms, FBOs) is dispatched through GLEW pointers fetched with
// glXGetProcAddressARB, which bypasses LD_PRELOAD entirely. So glUseProgram is
// NOT interposable here -- it is not even in the import table -- but the draw
// entry points are. Query the current program at draw time and push there.
// ProgSlot::frame limits this to one uniform push per program per frame.
static void shadow_before_draw() {
    if (!gl::GetIntegerv) return;
    // Injector-owned passes: the depth-only ones (cascade bucket replay,
    // local-light probe) render into targets with no colour attachment, and
    // the ImGui overlay must reach the screen unmodified. In all three the
    // shadow texture rebind and colour-mask muting below are wrong, not just
    // wasteful -- for the overlay they would mask it out entirely.
    // nwn_core::g_ownedPass extends that to draws issued by ANOTHER injector
    // module (nwn_oit.cpp): its resolve is a fullscreen composite that must
    // reach the screen exactly as written, like the overlay.
    if (g_cascadeReplayActive || g_localLightPassActive || g_overlayPassActive ||
        nwn_core::g_ownedPass) return;
    // PERFORMANCE: everything below needs the current program, but glGetIntegerv
    // is a driver-synchronising query and this runs on EVERY draw call in the
    // game -- ~15k per frame in a dense area. In the accepted CSM/trace path
    // g_haveShadowMat is never set (it belongs to the legacy single-map
    // renderer), so the query was pure waste. Test the cheap flags first.
    if (!g_inStencilShadow && !g_haveShadowMat) return;
    GLint prog = 0;
    gl::GetIntegerv(GL_CURRENT_PROGRAM, &prog);
    if (g_inStencilShadow) {
        if (prog) {
            bool seen = false;
            for (unsigned i = 0; i < g_stencilProgramCount; ++i)
                if (g_stencilPrograms[i] == (GLuint)prog) { seen = true; break; }
            if (!seen && g_stencilProgramCount < sizeof(g_stencilPrograms)/sizeof(g_stencilPrograms[0]))
                g_stencilPrograms[g_stencilProgramCount++] = (GLuint)prog;
        }
        // Reference tracing must not rebind the private sampler or touch
        // injected uniforms inside the engine's validated stencil pass.
        return;
    }
    if (!g_haveShadowMat) return;
    if (!prog) return;

    if (g_inOurPass) {
        // The light FBO also has a colour attachment. Only shaders that carry
        // our caster branch may write it; every other engine shader still
        // participates in the depth test, but cannot pollute the depth-as-
        // colour map with its diffuse/material output.
        const bool injectedCaster = shadow_on_program((GLuint)prog);
        ++g_lightDrawCalls;
        if (injectedCaster) ++g_lightCasterDrawCalls;
        else ++g_lightMutedDrawCalls;
        if (gl::ColorMask)
            gl::ColorMask(injectedCaster, injectedCaster,
                          injectedCaster, injectedCaster);
        return;
    }

    // The engine is free to change ANY texture-unit binding between frames and
    // between material draws. Binding the shadow map once before Scene::Render
    // only proved it was bound at that instant; by this draw it could be 0 or a
    // material texture. Rebind at the last legal instant, after all engine
    // material setup and immediately before glDrawElements/glDrawArrays.
    bind_shadow_texture();

    static bool said = false;
    if (!said) { said = true;
        fprintf(stderr, "[shadowmap] draw interposition live (first program=%d)\n",
                prog); }
    shadow_on_program((GLuint)prog);
}

// The GL 1.1 draw imports are the one reliable point shared by every engine
// Quad-sized draws per bucket, and which programs issue them. Diagnostic only;
// see the BLOB-SHADOW HUNT comment below.
static constexpr int kQuadProgSlots = 4;
static unsigned g_quadDraws[kBucketCount] = {};
static unsigned g_quadProg[kBucketCount][kQuadProgSlots] = {};
// Draws issued OUTSIDE any Scene::RenderDrawBucket call. nwn_core::g_currentBucket
// is -1 there, and the per-bucket census required a valid bucket, so this entire
// category was invisible to it -- which is exactly where a per-object shadow pass
// would live. Counted separately rather than folded in, because "how many draws
// does the engine make outside the bucket system" is itself the question.
static unsigned g_drawsNoBucket     = 0;
static unsigned g_quadDrawsNoBucket = 0;
static unsigned g_idxNoBucket       = 0;
static unsigned g_progNoBucket[kQuadProgSlots] = {};
static unsigned g_drawsNoBucketShown= 0;
static unsigned g_quadNoBucketShown = 0;
static unsigned g_idxNoBucketShown  = 0;

// Prints the fragment source of a program caught issuing quad-sized draws, so
// the blob shader can be recognised by a SIGNATURE in its text rather than by a
// program ID, which is not stable between runs. Bounded to a few dumps: this is
// a one-shot identification aid, not running instrumentation.
static void dump_quad_program_source(unsigned prog, int bucket) {
    static unsigned dumped = 0;
    if (!prog || dumped >= 3) return;
    using GetAttachedShaders_t = void (*)(GLuint, GLsizei, GLsizei*, GLuint*);
    using GetShaderSource_t    = void (*)(GLuint, GLsizei, GLsizei*, char*);
    static GetAttachedShaders_t getAttached = nullptr;
    static GetShaderSource_t    getSource   = nullptr;
    if (!getAttached)
        getAttached = (GetAttachedShaders_t)dlsym(RTLD_DEFAULT, "glGetAttachedShaders");
    if (!getSource)
        getSource = (GetShaderSource_t)dlsym(RTLD_DEFAULT, "glGetShaderSource");
    if (!getAttached || !getSource) return;
    GLuint shaders[8] = {};
    GLsizei shaderCount = 0;
    getAttached((GLuint)prog, 8, &shaderCount, shaders);
    ++dumped;
    for (GLsizei i = 0; i < shaderCount; ++i) {
        static char src[4096];
        GLsizei len = 0;
        src[0] = '\0';
        getSource(shaders[i], (GLsizei)sizeof(src) - 1, &len, src);
        if (len <= 0) continue;
        src[sizeof(src) - 1] = '\0';
        fprintf(stderr, "[shadowmap][quad] bucket=%d program=%u shader=%u "
                        "source (first %d chars):\n%s\n[shadowmap][quad] --- end ---\n",
                bucket, prog, shaders[i], (int)len, src);
    }
}

// material variant.  During Phase 3e we only count the original calls made
// inside one native RenderDrawBucket invocation; the call below still reaches
// NWN exactly once and with its unmodified state.
static void trace_normal_geometry_draw(GLsizei count, bool indexed) {
    if (g_overlayPassActive) return;   // UI draws are not scene geometry
    // The engine's own shadow pass, counted HERE and not in shadow_before_draw().
    // That function is reached from only 2 of the 10 draw wrappers -- this engine
    // draws mostly through glDrawRangeElements and glMultiDraw* -- so a counter
    // kept there read a FALSE ZERO and was once used to conclude, wrongly, that
    // Scene::RenderShadows draws nothing. Count where every draw funnels.
    if (g_inStencilShadow) ++g_engineShadowDraws;
    // INDICES PER BUCKET. A draw count alone cannot tell a particle quad from a
    // creature: both are one draw. ~6 indices/draw is a quad, hundreds is real
    // geometry -- which is the whole question about what belongs in the caster
    // set. Placed above the bounded-census early-outs on purpose: that census
    // stops after a few frames, and the question is about the live scene.
    // This is the ONLY function every draw wrapper funnels through.
    if (g_costReport && count > 0) {
        const int b = g_cascadeReplayActive ? g_cascadeReplayBucket
                                            : nwn_core::g_currentBucket;
        if (b >= 0 && b < kBucketCount) {
            if (g_cascadeReplayActive) g_replayBucketIdx[b] += (unsigned)count;
            else                       g_engineBucketIdx[b] += (unsigned)count;
        } else if (!g_cascadeReplayActive && !g_inOurPass) {
            ++g_drawsNoBucket;
            g_idxNoBucket += (unsigned)count;
            if (count <= 12) ++g_quadDrawsNoBucket;
            bool known = false;
            for (int i = 0; i < kQuadProgSlots; ++i)
                if (g_progNoBucket[i] == g_curProgram) { known = true; break; }
            if (!known)
                for (int i = 0; i < kQuadProgSlots; ++i)
                    if (g_progNoBucket[i] == 0) {
                        g_progNoBucket[i] = g_curProgram;
                        dump_quad_program_source(g_curProgram, -1);
                        break;
                    }
        }
        // BLOB-SHADOW HUNT. NWN's creature/placeable blob is one textured quad
        // per object, so it is invisible in a per-bucket AVERAGE (bucket 3
        // averages 35 indices/draw and still contains hundreds of quads). Count
        // quad-sized draws separately and remember which PROGRAM issued them --
        // the program is free here because we track glUseProgram ourselves.
        // Identifying by program is the point: the source is stable, the ID is
        // not (see AGENTS.md), so the eventual suppression matches the shader
        // signature rather than a number from a log.
        if (!g_cascadeReplayActive && !g_inOurPass && count <= 12 &&
            b >= 0 && b < kBucketCount) {
            ++g_quadDraws[b];
            bool known = false;
            for (int i = 0; i < kQuadProgSlots; ++i)
                if (g_quadProg[b][i] == g_curProgram) { known = true; break; }
            if (!known)
                for (int i = 0; i < kQuadProgSlots; ++i)
                    if (g_quadProg[b][i] == 0) {
                        g_quadProg[b][i] = g_curProgram;
                        dump_quad_program_source(g_curProgram, b);
                        break;
                    }
        }
    }
    // Cross-module per-draw observation (nwn_hooks_core.h). This function is
    // the ONLY one every draw wrapper funnels through, which is why the hook
    // sits here and not in shadow_before_draw() -- that one is reached only
    // from the glDrawArrays/glDrawElements interposers, and this engine draws
    // through glDrawRangeElements and glMultiDraw*. Deliberately ABOVE the
    // census early-outs below: an observer must see every engine draw, not only
    // the ones the bounded Phase 3e census happens to still be counting.
    if (nwn_core::g_drawObserver &&
        (!nwn_core::g_ownedPass || nwn_oit_observes_owned_draws()) && !g_inOurPass)
        nwn_core::g_drawObserver();
    if (!g_cascadeGeometryTrace || !g_traceGeometryActive ||
        !g_traceFrameActive || g_traceGeometryBuckets >= g_traceGeometryMaxBuckets ||
        count <= 0 || !gl::GetIntegerv) return;
    ++g_traceGeometryDraws;
    if (indexed) g_traceGeometryElements += (unsigned)count;
    else         g_traceGeometryArrays += (unsigned)count;
    GLint program = 0;
    gl::GetIntegerv(GL_CURRENT_PROGRAM, &program);
    if (program <= 0) return;
    // A fragment source dump by itself is not enough to identify foliage or
    // hair cards: NWN feeds most material variants through the same template,
    // which contains an inactive discard path. Join the actually attached
    // shader objects to the current native bucket instead. This is strictly
    // observational and is enabled only by the Phase 3n launcher.
    if (shadow_getenv("NWN_SHADOWMAP_DUMP_FRAGMENT")) {
        using GetAttachedShaders_t = void (*)(GLuint, GLsizei, GLsizei*, GLuint*);
        static GetAttachedShaders_t getAttached = nullptr;
        static std::vector<GLuint> reported;
        if (!getAttached)
            getAttached = (GetAttachedShaders_t)dlsym(RTLD_DEFAULT, "glGetAttachedShaders");
        bool seen = false;
        for (GLuint id : reported) if (id == (GLuint)program) { seen = true; break; }
        if (getAttached && !seen) {
            reported.push_back((GLuint)program);
            GLuint shaders[8] = {};
            GLsizei shaderCount = 0;
            getAttached((GLuint)program, 8, &shaderCount, shaders);
            fprintf(stderr, "[shadowmap][alpha] bucket=%d program=%d attached=",
                    g_traceGeometryBucket, program);
            for (GLsizei i = 0; i < shaderCount; ++i)
                fprintf(stderr, "%s%u", i ? "," : "", shaders[i]);
            fprintf(stderr, "  (inspect matching /tmp/nwn_shadow_fragment_<id>.glsl)\n");
        }
    }
    for (unsigned i = 0; i < g_traceGeometryProgramCount; ++i)
        if (g_traceGeometryPrograms[i] == (GLuint)program) return;
    if (g_traceGeometryProgramCount < sizeof(g_traceGeometryPrograms) / sizeof(g_traceGeometryPrograms[0]))
        g_traceGeometryPrograms[g_traceGeometryProgramCount++] = (GLuint)program;
}

static inline void finish_observed_engine_draw() {
    if (nwn_core::g_drawObserverAfter &&
        (!nwn_core::g_ownedPass || nwn_oit_observes_owned_draws()) && !g_inOurPass)
        nwn_core::g_drawObserverAfter();
}

#define DUPLICATE_OIT_FRINGE(drawCall)                                      \
    do {                                                                    \
        if (nwn_oit_begin_mode3_depth_duplicate()) {                        \
            drawCall;                                                       \
            nwn_oit_end_mode3_depth_duplicate();                            \
        }                                                                   \
        if (nwn_oit_begin_immediate_fringe()) {                             \
            drawCall;                                                       \
            nwn_oit_end_immediate_fringe();                                 \
        }                                                                   \
    } while (0)

#define DUPLICATE_A2C_OPAQUE_DEPTH(drawCall)                                \
    do {                                                                    \
        if (nwn_oit_begin_opaque_depth_duplicate()) {                       \
            drawCall;                                                       \
            nwn_oit_end_opaque_depth_duplicate();                           \
        }                                                                   \
    } while (0)

#define DUPLICATE_MODE3_OPAQUE_DEPTH(drawCall)                              \
    do {                                                                    \
        if (nwn_oit_begin_mode3_depth_duplicate()) {                        \
            drawCall;                                                       \
            nwn_oit_end_mode3_depth_duplicate();                            \
        }                                                                   \
    } while (0)

#define DUPLICATE_A2C_PRIVATE_EMITTER(drawCall)                             \
    do {                                                                    \
        if (nwn_oit_begin_private_emitter_duplicate()) {                    \
            drawCall;                                                       \
            nwn_oit_end_private_emitter_duplicate();                        \
        }                                                                   \
    } while (0)

// Resolve the source-classified fragment against the program actually bound
// immediately before this native draw. Shader/program IDs change per launch,
// so neither is hard-coded in the foliage proof.
static bool static_alpha_program_current() {
    if ((!g_cascadeStaticAlphaCapture && !g_cascadeStaticAlphaReceiver) ||
        !gl::GetIntegerv ||
        g_cascadeStaticAlphaFragments.empty()) return false;
    GLint current = 0;
    gl::GetIntegerv(GL_CURRENT_PROGRAM, &current);
    if (current <= 0) return false;
    using GetAttachedShaders_t = void (*)(GLuint, GLsizei, GLsizei*, GLuint*);
    static GetAttachedShaders_t getAttached = nullptr;
    static std::vector<GLuint> reported;
    static std::vector<GLuint> rejected;
    if (!getAttached)
        getAttached = (GetAttachedShaders_t)dlsym(RTLD_DEFAULT, "glGetAttachedShaders");
    if (!getAttached) return false;
    GLuint shaders[8] = {};
    GLsizei shaderCount = 0;
    getAttached((GLuint)current, 8, &shaderCount, shaders);
    for (GLsizei i = 0; i < shaderCount; ++i) {
        for (GLuint fragment : g_cascadeStaticAlphaFragments) {
            if (shaders[i] != fragment) continue;
            // The stock template is shared, but fAlphaDiscardValue only exists
            // in its compiled discard variant. Read the live material value;
            // -1 means no discard, while zero or higher is an active alpha cut.
            const GLint cutout = gl::GetUniformLocation((GLuint)current, "fAlphaDiscardValue");
            GLfloat threshold = 0.0f;
            if (cutout < 0 || !gl::GetUniformfv) return false;
            gl::GetUniformfv((GLuint)current, cutout, &threshold);
            if (!std::isfinite(threshold) || threshold < 0.0f) {
                bool seen = false;
                for (GLuint program : rejected)
                    if (program == (GLuint)current) { seen = true; break; }
                if (!seen) {
                    rejected.push_back((GLuint)current);
                    fprintf(stderr, "[shadowmap][alpha] ignored stock alpha program=%d "
                                    "fAlphaDiscardValue=%.3f\n", current, threshold);
                }
                return false;
            }
            bool seen = false;
            for (GLuint program : reported)
                if (program == (GLuint)current) { seen = true; break; }
            if (!seen) {
                reported.push_back((GLuint)current);
                fprintf(stderr, "[shadowmap][alpha] selected static AlphaDiscard program=%d "
                                "fragment=%u value=%.3f\n",
                        current, fragment, threshold);
            }
            return true;
        }
    }
    return false;
}

static void (*g_nextDrawElements)(GLenum, GLsizei, GLenum, const void*) = nullptr;
// The normal call is always issued before this macro.  It redirects only the
// duplicate into our depth layer and restores the two original uniforms before
// returning to NWN.
#define DUPLICATE_CASCADE_LIGHT(drawCall, drawCount)                         \
    do {                                                                      \
        const int savedReplayLayer = g_cascadeReplayLayer;                    \
        const int firstReplayLayer = g_cascadeMultiLayerCapture ? 0 :         \
            g_cascadeLightCaptureLayer;                                       \
        const int lastReplayLayer = g_cascadeMultiLayerCapture ?              \
            (kCascadeCount - 1) : g_cascadeLightCaptureLayer;                 \
        for (int replayLayer = firstReplayLayer; replayLayer <= lastReplayLayer; ++replayLayer) { \
            g_cascadeReplayLayer = replayLayer;                               \
            SavedState lightState;                                            \
            const bool lightActive = g_cascadeLightCaptureActive &&           \
                (!g_cascadeStaticAlphaFilterActive || static_alpha_program_current()); \
            if (lightActive) ++g_cascadeLightCaptureAttempts;                 \
            if (lightActive && begin_cascade_light_capture(lightState)) {     \
                float savedMV[16], savedProj[16];                             \
                NativeTransformSlot* lightSlot =                              \
                    begin_cascade_light_transform(savedMV, savedProj);        \
                if (lightSlot) {                                              \
                    drawCall;                                                 \
                    drawCount;                                                \
                    end_cascade_light_transform(lightSlot, savedMV, savedProj); \
                } else {                                                      \
                    ++g_cascadeLightCaptureSkipped;                          \
                }                                                             \
                end_cascade_light_capture(lightState);                        \
            } else if (lightActive) ++g_cascadeLightCaptureNotReady;          \
        }                                                                     \
        g_cascadeReplayLayer = savedReplayLayer;                              \
    } while (0)

// Only duplicate the material draw that can actually contribute to the normal
// scene colour.  NWN also submits hidden stencil-shadow proxy geometry through
// this broad dynamic stage; copying that geometry would make the shadow map
// disagree with the mesh the player sees.  The engine still chooses the light
// source -- this filter decides only which submitted geometry is a caster.
static bool local_cube_visible_dynamic_draw() {
    if (g_inStencilShadow) {
        ++g_localCubeDynamicSkipStencil;
        return false;
    }
    if (!gl::GetBooleanv || !gl::GetIntegerv) {
        ++g_localCubeDynamicSkipNoProgram;
        return false;
    }
    GLboolean colourMask[4] = {};
    gl::GetBooleanv(GL_COLOR_WRITEMASK, colourMask);
    // A depth/stencil-only submission has no visible RGB contribution.
    if (!colourMask[0] && !colourMask[1] && !colourMask[2]) {
        ++g_localCubeDynamicSkipNoColour;
        return false;
    }
    GLint program = 0;
    gl::GetIntegerv(GL_CURRENT_PROGRAM, &program);
    if (program <= 0) {
        ++g_localCubeDynamicSkipNoProgram;
        return false;
    }
    // Exclude any stencil program we learned from the engine's own shadow
    // pass, even if that pass happens to leave its colour state enabled.
    for (unsigned i = 0; i < g_stencilProgramCount; ++i) {
        if (g_stencilPrograms[i] == static_cast<GLuint>(program)) {
            ++g_localCubeDynamicSkipStencil;
            return false;
        }
    }
    return true;
}

// Dynamic characters are issued by NWN during Scene::RenderDynamicGeometry,
// not via a replayable bucket.  Duplicate each eligible visible-material draw
// once per cube face while that engine stage is live.  The original draw has
// already completed; every state and transform is restored before NWN resumes.
#define DUPLICATE_LOCAL_CUBE_DYNAMIC(drawCall, drawCount)                    \
    do {                                                                      \
        const bool duplicateLocal = g_localVisibleDynamicCaptureActive &&    \
                                    g_localVisibleDynamicPrepared;           \
        if (duplicateLocal && local_cube_visible_dynamic_draw()) {           \
          {                                                                   \
            float localCasterPos[3];                                          \
            const bool localHavePos =                                         \
                local_visible_draw_world_pos(localCasterPos);                 \
            SavedState localState;                                            \
            bool localBound = false;                                          \
            float localSavedMV[16], localSavedProj[16], localModel[16];        \
            NativeTransformSlot* localSlot = nullptr;                         \
            bool localHaveSlot = false;                                       \
            for (unsigned localLayer = 0;                                    \
                 localLayer < g_localVisibleSlotCount; ++localLayer) {       \
                if (!local_layer_accepts_caster((int)localLayer,              \
                                                localCasterPos,               \
                                                localHavePos)) continue;      \
                if (!localHaveSlot) {                                         \
                    if (!local_visible_begin_draw(localSlot, localSavedMV,     \
                                                  localSavedProj, localModel)) \
                        break;                                                \
                    localHaveSlot = true;                                     \
                }                                                             \
                if (!localBound) {                                            \
                    if (!begin_local_visible_dynamic_pass(localState)) break;  \
                    localBound = true;                                        \
                }                                                             \
                if (!local_visible_attach_layer((int)localLayer)) continue;    \
                local_visible_set_layer_transform(localSlot, localModel,       \
                                                  (int)localLayer);           \
                drawCall;                                                     \
                drawCount;                                                    \
                ++g_localVisibleDynamicDraws[localLayer];                    \
                ++g_localDupDraws;                                           \
            }                                                                 \
            if (localHaveSlot)                                                \
                end_local_cube_dynamic_transform(localSlot, localSavedMV,      \
                                                 localSavedProj);             \
            if (localBound) restore_state(localState);                        \
          }                                                                   \
        }                                                                     \
    } while (0)

extern "C" void NWN_GL_INTERPOSE(glDrawElements)(GLenum mode, GLsizei count, GLenum type,
                                const void* indices) {
    if (!g_nextDrawElements)
        g_nextDrawElements = (void(*)(GLenum, GLsizei, GLenum, const void*))
                             dlsym(RTLD_NEXT, "glDrawElements");
    shadow_before_draw();
    trace_normal_geometry_draw(count, true);
    const bool suppressNative = nwn_oit_suppress_current_draw();
    if (g_nextDrawElements && !suppressNative)
        g_nextDrawElements(mode, count, type, indices);
    if (g_nextDrawElements)
        DUPLICATE_OIT_FRINGE(g_nextDrawElements(mode, count, type, indices));
    if (g_nextDrawElements)
        DUPLICATE_A2C_OPAQUE_DEPTH(g_nextDrawElements(mode, count, type, indices));
    if (g_nextDrawElements)
        DUPLICATE_MODE3_OPAQUE_DEPTH(g_nextDrawElements(mode, count, type, indices));
    if (g_nextDrawElements)
        DUPLICATE_A2C_PRIVATE_EMITTER(g_nextDrawElements(mode, count, type, indices));
    finish_observed_engine_draw();
    note_cascade_replay_draws(1);
    note_local_light_draws(1);
    SavedState st;
    if (g_nextDrawElements && begin_cascade_camera_capture(st)) {
        g_nextDrawElements(mode, count, type, indices);
        ++g_cascadeCameraCaptureDraws;
        end_cascade_camera_capture(st);
    }
    if (g_nextDrawElements)
        DUPLICATE_CASCADE_LIGHT(g_nextDrawElements(mode, count, type, indices),
                                note_cascade_light_draws(1));
    if (g_nextDrawElements)
        DUPLICATE_LOCAL_CUBE_DYNAMIC(g_nextDrawElements(mode, count, type, indices), (void)0);
}

static void (*g_nextDrawArrays)(GLenum, GLint, GLsizei) = nullptr;
extern "C" void NWN_GL_INTERPOSE(glDrawArrays)(GLenum mode, GLint first, GLsizei count) {
    if (!g_nextDrawArrays)
        g_nextDrawArrays = (void(*)(GLenum, GLint, GLsizei))
                           dlsym(RTLD_NEXT, "glDrawArrays");
    shadow_before_draw();
    trace_normal_geometry_draw(count, false);
    const bool suppressNative = nwn_oit_suppress_current_draw();
    if (g_nextDrawArrays && !suppressNative)
        g_nextDrawArrays(mode, first, count);
    if (g_nextDrawArrays)
        DUPLICATE_OIT_FRINGE(g_nextDrawArrays(mode, first, count));
    if (g_nextDrawArrays)
        DUPLICATE_A2C_OPAQUE_DEPTH(g_nextDrawArrays(mode, first, count));
    if (g_nextDrawArrays)
        DUPLICATE_MODE3_OPAQUE_DEPTH(g_nextDrawArrays(mode, first, count));
    if (g_nextDrawArrays)
        DUPLICATE_A2C_PRIVATE_EMITTER(g_nextDrawArrays(mode, first, count));
    finish_observed_engine_draw();
    note_cascade_replay_draws(1);
    note_local_light_draws(1);
    SavedState st;
    if (g_nextDrawArrays && begin_cascade_camera_capture(st)) {
        g_nextDrawArrays(mode, first, count);
        ++g_cascadeCameraCaptureDraws;
        end_cascade_camera_capture(st);
    }
    if (g_nextDrawArrays)
        DUPLICATE_CASCADE_LIGHT(g_nextDrawArrays(mode, first, count),
                                note_cascade_light_draws(1));
    if (g_nextDrawArrays)
        DUPLICATE_LOCAL_CUBE_DYNAMIC(g_nextDrawArrays(mode, first, count), (void)0);
}

using DrawRangeElements_t = void (*)(GLenum, GLuint, GLuint, GLsizei, GLenum,
                                     const void*);
using MultiDrawArrays_t = void (*)(GLenum, const GLint*, const GLsizei*, GLsizei);
using MultiDrawElements_t = void (*)(GLenum, const GLsizei*, GLenum,
                                     const void* const*, GLsizei);
using DrawElementsBaseVertex_t = void (*)(GLenum, GLsizei, GLenum,
                                          const void*, GLint);
using DrawRangeElementsBaseVertex_t = void (*)(GLenum, GLuint, GLuint,
                                               GLsizei, GLenum, const void*, GLint);
using DrawElementsInstanced_t = void (*)(GLenum, GLsizei, GLenum,
                                         const void*, GLsizei);
using DrawElementsInstancedBaseVertex_t = void (*)(GLenum, GLsizei, GLenum,
                                                   const void*, GLsizei, GLint);
static DrawRangeElements_t  g_realDrawRangeElements = nullptr;
static MultiDrawArrays_t    g_realMultiDrawArrays = nullptr;
static MultiDrawElements_t  g_realMultiDrawElements = nullptr;
static DrawElementsBaseVertex_t g_realDrawElementsBaseVertex = nullptr;
static DrawRangeElementsBaseVertex_t g_realDrawRangeElementsBaseVertex = nullptr;
static DrawElementsInstanced_t g_realDrawElementsInstanced = nullptr;
static DrawElementsInstancedBaseVertex_t g_realDrawElementsInstancedBaseVertex = nullptr;

// NWN may cache core GL entry points directly from glXGetProcAddressARB rather
// than through either its PLT imports or exported GLEW globals. Keep these
// originals so the resolver-level trace wrapper can return an ABI-identical
// forwarding function without changing a draw.
using GlXGetProcAddressARB_t = void* (*)(const unsigned char*);
#ifndef _WIN32
// Guarded with the interposer that is its only user: glXGetProcAddressARB does
// not exist on Windows (wglGetProcAddress is IAT-hooked instead), so leaving
// this ungarded made the Windows build carry a permanently unused static.
static GlXGetProcAddressARB_t g_realGlXGetProcAddressARB = nullptr;
#endif

extern "C" void my_draw_range_elements(GLenum mode, GLuint start, GLuint end,
                                        GLsizei count, GLenum type,
                                        const void* indices) {
    trace_normal_geometry_draw(count, true);
    const bool suppressNative = nwn_oit_suppress_current_draw();
    if (g_realDrawRangeElements && !suppressNative)
        g_realDrawRangeElements(mode, start, end, count, type, indices);
    if (g_realDrawRangeElements)
        DUPLICATE_OIT_FRINGE(g_realDrawRangeElements(mode, start, end, count,
                                                      type, indices));
    if (g_realDrawRangeElements)
        DUPLICATE_A2C_OPAQUE_DEPTH(g_realDrawRangeElements(mode, start, end, count,
                                                            type, indices));
    if (g_realDrawRangeElements)
        DUPLICATE_MODE3_OPAQUE_DEPTH(g_realDrawRangeElements(
            mode, start, end, count, type, indices));
    if (g_realDrawRangeElements)
        DUPLICATE_A2C_PRIVATE_EMITTER(g_realDrawRangeElements(
            mode, start, end, count, type, indices));
    finish_observed_engine_draw();
    note_cascade_replay_draws(1);
    note_local_light_draws(1);
    SavedState st;
    if (g_realDrawRangeElements && begin_cascade_camera_capture(st)) {
        g_realDrawRangeElements(mode, start, end, count, type, indices);
        ++g_cascadeCameraCaptureDraws;
        end_cascade_camera_capture(st);
    }
    if (g_realDrawRangeElements)
        DUPLICATE_CASCADE_LIGHT(g_realDrawRangeElements(mode, start, end, count, type, indices),
                                note_cascade_light_draws(1));
    if (g_realDrawRangeElements)
        DUPLICATE_LOCAL_CUBE_DYNAMIC(g_realDrawRangeElements(mode, start, end, count, type, indices), (void)0);
}

extern "C" void my_multi_draw_arrays(GLenum mode, const GLint* first,
                                     const GLsizei* count, GLsizei drawcount) {
    if (count) for (GLsizei i = 0; i < drawcount; ++i)
        trace_normal_geometry_draw(count[i], false);
    const bool suppressNative = nwn_oit_suppress_current_draw();
    if (g_realMultiDrawArrays && !suppressNative)
        g_realMultiDrawArrays(mode, first, count, drawcount);
    if (g_realMultiDrawArrays)
        DUPLICATE_OIT_FRINGE(g_realMultiDrawArrays(mode, first, count, drawcount));
    if (g_realMultiDrawArrays)
        DUPLICATE_A2C_OPAQUE_DEPTH(g_realMultiDrawArrays(mode, first, count, drawcount));
    if (g_realMultiDrawArrays)
        DUPLICATE_MODE3_OPAQUE_DEPTH(g_realMultiDrawArrays(
            mode, first, count, drawcount));
    if (g_realMultiDrawArrays)
        DUPLICATE_A2C_PRIVATE_EMITTER(g_realMultiDrawArrays(
            mode, first, count, drawcount));
    finish_observed_engine_draw();
    note_cascade_replay_draws((unsigned)std::max(0, drawcount));
    note_local_light_draws((unsigned)std::max(0, drawcount));
    SavedState st;
    if (g_realMultiDrawArrays && begin_cascade_camera_capture(st)) {
        g_realMultiDrawArrays(mode, first, count, drawcount);
        g_cascadeCameraCaptureDraws += (unsigned)std::max(0, drawcount);
        end_cascade_camera_capture(st);
    }
    if (g_realMultiDrawArrays)
        DUPLICATE_CASCADE_LIGHT(g_realMultiDrawArrays(mode, first, count, drawcount),
                                note_cascade_light_draws((unsigned)std::max(0, drawcount)));
    if (g_realMultiDrawArrays)
        DUPLICATE_LOCAL_CUBE_DYNAMIC(g_realMultiDrawArrays(mode, first, count, drawcount), (void)0);
}

extern "C" void my_multi_draw_elements(GLenum mode, const GLsizei* count,
                                       GLenum type, const void* const* indices,
                                       GLsizei drawcount) {
    if (count) for (GLsizei i = 0; i < drawcount; ++i)
        trace_normal_geometry_draw(count[i], true);
    const bool suppressNative = nwn_oit_suppress_current_draw();
    if (g_realMultiDrawElements && !suppressNative)
        g_realMultiDrawElements(mode, count, type, indices, drawcount);
    if (g_realMultiDrawElements)
        DUPLICATE_OIT_FRINGE(g_realMultiDrawElements(mode, count, type, indices,
                                                      drawcount));
    if (g_realMultiDrawElements)
        DUPLICATE_A2C_OPAQUE_DEPTH(g_realMultiDrawElements(mode, count, type,
                                                            indices, drawcount));
    if (g_realMultiDrawElements)
        DUPLICATE_MODE3_OPAQUE_DEPTH(g_realMultiDrawElements(
            mode, count, type, indices, drawcount));
    if (g_realMultiDrawElements)
        DUPLICATE_A2C_PRIVATE_EMITTER(g_realMultiDrawElements(
            mode, count, type, indices, drawcount));
    finish_observed_engine_draw();
    note_cascade_replay_draws((unsigned)std::max(0, drawcount));
    note_local_light_draws((unsigned)std::max(0, drawcount));
    SavedState st;
    if (g_realMultiDrawElements && begin_cascade_camera_capture(st)) {
        g_realMultiDrawElements(mode, count, type, indices, drawcount);
        g_cascadeCameraCaptureDraws += (unsigned)std::max(0, drawcount);
        end_cascade_camera_capture(st);
    }
    if (g_realMultiDrawElements)
        DUPLICATE_CASCADE_LIGHT(g_realMultiDrawElements(mode, count, type, indices, drawcount),
                                note_cascade_light_draws((unsigned)std::max(0, drawcount)));
    if (g_realMultiDrawElements)
        DUPLICATE_LOCAL_CUBE_DYNAMIC(g_realMultiDrawElements(mode, count, type, indices, drawcount), (void)0);
}

extern "C" void my_draw_elements_base_vertex(GLenum mode, GLsizei count,
                                               GLenum type, const void* indices,
                                               GLint basevertex) {
    trace_normal_geometry_draw(count, true);
    const bool suppressNative = nwn_oit_suppress_current_draw();
    if (g_realDrawElementsBaseVertex && !suppressNative)
        g_realDrawElementsBaseVertex(mode, count, type, indices, basevertex);
    if (g_realDrawElementsBaseVertex)
        DUPLICATE_OIT_FRINGE(g_realDrawElementsBaseVertex(mode, count, type,
                                                          indices, basevertex));
    if (g_realDrawElementsBaseVertex)
        DUPLICATE_A2C_OPAQUE_DEPTH(g_realDrawElementsBaseVertex(mode, count, type,
                                                                indices, basevertex));
    if (g_realDrawElementsBaseVertex)
        DUPLICATE_MODE3_OPAQUE_DEPTH(g_realDrawElementsBaseVertex(
            mode, count, type, indices, basevertex));
    if (g_realDrawElementsBaseVertex)
        DUPLICATE_A2C_PRIVATE_EMITTER(g_realDrawElementsBaseVertex(
            mode, count, type, indices, basevertex));
    finish_observed_engine_draw();
    note_cascade_replay_draws(1);
    note_local_light_draws(1);
    SavedState st;
    if (g_realDrawElementsBaseVertex && begin_cascade_camera_capture(st)) {
        g_realDrawElementsBaseVertex(mode, count, type, indices, basevertex);
        ++g_cascadeCameraCaptureDraws;
        end_cascade_camera_capture(st);
    }
    if (g_realDrawElementsBaseVertex)
        DUPLICATE_CASCADE_LIGHT(g_realDrawElementsBaseVertex(mode, count, type, indices, basevertex),
                                note_cascade_light_draws(1));
    if (g_realDrawElementsBaseVertex)
        DUPLICATE_LOCAL_CUBE_DYNAMIC(g_realDrawElementsBaseVertex(mode, count, type, indices,
                                                                   basevertex), (void)0);
}

extern "C" void my_draw_range_elements_base_vertex(GLenum mode, GLuint start,
                                                     GLuint end, GLsizei count,
                                                     GLenum type, const void* indices,
                                                     GLint basevertex) {
    trace_normal_geometry_draw(count, true);
    const bool suppressNative = nwn_oit_suppress_current_draw();
    if (g_realDrawRangeElementsBaseVertex && !suppressNative)
        g_realDrawRangeElementsBaseVertex(mode, start, end, count, type,
                                           indices, basevertex);
    if (g_realDrawRangeElementsBaseVertex)
        DUPLICATE_OIT_FRINGE(g_realDrawRangeElementsBaseVertex(
            mode, start, end, count, type, indices, basevertex));
    if (g_realDrawRangeElementsBaseVertex)
        DUPLICATE_A2C_OPAQUE_DEPTH(g_realDrawRangeElementsBaseVertex(
            mode, start, end, count, type, indices, basevertex));
    if (g_realDrawRangeElementsBaseVertex)
        DUPLICATE_MODE3_OPAQUE_DEPTH(g_realDrawRangeElementsBaseVertex(
            mode, start, end, count, type, indices, basevertex));
    if (g_realDrawRangeElementsBaseVertex)
        DUPLICATE_A2C_PRIVATE_EMITTER(g_realDrawRangeElementsBaseVertex(
            mode, start, end, count, type, indices, basevertex));
    finish_observed_engine_draw();
    note_cascade_replay_draws(1);
    note_local_light_draws(1);
    SavedState st;
    if (g_realDrawRangeElementsBaseVertex && begin_cascade_camera_capture(st)) {
        g_realDrawRangeElementsBaseVertex(mode, start, end, count, type,
                                           indices, basevertex);
        ++g_cascadeCameraCaptureDraws;
        end_cascade_camera_capture(st);
    }
    if (g_realDrawRangeElementsBaseVertex)
        DUPLICATE_CASCADE_LIGHT(g_realDrawRangeElementsBaseVertex(mode, start, end, count, type,
                                                                     indices, basevertex),
                                note_cascade_light_draws(1));
    if (g_realDrawRangeElementsBaseVertex)
        DUPLICATE_LOCAL_CUBE_DYNAMIC(g_realDrawRangeElementsBaseVertex(mode, start, end, count, type,
                                                                        indices, basevertex), (void)0);
}

extern "C" void my_draw_elements_instanced(GLenum mode, GLsizei count,
                                            GLenum type, const void* indices,
                                            GLsizei instances) {
    for (GLsizei i = 0; i < instances; ++i) trace_normal_geometry_draw(count, true);
    const bool suppressNative = nwn_oit_suppress_current_draw();
    if (g_realDrawElementsInstanced && !suppressNative)
        g_realDrawElementsInstanced(mode, count, type, indices, instances);
    if (g_realDrawElementsInstanced)
        DUPLICATE_OIT_FRINGE(g_realDrawElementsInstanced(
            mode, count, type, indices, instances));
    if (g_realDrawElementsInstanced)
        DUPLICATE_A2C_OPAQUE_DEPTH(g_realDrawElementsInstanced(
            mode, count, type, indices, instances));
    if (g_realDrawElementsInstanced)
        DUPLICATE_MODE3_OPAQUE_DEPTH(g_realDrawElementsInstanced(
            mode, count, type, indices, instances));
    if (g_realDrawElementsInstanced)
        DUPLICATE_A2C_PRIVATE_EMITTER(g_realDrawElementsInstanced(
            mode, count, type, indices, instances));
    finish_observed_engine_draw();
    note_cascade_replay_draws((unsigned)std::max(0, instances));
    note_local_light_draws((unsigned)std::max(0, instances));
    SavedState st;
    if (g_realDrawElementsInstanced && begin_cascade_camera_capture(st)) {
        g_realDrawElementsInstanced(mode, count, type, indices, instances);
        g_cascadeCameraCaptureDraws += (unsigned)std::max(0, instances);
        end_cascade_camera_capture(st);
    }
    if (g_realDrawElementsInstanced)
        DUPLICATE_CASCADE_LIGHT(g_realDrawElementsInstanced(mode, count, type, indices, instances),
                                note_cascade_light_draws((unsigned)std::max(0, instances)));
    if (g_realDrawElementsInstanced)
        DUPLICATE_LOCAL_CUBE_DYNAMIC(g_realDrawElementsInstanced(mode, count, type, indices, instances),
                                     (void)0);
}

extern "C" void my_draw_elements_instanced_base_vertex(GLenum mode, GLsizei count,
                                                        GLenum type, const void* indices,
                                                        GLsizei instances, GLint basevertex) {
    for (GLsizei i = 0; i < instances; ++i) trace_normal_geometry_draw(count, true);
    const bool suppressNative = nwn_oit_suppress_current_draw();
    if (g_realDrawElementsInstancedBaseVertex && !suppressNative)
        g_realDrawElementsInstancedBaseVertex(mode, count, type, indices,
                                               instances, basevertex);
    if (g_realDrawElementsInstancedBaseVertex)
        DUPLICATE_OIT_FRINGE(g_realDrawElementsInstancedBaseVertex(
            mode, count, type, indices, instances, basevertex));
    if (g_realDrawElementsInstancedBaseVertex)
        DUPLICATE_A2C_OPAQUE_DEPTH(g_realDrawElementsInstancedBaseVertex(
            mode, count, type, indices, instances, basevertex));
    if (g_realDrawElementsInstancedBaseVertex)
        DUPLICATE_MODE3_OPAQUE_DEPTH(g_realDrawElementsInstancedBaseVertex(
            mode, count, type, indices, instances, basevertex));
    if (g_realDrawElementsInstancedBaseVertex)
        DUPLICATE_A2C_PRIVATE_EMITTER(g_realDrawElementsInstancedBaseVertex(
            mode, count, type, indices, instances, basevertex));
    finish_observed_engine_draw();
    note_cascade_replay_draws((unsigned)std::max(0, instances));
    note_local_light_draws((unsigned)std::max(0, instances));
    SavedState st;
    if (g_realDrawElementsInstancedBaseVertex && begin_cascade_camera_capture(st)) {
        g_realDrawElementsInstancedBaseVertex(mode, count, type, indices,
                                               instances, basevertex);
        g_cascadeCameraCaptureDraws += (unsigned)std::max(0, instances);
        end_cascade_camera_capture(st);
    }
    if (g_realDrawElementsInstancedBaseVertex)
        DUPLICATE_CASCADE_LIGHT(g_realDrawElementsInstancedBaseVertex(mode, count, type, indices,
                                                                         instances, basevertex),
                                note_cascade_light_draws((unsigned)std::max(0, instances)));
    if (g_realDrawElementsInstancedBaseVertex)
        DUPLICATE_LOCAL_CUBE_DYNAMIC(g_realDrawElementsInstancedBaseVertex(mode, count, type, indices,
                                                                             instances, basevertex), (void)0);
}

#undef DUPLICATE_OIT_FRINGE
#undef DUPLICATE_A2C_OPAQUE_DEPTH
#undef DUPLICATE_MODE3_OPAQUE_DEPTH
#undef DUPLICATE_A2C_PRIVATE_EMITTER

// This resolver hook is inert unless the geometry census or another module's
// draw observer needs it. It catches direct GL function pointers at the time
// NWN asks the driver for them and returns forwarding wrappers with exactly the
// requested ABI. No renderer state is observed or changed here.
#ifndef _WIN32
extern "C" void* glXGetProcAddressARB(const unsigned char* name) {
    if (!g_realGlXGetProcAddressARB)
        g_realGlXGetProcAddressARB = (GlXGetProcAddressARB_t)
            dlsym(RTLD_NEXT, "glXGetProcAddressARB");
    if (!g_realGlXGetProcAddressARB) return nullptr;
    void* real = g_realGlXGetProcAddressARB(name);
    if ((!g_cascadeGeometryTrace && !nwn_oit_needs_draw_observer()) ||
        !name || !real) return real;
    static unsigned drawRequests = 0;
    if (std::strncmp((const char*)name, "glDraw", 6) == 0 && drawRequests++ < 48)
        fprintf(stderr, "[shadowmap][geom] resolver request %s -> %p\n", name, real);
    if (std::strcmp((const char*)name, "glDrawElements") == 0) {
        static bool said = false;
        if (!said) { said = true; fprintf(stderr, "[shadowmap][geom] resolver captured glDrawElements\n"); }
        return (void*)glDrawElements;
    }
    if (std::strcmp((const char*)name, "glDrawArrays") == 0) {
        static bool said = false;
        if (!said) { said = true; fprintf(stderr, "[shadowmap][geom] resolver captured glDrawArrays\n"); }
        return (void*)glDrawArrays;
    }
    if (std::strcmp((const char*)name, "glDrawRangeElements") == 0) {
        g_realDrawRangeElements = (DrawRangeElements_t)real;
        static bool said = false;
        if (!said) { said = true; fprintf(stderr, "[shadowmap][geom] resolver captured glDrawRangeElements\n"); }
        return (void*)my_draw_range_elements;
    }
    if (std::strcmp((const char*)name, "glMultiDrawArrays") == 0) {
        g_realMultiDrawArrays = (MultiDrawArrays_t)real;
        static bool said = false;
        if (!said) { said = true; fprintf(stderr, "[shadowmap][geom] resolver captured glMultiDrawArrays\n"); }
        return (void*)my_multi_draw_arrays;
    }
    if (std::strcmp((const char*)name, "glMultiDrawElements") == 0) {
        g_realMultiDrawElements = (MultiDrawElements_t)real;
        static bool said = false; 
        if (!said) { said = true; fprintf(stderr, "[shadowmap][geom] resolver captured glMultiDrawElements\n"); }
        return (void*)my_multi_draw_elements;
    }
    if (std::strcmp((const char*)name, "glDrawElementsBaseVertex") == 0) {
        g_realDrawElementsBaseVertex = (DrawElementsBaseVertex_t)real;
        return (void*)my_draw_elements_base_vertex;
    }
    if (std::strcmp((const char*)name, "glDrawRangeElementsBaseVertex") == 0) {
        g_realDrawRangeElementsBaseVertex = (DrawRangeElementsBaseVertex_t)real;
        return (void*)my_draw_range_elements_base_vertex;
    }
    if (std::strcmp((const char*)name, "glDrawElementsInstanced") == 0) {
        g_realDrawElementsInstanced = (DrawElementsInstanced_t)real;
        return (void*)my_draw_elements_instanced;
    }
    if (std::strcmp((const char*)name, "glDrawElementsInstancedBaseVertex") == 0) {
        g_realDrawElementsInstancedBaseVertex = (DrawElementsInstancedBaseVertex_t)real;
        return (void*)my_draw_elements_instanced_base_vertex;
    }
    return real;
}
#endif
