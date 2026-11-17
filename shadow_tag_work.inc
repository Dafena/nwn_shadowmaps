// Internal A/B candidates and sampled CPU attribution; disabled on normal launches.
namespace tag_work {
enum Cost { Begin, Basics, Capability, Textures, DrawBufferRead, Attachment,
            DrawBufferEnable, Validation, Classification, Restore, CostCount };
#if defined(NWN_TAG_WORK_BENCH_DIAGNOSTIC) && !defined(_WIN32)
static const char* names[] = {"begin-total", "basic-state", "capability", "textures",
    "draw-buffer-read", "attachment", "draw-buffer-enable", "validation", "classification", "restore"};
struct Frame { double ms[CostCount]={}; unsigned draws=0; };
static Frame frame;
static bool active=false, sampled=false;
static unsigned sampleCursor=0;
static bool fastTextures=false, fastCapability=false;
// Diagnostic upper bound only: does not prove native attachment mutations are tracked.
static bool skipRepeatedValidation=false;
static bool suppressDrawTarget=false,suppressTagWrites=false;
struct Timer {
    Cost cost; bool measure; double start;
    explicit Timer(Cost c):cost(c),measure(active && sampled),start(measure?now_seconds():0) {}
    void end(){if(measure){frame.ms[cost]+=(now_seconds()-start)*1000;measure=false;}}
    ~Timer(){end();}
};
struct BeginDraw {
    double start=0;
    BeginDraw(){if(active){++frame.draws;sampled=(sampleCursor++%16)==0;if(sampled)start=now_seconds();}}
    ~BeginDraw(){if(active && sampled)frame.ms[Begin]+=(now_seconds()-start)*1000;}
};
#else
static constexpr bool fastTextures=false,fastCapability=false,skipRepeatedValidation=false;
static constexpr bool suppressDrawTarget=false,suppressTagWrites=false;
struct Timer { explicit Timer(Cost){} void end(){} };
struct BeginDraw { BeginDraw(){} };
#endif
} // namespace tag_work
