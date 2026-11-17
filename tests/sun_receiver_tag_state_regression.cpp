#include "../nwn_shadowmap.cpp"
static GLint nativeFbo = 42, attachmentType = GL_NONE, maxBuffers = 8;
static GLint currentProgram=9;static bool knownProgram=true,depthWrite=true;
static unsigned bufferChanges=0;
static GLuint attached = 0;
static GLint bound2D = 101, boundMS = 102;
static GLenum drawBuffers[8] = {
    GL_COLOR_ATTACHMENT0, GL_NONE, GL_NONE, GL_NONE, GL_NONE, GL_NONE, GL_NONE, GL_NONE};
static int changedAttachments = 0, tagKind = -1, clears = 0;
static int capabilityQueries=0,textureQueries=0,textureBinds=0,allocations=0;
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
        *out = currentProgram;
    else if (key == GL_MAX_DRAW_BUFFERS) {
        ++capabilityQueries;*out = maxBuffers;
    }
    else if (key == GL_SAMPLES)
        *out = 8;
    else if (key == GL_TEXTURE_BINDING_2D) {
        ++textureQueries;*out=bound2D;
    } else if (key == GL_TEXTURE_BINDING_2D_MULTISAMPLE) {
        ++textureQueries;*out=boundMS;
    }
    else
        *out = 0;
}
static void get_bool(GLenum key, GLboolean *out) {
    if (key == GL_COLOR_WRITEMASK)
        std::fill_n(out, 4, GL_TRUE);
    else
        *out = key==GL_DEPTH_WRITEMASK ? depthWrite : GL_TRUE;
}
static GLboolean enabled(GLenum) { return GL_FALSE; }
static GLint location(GLuint, const char *) { return knownProgram?11:-1; }
static void attachment(GLenum, GLenum, GLenum key, GLint *out) {
    *out = key==GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME ? GLint(attached) : (attached ? GL_TEXTURE : attachmentType);
}
static bool deletedFbo=false;
static GLboolean is_framebuffer(GLuint fbo){return fbo==42 && !deletedFbo;}
static void bind_framebuffer(GLenum target, GLuint fbo){if(target==GL_DRAW_FRAMEBUFFER)nativeFbo=GLint(fbo);}
static void attach(GLenum, GLenum, GLenum, GLuint tex, GLint) {
    attached = tex;
    ++changedAttachments;
}
static void buffers(GLsizei n, const GLenum *data) { ++bufferChanges;std::copy_n(data, n, drawBuffers); }
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
    ++textureBinds;
    if (target == GL_TEXTURE_2D)
        bound2D = tex;
    else
        boundMS = tex;
}
static void multisample(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLboolean) {++allocations;}
static void gen_textures(GLsizei n, GLuint* out){static GLuint next=200;while(n--)*out++=next++;}
static void delete_textures(GLsizei,const GLuint*){}
static void image_2d(GLenum,GLint,GLint,GLsizei,GLsizei,GLint,GLenum,GLenum,const void*){++allocations;}
static void texture_parameter(GLenum,GLenum,GLint){}
struct HighlightGobFixture { void* owner; };
static void* highlight_external(void* gob) {
    return static_cast<HighlightGobFixture*>(gob)->owner;
}
int main() {
    g_sunTagBatchAttachments=false; // Exercise the legacy fallback first.
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
    nativeFbo=42;attachmentType=GL_NONE;maxBuffers=8;
    gl::IsFramebuffer=is_framebuffer;gl::BindFramebuffer=bind_framebuffer;
    g_sunTagBatchAttachments=true;
    ++g_shadowFrameContext.serial;
    int priorChanges=changedAttachments,priorClears=clears;
    for(int i=0;i<32;++i){
        g_currentDrawOwnerKind=i%2 ? DrawOwnerKind::Creature:DrawOwnerKind::NonCreature;
        begin_sun_receiver_tag();
        if(!g_sunTagBorrowed || tagKind!=(i%2))++failures;
        end_sun_receiver_tag();
        if(g_sunTagBorrowed || drawBuffers[7]!=GL_NONE || attached!=103)++failures;
    }
    nativeFbo=43; // Cleanup must restore whichever native target is current.
    finish_sun_receiver_tag_batch();
    bool batchOk=changedAttachments-priorChanges==2 && clears-priorClears==1 &&
        !attached && !g_sunTagBatchFbo && nativeFbo==43;
    printf("sun batched attachment: 32 draws, two attachment changes, one clear, exact FBO restore %s\n",batchOk?"PASS":"FAIL");
    if(!batchOk)++failures;
    nativeFbo=42;begin_sun_receiver_tag();end_sun_receiver_tag();
    attached=999; // Engine replacement must never be detached by cleanup.
    finish_sun_receiver_tag_batch();
    if(attached!=999 || g_sunTagBatchFbo)++failures;
    attached=0;
    begin_sun_receiver_tag();end_sun_receiver_tag();deletedFbo=true;
    priorChanges=changedAttachments;finish_sun_receiver_tag_batch();
    if(changedAttachments!=priorChanges || g_sunTagBatchFbo)++failures;
    deletedFbo=false;attached=0;g_sunTagBatchAttachments=false;
    const int oldTextureQueries=textureQueries,oldTextureBinds=textureBinds;
    tag_work::fastTextures=false;
    bool textureOk=ensure_sun_tag_textures(4,1,8) && textureQueries==oldTextureQueries+2 && textureBinds==oldTextureBinds+2;
    tag_work::fastTextures=true;
    const int fastQueries=textureQueries,fastBinds=textureBinds;
    textureOk=textureOk && ensure_sun_tag_textures(4,1,8) && textureQueries==fastQueries && textureBinds==fastBinds;
    gl::GenTextures=gen_textures;gl::DeleteTextures=delete_textures;gl::TexImage2D=image_2d;gl::TexParameteri=texture_parameter;
    int oldAllocations=allocations;
    textureOk=textureOk && ensure_sun_tag_textures(8,1,8) && allocations==oldAllocations+1 && bound2D==101 && boundMS==102;
    oldAllocations=allocations;
    textureOk=textureOk && ensure_sun_tag_textures(8,1,4) && allocations==oldAllocations+1;
    g_sunTagDummyMS=0;oldAllocations=allocations;
    textureOk=textureOk && ensure_sun_tag_textures(8,1,4) && allocations==oldAllocations+1;
    textureOk=textureOk && !ensure_sun_tag_textures(0,1,4) && !ensure_sun_tag_textures(8,1,32);
    tag_work::fastTextures=false;
    printf("sun no-allocation fast path, resize/sample/missing-target fallback: %s\n",textureOk?"PASS":"FAIL");
    if(!textureOk)++failures;
    tag_work::fastCapability=true;maxBuffers=8;++g_shadowFrameContext.serial;
    int beforeCapabilities=capabilityQueries;
    bool capabilityOk=sun_tag_draw_buffer_limit(1)==8 && sun_tag_draw_buffer_limit(1)==8 && capabilityQueries==beforeCapabilities+1;
    maxBuffers=4;
    capabilityOk=capabilityOk && sun_tag_draw_buffer_limit(2)==4 && capabilityQueries==beforeCapabilities+2;
    ++g_shadowFrameContext.serial;maxBuffers=8;
    capabilityOk=capabilityOk && sun_tag_draw_buffer_limit(2)==8 && capabilityQueries==beforeCapabilities+3;
    capabilityOk=capabilityOk && sun_tag_draw_buffer_limit(0)==8 && sun_tag_draw_buffer_limit(0)==8 && capabilityQueries==beforeCapabilities+5;
    tag_work::fastCapability=false;
    printf("sun immutable capability cache: context/scene/unknown-context guards %s\n",capabilityOk?"PASS":"FAIL");
    if(!capabilityOk)++failures;
    for(int mode:{5,6}) {
        ++g_shadowFrameContext.serial;
        g_sunTagBatchAttachments=true;nativeFbo=42;
        tag_work::suppressDrawTarget=mode==5;tag_work::suppressTagWrites=mode==6;
        GLboolean priorMask[4];std::copy_n(masks[7],4,priorMask);
        begin_sun_receiver_tag();
        bool probeOk=g_sunTagBorrowed && drawBuffers[7]==(mode==5?GL_NONE:GL_COLOR_ATTACHMENT0+7);
        if(mode==6)for(bool bit:masks[7])probeOk=probeOk && !bit;
        end_sun_receiver_tag();finish_sun_receiver_tag_batch();
        probeOk=probeOk && !g_sunTagBorrowed && !g_sunTagProbeMaskBorrowed && attached==0 &&
                drawBuffers[7]==GL_NONE && std::memcmp(priorMask,masks[7],sizeof(priorMask))==0;
        printf("sun GPU diagnostic mode%d target/mask restoration: %s\n",mode,probeOk?"PASS":"FAIL");
        if(!probeOk)++failures;
        tag_work::suppressDrawTarget=tag_work::suppressTagWrites=false;
        g_sunTagBatchAttachments=false;
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
