// Exercise the real capture helpers with distinct engine READ/DRAW bindings.
#include "../nwn_shadowmap.cpp"
static GLint drawFbo = 4, readFbo = 0;
static GLint copiedFrom = -1;
static void get_int(GLenum key, GLint* value) {
    *value = key == GL_READ_FRAMEBUFFER_BINDING ? readFbo :
             key == GL_DRAW_FRAMEBUFFER_BINDING ? drawFbo : 0;
}
static void bind_fbo(GLenum target, GLuint fbo) {
    if (target != GL_READ_FRAMEBUFFER) drawFbo = (GLint)fbo;
    if (target != GL_DRAW_FRAMEBUFFER) readFbo = (GLint)fbo;
}
static void gen_fbo(GLsizei, GLuint* fbo) { *fbo = 90; }
static void attach(GLenum, GLenum, GLenum, GLuint, GLint) {}
static GLenum complete(GLenum) { return GL_FRAMEBUFFER_COMPLETE; }
static GLenum no_error() { return GL_NO_ERROR; }
static void blit(GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLbitfield,GLenum) {
    copiedFrom = readFbo;
}
static void copy_depth(GLenum,GLint,GLint,GLint,GLint,GLint,GLsizei,GLsizei) {
    copiedFrom = readFbo;
}
int main() {
    gl::GetIntegerv = get_int;
    gl::BindFramebuffer = bind_fbo;
    gl::GenFramebuffers = gen_fbo;
    gl::FramebufferTexture2D = attach;
    gl::CheckFramebufferStatus = complete;
    gl::GetError = no_error;
    gl::BlitFramebuffer = blit;
    gl::CopyTexSubImage2D = copy_depth;
    g_preDynamicDepthTex = g_preDynamicDepthFboTex = 5;
    g_preDynamicDepthFbo = 77;
    g_sceneDepthTex = g_sceneDepthFboTex = 6;
    g_sceneDepthFbo = 78;
    const GLint viewport[4] = {0,0,1920,1006};
    int failures = 0;
    for (int scenario = 0; scenario < 3; ++scenario) {
        const bool preCharacter = scenario != 1;
        const GLint originalDraw = scenario == 0 ? 4 : 0;
        const GLint originalRead = scenario == 0 ? 0 : 4;
        const GLint expectedSource = scenario == 2 ? 0 : 4;
        drawFbo = originalDraw; readFbo = originalRead; copiedFrom = -1;
        const bool copied = preCharacter ? capture_pre_dynamic_depth(viewport)
                                         : capture_scene_depth(viewport);
        const bool ok = copied && copiedFrom == expectedSource &&
            drawFbo == originalDraw && readFbo == originalRead;
        printf("local %s depth: source=%d expected=%d draw/read restored=%d %s\n",
            preCharacter ? "pre-character" : "completed-scene", copiedFrom,
            expectedSource,
            drawFbo == originalDraw && readFbo == originalRead, ok ? "PASS" : "FAIL");
        if (!ok) ++failures;
    }
    return failures ? 1 : 0;
}
