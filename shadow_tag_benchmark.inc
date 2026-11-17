// Matched opt-in A/B/A benchmark. No waits, quality changes or persistent settings.
namespace tag_work_benchmark {
#if defined(NWN_TAG_WORK_BENCH_DIAGNOSTIC) && !defined(_WIN32)
static constexpr int cheapSequence[]={0,1,0,2,0,3,0};
static constexpr int validationSequence[]={0,4,0,4,0};
static constexpr int gpuSequence[]={0,5,0,6,0,5,0};
static constexpr int leaseSequence[]={0,7,0,7,0};
static const int* sequence=cheapSequence;
static unsigned phaseCount=7;
static constexpr unsigned PhaseCount=7;
static const char* labels[]={"checkpoint","texture-fast-path","capability-cache","both","validation-upper-bound","tag-target-disabled","tag-writes-masked","buffer-selection-lease"};
struct Stats {
    std::vector<float> frame,scene,native,cpu;
    double work[tag_work::CostCount]={};unsigned draws=0,worlds=0;
    tag_buffer_lease::Counters lease;
};
static Stats stats[PhaseCount];
struct Record {GLuint ids[3]={};bool pending=false;unsigned phase=0;double cpu=0;tag_work::Frame work;tag_buffer_lease::Counters lease;};
static Record records[32];static Record* current=nullptr;
using Counter=void(*)(GLuint,GLenum);
using Result=void(*)(GLuint,GLenum,uint64_t*);
using Swap=void(*)(void*);
static Counter counter=nullptr;static Result result=nullptr;
static Swap realSwap=nullptr;static void** swapSlot=nullptr;static void* window=nullptr;
static void*(*focus)()=nullptr;
static int(*swapInterval)()=nullptr;
static bool initialized=false,running=false,inside=false;
static unsigned phase=0,cursor=0,lastSwapPhase=0;
static double phaseStart=0,lastSwap=0;
static ShadowFrameContext reference;
static bool receiver=false,fog=false,moving=false,fixed=false,film=false,puddles=false,rainParticles=false;
static float rainAmount=0,snowAmount=0;
static bool enabled(){static bool value=[](){const char* s=shadow_getenv("NWN_TAG_WORK_BENCH");return s && atoi(s)!=0;}();return value;}
static float percentile(std::vector<float> v,float fraction){if(v.empty())return 0;std::sort(v.begin(),v.end());return v[std::min(v.size()-1,size_t((v.size()-1)*fraction))];}
static void swap(void* target){
    realSwap(target);
    const double now=now_seconds();
    if(running && target==window){
        if(lastSwap>0 && lastSwapPhase==phase && now-phaseStart>=2 && lastSwap-phaseStart>=2)
            stats[phase].frame.push_back(float((now-lastSwap)*1000));
        lastSwap=now;lastSwapPhase=phase;
    }
}
static void shutdown(){tag_buffer_lease::shutdown();if(swapSlot && *swapSlot==(void*)swap)*swapSlot=(void*)realSwap;swapSlot=nullptr;}
static bool install_swap(){
    const auto* fn=(const unsigned char*)dlsym(RTLD_DEFAULT,"SDL_GL_SwapWindow");
    if(!fn || fn[0]!=0x55 || fn[1]!=0x48 || fn[2]!=0x89 || fn[3]!=0xe5 || fn[4]!=0x5d || fn[5]!=0xff || fn[6]!=0x25)return false;
    int32_t rel=0;std::memcpy(&rel,fn+7,4);void** slot=(void**)(fn+11+rel);
    // Refuse unknown or read-only slots; do not alter page protections.
    FILE* maps=fopen("/proc/self/maps","r");if(!maps)return false;
    char line[512];bool writable=false;
    while(fgets(line,sizeof(line),maps)){unsigned long a=0,b=0;char permissions[5]={};
        if(sscanf(line,"%lx-%lx %4s",&a,&b,permissions)==3 && (uintptr_t)slot>=a && (uintptr_t)slot+sizeof(void*)<=b){writable=permissions[1]=='w';break;}}
    fclose(maps);if(!writable || !*slot || *slot==(void*)swap)return false;
    realSwap=(Swap)*slot;swapSlot=slot;*slot=(void*)swap;
    fprintf(stderr,"[tag-work-bench] verified SDL swap slot; real frame intervals enabled\n");return true;
}
static bool initialize(){
    if(initialized)return true;
    if(!gl::GenQueries || !gl::GetQueryObjectuiv)return false;
    counter=(Counter)dlsym(RTLD_NEXT,"glQueryCounter");result=(Result)dlsym(RTLD_NEXT,"glGetQueryObjectui64v");
    focus=(void*(*)())dlsym(RTLD_DEFAULT,"SDL_GetKeyboardFocus");
    swapInterval=(int(*)())dlsym(RTLD_DEFAULT,"SDL_GL_GetSwapInterval");
    if(!counter || !result || !swapInterval || !install_swap())return false;
    for(auto& r:records){gl::GenQueries(3,r.ids);if(!r.ids[0] || !r.ids[1] || !r.ids[2]){shutdown();return false;}}
    initialized=true;return true;
}
static void poll(){for(auto& r:records)if(r.pending){
    GLuint ready=0;gl::GetQueryObjectuiv(r.ids[2],0x8867,&ready);if(!ready)continue;
    uint64_t a=0,b=0,c=0;result(r.ids[0],GL_QUERY_RESULT,&a);result(r.ids[1],GL_QUERY_RESULT,&b);result(r.ids[2],GL_QUERY_RESULT,&c);r.pending=false;
    if(b<a || c<b || c-a>1000000000ull)continue;
    auto& s=stats[r.phase];s.scene.push_back(float((c-a)/1e6));s.native.push_back(float((b-a)/1e6));s.cpu.push_back(float(r.cpu));
    for(int i=0;i<tag_work::CostCount;++i)s.work[i]+=r.work.ms[i]*16; // 1/16 draw sampling
    s.draws+=r.work.draws;++s.worlds;
    s.lease.acquired+=r.lease.acquired;s.lease.reused+=r.lease.reused;
    s.lease.released+=r.lease.released;s.lease.gaps+=r.lease.gaps;
    for(unsigned i=0;i<9;++i)s.lease.fences[i]+=r.lease.fences[i];
}}
static void report(unsigned p){const auto& s=stats[p];
    fprintf(stderr,"[tag-work-bench] phase=%u mode=%s frames=%zu frame_median_ms=%.4f frame_p95_ms=%.4f scene_gpu_ms=%.4f native_gpu_ms=%.4f scene_cpu_ms=%.4f\n",p,labels[sequence[p]],s.frame.size(),percentile(s.frame,.5f),percentile(s.frame,.95f),percentile(s.scene,.5f),percentile(s.native,.5f),percentile(s.cpu,.5f));
    if(s.worlds){fprintf(stderr,"[tag-work-bench] sampled_cpu_estimate_ms phase=%u draws_per_world=%.1f",p,double(s.draws)/s.worlds);
      for(int i=0;i<tag_work::CostCount;++i) {
          fprintf(stderr," %s=%.4f",tag_work::names[i],s.work[i]/s.worlds);
      }
      fprintf(stderr,"\n");
      if(s.lease.acquired || s.lease.gaps){
          fprintf(stderr,"[tag-work-bench] lease phase=%u acquired_per_world=%.2f reused_per_world=%.2f released_per_world=%.2f gaps=%u fences_per_world=",p,double(s.lease.acquired)/s.worlds,double(s.lease.reused)/s.worlds,double(s.lease.released)/s.worlds,s.lease.gaps);
          for(unsigned i=0;i<9;++i)fprintf(stderr,"%s%.2f",i?",":"",double(s.lease.fences[i])/s.worlds);
          fprintf(stderr,"\n");
      }
    }
}
static void stop(const char* reason){running=false;tag_work::fastTextures=tag_work::fastCapability=tag_work::skipRepeatedValidation=tag_work::suppressDrawTarget=tag_work::suppressTagWrites=false;tag_work::active=tag_work::sampled=false;tag_buffer_lease::requested=false;tag_buffer_lease::release(tag_buffer_lease::Fence::Boundary);
    fprintf(stderr,"[tag-work-bench] %s; checkpoint candidates restored\n",reason);
}
static void apply(){int mode=sequence[phase];tag_work::fastTextures=mode==1 || mode==3;tag_work::fastCapability=mode==2 || mode==3;tag_work::skipRepeatedValidation=mode==4;tag_work::suppressDrawTarget=mode==5;tag_work::suppressTagWrites=mode==6;tag_buffer_lease::requested=mode==7;lastSwap=0;
    fprintf(stderr,"[tag-work-bench] begin phase=%u mode=%s\n",phase,labels[mode]);
}
static bool same_view(const ShadowFrameContext& f){
    if(f.scene!=reference.scene || std::memcmp(f.viewport,reference.viewport,sizeof(f.viewport))!=0)return false;
    for(int i=0;i<16;++i)if(std::fabs(f.viewInverse[i]-reference.viewInverse[i])>(i>=12 && i<=14?.001f:.0001f) || std::fabs(f.projection[i]-reference.projection[i])>.00001f)return false;
    return receiver==g_receiverEnabled && fog==g_godraysEnabled && moving==g_dynamicCastersEnabled && fixed==g_staticCastersEnabled && film==g_wetFilmEnabled && puddles==g_rainPuddlesEnabled && rainParticles==g_shaderRainEnabled && std::fabs(rainAmount-weather_runtime::g.rainAmount)<.001f && std::fabs(snowAmount-weather_runtime::g.snowAmount)<.001f;
}
static void update(double now,const ShadowFrameContext& frame){
    poll();
    if(!running){
        // One-shot diagnostic visual preview control; never compiled into normal builds.
        if(FILE* preview=fopen("/tmp/nwn-tag-work-preview","r")){
            unsigned pid=0,on=0;int parsed=fscanf(preview,"%u %u",&pid,&on);fclose(preview);
            if(parsed==2 && pid==(unsigned)getpid()){
                std::remove("/tmp/nwn-tag-work-preview");
                bool ready=!on || tag_buffer_lease::install();
                tag_buffer_lease::requested=on && ready;
                if(!tag_buffer_lease::requested)tag_buffer_lease::release(tag_buffer_lease::Fence::Boundary);
                fprintf(stderr,"[tag-work-bench] visual preview %s\n",tag_buffer_lease::requested?"candidate ON":"checkpoint ON");
            }
        }
        FILE* f=fopen("/tmp/nwn-tag-work-benchmark.start","r");if(!f)return;
        unsigned pid=0,variant=0;int parsed=fscanf(f,"%u %u",&pid,&variant);fclose(f);
        if(parsed<1 || pid!=(unsigned)getpid() || variant>3)return;
        std::remove("/tmp/nwn-tag-work-benchmark.start");
        if(variant==3 && !tag_buffer_lease::install()){
            fprintf(stderr,"[tag-work-bench] refused: buffer-lease ownership coverage incomplete\n");return;
        }
        if(swapInterval()!=0){fprintf(stderr,"[tag-work-bench] refused: VSync must be OFF\n");return;}
        auto getWindow=(void*(*)())dlsym(RTLD_DEFAULT,"SDL_GL_GetCurrentWindow");window=getWindow?getWindow():nullptr;
        if(!window || (focus && !focus())){fprintf(stderr,"[tag-work-bench] refused: game must be focused\n");return;}
        for(auto& s:stats){
            s=Stats{};
            s.frame.reserve(4096);s.scene.reserve(4096);s.native.reserve(4096);s.cpu.reserve(4096);
        }
        for(auto& r:records)r.pending=false;
        reference=frame;receiver=g_receiverEnabled;fog=g_godraysEnabled;moving=g_dynamicCastersEnabled;fixed=g_staticCastersEnabled;film=g_wetFilmEnabled;puddles=g_rainPuddlesEnabled;rainParticles=g_shaderRainEnabled;rainAmount=weather_runtime::g.rainAmount;snowAmount=weather_runtime::g.snowAmount;
        sequence=variant==3?leaseSequence:(variant==2?gpuSequence:(variant==1?validationSequence:cheapSequence));phaseCount=(variant==1 || variant==3)?5:7;
        phase=0;phaseStart=now;running=true;apply();
        fprintf(stderr,"[tag-work-bench] START pid=%u viewport=%dx%d receiver=%d fog=%d moving=%d fixed=%d rain=%.3f; %u 8-second phases\n",pid,reference.viewport[2],reference.viewport[3],receiver,fog,moving,fixed,rainAmount,phaseCount);
        return;
    }
    bool same=same_view(frame),focused=!focus || focus()!=nullptr;
    if(!same || swapInterval()!=0 || !focused || tag_buffer_lease::failed()){
        float viewDelta=0,projectionDelta=0;int vi=0,pi=0;
        for(int i=0;i<16;++i){if(std::fabs(frame.viewInverse[i]-reference.viewInverse[i])>viewDelta){viewDelta=std::fabs(frame.viewInverse[i]-reference.viewInverse[i]);vi=i;}if(std::fabs(frame.projection[i]-reference.projection[i])>projectionDelta){projectionDelta=std::fabs(frame.projection[i]-reference.projection[i]);pi=i;}}
        fprintf(stderr,"[tag-work-bench] guard same=%d focus=%d swap=%d scene=%p/%p viewport=%d,%d,%d,%d/%d,%d,%d,%d view_delta=%g[%d] projection_delta=%g[%d] flags=%d%d%d%d%d%d%d/%d%d%d%d%d%d%d rain=%g/%g snow=%g/%g\n",same,focused,swapInterval(),frame.scene,reference.scene,frame.viewport[0],frame.viewport[1],frame.viewport[2],frame.viewport[3],reference.viewport[0],reference.viewport[1],reference.viewport[2],reference.viewport[3],viewDelta,vi,projectionDelta,pi,receiver,fog,moving,fixed,film,puddles,rainParticles,int(g_receiverEnabled),int(g_godraysEnabled),int(g_dynamicCastersEnabled),int(g_staticCastersEnabled),int(g_wetFilmEnabled),int(g_rainPuddlesEnabled),int(g_shaderRainEnabled),rainAmount,weather_runtime::g.rainAmount,snowAmount,weather_runtime::g.snowAmount);
        stop("ABORT: view, settings, weather or focus changed");return;
    }
    if(now-phaseStart>=8){
        if(stats[phase].frame.size()<30 || stats[phase].scene.size()<30){stop("ABORT: insufficient frame samples");return;}
        report(phase);
        if(++phase==phaseCount){
            fprintf(stderr,"[tag-work-bench] SUMMARY BEGIN actual tag framebuffer samples=%d\n",g_sunTagSamples);
            for(unsigned p=0;p<phaseCount;++p)report(p);
            fprintf(stderr,"[tag-work-bench] SUMMARY END\n");stop("complete");return;
        }
        phaseStart=now;apply();
    }
}
struct Scope {
    Record* record=nullptr;bool owns=false,world=false,nativeEnded=false;double start=0;
    ShadowFrameContext frame;
    explicit Scope(void* scene){
        if(!enabled() || inside || !g_glBound || !g_shadowFrameContext.valid || scene!=g_areaScene || scene!=g_shadowFrameContext.scene || g_shadowFrameContext.viewport[2]<g_maxVpWidth*9/10 || !initialize())return;
        frame=g_shadowFrameContext;owns=true;inside=true;
        // Secondary views must not drive the controller. Eligibility is known
        // only after native rendering, when mark_world receives the draw census.
        if(!running || now_seconds()-phaseStart<2)return;
        auto& r=records[cursor++%32];if(r.pending)return;
        r.phase=phase;record=current=&r;tag_work::frame=tag_work::Frame{};
        tag_work::active=true;tag_work::sampled=false;tag_buffer_lease::reset_frame();start=now_seconds();counter(r.ids[0],0x8E28);
    }
    void mark_world(bool value){world=value;}
    void native_end(){if(record && !nativeEnded){counter(record->ids[1],0x8E28);record->lease=tag_buffer_lease::end_native();nativeEnded=true;}}
    ~Scope(){
        if(record){
            native_end();counter(record->ids[2],0x8E28);record->cpu=(now_seconds()-start)*1000;
            record->work=tag_work::frame;record->pending=world;current=nullptr;
            tag_work::active=tag_work::sampled=false;
        }
        if(owns){if(world)update(now_seconds(),frame);inside=false;}
    }
};
#else
struct Scope {explicit Scope(void*){} void mark_world(bool){} void native_end(){}};
static void shutdown(){}
#endif
} // namespace tag_work_benchmark
