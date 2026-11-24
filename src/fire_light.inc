// Firelight: warm, flickering per-pixel light around torches, braziers and
// campfires.
//
// Source of truth is the engine's own light census (SetLightGL, published as
// g_lampLights): only lights the engine enabled this frame take part, at the
// engine's position and radius. A light counts as fire when lamp_is_fire
// accepts its colour (orange through yellow flame, not white, blue or
// warm-white electric lamps); it is then lit in a true flame orange rather
// than the engine colour, which in some areas is a sickly yellow-green.
// Nothing is selected or suppressed in the engine; this pass only adds light.
//
// One fullscreen pass over up to kFireMax of the nearest fires rebuilds each
// pixel's world position from this frame's scene depth and modulates the
// surface by its own colour (blend DST_COLOR, ONE: out = dst * (1 + light)).
// Each fire gets its own flicker -- a slow sway, a flutter and fast crackle --
// that brightens and dips the surfaces it reaches, and its light centre
// dances a few centimetres so lit patches move. Faces turned away receive less
// (wrapped Lambert from depth-derived normals). No occlusion: like the
// engine's own lamps, a wall does not block it, so the reach stays inside the
// engine radius.
namespace fire_light {
static const char* status = "off";
static GLuint program = 0, vao = 0, depthSampler = 0;
static bool failed = false;
static int lastCount = 0;
static constexpr int kFireMax = 32;

static const char* kFireFragmentShader =
    "#version 330 core\n"
    "uniform sampler2D sceneDepth;\n"
    "uniform mat4 vpInverse;\n"
    "uniform vec4 viewport4, eye4;\n"
    "uniform vec4 firePos[32];   // xyz, radius\n"
    "uniform vec4 fireColor[32]; // rgb (normalised), seed\n"
    "uniform int fireCount;\n"
    "uniform float strength, time;\n"
    "out vec4 color;\n"
    LIGHTNING_GLSL_HASH
    "float flicker(float seed) {\n"
    " uint s = uint(seed);\n"
    " float sway = noise(time * 1.3, s, true);\n"
    " float flutter = noise(time * 4.7, s + 3u, true);\n"
    " float crackle = noise(time * 13.0, s + 7u, false);\n"
    " return 0.55 * sway + 0.3 * flutter + 0.15 * crackle;\n"
    "}\n"
    "void main() {\n"
    " vec2 uv = (gl_FragCoord.xy - viewport4.xy) / viewport4.zw;\n"
    " float d = texture(sceneDepth, uv).r;\n"
    " if (d >= 0.999999) discard;\n"
    " vec4 h = vpInverse * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);\n"
    " vec3 p = h.xyz / h.w;\n"
    " vec3 n = cross(dFdx(p), dFdy(p)); float nl = length(n);\n"
    " n = nl > 1e-8 ? n / nl : vec3(0.0, 0.0, 1.0);\n"
    " if (dot(n, eye4.xyz - p) < 0.0) n = -n;\n"
    " vec3 light = vec3(0.0);\n"
    " for (int i = 0; i < 32; ++i) {\n"
    "  if (i >= fireCount) break;\n"
    "  float radius = firePos[i].w;\n"
    "  float seed = fireColor[i].w;\n"
    "  uint s = uint(seed);\n"
    // The flame's light centre dances a little; lit patches move with it.
    "  vec3 wobble = vec3(noise(time * 2.1, s + 11u, true), noise(time * 2.3, s + 13u, true),\n"
    "   0.5 * noise(time * 3.1, s + 17u, true)) * 0.08;\n"
    "  vec3 c = firePos[i].xyz + wobble;\n"
    "  vec3 toLight = c - p; float dist = length(toLight);\n"
    "  float reach = radius * 0.85;\n"
    "  if (dist > reach) continue;\n"
    "  float x = dist / reach;\n"
    "  float falloff = (1.0 - x * x) * (1.0 - x * x) / (1.0 + 4.0 * x * x);\n"
    "  float facing = clamp(dot(n, toLight / max(dist, 1e-4)) * 0.6 + 0.4, 0.0, 1.0);\n"
    // Mostly a brighten, dipping slightly below the engine's steady light.
    "  float f = flicker(seed);\n"
    "  float amount = 0.12 + 0.32 * f;\n"
    "  light += fireColor[i].rgb * amount * falloff * facing;\n"
    " }\n"
    " color = vec4(light * strength, 0.0);\n"
    "}\n";

static bool ensure_program() {
    if (program) return true;
    if (failed) return false;
    if (!weather_particles::ensure_entry_points()) { failed = true; return false; }
    program = lightning_beams::build_program(lightning_beams::kLightVertexShader,
                                             kFireFragmentShader, "firelight");
    if (program) weather_particles::genVaos(1, &vao);
    if (program) weather_particles::genSamplers(1, &depthSampler);
    if (depthSampler) {
        weather_particles::samplerParam(depthSampler, GL_TEXTURE_COMPARE_MODE, GL_NONE);
        weather_particles::samplerParam(depthSampler, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        weather_particles::samplerParam(depthSampler, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        weather_particles::samplerParam(depthSampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        weather_particles::samplerParam(depthSampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    if (!vao || !depthSampler) program = 0;
    failed = !program;
    fprintf(stderr, "[shadowmap][firelight] program %s\n", program ? "ready" : "failed");
    return program != 0;
}

static void draw(void* scene) {
    const auto& camera = g_shadowFrameContext;
    if (!g_fireLightEnabled) { status = "off"; lastCount = 0; return; }
    if (!camera.valid || scene != g_areaScene || camera.scene != scene) return;
    if (!gl::GetIntegerv || !gl::GetBooleanv || !gl::IsEnabled || !gl::ActiveTexture ||
        !gl::BindTexture || !gl::UniformMatrix4fv || !gl::Uniform1f || !gl::Uniform4f ||
        !gl::Uniform4fv || !gl::Uniform1i || !gl::GetUniformLocation || !gl::Enable ||
        !gl::Disable || !gl::BlendFuncSeparate || !gl::BlendEquationSeparate || !gl::DepthMask) {
        status = "missing GL state API"; return;
    }
    if (!ensure_program()) { status = "unavailable"; return; }
    // Nearest fires from the engine's completed light list.
    struct Pick { float distance; unsigned index; float colour[3]; };
    Pick picks[kLampPool];
    unsigned pickCount = 0;
    const unsigned count = std::min<unsigned>(g_lampLightCount, kLampPool);
    for (unsigned i = 0; i < count; ++i) {
        const LampEntry& e = g_lampLights[i];
        if (!(e.radius > 0.2f && e.radius < 40.0f)) continue;
        Pick pick{};
        if (!lamp_is_fire(e.color, pick.colour)) continue;
        const float dx = e.pos[0] - camera.eye.x, dy = e.pos[1] - camera.eye.y,
                    dz = e.pos[2] - camera.eye.z;
        pick.distance = std::sqrt(dx * dx + dy * dy + dz * dz) - e.radius;
        pick.index = i;
        picks[pickCount++] = pick;
    }
    if (pickCount == 0) { status = "no fires in view"; lastCount = 0; return; }
    std::sort(picks, picks + pickCount, [](const Pick& a, const Pick& b) { return a.distance < b.distance; });
    const int fires = (int)std::min<unsigned>(pickCount, kFireMax);
    GLint viewport[4] = {}; gl::GetIntegerv(GL_VIEWPORT, viewport);
    if (std::memcmp(viewport, camera.viewport, sizeof(viewport)) != 0) {
        status = "viewport mismatch"; return;
    }
    const bool sceneDepthReady = g_sceneDepthTex && g_sceneDepthSerial == camera.serial &&
        g_sceneDepthW == viewport[2] && g_sceneDepthH == viewport[3];
    if (!sceneDepthReady) {
        status = "no scene depth this frame (are sun shadows enabled?)"; return;
    }
    float pos[kFireMax * 4], col[kFireMax * 4];
    for (int i = 0; i < fires; ++i) {
        const LampEntry& e = g_lampLights[picks[i].index];
        for (int k = 0; k < 3; ++k) { pos[i * 4 + k] = e.pos[k]; col[i * 4 + k] = picks[i].colour[k]; }
        pos[i * 4 + 3] = e.radius;
        // A stable per-light seed: the engine light object's address.
        col[i * 4 + 3] = (float)((((uintptr_t)e.key) >> 4) & 0xFFFFu);
    }
    nwn_core::OwnedPass owned;
    weather_particles::StateGuard restore;
    for (GLenum cap : weather_particles::StateGuard::caps) gl::Disable(cap);
    if (weather_particles::depthRangeSet) weather_particles::depthRangeSet(0.0, 1.0);
    weather_particles::enableIndexed(GL_BLEND, 0);
    weather_particles::colorMaskIndexed(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
    gl::DepthMask(GL_FALSE);
    weather_particles::bindVao(vao);
    weather_particles::useProgram(program); g_curProgram = program;
    auto loc = [](const char* name) { return gl::GetUniformLocation(program, name); };
    gl::ActiveTexture(GL_TEXTURE0); gl::BindTexture(GL_TEXTURE_2D, g_sceneDepthTex);
    weather_particles::bindSampler(0, depthSampler);
    gl::Uniform1i(loc("sceneDepth"), 0);
    gl::UniformMatrix4fv(loc("vpInverse"), 1, GL_FALSE, camera.viewProjectionInverse);
    gl::Uniform4f(loc("viewport4"), (float)viewport[0], (float)viewport[1],
                  (float)viewport[2], (float)viewport[3]);
    gl::Uniform4f(loc("eye4"), camera.eye.x, camera.eye.y, camera.eye.z, 1.0f);
    gl::Uniform4fv(loc("firePos"), fires, pos);
    gl::Uniform4fv(loc("fireColor"), fires, col);
    gl::Uniform1i(loc("fireCount"), fires);
    gl::Uniform1f(loc("strength"), std::max(0.0f, std::min(g_fireLightStrength, 3.0f)));
    gl::Uniform1f(loc("time"), (float)std::fmod(now_seconds(), 4096.0));
    lightning_beams::set_blend(0x0306 /* GL_DST_COLOR */, GL_ONE);
    weather_particles::drawArrays(0x0004 /* GL_TRIANGLES */, 0, 3);
    lastCount = fires;
    status = "drawing";
}
} // namespace fire_light
static void draw_fire_light(void* scene) { fire_light::draw(scene); }
