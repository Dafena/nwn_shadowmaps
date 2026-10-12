// Headless Mesa/EGL smoke test of the REAL private pass (not retyped shaders).
// This substitutes only NWN observation state, never the pass or its GL calls.
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <dlfcn.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "../shadow_math.h"
#include "../nwn_overlay.h"
#include "../nwn_hooks_core.h"
namespace nwn_core { bool g_ownedPass=false; }
using CreateShader_t=decltype(&glCreateShader);
using CompileShader_t=decltype(&glCompileShader);
using GetShaderiv_t=decltype(&glGetShaderiv);
using CreateProgram_t=decltype(&glCreateProgram);
using AttachShader_t=decltype(&glAttachShader);
using LinkProgram_t=decltype(&glLinkProgram);
using GetProgramiv_t=decltype(&glGetProgramiv);
using ShaderSource_t=decltype(&glShaderSource);
using UseProgram_t=decltype(&glUseProgram);
static ShaderSource_t g_realShaderSource=&glShaderSource;
static UseProgram_t g_realUseProgram=&glUseProgram;
static GLuint g_curProgram=0;
static void* glew_or_dlsym(void*,const char* name) { return dlsym(RTLD_DEFAULT,name); }
namespace eng {
static void* GlewCreateShader=nullptr; static void* GlewCompileShader=nullptr;
static void* GlewGetShaderiv=nullptr; static void* GlewCreateProgram=nullptr;
static void* GlewAttachShader=nullptr; static void* GlewLinkProgram=nullptr;
static void* GlewGetProgramiv=nullptr; static void* GlewShaderSource=nullptr;
static void* GlewUseProgram=nullptr;
static float ambient[3]={0.5f,0.5f,0.5f};
static float* SunAmbient=ambient; static float* SunDiffuse=nullptr;
}
namespace gl {
static auto GetIntegerv=&glGetIntegerv; static auto GetBooleanv=&glGetBooleanv;
static auto IsEnabled=&glIsEnabled; static auto ActiveTexture=&glActiveTexture;
static auto BindTexture=&glBindTexture; static auto UniformMatrix4fv=&glUniformMatrix4fv;
static auto Uniform4f=&glUniform4f; static auto Uniform1f=&glUniform1f;
static auto Uniform1i=&glUniform1i; static auto GetUniformLocation=&glGetUniformLocation;
static auto Enable=&glEnable; static auto Disable=&glDisable;
static auto BlendFuncSeparate=&glBlendFuncSeparate;
static auto BlendEquationSeparate=&glBlendEquationSeparate;
static auto DepthFunc=&glDepthFunc; static auto DepthMask=&glDepthMask;
static auto ColorMask=&glColorMask;
}
static bool g_shaderRainEnabled=false;
static bool g_shaderRainHideNative=true;
static int g_shaderRainBudget=256;
static float g_shaderRainRadius=8,g_shaderRainWindX=0,g_shaderRainWindY=0,g_shaderRainSplashes=1;
static bool g_haveEngineFog=false;
static float g_fogStartEngine=0,g_fogEndEngine=0;
static int areaToken=1,sceneToken=2;
static void* g_areaScene=&sceneToken;
struct Vec3f { float x,y,z,w; };
struct Camera {
    bool valid=true; unsigned serial=1; void* scene=&sceneToken;
    GLint viewport[4]={0,0,256,256};
    Vec3f eye={0,-12,6,1};
    float projection[16]={},view[16]={},viewInverse[16]={};
};
static Camera g_shadowFrameContext;
namespace weather_runtime {
static constexpr int kWeatherRain=1;
struct Weather {
    void* observedScene=&sceneToken; bool weatherObserved=true,areaFlagsKnown=true,areaInterior=false,raining=true;
    int weatherType=1; float weatherDensity=1;
    bool precipitationOcclusionValid=true,precipitationOcclusionDirty=false;
    void* precipitationOcclusionScene=&sceneToken; void* precipitationOcclusionArea=&areaToken;
    void* weatherArea=&areaToken;
    GLuint precipitationOcclusionTexture=0;
    int precipitationOcclusionSize=128; float precipitationOcclusionExtent=64;
    float precipitationOcclusionVP[16]={};
    unsigned snowLightDataFrame=0,frameSerial=1; GLuint snowLightDataTexture=0; int snowLightDataCount=0;
};
static Weather g;
}
static double fakeTime=100;
static double now_seconds() { return fakeTime; }
const char* shadow_getenv(const char* name) { return getenv(name); }
#include "../weather_particles.inc"
static void require(bool value,const char* name) {
    if(!value) { fprintf(stderr,"FAIL: %s (GL error 0x%x)\n",name,glGetError()); exit(1); }
}
struct Snapshot {
    GLuint trackedProgram=g_curProgram;
    bool owned=nwn_core::g_ownedPass;
    GLint program,vao,active,depthFunc,texture[2],sampler[2],blend[6],fbo[2],viewport[4];
    GLboolean depthMask,colorMask[4],colorMaskOne[4],enabled[18],blendEnabled,blendEnabledOne;
    Snapshot() {
        glGetIntegerv(GL_CURRENT_PROGRAM,&program); glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao);
        glGetIntegerv(GL_ACTIVE_TEXTURE,&active); glGetIntegerv(GL_DEPTH_FUNC,&depthFunc);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&fbo[0]);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&fbo[1]); glGetIntegerv(GL_VIEWPORT,viewport);
        glGetBooleanv(GL_DEPTH_WRITEMASK,&depthMask); glGetBooleani_v(GL_COLOR_WRITEMASK,0,colorMask);
        blendEnabled=glIsEnabledi(GL_BLEND,0);
        blendEnabledOne=glIsEnabledi(GL_BLEND,1);
        glGetBooleani_v(GL_COLOR_WRITEMASK,1,colorMaskOne);
        GLenum queries[6]={GL_BLEND_SRC_RGB,GL_BLEND_DST_RGB,GL_BLEND_SRC_ALPHA,GL_BLEND_DST_ALPHA,
            GL_BLEND_EQUATION_RGB,GL_BLEND_EQUATION_ALPHA};
        for(int i=0;i<6;++i) glGetIntegerv(queries[i],&blend[i]);
        for(int i=0;i<18;++i) enabled[i]=glIsEnabled(weather_particles::StateGuard::caps[i]);
        for(int i=0;i<2;++i) {
            glActiveTexture(GL_TEXTURE0+i); glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture[i]);
            glGetIntegerv(GL_SAMPLER_BINDING,&sampler[i]);
        }
        glActiveTexture(active);
    }
    bool same(const Snapshot& b) const {
        return trackedProgram==b.trackedProgram && owned==b.owned &&
            program==b.program && vao==b.vao && active==b.active && depthFunc==b.depthFunc &&
            depthMask==b.depthMask && blendEnabled==b.blendEnabled && blendEnabledOne==b.blendEnabledOne &&
            !memcmp(texture,b.texture,sizeof(texture)) && !memcmp(sampler,b.sampler,sizeof(sampler)) &&
            !memcmp(blend,b.blend,sizeof(blend)) && !memcmp(fbo,b.fbo,sizeof(fbo)) &&
            !memcmp(viewport,b.viewport,sizeof(viewport)) && !memcmp(colorMask,b.colorMask,sizeof(colorMask)) &&
            !memcmp(colorMaskOne,b.colorMaskOne,sizeof(colorMaskOne)) &&
            !memcmp(enabled,b.enabled,sizeof(enabled));
    }
};
static unsigned drawFrames(int frames, float sceneDepth=1) {
    unsigned lit=0;
    for(int i=0;i<frames;++i) {
        glDisable(GL_SCISSOR_TEST); glDepthMask(GL_TRUE); glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
        glClearColor(0,0,0,0.6f); glClearDepth(sceneDepth); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        // Deliberately hostile inherited state: the pass must render and restore it.
        glEnable(GL_SCISSOR_TEST); glScissor(0,0,1,1); glDepthFunc(GL_GREATER);
        glDepthMask(GL_FALSE); glColorMask(GL_TRUE,GL_FALSE,GL_TRUE,GL_TRUE);
        glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE,GL_ONE,GL_ZERO);
        glBlendEquationSeparate(GL_FUNC_REVERSE_SUBTRACT,GL_FUNC_ADD);
        glEnablei(GL_BLEND,0); glActiveTexture(GL_TEXTURE0+5);
        ++g_shadowFrameContext.serial; fakeTime+=0.04;
        Snapshot before; draw_weather_particles(g_areaScene); Snapshot after;
        require(before.same(after),"GL state restored after private rain draw");
        require(glGetError()==GL_NO_ERROR,"private rain draw has no GL errors");
        std::vector<unsigned char> pixels(256*256*4);
        glReadPixels(0,0,256,256,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        for(size_t j=0;j<pixels.size();j+=4) {
            if(pixels[j] || pixels[j+1] || pixels[j+2]) ++lit;
            require(std::abs((int)pixels[j+3]-153)<=1,"native framebuffer alpha preserved");
        }
    }
    return lit;
}
int main() {
    weather_particles::load_environment();
    auto getDisplay=(PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    require(getDisplay!=nullptr,"surfaceless EGL entry point");
    EGLDisplay display=getDisplay(EGL_PLATFORM_SURFACELESS_MESA,EGL_DEFAULT_DISPLAY,nullptr);
    require(eglInitialize(display,nullptr,nullptr),"EGL initialized"); eglBindAPI(EGL_OPENGL_API);
    EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_DEPTH_SIZE,24,EGL_NONE};
    EGLConfig config; EGLint count=0; require(eglChooseConfig(display,attrs,&config,1,&count)&&count,"EGL config");
    EGLint contextAttrs[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,3,
        EGL_CONTEXT_OPENGL_PROFILE_MASK,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,EGL_NONE};
    EGLContext context=eglCreateContext(display,config,EGL_NO_CONTEXT,contextAttrs);
    EGLint surfaceAttrs[]={EGL_WIDTH,256,EGL_HEIGHT,256,EGL_NONE};
    EGLSurface surface=eglCreatePbufferSurface(display,config,surfaceAttrs);
    require(eglMakeCurrent(display,surface,surface,context),"OpenGL context current");
    printf("OpenGL %s / %s\n",glGetString(GL_VERSION),glGetString(GL_RENDERER));
    glViewport(0,0,256,256);
    auto& camera=g_shadowFrameContext;
    build_ortho(8,1,64,camera.projection);
    camera.view[0]=1; camera.view[6]=-1; camera.view[9]=1;
    camera.view[13]=-6; camera.view[14]=-12; camera.view[15]=1;
    require(mat4_inverse(camera.view,camera.viewInverse),"camera matrix inverse");
    auto& weather=weather_runtime::g;
    weather.precipitationOcclusionVP[0]=weather.precipitationOcclusionVP[5]=1.0f/64;
    weather.precipitationOcclusionVP[10]=-1.0f/64; weather.precipitationOcclusionVP[15]=1;
    std::vector<float> depth(128*128,0.5f); // Exposed horizontal floor at z=0.
    glGenTextures(1,&weather.precipitationOcclusionTexture);
    glBindTexture(GL_TEXTURE_2D,weather.precipitationOcclusionTexture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT24,128,128,0,GL_DEPTH_COMPONENT,GL_FLOAT,depth.data());
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_COMPARE_MODE,GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    // Nonzero native objects catch restoration bugs hidden by a default fixture.
    const char* nativeVs="#version 330 core\nvoid main(){gl_Position=vec4(0,0,0,1);}";
    const char* nativeFs="#version 330 core\nout vec4 color; void main(){color=vec4(1);}";
    GLuint v=glCreateShader(GL_VERTEX_SHADER),f=glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(v,1,&nativeVs,nullptr); glShaderSource(f,1,&nativeFs,nullptr);
    glCompileShader(v); glCompileShader(f);
    GLuint nativeProgram=glCreateProgram(); glAttachShader(nativeProgram,v);
    glAttachShader(nativeProgram,f); glLinkProgram(nativeProgram); glUseProgram(nativeProgram);
    g_curProgram=nativeProgram;
    glDeleteShader(v); glDeleteShader(f);
    GLuint nativeVao=0; glGenVertexArrays(1,&nativeVao); glBindVertexArray(nativeVao);
    GLuint nativeSamplers[2]={}; glGenSamplers(2,nativeSamplers);
    glBindSampler(0,nativeSamplers[0]); glBindSampler(1,nativeSamplers[1]);
    glDisablei(GL_BLEND,1); glColorMaski(1,GL_FALSE,GL_TRUE,GL_FALSE,GL_TRUE);
    require(glGetError()==GL_NO_ERROR,"nonzero native-state fixture initialized");
    require(drawFrames(2)==0,"disabled preview leaves scene unchanged");
    g_shaderRainEnabled=true;
    require(drawFrames(50)>0,"visible rain and splashes on exposed floor");
    require(weather_particles::program!=0,"actual runtime shaders linked");
    // Exercise exact-resource suppression with native-like state, plus fallbacks.
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D,weather.precipitationOcclusionTexture);
    glEnable(GL_DEPTH_TEST); glEnable(GL_BLEND); glDepthMask(GL_FALSE);
    glActiveTexture(GL_TEXTURE0+5);
    weather_particles::note_material_texture("FXPA_RAIN01");
    Snapshot rainBefore;
    require(weather_particles::suppress_native_draw(true),"exact native rain hidden after private draw");
    Snapshot rainAfter;
    require(rainBefore.same(rainAfter),"suppression check preserves GL state");
    require(!weather_particles::suppress_native_draw(false),"UI/replay/other scenes retain native draw");
    g_shaderRainEnabled=false;
    require(!weather_particles::suppress_native_draw(true),"preview off restores native rain immediately");
    g_shaderRainEnabled=true; g_shaderRainHideNative=false;
    require(!weather_particles::suppress_native_draw(true),"native-rain toggle restores rain");
    g_shaderRainHideNative=true; weather.precipitationOcclusionDirty=true;
    require(!weather_particles::suppress_native_draw(true),"incomplete map restores native rain");
    weather.precipitationOcclusionDirty=false; weather.areaInterior=true;
    require(!weather_particles::suppress_native_draw(true),"interior native draw untouched");
    weather.areaInterior=false;
    glDepthMask(GL_TRUE);
    require(!weather_particles::suppress_native_draw(true),"depth-writing geometry untouched");
    glDepthMask(GL_FALSE); glDisable(GL_DEPTH_TEST);
    require(!weather_particles::suppress_native_draw(true),"depthless UI untouched");
    glEnable(GL_DEPTH_TEST);
    weather_particles::note_material_texture("fxpa_snow01");
    require(!weather_particles::suppress_native_draw(true),"snow and unrelated particles untouched");
    require(!weather_particles::native_rain_name("fxpa_rain01_extra"),"rain resource match is exact");
    weather_particles::note_material_texture("fxpa_rain01");
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,0); glActiveTexture(GL_TEXTURE0+5);
    require(!weather_particles::suppress_native_draw(true),"changed live texture rejects stale material identity");
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,weather.precipitationOcclusionTexture);
    glActiveTexture(GL_TEXTURE0+5);
    ++g_shadowFrameContext.serial;
    require(weather_particles::suppress_native_draw(true),"next area frame uses completed previous private pass");
    ++g_shadowFrameContext.serial;
    require(!weather_particles::suppress_native_draw(true),"stale private frame restores native rain");
    require(drawFrames(3,0)==0,"completed scene depth occludes precipitation");
    g_shadowFrameContext.viewport[2]=128;
    require(drawFrames(3)==0,"mismatched native viewport fails closed");
    g_shadowFrameContext.viewport[2]=256;
    g_haveEngineFog=true; g_fogStartEngine=0; g_fogEndEngine=0.5;
    require(drawFrames(3)==0,"authored fog fades precipitation");
    g_haveEngineFog=false;
    weather.areaInterior=true;
    require(drawFrames(3)==0,"interiors emit no rain");
    weather.areaInterior=false; weather.raining=false; weather.weatherType=2;
    require(drawFrames(3)==0,"snow/clear weather emits no rain");
    weather.raining=true; weather.weatherType=1; weather.weatherDensity=0;
    require(drawFrames(3)==0,"zero density emits no rain");
    weather.weatherDensity=1; weather.precipitationOcclusionDirty=true;
    require(drawFrames(3)==0,"partial shelter generation fails closed");
    weather.precipitationOcclusionDirty=false; weather.precipitationOcclusionArea=nullptr;
    require(drawFrames(3)==0,"wrong-area shelter map fails closed");
    weather.precipitationOcclusionArea=weather.weatherArea;
    g_shaderRainWindX=3; g_shaderRainWindY=-2;
    require(drawFrames(30)>0,"wind-tilted rain emits without GL errors");
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,weather.precipitationOcclusionTexture);
    std::fill(depth.begin(),depth.end(),1.0f);
    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,128,128,GL_DEPTH_COMPONENT,GL_FLOAT,depth.data());
    g_shaderRainWindX=0; g_shaderRainWindY=0;
    require(drawFrames(3)>0,"rain continues through open air with no static ground");
    g_shaderRainSplashes=0;
    const double voidStartTime=fakeTime;
    const unsigned voidStreakPixels=drawFrames(3);
    fakeTime=voidStartTime; g_shaderRainSplashes=2;
    require(drawFrames(3)==voidStreakPixels,"open-air rain creates no false floating splashes");
    g_shaderRainSplashes=1;
    g_shaderRainBudget=kNwnShaderRainMaxBudget;
    require(drawFrames(1)>0,"extreme maximum budget draws safely in OpenGL 3.3");
    GLint rainGridSide=0;
    glGetUniformiv(weather_particles::program,
        glGetUniformLocation(weather_particles::program,"gridSide"),&rainGridSide);
    require(rainGridSide==512,"maximum budget really submits 262144 particle cells");
    g_shaderRainBudget=kNwnShaderRainMaxBudget+8192;
    require(drawFrames(1)>0,"oversized saved budget is bounded safely");
    glGetUniformiv(weather_particles::program,
        glGetUniformLocation(weather_particles::program,"gridSide"),&rainGridSide);
    require(rainGridSide==512,"GPU draw clamps oversized saved budgets to shared maximum");
    g_shaderRainBudget=256;
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,weather.precipitationOcclusionTexture);
    std::fill(depth.begin(),depth.end(),0.4375f); // Roof at z=8, not the floor beneath.
    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,128,128,GL_DEPTH_COMPONENT,GL_FLOAT,depth.data());
    require(drawFrames(30)>0,"roof receives precipitation as highest static surface");
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,weather.precipitationOcclusionTexture);
    GLint mode=0; glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_COMPARE_MODE,&mode);
    require(mode==GL_COMPARE_REF_TO_TEXTURE,"native shelter texture compare mode unchanged");
    printf("PASS: pixels, depth, alpha, GL state, weather/area gates, and wind\n");
    eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    eglDestroySurface(display,surface); eglDestroyContext(display,context); eglTerminate(display);
}
