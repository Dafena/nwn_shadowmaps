#include "shadow_fog_lamp_fade.h"
static fog_lamp_fade::History g_fogLampHistory;
// ---------------------------------------------------------------------------
// Selectable quarter-, half-, or full-resolution volumetric fog and sun shafts.
//
// Each pixel reconstructs its world-space camera ray from the completed scene
// depth, marches that ray through a participating medium, and at every step
// asks the directional cascade maps whether the sun (or moon) reaches it. The
// cascade maps already contain opaque, dynamic, and alpha-tested casters, so
// foliage holes shape the lit volume in world space.  Projected sun position
// is diagnostic-only; normal rendering never clamps a source to a screen edge
// or performs a radial image blur.
//
// Developer builds can combine two parts; shipping defaults to authored fog:
//   * the injector's own exponential height fog ("Atmospheric density",
//     "Height falloff", "Fog base height"), which both scatters light toward
//     the camera and attenuates the scene behind it (Beer-Lambert);
//   * the AREA's authored fog, read from the engine's fogParams/fogColor
//     uniforms. NWN has already blended that into every surface, so here it
//     only makes the sun visible in the air -- it is never applied twice.
//
// Passes: linear-distance downsample -> jittered ray march -> separable
// depth-aware blur -> depth-aware upsample composited as
//     scene * transmittance + (1 - scene) * bounded in-scattering.
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

// What the panel shows under the enable box. Set only by the selected area
// scene, so UI/portrait Scene::Render calls cannot make it flicker.
static void godrays_fail(const char* reason);
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
    GLint texture27 = 0;
    GLint texture28 = 0;
    GLint texture29 = 0;
    GLint texture29Array = 0;
    GLint texture30 = 0;
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
        gl::ActiveTexture(GL_TEXTURE0 + 27);
        gl::GetIntegerv(GL_TEXTURE_BINDING_2D, &texture27);
        gl::ActiveTexture(GL_TEXTURE0 + 28);
        gl::GetIntegerv(GL_TEXTURE_BINDING_2D, &texture28);
        gl::ActiveTexture(GL_TEXTURE0 + 29);
        gl::GetIntegerv(GL_TEXTURE_BINDING_2D, &texture29);
        gl::GetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &texture29Array);
        gl::ActiveTexture(GL_TEXTURE0 + 30);
        gl::GetIntegerv(GL_TEXTURE_BINDING_2D, &texture30);
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
        gl::ActiveTexture(GL_TEXTURE0 + 27);
        gl::BindTexture(GL_TEXTURE_2D, (GLuint)texture27);
        gl::ActiveTexture(GL_TEXTURE0 + 28);
        gl::BindTexture(GL_TEXTURE_2D, (GLuint)texture28);
        gl::ActiveTexture(GL_TEXTURE0 + 29);
        gl::BindTexture(GL_TEXTURE_2D, (GLuint)texture29);
        gl::BindTexture(GL_TEXTURE_2D_ARRAY, (GLuint)texture29Array);
        gl::ActiveTexture(GL_TEXTURE0 + 30);
        gl::BindTexture(GL_TEXTURE_2D, (GLuint)texture30);
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
    // Pass 1 -- low-resolution LINEAR scene distance.
    //
    // The march, the blur and the upsample all need to compare depths. Raw
    // window depth cannot be used for that: it is hyperbolic, and the old pass
    // additionally copied it into an RGBA16F target, whose 11-bit mantissa
    // resolves only ~0.0005 near 1.0 -- i.e. tens of metres at the far end of
    // the frustum. Ray end points terraced and the "depth-aware" blur weight
    // exp(-|dz|*600) was blind to every distant edge. Radial distance in
    // metres has a constant RELATIVE error in a half float (~0.05%), which is
    // what an edge test wants. The farthest of the covered texels is kept so
    // a thin foreground object does not truncate the fog behind it; the
    // depth-aware upsample puts the foreground edge back.
    static const char* depthFragment =
        "#version 330 core\n"
        "out vec4 nwnFragColor;uniform sampler2D nwnSceneDepth;"
        "uniform vec2 nwnTargetSize;uniform mat4 nwnCameraVPInv;uniform vec4 nwnEye;"
        "void main(){vec2 uv=gl_FragCoord.xy/nwnTargetSize;vec2 o=.25/nwnTargetSize;"
        "float d=max(max(texture(nwnSceneDepth,uv+vec2(-o.x,-o.y)).r,"
        "texture(nwnSceneDepth,uv+vec2(o.x,-o.y)).r),"
        "max(texture(nwnSceneDepth,uv+vec2(-o.x,o.y)).r,"
        "texture(nwnSceneDepth,uv+vec2(o.x,o.y)).r));"
        "float dist=60000.0;"
        "if(d<.9999995){vec4 h=nwnCameraVPInv*vec4(uv*2.0-1.0,d*2.0-1.0,1.0);"
        "if(abs(h.w)>1e-6)dist=min(length(h.xyz/h.w-nwnEye.xyz),60000.0);}"
        "nwnFragColor=vec4(dist,0.0,0.0,1.0);}\n";
    // Pass 2 -- single-scattering ray march.
    //
    //   nwnMedium   x = extinction of the injector's own medium at/below the
    //                   base height (1/m), y = exponential height falloff
    //                   (1/m), z = base height (world Z), w = extinction
    //                   equivalent of the AREA's authored fog (1/m, 0 = none)
    //   nwnFogRange x,y = engine fogStart/fogEnd (view-space Z, exactly what
    //                   inc_common's GetFog() ramps over), z = march limit,
    //                   w = ambient (fog-colour) in-scattering amount
    //   nwnShaft    x = sun in-scattering strength, y = Henyey-Greenstein g,
    //                   z = area sun/moon policy fade (shafts only),
    //                   w = 1 when the cascade maps may be sampled this frame
    //
    // Output: rgb = in-scattered radiance, a = transmittance applied to the
    // scene. The composite is  scene * a + rgb.
    //
    // The engine has ALREADY mixed its linear fog into every surface, so the
    // authored medium contributes sun shafts (and attenuates distant ones) but
    // neither fog-colour in-scattering nor scene extinction a second time.
    // Only the injector's own height medium does those.
    static const char* scatterFragment =
        "#version 330 core\n"
        "out vec4 nwnFragColor; uniform sampler2D nwnLinearDepth;"
        "uniform sampler2DArrayShadow nwnStaticLightDepth;"
        "uniform sampler2DArrayShadow nwnDynamicLightDepth;"
        "uniform vec2 nwnTargetSize,nwnSunUV;"
        "uniform vec4 nwnSunColor,nwnFogColor,nwnMedium,nwnFogRange,nwnShaft;"
        "uniform mat4 nwnCameraVPInv,nwnCameraView,nwnLightVP[4];"
        "uniform vec4 nwnClipFar,nwnEye,nwnLightDir;"
        "uniform int nwnSamples,nwnDebug,nwnDynamicLayers;"
        "uniform float nwnBias,nwnLampGain;"
        "uniform vec4 nwnLampPos[128],nwnLampColor[128];uniform vec2 nwnLampFalloff;"
        "uniform int nwnLampCount,nwnRefinePass;uniform sampler2D nwnRefineSceneDepth;"
        "uniform vec2 nwnRefineOrigin;"
        "bool nwnFogDepthMatch(float a,float b){return abs(a-b)<=max(.05,.02*min(a,b));}"
        "float nwnLit(vec3 world,float z){int layer=0;"
        "if(z>nwnClipFar.x)layer=1;if(z>nwnClipFar.y)layer=2;"
        "if(z>nwnClipFar.z)layer=3;"
        "vec4 lp=nwnLightVP[layer]*vec4(world,1);if(lp.w<=0.0)return 1.0;"
        "vec3 s=lp.xyz/lp.w*.5+.5;"
        "if(any(lessThanEqual(s,vec3(0)))||any(greaterThanEqual(s,vec3(1))))return 1.0;"
        "float a=texture(nwnStaticLightDepth,vec4(s.xy,float(layer),s.z-nwnBias));"
        "float b=layer<nwnDynamicLayers?texture(nwnDynamicLightDepth,"
        "vec4(s.xy,float(layer),s.z-nwnBias)):1.0;return min(a,b);}\n"
        // Native fog becomes opaque at its authored view-space end. Never
        // let geometry hidden beyond it change the volume's integration end.
        "float nwnFogMarchEnd(float sceneDist,float farLimit,float fogEnd,float authored,float viewPerMetre){"
        "return authored>0.0?min(min(sceneDist,farLimit),max(fogEnd,0.0)/max(viewPerMetre,1e-6)):min(sceneDist,farLimit);}"
        "void main(){ vec2 uv=(gl_FragCoord.xy-(nwnRefinePass!=0?nwnRefineOrigin:vec2(0)))/nwnTargetSize;"
        "if(nwnDebug==1){nwnFragColor=vec4(1,0,1,1);return;}"
        "float sceneDist=texture(nwnLinearDepth,uv).r;"
        "if(nwnRefinePass!=0){float d=texture(nwnRefineSceneDepth,uv).r;sceneDist=60000.0;"
        "if(d<.9999995){vec4 h=nwnCameraVPInv*vec4(uv*2.0-1.0,d*2.0-1.0,1);"
        "if(abs(h.w)>1e-6)sceneDist=min(length(h.xyz/h.w-nwnEye.xyz),60000.0);}"
        "ivec2 size=textureSize(nwnLinearDepth,0);ivec2 base=ivec2(floor(uv*vec2(size)-.5));"
        "bool matched=false;for(int j=0;j<2;++j)for(int i=0;i<2;++i){"
        "ivec2 q=clamp(base+ivec2(i,j),ivec2(0),size-ivec2(1));"
        "matched=matched||nwnFogDepthMatch(texelFetch(nwnLinearDepth,q,0).r,sceneDist);}"
        "if(matched)discard;}"
        "if(nwnDebug==2){nwnFragColor=vec4(vec3(sceneDist>=50000.0?1.0:0.0),1);return;}"
        "if(nwnDebug==4){ float a=1.0-smoothstep(.010,.030,length(uv-nwnSunUV));"
        "nwnFragColor=vec4(vec3(1.0,.75,.05)*a,1);return;}"
        // The ray direction comes from a point INSIDE the frustum (NDC z=0),
        // not from the far plane, whose unprojection is ill-conditioned.
        "vec4 midH=nwnCameraVPInv*vec4(uv*2.0-1.0,0.0,1.0);"
        "if(abs(midH.w)<1e-6){nwnFragColor=vec4(0,0,0,1);return;}"
        "vec3 delta=midH.xyz/midH.w-nwnEye.xyz;float deltaLength=length(delta);"
        "if(deltaLength<1e-5){nwnFragColor=vec4(0,0,0,1);return;}"
        "vec3 ray=delta/deltaLength;"
        // View-space depth gained per metre along this ray: the cascade
        // split and the engine fog ramp are both defined on view Z.
        "float viewPerMetre=max(-(nwnCameraView*vec4(ray,0.0)).z,0.0);"
        "float marchLength=nwnFogMarchEnd(sceneDist,nwnFogRange.z,nwnFogRange.y,nwnMedium.w,viewPerMetre);"
        "float stepLength=marchLength/float(nwnSamples);"
        // Interleaved gradient noise (Jimenez 2014) offsets each pixel's
        // samples within one step, turning the fixed-step banding into a
        // high-frequency pattern. It is a function of the pixel only -- never
        // of time -- so it cannot crawl, and the depth-aware blur that follows
        // is sized to remove it. (The earlier attempt at jitter fed the result
        // into a high-pass filter, which amplified the noise instead.)
        "float jitter=fract(52.9829189*fract(dot(gl_FragCoord.xy,"
        "vec2(.06711056,.00583715))));"
        "float span=max(nwnFogRange.y-nwnFogRange.x,1.0);"
        "float sunAcc=0.0,ambAcc=0.0,litAcc=0.0,tAll=1.0,tUser=1.0;"
        "for(int i=0;i<64;i++){if(i>=nwnSamples)break;"
        "float dist=(float(i)+jitter)*stepLength;"
        "vec3 p=nwnEye.xyz+ray*dist;float vz=dist*viewPerMetre;"
        "float sUser=nwnMedium.x*exp(-nwnMedium.y*max(p.z-nwnMedium.z,0.0));"
        "float sAuth=nwnMedium.w*clamp((vz-nwnFogRange.x)/(.2*span),0.0,1.0);"
        "float sAll=sUser+sAuth;"
        // Beer-Lambert over the step, integrated analytically so the result
        // is independent of the step count (Hillaire 2015): the light a step
        // scatters toward the eye is T*(1-exp(-sigma*dt)), not T*sigma*dt.
        "float eAll=exp(-sAll*stepLength);float w=tAll*(1.0-eAll);"
        "float vis=nwnShaft.w>.5?nwnLit(p,vz):0.0;"
        "sunAcc+=w*vis;litAcc+=vis;"
        "ambAcc+=w*sUser/max(sAll,1e-6);"
        "tAll*=eAll;tUser*=exp(-sUser*stepLength);}"
        // Henyey-Greenstein, normalised so an isotropic medium is 1. A
        // constant floor is mixed in because NWN's camera looks DOWN: with a
        // pure forward lobe the shafts exist only while staring at the sun,
        // which was the previous pass's camera-facing gate in another form.
        "float g=clamp(nwnShaft.y,0.0,.95);"
        "float ct=clamp(dot(ray,-normalize(nwnLightDir.xyz)),-1.0,1.0);"
        "float hg=(1.0-g*g)/pow(max(1.0+g*g-2.0*g*ct,1e-3),1.5);"
        "float phase=min(.4+.6*hg,6.0);"
        "if(nwnDebug==3){nwnFragColor=vec4(1.0-litAcc/float(nwnSamples),"
        "clamp(phase/6.0,0.0,1.0),clamp(sunAcc,0.0,1.0),1);return;}"
        "vec3 fogHue=nwnFogColor.rgb/max(max(nwnFogColor.r,"
        "max(nwnFogColor.g,nwnFogColor.b)),.05);"
        "vec3 shaft=nwnSunColor.rgb*mix(vec3(1.0),fogHue,.35)*"
        "(nwnShaft.x*phase*sunAcc);"
        "vec3 ambient=nwnFogColor.rgb*(nwnFogRange.w*ambAcc);"
        // Local lights (torches, lamps). Each selected light is a sphere of
        // its NWN radius; only the part of this pixel's ray that crosses it
        // can glow, and the ray already stops at the visible surface, so a
        // wall in front of a lamp hides the glow behind it. Inside the
        // sphere the light falls off exactly as NWN's own point lights do
        // (inc_lighting ComputePointLightSource, f = d^2/r^2):
        //     (1 - f) / (lightMaxIntensityInv + lightFalloffFactor * f)
        // normalised to 1 at the centre. That curve is steep, which is what
        // makes a soft bright core with a wide faint halo. Eight jittered
        // samples across the chord; the fog in between attenuates it.
        // Not shadowed: a lamp's glow is not blocked by geometry IN THE AIR
        // between the lamp and the ray.
        "vec3 lampAcc=vec3(0.0);"
        "for(int l=0;l<128;l++){if(l>=nwnLampCount)break;"
        "vec4 lp=nwnLampPos[l];vec3 oc=nwnEye.xyz-lp.xyz;float b=dot(oc,ray);"
        "float h=b*b-(dot(oc,oc)-lp.w*lp.w);if(h<=0.0)continue;h=sqrt(h);"
        "float t0=max(-b-h,0.0),t1=min(-b+h,marchLength);if(t1<=t0)continue;"
        "float dt=(t1-t0)*.125;float s=0.0;"
        "for(int k=0;k<8;k++){float t=t0+(float(k)+jitter)*dt;vec3 q=nwnEye.xyz+ray*t;"
        "float f=min(dot(q-lp.xyz,q-lp.xyz)/(lp.w*lp.w),1.0);"
        "float att=(1.0-f)*nwnLampFalloff.x/(nwnLampFalloff.x+nwnLampFalloff.y*f);"
        "float sig=nwnMedium.x*exp(-nwnMedium.y*max(q.z-nwnMedium.z,0.0))+"
        "nwnMedium.w*clamp((t*viewPerMetre-nwnFogRange.x)/(.2*span),0.0,1.0);"
        "s+=sig*att*exp(-sig*t);}"
        "lampAcc+=nwnLampColor[l].rgb*(nwnLampColor[l].a*s*dt);}"
        // One shoulder for all sources. Bounding each source separately still
        // allows their sum to exceed the LDR framebuffer's range. Apply the
        // area fade to radiance before the shoulder, not afterwards.
        "vec3 inscatter=vec3(1.0)-exp(-(ambient+shaft*nwnShaft.z+lampAcc*nwnLampGain));"
        "nwnFragColor=vec4(inscatter,tUser); }\n";
    // Pass 4 -- depth-aware upsample and composite.  Each full-resolution
    // pixel weights its four low-resolution neighbours by how closely their
    // distance matches its own, so fog computed behind a silhouette does not
    // bleed a halo onto the object in front of it.
    static const char* compositeFragment =
        "#version 330 core\n"
        "out vec4 nwnFragColor;uniform sampler2D nwnVolume,nwnLinearDepth,nwnSceneDepth;"
        "uniform vec2 nwnScreenSize,nwnScreenOrigin,nwnLowSize;"
        "uniform mat4 nwnCameraVPInv;uniform vec4 nwnEye;uniform int nwnDebug;"
        "bool nwnFogDepthMatch(float a,float b){return abs(a-b)<=max(.05,.02*min(a,b));}"
        "void main(){vec2 uv=(gl_FragCoord.xy-nwnScreenOrigin)/nwnScreenSize;"
        "if(all(equal(nwnLowSize,nwnScreenSize))){nwnFragColor=texelFetch(nwnVolume,ivec2(gl_FragCoord.xy-nwnScreenOrigin),0);return;}"
        "if(nwnDebug!=0){nwnFragColor=vec4(texture(nwnVolume,uv).rgb,1);return;}"
        "float depth=texture(nwnSceneDepth,uv).r;float dist=60000.0;"
        "if(depth<.9999995){vec4 h=nwnCameraVPInv*vec4(uv*2.0-1.0,depth*2.0-1.0,1.0);"
        "if(abs(h.w)>1e-6)dist=min(length(h.xyz/h.w-nwnEye.xyz),60000.0);}"
        "vec2 lp=uv*nwnLowSize-.5;ivec2 base=ivec2(floor(lp));vec2 f=lp-floor(lp);"
        "ivec2 hi=ivec2(nwnLowSize)-ivec2(1);vec4 sum=vec4(0);float total=0.0;"
        "for(int j=0;j<2;++j)for(int i=0;i<2;++i){"
        "ivec2 q=clamp(base+ivec2(i,j),ivec2(0),hi);"
        "float ld=texelFetch(nwnLinearDepth,q,0).r;"
        "if(!nwnFogDepthMatch(ld,dist))continue;"
        "float bw=(i==0?1.0-f.x:f.x)*(j==0?1.0-f.y:f.y)+.02;"
        "float w=bw/(.01+abs(ld-dist)/max(min(ld,dist),1.0));"
        "sum+=texelFetch(nwnVolume,q,0)*w;total+=w;}"
        "if(total<=0.0)discard;nwnFragColor=sum/total;}\n";
    // Pass 3 -- separable depth-aware Gaussian over the low-resolution
    // volume (colour AND transmittance). The edge tolerance is relative to
    // distance, so it behaves the same at 5 m and at 100 m.
    static const char* blurFragment =
        "#version 330 core\n"
        "out vec4 nwnFragColor;uniform sampler2D nwnVolume,nwnLinearDepth;"
        "uniform vec2 nwnTargetSize,nwnAxis;uniform float nwnSigma;"
        "void main(){ivec2 p=ivec2(gl_FragCoord.xy);ivec2 hi=ivec2(nwnTargetSize)-ivec2(1);"
        "float cd=texelFetch(nwnLinearDepth,p,0).r;float tolerance=.08*cd+.1;"
        "vec4 sum=vec4(0);float total=0.0;"
        "for(int i=-5;i<=5;++i){ivec2 q=clamp(p+ivec2(nwnAxis)*i,ivec2(0),hi);"
        "float d=texelFetch(nwnLinearDepth,q,0).r;float x=float(i);"
        "if(abs(d-cd)>max(.05,.02*min(d,cd)))continue;"
        "float w=exp(-.5*x*x/(nwnSigma*nwnSigma))*exp(-abs(d-cd)/tolerance);"
        "sum+=texelFetch(nwnVolume,q,0)*w;total+=w;}"
        "nwnFragColor=sum/max(total,1e-4);}\n";
    g_godrayMaskProgram = godrays_make_program(depthFragment, "linear-depth");
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

static void godrays_fail(const char* reason) {
    g_godraysStatus = reason;
    godrays_blocked(reason);
}

static void draw_godrays(void* scene) {
    if constexpr (!kGodraysFeatureAvailable) return;
    if (!script_env::b(script_env::FogOn, g_godraysEnabled)) {
        if (scene == g_areaScene) { g_godraysStatus = "off"; g_fogLampHistory.reset(); }
        return;
    }
    // Effective values: an area script's override, else the player's setting.
    using script_env::f;
    const float fogDensity       = f(script_env::FogDensity,       NWN_SHIP ? 0.0f : g_atmosphereDensity);
    const float fogHeightFalloff = f(script_env::FogHeightFalloff, g_godraysHeightFalloff);
    const float fogHeightBase    = f(script_env::FogHeightBase,    g_godraysHeightBase);
    const float fogBrightness    = f(script_env::FogBrightness,    g_godraysAmbient);
    const float fogLampGlow      = f(script_env::FogLampGlow,      g_godraysLampGlow);
    const float fogShafts        = f(script_env::FogShafts,        g_godraysStrength);
    const float fogAnisotropy    = f(script_env::FogAnisotropy,    g_godraysAnisotropy);
    // Trace mode owns the selected-area context but does not update the
    // normal-run g_isWorldScene flag.  The selected scene plus its frozen
    // context is the authoritative gate in both launch paths.
    if (scene != g_areaScene) {
        godrays_blocked("not-current-area-scene"); return;
    }
    if (!g_shadowFrameContext.valid || g_shadowFrameContext.scene != scene) {
        godrays_fail("no-current-area-context"); return;
    }
    // This is the shared authority used by the native-shadow fog fade too.
    // Polling is internally throttled and reads uniforms only; it never alters
    // NWN's shader state.
    // Interiors keep their authored look: the injector's outdoor haze would
    // otherwise fill every room. NWN's own interior flag is the authority.
    if (weather_runtime::g.areaFlagsKnown && weather_runtime::g.areaInterior &&
        weather_runtime::g.observedScene == scene) {
        g_godraysStatus = "off in interiors"; return;
    }
    poll_engine_fog();
    if (!gl::GetIntegerv || !gl::GetBooleanv || !gl::IsEnabled || !gl::Enable ||
        !gl::Disable || !gl::BlendFuncSeparate || !gl::BlendEquationSeparate ||
        !gl::Viewport || !gl::ActiveTexture || !gl::BindTexture ||
        !gl::BindFramebuffer || !gl::FramebufferTexture2D || !gl::DepthMask ||
        !gl::ColorMask || !gl::GetUniformLocation ||
        !gl::Uniform1i || !gl::Uniform1f || !gl::Uniform2f || !gl::Uniform4f ||
        !gl::UniformMatrix4fv) {
        godrays_fail("missing-gl-entry"); return;
    }
    UseProgram_t useProgram = g_realUseProgram ? g_realUseProgram :
        (UseProgram_t)glew_or_dlsym(eng::GlewUseProgram, "glUseProgram");
    if (!useProgram) { godrays_fail("no-glUseProgram-dispatcher"); return; }
    GLint entryViewport[4] = {0, 0, 0, 0}, entryFbo = 0;
    gl::GetIntegerv(GL_VIEWPORT, entryViewport);
    gl::GetIntegerv(GL_FRAMEBUFFER_BINDING, &entryFbo);
    if (entryFbo != 0 || entryViewport[2] <= 0 || entryViewport[3] <= 0) {
        godrays_fail(entryFbo ? "not-default-framebuffer" : "empty-viewport"); return;
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
        godrays_fail("shader-build-failed"); return;
    }
    if (!ensure_godray_targets(targetW, targetH)) {
        godrays_fail("target-allocation-failed"); return;
    }
    float sunU = 0.5f, sunV = 0.5f;
    float lightDirection[3] = {0.0f, 0.0f, -1.0f};
    const bool needsVolume = g_godraysDebug == 0 || g_godraysDebug == 3;
    const bool needsSunPosition = g_godraysDebug == 4;
    // Sun shafts need a light direction, NWN's permission to cast sun/moon
    // shadows, and this frame's cascade maps. The fog itself needs none of
    // them: without them it is drawn unlit-by-shafts rather than not at all.
    // (It used to return here, so a rainy night with moon shadows disabled
    // showed no fog whatsoever.)
    const char* shaftsOffReason = nullptr;
    if (needsVolume && !godrays_sun_direction(lightDirection))
        shaftsOffReason = "no sun/moon direction";
    if (needsSunPosition && !godrays_project_sun(sunU, sunV)) {
        godrays_fail("no-projectable-sun-direction"); return;
    }
    const float projectedSunU = sunU;
    const float projectedSunV = sunV;
    // The marker view pins an offscreen sun to the nearest edge so the
    // diagnostic stays visible. The volume itself never uses a screen-space
    // sun position: an offscreen sun still lights the fog in view.
    if (needsSunPosition) {
        sunU = std::max(0.02f, std::min(0.98f, sunU));
        sunV = std::max(0.02f, std::min(0.98f, sunV));
    }
    // NWN's own area policy (sun/moon shadows allowed, day/night fade) scales
    // the SHAFTS, so a sunset fades them out with the directional shadows
    // instead of popping. The fog body is unaffected.
    float policyFade = 1.0f;
    if (g_cascadeCompositeStrength > 1.0e-3f)
        policyFade = std::max(0.0f, std::min(1.0f,
            effective_area_sun_strength() / g_cascadeCompositeStrength));
    if (!shaftsOffReason && policyFade <= 0.001f)
        shaftsOffReason = "area has no sun/moon shadows";
    const bool cascadesReady = g_cascadeTargetsUsable && g_cascadeStaticTex &&
        g_cascadeDynamicTex && g_cascadeMath.valid &&
        g_cascadeMath.sourceFrame == g_shadowFrameContext.serial;
    if (!shaftsOffReason && !cascadesReady)
        shaftsOffReason = "sun shadow maps unavailable";
    const bool shaftsOn = needsVolume && !shaftsOffReason;
    if (g_godraysDebug == 3 && !shaftsOn) {
        godrays_fail(shaftsOffReason ? shaftsOffReason : "sun shadow maps unavailable");
        return;
    }
    // The scene-depth copy is made by the sun-shadow receiver. It must be from
    // THIS frame: with sun shadows switched off the texture still exists but
    // holds an old camera's depth.
    if (g_godraysDebug != 1 && g_godraysDebug != 4 &&
        (!g_sceneDepthTex || g_sceneDepthSerial != g_shadowFrameContext.serial)) {
        godrays_fail("no scene depth this frame (are sun shadows enabled?)"); return;
    }

    gl::Disable(GL_DEPTH_TEST); gl::Disable(GL_BLEND); gl::Disable(GL_SCISSOR_TEST);
    gl::DepthMask(GL_FALSE); gl::ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl::BindFramebuffer(GL_FRAMEBUFFER, g_godrayFbo);
    gl::Viewport(0, 0, targetW, targetH);

    // Pass 1: low-resolution linear scene distance (kept for every later pass).
    const bool haveDepth = g_godraysDebug != 1 && g_godraysDebug != 4;
    if (haveDepth) {
        gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                 g_godrayMaskTexture, 0);
        gl::BindTexture(GL_TEXTURE_2D, g_sceneDepthTex);
        useProgram(g_godrayMaskProgram);
        GLint location = gl::GetUniformLocation(g_godrayMaskProgram, "nwnSceneDepth");
        if (location >= 0) gl::Uniform1i(location, 28);
        location = gl::GetUniformLocation(g_godrayMaskProgram, "nwnTargetSize");
        if (location >= 0) gl::Uniform2f(location, (float)targetW, (float)targetH);
        location = gl::GetUniformLocation(g_godrayMaskProgram, "nwnCameraVPInv");
        if (location >= 0) gl::UniformMatrix4fv(location, 1, GL_FALSE,
                                                 g_shadowFrameContext.viewProjectionInverse);
        location = gl::GetUniformLocation(g_godrayMaskProgram, "nwnEye");
        if (location >= 0) gl::Uniform4f(location, g_shadowFrameContext.eye.x,
                                         g_shadowFrameContext.eye.y,
                                         g_shadowFrameContext.eye.z, 1.0f);
        godrays_draw_fullscreen();
    }

    // Pass 2: ray march.
    gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                             g_godrayScatterTexture, 0);
    gl::ActiveTexture(GL_TEXTURE0 + 29);
    gl::BindTexture(GL_TEXTURE_2D_ARRAY, shaftsOn ? g_cascadeStaticTex : 0);
    gl::ActiveTexture(GL_TEXTURE0 + 30);
    gl::BindTexture(GL_TEXTURE_2D_ARRAY, shaftsOn ? g_cascadeDynamicTex : 0);
    gl::ActiveTexture(GL_TEXTURE0 + 28);
    gl::BindTexture(GL_TEXTURE_2D, g_godrayMaskTexture);
    useProgram(g_godrayScatterProgram);
    GLint refineLocation=gl::GetUniformLocation(g_godrayScatterProgram,"nwnRefinePass");
    if(refineLocation>=0)gl::Uniform1i(refineLocation,0);
    // Separate texture unit: sampler2D and sampler2DArrayShadow must not alias.
    gl::ActiveTexture(GL_TEXTURE0+27);gl::BindTexture(GL_TEXTURE_2D,g_sceneDepthTex);
    GLint refineDepth=gl::GetUniformLocation(g_godrayScatterProgram,"nwnRefineSceneDepth");
    if(refineDepth>=0)gl::Uniform1i(refineDepth,27);
    GLint location = gl::GetUniformLocation(g_godrayScatterProgram, "nwnLinearDepth");
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

    // Sun/moon colour. SunDiffuse is an LDR engine-light value, not HDR solar
    // radiance: keep its hue at full saturation, and let its peak dim the
    // shafts only partially so a moon still reads without glowing like a sun.
    float rawR = (eng::SunDiffuse && std::isfinite(eng::SunDiffuse[0])) ? std::max(0.0f, eng::SunDiffuse[0]) : 1.0f;
    float rawG = (eng::SunDiffuse && std::isfinite(eng::SunDiffuse[1])) ? std::max(0.0f, eng::SunDiffuse[1]) : 0.88f;
    float rawB = (eng::SunDiffuse && std::isfinite(eng::SunDiffuse[2])) ? std::max(0.0f, eng::SunDiffuse[2]) : 0.62f;
    const float rawPeak = std::max(rawR, std::max(rawG, rawB));
    float tintR = 1.0f, tintG = 0.88f, tintB = 0.62f;
    if (rawPeak > 1.0e-4f) {
        tintR = rawR / rawPeak; tintG = rawG / rawPeak; tintB = rawB / rawPeak;
    }
    const float sunLevel = 0.35f + 0.65f * std::min(1.0f, rawPeak);
    location = gl::GetUniformLocation(g_godrayScatterProgram, "nwnSunColor");
    if (location >= 0) gl::Uniform4f(location, tintR * sunLevel, tintG * sunLevel,
                                     tintB * sunLevel, 1.0f);

    const bool authoredFog = g_haveEngineFog &&
        g_fogEndEngine - g_fogStartEngine >= 5.0f;
    g_fogFromEngine = authoredFog;
    g_fogStartLive = authoredFog ? g_fogStartEngine : 0.0f;
    g_fogEndLive = authoredFog ? g_fogEndEngine : 0.0f;
    const float authoredSpan = authoredFog ?
        std::max(5.0f, g_fogEndEngine - g_fogStartEngine) : 1.0f;
    // March far enough to cover the shadowed volume and the authored fog
    // range, but never so far that a sky ray spends its steps on empty air.
    float marchFar = cascadesReady ? std::max(60.0f, g_cascadeMath.clipFar[3]) : 60.0f;
    if (authoredFog) marchFar = std::max(marchFar, g_fogEndEngine);
    if (!std::isfinite(marchFar)) marchFar = 60.0f;
    marchFar = std::min(marchFar, 250.0f);
    location = gl::GetUniformLocation(g_godrayScatterProgram, "nwnFogRange");
    if (location >= 0) gl::Uniform4f(location,
        authoredFog ? g_fogStartEngine : 0.0f,
        authoredFog ? g_fogEndEngine : 1.0f,
        marchFar, fogBrightness);
    // The fog colour is the engine's `fogColor` uniform (gamma space, like the
    // framebuffer). With no authored fog the medium takes a dimmed copy of the
    // sun/moon colour, so a night area does not get a day-grey haze.
    float fogR = g_fogColorEngine[0], fogG = g_fogColorEngine[1], fogB = g_fogColorEngine[2];
    if (!g_haveEngineFogColor) {
        fogR = std::max(0.04f, std::min(1.0f, rawR * 0.70f));
        fogG = std::max(0.04f, std::min(1.0f, rawG * 0.74f));
        fogB = std::max(0.05f, std::min(1.0f, rawB * 0.82f));
    }
    location = gl::GetUniformLocation(g_godrayScatterProgram, "nwnFogColor");
    if (location >= 0) gl::Uniform4f(location, fogR, fogG, fogB, 1.0f);
    // "Atmospheric density" 0..1 maps to 0..0.04 extinction per metre: at the
    // top of the range a surface 40 m away keeps ~20% of its own colour. The
    // area's linear fog becomes an equivalent uniform medium that removes
    // ~78% over its authored span.
    constexpr float kDensityScale = 0.04f;
    location = gl::GetUniformLocation(g_godrayScatterProgram, "nwnMedium");
    // NWN already applies the area's linear opacity to scene surfaces. Shipping
    // defaults to lighting that medium without a second fixed haze. Explicit
    // area-script density overrides still work; developers retain manual tuning.
    const float atmosphereExtinction = std::max(0.0f, fogDensity) * kDensityScale;
    if (location >= 0) gl::Uniform4f(location,
        atmosphereExtinction,
        std::max(0.0f, fogHeightFalloff),
        fogHeightBase,
        authoredFog ? 1.5f / authoredSpan : 0.0f);
    // Local lights for the fog glow: the engine's own per-frame light census
    // (the same list the sun-shadow lamp lift and the rain tint use), not a
    // guess. Follow Lighting Max Lights, separately from shadow-map budgets. A
    // radius outside 0.25..40 m is not a lamp -- the sun has been seen
    // arriving through SetLightGL with a directional light's huge radius.
    static_assert(kMaxLampLights == fog_lamp_fade::kCapacity, "fog light capacities must match");
    float lampPos[kMaxLampLights * 4] = {}, lampColor[kMaxLampLights * 4] = {};
    const unsigned lampLimit=static_cast<unsigned>(std::max(1,std::min(lamp_upload_max(),static_cast<int>(kMaxLampLights))));
    int lampCount = 0;
    fog_lamp_fade::Light targets[kMaxLampLights] = {};
    unsigned targetCount=0;
    if (fogLampGlow > 0.0f) {
        struct LampPick { float distance; unsigned index; };
        LampPick picks[kLampPool];
        unsigned candidates = 0;
        const unsigned count = std::min<unsigned>(g_lampLightCount, kLampPool);
        for (unsigned i = 0; i < count; ++i) {
            const LampEntry& lamp = g_lampLights[i];
            if (!(lamp.radius >= 0.25f && lamp.radius <= 40.0f)) continue;
            if (!std::isfinite(lamp.pos[0]) || !std::isfinite(lamp.pos[1]) ||
                !std::isfinite(lamp.pos[2])) continue;
            if (std::max(lamp.color[0], std::max(lamp.color[1], lamp.color[2])) <= 0.01f)
                continue;
            const float dx = lamp.pos[0] - g_shadowFrameContext.eye.x;
            const float dy = lamp.pos[1] - g_shadowFrameContext.eye.y;
            const float dz = lamp.pos[2] - g_shadowFrameContext.eye.z;
            const float edge = std::sqrt(dx * dx + dy * dy + dz * dz) - lamp.radius;
            if (edge > marchFar) continue;
            picks[candidates++] = {std::max(0.0f, edge), i};
        }
        const unsigned keep = std::min(candidates, lampLimit);
        std::partial_sort(picks, picks + keep, picks + candidates,
            [](const LampPick& a, const LampPick& b) { return a.distance < b.distance; });
        for (unsigned k = 0; k < keep; ++k) {
            const LampEntry& lamp = g_lampLights[picks[k].index];
            auto& target=targets[targetCount++];
            target.key=lamp.key;target.radius=lamp.radius;
            for(int c=0;c<3;++c){
                target.pos[c]=lamp.pos[c];
                target.color[c]=std::isfinite(lamp.color[c]) ? std::max(0.f,std::min(2.f,lamp.color[c])) : 0.f;
            }
        }
    }
    static unsigned loggedLampLimit=0;
    if(targetCount && loggedLampLimit!=lampLimit){
        loggedLampLimit=lampLimit;
        fprintf(stderr,"[shadowmap][godrays] Lighting Max Lights=%u fog candidates selected=%u capacity=%u\n",lampLimit,targetCount,kMaxLampLights);
    }
    if(fogLampGlow<=0) g_fogLampHistory.reset();
    g_fogLampHistory.update(targets,targetCount,now_seconds(),scene,weather_runtime::g.weatherArea);
    for(const auto& slot:g_fogLampHistory.slots){
        if(!slot.light.key || slot.weight<=0)continue;
        for(int c=0;c<3;++c){lampPos[lampCount*4+c]=slot.light.pos[c];lampColor[lampCount*4+c]=slot.light.color[c];}
        // Fires glow orange in the fog (with firelight on), at the engine
        // light's own brightness, instead of its sometimes yellow-green colour.
        float fire[3];
        if(g_fireLightEnabled && lamp_is_fire(slot.light.color,fire)){
            const float peak=std::max(slot.light.color[0],std::max(slot.light.color[1],slot.light.color[2]));
            for(int c=0;c<3;++c) lampColor[lampCount*4+c]=fire[c]*peak;
        }
        lampPos[lampCount*4+3]=slot.light.radius;
        lampColor[lampCount*4+3]=slot.weight;
        ++lampCount;
    }
    location = gl::GetUniformLocation(g_godrayScatterProgram, "nwnLampCount");
    if (location >= 0) gl::Uniform1i(location, lampCount);
    if (lampCount > 0 && gl::Uniform4fv) {
        location = gl::GetUniformLocation(g_godrayScatterProgram, "nwnLampPos[0]");
        if (location >= 0) gl::Uniform4fv(location, lampCount, lampPos);
        location = gl::GetUniformLocation(g_godrayScatterProgram, "nwnLampColor[0]");
        if (location >= 0) gl::Uniform4fv(location, lampCount, lampColor);
    }
    // "Lamp glow" 0..3 -> in-scattering gain (default 0.2). Slot alpha
    // fades source radiance before the shared LDR shoulder.
    location = gl::GetUniformLocation(g_godrayScatterProgram, "nwnLampGain");
    if (location >= 0) gl::Uniform1f(location, 8.0f * std::max(0.0f, fogLampGlow));
    location = gl::GetUniformLocation(g_godrayScatterProgram, "nwnLampFalloff");
    if (location >= 0) gl::Uniform2f(location,
        (std::isfinite(g_lightMaxIntensityInv) && g_lightMaxIntensityInv > 1.0e-4f)
            ? g_lightMaxIntensityInv : 0.1f,
        (std::isfinite(g_lightFalloffFactor) && g_lightFalloffFactor >= 0.0f)
            ? g_lightFalloffFactor : 1.0f);
    g_godraysLampsDrawn = lampCount;
    location = gl::GetUniformLocation(g_godrayScatterProgram, "nwnShaft");
    if (location >= 0) gl::Uniform4f(location, fogShafts,
                                     fogAnisotropy, policyFade,
                                     shaftsOn ? 1.0f : 0.0f);
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
        // Pass 3: one separable depth-aware Gaussian. It exists to remove the
        // per-pixel jitter pattern, not to reshape the volume: the result is
        // composited as-is. (The previous pass subtracted a second, broader
        // blur from this one and kept only the positive difference. That
        // high-pass deleted the fog body and every shaft wider than the
        // kernel, and left dark halos beside the narrow ones.)
        useProgram(g_godrayBlurProgram);
        location = gl::GetUniformLocation(g_godrayBlurProgram, "nwnVolume");
        if (location >= 0) gl::Uniform1i(location, 28);
        location = gl::GetUniformLocation(g_godrayBlurProgram, "nwnLinearDepth");
        if (location >= 0) gl::Uniform1i(location, 29);
        location = gl::GetUniformLocation(g_godrayBlurProgram, "nwnTargetSize");
        if (location >= 0) gl::Uniform2f(location, (float)targetW, (float)targetH);
        const GLint axisLocation =
            gl::GetUniformLocation(g_godrayBlurProgram, "nwnAxis");
        const GLint sigmaLocation =
            gl::GetUniformLocation(g_godrayBlurProgram, "nwnSigma");
        if (sigmaLocation >= 0) gl::Uniform1f(sigmaLocation, 2.5f);
        gl::ActiveTexture(GL_TEXTURE0 + 29);
        gl::BindTexture(GL_TEXTURE_2D, g_godrayMaskTexture);
        gl::ActiveTexture(GL_TEXTURE0 + 28);

        gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                 g_godrayFilteredTexture, 0);
        gl::BindTexture(GL_TEXTURE_2D, g_godrayScatterTexture);
        if (axisLocation >= 0) gl::Uniform2f(axisLocation, 1.0f, 0.0f);
        godrays_draw_fullscreen();

        gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                 g_godrayScatterTexture, 0);
        gl::BindTexture(GL_TEXTURE_2D, g_godrayFilteredTexture);
        if (axisLocation >= 0) gl::Uniform2f(axisLocation, 0.0f, 1.0f);
        godrays_draw_fullscreen();
    }

    // Pass 4: depth-aware upsample into the scene.
    gl::BindFramebuffer(GL_FRAMEBUFFER, (GLuint)state.framebuffer);
    gl::Viewport(state.viewport[0], state.viewport[1], state.viewport[2], state.viewport[3]);
    gl::ActiveTexture(GL_TEXTURE0 + 30);
    gl::BindTexture(GL_TEXTURE_2D, g_sceneDepthTex);
    gl::ActiveTexture(GL_TEXTURE0 + 29);
    gl::BindTexture(GL_TEXTURE_2D, g_godrayMaskTexture);
    gl::ActiveTexture(GL_TEXTURE0 + 28);
    gl::BindTexture(GL_TEXTURE_2D, g_godrayScatterTexture);
    useProgram(g_godrayCompositeProgram);
    location = gl::GetUniformLocation(g_godrayCompositeProgram, "nwnVolume");
    if (location >= 0) gl::Uniform1i(location, 28);
    location = gl::GetUniformLocation(g_godrayCompositeProgram, "nwnLinearDepth");
    if (location >= 0) gl::Uniform1i(location, 29);
    location = gl::GetUniformLocation(g_godrayCompositeProgram, "nwnSceneDepth");
    if (location >= 0) gl::Uniform1i(location, 30);
    location = gl::GetUniformLocation(g_godrayCompositeProgram, "nwnDebug");
    if (location >= 0) gl::Uniform1i(location, g_godraysDebug);
    location = gl::GetUniformLocation(g_godrayCompositeProgram, "nwnScreenSize");
    if (location >= 0) gl::Uniform2f(location, (float)state.viewport[2], (float)state.viewport[3]);
    location = gl::GetUniformLocation(g_godrayCompositeProgram, "nwnScreenOrigin");
    if (location >= 0) gl::Uniform2f(location, (float)state.viewport[0], (float)state.viewport[1]);
    location = gl::GetUniformLocation(g_godrayCompositeProgram, "nwnLowSize");
    if (location >= 0) gl::Uniform2f(location, (float)targetW, (float)targetH);
    location = gl::GetUniformLocation(g_godrayCompositeProgram, "nwnCameraVPInv");
    if (location >= 0) gl::UniformMatrix4fv(location, 1, GL_FALSE,
                                             g_shadowFrameContext.viewProjectionInverse);
    location = gl::GetUniformLocation(g_godrayCompositeProgram, "nwnEye");
    if (location >= 0) gl::Uniform4f(location, g_shadowFrameContext.eye.x,
                                     g_shadowFrameContext.eye.y,
                                     g_shadowFrameContext.eye.z, 1.0f);
    gl::Enable(GL_BLEND);
    gl::BlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    // NWN supplies an already-lit, already authored-fogged LDR scene. Scatter
    // into its remaining channel headroom, rather than adding a second exposure:
    //   result = in-scattering * (1 - scene) + scene * transmittance.
    // With scatter/scene/T in [0,1], this stays in [0,1] without hard clipping.
    // The transmittance still attenuates only the injector's own medium. Debug modes
    // replace RGB so their channels cannot saturate against the underlying
    // scene. Framebuffer alpha belongs to NWN in both cases.
    if (g_godraysDebug == 0)
        gl::BlendFuncSeparate(GL_ONE_MINUS_DST_COLOR, GL_SRC_ALPHA, GL_ZERO, GL_ONE);
    else
        gl::BlendFuncSeparate(GL_ONE, GL_ZERO, GL_ZERO, GL_ONE);
    gl::ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
    godrays_draw_fullscreen();

    // Only pixels rejected by the low-resolution composite reach this march.
    // Reuse precisely the same medium/light uniforms and shadow maps as pass2.
    if(g_godraysDebug==0 && divisor>1){
        useProgram(g_godrayScatterProgram);
        if(refineLocation>=0)gl::Uniform1i(refineLocation,1);
        location=gl::GetUniformLocation(g_godrayScatterProgram,"nwnTargetSize");
        if(location>=0)gl::Uniform2f(location,(float)state.viewport[2],(float)state.viewport[3]);
        location=gl::GetUniformLocation(g_godrayScatterProgram,"nwnRefineOrigin");
        if(location>=0)gl::Uniform2f(location,(float)state.viewport[0],(float)state.viewport[1]);
        gl::ActiveTexture(GL_TEXTURE0+28);gl::BindTexture(GL_TEXTURE_2D,g_godrayMaskTexture);
        godrays_draw_fullscreen();
    }

    if (lampCount == 0) g_godraysLampsDrawn = 0;
    if (g_godraysDebug != 0) g_godraysStatus = "drawing (debug view)";
    else if (shaftsOn) g_godraysStatus = "drawing fog and sun shafts";
    else if (shaftsOffReason && std::strcmp(shaftsOffReason, "area has no sun/moon shadows") == 0)
        g_godraysStatus = "drawing fog only (area has no sun/moon shadows)";
    else if (shaftsOffReason && std::strcmp(shaftsOffReason, "no sun/moon direction") == 0)
        g_godraysStatus = "drawing fog only (no sun/moon direction)";
    else g_godraysStatus = "drawing fog only (sun shadow maps unavailable)";
    // Log transitions including the return to normal mode.  This is bounded
    // and leaves mode 0 diagnosable without turning on a full debug view.
    static int lastMode = -1;
    if (lastMode != g_godraysDebug) {
        lastMode = g_godraysDebug;
        fprintf(stderr, "[shadowmap][godrays] draw mode=%d target=%dx%d projectedSun=(%.3f,%.3f) strength=%.2f g=%.2f density=%.2f falloff=%.3f base=%.1f ambient=%.2f fade=%.2f march=%.1f fog=%s %.1f..%.1f color=%s\n",
                g_godraysDebug, targetW, targetH,
                projectedSunU, projectedSunV, fogShafts,
                fogAnisotropy, atmosphereExtinction / kDensityScale,
                fogHeightFalloff, fogHeightBase, fogBrightness,
                policyFade, marchFar,
                authoredFog ? "area" : "clear", g_fogStartLive, g_fogEndLive,
                g_haveEngineFogColor ? "area" : "sun-derived");
    }
}
