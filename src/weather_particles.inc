// Opt-in private precipitation with reversible exact-resource native-rain hiding.
// Shared GL 3.3 path: procedural quads, no geometry/compute extension.
namespace weather_particles {
static const char* status = "off";
static GLuint program = 0, vao = 0, depthSampler = 0, lightSampler = 0;
static bool failed = false;
static unsigned lastSerial = ~0u;
static const void* clockArea = nullptr;
static double clockStart = 0.0;
static GLuint boundNativeRainTexture = 0;
static unsigned hiddenSerial = ~0u;
// This exact resource is used by stock fx_rain and the supplied override.
// Do not classify a whole bucket or all particle shaders as weather.
static bool native_rain_name(const char* name) {
    if (!name) return false;
    const char* expected = "fxpa_rain01";
    for (; *expected; ++expected, ++name) {
        char c = *name;
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c != *expected) return false;
    }
    return *name == '\0';
}
static void note_material_texture(const char* name) {
    boundNativeRainTexture = 0;
    if (!native_rain_name(name) || !gl::GetIntegerv || !gl::ActiveTexture) return;
    GLint active = 0, texture = 0;
    gl::GetIntegerv(GL_ACTIVE_TEXTURE, &active);
    gl::ActiveTexture(GL_TEXTURE0);
    gl::GetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
    gl::ActiveTexture((GLenum)active);
    boundNativeRainTexture = (GLuint)texture;
}
static bool suppress_native_draw(bool nativeAreaDraw) {
    const auto& w = weather_runtime::g;
    const auto& camera = g_shadowFrameContext;
    if (!nativeAreaDraw || !script_env::b(script_env::RainOn,g_shaderRainEnabled) ||
        !g_shaderRainHideNative ||
        !boundNativeRainTexture || failed || !program || !camera.valid ||
        camera.scene != g_areaScene || w.observedScene != g_areaScene ||
        clockArea != w.weatherArea ||
        (lastSerial != camera.serial && lastSerial + 1u != camera.serial) ||
        !w.weatherObserved || !w.areaFlagsKnown || w.areaInterior || !w.raining ||
        w.weatherType != weather_runtime::kWeatherRain || w.weatherDensity <= 0.001f ||
        !w.precipitationOcclusionValid || w.precipitationOcclusionDirty ||
        w.precipitationOcclusionArea != w.weatherArea ||
        w.precipitationOcclusionScene != g_areaScene ||
        !w.precipitationOcclusionTexture || !gl::GetIntegerv ||
        !gl::GetBooleanv || !gl::IsEnabled || !gl::ActiveTexture) return false;
    GLint viewport[4] = {};
    gl::GetIntegerv(GL_VIEWPORT, viewport);
    if (std::memcmp(viewport, camera.viewport, sizeof(viewport)) != 0 ||
        !gl::IsEnabled(GL_DEPTH_TEST) || !gl::IsEnabled(GL_BLEND)) return false;
    GLboolean depthWrite = GL_TRUE;
    gl::GetBooleanv(GL_DEPTH_WRITEMASK, &depthWrite);
    if (depthWrite) return false;
    GLint active = 0, texture = 0;
    gl::GetIntegerv(GL_ACTIVE_TEXTURE, &active);
    gl::ActiveTexture(GL_TEXTURE0);
    gl::GetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
    gl::ActiveTexture((GLenum)active);
    if ((GLuint)texture != boundNativeRainTexture) return false;
    hiddenSerial = camera.serial;
    status = "drawing (native rain hidden)";
    return true;
}
using GenObjects = void (*)(GLsizei, GLuint*);
using BindVao = void (*)(GLuint);
using BindSampler = void (*)(GLuint, GLuint);
using SamplerParam = void (*)(GLuint, GLenum, GLint);
using DrawArrays = void (*)(GLenum, GLint, GLsizei);
static GenObjects genVaos = nullptr, genSamplers = nullptr;
static BindVao bindVao = nullptr;
static BindSampler bindSampler = nullptr;
static SamplerParam samplerParam = nullptr;
static DrawArrays drawArrays = nullptr;
static UseProgram_t useProgram = nullptr;
using IndexedEnable = void (*)(GLenum,GLuint);
using IndexedIsEnabled = GLboolean (*)(GLenum,GLuint);
using IndexedGetBoolean = void (*)(GLenum,GLuint,GLboolean*);
using IndexedColorMask = void (*)(GLuint,GLboolean,GLboolean,GLboolean,GLboolean);
using IndexedBlend = void (*)(GLuint,GLenum,GLenum,GLenum,GLenum);
using IndexedEquation = void (*)(GLuint,GLenum,GLenum);
static IndexedEnable enableIndexed=nullptr, disableIndexed=nullptr;
static IndexedIsEnabled isEnabledIndexed=nullptr;
static IndexedGetBoolean getBooleanIndexed=nullptr;
static IndexedColorMask colorMaskIndexed=nullptr;
static IndexedBlend blendIndexed=nullptr;
static IndexedEquation equationIndexed=nullptr;
using DepthRangeFn = void (*)(double,double);
static DepthRangeFn depthRangeSet=nullptr;
using GetFloatFn = void (*)(GLenum,GLfloat*);
static GetFloatFn depthRangeGet=nullptr;

static const char* kRainVertexShader =
    "#version 330 core\n"
    "uniform mat4 cameraVP, cameraInv, roofVP, roofInv;\n"
    "uniform sampler2D roofDepth, lightData;\n"
    "uniform vec4 eyeRadius, windTimeDensity, areaLight;\n"
    "uniform vec4 lightFalloff; uniform int gridSide, splashPass, lightCount;\n"
    "uniform float cellSize, splashStrength, heightStep;\n"
    "out vec2 uv; out vec3 tint; out float opacity; flat out int kind;\n"
    // x = splash life 0..1, y = per-drop seed. Constant across a quad.
    "flat out vec2 fx;\n"
    // Eye-space distance of this vertex, for the explicit occlusion test.
    "out float viewDepth;\n"
    "float hash(vec2 p) { vec3 q=fract(vec3(p.xyx)*0.1031);"
    " q+=dot(q,q.yzx+33.33); return fract((q.x+q.y)*q.z); }\n"
    "bool heightAt(vec2 xy,out float z) {\n"
    " vec4 p=roofVP*vec4(xy,0,1); vec2 t=p.xy/p.w*0.5+0.5;\n"
    " if(any(lessThanEqual(t,vec2(0.001))) || any(greaterThanEqual(t,vec2(0.999)))) return false;\n"
    " float d=textureLod(roofDepth,t,0).r; if(d>=0.999999) return false;\n"
    " vec4 h=roofInv*vec4(t*2-1,d*2-1,1); z=h.z/h.w; return true;\n"
    "}\n"
    "void main() {\n"
    " const vec2 corners[6]=vec2[6](vec2(-1,-1),vec2(1,-1),vec2(1,1),"
    " vec2(-1,-1),vec2(1,1),vec2(-1,1));\n"
    " int id=gl_VertexID/6; uv=corners[gl_VertexID%6]; kind=splashPass;\n"
    " opacity=0; tint=vec3(0); fx=vec2(0); viewDepth=0; gl_Position=vec4(2,2,2,1);\n"
    " vec2 cell=floor(eyeRadius.xy/cellSize)+vec2(id%gridSide,id/gridSide)-float(gridSide/2);\n"
    " float seed=hash(cell); if(hash(cell+17.1)>windTimeDensity.w) return;\n"
    " vec2 target=(cell+vec2(hash(cell+3.7),hash(cell+9.2)))*cellSize;\n"
    // An empty map texel means open air, not an absence of weather. Placeable
    // arenas/sky bridges lack static tile ground. Do not invent collision there.
    " float ground; if(!heightAt(target,ground)) ground=eyeRadius.z-12.0;\n"
    " vec3 velocity=vec3(windTimeDensity.xy,-16.0);\n"
    " vec3 start=vec3(target-windTimeDensity.xy*1.5,ground+24.0);\n"
    " float top; if(heightAt(start.xy,top) && start.z<=top+0.04) return;\n"
    " float hitTime=1.5, previous=0; bool hit=false;\n"
    // Bounded height-field sweep, then binary refinement of the first crossing.
    // This is static overhead collision, NOT a 3D/SDF side-wall simulation.
    " for(int i=1;i<=12;++i) {\n"
    "  float t=float(i)*0.125; vec3 p=start+velocity*t; float h;\n"
    "  if(!heightAt(p.xy,h)) { previous=t; continue; }\n"
    "  if(p.z<=h+0.005) { float lo=previous,hi=t;\n"
    "   for(int j=0;j<5;++j) { float mid=(lo+hi)*0.5; vec3 m=start+velocity*mid; float mh;\n"
    "    if(!heightAt(m.xy,mh) || m.z>mh) lo=mid; else hi=mid; }\n"
    "   hitTime=hi; hit=true; break; } previous=t;\n"
    " }\n"
    " vec3 impact=start+velocity*hitTime; float iz=0;\n"
    " if(hit) { if(!heightAt(impact.xy,iz)) return; impact.z=iz+0.025; }\n"
    " float age=mod(windTimeDensity.z+seed*1.78,1.78);\n"
    " vec3 center; float width,halfLength; vec3 axis;\n"
    " if(splashPass==0) {\n"
    "  if(age>=hitTime) return; center=start+velocity*age;\n"
    "  axis=normalize(velocity); width=0.013; halfLength=0.24;\n"
    "  opacity=0.30*smoothstep(0.0,0.06,age)*smoothstep(0.0,0.05,hitTime-age);\n"
    " } else {\n"
    "  if(!hit) return;\n"
    "  float life=age-hitTime; if(life<0 || life>0.28 || splashStrength<=0) return;\n"
    "  float hx,hy; if(!heightAt(impact.xy+vec2(heightStep,0),hx) ||"
    " !heightAt(impact.xy+vec2(0,heightStep),hy)) return;\n"
    "  if(abs(hx-iz)>heightStep*2 || abs(hy-iz)>heightStep*2) return;\n"
    "  vec3 normal=normalize(vec3((iz-hx)/heightStep,(iz-hy)/heightStep,1));\n"
    // One impact is drawn as two quads that share this simulated hit:
    //   pass 1 -- a quad lying IN the surface: contact flash and the
    //             expanding capillary ring, the part a top-down camera sees;
    //   pass 2 -- an upright quad standing on the surface: the crown sheet,
    //             the rebound jet and the ballistic secondary droplets.
    // Both are animated per fragment from fx, so the quads themselves never
    // move or scale -- only what is drawn inside them does. Sizes are larger
    // than a real 1-2 cm splash on purpose: at NWN's camera distance a pixel
    // covers about that much, and a true-scale splash would not rasterise.
    "  float energy=0.75+0.5*hash(cell+5.3);\n"
    "  fx=vec2(life/0.28,seed); axis=normal; center=impact;\n"
    "  if(splashPass==1) { width=0.16*energy; halfLength=width;\n"
    "   opacity=splashStrength*0.45; }\n"
    "  else { width=0.144*energy; halfLength=width;\n"
    "   center=impact+normal*halfLength; opacity=splashStrength*0.55; }\n"
    " }\n"
    " float distanceToEye=length(center.xy-eyeRadius.xy);\n"
    " opacity*=1-smoothstep(eyeRadius.w*0.75,eyeRadius.w,distanceToEye);\n"
    " vec3 toEye=eyeRadius.xyz-center;\n"
    " vec3 right=cross(axis,toEye);\n"
    " if(length(right)<0.0001) right=cameraInv[0].xyz; right=normalize(right);\n"
    // Streaks and the upright splash quad extend along their axis (velocity /
    // surface normal). The ring quad instead spans the surface plane, and is
    // nudged toward the eye so it does not z-fight the ground it lies on.
    " vec3 up=axis;\n"
    " if(splashPass==1) up=normalize(cross(right,axis));\n"
    " vec3 pos=center+right*uv.x*width+up*uv.y*halfLength;\n"
    " if(splashPass==1) pos+=normalize(toEye)*0.03;\n"
    " vec3 illumination=max(areaLight.rgb,vec3(0.035));\n"
    " for(int i=0;i<128;++i) { if(i>=lightCount) break;\n"
    "  vec4 lp=texelFetch(lightData,ivec2(i*2,0),0);\n"
    "  vec3 lc=texelFetch(lightData,ivec2(i*2+1,0),0).rgb;\n"
    "  float d2=dot(center-lp.xyz,center-lp.xyz); float fall=max(0.0,1-d2/max(lp.w,0.001));\n"
    "  illumination+=lc*fall*fall; }\n"
    " tint=min(illumination,vec3(2))*vec3(0.68,0.76,0.82);\n"
    " if(lightFalloff.w>0.5) opacity*=1-smoothstep(lightFalloff.x,lightFalloff.y,length(center-eyeRadius.xyz));\n"
    " gl_Position=cameraVP*vec4(pos,1); viewDepth=gl_Position.w;\n"
    "}\n";
static const char* kRainFragmentShader =
    "#version 330 core\n"
    "in vec2 uv; in vec3 tint; in float opacity; flat in int kind;\n"
    "flat in vec2 fx; in float viewDepth;\n"
    "uniform sampler2D sceneDepth; uniform vec4 sceneDepthInfo;\n"
    "uniform int sceneDepthOn;\n"
    "out vec4 nwnFragColor;\n"
    "float hash1(float n) { return fract(sin(n*91.3458)*47453.5453); }\n"
    "void main() {\n"
    // Explicit occlusion against the injector's own copy of the completed
    // scene depth. The fixed-function depth test stays on as well, but it
    // depends on state this pass inherits from the engine (depth range, the
    // bound framebuffer's depth attachment, multisample resolve). This test
    // depends on none of it: sceneDepthInfo.xy is the viewport origin and .zw
    // are the two projection terms that turn window depth back into distance.
    " if(sceneDepthOn!=0) {\n"
    "  float d=texelFetch(sceneDepth,ivec2(gl_FragCoord.xy-sceneDepthInfo.xy),0).r;\n"
    "  if(d<0.9999995) { float visible=sceneDepthInfo.w/(d*2.0-1.0+sceneDepthInfo.z);\n"
    "   if(viewDepth>visible*1.01+0.08) discard; }\n"
    " }\n"
    " float shape=0.0; float t=fx.x;\n"
    " if(kind==0) shape=exp(-uv.x*uv.x*5.0)*(1-smoothstep(0.55,1.0,abs(uv.y)));\n"
    " else if(kind==1) {\n"
    // Surface view. The wave front decelerates (~sqrt(t)) and thins as it
    // spreads; a weaker second ripple follows it; the first instants carry a
    // small bright contact flash where the drop flattens.
    "  float r=length(uv); float front=0.12+0.80*sqrt(t);\n"
    "  float thick=mix(0.16,0.08,t);\n"
    "  float ring=1.0-smoothstep(0.0,thick,abs(r-front));\n"
    "  float inner=(1.0-smoothstep(0.0,thick*0.8,abs(r-front*0.55)))*"
    "smoothstep(0.25,0.5,t)*0.45;\n"
    "  float flash=(1.0-smoothstep(0.0,0.26,r))*(1.0-smoothstep(0.0,0.18,t))*1.4;\n"
    "  shape=(ring+inner)*pow(1.0-t,1.5)+flash;\n"
    " } else {\n"
    // Side view: x in -1..1 across the impact, y in 0..2 above the surface.
    "  vec2 p=vec2(uv.x,uv.y+1.0);\n"
    // Crown: a bowl-shaped sheet thrown outward, lowest at the centre and
    // tallest at its beaded rim. It rises and collapses in the first ~55%.
    "  float ct=clamp(t/0.55,0.0,1.0); float rc=0.20+0.55*sqrt(ct);\n"
    "  float hc=0.60*sin(3.14159*ct); float ax=abs(p.x)/rc;\n"
    "  float beads=0.75+0.25*cos(p.x*(22.0+fx.y*12.0));\n"
    "  float crown=(1.0-smoothstep(0.04,0.13,abs(p.y-hc*ax*ax*ax)))*"
    "(1.0-smoothstep(0.90,1.05,ax))*beads*(1.0-ct);\n"
    // Rebound (Worthington) jet: after the crater closes a column rises from
    // the centre, necks, and sheds a bead from its tip. Its height varies
    // per drop, so not every impact throws an obvious one.
    "  float jt=clamp((t-0.30)/0.70,0.0,1.0); float on=step(0.0001,jt);\n"
    "  float hj=(0.35+0.75*fx.y)*sin(3.14159*jt);\n"
    "  float column=exp(-p.x*p.x/(0.010+0.020*(1.0-jt)))*"
    "(1.0-smoothstep(hj-0.08,hj,p.y));\n"
    "  float bead=1.0-smoothstep(0.07,0.13,length(p-vec2(0.0,hj+0.10)));\n"
    "  float jet=(column*0.8+bead)*on*(1.0-jt*jt);\n"
    // Secondary droplets: torn from the crown rim and thrown on ballistic
    // arcs with their own speed, direction and flight time, shrinking as
    // they go and gone when they land.
    "  float drops=0.0;\n"
    "  for(int i=0;i<5;++i) { float k=float(i)+fx.y*37.0;\n"
    "   float dt=t/(0.65+0.35*hash1(k+1.3)); if(dt>=1.0) continue;\n"
    "   float vx=(hash1(k+2.7)*2.0-1.0)*0.85; float peak=0.6+0.9*hash1(k+4.1);\n"
    "   vec2 c=vec2(vx*dt,4.0*peak*dt*(1.0-dt)+0.06);\n"
    "   float rad=0.13*(1.0-0.45*dt);\n"
    "   drops+=(1.0-smoothstep(rad*0.55,rad,length(p-c)))*(1.0-dt*dt); }\n"
    "  shape=crown*0.9+jet+min(drops,1.0);\n"
    " }\n"
    " float a=clamp(opacity*shape,0,0.8); if(a<0.002) discard;\n"
    " nwnFragColor=vec4(tint*a,a);\n"
    "}\n";

static float bounded(float value, float lo, float hi, float fallback) {
    return std::isfinite(value) ? std::max(lo,std::min(value,hi)) : fallback;
}
static void load_environment() {
    if (const char* s=shadow_getenv("NWN_SHADER_RAIN")) g_shaderRainEnabled=atoi(s)!=0;
    if (const char* s=shadow_getenv("NWN_SHADER_RAIN_HIDE_NATIVE")) g_shaderRainHideNative=atoi(s)!=0;
    if (const char* s=shadow_getenv("NWN_SHADER_RAIN_BUDGET"))
        g_shaderRainBudget=std::max(kNwnShaderRainMinBudget,std::min(atoi(s),kNwnShaderRainMaxBudget));
    auto envf=[](const char* key,float& v,float lo,float hi) {
        if(const char* s=shadow_getenv(key)) v=bounded((float)atof(s),lo,hi,v);
    };
    envf("NWN_SHADER_RAIN_RADIUS",g_shaderRainRadius,8,64);
    envf("NWN_SHADER_RAIN_WIND_X",g_shaderRainWindX,-8,8);
    envf("NWN_SHADER_RAIN_WIND_Y",g_shaderRainWindY,-8,8);
    envf("NWN_SHADER_RAIN_SPLASHES",g_shaderRainSplashes,0,2);
}
static bool ensure_program() {
    if (program && vao && depthSampler && lightSampler) return true;
    if (failed) return false;
    auto create=(CreateShader_t)glew_or_dlsym(eng::GlewCreateShader,"glCreateShader");
    auto compile=(CompileShader_t)glew_or_dlsym(eng::GlewCompileShader,"glCompileShader");
    auto shaderStatus=(GetShaderiv_t)glew_or_dlsym(eng::GlewGetShaderiv,"glGetShaderiv");
    auto createProgram=(CreateProgram_t)glew_or_dlsym(eng::GlewCreateProgram,"glCreateProgram");
    auto attach=(AttachShader_t)glew_or_dlsym(eng::GlewAttachShader,"glAttachShader");
    auto link=(LinkProgram_t)glew_or_dlsym(eng::GlewLinkProgram,"glLinkProgram");
    auto programStatus=(GetProgramiv_t)glew_or_dlsym(eng::GlewGetProgramiv,"glGetProgramiv");
    auto source=g_realShaderSource ? g_realShaderSource :
        (ShaderSource_t)dlsym(RTLD_NEXT,"glShaderSource");
    using DeleteObject=void (*)(GLuint);
    using InfoLog=void (*)(GLuint,GLsizei,GLsizei*,char*);
    auto deleteShader=(DeleteObject)dlsym(RTLD_NEXT,"glDeleteShader");
    auto deleteProgram=(DeleteObject)dlsym(RTLD_NEXT,"glDeleteProgram");
    auto shaderLog=(InfoLog)dlsym(RTLD_NEXT,"glGetShaderInfoLog");
    auto programLog=(InfoLog)dlsym(RTLD_NEXT,"glGetProgramInfoLog");
    genVaos=(GenObjects)dlsym(RTLD_NEXT,"glGenVertexArrays");
    genSamplers=(GenObjects)dlsym(RTLD_NEXT,"glGenSamplers");
    bindVao=(BindVao)dlsym(RTLD_NEXT,"glBindVertexArray");
    bindSampler=(BindSampler)dlsym(RTLD_NEXT,"glBindSampler");
    samplerParam=(SamplerParam)dlsym(RTLD_NEXT,"glSamplerParameteri");
    drawArrays=(DrawArrays)dlsym(RTLD_NEXT,"glDrawArrays");
    // Never enter the engine's patched GLEW bind: it may rebind shadow units
    // and register private uniforms as native material/camera observations.
    useProgram=g_realUseProgram ? g_realUseProgram :
        (UseProgram_t)dlsym(RTLD_NEXT,"glUseProgram");
    enableIndexed=(IndexedEnable)dlsym(RTLD_NEXT,"glEnablei");
    disableIndexed=(IndexedEnable)dlsym(RTLD_NEXT,"glDisablei");
    isEnabledIndexed=(IndexedIsEnabled)dlsym(RTLD_NEXT,"glIsEnabledi");
    getBooleanIndexed=(IndexedGetBoolean)dlsym(RTLD_NEXT,"glGetBooleani_v");
    colorMaskIndexed=(IndexedColorMask)dlsym(RTLD_NEXT,"glColorMaski");
    // Optional GL4/ARB_draw_buffers_blend: touch target zero only when available.
    blendIndexed=(IndexedBlend)dlsym(RTLD_NEXT,"glBlendFuncSeparatei");
    equationIndexed=(IndexedEquation)dlsym(RTLD_NEXT,"glBlendEquationSeparatei");
    // Optional: without it the pass simply inherits the engine's depth range.
    depthRangeSet=(DepthRangeFn)dlsym(RTLD_NEXT,"glDepthRange");
    depthRangeGet=(GetFloatFn)dlsym(RTLD_NEXT,"glGetFloatv");
    if(!depthRangeGet) depthRangeSet=nullptr;   // never set what cannot be restored
    GLint major=0; gl::GetIntegerv(0x821B /* GL_MAJOR_VERSION */,&major);
    bool indexedBlending=major>=4;
    if(!indexedBlending) {
        using GetStringIndexed=const unsigned char* (*)(GLenum,GLuint);
        auto getString=(GetStringIndexed)dlsym(RTLD_NEXT,"glGetStringi");
        GLint count=0; gl::GetIntegerv(0x821D /* GL_NUM_EXTENSIONS */,&count);
        for(GLint i=0;getString && i<count;++i) {
            const auto* ext=getString(0x1F03 /* GL_EXTENSIONS */,(GLuint)i);
            if(ext && !std::strcmp((const char*)ext,"GL_ARB_draw_buffers_blend"))
                indexedBlending=true;
        }
    }
    if(!indexedBlending) { blendIndexed=nullptr; equationIndexed=nullptr; }
    if (!create || !compile || !shaderStatus || !createProgram || !attach || !link ||
        !programStatus || !source || !deleteShader || !deleteProgram ||
        !genVaos || !genSamplers || !bindVao || !bindSampler || !samplerParam ||
        !drawArrays || !useProgram || !enableIndexed || !disableIndexed ||
        !isEnabledIndexed || !getBooleanIndexed || !colorMaskIndexed) {
        failed=true;
        fprintf(stderr,"[shadowmap][rain-particles] GL 3.3 entry points unavailable; native rain retained\n");
        return false;
    }
    GLuint shaders[2]={create(GL_VERTEX_SHADER),create(GL_FRAGMENT_SHADER)};
    const char* sources[2]={kRainVertexShader,kRainFragmentShader};
    bool okay=true;
    for(int i=0;i<2;++i) {
        source(shaders[i],1,&sources[i],nullptr); compile(shaders[i]);
        GLint compiled=0; shaderStatus(shaders[i],GL_COMPILE_STATUS,&compiled);
        if (!compiled) {
            char log[2048]={}; if(shaderLog) shaderLog(shaders[i],sizeof(log)-1,nullptr,log);
            fprintf(stderr,"[shadowmap][rain-particles] shader %d failed: %s\n",i,log);
            okay=false;
        }
    }
    if(okay) {
        program=createProgram(); attach(program,shaders[0]); attach(program,shaders[1]); link(program);
        GLint linked=0; programStatus(program,GL_LINK_STATUS,&linked);
        if(!linked) {
            char log[2048]={}; if(programLog) programLog(program,sizeof(log)-1,nullptr,log);
            fprintf(stderr,"[shadowmap][rain-particles] link failed: %s\n",log);
            deleteProgram(program); program=0; okay=false;
        }
    }
    for(GLuint s:shaders) deleteShader(s);
    if(okay) {
        genVaos(1,&vao); genSamplers(1,&depthSampler); genSamplers(1,&lightSampler);
        okay=vao && depthSampler && lightSampler;
        if(okay) for(GLuint s:{depthSampler,lightSampler}) {
            samplerParam(s,GL_TEXTURE_COMPARE_MODE,GL_NONE);
            samplerParam(s,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
            samplerParam(s,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            samplerParam(s,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
            samplerParam(s,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        }
    }
    failed=!okay;
    fprintf(stderr,"[shadowmap][rain-particles] private GL3.3 preview %s; native rain retained\n",okay?"ready":"failed");
    return okay;
}

// Only the states this pass changes. No FBO, viewport, texture object parameters,
// buffers, stencil contents or engine matrix stacks are modified.
struct StateGuard {
    GLuint trackedProgram=g_curProgram;
    GLint active=0, oldProgram=0, oldVao=0, depthFunc=0;
    GLint blend[6]={}, textures[3]={}, samplers[3]={};
    GLfloat depthRange[2]={0,1};
    GLboolean depthMask=0, colorMask[4]={}, blendEnabled=0;
    static constexpr GLenum caps[18]={
        GL_DEPTH_TEST,GL_CULL_FACE,GL_SCISSOR_TEST,
        GL_POLYGON_OFFSET_FILL,0x809E,0x80A0,0x0B90,0x8C89,
        0x8E51,0x0BF2, // sample mask, color logic op
        0x3000,0x3001,0x3002,0x3003,0x3004,0x3005,0x3006,0x3007
    };
    GLboolean enabled[18]={};
    StateGuard() {
        gl::GetIntegerv(GL_ACTIVE_TEXTURE,&active);
        gl::GetIntegerv(GL_CURRENT_PROGRAM,&oldProgram);
        gl::GetIntegerv(0x85B5,&oldVao);
        gl::GetIntegerv(GL_DEPTH_FUNC,&depthFunc);
        gl::GetBooleanv(GL_DEPTH_WRITEMASK,&depthMask);
        getBooleanIndexed(GL_COLOR_WRITEMASK,0,colorMask);
        blendEnabled=isEnabledIndexed(GL_BLEND,0);
        const GLenum queries[6]={GL_BLEND_SRC_RGB,GL_BLEND_DST_RGB,
            GL_BLEND_SRC_ALPHA,GL_BLEND_DST_ALPHA,GL_BLEND_EQUATION_RGB,GL_BLEND_EQUATION_ALPHA};
        for(int i=0;i<6;++i) gl::GetIntegerv(queries[i],&blend[i]);
        for(int i=0;i<18;++i) enabled[i]=gl::IsEnabled(caps[i]);
        for(int i=0;i<3;++i) {
            gl::ActiveTexture(GL_TEXTURE0+i);
            gl::GetIntegerv(GL_TEXTURE_BINDING_2D,&textures[i]);
            gl::GetIntegerv(0x8919,&samplers[i]);
        }
        gl::ActiveTexture((GLenum)active);
        if(depthRangeSet && depthRangeGet) depthRangeGet(0x0B70 /* GL_DEPTH_RANGE */,depthRange);
    }
    ~StateGuard() {
        if(depthRangeSet && depthRangeGet) depthRangeSet(depthRange[0],depthRange[1]);
        for(int i=0;i<3;++i) {
            gl::ActiveTexture(GL_TEXTURE0+i);
            gl::BindTexture(GL_TEXTURE_2D,(GLuint)textures[i]);
            bindSampler(i,(GLuint)samplers[i]);
        }
        gl::ActiveTexture((GLenum)active);
        useProgram((GLuint)oldProgram); g_curProgram=trackedProgram;
        bindVao((GLuint)oldVao);
        gl::DepthFunc((GLenum)depthFunc); gl::DepthMask(depthMask);
        colorMaskIndexed(0,colorMask[0],colorMask[1],colorMask[2],colorMask[3]);
        if(blendIndexed && equationIndexed) {
            blendIndexed(0,blend[0],blend[1],blend[2],blend[3]);
            equationIndexed(0,blend[4],blend[5]);
        } else {
            gl::BlendFuncSeparate(blend[0],blend[1],blend[2],blend[3]);
            gl::BlendEquationSeparate(blend[4],blend[5]);
        }
        if(blendEnabled) enableIndexed(GL_BLEND,0); else disableIndexed(GL_BLEND,0);
        for(int i=0;i<18;++i) {
            if(enabled[i]) gl::Enable(caps[i]); else gl::Disable(caps[i]);
        }
    }
};
static void draw(void* scene) {
    auto& w=weather_runtime::g;
    const auto& camera=g_shadowFrameContext;
    if(!script_env::b(script_env::RainOn,g_shaderRainEnabled)) { status="off"; return; }
    if(!camera.valid || scene!=g_areaScene || camera.scene!=scene ||
       w.observedScene!=scene) { status="waiting for selected area"; return; }
    if(!w.weatherObserved || !w.areaFlagsKnown || w.areaInterior ||
       !w.raining || w.weatherType!=weather_runtime::kWeatherRain) {
        clockArea=nullptr; status="no exterior rain"; return;
    }
    if(!w.precipitationOcclusionValid || w.precipitationOcclusionDirty ||
       w.precipitationOcclusionScene!=scene ||
       w.precipitationOcclusionArea!=w.weatherArea ||
       !w.precipitationOcclusionTexture || !w.precipitationOcclusionSize) {
        status="waiting for shelter depth"; return;
    }
    if(lastSerial==camera.serial) return;
    float inverse[16],vp[16];
    for(float v:w.precipitationOcclusionVP) {
        if(!std::isfinite(v)) { status="invalid shelter matrix"; return; }
    }
    if(!mat4_inverse(w.precipitationOcclusionVP,inverse)) {
        status="invalid shelter matrix"; return;
    }
    if(!gl::GetIntegerv || !gl::GetBooleanv || !gl::IsEnabled ||
       !gl::ActiveTexture || !gl::BindTexture || !gl::UniformMatrix4fv ||
       !gl::Uniform4f || !gl::Uniform1f || !gl::Uniform1i ||
       !gl::GetUniformLocation || !gl::Enable || !gl::Disable ||
       !gl::BlendFuncSeparate || !gl::BlendEquationSeparate ||
       !gl::DepthFunc || !gl::DepthMask || !gl::ColorMask) {
        status="missing GL state API"; return;
    }
    GLint vertexUnits=0; gl::GetIntegerv(0x8B4C,&vertexUnits);
    if(vertexUnits<2 || !ensure_program()) { status="unavailable; native retained"; return; }
    // Allocation above never binds anything, so its failure cannot leak state.
    nwn_core::OwnedPass owned;
    StateGuard restore;
    GLint viewport[4]={}; gl::GetIntegerv(GL_VIEWPORT,viewport);
    if(std::memcmp(viewport,camera.viewport,sizeof(viewport))!=0) {
        status="viewport mismatch; native retained"; return;
    }
    lastSerial=camera.serial;
    if(clockArea!=w.weatherArea) { clockArea=w.weatherArea; clockStart=now_seconds(); }
    const float radius=bounded(script_env::f(script_env::RainRadius,g_shaderRainRadius),8,64,40);
    const float splashes=bounded(script_env::f(script_env::RainSplashes,g_shaderRainSplashes),0,2,0.250f);
    const int budget=std::max(kNwnShaderRainMinBudget,std::min(g_shaderRainBudget,kNwnShaderRainMaxBudget));
    // Whole square grids avoid an uneven truncated row. Requested budget is a cap.
    const int side=(int)std::floor(std::sqrt((float)budget));
    const float spacing=2*radius/(side-2);
    mat4_mul(camera.projection,camera.view,vp);
    useProgram(program); g_curProgram=program; bindVao(vao);
    gl::ActiveTexture(GL_TEXTURE0); gl::BindTexture(GL_TEXTURE_2D,w.precipitationOcclusionTexture);
    bindSampler(0,depthSampler);
    gl::ActiveTexture(GL_TEXTURE0+1);
    const bool lightsReady=w.snowLightDataFrame==w.frameSerial && w.snowLightDataTexture;
    gl::BindTexture(GL_TEXTURE_2D,lightsReady?w.snowLightDataTexture:0); bindSampler(1,lightSampler);
    auto loc=[](const char* name) { return gl::GetUniformLocation(program,name); };
    gl::Uniform1i(loc("roofDepth"),0); gl::Uniform1i(loc("lightData"),1);
    // Completed scene depth, when the receiver copied it for THIS frame at
    // this viewport size and the camera is an ordinary perspective one.
    const bool sceneDepthReady=g_sceneDepthTex && g_sceneDepthSerial==camera.serial &&
        g_sceneDepthW==viewport[2] && g_sceneDepthH==viewport[3] &&
        camera.projection[11]==-1.0f && camera.projection[15]==0.0f &&
        std::isfinite(camera.projection[10]) && std::isfinite(camera.projection[14]) &&
        camera.projection[14]<0.0f;
    gl::ActiveTexture(GL_TEXTURE0+2);
    gl::BindTexture(GL_TEXTURE_2D,sceneDepthReady?g_sceneDepthTex:0); bindSampler(2,depthSampler);
    gl::Uniform1i(loc("sceneDepth"),2); gl::Uniform1i(loc("sceneDepthOn"),sceneDepthReady?1:0);
    gl::Uniform4f(loc("sceneDepthInfo"),(float)viewport[0],(float)viewport[1],
        camera.projection[10],camera.projection[14]);
    gl::Uniform1i(loc("lightCount"),lightsReady?w.snowLightDataCount:0);
    gl::Uniform1i(loc("gridSide"),side); gl::Uniform1f(loc("cellSize"),spacing);
    gl::Uniform1f(loc("heightStep"),std::max(0.15f,2*w.precipitationOcclusionExtent/w.precipitationOcclusionSize));
    gl::Uniform1f(loc("splashStrength"),splashes);
    gl::UniformMatrix4fv(loc("cameraVP"),1,GL_FALSE,vp);
    gl::UniformMatrix4fv(loc("cameraInv"),1,GL_FALSE,camera.viewInverse);
    gl::UniformMatrix4fv(loc("roofVP"),1,GL_FALSE,w.precipitationOcclusionVP);
    gl::UniformMatrix4fv(loc("roofInv"),1,GL_FALSE,inverse);
    gl::Uniform4f(loc("eyeRadius"),camera.eye.x,camera.eye.y,camera.eye.z,radius);
    gl::Uniform4f(loc("windTimeDensity"),
        bounded(script_env::f(script_env::RainWindX,g_shaderRainWindX),-8,8,0),
        bounded(script_env::f(script_env::RainWindY,g_shaderRainWindY),-8,8,0),(float)(now_seconds()-clockStart),
        bounded(w.weatherDensity,0,1,0));
    float illumination[3]={0,0,0};
    for(int i=0;i<3;++i) {
        if(eng::SunAmbient) illumination[i]+=eng::SunAmbient[i];
        if(eng::SunDiffuse) illumination[i]+=eng::SunDiffuse[i]*0.45f;
    }
    gl::Uniform4f(loc("areaLight"),illumination[0],illumination[1],illumination[2],1);
    const bool fog=g_haveEngineFog && std::isfinite(g_fogStartEngine) &&
        std::isfinite(g_fogEndEngine) && g_fogEndEngine>g_fogStartEngine;
    gl::Uniform4f(loc("lightFalloff"),fog?g_fogStartEngine:0,fog?g_fogEndEngine:1,0,fog?1:0);
    for(GLenum cap:StateGuard::caps) gl::Disable(cap);
    gl::Enable(GL_DEPTH_TEST); gl::DepthFunc(GL_LEQUAL); gl::DepthMask(GL_FALSE);
    // The engine may leave a narrowed depth range from its last draw. Rain
    // drawn through it lands at the wrong depth and passes the test against
    // every mesh, so the pass states the full range itself.
    if(depthRangeSet) depthRangeSet(0.0,1.0);
    enableIndexed(GL_BLEND,0);
    // Preserve native framebuffer alpha; RGB is premultiplied.
    if(blendIndexed && equationIndexed) {
        equationIndexed(0,GL_FUNC_ADD,GL_FUNC_ADD);
        blendIndexed(0,GL_ONE,GL_ONE_MINUS_SRC_ALPHA,GL_ZERO,GL_ONE);
    } else {
        gl::BlendEquationSeparate(GL_FUNC_ADD,GL_FUNC_ADD);
        gl::BlendFuncSeparate(GL_ONE,GL_ONE_MINUS_SRC_ALPHA,GL_ZERO,GL_ONE);
    }
    colorMaskIndexed(0,GL_TRUE,GL_TRUE,GL_TRUE,GL_FALSE);
    // Pass 0: falling streaks. Pass 1: surface ring. Pass 2: upright splash.
    for(int pass=0;pass<3;++pass) {
        if(pass>=1 && splashes<=0) break;
        gl::Uniform1i(loc("splashPass"),pass);
        drawArrays(0x0004 /* GL_TRIANGLES */,0,side*side*6);
    }
    status=hiddenSerial==camera.serial ? "drawing (native rain hidden)" : "drawing (native retained)";
}
} // namespace weather_particles
static void draw_weather_particles(void* scene) { weather_particles::draw(scene); }
