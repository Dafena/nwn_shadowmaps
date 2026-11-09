#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/glcorearb.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
static std::string read_file(const char *path) {
    std::ifstream f(path);
    return {std::istreambuf_iterator<char>(f), {}};
}
static GLuint program(const std::string &fragment, bool sloped = false) {
    const char *vs = "#version 330 core\nvoid main(){vec2 "
                     "p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0.,1.);}";
    const char *slopeVS =
        "#version 330 core\nvoid main(){vec2 "
        "p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,(p.x-0.5)*0.6,1.);}";
    if (sloped)
        vs = slopeVS;
    GLuint p = glCreateProgram();
    for (int stage = 0; stage < 2; ++stage) {
        GLuint s = glCreateShader(stage ? GL_FRAGMENT_SHADER : GL_VERTEX_SHADER);
        const char *text = stage ? fragment.c_str() : vs;
        glShaderSource(s, 1, &text, nullptr);
        glCompileShader(s);
        GLint ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[8192];
            glGetShaderInfoLog(s, sizeof(log), nullptr, log);
            std::fprintf(stderr, "%s\n", log);
            std::exit(1);
        }
        glAttachShader(p, s);
        glDeleteShader(s);
    }
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[8192];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        std::fprintf(stderr, "%s\n", log);
        std::exit(1);
    }
    return p;
}
static void ui(GLuint p, const char *n, int v) { glUniform1i(glGetUniformLocation(p, n), v); }
static void uf(GLuint p, const char *n, float v) { glUniform1f(glGetUniformLocation(p, n), v); }
static GLuint depth(int unit, bool array, const std::vector<float> &values, bool compare) {
    GLuint tex;
    glGenTextures(1, &tex);
    glActiveTexture(GL_TEXTURE0 + unit);
    GLenum target = array ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D;
    glBindTexture(target, tex);
    if (array)
        glTexImage3D(target, 0, GL_DEPTH_COMPONENT24, 4, 1, 4, 0, GL_DEPTH_COMPONENT, GL_FLOAT,
                     values.data());
    else
        glTexImage2D(target, 0, GL_DEPTH_COMPONENT24, 4, 1, 0, GL_DEPTH_COMPONENT, GL_FLOAT,
                     values.data());
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(target, GL_TEXTURE_COMPARE_MODE, compare ? GL_COMPARE_REF_TO_TEXTURE : GL_NONE);
    glTexParameteri(target, GL_TEXTURE_COMPARE_FUNC, GL_LESS);
    return tex;
}
static int failures = 0;
static void check(GLuint receiver, GLuint output, const char *label, const float expected[4]) {
    glBindFramebuffer(GL_FRAMEBUFFER, output);
    glViewport(0, 0, 4, 1);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    glUseProgram(receiver);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    float rgba[16] = {};
    glReadPixels(0, 0, 4, 1, GL_RGBA, GL_FLOAT, rgba);
    bool ok = glGetError() == GL_NO_ERROR;
    for (int i = 0; i < 4; ++i)
        ok &= std::fabs(rgba[i * 4] - expected[i]) < 0.01f;
    std::printf("GPU %s: %.2f %.2f %.2f %.2f %s\n", label, rgba[0], rgba[4], rgba[8], rgba[12],
                ok ? "PASS" : "FAIL");
    if (!ok)
        ++failures;
}
int main(int argc, char **argv) {
    if (argc != 3)
        return 2;
    EGLDisplay display =
        eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
    EGLint major = 0, minor = 0;
    if (!eglInitialize(display, &major, &minor)) {
        std::fprintf(stderr, "EGL init failed 0x%x\n", eglGetError());
        return 1;
    }
    eglBindAPI(EGL_OPENGL_API);
    EGLint count = 0;
    EGLConfig config;
    const EGLint attrs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
                            EGL_NONE};
    eglChooseConfig(display, attrs, &config, 1, &count);
    const EGLint ca[] = {EGL_CONTEXT_MAJOR_VERSION,
                         3,
                         EGL_CONTEXT_MINOR_VERSION,
                         3,
                         EGL_CONTEXT_OPENGL_PROFILE_MASK,
                         EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
                         EGL_NONE};
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, ca);
    const EGLint pa[] = {EGL_WIDTH, 4, EGL_HEIGHT, 1, EGL_NONE};
    EGLSurface surface = eglCreatePbufferSurface(display, config, pa);
    if (!eglMakeCurrent(display, surface, surface, context)) {
        std::fprintf(stderr, "EGL context failed\n");
        return 1;
    }
    std::printf("GPU renderer: %s\n", glGetString(GL_RENDERER));
    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    GLuint receiver = program(read_file(argv[1]));
    GLuint tagged = program(read_file(argv[2]));
    GLuint outputTex, output;
    glGenTextures(1, &outputTex);
    glBindTexture(GL_TEXTURE_2D, outputTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 4, 1, 0, GL_RGBA, GL_FLOAT, nullptr);
    glGenFramebuffers(1, &output);
    glBindFramebuffer(GL_FRAMEBUFFER, output);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, outputTex, 0);
    GLuint sceneDepth = depth(31, false, std::vector<float>(4, 0.5f), false);
    GLuint staticMap = depth(30, true, std::vector<float>(16, 1.0f), true);
    depth(29, true, std::vector<float>(16, 0.4f), true);
    std::vector<float> selective(16, 1.0f);
    for (int l = 0; l < 4; ++l)
        selective[l * 4 + 2] = 0.4f;
    depth(24, true, selective, true);
    depth(28, true, std::vector<float>(16, 1.0f), false);
    depth(27, false, std::vector<float>(4, 1.0f), true);
    depth(25, false, std::vector<float>(4, 0.5f), false);
    GLuint tag2D;
    glGenTextures(1, &tag2D);
    glActiveTexture(GL_TEXTURE0 + 22);
    glBindTexture(GL_TEXTURE_2D, tag2D);
    const GLuint z = 8388608u;
    GLuint tags[8] = {z, 0, z | 16777216u, 0, z | 16777216u, 0, z, 0};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32UI, 4, 1, 0, GL_RG_INTEGER, GL_UNSIGNED_INT, tags);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    GLuint dummyMS;
    glGenTextures(1, &dummyMS);
    glActiveTexture(GL_TEXTURE0 + 23);
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, dummyMS);
    glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, 1, GL_RG32UI, 4, 1, GL_TRUE);
    glUseProgram(receiver);
    ui(receiver, "nwnStaticSceneDepth", 31);
    ui(receiver, "nwnStaticLightDepth", 30);
    ui(receiver, "nwnDynamicLightDepth", 29);
    ui(receiver, "nwnLocalLightDepth", 28);
    ui(receiver, "nwnStaticWorldDepth", 27);
    ui(receiver, "nwnStaticSceneColor", 26);
    ui(receiver, "nwnPreDynamicSceneDepth", 25);
    ui(receiver, "nwnSunObjectDepth", 24);
    ui(receiver, "nwnSunReceiverTagsMS", 23);
    ui(receiver, "nwnSunReceiverTags", 22);
    ui(receiver, "nwnSunTagsReady", 1);
    ui(receiver, "nwnSunObjectReady", 1);
    ui(receiver, "nwnSunTagSamples", 0);
    ui(receiver, "nwnStaticDynamicEnabled", 1);
    ui(receiver, "nwnDynamicLayers", 4);
    ui(receiver, "nwnReceiverDebug", 2);
    uf(receiver, "nwnSelfIllumGuard", 1.0f);
    uf(receiver, "nwnStaticReceiverBias", 0.0025f);
    const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    float matrices[64];
    for (int i = 0; i < 4; ++i)
        std::copy_n(identity, 16, matrices + i * 16);
    glUniformMatrix4fv(glGetUniformLocation(receiver, "nwnStaticCameraVPInv"), 1, GL_FALSE,
                       identity);
    glUniformMatrix4fv(glGetUniformLocation(receiver, "nwnStaticCameraView"), 1, GL_FALSE,
                       identity);
    glUniformMatrix4fv(glGetUniformLocation(receiver, "nwnStaticLightVP[0]"), 4, GL_FALSE,
                       matrices);
    const float selectiveExpected[4] = {1, 0, 1, 1};
    check(receiver, output, "floor/creature/object-on-creature/placeable", selectiveExpected);
    // Highlighted creature samples must preserve NWN's native hover output.
    tags[4] = z | (3u << 24u);
    glActiveTexture(GL_TEXTURE0 + 22);
    glBindTexture(GL_TEXTURE_2D, tag2D);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 4, 1, GL_RG_INTEGER, GL_UNSIGNED_INT, tags);
    const float hoverExpected[4] = {1, 0, 0, 1};
    check(receiver, output, "native hover unaffected by object shadow", hoverExpected);
    tags[4] = z | (1u << 24u);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 4, 1, GL_RG_INTEGER, GL_UNSIGNED_INT, tags);
    check(receiver, output, "shadow restored after hover removal", selectiveExpected);
    tags[2] = 0x1000000u | 0x400000u;
    glActiveTexture(GL_TEXTURE0 + 22);
    glBindTexture(GL_TEXTURE_2D, tag2D);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 4, 1, GL_RG_INTEGER, GL_UNSIGNED_INT, tags);
    const float staleExpected[4] = {1, 1, 1, 1};
    check(receiver, output, "stale tag rejected by scene depth", staleExpected);
    glActiveTexture(GL_TEXTURE0 + 30);
    glBindTexture(GL_TEXTURE_2D_ARRAY, staticMap);
    std::vector<float> blocked(16, 0.4f);
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, 0, 4, 1, 4, GL_DEPTH_COMPONENT, GL_FLOAT,
                    blocked.data());
    check(receiver, output, "building shadows still reach all receivers", staleExpected);
    std::vector<float> lit(16, 1.0f);
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, 0, 4, 1, 4, GL_DEPTH_COMPONENT, GL_FLOAT,
                    lit.data());
    GLint maxSamples = 0;
    glGetIntegerv(GL_MAX_INTEGER_SAMPLES, &maxSamples);
    int samples = std::min(4, maxSamples);
    if (samples >= 2) {
        GLuint ms[3], fbo;
        glGenTextures(3, ms);
        const GLenum formats[3] = {GL_RGBA8, GL_DEPTH_COMPONENT24, GL_RG32UI};
        for (int i = 0; i < 3; ++i) {
            glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, ms[i]);
            glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples, formats[i], 4, 1, GL_TRUE);
        }
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D_MULTISAMPLE,
                               ms[0], 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D_MULTISAMPLE,
                               ms[1], 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT7, GL_TEXTURE_2D_MULTISAMPLE,
                               ms[2], 0);
        GLenum buffers[8] = {
            GL_COLOR_ATTACHMENT0, GL_NONE, GL_NONE, GL_NONE, GL_NONE, GL_NONE, GL_NONE,
            GL_COLOR_ATTACHMENT7};
        glDrawBuffers(8, buffers);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            return 1;
        const GLuint invalid[4] = {0xffffffffu, 0, 0, 0};
        glClearBufferuiv(GL_COLOR, 7, invalid);
        glClearDepth(1);
        glClear(GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
        glUseProgram(tagged);
        uf(tagged, "alpha", 0.5f);
        ui(tagged, "nwnSunReceiverKind", 1);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glActiveTexture(GL_TEXTURE0 + 23);
        glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, ms[2]);
        glUseProgram(receiver);
        ui(receiver, "nwnSunTagSamples", samples);
        const float msExpected[4] = {0, 0, 1, 0};
        check(receiver, output, "native multisample A2C receiver tags", msExpected);
        GLuint sloped = program(read_file(argv[2]), true);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glClearBufferuiv(GL_COLOR, 7, invalid);
        glClearDepth(1);
        glClear(GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glUseProgram(sloped);
        uf(sloped, "alpha", 1.0f);
        ui(sloped, "nwnSunReceiverKind", 1);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        GLuint depthFbo;
        glGenFramebuffers(1, &depthFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, depthFbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, sceneDepth, 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, depthFbo);
        glBlitFramebuffer(0, 0, 4, 1, 0, 0, 4, 1, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
        check(receiver, output, "sloped MSAA surface remains consistently creature", msExpected);
    } else
        std::printf("GPU multisample integer tags: unavailable\n");
    return failures ? 1 : 0;
}
