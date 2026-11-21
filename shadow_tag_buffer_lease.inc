#ifndef _WIN32
#include <tuple>
#include <type_traits>
extern "C" {
__attribute__((visibility("protected"))) void glClear(GLbitfield);
__attribute__((visibility("protected"))) void glDrawBuffer(GLenum);
__attribute__((visibility("protected"))) void glGetIntegerv(GLenum,GLint*);
__attribute__((visibility("protected"))) void glGetFloatv(GLenum,GLfloat*);
__attribute__((visibility("protected"))) void glGetBooleanv(GLenum,GLboolean*);
__attribute__((visibility("protected"))) void glGetDoublev(GLenum,double*);
}
namespace tag_buffer_lease {
// Dispatch changes install only after complete Linux boundary verification.
// Each optional alias retains its own original pointer; no alias is forwarded
// through an assumed-equivalent implementation.
#define TAG_BUFFER_HOOKS(X) \
 X(Clear,nullptr,"glClear",Clear,true,void(GLbitfield)) \
 X(DrawBuffer,nullptr,"glDrawBuffer",Selection,true,void(GLenum)) \
 X(GetInt,nullptr,"glGetIntegerv",Query,true,void(GLenum,GLint*)) \
 X(GetFloat,nullptr,"glGetFloatv",Query,true,void(GLenum,GLfloat*)) \
 X(GetBool,nullptr,"glGetBooleanv",Query,true,void(GLenum,GLboolean*)) \
 X(GetDouble,nullptr,"glGetDoublev",Query,true,void(GLenum,double*)) \
 X(Bind,"__glewBindFramebuffer","glBindFramebuffer",Bind,true,void(GLenum,GLuint)) \
 X(BindEXT,"__glewBindFramebufferEXT","glBindFramebufferEXT",Bind,false,void(GLenum,GLuint)) \
 X(Buffers,"__glewDrawBuffers","glDrawBuffers",Selection,true,void(GLsizei,const GLenum*)) \
 X(BuffersARB,"__glewDrawBuffersARB","glDrawBuffersARB",Selection,false,void(GLsizei,const GLenum*)) \
 X(BuffersATI,"__glewDrawBuffersATI","glDrawBuffersATI",Selection,false,void(GLsizei,const GLenum*)) \
 X(BuffersEXT,"__glewDrawBuffersEXT","glDrawBuffersEXT",Selection,false,void(GLsizei,const GLenum*)) \
 X(BuffersNV,"__glewDrawBuffersNV","glDrawBuffersNV",Selection,false,void(GLsizei,const GLenum*)) \
 X(Blit,"__glewBlitFramebuffer","glBlitFramebuffer",Blit,true,void(GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLbitfield,GLenum)) \
 X(BlitEXT,"__glewBlitFramebufferEXT","glBlitFramebufferEXT",Blit,false,void(GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLbitfield,GLenum)) \
 X(BlitNV,"__glewBlitFramebufferNV","glBlitFramebufferNV",Blit,false,void(GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLbitfield,GLenum)) \
 X(Check,"__glewCheckFramebufferStatus","glCheckFramebufferStatus",QueryAll,true,GLenum(GLenum)) \
 X(CheckEXT,"__glewCheckFramebufferStatusEXT","glCheckFramebufferStatusEXT",QueryAll,false,GLenum(GLenum)) \
 X(Attach2D,"__glewFramebufferTexture2D","glFramebufferTexture2D",Config,true,void(GLenum,GLenum,GLenum,GLuint,GLint)) \
 X(Attach2DEXT,"__glewFramebufferTexture2DEXT","glFramebufferTexture2DEXT",Config,false,void(GLenum,GLenum,GLenum,GLuint,GLint)) \
 X(Attach1D,"__glewFramebufferTexture1D","glFramebufferTexture1D",Config,false,void(GLenum,GLenum,GLenum,GLuint,GLint)) \
 X(Attach3D,"__glewFramebufferTexture3D","glFramebufferTexture3D",Config,false,void(GLenum,GLenum,GLenum,GLuint,GLint,GLint)) \
 X(AttachLayer,"__glewFramebufferTextureLayer","glFramebufferTextureLayer",Config,true,void(GLenum,GLenum,GLuint,GLint,GLint)) \
 X(AttachLayerEXT,"__glewFramebufferTextureLayerEXT","glFramebufferTextureLayerEXT",Config,false,void(GLenum,GLenum,GLuint,GLint,GLint)) \
 X(AttachTexture,"__glewFramebufferTexture","glFramebufferTexture",Config,true,void(GLenum,GLenum,GLuint,GLint)) \
 X(AttachTextureARB,"__glewFramebufferTextureARB","glFramebufferTextureARB",Config,false,void(GLenum,GLenum,GLuint,GLint)) \
 X(AttachRenderbuffer,"__glewFramebufferRenderbuffer","glFramebufferRenderbuffer",Config,true,void(GLenum,GLenum,GLenum,GLuint)) \
 X(AttachRenderbufferEXT,"__glewFramebufferRenderbufferEXT","glFramebufferRenderbufferEXT",Config,false,void(GLenum,GLenum,GLenum,GLuint)) \
 X(DeleteFbo,"__glewDeleteFramebuffers","glDeleteFramebuffers",Config,true,void(GLsizei,const GLuint*)) \
 X(DeleteFboEXT,"__glewDeleteFramebuffersEXT","glDeleteFramebuffersEXT",Config,false,void(GLsizei,const GLuint*)) \
 X(ClearFloat,"__glewClearBufferfv","glClearBufferfv",Clear,true,void(GLenum,GLint,const GLfloat*)) \
 X(ClearInt,"__glewClearBufferiv","glClearBufferiv",Clear,true,void(GLenum,GLint,const GLint*)) \
 X(ClearUint,"__glewClearBufferuiv","glClearBufferuiv",Clear,true,void(GLenum,GLint,const GLuint*)) \
 X(ClearDepthStencil,"__glewClearBufferfi","glClearBufferfi",Clear,true,void(GLenum,GLint,GLfloat,GLint)) \
 X(GetInt64,"__glewGetInteger64v","glGetInteger64v",Query,true,void(GLenum,int64_t*)) \
 X(GetAttachment,"__glewGetFramebufferAttachmentParameteriv","glGetFramebufferAttachmentParameteriv",QueryAll,true,void(GLenum,GLenum,GLenum,GLint*)) \
 X(GetAttachmentEXT,"__glewGetFramebufferAttachmentParameterivEXT","glGetFramebufferAttachmentParameterivEXT",QueryAll,false,void(GLenum,GLenum,GLenum,GLint*)) \
 X(NamedBuffers,"__glewNamedFramebufferDrawBuffers","glNamedFramebufferDrawBuffers",Selection,false,void(GLuint,GLsizei,const GLenum*)) \
 X(NamedBuffer,"__glewNamedFramebufferDrawBuffer","glNamedFramebufferDrawBuffer",Selection,false,void(GLuint,GLenum)) \
 X(NamedBuffersEXT,"__glewFramebufferDrawBuffersEXT","glFramebufferDrawBuffersEXT",Selection,false,void(GLuint,GLsizei,const GLenum*)) \
 X(NamedBufferEXT,"__glewFramebufferDrawBufferEXT","glFramebufferDrawBufferEXT",Selection,false,void(GLuint,GLenum)) \
 X(NamedTexture,"__glewNamedFramebufferTexture","glNamedFramebufferTexture",Config,false,void(GLuint,GLenum,GLuint,GLint)) \
 X(NamedTextureLayer,"__glewNamedFramebufferTextureLayer","glNamedFramebufferTextureLayer",Config,false,void(GLuint,GLenum,GLuint,GLint,GLint)) \
 X(NamedRenderbuffer,"__glewNamedFramebufferRenderbuffer","glNamedFramebufferRenderbuffer",Config,false,void(GLuint,GLenum,GLenum,GLuint)) \
 X(NamedBlit,"__glewBlitNamedFramebuffer","glBlitNamedFramebuffer",Blit,false,void(GLuint,GLuint,GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLbitfield,GLenum)) \
 X(NamedClearFloat,"__glewClearNamedFramebufferfv","glClearNamedFramebufferfv",Clear,false,void(GLuint,GLenum,GLint,const GLfloat*)) \
 X(NamedClearInt,"__glewClearNamedFramebufferiv","glClearNamedFramebufferiv",Clear,false,void(GLuint,GLenum,GLint,const GLint*)) \
 X(NamedClearUint,"__glewClearNamedFramebufferuiv","glClearNamedFramebufferuiv",Clear,false,void(GLuint,GLenum,GLint,const GLuint*)) \
 X(NamedClearDepth,"__glewClearNamedFramebufferfi","glClearNamedFramebufferfi",Clear,false,void(GLuint,GLenum,GLint,GLfloat,GLint)) \
 X(NamedGet,"__glewGetNamedFramebufferParameteriv","glGetNamedFramebufferParameteriv",QueryAll,false,void(GLuint,GLenum,GLint*)) \
 X(NamedGetEXT,"__glewGetFramebufferParameterivEXT","glGetFramebufferParameterivEXT",QueryAll,false,void(GLuint,GLenum,GLint*)) \
 X(NamedAttachGet,"__glewGetNamedFramebufferAttachmentParameteriv","glGetNamedFramebufferAttachmentParameteriv",QueryAll,false,void(GLuint,GLenum,GLenum,GLint*)) \
 X(GetIntIndexed,"__glewGetIntegeri_v","glGetIntegeri_v",Query,false,void(GLenum,GLuint,GLint*)) \
 X(GetBoolIndexed,"__glewGetBooleani_v","glGetBooleani_v",Query,false,void(GLenum,GLuint,GLboolean*)) \
 X(GetInt64Indexed,"__glewGetInteger64i_v","glGetInteger64i_v",Query,false,void(GLenum,GLuint,int64_t*)) \
 X(GetFloatIndexed,"__glewGetFloati_v","glGetFloati_v",Query,false,void(GLenum,GLuint,GLfloat*)) \
 X(GetDoubleIndexed,"__glewGetDoublei_v","glGetDoublei_v",Query,false,void(GLenum,GLuint,double*)) \
 X(Validate,"__glewValidateProgram","glValidateProgram",QueryAll,false,void(GLuint)) \
 X(ValidatePipeline,"__glewValidateProgramPipeline","glValidateProgramPipeline",QueryAll,false,void(GLuint)) \
 X(DrawArrayIndirect,"__glewDrawArraysIndirect","glDrawArraysIndirect",Unobserved,false,void(GLenum,const void*)) \
 X(DrawElementIndirect,"__glewDrawElementsIndirect","glDrawElementsIndirect",Unobserved,false,void(GLenum,GLenum,const void*)) \
 X(MultiArrayIndirect,"__glewMultiDrawArraysIndirect","glMultiDrawArraysIndirect",Unobserved,false,void(GLenum,const void*,GLsizei,GLsizei)) \
 X(MultiElementIndirect,"__glewMultiDrawElementsIndirect","glMultiDrawElementsIndirect",Unobserved,false,void(GLenum,GLenum,const void*,GLsizei,GLsizei)) \
 X(DrawArrayInstanced,"__glewDrawArraysInstanced","glDrawArraysInstanced",Unobserved,false,void(GLenum,GLint,GLsizei,GLsizei)) \
 X(DrawArrayInstancedARB,"__glewDrawArraysInstancedARB","glDrawArraysInstancedARB",Unobserved,false,void(GLenum,GLint,GLsizei,GLsizei)) \
 X(DrawElementInstancedARB,"__glewDrawElementsInstancedARB","glDrawElementsInstancedARB",Unobserved,false,void(GLenum,GLsizei,GLenum,const void*,GLsizei)) \
 X(DrawArrayBaseInstance,"__glewDrawArraysInstancedBaseInstance","glDrawArraysInstancedBaseInstance",Unobserved,false,void(GLenum,GLint,GLsizei,GLsizei,GLuint)) \
 X(DrawElementBaseInstance,"__glewDrawElementsInstancedBaseInstance","glDrawElementsInstancedBaseInstance",Unobserved,false,void(GLenum,GLsizei,GLenum,const void*,GLsizei,GLuint)) \
 X(DrawElementBaseVertexBaseInstance,"__glewDrawElementsInstancedBaseVertexBaseInstance","glDrawElementsInstancedBaseVertexBaseInstance",Unobserved,false,void(GLenum,GLsizei,GLenum,const void*,GLsizei,GLint,GLuint)) \
 X(Feedback,"__glewDrawTransformFeedback","glDrawTransformFeedback",Unobserved,false,void(GLenum,GLuint)) \
 X(FeedbackStream,"__glewDrawTransformFeedbackStream","glDrawTransformFeedbackStream",Unobserved,false,void(GLenum,GLuint,GLuint)) \
 X(FeedbackInstanced,"__glewDrawTransformFeedbackInstanced","glDrawTransformFeedbackInstanced",Unobserved,false,void(GLenum,GLuint,GLsizei)) \
 X(FeedbackStreamInstanced,"__glewDrawTransformFeedbackStreamInstanced","glDrawTransformFeedbackStreamInstanced",Unobserved,false,void(GLenum,GLuint,GLuint,GLsizei)) \
 X(CommandsNV,"__glewDrawCommandsNV","glDrawCommandsNV",Unobserved,false,void(GLenum,GLuint,const intptr_t*,const GLsizei*,GLuint)) \
 X(CommandsAddressNV,"__glewDrawCommandsAddressNV","glDrawCommandsAddressNV",Unobserved,false,void(GLenum,const uint64_t*,const GLsizei*,GLuint)) \
 X(CommandsStatesNV,"__glewDrawCommandsStatesNV","glDrawCommandsStatesNV",Unobserved,false,void(GLuint,const intptr_t*,const GLsizei*,const GLuint*,const GLuint*,GLuint)) \
 X(CommandsStatesAddressNV,"__glewDrawCommandsStatesAddressNV","glDrawCommandsStatesAddressNV",Unobserved,false,void(const uint64_t*,const GLsizei*,const GLuint*,const GLuint*,GLuint)) \
 X(CallListNV,"__glewCallCommandListNV","glCallCommandListNV",Unobserved,false,void(GLuint)) \
 X(DrawVkImageNV,"__glewDrawVkImageNV","glDrawVkImageNV",Unobserved,false,void(uint64_t,GLuint,GLfloat,GLfloat,GLfloat,GLfloat,GLfloat,GLfloat,GLfloat,GLfloat,GLfloat)) \
 X(MeshNV,"__glewDrawMeshTasksNV","glDrawMeshTasksNV",Unobserved,false,void(GLuint,GLuint)) \
 X(MeshIndirectNV,"__glewDrawMeshTasksIndirectNV","glDrawMeshTasksIndirectNV",Unobserved,false,void(intptr_t)) \
 X(MeshMultiIndirectNV,"__glewMultiDrawMeshTasksIndirectNV","glMultiDrawMeshTasksIndirectNV",Unobserved,false,void(intptr_t,GLsizei,GLsizei)) \
 X(MeshMultiCountNV,"__glewMultiDrawMeshTasksIndirectCountNV","glMultiDrawMeshTasksIndirectCountNV",Unobserved,false,void(intptr_t,intptr_t,GLsizei,GLsizei)) \
 X(MakeCurrent,"sdl:SDL_GL_MakeCurrent","SDL_GL_MakeCurrent",Context,true,int(void*,void*)) \
 X(DeleteContext,"sdl:SDL_GL_DeleteContext","SDL_GL_DeleteContext",Context,true,void(void*))

enum HookId {
#define TAG_ID(id,...) id,
 TAG_BUFFER_HOOKS(TAG_ID)
#undef TAG_ID
 HookCount
};
enum class Action { Bind,Clear,Selection,Query,QueryAll,Config,Context,Blit,Unobserved };
struct Hook {const char* slotName;const char* api;void* replacement;void* original;void** slot;bool required;};
static Hook* table();
static bool ready=false,unsupported=false,collecting=false;
static void*(*contextFn)()=nullptr;
static void(*previousCapture)()=nullptr;
static thread_local Counters counters;
struct Lease {bool active=false;GLuint fbo=0;void* context=nullptr;GLenum native[8]={},selected[8]={};};
static thread_local Lease lease;
static thread_local void* attachmentContext=nullptr;
static decltype(gl::GetIntegerv) driverGet=nullptr;
static decltype(gl::DrawBuffers) driverBuffers=nullptr;
static decltype(gl::BindFramebuffer) driverBind=nullptr;
static bool draw_query(GLenum p){return p==0x0C01 || (p>=GL_DRAW_BUFFER0 && p<GL_DRAW_BUFFER0+32);}
static void gap(const char* message){
    unsupported=true;requested=false;if(collecting)++counters.gaps;
    fprintf(stderr,"[tag-buffer-lease] ownership gap: %s; candidate disabled\n",message);
}
static bool enabled(){return requested && ready && !unsupported && g_sunTagBatchAttachments;}
static bool failed(){return unsupported;}
static bool safe_context(){
    if(!contextFn)return !lease.active;
    void* current=contextFn();
    return (!lease.active || current==lease.context) &&
           (!g_sunTagBatchFbo || !attachmentContext || current==attachmentContext);
}
static void attachment_selected(){if(ready && contextFn)attachmentContext=contextFn();}
static void attachment_finished(){attachmentContext=nullptr;}
static void release(Fence reason){
    if(!lease.active)return;
    Internal internal;
    if(contextFn && contextFn()!=lease.context){gap("unobserved context switch");return;}
    GLint old=0;driverGet(GL_DRAW_FRAMEBUFFER_BINDING,&old);
    if(collecting){++counters.released;++counters.fences[unsigned(reason)];}
    // Both binds target DRAW only: the engine's independent read binding stays intact.
    if(gl::IsFramebuffer && gl::IsFramebuffer(lease.fbo)){
        if((GLuint)old!=lease.fbo)driverBind(GL_DRAW_FRAMEBUFFER,lease.fbo);
        driverBuffers(8,lease.native);
        if((GLuint)old!=lease.fbo)driverBind(GL_DRAW_FRAMEBUFFER,(GLuint)old);
    }
    lease=Lease{};
}
static bool native_buffers(GLuint fbo,GLenum* actual){
    if(!lease.active)return false;
    if(!enabled() || fbo!=lease.fbo || (contextFn && contextFn()!=lease.context)){
        release(Fence::Boundary);return false;
    }
    if(std::memcmp(actual,lease.selected,sizeof(lease.selected))!=0){
        // Preserve newly observed native mappings, rather than overwriting them
        // with a stale snapshot. Our reserved mapping is the only injected entry.
        GLenum oldReserved=lease.native[kSunTagOutput];
        std::copy_n(actual,8,lease.native);
        if(lease.native[kSunTagOutput]==GL_COLOR_ATTACHMENT0+kSunTagOutput)
            lease.native[kSunTagOutput]=oldReserved;
        release(Fence::Boundary);gap("unobserved draw-buffer mutation");
        GLint value=0;driverGet(GL_DRAW_BUFFER0+kSunTagOutput,&value);actual[kSunTagOutput]=(GLenum)value;
        return false;
    }
    std::copy_n(lease.native,8,actual);return true;
}
static bool already_selected(GLuint fbo,const GLenum* selected){
    bool same=enabled() && lease.active && lease.fbo==fbo &&
        std::memcmp(selected,lease.selected,sizeof(lease.selected))==0;
    if(same && collecting)++counters.reused;
    return same;
}
static void selected(GLuint fbo,const GLenum* native,const GLenum* selectedBuffers){
    if(!enabled() || lease.active || !g_sunTagBatchFbo || !contextFn)return;
    void* context=contextFn();if(!context)return;
    lease.active=true;lease.fbo=fbo;lease.context=context;
    std::copy_n(native,8,lease.native);std::copy_n(selectedBuffers,8,lease.selected);
    if(collecting)++counters.acquired;
}
static bool retain(){return enabled() && lease.active;}
static void reset_frame(){counters=Counters{};collecting=true;}
static Counters end_native(){collecting=false;return counters;}
static void before(Action action){
    if(internalDepth)return;
    if(!lease.active && !(action==Action::Context && g_sunTagBatchFbo))return;
    Fence f=Fence::Boundary;
    switch(action){
    case Action::Bind:f=Fence::Bind;break;
    case Action::Clear:f=Fence::Clear;break;
    case Action::Selection:f=Fence::Selection;break;
    case Action::Query:case Action::QueryAll:f=Fence::Query;break;
    case Action::Config:f=Fence::Config;break;
    case Action::Context:f=Fence::Context;break;
    case Action::Blit:f=Fence::Blit;break;
    case Action::Unobserved:f=Fence::Boundary;break;
    }
    release(f);
    if(!unsupported && safe_context() && (action==Action::Context || action==Action::Config)){
        Internal internal;finish_sun_receiver_tag_batch();
    }
}
static void resume_context(){
    if((lease.active || g_sunTagBatchFbo) && safe_context()){
        release(Fence::Context);
        Internal internal;finish_sun_receiver_tag_batch();
    }
}
static void capture_boundary(){release(Fence::Owned);if(previousCapture)previousCapture();}

template<HookId I,Action A,typename Signature>struct Forward;
template<HookId I,Action A,typename R,typename...Args>
struct Forward<I,A,R(Args...)>{
    static R call(Args...args){
        Hook& h=table()[I];
        if(!h.original)h.original=dlsym(RTLD_NEXT,h.api);
        if constexpr(A==Action::Query){
            if(draw_query(std::get<0>(std::tuple<Args...>(args...))))before(A);
        }else before(A);
        using Fn=R(*)(Args...);Fn real=(Fn)h.original;
        if(!real){if constexpr(std::is_void<R>::value)return;else return R{};}
        if constexpr(A==Action::Context){
            if constexpr(std::is_void<R>::value){real(args...);resume_context();return;}
            else {R value=real(args...);resume_context();return value;}
        }else return real(args...);
    }
};
static Hook hooks[HookCount]={
#define TAG_ENTRY(id,slot,api,action,required,...) {slot,api,(void*)Forward<id,Action::action,__VA_ARGS__>::call,nullptr,nullptr,required},
 TAG_BUFFER_HOOKS(TAG_ENTRY)
#undef TAG_ENTRY
};
static Hook* table(){return hooks;}

// Own passes in this TU use private driver pointers. Route their relevant
// operations through the same fence, with Internal protecting tagging itself.
#define TAG_ROOT_PROXIES(X) \
 X(GetIntegerv,GetInt) X(GetFloatv,GetFloat) X(GetBooleanv,GetBool) X(GetBooleani_v,GetBoolIndexed) \
 X(Clear,Clear) X(DrawBuffer,DrawBuffer) X(DrawBuffers,Buffers) \
 X(BindFramebuffer,Bind) X(BlitFramebuffer,Blit) X(CheckFramebufferStatus,Check) \
 X(FramebufferTexture2D,Attach2D) X(FramebufferTextureLayer,AttachLayer) \
 X(DeleteFramebuffers,DeleteFbo) X(ClearBufferuiv,ClearUint) X(GetFramebufferAttachmentParameteriv,GetAttachment)
#define TAG_SAVED(field,id) static decltype(gl::field) saved_##field=nullptr;
TAG_ROOT_PROXIES(TAG_SAVED)
#undef TAG_SAVED
static bool writable_slot(void** slot){
    FILE* maps=fopen("/proc/self/maps","r");if(!maps)return false;
    char line[512];bool ok=false;
    while(fgets(line,sizeof(line),maps)){
        unsigned long a=0,b=0;char permissions[5]={};
        if(sscanf(line,"%lx-%lx %4s",&a,&b,permissions)==3 && (uintptr_t)slot>=a && (uintptr_t)slot+sizeof(void*)<=b){ok=permissions[1]=='w';break;}
    }
    fclose(maps);return ok;
}
static bool install(){
    if(unsupported)return false;
    if(ready)return true;
    if(!gl::GetIntegerv || !gl::DrawBuffers || !gl::BindFramebuffer || !gl::IsFramebuffer)return false;
    const bool coreVisible=dlsym(RTLD_DEFAULT,"glClear")==reinterpret_cast<void*>(::glClear) &&
        dlsym(RTLD_DEFAULT,"glDrawBuffer")==reinterpret_cast<void*>(::glDrawBuffer) &&
        dlsym(RTLD_DEFAULT,"glGetIntegerv")==reinterpret_cast<void*>(::glGetIntegerv) &&
        dlsym(RTLD_DEFAULT,"glGetFloatv")==reinterpret_cast<void*>(::glGetFloatv) &&
        dlsym(RTLD_DEFAULT,"glGetBooleanv")==reinterpret_cast<void*>(::glGetBooleanv) &&
        dlsym(RTLD_DEFAULT,"glGetDoublev")==reinterpret_cast<void*>(::glGetDoublev);
    if(!coreVisible){fprintf(stderr,"[tag-buffer-lease] core interposition coverage unavailable\n");return false;}
    GLint profile=0;gl::GetIntegerv(GL_CONTEXT_PROFILE_MASK,&profile);
    if(!(profile&GL_CONTEXT_CORE_PROFILE_BIT))return false;
    GLint max=0;gl::GetIntegerv(GL_MAX_DRAW_BUFFERS,&max);
    if(max!=8){fprintf(stderr,"[tag-buffer-lease] unsupported draw-buffer count %d\n",max);return false;}
    contextFn=(void*(*)())dlsym(RTLD_DEFAULT,"SDL_GL_GetCurrentContext");if(!contextFn)return false;
    // Refuse unsupported selector APIs rather than assume they are unused.
    const char* unsupportedSelectors[]={"__glewDrawBuffersIndexedEXT"};
    for(const char* name:unsupportedSelectors){void** slot=(void**)dlsym(RTLD_DEFAULT,name);
        if(slot && *slot){fprintf(stderr,"[tag-buffer-lease] untracked selector available: %s\n",name);return false;}}
    unsigned patched=0;
    for(auto& h:hooks){
        if(!h.slotName){
            h.original=dlsym(RTLD_NEXT,h.api);
        }else if(std::strncmp(h.slotName,"sdl:",4)==0){
            const auto* fn=(const uint8_t*)dlsym(RTLD_DEFAULT,h.slotName+4);
            if(fn && fn[0]==0x55 && fn[1]==0x48 && fn[2]==0x89 && fn[3]==0xe5 && fn[4]==0x5d && fn[5]==0xff && fn[6]==0x25){
                int32_t rel=0;std::memcpy(&rel,fn+7,4);h.slot=(void**)(fn+11+rel);
                if(writable_slot(h.slot))h.original=*h.slot;else h.slot=nullptr;
            }
        }else{
            h.slot=(void**)dlsym(RTLD_DEFAULT,h.slotName);
            if(h.slot && writable_slot(h.slot))h.original=*h.slot;else h.slot=nullptr;
        }
        if(h.required && !h.original){fprintf(stderr,"[tag-buffer-lease] missing required boundary: %s\n",h.api);return false;}
    }
    driverGet=gl::GetIntegerv;driverBuffers=gl::DrawBuffers;driverBind=gl::BindFramebuffer;
    for(auto& h:hooks)if(h.slot && h.original){*h.slot=h.replacement;++patched;}
#define TAG_SET(field,id) saved_##field=gl::field;gl::field=(decltype(gl::field))hooks[id].replacement;
    TAG_ROOT_PROXIES(TAG_SET)
#undef TAG_SET
    previousCapture=nwn_core::g_beforeFramebufferStateCapture;
    nwn_core::g_beforeFramebufferStateCapture=capture_boundary;
    ready=true;
    fprintf(stderr,"[tag-buffer-lease] installed %u verified native slots, own-pass fences and root dispatch proxies\n",patched);
    return true;
}
static void shutdown(){
    // Do not issue GL calls here: the context may already have been destroyed.
    requested=false;
    if(nwn_core::g_beforeFramebufferStateCapture==capture_boundary)
        nwn_core::g_beforeFramebufferStateCapture=previousCapture;
    for(auto& h:hooks)if(h.slot && *h.slot==h.replacement)*h.slot=h.original;
#define TAG_RESTORE(field,id) if(saved_##field && gl::field==(decltype(gl::field))hooks[id].replacement)gl::field=saved_##field;
    TAG_ROOT_PROXIES(TAG_RESTORE)
#undef TAG_RESTORE
    ready=false;
}
#undef TAG_ROOT_PROXIES
#undef TAG_BUFFER_HOOKS
} // namespace tag_buffer_lease

// Legacy core entry points are imported directly by this measured executable.
// Modern entry points are fenced at the engine's verified GLEW slots instead.
extern "C" void glClear(GLbitfield m){tag_buffer_lease::Forward<tag_buffer_lease::Clear,tag_buffer_lease::Action::Clear,void(GLbitfield)>::call(m);}
extern "C" void glDrawBuffer(GLenum b){tag_buffer_lease::Forward<tag_buffer_lease::DrawBuffer,tag_buffer_lease::Action::Selection,void(GLenum)>::call(b);}
extern "C" void glGetIntegerv(GLenum p,GLint* v){tag_buffer_lease::Forward<tag_buffer_lease::GetInt,tag_buffer_lease::Action::Query,void(GLenum,GLint*)>::call(p,v);}
extern "C" void glGetFloatv(GLenum p,GLfloat* v){tag_buffer_lease::Forward<tag_buffer_lease::GetFloat,tag_buffer_lease::Action::Query,void(GLenum,GLfloat*)>::call(p,v);}
extern "C" void glGetBooleanv(GLenum p,GLboolean* v){tag_buffer_lease::Forward<tag_buffer_lease::GetBool,tag_buffer_lease::Action::Query,void(GLenum,GLboolean*)>::call(p,v);}
extern "C" void glGetDoublev(GLenum p,double* v){tag_buffer_lease::Forward<tag_buffer_lease::GetDouble,tag_buffer_lease::Action::Query,void(GLenum,double*)>::call(p,v);}
#endif
