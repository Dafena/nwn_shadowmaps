#define main checkpoint_tag_state_main
#include "sun_receiver_tag_state_regression.cpp"
#undef main
#include <array>
#include <map>
static std::map<GLuint,std::array<GLenum,8>> framebufferBuffers;
static GLint readFbo=51;
static void model_buffers(GLsizei n,const GLenum* values){
    buffers(n,values);
    std::copy_n(drawBuffers,8,framebufferBuffers[(GLuint)nativeFbo].begin());
}
static void model_bind(GLenum target,GLuint fbo){
    if(target==GL_FRAMEBUFFER || target==GL_READ_FRAMEBUFFER)readFbo=(GLint)fbo;
    if(target==GL_READ_FRAMEBUFFER)return;
    std::copy_n(drawBuffers,8,framebufferBuffers[(GLuint)nativeFbo].begin());
    nativeFbo=(GLint)fbo;
    std::copy_n(framebufferBuffers[fbo].begin(),8,drawBuffers);
}
static void model_get(GLenum key,GLint* value){
    if(key==GL_READ_FRAMEBUFFER_BINDING)*value=readFbo;
    else get_int(key,value);
}
static void* fakeContext=(void*)0x100;
static void* current_context(){return fakeContext;}
static bool clearSawNative=false;
static void native_clear(GLbitfield mask){clearSawNative=mask==GL_COLOR_BUFFER_BIT && drawBuffers[7]==GL_NONE;}
static bool contextSawNative=false;
static int make_current(void*,void* next){
    contextSawNative=drawBuffers[7]==GL_NONE && attached==0;
    fakeContext=next;return 0;
}
static void configure_case(){
    namespace lease=tag_buffer_lease;
    fakeContext=(void*)0x100;nativeFbo=42;attachmentType=GL_NONE;maxBuffers=8;
    currentProgram=9;knownProgram=true;depthWrite=true;deletedFbo=false;attached=0;
    for(auto& buffer:drawBuffers)buffer=GL_NONE;
    drawBuffers[0]=GL_COLOR_ATTACHMENT0;
    framebufferBuffers.clear();readFbo=51;
    std::copy_n(drawBuffers,8,framebufferBuffers[42].begin());
    framebufferBuffers[43][0]=GL_COLOR_ATTACHMENT0+2;
    g_receiverEnabled=true;g_inOurPass=false;g_cascadeReplayActive=false;g_localLightPassActive=false;
    g_overlayPassActive=false;nwn_core::g_ownedPass=false;g_inStencilShadow=false;
    g_shadowFrameContext.valid=true;g_traceFrameActive=true;
    g_areaScene=g_traceCurrentScene=(void*)1;nwn_core::g_currentBucket=0;g_currentDrawOwner=nullptr;
    g_sunTagTex=103;g_sunTagDummy2D=104;g_sunTagDummyMS=105;g_sunTagWidth=4;g_sunTagHeight=1;g_sunTagSamples=8;
    g_sunTagBorrowed=false;g_sunTagBatchFbo=g_sunTagBatchTexture=0;
    g_sunTagFrame=0;g_sunTagSourceFbo=0;++g_shadowFrameContext.serial;
    g_sunTagBatchAttachments=true;g_shadowStageSkipTags=false;
    tag_work::fastTextures=tag_work::fastCapability=tag_work::skipRepeatedValidation=false;
    tag_work::suppressDrawTarget=tag_work::suppressTagWrites=false;
    gl::GetIntegerv=model_get;gl::DrawBuffers=model_buffers;gl::BindFramebuffer=model_bind;
    gl::IsFramebuffer=is_framebuffer;
    lease::lease=lease::Lease{};lease::attachmentContext=nullptr;
    lease::driverGet=model_get;lease::driverBuffers=model_buffers;lease::driverBind=model_bind;
    lease::ready=true;lease::unsupported=false;lease::requested=true;lease::contextFn=current_context;
    lease::previousCapture=nullptr;lease::reset_frame();
    nwn_core::g_beforeFramebufferStateCapture=lease::capture_boundary;
}
static void draw_tag(){begin_sun_receiver_tag();end_sun_receiver_tag();}
static void finish_case(){
    finish_sun_receiver_tag_batch();
    tag_buffer_lease::requested=false;tag_buffer_lease::ready=false;
    nwn_core::g_beforeFramebufferStateCapture=nullptr;
}
int main(){
    if(checkpoint_tag_state_main())return 1;
    namespace l=tag_buffer_lease;
    unsigned failures=0;
    auto check=[&](bool ok,const char* name){printf("buffer lease %s: %s\n",name,ok?"PASS":"FAIL");if(!ok)++failures;};
    configure_case();unsigned before=bufferChanges;
    bool classes=true;
    for(int i=0;i<32;++i){
        g_currentDrawOwnerKind=i%2?DrawOwnerKind::Creature:DrawOwnerKind::NonCreature;
        draw_tag();classes=classes && tagKind==(i%2) && drawBuffers[7]==GL_COLOR_ATTACHMENT0+7;
    }
    check(classes && bufferChanges==before+1,"32 draws retain selection and update classification");
    model_bind(GL_DRAW_FRAMEBUFFER,43);finish_case();
    check(bufferChanges==before+2 && nativeFbo==43 && drawBuffers[0]==GL_COLOR_ATTACHMENT0+2 &&
          framebufferBuffers[42][0]==GL_COLOR_ATTACHMENT0 && framebufferBuffers[42][7]==GL_NONE &&
          readFbo==51 && attached==0,"scene cleanup restores source mapping and current draw FBO");
    configure_case();draw_tag();
    l::table()[l::Clear].original=(void*)native_clear;
    l::Forward<l::Clear,l::Action::Clear,void(GLbitfield)>::call(GL_COLOR_BUFFER_BIT);
    check(clearSawNative && !l::lease.active && drawBuffers[7]==GL_NONE,"native clear sees no integer tag destination");finish_case();
    configure_case();draw_tag();GLint value=-1;
    l::table()[l::GetInt].original=(void*)model_get;
    l::Forward<l::GetInt,l::Action::Query,void(GLenum,GLint*)>::call(GL_DRAW_BUFFER0+7,&value);
    check(value==GL_NONE && !l::lease.active,"native query observes original buffer selection");finish_case();
    configure_case();draw_tag();
    GLenum next[8]={GL_COLOR_ATTACHMENT0+1,GL_NONE,GL_NONE,GL_NONE,GL_NONE,GL_NONE,GL_NONE,GL_NONE};
    l::table()[l::Buffers].original=(void*)model_buffers;
    l::Forward<l::Buffers,l::Action::Selection,void(GLsizei,const GLenum*)>::call(8,next);
    draw_tag();finish_case();
    check(std::memcmp(next,drawBuffers,sizeof(next))==0,"native mapping changes become the next restoration snapshot");
    configure_case();draw_tag();
    l::before(l::Action::Blit);
    check(!l::lease.active && drawBuffers[7]==GL_NONE,"blit fence excludes the private integer destination");finish_case();
    configure_case();draw_tag();
    {nwn_core::OwnedPass owned;check(nwn_core::g_ownedPass && drawBuffers[7]==GL_NONE,"owned pass releases selection before state capture");}
    finish_case();
    configure_case();draw_tag();depthWrite=false;
    begin_sun_receiver_tag();
    check(!g_sunTagBorrowed && drawBuffers[7]==GL_NONE,"non-tagging depth-write-off draw fails back to native buffers");finish_case();
    configure_case();draw_tag();currentProgram=77;knownProgram=false;
    begin_sun_receiver_tag();
    check(!g_sunTagBorrowed && drawBuffers[7]==GL_NONE,"unknown shader fails back to native buffers");finish_case();
    configure_case();draw_tag();
    l::table()[l::MakeCurrent].original=(void*)make_current;
    l::Forward<l::MakeCurrent,l::Action::Context,int(void*,void*)>::call(nullptr,(void*)0x200);
    check(contextSawNative && !l::lease.active && !attached,"context switch restores buffers and attachment before switching");finish_case();
    configure_case();draw_tag();
    model_buffers(8,next);GLenum observed[8];std::copy_n(drawBuffers,8,observed);
    l::native_buffers(42,observed);
    check(l::failed() && !l::lease.active && std::memcmp(next,drawBuffers,sizeof(next))==0,"unobserved mutation disables candidate without overwriting new native intent");finish_case();
    configure_case();draw_tag();fakeContext=(void*)0x200;
    before=bufferChanges;finish_sun_receiver_tag_batch();
    check(l::lease.active && attached==103 && bufferChanges==before,"wrong context never mutates an unrelated FBO");
    l::release(l::Fence::Context);
    fakeContext=(void*)0x100;l::resume_context();
    check(l::failed() && !l::lease.active && !attached && drawBuffers[7]==GL_NONE,"deferred owner-context cleanup restores original state");finish_case();
    configure_case();l::ready=false;draw_tag();
    check(drawBuffers[7]==GL_NONE && !l::lease.active,"incomplete hook coverage retains per-draw checkpoint behavior");finish_case();
    // Restoration never overwrites a later owner's dispatch pointer.
    void* nativeSlot=(void*)bind_framebuffer;
    auto& hook=l::table()[l::Bind];hook.slot=&nativeSlot;hook.original=(void*)bind_framebuffer;
    nativeSlot=hook.replacement;l::shutdown();check(nativeSlot==(void*)bind_framebuffer,"shutdown restores the borrowed dispatch slot");
    nativeSlot=(void*)buffers;l::shutdown();check(nativeSlot==(void*)buffers,"shutdown preserves a subsequent dispatch owner");hook.slot=nullptr;
    l::unsupported=false;l::previousCapture=nullptr;l::contextFn=nullptr;l::lease=l::Lease{};l::attachmentContext=nullptr;
    return failures?1:0;
}
