#include "../nwn_shadowmap.cpp"
static GLint nativeFbo = 42, attachmentType = GL_NONE, maxBuffers = 8;
static GLuint attached = 0;
static GLint bound2D = 101, boundMS = 102;
static GLenum drawBuffers[8] = {
    GL_COLOR_ATTACHMENT0, GL_NONE, GL_NONE, GL_NONE, GL_NONE, GL_NONE, GL_NONE, GL_NONE};
static int changedAttachments = 0, tagKind = -1, clears = 0;
static void get_int(GLenum key, GLint *out) {
    if (key == GL_VIEWPORT) {
        out[0] = out[1] = 0;
        out[2] = 4;
        out[3] = 1;
    } else if (key >= GL_DRAW_BUFFER0 && key < GL_DRAW_BUFFER0 + 8)
        *out = drawBuffers[key - GL_DRAW_BUFFER0];
    else if (key == GL_DRAW_FRAMEBUFFER_BINDING)
        *out = nativeFbo;
    else if (key == GL_CURRENT_PROGRAM)
        *out = 9;
    else if (key == GL_MAX_DRAW_BUFFERS)
        *out = maxBuffers;
    else if (key == GL_SAMPLES)
        *out = 8;
    else if (key == GL_TEXTURE_BINDING_2D)
        *out = bound2D;
    else if (key == GL_TEXTURE_BINDING_2D_MULTISAMPLE)
        *out = boundMS;
    else
        *out = 0;
}
static void get_bool(GLenum key, GLboolean *out) {
    if (key == GL_COLOR_WRITEMASK)
        std::fill_n(out, 4, GL_TRUE);
    else
        *out = GL_TRUE;
}
static GLboolean enabled(GLenum) { return GL_FALSE; }
static GLint location(GLuint, const char *) { return 11; }
static void attachment(GLenum, GLenum, GLenum, GLint *out) { *out = attachmentType; }
static void attach(GLenum, GLenum, GLenum, GLuint tex, GLint) {
    attached = tex;
    ++changedAttachments;
}
static void buffers(GLsizei n, const GLenum *data) { std::copy_n(data, n, drawBuffers); }
static GLenum complete(GLenum) { return GL_FRAMEBUFFER_COMPLETE; }
static void clear_uint(GLenum, GLint, const GLuint *) { ++clears; }
static GLboolean masks[8][4] = {
    {GL_FALSE, GL_TRUE, GL_FALSE, GL_TRUE},
    {GL_TRUE, GL_FALSE, GL_TRUE, GL_FALSE},
    {}, {}, {}, {}, {}, {GL_FALSE, GL_TRUE, GL_FALSE, GL_TRUE}};
static void get_bool_indexed(GLenum, GLuint index, GLboolean* out) {
    std::copy_n(masks[index], 4, out);
}
static void color_mask_indexed(GLuint index, GLboolean r, GLboolean g, GLboolean b, GLboolean a) {
    masks[index][0]=r; masks[index][1]=g; masks[index][2]=b; masks[index][3]=a;
}
static void color_mask(GLboolean r, GLboolean g, GLboolean b, GLboolean a) {
    for (GLuint i=0;i<8;++i) color_mask_indexed(i,r,g,b,a);
}
static void uniform_int(GLint, GLint value) { tagKind = value; }
static void bind_texture(GLenum target, GLuint tex) {
    if (target == GL_TEXTURE_2D)
        bound2D = tex;
    else
        boundMS = tex;
}
static void multisample(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLboolean) {}
struct HighlightGobFixture { void* owner; };
static void* highlight_external(void* gob) {
    return static_cast<HighlightGobFixture*>(gob)->owner;
}
int main() {
    GLboolean savedMasks[8][4];
    std::memcpy(savedMasks, masks, sizeof(masks));
    gl::GetIntegerv = get_int;
    gl::GetBooleanv = get_bool;
    gl::IsEnabled = enabled;
    gl::GetUniformLocation = location;
    gl::GetFramebufferAttachmentParameteriv = attachment;
    gl::FramebufferTexture2D = attach;
    gl::DrawBuffers = buffers;
    gl::CheckFramebufferStatus = complete;
    gl::ClearBufferuiv = clear_uint;
    gl::ColorMask = color_mask;
    gl::ColorMaski = color_mask_indexed;
    gl::GetBooleani_v = get_bool_indexed;
    gl::Uniform1i = uniform_int;
    gl::BindTexture = bind_texture;
    gl::TexImage2DMultisample = multisample;
    g_receiverEnabled = true;
    g_traceFrameActive = true;
    g_shadowFrameContext.valid = true;
    g_areaScene = g_traceCurrentScene = (void *)1;
    nwn_core::g_currentBucket = 2;
    g_sunTagTex = 103;
    g_sunTagDummy2D = 104;
    g_sunTagDummyMS = 105;
    g_sunTagWidth = 4;
    g_sunTagHeight = 1;
    g_sunTagSamples = 8;
    int failures = 0;
    for (auto kind : {DrawOwnerKind::Creature, DrawOwnerKind::NonCreature}) {
        ++g_shadowFrameContext.serial;
        g_currentDrawOwnerKind = kind;
        const int priorClears = clears;
        begin_sun_receiver_tag();
        const bool active =
            g_sunTagBorrowed && attached == 103 && drawBuffers[7] == GL_COLOR_ATTACHMENT0 + 7 &&
            clears == priorClears + 1 && tagKind == (kind == DrawOwnerKind::Creature ? 1 : 0);
        end_sun_receiver_tag();
        const bool restored = !g_sunTagBorrowed && attached == 0 &&
                              drawBuffers[0] == GL_COLOR_ATTACHMENT0 && drawBuffers[7] == GL_NONE &&
                              bound2D == 101 && boundMS == 102;
        printf("sun native MRT type=%d active=%d restored=%d %s\n", (int)kind, active, restored,
               active && restored ? "PASS" : "FAIL");
        if (!active || !restored)
            ++failures;
    }
    for (int scenario = 0; scenario < 3; ++scenario) {
        nativeFbo = scenario == 0 ? 0 : 42;
        attachmentType = scenario == 1 ? GL_TEXTURE : GL_NONE;
        maxBuffers = scenario == 2 ? 4 : 8;
        const int priorChanges = changedAttachments;
        begin_sun_receiver_tag();
        const bool safe = !g_sunTagBorrowed && changedAttachments == priorChanges;
        printf("sun unsupported/native-owned target %d unchanged=%d %s\n", scenario, safe,
               safe ? "PASS" : "FAIL");
        if (!safe)
            ++failures;
    }
    // Exercise the measured native list, including inherited part ownership
    // and removal. No mouse position or brightness heuristics participate.
    unsigned char scene[0xa0] = {};
    int ownerA = 0, ownerB = 0;
    HighlightGobFixture root = {&ownerA};
    const void* objects[] = {&root};
    const void* list = objects;
    int count = 1;
    std::memcpy(scene + 0x80, &list, sizeof(list));
    std::memcpy(scene + 0x88, &count, sizeof(count));
    g_areaScene = scene;
    eng::GobGetExternalData = highlight_external;
    g_currentDrawOwner = &ownerA;
    const bool matchedPart = current_draw_is_highlighted();
    g_currentDrawOwner = &ownerB;
    const bool otherObject = current_draw_is_highlighted();
    g_currentDrawOwner = &ownerA;
    count = 0;
    std::memcpy(scene + 0x88, &count, sizeof(count));
    const bool removed = !current_draw_is_highlighted();
    const bool highlightOk = matchedPart && !otherObject && removed;
    printf("sun native highlight ownership: inherited-part=%d other=%d removed=%d %s\n",
           matchedPart, otherObject, removed, highlightOk ? "PASS" : "FAIL");
    if (!highlightOk) ++failures;
    const bool masksRestored = std::memcmp(savedMasks, masks, sizeof(masks)) == 0;
    printf("sun indexed colour-mask restore: %s\n", masksRestored ? "PASS" : "FAIL");
    if (!masksRestored) ++failures;
    return failures ? 1 : 0;
}
