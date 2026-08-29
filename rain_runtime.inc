// ===========================================================================
//  Injector-owned rain runtime -- Linux reference implementation
//
//  This module owns weather discovery, conservative receiver classification,
//  and the stock-material wet-surface branch. The effect is injected into the
//  shaders already used by NWN, but remains inert unless the draw funnel marks
//  the current draw as static opaque or static alpha area scenery.
//
//  Windows remains a no-op until its separately requested port. Keep this
//  interface platform-neutral so that port only supplies bindings proven to
//  differ from the accepted Linux behaviour.
// ===========================================================================

#ifndef _WIN32

namespace rain_runtime {

static constexpr int kWeatherClear = 0;
static constexpr int kWeatherRain  = 1;
static constexpr int kWeatherSnow  = 2;
static constexpr unsigned kProgramCapacity = 256;
// NWN reserves these units for the same framebuffer snapshots consumed by
// fswater.shd. They are engine-owned; rain only samples them and never changes
// their bindings.
static constexpr int kFramebufferColorUnit = 11;
static constexpr int kFramebufferDepthUnit = 12;
static constexpr int kWaterNoiseUnit = 15;

struct ProgramState {
    GLuint program = 0;
    GLint skinmesh = -1;
    GLint waterTime = -1;
    GLint waterWind = -1;
    GLint waterNoise = -1;
    GLint rainWetness = -1;
    GLint rainEligible = -1;
    GLint rainTime = -1;
    GLint rainFbColor = -1;
    GLint rainFbDepth = -1;
    GLint rainReflectionReady = -1;
    GLint rainNoiseTex = -1;
    GLint rainNoiseReady = -1;
    GLint skinmeshValue = 0;
    bool skinmeshKnown = false;
};

struct RuntimeState {
    ProgramState programs[kProgramCapacity] = {};
    unsigned programCount = 0;
    unsigned frameSerial = 0;
    unsigned sceneDraws = 0;
    unsigned eligibleDraws = 0;
    unsigned skinnedDraws = 0;
    unsigned waterDraws = 0;
    unsigned nonSceneDraws = 0;
    unsigned unclassifiedDraws = 0;
    int weatherType = kWeatherClear;
    float weatherDensity = 0.0f;
    float wetness = 0.0f;
    void* weatherArea = nullptr;
    bool weatherObserved = false;
    bool raining = false;
    bool reflectionReady = false;
    bool noiseReady = false;
    unsigned reflectionProbeFrame = ~0u;
    double lastTick = 0.0;
    double nextStatus = 0.0;
};

static RuntimeState g;

using Uniform1i_t = void (*)(GLint, GLint);
static Uniform1i_t g_realUniform1i = nullptr;
static subhook_t g_weatherHook = nullptr;
static eng::AreaSetWeather_t g_weatherTrampoline = nullptr;

static bool area_draw_active() {
    const bool traceArea = g_traceEnabled && g_traceFrameActive &&
                           g_traceCurrentScene &&
                           g_traceCurrentScene == g_areaScene;
    const bool legacyArea = g_inSceneRender && g_renderingScene &&
                            g_renderingScene == g_areaScene;
    return traceArea || legacyArea;
}

static ProgramState* program_state(GLuint program) {
    if (!program || !gl::GetUniformLocation) return nullptr;
    for (unsigned i = 0; i < g.programCount; ++i)
        if (g.programs[i].program == program) return &g.programs[i];
    if (g.programCount >= kProgramCapacity) return nullptr;

    ProgramState& state = g.programs[g.programCount++];
    state.program = program;
    state.skinmesh = gl::GetUniformLocation(program, "skinmesh");
    // This combination is specific to NWN's stock water path. Water gets its
    // own ripple branch and must not be treated as an ordinary puddle receiver.
    state.waterTime = gl::GetUniformLocation(program, "worldtimerTimeOfDay");
    state.waterWind = gl::GetUniformLocation(program, "areaGlobalWind");
    state.waterNoise = gl::GetUniformLocation(program, "texUnitNoise");
    state.rainWetness = gl::GetUniformLocation(program, "nwnRainWetness");
    state.rainEligible = gl::GetUniformLocation(program, "nwnRainEligible");
    state.rainTime = gl::GetUniformLocation(program, "nwnRainTime");
    state.rainFbColor = gl::GetUniformLocation(program, "nwnRainFbColor");
    state.rainFbDepth = gl::GetUniformLocation(program, "nwnRainFbDepth");
    state.rainReflectionReady =
        gl::GetUniformLocation(program, "nwnRainReflectionReady");
    state.rainNoiseTex = gl::GetUniformLocation(program, "nwnRainNoiseTex");
    state.rainNoiseReady = gl::GetUniformLocation(program, "nwnRainNoiseReady");
    return &state;
}

static void probe_framebuffer_reflection_inputs() {
    if (g.reflectionProbeFrame == g.frameSerial || !gl::ActiveTexture ||
        !gl::GetIntegerv) return;
    g.reflectionProbeFrame = g.frameSerial;
    GLint oldActive = GL_TEXTURE0, color = 0, depth = 0, noise = 0;
    gl::GetIntegerv(GL_ACTIVE_TEXTURE, &oldActive);
    gl::ActiveTexture(GL_TEXTURE0 + kFramebufferColorUnit);
    gl::GetIntegerv(GL_TEXTURE_BINDING_2D, &color);
    gl::ActiveTexture(GL_TEXTURE0 + kFramebufferDepthUnit);
    gl::GetIntegerv(GL_TEXTURE_BINDING_2D, &depth);
    gl::ActiveTexture(GL_TEXTURE0 + kWaterNoiseUnit);
    gl::GetIntegerv(GL_TEXTURE_BINDING_2D, &noise);
    gl::ActiveTexture((GLenum)oldActive);
    const bool ready = color != 0 && depth != 0;
    const bool noiseReady = noise != 0;
    static bool reported = false;
    static bool lastReady = false;
    static bool lastNoiseReady = false;
    if (!reported || ready != lastReady || noiseReady != lastNoiseReady) {
        reported = true;
        lastReady = ready;
        lastNoiseReady = noiseReady;
        fprintf(stderr,
                "[rain][linux] water inputs reflection=%s noise=%s "
                "color=%d depth=%d noise=%d\n",
                ready ? "ready" : "unavailable",
                noiseReady ? "ready" : "unavailable", color, depth, noise);
    }
    g.reflectionReady = ready;
    g.noiseReady = noiseReady;
}

static void push_draw_state(ProgramState* state, float receiverClass) {
    if (!state || state->rainEligible < 0 || !gl::Uniform1f) return;
    // The current engine program is already bound at the draw funnel. Always
    // upload eligibility, including zero, because NWN reuses a material
    // program across static meshes, creatures, water and auxiliary passes.
    // 0 = excluded, 1 = opaque static, 2 = static alpha/card. The shader uses
    // class 2 for wet noise but explicitly forbids standing puddles there.
    gl::Uniform1f(state->rainEligible, receiverClass);
    if (state->rainWetness >= 0)
        gl::Uniform1f(state->rainWetness, g.wetness);
    if (state->rainTime >= 0) {
        const float t = (float)std::fmod(now_seconds(), 4096.0);
        gl::Uniform1f(state->rainTime, t);
    }
    if (receiverClass > 0.5f) probe_framebuffer_reflection_inputs();
    if (state->rainReflectionReady >= 0)
        gl::Uniform1f(state->rainReflectionReady,
                      receiverClass > 0.5f && g.reflectionReady ? 1.0f : 0.0f);
    if (state->rainNoiseReady >= 0)
        gl::Uniform1f(state->rainNoiseReady,
                      receiverClass > 0.5f && g.noiseReady ? 1.0f : 0.0f);
    if (gl::Uniform1i) {
        if (state->rainFbColor >= 0)
            gl::Uniform1i(state->rainFbColor, kFramebufferColorUnit);
        if (state->rainFbDepth >= 0)
            gl::Uniform1i(state->rainFbDepth, kFramebufferDepthUnit);
        if (state->rainNoiseTex >= 0)
            gl::Uniform1i(state->rainNoiseTex, kWaterNoiseUnit);
    }
}

static void observe_engine_weather(void* area, int weather, float densityPercent) {
    if (!area || weather < kWeatherClear || weather > kWeatherSnow ||
        !std::isfinite(densityPercent)) return;

    // Wetness belongs to a CNWCArea instance. An area transition or save load
    // must not inherit the previous instance's drying tail; only a weather
    // change within the same area is allowed to fade naturally.
    const bool areaChanged = g.weatherArea && g.weatherArea != area;
    if (areaChanged) {
        fprintf(stderr,
                "[rain][linux] area changed %p -> %p; resetting inherited wetness %.3f\n",
                g.weatherArea, area, g.wetness);
        g.weatherObserved = false;
        g.weatherType = kWeatherClear;
        g.weatherDensity = 0.0f;
        g.raining = false;
        g.wetness = 0.0f;
        g.lastTick = now_seconds();
        g.nextStatus = 0.0;
    }
    g.weatherArea = area;

    const float density = std::max(0.0f, std::min(1.0f, densityPercent * 0.01f));
    const bool first = !g.weatherObserved;
    const bool changed = first || weather != g.weatherType ||
                         std::fabs(density - g.weatherDensity) > 0.01f;
    g.weatherObserved = true;
    g.weatherType = weather;
    g.weatherDensity = density;
    g.raining = weather == kWeatherRain && g.weatherDensity > 0.001f;
    if (changed) {
        const char* name = weather == kWeatherRain ? "rain" :
                           weather == kWeatherSnow ? "snow" : "clear";
        fprintf(stderr,
                "[rain][linux] engine weather=%s type=%d density=%.3f area=%p\n",
                name, weather, g.weatherDensity, area);
    }
}

static void note_program(GLuint program) {
    if (!area_draw_active()) return;
    (void)program_state(program);
}

static void observe_uniform1i(GLint location, GLint value) {
    if (!area_draw_active() || !g_curProgram) return;
    ProgramState* state = program_state(g_curProgram);
    if (!state || state->skinmesh < 0 || location != state->skinmesh) return;
    state->skinmeshValue = value;
    state->skinmeshKnown = true;
}

static void observe_draw(GLuint program, GLsizei count) {
    if (!area_draw_active() || !program || count <= 0 || g_inOurPass ||
        g_cascadeReplayActive || g_localLightPassActive ||
        g_overlayPassActive || nwn_core::g_ownedPass)
        return;

    ProgramState* state = program_state(program);
    if (!state) return;
    ++g.sceneDraws;

    if (state->skinmesh >= 0 && !state->skinmeshKnown) {
        push_draw_state(state, false);
        ++g.unclassifiedDraws;
        return;
    }
    if (state->skinmesh >= 0 && state->skinmeshValue != 0) {
        push_draw_state(state, false);
        ++g.skinnedDraws;
        return;
    }

    const bool water = state->waterTime >= 0 && state->waterWind >= 0 &&
                       state->waterNoise >= 0;
    if (water) {
        push_draw_state(state, false);
        ++g.waterDraws;
        return;
    }

    // Bucket 0 is validated static opaque geometry. Bucket 1 is validated
    // static alpha/card geometry and receives wet noise without puddles.
    // Buckets 2/3 are dynamic body/hair geometry and remain excluded, keeping
    // players out independently of a particular model or material variant.
    const int bucket = nwn_core::g_currentBucket;
    if (bucket != 0 && bucket != 1) {
        push_draw_state(state, false);
        if (bucket >= 0 && bucket < kBucketCount) ++g.nonSceneDraws;
        else                                     ++g.unclassifiedDraws;
        return;
    }
    if (state->rainEligible < 0) {
        ++g.unclassifiedDraws;
        return;
    }
    push_draw_state(state, bucket == 1 ? 2.0f : 1.0f);
    ++g.eligibleDraws;
}

static void scene_begin(void* scene) {
    if (!scene || scene != g_areaScene) return;
    g.frameSerial = g_shadowFrameContext.serial;
    g.sceneDraws = g.eligibleDraws = g.skinnedDraws = 0;
    g.waterDraws = g.nonSceneDraws = g.unclassifiedDraws = 0;
}

static void scene_end(void* scene) {
    if (!scene || scene != g_areaScene) return;
    const double now = now_seconds();
    if (g.lastTick <= 0.0) g.lastTick = now;
    const float dt = (float)std::max(0.0, std::min(0.25, now - g.lastTick));
    g.lastTick = now;

    // CNWCArea::SetWeather publishes persistent area state, not a frame pulse.
    // Keep it until the engine sends the next clear/rain/snow decision.
    const float target = g.raining ? g.weatherDensity : 0.0f;
    const float rate = target > g.wetness ? 0.55f : 0.12f;
    const float step = rate * dt;
    if (g.wetness < target) g.wetness = std::min(target, g.wetness + step);
    else                    g.wetness = std::max(target, g.wetness - step);

    if (g.weatherObserved && now >= g.nextStatus) {
        g.nextStatus = now + 30.0;
        fprintf(stderr,
                "[rain][linux] frame=%u raining=%d wetness=%.3f draws=%u "
                "eligible=%u skinned-excluded=%u non-scene=%u water=%u "
                "unclassified=%u\n",
                g.frameSerial, (int)g.raining, g.wetness, g.sceneDraws,
                g.eligibleDraws, g.skinnedDraws, g.nonSceneDraws, g.waterDraws,
                g.unclassifiedDraws);
    }
}

static bool patch_fragment_shader(std::string& source) {
    // Stock world materials share this final output shape. Requiring all
    // inputs makes post-processing, UI, particles and unusual custom shaders
    // fail closed instead of receiving a guessed transform.
    if (source.find("#define SHADER_TYPE 2") == std::string::npos ||
        source.find("vec3 vFragmentNormal;") == std::string::npos ||
        source.find("varying highp vec3 vPosView;") == std::string::npos ||
        source.find("uniform highp mat4 m_view_inv;") == std::string::npos)
        return false;

    const size_t mainPos = source.rfind("void main");
    if (mainPos == std::string::npos) return false;
    const size_t outputPos = source.find("gl_FragColor = FragmentColor;", mainPos);
    if (outputPos == std::string::npos) return false;

    static const char* kDeclarations =
        "uniform float nwnRainWetness;\n"
        "uniform float nwnRainEligible;\n"
        "uniform float nwnRainTime;\n"
        "uniform sampler2D nwnRainFbColor;\n"
        "uniform sampler2D nwnRainFbDepth;\n"
        "uniform float nwnRainReflectionReady;\n"
        "uniform sampler2D nwnRainNoiseTex;\n"
        "uniform float nwnRainNoiseReady;\n"
        "float nwnRainHash(vec2 p) {\n"
        "    p = fract(p * vec2(123.34, 345.45));\n"
        "    p += dot(p, p + 34.345);\n"
        "    return fract(p.x * p.y);\n"
        "}\n"
        "float nwnRainNoise(vec2 p) {\n"
        "    vec2 i = floor(p);\n"
        "    vec2 f = fract(p);\n"
        "    f = f * f * (3.0 - 2.0 * f);\n"
        "    return mix(mix(nwnRainHash(i), nwnRainHash(i + vec2(1.0, 0.0)), f.x),\n"
        "               mix(nwnRainHash(i + vec2(0.0, 1.0)),\n"
        "                   nwnRainHash(i + vec2(1.0, 1.0)), f.x), f.y);\n"
        "}\n"
        "float nwnRainPuddle(vec2 worldXY, float wetness) {\n"
        // Several compact octaves avoid the original tile-sized connected
        // regions. Low values represent local basins; the threshold grows
        // slowly with wetness so islands expand without joining half the area.
        "    float broad = nwnRainNoise(worldXY * 0.62);\n"
        "    broad = 0.58 * broad + 0.42 * nwnRainNoise(worldXY * 1.31 + 19.7);\n"
        "    float edge = nwnRainNoise(worldXY * 3.45 - 7.4);\n"
        "    float field = broad + (edge - 0.5) * 0.13;\n"
        "    float threshold = mix(0.16, 0.33, wetness);\n"
        "    return 1.0 - smoothstep(threshold - 0.04, threshold + 0.04, field);\n"
        "}\n"
        "float nwnRainDepth(vec2 uv) {\n"
        "    float d = texture2D(nwnRainFbDepth, uv).x;\n"
        "    return nearClip / max(0.00001, 1.0 - d * fDepthToLinearModifier);\n"
        "}\n"
        "vec3 nwnRainViewPosition(vec2 uv) {\n"
        "    float d = texture2D(nwnRainFbDepth, uv).x;\n"
        "    vec4 p = m_proj_inv * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);\n"
        "    return p.xyz / max(0.00001, p.w);\n"
        "}\n"
        "vec3 nwnRainDepthNormal(vec2 uv) {\n"
        "    vec3 p = nwnRainViewPosition(uv);\n"
        "    vec3 px = nwnRainViewPosition(uv + vec2(pixelParams.x, 0.0));\n"
        "    vec3 py = nwnRainViewPosition(uv + vec2(0.0, pixelParams.y));\n"
        "    return normalize(cross(px - p, py - p));\n"
        "}\n"
        // This is the stock water shader's perspective-correct depth walk,
        // namespaced for the injector. Its relative depth error is returned in
        // w and is used exactly as a confidence term instead of accepting a
        // stretched nearest sample.
        "vec4 nwnRainTraceReflection(vec3 origin, vec3 ray) {\n"
        "    const int iterations = 10;\n"
        "    float maxDistance = clamp(abs(farClip + origin.z), 1.0, 30.0);\n"
        "    float angleFactor = mix(1.0, abs(ray.z), 0.9);\n"
        "    maxDistance *= angleFactor;\n"
        "    ray /= max(0.001, angleFactor);\n"
        "    float stepMax = maxDistance / float(iterations);\n"
        "    float minRelativeError = 4096.0;\n"
        "    float hitDepth = maxDistance + origin.z;\n"
        "    vec2 hitUv = vec2(-1.0);\n"
        "    vec3 rayEndProj = mat3(m_proj) * (origin + maxDistance * ray);\n"
        "    vec2 rayEndUv = 0.5 * (rayEndProj.xy / rayEndProj.z) + 0.5;\n"
        "    vec3 originProj = mat3(m_proj) * origin;\n"
        "    vec2 originUv = 0.5 * (originProj.xy / originProj.z) + 0.5;\n"
        "    float denomInv = 1.0 / (1.0 / (origin.z + ray.z * maxDistance)\n"
        "                              - 1.0 / origin.z);\n"
        "    float interpOffset = (1.0 / origin.z) * denomInv;\n"
        "    float depthDelta = 0.035;\n"
        "    float travel = 0.0;\n"
        "    bool crossed = false;\n"
        "    for (int i = 0; i < iterations; ++i) {\n"
        "        travel += clamp(depthDelta, -stepMax, stepMax);\n"
        "        travel = max(0.01, travel);\n"
        "        vec3 offset = travel * ray;\n"
        "        vec2 uv = mix(originUv, rayEndUv,\n"
        "            (1.0 / (origin.z + offset.z)) * denomInv - interpOffset);\n"
        "        if (uv.x <= 0.001 || uv.x >= 0.999 ||\n"
        "            uv.y <= 0.001 || uv.y >= 0.999) break;\n"
        "        float sampleDepth = nwnRainDepth(uv);\n"
        "        depthDelta = sampleDepth + origin.z + offset.z;\n"
        "        float relativeError = depthDelta / (travel + 1.0);\n"
        "        if (abs(relativeError) <= abs(minRelativeError)) {\n"
        "            hitUv = uv; minRelativeError = relativeError;\n"
        "            hitDepth = sampleDepth;\n"
        "        }\n"
        "        if (depthDelta < 0.0) crossed = true;\n"
        "        if (crossed) stepMax *= 0.5;\n"
        "    }\n"
        "    float error = clamp(abs(minRelativeError) / 0.20, 0.0, 1.0);\n"
        "    return vec4(hitUv, -hitDepth, error);\n"
        "}\n"
        // Return scalar ripple height plus its analytic world-space gradient.
        // Reflection/refraction consume the gradient as a normal; no ring is
        // painted directly into the material colour.
        "vec3 nwnRainRippleField(vec2 worldXY, float time, float rippleScale) {\n"
        "    vec2 grid = worldXY * 1.45;\n"
        "    vec2 baseCell = floor(grid);\n"
        "    float height = 0.0;\n"
        "    vec2 gradient = vec2(0.0);\n"
        // Include neighboring cells so a ring remains continuous when its
        // radius crosses a procedural-cell boundary.
        "    for (int y = -1; y <= 1; ++y) {\n"
        "        for (int x = -1; x <= 1; ++x) {\n"
        "            vec2 cell = baseCell + vec2(float(x), float(y));\n"
        "            float seed = nwnRainHash(cell);\n"
        "            vec2 center = cell + vec2(nwnRainHash(cell + 13.1),\n"
        "                                      nwnRainHash(cell + 37.7));\n"
        "            vec2 delta = grid - center;\n"
        "            float safeScale = max(rippleScale, 0.01);\n"
        "            float maxRadius = 0.62 * safeScale;\n"
        "            float radius = fract(time * 0.72 + seed * 5.37) * maxRadius;\n"
        "            float dist = length(delta);\n"
        "            float q = (dist - radius) / safeScale;\n"
        "            float fade = 1.0 - radius / maxRadius;\n"
        "            float ringHeight = exp(-58.0 * q * q) * fade;\n"
        "            height += ringHeight;\n"
        "            if (dist > 0.0001) {\n"
        "                float dHeight = -116.0 * q * ringHeight / safeScale;\n"
        "                gradient += normalize(delta) * dHeight * 1.45;\n"
        "            }\n"
        "        }\n"
        "    }\n"
        "    gradient /= max(1.0, length(gradient) * 0.22);\n"
        "    return vec3(clamp(height, 0.0, 1.0), gradient);\n"
        "}\n"
        "float nwnRainGroundNoiseHeight(vec2 p, float time) {\n"
        "    if (nwnRainNoiseReady > 0.5) {\n"
        "        float a = texture2D(nwnRainNoiseTex,\n"
        "                            p * 0.58 + vec2(time * 0.021, -time * 0.047)).r;\n"
        "        float b = texture2D(nwnRainNoiseTex,\n"
        "                            p * 1.73 + vec2(-time * 0.063, time * 0.029)).r;\n"
        "        return mix(a, b, 0.38);\n"
        "    }\n"
        "    return 0.62 * nwnRainNoise(p * 0.58 + time * 0.021)\n"
        "         + 0.38 * nwnRainNoise(p * 1.73 - time * 0.037);\n"
        "}\n"
        "vec2 nwnRainGroundNoiseSlope(vec2 p, float time) {\n"
        "    const float e = 0.035;\n"
        "    float h = nwnRainGroundNoiseHeight(p, time);\n"
        "    return vec2(nwnRainGroundNoiseHeight(p + vec2(e, 0.0), time) - h,\n"
        "                nwnRainGroundNoiseHeight(p + vec2(0.0, e), time) - h) / e;\n"
        "}\n";
    source.insert(mainPos, kDeclarations);

    const size_t shiftedOutput = outputPos + std::strlen(kDeclarations);
    static const char* kBody =
        "// injector-owned procedural rain puddles\n"
        "#if POSITION_VIEW == 1 && VERTEX_NORMAL == 1\n"
        "if (nwnRainEligible > 0.5 && nwnRainWetness > 0.001) {\n"
        "    vec3 nwnRainWorld = (m_view_inv * vec4(vPosView, 1.0)).xyz;\n"
        // Material normals include normal maps and smoothed cliff normals, so
        // they cannot decide whether standing water is physically plausible.
        // Position derivatives recover the actual triangle plane instead.
        "    vec3 nwnRainDx = dFdx(nwnRainWorld);\n"
        "    vec3 nwnRainDy = dFdy(nwnRainWorld);\n"
        "    vec3 nwnRainGeomNormal = normalize(cross(nwnRainDx, nwnRainDy));\n"
        "    float nwnRainGeomUp = abs(nwnRainGeomNormal.z);\n"
        "    float nwnRainStaticAlpha = step(1.5, nwnRainEligible);\n"
        // Geometry derivatives remain the hard cliff rejection. Interpolated
        // stock surface normals provide the smooth flatness transition across
        // neighboring terrain triangles, eliminating tile/triangle outlines.
        "    vec3 nwnRainSmoothWorldN = normalize(mat3(m_view_inv) * vSurfaceNormal);\n"
        "    float nwnRainCliffReject = smoothstep(0.68, 0.78, nwnRainGeomUp);\n"
        "    float nwnRainSmoothFlat = smoothstep(0.88, 0.98,\n"
        "                                          abs(nwnRainSmoothWorldN.z));\n"
        "    float nwnRainFlat = nwnRainCliffReject * nwnRainSmoothFlat;\n"
        "    float nwnRainFacing = mix(0.28, 1.0, smoothstep(0.08, 0.78, nwnRainGeomUp));\n"
        "    float nwnRainMask = nwnRainPuddle(nwnRainWorld.xy, nwnRainWetness)\n"
        "                      * nwnRainFlat * nwnRainWetness\n"
        "                      * (1.0 - nwnRainStaticAlpha);\n"
        // This is perceived shallow depth only. It drives absorption and
        // refraction strength but never writes gl_FragDepth or moves geometry.
        "    float nwnRainOpticalDepth = smoothstep(0.08, 0.92, nwnRainMask);\n"
        // Apply only the visibility left after NWN's native fog pass. The rain
        // branch remains at the safe final-colour location, so shadow/A2C
        // material logic is untouched, while distant wetness converges back to
        // the already-fogged base material exactly as shadow strength does.
        "    float nwnRainFogVisibility = 1.0;\n"
        "#if FOG == 1\n"
        "    nwnRainFogVisibility = 1.0 - GetFogAlpha(fFogFragCoord);\n"
        "#endif\n"
        "    nwnRainMask *= nwnRainFogVisibility;\n"
        "    float nwnRainGrazing = pow(1.0 - abs(dot(normalize(-vPosView),\n"
        "                                      normalize(vFragmentNormal))), 4.0);\n"
        // Every exposed static surface may become mildly wet. Only the strict
        // geometric-flatness branch below can become standing water.
        "    float nwnRainFilm = nwnRainWetness * nwnRainFacing * 0.14\n"
        "                      * nwnRainFogVisibility;\n"
        // Non-puddle wet ground gets an engine-noise micro-normal plus the
        // accepted scalar impact normals. Only a restrained normal-derived sky
        // specular term is added; there is no painted ring or depth write.
        "    float nwnRainGroundOnly = (1.0 - smoothstep(0.04, 0.32,\n"
        "                                      nwnRainOpticalDepth))\n"
        "                              * nwnRainSmoothFlat;\n"
        // Alpha/card surfaces may be vertical or sloped. Give them the same
        // film disturbance through a dominant-axis projection, but never the
        // flatness-driven standing-water branch used by opaque terrain.
        "    nwnRainGroundOnly = max(nwnRainGroundOnly,\n"
        "                             nwnRainStaticAlpha * nwnRainFacing);\n"
        "    vec3 nwnRainAbsGeomNormal = abs(nwnRainGeomNormal);\n"
        "    vec2 nwnRainNoiseCoord = nwnRainWorld.xy;\n"
        "    vec3 nwnRainNoiseBaseN = vec3(0.0, 0.0, 1.0);\n"
        "    vec3 nwnRainNoiseTangentU = vec3(1.0, 0.0, 0.0);\n"
        "    vec3 nwnRainNoiseTangentV = vec3(0.0, 1.0, 0.0);\n"
        "    if (nwnRainStaticAlpha > 0.5) {\n"
        "        nwnRainNoiseBaseN = nwnRainSmoothWorldN;\n"
        "        if (nwnRainAbsGeomNormal.x > nwnRainAbsGeomNormal.y &&\n"
        "            nwnRainAbsGeomNormal.x > nwnRainAbsGeomNormal.z) {\n"
        "            nwnRainNoiseCoord = nwnRainWorld.yz;\n"
        "            nwnRainNoiseTangentU = vec3(0.0, 1.0, 0.0);\n"
        "            nwnRainNoiseTangentV = vec3(0.0, 0.0, 1.0);\n"
        "        } else if (nwnRainAbsGeomNormal.y > nwnRainAbsGeomNormal.z) {\n"
        "            nwnRainNoiseCoord = nwnRainWorld.xz;\n"
        "            nwnRainNoiseTangentU = vec3(1.0, 0.0, 0.0);\n"
        "            nwnRainNoiseTangentV = vec3(0.0, 0.0, 1.0);\n"
        "        }\n"
        "    }\n"
        "    float nwnRainGroundNoise = 0.5;\n"
        "    float nwnRainGroundSpec = 0.0;\n"
        "    if (nwnRainGroundOnly > 0.001) {\n"
        "        nwnRainGroundNoise = nwnRainGroundNoiseHeight(\n"
        "                                      nwnRainNoiseCoord, nwnRainTime);\n"
        "        vec2 nwnRainGroundSlope = nwnRainGroundNoiseSlope(\n"
        "                                      nwnRainNoiseCoord, nwnRainTime);\n"
        // Ground drops temporarily erase the fine moving film beneath their
        // contact wave. The wave then replaces that detail in the normal only:
        // it breaks the noise optically without painting a ring into albedo.
        "        vec3 nwnRainGroundImpact = nwnRainRippleField(\n"
        "                                      nwnRainNoiseCoord * 0.95,\n"
        "                                      nwnRainTime * 1.32, 0.50);\n"
        "        float nwnRainGroundBreak = smoothstep(0.035, 0.30,\n"
        "                                      nwnRainGroundImpact.x);\n"
        "        nwnRainGroundNoise = mix(nwnRainGroundNoise, 0.5,\n"
        "                                  nwnRainGroundBreak * 0.82);\n"
        "        vec2 nwnRainGroundSurface = nwnRainGroundSlope * 0.050\n"
        "                                  * (1.0 - nwnRainGroundBreak * 0.72)\n"
        "                                  - nwnRainGroundImpact.yz * 0.032;\n"
        "        vec3 nwnRainGroundWorldN = normalize(nwnRainNoiseBaseN\n"
        "                                      - nwnRainGroundSurface.x\n"
        "                                        * nwnRainNoiseTangentU\n"
        "                                      - nwnRainGroundSurface.y\n"
        "                                        * nwnRainNoiseTangentV);\n"
        "        vec3 nwnRainGroundViewN = normalize(\n"
        "                                      mat3(m_view) * nwnRainGroundWorldN);\n"
        "        vec3 nwnRainGroundIncident = normalize(vPosView);\n"
        "        vec3 nwnRainSkyView = normalize(\n"
        "                                      mat3(m_view) * vec3(0.0, 0.0, 1.0));\n"
        "        vec3 nwnRainGroundHalf = normalize(\n"
        "                                      -nwnRainGroundIncident + nwnRainSkyView);\n"
        "        nwnRainGroundSpec = pow(max(dot(nwnRainGroundViewN,\n"
        "                                      nwnRainGroundHalf), 0.0), 20.0)\n"
        "                              * mix(0.45, 1.0, nwnRainGroundNoise)\n"
        "                              * nwnRainGroundOnly;\n"
        "    }\n"
        "    nwnRainFilm *= mix(1.0, mix(0.62, 1.42, nwnRainGroundNoise),\n"
        "                       nwnRainGroundOnly);\n"
        "    vec3 nwnRainFilmColor = FragmentColor.rgb * vec3(0.84, 0.87, 0.90);\n"
        "    nwnRainFilmColor += vec3(0.025, 0.035, 0.05) * nwnRainGrazing;\n"
        "    FragmentColor.rgb = mix(FragmentColor.rgb, nwnRainFilmColor, nwnRainFilm);\n"
        // Do not bury the micro-normal response inside the 14% film blend.
        // Apply a bounded normal highlight and low-contrast noise modulation
        // directly to visible, non-puddle wet ground.
        "    float nwnRainGroundVisibility = nwnRainGroundOnly * nwnRainWetness\n"
        "                                  * nwnRainFogVisibility;\n"
        "    FragmentColor.rgb *= 1.0 + (nwnRainGroundNoise - 0.5) * 0.10\n"
        "                              * nwnRainGroundVisibility;\n"
        "    FragmentColor.rgb += vec3(0.11, 0.135, 0.17)\n"
        "                           * nwnRainGroundSpec * nwnRainWetness\n"
        "                           * nwnRainFogVisibility;\n"
        "    vec3 nwnRainAbsorption = mix(vec3(0.74, 0.78, 0.83),\n"
        "                                 vec3(0.42, 0.49, 0.58),\n"
        "                                 nwnRainOpticalDepth);\n"
        "    vec3 nwnRainWetColor = FragmentColor.rgb * nwnRainAbsorption;\n"
        "    nwnRainWetColor += vec3(0.10, 0.13, 0.17) * nwnRainGrazing;\n"
        "    if (nwnRainReflectionReady > 0.5 && nwnRainMask > 0.002) {\n"
        "        vec3 nwnRainRipple = nwnRainRippleField(nwnRainWorld.xy,\n"
        "                                                nwnRainTime, 1.0);\n"
        "        vec2 nwnRainWave = vec2(\n"
        "            sin(nwnRainWorld.x * 5.1 + nwnRainWorld.y * 2.7 + nwnRainTime * 1.4),\n"
        "            cos(nwnRainWorld.y * 4.7 - nwnRainWorld.x * 2.2 + nwnRainTime * 1.1));\n"
        "        vec2 nwnRainSurface = -nwnRainRipple.yz * 0.020\n"
        "                            + nwnRainWave * 0.012;\n"
        "        vec3 nwnRainWaterWorldN = normalize(vec3(nwnRainSurface, 1.0));\n"
        "        vec3 nwnRainWaterViewN = normalize(mat3(m_view) * nwnRainWaterWorldN);\n"
        "        vec3 nwnRainIncident = normalize(vPosView);\n"
        // Refraction dominates from above, as it does in fswater.shd. Reject
        // offsets that cross onto a different depth layer so a nearby creature
        // cannot smear into the puddle through the previous framebuffer.
        "        vec2 nwnRainScreenUv = gl_FragCoord.xy * pixelParams.xy;\n"
        "        float nwnRainRefractionScale = mix(0.0035, 0.0115,\n"
        "                                               nwnRainOpticalDepth);\n"
        "        vec2 nwnRainRefractUv = clamp(nwnRainScreenUv +\n"
        "            nwnRainSurface * vec2(nwnRainRefractionScale,\n"
        "                nwnRainRefractionScale * pixelParams.x /\n"
        "                                      max(0.00001, pixelParams.y)),\n"
        "            vec2(0.002), vec2(0.998));\n"
        "        vec3 nwnRainRefractPos = nwnRainViewPosition(nwnRainRefractUv);\n"
        "        float nwnRainRefractGate = 1.0 - smoothstep(0.12, 1.25,\n"
        "            abs(nwnRainRefractPos.z - vPosView.z));\n"
        "        vec3 nwnRainRefracted = texture2D(nwnRainFbColor,\n"
        "                                           nwnRainRefractUv).rgb;\n"
        "        float nwnRainRefractionMix = nwnRainRefractGate\n"
        "            * mix(0.20, 0.62, nwnRainOpticalDepth);\n"
        "        nwnRainWetColor = mix(nwnRainWetColor, nwnRainRefracted,\n"
        "                              nwnRainRefractionMix);\n"
        "        vec3 nwnRainRay = reflect(nwnRainIncident, nwnRainWaterViewN);\n"
        "        vec4 nwnRainHit = nwnRainTraceReflection(vPosView +\n"
        "                              nwnRainWaterViewN * 0.035, nwnRainRay);\n"
        "        vec3 nwnRainReflected = texture2D(nwnRainFbColor,\n"
        "                                      clamp(nwnRainHit.xy, 0.002, 0.998)).rgb;\n"
        "        float nwnRainNdotV = clamp(dot(nwnRainWaterViewN,\n"
        "                                      -nwnRainIncident), 0.0, 1.0);\n"
        "        float nwnRainFresnel = 0.02 + 0.98 * pow(1.0 - nwnRainNdotV, 5.0);\n"
        "        float nwnRainHitValid = step(0.0, nwnRainHit.x) *\n"
        "            step(0.0, nwnRainHit.y) * step(nwnRainHit.x, 1.0) *\n"
        "            step(nwnRainHit.y, 1.0);\n"
        "        vec3 nwnRainHitNormal = nwnRainDepthNormal(\n"
        "                                      clamp(nwnRainHit.xy, 0.002, 0.998));\n"
        "        float nwnRainNormalGate = clamp(0.1 - 4.0 *\n"
        "                                      dot(nwnRainHitNormal, nwnRainRay),\n"
        "                                      0.0, 1.0);\n"
        "        float nwnRainEdge = min(min(nwnRainHit.x, nwnRainHit.y),\n"
        "            min(1.0 - nwnRainHit.x, 1.0 - nwnRainHit.y));\n"
        "        float nwnRainReflectMix = nwnRainHitValid * nwnRainNormalGate *\n"
        "            (1.0 - nwnRainHit.w) * smoothstep(0.0, 0.055, nwnRainEdge) *\n"
        "            nwnRainFresnel * 0.92;\n"
        "        nwnRainWetColor = mix(nwnRainWetColor, nwnRainReflected,\n"
        "                              nwnRainReflectMix);\n"
        "    }\n"
        "    FragmentColor.rgb = mix(FragmentColor.rgb, nwnRainWetColor,\n"
        "                            clamp(nwnRainMask * 0.86, 0.0, 0.86));\n"
        "}\n"
        "#endif\n"
        "    ";
    source.insert(shiftedOutput, kBody);
    return true;
}

} // namespace rain_runtime

extern "C" int rain_area_set_weather(void* area, uint8_t weather,
                                      float densityPercent) {
    int result = 0;
    if (rain_runtime::g_weatherTrampoline)
        result = rain_runtime::g_weatherTrampoline(area, weather, densityPercent);
    else if (rain_runtime::g_weatherHook && eng::AreaSetWeather) {
        // Same validated Linux fallback used by the renderer trace hooks for
        // prologues subhook cannot relocate: remove, call the real entry, then
        // reinstall. CNWCArea weather changes run on the single render thread
        // and occur only on area/weather transitions, so this is not a hot path.
        subhook_remove(rain_runtime::g_weatherHook);
        result = eng::AreaSetWeather(area, weather, densityPercent);
        subhook_install(rain_runtime::g_weatherHook);
    }
    if (result)
        rain_runtime::observe_engine_weather(area, (int)weather, densityPercent);
    return result;
}

static void install_rain_engine_hooks() {
    if (rain_runtime::g_weatherHook || !eng::AreaSetWeather) return;
    rain_runtime::g_weatherHook =
        subhook_new((void*)eng::AreaSetWeather, (void*)rain_area_set_weather,
                    SUBHOOK_64BIT_OFFSET);
    if (!rain_runtime::g_weatherHook ||
        subhook_install(rain_runtime::g_weatherHook) != 0) {
        fprintf(stderr,
                "[rain][linux] warning: CNWCArea::SetWeather hook unavailable; "
                "rain effects will fail closed\n");
        if (rain_runtime::g_weatherHook) {
            subhook_free(rain_runtime::g_weatherHook);
            rain_runtime::g_weatherHook = nullptr;
        }
        return;
    }
    rain_runtime::g_weatherTrampoline = (eng::AreaSetWeather_t)
        subhook_get_trampoline(rain_runtime::g_weatherHook);
    fprintf(stderr, "[rain][linux] engine weather observer active%s\n",
            rain_runtime::g_weatherTrampoline ? "" :
            " (remove/call/reinstall fallback)");
}

static void uninstall_rain_engine_hooks() {
    if (!rain_runtime::g_weatherHook) return;
    subhook_remove(rain_runtime::g_weatherHook);
    subhook_free(rain_runtime::g_weatherHook);
    rain_runtime::g_weatherHook = nullptr;
    rain_runtime::g_weatherTrampoline = nullptr;
}

static bool rain_needs_shader_sources() { return true; }
static bool rain_needs_draw_observer() { return true; }
static bool rain_patch_shader_source(std::string& source) {
    return rain_runtime::patch_fragment_shader(source);
}
static void rain_observe_draw(GLuint program, GLsizei count) {
    rain_runtime::observe_draw(program, count);
}
static void rain_note_program(GLuint program) {
    rain_runtime::note_program(program);
}
extern "C" void rain_uniform1i(GLint location, GLint value) {
    rain_runtime::observe_uniform1i(location, value);
    if (rain_runtime::g_realUniform1i)
        rain_runtime::g_realUniform1i(location, value);
}
static void install_rain_uniform_patch() {
    static bool tried = false;
    if (tried || !eng::GlewUniform1i) return;
    tried = true;
    void* current = *eng::GlewUniform1i;
    if (!current) { tried = false; return; }
    rain_runtime::g_realUniform1i = (rain_runtime::Uniform1i_t)current;
    *eng::GlewUniform1i = (void*)rain_uniform1i;
    fprintf(stderr, "[rain][linux] hooked glUniform1i for skinmesh classification\n");
}
static void rain_scene_begin(void* scene) { rain_runtime::scene_begin(scene); }
static void rain_scene_end(void* scene) { rain_runtime::scene_end(scene); }

#else

// Deliberate Windows hold: the accepted Linux module is ported only after a
// separate request, with platform-specific changes unless a shared bug is
// confirmed first.
static bool rain_needs_shader_sources() { return false; }
static bool rain_needs_draw_observer() { return false; }
static bool rain_patch_shader_source(std::string&) { return false; }
static void rain_observe_draw(GLuint, GLsizei) {}
static void rain_note_program(GLuint) {}
static void install_rain_uniform_patch() {}
static void install_rain_engine_hooks() {}
static void uninstall_rain_engine_hooks() {}
static void rain_scene_begin(void*) {}
static void rain_scene_end(void*) {}

#endif
