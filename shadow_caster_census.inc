// One-frame read-only compatibility evidence, never a rendering fast path.
namespace caster_census {
#if defined(NWN_TAG_WORK_BENCH_DIAGNOSTIC) && !defined(_WIN32)
static FILE* events=nullptr;
static void* activePart=nullptr;
using EngineModel=const float*(*)();
using EnginePartPredicate=bool(*)(void*);
static EngineModel engineModel=nullptr;
static EnginePartPredicate engineOpaque=nullptr,engineRenderable=nullptr;
static void report_engine_contract(const char* boundary,void* part){
    if(!events || !part || !engineModel || !engineOpaque || !engineRenderable)return;
    GLint program=0;gl::GetIntegerv(GL_CURRENT_PROGRAM,&program);
    fprintf(events,"engine_contract,%s,%p,%d,%d,%d",boundary,part,program,int(engineOpaque(part)),int(engineRenderable(part)));
    const float* model=engineModel();
    if(model)for(int i=0;i<16;++i)fprintf(events,",%.9g",double(model[i]));
    fputc('\n',events);
}

static std::vector<void*> nominatedParts;
static bool currentNominated=false;
static double nominatedAll=0,nominatedLater=0,nominatedDriverLater=0,nominatedDriverAll=0;
static double crossAll=0,crossLater=0,crossDriverLater=0;
static unsigned crossSourceCalls=0,crossLaterCalls=0;
static std::vector<double> crossAllSamples,crossLaterSamples,crossDriverSamples,crossOtherSamples,crossSourceSamples,crossCallSamples;
static bool sun_mesh_pass(){return g_cascadeReplayActive && g_cascadeReplayBucket>=2 && g_cascadeReplayBucket<=3;}
static std::vector<double> localDriverSamples,localOtherSamples;
static std::vector<double> nominatedAllSamples,nominatedLaterSamples,nominatedDriverSamples,nominatedOtherSamples;
static bool timingRunning=false,timingFrame=false;
static unsigned partDepth=0;
static double timingStart=0,partAll=0,partNonCreature=0;
static unsigned localDraws=0,coveredDraws=0,partCalls=0;
static ShadowFrameContext timingReference;
static std::vector<double> allSamples,nonCreatureSamples,coverageSamples,drawSamples,callSamples;
static double median(std::vector<double> values){if(values.empty())return 0;std::sort(values.begin(),values.end());return values[values.size()/2];}
template<class Call> static inline void submit(Call call){
    const bool local=timingFrame && g_localLightPassActive && currentNominated;
    const bool later=timingFrame && currentNominated && (g_localLightPassActive || (sun_mesh_pass() && g_cascadeReplayLayer>0));
    if(local || later){const double start=now_seconds();call();const double elapsed=(now_seconds()-start)*1000;if(local){nominatedDriverAll+=elapsed;if(g_localObjectCaptureLayer>0)nominatedDriverLater+=elapsed;}if(later)crossDriverLater+=elapsed;}
    else call();
}

struct PartScope {
    bool enabled=false,root=false,nonCreature=false,nominated=false,later=false,local=false,crossTarget=false,entered=false,previousNominated=false;void* previousPart=nullptr;double start=0;
    explicit PartScope(void* part){
        if(events || timingFrame){entered=true;previousPart=activePart;previousNominated=currentNominated;activePart=part;currentNominated=std::find(nominatedParts.begin(),nominatedParts.end(),part)!=nominatedParts.end();}
        if(events)report_engine_contract("part-enter",part);
        nominated=currentNominated;later=g_localObjectCaptureLayer>0;
        local=g_localLightPassActive;crossTarget=local || (sun_mesh_pass() && g_cascadeReplayLayer>0);
        enabled=timingFrame && (local || sun_mesh_pass());
        if(enabled){root=partDepth++==0;nonCreature=g_currentDrawOwnerKind==DrawOwnerKind::NonCreature;start=now_seconds();}
    }
    ~PartScope(){if(enabled){--partDepth;if(root){const double elapsed=(now_seconds()-start)*1000;if(local){partAll+=elapsed;if(nonCreature)partNonCreature+=elapsed;if(nominated){nominatedAll+=elapsed;if(later)nominatedLater+=elapsed;}++partCalls;}if(nominated){crossAll+=elapsed;if(crossTarget){crossLater+=elapsed;++crossLaterCalls;}else ++crossSourceCalls;}}}if(entered){activePart=previousPart;currentNominated=previousNominated;}}
};
struct TimingScope {
    bool owns=false;
    explicit TimingScope(void* scene){
        if(scene!=g_areaScene || !g_shadowFrameContext.valid || events || timingFrame)return;
        const double now=now_seconds();
        if(!timingRunning){
            FILE* marker=fopen("/tmp/nwn-caster-timing.start","r");if(!marker)return;
            unsigned pid=0;int parsed=fscanf(marker,"%u",&pid);fclose(marker);
            if(parsed!=1 || pid!=(unsigned)getpid())return;
            std::remove("/tmp/nwn-caster-timing.start");
            localDriverSamples.clear();localOtherSamples.clear();crossAllSamples.clear();crossLaterSamples.clear();crossDriverSamples.clear();crossOtherSamples.clear();crossSourceSamples.clear();crossCallSamples.clear();
            nominatedAllSamples.clear();nominatedLaterSamples.clear();nominatedDriverSamples.clear();nominatedOtherSamples.clear();
            allSamples.clear();nonCreatureSamples.clear();coverageSamples.clear();drawSamples.clear();callSamples.clear();
            timingReference=g_shadowFrameContext;timingStart=now;timingRunning=true;
            fprintf(stderr,"[caster-timing] START pid=%u local_receive=%d cast=%d; 12 seconds, CPU hook coverage only\n",pid,int(g_localLightReceiver),int(g_localLightEmittersCast));
        }
        bool same=timingReference.scene==scene && std::memcmp(timingReference.viewport,g_shadowFrameContext.viewport,sizeof(timingReference.viewport))==0;
        for(int i=0;i<16;++i)if(std::fabs(timingReference.viewInverse[i]-g_shadowFrameContext.viewInverse[i])>.0001f || std::fabs(timingReference.projection[i]-g_shadowFrameContext.projection[i])>.00001f)same=false;
        if(!same){timingRunning=false;fprintf(stderr,"[caster-timing] ABORT camera changed\n");return;}
        if(now-timingStart>=12){
            timingRunning=false;
            fprintf(stderr,"[caster-timing] COMPLETE frames=%zu part_all_ms=%.6f part_non_creature_ms=%.6f local_draws=%.0f covered_draws=%.0f root_part_calls=%.0f (nested intervals; not completed frame time)\n",allSamples.size(),median(allSamples),median(nonCreatureSamples),median(drawSamples),median(coverageSamples),median(callSamples));
            fprintf(stderr,"[caster-timing] nominated_parts=%zu all_ms=%.6f later_light_body_ms=%.6f later_light_draw_submission_ms=%.6f later_light_other_upper_bound_ms=%.6f (diagnostic nomination, not rendering authority)\n",nominatedParts.size(),median(nominatedAllSamples),median(nominatedLaterSamples),median(nominatedDriverSamples),median(nominatedOtherSamples));
            fprintf(stderr,"[caster-timing] cross_sun_local all_ms=%.6f later_body_ms=%.6f later_draw_submission_ms=%.6f later_other_upper_bound_ms=%.6f source_mesh_calls=%.0f later_mesh_calls=%.0f (source=first sun cascade; no rendering reuse)\n",median(crossAllSamples),median(crossLaterSamples),median(crossDriverSamples),median(crossOtherSamples),median(crossSourceSamples),median(crossCallSamples));
            fprintf(stderr,"[caster-timing] local_consumers_from_sun body_ms=%.6f draw_submission_ms=%.6f other_upper_bound_ms=%.6f (no rendering reuse)\n",median(nominatedAllSamples),median(localDriverSamples),median(localOtherSamples));return;
        }
        if(now-timingStart<2)return;
        partAll=partNonCreature=nominatedAll=nominatedLater=nominatedDriverLater=nominatedDriverAll=crossAll=crossLater=crossDriverLater=0;crossSourceCalls=crossLaterCalls=0;localDraws=coveredDraws=partCalls=0;partDepth=0;timingFrame=owns=true;
    }
    ~TimingScope(){if(owns){timingFrame=false;localDriverSamples.push_back(nominatedDriverAll);localOtherSamples.push_back(std::max(0.0,nominatedAll-nominatedDriverAll));crossAllSamples.push_back(crossAll);crossLaterSamples.push_back(crossLater);crossDriverSamples.push_back(crossDriverLater);crossOtherSamples.push_back(std::max(0.0,crossLater-crossDriverLater));crossSourceSamples.push_back(crossSourceCalls);crossCallSamples.push_back(crossLaterCalls);nominatedAllSamples.push_back(nominatedAll);nominatedLaterSamples.push_back(nominatedLater);nominatedDriverSamples.push_back(nominatedDriverLater);nominatedOtherSamples.push_back(std::max(0.0,nominatedLater-nominatedDriverLater));allSamples.push_back(partAll);nonCreatureSamples.push_back(partNonCreature);coverageSamples.push_back(coveredDraws);drawSamples.push_back(localDraws);callSamples.push_back(partCalls);}}
};
static const void* item=nullptr;
static unsigned ordinal=0;
static std::vector<GLuint> programs;
static char directory[1024]={};
using ProgramInfo=void(*)(GLuint,GLenum,GLint*);
using ActiveUniform=void(*)(GLuint,GLuint,GLsizei,GLsizei*,GLint*,GLenum*,char*);
using AttachedShaders=void(*)(GLuint,GLsizei,GLsizei*,GLuint*);
using ShaderInfo=void(*)(GLuint,GLenum,GLint*);
using ShaderSource=void(*)(GLuint,GLsizei,GLsizei*,char*);
static ProgramInfo programInfo=nullptr;
static ActiveUniform activeUniform=nullptr;
static AttachedShaders attachedShaders=nullptr;
static ShaderInfo shaderInfo=nullptr;
static ShaderSource shaderSource=nullptr;
static void note_item(const void* value){if(events){item=value;ordinal=0;}}
static void describe(GLuint program){
    if(std::find(programs.begin(),programs.end(),program)!=programs.end())return;
    if(programs.size()>=128)return;
    programs.push_back(program);
    char path[1200];std::snprintf(path,sizeof(path),"%s/caster-program-%u.txt",directory,program);
    FILE* file=fopen(path,"w");if(!file)return;
    GLint count=0;programInfo(program,0x8B86,&count); // GL_ACTIVE_UNIFORMS
    fprintf(file,"program=%u active_uniforms=%d\n",program,count);
    if(count<0 || count>4096){fclose(file);return;}
    for(GLint i=0;i<count;++i){
        char name[1024]={};GLsizei length=0;GLint size=0;GLenum type=0;
        activeUniform(program,(GLuint)i,sizeof(name)-1,&length,&size,&type,name);
        fprintf(file,"uniform type=0x%x size=%d name=%s\n",type,size,name);
    }
    GLuint shaders[16]={};GLsizei attached=0;
    attachedShaders(program,16,&attached,shaders);
    for(GLsizei i=0;i<attached;++i){
        GLint length=0,type=0;shaderInfo(shaders[i],0x8B84,&length); // source length
        shaderInfo(shaders[i],0x8B4F,&type); // shader type
        if(length<=0 || length>2*1024*1024){fprintf(file,"shader=%u source_unavailable length=%d\n",shaders[i],length);continue;}
        std::vector<char> source(size_t(length)+1,0);GLsizei written=0;
        shaderSource(shaders[i],length,&written,source.data());
        fprintf(file,"\n--- shader=%u type=0x%x bytes=%d ---\n",shaders[i],type,written);
        if(written>0)fwrite(source.data(),1,size_t(written),file);
        fputc('\n',file);
    }
    fclose(file);
}
static void observe(GLsizei count,bool indexed){
    if(timingFrame && g_localLightPassActive){++localDraws;if(partDepth>0)++coveredDraws;}
    if(!events || !gl::GetIntegerv)return;
    GLint program=0;gl::GetIntegerv(GL_CURRENT_PROGRAM,&program);
    if(program<=0)return;
    describe((GLuint)program);
    report_engine_contract("draw",activePart);
    const char* phase=g_localLightPassActive?"local":g_cascadeReplayActive?"sun":"native-or-other";
    fprintf(events,"%s,%d,%d,%p,%u,%d,%d,%d,%d\n",phase,
        g_localLightPassActive?g_localLightCurrentBucket:nwn_core::g_currentBucket,
        g_localLightPassActive?g_localObjectCaptureLayer:g_cascadeReplayLayer,
        item,ordinal++,int(g_currentDrawOwnerKind),program,count,int(indexed));
    GLint skinValue=-1;
    if(gl::GetUniformiv){const GLint skin=gl::GetUniformLocation((GLuint)program,"skinmesh");if(skin>=0){GLint value=0;gl::GetUniformiv((GLuint)program,skin,&value);skinValue=value;fprintf(events,"integer,skinmesh,%d\n",value);}}
    const GLint alpha=gl::GetUniformLocation((GLuint)program,"fAlphaDiscardValue");
    if(alpha>=0){GLfloat value=0;gl::GetUniformfv((GLuint)program,alpha,&value);fprintf(events,"scalar,fAlphaDiscardValue,%.9g\n",double(value));}
    if(activePart && g_currentDrawOwnerKind==DrawOwnerKind::NonCreature && skinValue==0 && alpha<0 && std::find(nominatedParts.begin(),nominatedParts.end(),activePart)==nominatedParts.end())nominatedParts.push_back(activePart);
    const char* names[]={"m_m","m_mv","m_mvp","m_view","m_proj","m_view_inv"};
    for(const char* name:names){
        const GLint location=gl::GetUniformLocation((GLuint)program,name);
        if(location<0)continue;
        GLfloat value[16]={};gl::GetUniformfv((GLuint)program,location,value);
        fprintf(events,"matrix,%s",name);
        for(float v:value)fprintf(events,",%.9g",double(v));
        fputc('\n',events);
    }
}
struct Scope {
    bool owns=false;
    explicit Scope(void* scene){
        if(events || scene!=g_areaScene || !g_shadowFrameContext.valid)return;
        FILE* marker=fopen("/tmp/nwn-caster-census.start","r");if(!marker)return;
        unsigned pid=0;int parsed=fscanf(marker,"%u",&pid);fclose(marker);
        if(parsed!=1 || pid!=(unsigned)getpid())return;
        std::remove("/tmp/nwn-caster-census.start");
        programInfo=(ProgramInfo)dlsym(RTLD_NEXT,"glGetProgramiv");
        activeUniform=(ActiveUniform)dlsym(RTLD_NEXT,"glGetActiveUniform");
        attachedShaders=(AttachedShaders)dlsym(RTLD_NEXT,"glGetAttachedShaders");
        shaderInfo=(ShaderInfo)dlsym(RTLD_NEXT,"glGetShaderiv");
        shaderSource=(ShaderSource)dlsym(RTLD_NEXT,"glGetShaderSource");
        if(!programInfo || !activeUniform || !attachedShaders || !shaderInfo || !shaderSource ||
           !gl::GetUniformLocation || !gl::GetUniformfv){fprintf(stderr,"[caster-census] unavailable read-only GL interface\n");return;}
        const char* dir=shadow_getenv("NWN_SHADOWMAP_OUT_DIR");
        if(!dir || !*dir){fprintf(stderr,"[caster-census] requires isolated OUT_DIR\n");return;}
        std::snprintf(directory,sizeof(directory),"%s",dir);
        char path[1200];std::snprintf(path,sizeof(path),"%s/caster-draws.csv",directory);
        events=fopen(path,"w");if(!events)return;
        engineModel=(EngineModel)dlsym(RTLD_DEFAULT,"_ZN15RenderInterface14GetModelMatrixEv");
        engineOpaque=(EnginePartPredicate)dlsym(RTLD_DEFAULT,"_ZN11PartTriMesh11GetIsOpaqueEv");
        engineRenderable=(EnginePartPredicate)dlsym(RTLD_DEFAULT,"_ZN11PartTriMesh16IsPartRenderableEv");
        fprintf(events,"engine_interfaces,%d,%d,%d\n",int(engineModel!=nullptr),int(engineOpaque!=nullptr),int(engineRenderable!=nullptr));
        programs.clear();nominatedParts.clear();item=nullptr;ordinal=0;owns=true;
        fprintf(events,"phase,bucket,layer,item,ordinal,owner_kind,program,count,indexed\n");
        fprintf(stderr,"[caster-census] START frame=%u (diagnostic frame is not a performance sample)\n",g_shadowFrameContext.serial);
    }
    ~Scope(){if(owns){fclose(events);events=nullptr;fprintf(stderr,"[caster-census] COMPLETE programs=%zu directory=%s\n",programs.size(),directory);}}
};
#else
template<class Call> static inline void submit(Call call){call();}
struct PartScope {explicit PartScope(void*){}};
struct TimingScope {explicit TimingScope(void*){}};
static inline void note_item(const void*){}
static inline void observe(GLsizei,bool){}
struct Scope {explicit Scope(void*){}};
#endif
}
