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
#define GL_NO_ERROR                  0
#define GL_TRIANGLES            0x0004
#define GL_DEPTH_TEST           0x0B71
#define GL_DEPTH_WRITEMASK      0x0B72
#define GL_DEPTH_FUNC           0x0B74
#define GL_VIEWPORT             0x0BA2
#define GL_BLEND                0x0BE2
#define GL_CULL_FACE            0x0B44
#define GL_SAMPLE_ALPHA_TO_COVERAGE 0x809E
#define GL_SAMPLES              0x80A9
#define GL_SCISSOR_TEST         0x0C11
#define GL_COLOR_WRITEMASK      0x0C23
#define GL_TEXTURE_2D           0x0DE1
#define GL_TEXTURE_2D_MULTISAMPLE 0x9100
#define GL_TEXTURE_BINDING_2D_MULTISAMPLE 0x9104
#define GL_FLOAT                0x1406
#define GL_UNSIGNED_INT         0x1405
#define GL_RED                  0x1903
#define GL_RGBA                 0x1908
#define GL_DEPTH_COMPONENT      0x1902
#define GL_COLOR                0x1800
#define GL_ONE                       1
#define GL_SRC_ALPHA            0x0302
#define GL_ONE_MINUS_SRC_ALPHA  0x0303
#define GL_LEQUAL               0x0203
#define GL_LESS                 0x0201
#define GL_ALWAYS               0x0207
#define GL_NEAREST              0x2600
#define GL_TEXTURE_MAG_FILTER   0x2800
#define GL_TEXTURE_MIN_FILTER   0x2801
#define GL_TEXTURE_WRAP_S       0x2802
#define GL_TEXTURE_WRAP_T       0x2803
#define GL_TEXTURE_BINDING_2D   0x8069
#define GL_CLAMP_TO_EDGE        0x812F
#define GL_R16F                 0x822D
#define GL_DEPTH_COMPONENT24    0x81A6
#define GL_RGBA16F              0x881A
#define GL_NONE                      0
#define GL_DEPTH_BUFFER_BIT     0x00000100
#define GL_TEXTURE0             0x84C0
#define GL_ACTIVE_TEXTURE       0x84E0
#define GL_FRAGMENT_SHADER      0x8B30
#define GL_VERTEX_SHADER        0x8B31
#define GL_COMPILE_STATUS       0x8B81
#define GL_LINK_STATUS          0x8B82
#define GL_CURRENT_PROGRAM      0x8B8D
#define GL_FRAMEBUFFER          0x8D40
#define GL_READ_FRAMEBUFFER     0x8CA8
#define GL_DRAW_FRAMEBUFFER     0x8CA9
#define GL_FRAMEBUFFER_BINDING  0x8CA6
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_COLOR_ATTACHMENT0    0x8CE0
#define GL_DEPTH_ATTACHMENT     0x8D00
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
    GLenum (*GetError)();
    void   (*GetIntegerv)(GLenum, GLint*);
    void   (*GetBooleanv)(GLenum, GLboolean*);
    GLboolean (*IsEnabled)(GLenum);
    void   (*Enable)(GLenum);
    void   (*Disable)(GLenum);
    void   (*BlendFuncSeparate)(GLenum, GLenum, GLenum, GLenum);
    void   (*Viewport)(GLint, GLint, GLsizei, GLsizei);
    void   (*DepthMask)(GLboolean);
    void   (*DepthFunc)(GLenum);
    void   (*ColorMask)(GLboolean, GLboolean, GLboolean, GLboolean);
    void   (*ClearBufferfv)(GLenum, GLint, const GLfloat*);
    void   (*GenTextures)(GLsizei, GLuint*);
    void   (*DeleteTextures)(GLsizei, const GLuint*);
    void   (*BindTexture)(GLenum, GLuint);
    void   (*TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint,
                         GLenum, GLenum, const void*);
    void   (*TexImage2DMultisample)(GLenum, GLsizei, GLint, GLsizei, GLsizei,
                                    GLboolean);
    void   (*TexParameteri)(GLenum, GLenum, GLint);
    void   (*CopyTexSubImage2D)(GLenum, GLint, GLint, GLint, GLint, GLint,
                                GLsizei, GLsizei);
    void   (*ActiveTexture)(GLenum);
    void   (*GenFramebuffers)(GLsizei, GLuint*);
    void   (*DeleteFramebuffers)(GLsizei, const GLuint*);
    void   (*BindFramebuffer)(GLenum, GLuint);
    void   (*FramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
    GLenum (*CheckFramebufferStatus)(GLenum);
    void   (*DrawBuffers)(GLsizei, const GLenum*);
    void   (*DrawBuffer)(GLenum);
    void   (*ReadBuffer)(GLenum);
    void   (*BlitFramebuffer)(GLint, GLint, GLint, GLint,
                              GLint, GLint, GLint, GLint,
                              GLbitfield, GLenum);
    void   (*ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
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
    void   (*GetUniformiv)(GLuint, GLint, GLint*);
    void   (*GetUniformfv)(GLuint, GLint, GLfloat*);
    void   (*GetAttachedShaders)(GLuint, GLsizei, GLsizei*, GLuint*);
    void   (*Uniform1i)(GLint, GLint);
    void   (*Uniform1f)(GLint, GLfloat);
    void   (*Uniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
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
    OITBIND(GetError,               "glGetError");
    OITBIND(GetBooleanv,            "glGetBooleanv");
    OITBIND(IsEnabled,              "glIsEnabled");
    OITBIND(Enable,                 "glEnable");
    OITBIND(Disable,                "glDisable");
    OITBIND(BlendFuncSeparate,      "glBlendFuncSeparate");
    OITBIND(Viewport,               "glViewport");
    OITBIND(DepthMask,              "glDepthMask");
    OITBIND(DepthFunc,              "glDepthFunc");
    OITBIND(ColorMask,              "glColorMask");
    OITBIND(ClearBufferfv,          "glClearBufferfv");
    OITBIND(GenTextures,            "glGenTextures");
    OITBIND(DeleteTextures,         "glDeleteTextures");
    OITBIND(BindTexture,            "glBindTexture");
    OITBIND(TexImage2D,             "glTexImage2D");
    OITBIND(TexImage2DMultisample,  "glTexImage2DMultisample");
    OITBIND(TexParameteri,          "glTexParameteri");
    OITBIND(CopyTexSubImage2D,      "glCopyTexSubImage2D");
    OITBIND(ActiveTexture,          "glActiveTexture");
    OITBIND(GenFramebuffers,        "glGenFramebuffers");
    OITBIND(DeleteFramebuffers,     "glDeleteFramebuffers");
    OITBIND(BindFramebuffer,        "glBindFramebuffer");
    OITBIND(FramebufferTexture2D,   "glFramebufferTexture2D");
    OITBIND(CheckFramebufferStatus, "glCheckFramebufferStatus");
    OITBIND(DrawBuffers,            "glDrawBuffers");
    OITBIND(DrawBuffer,             "glDrawBuffer");
    OITBIND(ReadBuffer,             "glReadBuffer");
    OITBIND(BlitFramebuffer,        "glBlitFramebuffer");
    OITBIND(ReadPixels,             "glReadPixels");
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
    OITBIND(GetUniformiv,           "glGetUniformiv");
    OITBIND(GetUniformfv,           "glGetUniformfv");
    OITBIND(GetAttachedShaders,     "glGetAttachedShaders");
    OITBIND(Uniform1i,              "glUniform1i");
    OITBIND(Uniform1f,              "glUniform1f");
    OITBIND(Uniform4f,              "glUniform4f");
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
bool  g_foliageCensus = false;  // source-classified foliage only; read-only
bool  g_materialModeCensus = false; // explicit MTR NWN_ALPHA_MODE; read-only
bool  g_materialIdentityCensus = false; // stock Material + texture identity
bool  g_textureCensus = false;  // explicit, slow CAurTexture bind-name diagnostic
bool  g_foliageShader = false;  // compile an inert MRT branch into stock alpha shaders
bool  g_foliageReplay = false;  // private late bucket-1 accumulation proof
bool  g_foliageReplayNoDepth = false; // diagnostic: isolate copied-depth rejection
bool  g_foliageDepthless = false; // original bucket-1 colour, no alpha-card depth write
bool  g_foliageVisible = false; // depth-writing core plus raw-alpha OIT fringe
bool  g_foliageA2c = false;     // native single-pass alpha-to-coverage foliage
bool  g_visibleBucketFinalize = false;
bool  g_visibleResolveStage = false;
bool  g_visibleAccumReady = false;
GLint g_visibleAccumFbo = -1;
GLint g_visibleAccumViewport[4] = {};
int   g_visibleFinalizeBucket = 6; // after water, before desktop depth-based fog
bool  g_settingsRead = false;
bool  g_failed    = false;      // hard failure; stop trying, stay out of the way
float g_testColor[3] = { 1.0f, 0.25f, 0.25f };
float g_testAlpha = 0.35f;
float g_alphaGain = 1.0f;       // preserve authored alpha unless explicitly tuned
float g_coreCutoff = 0.50f;     // solid/depth core; lower alpha remains soft OIT

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
    const char* foliage = getenv("NWN_OIT_FOLIAGE_CENSUS");
    g_foliageCensus = (foliage && *foliage && *foliage != '0');
    const char* materialModeCensus = getenv("NWN_ALPHA_MODE_CENSUS");
    g_materialModeCensus = (materialModeCensus && *materialModeCensus &&
                            *materialModeCensus != '0');
    const char* materialIdentityCensus = getenv("NWN_ALPHA_IDENTITY_CENSUS");
    g_materialIdentityCensus = (materialIdentityCensus &&
                                *materialIdentityCensus &&
                                *materialIdentityCensus != '0');
    const char* textureCensus = getenv("NWN_OIT_TEXTURE_CENSUS");
    g_textureCensus = (textureCensus && *textureCensus && *textureCensus != '0');
    const char* foliageShader = getenv("NWN_OIT_FOLIAGE_SHADER");
    g_foliageShader = (foliageShader && *foliageShader && *foliageShader != '0');
    const char* foliageReplay = getenv("NWN_OIT_FOLIAGE_REPLAY");
    g_foliageReplay = (foliageReplay && *foliageReplay && *foliageReplay != '0');
    const char* foliageReplayNoDepth = getenv("NWN_OIT_FOLIAGE_REPLAY_NO_DEPTH");
    g_foliageReplayNoDepth = (foliageReplayNoDepth && *foliageReplayNoDepth &&
                              *foliageReplayNoDepth != '0');
    const char* foliageDepthless = getenv("NWN_OIT_FOLIAGE_DEPTHLESS");
    g_foliageDepthless = (foliageDepthless && *foliageDepthless &&
                          *foliageDepthless != '0');
    const char* foliageVisible = getenv("NWN_OIT_FOLIAGE_VISIBLE");
    g_foliageVisible = (foliageVisible && *foliageVisible && *foliageVisible != '0');
    const char* foliageA2c = getenv("NWN_A2C_FOLIAGE");
    g_foliageA2c = (foliageA2c && *foliageA2c && *foliageA2c != '0');
    g_alphaGain = env_float("NWN_OIT_ALPHA_GAIN", 1.0f);
    if (g_alphaGain < 0.25f) g_alphaGain = 0.25f;
    if (g_alphaGain > 8.0f) g_alphaGain = 8.0f;
    g_coreCutoff = env_float("NWN_OIT_CORE_CUTOFF", 0.50f);
    if (g_coreCutoff < 0.10f) g_coreCutoff = 0.10f;
    if (g_coreCutoff > 0.90f) g_coreCutoff = 0.90f;
    if (const char* finalizeBucket = getenv("NWN_OIT_FINALIZE_BUCKET")) {
        const int parsed = atoi(finalizeBucket);
        if (parsed >= 4 && parsed <= 8) g_visibleFinalizeBucket = parsed;
    }
    if (g_foliageVisible) {
        g_foliageReplay = true;
        g_foliageShader = true;
    }
    if (g_foliageA2c) g_foliageShader = true;
    if (g_foliageReplay) g_foliageShader = true;
    if (g_census)
        fprintf(stderr, "[oit] Phase 2a census enabled: reports blend/depth/cull "
                        "state once per (bucket, program) pair. Read-only -- no "
                        "draw is redirected, duplicated or filtered.\n");
    if (g_foliageCensus)
        fprintf(stderr, "[oit][foliage] source-classified census enabled. "
                        "Read-only -- normal foliage colour/depth and the OIT "
                        "targets are untouched.\n");
    if (g_materialModeCensus)
        fprintf(stderr, "[oit][material-mode] NWN_ALPHA_MODE census enabled. "
                        "Valid non-negative modes are read-only; no draw is "
                        "redirected, suppressed, replayed or restated.\n");
    if (g_materialIdentityCensus)
        fprintf(stderr, "[oit][material-identity] stock-path census enabled. "
                        "Read-only; native shader selection and draw state remain intact.\n");
    if (g_textureCensus)
        fprintf(stderr, "[oit][texture] explicit bind-name census enabled. "
                        "Diagnostic only; the texture hook uses a slow, "
                        "trampoline-free original call.\n");
    if (g_foliageShader)
        fprintf(stderr, "[oit][foliage] opt-in MRT shader branch enabled. "
                        "The branch defaults off; this milestone does not route "
                        "or suppress engine draws.\n");
    if (g_foliageReplay)
        fprintf(stderr, "[oit][foliage] private late bucket-1 replay enabled. "
                        "Accumulation is read back for proof but not composited.\n");
    if (g_foliageReplay && g_foliageReplayNoDepth)
        fprintf(stderr, "[oit][foliage] replay depth diagnostic enabled: "
                        "GL_ALWAYS isolates geometry/MRT routing from copied depth.\n");
    if (g_foliageDepthless)
        fprintf(stderr, "[oit][foliage] bucket-1 depthless original enabled: "
                        "screen colour is native, depth writes are restored after each draw.\n");
    if (g_foliageVisible)
        fprintf(stderr, "[oit] VISIBLE hybrid foliage route enabled: alpha>=%.2f "
                        "is an opaque two-sided depth core; alpha 0.01..%.2f is "
                        "two-sided raw-alpha OIT; alphaGain=%.2f "
                        "resolveAfterBucket=%d.\n",
                g_coreCutoff, g_coreCutoff, g_alphaGain,
                g_visibleFinalizeBucket);
    if (g_foliageA2c)
        fprintf(stderr, "[a2c] foliage alpha-to-coverage requested; it will "
                        "activate only on a live framebuffer with samples>=2.\n");
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
GLuint g_texDepth    = 0;      // copied completed scene depth for late replay
int    g_w = 0, g_h = 0;

// A2C emitter path. This depth image deliberately contains ordinary opaque
// geometry but not alpha-to-coverage foliage. Additive emitters compare
// against it in their own fragment shader, so foliage coverage cannot erase
// fire while walls and character bodies still occlude it normally.
GLuint g_a2cOpaqueDepthFbo = 0;
GLuint g_a2cOpaqueDepthTex = 0;
int    g_a2cOpaqueDepthW = 0, g_a2cOpaqueDepthH = 0;
int    g_a2cOpaqueDepthSamples = 0;
bool   g_a2cOpaqueDepthReady = false;
bool   g_a2cOpaqueDuplicatePending = false;
unsigned g_a2cOpaqueDuplicateDraws = 0;

// Texture units this pass borrows. High on purpose: the shadow module owns low
// ones, and every binding here is saved and restored anyway.
const GLenum kUnitCombined = GL_TEXTURE0 + 12;
const GLenum kUnitSum      = GL_TEXTURE0 + 13;
const GLenum kUnitTransl   = GL_TEXTURE0 + 14;
const GLenum kUnitA2cOpaqueDepth = GL_TEXTURE0 + 15;

GLuint make_target(GLint internalFormat, GLenum format, GLenum type,
                   int w, int h) {
    GLuint t = 0;
    g.GenTextures(1, &t);
    if (!t) return 0;
    g.BindTexture(GL_TEXTURE_2D, t);
    g.TexImage2D(GL_TEXTURE_2D, 0, internalFormat, w, h, 0, format, type, nullptr);
    g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

void destroy_accum_targets() {
    if (g_fbo)         { g.DeleteFramebuffers(1, &g_fbo);      g_fbo = 0; }
    if (g_texCombined) { g.DeleteTextures(1, &g_texCombined);  g_texCombined = 0; }
    if (g_texSum)      { g.DeleteTextures(1, &g_texSum);       g_texSum = 0; }
    if (g_texTransl)   { g.DeleteTextures(1, &g_texTransl);    g_texTransl = 0; }
    if (g_texDepth)    { g.DeleteTextures(1, &g_texDepth);     g_texDepth = 0; }
    g_w = g_h = 0;
}

void destroy_a2c_opaque_depth() {
    if (g_a2cOpaqueDepthFbo) {
        g.DeleteFramebuffers(1, &g_a2cOpaqueDepthFbo);
        g_a2cOpaqueDepthFbo = 0;
    }
    if (g_a2cOpaqueDepthTex) {
        g.DeleteTextures(1, &g_a2cOpaqueDepthTex);
        g_a2cOpaqueDepthTex = 0;
    }
    g_a2cOpaqueDepthW = g_a2cOpaqueDepthH = 0;
    g_a2cOpaqueDepthSamples = 0;
    g_a2cOpaqueDepthReady = false;
}

void destroy_targets() {
    destroy_accum_targets();
    destroy_a2c_opaque_depth();
}

bool ensure_a2c_opaque_depth(int w, int h, int samples) {
    if (g_a2cOpaqueDepthFbo && g_a2cOpaqueDepthTex &&
        w == g_a2cOpaqueDepthW && h == g_a2cOpaqueDepthH &&
        samples == g_a2cOpaqueDepthSamples)
        return true;
    if (g_a2cOpaqueDepthFbo) g.DeleteFramebuffers(1, &g_a2cOpaqueDepthFbo);
    if (g_a2cOpaqueDepthTex) g.DeleteTextures(1, &g_a2cOpaqueDepthTex);
    g_a2cOpaqueDepthFbo = g_a2cOpaqueDepthTex = 0;
    g_a2cOpaqueDepthReady = false;
    g_a2cOpaqueDepthSamples = 0;
    if (w <= 0 || h <= 0 || samples < 2) return false;

    g.ActiveTexture(kUnitA2cOpaqueDepth);
    g.GenTextures(1, &g_a2cOpaqueDepthTex);
    g.BindTexture(GL_TEXTURE_2D_MULTISAMPLE, g_a2cOpaqueDepthTex);
    g.TexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples,
                            GL_DEPTH_COMPONENT24, w, h, GL_TRUE);
    g.GenFramebuffers(1, &g_a2cOpaqueDepthFbo);
    if (!g_a2cOpaqueDepthTex || !g_a2cOpaqueDepthFbo) {
        destroy_a2c_opaque_depth();
        return false;
    }
    g.BindFramebuffer(GL_FRAMEBUFFER, g_a2cOpaqueDepthFbo);
    g.FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                           GL_TEXTURE_2D_MULTISAMPLE,
                           g_a2cOpaqueDepthTex, 0);
    g.DrawBuffer(GL_NONE);
    g.ReadBuffer(GL_NONE);
    if (g.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "[a2c][emitter] opaque-depth framebuffer incomplete\n");
        destroy_a2c_opaque_depth();
        return false;
    }
    g_a2cOpaqueDepthW = w;
    g_a2cOpaqueDepthH = h;
    g_a2cOpaqueDepthSamples = samples;
    fprintf(stderr, "[a2c][emitter] opaque-only depth target ready: %dx%d D24, %dx MSAA\n",
            w, h, samples);
    return true;
}

// Creates or resizes the accumulation targets. Caller has already saved the
// framebuffer binding and the texture binding on unit kUnitCombined.
bool ensure_targets(int w, int h) {
    if (g_fbo && w == g_w && h == g_h) return true;
    destroy_accum_targets();
    if (w <= 0 || h <= 0) return false;

    g.ActiveTexture(kUnitCombined);
    g_texCombined = make_target(GL_RGBA16F, GL_RGBA, GL_FLOAT, w, h);
    g_texSum      = make_target(GL_R16F,    GL_RED,  GL_FLOAT, w, h);
    g_texTransl   = make_target(GL_R16F,    GL_RED,  GL_FLOAT, w, h);
    g_texDepth    = make_target(GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT,
                                GL_UNSIGNED_INT, w, h);
    if (!g_texCombined || !g_texSum || !g_texTransl || !g_texDepth) {
        fprintf(stderr, "[oit] target allocation failed at %dx%d\n", w, h);
        destroy_accum_targets();
        return false;
    }

    g.GenFramebuffers(1, &g_fbo);
    if (!g_fbo) { destroy_accum_targets(); return false; }
    g.BindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    g.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + 0, GL_TEXTURE_2D, g_texCombined, 0);
    g.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + 1, GL_TEXTURE_2D, g_texSum,      0);
    g.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + 2, GL_TEXTURE_2D, g_texTransl,   0);
    g.FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                           g_texDepth, 0);
    const GLenum bufs[3] = { GL_COLOR_ATTACHMENT0 + 0,
                             GL_COLOR_ATTACHMENT0 + 1,
                             GL_COLOR_ATTACHMENT0 + 2 };
    g.DrawBuffers(3, bufs);

    const GLenum status = g.CheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "[oit] MRT framebuffer incomplete (0x%04X) at %dx%d\n",
                (unsigned)status, w, h);
        destroy_accum_targets();
        return false;
    }
    g_w = w; g_h = h;
    fprintf(stderr, "[oit] accumulation targets ready: %dx%d, 3 attachments "
                    "(RGBA16F combined, R16F sum, R16F translucence, copied D24)\n",
            w, h);
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
    // fTotal>0 gates the whole thing exactly as the console shader does. With no
    // transparent fragment, the ONE/SRC_ALPHA composite must emit (rgb=0,a=1):
    //   out.rgb = 0 + scene.rgb * 1
    // Alpha zero is not the identity for this blend mode; it erases the scene.
    static const char* fs =
        "#version 330 compatibility\n"
        "uniform sampler2D oitCombined;\n"
        "uniform sampler2D oitSum;\n"
        "uniform sampler2D oitTranslucence;\n"
        "void main(){\n"
        "  vec2 sz = vec2(textureSize(oitCombined,0));\n"
        "  vec2 uv = gl_FragCoord.xy / sz;\n"
        "  float fTotal = texture(oitSum, uv).r;\n"
        "  vec4 Color = vec4(0.0, 0.0, 0.0, 1.0);\n"
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
    GLint     texCombined, texSum, texTransl, texA2cOpaqueDepth;
    GLboolean depthMask;
    GLboolean depthTest;
    GLint     depthFunc;
    GLboolean blend;
    GLboolean cull;
    GLboolean scissor;
    GLboolean colorMask[4];
    GLint     blendSrcRGB, blendDstRGB, blendSrcAlpha, blendDstAlpha;
};

void save_state(SavedState& s) {
    g.GetIntegerv(GL_FRAMEBUFFER_BINDING, &s.fbo);
    g.GetIntegerv(GL_VIEWPORT,            s.viewport);
    g.GetIntegerv(GL_CURRENT_PROGRAM,     &s.program);
    g.GetIntegerv(GL_ACTIVE_TEXTURE,      &s.activeTexture);
    g.GetBooleanv(GL_DEPTH_WRITEMASK,     &s.depthMask);
    g.GetIntegerv(GL_DEPTH_FUNC,          &s.depthFunc);
    g.GetBooleanv(GL_COLOR_WRITEMASK,      s.colorMask);
    s.depthTest = g.IsEnabled(GL_DEPTH_TEST);
    s.blend     = g.IsEnabled(GL_BLEND);
    s.cull      = g.IsEnabled(GL_CULL_FACE);
    s.scissor   = g.IsEnabled(GL_SCISSOR_TEST);
    g.GetIntegerv(GL_BLEND_SRC_RGB,   &s.blendSrcRGB);
    g.GetIntegerv(GL_BLEND_DST_RGB,   &s.blendDstRGB);
    g.GetIntegerv(GL_BLEND_SRC_ALPHA, &s.blendSrcAlpha);
    g.GetIntegerv(GL_BLEND_DST_ALPHA, &s.blendDstAlpha);
    g.ActiveTexture(kUnitCombined); g.GetIntegerv(GL_TEXTURE_BINDING_2D, &s.texCombined);
    g.ActiveTexture(kUnitSum);      g.GetIntegerv(GL_TEXTURE_BINDING_2D, &s.texSum);
    g.ActiveTexture(kUnitTransl);   g.GetIntegerv(GL_TEXTURE_BINDING_2D, &s.texTransl);
    g.ActiveTexture(kUnitA2cOpaqueDepth);
    g.GetIntegerv(GL_TEXTURE_BINDING_2D_MULTISAMPLE, &s.texA2cOpaqueDepth);
}

void restore_state(const SavedState& s) {
    g.ActiveTexture(kUnitA2cOpaqueDepth);
    g.BindTexture(GL_TEXTURE_2D_MULTISAMPLE, (GLuint)s.texA2cOpaqueDepth);
    g.ActiveTexture(kUnitTransl);   g.BindTexture(GL_TEXTURE_2D, (GLuint)s.texTransl);
    g.ActiveTexture(kUnitSum);      g.BindTexture(GL_TEXTURE_2D, (GLuint)s.texSum);
    g.ActiveTexture(kUnitCombined); g.BindTexture(GL_TEXTURE_2D, (GLuint)s.texCombined);
    g.ActiveTexture((GLenum)s.activeTexture);
    g.BlendFuncSeparate((GLenum)s.blendSrcRGB,   (GLenum)s.blendDstRGB,
                        (GLenum)s.blendSrcAlpha, (GLenum)s.blendDstAlpha);
    if (s.blend)     g.Enable(GL_BLEND);      else g.Disable(GL_BLEND);
    if (s.depthTest) g.Enable(GL_DEPTH_TEST); else g.Disable(GL_DEPTH_TEST);
    if (s.cull)      g.Enable(GL_CULL_FACE);  else g.Disable(GL_CULL_FACE);
    if (s.scissor)   g.Enable(GL_SCISSOR_TEST); else g.Disable(GL_SCISSOR_TEST);
    g.DepthMask(s.depthMask);
    g.DepthFunc((GLenum)s.depthFunc);
    g.ColorMask(s.colorMask[0], s.colorMask[1], s.colorMask[2], s.colorMask[3]);
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
#define GL_BLEND_EQUATION_RGB   0x8009

struct CensusEntry {
    int    bucket;
    GLuint program;
};
CensusEntry g_censusSeen[192];
unsigned    g_censusCount = 0;

// Shader object IDs are valid only for this process. The stable identity is
// the source signature detected by the existing glShaderSource interposer;
// this bounded registry merely carries that identity to live draw programs.
GLuint   g_foliageFragments[256] = {};
unsigned g_foliageFragmentCount = 0;

// Programs are classified once and then reused by both the census and the
// forthcoming draw router. A negative result is rescanned when the fragment
// registry grows: NWN can compile/link a program before the interposer has seen
// every stock variant, so caching "not foliage" forever would make startup
// order decide rendering behaviour.
struct FoliageProgram {
    GLuint program;
    GLuint fragment;
    GLint  alphaDiscardLoc;
    GLint  materialModeLoc;
    GLint  oitPassLoc;
    GLint  oitFringeOnlyLoc;
    GLint  oitCutoffLoc;
    GLint  oitAlphaGainLoc;
    GLint  oitDepthPassLoc;
    GLint  oitOpaqueCoreLoc;
    GLint  a2cPassLoc;
    GLint  a2cEmitterPassLoc;
    GLint  a2cEmitterDepthLoc;
    GLint  a2cEmitterViewportLoc;
    GLint  a2cEmitterBiasLoc;
    GLint  a2cEmitterSamplesLoc;
    unsigned fragmentGeneration;
};
FoliageProgram g_foliagePrograms[384] = {};
unsigned       g_foliageProgramCount = 0;
FoliageProgram* g_immediateProgram = nullptr;
bool           g_immediatePrepared = false;
bool           g_immediateActive = false;
unsigned       g_immediateDraws = 0;
GLint          g_immediateSceneFbo = -1;
GLint          g_immediateViewport[4] = {};
bool           g_privateReplayActive = false;
bool           g_privateDepthReplayActive = false;
unsigned       g_privateReplayDraws = 0;
unsigned       g_privateDepthReplayDraws = 0;
bool           g_originalDepthOverride = false;
GLboolean      g_originalDepthMask = GL_TRUE;
GLboolean      g_originalColorMask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
GLboolean      g_originalBlend = GL_FALSE;
GLboolean      g_originalCull = GL_FALSE;
GLboolean      g_originalA2c = GL_FALSE;
GLboolean      g_originalDepthTest = GL_TRUE;
GLint          g_originalDepthFunc = GL_LESS;
GLint          g_originalBlendSrcRGB = GL_ONE;
GLint          g_originalBlendDstRGB = GL_ONE;
GLint          g_originalBlendSrcAlpha = GL_ONE;
GLint          g_originalBlendDstAlpha = GL_ONE;
unsigned       g_originalDepthlessDraws = 0;
GLint          g_originalOpaqueCoreLoc = -1;
GLint          g_originalA2cLoc = -1;
GLint          g_originalEmitterPassLoc = -1;
bool           g_emitterDepthBorrowed = false;
GLint          g_emitterOldActiveTexture = GL_TEXTURE0;
GLint          g_emitterOldDepthTexture = 0;
char           g_boundTextureNames[32][128] = {};
struct SeenTextureName { unsigned unit; char name[128]; };
SeenTextureName g_seenTextureNames[256] = {};
unsigned        g_seenTextureNameCount = 0;

void*           g_currentMaterial = nullptr;
char            g_currentMaterialTexture0[128] = {};
unsigned long long g_currentMaterialSerial = 0;
int             g_currentMaterialBucket = -1;

struct MaterialResourceIdentity {
    void* material;
    void* sharedMaterial;
    int mode;
    char name[128];
};
MaterialResourceIdentity g_materialResources[4096] = {};
unsigned g_materialResourceCount = 0;

struct SharedMaterialMode {
    void* sharedMaterial;
    int mode;
};
SharedMaterialMode g_sharedMaterialModes[4096] = {};
unsigned g_sharedMaterialModeCount = 0;

int shared_material_mode(void* sharedMaterial) {
    for (unsigned i = 0; i < g_sharedMaterialModeCount; ++i)
        if (g_sharedMaterialModes[i].sharedMaterial == sharedMaterial)
            return g_sharedMaterialModes[i].mode;
    return 0;
}

const char* material_resource_name(void* material) {
    for (unsigned i = 0; i < g_materialResourceCount; ++i)
        if (g_materialResources[i].material == material)
            return g_materialResources[i].name;
    return "";
}

int material_resource_mode(void* material) {
    for (unsigned i = 0; i < g_materialResourceCount; ++i)
        if (g_materialResources[i].material == material)
            return g_materialResources[i].mode;
    return 0;
}

struct SeenMaterialIdentity {
    int bucket;
    GLuint program;
    GLuint texture0;
    void* material;
    int mode;
    char materialName[128];
    char name[128];
};
SeenMaterialIdentity g_seenMaterialIdentities[512] = {};
unsigned g_seenMaterialIdentityCount = 0;

void report_material_identity(int bucket, GLuint program, bool foliage) {
    if (!g_materialIdentityCensus || !foliage ||
        (bucket != 1 && bucket != 3) || !g_currentMaterial ||
        g_currentMaterialBucket != bucket)
        return;

    GLint oldActive = GL_TEXTURE0;
    GLint texture0 = 0;
    g.GetIntegerv(GL_ACTIVE_TEXTURE, &oldActive);
    g.ActiveTexture(GL_TEXTURE0);
    g.GetIntegerv(GL_TEXTURE_BINDING_2D, &texture0);
    g.ActiveTexture((GLenum)oldActive);
    const char* materialName = material_resource_name(g_currentMaterial);
    const int materialMode = material_resource_mode(g_currentMaterial);

    for (unsigned i = 0; i < g_seenMaterialIdentityCount; ++i) {
        const SeenMaterialIdentity& seen = g_seenMaterialIdentities[i];
        if (seen.bucket == bucket && seen.program == program &&
            seen.texture0 == (GLuint)texture0 &&
            seen.material == g_currentMaterial &&
            seen.mode == materialMode &&
            std::strcmp(seen.materialName, materialName) == 0 &&
            std::strcmp(seen.name, g_currentMaterialTexture0) == 0)
            return;
    }
    if (g_seenMaterialIdentityCount >=
        sizeof(g_seenMaterialIdentities) / sizeof(g_seenMaterialIdentities[0]))
        return;

    SeenMaterialIdentity& entry =
        g_seenMaterialIdentities[g_seenMaterialIdentityCount++];
    entry.bucket = bucket;
    entry.program = program;
    entry.texture0 = (GLuint)texture0;
    entry.material = g_currentMaterial;
    entry.mode = materialMode;
    std::memset(entry.materialName, 0, sizeof(entry.materialName));
    std::memcpy(entry.materialName, materialName,
                strnlen(materialName, sizeof(entry.materialName) - 1));
    std::memcpy(entry.name, g_currentMaterialTexture0, sizeof(entry.name));
    fprintf(stderr,
            "[oit][material-identity-census] bucket=%d program=%u material=%p "
            "serial=%llu mtr=%s mode=%d texture0=%u name=%s\n",
            bucket, (unsigned)program, g_currentMaterial,
            g_currentMaterialSerial,
            materialName[0] ? materialName : "<unknown>", materialMode,
            (unsigned)texture0,
            g_currentMaterialTexture0[0] ? g_currentMaterialTexture0 : "<none>");
}

void report_foliage_texture_names(int bucket, GLuint program) {
    if (!g_textureCensus || bucket != 1) return;
    for (unsigned unit = 0; unit < 32; ++unit) {
        const char* name = g_boundTextureNames[unit];
        if (!name[0]) continue;
        bool seen = false;
        for (unsigned i = 0; i < g_seenTextureNameCount; ++i) {
            if (g_seenTextureNames[i].unit == unit &&
                std::strcmp(g_seenTextureNames[i].name, name) == 0) {
                seen = true;
                break;
            }
        }
        if (seen || g_seenTextureNameCount >= 256) continue;
        SeenTextureName& entry = g_seenTextureNames[g_seenTextureNameCount++];
        entry.unit = unit;
        std::memcpy(entry.name, name, sizeof(entry.name) - 1);
        entry.name[sizeof(entry.name) - 1] = '\0';
        fprintf(stderr, "[oit][foliage-texture] bucket=%d program=%u unit=%u "
                        "name=%s\n", bucket, (unsigned)program, unit, name);
    }
}

FoliageProgram* foliage_program(GLuint program) {
    if (!program || !g.GetAttachedShaders) return nullptr;
    FoliageProgram* entry = nullptr;
    for (unsigned i = 0; i < g_foliageProgramCount; ++i) {
        if (g_foliagePrograms[i].program == program) {
            entry = &g_foliagePrograms[i];
            break;
        }
    }
    if (!entry) {
        if (g_foliageProgramCount >=
            sizeof(g_foliagePrograms) / sizeof(g_foliagePrograms[0])) {
            static bool warned = false;
            if (!warned) {
                warned = true;
                fprintf(stderr, "[oit][foliage] program registry full; later "
                                "programs cannot be routed\n");
            }
            return nullptr;
        }
        entry = &g_foliagePrograms[g_foliageProgramCount++];
        entry->program = program;
        entry->alphaDiscardLoc = -1;
        entry->materialModeLoc = -1;
        entry->oitPassLoc = -1;
        entry->oitFringeOnlyLoc = -1;
        entry->oitCutoffLoc = -1;
        entry->oitAlphaGainLoc = -1;
        entry->oitDepthPassLoc = -1;
        entry->oitOpaqueCoreLoc = -1;
        entry->a2cPassLoc = -1;
        entry->a2cEmitterPassLoc = -1;
        entry->a2cEmitterDepthLoc = -1;
        entry->a2cEmitterViewportLoc = -1;
        entry->a2cEmitterBiasLoc = -1;
        entry->a2cEmitterSamplesLoc = -1;
    }
    if (entry->fragment ||
        entry->fragmentGeneration == g_foliageFragmentCount)
        return entry;

    entry->fragmentGeneration = g_foliageFragmentCount;
    GLuint attached[8] = {};
    GLsizei count = 0;
    g.GetAttachedShaders(program, 8, &count, attached);
    for (GLsizei i = 0; i < count; ++i) {
        for (unsigned j = 0; j < g_foliageFragmentCount; ++j) {
            if (attached[i] != g_foliageFragments[j]) continue;
            entry->fragment = attached[i];
            entry->alphaDiscardLoc = g.GetUniformLocation
                ? g.GetUniformLocation(program, "fAlphaDiscardValue") : -1;
            entry->materialModeLoc = g.GetUniformLocation
                ? g.GetUniformLocation(program, "NWN_ALPHA_MODE") : -1;
            entry->oitPassLoc = g.GetUniformLocation
                ? g.GetUniformLocation(program, "nwnOitPass") : -1;
            entry->oitFringeOnlyLoc = g.GetUniformLocation
                ? g.GetUniformLocation(program, "nwnOitFringeOnly") : -1;
            entry->oitCutoffLoc = g.GetUniformLocation
                ? g.GetUniformLocation(program, "nwnOitCutoff") : -1;
            entry->oitAlphaGainLoc = g.GetUniformLocation
                ? g.GetUniformLocation(program, "nwnOitAlphaGain") : -1;
            entry->oitDepthPassLoc = g.GetUniformLocation
                ? g.GetUniformLocation(program, "nwnOitDepthPass") : -1;
            entry->oitOpaqueCoreLoc = g.GetUniformLocation
                ? g.GetUniformLocation(program, "nwnOitOpaqueCore") : -1;
            entry->a2cPassLoc = g.GetUniformLocation
                ? g.GetUniformLocation(program, "nwnA2cPass") : -1;
            entry->a2cEmitterPassLoc = g.GetUniformLocation
                ? g.GetUniformLocation(program, "nwnA2cEmitterPass") : -1;
            entry->a2cEmitterDepthLoc = g.GetUniformLocation
                ? g.GetUniformLocation(program, "nwnA2cEmitterOpaqueDepth") : -1;
            entry->a2cEmitterViewportLoc = g.GetUniformLocation
                ? g.GetUniformLocation(program, "nwnA2cEmitterViewport") : -1;
            entry->a2cEmitterBiasLoc = g.GetUniformLocation
                ? g.GetUniformLocation(program, "nwnA2cEmitterDepthBias") : -1;
            entry->a2cEmitterSamplesLoc = g.GetUniformLocation
                ? g.GetUniformLocation(program, "nwnA2cEmitterSamples") : -1;
            if (entry->a2cEmitterDepthLoc >= 0)
                g.Uniform1i(entry->a2cEmitterDepthLoc, 15);
            fprintf(stderr, "[oit][foliage] program=%u joined to source-classified "
                            "fragment=%u cutoffLoc=%d materialModeLoc=%d "
                            "oitPassLoc=%d\n",
                    (unsigned)program, (unsigned)entry->fragment,
                    (int)entry->alphaDiscardLoc, (int)entry->materialModeLoc,
                    (int)entry->oitPassLoc);
            return entry;
        }
    }
    return entry;
}

struct MaterialModeCensusEntry {
    int bucket;
    GLuint program;
    GLint mode;
};
MaterialModeCensusEntry g_materialModeCensusSeen[256] = {};
unsigned g_materialModeCensusCount = 0;

void report_material_mode_census(int bucket, GLuint program,
                                 FoliageProgram* foliageProgram) {
    if (!g_materialModeCensus || !foliageProgram || !g.GetUniformiv) return;

    GLint mode = 0;
    const GLint location = foliageProgram->materialModeLoc;
    if (location >= 0) g.GetUniformiv(program, location, &mode);

    for (unsigned i = 0; i < g_materialModeCensusCount; ++i) {
        const MaterialModeCensusEntry& seen = g_materialModeCensusSeen[i];
        if (seen.bucket == bucket && seen.program == program && seen.mode == mode)
            return;
    }
    if (g_materialModeCensusCount >=
        sizeof(g_materialModeCensusSeen) / sizeof(g_materialModeCensusSeen[0]))
        return;
    g_materialModeCensusSeen[g_materialModeCensusCount++] =
        {bucket, program, mode};

    GLfloat alphaDiscard = -9999.0f;
    if (foliageProgram->alphaDiscardLoc >= 0 && g.GetUniformfv)
        g.GetUniformfv(program, foliageProgram->alphaDiscardLoc, &alphaDiscard);
    const char* route = (bucket == 1 || bucket == 3)
        ? "ordinary-alpha-candidate"
        : "late-or-auxiliary-observation-only";
    fprintf(stderr,
            "[oit][material-mode-census] bucket=%d program=%u fragment=%u "
            "location=%d mode=%d alphaDiscard=%s%.4f route=%s\n",
            bucket, (unsigned)program, (unsigned)foliageProgram->fragment,
            (int)location, (int)mode,
            foliageProgram->alphaDiscardLoc < 0 ? "missing/" : "",
            alphaDiscard, route);
}

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
    const bool areaDrawWithoutBucket = bucket < 0 &&
                                       nwn_core::area_scene_draw_active();
    if (!g_glBound || (bucket < 0 && !areaDrawWithoutBucket &&
                       !g_privateReplayActive && !g_privateDepthReplayActive))
        return;

    GLint prog = 0;
    g.GetIntegerv(GL_CURRENT_PROGRAM, &prog);
    if (prog <= 0) return;

    FoliageProgram* foliageProgram = foliage_program((GLuint)prog);
    const bool foliage = foliageProgram && foliageProgram->fragment != 0;
    if (!g_privateReplayActive && !g_privateDepthReplayActive && foliage)
        report_foliage_texture_names(bucket, (GLuint)prog);
    if (!g_privateReplayActive && !g_privateDepthReplayActive)
        report_material_identity(bucket, (GLuint)prog, foliage);
    if (g_privateDepthReplayActive) {
        const bool eligible = foliage && foliageProgram->alphaDiscardLoc >= 0 &&
                              foliageProgram->oitDepthPassLoc >= 0;
        g.ColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        if (!eligible) {
            g.DepthMask(GL_FALSE);
            return;
        }
        g.Uniform1i(foliageProgram->oitDepthPassLoc, 1);
        g.Enable(GL_DEPTH_TEST);
        g.DepthFunc(GL_LESS);
        g.DepthMask(GL_TRUE);
        g.Disable(GL_BLEND);
        ++g_privateDepthReplayDraws;
        return;
    }
    if (g_privateReplayActive) {
        const bool eligible = foliage && foliageProgram->alphaDiscardLoc >= 0 &&
                              foliageProgram->oitPassLoc >= 0;
        g.DepthMask(GL_FALSE);
        if (!eligible) {
            // Bucket 1 is empirically the static alpha/card bucket, but keep a
            // hard filter anyway: a future engine/material variant must not
            // write ordinary colour into attachment 0 of the private MRT.
            g.ColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            return;
        }
        g.Uniform1i(foliageProgram->oitPassLoc, 1);
        if (foliageProgram->oitFringeOnlyLoc >= 0)
            g.Uniform1i(foliageProgram->oitFringeOnlyLoc,
                        g_foliageVisible ? 1 : 0);
        if (g_foliageVisible && foliageProgram->oitCutoffLoc >= 0) {
            g.Uniform1f(foliageProgram->oitCutoffLoc, g_coreCutoff);
        }
        if (foliageProgram->oitAlphaGainLoc >= 0)
            g.Uniform1f(foliageProgram->oitAlphaGainLoc,
                        g_foliageVisible ? g_alphaGain : 1.0f);
        g.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        g.Enable(GL_DEPTH_TEST);
        g.DepthFunc(g_foliageReplayNoDepth ? GL_ALWAYS
                                           : (g_foliageVisible ? GL_LESS : GL_LEQUAL));
        g.Enable(GL_BLEND);
        g.BlendFuncSeparate(GL_ONE, GL_SRC_ALPHA, GL_ONE, GL_SRC_ALPHA);
        ++g_privateReplayDraws;
        return;
    }

    // Keep the opaque-only depth image current after the static snapshot.
    // Bucket 2 is the empirically proven dynamic opaque stage; duplicating its
    // depth writes here adds characters to the private image without copying
    // the bucket-1/3 A2C foliage that must remain transparent to emitters.
    if (g_foliageA2c && g_a2cOpaqueDepthReady && bucket == 2) {
        GLboolean depthMask = GL_FALSE;
        g.GetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
        g_a2cOpaqueDuplicatePending = depthMask && g.IsEnabled(GL_DEPTH_TEST) &&
                                      !g.IsEnabled(GL_BLEND);
    }

    // NWNEE submits visible particle quads in bucket 6.  The torch flame uses
    // ordinary source-over blending there (not the additive bucket previously
    // inferred from an unrelated effect); some glow variants remain additive.
    // Both use a stock scene-colour fragment with no live alpha-discard uniform.
    // Route only those measured particle signatures. Their shader manually
    // tests opaque-only depth while hardware depth is disabled, so A2C foliage
    // cannot erase the effect and opaque geometry still can.
    if (g_foliageA2c && g_a2cOpaqueDepthReady &&
        (bucket == 6 || areaDrawWithoutBucket) && foliage &&
        foliageProgram->alphaDiscardLoc < 0 &&
        foliageProgram->a2cEmitterPassLoc >= 0 &&
        foliageProgram->a2cEmitterDepthLoc >= 0 &&
        foliageProgram->a2cEmitterViewportLoc >= 0 &&
        g.IsEnabled(GL_BLEND)) {
        GLint src = 0, dst = 0;
        g.GetIntegerv(GL_BLEND_SRC_RGB, &src);
        g.GetIntegerv(GL_BLEND_DST_RGB, &dst);
        const bool sourceOver = src == GL_SRC_ALPHA &&
                                dst == GL_ONE_MINUS_SRC_ALPHA;
        const bool additive = dst == GL_ONE &&
                              (src == GL_SRC_ALPHA || src == GL_ONE);
        if (sourceOver || additive) {
            if (!g_originalDepthOverride) {
                g.GetBooleanv(GL_DEPTH_WRITEMASK, &g_originalDepthMask);
                g.GetBooleanv(GL_COLOR_WRITEMASK, g_originalColorMask);
                g.GetIntegerv(GL_DEPTH_FUNC, &g_originalDepthFunc);
                g.GetIntegerv(GL_BLEND_SRC_RGB, &g_originalBlendSrcRGB);
                g.GetIntegerv(GL_BLEND_DST_RGB, &g_originalBlendDstRGB);
                g.GetIntegerv(GL_BLEND_SRC_ALPHA, &g_originalBlendSrcAlpha);
                g.GetIntegerv(GL_BLEND_DST_ALPHA, &g_originalBlendDstAlpha);
                g_originalBlend = g.IsEnabled(GL_BLEND);
                g_originalCull = g.IsEnabled(GL_CULL_FACE);
                g_originalDepthTest = g.IsEnabled(GL_DEPTH_TEST);
                g_originalA2c = g.IsEnabled(GL_SAMPLE_ALPHA_TO_COVERAGE);
                g_originalDepthOverride = true;
            }
            GLint vp[4] = {};
            g.GetIntegerv(GL_VIEWPORT, vp);
            g.GetIntegerv(GL_ACTIVE_TEXTURE, &g_emitterOldActiveTexture);
            g.ActiveTexture(kUnitA2cOpaqueDepth);
            g.GetIntegerv(GL_TEXTURE_BINDING_2D_MULTISAMPLE,
                          &g_emitterOldDepthTexture);
            g.BindTexture(GL_TEXTURE_2D_MULTISAMPLE, g_a2cOpaqueDepthTex);
            g.ActiveTexture((GLenum)g_emitterOldActiveTexture);
            g.Uniform1i(foliageProgram->a2cEmitterDepthLoc, 15);
            g.Uniform4f(foliageProgram->a2cEmitterViewportLoc,
                        (GLfloat)vp[0], (GLfloat)vp[1],
                        (GLfloat)vp[2], (GLfloat)vp[3]);
            if (foliageProgram->a2cEmitterBiasLoc >= 0)
                g.Uniform1f(foliageProgram->a2cEmitterBiasLoc, 0.00001f);
            if (foliageProgram->a2cEmitterSamplesLoc >= 0)
                g.Uniform1i(foliageProgram->a2cEmitterSamplesLoc,
                            g_a2cOpaqueDepthSamples);
            g.Uniform1i(foliageProgram->a2cEmitterPassLoc, 1);
            g_originalEmitterPassLoc = foliageProgram->a2cEmitterPassLoc;
            g_emitterDepthBorrowed = true;
            g.Disable(GL_DEPTH_TEST);
            g.DepthMask(GL_FALSE);
            static bool reportedEmitter = false;
            if (!reportedEmitter) {
                reportedEmitter = true;
                fprintf(stderr, "[a2c][emitter] bucket-6 scene particles now test "
                                "opaque-only depth; foliage coverage preserved "
                                "(dynamic opaque duplicates=%u)\n",
                        g_a2cOpaqueDuplicateDraws);
            }
        }
    }

    if (g_foliageA2c && (bucket == 1 || bucket == 3) && foliage &&
        foliageProgram->alphaDiscardLoc >= 0 &&
        foliageProgram->a2cPassLoc >= 0) {
        GLint samples = 0;
        g.GetIntegerv(GL_SAMPLES, &samples);
        static GLint reportedSamples = -1;
        if (samples != reportedSamples) {
            reportedSamples = samples;
            fprintf(stderr, "[a2c] live foliage framebuffer samples=%d: %s\n",
                    samples, samples >= 2 ? "alpha-to-coverage ACTIVE"
                                          : "A2C unavailable; native draw retained");
        }
        if (samples >= 2) {
            if (!g_originalDepthOverride) {
                g.GetBooleanv(GL_DEPTH_WRITEMASK, &g_originalDepthMask);
                g.GetBooleanv(GL_COLOR_WRITEMASK, g_originalColorMask);
                g.GetIntegerv(GL_DEPTH_FUNC, &g_originalDepthFunc);
                g.GetIntegerv(GL_BLEND_SRC_RGB, &g_originalBlendSrcRGB);
                g.GetIntegerv(GL_BLEND_DST_RGB, &g_originalBlendDstRGB);
                g.GetIntegerv(GL_BLEND_SRC_ALPHA, &g_originalBlendSrcAlpha);
                g.GetIntegerv(GL_BLEND_DST_ALPHA, &g_originalBlendDstAlpha);
                g_originalBlend = g.IsEnabled(GL_BLEND);
                g_originalCull = g.IsEnabled(GL_CULL_FACE);
                g_originalA2c = g.IsEnabled(GL_SAMPLE_ALPHA_TO_COVERAGE);
                g.DepthMask(GL_TRUE);
                g.DepthFunc(GL_LESS);
                g.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                g.Disable(GL_BLEND);
                g.Disable(GL_CULL_FACE);
                g.Enable(GL_SAMPLE_ALPHA_TO_COVERAGE);
                g_originalDepthOverride = true;
            }
            g.Uniform1i(foliageProgram->a2cPassLoc, 1);
            nwn_shadow_begin_a2c_receiver((unsigned int)prog);
            g_originalA2cLoc = foliageProgram->a2cPassLoc;
        }
    } else if (g_foliageVisible && (bucket == 1 || bucket == 3) && foliage &&
        foliageProgram->alphaDiscardLoc >= 0 &&
        foliageProgram->oitCutoffLoc >= 0 &&
        foliageProgram->oitOpaqueCoreLoc >= 0) {
        // Establish real scene depth while the native draw data is live.  The
        // private OIT replay later accepts only alpha below this threshold, so
        // the two paths do not double-blend.
        if (!g_originalDepthOverride) {
            g.GetBooleanv(GL_DEPTH_WRITEMASK, &g_originalDepthMask);
            g.GetBooleanv(GL_COLOR_WRITEMASK, g_originalColorMask);
            g.GetIntegerv(GL_DEPTH_FUNC, &g_originalDepthFunc);
            g.GetIntegerv(GL_BLEND_SRC_RGB, &g_originalBlendSrcRGB);
            g.GetIntegerv(GL_BLEND_DST_RGB, &g_originalBlendDstRGB);
            g.GetIntegerv(GL_BLEND_SRC_ALPHA, &g_originalBlendSrcAlpha);
            g.GetIntegerv(GL_BLEND_DST_ALPHA, &g_originalBlendDstAlpha);
            g_originalBlend = g.IsEnabled(GL_BLEND);
            g_originalCull = g.IsEnabled(GL_CULL_FACE);
            g_originalDepthTest = g.IsEnabled(GL_DEPTH_TEST);
            g.DepthMask(GL_TRUE);
            g.DepthFunc(GL_LESS);
            g.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            g.Disable(GL_BLEND);
            g.Disable(GL_CULL_FACE);
            g_originalDepthOverride = true;
        }
        g.Uniform1f(foliageProgram->oitCutoffLoc, g_coreCutoff);
        g.Uniform1i(foliageProgram->oitOpaqueCoreLoc, 1);
        g_originalOpaqueCoreLoc = foliageProgram->oitOpaqueCoreLoc;
        g_immediateProgram = foliageProgram;
    }

    if (g_foliageDepthless && bucket == 1 && foliage &&
        foliageProgram->alphaDiscardLoc >= 0) {
        if (!g_originalDepthOverride) {
            g.GetBooleanv(GL_DEPTH_WRITEMASK, &g_originalDepthMask);
            g.GetBooleanv(GL_COLOR_WRITEMASK, g_originalColorMask);
            g.DepthMask(GL_FALSE);
            g_originalDepthOverride = true;
        }
        ++g_originalDepthlessDraws;
    }

    report_material_mode_census(bucket, (GLuint)prog, foliageProgram);

    for (unsigned i = 0; i < g_censusCount; ++i)
        if (g_censusSeen[i].bucket == bucket && g_censusSeen[i].program == (GLuint)prog)
            return;                       // already reported this pair
    if (g_censusCount >= sizeof(g_censusSeen) / sizeof(g_censusSeen[0])) return;
    g_censusSeen[g_censusCount].bucket  = bucket;
    g_censusSeen[g_censusCount].program = (GLuint)prog;
    ++g_censusCount;

    // Replay mode uses this observer only to populate the source-derived
    // program cache. Keep it silent unless a census was explicitly requested.
    if (!g_foliageCensus && !g_census) return;
    if (g_foliageCensus && !g_census && !foliage) return;

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

    GLfloat alphaDiscard = -9999.0f;
    GLint alphaDiscardLoc = -1;
    if (foliage && g.GetUniformfv) {
        alphaDiscardLoc = foliageProgram->alphaDiscardLoc;
        if (alphaDiscardLoc >= 0)
            g.GetUniformfv((GLuint)prog, alphaDiscardLoc, &alphaDiscard);
    }

    if (foliage) {
        const char* verdict = alphaDiscardLoc < 0
            ? "REJECT - source matched but live cutoff uniform is absent"
            : alphaDiscard < 0.0f
                ? "REJECT - discard variant is inactive for this material"
                : !blend
                    ? "CUTOUT BASELINE - eligible for smooth-alpha conversion"
                    : (srcRGB == 0x0302 && dstRGB == 0x0303)
                        ? "ALREADY SRC_OVER - eligible for foliage OIT routing"
                        : "REJECT - non-src-over blend mode";
        fprintf(stderr, "[oit][foliage-census] bucket=%d program=%u fragment=%u "
                        "alphaDiscard=%s%.4f blend=%s rgb=%s/%s alpha=%s/%s "
                        "depthtest=%d depthwrite=%d cull=%d -> %s\n",
                bucket, (unsigned)prog, (unsigned)foliageProgram->fragment,
                alphaDiscardLoc < 0 ? "missing/" : "", alphaDiscard,
                blend ? "ON" : "off",
                blend_factor_name(srcRGB), blend_factor_name(dstRGB),
                blend_factor_name(srcA), blend_factor_name(dstA),
                depth ? 1 : 0, depthMask ? 1 : 0, cull ? 1 : 0, verdict);
        return;
    }

    fprintf(stderr, "[oit][census] bucket=%d program=%u blend=%s rgb=%s/%s "
                    "alpha=%s/%s depthtest=%d depthwrite=%d cull=%d  -> %s\n",
            bucket, (unsigned)prog, blend ? "ON" : "off",
            blend_factor_name(srcRGB), blend_factor_name(dstRGB),
            blend_factor_name(srcA),   blend_factor_name(dstA),
            depth ? 1 : 0, depthMask ? 1 : 0, cull ? 1 : 0,
            oit_verdict(blend, srcRGB, dstRGB));
}

void restore_original_draw_state() {
    if (g_originalEmitterPassLoc >= 0) {
        g.Uniform1i(g_originalEmitterPassLoc, 0);
        g_originalEmitterPassLoc = -1;
    }
    if (g_emitterDepthBorrowed) {
        g.ActiveTexture(kUnitA2cOpaqueDepth);
        g.BindTexture(GL_TEXTURE_2D_MULTISAMPLE,
                      (GLuint)g_emitterOldDepthTexture);
        g.ActiveTexture((GLenum)g_emitterOldActiveTexture);
        g_emitterDepthBorrowed = false;
    }
    if (g_originalA2cLoc >= 0) {
        g.Uniform1i(g_originalA2cLoc, 0);
        g_originalA2cLoc = -1;
    }
    if (g_originalOpaqueCoreLoc >= 0) {
        g.Uniform1i(g_originalOpaqueCoreLoc, 0);
        g_originalOpaqueCoreLoc = -1;
    }
    if (!g_originalDepthOverride) return;
    g.DepthMask(g_originalDepthMask);
    g.ColorMask(g_originalColorMask[0], g_originalColorMask[1],
                g_originalColorMask[2], g_originalColorMask[3]);
    g.DepthFunc((GLenum)g_originalDepthFunc);
    g.BlendFuncSeparate((GLenum)g_originalBlendSrcRGB,
                        (GLenum)g_originalBlendDstRGB,
                        (GLenum)g_originalBlendSrcAlpha,
                        (GLenum)g_originalBlendDstAlpha);
    if (g_originalBlend) g.Enable(GL_BLEND); else g.Disable(GL_BLEND);
    if (g_originalCull) g.Enable(GL_CULL_FACE); else g.Disable(GL_CULL_FACE);
    if (g_originalDepthTest) g.Enable(GL_DEPTH_TEST); else g.Disable(GL_DEPTH_TEST);
    if (g_originalA2c) g.Enable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    else g.Disable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    g_originalDepthOverride = false;
}

void census_observe_draw_after() {
    nwn_shadow_end_a2c_receiver();
    restore_original_draw_state();
    g_immediateProgram = nullptr;
}

void reset_foliage_pass_uniforms(GLuint restoreProgram) {
    for (unsigned i = 0; i < g_foliageProgramCount; ++i) {
        FoliageProgram& entry = g_foliagePrograms[i];
        if (!entry.fragment || entry.oitPassLoc < 0) continue;
        g.UseProgram(entry.program);
        g.Uniform1i(entry.oitPassLoc, 0);
        if (entry.oitFringeOnlyLoc >= 0)
            g.Uniform1i(entry.oitFringeOnlyLoc, 0);
        if (entry.oitDepthPassLoc >= 0)
            g.Uniform1i(entry.oitDepthPassLoc, 0);
        if (entry.oitOpaqueCoreLoc >= 0)
            g.Uniform1i(entry.oitOpaqueCoreLoc, 0);
        if (entry.a2cPassLoc >= 0)
            g.Uniform1i(entry.a2cPassLoc, 0);
        if (entry.a2cEmitterPassLoc >= 0)
            g.Uniform1i(entry.a2cEmitterPassLoc, 0);
    }
    g.UseProgram(restoreProgram);
}

bool private_foliage_replay(void* scene, const SavedState& st) {
    if (!scene || !g_fbo || !g_texDepth) return false;

    // At the configured visible finalize bucket this is the completed scene
    // depth: ordinary opaque geometry plus the injected console-style
    // alpha>=0.99 cores.  Water is already in colour on desktop but, as on the
    // console path, did not write depth.
    g.BindFramebuffer(GL_FRAMEBUFFER, (GLuint)st.fbo);
    g.ActiveTexture(kUnitCombined);
    g.BindTexture(GL_TEXTURE_2D, g_texDepth);
    g.CopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                        st.viewport[0], st.viewport[1], g_w, g_h);

    const GLfloat clearCombined[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    const GLfloat clearSum[4]      = {0.0f, 0.0f, 0.0f, 0.0f};
    const GLfloat clearTransl[4]   = {1.0f, 0.0f, 0.0f, 0.0f};
    g.BindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    g.Viewport(0, 0, g_w, g_h);
    g.ClearBufferfv(GL_COLOR, 0, clearCombined);
    g.ClearBufferfv(GL_COLOR, 1, clearSum);
    g.ClearBufferfv(GL_COLOR, 2, clearTransl);
    g.Disable(GL_SCISSOR_TEST);
    g.DepthMask(GL_FALSE);
    g.Enable(GL_DEPTH_TEST);
    g.DepthFunc(g_foliageVisible ? GL_LESS : GL_LEQUAL);
    if (g_foliageVisible) g.Disable(GL_CULL_FACE);
    g.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    g.Enable(GL_BLEND);
    g.BlendFuncSeparate(GL_ONE, GL_SRC_ALPHA, GL_ONE, GL_SRC_ALPHA);

    // Copying depth and preparing the MRT above borrow units 12..14 and leave
    // unit 12 active.  RenderDrawBucket uses the engine's texture-state cache;
    // when that cache says a material texture is already bound, it may omit
    // both glActiveTexture and glBindTexture.  Re-entering it with our unit
    // active therefore makes replayed alpha cards sample stale textures (or
    // bind their next texture on the wrong unit), producing camera-dependent
    // checker/UV garbage.  Re-establish the exact texture state observed at
    // bucket completion before giving control back to the engine.
    g.ActiveTexture(kUnitTransl);
    g.BindTexture(GL_TEXTURE_2D, (GLuint)st.texTransl);
    g.ActiveTexture(kUnitSum);
    g.BindTexture(GL_TEXTURE_2D, (GLuint)st.texSum);
    g.ActiveTexture(kUnitCombined);
    g.BindTexture(GL_TEXTURE_2D, (GLuint)st.texCombined);
    g.ActiveTexture((GLenum)st.activeTexture);

    g_privateReplayDraws = 0;
    g_privateReplayActive = true;
    const bool okStatic = nwn_core_replay_bucket(scene, 1);
    const bool okDynamic = !g_foliageVisible || nwn_core_replay_bucket(scene, 3);
    const bool ok = okStatic && okDynamic;
    g_privateReplayActive = false;
    reset_foliage_pass_uniforms((GLuint)st.program);

    // One bounded readback proves that real alpha reached the sum target. The
    // pass remains private: nothing is resolved into the scene framebuffer.
    static bool reported = false;
    static unsigned diagnosticSamples = 0;
    const bool sample = !reported || (g_foliageCensus && diagnosticSamples < 12);
    if (ok && g_privateReplayDraws && sample) {
        const size_t count = (size_t)g_w * (size_t)g_h;
        GLfloat* sum = (GLfloat*)std::malloc(count * sizeof(GLfloat));
        if (sum) {
            g.BindFramebuffer(GL_FRAMEBUFFER, g_fbo);
            g.ReadBuffer(GL_COLOR_ATTACHMENT0 + 1);
            g.ReadPixels(0, 0, g_w, g_h, GL_RED, GL_FLOAT, sum);
            size_t covered = 0;
            GLfloat maximum = 0.0f;
            for (size_t i = 0; i < count; ++i) {
                if (sum[i] > 0.0001f) ++covered;
                if (sum[i] > maximum) maximum = sum[i];
            }
            std::free(sum);
            if (covered) reported = true;
            fprintf(stderr, "[oit][foliage-replay] buckets=%s eligibleDraws=%u "
                            "covered=%zu/%zu maxSum=%.4f sourceFbo=%d "
                            "viewport=%d,%d %dx%d depth=completed-scene-copy "
                            "screen=untouched\n",
                    g_foliageVisible ? "1+3" : "1", g_privateReplayDraws,
                    covered, count, maximum, st.fbo,
                    st.viewport[0], st.viewport[1],
                    st.viewport[2], st.viewport[3]);
            ++diagnosticSamples;
        }
    }
    return ok;
}

}   // anonymous namespace

// ---------------------------------------------------------------------------
//  Entry points (declared in nwn_hooks_core.h)
// ---------------------------------------------------------------------------

bool nwn_oit_active(void) { return g_enabled && !g_failed && g_program != 0; }

bool nwn_oit_needs_shader_sources(void) {
    read_settings();
    return g_foliageCensus || g_materialModeCensus || g_materialIdentityCensus ||
           g_foliageShader ||
           g_foliageDepthless;
}

bool nwn_oit_needs_draw_observer(void) {
    read_settings();
    return g_census || g_foliageCensus || g_materialModeCensus ||
           g_materialIdentityCensus || g_foliageReplay || g_foliageDepthless ||
           g_foliageA2c;
}

bool nwn_oit_needs_bucket_hook(void) {
    return nwn_oit_needs_draw_observer();
}

bool nwn_oit_needs_texture_tracking(void) {
    read_settings();
    return g_textureCensus;
}

bool nwn_oit_needs_material_identity_tracking(void) {
    read_settings();
    return g_materialIdentityCensus;
}

bool nwn_oit_wants_material_mode_census(void) {
    read_settings();
    return g_materialModeCensus;
}

bool nwn_oit_wants_foliage_shader_branch(void) {
    read_settings();
    return g_foliageShader;
}

bool nwn_oit_wants_a2c_emitter_shader_branch(void) {
    read_settings();
    return g_foliageA2c;
}

bool nwn_oit_observes_owned_draws(void) {
    return g_privateReplayActive || g_privateDepthReplayActive;
}

void nwn_oit_note_foliage_fragment(unsigned int shader) {
    read_settings();
    if ((!g_foliageCensus && !g_materialModeCensus &&
         !g_materialIdentityCensus && !g_foliageShader &&
         !g_foliageDepthless) || shader == 0) return;
    for (unsigned i = 0; i < g_foliageFragmentCount; ++i)
        if (g_foliageFragments[i] == (GLuint)shader) return;
    if (g_foliageFragmentCount >=
        sizeof(g_foliageFragments) / sizeof(g_foliageFragments[0])) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            fprintf(stderr, "[oit][foliage] fragment registry full; later "
                            "source matches will not be classified\n");
        }
        return;
    }
    g_foliageFragments[g_foliageFragmentCount++] = (GLuint)shader;
    fprintf(stderr, "[oit][foliage] source-classified fragment=%u "
                    "(NO_DISCARD=0 + fAlphaDiscardValue)\n", shader);
}

void nwn_oit_note_emitter_fragment(unsigned int shader) {
    read_settings();
    if (!g_foliageA2c || shader == 0) return;
    for (unsigned i = 0; i < g_foliageFragmentCount; ++i)
        if (g_foliageFragments[i] == (GLuint)shader) return;
    if (g_foliageFragmentCount >=
        sizeof(g_foliageFragments) / sizeof(g_foliageFragments[0])) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            fprintf(stderr, "[a2c][emitter] fragment registry full; later "
                            "particle shaders cannot be routed\n");
        }
        return;
    }
    g_foliageFragments[g_foliageFragmentCount++] = (GLuint)shader;
    fprintf(stderr, "[a2c][emitter] generic scene-colour fragment=%u instrumented\n",
            shader);
}

void nwn_oit_note_texture_bind(unsigned int unit, const char* name) {
    if (unit >= 32 || !name || !*name) return;
    size_t length = strnlen(name, sizeof(g_boundTextureNames[unit]));
    if (length == 0 || length >= sizeof(g_boundTextureNames[unit])) return;
    for (size_t i = 0; i < length; ++i) {
        const unsigned char c = (unsigned char)name[i];
        if (c < 0x20 || c > 0x7e) return;
    }
    std::memcpy(g_boundTextureNames[unit], name, length);
    g_boundTextureNames[unit][length] = '\0';
}

bool ascii_token_equal(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        unsigned char ca = (unsigned char)*a++;
        unsigned char cb = (unsigned char)*b++;
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb - 'A' + 'a');
        if (ca != cb) return false;
    }
    return *a == '\0' && *b == '\0';
}

void nwn_oit_note_shared_material_field(void* sharedMaterial,
                                        const char* field) {
    if (!g_materialIdentityCensus || !sharedMaterial || !field) return;
    char directive[32] = {};
    char type[32] = {};
    char name[128] = {};
    int mode = 0;
    if (std::sscanf(field, "%31s %31s %127s %d",
                    directive, type, name, &mode) != 4 ||
        !ascii_token_equal(directive, "parameter") ||
        !ascii_token_equal(type, "int") ||
        !ascii_token_equal(name, "NWN_ALPHA_MODE") ||
        mode < 0 || mode > 4)
        return;

    SharedMaterialMode* entry = nullptr;
    for (unsigned i = 0; i < g_sharedMaterialModeCount; ++i) {
        if (g_sharedMaterialModes[i].sharedMaterial == sharedMaterial) {
            entry = &g_sharedMaterialModes[i];
            break;
        }
    }
    if (!entry) {
        if (g_sharedMaterialModeCount >=
            sizeof(g_sharedMaterialModes) / sizeof(g_sharedMaterialModes[0]))
            return;
        entry = &g_sharedMaterialModes[g_sharedMaterialModeCount++];
        entry->sharedMaterial = sharedMaterial;
    }
    entry->mode = mode;
}

void nwn_oit_note_material_resource(void* material, void* sharedMaterial,
                                    const char* materialName) {
    if (!g_materialIdentityCensus || !material || !materialName ||
        !*materialName)
        return;
    const size_t length = strnlen(materialName,
                                  sizeof(g_materialResources[0].name));
    if (length == 0 || length >= sizeof(g_materialResources[0].name)) return;
    for (size_t i = 0; i < length; ++i) {
        const unsigned char c = (unsigned char)materialName[i];
        if (c < 0x20 || c > 0x7e) return;
    }

    MaterialResourceIdentity* entry = nullptr;
    for (unsigned i = 0; i < g_materialResourceCount; ++i) {
        if (g_materialResources[i].material == material) {
            entry = &g_materialResources[i];
            break;
        }
    }
    if (!entry) {
        if (g_materialResourceCount >=
            sizeof(g_materialResources) / sizeof(g_materialResources[0])) {
            static bool warned = false;
            if (!warned) {
                warned = true;
                fprintf(stderr, "[oit][material-identity] material resource "
                                "registry full; later identities stay native\n");
            }
            return;
        }
        entry = &g_materialResources[g_materialResourceCount++];
        entry->material = material;
    }
    entry->sharedMaterial = sharedMaterial;
    entry->mode = shared_material_mode(sharedMaterial);
    std::memcpy(entry->name, materialName, length);
    entry->name[length] = '\0';
}

void nwn_oit_note_material_bind(void* material, const char* texture0Name) {
    if (!g_materialIdentityCensus) return;
    g_currentMaterial = material;
    g_currentMaterialBucket = nwn_core::g_currentBucket;
    ++g_currentMaterialSerial;
    g_currentMaterialTexture0[0] = '\0';
    if (!texture0Name || !*texture0Name) return;
    const size_t length = strnlen(texture0Name,
                                  sizeof(g_currentMaterialTexture0));
    if (length == 0 || length >= sizeof(g_currentMaterialTexture0)) return;
    for (size_t i = 0; i < length; ++i) {
        const unsigned char c = (unsigned char)texture0Name[i];
        if (c < 0x20 || c > 0x7e) return;
    }
    std::memcpy(g_currentMaterialTexture0, texture0Name, length);
    g_currentMaterialTexture0[length] = '\0';
}

void nwn_oit_shutdown(void) {
    if (!g_glBound) return;
    destroy_targets();
    if (g_program) { /* program deletion needs glDeleteProgram; not resolved
                        because shutdown runs at process exit, where the GL
                        context is already gone on both platforms. */ }
}

void nwn_oit_prepare(void) {
    read_settings();
    if ((!g_enabled && !g_census && !g_foliageCensus && !g_materialModeCensus &&
         !g_materialIdentityCensus && !g_foliageReplay &&
         !g_foliageDepthless && !g_foliageA2c) ||
        g_failed) return;

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

    // The census observes the engine's own draws and must therefore be present
    // before Scene::Render starts. The callback itself is read-only. Refuse to
    // replace another module's observer if the shared slot is already owned.
    if (g_census || g_foliageCensus || g_materialModeCensus ||
        g_materialIdentityCensus || g_foliageReplay ||
        g_foliageDepthless || g_foliageA2c) {
        if (!nwn_core::g_drawObserver) {
            nwn_core::g_drawObserver = census_observe_draw;
            nwn_core::g_drawObserverAfter = census_observe_draw_after;
            fprintf(stderr, "[oit] draw observer installed before scene submission\n");
        } else if (nwn_core::g_drawObserver != census_observe_draw) {
            static bool warned = false;
            if (!warned) {
                warned = true;
                fprintf(stderr, "[oit] WARNING: draw observer slot already owned; "
                                "census cannot run\n");
            }
        }
    }
}

void nwn_oit_bucket_begin(void* scene, int bucket) {
    read_settings();
    if (g_materialIdentityCensus) {
        g_currentMaterial = nullptr;
        g_currentMaterialTexture0[0] = '\0';
        g_currentMaterialBucket = -1;
    }
    if ((!g_foliageVisible && !g_foliageA2c) || g_failed || !scene ||
        (bucket != 1 && bucket != 3)) return;
    nwn_oit_prepare();
    if (!g_glBound) return;

    SavedState st;
    save_state(st);

    if (g_foliageA2c && bucket == 1) {
        g_a2cOpaqueDepthReady = false;
        g_a2cOpaqueDuplicatePending = false;
        g_a2cOpaqueDuplicateDraws = 0;
        GLint samples = 0;
        g.GetIntegerv(GL_SAMPLES, &samples);
        if (ensure_a2c_opaque_depth(st.viewport[2], st.viewport[3], samples)) {
            for (unsigned drained = 0;
                 drained < 32 && g.GetError() != GL_NO_ERROR; ++drained) {}
            g.BindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)st.fbo);
            g.BindFramebuffer(GL_DRAW_FRAMEBUFFER, g_a2cOpaqueDepthFbo);
            g.BlitFramebuffer(st.viewport[0], st.viewport[1],
                              st.viewport[0] + st.viewport[2],
                              st.viewport[1] + st.viewport[3],
                              0, 0, st.viewport[2], st.viewport[3],
                              GL_DEPTH_BUFFER_BIT, GL_NEAREST);
            g_a2cOpaqueDepthReady = g.GetError() == GL_NO_ERROR;
            static bool reportedSnapshot = false;
            if (g_a2cOpaqueDepthReady && !reportedSnapshot) {
                reportedSnapshot = true;
                fprintf(stderr, "[a2c][emitter] captured pre-foliage opaque depth; "
                                "dynamic opaque bucket 2 will extend it\n");
            } else if (!g_a2cOpaqueDepthReady) {
                static bool reportedFailure = false;
                if (!reportedFailure) {
                    reportedFailure = true;
                    fprintf(stderr, "[a2c][emitter] opaque-depth MSAA blit failed; "
                                    "native emitter depth retained\n");
                }
            }
        }
        restore_state(st);
        if (!g_foliageVisible) return;
        save_state(st);
    }

    if (!g_foliageVisible) {
        restore_state(st);
        return;
    }
    if (!ensure_targets(st.viewport[2], st.viewport[3])) {
        restore_state(st);
        g_failed = true;
        return;
    }

    // Refresh the private depth from the scene at each eligible bucket. More
    // importantly, accumulation itself now happens as an immediate duplicate
    // of each native draw, while all transform/material uniforms are live.
    g.BindFramebuffer(GL_FRAMEBUFFER, (GLuint)st.fbo);
    g.ActiveTexture(kUnitCombined);
    g.BindTexture(GL_TEXTURE_2D, g_texDepth);
    g.CopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                        st.viewport[0], st.viewport[1], g_w, g_h);

    if (bucket == 1 || !g_immediatePrepared) {
        const GLfloat clearCombined[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        const GLfloat clearSum[4]      = {0.0f, 0.0f, 0.0f, 0.0f};
        const GLfloat clearTransl[4]   = {1.0f, 0.0f, 0.0f, 0.0f};
        g.BindFramebuffer(GL_FRAMEBUFFER, g_fbo);
        g.Viewport(0, 0, g_w, g_h);
        g.ClearBufferfv(GL_COLOR, 0, clearCombined);
        g.ClearBufferfv(GL_COLOR, 1, clearSum);
        g.ClearBufferfv(GL_COLOR, 2, clearTransl);
        g_immediateDraws = 0;
        g_visibleAccumReady = false;
    }

    g_immediateSceneFbo = st.fbo;
    for (int i = 0; i < 4; ++i) {
        g_immediateViewport[i] = st.viewport[i];
        g_visibleAccumViewport[i] = st.viewport[i];
    }
    g_visibleAccumFbo = st.fbo;
    g_immediatePrepared = true;
    restore_state(st);
}

static SavedState g_a2cOpaqueDuplicateState = {};
static bool g_a2cOpaqueDuplicateActive = false;

bool nwn_oit_begin_opaque_depth_duplicate(void) {
    const bool pending = g_a2cOpaqueDuplicatePending;
    g_a2cOpaqueDuplicatePending = false;
    if (!pending || !g_foliageA2c || !g_a2cOpaqueDepthReady ||
        g_a2cOpaqueDuplicateActive)
        return false;
    save_state(g_a2cOpaqueDuplicateState);
    g.BindFramebuffer(GL_FRAMEBUFFER, g_a2cOpaqueDepthFbo);
    g.Viewport(0, 0, g_a2cOpaqueDepthW, g_a2cOpaqueDepthH);
    g.Disable(GL_SCISSOR_TEST);
    g.ColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    g.Disable(GL_BLEND);
    g.Enable(GL_DEPTH_TEST);
    g.DepthMask(GL_TRUE);
    g_a2cOpaqueDuplicateActive = true;
    return true;
}

void nwn_oit_end_opaque_depth_duplicate(void) {
    if (!g_a2cOpaqueDuplicateActive) return;
    restore_state(g_a2cOpaqueDuplicateState);
    g_a2cOpaqueDuplicateActive = false;
    ++g_a2cOpaqueDuplicateDraws;
}

bool nwn_oit_begin_immediate_fringe(void) {
    FoliageProgram* p = g_immediateProgram;
    if (!g_foliageVisible || !g_immediatePrepared || g_immediateActive ||
        !p || p->oitPassLoc < 0 || p->oitFringeOnlyLoc < 0 ||
        p->oitCutoffLoc < 0) return false;

    // The native call immediately before this one rendered the solid core.
    // Switch the same still-live draw to the private MRT for its soft fringe.
    if (g_originalOpaqueCoreLoc >= 0) {
        g.Uniform1i(g_originalOpaqueCoreLoc, 0);
        g_originalOpaqueCoreLoc = -1;
    }
    g.Uniform1i(p->oitPassLoc, 1);
    g.Uniform1i(p->oitFringeOnlyLoc, 1);
    g.Uniform1f(p->oitCutoffLoc, g_coreCutoff);
    if (p->oitAlphaGainLoc >= 0) g.Uniform1f(p->oitAlphaGainLoc, g_alphaGain);

    g.BindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    g.Viewport(0, 0, g_w, g_h);
    g.Disable(GL_SCISSOR_TEST);
    g.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    g.Enable(GL_DEPTH_TEST);
    // Core and fringe are two evaluations of the same card. After the scene
    // depth has been copied to D24, their quantized depth is commonly equal at
    // grazing camera angles. Strict LESS then rejects the complete soft band
    // while the alpha-cut core remains. The fringe shader already discards
    // alpha>=coreCutoff, so LEQUAL cannot double-render the solid core.
    g.DepthFunc(g_foliageReplayNoDepth ? GL_ALWAYS : GL_LEQUAL);
    g.DepthMask(GL_FALSE);
    g.Disable(GL_CULL_FACE);
    g.Enable(GL_BLEND);
    g.BlendFuncSeparate(GL_ONE, GL_SRC_ALPHA, GL_ONE, GL_SRC_ALPHA);
    g_immediateActive = true;
    return true;
}

void nwn_oit_end_immediate_fringe(void) {
    if (!g_immediateActive) return;
    FoliageProgram* p = g_immediateProgram;
    if (p) {
        if (p->oitPassLoc >= 0) g.Uniform1i(p->oitPassLoc, 0);
        if (p->oitFringeOnlyLoc >= 0) g.Uniform1i(p->oitFringeOnlyLoc, 0);
    }
    g.BindFramebuffer(GL_FRAMEBUFFER, (GLuint)g_immediateSceneFbo);
    g.Viewport(g_immediateViewport[0], g_immediateViewport[1],
               g_immediateViewport[2], g_immediateViewport[3]);
    restore_original_draw_state();
    g_immediateProgram = nullptr;
    g_immediateActive = false;
    ++g_immediateDraws;
}

void nwn_oit_frame(void* scene) {
    nwn_oit_prepare();
    if (g_foliageVisible && !g_visibleBucketFinalize)
        return; // never composite visible scene transparency over later UI/draws
    if ((!g_enabled && !g_census && !g_foliageCensus && !g_foliageReplay &&
         !g_foliageDepthless) ||
        g_failed || !g_glBound)
        return;

    if (!g_enabled && !g_foliageReplay) {
        if (g_foliageDepthless && g_originalDepthlessDraws) {
            static bool reportedDepthless = false;
            if (!reportedDepthless) {
                fprintf(stderr, "[oit][foliage-depthless] bucket=1 draws=%u "
                                "nativeColor=yes depthWrite=no stateRestored=yes\n",
                        g_originalDepthlessDraws);
                reportedDepthless = true;
            }
            g_originalDepthlessDraws = 0;
        }
        return;                          // census-only run
    }
    if ((g_enabled || g_foliageVisible) && !build_program()) {
        g_failed = true;
        return;
    }

    SavedState st;
    save_state(st);

    if (g_foliageVisible && g_visibleResolveStage && g_visibleAccumReady) {
        bool sameTarget = st.fbo == g_visibleAccumFbo;
        for (int i = 0; i < 4; ++i)
            sameTarget = sameTarget && st.viewport[i] == g_visibleAccumViewport[i];
        if (!sameTarget) {
            static unsigned mismatches = 0;
            if (mismatches++ < 8)
                fprintf(stderr, "[oit] accumulation/resolve target mismatch: "
                                "accum fbo=%d vp=%d,%d %dx%d, resolve "
                                "fbo=%d vp=%d,%d %dx%d; skipping resolve\n",
                        g_visibleAccumFbo, g_visibleAccumViewport[0],
                        g_visibleAccumViewport[1], g_visibleAccumViewport[2],
                        g_visibleAccumViewport[3], st.fbo, st.viewport[0],
                        st.viewport[1], st.viewport[2], st.viewport[3]);
            g_visibleAccumReady = false;
            restore_state(st);
            return;
        }
    }

    // Match the accumulation resolution to whatever the engine is drawing at.
    if (!ensure_targets(st.viewport[2], st.viewport[3])) {
        restore_state(st);
        g_failed = true;
        return;
    }

    // Everything below is this module's own draw traffic. The guard keeps it out
    // of the shadow module's per-draw hook (and out of any future module's).
    nwn_core::OwnedPass owned;

    bool foliageAccumulated = false;
    if (g_foliageVisible && g_visibleResolveStage) {
        // Bucket 1/3 draw data was accumulated while valid at bucket 3. The
        // native alpha core already wrote scene depth in-place, so this late
        // stage only composites after water and before the final fog bucket.
        if (!g_visibleAccumReady) {
            restore_state(st);
            return;
        }
        foliageAccumulated = true;
    } else if (g_foliageReplay) {
        if (!private_foliage_replay(scene, st)) {
            fprintf(stderr, "[oit][foliage-replay] guarded bucket replay failed; "
                            "disabling private replay\n");
            g_foliageReplay = false;
        }
        if (g_foliageVisible) {
            g_visibleAccumReady = g_foliageReplay;
            g_visibleAccumFbo = st.fbo;
            for (int i = 0; i < 4; ++i)
                g_visibleAccumViewport[i] = st.viewport[i];
            restore_state(st);
            return;
        }
        if (!g_foliageVisible || !g_foliageReplay) {
            restore_state(st);
            return;
        }
        foliageAccumulated = true;
    }

    // ---- accumulate ------------------------------------------------------
    // PHASE 1: no engine geometry is redirected here yet, so the buffers are
    // cleared to a synthetic single layer of colour C at alpha a:
    //   combined = C*a,  sum = a,  translucence = 1-a
    // Phase 2 replaces these clears with the console's real clear values
    // (0,0,0,0 / 0 / 1) followed by the redirected transparent draws.
    if (!foliageAccumulated) {
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
    }

    // ---- resolve + composite --------------------------------------------
    // Back into whatever the engine had bound, with the console's exact blend:
    // out = src.rgb + dst * src.a, where src.a is the accumulated transmittance.
    g.BindFramebuffer(GL_FRAMEBUFFER, (GLuint)st.fbo);
    g.Viewport(st.viewport[0], st.viewport[1], st.viewport[2], st.viewport[3]);
    g.Disable(GL_DEPTH_TEST);
    g.DepthMask(GL_FALSE);
    g.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
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
    if (g_foliageVisible) g_visibleAccumReady = false;

    static unsigned frames = 0;
    if (++frames == 1)
        fprintf(stderr, "[oit] first %s composite executed at %dx%d\n",
                foliageAccumulated ? "foliage" : "synthetic", g_w, g_h);
}

void nwn_oit_bucket_complete(void* scene, int bucket) {
    read_settings();
    if (!g_foliageVisible || g_failed) return;
    if (bucket != 3 && bucket != g_visibleFinalizeBucket) return;
    if (bucket == 3) {
        g_visibleAccumReady = g_immediatePrepared && g_immediateDraws > 0;
        static bool reported = false;
        if (!reported && g_visibleAccumReady) {
            reported = true;
            fprintf(stderr, "[oit] immediate foliage accumulation active: "
                            "draws=%u, native transform/material state retained\n",
                    g_immediateDraws);
        }
        return;
    }
    g_visibleBucketFinalize = true;
    g_visibleResolveStage = true;
    nwn_oit_frame(scene);
    g_visibleResolveStage = false;
    g_visibleBucketFinalize = false;
    g_immediatePrepared = false;
}
