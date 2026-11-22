// Native-scene diagnostic, opt-in via NWN_WET_FILM_PROFILE=1. No GL waits.
// A PID in /tmp/nwn-wet-film-profile.start starts a matched ablation sequence.
namespace wet_film_profile {
#ifndef _WIN32
static constexpr int ablations[]={0,1,0,2,0,3,0,4,0,5,0};
static constexpr int candidates[]={0,1,0,6,0};
static constexpr unsigned phases=sizeof(ablations)/sizeof(ablations[0]);
static const int* sequence=ablations;
static unsigned phaseCount=phases;
static constexpr unsigned capacity=120;
static const char* labels[]={"detailed","no-shelter","no-noise","no-impacts","base-tint","film-off","shelter-equivalent"};
struct Query { GLuint start=0,end=0; bool pending=false; unsigned phase=0; double cpu=0; };
static Query queries[32];
static float gpu[phases][capacity]={},cpu[phases][capacity]={};
static unsigned count[phases]={},positions[phases]={};
static unsigned phase=0,cursor=0,pollFrames=0;
static bool initialized=false,failed=false,running=false,inside=false,paused=false;
static ShadowFrameContext referenceFrame;
static const void* referenceArea=nullptr;
static double phaseStart=0,lastFrame=0;
static uint32_t cameraKey=0;
using CounterFn=void(*)(GLuint,GLenum);
using Result64Fn=void(*)(GLuint,GLenum,uint64_t*);
static CounterFn counter=nullptr;
static Result64Fn result64=nullptr;
static void*(*keyboardFocus)()=nullptr;
static bool initialize() {
    if(initialized)return true;
    if(failed || !gl::GenQueries || !gl::GetQueryObjectuiv)return false;
    counter=(CounterFn)dlsym(RTLD_NEXT,"glQueryCounter");
    result64=(Result64Fn)dlsym(RTLD_NEXT,"glGetQueryObjectui64v");
    keyboardFocus=(void*(*)())dlsym(RTLD_DEFAULT,"SDL_GetKeyboardFocus");
    if(!counter || !result64){failed=true;return false;}
    if(g_wetFilmCandidateProfile){sequence=candidates;phaseCount=sizeof(candidates)/sizeof(candidates[0]);}
    GLuint ids[64]={};gl::GenQueries(64,ids);
    for(unsigned i=0;i<32;++i){queries[i].start=ids[i*2];queries[i].end=ids[i*2+1];if(!queries[i].start || !queries[i].end){failed=true;return false;}}
    initialized=true;return true;
}
static uint32_t key(const ShadowFrameContext& frame) {
    return frame.viewHash ^ (frame.projectionHash*16777619u) ^
        (uint32_t)(uintptr_t)frame.scene ^ (uint32_t)g_weatherShelterSize*65537u ^
        (uint32_t)g_staticWorldSize*31u;
}
static bool same_view(const ShadowFrameContext& frame) {
    if(frame.scene!=referenceFrame.scene || weather_runtime::g.weatherArea!=referenceArea ||
       std::memcmp(frame.viewport,referenceFrame.viewport,sizeof(frame.viewport))!=0)return false;
    for(int i=0;i<16;++i) {
        const float tolerance=i>=12 && i<=14 ? 0.001f : 0.0001f;
        if(std::fabs(frame.viewInverse[i]-referenceFrame.viewInverse[i])>tolerance ||
           std::fabs(frame.projection[i]-referenceFrame.projection[i])>0.00001f)return false;
    }
    return true;
}
static bool focused(){return !keyboardFocus || keyboardFocus()!=nullptr;}
static void poll() {
    for(Query& q:queries)if(q.pending){
        GLuint ready=0;gl::GetQueryObjectuiv(q.end,0x8867 /* RESULT_AVAILABLE */,&ready);
        if(!ready)continue;
        uint64_t first=0,last=0;result64(q.start,GL_QUERY_RESULT,&first);result64(q.end,GL_QUERY_RESULT,&last);q.pending=false;
        if(q.phase>=phases || last<first || last-first>1000000000ull)continue;
        unsigned i=positions[q.phase]++%capacity;
        gpu[q.phase][i]=(float)((last-first)/1000000.0);cpu[q.phase][i]=(float)q.cpu;
        count[q.phase]=std::min(capacity,count[q.phase]+1);
    }
}
static float median(const float* data,unsigned n) {
    if(!n)return 0;
    std::vector<float> copy(data,data+n);std::sort(copy.begin(),copy.end());
    return n%2 ? copy[n/2] : (copy[n/2-1]+copy[n/2])*0.5f;
}
static void report(unsigned p) {
    fprintf(stderr,"[wet-film-profile] phase=%u mode=%s samples=%u gpu_scene_ms=%.4f cpu_scene_ms=%.4f\n",
            p,labels[sequence[p]],count[p],median(gpu[p],count[p]),median(cpu[p],count[p]));
}
static void stop(const char* reason) {
    running=false;paused=false;g_wetFilmProbeMode=0;
    for(Query& q:queries)q.pending=false;
    fprintf(stderr,"[wet-film-profile] %s; detailed restored\n",reason);
}
static void update(double now,const ShadowFrameContext& frame) {
    poll();
    if(running){
        if(!focused()) {
            if(!paused)fprintf(stderr,"[wet-film-profile] paused: waiting for game focus; detailed restored\n");
            paused=true;g_wetFilmProbeMode=0;count[phase]=positions[phase]=0;
            for(Query& q:queries)if(q.phase==phase)q.pending=false;
            lastFrame=now;return;
        }
        if(paused) {
            paused=false;phaseStart=now;g_wetFilmProbeMode=sequence[phase];
            fprintf(stderr,"[wet-film-profile] resumed phase=%u mode=%s\n",phase,labels[sequence[phase]]);
        }
        if(!same_view(frame) || !g_wetFilmEnabled || g_rainPuddlesEnabled || weather_runtime::g.rainAmount<0.99f) {
            fprintf(stderr,"[wet-film-profile] invalid view=%d film=%d puddles=%d rain=%.3f old_key=%08x new_key=%08x\n",
                    same_view(frame)?0:1,(int)g_wetFilmEnabled,(int)g_rainPuddlesEnabled,weather_runtime::g.rainAmount,cameraKey,key(frame));
            stop("aborted: scene, camera, weather or settings changed");
        }
        else if(now-lastFrame>0.5 && count[phase]>0) {
            count[phase]=positions[phase]=0;phaseStart=now;
            for(Query& q:queries)if(q.phase==phase)q.pending=false;
            fprintf(stderr,"[wet-film-profile] restarting phase=%u after render-cadence interruption\n",phase);
        }
        else if(now-phaseStart>=6.0){
            if(count[phase]<30){stop("aborted: insufficient steady samples");lastFrame=now;return;}
            report(phase);++phase;
            if(phase==phaseCount){
                fprintf(stderr,"[wet-film-profile] SUMMARY BEGIN\n");for(unsigned p=0;p<phaseCount;++p)report(p);
                fprintf(stderr,"[wet-film-profile] SUMMARY END\n");stop("complete");
            } else {g_wetFilmProbeMode=sequence[phase];phaseStart=now;}
        }
    }
    lastFrame=now;
    if(running || (++pollFrames%30)!=0 || !focused())return;
    FILE* file=fopen("/tmp/nwn-wet-film-profile.start","r");if(!file)return;
    unsigned requested=0;const int parsed=fscanf(file,"%u",&requested);fclose(file);
    if(parsed!=1 || requested!=(unsigned)getpid())return;
    if(!g_wetFilmEnabled || g_rainPuddlesEnabled || weather_runtime::g.rainAmount<0.99f ||
       !weather_runtime::g.precipitationOcclusionValid)return;
    std::remove("/tmp/nwn-wet-film-profile.start");
    std::fill(count,count+phases,0u);std::fill(positions,positions+phases,0u);
    for(Query& q:queries)q.pending=false;
    phase=0;phaseStart=now;cameraKey=key(frame);referenceFrame=frame;referenceArea=weather_runtime::g.weatherArea;
    g_wetFilmProbeMode=0;running=true;paused=false;
    fprintf(stderr,"[wet-film-profile] start pid=%u camera=%08x shelter=%d sun=%d viewport=%dx%d; %u phases, 6 seconds each\n",
            (unsigned)getpid(),cameraKey,weather_runtime::g.precipitationOcclusionSize,g_staticWorldSize,
            frame.viewport[2],frame.viewport[3],phaseCount);
}
struct Scope {
    Query* query=nullptr;bool owns=false,world=false;double start=0;
    ShadowFrameContext frame;
    explicit Scope(void* scene) {
        if(!g_wetFilmProfiling || inside || !g_glBound || !g_shadowFrameContext.valid ||
           scene!=g_areaScene || g_shadowFrameContext.scene!=scene ||
           g_shadowFrameContext.viewport[2]<g_maxVpWidth*9/10 || !initialize())return;
        frame=g_shadowFrameContext;inside=true;owns=true;const double now=now_seconds();
        if(!running || paused || now-phaseStart<1.5)return;
        Query& q=queries[cursor++%32];if(q.pending)return;
        q.phase=phase;query=&q;start=now_seconds();counter(q.start,0x8E28 /* TIMESTAMP */);
    }
    void mark_world(bool value){world=value;}
    ~Scope(){
        if(query){
            counter(query->end,0x8E28);
            if(world){query->cpu=(now_seconds()-start)*1000.0;query->pending=true;}
        }
        if(owns){if(world)update(now_seconds(),frame);inside=false;}
    }
};
#else
struct Scope { explicit Scope(void*) {} void mark_world(bool) {} };
#endif
} // namespace wet_film_profile
