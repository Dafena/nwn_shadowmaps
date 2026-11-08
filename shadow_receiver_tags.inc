// Receiver classification travels with the engine's actual material draw.
// A private integer MRT stores depth24, creature/highlight flags and depth span.
// Native depth testing, discard and MSAA/A2C coverage apply to the tag too.
// No native colour/depth is replaced, and no camera geometry is replayed.
static constexpr int kSunTagOutput = 7;
static bool g_sunTagBorrowed = false;
static GLenum g_sunTagOldDrawBuffers[8] = {};
struct SunTagProgram {
    GLuint program;
    GLint kind;
};
static std::vector<SunTagProgram> g_sunTagPrograms;

static void sun_tag_forget_program(GLuint program) {
    g_sunTagPrograms.erase(
        std::remove_if(g_sunTagPrograms.begin(), g_sunTagPrograms.end(),
                       [=](const SunTagProgram &p) { return p.program == program; }),
        g_sunTagPrograms.end());
}

static bool inject_sun_receiver_tag(std::string &source) {
    if (source.find("nwnSunReceiverKind") != std::string::npos ||
        source.find("gl_FragDepth") != std::string::npos ||
        source.rfind("void main") == std::string::npos)
        return false;
    const std::string plain = "out vec4 compat_glFragColor;";
    const std::string located = "layout(location=0) out vec4 compat_glFragColor;";
    size_t output = source.find(located);
    if (output == std::string::npos) {
        output = source.find(plain);
        if (output == std::string::npos)
            return false;
        source.replace(output, plain.size(), located);
    }
    const size_t main = source.rfind("void main");
    if (main == std::string::npos)
        return false;
    // Wrap main so a native early return still publishes its receiver class;
    // native discard continues to discard both colour and tag.
    source.replace(main + 5, 4, "nwnSunTaggedMain");
    source.insert(output + located.size(), "\nlayout(location=7) out uvec2 nwnSunReceiverTag;\n"
                                           "uniform int nwnSunReceiverKind;\n");
    // Native per-fragment shading evaluates depth once, while MSAA tests
    // depth at each covered sample. Carry the primitive's depth-plane span,
    // computed before any discard, so that exact coverage is not rejected
    // because a sloped triangle's sample depth differs from its centre.
    source += "\nvoid main(){float nwnSunDepthSpan=fwidth(gl_FragCoord.z)+4.0/16777215.0;"
              "nwnSunTaggedMain();"
              "uint z=uint(clamp(gl_FragCoord.z,0.0,1.0)*16777215.0+0.5);"
              "nwnSunReceiverTag=uvec2(z|(uint(nwnSunReceiverKind&3)<<24u),floatBitsToUint("
              "nwnSunDepthSpan));}\n";
    return true;
}

static bool ensure_sun_tag_textures(int width, int height, int samples) {
    if (!gl::TexImage2DMultisample || width <= 0 || height <= 0 || samples > 16)
        return false;
    GLint old2D = 0, oldMS = 0;
    gl::GetIntegerv(GL_TEXTURE_BINDING_2D, &old2D);
    gl::GetIntegerv(GL_TEXTURE_BINDING_2D_MULTISAMPLE, &oldMS);
    if (!g_sunTagDummy2D) {
        const GLuint clear[2] = {0xffffffffu, 0};
        gl::GenTextures(1, &g_sunTagDummy2D);
        gl::BindTexture(GL_TEXTURE_2D, g_sunTagDummy2D);
        gl::TexImage2D(GL_TEXTURE_2D, 0, GL_RG32UI, 1, 1, 0, GL_RG_INTEGER, GL_UNSIGNED_INT, clear);
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    if (!g_sunTagDummyMS) {
        gl::GenTextures(1, &g_sunTagDummyMS);
        gl::BindTexture(GL_TEXTURE_2D_MULTISAMPLE, g_sunTagDummyMS);
        gl::TexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, 1, GL_RG32UI, 1, 1, GL_TRUE);
    }
    if (!g_sunTagTex || width != g_sunTagWidth || height != g_sunTagHeight ||
        samples != g_sunTagSamples) {
        if (g_sunTagTex)
            gl::DeleteTextures(1, &g_sunTagTex);
        gl::GenTextures(1, &g_sunTagTex);
        const GLenum target = samples > 0 ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
        gl::BindTexture(target, g_sunTagTex);
        if (samples > 0)
            gl::TexImage2DMultisample(target, samples, GL_RG32UI, width, height, GL_TRUE);
        else {
            gl::TexImage2D(target, 0, GL_RG32UI, width, height, 0, GL_RG_INTEGER, GL_UNSIGNED_INT,
                           nullptr);
            gl::TexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            gl::TexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        }
        g_sunTagWidth = width;
        g_sunTagHeight = height;
        g_sunTagSamples = samples;
        g_sunTagFrame = 0;
    }
    gl::BindTexture(GL_TEXTURE_2D, (GLuint)old2D);
    gl::BindTexture(GL_TEXTURE_2D_MULTISAMPLE, (GLuint)oldMS);
    return g_sunTagTex && g_sunTagDummy2D && g_sunTagDummyMS;
}

static void end_sun_receiver_tag() {
    if (!g_sunTagBorrowed)
        return;
    // Every caller restores its own FBO before reaching this boundary.
    gl::FramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + kSunTagOutput,
                             g_sunTagSamples > 0 ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D, 0, 0);
    gl::DrawBuffers(8, g_sunTagOldDrawBuffers);
    g_sunTagBorrowed = false;
}

static void begin_sun_receiver_tag() {
    if (g_sunTagBorrowed || !g_receiverEnabled || g_inOurPass || g_cascadeReplayActive ||
        g_localLightPassActive || g_overlayPassActive || nwn_core::g_ownedPass ||
        g_inStencilShadow || !g_shadowFrameContext.valid || !g_traceFrameActive ||
        g_traceCurrentScene != g_areaScene || nwn_core::g_currentBucket < 0 || !gl::DrawBuffers ||
        !gl::ClearBufferuiv || !gl::GetBooleani_v || !gl::ColorMaski)
        return;
    GLboolean depthWrite = GL_FALSE;
    gl::GetBooleanv(GL_DEPTH_WRITEMASK, &depthWrite);
    if (!depthWrite)
        return;
    GLint fbo = 0, vp[4] = {}, samples = 0, maxBuffers = 0, program = 0;
    gl::GetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fbo);
    if (!fbo)
        return; // The default framebuffer cannot accept a private MRT.
    gl::GetIntegerv(GL_CURRENT_PROGRAM, &program);
    if (program <= 0)
        return;
    GLint kind = -1;
    bool known = false;
    for (const auto &p : g_sunTagPrograms)
        if (p.program == (GLuint)program) {
            kind = p.kind;
            known = true;
            break;
        }
    if (!known) {
        kind = gl::GetUniformLocation((GLuint)program, "nwnSunReceiverKind");
        g_sunTagPrograms.push_back({(GLuint)program, kind});
    }
    if (kind < 0)
        return;
    gl::GetIntegerv(GL_MAX_DRAW_BUFFERS, &maxBuffers);
    if (maxBuffers < 8)
        return;
    GLint type = 0;
    gl::GetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,
                                            GL_COLOR_ATTACHMENT0 + kSunTagOutput,
                                            GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &type);
    if (type != GL_NONE)
        return; // Never borrow an attachment NWN already owns.
    gl::GetIntegerv(GL_VIEWPORT, vp);
    if (vp[0] != 0 || vp[1] != 0)
        return;
    if (g_sunTagFrame == g_shadowFrameContext.serial && g_sunTagSourceFbo != (GLuint)fbo)
        return;
    gl::GetIntegerv(GL_SAMPLES, &samples);
    if (!ensure_sun_tag_textures(vp[2], vp[3], samples))
        return;
    for (int i = 0; i < 8; ++i) {
        GLint value = 0;
        gl::GetIntegerv(GL_DRAW_BUFFER0 + i, &value);
        g_sunTagOldDrawBuffers[i] = (GLenum)value;
    }
    GLenum buffers[8];
    std::copy_n(g_sunTagOldDrawBuffers, 8, buffers);
    buffers[kSunTagOutput] = GL_COLOR_ATTACHMENT0 + kSunTagOutput;
    gl::FramebufferTexture2D(GL_DRAW_FRAMEBUFFER, buffers[kSunTagOutput],
                             samples > 0 ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D, g_sunTagTex,
                             0);
    gl::DrawBuffers(8, buffers);
    g_sunTagBorrowed = true;
    if (gl::CheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        end_sun_receiver_tag();
        return;
    }
    if (g_sunTagFrame != g_shadowFrameContext.serial) {
        const GLuint clear[4] = {0xffffffffu, 0, 0, 0};
        const GLboolean scissor = gl::IsEnabled(GL_SCISSOR_TEST);
        GLboolean mask[4];
        gl::GetBooleani_v(GL_COLOR_WRITEMASK, kSunTagOutput, mask);
        if (scissor)
            gl::Disable(GL_SCISSOR_TEST);
        gl::ColorMaski(kSunTagOutput, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        gl::ClearBufferuiv(GL_COLOR, kSunTagOutput, clear);
        gl::ColorMaski(kSunTagOutput, mask[0], mask[1], mask[2], mask[3]);
        if (scissor)
            gl::Enable(GL_SCISSOR_TEST);
        g_sunTagFrame = g_shadowFrameContext.serial;
        g_sunTagSourceFbo = (GLuint)fbo;
    }
    const bool highlighted = current_draw_is_highlighted();
    gl::Uniform1i(kind, (current_draw_is_creature() ? 1 : 0) | (highlighted ? 2 : 0));
    if (highlighted) {
        static bool reportedHighlight = false;
        if (!reportedHighlight) {
            reportedHighlight = true;
            fprintf(stderr, "[shadowmap][sun-highlight] native Scene highlight owner matched; "
                            "preserving hover rendering through the shadow composite\n");
        }
    }
    static bool reported = false;
    if (!reported) {
        reported = true;
        fprintf(stderr,
                "[shadowmap][sun-receiver-tag] live fbo=%d size=%dx%d samples=%d; "
                "engine object types, native coverage, no camera replay\n",
                fbo, vp[2], vp[3], samples);
    }
}
