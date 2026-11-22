// Verified Linux native gateways; unsupported prefixes keep original fallback.
namespace native_hook_gateway {
#if defined(__linux__) && defined(__x86_64__)
#if defined(NWN_TAG_WORK_BENCH_DIAGNOSTIC)
static bool enabled=false;
#else
static bool enabled=true;
#endif
static verified_gateway::Gateway part,nextItem;
#if defined(NWN_TAG_WORK_BENCH_DIAGNOSTIC)
struct Counters {uint64_t parts=0,items=0;};
static thread_local Counters counts;
static thread_local bool collecting=false;
static void count_part(){if(collecting)++counts.parts;}
static void count_next(){if(collecting)++counts.items;}
#else
static inline void count_part(){}
static inline void count_next(){}
#endif
static void prepare_part(void* source){
    void* predicate=dlsym(RTLD_DEFAULT,"_ZN11PartTriMesh16IsPartRenderableEv");
    fprintf(stderr,"[native-gateway] Part draw prefix/callee %s\n",verified_gateway::make_part(part,source,predicate)?"verified":"unsupported; original fallback");
}
static void prepare_next(void* source){fprintf(stderr,"[native-gateway] Iterator prefix %s\n",verified_gateway::make_next(nextItem,source)?"verified":"unsupported; original fallback");}
static inline bool ready(){return part.code && nextItem.code;}
static bool call_part(void* value){if(!enabled || !part.code)return false;count_part();((void(*)(void*))part.code)(value);return true;}
static bool call_next(void* self,int bucket,int order,const void*& result){if(!enabled || !nextItem.code)return false;count_next();result=((const void*(*)(void*,int,int))nextItem.code)(self,bucket,order);return true;}
#if defined(NWN_TAG_WORK_BENCH_DIAGNOSTIC)
static void reset_frame(){counts=Counters{};collecting=true;}
static Counters end_native(){collecting=false;return counts;}
#endif
static void shutdown(){enabled=false;
#if defined(NWN_TAG_WORK_BENCH_DIAGNOSTIC)
collecting=false;
#endif
part.reset();nextItem.reset();}
#else
static inline void prepare_part(void*){}
static inline void prepare_next(void*){}
static inline bool call_part(void*){return false;}
static inline bool call_next(void*,int,int,const void*&){return false;}
static inline void shutdown(){}
#endif
}
