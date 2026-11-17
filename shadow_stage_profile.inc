// Opt-in, non-blocking GPU timestamps. No rendering state or preferences changed.
namespace shadow_stage_profile {
#ifndef _WIN32
enum Stage { Scene, Native, Receiver, Depth, Colour, Composite, Fog, Count };
static const char* names[]={"scene-total","native-scene-including-hooks","receiver-total","depth-copy","colour-copy","composite-draw","fog"};
using Counter=void(*)(GLuint,GLenum);
using Result=void(*)(GLuint,GLenum,uint64_t*);
struct Record { GLuint ids[Count*2]={}; bool pending=false,used[Count]={}; double cpu[Count]={}; };
static Record ring[32];static unsigned cursor=0;
static Record* current=nullptr;
static bool finalReceiver=false;
static Counter counter=nullptr;static Result result=nullptr;
static bool initialized=false,running=false;
static bool restoreBatch=false;
static double start=0;
static ShadowFrameContext reference;
static std::vector<float> gpu[Count],cpu[Count];
static bool enabled(){static bool value=[](){const char* s=shadow_getenv("NWN_SHADOW_STAGE_PROFILE");return s && atoi(s)!=0;}();return value;}
static float median(std::vector<float> v){if(v.empty())return 0;std::sort(v.begin(),v.end());return v[v.size()/2];}
static void poll(){for(auto& r:ring)if(r.pending){GLuint ready=0;gl::GetQueryObjectuiv(r.ids[Scene*2+1],0x8867,&ready);if(!ready)continue;r.pending=false;
 for(int i=0;i<Count;++i)if(r.used[i]){uint64_t a=0,b=0;result(r.ids[i*2],GL_QUERY_RESULT,&a);result(r.ids[i*2+1],GL_QUERY_RESULT,&b);if(b>=a && b-a<1000000000ull){gpu[i].push_back(float((b-a)/1e6));cpu[i].push_back(float(r.cpu[i]));}}
}}
static void report(){fprintf(stderr,"[shadow-stage-profile] SUMMARY BEGIN (overlapping intervals; do not sum totals)\n");for(int i=0;i<Count;++i)fprintf(stderr,"[shadow-stage-profile] %s samples=%zu gpu_ms=%.4f cpu_ms=%.4f\n",names[i],gpu[i].size(),median(gpu[i]),median(cpu[i]));fprintf(stderr,"[shadow-stage-profile] SUMMARY END\n");}
struct Timer {
 Record* record=nullptr;Stage stage;double begin=0;
 explicit Timer(Stage s):stage(s){if(!current || current->used[s] || (s>=Depth && s<=Composite && !finalReceiver))return;record=current;record->used[s]=true;begin=now_seconds();counter(record->ids[s*2],0x8E28);}
 void end(){if(!record)return;counter(record->ids[stage*2+1],0x8E28);record->cpu[stage]=(now_seconds()-begin)*1000;record=nullptr;}
 ~Timer(){end();}
};
struct Scope {
 bool owns=false,main=false;Record* record=nullptr;double begin=0;
 explicit Scope(void* scene){
  if(!enabled() || current || !g_glBound || scene!=g_areaScene || !g_shadowFrameContext.valid || g_shadowFrameContext.scene!=scene || g_shadowFrameContext.viewport[2]<g_maxVpWidth*9/10)return;
  if(!initialized){if(!gl::GenQueries || !gl::GetQueryObjectuiv)return;counter=(Counter)dlsym(RTLD_NEXT,"glQueryCounter");result=(Result)dlsym(RTLD_NEXT,"glGetQueryObjectui64v");if(!counter || !result)return;for(auto& r:ring)gl::GenQueries(Count*2,r.ids);initialized=true;}
  poll();double now=now_seconds();
  if(!running){FILE* f=fopen("/tmp/nwn-shadow-stage-profile.start","r");if(!f)return;unsigned pid=0,mode=0;int parsed=fscanf(f,"%u %u",&pid,&mode);fclose(f);if(parsed<1 || pid!=(unsigned)getpid() || mode>2)return;std::remove("/tmp/nwn-shadow-stage-profile.start");for(int i=0;i<Count;++i){gpu[i].clear();cpu[i].clear();}for(auto& r:ring)r.pending=false;reference=g_shadowFrameContext;start=now;running=true;restoreBatch=g_sunTagBatchAttachments;g_shadowStageSkipTags=mode==1;g_sunTagBatchAttachments=mode==2;fprintf(stderr,"[shadow-stage-profile] tag-mode=%u (0=legacy attachments, 1=bypass, 2=batch attachments)\n",mode);GLint samples=0;gl::GetIntegerv(GL_SAMPLES,&samples);fprintf(stderr,"[shadow-stage-profile] MSAA=%d film=%d puddles=%d privateRain=%d\n",samples,int(g_wetFilmEnabled),int(g_rainPuddlesEnabled),int(g_shaderRainEnabled));fprintf(stderr,"[shadow-stage-profile] START pid=%u viewport=%dx%d receiver=%d fog=%d moving=%d fixed=%d; 15 seconds\n",pid,reference.viewport[2],reference.viewport[3],int(g_receiverEnabled),int(g_godraysEnabled),int(g_dynamicCastersEnabled),int(g_staticCastersEnabled));}
  bool same=reference.scene==g_shadowFrameContext.scene && std::memcmp(reference.viewport,g_shadowFrameContext.viewport,sizeof(reference.viewport))==0;
  for(int i=0;i<16;++i)if(std::fabs(reference.viewInverse[i]-g_shadowFrameContext.viewInverse[i])>(i>=12 && i<=14 ? .001f:.0001f) || std::fabs(reference.projection[i]-g_shadowFrameContext.projection[i])>.00001f)same=false;
  if(!same){running=false;g_shadowStageSkipTags=false;g_sunTagBatchAttachments=restoreBatch;fprintf(stderr,"[shadow-stage-profile] ABORT camera changed; no complete comparison\n");return;}
  if(now-start>=15){running=false;report();g_shadowStageSkipTags=false;g_sunTagBatchAttachments=restoreBatch;fprintf(stderr,"[shadow-stage-profile] configured tagging restored\n");return;}
  if(now-start<2)return;
  auto& r=ring[cursor++%32];if(r.pending)return;std::fill(r.used,r.used+Count,false);r.used[Scene]=true;current=record=&r;owns=true;begin=now;counter(r.ids[Scene*2],0x8E28);
 }
 void mark_world(bool value){main=value;}
 ~Scope(){if(!owns)return;counter(record->ids[Scene*2+1],0x8E28);record->cpu[Scene]=(now_seconds()-begin)*1000;record->pending=main;current=nullptr;}
};
#else
enum Stage { Scene, Native, Receiver, Depth, Colour, Composite, Fog, Count };
static bool finalReceiver=false;
struct Timer {explicit Timer(Stage){} void end(){}};
struct Scope {explicit Scope(void*){} void mark_world(bool){}};
#endif
} // namespace shadow_stage_profile
