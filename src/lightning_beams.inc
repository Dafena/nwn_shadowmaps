// Procedural lightning beams (opt-out, default on).
//
// The engine draws a Lightning/Linked emitter as one jagged quad strip per
// emitter. EOM's vdu_beam000 textures its bolts with the exact resource
// eom_lbolt01, so a draw with that texture bound IS a beam strip and nothing
// else. For such a draw this module
//   1. reads the strip's vertices (vPos) and the program's m_mv, and reduces
//      them to the beam's two world-space endpoints;
//   2. hides that native draw -- but only when the private pass is ready and
//      every read succeeded, otherwise the native strip is drawn unchanged;
//   3. after the scene, draws one camera-facing ribbon per beam whose fragment
//      shader generates a braid of fine flickering strands that meet at the
//      source, wander, spread toward the target, and carry an HDR core (for
//      the engine's bloom), a blue glow and a faint haze.
// The three emitters of one beam produce three strips with the same ends; they
// are merged into one beam. Players without the plugin see the textured
// fallback strips from the HAK.
namespace lightning_beams {
static const char* status = "off";
static GLuint program = 0, vao = 0;
static bool failed = false;
static GLuint boundTexture = 0;
// What the bound texture identifies: a beam strip (vdu_beam000, eom_lbolt01)
// or a sky-to-ground strike strip (EOM's vim_exp2light, eom_lstrike01).
enum StripKind { kNone = 0, kBeamStrip = 1, kStrikeStrip = 2 };
static int boundKind = kNone;
static unsigned hiddenSerial = ~0u;
static unsigned reports = 0;

struct Beam { float a[3], b[3]; unsigned serial; int strips; };
static constexpr int kMaxBeams = 32;
static Beam beams[kMaxBeams];
static int beamCount = 0;
static unsigned beamSerial = ~0u;

// Strikes persist across frames: the native emitter's particles fly down the
// strip, so a single frame's strip may not reach the ground. The ground point
// is the lowest end seen; the residual plasma outlives the native effect.
static constexpr float kStrikeResidualSeconds = 1.6f;
struct Strike {
    float ground[3] = {};
    double first = 0.0, last = 0.0;
    unsigned seed = 0;
    bool live = false;
};
static constexpr int kMaxStrikes = 8;
static Strike strikes[kMaxStrikes];
static unsigned strikeSerialSeed = 1;
static double strike_clock();
static void note_strike(const float* bottom) {
    const double now = strike_clock();
    int slot = -1;
    for (int i = 0; i < kMaxStrikes; ++i) {
        const Strike& s = strikes[i];
        if (!s.live) continue;
        const float dx = s.ground[0] - bottom[0], dy = s.ground[1] - bottom[1];
        if (dx * dx + dy * dy < 2.5f * 2.5f && now - s.last < 0.3) { slot = i; break; }
    }
    if (slot < 0) {
        for (int i = 0; i < kMaxStrikes && slot < 0; ++i) if (!strikes[i].live) slot = i;
        if (slot < 0) return;
        Strike& s = strikes[slot];
        for (int k = 0; k < 3; ++k) s.ground[k] = bottom[k];
        s.first = now; s.seed = strikeSerialSeed++ * 7919u;
    }
    Strike& s = strikes[slot];
    s.ground[0] = bottom[0]; s.ground[1] = bottom[1];
    s.ground[2] = std::min(s.ground[2], bottom[2]);
    s.last = now; s.live = true;
}

static int strip_kind(const char* name) {
    if (!name) return kNone;
    char lower[24] = {};
    for (int i = 0; i < 23 && name[i]; ++i) {
        char c = name[i];
        lower[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 'a' - 'A') : c;
    }
    if (!std::strcmp(lower, "eom_lbolt01")) return kBeamStrip;
    if (!std::strcmp(lower, "eom_lstrike01")) return kStrikeStrip;
    return kNone;
}
static void note_material_texture(const char* name) {
    boundTexture = 0; boundKind = kNone;
    const int kind = strip_kind(name);
    if (!kind || !gl::GetIntegerv || !gl::ActiveTexture) return;
    GLint active = 0, texture = 0;
    gl::GetIntegerv(GL_ACTIVE_TEXTURE, &active);
    gl::ActiveTexture(GL_TEXTURE0);
    gl::GetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
    gl::ActiveTexture((GLenum)active);
    boundTexture = (GLuint)texture;
    boundKind = kind;
}

// Read-only GL queries used at the native draw. Resolved once.
using GetAttribLocationFn = GLint (*)(GLuint, const char*);
using GetVertexAttribivFn = void (*)(GLuint, GLenum, GLint*);
using GetVertexAttribPointervFn = void (*)(GLuint, GLenum, void**);
using GetBufferSubDataFn = void (*)(GLenum, ptrdiff_t, ptrdiff_t, void*);
using GetBufferParameterivFn = void (*)(GLenum, GLenum, GLint*);
using BindBufferFn = void (*)(GLenum, GLuint);
static GetAttribLocationFn getAttribLocation = nullptr;
static GetVertexAttribivFn getVertexAttribiv = nullptr;
static GetVertexAttribPointervFn getVertexAttribPointerv = nullptr;
static GetBufferSubDataFn getBufferSubData = nullptr;
static GetBufferParameterivFn getBufferParameteriv = nullptr;
static BindBufferFn bindBuffer = nullptr;
static bool readersResolved = false;
static bool resolve_readers() {
    if (!readersResolved) {
        readersResolved = true;
        getAttribLocation = (GetAttribLocationFn)dlsym(RTLD_NEXT, "glGetAttribLocation");
        getVertexAttribiv = (GetVertexAttribivFn)dlsym(RTLD_NEXT, "glGetVertexAttribiv");
        getVertexAttribPointerv = (GetVertexAttribPointervFn)dlsym(RTLD_NEXT, "glGetVertexAttribPointerv");
        getBufferSubData = (GetBufferSubDataFn)dlsym(RTLD_NEXT, "glGetBufferSubData");
        getBufferParameteriv = (GetBufferParameterivFn)dlsym(RTLD_NEXT, "glGetBufferParameteriv");
        bindBuffer = (BindBufferFn)dlsym(RTLD_NEXT, "glBindBuffer");
    }
    return getAttribLocation && getVertexAttribiv && getVertexAttribPointerv &&
        getBufferSubData && getBufferParameteriv && bindBuffer &&
        gl::GetIntegerv && gl::GetUniformLocation && gl::GetUniformfv;
}

// Copy [offset, offset+size) of the buffer bound in `binding` (or CPU memory
// when no buffer is bound). Restores GL_ARRAY_BUFFER / element bindings.
static bool read_bytes(GLenum target, GLenum bindingQuery, GLuint buffer,
                       const void* pointer, size_t offset, size_t size, void* out) {
    if (!buffer) {
        if (!pointer) return false;
        std::memcpy(out, static_cast<const unsigned char*>(pointer) + offset, size);
        return true;
    }
    GLint previous = 0;
    gl::GetIntegerv(bindingQuery, &previous);
    if ((GLuint)previous != buffer) bindBuffer(target, buffer);
    GLint mapped = 0, bufferSize = 0;
    getBufferParameteriv(target, 0x88BC /* GL_BUFFER_MAPPED */, &mapped);
    getBufferParameteriv(target, 0x8764 /* GL_BUFFER_SIZE */, &bufferSize);
    const bool okay = !mapped && bufferSize > 0 &&
        offset + size <= (size_t)bufferSize;
    if (okay) getBufferSubData(target, (ptrdiff_t)offset, (ptrdiff_t)size, out);
    if ((GLuint)previous != buffer) bindBuffer(target, (GLuint)previous);
    return okay;
}

static bool matrix_finite(const float* m) {
    for (int i = 0; i < 16; ++i) if (!std::isfinite(m[i])) return false;
    return true;
}
static void transform(const float* m, const float* p, float w, float* out) {
    for (int r = 0; r < 4; ++r)
        out[r] = m[r] * p[0] + m[4 + r] * p[1] + m[8 + r] * p[2] + m[12 + r] * w;
}

// Reduce the strip's world-space vertices to its two ends. The farthest pair
// spans the beam; each end is the mean of the vertices near that extreme, so
// the strip's half-width offsets cancel. The end nearer the first submitted
// vertex is the emitter (source) side.
static bool beam_ends(const float (*v)[3], int n, int firstVertex, float* a, float* b) {
    if (n < 4) return false;
    auto dist2 = [&](int i, int j) {
        float s = 0; for (int k = 0; k < 3; ++k) { float d = v[i][k] - v[j][k]; s += d * d; }
        return s;
    };
    int p = 0, q = 0; float best = -1;
    for (int i = 0; i < n; ++i) { float d = dist2(0, i); if (d > best) { best = d; p = i; } }
    best = -1;
    for (int i = 0; i < n; ++i) { float d = dist2(p, i); if (d > best) { best = d; q = i; } }
    const float length = std::sqrt(best);
    if (!(length > 0.3f && length < 120.0f)) return false;
    float axis[3]; for (int k = 0; k < 3; ++k) axis[k] = (v[q][k] - v[p][k]) / length;
    const float slack = 0.04f * length + 0.05f;
    float sumA[3] = {}, sumB[3] = {}; int na = 0, nb = 0;
    for (int i = 0; i < n; ++i) {
        float t = 0; for (int k = 0; k < 3; ++k) t += (v[i][k] - v[p][k]) * axis[k];
        if (t < slack) { for (int k = 0; k < 3; ++k) sumA[k] += v[i][k]; ++na; }
        else if (t > length - slack) { for (int k = 0; k < 3; ++k) sumB[k] += v[i][k]; ++nb; }
    }
    if (!na || !nb) return false;
    for (int k = 0; k < 3; ++k) { a[k] = sumA[k] / na; b[k] = sumB[k] / nb; }
    float da = 0, db = 0;
    for (int k = 0; k < 3; ++k) {
        da += (v[firstVertex][k] - a[k]) * (v[firstVertex][k] - a[k]);
        db += (v[firstVertex][k] - b[k]) * (v[firstVertex][k] - b[k]);
    }
    if (db < da) for (int k = 0; k < 3; ++k) std::swap(a[k], b[k]);
    return true;
}

static void add_beam(const float* a, const float* b, unsigned serial) {
    if (beamSerial != serial) { beamSerial = serial; beamCount = 0; }
    float length = 0;
    for (int k = 0; k < 3; ++k) length += (b[k] - a[k]) * (b[k] - a[k]);
    length = std::sqrt(length);
    const float merge2 = std::pow(std::max(0.4f, 0.08f * length), 2.0f);
    for (int i = 0; i < beamCount; ++i) {
        float s = 0, e = 0;
        for (int k = 0; k < 3; ++k) {
            s += (beams[i].a[k] - a[k]) * (beams[i].a[k] - a[k]);
            e += (beams[i].b[k] - b[k]) * (beams[i].b[k] - b[k]);
        }
        if (s < merge2 && e < merge2) { ++beams[i].strips; return; }
    }
    if (beamCount >= kMaxBeams) return;
    Beam& beam = beams[beamCount++];
    for (int k = 0; k < 3; ++k) { beam.a[k] = a[k]; beam.b[k] = b[k]; }
    beam.serial = serial; beam.strips = 1;
}

// Called at every engine draw. True means the native draw must be skipped
// because this beam will be drawn privately.
static bool capture_native_draw(bool nativeAreaDraw, const DrawRef& draw) {
    if (!boundTexture) return false;
    const auto& camera = g_shadowFrameContext;
    if (!nativeAreaDraw || !g_lightningEnabled || failed || !program ||
        !camera.valid || camera.scene != g_areaScene || !resolve_readers()) return false;
    GLint active = 0, texture = 0, current = 0;
    gl::GetIntegerv(GL_ACTIVE_TEXTURE, &active);
    gl::ActiveTexture(GL_TEXTURE0);
    gl::GetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
    gl::ActiveTexture((GLenum)active);
    if ((GLuint)texture != boundTexture) return false;
    gl::GetIntegerv(GL_CURRENT_PROGRAM, &current);
    const bool report = reports < 6;
    auto fail = [&](const char* why) {
        if (report) { ++reports; fprintf(stderr, "[shadowmap][lightning] native strip kept: %s\n", why); }
        return false;
    };
    if (!current) return fail("no program");
    const GLuint prog = (GLuint)current;
    const GLint position = getAttribLocation(prog, "vPos");
    if (position < 0) return fail("no vPos");
    GLint enabled = 0, size = 0, type = 0, stride = 0, buffer = 0;
    getVertexAttribiv((GLuint)position, 0x8622 /* ENABLED */, &enabled);
    getVertexAttribiv((GLuint)position, 0x8623 /* SIZE */, &size);
    getVertexAttribiv((GLuint)position, 0x8625 /* TYPE */, &type);
    getVertexAttribiv((GLuint)position, 0x8624 /* STRIDE */, &stride);
    getVertexAttribiv((GLuint)position, 0x889F /* BUFFER_BINDING */, &buffer);
    void* pointer = nullptr;
    getVertexAttribPointerv((GLuint)position, 0x8645 /* POINTER */, &pointer);
    if (!enabled || size < 3 || type != GL_FLOAT) return fail("unexpected vPos format");
    if (stride == 0) stride = size * (GLint)sizeof(float);

    // Referenced vertex range.
    constexpr int kMaxIndices = 4096, kMaxVertices = 1024;
    if (draw.count < 4 || draw.count > kMaxIndices) return fail("vertex count out of range");
    static unsigned indexData[kMaxIndices];
    int firstVertex = 0, lo = 0, hi = 0;
    if (draw.elements) {
        const size_t width = draw.type == 0x1405 ? 4 : draw.type == 0x1403 ? 2 :
                             draw.type == 0x1401 ? 1 : 0;
        if (!width) return fail("unknown index type");
        GLint elementBuffer = 0;
        gl::GetIntegerv(0x8895 /* ELEMENT_ARRAY_BUFFER_BINDING */, &elementBuffer);
        static unsigned char raw[kMaxIndices * 4];
        if (!read_bytes(0x8893, 0x8895, (GLuint)elementBuffer,
                        elementBuffer ? nullptr : draw.indices,
                        elementBuffer ? (size_t)draw.indices : 0,
                        width * (size_t)draw.count, raw)) return fail("index read failed");
        lo = INT32_MAX; hi = -1;
        for (int i = 0; i < draw.count; ++i) {
            unsigned value = width == 4 ? reinterpret_cast<const uint32_t*>(raw)[i] :
                             width == 2 ? reinterpret_cast<const uint16_t*>(raw)[i] : raw[i];
            indexData[i] = value + (unsigned)draw.baseVertex;
            lo = std::min(lo, (int)indexData[i]); hi = std::max(hi, (int)indexData[i]);
        }
        firstVertex = (int)indexData[0];
    } else {
        lo = draw.first; hi = draw.first + draw.count - 1; firstVertex = lo;
    }
    const int n = hi - lo + 1;
    if (lo < 0 || n < 4 || n > kMaxVertices) return fail("vertex range out of range");
    static unsigned char vertexBytes[kMaxVertices * 64];
    if ((size_t)stride * (size_t)n > sizeof(vertexBytes) || stride < 12) return fail("stride too large");
    const size_t base = (size_t)pointer;   // buffer offset when a buffer is bound
    if (!read_bytes(0x8892, 0x8894, (GLuint)buffer, buffer ? nullptr : pointer,
                    (buffer ? base : 0) + (size_t)lo * stride,
                    (size_t)(n - 1) * stride + 12, vertexBytes)) return fail("vertex read failed");

    // Object space -> world space through the engine's own matrices.
    float toWorld[16];
    const GLint mvAt = gl::GetUniformLocation(prog, "m_mv");
    if (mvAt >= 0) {
        float mv[16]; gl::GetUniformfv(prog, mvAt, mv);
        if (!matrix_finite(mv)) return fail("bad m_mv");
        mat4_mul(camera.viewInverse, mv, toWorld);
    } else {
        return fail("no m_mv");
    }
    static float world[kMaxVertices][3];
    for (int i = 0; i < n; ++i) {
        float p[3]; std::memcpy(p, vertexBytes + (size_t)i * stride, sizeof(p));
        float o[4]; transform(toWorld, p, 1.0f, o);
        if (!std::isfinite(o[0]) || !std::isfinite(o[1]) || !std::isfinite(o[2])) return fail("bad vertex");
        world[i][0] = o[0]; world[i][1] = o[1]; world[i][2] = o[2];
    }
    float a[3], b[3];
    if (boundKind == kStrikeStrip) {
        // A strike only needs its ground contact. The native strip hangs
        // straight down from the model's sky emitter and is symmetric about
        // its centre line, so the ground point is the mean XY of every vertex
        // the draw references, at the mean height of its two lowest. (The
        // beam end-finder failed here: on a 4 m x 19 m quad the far pair is a
        // diagonal and one bottom corner fell outside its end slack, putting
        // the bolt metres to the side.)
        static unsigned char used[kMaxVertices];
        std::memset(used, 0, (size_t)n);
        if (draw.elements) for (int i = 0; i < draw.count; ++i) used[indexData[i] - (unsigned)lo] = 1;
        else std::memset(used, 1, (size_t)n);
        double sx = 0, sy = 0; int count = 0;
        float low0 = 1e30f, low1 = 1e30f;
        for (int i = 0; i < n; ++i) {
            if (!used[i]) continue;
            sx += world[i][0]; sy += world[i][1]; ++count;
            const float z = world[i][2];
            if (z < low0) { low1 = low0; low0 = z; } else if (z < low1) low1 = z;
        }
        if (count < 4 || !(low1 < 1e29f)) return fail("no strike ground");
        a[0] = (float)(sx / count); a[1] = (float)(sy / count); a[2] = 0.5f * (low0 + low1);
        for (int k = 0; k < 3; ++k) b[k] = a[k];
        note_strike(a);
    } else {
        if (!beam_ends(world, n, firstVertex - lo, a, b)) return fail("no beam ends");
        add_beam(a, b, camera.serial);
    }
    if (report) {
        ++reports;
        fprintf(stderr, "[shadowmap][lightning] %s strip %s n=%d buffer=%d -> (%.2f %.2f %.2f) to (%.2f %.2f %.2f)\n",
                boundKind == kStrikeStrip ? "strike" : "beam",
                draw.elements ? "elements" : "arrays", n, buffer, a[0], a[1], a[2], b[0], b[1], b[2]);
    }
    hiddenSerial = camera.serial;
    return true;
}

// Shared GLSL helpers: integer PCG hash and value noise. A sin() hash
// collapses once time-based seeds grow large (every strand became the same
// smooth curve), so every random number here is integer-based.
#define LIGHTNING_GLSL_HASH \
    "uint pcg(uint v) { uint s = v * 747796405u + 2891336453u;\n" \
    " uint w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u; return (w >> 22u) ^ w; }\n" \
    "float h01(uint v) { return float(pcg(v) & 0xFFFFFFu) / 16777215.0; }\n" \
    "float noise(float x, uint s, bool smoothed) {\n" \
    " float cell = floor(x), f = x - cell; float u = smoothed ? f * f * (3.0 - 2.0 * f) : f;\n" \
    " uint i = uint(int(cell) + 65536);\n" \
    " return mix(h01(pcg(i) ^ s), h01(pcg(i + 1u) ^ s), u) * 2.0 - 1.0;\n" \
    "}\n"

// The ribbon runs `extend` metres past both ends, so the glow and haze close
// in rounded caps instead of stopping on a straight line at hand and target.
static const char* kVertexShader =
    "#version 330 core\n"
    "uniform mat4 vp; uniform vec4 beamA4, beamB4, eye4; uniform float halfWidth, extend;\n"
    "out vec2 coord; out float eyeDist;\n"
    "const int kSegments = 96;\n"
    "void main() {\n"
    " vec3 beamA = beamA4.xyz, beamB = beamB4.xyz, eye = eye4.xyz;\n"
    " const vec2 corners[6] = vec2[6](vec2(0,-1),vec2(1,-1),vec2(1,1),vec2(0,-1),vec2(1,1),vec2(0,1));\n"
    " vec2 k = corners[gl_VertexID % 6];\n"
    " vec3 span = beamB - beamA; float len = max(length(span), 1e-4); vec3 dir = span / len;\n"
    " float e = extend / len;\n"
    " float t = mix(-e, 1.0 + e, (float(gl_VertexID / 6) + k.x) / float(kSegments));\n"
    " vec3 p = beamA + dir * (t * len);\n"
    " vec3 side = cross(dir, eye - p); float sl = length(side);\n"
    " side = sl > 1e-4 ? side / sl : normalize(cross(dir, abs(dir.z) < 0.9 ? vec3(0,0,1) : vec3(1,0,0)));\n"
    " coord = vec2(t, k.y * halfWidth);\n"
    " vec3 world = p + side * k.y * halfWidth;\n"
    " eyeDist = length(world - eye);\n"
    " gl_Position = vp * vec4(world, 1.0);\n"
    "}\n";

// Each strand is a lateral offset y(t), in metres, along the beam: a slow
// smooth wander scaled by the beam's spread (pinned to the source by an
// envelope, loosely gathered again at the target) plus jagged piecewise-linear
// jitter in fixed metres, so kinks read the same on short and long beams.
// Strands re-randomise at their own 12-21 Hz, which reads as crackling.
// Distances are corrected by the local slope so steep kinks stay as thin as
// flat runs. Past either end, distance includes the overshoot along the beam,
// which rounds every glow off.
static const char* kFragmentShader =
    "#version 330 core\n"
    "in vec2 coord; in float eyeDist;\n"
    "uniform float beamLen, time, seed, halfWidth, spread, intensity;\n"
    "uniform vec4 coreColor4, glowColor4;\n"
    "out vec4 color;\n"
    LIGHTNING_GLSL_HASH
    "float strandY(float t, uint s, float amp) {\n"
    " float x = t * beamLen;\n"
    " float env = sqrt(smoothstep(0.0, 0.28, t)) * (1.0 - 0.6 * smoothstep(0.72, 1.0, t));\n"
    " float wander = noise(x * 0.22, s, true) + 0.5 * noise(x * 0.6, s + 11u, true);\n"
    " float jitter = 0.10 * noise(x * 2.2, s + 23u, false) + 0.05 * noise(x * 5.5, s + 37u, false)\n"
    "  + 0.025 * noise(x * 13.0, s + 41u, false) + 0.012 * noise(x * 30.0, s + 53u, false);\n"
    " return amp * env * wander + smoothstep(0.0, 0.06, t) * jitter;\n"
    "}\n"
    "void main() {\n"
    " float t = coord.x, y = coord.y;\n"
    " float tc = clamp(t, 0.0, 1.0), over = (t - tc) * beamLen;\n"
    " float px = max(fwidth(y), 1e-5);\n"
    " uint beam = uint(seed * 1000.0);\n"
    " float core = 0.0, glow = 0.0;\n"
    " for (int i = 0; i < 10; ++i) {\n"
    "  uint id = pcg(beam * 16u + uint(i));\n"
    "  bool outlier = i >= 6;\n"
    "  float rate = 12.0 + 9.0 * h01(id);\n"
    "  uint s = pcg(id ^ uint(floor(time * rate + h01(id + 7u))) * 2654435761u);\n"
    "  if (h01(s + 5u) < (outlier ? 0.45 : 0.12)) continue;\n"
    "  float amp = spread * (outlier ? 1.8 : 0.5 + 0.6 * h01(s + 9u));\n"
    "  float dt = 0.0015;\n"
    "  float c = strandY(tc, s, amp), c2 = strandY(tc + dt, s, amp);\n"
    "  float slope = (c2 - c) / (dt * beamLen);\n"
    "  float across = abs(y - c) / sqrt(1.0 + slope * slope);\n"
    "  float d = sqrt(across * across + over * over);\n"
    "  float width = max(outlier ? 0.006 : 0.010, px * 0.8);\n"
    "  float bright = outlier ? 0.35 : 1.0 - 0.08 * float(i);\n"
    "  core += bright * exp(-(d * d) / (width * width));\n"
    "  glow += bright * (0.55 * exp(-(d * d) / (36.0 * width * width)) + 0.10 * exp(-d / 0.18));\n"
    " }\n"
    " float cap = length(vec2(over, y));\n"
    " float taper = mix(1.35, 0.75, tc);\n"
    " float haze = 0.08 * exp(-pow(cap / (spread * 1.4 + 0.1), 2.0));\n"
    " float flicker = 0.85 + 0.15 * h01(uint(floor(time * 30.0)) ^ beam);\n"
    " float edge = 1.0 - smoothstep(0.6, 1.0, cap / halfWidth);\n"
    " vec3 coreColor = coreColor4.rgb, glowColor = glowColor4.rgb;\n"
    " vec3 rgb = coreColor * min(core, 3.0) * 2.2 * taper + glowColor * (glow * taper + haze);\n"
    " float nearFade = smoothstep(1.5, 6.0, eyeDist);\n"
    " color = vec4(rgb * intensity * flicker * edge * nearFade, 0.0);\n"
    "}\n";

// Light on the surroundings: a fullscreen pass per light that rebuilds each
// pixel's world position from this frame's scene depth, measures its distance
// to a capsule (a beam, or a zero-length one at an impact) and brightens it.
// Pass 0 scales the surface by its own colour (blend DST_COLOR, ONE: out =
// dst * (1 + light)); pass 1 adds a faint direct blue so surfaces with little
// blue (EOM's gold floors) still visibly pick it up. A wrapped Lambert term
// from depth-derived normals keeps faces turned away dimmer. It flickers.
// Optional: without scene depth this frame it is skipped.
static const char* kLightVertexShader =
    "#version 330 core\n"
    "void main() {\n"
    " vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);\n"
    " gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";
static const char* kLightFragmentShader =
    "#version 330 core\n"
    "uniform sampler2D sceneDepth;\n"
    "uniform mat4 vpInverse;\n"
    "uniform vec4 viewport4, beamA4, beamB4, eye4, lightColor4;\n"
    "uniform float spread, reach, strength, time, seed, emissive, scale;\n"
    "out vec4 color;\n"
    LIGHTNING_GLSL_HASH
    "void main() {\n"
    " vec2 uv = (gl_FragCoord.xy - viewport4.xy) / viewport4.zw;\n"
    " float d = texture(sceneDepth, uv).r;\n"
    " if (d >= 0.999999) discard;\n"
    " vec4 h = vpInverse * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);\n"
    " vec3 p = h.xyz / h.w;\n"
    " vec3 a = beamA4.xyz, ab = beamB4.xyz - a;\n"
    " float t = clamp(dot(p - a, ab) / max(dot(ab, ab), 1e-6), 0.0, 1.0);\n"
    " vec3 q = a + ab * t;\n"
    " float r = max(length(p - q) - spread * 0.5, 0.0);\n"
    " if (r > reach) discard;\n"
    " vec3 n = cross(dFdx(p), dFdy(p)); float nl = length(n);\n"
    " n = nl > 1e-8 ? n / nl : vec3(0.0, 0.0, 1.0);\n"
    " if (dot(n, eye4.xyz - p) < 0.0) n = -n;\n"
    " vec3 toLight = q - p; float tl = length(toLight);\n"
    " float facing = tl > 1e-4 ? clamp(dot(n, toLight / tl) * 0.55 + 0.45, 0.0, 1.0) : 1.0;\n"
    " float falloff = 1.0 / (1.0 + (r * r) / 2.5);\n"
    " falloff *= 1.0 - smoothstep(reach * 0.55, reach, r);\n"
    " float taper = mix(1.25, 0.8, t);\n"
    " uint beam = uint(seed * 1000.0);\n"
    " float flicker = 0.55 + 0.45 * h01(uint(floor(time * 24.0)) ^ (beam * 7919u));\n"
    " vec3 light = lightColor4.rgb * strength * scale * falloff * facing * taper * flicker;\n"
    " color = vec4(emissive > 0.5 ? light * vec3(0.06, 0.08, 0.12) : light, 0.0);\n"
    "}\n";

// Impact (and, smaller, source) effect: a camera-facing disc at the end of the
// beam holding the impact flash, short crackling arcs radiating from the
// contact point, and a swirling plasma blob. After the beam stops, the flash
// goes out at once but the plasma and a few sparse arcs linger and fade over
// `kResidualSeconds` -- the residual plasma.
static const char* kImpactVertexShader =
    "#version 330 core\n"
    "uniform mat4 vp; uniform vec4 center4, right4, up4; uniform float size;\n"
    "out vec2 local;\n"
    "void main() {\n"
    " const vec2 corners[6] = vec2[6](vec2(-1,-1),vec2(1,-1),vec2(1,1),vec2(-1,-1),vec2(1,1),vec2(-1,1));\n"
    " local = corners[gl_VertexID % 6];\n"
    " vec3 p = center4.xyz + (right4.xyz * local.x + up4.xyz * local.y) * size;\n"
    " gl_Position = vp * vec4(p, 1.0);\n"
    "}\n";
static const char* kImpactFragmentShader =
    "#version 330 core\n"
    "in vec2 local;\n"
    "uniform float time, seed, residual, activeAge, strength, isSource;\n"
    "uniform vec4 coreColor4, glowColor4;\n"
    "out vec4 color;\n"
    LIGHTNING_GLSL_HASH
    "float noise2(vec2 p, uint s) {\n"
    " vec2 i = floor(p), f = p - i; vec2 u = f * f * (3.0 - 2.0 * f);\n"
    " uvec2 c = uvec2(ivec2(i) + 4096);\n"
    " float a = h01(pcg(c.x + pcg(c.y)) ^ s), b = h01(pcg(c.x + 1u + pcg(c.y)) ^ s);\n"
    " float e = h01(pcg(c.x + pcg(c.y + 1u)) ^ s), g = h01(pcg(c.x + 1u + pcg(c.y + 1u)) ^ s);\n"
    " return mix(mix(a, b, u.x), mix(e, g, u.x), u.y);\n"
    "}\n"
    "void main() {\n"
    " float r = length(local);\n"
    " if (r > 1.0) discard;\n"
    " float beamOn = 1.0 - smoothstep(0.0, 0.08, residual);\n"
    " float fade = 1.0 - smoothstep(0.0, isSource > 0.5 ? 0.15 : 0.9, residual);\n"
    " if (fade <= 0.0) discard;\n"
    " float a = atan(local.y, local.x);\n"
    " float px = max(fwidth(r), 1e-4);\n"
    " uint impact = uint(seed * 1000.0) + (isSource > 0.5 ? 50000u : 0u);\n"
    " float burst = 1.0 + 1.5 * exp(-activeAge * 8.0);\n"
    " float flicker = 0.6 + 0.4 * h01(uint(floor(time * 30.0)) ^ (impact * 13u));\n"
    " float flash = (exp(-r * r * 40.0) * 1.6 + exp(-r * r * 7.0) * 0.45) * beamOn * burst * flicker;\n"
    " float core = 0.0, glow = 0.0;\n"
    " int arcs = isSource > 0.5 ? 4 : 8;\n"
    " for (int k = 0; k < 8; ++k) {\n"
    "  if (k >= arcs) break;\n"
    "  uint id = pcg(impact * 97u + uint(k));\n"
    "  float rate = 14.0 + 10.0 * h01(id);\n"
    "  uint s = pcg(id ^ uint(floor(time * rate + h01(id + 3u))) * 2654435761u);\n"
    "  float threshold = beamOn > 0.5 ? 0.25 : 0.7 + 0.25 * residual;\n"
    "  if (h01(s + 1u) < threshold) continue;\n"
    "  float len = mix(0.35, 0.95, h01(s + 2u)) * (beamOn > 0.5 ? 1.0 : 0.55);\n"
    "  if (r > len) continue;\n"
    "  float wiggle = 0.5 * noise(r * 6.0, s + 4u, true) + 0.25 * noise(r * 15.0, s + 5u, false)\n"
    "   + 0.12 * noise(r * 35.0, s + 6u, false);\n"
    "  float aim = h01(s + 3u) * 6.2831853 + wiggle * 1.2;\n"
    "  float da = abs(mod(a - aim + 3.14159265, 6.2831853) - 3.14159265);\n"
    "  float d = r * da;\n"
    "  float w = max(0.012, px * 0.8);\n"
    "  float tip = 1.0 - smoothstep(len * 0.55, len, r);\n"
    "  core += tip * exp(-(d * d) / (w * w));\n"
    "  glow += tip * 0.5 * exp(-(d * d) / (25.0 * w * w));\n"
    " }\n"
    " vec2 q = local * 3.0; float tt = time * 1.7;\n"
    " float n = noise2(q + vec2(tt, -tt * 0.7), impact) * 0.6 +\n"
    "  noise2(q * 2.1 - vec2(tt * 1.3, tt), impact + 7u) * 0.4;\n"
    " float blob = exp(-r * r * 9.0) * smoothstep(0.35, 0.8, n);\n"
    " float plasma = blob * (0.6 * beamOn + 1.1 * (1.0 - beamOn)) * fade * (isSource > 0.5 ? 0.5 : 1.0);\n"
    " float ring = 1.0 - smoothstep(0.85, 1.0, r);\n"
    " vec3 rgb = coreColor4.rgb * (flash + min(core, 2.0) * 1.8 * fade) +\n"
    "  glowColor4.rgb * (glow * fade + plasma * 1.2 + flash * 0.4);\n"
    " color = vec4(rgb * strength * ring, 0.0);\n"
    "}\n";

// Sparks: stateless particles. Each of kSparks slots respawns on its own cycle
// with a new random direction (scattered, biased back toward the caster and
// upward), flies under gravity and draws as a screen-space streak along its
// velocity. Only sparks emitted while the beam was on are drawn, so they keep
// falling for a moment after it stops. Depth testing hides them when they
// fall into the floor.
static constexpr int kSparks = 40;
static const char* kSparkVertexShader =
    "#version 330 core\n"
    "uniform mat4 vp; uniform vec4 center4, beamDir4, viewport4;\n"
    "uniform float time, seed, residual, activeAge, speedScale;\n"
    "out vec2 streak; out float bright;\n"
    LIGHTNING_GLSL_HASH
    "void main() {\n"
    " const vec2 corners[6] = vec2[6](vec2(0,-1),vec2(1,-1),vec2(1,1),vec2(0,-1),vec2(1,1),vec2(0,1));\n"
    " vec2 k = corners[gl_VertexID % 6];\n"
    " uint id = pcg(uint(seed * 1000.0) * 131u + uint(gl_VertexID / 6));\n"
    " bright = 0.0; streak = vec2(0.0); gl_Position = vec4(2.0, 2.0, 2.0, 1.0);\n"
    " float life = 0.25 + 0.45 * h01(id);\n"
    " float cycle = time / life + h01(id + 1u);\n"
    " float u = fract(cycle), age = u * life;\n"
    " uint s = pcg(id ^ uint(floor(cycle)) * 2654435761u);\n"
    " if (age < residual || age > activeAge || h01(s + 9u) < 0.3) return;\n"
    " float z = h01(s + 2u) * 2.0 - 1.0, phi = h01(s + 3u) * 6.2831853;\n"
    " vec3 dir = vec3(sqrt(1.0 - z * z) * cos(phi), sqrt(1.0 - z * z) * sin(phi), z);\n"
    " dir = normalize(dir - beamDir4.xyz * 0.6 + vec3(0.0, 0.0, 0.5));\n"
    " float speed = mix(1.5, 5.0, h01(s + 4u)) * speedScale;\n"
    " vec3 gravity = vec3(0.0, 0.0, -9.8);\n"
    " vec3 velocity = dir * speed + gravity * age;\n"
    " vec3 head = center4.xyz + dir * speed * age + 0.5 * gravity * age * age;\n"
    " vec3 tail = head - velocity * 0.035;\n"
    " vec4 ch = vp * vec4(head, 1.0), ct = vp * vec4(tail, 1.0);\n"
    " if (ch.w <= 0.05 || ct.w <= 0.05) return;\n"
    " vec2 half_ = 0.5 * viewport4.zw;\n"
    " vec2 sh = ch.xy / ch.w * half_, st = ct.xy / ct.w * half_;\n"
    " vec2 axis = sh - st; float al = length(axis);\n"
    " vec2 along = al > 1e-3 ? axis / al : vec2(1.0, 0.0);\n"
    " vec2 across = vec2(-along.y, along.x);\n"
    " const float halfWidth = 1.5;\n"
    " vec2 pixel = mix(st - along * halfWidth, sh + along * halfWidth, k.x) + across * k.y * halfWidth;\n"
    " vec4 clip = mix(ct, ch, k.x);\n"
    " gl_Position = vec4(pixel / half_ * clip.w, clip.z, clip.w);\n"
    " streak = k;\n"
    " bright = pow(1.0 - u, 1.5) * (0.6 + 0.8 * h01(s + 5u));\n"
    "}\n";
static const char* kSparkFragmentShader =
    "#version 330 core\n"
    "in vec2 streak; in float bright;\n"
    "uniform vec4 coreColor4, glowColor4; uniform float strength;\n"
    "out vec4 color;\n"
    "void main() {\n"
    " float across = 1.0 - abs(streak.y); float c = across * across;\n"
    " float head = mix(0.4, 1.0, streak.x);\n"
    " vec3 rgb = mix(glowColor4.rgb, coreColor4.rgb, c) * c * head * bright * 2.5;\n"
    " color = vec4(rgb * strength, 0.0);\n"
    "}\n";

// Sky-to-ground strike (Call Lightning). Drawn on the beam ribbon (vertex
// shader above) from the ground point (t = 0) straight up (t = 1): a jagged
// main channel whose wander grows with height and is fixed per strike, a
// braided column of thinner strands around it, and forking branches that
// leave the channel and run down and out. The channel's fine jitter and the
// branches re-form on every return stroke; `pulse` carries the stroke
// brightness (a flash that decays, over a dim continuing current).
static const char* kStrikeFragmentShader =
    "#version 330 core\n"
    "in vec2 coord; in float eyeDist;\n"
    "uniform float beamLen, time, seed, halfWidth, intensity, pulse, stroke;\n"
    "uniform vec4 coreColor4, glowColor4;\n"
    "out vec4 color;\n"
    LIGHTNING_GLSL_HASH
    "uint strikeSeed, strokeSeed;\n"
    "float channel(float t) {\n"
    " float x = t * beamLen;\n"
    " float wander = noise(x * 0.08, strikeSeed, true) + 0.5 * noise(x * 0.25, strikeSeed + 1u, true);\n"
    " float jag = 0.35 * noise(x * 0.7, strokeSeed, false) + 0.18 * noise(x * 1.8, strokeSeed + 2u, false)\n"
    "  + 0.08 * noise(x * 4.5, strokeSeed + 3u, false) + 0.03 * noise(x * 11.0, strokeSeed + 4u, false);\n"
    " return smoothstep(0.0, 0.03, t) * ((0.25 + 2.2 * t) * 0.6 * wander + jag * (0.6 + t));\n"
    "}\n"
    "float line(float y, float c, float c2, float dt, float over, float width) {\n"
    " float slope = (c2 - c) / (dt * beamLen);\n"
    " float across = abs(y - c) / sqrt(1.0 + slope * slope);\n"
    " float d = sqrt(across * across + over * over);\n"
    " return d;\n"
    "}\n"
    "void main() {\n"
    " float t = coord.x, y = coord.y;\n"
    " float tc = clamp(t, 0.0, 1.0), over = (t - tc) * beamLen;\n"
    " float px = max(fwidth(y), 1e-5);\n"
    " strikeSeed = pcg(uint(seed * 1000.0));\n"
    " strokeSeed = pcg(strikeSeed ^ uint(stroke) * 2654435761u);\n"
    " const float dt = 0.0015;\n"
    " float main0 = channel(tc), main1 = channel(tc + dt);\n"
    " float core = 0.0, glow = 0.0;\n"
    // Main channel: thick, brightest.
    " float d = line(y, main0, main1, dt, over, 0.0);\n"
    " float w = max(0.045, px * 0.9);\n"
    " core += 1.6 * exp(-(d * d) / (w * w));\n"
    " glow += 0.9 * exp(-(d * d) / (49.0 * w * w)) + 0.22 * exp(-d / 0.7);\n"
    // Braided column: thinner strands wrapping the channel, wider higher up.
    " for (int i = 0; i < 5; ++i) {\n"
    "  uint id = pcg(strikeSeed * 16u + uint(i));\n"
    "  uint s = pcg(id ^ uint(floor(time * (14.0 + 8.0 * h01(id)))) * 2246822519u);\n"
    "  if (h01(s + 1u) < 0.25) continue;\n"
    "  float reach = 0.1 + 0.55 * tc;\n"
    "  float o0 = reach * (noise(tc * beamLen * 0.5, s, true) + 0.4 * noise(tc * beamLen * 3.0, s + 5u, false));\n"
    "  float o1 = reach * (noise((tc + dt) * beamLen * 0.5, s, true) + 0.4 * noise((tc + dt) * beamLen * 3.0, s + 5u, false));\n"
    "  float ds = line(y, main0 + o0, main1 + o1, dt, over, 0.0);\n"
    "  float ws = max(0.014, px * 0.8);\n"
    "  core += 0.6 * exp(-(ds * ds) / (ws * ws));\n"
    "  glow += 0.25 * exp(-(ds * ds) / (36.0 * ws * ws));\n"
    " }\n"
    // Branches: leave the channel at tb and run down and out for len, fading.
    " for (int b = 0; b < 7; ++b) {\n"
    "  uint s = pcg(strokeSeed * 31u + uint(b));\n"
    "  if (h01(s) < 0.3) continue;\n"
    "  float tb = mix(0.2, 0.95, h01(s + 1u));\n"
    "  float span = mix(0.06, 0.25, h01(s + 2u));\n"
    "  if (tc > tb || tc < tb - span) continue;\n"
    "  float side = h01(s + 3u) < 0.5 ? -1.0 : 1.0;\n"
    "  float lean = mix(0.35, 1.1, h01(s + 4u));\n"
    "  float start = channel(tb);\n"
    "  float down0 = (tb - tc) * beamLen, down1 = (tb - tc - dt) * beamLen;\n"
    "  float j0 = 0.25 * noise(down0 * 1.6, s + 5u, false) + 0.1 * noise(down0 * 5.0, s + 6u, false);\n"
    "  float j1 = 0.25 * noise(down1 * 1.6, s + 5u, false) + 0.1 * noise(down1 * 5.0, s + 6u, false);\n"
    "  float c0 = start + side * lean * down0 + j0 * smoothstep(0.0, 0.5, down0);\n"
    "  float c1 = start + side * lean * down1 + j1 * smoothstep(0.0, 0.5, down1);\n"
    "  float db = line(y, c0, c1, dt, 0.0, 0.0);\n"
    "  float fadeTip = 1.0 - smoothstep(0.4, 1.0, (tb - tc) / span);\n"
    "  float wb = max(0.012, px * 0.8);\n"
    "  core += 0.8 * fadeTip * exp(-(db * db) / (wb * wb));\n"
    "  glow += 0.35 * fadeTip * exp(-(db * db) / (36.0 * wb * wb));\n"
    " }\n"
    " float cap = length(vec2(over, y - main0));\n"
    " float haze = 0.06 * exp(-pow(cap / 1.2, 2.0));\n"
    " float ground = 1.0 + 0.4 * exp(-tc * beamLen / 1.0);\n"
    " float top = 1.0 - 0.5 * smoothstep(0.9, 1.0, tc);\n"
    " float edge = 1.0 - smoothstep(0.6, 1.0, abs(y) / halfWidth);\n"
    " vec3 rgb = coreColor4.rgb * min(core, 3.0) * 2.4 + glowColor4.rgb * (glow + haze);\n"
    " float nearFade = smoothstep(2.0, 8.0, eyeDist);\n"
    " color = vec4(rgb * ground * top * edge * nearFade * pulse * intensity, 0.0);\n"
    "}\n";

// Strike ground effect: a decal projected onto the real surfaces around the
// contact point (fullscreen, from this frame's scene depth, so it follows the
// floor without z-fighting). Jagged arcs crawl out radially and fork, an
// expanding shock ring marks the first stroke, and a plasma pool lingers and
// fades after the strike ends.
static const char* kGroundFragmentShader =
    "#version 330 core\n"
    "uniform sampler2D sceneDepth;\n"
    "uniform mat4 vpInverse;\n"
    "uniform vec4 viewport4, center4, coreColor4, glowColor4;\n"
    "uniform float radius, time, seed, stroke, pulse, residual, activeAge, intensity;\n"
    "out vec4 color;\n"
    LIGHTNING_GLSL_HASH
    "float noise2(vec2 p, uint s) {\n"
    " vec2 i = floor(p), f = p - i; vec2 u = f * f * (3.0 - 2.0 * f);\n"
    " uvec2 c = uvec2(ivec2(i) + 4096);\n"
    " float a = h01(pcg(c.x + pcg(c.y)) ^ s), b = h01(pcg(c.x + 1u + pcg(c.y)) ^ s);\n"
    " float e = h01(pcg(c.x + pcg(c.y + 1u)) ^ s), g = h01(pcg(c.x + 1u + pcg(c.y + 1u)) ^ s);\n"
    " return mix(mix(a, b, u.x), mix(e, g, u.x), u.y);\n"
    "}\n"
    "float arc(float r, float a, float aim, float len, float startR, uint s, float px, out float glowOut) {\n"
    " glowOut = 0.0;\n"
    " if (r > len || r < startR) return 0.0;\n"
    " float wiggle = 0.45 * noise(r * 1.3, s + 4u, true) + 0.22 * noise(r * 4.0, s + 5u, false)\n"
    "  + 0.1 * noise(r * 11.0, s + 6u, false);\n"
    " float ang = aim + wiggle / max(r, 0.6);\n"
    " float da = abs(mod(a - ang + 3.14159265, 6.2831853) - 3.14159265);\n"
    " float d = r * da;\n"
    " float w = max(0.03, px * 0.9);\n"
    " float tip = 1.0 - smoothstep(len * 0.5, len, r);\n"
    " glowOut = tip * 0.45 * exp(-(d * d) / (25.0 * w * w));\n"
    " return tip * exp(-(d * d) / (w * w));\n"
    "}\n"
    "void main() {\n"
    " vec2 uv = (gl_FragCoord.xy - viewport4.xy) / viewport4.zw;\n"
    " float depth = texture(sceneDepth, uv).r;\n"
    " if (depth >= 0.999999) discard;\n"
    " vec4 h = vpInverse * vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);\n"
    " vec3 p = h.xyz / h.w;\n"
    " vec2 v = p.xy - center4.xy; float r = length(v);\n"
    " float dz = p.z - center4.z;\n"
    " if (r > radius || dz > 1.5 || dz < -1.0) discard;\n"
    " vec3 n = cross(dFdx(p), dFdy(p)); float nl = length(n);\n"
    " n = nl > 1e-8 ? n / nl : vec3(0.0, 0.0, 1.0);\n"
    " float flat_ = mix(0.35, 1.0, smoothstep(0.3, 0.8, abs(n.z)));\n"
    " float heightFade = 1.0 - smoothstep(0.4, 1.5, abs(dz));\n"
    " float a = atan(v.y, v.x);\n"
    " float px = max(fwidth(r), 1e-4);\n"
    " uint strike = pcg(uint(seed * 1000.0));\n"
    " uint strokeSeed = pcg(strike ^ uint(stroke) * 2654435761u);\n"
    " float beamOn = 1.0 - smoothstep(0.0, 0.1, residual);\n"
    " float fade = 1.0 - smoothstep(0.0, 1.6, residual);\n"
    " float core = 0.0, glow = 0.0;\n"
    " for (int k = 0; k < 10; ++k) {\n"
    "  uint id = pcg(strike * 97u + uint(k));\n"
    "  uint s = pcg(id ^ strokeSeed ^ uint(floor(time * (16.0 + 10.0 * h01(id)))) * 2246822519u);\n"
    "  float keep = beamOn > 0.5 ? 0.3 : 0.75 + 0.2 * residual;\n"
    "  if (h01(s + 1u) < keep) continue;\n"
    "  float len = radius * mix(0.35, 0.95, h01(s + 2u)) * (beamOn > 0.5 ? 1.0 : 0.5);\n"
    "  float aim = h01(s + 3u) * 6.2831853;\n"
    "  float g1; core += arc(r, a, aim, len, 0.0, s, px, g1); glow += g1;\n"
    "  float forkR = len * mix(0.3, 0.6, h01(s + 7u));\n"
    "  float forkAim = aim + (h01(s + 8u) < 0.5 ? -1.0 : 1.0) * mix(0.25, 0.6, h01(s + 9u));\n"
    "  float g2; core += 0.7 * arc(r, a, forkAim, len * 0.85, forkR, s + 11u, px, g2); glow += 0.7 * g2;\n"
    " }\n"
    " float ringR = activeAge * 16.0;\n"
    " float ring = activeAge < 0.4 ? exp(-pow((r - ringR) / 0.35, 2.0)) * (1.0 - activeAge / 0.4) : 0.0;\n"
    " float n1 = noise2(v * 1.4 + vec2(time * 0.9, -time * 0.6), strike) * 0.6 +\n"
    "  noise2(v * 3.1 - vec2(time * 1.3, time), strike + 5u) * 0.4;\n"
    " float pool = exp(-pow(r / 1.4, 2.0)) * (0.5 + 0.8 * smoothstep(0.35, 0.8, n1));\n"
    " float plasma = pool * (0.45 * beamOn * min(pulse, 1.0) + 0.6 * (1.0 - beamOn)) * fade;\n"
    " vec3 rgb = coreColor4.rgb * (min(core, 2.0) * 2.0 * pulse * fade + ring * 1.2) +\n"
    "  glowColor4.rgb * (glow * fade + plasma + ring * 0.8);\n"
    " color = vec4(rgb * flat_ * heightFade * intensity, 0.0);\n"
    "}\n";

// Storm cloud the strike is summoned from: a horizontal disc at the top of
// the bolt, seen from below. A dark spiral of cloud (alpha-blended,
// premultiplied) turns around a bright eye the bolt drops out of; each return
// stroke lights it from inside, with patches of sheet lightning. It forms on
// first contact and fades with the residual plasma.
static constexpr float kStrikeCloudHeight = 18.0f;
static const char* kCloudVertexShader =
    "#version 330 core\n"
    "uniform mat4 vp; uniform vec4 center4; uniform float radius;\n"
    "out vec2 local;\n"
    "void main() {\n"
    " const vec2 corners[6] = vec2[6](vec2(-1,-1),vec2(1,-1),vec2(1,1),vec2(-1,-1),vec2(1,1),vec2(-1,1));\n"
    " local = corners[gl_VertexID % 6];\n"
    " gl_Position = vp * vec4(center4.xyz + vec3(local * radius, 0.0), 1.0);\n"
    "}\n";
static const char* kCloudFragmentShader =
    "#version 330 core\n"
    "in vec2 local;\n"
    "uniform float time, seed, stroke, pulse, grow, fade, intensity;\n"
    "uniform vec4 coreColor4, glowColor4;\n"
    "out vec4 color;\n"
    LIGHTNING_GLSL_HASH
    "float noise2(vec2 p, uint s) {\n"
    " vec2 i = floor(p), f = p - i; vec2 u = f * f * (3.0 - 2.0 * f);\n"
    " uvec2 c = uvec2(ivec2(i) + 4096);\n"
    " float a = h01(pcg(c.x + pcg(c.y)) ^ s), b = h01(pcg(c.x + 1u + pcg(c.y)) ^ s);\n"
    " float e = h01(pcg(c.x + pcg(c.y + 1u)) ^ s), g = h01(pcg(c.x + 1u + pcg(c.y + 1u)) ^ s);\n"
    " return mix(mix(a, b, u.x), mix(e, g, u.x), u.y);\n"
    "}\n"
    "void main() {\n"
    " float r = length(local);\n"
    " if (r > 1.0) discard;\n"
    " uint cloud = pcg(uint(seed * 1000.0));\n"
    " float a = atan(local.y, local.x);\n"
    // Spiral: the angle twists with log-radius and turns faster near the eye.
    " float swirl = a - 2.4 * log(r + 0.04) + time * (0.35 + 0.9 / (r + 0.35));\n"
    " vec2 q = vec2(cos(swirl), sin(swirl)) * (0.6 + r * 2.6);\n"
    " float d = noise2(q * 1.3, cloud) * 0.55 + noise2(q * 2.9, cloud + 3u) * 0.3 +\n"
    "  noise2(q * 6.1 + time * 0.4, cloud + 7u) * 0.15;\n"
    " float rim = 1.0 - smoothstep(0.6, 1.0, r);\n"
    " float eye = exp(-pow(r / 0.11, 2.0));\n"
    " float body = smoothstep(0.08, 0.22, r);\n"
    " float alpha = clamp(d * 1.5 - 0.3, 0.0, 1.0) * rim * body * grow * fade * 0.9;\n"
    " vec3 cloudColor = mix(vec3(0.04, 0.05, 0.08), vec3(0.16, 0.19, 0.28), d);\n"
    " uint strokeSeed = pcg(cloud ^ uint(stroke) * 2654435761u);\n"
    " float sheet = smoothstep(0.62, 0.85, noise2(q * 0.9, strokeSeed)) * pulse * 0.6;\n"
    " float inner = pulse * (0.9 * exp(-pow(r / 0.35, 2.0)) + 0.25 * d * exp(-r * 2.0));\n"
    " vec3 light = glowColor4.rgb * (inner + sheet) * alpha + coreColor4.rgb * eye * (0.5 + pulse) * grow * fade;\n"
    " color = vec4((cloudColor * alpha + light) * intensity, alpha);\n"
    "}\n";

// Return strokes of one strike: the first at contact, then every 70-270 ms
// while the native effect lasts. Each is a flash that decays, over a dim
// continuing current. Returns the current stroke's index and brightness.
static float strike_pulse(const Strike& s, double now, unsigned& strokeIndex) {
    const double age = std::max(0.0, now - s.first);
    double at = 0.0;
    unsigned k = 0;
    auto hash = [](unsigned v) {
        v = v * 747796405u + 2891336453u;
        v = ((v >> ((v >> 28u) + 4u)) ^ v) * 277803737u;
        return (float)(((v >> 22u) ^ v) & 0xFFFFFFu) / 16777215.0f;
    };
    while (k < 64) {
        const double next = at + 0.07 + 0.2 * hash(s.seed + k * 31u);
        if (next > age) break;
        at = next; ++k;
    }
    strokeIndex = k;
    const float since = (float)(age - at);
    const float flash = k == 0 ? 1.6f : 0.7f + 0.6f * hash(s.seed + k * 17u + 5u);
    return 0.22f + flash * std::exp(-since * 14.0f);
}
static double strike_clock() { return now_seconds(); }

// The rain pass's entry points and StateGuard are shared (see
// weather_particles::ensure_entry_points), so both passes restore alike.
using weather_particles::genVaos;
using weather_particles::bindVao;
using weather_particles::drawArrays;
using weather_particles::useProgram;
static GLuint lightProgram = 0, impactProgram = 0, sparkProgram = 0, depthSampler = 0;
static GLuint strikeProgram = 0, groundProgram = 0, cloudProgram = 0;

// Impact points persist across frames so the plasma can outlive the beam.
static constexpr float kResidualSeconds = 1.0f;
struct Impact {
    float p[3] = {}, dir[3] = {};
    double first = 0.0, last = 0.0;
    bool live = false, source = false;
};
static constexpr int kMaxImpacts = 24;
static Impact impacts[kMaxImpacts];

static void note_impact(const float* p, const float* dir, bool source, double now) {
    int slot = -1;
    for (int i = 0; i < kMaxImpacts; ++i) {
        Impact& m = impacts[i];
        if (!m.live || m.source != source) continue;
        float d2 = 0;
        for (int k = 0; k < 3; ++k) d2 += (m.p[k] - p[k]) * (m.p[k] - p[k]);
        if (d2 < 0.8f * 0.8f) { slot = i; break; }
    }
    if (slot < 0) {
        for (int i = 0; i < kMaxImpacts && slot < 0; ++i) if (!impacts[i].live) slot = i;
        if (slot < 0) return;
        impacts[slot].first = now;
    }
    Impact& m = impacts[slot];
    for (int k = 0; k < 3; ++k) { m.p[k] = p[k]; m.dir[k] = dir[k]; }
    m.last = now; m.live = true; m.source = source;
}
static void expire_impacts(double now) {
    for (Impact& m : impacts)
        if (m.live && now - m.last > kResidualSeconds) m.live = false;
}

static GLuint build_program(const char* vs, const char* fs, const char* label) {
    auto create=(CreateShader_t)glew_or_dlsym(eng::GlewCreateShader,"glCreateShader");
    auto compile=(CompileShader_t)glew_or_dlsym(eng::GlewCompileShader,"glCompileShader");
    auto shaderStatus=(GetShaderiv_t)glew_or_dlsym(eng::GlewGetShaderiv,"glGetShaderiv");
    auto createProgram=(CreateProgram_t)glew_or_dlsym(eng::GlewCreateProgram,"glCreateProgram");
    auto attach=(AttachShader_t)glew_or_dlsym(eng::GlewAttachShader,"glAttachShader");
    auto link=(LinkProgram_t)glew_or_dlsym(eng::GlewLinkProgram,"glLinkProgram");
    auto programStatus=(GetProgramiv_t)glew_or_dlsym(eng::GlewGetProgramiv,"glGetProgramiv");
    auto source=g_realShaderSource ? g_realShaderSource :
        (ShaderSource_t)dlsym(RTLD_NEXT,"glShaderSource");
    using DeleteObject=void (*)(GLuint);
    using InfoLog=void (*)(GLuint,GLsizei,GLsizei*,char*);
    auto deleteShader=(DeleteObject)dlsym(RTLD_NEXT,"glDeleteShader");
    auto deleteProgram=(DeleteObject)dlsym(RTLD_NEXT,"glDeleteProgram");
    auto shaderLog=(InfoLog)dlsym(RTLD_NEXT,"glGetShaderInfoLog");
    auto programLog=(InfoLog)dlsym(RTLD_NEXT,"glGetProgramInfoLog");
    if (!create || !compile || !shaderStatus || !createProgram || !attach || !link ||
        !programStatus || !source || !deleteShader || !deleteProgram) return 0;
    GLuint shaders[2]={create(GL_VERTEX_SHADER),create(GL_FRAGMENT_SHADER)};
    const char* sources[2]={vs,fs};
    bool okay=true;
    for(int i=0;i<2;++i) {
        source(shaders[i],1,&sources[i],nullptr); compile(shaders[i]);
        GLint compiled=0; shaderStatus(shaders[i],GL_COMPILE_STATUS,&compiled);
        if (!compiled) {
            char log[2048]={}; if(shaderLog) shaderLog(shaders[i],sizeof(log)-1,nullptr,log);
            fprintf(stderr,"[shadowmap][lightning] %s shader %d failed: %s\n",label,i,log);
            okay=false;
        }
    }
    GLuint built=0;
    if(okay) {
        built=createProgram(); attach(built,shaders[0]); attach(built,shaders[1]); link(built);
        GLint linked=0; programStatus(built,GL_LINK_STATUS,&linked);
        if(!linked) {
            char log[2048]={}; if(programLog) programLog(built,sizeof(log)-1,nullptr,log);
            fprintf(stderr,"[shadowmap][lightning] %s link failed: %s\n",label,log);
            deleteProgram(built); built=0;
        }
    }
    for(GLuint s:shaders) deleteShader(s);
    return built;
}

static bool ensure_program() {
    if (program && vao) return true;
    if (failed) return false;
    bool okay = weather_particles::ensure_entry_points();
    if (okay) program = build_program(kVertexShader, kFragmentShader, "beam");
    okay = okay && program;
    if (okay) { genVaos(1,&vao); okay=vao!=0; }
    if (okay) {
        // Light, impacts and sparks are optional: a failure leaves the beams.
        lightProgram = build_program(kLightVertexShader, kLightFragmentShader, "light");
        impactProgram = build_program(kImpactVertexShader, kImpactFragmentShader, "impact");
        sparkProgram = build_program(kSparkVertexShader, kSparkFragmentShader, "spark");
        strikeProgram = build_program(kVertexShader, kStrikeFragmentShader, "strike");
        groundProgram = build_program(kLightVertexShader, kGroundFragmentShader, "strike ground");
        cloudProgram = build_program(kCloudVertexShader, kCloudFragmentShader, "storm cloud");
        weather_particles::genSamplers(1,&depthSampler);
        if (depthSampler) {
            weather_particles::samplerParam(depthSampler,GL_TEXTURE_COMPARE_MODE,GL_NONE);
            weather_particles::samplerParam(depthSampler,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
            weather_particles::samplerParam(depthSampler,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            weather_particles::samplerParam(depthSampler,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
            weather_particles::samplerParam(depthSampler,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        }
        if (!depthSampler) { lightProgram = 0; groundProgram = 0; }
    }
    failed=!okay;
    fprintf(stderr,"[shadowmap][lightning] procedural beams %s; light %s; impacts %s; sparks %s; strikes %s; strike ground %s; cloud %s\n",
            okay?"ready":"failed; native beams retained", lightProgram?"ready":"unavailable",
            impactProgram?"ready":"unavailable", sparkProgram?"ready":"unavailable",
            strikeProgram?"ready":"unavailable", groundProgram?"ready":"unavailable",
            cloudProgram?"ready":"unavailable");
    return okay;
}

static float beam_length(const Beam& beam) {
    float length = 0;
    for (int k = 0; k < 3; ++k) length += (beam.b[k] - beam.a[k]) * (beam.b[k] - beam.a[k]);
    return std::sqrt(length);
}
static float beam_spread(float length) { return std::max(0.12f, std::min(0.06f * length, 0.7f)); }

static void set_blend(GLenum src, GLenum dst) {
    if (weather_particles::blendIndexed && weather_particles::equationIndexed) {
        weather_particles::equationIndexed(0, GL_FUNC_ADD, GL_FUNC_ADD);
        weather_particles::blendIndexed(0, src, dst, GL_ZERO, GL_ONE);
    } else {
        gl::BlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
        gl::BlendFuncSeparate(src, dst, GL_ZERO, GL_ONE);
    }
}

static void draw(void* scene) {
    const auto& camera = g_shadowFrameContext;
    if (!g_lightningEnabled) { status = "off"; return; }
    if (!camera.valid || scene != g_areaScene || camera.scene != scene) return;
    if (!gl::GetIntegerv || !gl::GetBooleanv || !gl::IsEnabled || !gl::ActiveTexture ||
        !gl::BindTexture || !gl::UniformMatrix4fv || !gl::Uniform1f || !gl::Uniform4f ||
        !gl::Uniform1i || !gl::GetUniformLocation || !gl::Enable || !gl::Disable ||
        !gl::BlendFuncSeparate || !gl::BlendEquationSeparate || !gl::DepthFunc ||
        !gl::DepthMask) {
        status = "missing GL state API"; return;
    }
    // Compile even before the first beam, so the first cast is already private.
    if (!ensure_program()) { status = "unavailable; native beams retained"; return; }
    const double now = now_seconds();
    const bool haveBeams = beamSerial == camera.serial && beamCount > 0;
    if (haveBeams) {
        for (int i = 0; i < beamCount; ++i) {
            const Beam& beam = beams[i];
            const float length = std::max(beam_length(beam), 1e-4f);
            float dir[3], back[3];
            for (int k = 0; k < 3; ++k) {
                dir[k] = (beam.b[k] - beam.a[k]) / length;
                back[k] = -dir[k];
            }
            note_impact(beam.b, dir, false, now);
            note_impact(beam.a, back, true, now);
        }
    }
    expire_impacts(now);
    bool anyImpact = false;
    for (const Impact& m : impacts) anyImpact = anyImpact || m.live;
    bool anyStrike = false;
    for (Strike& st : strikes) {
        if (st.live && now - st.last > kStrikeResidualSeconds) st.live = false;
        anyStrike = anyStrike || st.live;
    }
    if (!haveBeams && !anyImpact && !anyStrike) { status = "ready (no beams)"; return; }
    GLint viewport[4] = {}; gl::GetIntegerv(GL_VIEWPORT, viewport);
    if (std::memcmp(viewport, camera.viewport, sizeof(viewport)) != 0) {
        status = "viewport mismatch"; return;
    }
    nwn_core::OwnedPass owned;
    weather_particles::StateGuard restore;
    const float time = (float)std::fmod(now, 4096.0);
    const float intensity = std::max(0.0f, std::min(g_lightningIntensity, 4.0f));
    const float lightStrength = std::max(0.0f, std::min(g_lightningLight, 3.0f));
    for (GLenum cap : weather_particles::StateGuard::caps) gl::Disable(cap);
    if (weather_particles::depthRangeSet) weather_particles::depthRangeSet(0.0, 1.0);
    weather_particles::enableIndexed(GL_BLEND, 0);
    weather_particles::colorMaskIndexed(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
    gl::DepthMask(GL_FALSE);
    bindVao(vao);
    float vp[16]; mat4_mul(camera.projection, camera.view, vp);
    const float kCore[3] = {0.85f, 0.92f, 1.0f}, kGlow[3] = {0.16f, 0.40f, 1.0f};
    auto impact_state = [&](const Impact& m, float& residual, float& activeAge) {
        residual = (float)std::max(0.0, now - m.last);
        activeAge = (float)std::max(0.0, m.last - m.first) + residual;
    };
    // Strike state: residual = time since the native strip was last seen,
    // activeAge = time since first contact, pulse = return-stroke brightness.
    // The bolt rises to the storm cloud it is summoned from.
    auto strike_height = [&](const Strike&) { return kStrikeCloudHeight; };
    struct StrikeState { float residual, activeAge, pulse; unsigned stroke; bool on; };
    auto strike_state = [&](const Strike& st) {
        StrikeState out{};
        out.residual = (float)std::max(0.0, now - st.last);
        out.activeAge = (float)std::max(0.0, now - st.first);
        out.on = out.residual < 0.1f;
        out.pulse = out.on ? strike_pulse(st, now, out.stroke) : 0.0f;
        if (!out.on) strike_pulse(st, st.last, out.stroke);
        return out;
    };

    // 1. Light on the surroundings (before anything additive, which must not
    //    be tinted by the surface colour): beams, then impact points.
    const bool sceneDepthReady = lightProgram && g_sceneDepthTex &&
        g_sceneDepthSerial == camera.serial &&
        g_sceneDepthW == viewport[2] && g_sceneDepthH == viewport[3];
    bool lit = false;
    if (sceneDepthReady && lightStrength > 0.0f && intensity > 0.0f) {
        useProgram(lightProgram); g_curProgram = lightProgram;
        auto loc = [](const char* name) { return gl::GetUniformLocation(lightProgram, name); };
        gl::ActiveTexture(GL_TEXTURE0); gl::BindTexture(GL_TEXTURE_2D, g_sceneDepthTex);
        weather_particles::bindSampler(0, depthSampler);
        gl::Uniform1i(loc("sceneDepth"), 0);
        gl::UniformMatrix4fv(loc("vpInverse"), 1, GL_FALSE, camera.viewProjectionInverse);
        gl::Uniform4f(loc("viewport4"), (float)viewport[0], (float)viewport[1],
                      (float)viewport[2], (float)viewport[3]);
        gl::Uniform4f(loc("eye4"), camera.eye.x, camera.eye.y, camera.eye.z, 1.0f);
        gl::Uniform4f(loc("lightColor4"), 0.35f, 0.55f, 1.0f, 1.0f);
        gl::Uniform1f(loc("strength"), 2.6f * lightStrength * std::min(intensity, 1.5f));
        gl::Uniform1f(loc("time"), time);
        gl::Disable(GL_DEPTH_TEST);
        for (int pass = 0; pass < 2; ++pass) {
            gl::Uniform1f(loc("emissive"), (float)pass);
            if (pass == 0) set_blend(0x0306 /* GL_DST_COLOR */, GL_ONE);
            else set_blend(GL_ONE, GL_ONE);
            if (haveBeams) for (int i = 0; i < beamCount; ++i) {
                const Beam& beam = beams[i];
                const float length = beam_length(beam);
                gl::Uniform4f(loc("beamA4"), beam.a[0], beam.a[1], beam.a[2], 1.0f);
                gl::Uniform4f(loc("beamB4"), beam.b[0], beam.b[1], beam.b[2], 1.0f);
                gl::Uniform1f(loc("spread"), beam_spread(length));
                gl::Uniform1f(loc("reach"), std::max(4.0f, std::min(0.5f * length, 8.0f)));
                gl::Uniform1f(loc("seed"), (float)(i + 1) * 1.618f);
                gl::Uniform1f(loc("scale"), 1.0f);
                drawArrays(0x0004 /* GL_TRIANGLES */, 0, 3);
            }
            // Impact points: a flash while the beam is on, then the fading plasma.
            for (int i = 0; i < kMaxImpacts; ++i) {
                const Impact& m = impacts[i];
                if (!m.live || m.source) continue;
                float residual, activeAge; impact_state(m, residual, activeAge);
                const float on = residual < 0.08f ? 1.0f : 0.0f;
                const float burst = 1.0f + 1.5f * std::exp(-activeAge * 8.0f);
                const float scale = on * 0.9f * burst +
                    (1.0f - on) * 0.7f * std::max(0.0f, 1.0f - residual / kResidualSeconds);
                if (scale <= 0.0f) continue;
                gl::Uniform4f(loc("beamA4"), m.p[0], m.p[1], m.p[2], 1.0f);
                gl::Uniform4f(loc("beamB4"), m.p[0], m.p[1], m.p[2], 1.0f);
                gl::Uniform1f(loc("spread"), 0.0f);
                gl::Uniform1f(loc("reach"), 3.0f);
                gl::Uniform1f(loc("seed"), 40.0f + (float)i * 1.618f);
                gl::Uniform1f(loc("scale"), scale);
                drawArrays(0x0004 /* GL_TRIANGLES */, 0, 3);
            }
            // Strikes: the whole column while a stroke is on, and a strong
            // ground light that fades with the residual plasma.
            for (int i = 0; i < kMaxStrikes; ++i) {
                const Strike& st = strikes[i];
                if (!st.live) continue;
                const StrikeState ss = strike_state(st);
                const float* g = st.ground;
                if (ss.on) {
                    gl::Uniform4f(loc("beamA4"), g[0], g[1], g[2] + 0.5f, 1.0f);
                    gl::Uniform4f(loc("beamB4"), g[0], g[1], g[2] + strike_height(st), 1.0f);
                    gl::Uniform1f(loc("spread"), 0.6f);
                    gl::Uniform1f(loc("reach"), 9.0f);
                    gl::Uniform1f(loc("seed"), 80.0f + (float)i * 1.618f);
                    gl::Uniform1f(loc("scale"), 0.45f * ss.pulse);
                    drawArrays(0x0004 /* GL_TRIANGLES */, 0, 3);
                }
                const float groundScale = ss.on ? 0.55f * std::min(ss.pulse, 1.2f) :
                    0.45f * std::max(0.0f, 1.0f - ss.residual / kStrikeResidualSeconds);
                if (groundScale <= 0.0f) continue;
                gl::Uniform4f(loc("beamA4"), g[0], g[1], g[2] + 0.4f, 1.0f);
                gl::Uniform4f(loc("beamB4"), g[0], g[1], g[2] + 0.4f, 1.0f);
                gl::Uniform1f(loc("spread"), 0.0f);
                gl::Uniform1f(loc("reach"), 8.0f);
                gl::Uniform1f(loc("seed"), 90.0f + (float)i * 1.618f);
                gl::Uniform1f(loc("scale"), groundScale);
                drawArrays(0x0004 /* GL_TRIANGLES */, 0, 3);
            }
        }
        lit = true;
    }

    // 2. The strands.
    gl::Enable(GL_DEPTH_TEST); gl::DepthFunc(GL_LEQUAL);
    set_blend(GL_ONE, GL_ONE);
    if (haveBeams) {
        useProgram(program); g_curProgram = program;
        auto loc = [](const char* name) { return gl::GetUniformLocation(program, name); };
        gl::UniformMatrix4fv(loc("vp"), 1, GL_FALSE, vp);
        gl::Uniform4f(loc("eye4"), camera.eye.x, camera.eye.y, camera.eye.z, 1.0f);
        gl::Uniform4f(loc("coreColor4"), kCore[0], kCore[1], kCore[2], 1.0f);
        gl::Uniform4f(loc("glowColor4"), kGlow[0], kGlow[1], kGlow[2], 1.0f);
        gl::Uniform1f(loc("time"), time);
        gl::Uniform1f(loc("intensity"), intensity);
        for (int i = 0; i < beamCount; ++i) {
            const Beam& beam = beams[i];
            const float length = beam_length(beam);
            const float spread = beam_spread(length);
            const float halfWidth = spread * 2.6f + 0.35f;
            gl::Uniform4f(loc("beamA4"), beam.a[0], beam.a[1], beam.a[2], 1.0f);
            gl::Uniform4f(loc("beamB4"), beam.b[0], beam.b[1], beam.b[2], 1.0f);
            gl::Uniform1f(loc("beamLen"), length);
            gl::Uniform1f(loc("spread"), spread);
            gl::Uniform1f(loc("halfWidth"), halfWidth);
            gl::Uniform1f(loc("extend"), halfWidth);
            gl::Uniform1f(loc("seed"), (float)(i + 1) * 1.618f);
            drawArrays(0x0004 /* GL_TRIANGLES */, 0, 96 * 6);
        }
    }

    // 2a. Storm clouds (alpha-blended, so before the additive bolts that drop
    //     out of them).
    if (cloudProgram && intensity > 0.0f && anyStrike) {
        useProgram(cloudProgram); g_curProgram = cloudProgram;
        auto loc = [](const char* name) { return gl::GetUniformLocation(cloudProgram, name); };
        gl::UniformMatrix4fv(loc("vp"), 1, GL_FALSE, vp);
        gl::Uniform4f(loc("coreColor4"), kCore[0], kCore[1], kCore[2], 1.0f);
        gl::Uniform4f(loc("glowColor4"), kGlow[0], kGlow[1], kGlow[2], 1.0f);
        gl::Uniform1f(loc("time"), time);
        gl::Uniform1f(loc("intensity"), std::min(intensity, 1.5f));
        gl::Uniform1f(loc("radius"), 10.0f);
        set_blend(GL_ONE, 0x0303 /* GL_ONE_MINUS_SRC_ALPHA */);
        for (int i = 0; i < kMaxStrikes; ++i) {
            const Strike& st = strikes[i];
            if (!st.live) continue;
            const StrikeState ss = strike_state(st);
            gl::Uniform4f(loc("center4"), st.ground[0], st.ground[1],
                          st.ground[2] + strike_height(st), 1.0f);
            gl::Uniform1f(loc("seed"), (float)(st.seed % 100000u) * 0.001f + 3.0f);
            gl::Uniform1f(loc("stroke"), (float)ss.stroke);
            gl::Uniform1f(loc("pulse"), ss.pulse);
            gl::Uniform1f(loc("grow"), std::min(1.0f, ss.activeAge / 0.35f));
            gl::Uniform1f(loc("fade"), std::max(0.0f, 1.0f - ss.residual / kStrikeResidualSeconds));
            drawArrays(0x0004 /* GL_TRIANGLES */, 0, 6);
        }
        set_blend(GL_ONE, GL_ONE);
    }

    // 2b. Strike bolts while a stroke is on.
    if (strikeProgram && intensity > 0.0f && anyStrike) {
        useProgram(strikeProgram); g_curProgram = strikeProgram;
        auto loc = [](const char* name) { return gl::GetUniformLocation(strikeProgram, name); };
        gl::UniformMatrix4fv(loc("vp"), 1, GL_FALSE, vp);
        gl::Uniform4f(loc("eye4"), camera.eye.x, camera.eye.y, camera.eye.z, 1.0f);
        gl::Uniform4f(loc("coreColor4"), kCore[0], kCore[1], kCore[2], 1.0f);
        gl::Uniform4f(loc("glowColor4"), kGlow[0], kGlow[1], kGlow[2], 1.0f);
        gl::Uniform1f(loc("time"), time);
        gl::Uniform1f(loc("intensity"), intensity);
        for (int i = 0; i < kMaxStrikes; ++i) {
            const Strike& st = strikes[i];
            if (!st.live) continue;
            const StrikeState ss = strike_state(st);
            if (!ss.on) continue;
            const float* g = st.ground;
            gl::Uniform4f(loc("beamA4"), g[0], g[1], g[2], 1.0f);
            gl::Uniform4f(loc("beamB4"), g[0], g[1], g[2] + strike_height(st), 1.0f);
            gl::Uniform1f(loc("beamLen"), strike_height(st));
            gl::Uniform1f(loc("halfWidth"), 6.0f);
            gl::Uniform1f(loc("extend"), 1.5f);
            gl::Uniform1f(loc("seed"), (float)(st.seed % 100000u) * 0.001f + 3.0f);
            gl::Uniform1f(loc("pulse"), ss.pulse);
            gl::Uniform1f(loc("stroke"), (float)ss.stroke);
            drawArrays(0x0004 /* GL_TRIANGLES */, 0, 96 * 6);
        }
    }

    // 3. Impact and source discs: flash, crackling arcs, plasma.
    if (impactProgram && intensity > 0.0f) {
        useProgram(impactProgram); g_curProgram = impactProgram;
        auto loc = [](const char* name) { return gl::GetUniformLocation(impactProgram, name); };
        // Camera right/up in world space: the first two rows of the view rotation.
        const float* v = camera.view;
        gl::UniformMatrix4fv(loc("vp"), 1, GL_FALSE, vp);
        gl::Uniform4f(loc("right4"), v[0], v[4], v[8], 0.0f);
        gl::Uniform4f(loc("up4"), v[1], v[5], v[9], 0.0f);
        gl::Uniform4f(loc("coreColor4"), kCore[0], kCore[1], kCore[2], 1.0f);
        gl::Uniform4f(loc("glowColor4"), kGlow[0], kGlow[1], kGlow[2], 1.0f);
        gl::Uniform1f(loc("time"), time);
        gl::Uniform1f(loc("strength"), intensity);
        for (int i = 0; i < kMaxImpacts; ++i) {
            const Impact& m = impacts[i];
            if (!m.live) continue;
            float residual, activeAge; impact_state(m, residual, activeAge);
            // Pull the disc toward the camera so the target's body does not
            // swallow half of it.
            float toEye[3] = {camera.eye.x - m.p[0], camera.eye.y - m.p[1], camera.eye.z - m.p[2]};
            const float eyeDistance = std::sqrt(toEye[0]*toEye[0] + toEye[1]*toEye[1] + toEye[2]*toEye[2]);
            const float pull = eyeDistance > 1.0f ? 0.4f / eyeDistance : 0.0f;
            gl::Uniform4f(loc("center4"), m.p[0] + toEye[0] * pull, m.p[1] + toEye[1] * pull,
                          m.p[2] + toEye[2] * pull, 1.0f);
            gl::Uniform1f(loc("size"), m.source ? 0.45f : 0.9f);
            gl::Uniform1f(loc("isSource"), m.source ? 1.0f : 0.0f);
            gl::Uniform1f(loc("residual"), residual);
            gl::Uniform1f(loc("activeAge"), activeAge);
            gl::Uniform1f(loc("seed"), 1.0f + (float)i * 1.618f);
            drawArrays(0x0004 /* GL_TRIANGLES */, 0, 6);
        }
    }

    // 4. Sparks from the target end.
    if (sparkProgram && intensity > 0.0f) {
        useProgram(sparkProgram); g_curProgram = sparkProgram;
        auto loc = [](const char* name) { return gl::GetUniformLocation(sparkProgram, name); };
        gl::UniformMatrix4fv(loc("vp"), 1, GL_FALSE, vp);
        gl::Uniform4f(loc("viewport4"), (float)viewport[0], (float)viewport[1],
                      (float)viewport[2], (float)viewport[3]);
        gl::Uniform4f(loc("coreColor4"), kCore[0], kCore[1], kCore[2], 1.0f);
        gl::Uniform4f(loc("glowColor4"), kGlow[0], kGlow[1], kGlow[2], 1.0f);
        gl::Uniform1f(loc("time"), time);
        gl::Uniform1f(loc("strength"), intensity);
        for (int i = 0; i < kMaxImpacts; ++i) {
            const Impact& m = impacts[i];
            if (!m.live || m.source) continue;
            float residual, activeAge; impact_state(m, residual, activeAge);
            gl::Uniform4f(loc("center4"), m.p[0], m.p[1], m.p[2], 1.0f);
            gl::Uniform4f(loc("beamDir4"), m.dir[0], m.dir[1], m.dir[2], 0.0f);
            gl::Uniform1f(loc("residual"), residual);
            gl::Uniform1f(loc("activeAge"), activeAge);
            gl::Uniform1f(loc("seed"), 1.0f + (float)i * 1.618f);
            gl::Uniform1f(loc("speedScale"), 1.0f);
            drawArrays(0x0004 /* GL_TRIANGLES */, 0, kSparks * 6);
        }
        // Strikes throw more, faster sparks up and out of the contact point.
        for (int i = 0; i < kMaxStrikes; ++i) {
            const Strike& st = strikes[i];
            if (!st.live) continue;
            const StrikeState ss = strike_state(st);
            gl::Uniform4f(loc("center4"), st.ground[0], st.ground[1], st.ground[2] + 0.2f, 1.0f);
            gl::Uniform4f(loc("beamDir4"), 0.0f, 0.0f, -1.0f, 0.0f);
            gl::Uniform1f(loc("residual"), ss.residual);
            gl::Uniform1f(loc("activeAge"), ss.activeAge);
            gl::Uniform1f(loc("seed"), 60.0f + (float)i * 1.618f);
            gl::Uniform1f(loc("speedScale"), 1.8f);
            drawArrays(0x0004 /* GL_TRIANGLES */, 0, kSparks * 3 * 6);
        }
    }

    // 5. Strike ground decal: arcs crawling over the real surfaces, the shock
    //    ring and the plasma pool. Needs this frame's scene depth.
    if (groundProgram && sceneDepthReady && intensity > 0.0f && anyStrike) {
        useProgram(groundProgram); g_curProgram = groundProgram;
        auto loc = [](const char* name) { return gl::GetUniformLocation(groundProgram, name); };
        gl::ActiveTexture(GL_TEXTURE0); gl::BindTexture(GL_TEXTURE_2D, g_sceneDepthTex);
        weather_particles::bindSampler(0, depthSampler);
        gl::Uniform1i(loc("sceneDepth"), 0);
        gl::UniformMatrix4fv(loc("vpInverse"), 1, GL_FALSE, camera.viewProjectionInverse);
        gl::Uniform4f(loc("viewport4"), (float)viewport[0], (float)viewport[1],
                      (float)viewport[2], (float)viewport[3]);
        gl::Uniform4f(loc("coreColor4"), kCore[0], kCore[1], kCore[2], 1.0f);
        gl::Uniform4f(loc("glowColor4"), kGlow[0], kGlow[1], kGlow[2], 1.0f);
        gl::Uniform1f(loc("time"), time);
        gl::Uniform1f(loc("intensity"), intensity);
        gl::Uniform1f(loc("radius"), 5.0f);
        gl::Disable(GL_DEPTH_TEST);
        for (int i = 0; i < kMaxStrikes; ++i) {
            const Strike& st = strikes[i];
            if (!st.live) continue;
            const StrikeState ss = strike_state(st);
            gl::Uniform4f(loc("center4"), st.ground[0], st.ground[1], st.ground[2], 1.0f);
            gl::Uniform1f(loc("seed"), (float)(st.seed % 100000u) * 0.001f + 3.0f);
            gl::Uniform1f(loc("stroke"), (float)ss.stroke);
            gl::Uniform1f(loc("pulse"), ss.on ? ss.pulse : 0.6f);
            gl::Uniform1f(loc("residual"), ss.residual);
            gl::Uniform1f(loc("activeAge"), ss.activeAge);
            drawArrays(0x0004 /* GL_TRIANGLES */, 0, 3);
        }
    }

    const bool hidden = hiddenSerial == camera.serial;
    status = !haveBeams ? (anyStrike ? "drawing strike" : "residual plasma") :
             lit ? (hidden ? "drawing + light (native strips hidden)" : "drawing + light")
                 : sceneDepthReady || lightStrength <= 0.0f
                     ? (hidden ? "drawing (native strips hidden)" : "drawing")
                     : "drawing; no light (no scene depth this frame)";
}
} // namespace lightning_beams
static void draw_lightning_beams(void* scene) { lightning_beams::draw(scene); }
