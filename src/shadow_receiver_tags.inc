// Receiver classification travels with the engine's actual material draw.
// A private integer MRT stores depth24, creature/highlight flags and depth span.
// Native depth testing, discard and MSAA/A2C coverage apply to the tag too.
// No native colour/depth is replaced, and no camera geometry is replayed.
static constexpr int kSunTagOutput = 7;
static bool g_sunTagBorrowed = false;
#if defined(NWN_TAG_WORK_BENCH_DIAGNOSTIC) && !defined(_WIN32)
static bool g_sunTagProbeMaskBorrowed=false;
static GLboolean g_sunTagProbeMask[4]={};
#endif
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
    if (tag_work::fastTextures && g_sunTagTex && g_sunTagDummy2D && g_sunTagDummyMS &&
        width==g_sunTagWidth && height==g_sunTagHeight && samples==g_sunTagSamples)
        return true;
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

// Hold the reserved attachment during one native
// scene pass, while restoring draw buffers after every individual draw.
// Linux defaults to this validated path; Windows retains per-draw attachment.
static GLuint g_sunTagBatchFbo = 0, g_sunTagBatchTexture = 0;
static GLenum g_sunTagBatchTarget = GL_TEXTURE_2D;
static void finish_sun_receiver_tag_batch() {
    if(!tag_buffer_lease::safe_context())return;
    tag_buffer_lease::release(tag_buffer_lease::Fence::Boundary);
    tag_buffer_lease::Internal internal;
    if (!g_sunTagBatchFbo) return;
    if (gl::IsFramebuffer && gl::IsFramebuffer(g_sunTagBatchFbo)) {
        GLint previous=0, type=0, name=0;
        gl::GetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&previous);
        gl::BindFramebuffer(GL_DRAW_FRAMEBUFFER,g_sunTagBatchFbo);
        gl::GetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,
            GL_COLOR_ATTACHMENT0+kSunTagOutput,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE,&type);
        gl::GetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,
            GL_COLOR_ATTACHMENT0+kSunTagOutput,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,&name);
        // Never detach an attachment the engine replaced during its pass.
        if (type==GL_TEXTURE && (GLuint)name==g_sunTagBatchTexture)
            gl::FramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0+kSunTagOutput,
                                    g_sunTagBatchTarget,0,0);
        gl::BindFramebuffer(GL_DRAW_FRAMEBUFFER,(GLuint)previous);
    }
    g_sunTagBatchFbo=g_sunTagBatchTexture=0;
    tag_buffer_lease::attachment_finished();
}

static void end_sun_receiver_tag() {
    if (!g_sunTagBorrowed)
        return;
    tag_buffer_lease::Internal internal;
    tag_work::Timer timing(tag_work::Restore);
#if defined(NWN_TAG_WORK_BENCH_DIAGNOSTIC) && !defined(_WIN32)
    if(g_sunTagProbeMaskBorrowed){
        gl::ColorMaski(kSunTagOutput,g_sunTagProbeMask[0],g_sunTagProbeMask[1],g_sunTagProbeMask[2],g_sunTagProbeMask[3]);
        g_sunTagProbeMaskBorrowed=false;
    }
#endif
    // Every caller restores its own FBO before reaching this boundary.
    if (!g_sunTagBatchFbo)
        gl::FramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + kSunTagOutput,
                                 g_sunTagSamples > 0 ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D, 0, 0);
    if(!tag_buffer_lease::retain())gl::DrawBuffers(8, g_sunTagOldDrawBuffers);
    g_sunTagBorrowed = false;
}

// Cache only an immutable limit, bounded by both current context and scene serial.
// Unknown contexts retain the checkpoint's query path. Mutable FBO state is never cached.
static GLint sun_tag_draw_buffer_limit(std::uintptr_t context) {
    static std::uintptr_t cachedContext=0;
    static unsigned cachedSerial=0;
    static GLint cachedLimit=0;
    if (!tag_work::fastCapability || !context) {
        GLint limit=0;gl::GetIntegerv(GL_MAX_DRAW_BUFFERS,&limit);return limit;
    }
    if (context!=cachedContext || cachedSerial!=g_shadowFrameContext.serial) {
        gl::GetIntegerv(GL_MAX_DRAW_BUFFERS,&cachedLimit);
        cachedContext=context;cachedSerial=g_shadowFrameContext.serial;
    }
    return cachedLimit;
}

// Any draw that cannot publish a tag must see the native buffer selection.
struct SunTagBufferEligibility {
    tag_buffer_lease::Internal internal;
    bool previouslyBorrowed=g_sunTagBorrowed;
    ~SunTagBufferEligibility(){
        if(!previouslyBorrowed && !g_sunTagBorrowed)
            tag_buffer_lease::release(tag_buffer_lease::Fence::Boundary);
    }
};
static bool material_receiver_preview();
#if defined(NWN_TAG_WORK_BENCH_DIAGNOSTIC) && !defined(_WIN32)
// Diagnostic snapshot only. The live capture texture remains the write target.
static GLuint g_materialSunSnapshot=0, g_materialStaticSnapshot=0;
static int g_materialStaticSize=0;
static bool g_materialStaticReady=false;
static std::uintptr_t g_materialSunContext=0;
static int g_materialSunSize=0;
static void* g_materialSunScene=nullptr;
static unsigned g_materialSunSerial=0;
static CascadeMathState g_materialSunMath;
using MaterialCopyImage = void (*)(GLuint, GLenum, GLint, GLint, GLint, GLint,
    GLuint, GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei);
static MaterialCopyImage g_materialCopyImage=nullptr;

static void release_material_sun_snapshot() {
    g_materialSunSerial=0;
    if (g_materialSunSnapshot && gl::DeleteTextures &&
        nwn_drawing_probe_context()==g_materialSunContext) {
        gl::DeleteTextures(1,&g_materialSunSnapshot);
        if (g_materialStaticSnapshot) gl::DeleteTextures(1,&g_materialStaticSnapshot);
        g_materialStaticSnapshot=0;
        g_materialStaticSize=0;
        g_materialStaticReady=false;
        g_materialSunSnapshot=0;
        g_materialSunContext=0;
        g_materialSunSize=0;
    }
}

static void configure_material_sun_audit() {
    g_materialCopyImage=nullptr;
    g_materialSunSerial=0;
    if (!gl::GetString || !gl::GetIntegerv) return;
    const auto* version=reinterpret_cast<const char*>(gl::GetString(GL_VERSION));
    int major=0, minor=0;
    if (!version || std::sscanf(version,"%d.%d",&major,&minor)!=2 || major<3) return;
    bool supported=major>4 || (major==4 && minor>=3);
    if (!supported) {
        using GetStringIndexed=const unsigned char* (*)(GLenum,GLuint);
        const auto getStringIndexed=reinterpret_cast<GetStringIndexed>(dlsym(RTLD_NEXT,"glGetStringi"));
        if (!getStringIndexed) return;
        GLint count=0;
        gl::GetIntegerv(0x821D,&count); // GL_NUM_EXTENSIONS
        for (GLint i=0;i<count;++i) {
            const auto* extension=reinterpret_cast<const char*>(getStringIndexed(0x1F03,i));
            if (extension && std::strcmp(extension,"GL_ARB_copy_image")==0) {
                supported=true;
                break;
            }
        }
    }
    if (supported)
        g_materialCopyImage=reinterpret_cast<MaterialCopyImage>(dlsym(RTLD_NEXT,"glCopyImageSubData"));
}

struct MaterialReceiverAudit {
    unsigned creatures=0, highlighted=0, blended=0;
    unsigned filteredSun=0, sunLayersTouchedThisFrame=0, completedLocal=0;
    unsigned snapshotCopies=0, snapshotReadyDraws=0;
    double snapshotCpuMs=0;
};
static bool g_materialReceiverAuditActive=false;
static MaterialReceiverAudit g_materialReceiverAudit;

static void snapshot_material_sun(void* scene) {
    if ((!g_materialReceiverAuditActive && !material_receiver_preview()) || !g_materialCopyImage || scene!=g_areaScene ||
        !g_sunObjectTex || !g_cascadeMath.valid || !g_shadowFrameContext.valid ||
        g_shadowFrameContext.scene!=scene || !gl::GetError || !gl::TexImage3D)
        return;
    const int layers=std::min(g_cascadeActiveCount,g_cascadeDynamicLayers);
    if (layers<=0 || layers>kCascadeCount || !g_shadowFrameContext.serial) return;
    for (int layer=0;layer<layers;++layer)
        if (g_sunObjectFrame[layer]!=g_shadowFrameContext.serial ||
            g_cascadeDynamicCaptureFrameLayer[layer]!=g_shadowFrameContext.serial)
            return;
    const auto context=nwn_drawing_probe_context();
    if (!context || (g_materialSunContext && context!=g_materialSunContext)) return;
    if (g_materialSunSerial==g_shadowFrameContext.serial && g_materialSunScene==scene) return;

    const double start=now_seconds();
    g_materialSunSerial=0;
    const auto allocate=[&](GLuint& texture,int& size) {
        if (texture && size==g_size) return true;
        GLint previous=0;
        gl::GetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY,&previous);
        if (!texture) gl::GenTextures(1,&texture);
        if (!texture) return false;
        g_materialSunContext=context;
        gl::BindTexture(GL_TEXTURE_2D_ARRAY,texture);
        gl::TexImage3D(GL_TEXTURE_2D_ARRAY,0,GL_DEPTH_COMPONENT24,g_size,g_size,kCascadeCount,
                       0,GL_DEPTH_COMPONENT,GL_UNSIGNED_INT,nullptr);
        gl::TexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        gl::TexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        gl::TexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_COMPARE_MODE,GL_COMPARE_REF_TO_TEXTURE);
        gl::TexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_COMPARE_FUNC,GL_LESS);
        gl::TexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        gl::TexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        gl::BindTexture(GL_TEXTURE_2D_ARRAY,static_cast<GLuint>(previous));
        if (gl::GetError()!=GL_NO_ERROR) return false;
        size=g_size;
        return true;
    };
    if (!allocate(g_materialSunSnapshot,g_materialSunSize)) return;
    g_materialStaticReady=false;
    if (material_receiver_preview() && g_cascadeStaticTex) {
        bool complete=true;
        for (int layer=0;layer<layers;++layer)
            complete=complete && g_cascadeStaticCaptureFrameLayer[layer]==g_shadowFrameContext.serial;
        if (complete && allocate(g_materialStaticSnapshot,g_materialStaticSize)) {
            g_materialCopyImage(g_cascadeStaticTex,GL_TEXTURE_2D_ARRAY,0,0,0,0,
                                g_materialStaticSnapshot,GL_TEXTURE_2D_ARRAY,0,0,0,0,
                                g_size,g_size,layers);
            g_materialStaticReady=gl::GetError()==GL_NO_ERROR;
        }
    }

    g_materialCopyImage(g_sunObjectTex,GL_TEXTURE_2D_ARRAY,0,0,0,0,
                        g_materialSunSnapshot,GL_TEXTURE_2D_ARRAY,0,0,0,0,
                        g_size,g_size,layers);
    if (gl::GetError()!=GL_NO_ERROR) return;
    g_materialSunMath=g_cascadeMath;
    g_materialSunScene=scene;
    g_materialSunSerial=g_shadowFrameContext.serial;
    ++g_materialReceiverAudit.snapshotCopies;
    g_materialReceiverAudit.snapshotCpuMs+=(now_seconds()-start)*1000.0;
}

static void observe_material_receiver_candidate(bool creature, bool highlighted) {
    if (!g_materialReceiverAuditActive || !creature || nwn_core::g_currentBucket!=2) return;
    auto& audit=g_materialReceiverAudit;
    ++audit.creatures;
    if (g_materialSunSerial && g_materialSunSerial+1==g_shadowFrameContext.serial &&
        g_materialSunScene==g_areaScene && g_materialSunSize==g_size &&
        g_materialSunMath.valid && nwn_drawing_probe_context()==g_materialSunContext)
        ++audit.snapshotReadyDraws;
    if (highlighted) ++audit.highlighted;
    if (gl::IsEnabled && gl::IsEnabled(GL_BLEND)) ++audit.blended;
    if (g_sunObjectTex) {
        ++audit.filteredSun;
        const int layers=std::min(g_cascadeActiveCount,g_cascadeDynamicLayers);
        for (int layer=0;layer<layers;++layer) {
            if (g_sunObjectFrame[layer]==g_shadowFrameContext.serial) {
                ++audit.sunLayersTouchedThisFrame;
                break;
            }
        }
    }
    if (g_localObjectMapReady && g_localObjectTargetUsable && g_localObjectDepthTex &&
        g_localObjectDepthTex!=g_localObjectWriteDepthTex)
        ++audit.completedLocal;
}
#endif

static bool material_receiver_preview() {
#if defined(NWN_TAG_WORK_BENCH_DIAGNOSTIC) && !defined(_WIN32)
    static const bool enabled=[] {
        const char* value=shadow_getenv("NWN_MATERIAL_RECEIVER_PREVIEW");
        return value && atoi(value)!=0;
    }();
    return enabled;
#else
    return false;
#endif
}

#if defined(NWN_TAG_WORK_BENCH_DIAGNOSTIC) && !defined(_WIN32)
// Read-only attachment gate. A shared attachment is necessary before checking
// ownership of individual stencil bits or changing any stencil state.
namespace stencil_receiver_audit {
struct Observation {
    GLint fbo=0, type=0, name=0, bits=0, samples=0;
    GLint depthType=0, depthName=0, depthBits=0, readFbo=0;
    bool composite=false;
    unsigned count=0;
};
static bool active=false;
static unsigned serial=0;
static std::vector<Observation> observations;
struct BucketBoundary {
    int bucket;
    GLint drawFbo, samples;
    unsigned staticMask, dynamicMask, filteredSunMask;
    bool localReady;
};
static std::vector<BucketBoundary> boundaries;

struct CreatureDraw {
    int bucket=0;
    GLint program=0, depthWrite=0, blend=0, coverage=0, layerPass=-1;
    GLint mode2=-1;
    unsigned staticMask=0, filteredSunMask=0, count=0;
    bool filteredLocal=false;
};
static std::vector<CreatureDraw> creatureDraws;

static void observe_creature_draw() {
    if (!active || !serial || serial!=g_shadowFrameContext.serial ||
        !current_draw_is_creature() || nwn_core::g_currentBucket<0 ||
        nwn_core::g_currentBucket==2 || g_traceCurrentScene!=g_areaScene ||
        g_inOurPass || g_cascadeReplayActive || g_localLightPassActive ||
        nwn_core::g_ownedPass || g_overlayPassActive ||
        !gl::GetIntegerv || !gl::IsEnabled || !gl::GetUniformLocation) return;
    CreatureDraw row;
    row.bucket=nwn_core::g_currentBucket;
    gl::GetIntegerv(GL_CURRENT_PROGRAM,&row.program);
    if (!row.program) return;
    gl::GetIntegerv(GL_DEPTH_WRITEMASK,&row.depthWrite);
    row.blend=gl::IsEnabled(GL_BLEND);
    row.coverage=gl::IsEnabled(GL_SAMPLE_ALPHA_TO_COVERAGE);
    GLint layer=gl::GetUniformLocation(row.program,"wmLayerAlphaPass");
    if (layer<0) layer=gl::GetUniformLocation(row.program,"wmDiagAlphaPass");
    if (layer>=0 && gl::GetUniformfv) {
        GLfloat value=-1.0f;
        gl::GetUniformfv(row.program,layer,&value);
        if (value>=0.0f && value<=16.0f) row.layerPass=static_cast<GLint>(value);
    }
    const GLint mode2=gl::GetUniformLocation(row.program,"nwnA2cPass");
    if (mode2>=0 && gl::GetUniformiv) gl::GetUniformiv(row.program,mode2,&row.mode2);
    for (int i=0;i<kCascadeCount;++i) {
        if (g_cascadeStaticCaptureFrameLayer[i]==serial) row.staticMask|=1u<<i;
        if (g_sunObjectTex && g_sunObjectFrame[i]==serial) row.filteredSunMask|=1u<<i;
    }
    row.filteredLocal=g_localObjectMapReady && g_localObjectTargetUsable &&
                      g_localObjectDepthTex && g_localObjectDepthTex!=g_localObjectWriteDepthTex;
    for (auto& entry:creatureDraws) {
        if (entry.bucket==row.bucket && entry.program==row.program &&
            entry.depthWrite==row.depthWrite && entry.blend==row.blend &&
            entry.coverage==row.coverage && entry.layerPass==row.layerPass &&
            entry.mode2==row.mode2 && entry.staticMask==row.staticMask &&
            entry.filteredSunMask==row.filteredSunMask && entry.filteredLocal==row.filteredLocal) {
            ++entry.count;
            return;
        }
    }
    if (creatureDraws.size()<128) {
        row.count=1;
        creatureDraws.push_back(row);
    }
}

static void after_bucket(int bucket) {
    if (!active || !serial || serial!=g_shadowFrameContext.serial ||
        boundaries.size()>=64 || !gl::GetIntegerv) return;
    BucketBoundary row{bucket,0,0,0,0,0,g_localObjectMapReady};
    gl::GetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&row.drawFbo);
    gl::GetIntegerv(GL_SAMPLES,&row.samples);
    for (int layer=0;layer<kCascadeCount;++layer) {
        if (g_cascadeStaticCaptureFrameLayer[layer]==serial) row.staticMask|=1u<<layer;
        if (g_cascadeDynamicCaptureFrameLayer[layer]==serial) row.dynamicMask|=1u<<layer;
        if (g_sunObjectTex && g_sunObjectFrame[layer]==serial) row.filteredSunMask|=1u<<layer;
    }
    boundaries.push_back(row);
}


static void observe(bool composite, GLint fbo) {
    if (!active || !gl::GetIntegerv || !gl::GetFramebufferAttachmentParameteriv) return;
    const unsigned frame=g_shadowFrameContext.serial;
    if (!frame) return;
    if (!serial) serial=frame;
    if (serial!=frame) return;

    constexpr GLenum stencilBits=0x0D57;
    constexpr GLenum stencilAttachment=0x8D20;
    Observation found;
    found.fbo=fbo;
    found.composite=composite;
    gl::GetIntegerv(stencilBits,&found.bits);
    gl::GetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&found.readFbo);
    // GL_DEPTH is the default framebuffer attachment selector.
    const GLenum depthAttachment=fbo ? GL_DEPTH_ATTACHMENT : 0x1801;
    gl::GetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,depthAttachment,
        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE,&found.depthType);
    if (found.depthType!=GL_NONE) {
        gl::GetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,depthAttachment,
            0x8216,&found.depthBits); // GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE
        if (fbo)
            gl::GetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,depthAttachment,
                GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,&found.depthName);
    }
    gl::GetIntegerv(GL_SAMPLES,&found.samples);
    if (fbo) {
        gl::GetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,stencilAttachment,
            GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE,&found.type);
        if (found.type!=GL_NONE)
            gl::GetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,stencilAttachment,
                GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,&found.name);
    }
    for (auto& observation:observations) {
        if (observation.fbo==found.fbo && observation.type==found.type &&
            observation.name==found.name && observation.bits==found.bits &&
            observation.samples==found.samples && observation.composite==found.composite &&
            observation.depthType==found.depthType && observation.depthName==found.depthName &&
            observation.depthBits==found.depthBits && observation.readFbo==found.readFbo) {
            ++observation.count;
            return;
        }
    }
    found.count=1;
    observations.push_back(found);
}
}
#endif

#if defined(NWN_TAG_WORK_BENCH_DIAGNOSTIC) && !defined(_WIN32)
namespace creature_coverage {
static bool active=false;
static GLint framebuffer=0;
struct Counts { unsigned seen=0, owner=0, blend=0, depthDisabled=0, coverage=0, target=0, shader=0, depthWrite=0, submitted=0; };
static Counts counts;
static bool shade_preview() {
    static const bool value=[] {
        const char* flag=shadow_getenv("NWN_CREATURE_FILTERED_SHADE_PREVIEW");
        return flag && atoi(flag)!=0 && creature_filtered_sun_requested();
    }();
    return value;
}
static bool completed_sun() {
    if (!g_sunObjectTex || !g_cascadeStaticTex || !g_shadowFrameContext.valid ||
        !g_cascadeMath.valid) return false;
    const int layers=std::min(g_cascadeActiveCount,g_cascadeDynamicLayers);
    if (layers<=0 || layers>kCascadeCount) return false;
    for (int i=0;i<layers;++i)
        if (g_sunObjectFrame[i]!=g_shadowFrameContext.serial ||
            g_cascadeStaticCaptureFrameLayer[i]!=g_shadowFrameContext.serial)
            return false;
    return true;
}
static bool enabled() {
    static const bool value=[] {
        const char* flag=shadow_getenv("NWN_CREATURE_COVERAGE_PREVIEW");
        return flag && atoi(flag)!=0;
    }();
    return value || shade_preview();
}
static GLint nativeTint=-1;
static GLuint nativeTintProgram=0;

static void begin_native_tint() {
    if (!enabled() || shade_preview() || nativeTint>=0 || !current_draw_is_creature() ||
        nwn_core::g_currentBucket!=3 || g_traceCurrentScene!=g_areaScene ||
        g_inOurPass || g_cascadeReplayActive || g_localLightPassActive ||
        nwn_core::g_ownedPass || g_overlayPassActive || !gl::GetIntegerv ||
        !gl::GetUniformLocation || !gl::Uniform1i || !gl::GetUniformiv ||
        !gl::IsEnabled || !gl::IsEnabled(GL_SAMPLE_ALPHA_TO_COVERAGE)) return;
    GLint program=0;
    gl::GetIntegerv(GL_CURRENT_PROGRAM,&program);
    if (!program) return;
    const GLint mode=gl::GetUniformLocation(program,"nwnA2cPass");
    GLint pass=0;
    if (mode>=0) gl::GetUniformiv(program,mode,&pass);
    if (pass!=1) return;
    const GLint tint=gl::GetUniformLocation(program,"nwnCreatureCoverage");
    if (tint<0) return;
    nativeTint=tint;
    nativeTintProgram=static_cast<GLuint>(program);
    gl::Uniform1i(tint,1);
    static unsigned reports=0;
    if (reports++<6)
        fprintf(stderr,"[creature-coverage-native] frame=%u program=%d mode2=1\n",
                g_shadowFrameContext.serial,program);
}

static void end_native_tint() {
    if (nativeTint<0) return;
    GLint program=0;
    gl::GetIntegerv(GL_CURRENT_PROGRAM,&program);
    if (static_cast<GLuint>(program)==nativeTintProgram) gl::Uniform1i(nativeTint,0);
    nativeTint=-1;
}

static bool inject(std::string& source) {
    if (!enabled() ||
        source.find("out vec4 compat_glFragColor;")==std::string::npos ||
        source.find("gl_FragDepth")!=std::string::npos) return false;
    const auto main=source.rfind("void main");
    if (main==std::string::npos) return false;
    source.replace(main+5,4,"nwnCoverageNativeMain");
    source+="\nuniform int nwnCreatureCoverage;"
            "\nvoid main(){nwnCoverageNativeMain();"
            "if(nwnCreatureCoverage==1)compat_glFragColor.rgb=vec3(1.0,0.0,1.0);";
    if (source.find("float nwnA2cDirectionalShadow()")!=std::string::npos)
        source+="if(nwnCreatureCoverage==2)compat_glFragColor.rgb*="
                "min(nwnA2cDirectionalShadow(),nwnA2cLocalShadow());";
    source+="}\n";
    return true;
}
template<class Draw> static void submit(Draw draw) {
    ++counts.seen;
    if (!current_draw_is_creature()) { ++counts.owner; return; }
    if (gl::IsEnabled(GL_BLEND)) ++counts.blend;
    const bool depthEnabled=gl::IsEnabled(GL_DEPTH_TEST);
    if (!depthEnabled) ++counts.depthDisabled;
    // Leave the engine's alpha-to-coverage mask intact. The tint only changes RGB.
    if (gl::IsEnabled(GL_SAMPLE_ALPHA_TO_COVERAGE)) ++counts.coverage;
    GLint target=0,program=0,depthFunc=0;
    gl::GetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&target);
    gl::GetIntegerv(GL_CURRENT_PROGRAM,&program);
    if (target!=framebuffer || !program) { ++counts.target; return; }
    const GLint tint=gl::GetUniformLocation(program,"nwnCreatureCoverage");
    if (tint<0 || !gl::GetBooleani_v || !gl::ColorMaski) { ++counts.shader; return; }
    const bool shade=shade_preview();
    if (shade && (!completed_sun() || !nwn_shadow_begin_a2c_receiver(program))) return;
    GLboolean colourMask[4]={};
    gl::GetBooleani_v(GL_COLOR_WRITEMASK,0,colourMask);
    gl::ColorMaski(0,GL_TRUE,GL_TRUE,GL_TRUE,GL_FALSE);
    GLboolean depthWrite=GL_FALSE;
    gl::GetBooleanv(GL_DEPTH_WRITEMASK,&depthWrite);
    if (!depthWrite) ++counts.depthWrite;
    gl::GetIntegerv(GL_DEPTH_FUNC,&depthFunc);
    gl::Uniform1i(tint,shade ? 2 : 1);
    if (!depthEnabled) gl::Enable(GL_DEPTH_TEST);
    gl::DepthFunc(0x0202); // GL_EQUAL
    gl::DepthMask(GL_FALSE);
    draw();
    gl::ColorMaski(0,colourMask[0],colourMask[1],colourMask[2],colourMask[3]);
    gl::DepthMask(depthWrite);
    gl::DepthFunc(static_cast<GLenum>(depthFunc));
    if (!depthEnabled) gl::Disable(GL_DEPTH_TEST);
    gl::Uniform1i(tint,0);
    if (shade) nwn_shadow_end_a2c_receiver();
    ++counts.submitted;
}
}
#endif

static void begin_sun_receiver_tag() {
    SunTagBufferEligibility bufferEligibility;
    tag_work::BeginDraw beginTiming;
    if(!tag_buffer_lease::safe_context())return;
    if (material_receiver_preview() || g_shadowStageSkipTags || g_sunTagBorrowed || !g_receiverEnabled || g_inOurPass || g_cascadeReplayActive ||
        g_localLightPassActive || g_overlayPassActive || nwn_core::g_ownedPass ||
        g_inStencilShadow || !g_shadowFrameContext.valid || !g_traceFrameActive ||
        g_traceCurrentScene != g_areaScene || nwn_core::g_currentBucket < 0 || !gl::DrawBuffers ||
        !gl::ClearBufferuiv || !gl::GetBooleani_v || !gl::ColorMaski)
        return;
    tag_work::Timer basicTiming(tag_work::Basics);
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
    basicTiming.end();
    { tag_work::Timer timing(tag_work::Capability);
      maxBuffers=sun_tag_draw_buffer_limit(tag_work::fastCapability ? nwn_drawing_probe_context() : 0); }
    if (maxBuffers < 8)
        return;
    GLint type = 0;
    gl::GetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,
                                            GL_COLOR_ATTACHMENT0 + kSunTagOutput,
                                            GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &type);
    bool held = false;
    if (g_sunTagBatchFbo==(GLuint)fbo && type==GL_TEXTURE) {
        GLint name=0;
        gl::GetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,
            GL_COLOR_ATTACHMENT0+kSunTagOutput,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,&name);
        held=(GLuint)name==g_sunTagBatchTexture;
    }
    if (type != GL_NONE && !held)
        return; // Never borrow an attachment NWN already owns.
    gl::GetIntegerv(GL_VIEWPORT, vp);
    if (vp[0] != 0 || vp[1] != 0)
        return;
    if (g_sunTagFrame == g_shadowFrameContext.serial && g_sunTagSourceFbo != (GLuint)fbo)
        return;
    gl::GetIntegerv(GL_SAMPLES, &samples);
    bool texturesReady=false;
    { tag_work::Timer timing(tag_work::Textures);
      texturesReady=ensure_sun_tag_textures(vp[2],vp[3],samples); }
    if (!texturesReady) return;
    { tag_work::Timer timing(tag_work::DrawBufferRead);
    for (int i = 0; i < 8; ++i) {
        GLint value = 0;
        gl::GetIntegerv(GL_DRAW_BUFFER0 + i, &value);
        g_sunTagOldDrawBuffers[i] = (GLenum)value;
    }
    }
    tag_buffer_lease::native_buffers((GLuint)fbo,g_sunTagOldDrawBuffers);
    GLenum buffers[8];
    std::copy_n(g_sunTagOldDrawBuffers, 8, buffers);
    buffers[kSunTagOutput] = GL_COLOR_ATTACHMENT0 + kSunTagOutput;
    { tag_work::Timer timing(tag_work::Attachment);
    if (!held || g_sunTagBatchTexture!=g_sunTagTex) {
        if (g_sunTagBatchFbo) finish_sun_receiver_tag_batch();
        gl::FramebufferTexture2D(GL_DRAW_FRAMEBUFFER, buffers[kSunTagOutput],
                                 samples > 0 ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D, g_sunTagTex,0);
        if (g_sunTagBatchAttachments && gl::IsFramebuffer && gl::BindFramebuffer) {
            g_sunTagBatchFbo=(GLuint)fbo;
            g_sunTagBatchTexture=g_sunTagTex;
            g_sunTagBatchTarget=samples>0 ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
            tag_buffer_lease::attachment_selected();
        }
    }
    }
    if(tag_work::suppressDrawTarget)buffers[kSunTagOutput]=GL_NONE;
    { tag_work::Timer timing(tag_work::DrawBufferEnable);
      if(!tag_buffer_lease::already_selected((GLuint)fbo,buffers)) {
          gl::DrawBuffers(8,buffers);
          tag_buffer_lease::selected((GLuint)fbo,g_sunTagOldDrawBuffers,buffers);
      }
    }
    g_sunTagBorrowed = true;
    GLenum complete=0;
    {
        tag_work::Timer timing(tag_work::Validation);
#if defined(NWN_TAG_WORK_BENCH_DIAGNOSTIC) && !defined(_WIN32)
        static GLuint checkedFbo=0,checkedTexture=0;
        static unsigned checkedSerial=0;
        static GLenum checkedBuffers[8]={};
        bool repeated=tag_work::skipRepeatedValidation && held && checkedFbo==(GLuint)fbo && checkedTexture==g_sunTagTex &&
                      checkedSerial==g_shadowFrameContext.serial &&
                      std::memcmp(checkedBuffers,buffers,sizeof(buffers))==0;
        if (tag_work::skipRepeatedValidation && repeated) complete=GL_FRAMEBUFFER_COMPLETE;
        else {
            complete=gl::CheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
            if(tag_work::skipRepeatedValidation && complete==GL_FRAMEBUFFER_COMPLETE){
                checkedFbo=(GLuint)fbo;checkedTexture=g_sunTagTex;
                checkedSerial=g_shadowFrameContext.serial;
                std::copy_n(buffers,8,checkedBuffers);
            }
        }
#else
        complete=gl::CheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
#endif
    }
    if (complete != GL_FRAMEBUFFER_COMPLETE) {
        end_sun_receiver_tag();
        finish_sun_receiver_tag_batch();
        return;
    }
    if (g_sunTagFrame != g_shadowFrameContext.serial) {
        if(tag_work::suppressDrawTarget){
            buffers[kSunTagOutput]=GL_COLOR_ATTACHMENT0+kSunTagOutput;
            gl::DrawBuffers(8,buffers);
        }
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
        if(tag_work::suppressDrawTarget){
            buffers[kSunTagOutput]=GL_NONE;
            gl::DrawBuffers(8,buffers);
        }
    }
#if defined(NWN_TAG_WORK_BENCH_DIAGNOSTIC) && !defined(_WIN32)
    if(tag_work::suppressTagWrites){
        gl::GetBooleani_v(GL_COLOR_WRITEMASK,kSunTagOutput,g_sunTagProbeMask);
        gl::ColorMaski(kSunTagOutput,GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);
        g_sunTagProbeMaskBorrowed=true;
    }
#endif
    bool highlighted=false;
    { tag_work::Timer timing(tag_work::Classification);
      highlighted=current_draw_is_highlighted();
      const bool creature=current_draw_is_creature();
      gl::Uniform1i(kind,(creature?1:0)|(highlighted?2:0));
#if defined(NWN_TAG_WORK_BENCH_DIAGNOSTIC) && !defined(_WIN32)
      observe_material_receiver_candidate(creature,highlighted);
#endif
    }
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
