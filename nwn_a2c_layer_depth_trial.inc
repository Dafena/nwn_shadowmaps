// Deliberately excluded from accepted builds. An explicit material list selects
// the experiment; geometry is drawn once, and only its depth is rolled back.
namespace ordered_a2c_trial {
struct Target {
    GLuint fbo = 0, texture = 0;
    GLint width = 0, height = 0, samples = 0, format = 0;
};
Target target;
bool pending = false;
GLint destination = 0, viewport[4] = {};
unsigned captures = 0, restores = 0;
bool observedDraw = false;
void bench_frame() {
    if (!std::getenv("NWN_A2C_ORDERED_BENCH") || !observedDraw) return;
    observedDraw = false;
    static auto start = std::chrono::steady_clock::now();
    static unsigned frames = 0, previousCaptures = 0;
    ++frames;
    const auto now = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(now-start).count();
    if (seconds >= 5.0) {
        fprintf(stderr, "[a2c][ordered-trial-bench] worldFrames=%u seconds=%.3f fps=%.2f depthCopies=%u control=%d\n",
                frames, seconds, frames/seconds, 2*(captures-previousCaptures),
                std::getenv("NWN_A2C_ORDERED_NO_COPY") ? 1 : 0);
        start = now; frames = 0; previousCaptures = captures;
    }
}

void (*attachment)(GLenum, GLenum, GLenum, GLint*) = nullptr;
void (*bindRenderbuffer)(GLenum, GLuint) = nullptr;
void (*renderbufferParameter)(GLenum, GLenum, GLint*) = nullptr;
void (*textureParameter)(GLenum, GLint, GLenum, GLint*) = nullptr;

bool selected(const char* name) {
    const char* list = std::getenv("NWN_A2C_ORDERED_MATERIALS");
    if (!name || !list || !*name) return false;
    const size_t length = std::strlen(name);
    for (const char* p = list; *p;) {
        const char* end = std::strchr(p, ',');
        if (!end) end = p + std::strlen(p);
        if (size_t(end - p) == length && std::memcmp(p, name, length) == 0)
            return true;
        p = *end ? end + 1 : end;
    }
    return false;
}

bool functions() {
    if (!attachment) {
        *(void**)(&attachment) = dlsym(RTLD_DEFAULT, "glGetFramebufferAttachmentParameteriv");
        *(void**)(&bindRenderbuffer) = dlsym(RTLD_DEFAULT, "glBindRenderbuffer");
        *(void**)(&renderbufferParameter) = dlsym(RTLD_DEFAULT, "glGetRenderbufferParameteriv");
        *(void**)(&textureParameter) = dlsym(RTLD_DEFAULT, "glGetTexLevelParameteriv");
    }
    const bool ready = attachment && bindRenderbuffer && renderbufferParameter &&
                       textureParameter && g.BlitFramebuffer && g.TexImage2DMultisample;
    static bool reportedMissing = false;
    if (!ready && !reportedMissing) {
        fprintf(stderr, "[a2c][ordered-trial] missing API attachment=%d bindRB=%d queryRB=%d queryTex=%d blit=%d texMS=%d\n",
                attachment!=nullptr, bindRenderbuffer!=nullptr, renderbufferParameter!=nullptr,
                textureParameter!=nullptr, g.BlitFramebuffer!=nullptr, g.TexImage2DMultisample!=nullptr);
        reportedMissing = true;
    }
    return ready;
}

// Each operation restores the live bindings, including when allocation fails.
struct Bindings {
    GLint read = 0, draw = 0, texture = 0, active = 0, renderbuffer = 0;
    GLboolean scissor = GL_FALSE;
    Bindings() {
        g.GetIntegerv(0x8CAA, &read); // GL_READ_FRAMEBUFFER_BINDING
        g.GetIntegerv(GL_FRAMEBUFFER_BINDING, &draw);
        g.GetIntegerv(GL_ACTIVE_TEXTURE, &active);
        g.GetIntegerv(GL_TEXTURE_BINDING_2D_MULTISAMPLE, &texture);
        g.GetIntegerv(0x8CA7, &renderbuffer); // GL_RENDERBUFFER_BINDING
        scissor = g.IsEnabled(GL_SCISSOR_TEST);
        g.Disable(GL_SCISSOR_TEST);
    }
    ~Bindings() {
        g.BindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)read);
        g.BindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)draw);
        g.ActiveTexture((GLenum)active);
        g.BindTexture(GL_TEXTURE_2D_MULTISAMPLE, (GLuint)texture);
        bindRenderbuffer(0x8D41, (GLuint)renderbuffer);
        if (scissor) g.Enable(GL_SCISSOR_TEST); else g.Disable(GL_SCISSOR_TEST);
    }
};

bool begin(const char* material, int samples) {
    if (pending || samples < 2 || !selected(material)) return false;
    observedDraw = true;
    if (std::getenv("NWN_A2C_ORDERED_NO_COPY") || !functions()) return false;
    Bindings saved;
    GLint vp[4] = {}, type = 0, object = 0, format = 0, width = 0, height = 0;
    g.GetIntegerv(GL_VIEWPORT, vp);
    if (saved.draw == 0) return false;
    attachment(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, 0x8CD0, &type);
    attachment(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, 0x8CD1, &object);
    if (type == 0x8D41) { // GL_RENDERBUFFER
        bindRenderbuffer(0x8D41, (GLuint)object);
        renderbufferParameter(0x8D41, 0x8D44, &format);
        renderbufferParameter(0x8D41, 0x8D42, &width);
        renderbufferParameter(0x8D41, 0x8D43, &height);
    } else if (type == 0x1702) { // GL_TEXTURE; accepted draw is multisampled.
        g.BindTexture(GL_TEXTURE_2D_MULTISAMPLE, (GLuint)object);
        textureParameter(GL_TEXTURE_2D_MULTISAMPLE, 0, 0x1003, &format);
        textureParameter(GL_TEXTURE_2D_MULTISAMPLE, 0, 0x1000, &width);
        textureParameter(GL_TEXTURE_2D_MULTISAMPLE, 0, 0x1001, &height);
    } else return false;
    static bool reportedAttachment = false;
    if (!reportedAttachment) {
        fprintf(stderr, "[a2c][ordered-trial] measured drawFbo=%d attachmentType=0x%x object=%d format=0x%x size=%dx%d viewport=%d,%d,%d,%d\n",
                saved.draw, type, object, format, width, height, vp[0],vp[1],vp[2],vp[3]);
        reportedAttachment = true;
    }
    if (vp[0] < 0 || vp[1] < 0 || vp[2] <= 0 || vp[3] <= 0 ||
        vp[0] + vp[2] > width || vp[1] + vp[3] > height) return false;
    // NWN requests an unsized GL_DEPTH_STENCIL renderbuffer. Resolve its
    // measured component sizes instead of guessing the actual allocation.
    if (format == 0x84F9 || format == GL_DEPTH_COMPONENT) {
        GLint depthBits = 0, stencilBits = 0, component = 0;
        attachment(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, 0x8216, &depthBits);
        attachment(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, 0x8217, &stencilBits);
        attachment(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, 0x8211, &component);
        if (depthBits == 24) format = stencilBits ? 0x88F0 : GL_DEPTH_COMPONENT24;
        else if (depthBits == 32 && component == 0x1406)
            format = stencilBits ? 0x8CAD : 0x8CAC;
        else if (depthBits == 16 && !stencilBits) format = 0x81A5;
        else return false;
        static bool reportedSized = false;
        if (!reportedSized) {
            fprintf(stderr, "[a2c][ordered-trial] resolved depthBits=%d stencilBits=%d component=0x%x sizedFormat=0x%x\n",depthBits,stencilBits,component,format);
            reportedSized = true;
        }
    }
    if (format != GL_DEPTH_COMPONENT24 && format != 0x81A5 &&
        format != 0x8CAC && format != 0x88F0 && format != 0x8CAD) return false;
    if (!target.fbo || target.width != vp[2] || target.height != vp[3] ||
        target.samples != samples || target.format != format) {
        if (target.fbo) g.DeleteFramebuffers(1, &target.fbo);
        if (target.texture) g.DeleteTextures(1, &target.texture);
        target = {};
        g.GenTextures(1, &target.texture);
        g.BindTexture(GL_TEXTURE_2D_MULTISAMPLE, target.texture);
        g.TexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples, format,
                                vp[2], vp[3], GL_TRUE);
        g.GenFramebuffers(1, &target.fbo);
        g.BindFramebuffer(GL_FRAMEBUFFER, target.fbo);
        g.FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                               GL_TEXTURE_2D_MULTISAMPLE, target.texture, 0);
        g.DrawBuffer(GL_NONE); g.ReadBuffer(GL_NONE);
        const GLenum status = g.CheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            fprintf(stderr, "[a2c][ordered-trial] incomplete depth target=0x%x\n", status);
            g.DeleteFramebuffers(1, &target.fbo);
            g.DeleteTextures(1, &target.texture);
            target = {};
            return false;
        }
        target.width = vp[2]; target.height = vp[3];
        target.samples = samples; target.format = format;
    }
    g.BindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)saved.draw);
    g.BindFramebuffer(GL_DRAW_FRAMEBUFFER, target.fbo);
    g.BlitFramebuffer(vp[0], vp[1], vp[0]+vp[2], vp[1]+vp[3],
                       0, 0, vp[2], vp[3], GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    const GLenum error = g.GetError();
    if (error != GL_NO_ERROR) {
        fprintf(stderr, "[a2c][ordered-trial] capture failed=0x%x; native depth retained\n", error);
        return false;
    }
    destination = saved.draw;
    std::memcpy(viewport, vp, sizeof(viewport));
    pending = true;
    if (++captures == 1)
        fprintf(stderr, "[a2c][ordered-trial] material=%s samples=%d format=0x%x viewport=%dx%d capture ACTIVE\n",
                material, samples, format, vp[2], vp[3]);
    return true;
}

void end() {
    if (!pending) return;
    pending = false;
    Bindings saved;
    g.BindFramebuffer(GL_READ_FRAMEBUFFER, target.fbo);
    g.BindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)destination);
    g.BlitFramebuffer(0, 0, viewport[2], viewport[3], viewport[0], viewport[1],
                       viewport[0]+viewport[2], viewport[1]+viewport[3],
                       GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    const GLenum error = g.GetError();
    if (error != GL_NO_ERROR)
        fprintf(stderr, "[a2c][ordered-trial] restore failed=0x%x\n", error);
    else if (++restores == 1)
        fprintf(stderr, "[a2c][ordered-trial] depth restored; native color retained\n");
}
} // namespace ordered_a2c_trial
