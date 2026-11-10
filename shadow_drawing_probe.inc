// Opt-in gate 1: observe the existing game token channel and raw mouse events.
// No credentials are logged and no input is swallowed. Optional local ink uses
// only the dedicated eom_draw_pad texture; this is not a shared world board.
#include "drawing_probe.h"
#include "drawing_input_journal.h"
#include "drawing_preview.h"
#include "drawing_texture_upload.h"
#include "drawing_world_canvas.h"
#include <atomic>
#include <chrono>
#include <mutex>
static bool drawing_probe_enabled() {
    static const bool enabled=[] { const char* v=std::getenv("NWN_DRAW_PROBE");
        return v && std::strcmp(v,"1")==0; }();
    return enabled;
}
using DrawingSetToken=void (*)(void*,int,const void*);
static DrawingSetToken g_drawingSetToken=nullptr;
static subhook_t g_drawingTokenHook=nullptr;
static drawing_probe::InputJournal g_drawingProbeJournal;
static std::mutex g_drawingProbeJournalMutex;
static drawing_probe::Preview g_drawingPreview;
static bool g_drawingProbeMovementLimitReported=false;
static std::atomic<bool> g_drawingProbeArmed{false};
static std::atomic<unsigned long long> g_drawingProbeOpenedUs{0};
static unsigned g_drawingPreviewTexture=0;
static std::uintptr_t g_drawingPreviewContext=0;
static bool g_drawingPreviewBottomUp=false;
static std::uint64_t g_drawingPreviewTextureGeneration=0;
static unsigned g_drawingWorldTexture=0;
static std::uintptr_t g_drawingWorldContext=0;
static std::uint64_t g_drawingWorldGeneration=0;
unsigned nwn_drawing_probe_texture_for_context(std::uintptr_t context) {
    if(!drawing_probe_enabled() || !g_drawingProbeArmed || !context)return 0;
    std::lock_guard<std::mutex> guard(g_drawingProbeJournalMutex);
    return context==g_drawingPreviewContext?g_drawingPreviewTexture:0;
}
void nwn_drawing_probe_texture(unsigned texture,std::uintptr_t context,int orientation) {
    std::lock_guard<std::mutex> guard(g_drawingProbeJournalMutex);
    if(orientation>=0){
        g_drawingPreviewTexture=texture;g_drawingPreviewContext=context;
        g_drawingPreviewBottomUp=orientation!=0;++g_drawingPreviewTextureGeneration;
        std::fprintf(stderr,"[drawing-probe] dedicated image signature captured row_order=%s\n",
            orientation?"bottom-up":"top-down");std::fflush(stderr);
    }else if(texture==g_drawingPreviewTexture && context==g_drawingPreviewContext){
        g_drawingPreviewTexture=0;++g_drawingPreviewTextureGeneration;
    }
    if(orientation<0 && texture==g_drawingWorldTexture && context==g_drawingWorldContext){
        g_drawingWorldTexture=0;++g_drawingWorldGeneration;
    }
}
static void drawing_probe_note_world_material(void* material) {
    if(!drawing_probe_enabled() || !material || !eng::MaterialGetTexture ||
       !eng::AurTextureGetName || !eng::DrawingTextureId)return;
    void* resource=eng::MaterialGetTexture(material,5);
    if(!resource)return;
    const char* name=eng::AurTextureGetName(resource);
    if(!name || std::strncmp(name,"eom_draw_world",15)!=0)return;
    const unsigned texture=eng::DrawingTextureId(resource);
    const auto context=nwn_drawing_probe_context();
    if(!texture || !context)return;
    std::lock_guard<std::mutex> guard(g_drawingProbeJournalMutex);
    if(texture!=g_drawingWorldTexture || context!=g_drawingWorldContext){
        g_drawingWorldTexture=texture;g_drawingWorldContext=context;++g_drawingWorldGeneration;
        std::fprintf(stderr,"[drawing-probe] dedicated world emission texture bound\n");std::fflush(stderr);
    }
}
bool nwn_drawing_probe_active() {return drawing_probe_enabled();}
static unsigned long long drawing_probe_time_us() {
    return static_cast<unsigned long long>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
void nwn_drawing_probe_geometry(float x,float y,float w,float h,bool bottomUp) {
    std::lock_guard<std::mutex> guard(g_drawingProbeJournalMutex);
    g_drawingPreview.image({x,y,w,h,bottomUp},drawing_probe_time_us());
    if(g_drawingPreviewBottomUp!=bottomUp){g_drawingPreviewBottomUp=bottomUp;++g_drawingPreviewTextureGeneration;}
    static std::array<float,4> previous{};
    const std::array<float,4> next{x,y,w,h};
    if(previous!=next){
        previous=next;
        std::fprintf(stderr,"[drawing-probe] image_quad x=%.2f y=%.2f w=%.2f h=%.2f bottom_up=%d\n",
                     x,y,w,h,int(bottomUp));std::fflush(stderr);
    }
}

void nwn_drawing_probe_frame() {
    if(!drawing_probe_enabled())return;
    if(g_drawingProbeArmed && drawing_probe_time_us()-g_drawingProbeOpenedUs>=600000000ULL) {
        g_drawingProbeArmed=false;
        {std::lock_guard<std::mutex> guard(g_drawingProbeJournalMutex);g_drawingPreview.close();}
        std::fprintf(stderr,"[drawing-probe] recording expired\n");
        std::fflush(stderr);
    }
    // Use the engine's scale result, never infer scale from a screenshot or DPI.
    // Windows 89.8193.37-17 export is a static float getter (movss; ret).
    // This optional Linux binding has not had native Linux runtime validation.
    if(g_drawingProbeArmed && eng::DrawingGuiScale) {
        static float previousScale=-1;
        const float scale=eng::DrawingGuiScale();
        {std::lock_guard<std::mutex> guard(g_drawingProbeJournalMutex);g_drawingPreview.scale(scale);}
        if(std::isfinite(scale) && scale>0 && scale<=16 && std::fabs(scale-previousScale)>0.0001f) {
            previousScale=scale;
            std::fprintf(stderr,"[drawing-probe] gui_scale=%.4f t_us=%llu\n",scale,drawing_probe_time_us());
            std::fflush(stderr);
        }
    }
#ifdef _WIN32
    // The settings panel is not a prerequisite for observing board gestures.
    nwn_win_install_input_capture();
#else
    using MouseState=uint32_t(*)(int*,int*);
    static auto poll=reinterpret_cast<MouseState>(dlsym(RTLD_DEFAULT,"SDL_GetMouseState"));
    if(!poll || !g_drawingProbeArmed)return;
    static int previousX=-1,previousY=-1;static uint32_t previousButtons=0;
    int x=0,y=0;const uint32_t buttons=poll(&x,&y);
    if(x!=previousX || y!=previousY)nwn_drawing_probe_input(0,float(x),float(y));
    if((buttons^previousButtons)&1)nwn_drawing_probe_input((buttons&1)?1:2,float(x),float(y));
    previousX=x;previousY=y;previousButtons=buttons;
#endif
}

void nwn_drawing_probe_input(int kind,float x,float y) {
    if(!drawing_probe_enabled() || !g_drawingProbeArmed || !std::isfinite(x) || !std::isfinite(y)) return;
    const auto time=drawing_probe_time_us();
    std::lock_guard<std::mutex> guard(g_drawingProbeJournalMutex);
    if(g_drawingProbeJournal.record(kind,time)) {
        g_drawingPreview.input(kind,{x,y},time);
        std::fprintf(stderr,"[drawing-probe] pointer kind=%d x=%.2f y=%.2f t_us=%llu\n",kind,x,y,time);
        // Flush each transition and about ten times/second during a drag. Hover
        // motion writes nothing, so it cannot consume the gesture recording.
        if(kind!=0 || g_drawingProbeJournal.movements%12==0)std::fflush(stderr);
    }
    if(g_drawingProbeJournal.movements>=drawing_probe::InputJournal::movement_limit &&
       !g_drawingProbeMovementLimitReported) {
        g_drawingProbeMovementLimitReported=true;
        g_drawingPreview.pause();
        std::fprintf(stderr,"[drawing-probe] movement limit reached; controls still recorded; preview paused until Reset\n");
        std::fflush(stderr);
    }
    if(g_drawingProbeJournal.controls>=drawing_probe::InputJournal::control_limit) {
        g_drawingProbeArmed=false;
        g_drawingPreview.close();
        std::fprintf(stderr,"[drawing-probe] control limit reached; recording stopped\n");
        std::fflush(stderr);
    }
}

static void drawing_set_token_detour(void* table,int number,const void* text) {
    // The original setter always runs, including for unrecognized/invalid tokens.
    // Use a real trampoline only; never unhook/re-hook a potentially concurrent call.
    g_drawingSetToken(table,number,text);
    if(number!=drawing_probe::token_number || !text) return;
    // CExoString's pointer/uint32 layout is documented in Anvil's native binding.
    const char* value=nullptr; uint32_t size=0;
    std::memcpy(&value,text,sizeof(value));
    std::memcpy(&size,static_cast<const char*>(text)+sizeof(value),sizeof(size));
    if(!value || !size || size>257) return;
    char bounded[258]{}; std::memcpy(bounded,value,size); bounded[size]='\0';
    drawing_probe::Message message;
    if(!drawing_probe::parse(bounded,message)) return;
    {
        std::lock_guard<std::mutex> guard(g_drawingProbeJournalMutex);
        if(!g_drawingPreview.native(message,drawing_probe_time_us()))return;
    }
    if(message.kind=='O'){
        std::lock_guard<std::mutex> guard(g_drawingProbeJournalMutex);
        g_drawingProbeJournal={};g_drawingProbeMovementLimitReported=false;
        g_drawingProbeOpenedUs=drawing_probe_time_us();g_drawingProbeArmed=true;
        if(!eng::DrawingGuiScale)std::fprintf(stderr,"[drawing-probe] GUI scale binding unavailable\n");
    }
    if(message.kind=='C')g_drawingProbeArmed=false;
    std::fprintf(stderr,"[drawing-probe] native kind=%c seq=%u x=%.2f y=%.2f w=%.2f h=%.2f t_us=%llu (credential redacted)\n",
        message.kind,message.sequence,message.x,message.y,message.w,message.h,drawing_probe_time_us());
    std::fflush(stderr);
}

// NUI's Nuklear/stbi loader bypasses Aurora's named texture cache. Admit only
// the dedicated asset's exact pixel signature observed on a native GL upload.
// Invalidate on deletion/reallocation; never retain ownership of its GL object.
static const drawing_probe::UploadApi& drawing_probe_upload_api() {
    static drawing_probe::UploadApi api{
        gl::GetIntegerv,gl::ActiveTexture,gl::BindTexture,
        reinterpret_cast<unsigned char(*)(unsigned)>(dlsym(RTLD_DEFAULT,"glIsTexture")),
        gl::GetTexLevelParameteriv,gl::GetTexParameteriv,
        reinterpret_cast<void(*)(unsigned,int)>(dlsym(RTLD_DEFAULT,"glPixelStorei")),
        reinterpret_cast<void(*)(unsigned,unsigned)>(dlsym(RTLD_DEFAULT,"glBindBuffer")),
        gl::TexSubImage2D,
        reinterpret_cast<void(*)(unsigned)>(dlsym(RTLD_DEFAULT,"glGenerateMipmap"))};
    return api;
}

static std::vector<unsigned char> drawing_probe_read_world_base(const drawing_probe::UploadApi& api,unsigned texture) {
    if(!api.get || !api.active || !api.bind || !api.isTexture || !api.level ||
       !api.pixelStore || !api.bindBuffer || !gl::GetTexImage || !api.isTexture(texture))return {};
    int active=0,bound=0;api.get(0x84E0,&active);api.active(0x84C0);api.get(0x8069,&bound);
    api.bind(0x0DE1,texture);
    int width=0,height=0,format=0,compressed=0;
    api.level(0x0DE1,0,0x1000,&width);api.level(0x0DE1,0,0x1001,&height);
    api.level(0x0DE1,0,0x1003,&format);api.level(0x0DE1,0,0x86A1,&compressed);
    if(width!=2048 || height!=2048 || compressed ||
       (format!=0x8058 && format!=0x1908 && format!=0x8051 && format!=0x1907)){
        api.bind(0x0DE1,unsigned(bound));api.active(unsigned(active));return {};
    }
    std::vector<unsigned char> data(2048*2048*4);
    int pbo=0,alignment=0,row=0,skipRows=0,skipPixels=0;
    api.get(0x88ED,&pbo);api.get(0x0D05,&alignment);api.get(0x0D02,&row);
    api.get(0x0D03,&skipRows);api.get(0x0D04,&skipPixels);
    api.bindBuffer(0x88EB,0);api.pixelStore(0x0D05,1);api.pixelStore(0x0D02,0);
    api.pixelStore(0x0D03,0);api.pixelStore(0x0D04,0);
    gl::GetTexImage(0x0DE1,0,0x1908,0x1401,data.data());
    api.pixelStore(0x0D05,alignment);api.pixelStore(0x0D02,row);
    api.pixelStore(0x0D03,skipRows);api.pixelStore(0x0D04,skipPixels);
    api.bindBuffer(0x88EB,unsigned(pbo));api.bind(0x0DE1,unsigned(bound));api.active(unsigned(active));
    return data;
}

static void drawing_probe_upload_world() {
    unsigned texture=0,strokes=0;std::uint64_t generation=0,revision=0;
    const auto context=nwn_drawing_probe_context();
    {
        std::lock_guard<std::mutex> guard(g_drawingProbeJournalMutex);
        if(!context || context!=g_drawingWorldContext || !g_drawingWorldTexture || !g_drawingPreview.revision)return;
        texture=g_drawingWorldTexture;generation=g_drawingWorldGeneration;revision=g_drawingPreview.revision;
    }
    static std::vector<unsigned char> base;
    static std::uint64_t baseGeneration=~std::uint64_t(0),uploaded=~std::uint64_t(0),lastAttempt=0;
    static int orientation=-1;
    const auto now=drawing_probe_time_us();if(now-lastAttempt<33333)return;
    lastAttempt=now;
    const auto& api=drawing_probe_upload_api();
    if(baseGeneration!=generation){
        auto candidate=drawing_probe_read_world_base(api,texture);
        orientation=candidate.empty()?-1:drawing_probe::world_marker(candidate.data());
        if(orientation<0){
            static unsigned failures=0;
            if(failures++<3){std::fprintf(stderr,"[drawing-probe] world base signature/format unavailable; no write\n");std::fflush(stderr);}
            return;
        }
        base=std::move(candidate);baseGeneration=generation;uploaded=~std::uint64_t(0);
        std::fprintf(stderr,"[drawing-probe] world base verified; sketch-only composite ready\n");std::fflush(stderr);
    }
    if(uploaded==revision)return;
    std::vector<unsigned char> ink;
    {
        std::lock_guard<std::mutex> guard(g_drawingProbeJournalMutex);
        if(generation!=g_drawingWorldGeneration || texture!=g_drawingWorldTexture || context!=g_drawingWorldContext)return;
        revision=g_drawingPreview.revision;ink=g_drawingPreview.raster(false,true);
        strokes=g_drawingPreview.stroke_count();
    }
    auto composed=drawing_probe::world_composite(base,ink,orientation!=0,true);
    if(!composed.empty() && drawing_probe::upload(api,texture,composed.data(),2048)){
        uploaded=revision;
        static unsigned previous=~0u;
        if(previous!=strokes){previous=strokes;
            std::fprintf(stderr,"[drawing-probe] world_canvas uploaded strokes=%u\n",strokes);std::fflush(stderr);}
    }
}

static void drawing_probe_upload_preview() {
    if(!drawing_probe_enabled())return;
    // Keep the finished drawing visible in-world after the NUI closes.
    drawing_probe_upload_world();
    if(!g_drawingProbeArmed)return;
    const auto& api=drawing_probe_upload_api();
    static unsigned lastTexture=0;
    static std::uint64_t uploaded=~std::uint64_t(0),lastAttempt=0,lastGeneration=0;
    const auto now=drawing_probe_time_us();
    if(now-lastAttempt<33333)return;
    lastAttempt=now;
    const auto context=nwn_drawing_probe_context();
    unsigned texture=0;std::uint64_t generation=0;
    std::vector<unsigned char> pixels;
    std::uint64_t revision=0;
    unsigned strokes=0,rejected=0;bool calibrated=false,full=false;
    {
        std::lock_guard<std::mutex> guard(g_drawingProbeJournalMutex);
        texture=g_drawingPreviewTexture;generation=g_drawingPreviewTextureGeneration;
        if(!texture || !context || context!=g_drawingPreviewContext || !g_drawingPreview.active ||
           (uploaded==g_drawingPreview.revision && lastTexture==texture && lastGeneration==generation))return;
        revision=g_drawingPreview.revision;pixels=g_drawingPreview.raster(g_drawingPreviewBottomUp);
        strokes=g_drawingPreview.stroke_count();rejected=g_drawingPreview.rejected;
        calibrated=g_drawingPreview.calibrated;full=g_drawingPreview.full;
    }
    if(drawing_probe::upload(api,texture,pixels.data())) {
        const bool first=lastTexture!=texture;
        uploaded=revision;lastTexture=texture;lastGeneration=generation;
        static unsigned previousStrokes=~0u;
        if(first || previousStrokes!=strokes){
            previousStrokes=strokes;
            std::fprintf(stderr,"[drawing-probe] local_preview uploaded strokes=%u calibrated=%d full=%d rejected=%u\n",
                strokes,int(calibrated),int(full),rejected);std::fflush(stderr);
        }
    }else{
        static unsigned failures=0;
        if(failures++<3){std::fprintf(stderr,"[drawing-probe] local_preview texture validation unavailable; no write\n");std::fflush(stderr);}
    }
}

static void install_drawing_probe() {
    if(!drawing_probe_enabled() || !eng::DrawingSetCustomToken) return;
    nwn_drawing_probe_install_texture_observer();
    auto hook=subhook_new(eng::DrawingSetCustomToken,
        reinterpret_cast<void*>(drawing_set_token_detour),SUBHOOK_64BIT_OFFSET);
    void* trampoline=hook?subhook_get_trampoline(hook):nullptr;
#ifdef _WIN32
    bool privateTrampoline=false;
    if(hook && !trampoline) {
        trampoline=nwn_win_drawing_token_trampoline(eng::DrawingSetCustomToken);
        privateTrampoline=trampoline!=nullptr;
    }
#endif
    if(!hook || !trampoline) {
        if(hook)subhook_free(hook);
        std::fprintf(stderr,"[drawing-probe] unavailable: no safe token trampoline\n");return;
    }
    g_drawingSetToken=reinterpret_cast<DrawingSetToken>(trampoline);
    if(subhook_install(hook)!=0){
#ifdef _WIN32
        if(privateTrampoline)VirtualFree(trampoline,0,MEM_RELEASE);
#endif
        subhook_free(hook);g_drawingSetToken=nullptr;return;
    }
    g_drawingTokenHook=hook;
    std::fprintf(stderr,"[drawing-probe] token observation installed; diagnostic only\n");
}
