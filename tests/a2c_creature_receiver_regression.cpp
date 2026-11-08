// Exercise the real A2C receiver upload, including authoritative owner type.
#include "../nwn_shadowmap.cpp"

static float uploadedStrength = -1.0f;
static GLint uniform_location(GLuint, const char* name) {
    const char* required[] = {
        "nwnA2cStaticDepth", "nwnA2cDynamicDepth", "nwnA2cCameraVPInv",
        "nwnA2cCameraView", "nwnA2cLightVP[0]", "nwnA2cClipFar",
        "nwnA2cViewport", "nwnA2cShadowStrength"
    };
    for (int i = 0; i < 8; ++i)
        if (std::strcmp(name, required[i]) == 0) return i;
    return -1;
}
static void get_int(GLenum key, GLint* out) {
    *out = key == GL_ACTIVE_TEXTURE ? GL_TEXTURE0 : 0;
}
static void uniform_int(GLint, GLint) {}
static void uniform_float(GLint location, GLfloat value) {
    if (location == 7) uploadedStrength = value;
}
static void uniform_vec4(GLint, GLfloat, GLfloat, GLfloat, GLfloat) {}
static void uniform_matrix(GLint, GLsizei, GLboolean, const GLfloat*) {}
static void active_texture(GLenum) {}
static void bind_texture(GLenum, GLuint) {}

int main() {
    gl::GetUniformLocation = uniform_location;
    gl::GetIntegerv = get_int;
    gl::Uniform1i = uniform_int;
    gl::Uniform1f = uniform_float;
    gl::Uniform4f = uniform_vec4;
    gl::UniformMatrix4fv = uniform_matrix;
    gl::ActiveTexture = active_texture;
    gl::BindTexture = bind_texture;
    g_receiverEnabled = true;
    g_cascadeCompositeShadows = true;
    g_cascadeCsmStaticReceiver = true;
    g_cascadeTargetsUsable = true;
    g_cascadeStaticTex = 1;
    g_cascadeDynamicTex = 2;
    g_shadowFrameContext.valid = true;
    g_cascadeMath.valid = true;
    g_haveAreaShadowFlags = false;
    g_cascadeCompositeStrength = 0.42f;

    int failures = 0;
    for (DrawOwnerKind kind : {DrawOwnerKind::Creature,
                              DrawOwnerKind::NonCreature,
                              DrawOwnerKind::Unknown}) {
        g_receiverDebug = 0;
        g_currentDrawOwnerKind = kind;
        uploadedStrength = -1.0f;
        const bool started = nwn_shadow_begin_a2c_receiver(123);
        nwn_shadow_end_a2c_receiver();
        const float expected = kind == DrawOwnerKind::Creature ? 0.0f : 0.42f;
        const bool ok = started && std::fabs(uploadedStrength - expected) < 1e-6f;
        std::printf("A2C owner=%d inline sun=%.2f expected=%.2f %s\n",
                    (int)kind, uploadedStrength, expected, ok ? "PASS" : "FAIL");
        if (!ok) ++failures;
    }
    g_currentDrawOwnerKind = DrawOwnerKind::Creature;
    g_receiverDebug = 7;
    uploadedStrength = -1.0f;
    const bool comparisonStarted = nwn_shadow_begin_a2c_receiver(123);
    nwn_shadow_end_a2c_receiver();
    const bool comparisonOk = comparisonStarted &&
        std::fabs(uploadedStrength - 0.42f) < 1e-6f;
    std::printf("A2C creature old-path comparison: %s\n",
                comparisonOk ? "PASS" : "FAIL");
    if (!comparisonOk) ++failures;
    return failures ? 1 : 0;
}
