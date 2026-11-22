// ---------------------------------------------------------------------------
// Heat haze: screen-space refraction of the finished scene.
//
// Hot air bends light, so what lies BEHIND it wobbles. The pass copies the
// completed frame, then redraws only the pixels seen through hot air with
// their lookup shifted by a slowly rising noise field. How much a pixel
// shimmers comes from the world, not the screen:
//   * distance  -- shimmer builds up along long sight lines over hot ground,
//                  starting at "Start distance";
//   * height    -- it is strongest in the hot layer near the ground and fades
//                  above "Base height" at "Height falloff" per metre;
//   * fires     -- warm-coloured local lights (torches, lava, braziers) from
//                  the engine's light census add a column of shimmer above
//                  themselves, visible only against what lies behind them.
// A shifted lookup that would pull a nearer surface into a farther one is
// refused, so foreground silhouettes do not smear into the haze behind them.
// Pixels with no shimmer are left untouched (discarded), which preserves the
// engine's multisampled edges everywhere else.
//
// Runs after the volumetric fog so the fog shimmers too. Needs this frame's
// scene-depth copy, which the sun-shadow receiver makes.
// ---------------------------------------------------------------------------
static GLuint g_heatProgram = 0;
static GLuint g_heatColorTexture = 0;
static GLuint g_heatFbo = 0;
static int g_heatWidth = 0, g_heatHeight = 0;

static void build_heat_program() {
    if (g_heatProgram) return;
    static bool attempted = false;
    if (attempted) return;
    attempted = true;
    static const char* heatFragment =
        "#version 330 core\n"
        "out vec4 nwnFragColor;uniform sampler2D nwnSceneColor,nwnSceneDepth;"
        "uniform vec2 nwnScreenSize,nwnScreenOrigin;"
        "uniform mat4 nwnCameraVPInv;uniform vec4 nwnEye;"
        // x strength, y noise scale, z animation phase, w start distance
        "uniform vec4 nwnHeat;"
        // x base height, y height falloff, z fire strength
        "uniform vec4 nwnHeatHeight;"
        "uniform vec4 nwnFirePos[8];uniform int nwnFireCount;"
        "float nwnHash(vec2 p){vec3 q=fract(vec3(p.xyx)*.1031);q+=dot(q,q.yzx+33.33);"
        "return fract((q.x+q.y)*q.z);}"
        "float nwnNoise(vec2 p){vec2 i=floor(p),f=fract(p);vec2 u=f*f*(3.0-2.0*f);"
        "return mix(mix(nwnHash(i),nwnHash(i+vec2(1,0)),u.x),"
        "mix(nwnHash(i+vec2(0,1)),nwnHash(i+vec2(1,1)),u.x),u.y);}"
        "vec2 nwnField(vec2 p){return vec2(nwnNoise(p),nwnNoise(p+vec2(5.2,1.3)))-.5;}"
        "vec3 nwnWorld(vec2 uv,float d){vec4 h=nwnCameraVPInv*vec4(uv*2.0-1.0,d*2.0-1.0,1.0);"
        "return h.xyz/h.w;}"
        "void main(){vec2 uv=(gl_FragCoord.xy-nwnScreenOrigin)/nwnScreenSize;"
        "float d=texture(nwnSceneDepth,uv).r;bool sky=d>=.9999995;"
        // Sky pixels use a point 200 m out along their ray, so the horizon
        // behind distant terrain shimmers like the terrain does.
        "vec3 ray=normalize(nwnWorld(uv,.5)-nwnEye.xyz);"
        "float dist=sky?200.0:length(nwnWorld(uv,d)-nwnEye.xyz);"
        "vec3 p=nwnEye.xyz+ray*dist;"
        // The hottest air the sight line crosses is near its lowest point.
        "float lowZ=min(p.z,nwnEye.z);"
        "float amount=smoothstep(nwnHeat.w,nwnHeat.w*2.0+10.0,dist)*"
        "exp(-nwnHeatHeight.y*max(lowZ-nwnHeatHeight.x,0.0));"
        // Fire columns: the ray must pass close to the hot air above a fire
        // AND see something beyond it.
        "for(int i=0;i<8;i++){if(i>=nwnFireCount)break;"
        "vec3 c=nwnFirePos[i].xyz+vec3(0,0,1.0);float r=nwnFirePos[i].w;"
        "float t=dot(c-nwnEye.xyz,ray);if(t<=0.0||t>=dist)continue;"
        "float m=length(nwnEye.xyz+ray*t-c);"
        "amount+=nwnHeatHeight.z*(1.0-smoothstep(0.0,r,m));}"
        "amount=clamp(amount,0.0,1.5);"
        "if(amount*nwnHeat.x<.01)discard;"
        // Two octaves of noise scrolling UP the screen (heat rises), in
        // aspect-correct screen units so cells stay round.
        "vec2 np=uv*vec2(nwnScreenSize.x/nwnScreenSize.y,1.0)*(18.0*nwnHeat.y);"
        "vec2 off=nwnField(np+vec2(0.0,-nwnHeat.z))*.65+"
        "nwnField(np*2.3+vec2(3.1,-nwnHeat.z*1.7))*.35;"
        // Strength 1 shifts up to ~4 pixels at 1080p.
        "vec2 shift=off*amount*nwnHeat.x*.008*vec2(nwnScreenSize.y/nwnScreenSize.x,1.0);"
        "vec2 lim=.5/nwnScreenSize;vec2 suv=clamp(uv+shift,lim,1.0-lim);"
        "float sd=texture(nwnSceneDepth,suv).r;"
        "float sdist=sd>=.9999995?200.0:length(nwnWorld(suv,sd)-nwnEye.xyz);"
        "if(sdist<dist*.97-.2)discard;"
        "nwnFragColor=vec4(texture(nwnSceneColor,suv).rgb,1.0);}\n";
    g_heatProgram = godrays_make_program(heatFragment, "heat-haze");
    if (g_heatProgram)
        fprintf(stderr, "[shadowmap][heat] heat haze program compiled\n");
}

static bool ensure_heat_targets(int width, int height) {
    if (width <= 0 || height <= 0) return false;
    if (!g_heatColorTexture) gl::GenTextures(1, &g_heatColorTexture);
    if (!g_heatFbo) gl::GenFramebuffers(1, &g_heatFbo);
    if (!g_heatColorTexture || !g_heatFbo) return false;
    if (width != g_heatWidth || height != g_heatHeight) {
        gl::BindTexture(GL_TEXTURE_2D, g_heatColorTexture);
        gl::TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                       GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        g_heatWidth = width;
        g_heatHeight = height;
    }
    gl::BindFramebuffer(GL_FRAMEBUFFER, g_heatFbo);
    gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                             g_heatColorTexture, 0);
    return gl::CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
}

// GodrayGlState restores the draw framebuffer through GL_FRAMEBUFFER, which
// also rebinds READ. This restores the engine's read binding afterwards; it
// is declared first so it is destroyed last.
struct HeatReadFramebuffer {
    GLint read = 0;
    GLboolean srgb = GL_FALSE;
    HeatReadFramebuffer() {
        gl::GetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
        srgb = gl::IsEnabled(0x8DB9 /* GL_FRAMEBUFFER_SRGB */);
    }
    ~HeatReadFramebuffer() {
        gl::BindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)read);
        if (srgb) gl::Enable(0x8DB9); else gl::Disable(0x8DB9);
    }
};

static void draw_heat_haze(void* scene) {
    if (!script_env::b(script_env::HeatOn, g_heatEnabled)) {
        if (scene == g_areaScene) g_heatStatus = "off";
        return;
    }
    if (scene != g_areaScene) return;
    if (!g_shadowFrameContext.valid || g_shadowFrameContext.scene != scene) {
        g_heatStatus = "no current area camera"; return;
    }
    if (!gl::GetIntegerv || !gl::GetBooleanv || !gl::IsEnabled || !gl::Enable ||
        !gl::Disable || !gl::BlendFuncSeparate || !gl::BlendEquationSeparate ||
        !gl::Viewport || !gl::ActiveTexture || !gl::BindTexture || !gl::GenTextures ||
        !gl::TexImage2D || !gl::TexParameteri || !gl::GenFramebuffers ||
        !gl::BindFramebuffer || !gl::FramebufferTexture2D ||
        !gl::CheckFramebufferStatus || !gl::BlitFramebuffer || !gl::DepthMask ||
        !gl::ColorMask || !gl::GetUniformLocation || !gl::Uniform1i ||
        !gl::Uniform1f || !gl::Uniform2f || !gl::Uniform4f || !gl::UniformMatrix4fv) {
        g_heatStatus = "missing GL entry points"; return;
    }
    UseProgram_t useProgram = g_realUseProgram ? g_realUseProgram :
        (UseProgram_t)glew_or_dlsym(eng::GlewUseProgram, "glUseProgram");
    if (!useProgram) { g_heatStatus = "no glUseProgram"; return; }
    GLint viewport[4] = {0, 0, 0, 0}, entryFbo = 0;
    gl::GetIntegerv(GL_VIEWPORT, viewport);
    gl::GetIntegerv(GL_FRAMEBUFFER_BINDING, &entryFbo);
    if (entryFbo != 0 || viewport[2] <= 0 || viewport[3] <= 0) {
        g_heatStatus = "not drawing to the screen"; return;
    }
    if (!g_sceneDepthTex || g_sceneDepthSerial != g_shadowFrameContext.serial ||
        g_sceneDepthW != viewport[2] || g_sceneDepthH != viewport[3]) {
        g_heatStatus = "no scene depth this frame (are sun shadows enabled?)"; return;
    }

    nwn_core::OwnedPass ownedPass;
    HeatReadFramebuffer readRestore;
    GodrayGlState state(useProgram);

    build_heat_program();
    if (!g_heatProgram) { g_heatStatus = "shader build failed"; return; }
    if (!ensure_heat_targets(viewport[2], viewport[3])) {
        g_heatStatus = "target allocation failed"; return;
    }

    // Copy (and, with MSAA, resolve) the finished frame.
    gl::Disable(GL_SCISSOR_TEST);
    gl::Disable(0x8DB9 /* GL_FRAMEBUFFER_SRGB: copy and write back raw */);
    gl::BindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    gl::BindFramebuffer(GL_DRAW_FRAMEBUFFER, g_heatFbo);
    if (gl::GetError) while (gl::GetError() != GL_NO_ERROR) {}
    gl::BlitFramebuffer(viewport[0], viewport[1], viewport[0] + viewport[2],
                        viewport[1] + viewport[3], 0, 0, viewport[2], viewport[3],
                        GL_COLOR_BUFFER_BIT, GL_NEAREST);
    if (gl::GetError && gl::GetError() != GL_NO_ERROR) {
        g_heatStatus = "screen copy failed"; return;
    }

    // Warm local lights become fire columns.
    float firePos[8 * 4] = {};
    int fireCount = 0;
    const float fire = script_env::f(script_env::HeatFire, g_heatFire);
    if (fire > 0.0f) {
        struct Pick { float distance; unsigned index; };
        Pick picks[kLampPool];
        unsigned candidates = 0;
        const unsigned count = std::min<unsigned>(g_lampLightCount, kLampPool);
        for (unsigned i = 0; i < count; ++i) {
            const LampEntry& lamp = g_lampLights[i];
            if (!(lamp.radius >= 0.25f && lamp.radius <= 40.0f)) continue;
            const float r = lamp.color[0], g = lamp.color[1], b = lamp.color[2];
            if (!(r >= 0.35f && r >= g && g >= b * 1.1f)) continue;   // warm only
            if (!std::isfinite(lamp.pos[0]) || !std::isfinite(lamp.pos[1]) ||
                !std::isfinite(lamp.pos[2])) continue;
            const float dx = lamp.pos[0] - g_shadowFrameContext.eye.x;
            const float dy = lamp.pos[1] - g_shadowFrameContext.eye.y;
            const float dz = lamp.pos[2] - g_shadowFrameContext.eye.z;
            const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (distance > 60.0f) continue;
            picks[candidates++] = {distance, i};
        }
        const unsigned keep = std::min(candidates, 8u);
        std::partial_sort(picks, picks + keep, picks + candidates,
            [](const Pick& a, const Pick& b) { return a.distance < b.distance; });
        for (unsigned k = 0; k < keep; ++k) {
            const LampEntry& lamp = g_lampLights[picks[k].index];
            firePos[k * 4 + 0] = lamp.pos[0];
            firePos[k * 4 + 1] = lamp.pos[1];
            firePos[k * 4 + 2] = lamp.pos[2];
            // Column radius follows the light's size but stays plausible.
            firePos[k * 4 + 3] = std::max(0.6f, std::min(2.0f, lamp.radius * 0.25f));
        }
        fireCount = (int)keep;
    }

    // Redraw the shimmering pixels into the screen.
    gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl::Viewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    gl::Disable(GL_DEPTH_TEST); gl::Disable(GL_BLEND);
    gl::DepthMask(GL_FALSE);
    gl::ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
    gl::ActiveTexture(GL_TEXTURE0 + 29);
    gl::BindTexture(GL_TEXTURE_2D, g_sceneDepthTex);
    gl::ActiveTexture(GL_TEXTURE0 + 28);
    gl::BindTexture(GL_TEXTURE_2D, g_heatColorTexture);
    useProgram(g_heatProgram);
    auto loc = [](const char* name) { return gl::GetUniformLocation(g_heatProgram, name); };
    GLint location = loc("nwnSceneColor");
    if (location >= 0) gl::Uniform1i(location, 28);
    location = loc("nwnSceneDepth");
    if (location >= 0) gl::Uniform1i(location, 29);
    location = loc("nwnScreenSize");
    if (location >= 0) gl::Uniform2f(location, (float)viewport[2], (float)viewport[3]);
    location = loc("nwnScreenOrigin");
    if (location >= 0) gl::Uniform2f(location, (float)viewport[0], (float)viewport[1]);
    location = loc("nwnCameraVPInv");
    if (location >= 0) gl::UniformMatrix4fv(location, 1, GL_FALSE,
                                             g_shadowFrameContext.viewProjectionInverse);
    location = loc("nwnEye");
    if (location >= 0) gl::Uniform4f(location, g_shadowFrameContext.eye.x,
                                     g_shadowFrameContext.eye.y,
                                     g_shadowFrameContext.eye.z, 1.0f);
    const float speed = script_env::f(script_env::HeatSpeed, g_heatSpeed);
    // Phase wraps so float precision never degrades over a long session.
    const float phase = (float)std::fmod(now_seconds() * 1.6 * speed, 4096.0);
    location = loc("nwnHeat");
    if (location >= 0) gl::Uniform4f(location,
        script_env::f(script_env::HeatStrength, g_heatStrength),
        script_env::f(script_env::HeatScale, g_heatScale), phase,
        script_env::f(script_env::HeatStart, g_heatStart));
    location = loc("nwnHeatHeight");
    if (location >= 0) gl::Uniform4f(location,
        script_env::f(script_env::HeatBase, g_heatBase),
        script_env::f(script_env::HeatFalloff, g_heatFalloff), fire, 0.0f);
    location = loc("nwnFireCount");
    if (location >= 0) gl::Uniform1i(location, fireCount);
    if (fireCount > 0 && gl::Uniform4fv) {
        location = loc("nwnFirePos[0]");
        if (location >= 0) gl::Uniform4fv(location, fireCount, firePos);
    }
    godrays_draw_fullscreen();
    g_heatFiresDrawn = fireCount;
    g_heatStatus = "drawing";
}
