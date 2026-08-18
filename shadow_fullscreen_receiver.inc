// ---------------------------------------------------------------------------
// Injector-owned receiver proof: reconstruct world position from the completed
// world depth buffer, rather than from an NWN material varying.  It is opt-in
// because it deliberately paints a red/green diagnostic over the entire world.
// ---------------------------------------------------------------------------
// Resolve a GL entry point that the Linux build reaches through a GLEW
// pointer. nwmain.exe exports NO __glew* symbols, so on Windows every one of
// these slots is null and any function guarding on them silently does nothing
// -- which is exactly why the receiver shader was never built there while
// ImGui (which uses its own loader) rendered fine. Falling back to dlsym gets
// the driver pointer via wglGetProcAddress once a context is current.
static void* glew_or_dlsym(void** slot, const char* name) {
    if (slot && *slot) return *slot;
    return dlsym(RTLD_DEFAULT, name);
}

static GLuint g_fullscreenReceiverProgram = 0;

static void build_fullscreen_receiver_program() {
    if (g_fullscreenReceiverProgram ) return;  // GLEW-null checks removed: see glew_or_dlsym
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
        "void main(){ vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);"
        "gl_Position=vec4(p*2.0-1.0,0.0,1.0); }\n";
    static const char* fs =
        "#version 330 compatibility\n"
        "uniform sampler2D nwnSceneDepth; uniform sampler2D nwnShadowDepth;\n"
        "uniform mat4 nwnSceneVPInv; uniform mat4 nwnLightVP;\n"
        "void main(){ vec2 sz=vec2(textureSize(nwnSceneDepth,0));"
        "vec2 uv=gl_FragCoord.xy/sz; float d=texture2D(nwnSceneDepth,uv).r;"
        "if(d>=0.999999) discard; vec4 w=nwnSceneVPInv*vec4(uv*2.0-1.0,d*2.0-1.0,1.0);"
        "w/=w.w; vec4 l=nwnLightVP*w; vec3 s=l.xyz/l.w*0.5+0.5;"
        "vec3 c=vec3(0.0,0.0,1.0); if(l.w>0.0 && s.x>=0.0 && s.x<=1.0 &&"
        "s.y>=0.0 && s.y<=1.0 && s.z>=0.0 && s.z<=1.0){ float z=texture2D(nwnShadowDepth,s.xy).r;"
        "c=(s.z-0.0025>z)?vec3(1,0,0):vec3(0,1,0); } gl_FragColor=vec4(c,1); }\n";
    GLuint v=create(GL_VERTEX_SHADER), f=create(GL_FRAGMENT_SHADER);
    const GLint vl=(GLint)strlen(vs), fl=(GLint)strlen(fs);
    source(v,1,&vs,&vl); source(f,1,&fs,&fl); compile(v); compile(f);
    GLint vok=0, fok=0; getShader(v,GL_COMPILE_STATUS,&vok); getShader(f,GL_COMPILE_STATUS,&fok);
    if (!vok || !fok) { fprintf(stderr,"[shadowmap] fullscreen receiver shader compile FAILED\n"); return; }
    GLuint p=createProg(); attach(p,v); attach(p,f); link(p);
    GLint pok=0; getProg(p,GL_LINK_STATUS,&pok);
    if (!pok) { fprintf(stderr,"[shadowmap] fullscreen receiver link FAILED\n"); return; }
    g_fullscreenReceiverProgram=p;
    fprintf(stderr,"[shadowmap] fullscreen depth receiver compiled (program=%u)\n",p);
}

static bool ensure_scene_depth_texture(int w, int h) {
    if (w <= 0 || h <= 0) return false;
    if (!g_sceneDepthTex) gl::GenTextures(1, &g_sceneDepthTex);
    if (!g_sceneDepthTex) return false;
    gl::BindTexture(GL_TEXTURE_2D, g_sceneDepthTex);
    if (w != g_sceneDepthW || h != g_sceneDepthH) {
        gl::TexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0,
                       GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
        g_sceneDepthW=w; g_sceneDepthH=h;
    }
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // This texture is sampled as raw depth by sampler2D, unlike the light map
    // which has already been copied into a reliable R32F colour texture.
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
    return gl::GetError() == GL_NO_ERROR;
}

// Get the scene depth into g_sceneDepthTex.
//
// glCopyTexSubImage2D was the original method and it is NOT VALID for this:
// it copies from the READ BUFFER -- a colour buffer -- into a
// GL_DEPTH_COMPONENT24 texture, and it is also invalid outright when the
// source framebuffer is multisampled. NVIDIA's Linux driver accepts it anyway,
// which is why Linux and Proton worked for months; the Windows driver raises
// GL_INVALID_OPERATION (1282), and the receiver's draw was gated on the error
// flag being clear, so native Windows silently drew nothing. Measured:
//     BLOCKED at glErrorBeforeDraw (a=1282 b=1920 c=1080)
//
// glBlitFramebuffer with GL_DEPTH_BUFFER_BIT is the spec-clean way to move
// depth out of the default framebuffer, and it is defined for a multisampled
// source (NEAREST, matching formats). The old path stays as a fallback for a
// context that cannot blit.
// The engine's FINISHED COLOUR for this frame.
//
// The sun-shadow lift was driven by distance-to-lamp, which cannot know about
// the wall between a lamp and the floor, the lamp's real brightness, or its
// cone -- so it smeared a broad even lift instead of pooling under the lamps.
// NWN has already solved all of that and drawn the answer to the screen. Read
// the pixel's own brightness and lift the shadow where the scene is genuinely
// lit. Sampled BEFORE our own composite, so it is the engine's lighting, not
// ours feeding back.
static GLuint g_sceneColorTex = 0;
static int    g_sceneColorW = 0, g_sceneColorH = 0;
static GLuint g_sceneDepthFbo = 0;
static GLuint g_sceneDepthFboTex = 0;

static bool capture_scene_color(const GLint vp[4]) {
    const int w = vp[2], h = vp[3];
    if (w <= 0 || h <= 0 || !gl::CopyTexSubImage2D) return false;
    // DRAIN FIRST. glGetError returns the ACCUMULATED flag, so an error raised
    // by the ENGINE (or anything else in the process) is still latched when we
    // check below -- the same trap the receiver's own draw gate documents.
    // Without this the result flips false at random, and because it gates the
    // self-illumination guard, the shadow strength changed frame to frame:
    // visible flicker on Linux, reported 2026-08-15.
    while (gl::GetError() != GL_NO_ERROR) {}
    if (!g_sceneColorTex) gl::GenTextures(1, &g_sceneColorTex);
    if (!g_sceneColorTex) return false;
    gl::BindTexture(GL_TEXTURE_2D, g_sceneColorTex);
    if (w != g_sceneColorW || h != g_sceneColorH) {
        gl::TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA,
                       GL_UNSIGNED_BYTE, nullptr);
        g_sceneColorW = w; g_sceneColorH = h;
    }
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // Colour, unlike depth, is valid through glCopyTexSubImage2D from the
    // read buffer -- that is what this call is for.
    gl::CopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, vp[0], vp[1], w, h);
    const bool ok = (gl::GetError() == GL_NO_ERROR);
    // LATCH. Even with the drain, one bad frame must not toggle the guard off
    // and back on -- that is a visible change in shadow darkness, and a
    // capture that worked once will hold usable colour. Once true, stay true.
    static bool everOk = false;
    if (ok) everOk = true;
    return everOk;
}

static bool capture_scene_depth(const GLint vp[4]) {
    const int w = vp[2], h = vp[3];
    if (w <= 0 || h <= 0) return false;

    // DEFAULT ON since 2026-08-10, after three defects were fixed and the path
    // was verified in game on Linux with AA both on and off. This is what lets
    // native Windows keep ANTIALIASING enabled: glCopyTexSubImage2D below is
    // invalid on a multisampled framebuffer and the Windows driver enforces it.
    // NWN_SHADOWMAP_DEPTH_BLIT=0 falls back to the copy.
    //
    // The three defects, all of them mine, each with its own symptom -- see the
    // comments at their sites and SHADOWMAP_STATUS.md:
    //   1. ReadBuffer(GL_NONE) landed on the DEFAULT framebuffer  -> dark scene
    //   2. an unbounded glGetError drain loop                     -> frozen image
    //   3. forcing READ_FRAMEBUFFER to 0                          -> all-far depth
    // (3) was the subtle one: the engine renders with readFbo=1, drawFbo=0, and
    // the copy this replaces implicitly read FBO 1 while the blit read the
    // window. Verified: readFbo=1 drawFbo=0, probe non-far=863207/1931520.
    static const bool kUseBlit = [](){
        const char* v = shadow_getenv("NWN_SHADOWMAP_DEPTH_BLIT");
        return (v == nullptr) || (atoi(v) != 0);
    }();
    if (kUseBlit && gl::BlitFramebuffer && gl::GenFramebuffers && gl::BindFramebuffer &&
        gl::FramebufferTexture2D && gl::CheckFramebufferStatus) {
        if (!g_sceneDepthFbo) gl::GenFramebuffers(1, &g_sceneDepthFbo);
        if (g_sceneDepthFbo) {
            GLint prevRead = 0, prevDraw = 0;
            gl::GetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
            gl::GetIntegerv(GL_FRAMEBUFFER_BINDING, &prevDraw);
            gl::BindFramebuffer(GL_DRAW_FRAMEBUFFER, g_sceneDepthFbo);
            if (g_sceneDepthFboTex != g_sceneDepthTex) {
                gl::FramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                         GL_TEXTURE_2D, g_sceneDepthTex, 0);
                // Depth-only: without this the FBO is incomplete on drivers that
                // still expect a colour draw buffer. DrawBuffer is per-framebuffer
                // and OUR fbo is the bound DRAW target here, so this is safe.
                if (gl::DrawBuffer) gl::DrawBuffer(0 /*GL_NONE*/);
                // DEFECT 1 (caused the darkened scene): glReadBuffer applies to the
                // READ framebuffer, and at this point that is still the ENGINE's --
                // the default framebuffer. The first version called
                // ReadBuffer(GL_NONE) here and never restored it, so the window's
                // read buffer stayed NONE: our own blit then had nothing to read and
                // the engine's reads were broken too. The receiver got garbage depth
                // and shadowed everything. We never read from our own FBO, so the
                // call is simply gone -- do not "fix" it by binding and restoring.
                g_sceneDepthFboTex = g_sceneDepthTex;
            }
            const GLenum status = gl::CheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
            GLenum blitErr = GL_NO_ERROR;
            bool ok = (status == GL_FRAMEBUFFER_COMPLETE);
            if (ok) {
                // DEFECT 3 (caused the darkened scene, second attempt): do NOT
                // force the read framebuffer to 0. glCopyTexSubImage2D -- the path
                // this replaces -- reads from whatever is bound to GL_READ_FRAMEBUFFER,
                // and the receiver only ever checked the DRAW binding. If the engine
                // leaves its own FBO bound for reading, the copy took the real scene
                // depth from it while this blit took the window's, which holds no
                // usable depth: every pixel then reconstructs at the near plane and
                // the receiver shadows the entire screen uniformly -- exactly the
                // symptom. Leave the read binding exactly as the engine had it, so
                // the source is identical to the copy path's.
                // DEFECT 2 (caused the freeze): this drain was UNBOUNDED. A driver
                // that keeps reporting -- GL_CONTEXT_LOST is sticky, and a resolve
                // error can be re-raised -- spins here forever with the window up,
                // which is exactly "the whole image froze". Bound every drain loop.
                for (unsigned drain = 0; drain < 32 && gl::GetError() != GL_NO_ERROR; ++drain) {}
                gl::BlitFramebuffer(vp[0], vp[1], vp[0] + w, vp[1] + h,
                                    0, 0, w, h, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
                blitErr = gl::GetError();
                ok = (blitErr == GL_NO_ERROR);
            }
            gl::BindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)prevDraw);
            gl::BindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prevRead);
            static bool loggedPath = false;
            if (!loggedPath) {
                loggedPath = true;
                GLint samples = 0;
                gl::GetIntegerv(GL_SAMPLES, &samples);
                // CAN THE PER-FRAME RESOLVE BE ELIMINATED?
                //
                // With MSAA on, this blit is a full-screen multisample DEPTH
                // RESOLVE every frame, and it exists only when antialiasing and
                // shadows are both on -- which is exactly the superlinear cost
                // reported. If the depth the engine renders into is a TEXTURE,
                // the receiver can bind it as GL_TEXTURE_2D_MULTISAMPLE and
                // texelFetch sample 0 instead, and the resolve goes away
                // entirely (one sample is ample for a low-frequency shadow
                // term). If it is a RENDERBUFFER, it cannot be sampled and the
                // resolve has to be made cheaper some other way. This one line
                // decides which of those two jobs it is.
                GLint objType = 0, objName = 0;
                if (gl::GetFramebufferAttachmentParameteriv) {
                    while (gl::GetError() != GL_NO_ERROR) {}
                    gl::GetFramebufferAttachmentParameteriv(
                        GL_READ_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &objType);
                    if (gl::GetError() != GL_NO_ERROR) objType = 0;
                    else gl::GetFramebufferAttachmentParameteriv(
                        GL_READ_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &objName);
                }
                fprintf(stderr, "[shadowmap][msaa] read-fbo depth attachment "
                                "type=0x%x (%s) name=%d samples=%d\n",
                        objType,
                        objType == GL_TEXTURE      ? "TEXTURE -- resolve is removable"
                      : objType == GL_RENDERBUFFER ? "RENDERBUFFER -- not samplable"
                      : "none/unknown", objName, samples);
                fprintf(stderr, "[shadowmap][static] scene depth via %s "
                                "(fbo status 0x%x, blitErr 0x%x, %dx%d, samples=%d, "
                                "vp origin %d,%d, readFbo=%d drawFbo=%d)\n",
                        ok ? "glBlitFramebuffer" : "COPY fallback -- blit failed",
                        status, blitErr, w, h, samples, vp[0], vp[1],
                        (int)prevRead, (int)prevDraw);
            }
            if (ok) return true;
        }
    }

    // Fallback: the historical path. Kept because it is what Linux shipped.
    gl::CopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, vp[0], vp[1], w, h);
    return gl::GetError() == GL_NO_ERROR;
}

static void draw_fullscreen_depth_receiver(void* scene) {
    if (!shadow_getenv("NWN_SHADOWMAP_FULLSCREEN_RECEIVER") || scene != g_areaScene ||
        !g_isWorldScene || !g_haveWorldCameraVPInv || !g_haveLightWorldVP ||
        !g_gpuStepTex || !g_realUseProgram) return;
    build_fullscreen_receiver_program();
    if (!g_fullscreenReceiverProgram) return;
    GLint vp[4]={0,0,0,0}, oldProg=0, active=GL_TEXTURE0, old16=0, old17=0;
    GLboolean oldDepth=gl::IsEnabled(GL_DEPTH_TEST), oldBlend=gl::IsEnabled(GL_BLEND),
              oldMask=GL_TRUE, oldColor[4]={GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE};
    gl::GetIntegerv(GL_VIEWPORT,vp); gl::GetIntegerv(GL_CURRENT_PROGRAM,&oldProg);
    gl::GetIntegerv(GL_ACTIVE_TEXTURE,&active); gl::GetBooleanv(GL_DEPTH_WRITEMASK,&oldMask);
    gl::GetBooleanv(GL_COLOR_WRITEMASK,oldColor);
    gl::ActiveTexture(GL_TEXTURE0+SHADOW_UNIT+1);
    gl::GetIntegerv(GL_TEXTURE_BINDING_2D,&old17);
    const bool copied = ensure_scene_depth_texture(vp[2],vp[3]);
    if (copied) {
        // Copy from the same read framebuffer/view that NWN just finished drawing.
        gl::CopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,vp[0],vp[1],vp[2],vp[3]);
        gl::ActiveTexture(GL_TEXTURE0+SHADOW_UNIT);
        gl::GetIntegerv(GL_TEXTURE_BINDING_2D,&old16);
        gl::BindTexture(GL_TEXTURE_2D,g_gpuStepTex);
        gl::ActiveTexture(GL_TEXTURE0+SHADOW_UNIT+1);
        gl::BindTexture(GL_TEXTURE_2D,g_sceneDepthTex);
        if (oldDepth) gl::Disable(GL_DEPTH_TEST);
        if (oldBlend) gl::Disable(GL_BLEND);
        gl::DepthMask(GL_FALSE); gl::ColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
        g_realUseProgram(g_fullscreenReceiverProgram);
        GLint a=gl::GetUniformLocation(g_fullscreenReceiverProgram,"nwnSceneDepth");
        GLint b=gl::GetUniformLocation(g_fullscreenReceiverProgram,"nwnShadowDepth");
        GLint c=gl::GetUniformLocation(g_fullscreenReceiverProgram,"nwnSceneVPInv");
        GLint d=gl::GetUniformLocation(g_fullscreenReceiverProgram,"nwnLightVP");
        if(a>=0) gl::Uniform1i(a,SHADOW_UNIT+1);
        if(b>=0) gl::Uniform1i(b,SHADOW_UNIT);
        if(c>=0) gl::UniformMatrix4fv(c,1,GL_FALSE,g_worldCameraVPInv);
        if(d>=0) gl::UniformMatrix4fv(d,1,GL_FALSE,g_lightWorldVP);
        if (!g_nextDrawArrays) g_nextDrawArrays=(void(*)(GLenum,GLint,GLsizei))dlsym(RTLD_NEXT,"glDrawArrays");
        if (g_nextDrawArrays) g_nextDrawArrays(0x0004,0,3);
        static bool said=false;
        if(!said){said=true; fprintf(stderr,"[shadowmap] fullscreen depth receiver draw live\n");}
    }
    g_realUseProgram((GLuint)oldProg);
    gl::ActiveTexture(GL_TEXTURE0+SHADOW_UNIT); gl::BindTexture(GL_TEXTURE_2D,(GLuint)old16);
    gl::ActiveTexture(GL_TEXTURE0+SHADOW_UNIT+1); gl::BindTexture(GL_TEXTURE_2D,(GLuint)old17);
    gl::ActiveTexture((GLenum)active);
    if(oldDepth) gl::Enable(GL_DEPTH_TEST);
    if(oldBlend) gl::Enable(GL_BLEND);
    gl::DepthMask(oldMask); gl::ColorMask(oldColor[0],oldColor[1],oldColor[2],oldColor[3]);
}

// ---------------------------------------------------------------------------
// Phase 3i: static-only, injector-owned receiver proof.
//
// This is deliberately a new program rather than an evolution of the old
// fullscreen red/green experiment above.  Its two matrices both originate at
// the selected area's Scene::Render entry and its light depth was duplicated
// during that same Scene::Render invocation.  It therefore has no dependency
// on later UI, portrait, zoom or material uniform traffic.
// ---------------------------------------------------------------------------
static void build_static_receiver_program() {
    if (g_staticReceiverProgram) return;
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
        !getProg || !source) {
        static bool logged = false;
        if (!logged) {
            logged = true;
            fprintf(stderr, "[shadowmap][static] receiver program unavailable: "
                    "create=%p compile=%p getShader=%p createProg=%p attach=%p "
                    "link=%p getProg=%p source=%p\n",
                    (void*)create, (void*)compile, (void*)getShader, (void*)createProg,
                    (void*)attach, (void*)link, (void*)getProg, (void*)source);
        }
        return;
    }
    if (!create || !compile || !getShader || !createProg || !attach || !link ||
        !getProg || !source) return;
    static const char* vs =
        "#version 330 compatibility\n"
        "void main(){ vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);"
        "gl_Position=vec4(p*2.0-1.0,0.0,1.0); }\n";
    static const char* fs =
        "#version 330 compatibility\n"
        "uniform sampler2D nwnStaticSceneDepth;\n"
        // SELF-ILLUMINATION GUARD. The engine's finished colour for this pixel.
        // A self-illuminated surface is bright no matter how it is lit, so a
        // shadow laid over it is simply wrong -- glowing runes and lava went
        // dim under the sun's shadow. There is no material information in a
        // fullscreen pass, but "already bright" is exactly the observable that
        // distinguishes them, and it costs one texture read.
        "uniform sampler2D nwnStaticSceneColor; uniform float nwnSelfIllumGuard;\n"
        "uniform sampler2DArrayShadow nwnStaticLightDepth;\n"
        "uniform sampler2DArrayShadow nwnDynamicLightDepth;\n"
        "uniform mat4 nwnStaticCameraVPInv; uniform mat4 nwnStaticCameraView;\n"
        "uniform mat4 nwnStaticLightVP[4];\n"
        "uniform float nwnStaticClipFar0,nwnStaticClipFar1,nwnStaticClipFar2,nwnStaticClipFar3;\n"
        "uniform vec3 nwnStaticViewportOrigin; uniform int nwnStaticLayer;\n"
        "uniform int nwnStaticDynamicEnabled,nwnStaticCSMEnabled,nwnStaticComposite;\n"
        "uniform int nwnDynamicLayers; uniform int nwnStaticNearLayers;\n"
        "uniform int nwnFogEnabled; uniform float nwnFogStart,nwnFogEnd;\n"
        "uniform float nwnStaticCompositeStrength,nwnStaticReceiverBias,nwnStaticCascadeBlend,nwnStaticPCFRadius;\n"
        "uniform sampler2DShadow nwnStaticWorldDepth;\n"
        "uniform mat4 nwnStaticWorldVP; uniform int nwnStaticWorldEnabled;\n"
        // PLAIN sampler (not sampler2DShadow) and an ARRAY of six cube faces.
        // Non-shadow because doing the comparison in the shader is what made it
        // observable at all (debug 4); the texture's GL_TEXTURE_COMPARE_MODE is
        // NONE to match, and the two must always change together. Array because
        // a point light needs six faces.
        "uniform sampler2DArray nwnLocalLightDepth;\n"
        "uniform mat4 nwnLocalLightVP[32]; uniform int nwnLocalLightEnabled;\n"
        "uniform vec4 nwnLocalSlotPos[32]; uniform float nwnLocalSlotRadius[32];\n"
        // NWN's own per-light fade, 0..1. The engine ramps this when a light
        // turns on or off and its stencil shadow follows it; without this the
        // shadow stayed at full strength and then vanished in one frame.
        "uniform float nwnLocalSlotFade[32];\n"
        "uniform int nwnLocalSlots;\n"
        // Phase 7c: private six-face point-light cube receiver.  It is
        // deliberately separate from the established contact texture above;
        // the test launcher chooses which term is visible.
        "uniform float nwnLocalFalloff, nwnLocalLift;"
        "uniform float nwnLocalEdgeFade, nwnLocalSoft, nwnLocalNormalBias, nwnLocalMinSep;\n"
        // tan(fov/2) of the capture cone. Was hardcoded 2.747, i.e. tan(70)
        // for the 140-degree default -- which silently became wrong the
        // moment the FOV was tuned, under-estimating texel size and
        // bringing back acne. It follows the capture now.
        "uniform float nwnLocalTanHalfFov;\n"
        "uniform vec3 nwnLocalLightPos;\n"
        "uniform float nwnLocalLightRadius,nwnLocalLightStrength,nwnLocalLightBias,nwnLocalLightLift;\n"
        // The LIFT's own light, separate from the shadow cube's. The shadow
        // lookup must use the position the map was rendered with (one frame
        // old); the lift only needs "is a lamp near this pixel", so it takes
        // the LIVE census position and works with no capture at all.
        "uniform vec4 nwnLamps[128]; uniform int nwnLampCount;\n"
        "uniform vec2 nwnLampFalloff;\n"
        // Diagnostic ladder (NWN_SHADOWMAP_RECEIVER_DEBUG), off by default.
        // 1 = paint every non-background pixel green, bypassing all shadow
        //     maths: proves whether this pass reaches the screen at all.
        // 2 = red = sun shade, green = local shade, so a term that is
        //     silently zero everywhere is visible as an absent channel.
        "uniform int nwnReceiverDebug;\n"
        // World-anchored static map: one lookup, independent of cascade layer,
        // because it is anchored to the world rather than the camera.
        "float nwnWorldStaticLit(vec4 w){"
        "if(nwnStaticWorldEnabled==0)return 1.0;"
        "vec4 l=nwnStaticWorldVP*w;if(l.w<=0.0)return 1.0;"
        "vec3 s=l.xyz/l.w*0.5+0.5;"
        "if(s.x<=0.0||s.x>=1.0||s.y<=0.0||s.y>=1.0||s.z<=0.0||s.z>=1.0)return 1.0;"
        "vec2 t=1.0/vec2(textureSize(nwnStaticWorldDepth,0).xy);float total=0.0;"
        "for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x)"
        "total+=texture(nwnStaticWorldDepth,vec3(s.xy+vec2(float(x),float(y))*t,"
        "s.z-nwnStaticReceiverBias));"
        "return total/9.0;}\n"
        "float nwnLayerLit(int layer,vec4 w,float worldStatic){"
        "vec4 l=nwnStaticLightVP[layer]*w;if(l.w<=0.0)return 1.0;"
        "vec3 s=l.xyz/l.w*0.5+0.5;"
        "if(s.x<=0.0||s.x>=1.0||s.y<=0.0||s.y>=1.0||s.z<=0.0||s.z>=1.0)return 1.0;"
        "if(nwnStaticPCFRadius<=0.0){"
        "float a=nwnStaticWorldEnabled!=0?"
        "min(worldStatic,layer<nwnStaticNearLayers?"
        "texture(nwnStaticLightDepth,vec4(s.xy,float(layer),s.z-nwnStaticReceiverBias)):1.0):"
        "texture(nwnStaticLightDepth,vec4(s.xy,float(layer),s.z-nwnStaticReceiverBias));"
        "float b=(nwnStaticDynamicEnabled!=0&&layer<nwnDynamicLayers)?texture(nwnDynamicLightDepth,vec4(s.xy,float(layer),s.z-nwnStaticReceiverBias)):1.0;"
        "return min(a,b);}"
        "vec2 t=1.0/vec2(textureSize(nwnStaticLightDepth,0).xy);float total=0.0;"
        "for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){vec2 u=s.xy+vec2(float(x),float(y))*t*nwnStaticPCFRadius;"
        "float a=nwnStaticWorldEnabled!=0?"
        "min(worldStatic,layer<nwnStaticNearLayers?"
        "texture(nwnStaticLightDepth,vec4(u,float(layer),s.z-nwnStaticReceiverBias)):1.0):"
        "texture(nwnStaticLightDepth,vec4(u,float(layer),s.z-nwnStaticReceiverBias));"
        "float b=(nwnStaticDynamicEnabled!=0&&layer<nwnDynamicLayers)?texture(nwnDynamicLightDepth,vec4(u,float(layer),s.z-nwnStaticReceiverBias)):1.0;total+=min(a,b);}"
        "return total/9.0;}\n"
        // Phase 6d: one local light, one perspective depth map. Returns how
        // shadowed this world point is by that light, 0..1. Falls off with
        // distance so the term ends where the light's own reach ends rather
        // than at the depth map's frustum edge, and is skipped entirely
        // outside the map (that is "this light does not illuminate here", not
        // "shadowed"), which keeps the single-cone limitation invisible.
        // Returns (shade, coverage). Coverage matters for diagnosis: a pixel
        // outside the light's frustum and a pixel that is simply lit BOTH give
        // shade 0, so a "no green" debug view cannot tell "the cone is aimed
        // wrong" from "nothing is shadowed" -- which is exactly what made the
        // camera-aimed cone hard to spot. Debug 2 shows coverage in blue.
        // NORMAL-OFFSET BIAS. The receiver has no normals -- but it does have
        // the reconstructed world position, and the geometric normal is just
        // the cross product of its screen-space derivatives. Moving the LOOKUP
        // a texel-ish along that normal is what removes acne on a curved
        // surface like a face: the sample no longer lands on the very surface
        // being tested, so the comparison stops flipping.
        //
        // Depth bias could not do this (it pushes along the light ray, so on a
        // grazing surface the needed amount is unbounded -- raising it just
        // detached the shadow), and back-face culling did not take because NWN
        // sets its own cull state per draw during the replay.
        //
        // n MUST be computed in uniform control flow (derivatives), so main()
        // works it out once and passes it in.
        "vec2 nwnLocalShadeCov(vec4 w, vec3 nrm){"
        "if(nwnLocalLightEnabled==0)return vec2(0.0);"
        // SIX FACES: pick the one this fragment falls in from the major axis of
        // (fragment - light). A point light is a sphere of directions; a single
        // face can hold the light or its surroundings but never both, which is
        // why one cone measured coverage everywhere and occlusion nowhere.
        // FOUR LIGHTS, one layer each -- the same shape as the sun's cascade
        // layers, which is the path that demonstrably works. Darkest wins.
        "float best=0.0;float cov=0.0;"
        "for(int sl=0;sl<nwnLocalSlots&&sl<32;++sl){"
        "float r2=nwnLocalSlotRadius[sl]; if(r2<=0.0) continue;"
        "vec3 off=w.xyz-nwnLocalSlotPos[sl].xyz;"
        "float d2=dot(off,off); if(d2>=r2*r2) continue;"
        // Offset scales with this light's texel footprint at this distance:
        //   texel_world ~= 2 * tan(fov/2) * dist / mapSize
        // so it stays correct as the resolution or the light's range changes.
        "float dist=sqrt(d2);"
        "float texw=nwnLocalTanHalfFov*dist/float(textureSize(nwnLocalLightDepth,0).x);"
        // dFdx/dFdy gives us a geometric normal, but not an orientation: its
        // sign is determined by the screen mapping and can flip when the
        // camera yaws.  Applying that unsigned normal directly made the local
        // lookup jump to opposite sides of a surface as the camera turned.
        // Orient it toward THIS light before offsetting, so the offset is a
        // world/light-space operation rather than a camera-space one.
        "vec3 nlight=nrm*(dot(nrm,nwnLocalSlotPos[sl].xyz-w.xyz)>=0.0?1.0:-1.0);"
        "vec4 wo=vec4(w.xyz+nlight*texw*nwnLocalNormalBias,1.0);"
        "vec4 l=nwnLocalLightVP[sl]*wo; if(l.w<=0.0) continue;"
        "vec3 s=l.xyz/l.w*0.5+0.5;"
        "if(s.x<=0.0||s.x>=1.0||s.y<=0.0||s.y>=1.0||s.z<=0.0||s.z>=1.0) continue;"
        "cov=1.0;"
        // SOFTEN THE EDGES. Three of them, all previously hard:
        //   * the frustum border -- the shadow simply stopped where s.xy left
        //     the map, which is the abrupt cutoff you can see on the floor;
        //   * the far plane, same thing in depth;
        //   * the shadow's own outline, sampled NEAREST.
        // The first two fade over a band; the third gets 3x3 PCF.
        "vec2 eg=min(s.xy,vec2(1.0)-s.xy);"
        "float edge=clamp(min(eg.x,eg.y)/max(1e-4,nwnLocalEdgeFade),0.0,1.0);"
        "edge*=clamp((1.0-s.z)/max(1e-4,nwnLocalEdgeFade*0.5),0.0,1.0);"
        "edge=edge*edge*(3.0-2.0*edge);"          // smoothstep, no visible seam
        // PCF, identical in form to the sun's nwnLayerLit: 3x3 taps spaced by
        // ONE TEXEL times the radius, and a radius of 0 means take the single
        // sample and treat the result as binary -- so the two shadow systems
        // respond to their radius the same way.
        // MINIMUM SEPARATION, in WORLD units -- this is "no self-shadowing".
        //
        // Acne is precisely the case where the stored depth and the receiver's
        // own depth are the same surface, so ref-stored ~ 0. A real shadow has
        // a caster tens of centimetres nearer. Requiring a real gap therefore
        // removes self-shadowing outright while leaving genuine shadows alone,
        // and unlike a depth bias it means the same thing at every distance.
        //
        // The conversion is the derivative of the perspective depth curve at
        // this distance; zn/zf are rebuilt from the light's radius exactly as
        // the capture builds them, so the two cannot drift apart.
        // r2 holds the RADIUS, not its square (the name is a leftover) -- the
        // sqrt here was wrong and made the separation conversion use zn/zf from
        // a radius of 7.7 instead of 60. And these must mirror the CAPTURE's
        // formula exactly, including its 20-unit minimum range, or the world
        // units below convert against a frustum that was never rendered.
        "float rr=r2;"
        "float zni=max(0.05,min(0.5,rr*0.01));"
                // MIRRORS THE CAPTURE EXACTLY, lift included. The capture raises the
        // apex by nwnLocalLift and extends its far plane by the same amount;
        // leaving it out here converts world units against a frustum that was
        // never rendered, which is what the note above warns about.
        "float zfi=max(20.0,rr*2.0+max(0.0,nwnLocalLift));"
        "float dzdd=zni*zfi/max(1e-6,dist*dist*(zfi-zni));"
        "float need=nwnLocalLightBias+nwnLocalMinSep*dzdd;"
        "float lit=0.0;"
        "if(nwnLocalSoft<=0.0){"
        "float st=texture(nwnLocalLightDepth,vec3(s.xy,float(sl))).r;"
        "lit=(s.z-need<=st)?1.0:0.0;"
        "}else{"
        "vec2 tx=vec2(1.0)/vec2(textureSize(nwnLocalLightDepth,0).xy);"
        "for(int py=-1;py<=1;++py)for(int px=-1;px<=1;++px){"
        "vec2 u=s.xy+vec2(float(px),float(py))*tx*nwnLocalSoft;"
        "float st=texture(nwnLocalLightDepth,vec3(u,float(sl))).r;"
        "lit+=(s.z-need<=st)?1.0:0.0;}"
        "lit/=9.0;}"
        "float fa=d2/(r2*r2);"
        "float att=clamp((1.0-fa)/max(1e-4,nwnLampFalloff.x+nwnLampFalloff.y*fa),0.0,1.0);"
        // Compress the intensity->opacity range. See g_localLightFalloff.
        // NOTE this is the LOCAL shadow's att, not the sun-lift one further
        // down -- that expression carries a DO-NOT-FIX note and is untouched.
        "att=pow(att,max(0.05,nwnLocalFalloff));"
        "best=max(best,clamp(1.0-lit,0.0,1.0)*att*edge*nwnLocalSlotFade[sl]);}"
        "return vec2(best,cov);}\n"
        // Cube-specific diagnostic operands.  Do not reuse nwnLocalProbe:
        // it addresses the older one-layer-per-light matrices and became
        // meaningless once the debug launcher rebound its texture unit to the
        // private six-face target.  Output is (stored depth, reference depth,
        // coverage); the debug branch turns it into a directly readable delta.
         // Debug 4 reports the COMPARISON OPERANDS for slot 0 -- the one thing
        // no other mode can show. Every input has now been verified live
        // (census, capture with a caster at 0.54 units, fresh map, valid
        // uniforms, four real lamps), so the answer has to be in these two
        // numbers.
        //   x = stored depth in slot 0, y = the fragment's reference depth,
        //   both negative if the fragment is outside slot 0's frustum.
        "vec2 nwnLocalProbe(vec4 w){"
        "vec4 l=nwnLocalLightVP[0]*w; if(l.w<=0.0) return vec2(-1.0);"
        "vec3 s=l.xyz/l.w*0.5+0.5;"
        "if(s.x<=0.0||s.x>=1.0||s.y<=0.0||s.y>=1.0||s.z<=0.0||s.z>=1.0) return vec2(-1.0);"
        "return vec2(texture(nwnLocalLightDepth,vec3(s.xy,0.0)).r, s.z);}\n"
                // Debug 3 deliberately precedes EVERY texture fetch and discard: it is
        // the only probe that separates "this pass never reaches the
        // framebuffer" from "it reaches it but the background test discards
        // everything because the copied scene depth is all-far". Debug 1/2 sit
        // after that discard and therefore cannot tell those two apart.
        "void main(){ if(nwnReceiverDebug==3){gl_FragColor=vec4(1.0,0.0,1.0,1.0);return;}"
        "vec2 sz=vec2(textureSize(nwnStaticSceneDepth,0));"
        "vec2 uv=(gl_FragCoord.xy-nwnStaticViewportOrigin.xy)/sz;"
        "float d=texture2D(nwnStaticSceneDepth,uv).r; if(d>=0.999999) discard;"
        "if(nwnReceiverDebug==1){gl_FragColor=vec4(0.0,1.0,0.0,1.0);return;}"
        "vec4 w=nwnStaticCameraVPInv*vec4(uv*2.0-1.0,d*2.0-1.0,1.0); w/=w.w;"
        "float z=0.0;int layer=nwnStaticLayer; if(nwnStaticCSMEnabled!=0){"
        "z=-((nwnStaticCameraView*w).z); if(z<=0.0||z>nwnStaticClipFar3) discard;"
        "if(z<=nwnStaticClipFar0)layer=0;else if(z<=nwnStaticClipFar1)layer=1;"
        "else if(z<=nwnStaticClipFar2)layer=2;else layer=3;}"
        // PERFORMANCE: evaluate the world-anchored static term ONCE per pixel.
        // It has its own 3x3 PCF, and it used to be called from inside
        // nwnLayerLit -- which the receiver already calls inside a 3x3 PCF loop,
        // and twice more when cascade blending is on. That is 81-162 texture
        // samples per pixel for the static term alone (>150M per frame at
        // 1920x1009) and it dominated the frame.
        "float worldStatic=nwnWorldStaticLit(w);"
        "float lit=nwnLayerLit(layer,w,worldStatic);"
        "if(nwnStaticCSMEnabled!=0&&nwnStaticCascadeBlend>0.0&&layer<3){"
        "float edge=layer==0?nwnStaticClipFar0:layer==1?nwnStaticClipFar1:nwnStaticClipFar2;"
        "float from=max(0.0,edge-nwnStaticCascadeBlend);"
        "if(z>from){float nextLit=nwnLayerLit(layer+1,w,worldStatic);lit=mix(lit,nextLit,smoothstep(from,edge,z));}}"
        // Sun and local-light terms are resolved separately and then combined,
        // so a pixel the sun leaves lit can still be shadowed by a local light.
        // The old code discarded on the sun test alone, which would have made
        // every local-light shadow invisible. With the local term disabled
        // this reduces to exactly the previous behaviour.
        "bool filtered=nwnStaticPCFRadius>0.0||nwnStaticCascadeBlend>0.0;"
        "float sunShade=filtered?clamp(1.0-lit,0.0,1.0):(lit>0.5?0.0:1.0);"
        // Geometric normal of the receiving surface, from the depth-derived
        // world position. Cheap, and the only normal available here.
        // The derivative normal is only valid across a CONTINUOUS surface. At
        // a dithered or alpha-tested edge the two neighbours are different
        // surfaces (hair pixel beside a face pixel), the cross product is
        // nonsense, and offsetting along it throws the lookup somewhere random
        // -- the dark fringing around the dithering. Detect the discontinuity
        // by how far the position moves per pixel and drop the offset there:
        // an edge pixel gets no normal offset rather than a wrong one.
        "vec3 dxw=dFdx(w.xyz), dyw=dFdy(w.xyz);"
        "vec3 nrmW=normalize(cross(dxw,dyw));"
        "float step2=max(dot(dxw,dxw),dot(dyw,dyw));"
        "float cont=1.0-smoothstep(0.02,0.09,step2);"   // ~0.14..0.3 world units/px
        "vec2 localTerm=nwnLocalShadeCov(w,nrmW*cont);"
        "float localShade=localTerm.x;"
        // A SURFACE LIT BY A LOCAL LIGHT MUST NOT BE DARKENED BY THE SUN'S
        // SHADOW. The composite is flat translucent black over every
        // sun-shadowed pixel, which knows nothing about torches: walk under an
        // awning with a torch and the torchlight, the character and the floor
        // all dim together, which is backwards -- being out of the SUN says
        // nothing about a lamp two feet away.
        //
        // Lift the sun term by the local light's own falloff. This is the
        // distance term only, deliberately: it needs no depth map, so it works
        // for the light even where the cube has no coverage, and it cannot be
        // broken by the local-shadow work still in progress.
        // Brightest lamp wins, not a sum: two overlapping lights should not
        // over-lift into a hole in the shadow.
        // How lit is this pixel ALREADY, by the engine's own lighting? That
        // single number replaces the whole distance-to-lamp model: it accounts
        // for every lamp, its real falloff and cone, and the walls in between,
        // because NWN worked all of that out before we ever saw the frame.
        // NWN'S OWN ATTENUATION, from ComputePointLightSource in
        // data/base_shaders.bif -- not a curve of mine. Each lamp's w is its
        // range SQUARED, matching lightColor[n].a in the engine's shaders:
        //     f   = d^2 / range^2,  f >= 1 means the light does not reach
        //     att = (1 - f) / (maxIntensityInv + falloffFactor * f)
        // Brightest lamp wins rather than summing, so overlapping lights cannot
        // punch a hole through the shadow.
        // PERFORMANCE GATE -- this loop runs on EVERY receiver fragment, once
        // per uploaded lamp, and an area with 63 lamps therefore ran 63
        // iterations per pixel over the whole screen. That is the regression
        // that made the local-light build slower than the one before it, in a
        // scene where the sun cascades had not changed at all. Same shape as
        // the worldStatic hoist above: the work was correct and unconditional.
        // The gate is EXACT, not an approximation. The loop's only effect is
        // "sunShade *= 1 - lift*lampLit", so it cannot change a pixel where
        // sunShade is already 0 (the sun leaves it lit -- the majority of an
        // outdoor frame) or where the lift is switched off.
        "float lampLit=0.0;"
        "if(sunShade>0.0&&nwnLocalLightLift>0.0){"
        "for(int li=0;li<nwnLampCount&&li<128;++li){"
        "float r2=nwnLamps[li].w; if(r2<=0.0) continue;"
        "vec3 off=w.xyz-nwnLamps[li].xyz;"
        "float d2=dot(off,off); if(d2>=r2) continue;"
        "float f=d2/r2;"
        // DO NOT "FIX" THIS EXPRESSION FOR A WINDOWS SYMPTOM. It is shared by
        // both platforms and by every lamp. On 2026-08-15 it was normalised to
        // stop a torch blanking the sun's shadow on Windows -- and it broke
        // Linux, which had been confirmed working that same day. Reverted.
        // If Windows needs different behaviour here it gets its OWN path, set
        // from the host per platform. See the PLATFORM RULE in AGENTS.md.
        //
        // Known and accepted: falloff.x is 1/maxIntensity, so this peaks well
        // above 1 and CLAMPS across most of a lamp's radius rather than falling
        // off smoothly. That saturation is why a dense area (30 lamps of radius
        // ~38 in ~80 units, measured) lifts the sun's shadow everywhere.
        "float att=(1.0-f)/max(1e-4,nwnLampFalloff.x+nwnLampFalloff.y*f);"
        "lampLit=max(lampLit,clamp(att,0.0,1.0));}}"
        "float lampLift=1.0-clamp(nwnLocalLightLift*lampLit,0.0,1.0);"
        "sunShade*=lampLift;"
        // Fade the SUN's shadow out as the surface approaches full brightness.
        // The threshold is where it starts; it reaches zero at white. Smoothstep
        // so there is no visible edge where the guard begins.
        //
        // SUN ONLY, deliberately. It was applied to localShade as well at first
        // and that washed local shadows out: a torch-lit floor is bright by
        // definition, so the guard erased the very shadows the torch was
        // casting. A light dimming its own shadow is not the problem this
        // solves -- self-illuminated surfaces being darkened by the SUN is.
        "if(nwnSelfIllumGuard<1.0){"
        "vec3 sc=texture2D(nwnStaticSceneColor,uv).rgb;"
        "float lum=max(sc.r,max(sc.g,sc.b));"
        "float keep=1.0-smoothstep(nwnSelfIllumGuard,1.0,lum);"
        "sunShade*=keep;}"
        "float sunAlpha=nwnStaticCompositeStrength*sunShade;"
        "float localAlpha=nwnLocalLightStrength*localShade;"
        // Darkest wins rather than summing: two overlapping shadows should not
        // stack into a black hole.
        "float alpha=max(sunAlpha,localAlpha);"
        "float shade=max(sunShade,localShade);"
        // Fog occlusion: the engine already fogged this pixel, so a shadow on it
        // must fade by the same amount or it reads as a dark smear floating in
        // the haze. Linear fog on camera-space depth, matching the engine's
        // curve when its fog state is readable.
        "if(nwnFogEnabled!=0&&nwnFogEnd>nwnFogStart){"
        "float fog=clamp((z-nwnFogStart)/(nwnFogEnd-nwnFogStart),0.0,1.0);"
        "alpha*=1.0-fog;shade*=1.0-fog;}"
        // red = sun shadow, green = local shadow, BLUE = inside the local
        // light's frustum at all. Blue with no green means the cone reaches
        // there but nothing is shadowed; no blue means the cone does not
        // reach, i.e. an aiming/FOV problem rather than a shadow problem.
        "if(nwnReceiverDebug==2){gl_FragColor=vec4(sunShade,localShade,localTerm.y*0.6,1.0);return;}"
        // Debug 4 shows the COMPARISON OPERANDS, which is the one thing no
        // previous mode could: with a sampler2DShadow the compare happened
        // inside the hardware and only its verdict was visible. Both operands
        // live in a very narrow band near 1.0, so show their DIFFERENCE, which
        // is the quantity that actually decides a shadow.
        //   red   = the reference is BEHIND the stored surface, i.e. shadowed
        //   green = stored depth, stretched over the map's real range
        //   blue  = reference depth, same stretch
        // Red anywhere means the comparison sees an occluder. Red nowhere with
        // green/blue identical means the map holds only the receiving surface.
        "if(nwnReceiverDebug==4){"
        "vec2 pr=nwnLocalProbe(w);"
        "if(pr.x<0.0){gl_FragColor=vec4(0.0,0.0,0.35,1.0);return;}"   // outside slot 0
        "float st4=pr.x,rf4=pr.y;"
        "gl_FragColor=vec4(clamp((rf4-nwnLocalLightBias-st4)*200.0,0.0,1.0),"
        "clamp((st4-0.8)*5.0,0.0,1.0),clamp((rf4-0.8)*5.0,0.0,1.0),1.0);return;}"
        "if(shade<=0.001||alpha<=0.001)discard;"
        "if(nwnStaticComposite!=0) gl_FragColor=vec4(0.0,0.0,0.0,alpha);"
        "else gl_FragColor=vec4(0.70*shade,0.0,0.0,1.0); }\n";
    GLuint v=create(GL_VERTEX_SHADER), f=create(GL_FRAGMENT_SHADER);
    const GLint vl=(GLint)strlen(vs), fl=(GLint)strlen(fs);
    source(v,1,&vs,&vl); source(f,1,&fs,&fl); compile(v); compile(f);
    GLint vok=0, fok=0; getShader(v,GL_COMPILE_STATUS,&vok); getShader(f,GL_COMPILE_STATUS,&fok);
    if (!vok || !fok) {
        // Print the driver's actual error. "compile FAILED" with no log meant a
        // one-line GLSL slip (a stale call with the old argument count) cost a
        // full test round trip.
        auto getLog = (GetShaderInfoLog_t)glew_or_dlsym(eng::GlewGetShaderInfoLog,
                                                        "glGetShaderInfoLog");
        char info[2048];
        if (getLog) {
            GLsizei n = 0;
            if (!vok) { getLog(v, (GLsizei)sizeof(info), &n, info);
                        fprintf(stderr, "[shadowmap][static] receiver VERTEX shader failed:\n%s\n", info); }
            if (!fok) { getLog(f, (GLsizei)sizeof(info), &n, info);
                        fprintf(stderr, "[shadowmap][static] receiver FRAGMENT shader failed:\n%s\n", info);
                        // The driver's info log came back EMPTY on the failure
                        // that broke this receiver, which turned a one-line GLSL
                        // error into a test round trip. Dump the exact source so
                        // it can be checked offline:
                        //   glslangValidator -S frag shadowmap_failed_shader.frag
                        if (FILE* dump = open_dump_file("shadowmap_failed_shader.frag")) {
                            fputs(fs, dump);
                            fclose(dump);
                            fprintf(stderr, "[shadowmap][static] wrote shadowmap_failed_shader.frag "
                                            "(validate: glslangValidator -S frag <file>)\n");
                        } }
        } else {
            fprintf(stderr,"[shadowmap][static] receiver shader compile FAILED (no info log available)\n");
        }
        return;
    }
    GLuint p=createProg(); attach(p,v); attach(p,f); link(p);
    GLint pok=0; getProg(p,GL_LINK_STATUS,&pok);
    if (!pok) { fprintf(stderr,"[shadowmap][static] receiver program link FAILED\n"); return; }
    g_staticReceiverProgram=p;
    fprintf(stderr,"[shadowmap][static] fullscreen static receiver compiled (program=%u)\n",p);
}

