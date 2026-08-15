// nwn_oit.cpp
// ---------------------------------------------------------------------------
// Order-independent transparency for NWN:EE, ported from the console renderer.
//
// WHERE THIS COMES FROM
//   The PS4 (CUSA15938/CUSA15670/ovr) and Switch (romfs/ovr) packages both ship
//   the renderer technique sources, and their transparency design is identical
//   (OpenGL33.technique is byte-identical after CRLF normalisation; GNM differs
//   only in a grass program name). The consoles do NOT sort transparency -- they
//   run weighted-blended OIT. Passes TransparencySum / PrePassTransparent /
//   TransparencyApply over screenTransparencyCombined, screenTransparencySum,
//   screenTranslucence (+ a radiance target the console's DEFERRED renderer
//   needs and the desktop FORWARD renderer has no use for, so it is dropped
//   here: three attachments, not four).
//
//   None of this exists in the desktop build. nwmain-linux and base_shaders.bif
//   contain no TransparencySum / TransparencyApply / screenTranslucence /
//   screenTransparencyCombined strings at all, and the desktop stage labels are
//   the legacy forward ones (Static Opaque / Static Transparent / Dynamic
//   Geometry / Sample Framebuffer / Additive). It is not a dormant feature to
//   switch on; it has to be rebuilt against the forward renderer.
//
// THE ACCUMULATION TRICK worth understanding before touching anything here:
//   ONE blend state -- src = ONE, dst = SRC_ALPHA -- serves both an additive and
//   a multiplicative accumulator, because the destination factor is the
//   fragment's OWN alpha:
//     att0 combined     = vec4(C*a, 1.0)      -> dst = C*a + dst*1   = sum(C*a)
//     att1 sum          = vec2(a,   1.0)      -> dst = a   + dst*1   = sum(a)
//     att2 translucence = vec2(0.0, 1.0-a)    -> dst = 0   + dst*(1-a)
//                                                   (cleared to 1)   = prod(1-a)
//   So no per-attachment blending (glBlendFunci, GL 4.0) is needed, which is why
//   this is reachable on the client's GL 3.3 context at all. Note that this also
//   corrects an older project note claiming no per-pixel OIT is possible here:
//   that is true of LINKED-LIST OIT (no image load/store on 3.3), not of
//   blend-based OIT.
//
// PHASE 1 (THIS FILE, TODAY) -- PROVE THE PIPE, TOUCH NO ENGINE GEOMETRY.
//   No draw is redirected yet. The three targets are created and CLEARED to a
//   synthetic single transparent layer, then resolved and composited with the
//   console's exact TransparencyApply math and blend. That is a real test, not a
//   placeholder, because of an identity worth checking first: for ONE layer of
//   colour C at alpha a, the resolve must reduce to plain alpha blending.
//     combined = C*a, sum = a, translucence = 1-a
//     -> Color.a = 1-a, I = (1-(1-a))/a = 1, Color.rgb = C*a
//     -> out = C*a + dst*(1-a)                         <- exactly src-over
//   So a correct build tints the screen by exactly NWN_OIT_TEST_ALPHA, and any
//   error in FBO setup, clear values, resolve math, blend state, or state
//   save/restore shows up as the wrong tint or a corrupted frame rather than as
//   something subtle to argue about later.
//
// WHAT IT DELIBERATELY DOES NOT DO YET
//   Redirect the engine's transparent draws (that needs the shader-source
//   transform and a caster partition -- emitters, additive glows,
//   sample_framebuffer materials and water must NOT join), and share the
//   engine's scene depth. Both are Phase 2.
//
// ORDERING NOTE, revisit in Phase 2: this runs AFTER draw_static_receiver() in
// the shadow module's frame sequence, which is the minimum-interference choice
// -- the validated receiver sees exactly the framebuffer state it saw before
// this file existed. It is not obviously the right FINAL order (transparency is
// part of the scene on consoles, whereas the receiver is a fullscreen darkening
// over the finished frame), but reordering is a deliberate experiment to run
// once real geometry is in the buffers, not a guess to bake in now.
//
// This TU must compile under both g++ (the .so) and mingw-w64 (version.dll).
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "nwn_platform.h"   // must precede the POSIX headers it shims
#ifndef _WIN32
#include <dlfcn.h>          // dlsym(RTLD_DEFAULT, ...)
#endif

#include "nwn_hooks_core.h"

// ---------------------------------------------------------------------------
//  GL types and enums. Hand-rolled, exactly as nwn_shadowmap.cpp does it: there
//  is no GL header in this build, and pulling one in beside <windows.h> for the
//  mingw target is not worth the conflicts.
// ---------------------------------------------------------------------------
typedef unsigned int  GLenum;
typedef unsigned int  GLuint;
typedef int           GLint;
typedef int           GLsizei;
typedef unsigned int  GLbitfield;
typedef unsigned char GLboolean;
typedef float         GLfloat;
typedef char          GLchar;

#define GL_FALSE                     0
#define GL_TRUE                      1
#define GL_TRIANGLES            0x0004
#define GL_DEPTH_TEST           0x0B71
#define GL_DEPTH_WRITEMASK      0x0B72
#define GL_VIEWPORT             0x0BA2
#define GL_BLEND                0x0BE2
#define GL_TEXTURE_2D           0x0DE1
#define GL_FLOAT                0x1406
#define GL_RED                  0x1903
#define GL_RGBA                 0x1908
#define GL_COLOR                0x1800
#define GL_ONE                       1
#define GL_SRC_ALPHA            0x0302
#define GL_NEAREST              0x2600
#define GL_TEXTURE_MAG_FILTER   0x2800
#define GL_TEXTURE_MIN_FILTER   0x2801
#define GL_TEXTURE_WRAP_S       0x2802
#define GL_TEXTURE_WRAP_T       0x2803
#define GL_TEXTURE_BINDING_2D   0x8069
#define GL_CLAMP_TO_EDGE        0x812F
#define GL_R16F                 0x822D
#define GL_RGBA16F              0x881A
#define GL_TEXTURE0             0x84C0
#define GL_ACTIVE_TEXTURE       0x84E0
#define GL_FRAGMENT_SHADER      0x8B30
#define GL_VERTEX_SHADER        0x8B31
#define GL_COMPILE_STATUS       0x8B81
#define GL_LINK_STATUS          0x8B82
#define GL_CURRENT_PROGRAM      0x8B8D
#define GL_FRAMEBUFFER          0x8D40
#define GL_FRAMEBUFFER_BINDING  0x8CA6
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_COLOR_ATTACHMENT0    0x8CE0
#define GL_BLEND_SRC_RGB        0x80C9
#define GL_BLEND_DST_RGB        0x80C8
#define GL_BLEND_SRC_ALPHA      0x80CB
#define GL_BLEND_DST_ALPHA      0x80CA

namespace {

// ---------------------------------------------------------------------------
//  This module's own GL table. Deliberately NOT shared with nwn_shadowmap.cpp's
//  `gl::` namespace -- see the header. dlsym is the platform shim's on Windows,
//  which falls back to wglGetProcAddress, so resolution must happen from the
//  render path where a GL context is guaranteed current (same reason the shadow
//  module defers bind_gl to the first Scene::Render).
// ---------------------------------------------------------------------------
struct GL {
    void   (*GetIntegerv)(GLenum, GLint*);
    void   (*GetBooleanv)(GLenum, GLboolean*);
    GLboolean (*IsEnabled)(GLenum);
    void   (*Enable)(GLenum);
    void   (*Disable)(GLenum);
    void   (*BlendFuncSeparate)(GLenum, GLenum, GLenum, GLenum);
    void   (*Viewport)(GLint, GLint, GLsizei, GLsizei);
    void   (*DepthMask)(GLboolean);
    void   (*ClearBufferfv)(GLenum, GLint, const GLfloat*);
    void   (*GenTextures)(GLsizei, GLuint*);
    void   (*DeleteTextures)(GLsizei, const GLuint*);
    void   (*BindTexture)(GLenum, GLuint);
    void   (*TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint,
                         GLenum, GLenum, const void*);
    void   (*TexParameteri)(GLenum, GLenum, GLint);
    void   (*ActiveTexture)(GLenum);
    void   (*GenFramebuffers)(GLsizei, GLuint*);
    void   (*DeleteFramebuffers)(GLsizei, const GLuint*);
    void   (*BindFramebuffer)(GLenum, GLuint);
    void   (*FramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
    GLenum (*CheckFramebufferStatus)(GLenum);
    void   (*DrawBuffers)(GLsizei, const GLenum*);
    GLuint (*CreateShader)(GLenum);
    void   (*ShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*);
    void   (*CompileShader)(GLuint);
    void   (*GetShaderiv)(GLuint, GLenum, GLint*);
    void   (*GetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
    void   (*DeleteShader)(GLuint);
    GLuint (*CreateProgram)();
    void   (*AttachShader)(GLuint, GLuint);
    void   (*LinkProgram)(GLuint);
    void   (*GetProgramiv)(GLuint, GLenum, GLint*);
    void   (*GetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
    void   (*UseProgram)(GLuint);
    GLint  (*GetUniformLocation)(GLuint, const GLchar*);
    void   (*Uniform1i)(GLint, GLint);
    void   (*DrawArrays)(GLenum, GLint, GLsizei);
};
GL g = {};
bool g_glBound = false;

bool bind_gl() {
    bool ok = true;
    #define OITBIND(field, name)                                              \
        do { *(void**)(&g.field) = dlsym(RTLD_DEFAULT, name);                 \
             if (!g.field) {                                                  \
                 fprintf(stderr, "[oit] missing GL entry: %s\n", name);        \
                 ok = false; } } while (0)
    OITBIND(GetIntegerv,            "glGetIntegerv");
    OITBIND(GetBooleanv,            "glGetBooleanv");
    OITBIND(IsEnabled,              "glIsEnabled");
    OITBIND(Enable,                 "glEnable");
    OITBIND(Disable,                "glDisable");
    OITBIND(BlendFuncSeparate,      "glBlendFuncSeparate");
    OITBIND(Viewport,               "glViewport");
    OITBIND(DepthMask,              "glDepthMask");
    OITBIND(ClearBufferfv,          "glClearBufferfv");
    OITBIND(GenTextures,            "glGenTextures");
    OITBIND(DeleteTextures,         "glDeleteTextures");
    OITBIND(BindTexture,            "glBindTexture");
    OITBIND(TexImage2D,             "glTexImage2D");
    OITBIND(TexParameteri,          "glTexParameteri");
    OITBIND(ActiveTexture,          "glActiveTexture");
    OITBIND(GenFramebuffers,        "glGenFramebuffers");
    OITBIND(DeleteFramebuffers,     "glDeleteFramebuffers");
    OITBIND(BindFramebuffer,        "glBindFramebuffer");
    OITBIND(FramebufferTexture2D,   "glFramebufferTexture2D");
    OITBIND(CheckFramebufferStatus, "glCheckFramebufferStatus");
    OITBIND(DrawBuffers,            "glDrawBuffers");
    OITBIND(CreateShader,           "glCreateShader");
    OITBIND(ShaderSource,           "glShaderSource");
    OITBIND(CompileShader,          "glCompileShader");
    OITBIND(GetShaderiv,            "glGetShaderiv");
    OITBIND(GetShaderInfoLog,       "glGetShaderInfoLog");
    OITBIND(DeleteShader,           "glDeleteShader");
    OITBIND(CreateProgram,          "glCreateProgram");
    OITBIND(AttachShader,           "glAttachShader");
    OITBIND(LinkProgram,            "glLinkProgram");
    OITBIND(GetProgramiv,           "glGetProgramiv");
    OITBIND(GetProgramInfoLog,      "glGetProgramInfoLog");
    OITBIND(UseProgram,             "glUseProgram");
    OITBIND(GetUniformLocation,     "glGetUniformLocation");
    OITBIND(Uniform1i,              "glUniform1i");
    #undef OITBIND

    // glDrawArrays is the ONE entry point resolved differently, and deliberately.
    // On Linux RTLD_DEFAULT would find the injector's OWN interposed
    // glDrawArrays (this .so is ahead of libGL in the global scope), so this
    // module's resolve triangle would travel through the shadow module's
    // per-draw path: counted by its geometry tracer, and -- worse -- eligible
    // for DUPLICATE_CASCADE_LIGHT, which re-draws the incoming primitive into
    // every cascade layer. Today that macro is gated on capture flags that are
    // false by the time this runs, so nothing would actually go wrong; but that
    // is a coincidence of WHERE the frame hook sits, not a guarantee, and this
    // project's history is that re-entrant replay bugs arrive in new disguises.
    // RTLD_NEXT skips this library and lands on the driver, which makes the
    // bypass structural instead of positional.
    // Windows needs nothing here: nwn_win_dlsym ignores the handle and goes to
    // opengl32 directly, and its interposition is an IAT patch on nwmain.exe's
    // imports, which our own calls never go through.
    *(void**)(&g.DrawArrays) = dlsym(RTLD_NEXT, "glDrawArrays");
    if (!g.DrawArrays) *(void**)(&g.DrawArrays) = dlsym(RTLD_DEFAULT, "glDrawArrays");
    if (!g.DrawArrays) { fprintf(stderr, "[oit] missing GL entry: glDrawArrays\n"); ok = false; }
    return ok;
}

// ---------------------------------------------------------------------------
//  Settings
// ---------------------------------------------------------------------------
bool  g_enabled   = false;
bool  g_census    = false;      // NWN_OIT_CENSUS -- Phase 2a, see below
bool  g_settingsRead = false;
bool  g_failed    = false;      // hard failure; stop trying, stay out of the way
float g_testColor[3] = { 1.0f, 0.25f, 0.25f };
float g_testAlpha = 0.35f;

float env_float(const char* name, float dflt) {
    const char* v = getenv(name);
    if (!v || !*v) return dflt;
    return (float)atof(v);
}

void read_settings() {
    if (g_settingsRead) return;
    g_settingsRead = true;
    const char* e = getenv("NWN_OIT");
    g_enabled = (e && *e && *e != '0');
    const char* cen = getenv("NWN_OIT_CENSUS");
    g_census = (cen && *cen && *cen != '0');
    if (g_census)
        fprintf(stderr, "[oit] Phase 2a census enabled: reports blend/depth/cull "
                        "state once per (bucket, program) pair. Read-only -- no "
                        "draw is redirected, duplicated or filtered.\n");
    if (!g_enabled) return;
    g_testAlpha = env_float("NWN_OIT_TEST_ALPHA", 0.35f);
    if (g_testAlpha < 0.0f) g_testAlpha = 0.0f;
    if (g_testAlpha > 1.0f) g_testAlpha = 1.0f;
    const char* c = getenv("NWN_OIT_TEST_COLOR");
    if (c && *c) {
        float r = 0, gg = 0, b = 0;
        if (sscanf(c, "%f,%f,%f", &r, &gg, &b) == 3) {
            g_testColor[0] = r; g_testColor[1] = gg; g_testColor[2] = b;
        } else {
            fprintf(stderr, "[oit] NWN_OIT_TEST_COLOR must be \"r,g,b\"; ignoring \"%s\"\n", c);
        }
    }
    fprintf(stderr, "[oit] enabled -- PHASE 1 pipe proof, no engine geometry is "
                    "redirected. Expect the frame tinted (%.2f,%.2f,%.2f) at alpha "
                    "%.2f; that is the single-layer identity with plain alpha "
                    "blending.\n",
            g_testColor[0], g_testColor[1], g_testColor[2], g_testAlpha);
}

// ---------------------------------------------------------------------------
//  Targets
// ---------------------------------------------------------------------------
GLuint g_fbo = 0;
GLuint g_texCombined = 0;      // RGBA16F  sum(C*a)
GLuint g_texSum      = 0;      // R16F     sum(a)
GLuint g_texTransl   = 0;      // R16F     prod(1-a)
int    g_w = 0, g_h = 0;

// Texture units this pass borrows. High on purpose: the shadow module owns low
// ones, and every binding here is saved and restored anyway.
const GLenum kUnitCombined = GL_TEXTURE0 + 12;
const GLenum kUnitSum      = GL_TEXTURE0 + 13;
const GLenum kUnitTransl   = GL_TEXTURE0 + 14;

GLuint make_target(GLint internalFormat, GLenum format, int w, int h) {
    GLuint t = 0;
    g.GenTextures(1, &t);
    if (!t) return 0;
    g.BindTexture(GL_TEXTURE_2D, t);
    g.TexImage2D(GL_TEXTURE_2D, 0, internalFormat, w, h, 0, format, GL_FLOAT, nullptr);
    g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

void destroy_targets() {
    if (g_fbo)         { g.DeleteFramebuffers(1, &g_fbo);      g_fbo = 0; }
    if (g_texCombined) { g.DeleteTextures(1, &g_texCombined);  g_texCombined = 0; }
    if (g_texSum)      { g.DeleteTextures(1, &g_texSum);       g_texSum = 0; }
    if (g_texTransl)   { g.DeleteTextures(1, &g_texTransl);    g_texTransl = 0; }
    g_w = g_h = 0;
}

// Creates or resizes the accumulation targets. Caller has already saved the
// framebuffer binding and the texture binding on unit kUnitCombined.
bool ensure_targets(int w, int h) {
    if (g_fbo && w == g_w && h == g_h) return true;
    destroy_targets();
    if (w <= 0 || h <= 0) return false;

    g.ActiveTexture(kUnitCombined);
    g_texCombined = make_target(GL_RGBA16F, GL_RGBA, w, h);
    g_texSum      = make_target(GL_R16F,    GL_RED,  w, h);
    g_texTransl   = make_target(GL_R16F,    GL_RED,  w, h);
    if (!g_texCombined || !g_texSum || !g_texTransl) {
        fprintf(stderr, "[oit] target allocation failed at %dx%d\n", w, h);
        destroy_targets();
        return false;
    }

    g.GenFramebuffers(1, &g_fbo);
    if (!g_fbo) { destroy_targets(); return false; }
    g.BindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    g.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + 0, GL_TEXTURE_2D, g_texCombined, 0);
    g.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + 1, GL_TEXTURE_2D, g_texSum,      0);
    g.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + 2, GL_TEXTURE_2D, g_texTransl,   0);
    const GLenum bufs[3] = { GL_COLOR_ATTACHMENT0 + 0,
                             GL_COLOR_ATTACHMENT0 + 1,
                             GL_COLOR_ATTACHMENT0 + 2 };
    g.DrawBuffers(3, bufs);

    const GLenum status = g.CheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "[oit] MRT framebuffer incomplete (0x%04X) at %dx%d\n",
                (unsigned)status, w, h);
        destroy_targets();
        return false;
    }
    g_w = w; g_h = h;
    fprintf(stderr, "[oit] accumulation targets ready: %dx%d, 3 attachments "
                    "(RGBA16F combined, R16F sum, R16F translucence)\n", w, h);
    return true;
}

// ---------------------------------------------------------------------------
//  Resolve program -- the console's TransparencyApply, minus the radiance
//  target the forward renderer has no equivalent for.
// ---------------------------------------------------------------------------
GLuint g_program = 0;

GLuint compile(GLenum type, const char* src, const char* label) {
    GLuint s = g.CreateShader(type);
    if (!s) return 0;
    const GLint len = (GLint)strlen(src);
    g.ShaderSource(s, 1, &src, &len);
    g.CompileShader(s);
    GLint ok = 0;
    g.GetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {0};
        g.GetShaderInfoLog(s, (GLsizei)sizeof(log) - 1, nullptr, log);
        fprintf(stderr, "[oit] %s shader compile FAILED:\n%s\n", label, log);
        g.DeleteShader(s);
        return 0;
    }
    return s;
}

bool build_program() {
    if (g_program) return true;
    // Fullscreen triangle from gl_VertexID -- no VBO, no VAO. Same idiom the
    // shadow module's receiver uses, and it is what makes this pass safe to run
    // inside the engine's vertex-array state without touching it.
    static const char* vs =
        "#version 330 compatibility\n"
        "void main(){ vec2 p=vec2((gl_VertexID<<1)&2, gl_VertexID&2);"
        " gl_Position=vec4(p*2.0-1.0, 0.0, 1.0); }\n";
    // fTotal>0 gates the whole thing exactly as the console shader does: with no
    // transparent fragment at this texel there is nothing to composite, and the
    // ONE/SRC_ALPHA blend below must then see alpha 0 so the destination is
    // multiplied by... 0. Which is why Color starts at vec4(0) and the alpha
    // written in that case is 0 -- NOT 1. Getting this backwards blacks out
    // every pixel with no transparency in front of it.
    static const char* fs =
        "#version 330 compatibility\n"
        "uniform sampler2D oitCombined;\n"
        "uniform sampler2D oitSum;\n"
        "uniform sampler2D oitTranslucence;\n"
        "void main(){\n"
        "  vec2 sz = vec2(textureSize(oitCombined,0));\n"
        "  vec2 uv = gl_FragCoord.xy / sz;\n"
        "  float fTotal = texture(oitSum, uv).r;\n"
        "  vec4 Color = vec4(0.0);\n"
        "  if (fTotal > 0.0) {\n"
        "    Color = texture(oitCombined, uv);\n"
        "    Color.a = clamp(texture(oitTranslucence, uv).r, 0.0, 1.0);\n"
        "    float fIntensity = (1.0 - Color.a) / fTotal;\n"
        "    Color.rgb *= fIntensity;\n"
        "  }\n"
        "  gl_FragColor = Color;\n"
        "}\n";

    GLuint v = compile(GL_VERTEX_SHADER, vs, "resolve vertex");
    if (!v) return false;
    GLuint f = compile(GL_FRAGMENT_SHADER, fs, "resolve fragment");
    if (!f) { g.DeleteShader(v); return false; }

    GLuint p = g.CreateProgram();
    g.AttachShader(p, v);
    g.AttachShader(p, f);
    g.LinkProgram(p);
    GLint ok = 0;
    g.GetProgramiv(p, GL_LINK_STATUS, &ok);
    g.DeleteShader(v);
    g.DeleteShader(f);
    if (!ok) {
        char log[1024] = {0};
        g.GetProgramInfoLog(p, (GLsizei)sizeof(log) - 1, nullptr, log);
        fprintf(stderr, "[oit] resolve program link FAILED:\n%s\n", log);
        return false;
    }
    g_program = p;
    fprintf(stderr, "[oit] resolve program compiled (program=%u)\n", p);
    return true;
}

// ---------------------------------------------------------------------------
//  Saved GL state. Every field here is something this pass writes.
// ---------------------------------------------------------------------------
struct SavedState {
    GLint     fbo;
    GLint     viewport[4];
    GLint     program;
    GLint     activeTexture;
    GLint     texCombined, texSum, texTransl;
    GLboolean depthMask;
    GLboolean depthTest;
    GLboolean blend;
    GLint     blendSrcRGB, blendDstRGB, blendSrcAlpha, blendDstAlpha;
};

void save_state(SavedState& s) {
    g.GetIntegerv(GL_FRAMEBUFFER_BINDING, &s.fbo);
    g.GetIntegerv(GL_VIEWPORT,            s.viewport);
    g.GetIntegerv(GL_CURRENT_PROGRAM,     &s.program);
    g.GetIntegerv(GL_ACTIVE_TEXTURE,      &s.activeTexture);
    g.GetBooleanv(GL_DEPTH_WRITEMASK,     &s.depthMask);
    s.depthTest = g.IsEnabled(GL_DEPTH_TEST);
    s.blend     = g.IsEnabled(GL_BLEND);
    g.GetIntegerv(GL_BLEND_SRC_RGB,   &s.blendSrcRGB);
    g.GetIntegerv(GL_BLEND_DST_RGB,   &s.blendDstRGB);
    g.GetIntegerv(GL_BLEND_SRC_ALPHA, &s.blendSrcAlpha);
    g.GetIntegerv(GL_BLEND_DST_ALPHA, &s.blendDstAlpha);
    g.ActiveTexture(kUnitCombined); g.GetIntegerv(GL_TEXTURE_BINDING_2D, &s.texCombined);
    g.ActiveTexture(kUnitSum);      g.GetIntegerv(GL_TEXTURE_BINDING_2D, &s.texSum);
    g.ActiveTexture(kUnitTransl);   g.GetIntegerv(GL_TEXTURE_BINDING_2D, &s.texTransl);
}

void restore_state(const SavedState& s) {
    g.ActiveTexture(kUnitTransl);   g.BindTexture(GL_TEXTURE_2D, (GLuint)s.texTransl);
    g.ActiveTexture(kUnitSum);      g.BindTexture(GL_TEXTURE_2D, (GLuint)s.texSum);
    g.ActiveTexture(kUnitCombined); g.BindTexture(GL_TEXTURE_2D, (GLuint)s.texCombined);
    g.ActiveTexture((GLenum)s.activeTexture);
    g.BlendFuncSeparate((GLenum)s.blendSrcRGB,   (GLenum)s.blendDstRGB,
                        (GLenum)s.blendSrcAlpha, (GLenum)s.blendDstAlpha);
    if (s.blend)     g.Enable(GL_BLEND);      else g.Disable(GL_BLEND);
    if (s.depthTest) g.Enable(GL_DEPTH_TEST); else g.Disable(GL_DEPTH_TEST);
    g.DepthMask(s.depthMask);
    g.UseProgram((GLuint)s.program);
    g.Viewport(s.viewport[0], s.viewport[1], s.viewport[2], s.viewport[3]);
    g.BindFramebuffer(GL_FRAMEBUFFER, (GLuint)s.fbo);
}

// ---------------------------------------------------------------------------
//  PHASE 2a CENSUS (NWN_OIT_CENSUS=1) -- which buckets carry real BLENDED
//  transparency, as opposed to alpha-cutout.
//
//  The shadow project's Phase 3e census already reports draws/vertices/programs
//  per bucket. What it cannot answer is the question that decides membership of
//  the OIT accumulation: is this bucket BLENDING, and with which factors? The
//  console technique routes alpha-cutout through the OPAQUE prepass
//  (TRANSPARENT_OPAQUE, discard below 0.99) and only genuinely blended surfaces
//  through TransparencySum, so "has an alpha channel" is not the criterion --
//  the blend state is.
//
//  Expected signatures:
//    blend off                       -> opaque or alpha-cutout. NOT an OIT
//                                       candidate; it writes depth and is
//                                       already order-independent.
//    SRC_ALPHA / ONE_MINUS_SRC_ALPHA -> ordinary transparency. THE candidate.
//    SRC_ALPHA / ONE, or ONE / ONE   -> additive glow. Must NOT join: a
//                                       weighted average is meaningless for
//                                       additive light, and the console keeps
//                                       emitters out of the OIT path for
//                                       exactly this reason.
//
//  Cost: one glGetIntegerv burst per (bucket, program) pair FIRST SEEN, not per
//  draw. Those queries are driver-synchronising, so this is a diagnostic mode
//  and the dedupe is what keeps it usable rather than a nicety.
// ---------------------------------------------------------------------------
#define GL_CULL_FACE            0x0B44
#define GL_BLEND_EQUATION_RGB   0x8009

struct CensusEntry {
    int    bucket;
    GLuint program;
};
CensusEntry g_censusSeen[192];
unsigned    g_censusCount = 0;

const char* blend_factor_name(GLint f) {
    switch (f) {
        case 0:      return "ZERO";
        case 1:      return "ONE";
        case 0x0300: return "SRC_COLOR";
        case 0x0301: return "ONE_MINUS_SRC_COLOR";
        case 0x0302: return "SRC_ALPHA";
        case 0x0303: return "ONE_MINUS_SRC_ALPHA";
        case 0x0304: return "DST_ALPHA";
        case 0x0305: return "ONE_MINUS_DST_ALPHA";
        case 0x0306: return "DST_COLOR";
        case 0x0307: return "ONE_MINUS_DST_COLOR";
        default:     return "?";
    }
}

// Reads the blend signature and names what it implies for OIT membership. The
// verdict is deliberately printed next to the raw factors so a wrong call is
// visible rather than buried.
const char* oit_verdict(GLboolean blend, GLint src, GLint dst) {
    if (!blend)                             return "CUTOUT/OPAQUE - excluded (writes depth)";
    if (src == 0x0302 && dst == 0x0303)     return "SRC_OVER - OIT CANDIDATE";
    if (dst == 1)                           return "ADDITIVE - excluded";
    if (src == 1 && dst == 0)               return "REPLACE - excluded";
    return "other - inspect";
}

void census_observe_draw() {
    const int bucket = nwn_core::g_currentBucket;
    if (!g_glBound || bucket < 0) return;

    GLint prog = 0;
    g.GetIntegerv(GL_CURRENT_PROGRAM, &prog);
    if (prog <= 0) return;

    for (unsigned i = 0; i < g_censusCount; ++i)
        if (g_censusSeen[i].bucket == bucket && g_censusSeen[i].program == (GLuint)prog)
            return;                       // already reported this pair
    if (g_censusCount >= sizeof(g_censusSeen) / sizeof(g_censusSeen[0])) return;
    g_censusSeen[g_censusCount].bucket  = bucket;
    g_censusSeen[g_censusCount].program = (GLuint)prog;
    ++g_censusCount;

    GLint srcRGB = 0, dstRGB = 0, srcA = 0, dstA = 0;
    GLboolean depthMask = GL_TRUE;
    g.GetIntegerv(GL_BLEND_SRC_RGB,   &srcRGB);
    g.GetIntegerv(GL_BLEND_DST_RGB,   &dstRGB);
    g.GetIntegerv(GL_BLEND_SRC_ALPHA, &srcA);
    g.GetIntegerv(GL_BLEND_DST_ALPHA, &dstA);
    g.GetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
    const GLboolean blend = g.IsEnabled(GL_BLEND);
    const GLboolean depth = g.IsEnabled(GL_DEPTH_TEST);
    const GLboolean cull  = g.IsEnabled(GL_CULL_FACE);

    fprintf(stderr, "[oit][census] bucket=%d program=%u blend=%s rgb=%s/%s "
                    "alpha=%s/%s depthtest=%d depthwrite=%d cull=%d  -> %s\n",
            bucket, (unsigned)prog, blend ? "ON" : "off",
            blend_factor_name(srcRGB), blend_factor_name(dstRGB),
            blend_factor_name(srcA),   blend_factor_name(dstA),
            depth ? 1 : 0, depthMask ? 1 : 0, cull ? 1 : 0,
            oit_verdict(blend, srcRGB, dstRGB));
}

}   // anonymous namespace

// ---------------------------------------------------------------------------
//  Entry points (declared in nwn_hooks_core.h)
// ---------------------------------------------------------------------------

bool nwn_oit_active(void) { return g_enabled && !g_failed && g_program != 0; }

void nwn_oit_shutdown(void) {
    if (!g_glBound) return;
    destroy_targets();
    if (g_program) { /* program deletion needs glDeleteProgram; not resolved
                        because shutdown runs at process exit, where the GL
                        context is already gone on both platforms. */ }
}

void nwn_oit_frame(void) {
    read_settings();
    if ((!g_enabled && !g_census) || g_failed) return;

    if (!g_glBound) {
        g_glBound = bind_gl();
        if (!g_glBound) {
            // On Windows modern GL only resolves once a context is current. This
            // is called from the render path, so a failure here is real.
            fprintf(stderr, "[oit] GL entry points unavailable; module disabled\n");
            g_failed = true;
            return;
        }
        fprintf(stderr, "[oit] GL entry points bound\n");
    }

    // The census is independent of compositing: it observes the engine's own
    // draws, so it is useful (and honest) with NWN_OIT off entirely. Installed
    // from here rather than at load time because it needs the GL table, which
    // needs a current context.
    if (g_census && !nwn_core::g_drawObserver)
        nwn_core::g_drawObserver = census_observe_draw;

    if (!g_enabled) return;              // census-only run: nothing to composite
    if (!build_program()) { g_failed = true; return; }

    SavedState st;
    save_state(st);

    // Match the accumulation resolution to whatever the engine is drawing at.
    if (!ensure_targets(st.viewport[2], st.viewport[3])) {
        restore_state(st);
        g_failed = true;
        return;
    }

    // Everything below is this module's own draw traffic. The guard keeps it out
    // of the shadow module's per-draw hook (and out of any future module's).
    nwn_core::OwnedPass owned;

    // ---- accumulate ------------------------------------------------------
    // PHASE 1: no engine geometry is redirected here yet, so the buffers are
    // cleared to a synthetic single layer of colour C at alpha a:
    //   combined = C*a,  sum = a,  translucence = 1-a
    // Phase 2 replaces these clears with the console's real clear values
    // (0,0,0,0 / 0 / 1) followed by the redirected transparent draws.
    const float a = g_testAlpha;
    const GLfloat clearCombined[4] = { g_testColor[0] * a, g_testColor[1] * a,
                                       g_testColor[2] * a, 1.0f };
    const GLfloat clearSum[4]      = { a, 0.0f, 0.0f, 0.0f };
    const GLfloat clearTransl[4]   = { 1.0f - a, 0.0f, 0.0f, 0.0f };
    g.BindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    g.Viewport(0, 0, g_w, g_h);
    g.ClearBufferfv(GL_COLOR, 0, clearCombined);
    g.ClearBufferfv(GL_COLOR, 1, clearSum);
    g.ClearBufferfv(GL_COLOR, 2, clearTransl);

    // ---- resolve + composite --------------------------------------------
    // Back into whatever the engine had bound, with the console's exact blend:
    // out = src.rgb + dst * src.a, where src.a is the accumulated transmittance.
    g.BindFramebuffer(GL_FRAMEBUFFER, (GLuint)st.fbo);
    g.Viewport(st.viewport[0], st.viewport[1], st.viewport[2], st.viewport[3]);
    g.Disable(GL_DEPTH_TEST);
    g.DepthMask(GL_FALSE);
    g.Enable(GL_BLEND);
    g.BlendFuncSeparate(GL_ONE, GL_SRC_ALPHA, GL_ONE, GL_SRC_ALPHA);

    g.UseProgram(g_program);
    g.ActiveTexture(kUnitCombined); g.BindTexture(GL_TEXTURE_2D, g_texCombined);
    g.ActiveTexture(kUnitSum);      g.BindTexture(GL_TEXTURE_2D, g_texSum);
    g.ActiveTexture(kUnitTransl);   g.BindTexture(GL_TEXTURE_2D, g_texTransl);
    GLint loc;
    loc = g.GetUniformLocation(g_program, "oitCombined");
    if (loc >= 0) g.Uniform1i(loc, (GLint)(kUnitCombined - GL_TEXTURE0));
    loc = g.GetUniformLocation(g_program, "oitSum");
    if (loc >= 0) g.Uniform1i(loc, (GLint)(kUnitSum - GL_TEXTURE0));
    loc = g.GetUniformLocation(g_program, "oitTranslucence");
    if (loc >= 0) g.Uniform1i(loc, (GLint)(kUnitTransl - GL_TEXTURE0));

    g.DrawArrays(GL_TRIANGLES, 0, 3);

    restore_state(st);

    static unsigned frames = 0;
    if (++frames == 1)
        fprintf(stderr, "[oit] first composite executed at %dx%d\n", g_w, g_h);
}
