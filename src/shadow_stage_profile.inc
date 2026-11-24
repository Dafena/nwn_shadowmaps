// Opt-in, non-blocking GPU timestamps. No rendering state or preferences changed.
namespace shadow_stage_profile {
#ifndef _WIN32
enum Stage { Scene, Native, Receiver, Depth, Colour, Composite, Fog, Count };
static const char* names[]={"scene-total","native-scene-including-hooks","receiver-total","depth-copy","colour-copy","composite-draw","fog"};
using Counter=void(*)(GLuint,GLenum);
using Result=void(*)(GLuint,GLenum,uint64_t*);
struct Record { unsigned frame=0, nativeDraws=0, replayDraws=0, cache[7]={}; double interval=0; bool moving=false; GLuint ids[Count*2]={}; bool pending=false,used[Count]={}; double cpu[Count]={}; };
static Record ring[32];static unsigned cursor=0;
static Record* current=nullptr;
static bool finalReceiver=false;
static Counter counter=nullptr;static Result result=nullptr;
static bool initialized=false,running=false;
static bool restoreBatch=false;
static bool validationComparison=false;
#if defined(NWN_TAG_WORK_BENCH_DIAGNOSTIC)
static bool restoreValidation=false, restoreScalarCache=false;
#endif
static bool valid_mode(unsigned mode){
#if defined(NWN_TAG_WORK_BENCH_DIAGNOSTIC)
    return mode==3 || (mode>=7 && mode<=9) || mode==12;
#else
    return mode==3 || mode==9;
#endif
}
static void restore_tagging(){
    g_shadowStageSkipTags=false;
    g_sunTagBatchAttachments=restoreBatch;
#if defined(NWN_TAG_WORK_BENCH_DIAGNOSTIC)
    if(validationComparison) tag_work::skipRepeatedValidation=restoreValidation;
    weather_runtime::cacheWeatherScalars=restoreScalarCache;
    weather_runtime::countWeatherBinds=false;
    g_materialReceiverAuditActive=false;
    stencil_receiver_audit::active=false;
    release_material_sun_snapshot();
#endif
    weather_runtime::configure_weather_multi_bind(false);
}
static double start=0;
static ShadowFrameContext reference, previous;
static bool motion=false;
static unsigned frameNumber=0, dropped=0;
static double previousStart=0;
struct MotionRow {
    Record frame;
    double gpu[Count]={};
};
static std::vector<MotionRow> motionRows;
static void snapshot_cache(unsigned (&cache)[7]) {
    cache[0]=g_cascadeCacheHits;
    cache[1]=g_cascadeCacheRefits;
    cache[2]=g_cascadeRefitMove;
    cache[3]=g_cascadeRefitTurn;
    cache[4]=g_cascadeRefitSun;
    cache[5]=g_cascadeRefitScene;
    cache[6]=g_cascadeRefitDisabled;
}
static std::vector<float> gpu[Count],cpu[Count];
#if defined(NWN_TAG_WORK_BENCH_DIAGNOSTIC)
static std::vector<float> drawCpu[draw_cpu_probe::Count];
static std::vector<float> pathCpu[draw_cpu_probe::PathCount][draw_cpu_probe::LocalReplay];
static std::vector<float> tagCpu[tag_work::CostCount];
#endif
static bool enabled(){static bool value=[](){const char* s=shadow_getenv("NWN_SHADOW_STAGE_PROFILE");return s && atoi(s)!=0;}();return value;}
static float median(std::vector<float> v){if(v.empty())return 0;std::sort(v.begin(),v.end());return v[v.size()/2];}
static void poll(){for(auto& r:ring)if(r.pending){GLuint ready=0;gl::GetQueryObjectuiv(r.ids[Scene*2+1],0x8867,&ready);if(!ready)continue;r.pending=false;
 MotionRow row{};
 if(motion) row.frame=r;
 for(int i=0;i<Count;++i)if(r.used[i]){uint64_t a=0,b=0;result(r.ids[i*2],GL_QUERY_RESULT,&a);result(r.ids[i*2+1],GL_QUERY_RESULT,&b);if(b>=a && b-a<1000000000ull){gpu[i].push_back(float((b-a)/1e6));cpu[i].push_back(float(r.cpu[i]));row.gpu[i]=(b-a)/1e6;}}
 if(motion) motionRows.push_back(row);
}}
static void report(){
if(weather_runtime::weatherMultiBind)
    fprintf(stderr,"[weather-multi-bind] calls=%u\n",weather_runtime::weatherMultiBindCalls);
#if defined(NWN_TAG_WORK_BENCH_DIAGNOSTIC)
if(stencil_receiver_audit::active){
    for (const auto& row:stencil_receiver_audit::observations)
        fprintf(stderr,"[stencil-receiver-audit] %s fbo=%d type=0x%x name=%d bits=%d samples=%d observations=%u read_fbo=%d depth_type=0x%x depth_name=%d depth_bits=%d\n",
                row.composite ? "composite" : "geometry",row.fbo,unsigned(row.type),
                row.name,row.bits,row.samples,row.count,row.readFbo,
                unsigned(row.depthType),row.depthName,row.depthBits);
    for (const auto& row:stencil_receiver_audit::creatureDraws)
        fprintf(stderr,"[creature-transparent-audit] bucket=%d program=%d depth_write=%d blend=%d a2c=%d layer_pass=%d mode2=%d static=0x%x filtered_sun=0x%x filtered_local=%d draws=%u\n",
                row.bucket,row.program,row.depthWrite,row.blend,row.coverage,row.layerPass,row.mode2,
                row.staticMask,row.filteredSunMask,int(row.filteredLocal),row.count);
    for (const auto& row:stencil_receiver_audit::boundaries)
        fprintf(stderr,"[receiver-bucket-boundary] bucket=%d draw_fbo=%d samples=%d static_layers=0x%x dynamic_layers=0x%x filtered_sun=0x%x filtered_local=%d\n",
                row.bucket,row.drawFbo,row.samples,row.staticMask,row.dynamicMask,row.filteredSunMask,int(row.localReady));
    fprintf(stderr,"[stencil-receiver-audit] frame=%u attachment observation only\n",stencil_receiver_audit::serial);
}
if(g_materialReceiverAuditActive){
    const auto& snapshot=g_materialReceiverAudit;
    fprintf(stderr,"[material-sun-snapshot] copy_supported=%d copies=%u ready_draws=%u mean_submit_ms=%.4f\n",
            int(g_materialCopyImage!=nullptr),snapshot.snapshotCopies,snapshot.snapshotReadyDraws,
            snapshot.snapshotCopies ? snapshot.snapshotCpuMs/snapshot.snapshotCopies : 0.0);
    const auto& audit=g_materialReceiverAudit;
    fprintf(stderr,"[material-receiver-audit] bucket2_tagged_creature_draws=%u highlighted=%u blended=%u filtered_sun_allocated=%u sun_layer_touched_this_frame=%u completed_filtered_local=%u\n",
            audit.creatures,audit.highlighted,audit.blended,audit.filteredSun,
            audit.sunLayersTouchedThisFrame,audit.completedLocal);
}
if(weather_runtime::countWeatherBinds){
    constexpr const char* labels[]={"snow-trails","reflection-color","reflection-depth","snow-lights","shelter"};
    static_assert(sizeof(labels)/sizeof(labels[0]) == static_cast<unsigned>(weather_runtime::WeatherBinding::Count));
    for(unsigned i=0;i<sizeof(labels)/sizeof(labels[0]);++i){
        const auto& counts=weather_runtime::weatherBindCounts[i];
        fprintf(stderr,"[weather-bind-count] %s attempts=%u already_bound=%u\n",
                labels[i],counts.attempts,counts.alreadyBound);
    }
    fprintf(stderr,"[weather-bind-count] GL queries enabled for counting; timings are not comparable\n");
}
#endif
if(motion){
 unsigned outstanding=0;
 for(const auto& r:ring) if(r.pending) ++outstanding;
 fprintf(stderr,"[shadow-motion] dropped=%u pending=%u\n",dropped,outstanding);
 fprintf(stderr,"[shadow-motion] frame,moving,previous_frame_interval_ms,native_draws,sun_replay_draws,cache_hits,refits,move,turn,sun,scene,disabled");
 for(const auto* name:names) fprintf(stderr,",%s_cpu_ms,%s_gpu_ms",name,name);
 fputc('\n',stderr);
 for(const auto& row:motionRows){
    const auto& r=row.frame;
    fprintf(stderr,"[shadow-motion] %u,%d,%.4f,%u,%u",r.frame,int(r.moving),r.interval,r.nativeDraws,r.replayDraws);
    for(auto count:r.cache) fprintf(stderr,",%u",count);
    for(int i=0;i<Count;++i)
        if(r.used[i]) fprintf(stderr,",%.4f,%.4f",r.cpu[i],row.gpu[i]);
        else fputs(",,",stderr);
    fputc('\n',stderr);
 }
}
fprintf(stderr,"[shadow-stage-profile] live local_cast=%d local_receive=%d\n",int(g_localLightEmittersCast),int(g_localLightReceiver));
#if defined(NWN_TAG_WORK_BENCH_DIAGNOSTIC)
const char* groups[]={"weather-observer","layer-before-draw","alpha-shadow-begin","alpha-shadow-end","trace-draw-total","observer-before","tag-begin","finish-draw-total","observer-after","tag-end","weather-program","weather-area-state","weather-uniforms-and-bindings","weather-reflection-inputs","weather-texture-bindings","local-replay","sun-replay","shelter-capture","local-prepare","local-dynamic-duplicate","local-filtered-duplicate","static-world-total","sun-engine-bucket","local-engine-bucket","world-engine-bucket","sun-state-save","local-state-save","sun-layer-setup","local-layer-setup","sun-restore","local-restore","sun-draw-wrapper","local-draw-wrapper","sun-gl-submit","local-gl-submit"};
static_assert(sizeof(groups)/sizeof(groups[0]) == draw_cpu_probe::Count);
const char* paths[]={"native","sun","local"};
for(unsigned path=0;path<draw_cpu_probe::PathCount;++path)
    for(unsigned i=0;i<draw_cpu_probe::LocalReplay;++i)
        fprintf(stderr,"[shadow-stage-profile] draw_path_cpu %s %s samples=%zu estimated_median_ms=%.4f\n",
                paths[path],groups[i],pathCpu[path][i].size(),median(pathCpu[path][i]));

for(unsigned i=0;i<tag_work::CostCount;++i)
    fprintf(stderr,"[shadow-stage-profile] tag_cpu %s samples=%zu estimated_median_ms=%.4f\n",
            tag_work::names[i],tagCpu[i].size(),median(tagCpu[i]));
for(unsigned i=0;i<draw_cpu_probe::Count;++i)fprintf(stderr,"[shadow-stage-profile] sampled_draw_cpu %s samples=%zu median_ms=%.4f\n",groups[i],drawCpu[i].size(),median(drawCpu[i]));
#endif
fprintf(stderr,"[shadow-stage-profile] SUMMARY BEGIN (overlapping intervals; do not sum totals)\n");for(int i=0;i<Count;++i)fprintf(stderr,"[shadow-stage-profile] %s samples=%zu gpu_ms=%.4f cpu_ms=%.4f\n",names[i],gpu[i].size(),median(gpu[i]),median(cpu[i]));fprintf(stderr,"[shadow-stage-profile] SUMMARY END\n");}
struct Timer {
 Record* record=nullptr;Stage stage;double begin=0;
 explicit Timer(Stage s):stage(s){if(!current || current->used[s] || (s>=Depth && s<=Composite && !finalReceiver))return;record=current;record->used[s]=true;begin=now_seconds();counter(record->ids[s*2],0x8E28);}
 void end(){if(!record)return;counter(record->ids[stage*2+1],0x8E28);record->cpu[stage]=(now_seconds()-begin)*1000;record=nullptr;}
 ~Timer(){end();}
};
struct Scope {
 bool owns=false,main=false,ownsTagProbe=false;Record* record=nullptr;double begin=0;
 explicit Scope(void* scene){
  if(!enabled() || current || !g_glBound || scene!=g_areaScene || !g_shadowFrameContext.valid || g_shadowFrameContext.scene!=scene || g_shadowFrameContext.viewport[2]<g_maxVpWidth*9/10)return;
  if(!initialized){if(!gl::GenQueries || !gl::GetQueryObjectuiv)return;counter=(Counter)dlsym(RTLD_NEXT,"glQueryCounter");result=(Result)dlsym(RTLD_NEXT,"glGetQueryObjectui64v");if(!counter || !result)return;for(auto& r:ring)gl::GenQueries(Count*2,r.ids);initialized=true;}
  poll();double now=now_seconds();
  if(!running){FILE* f=fopen("/tmp/nwn-shadow-stage-profile.start","r");if(!f)return;unsigned pid=0,mode=0;int parsed=fscanf(f,"%u %u",&pid,&mode);fclose(f);if(parsed<1 || pid!=(unsigned)getpid() || !valid_mode(mode))return;std::remove("/tmp/nwn-shadow-stage-profile.start");for(int i=0;i<Count;++i){gpu[i].clear();cpu[i].clear();}
#if defined(NWN_TAG_WORK_BENCH_DIAGNOSTIC)
for(auto& values:drawCpu)values.clear();
for(auto& path:pathCpu)for(auto& values:path)values.clear();
for(auto& values:tagCpu)values.clear();
#endif
for(auto& r:ring){r.pending=false;}
validationComparison=mode==5 || mode==6;
weather_runtime::configure_weather_multi_bind(mode==9);
#if defined(NWN_TAG_WORK_BENCH_DIAGNOSTIC)
restoreScalarCache=weather_runtime::cacheWeatherScalars;
weather_runtime::cacheWeatherScalars=mode==7;
weather_runtime::countWeatherBinds=mode==8;
stencil_receiver_audit::active=mode==12;
stencil_receiver_audit::serial=0;
stencil_receiver_audit::observations.clear();
stencil_receiver_audit::boundaries.clear();
stencil_receiver_audit::creatureDraws.clear();
g_materialReceiverAuditActive=mode==10 || mode==11;
if(mode==11) configure_material_sun_audit();
else g_materialCopyImage=nullptr;
g_materialReceiverAudit={};
for(auto& counts:weather_runtime::weatherBindCounts) counts={};
if(validationComparison){
    restoreValidation=tag_work::skipRepeatedValidation;
    tag_work::skipRepeatedValidation=mode==6;
}
#endif
motion=mode>=3;motionRows.clear();frameNumber=dropped=0;previousStart=0;
reference=previous=g_shadowFrameContext;start=now;running=true;restoreBatch=g_sunTagBatchAttachments;g_shadowStageSkipTags=mode==1 || mode==4;if(!motion || validationComparison)g_sunTagBatchAttachments=mode==2 || validationComparison;fprintf(stderr,"[shadow-stage-profile] tag-mode=%u (0=legacy attachments, 1=bypass, 2=batch attachments, 3=motion baseline, 4=motion without tags, 5=validation baseline, 6=reuse validation, 7=cache weather scalars, 8=count weather binds, 9=batch weather binds, 10=material receiver audit, 11=sun snapshot audit, 12=stencil attachment audit)\n",mode);GLint samples=0;gl::GetIntegerv(GL_SAMPLES,&samples);fprintf(stderr,"[shadow-stage-profile] MSAA=%d film=%d puddles=%d privateRain=%d\n",samples,int(g_wetFilmEnabled),int(g_rainPuddlesEnabled),int(g_shaderRainEnabled));fprintf(stderr,"[shadow-stage-profile] START pid=%u viewport=%dx%d receiver=%d fog=%d moving=%d fixed=%d; 15 seconds\n",pid,reference.viewport[2],reference.viewport[3],int(g_receiverEnabled),int(g_godraysEnabled),int(g_dynamicCastersEnabled),int(g_staticCastersEnabled));}
  bool same=reference.scene==g_shadowFrameContext.scene && std::memcmp(reference.viewport,g_shadowFrameContext.viewport,sizeof(reference.viewport))==0;
  bool moved=false;
  const auto& pose=motion && !validationComparison ? previous : reference;
  for(int i=0;i<16;++i){
    if(std::fabs(pose.viewInverse[i]-g_shadowFrameContext.viewInverse[i])>
        (i>=12 && i<=14 ? .001f:.0001f)) moved=true;
    if(std::fabs(reference.projection[i]-g_shadowFrameContext.projection[i])>.00001f) same=false;
  }
  if((!motion || validationComparison) && moved) same=false;
  previous=g_shadowFrameContext;
  if(!same){running=false;restore_tagging();fprintf(stderr,"[shadow-stage-profile] ABORT capture conditions changed; no complete comparison\n");return;}
  if(now-start>=15){running=false;report();restore_tagging();fprintf(stderr,"[shadow-stage-profile] configured tagging restored\n");return;}
  if(now-start<2)return;
  const double interval=previousStart ? (now-previousStart)*1000 : 0;
  previousStart=now;
  ++frameNumber;
  auto& r=ring[cursor++%32];
  if(r.pending){++dropped;return;}
  if(motion){
    r.frame=frameNumber;
    r.interval=interval;
    r.moving=moved;
    snapshot_cache(r.cache);
  }
  std::fill(r.used,r.used+Count,false);r.used[Scene]=true;current=record=&r;owns=true;begin=now;
#if defined(NWN_TAG_WORK_BENCH_DIAGNOSTIC)
  std::fill(draw_cpu_probe::ms,draw_cpu_probe::ms+draw_cpu_probe::Count,0.0);for(auto& path:draw_cpu_probe::pathMs)std::fill(std::begin(path),std::end(path),0.0);
  draw_cpu_probe::active=true;
  // Keep the independent tag benchmark in sole control when requested.
  const char* tagBenchmark=shadow_getenv("NWN_TAG_WORK_BENCH");
  if(motion && !validationComparison && !tag_work::active && !(tagBenchmark && atoi(tagBenchmark))){
    tag_work::frame={};
    tag_work::sampled=false;
    tag_work::active=true;
    ownsTagProbe=true;
  }
#endif
  counter(r.ids[Scene*2],0x8E28);
 }
 void mark_world(bool value){
    main=value;
    if(owns && motion && main){
        record->nativeDraws=g_frameDrawCalls;
        record->replayDraws=g_replayDrawsFrame;
    }
 }
 ~Scope(){if(!owns)return;
#if defined(NWN_TAG_WORK_BENCH_DIAGNOSTIC)
  draw_cpu_probe::active=false;
  if(main)for(unsigned path=0;path<draw_cpu_probe::PathCount;++path)
    for(unsigned i=0;i<draw_cpu_probe::LocalReplay;++i)
        pathCpu[path][i].push_back(float(draw_cpu_probe::pathMs[path][i]));
  if(ownsTagProbe){
    tag_work::active=tag_work::sampled=false;
    if(main)for(unsigned i=0;i<tag_work::CostCount;++i)
        tagCpu[i].push_back(float(tag_work::frame.ms[i]*16.0));
  }
  if(main)for(unsigned i=0;i<draw_cpu_probe::Count;++i)drawCpu[i].push_back(float(draw_cpu_probe::ms[i]));
#endif
  if(motion){
    unsigned after[7];
    snapshot_cache(after);
    for(unsigned i=0;i<7;++i)record->cache[i]=after[i]-record->cache[i];
  }
  counter(record->ids[Scene*2+1],0x8E28);record->cpu[Scene]=(now_seconds()-begin)*1000;record->pending=main;current=nullptr;}
};
#else
enum Stage { Scene, Native, Receiver, Depth, Colour, Composite, Fog, Count };
static bool finalReceiver=false;
struct Timer {explicit Timer(Stage){} void end(){}};
struct Scope {explicit Scope(void*){} void mark_world(bool){}};
#endif
} // namespace shadow_stage_profile
